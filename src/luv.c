#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"

#include "luv.h"

int luvL_lib_xdup(lua_State* L) {
  lua_State* L1 = (lua_State*)lua_touserdata(L, 2);
  lua_getfield(L, 1, "__name");
  const char* name = lua_tostring(L, -1);
  lua_getfield(L1, LUA_REGISTRYINDEX, name);
  return 0;
}

int luvL_new_module(lua_State* L, const char* name, luaL_Reg* funcs) {
  lua_newtable(L);
  lua_pushstring(L, name);
  lua_setfield(L, -2, "__name");
  lua_pushvalue(L, -1);
  lua_setmetatable(L, -2);
  lua_pushcfunction(L, luvL_lib_xdup);
  lua_setfield(L, -2, "__xdup");
  lua_pushvalue(L, -1);
  lua_setfield(L, LUA_REGISTRYINDEX, name);
  if (funcs) {
    luaL_register(L, NULL, funcs);
  }
  return 1;
}

int luvL_new_class(lua_State* L, const char* name, luaL_Reg* meths) {
  luaL_newmetatable(L, name);
  lua_pushvalue(L, -1);
  lua_setfield(L, -2, "__index");
  if (meths) {
    luaL_register(L, NULL, meths);
  }
  return 1;
}

uv_loop_t* luvL_event_loop(luv_state_t* state) {
  return luvL_thread_self(state->L)->loop;
}

luaL_Reg luv_funcs[] = {
  {NULL,            NULL}
};

static const luv_const_reg_t luv_zmq_consts[] = {
  /* ctx options */
  {"IO_THREADS",        ZMQ_IO_THREADS},
  {"MAX_SOCKETS",       ZMQ_MAX_SOCKETS},

  /* socket types */
  {"REQ",               ZMQ_REQ},
  {"REP",               ZMQ_REP},
  {"DEALER",            ZMQ_DEALER},
  {"ROUTER",            ZMQ_ROUTER},
  {"PUB",               ZMQ_PUB},
  {"SUB",               ZMQ_SUB},
  {"PUSH",              ZMQ_PUSH},
  {"PULL",              ZMQ_PULL},
  {"PAIR",              ZMQ_PAIR},

  /* socket options */
  {"SNDHWM",            ZMQ_SNDHWM},
  {"RCVHWM",            ZMQ_RCVHWM},
  {"AFFINITY",          ZMQ_AFFINITY},
  {"IDENTITY",          ZMQ_IDENTITY},
  {"SUBSCRIBE",         ZMQ_SUBSCRIBE},
  {"UNSUBSCRIBE",       ZMQ_UNSUBSCRIBE},
  {"RATE",              ZMQ_RATE},
  {"RECOVERY_IVL",      ZMQ_RECOVERY_IVL},
  {"SNDBUF",            ZMQ_SNDBUF},
  {"RCVBUF",            ZMQ_RCVBUF},
  {"RCVMORE",           ZMQ_RCVMORE},
  {"FD",                ZMQ_FD},
  {"EVENTS",            ZMQ_EVENTS},
  {"TYPE",              ZMQ_TYPE},
  {"LINGER",            ZMQ_LINGER},
  {"RECONNECT_IVL",     ZMQ_RECONNECT_IVL},
  {"BACKLOG",           ZMQ_BACKLOG},
  {"RECONNECT_IVL_MAX", ZMQ_RECONNECT_IVL_MAX},
  {"RCVTIMEO",          ZMQ_RCVTIMEO},
  {"SNDTIMEO",          ZMQ_SNDTIMEO},
  {"IPV4ONLY",          ZMQ_IPV4ONLY},
  {"ROUTER_BEHAVIOR",   ZMQ_ROUTER_BEHAVIOR},
  {"TCP_KEEPALIVE",     ZMQ_TCP_KEEPALIVE},
  {"TCP_KEEPALIVE_IDLE",ZMQ_TCP_KEEPALIVE_IDLE},
  {"TCP_KEEPALIVE_CNT", ZMQ_TCP_KEEPALIVE_CNT},
  {"TCP_KEEPALIVE_INTVL",ZMQ_TCP_KEEPALIVE_INTVL},
  {"TCP_ACCEPT_FILTER", ZMQ_TCP_ACCEPT_FILTER},

  /* msg options */
  {"MORE",              ZMQ_MORE},

  /* send/recv flags */
  {"DONTWAIT",          ZMQ_DONTWAIT},
  {"SNDMORE",           ZMQ_SNDMORE},

  /* poll events */
  {"POLLIN",            ZMQ_POLLIN},
  {"POLLOUT",           ZMQ_POLLOUT},
  {"POLLERR",           ZMQ_POLLERR},

  /* devices */
  {"STREAMER",          ZMQ_STREAMER},
  {"FORWARDER",         ZMQ_FORWARDER},
  {"QUEUE",             ZMQ_QUEUE},
  {NULL,                0}
};

LUALIB_API int luaopen_luv(lua_State *L) {
  lua_settop(L, 0);

  /* luv */
  luvL_new_module(L, "luv", luv_funcs);

  /* luv.thread */
  luvL_new_module(L, "luv_thread", luv_thread_funcs);
  lua_setfield(L, -2, "thread");
  luvL_new_class(L, LUV_THREAD_T, luv_thread_meths);
  lua_pop(L, 1);

  luvL_thread_init_main(L);
  lua_pop(L, 1);

  /* luv.fiber */
  luvL_new_module(L, "luv_fiber", luv_fiber_funcs);

  /* borrow coroutine.yield (fast on LJ2) */
  lua_getglobal(L, "coroutine");
  lua_getfield(L, -1, "yield");
  lua_setfield(L, -3, "yield");
  lua_pop(L, 1); /* coroutine */

  lua_setfield(L, -2, "fiber");

  luvL_new_class(L, LUV_FIBER_T, luv_fiber_meths);
  lua_pop(L, 1);

  /* luv.codec */
  luvL_new_module(L, "luv_codec", luv_codec_funcs);
  lua_setfield(L, -2, "codec");

  /* luv.timer */
  luvL_new_module(L, "luv_timer", luv_timer_funcs);
  lua_setfield(L, -2, "timer");
  luvL_new_class(L, LUV_TIMER_T, luv_timer_meths);
  lua_pop(L, 1);

  /* luv.fs */
  luvL_new_module(L, "luv_fs", luv_fs_funcs);
  lua_setfield(L, -2, "fs");
  luvL_new_class(L, LUV_FILE_T, luv_file_meths);
  lua_pop(L, 1);

  /* luv.net */
  luvL_new_module(L, "luv_net", luv_net_funcs);
  lua_setfield(L, -2, "net");
  luvL_new_class(L, LUV_NET_TCP_T, luv_net_tcp_meths);
  luaL_register(L, NULL, luv_stream_meths);
  lua_pop(L, 1);

  /* luv.zmq */
  luvL_new_module(L, "luv_zmq", luv_zmq_funcs);
  const luv_const_reg_t* c = luv_zmq_consts;
  for (; c->key; c++) {
    lua_pushinteger(L, c->val);
    lua_setfield(L, -2, c->key);
  }
  lua_setfield(L, -2, "zmq");
  luvL_new_class(L, LUV_ZMQ_CTX_T, luv_zmq_ctx_meths);
  luvL_new_class(L, LUV_ZMQ_SOCKET_T, luv_zmq_socket_meths);
  lua_pop(L, 2);

  lua_settop(L, 1);
  return 1;
}

