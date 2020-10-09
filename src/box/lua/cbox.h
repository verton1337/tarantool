/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2020, Tarantool AUTHORS, please see AUTHORS file.
 */

#pragma once

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct lua_State;

/**
 * Initialize cbox Lua module.
 *
 * @param L Lua state where to register the cbox module.
 */
void
box_lua_cbox_init(struct lua_State *L);
#if defined(__cplusplus)
}
#endif /* defined(__plusplus) */
