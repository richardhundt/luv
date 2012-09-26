#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"

#include "luv.h"
#include "luv_core.h"
#include "luv_cond.h"
#include "luv_timer.h"
#include "luv_fs.h"
#include "luv_net.h"
#include "luv_zmq.h"
#include "luv_thread.h"

static int luv__lib_xdup(lua_State* L) {
  lua_State* L1 = (lua_State*)lua_touserdata(L, 2);
  lua_getfield(L, 1, "__name");
  const char* name = lua_tostring(L, -1);
  lua_getfield(L1, LUA_REGISTRYINDEX, name);
  return 0;
}

int luv__new_namespace(lua_State* L, const char* name) {
  int top = lua_gettop(L);
  lua_newtable(L);
  lua_pushstring(L, name);
  lua_setfield(L, -2, "__name");
  lua_pushvalue(L, -1);
  lua_setmetatable(L, -2);
  lua_pushcfunction(L, luv__lib_xdup);
  lua_setfield(L, -2, "__xdup");
  lua_pushvalue(L, -1);
  lua_setfield(L, LUA_REGISTRYINDEX, name);
  assert(lua_gettop(L) == top + 1);
  return 1;
}

LUALIB_API int luaopen_luv(lua_State *L) {
  lua_settop(L, 0);

  /* luv */
  luv__new_namespace(L, LUV_REG_KEY);
  
  /* luv.fiber */
  luaopenL_luv_core(L);
  luaopenL_luv_cond(L);

  /* luv.fs */
  luaopenL_luv_fs(L);

  /* luv.timer */
  luaopenL_luv_timer(L);

  /* luv.net */
  luaopenL_luv_net(L);
  
  /* luv.zmq */
  luaopenL_luv_zmq(L);

  /* luv.thread */
  luaopenL_luv_thread(L);

  lua_settop(L, 0);
  lua_getfield(L, LUA_REGISTRYINDEX, LUV_REG_KEY);
  return 1;
}

