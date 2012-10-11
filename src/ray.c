#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"

#include "ray_lib.h"

static int MAIN_INITIALIZED = 0;
static void _sleep_cb(uv_timer_t* handle, int status) {
  rayL_state_ready((ray_state_t*)handle->data);
  free(handle);
}
static int ray_sleep(lua_State* L) {
  lua_Number timeout = luaL_checknumber(L, 1);
  ray_state_t* state = rayL_state_self(L);
  uv_timer_t*  timer = (uv_timer_t*)malloc(sizeof(uv_timer_t));
  timer->data = state;
  uv_timer_init(rayL_event_loop(L), timer);
  uv_timer_start(timer, _sleep_cb, (long)(timeout * 1000), 0L);
  return rayL_state_suspend(state);
}

static int ray_mem_free(lua_State* L) {
  lua_pushinteger(L, uv_get_free_memory());
  return 1;
}

static int ray_mem_total(lua_State* L) {
  lua_pushinteger(L, uv_get_total_memory());
  return 1;
}

static int ray_hrtime(lua_State* L) {
  lua_pushinteger(L, uv_hrtime());
  return 1;
}

static int ray_self(lua_State* L) {
  lua_pushthread(L);
  lua_gettable(L, LUA_REGISTRYINDEX);
  return 1;
}

static int ray_cpu_info(lua_State* L) {
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

static int ray_interface_addresses(lua_State* L) {
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

luaL_Reg ray_funcs[] = {
  {"cpu_info",            ray_cpu_info},
  {"mem_free",            ray_mem_free},
  {"mem_total",           ray_mem_total},
  {"hrtime",              ray_hrtime},
  {"self",                ray_self},
  {"sleep",               ray_sleep},
  {"interface_addresses", ray_interface_addresses},
  {NULL,            NULL}
};

static const ray_const_reg_t ray_zmq_consts[] = {
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

LUALIB_API int luaopen_ray(lua_State *L) {

#ifndef WIN32
  signal(SIGPIPE, SIG_IGN);
#endif

  int i;
  uv_loop_t*    loop;
  ray_state_t*  curr;
  ray_object_t* stdfh;

  lua_settop(L, 0);

  /* register decoders */
  lua_pushcfunction(L, rayL_lib_decoder);
  lua_setfield(L, LUA_REGISTRYINDEX, "ray:lib:decoder");

  lua_pushcfunction(L, rayL_zmq_ctx_decoder);
  lua_setfield(L, LUA_REGISTRYINDEX, "ray:zmq:decoder");

  /* ray */
  rayL_new_module(L, "ray", ray_funcs);
  lua_pushvalue(L, -1);
  lua_setfield(L, LUA_REGISTRYINDEX, RAY_REG_KEY);

  /* ray.thread */
  rayL_new_module(L, "ray_thread", ray_thread_funcs);
  lua_setfield(L, -2, "thread");
  rayL_new_class(L, RAY_THREAD_T, ray_thread_meths);
  lua_pop(L, 1);

  if (!MAIN_INITIALIZED) {
    rayL_thread_init_main(L);
    lua_pop(L, 1);
  }

  /* ray.fiber */
  rayL_new_module(L, "ray_fiber", ray_fiber_funcs);

  /* borrow coroutine.yield (fast on LJ2) */
  lua_getglobal(L, "coroutine");
  lua_getfield(L, -1, "yield");
  lua_setfield(L, -3, "yield");
  lua_pop(L, 1); /* coroutine */

  lua_setfield(L, -2, "fiber");

  rayL_new_class(L, RAY_FIBER_T, ray_fiber_meths);
  lua_pop(L, 1);

  /* ray.codec */
  rayL_new_module(L, "ray_codec", ray_codec_funcs);
  lua_setfield(L, -2, "codec");

  /* ray.timer */
  rayopen_timer(L);

  /* ray.idle */
  rayL_new_module(L, "ray_idle", ray_idle_funcs);
  lua_setfield(L, -2, "idle");
  rayL_new_class(L, RAY_IDLE_T, ray_idle_meths);
  lua_pop(L, 1);

  /* ray.fs */
  rayL_new_module(L, "ray_fs", ray_fs_funcs);
  lua_setfield(L, -2, "fs");
  rayL_new_class(L, RAY_FILE_T, ray_file_meths);
  lua_pop(L, 1);

  /* ray.pipe */
  rayL_new_module(L, "ray_pipe", ray_pipe_funcs);
  lua_setfield(L, -2, "pipe");
  rayL_new_class(L, RAY_PIPE_T, ray_stream_meths);
  luaL_register(L, NULL, ray_pipe_meths);
  lua_pop(L, 1);

  /* ray.std{in,out,err} */
  if (!MAIN_INITIALIZED) {
    MAIN_INITIALIZED = 1;
    loop = rayL_event_loop(L);
    curr = rayL_state_self(L);

    const char* stdfhs[] = { "stdin", "stdout", "stderr" };
    for (i = 0; i < 3; i++) {
#ifdef WIN32
      const uv_file fh = GetStdHandle(i == 0 ? STD_INPUT_HANDLE
       : (i == 1 ? STD_OUTPUT_HANDLE : STD_ERROR_HANDLE));
#else
      const uv_file fh = i;
#endif
      stdfh = (ray_object_t*)lua_newuserdata(L, sizeof(ray_object_t));
      luaL_getmetatable(L, RAY_PIPE_T);
      lua_setmetatable(L, -2);
      rayL_object_init(curr, stdfh);
      uv_pipe_init(loop, &stdfh->h.pipe, 0);
      uv_pipe_open(&stdfh->h.pipe, fh);
      lua_pushvalue(L, -1);
      lua_setfield(L, LUA_REGISTRYINDEX, stdfhs[i]);
      lua_setfield(L, -2, stdfhs[i]);
    }
  }

  /* ray.net */
  rayL_new_module(L, "ray_net", ray_net_funcs);
  lua_setfield(L, -2, "net");
  rayL_new_class(L, RAY_NET_TCP_T, ray_stream_meths);
  luaL_register(L, NULL, ray_net_tcp_meths);
  lua_pop(L, 1);

  /* ray.process */
  rayL_new_module(L, "ray_process", ray_process_funcs);
  lua_setfield(L, -2, "process");
  rayL_new_class(L, RAY_PROCESS_T, ray_process_meths);
  lua_pop(L, 1);

  /* ray.zmq */
  rayL_new_module(L, "ray_zmq", ray_zmq_funcs);
  const ray_const_reg_t* c = ray_zmq_consts;
  for (; c->key; c++) {
    lua_pushinteger(L, c->val);
    lua_setfield(L, -2, c->key);
  }
  lua_setfield(L, -2, "zmq");
  rayL_new_class(L, RAY_ZMQ_CTX_T, ray_zmq_ctx_meths);
  rayL_new_class(L, RAY_ZMQ_SOCKET_T, ray_zmq_socket_meths);
  lua_pop(L, 2);

  lua_settop(L, 1);
  return 1;
}

#ifdef __cplusplus
}
#endif
