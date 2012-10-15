#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"

#ifdef __cplusplus
}
#endif


#include "luv.h"

static int MAIN_INITIALIZED = 0;

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

int luvL_new_module(lua_State* L, const char* name, luaL_Reg* funcs) {
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

int luvL_new_class(lua_State* L, const char* name, luaL_Reg* meths) {
  luaL_newmetatable(L, name);
  lua_pushvalue(L, -1);
  lua_setfield(L, -2, "__index");
  if (meths) {
    luaL_register(L, NULL, meths);
  }
  return 1;
}

uv_loop_t* luvL_event_loop(lua_State* L) {
  return luvL_state_self(L)->loop;
}

static void _sleep_cb(uv_timer_t* handle, int status) {
  luvL_state_ready((luv_state_t*)handle->data);
  free(handle);
}
static int luv_sleep(lua_State* L) {
  lua_Number timeout = luaL_checknumber(L, 1);
  luv_state_t* state = luvL_state_self(L);
  uv_timer_t*  timer = (uv_timer_t*)malloc(sizeof(uv_timer_t));
  timer->data = state;
  uv_timer_init(luvL_event_loop(L), timer);
  uv_timer_start(timer, _sleep_cb, (long)(timeout * 1000), 0L);
  return luvL_state_suspend(state);
}

static int luv_mem_free(lua_State* L) {
  lua_pushinteger(L, uv_get_free_memory());
  return 1;
}

static int luv_mem_total(lua_State* L) {
  lua_pushinteger(L, uv_get_total_memory());
  return 1;
}

static int luv_hrtime(lua_State* L) {
  lua_pushinteger(L, uv_hrtime());
  return 1;
}

static int luv_self(lua_State* L) {
  lua_pushthread(L);
  lua_gettable(L, LUA_REGISTRYINDEX);
  return 1;
}

static int luv_cpu_info(lua_State* L) {
  int size, i;
  uv_cpu_info_t* info;
  uv_err_t err = uv_cpu_info(&info, &size);

  lua_settop(L, 0);

  if (err.code) {
    lua_pushboolean(L, 0);
    luaL_error(L, uv_strerror(err));
    return 2;
  }

  lua_newtable(L);

  for (i = 0; i < size; i++) {
    lua_newtable(L);

    lua_pushstring(L, info[i].model);
    lua_setfield(L, -2, "model");

    lua_pushinteger(L, (lua_Integer)info[i].speed);
    lua_setfield(L, -2, "speed");

    lua_newtable(L); /* times */

    lua_pushinteger(L, (lua_Integer)info[i].cpu_times.user);
    lua_setfield(L, -2, "user");

    lua_pushinteger(L, (lua_Integer)info[i].cpu_times.nice);
    lua_setfield(L, -2, "nice");

    lua_pushinteger(L, (lua_Integer)info[i].cpu_times.sys);
    lua_setfield(L, -2, "sys");

    lua_pushinteger(L, (lua_Integer)info[i].cpu_times.idle);
    lua_setfield(L, -2, "idle");

    lua_pushinteger(L, (lua_Integer)info[i].cpu_times.irq);
    lua_setfield(L, -2, "irq");

    lua_setfield(L, -2, "times");

    lua_rawseti(L, 1, i + 1);
  }

  uv_free_cpu_info(info, size);
  return 1;
}

static int luv_interface_addresses(lua_State* L) {
  int size, i;
  char buf[INET6_ADDRSTRLEN];

  uv_interface_address_t* info;
  uv_err_t err = uv_interface_addresses(&info, &size);

  lua_settop(L, 0);

  if (err.code) {
    lua_pushboolean(L, 0);
    luaL_error(L, uv_strerror(err));
    return 2;
  }

  lua_newtable(L);

  for (i = 0; i < size; i++) {
    uv_interface_address_t addr = info[i];

    lua_newtable(L);

    lua_pushstring(L, addr.name);
    lua_setfield(L, -2, "name");

    lua_pushboolean(L, addr.is_internal);
    lua_setfield(L, -2, "is_internal");

    if (addr.address.address4.sin_family == PF_INET) {
      uv_ip4_name(&addr.address.address4, buf, sizeof(buf));
    }
    else if (addr.address.address4.sin_family == PF_INET6) {
      uv_ip6_name(&addr.address.address6, buf, sizeof(buf));
    }

    lua_pushstring(L, buf);
    lua_setfield(L, -2, "address");

    lua_rawseti(L, -2, i + 1);
  }

  uv_free_interface_addresses(info, size);

  return 1;
}

luaL_Reg luv_funcs[] = {
  {"cpu_info",            luv_cpu_info},
  {"mem_free",            luv_mem_free},
  {"mem_total",           luv_mem_total},
  {"hrtime",              luv_hrtime},
  {"self",                luv_self},
  {"sleep",               luv_sleep},
  {"interface_addresses", luv_interface_addresses},
  {NULL,            NULL}
};

#ifdef USE_ZMQ
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
#endif

#ifdef __cplusplus
extern "C" {
#endif
LUALIB_API int luaopen_luv(lua_State *L) {

#ifndef WIN32
  signal(SIGPIPE, SIG_IGN);
#endif

  int i;
  uv_loop_t*    loop;
  luv_state_t*  curr;
  luv_object_t* stdfh;

  lua_settop(L, 0);

  /* register decoders */
  lua_pushcfunction(L, luvL_lib_decoder);
  lua_setfield(L, LUA_REGISTRYINDEX, "luv:lib:decoder");

#ifdef USE_ZMQ
  lua_pushcfunction(L, luvL_zmq_ctx_decoder);
  lua_setfield(L, LUA_REGISTRYINDEX, "luv:zmq:decoder");
#endif

  /* luv */
  luvL_new_module(L, "luv", luv_funcs);

  /* luv.thread */
  luvL_new_module(L, "luv_thread", luv_thread_funcs);
  lua_setfield(L, -2, "thread");
  luvL_new_class(L, LUV_THREAD_T, luv_thread_meths);
  lua_pop(L, 1);

  if (!MAIN_INITIALIZED) {
    luvL_thread_init_main(L);
    lua_pop(L, 1);
  }

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

  /* luv.idle */
  luvL_new_module(L, "luv_idle", luv_idle_funcs);
  lua_setfield(L, -2, "idle");
  luvL_new_class(L, LUV_IDLE_T, luv_idle_meths);
  lua_pop(L, 1);

  /* luv.fs */
  luvL_new_module(L, "luv_fs", luv_fs_funcs);
  lua_setfield(L, -2, "fs");
  luvL_new_class(L, LUV_FILE_T, luv_file_meths);
  lua_pop(L, 1);

  /* luv.pipe */
  luvL_new_module(L, "luv_pipe", luv_pipe_funcs);
  lua_setfield(L, -2, "pipe");
  luvL_new_class(L, LUV_PIPE_T, luv_stream_meths);
  luaL_register(L, NULL, luv_pipe_meths);
  lua_pop(L, 1);

  /* luv.std{in,out,err} */
  if (!MAIN_INITIALIZED) {
    MAIN_INITIALIZED = 1;
    loop = luvL_event_loop(L);
    curr = luvL_state_self(L);

    const char* stdfhs[] = { "stdin", "stdout", "stderr" };
    for (i = 0; i < 3; i++) {
#ifdef WIN32
      const uv_file fh = GetStdHandle(i == 0 ? STD_INPUT_HANDLE
       : (i == 1 ? STD_OUTPUT_HANDLE : STD_ERROR_HANDLE));
#else
      const uv_file fh = i;
#endif
      stdfh = (luv_object_t*)lua_newuserdata(L, sizeof(luv_object_t));
      luaL_getmetatable(L, LUV_PIPE_T);
      lua_setmetatable(L, -2);
      luvL_object_init(curr, stdfh);
      uv_pipe_init(loop, &stdfh->h.pipe, 0);
      uv_pipe_open(&stdfh->h.pipe, fh);
      lua_pushvalue(L, -1);
      lua_setfield(L, LUA_REGISTRYINDEX, stdfhs[i]);
      lua_setfield(L, -2, stdfhs[i]);
    }
  }

  /* luv.net */
  luvL_new_module(L, "luv_net", luv_net_funcs);
  lua_setfield(L, -2, "net");
  luvL_new_class(L, LUV_NET_TCP_T, luv_stream_meths);
  luaL_register(L, NULL, luv_net_tcp_meths);
  lua_pop(L, 1);

  /* luv.process */
  luvL_new_module(L, "luv_process", luv_process_funcs);
  lua_setfield(L, -2, "process");
  luvL_new_class(L, LUV_PROCESS_T, luv_process_meths);
  lua_pop(L, 1);

  /* luv.zmq */
#ifdef USE_ZMQ
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
#endif

  lua_settop(L, 1);
  return 1;
}

#ifdef __cplusplus
}
#endif
