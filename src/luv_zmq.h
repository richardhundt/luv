#ifndef LUV_ZMQ_H
#define LUV_ZMQ_H

#include "luv_core.h"

#define LUV_ZMQ_CTX_T    "luv.zmq.ctx"
#define LUV_ZMQ_SOCKET_T "luv.zmq.socket"

#define LUV_ZMQ_SCLOSED   (1 << 0)
#define LUV_ZMQ_SREADABLE (1 << 1)

LUALIB_API int luaopenL_luv_zmq(lua_State *L);

#endif /* LUV_ZMQ_H */
