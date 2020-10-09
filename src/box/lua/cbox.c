/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2020, Tarantool AUTHORS, please see AUTHORS file.
 */

#include <string.h>
#include <lua.h>

#define RB_COMPACT 1
#include <small/rb.h>

#include "diag.h"

#include "box/module_cache.h"
#include "box/error.h"
#include "box/port.h"

#include "trivia/util.h"
#include "lua/utils.h"

/**
 * Function descriptor.
 */
struct cbox_func {
	/**
	 * Gather functions into rbtree.
	 */
	rb_node(struct cbox_func) nd;

	/**
	 * Symbol descriptor for the function in
	 * an associated module.
	 */
	struct module_sym mod_sym;

	/**
	 * Number of references to the function
	 * instance.
	 */
	ssize_t ref;

	/** Function name. */
	const char *name;

	/** Function name length. */
	size_t name_len;

	/** Function name keeper. */
	char inplace[0];
};

/**
 * A tree to lookup functions by name.
 */
typedef rb_tree(struct cbox_func) func_rb_t;
static func_rb_t func_rb_root;

static int
cbox_func_cmp(const struct cbox_func *a, const struct cbox_func *b)
{
	ssize_t len = (ssize_t)a->name_len - (ssize_t)b->name_len;
	if (len == 0)
		return strcmp(a->name, b->name);
	return len < 0 ? -1 : 1;
}

rb_gen(MAYBE_UNUSED static, func_rb_, func_rb_t,
       struct cbox_func, nd, cbox_func_cmp);

/**
 * Find function in a tree.
 */
struct cbox_func *
cbox_func_find(const char *name, size_t name_len)
{
	struct cbox_func v = {
		.name		= name,
		.name_len	= name_len,
	};
	return func_rb_search(&func_rb_root, &v);
}

/**
 * Unreference a function instance.
 */
static void
cbox_func_unref(struct cbox_func *cf)
{
	assert(cf->ref > 0);
	if (cf->ref-- == 1)
		func_rb_remove(&func_rb_root, cf);
}

/**
 * Reference a function instance.
 */
static bool
cbox_func_ref(struct cbox_func *cf)
{
	assert(cf->ref >= 0);

	/*
	 * Hardly to ever happen but just
	 * to make sure.
	 */
	if (cf->ref == SSIZE_MAX) {
		const char *fmt =
			"Too many function references (max %zd)";
		diag_set(IllegalParams, fmt, SSIZE_MAX);
		return false;
	}

	if (cf->ref++ == 0)
		func_rb_insert(&func_rb_root, cf);

	return true;
}

/**
 * Allocate a new function instance.
 */
static struct cbox_func *
cbox_func_new(const char *name, size_t name_len)
{
	const ssize_t cf_size = sizeof(struct cbox_func);
	ssize_t size = cf_size + name_len + 1;
	if (size < 0) {
		const size_t max_len = SSIZE_MAX - cf_size - 1;
		const char *fmt = "Function name is too long (max %zd)";
		diag_set(IllegalParams, fmt, max_len);
		return NULL;
	}

	struct cbox_func *cf = malloc(size);
	if (cf == NULL) {
		diag_set(OutOfMemory, size, "malloc", "cf");
		return NULL;
	}

	cf->mod_sym.addr	= NULL;
	cf->mod_sym.module	= NULL;
	cf->ref			= 0;
	cf->mod_sym.name	= cf->inplace;
	cf->name		= cf->inplace;
	cf->name_len		= name_len;

	memcpy(cf->inplace, name, name_len);
	cf->inplace[name_len] = '\0';

	memset(&cf->nd, 0, sizeof(cf->nd));
	return cf;
}

/**
 * Fetch a function instance from the Lua object.
 */
static struct cbox_func *
lcbox_func_get_handler(struct lua_State *L)
{
	struct cbox_func *cf = NULL;
	int top = lua_gettop(L);

	if (lua_getmetatable(L, -top) != 0) {
		lua_getfield(L, -1, "__cbox_func");
		if (lua_isuserdata(L, -1)) {
			struct cbox_func **pcf = lua_touserdata(L, -1);
			cf = pcf[0];
		}
		lua_pop(L, 2);
	}
	return cf;
}

/**
 * Free a function instance.
 *
 * It is called by Lua itself when a variable has no more reference.
 * Since we associate a function instance for each variable we
 * can't just free the memory immediately, instead we must be sure
 * the final unload() is called and there are no more instances left
 * in the tree thus next load() will have to allocate a new instance.
 */
static int
lcbox_func_gc(struct lua_State *L)
{
	struct cbox_func *cf = lcbox_func_get_handler(L);
	if (cf->ref == 0) {
		TRASH(cf);
		free(cf);
	}
	return 0;
}

/**
 * Call a function by its name from the Lua code.
 */
static int
lcbox_func_call(struct lua_State *L)
{
	struct cbox_func *cf = lcbox_func_get_handler(L);
	if (cf == NULL) {
		/*
		 * How can this happen? Someone screwed
		 * internal object data intentionally?
		 * Whatever, the pointer is ruined we
		 * can't do anything.
		 */
		const char *fmt = "Function is corrupted";
		diag_set(IllegalParams, fmt);
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
	if (!cbox_func_ref(cf))
		return luaT_push_nil_and_error(L);

	lua_newtable(L);

	lua_pushstring(L, "name");
	lua_pushstring(L, cf->name);
	lua_settable(L, -3);

	lua_newtable(L);

	/*
	 * A new variable should be callable for
	 * convenient use in Lua.
	 */
	lua_pushstring(L, "__call");
	lua_pushcfunction(L, lcbox_func_call);
	lua_settable(L, -3);

	/*
	 * We will release the memory associated
	 * with the objet if only no active refs
	 * are left.
	 */
	lua_pushstring(L, "__gc");
	lua_pushcfunction(L, lcbox_func_gc);
	lua_settable(L, -3);

	/*
	 * Carry the pointer to the function so
	 * we won't need to run a lookup when
	 * calling.
	 */
	lua_pushstring(L, "__cbox_func");
	*(struct cbox_func **)lua_newuserdata(L, sizeof(cf)) = cf;
	lua_settable(L, -3);

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

	cbox_func_unref(cf);
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
 * Initialize cbox Lua module.
 *
 * @param L Lua state where to register the cbox module.
 */
void
box_lua_cbox_init(struct lua_State *L)
{
	static const struct luaL_Reg cbox_methods[] = {
		{ NULL, NULL }
	};
	luaL_register_module(L, "cbox", cbox_methods);

	/* func table */
	static const struct luaL_Reg func_table[] = {
		{ "load",	lcbox_func_load },
		{ "unload",	lcbox_func_unload },
	};

	lua_newtable(L);
	for (size_t i = 0; i < lengthof(func_table); i++) {
		lua_pushstring(L, func_table[i].name);
		lua_pushcfunction(L, func_table[i].func);
		lua_settable(L, -3);
	}
	lua_setfield(L, -2, "func");

	/* module table */
	static const struct luaL_Reg module_table[] = {
		{ "reload",	lcbox_module_reload },
	};

	lua_newtable(L);
	for (size_t i = 0; i < lengthof(module_table); i++) {
		lua_pushstring(L, module_table[i].name);
		lua_pushcfunction(L, module_table[i].func);
		lua_settable(L, -3);
	}
	lua_setfield(L, -2, "module");

	lua_pop(L, 1);
}

/**
 * Initialize cbox module.
 *
 * @return 0 on success, -1 on error (diag is set).	
 */
int
cbox_init(void)
{
	func_rb_new(&func_rb_root);
	return 0;
}

/**
 * Free cbox module.
 */
void
cbox_free(void)
{
	struct cbox_func *cf = func_rb_first(&func_rb_root);
	while (cf != NULL) {
		func_rb_remove(&func_rb_root, cf);
		cf = func_rb_first(&func_rb_root);
	}
	func_rb_new(&func_rb_root);
}
