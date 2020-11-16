#pragma once
/*
 * Copyright 2010-2020, Tarantool AUTHORS, please see AUTHORS file.
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
#include "raftlib.h"

#if defined(__cplusplus)
extern "C" {
#endif

struct raft_request;

/** Raft state of this instance. */
static inline struct raft *
box_raft(void)
{
	extern struct raft box_raft_global;
	/**
	 * Ensure the raft node can be used. I.e. that it is properly
	 * initialized. Entirely for debug purposes.
	 */
	assert(box_raft_global.state != 0);
	return &box_raft_global;
}

/**
 * Let the global raft know that the election quorum could change. It happens
 * when configuration is updated, and when new nodes are added or old are
 * deleted from the cluster.
 */
void
box_raft_reconsider_election_quorum(void);

/**
 * Recovery a single Raft request. Raft state machine is not turned on yet, this
 * works only during instance recovery from the journal.
 */
void
box_raft_recover(const struct raft_request *req);

/** Save complete Raft state into a request to be persisted on disk locally. */
void
box_raft_checkpoint_local(struct raft_request *req);

/**
 * Save complete Raft state into a request to be sent to other instances of the
 * cluster.
 */
void
box_raft_checkpoint_remote(struct raft_request *req);

/** Handle a single Raft request from a node with instance id @a source. */
int
box_raft_process(struct raft_request *req, uint32_t source);

void
box_raft_init(void);

void
box_raft_free(void);

#if defined(__cplusplus)
}
#endif
