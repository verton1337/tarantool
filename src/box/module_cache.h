/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2020, Tarantool AUTHORS, please see AUTHORS file.
 */

#pragma once

#include <small/rlist.h>

#include "func_def.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

/**
 * Function name descriptor: a symbol and a package.
 */
struct func_name_desc {
	/**
	 * Null-terminated symbol name, e.g.
	 * "func" for "mod.submod.func".
	 */
	const char *sym;
	/**
	 * Package name, e.g. "mod.submod" for
	 * "mod.submod.func".
	 */
	const char *package;
	/**
	 * A pointer to the last character in ->package + 1.
	 */
	const char *package_end;
};

/***
 * Parse function name into a name descriptor.
 * 
 * For example, str = foo.bar.baz => sym = baz, package = foo.bar
 *
 * @param str function name, e.g. "module.submodule.function".
 * @param[out] d parsed symbol and a package name.
 */
void
parse_func_name(const char *str, struct func_name_desc *d);

/**
 * Dynamic shared module.
 */
struct module {
	/**
	 * Module dlhandle.
	 */
	void *handle;
	/**
	 * List of associated symbols (functions).
	 */
	struct rlist mod_sym_head;
	/**
	 * Count of active calls.
	 */
	size_t calls;
	/**
	 * Module's package name.
	 */
	char package[0];
};

/**
 * Callable symbol bound to a module.
 */
struct module_sym {
	/**
	 * Anchor for module membership.
	 */
	struct rlist list;
	/**
	 * For C functions, address of the function.
	 */
	box_function_f addr;
	/**
	 * Each stored function keeps a handle to the
	 * dynamic library for the C callback.
	 */
	struct module *module;
	/**
	 * Symbol (function) name definition.
	 */
	char *name;
};

/**
 * Load a new module symbol.
 *
 * @param mod_sym symbol to load.
 *
 * @returns 0 on succse, -1 otherwise, diag is set.
 */
int
module_sym_load(struct module_sym *mod_sym);

/**
 * Unload a module's symbol.
 *
 * @param mod_sym symbol to unload.
 */
void
module_sym_unload(struct module_sym *mod_sym);

/**
 * Execute a module symbol (run a function).
 *
 * The function packs function arguments into a message pack
 * and send it as a function argument. Function may return
 * results via @a ret stream.
 *
 * @param mod_sym module symbol to run.
 * @param args function arguments
 * @param[out] execution results
 *
 * @returns 0 on success, -1 otherwise, diag is set.
 */
int
module_sym_call(struct module_sym *mod_sym, struct port *args,
		struct port *ret);

/**
 * Reload a module and all associated symbols.
 *
 * @param str function name, e.g. "module.submodule.function".
 * @param[out] d parsed symbol and a package name.
 *
 * @return 0 on succes, -1 otherwise, diag is set.
 */
int
module_reload(const char *package, const char *package_end,
	      struct module **module);

/**
 * Initialize modules subsystem.
 *
 * @return 0 on succes, -1 otherwise, diag is set.
 */
int
module_init(void);

/**
 * Free modules subsystem.
 */
void
module_free(void);

#if defined(__cplusplus)
}
#endif /* defined(__plusplus) */
