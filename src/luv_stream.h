#ifndef LUV_STREAM_H
#define LUV_STREAM_H

#include "luv_core.h"
#include "luv_cond.h"

#define LUV_STREAM_T "luv.stream"

#define LUV_SSTARTED  (1 << 0)
#define LUV_SWAITING  (1 << 1)
#define LUV_SSTOPPED  (1 << 2)
#define LUV_SREADING  (1 << 3)
#define LUV_SSHUTDOWN (1 << 4)
#define LUV_SCLOSED   (1 << 5)

#define luv_stream_started(S) ((S)->flags & LUV_SSTARTED)
#define luv_stream_waiting(S) ((S)->flags & LUV_SWAITING)
#define luv_stream_stopped(S) ((S)->flags & LUV_SSTOPPED)
#define luv_stream_reading(S) ((S)->flags & LUV_SREADING)
#define luv_stream_closed(S)  ((S)->flags & LUV_SCLOSED)

luaL_Reg luv_stream_meths[16];

LUALIB_API int luaopenL_luv_stream(lua_State *L);

#endif /* LUV_STREAM_H */
