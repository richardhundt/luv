#include <stdio.h>
#include <assert.h>

#include "ray.h"
#include "ray_lib.h"

int rayL_traceback(lua_State* L) {
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

int rayL_lib_decoder(lua_State* L) {
  const char* name = lua_tostring(L, -1);
  lua_getfield(L, LUA_REGISTRYINDEX, name);
  TRACE("LIB DECODE HOOK: %s\n", name);
  assert(lua_istable(L, -1));
  return 1;
}

/* return "ray:lib:decoder", <modname> */
int rayL_lib_encoder(lua_State* L) {
  TRACE("LIB ENCODE HOOK\n");
  lua_pushstring(L, "ray:lib:decoder");
  lua_getfield(L, 1, "__name");
  assert(!lua_isnil(L, -1));
  return 2;
}

int rayL_module(lua_State* L, const char* name, luaL_Reg* funcs) {
  TRACE("new module: %s, funcs: %p\n", name, funcs);
  lua_newtable(L);

  lua_pushstring(L, name);
  lua_setfield(L, -2, "__name");

  /* its own metatable */
  lua_pushvalue(L, -1);
  lua_setmetatable(L, -2);

  /* so that it can pass through a thread boundary */
  lua_pushcfunction(L, rayL_lib_encoder);
  lua_setfield(L, -2, "__codec");

  lua_pushvalue(L, -1);
  lua_setfield(L, LUA_REGISTRYINDEX, name);

  if (funcs) {
    TRACE("register funcs...\n");
    luaL_register(L, NULL, funcs);
  }

  TRACE("DONE\n");
  return 1;
}

int rayL_class(lua_State* L, const char* name, luaL_Reg* meths) {
  luaL_newmetatable(L, name);
  lua_pushvalue(L, -1);
  lua_setfield(L, -2, "__index");

  if (meths) luaL_register(L, NULL, meths);

  lua_pushstring(L, name);
  lua_setfield(L, -2, "__name");

  return 1;
}

void* rayL_checkudata(lua_State* L, int idx, const char* name) {
  luaL_checktype(L, idx, LUA_TUSERDATA);
  luaL_getmetatable(L, name);
  lua_pushvalue(L, idx);
  for (; lua_getmetatable(L, -1); ) {
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
  luaL_error(L, "userdata<%s> expected at %i", name, idx);
  /* the above longjmp's anyway, but need to keep gcc happy */
  return NULL;
}

LUALIB_API int rayL_core_init(lua_State *L) {
  lua_settop(L, 0);
  lua_getfield(L, LUA_REGISTRYINDEX, RAY_REG_KEY);
  if (lua_isnil(L, -1)) {
    lua_pop(L, 1);
    luaopen_ray(L);
    lua_getfield(L, LUA_REGISTRYINDEX, RAY_REG_KEY);
  }
  return 1;
}

