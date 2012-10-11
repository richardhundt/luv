#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"

#include "ray.h"
#include "ray_lib.h"
#include "ray_common.h"
#include "ray_cond.h"
#include "ray_codec.h"
#include "ray_state.h"
#include "ray_fiber.h"
#include "ray_thread.h"
#include "ray_object.h"

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

static luaL_Reg ray_funcs[] = {
  {"cpu_info",            ray_cpu_info},
  {"mem_free",            ray_mem_free},
  {"mem_total",           ray_mem_total},
  {"hrtime",              ray_hrtime},
  {"self",                ray_self},
  {"sleep",               ray_sleep},
  {"interface_addresses", ray_interface_addresses},
  {NULL,            NULL}
};

LUALIB_API int luaopen_ray(lua_State *L) {

#ifndef WIN32
  signal(SIGPIPE, SIG_IGN);
#endif

  lua_settop(L, 0);

  /* register lib __codec hook */
  lua_pushcfunction(L, rayL_lib_decoder);
  lua_setfield(L, LUA_REGISTRYINDEX, "ray:lib:decoder");

  /* ray */
  rayL_module(L, "ray", ray_funcs);
  lua_pushvalue(L, -1);
  lua_setfield(L, LUA_REGISTRYINDEX, RAY_REG_KEY);

  lua_checkstack(L, 20);
  TRACE("opening thread, top: %i...\n", lua_gettop(L));
  luaopen_ray_thread(L);
  TRACE("opening fiber, top: %i...\n", lua_gettop(L));
  luaopen_ray_fiber(L);
  TRACE("opening codec, top: %i...\n", lua_gettop(L));
  luaopen_ray_codec(L);
  TRACE("OK\n");

#ifdef CHEESE
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
#endif

  lua_settop(L, 1);
  TRACE("HERE?\n");
  return 1;
}

#ifdef __cplusplus
}
#endif
