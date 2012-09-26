#ifndef LUV_THREAD_H
#define LUV_THREAD_H

#include "luv_core.h"

#define LUV_THREAD_T  "luv.thread"
#define LUV_CHANNEL_T "luv.thread.channel"

LUALIB_API int luaopenL_luv_thread(lua_State *L);

#endif /* LUV_THREAD_H */