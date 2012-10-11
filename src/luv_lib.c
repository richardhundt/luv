#include "luv_common.h"
#include "luv_lib.h"

int luvL_traceback(lua_State* L) {
  lua_getfield(L, LUA_GLOBALSINDEX, "debug");
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    return 1;
  }
  lua_getfield(L, -1, "traceback");
  if (!lua_isfunction(L, -1)) {
    lua_pop(L, 2);
    return 1;
  }

  lua_pushvalue(L, 1);    /* pass error message */
  lua_pushinteger(L, 2);  /* skip this function and traceback */
  lua_call(L, 2, 1);      /* call debug.traceback */

  return 1;
}

int luvL_lib_decoder(lua_State* L) {
  const char* name = lua_tostring(L, -1);
  lua_getfield(L, LUA_REGISTRYINDEX, name);
  TRACE("LIB DECODE HOOK: %s\n", name);
  assert(lua_istable(L, -1));
  return 1;
}

/* return "luv:lib:decoder", <modname> */
int luvL_lib_encoder(lua_State* L) {
  TRACE("LIB ENCODE HOOK\n");
  lua_pushstring(L, "luv:lib:decoder");
  lua_getfield(L, 1, "__name");
  assert(!lua_isnil(L, -1));
  return 2;
}

int luvL_module_new(lua_State* L, const char* name, luaL_Reg* funcs) {
  lua_newtable(L);

  lua_pushstring(L, name);
  lua_setfield(L, -2, "__name");

  lua_pushvalue(L, -1);
  lua_setmetatable(L, -2);

  lua_pushcfunction(L, luvL_lib_encoder);
  lua_setfield(L, -2, "__codec");

  lua_pushvalue(L, -1);
  lua_setfield(L, LUA_REGISTRYINDEX, name);

  if (funcs) {
    luaL_register(L, NULL, funcs);
  }
  return 1;
}

int luvL_class_new(lua_State* L, const char* name, luaL_Reg* meths) {
  luaL_newmetatable(L, name);
  lua_pushvalue(L, -1);
  lua_setfield(L, -2, "__index");
  lua_pushstring(L, name);
  lua_setfield(L, -2, "__name");
  if (meths) {
    luaL_register(L, NULL, meths);
  }
  return 1;
}

int luvL_class_extend(lua_State* L, const char* b, const char* n, luaL_Reg* m) {
  luvL_class_new(L, n, m);
  luaL_getmetatable(L, b);
  lua_setmetatable(L, -2);
  return 1;
}

int luvL_class_mixin(lua_State* L, const char* from) {
  int idx = lua_gettop(L);
  luaL_getmetatable(L, from);
  lua_pushnil(L);
  while (lua_next(L, -2) != 0) {
    lua_pushvalue(L, -2);
    lua_pushvalue(L, -2);
    lua_settable(L, idx);
    lua_pop(L, 1);
  }
  lua_pop(L, 1); /* base metatable */
  assert(lua_gettop(L) == idx);
  return 1;
}

void* luvL_checkudata(lua_State* L, int idx, const char* name) {
  luaL_getmetatable(L, name);
  lua_pushvalue(L, idx);
  luaL_checktype(L, -1, LUA_TUSERDATA);
  for (;lua_getmetatable(L, -1);) {
    if (lua_equal(L, -1, -3)) {
      /* found it */
      lua_pop(L, 3);
      return lua_touserdata(L, idx);
    }
    else if (lua_equal(L, -1, -2)) {
      /* table is it's own metatable, end here */
      break;
    }
    else {
      /* up the inheritance chain */
      lua_replace(L, -2);
    }
  }
  return luaL_error(L, "userdata<%s> expected at %i", name, idx);
}


