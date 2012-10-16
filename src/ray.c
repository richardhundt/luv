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

static int ray_self(lua_State* L) {
  lua_pushthread(L);
  lua_gettable(L, LUA_REGISTRYINDEX);
  return 1;
}

static luaL_Reg ray_funcs[] = {
  {"self",  ray_self},
  {NULL,    NULL}
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

  luaopen_ray_thread(L);
  luaopen_ray_fiber(L);
  luaopen_ray_codec(L);

  lua_pop(L, 3);

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
