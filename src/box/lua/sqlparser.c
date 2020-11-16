#include "execute.h"
#include "lua/utils.h"

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

static int
lbox_sqlparser_parse(struct lua_State *L)
{
	lua_pushliteral(L, "sqlparser.parse");
	return 1;
}

static int
lbox_sqlparser_serialize(struct lua_State *L)
{
	lua_pushliteral(L, "sqlparser.serialize");
	return 1;
}

static int
lbox_sqlparser_deserialize(struct lua_State *L)
{
	lua_pushliteral(L, "sqlparser.deserialize");
	return 1;
}

void
box_lua_sqlparser_init(struct lua_State *L)
{
	static const struct luaL_Reg meta[] = {
		{ "parse", lbox_sqlparser_parse },
		{ "serialize", lbox_sqlparser_serialize },
		{ "deserialize", lbox_sqlparser_deserialize },
		{ NULL, NULL },
	};
	luaL_register_module(L, "sqlparser", meta);
	lua_pop(L, 1);

	return;
}
