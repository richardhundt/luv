#ifndef _LUV_H_
#define _LUV_H_

#ifdef __cplusplus
extern "C" {
#endif

#ifdef WIN32
#  ifdef LUV_EXPORT
#    define LUALIB_API __declspec(dllexport)
#  else
#    define LUALIB_API __declspec(dllimport)
#  endif
#else
#  define LUALIB_API LUA_API
#endif

#include "luv_lib.h"
#include "luv_hash.h"
#include "luv_cond.h"
#include "luv_object.h"
#include "luv_state.h"
#include "luv_thread.h"
#include "luv_fiber.h"
#include "luv_timer.h"
#include "luv_idle.h"
#include "luv_fs.h"
#include "luv_stream.h"
#include "luv_net.h"
#include "luv_process.h"
#include "luv_pipe.h"
#include "luv_zmq.h"
#include "luv_codec.h"

extern luaL_Reg luv_thread_funcs[32];
extern luaL_Reg luv_thread_meths[32];

extern luaL_Reg luv_fiber_funcs[32];
extern luaL_Reg luv_fiber_meths[32];

extern luaL_Reg luv_cond_funcs[32];
extern luaL_Reg luv_cond_meths[32];

extern luaL_Reg luv_codec_funcs[32];

extern luaL_Reg luv_timer_funcs[32];
extern luaL_Reg luv_timer_meths[32];

extern luaL_Reg luv_idle_funcs[32];
extern luaL_Reg luv_idle_meths[32];

extern luaL_Reg luv_fs_funcs[32];
extern luaL_Reg luv_file_meths[32];

extern luaL_Reg luv_stream_meths[32];

extern luaL_Reg luv_net_funcs[32];
extern luaL_Reg luv_net_tcp_meths[32];
extern luaL_Reg luv_net_udp_meths[32];

extern luaL_Reg luv_pipe_funcs[32];
extern luaL_Reg luv_pipe_meths[32];

extern luaL_Reg luv_process_funcs[32];
extern luaL_Reg luv_process_meths[32];

extern luaL_Reg luv_zmq_funcs[32];
extern luaL_Reg luv_zmq_ctx_meths[32];
extern luaL_Reg luv_zmq_socket_meths[32];

LUALIB_API int luaopen_luv(lua_State *L);

#ifdef __cplusplus
}
#endif

#endif /* _LUV_H_ */
