#ifndef LUV_H
#define LUV_H

#define LUV_REG_KEY "luv"

#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"

int luv__new_namespace(lua_State* L, const char* name);

LUALIB_API int luaopen_luv(lua_State *L);

#endif /* LUV_H */
