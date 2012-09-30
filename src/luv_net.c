#include "luv.h"
#include <string.h>

static int luv_new_tcp(lua_State* L) {
  luv_state_t*  curr = luvL_state_self(L);
  luv_object_t* self = lua_newuserdata(L, sizeof(luv_object_t));
  luaL_getmetatable(L, LUV_NET_TCP_T);
  lua_setmetatable(L, -2);

  luvL_object_init(curr, self);
  uv_tcp_init(luvL_event_loop(curr), &self->h.tcp);

  return 1;
}

static void _getaddrinfo_cb(uv_getaddrinfo_t* req, int s, struct addrinfo* ai) {
  luv_state_t* curr = container_of(req, luv_state_t, req);
  char host[INET6_ADDRSTRLEN];
  int  port;

  if (ai->ai_family == PF_INET) {
    struct sockaddr_in* addr = (struct sockaddr_in*)ai->ai_addr;
    uv_ip4_name(addr, host, INET6_ADDRSTRLEN);
    port = addr->sin_port;
  }
  else if (ai->ai_family == PF_INET6) {
    struct sockaddr_in6* addr = (struct sockaddr_in6*)ai->ai_addr;
    uv_ip6_name((struct sockaddr_in6*)ai->ai_addr, host, INET6_ADDRSTRLEN);
    port = addr->sin6_port;
  }
  lua_settop(curr->L, 0);
  lua_pushstring(curr->L, host);
  lua_pushinteger(curr->L, port);

  uv_freeaddrinfo(ai);

  luvL_state_ready(curr);
}

static int luv_getaddrinfo(lua_State* L) {
  luv_state_t* curr = luvL_state_self(L);
  uv_loop_t*   loop = luvL_event_loop(curr);
  uv_getaddrinfo_t* req = &curr->req.getaddrinfo;

  const char* node      = NULL;
  const char* service   = NULL;
  struct addrinfo hints;

  if (!lua_isnoneornil(L, 1)) {
    node = luaL_checkstring(L, 1);
  }
  if (!lua_isnoneornil(L, 2)) {
    service = luaL_checkstring(L, 2);
  }
  if (node == NULL && service == NULL) {
    return luaL_error(L, "getaddrinfo: provide either node or service");
  }

  hints.ai_family   = PF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;
  hints.ai_flags    = 0;

  if (lua_istable(L, 3)) {
    lua_getfield(L, 3, "family");
    if (!lua_isnil(L, -1)) {
      const char* s = lua_tostring(L, -1);
      if (strcmp(s, "INET") == 0) {
        hints.ai_family = PF_INET;
      }
      else if (strcmp(s, "INET6")) {
        hints.ai_family = PF_INET6;
      }
      else {
        return luaL_error(L, "unsupported family: %s", s);
      }
    }
    lua_pop(L, 1);

    lua_getfield(L, 3, "socktype");
    if (!lua_isnil(L, -1)) {
      const char* s = lua_tostring(L, -1);
      if (strcmp(s, "STREAM")) {
        hints.ai_socktype = SOCK_STREAM;
      }
      else if (strcmp(s, "DGRAM")) {
        hints.ai_socktype = SOCK_DGRAM;
      }
      else {
        return luaL_error(L, "unsupported socktype: %s", s);
      }
    }
    lua_pop(L, 1);

    lua_getfield(L, 3, "protocol");
    if (!lua_isnil(L, -1)) {
      const char* s = lua_tostring(L, -1);
      if (strcmp(s, "TCP")) {
        hints.ai_protocol = IPPROTO_TCP;
      }
      else if (strcmp(s, "UDP")) {
        hints.ai_protocol = IPPROTO_UDP;
      }
      else {
        return luaL_error(L, "unsupported protocol: %s", s);
      }
    }
    lua_pop(L, 1);
  }

  int rv = uv_getaddrinfo(loop, req, _getaddrinfo_cb, node, service, &hints);
  if (rv) {
    uv_err_t err = uv_last_error(loop);
    return luaL_error(L, uv_strerror(err));
  }

  return luvL_state_suspend(curr);
}

static int luv_tcp_bind(lua_State* L) {
  luv_object_t *self = luaL_checkudata(L, 1, LUV_NET_TCP_T);

  struct sockaddr_in addr;
  const char* host;
  int port, rv;

  host = luaL_checkstring(L, 2);
  port = luaL_checkint(L, 3);
  addr = uv_ip4_addr(host, port);

  rv = uv_tcp_bind(&self->h.tcp, addr);
  lua_pushinteger(L, rv);

  return 1;
}

static int luv_tcp_connect(lua_State *L) {
  luv_object_t* self = luaL_checkudata(L, 1, LUV_NET_TCP_T);
  luv_state_t*  curr = luvL_state_self(L);

  struct sockaddr_in addr;
  const char* host;
  int port, rv;

  host = luaL_checkstring(L, 2);
  port = luaL_checkint(L, 3);
  addr = uv_ip4_addr(host, port);

  lua_settop(L, 2);

  rv = uv_tcp_connect(&curr->req.connect, &self->h.tcp, addr, luvL_connect_cb);
  if (rv) {
    uv_err_t err = uv_last_error(self->h.handle.loop);
    lua_settop(L, 0);
    lua_pushnil(L);
    lua_pushstring(L, uv_strerror(err));
    return 2;
  }

  return luvL_state_suspend(curr);
}

static int luv_tcp_nodelay(lua_State* L) {
  luv_object_t* self = luaL_checkudata(L, 1, LUV_NET_TCP_T);
  luaL_checktype(L, 2, LUA_TBOOLEAN);
  int enable = lua_toboolean(L, 2);
  lua_settop(L, 2);
  int rv = uv_tcp_nodelay(&self->h.tcp, enable);
  lua_pushinteger(L, rv);
  return 1;
}
static int luv_tcp_keepalive(lua_State* L) {
  luv_object_t* self = luaL_checkudata(L, 1, LUV_NET_TCP_T);
  luaL_checktype(L, 2, LUA_TBOOLEAN);
  int enable = lua_toboolean(L, 2);
  unsigned int delay = 0;
  if (enable) {
    delay = luaL_checkint(L, 3);
  }
  int rv = uv_tcp_keepalive(&self->h.tcp, enable, delay);
  lua_settop(L, 1);
  lua_pushinteger(L, rv);
  return 1;
}

/* mostly stolen from Luvit */
static int luv_tcp_getsockname(lua_State* L) {
  luv_object_t* self = luaL_checkudata(L, 1, LUV_NET_TCP_T);
  luv_state_t*  curr = luvL_state_self(L);

  int port = 0;
  char ip[INET6_ADDRSTRLEN];
  int family;

  struct sockaddr_storage addr;
  int len = sizeof(addr);

  if (uv_tcp_getsockname(&self->h.tcp, (struct sockaddr*)&addr, &len)) {
    uv_err_t err = uv_last_error(luvL_event_loop(curr));
    return luaL_error(L, "getsockname: %s", uv_strerror(err));
  }

  family = addr.ss_family;
  if (family == AF_INET) {
    struct sockaddr_in* addrin = (struct sockaddr_in*)&addr;
    uv_inet_ntop(AF_INET, &(addrin->sin_addr), ip, INET6_ADDRSTRLEN);
    port = ntohs(addrin->sin_port);
  }
  else if (family == AF_INET6) {
    struct sockaddr_in6* addrin6 = (struct sockaddr_in6*)&addr;
    uv_inet_ntop(AF_INET6, &(addrin6->sin6_addr), ip, INET6_ADDRSTRLEN);
    port = ntohs(addrin6->sin6_port);
  }

  lua_newtable(L);
  lua_pushnumber(L, port);
  lua_setfield(L, -2, "port");
  lua_pushnumber(L, family);
  lua_setfield(L, -2, "family");
  lua_pushstring(L, ip);
  lua_setfield(L, -2, "address");

  return 1;
}

/* mostly stolen from Luvit */
static int luv_tcp_getpeername(lua_State* L) {
  luv_object_t* self = luaL_checkudata(L, 1, LUV_NET_TCP_T);
  luv_state_t*  curr = luvL_state_self(L);

  int port = 0;
  char ip[INET6_ADDRSTRLEN];
  int family;

  struct sockaddr_storage addr;
  int len = sizeof(addr);

  if (uv_tcp_getpeername(&self->h.tcp, (struct sockaddr*)&addr, &len)) {
    uv_err_t err = uv_last_error(luvL_event_loop(curr));
    return luaL_error(L, "getpeername: %s", uv_strerror(err));
  }

  family = addr.ss_family;
  if (family == AF_INET) {
    struct sockaddr_in* addrin = (struct sockaddr_in*)&addr;
    uv_inet_ntop(AF_INET, &(addrin->sin_addr), ip, INET6_ADDRSTRLEN);
    port = ntohs(addrin->sin_port);
  }
  else if (family == AF_INET6) {
    struct sockaddr_in6* addrin6 = (struct sockaddr_in6*)&addr;
    uv_inet_ntop(AF_INET6, &(addrin6->sin6_addr), ip, INET6_ADDRSTRLEN);
    port = ntohs(addrin6->sin6_port);
  }

  lua_newtable(L);
  lua_pushnumber(L, port);
  lua_setfield(L, -2, "port");
  lua_pushnumber(L, family);
  lua_setfield(L, -2, "family");
  lua_pushstring(L, ip);
  lua_setfield(L, -2, "address");

  return 1;
}

static int luv_tcp_free(lua_State *L) {
  luv_object_t* self = lua_touserdata(L, 1);
  luvL_object_close(self);
  return 0;
}
static int luv_tcp_tostring(lua_State *L) {
  luv_object_t *self = luaL_checkudata(L, 1, LUV_NET_TCP_T);
  lua_pushfstring(L, "userdata<%s>: %p", LUV_NET_TCP_T, self);
  return 1;
}

luaL_Reg luv_net_funcs[] = {
  {"tcp",         luv_new_tcp},
  /* {"udp",       luv_new_udp}, */
  {"getaddrinfo", luv_getaddrinfo},
  {NULL,        NULL}
};

luaL_Reg luv_net_tcp_meths[] = {
  {"bind",        luv_tcp_bind},
  {"connect",     luv_tcp_connect},
  {"getsockname", luv_tcp_getsockname},
  {"getpeername", luv_tcp_getpeername},
  {"keepalive",   luv_tcp_keepalive},
  {"nodelay",     luv_tcp_nodelay},
  {"__gc",        luv_tcp_free},
  {"__tostring",  luv_tcp_tostring},
  {NULL,          NULL}
};

/*
luaL_Reg luv_udp_meths[] = {
  {"bind",      luv_udp_bind},
  {"send",      luv_udp_send},
  {"recv",      luv_udp_recv},
  {"__gc",      luv_udp_free},
  {"__tostring",luv_udp_tostring},
  {NULL,        NULL}
};
*/

