/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2020, Tarantool AUTHORS, please see AUTHORS file.
 */

#include <string.h>
#include <lua.h>

#include "assoc.h"
#include "diag.h"

#include "box/module_cache.h"
#include "box/error.h"
#include "box/port.h"

#include "trivia/util.h"
#include "lua/utils.h"

/** A type to find a function handle from an object. */
static const char *cbox_func_handle_uname = "cbox_func_handle";

/**
 * Function descriptor.
 */
struct cbox_func {
	/**
	 * Symbol descriptor for the function in
	 * an associated module.
	 */
	struct module_sym mod_sym;
	/**
	 * Number of loads of the function.
	 */
	int64_t load_count;
	/**
	 * Function name.
	 */
	const char *name;
	/**
	 * Function name length.
	 */
	size_t name_len;
	/**
	 * Function name in-place keeper.
	 */
	char inplace[0];
};

/**
 * Function name to cbox_func hash.
 */
static struct mh_strnptr_t *func_hash = NULL;

/**
 * Find function in cbox_func hash.
 */
struct cbox_func *
cbox_func_find(const char *name, size_t name_len)
{
	mh_int_t e = mh_strnptr_find_inp(func_hash, name, name_len);
	if (e == mh_end(func_hash))
		return NULL;
	return mh_strnptr_node(func_hash, e)->val;
}

/**
 * Delete a function instance from the hash or decrease
 * a reference if the function is still loaded.
 */
static void
cbox_func_del(struct cbox_func *cf)
{
	assert(cf->load_count > 0);
	if (cf->load_count-- != 1)
		return;

	mh_int_t e = mh_strnptr_find_inp(func_hash, cf->name, cf->name_len);
	assert(e != mh_end(func_hash));
	mh_strnptr_del(func_hash, e, NULL);
}

/**
 * Add a function instance into the hash or increase
 * a reference if the function is already exist.
 */
static bool
cbox_func_add(struct cbox_func *cf)
{
	assert(cf->load_count >= 0);
	if (cf->load_count++ != 0)
		return true;

	const struct mh_strnptr_node_t nd = {
		.str	= cf->name,
		.len	= cf->name_len,
		.hash	= mh_strn_hash(cf->name, cf->name_len),
		.val	= cf,
	};

	mh_int_t e = mh_strnptr_put(func_hash, &nd, NULL, NULL);
	if (e == mh_end(func_hash)) {
		diag_set(OutOfMemory, sizeof(nd),
			 "malloc", "cbox_func node");
		return false;
	}
	return true;
}

/**
 * Allocate a new function instance.
 */
static struct cbox_func *
cbox_func_new(const char *name, size_t name_len)
{
	const ssize_t cf_size = sizeof(struct cbox_func);
	size_t size = cf_size + name_len + 1;
	struct cbox_func *cf = malloc(size);
	if (cf == NULL) {
		diag_set(OutOfMemory, size, "malloc", "cf");
		return NULL;
	}

	cf->mod_sym.addr	= NULL;
	cf->mod_sym.module	= NULL;
	cf->load_count		= 0;
	cf->mod_sym.name	= cf->inplace;
	cf->name		= cf->inplace;
	cf->name_len		= name_len;

	memcpy(cf->inplace, name, name_len);
	cf->inplace[name_len] = '\0';

	return cf;
}

/**
 * Load a new function.
 *
 * This function takes a function name from the caller
 * stack @a L and creates a new function object. If
 * the function is already loaded we simply return
 * a reference to existing one.
 *
 * Possible errors:
 *
 * - IllegalParams: function name is either not supplied
 *   or not a string.
 * - IllegalParams: function references limit exceeded.
 * - OutOfMemory: unable to allocate a function.
 *
 * @returns function object on success or {nil,error} on error,
 * the error is set to the diagnostics area.
 */
static int
lcbox_func_load(struct lua_State *L)
{
	const char *method = "cbox.func.load";
	struct cbox_func *cf = NULL;

	if (lua_gettop(L) != 1 || !lua_isstring(L, 1)) {
		const char *fmt =
			"Expects %s(\'name\') but no name passed";
		diag_set(IllegalParams, fmt, method);
		return luaT_push_nil_and_error(L);
	}

	size_t name_len;
	const char *name = lua_tolstring(L, 1, &name_len);

	cf = cbox_func_find(name, name_len);
	if (cf == NULL) {
		cf = cbox_func_new(name, name_len);
		if (cf == NULL)
			return luaT_push_nil_and_error(L);
	}
	if (!cbox_func_add(cf))
		return luaT_push_nil_and_error(L);

	*(struct cbox_func **)lua_newuserdata(L, sizeof(cf)) = cf;
	luaL_getmetatable(L, cbox_func_handle_uname);
	lua_setmetatable(L, -2);
	return 1;
}

/**
 * Unload a function.
 *
 * This function takes a function name from the caller
 * stack @a L and unloads a function object.
 *
 * Possible errors:
 *
 * - IllegalParams: function name is either not supplied
 *   or not a string.
 * - IllegalParams: the function does not exist.
 *
 * @returns true on success or {nil,error} on error,
 * the error is set to the diagnostics area.
 */
static int
lcbox_func_unload(struct lua_State *L)
{
	const char *method = "cbox.func.unload";
	const char *name = NULL;

	if (lua_gettop(L) != 1 || !lua_isstring(L, 1)) {
		const char *fmt =
			"Expects %s(\'name\') but no name passed";
		diag_set(IllegalParams, fmt, method);
		return luaT_push_nil_and_error(L);
	}

	size_t name_len;
	name = lua_tolstring(L, 1, &name_len);

	struct cbox_func *cf = cbox_func_find(name, name_len);
	if (cf == NULL) {
		const char *fmt = tnt_errcode_desc(ER_NO_SUCH_FUNCTION);
		diag_set(IllegalParams, fmt, name);
		return luaT_push_nil_and_error(L);
	}

	cbox_func_del(cf);
	lua_pushboolean(L, true);
	return 1;
}

/**
 * Reload a module.
 *
 * This function takes a module name from the caller
 * stack @a L and reloads all functions associated with
 * the module.
 *
 * Possible errors:
 *
 * - IllegalParams: module name is either not supplied
 *   or not a string.
 * - IllegalParams: the function does not exist.
 * - ClientError: a module with the name provided does
 *   not exist.
 *
 * @returns true on success or {nil,error} on error,
 * the error is set to the diagnostics area.
 */
static int
lcbox_module_reload(struct lua_State *L)
{
	const char *method = "cbox.module.reload";
	const char *fmt = "Expects %s(\'name\') but no name passed";

	if (lua_gettop(L) != 1 || !lua_isstring(L, 1)) {
		diag_set(IllegalParams, fmt, method);
		return luaT_push_nil_and_error(L);
	}

	size_t name_len;
	const char *name = lua_tolstring(L, 1, &name_len);
	if (name == NULL || name_len < 1) {
		diag_set(IllegalParams, fmt, method);
		return luaT_push_nil_and_error(L);
	}

	struct module *module = NULL;
	if (module_reload(name, &name[name_len], &module) == 0) {
		if (module != NULL) {
			lua_pushboolean(L, true);
			return 1;
		}
		diag_set(ClientError, ER_NO_SUCH_MODULE, name);
	}
	return luaT_push_nil_and_error(L);
}

/**
 * Fetch cbox_func instance from an object.
 */
static struct cbox_func *
cbox_fetch_func_handle(struct lua_State *L)
{
	struct cbox_func **cf_ptr = luaL_testudata(L, 1, cbox_func_handle_uname);
	if (cf_ptr != NULL) {
		assert(*cf_ptr != NULL);
		return *cf_ptr;
	}
	return NULL;
}

/**
 * Function handle representation for REPL (console).
 */
static int
lcbox_handle_serialize(struct lua_State *L)
{
	struct cbox_func *cf = cbox_fetch_func_handle(L);
	if (cf == NULL) {
		diag_set(IllegalParams, "Bad params, use __serialize(obj)");
		return luaT_error(L);
	}

	lua_createtable(L, 0, 0);
	lua_pushstring(L, cf->name);
	lua_setfield(L, -2, "name");

	return 1;
}

/**
 * Handle __index request for a function object.
 */
static int
lcbox_handle_index(struct lua_State *L)
{
	/*
	 * Instead of showing userdata pointer
	 * lets provide a serialized value.
	 */
	lua_getmetatable(L, 1);
	lua_pushvalue(L, 2);
	lua_rawget(L, -2);
	if (!lua_isnil(L, -1))
		return 1;

	struct cbox_func *cf = cbox_fetch_func_handle(L);
	size_t len = 0;
	const char *key = lua_tolstring(L, 2, &len);

	if (lua_type(L, 2) != LUA_TSTRING || cf == NULL || key == NULL) {
		diag_set(IllegalParams,
			 "Bad params, use __index(obj, <string>)");
		return luaT_error(L);
	}

	if (strcmp(key, "name") == 0) {
		lua_pushstring(L, cf->name);
		return 1;
	}

	return 0;
}

/**
 * Free function handle if there is no active loads left.
 */
static int
lcbox_handle_gc(struct lua_State *L)
{
	struct cbox_func *cf = cbox_fetch_func_handle(L);
	if (cf != NULL && cf->load_count == 0) {
		TRASH(cf);
		free(cf);
	}
	return 0;
}

/**
 * Call a function by its name from the Lua code.
 */
static int
lcbox_handle_call(struct lua_State *L)
{
	struct cbox_func *cf = cbox_fetch_func_handle(L);
	if (cf == NULL) {
		diag_set(IllegalParams, "Function is corrupted");
		return luaT_push_nil_and_error(L);
	}

	/*
	 * FIXME: We should get rid of luaT_newthread but this
	 * requires serious modifications. In particular
	 * port_lua_do_dump uses tarantool_L reference and
	 * coro_ref must be valid as well.
	 */
	lua_State *args_L = luaT_newthread(tarantool_L);
	if (args_L == NULL)
		return luaT_push_nil_and_error(L);

	int coro_ref = luaL_ref(tarantool_L, LUA_REGISTRYINDEX);
	lua_xmove(L, args_L, lua_gettop(L) - 1);

	struct port args;
	port_lua_create(&args, args_L);
	((struct port_lua *)&args)->ref = coro_ref;

	struct port ret;
	if (module_sym_call(&cf->mod_sym, &args, &ret) != 0) {
		port_destroy(&args);
		return luaT_push_nil_and_error(L);
	}

	int top = lua_gettop(L);
	lua_pushboolean(L, true);
	port_dump_lua(&ret, L, true);
	int cnt = lua_gettop(L) - top;

	port_destroy(&ret);
	port_destroy(&args);

	return cnt;
}

/**
 * Initialize cbox module.
 */
void
box_lua_cbox_init(struct lua_State *L)
{
	func_hash = mh_strnptr_new();
	if (func_hash == NULL) {
		panic("Can't allocate cbox hash table");
	}

	static const struct luaL_Reg cbox_methods[] = {
		{ NULL, NULL },
	};
	luaL_register_module(L, "cbox", cbox_methods);
	lua_pop(L, 1);

	static const struct luaL_Reg func_methods[] = {
		{ "load",		lcbox_func_load		},
		{ "unload",		lcbox_func_unload	},
		{ NULL, NULL },
	};
	luaL_register_module(L, "cbox.func", func_methods);
	lua_pop(L, 1);

	static const struct luaL_Reg module_methods[] = {
		{ "reload",		lcbox_module_reload	},
		{ NULL, NULL },
	};
	luaL_register_module(L, "cbox.module", module_methods);
	lua_pop(L, 1);

	static const struct luaL_Reg func_handle_methods[] = {
		{ "__index",		lcbox_handle_index	},
		{ "__serialize",	lcbox_handle_serialize	},
		{ "__call",		lcbox_handle_call	},
		{ "__gc",		lcbox_handle_gc		},
		{ NULL, NULL },
	};
	luaL_register_type(L, cbox_func_handle_uname, func_handle_methods);
}
