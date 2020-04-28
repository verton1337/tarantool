/*
 * Copyright 2010-2016, Tarantool AUTHORS, please see AUTHORS file.
 *
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 *
 * 1. Redistributions of source code must retain the above
 *    copyright notice, this list of conditions and the
 *    following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY <COPYRIGHT HOLDER> ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * <COPYRIGHT HOLDER> OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include "recovery.h"

#include "small/rlist.h"
#include "scoped_guard.h"
#include "trigger.h"
#include "fiber.h"
#include "xlog.h"
#include "xrow.h"
#include "xstream.h"
#include "wal.h" /* wal_watcher */
#include "replication.h"
#include "session.h"
#include "coio_file.h"
#include "error.h"

/*
 * Recovery subsystem
 * ------------------
 *
 * A facade of the recovery subsystem is struct recovery.
 *
 * Depending on the actual task being performed the recovery
 * can be in a different state.
 *
 * Let's enumerate all possible distinct states of recovery:
 *
 * IR - initial recovery, initiated right after server start:
 * reading data from a checkpoint and existing WALs
 * and restoring the in-memory state
 * IRR - initial replication relay mode, reading data from
 * existing WALs (xlogs) and sending it to the client.
 *
 * HS - standby mode, entered once all existing WALs are read:
 * following the WAL directory for all changes done by the master
 * and updating the in-memory state
 * RR - replication relay, following the WAL directory for all
 * changes done by the master and sending them to the
 * replica
 *
 * The following state transitions are possible/supported:
 *
 * recovery_init() -> IR | IRR # recover()
 * IR -> HS         # recovery_follow_local()
 * IRR -> RR        # recovery_follow_local()
 */

/* {{{ Initial recovery */

/**
 * Returns NULL in case of error.
 */
struct recovery *
recovery_new(const char *wal_dirname, bool force_recovery,
	     const struct vclock *vclock)
{
	struct recovery *r;

	r = (struct recovery *)calloc(1, sizeof(*r));
	if (r == NULL) {
		diag_set(OutOfMemory, sizeof(*r), "calloc",
			 "struct recovery");
	}

	xdir_create(&r->wal_dir, wal_dirname, XLOG, &INSTANCE_UUID,
		    &xlog_opts_default);
	r->wal_dir.force_recovery = force_recovery;

	vclock_copy(&r->vclock, vclock);

	/**
	 * Avoid scanning WAL dir before we recovered
	 * the snapshot and know instance UUID - this will
	 * make sure the scan skips files with wrong
	 * UUID, see replication/cluster.test for
	 * details.
	 */
	if (xdir_check(&r->wal_dir) != 0) {
		free(r);
		return NULL;
	}

	r->watcher = NULL;
	rlist_create(&r->on_close_log);
	return r;
}

int
recovery_scan(struct recovery *r, struct vclock *end_vclock,
	      struct vclock *gc_vclock)
{
	if (xdir_scan(&r->wal_dir) == -1)
		return -1;

	if (xdir_last_vclock(&r->wal_dir, end_vclock) < 0 ||
	    vclock_compare(end_vclock, &r->vclock) < 0) {
		/* No xlogs after last checkpoint. */
		vclock_copy(gc_vclock, &r->vclock);
		vclock_copy(end_vclock, &r->vclock);
		return 0;
	}

	if (xdir_first_vclock(&r->wal_dir, gc_vclock) < 0)
		unreachable();

	/* Scan the last xlog to find end vclock. */
	struct xlog_cursor cursor;
	if (xdir_open_cursor(&r->wal_dir, vclock_sum(end_vclock), &cursor) != 0) {
		/*
		 * FIXME: Why do we ignore errors?!
		 */
		return 0;
	}

	struct xrow_header row;
	while (xlog_cursor_next(&cursor, &row, true) == 0)
		vclock_follow_xrow(end_vclock, &row);

	xlog_cursor_close(&cursor, false);
	return 0;
}

static int
recovery_close_log(struct recovery *r)
{
	if (xlog_cursor_is_open(&r->cursor)) {
		if (xlog_cursor_is_eof(&r->cursor)) {
			say_info("done `%s'", r->cursor.name);
		} else {
			say_warn("file `%s` wasn't correctly closed",
				 r->cursor.name);
		}
		xlog_cursor_close(&r->cursor, false);

		if (trigger_run(&r->on_close_log, NULL) != 0)
			return -1;
	}
	return 0;
}

static int
recovery_open_log(struct recovery *r, const struct vclock *vclock)
{
	struct xlog_meta meta = r->cursor.meta;
	enum xlog_cursor_state state = r->cursor.state;
	int rc = 0;

	if (recovery_close_log(r) != 0)
		return -1;

	if (xdir_open_cursor(&r->wal_dir, vclock_sum(vclock), &r->cursor) != 0)
		return -1;

	if (state == XLOG_CURSOR_NEW &&
	    vclock_compare(vclock, &r->vclock) > 0) {
		/*
		 * This is the first WAL we are about to scan
		 * and the best clock we could find is greater
		 * or is incomparable with the initial recovery
		 * position.
		 */
		goto gap_error;
	}

	if (state != XLOG_CURSOR_NEW &&
	    vclock_is_set(&r->cursor.meta.prev_vclock) &&
	    vclock_compare(&r->cursor.meta.prev_vclock, &meta.vclock) != 0) {
		/*
		 * WALs are missing between the last scanned WAL
		 * and the next one.
		 */
		goto gap_error;
	}
out:
	/*
	 * We must promote recovery clock even if we don't recover
	 * anything from the next WAL. Otherwise if the last WAL
	 * in the directory is corrupted or empty and the previous
	 * one has an LSN gap at the end (due to a write error),
	 * we will create the next WAL between two existing ones,
	 * thus breaking the file order.
	 */
	if (vclock_compare(&r->vclock, vclock) < 0)
		vclock_copy(&r->vclock, vclock);
	return rc;

gap_error:
	diag_set(XlogGapError, &r->vclock, vclock);
	if (r->wal_dir.force_recovery) {
		diag_log();
		say_warn("ignoring a gap in LSN");
	} else {
		rc = -1;
	}
	goto out;
}

void
recovery_delete(struct recovery *r)
{
	assert(r->watcher == NULL);

	trigger_destroy(&r->on_close_log);
	xdir_destroy(&r->wal_dir);
	if (xlog_cursor_is_open(&r->cursor)) {
		/*
		 * Possible if shutting down a replication
		 * relay or if error during startup.
		 */
		xlog_cursor_close(&r->cursor, false);
	}
	free(r);
}

/**
 * Read all rows in a file starting from the last position.
 * Advance the position. If end of file is reached,
 * set l.eof_read.
 * The reading will be stopped on reaching stop_vclock.
 * Use NULL for boundless recover
 */
static int
recover_xlog(struct recovery *r, struct xstream *stream,
	     const struct vclock *stop_vclock)
{
	bool force_recovery = r->wal_dir.force_recovery;
	struct xrow_header row;
	uint64_t row_count = 0;
	int rc;

	while ((rc = xlog_cursor_next(&r->cursor, &row, force_recovery)) == 0) {
		/*
		 * Read the next row from xlog file.
		 *
		 * xlog_cursor_next() returns 1 when
		 * it can not read more rows. This doesn't mean
		 * the file is fully read: it's fully read only
		 * when EOF marker has been read, see i.eof_read
		 */
		if (stop_vclock != NULL &&
		    r->vclock.signature >= stop_vclock->signature)
			return 0;

		int64_t current_lsn = vclock_get(&r->vclock, row.replica_id);
		if (row.lsn <= current_lsn)
			continue; /* already applied, skip */

		/*
		 * All rows in xlog files have an assigned replica
		 * id. The only exception are local rows, which
		 * are signed with a zero replica id.
		 */
		assert(row.replica_id != 0 || row.group_id == GROUP_LOCAL);

		/*
		 * We can promote the vclock either before or
		 * after xstream_write(): it only makes any impact
		 * in case of forced recovery, when we skip the
		 * failed row anyway.
		 */
		vclock_follow_xrow(&r->vclock, &row);
		if (xstream_write(stream, &row) == 0) {
			++row_count;
			if (row_count % 100000 == 0) {
				say_info("%.1fM rows processed",
					 row_count / 1000000.);
			}
			continue;
		}

		if (!force_recovery) {
			rc = -1;
			break;
		}

		say_error("skipping row {%u: %lld}",
			  (unsigned)row.replica_id, (long long)row.lsn);
		diag_log();
	}

	return rc;
}

/**
 * Find out if there are new .xlog files since the current
 * LSN, and read them all up.
 *
 * Reading will be stopped on reaching recovery
 * vclock signature > to_checkpoint (after playing to_checkpoint record)
 * use NULL for boundless recover
 *
 * This function will not close r->current_wal if
 * recovery was successful.
 */
int
recover_remaining_wals(struct recovery *r, struct xstream *stream,
		       const struct vclock *stop_vclock, bool scan_dir)
{
	struct vclock *clock;

	if (scan_dir) {
		if (xdir_scan(&r->wal_dir) != 0)
			return -1;
	}

	if (xlog_cursor_is_open(&r->cursor)) {
		/* If there's a WAL open, recover from it first. */
		assert(!xlog_cursor_is_eof(&r->cursor));
		clock = vclockset_search(&r->wal_dir.index,
					 &r->cursor.meta.vclock);
		if (clock != NULL)
			goto recover_current_wal;
		/*
		 * The current WAL has disappeared under our feet -
		 * assume anything can happen in production and go on.
		 */
		say_error("file `%s' was deleted under our feet",
			  r->cursor.name);
	}

	for (clock = vclockset_match(&r->wal_dir.index, &r->vclock);
	     clock != NULL;
	     clock = vclockset_next(&r->wal_dir.index, clock)) {
		if (stop_vclock != NULL &&
		    clock->signature >= stop_vclock->signature) {
			break;
		}

		if (xlog_cursor_is_eof(&r->cursor) &&
		    vclock_sum(&r->cursor.meta.vclock) >= vclock_sum(clock)) {
			/*
			 * If we reached EOF while reading last xlog,
			 * we don't need to rescan it.
			 */
			continue;
		}

		if (recovery_open_log(r, clock) != 0)
			return -1;

		say_info("recover from `%s'", r->cursor.name);

recover_current_wal:
		if (recover_xlog(r, stream, stop_vclock) < 0)
			return -1;
	}

	if (xlog_cursor_is_eof(&r->cursor)) {
		if (recovery_close_log(r) != 0)
			return -1;
	}

	if (stop_vclock != NULL && vclock_compare(&r->vclock, stop_vclock) != 0) {
		diag_set(XlogGapError, &r->vclock, stop_vclock);
		return -1;
	}

	region_free(&fiber()->gc);
	return 0;
}

int
recovery_finalize(struct recovery *r)
{
	return recovery_close_log(r);
}


/* }}} */

/* {{{ Local recovery: support of hot standby and replication relay */

/**
 * Implements a subscription to WAL updates via fs events.
 * Any change to the WAL dir itself or a change in the XLOG
 * file triggers a wakeup. The WAL dir path is set in the
 * constructor. XLOG file path is set with set_log_path().
 */
struct wal_subscr {
	struct fiber	*f;
	unsigned int	events;
	struct ev_stat	dir_stat;
	struct ev_stat	file_stat;
	char		dir_path[PATH_MAX];
	char		file_path[PATH_MAX];
};

static void
wal_subscr_wakeup(struct wal_subscr *ws, unsigned int events)
{
	ws->events |= events;
	if (ws->f->flags & FIBER_IS_CANCELLABLE)
		fiber_wakeup(ws->f);
}

static void
wal_subscr_dir_stat_cb(struct ev_loop *, struct ev_stat *stat, int)
{
	struct wal_subscr *ws = (struct wal_subscr *)stat->data;
	wal_subscr_wakeup(ws, WAL_EVENT_ROTATE);
}

static void
wal_subscr_file_stat_cb(struct ev_loop *, struct ev_stat *stat, int)
{
	struct wal_subscr *ws = (struct wal_subscr *)stat->data;
	wal_subscr_wakeup(ws, WAL_EVENT_WRITE);
}

static void
wal_subscr_set_log_path(struct wal_subscr *ws, const char *path)
{
	size_t len;

	/*
	 * Avoid toggling ev_stat if the path didn't change.
	 * Note: .file_path valid iff file_stat is active.
	 */
	if (path && ev_is_active(&ws->file_stat) &&
	    strcmp(ws->file_path, path) == 0) {
		return;
	}

	ev_stat_stop(loop(), &ws->file_stat);
	if (path == NULL)
		return;

	len = snprintf(ws->file_path, sizeof(ws->file_path), "%s", path);
	if (len >= sizeof(ws->file_path))
		panic("wal_subscr: log path is too long: %s", path);

	ev_stat_set(&ws->file_stat, ws->file_path, 0.0);
	ev_stat_start(loop(), &ws->file_stat);
}

static void
wal_subscr_create(struct wal_subscr *ws, const char *wal_dir)
{
	size_t len;

	memset(ws, 0, sizeof(*ws));

	ws->f = fiber();
	ws->events = 0;

	len = snprintf(ws->dir_path, sizeof(ws->dir_path), "%s", wal_dir);
	if (len >= sizeof(ws->dir_path))
		panic("wal_subscr: wal dir path is too long: %s", wal_dir);

	ev_stat_init(&ws->dir_stat, wal_subscr_dir_stat_cb, "", 0.0);
	ev_stat_init(&ws->file_stat, wal_subscr_file_stat_cb, "", 0.0);
	ws->dir_stat.data = ws;
	ws->file_stat.data = ws;

	ev_stat_set(&ws->dir_stat, ws->dir_path, 0.0);
	ev_stat_start(loop(), &ws->dir_stat);
}

static void
wal_subscr_destroy(struct wal_subscr *ws)
{
	ev_stat_stop(loop(), &ws->file_stat);
	ev_stat_stop(loop(), &ws->dir_stat);
}

static int
hot_standby_f(va_list ap)
{
	struct recovery *r = va_arg(ap, struct recovery *);
	struct xstream *stream = va_arg(ap, struct xstream *);
	bool scan_dir = true;
	int rc = 0;

	ev_tstamp wal_dir_rescan_delay = va_arg(ap, ev_tstamp);
	fiber_set_user(fiber(), &admin_credentials);

	struct wal_subscr ws;
	wal_subscr_create(&ws, r->wal_dir.dirname);

	while (! fiber_is_cancelled()) {

		/*
		 * Recover until there is no new stuff which appeared in
		 * the log dir while recovery was running.
		 *
		 * Use vclock signature to represent the current wal
		 * since the xlog object itself may be freed in
		 * recover_remaining_rows().
		 */
		int64_t start, end;
		do {
			start = vclock_sum(&r->vclock);

			if (recover_remaining_wals(r, stream, NULL, scan_dir) != 0) {
				/*
				 * Since we're the fiber function the wrapper
				 * fiber_cxx_invoke doesn't log the real reson
				 * of the failure. Thus make it so explicitly.
				 */
				diag_log();
				rc = -1;
				goto out;
			}

			end = vclock_sum(&r->vclock);
			/*
			 * Continue, given there's been progress *and* there is a
			 * chance new WALs have appeared since.
			 * Sic: end * is < start (is 0) if someone deleted all logs
			 * on the filesystem.
			 */
		} while (end > start && !xlog_cursor_is_open(&r->cursor));

		wal_subscr_set_log_path(&ws, xlog_cursor_is_open(&r->cursor) ?
					r->cursor.name : NULL);

		bool timed_out = false;
		if (ws.events == 0) {
			/**
			 * Allow an immediate wakeup/break loop
			 * from recovery_stop_local().
			 */
			fiber_set_cancellable(true);
			timed_out = fiber_yield_timeout(wal_dir_rescan_delay);
			fiber_set_cancellable(false);
		}

		scan_dir = timed_out || (ws.events & WAL_EVENT_ROTATE) != 0;
		ws.events = 0;
	}
out:
	wal_subscr_destroy(&ws);
	return rc;
}

int
recovery_follow_local(struct recovery *r, struct xstream *stream,
		      const char *name, ev_tstamp wal_dir_rescan_delay)
{
	/*
	 * Start 'hot_standby' background fiber to follow xlog changes.
	 * It will pick up from the position of the currently open
	 * xlog.
	 */
	assert(r->watcher == NULL);
	r->watcher = fiber_new(name, hot_standby_f);
	if (!r->watcher)
		return -1;
	fiber_set_joinable(r->watcher, true);
	fiber_start(r->watcher, r, stream, wal_dir_rescan_delay);
	return 0;
}

int
recovery_stop_local(struct recovery *r)
{
	if (r->watcher) {
		struct fiber *f = r->watcher;
		r->watcher = NULL;
		fiber_cancel(f);
		if (fiber_join(f) != 0)
			return -1;
	}
	return 0;
}

/* }}} */
