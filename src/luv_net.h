#ifndef LUV_NET_H
#define LUV_NET_H

#include "luv_core.h"

#define LUV_TCP_T "luv.net.tcp"
#define LUV_UDP_T "luv.net.udp"

#define LUV_NET_KEY "luv.net"

LUALIB_API int luaopenL_luv_net(lua_State *L);

#endif /* LUV_NET_H */

