#include "ray_lib.h"
#include "ray_actor.h"
#include "ray_stream.h"
#include <string.h>

static ray_vtable_t tcp_v = {
  recv: rayM_stream_recv,
  send: rayM_stream_send,
  close: rayM_stream_close
};

static int tcp_new(lua_State* L) {
  ray_actor_t* self = ray_actor_new(L, RAY_TCP_T, &tcp_v);
  uv_tcp_init(ray_get_loop(L), &self->h.tcp);
  return 1;
}

static void _getaddrinfo_cb(uv_getaddrinfo_t* req, int s, struct addrinfo* ai) {
  ray_actor_t* self = container_of(req, ray_actor_t, r);
  char host[INET6_ADDRSTRLEN];
  int  port = 0;

  if (ai->ai_family == PF_INET) {
    struct sockaddr_in* addr = (struct sockaddr_in*)ai->ai_addr;
    uv_ip4_name(addr, host, INET6_ADDRSTRLEN);
    port = addr->sin_port;
  }
  else if (ai->ai_family == PF_INET6) {
    struct sockaddr_in6* addr = (struct sockaddr_in6*)ai->ai_addr;
    uv_ip6_name(addr, host, INET6_ADDRSTRLEN);
    port = addr->sin6_port;
  }
  lua_settop(self->L, 0);
  lua_pushstring(self->L, host);
  lua_pushinteger(self->L, port);

  uv_freeaddrinfo(ai);

  ray_notify(self, 2);
  lua_settop(self->L, 0);
}

static int net_getaddrinfo(lua_State* L) {
  ray_actor_t* curr = ray_get_self(L);
  uv_loop_t*   loop = ray_get_loop(L);
  uv_getaddrinfo_t* req = &curr->r.getaddrinfo;

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
      else if (strcmp(s, "INET6") == 0) {
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
      if (strcmp(s, "STREAM") == 0) {
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
      if (strcmp(s, "TCP") == 0) {
        hints.ai_protocol = IPPROTO_TCP;
      }
      else if (strcmp(s, "UDP") == 0) {
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

  return ray_recv(curr, curr);
}

static int tcp_bind(lua_State* L) {
  ray_actor_t *self = (ray_actor_t*)luaL_checkudata(L, 1, RAY_TCP_T);

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

static int tcp_connect(lua_State *L) {
  ray_actor_t* self = (ray_actor_t*)luaL_checkudata(L, 1, RAY_TCP_T);
  ray_actor_t* curr = ray_get_self(L);

  struct sockaddr_in addr;
  const char* host;
  int port, rv;

  host = luaL_checkstring(L, 2);
  port = luaL_checkint(L, 3);
  addr = uv_ip4_addr(host, port);

  lua_settop(L, 2);

  curr->r.req.data = self;

  /* put a copy of self on the stack to return on success */
  lua_settop(L, 1);
  lua_settop(self->L, 0);
  lua_xmove(L, self->L, 1);

  rv = uv_tcp_connect(&curr->r.connect, &self->h.tcp, addr, ray_connect_cb);
  if (rv) {
    uv_err_t err = uv_last_error(ray_get_loop(L));
    lua_settop(L, 0);
    lua_pushnil(L);
    lua_pushstring(L, uv_strerror(err));
    return 2;
  }

  return ray_recv(curr, self);
}

static int tcp_nodelay(lua_State* L) {
  ray_actor_t* self = (ray_actor_t*)luaL_checkudata(L, 1, RAY_TCP_T);
  luaL_checktype(L, 2, LUA_TBOOLEAN);
  int enable = lua_toboolean(L, 2);
  lua_settop(L, 2);
  int rv = uv_tcp_nodelay(&self->h.tcp, enable);
  lua_pushinteger(L, rv);
  return 1;
}
static int tcp_keepalive(lua_State* L) {
  ray_actor_t* self = (ray_actor_t*)luaL_checkudata(L, 1, RAY_TCP_T);
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
static int tcp_getsockname(lua_State* L) {
  ray_actor_t* self = (ray_actor_t*)luaL_checkudata(L, 1, RAY_TCP_T);

  int port = 0;
  char ip[INET6_ADDRSTRLEN];
  int family;

  struct sockaddr_storage addr;
  int len = sizeof(addr);

  if (uv_tcp_getsockname(&self->h.tcp, (struct sockaddr*)&addr, &len)) {
    uv_err_t err = uv_last_error(ray_get_loop(L));
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
static int tcp_getpeername(lua_State* L) {
  ray_actor_t* self = (ray_actor_t*)luaL_checkudata(L, 1, RAY_TCP_T);

  int port = 0;
  char ip[INET6_ADDRSTRLEN];
  int family;

  struct sockaddr_storage addr;
  int len = sizeof(addr);

  if (uv_tcp_getpeername(&self->h.tcp, (struct sockaddr*)&addr, &len)) {
    uv_err_t err = uv_last_error(ray_get_loop(L));
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

static int tcp_tostring(lua_State *L) {
  ray_actor_t *self = (ray_actor_t*)luaL_checkudata(L, 1, RAY_TCP_T);
  lua_pushfstring(L, "userdata<%s>: %p", RAY_TCP_T, self);
  return 1;
}

static ray_vtable_t udp_v = {
  recv: rayM_stream_recv,
  send: rayM_stream_send,
  close: rayM_stream_close
};

static int udp_new(lua_State* L) {
  ray_actor_t* self = ray_actor_new(L, RAY_UDP_T, &udp_v);
  uv_udp_init(ray_get_loop(L), &self->h.udp);
  return 1;
}

static int udp_bind(lua_State* L) {
  ray_actor_t* self = (ray_actor_t*)luaL_checkudata(L, 1, RAY_UDP_T);
  const char*  host = luaL_checkstring(L, 2);
  int          port = luaL_checkint(L, 3);

  int flags = 0;

  struct sockaddr_in address = uv_ip4_addr(host, port);

  if (uv_udp_bind(&self->h.udp, address, flags)) {
    uv_err_t err = uv_last_error(ray_get_loop(L));
    return luaL_error(L, uv_strerror(err));
  }

  return 0;
}

static void _send_cb(uv_udp_send_t* req, int status) {
  ray_actor_t* curr = container_of(req, ray_actor_t, r);
  ray_actor_t* self = (ray_actor_t*)req->data;
  lua_settop(curr->L, 0);
  lua_pushinteger(curr->L, status);
  ray_send(curr, self);
}

static int udp_send(lua_State* L) {
  ray_actor_t* self = (ray_actor_t*)luaL_checkudata(L, 1, RAY_UDP_T);
  ray_actor_t* curr = ray_get_self(L);

  curr->r.req.data = self;

  size_t len;

  const char* host = luaL_checkstring(L, 2);
  int         port = luaL_checkint(L, 3);
  const char* mesg = luaL_checklstring(L, 4, &len);

  uv_buf_t buf = uv_buf_init((char*)mesg, len);
  struct sockaddr_in addr = uv_ip4_addr(host, port);

  if (uv_udp_send(&curr->r.udp_send, &self->h.udp, &buf, 1, addr, _send_cb)) {
    /* TODO: this shouldn't be fatal */
    uv_err_t err = uv_last_error(ray_get_loop(L));
    return luaL_error(L, uv_strerror(err));
  }

  return ray_recv(curr, self);
}

static void _recv_cb(uv_udp_t* handle, ssize_t nread, uv_buf_t buf, struct sockaddr* peer, unsigned flags) {
  ray_actor_t* self = container_of(handle, ray_actor_t, h);

  char host[INET6_ADDRSTRLEN];
  int  port = 0;

  lua_settop(self->L, 0);
  lua_pushlstring(self->L, (char*)buf.base, buf.len);

  if (peer->sa_family == PF_INET) {
    struct sockaddr_in* addr = (struct sockaddr_in*)peer;
    uv_ip4_name(addr, host, INET6_ADDRSTRLEN);
    port = addr->sin_port;
  }
  else if (peer->sa_family == PF_INET6) {
    struct sockaddr_in6* addr = (struct sockaddr_in6*)peer;
    uv_ip6_name(addr, host, INET6_ADDRSTRLEN);
    port = addr->sin6_port;
  }

  lua_pushstring(self->L, host);
  lua_pushinteger(self->L, port);

  ray_notify(self, 3);
  lua_settop(self->L, 0);
}

static int udp_recv(lua_State* L) {
  ray_actor_t* self = (ray_actor_t*)luaL_checkudata(L, 1, RAY_UDP_T);
  uv_udp_recv_start(&self->h.udp, ray_alloc_cb, _recv_cb);
  return ray_recv(ray_get_self(L), self);
}

static const char* RAY_UDP_MEMBERSHIP_OPTS[] = { "join", "leave", NULL };

static int udp_membership(lua_State* L) {
  ray_actor_t* self = (ray_actor_t*)luaL_checkudata(L, 1, RAY_UDP_T);
  const char*  iaddr = luaL_checkstring(L, 3);
  const char*  maddr = luaL_checkstring(L, 2);

  int option = luaL_checkoption(L, 4, NULL, RAY_UDP_MEMBERSHIP_OPTS);
  uv_membership membership = option ? UV_LEAVE_GROUP : UV_JOIN_GROUP;

  if (uv_udp_set_membership(&self->h.udp, maddr, iaddr, membership)) {
    uv_err_t err = uv_last_error(ray_get_loop(L));
    return luaL_error(L, uv_strerror(err));
  }

  return 0;
}

static int udp_free(lua_State *L) {
  ray_actor_t* self = (ray_actor_t*)lua_touserdata(L, 1);
  ray_close(self);
  ray_actor_free(self);
  return 1;
}
static int udp_tostring(lua_State *L) {
  ray_actor_t *self = (ray_actor_t*)luaL_checkudata(L, 1, RAY_UDP_T);
  lua_pushfstring(L, "userdata<%s>: %p", RAY_UDP_T, self);
  return 1;
}

static luaL_Reg net_funcs[] = {
  {"tcp",         tcp_new},
  {"udp",         udp_new},
  {"getaddrinfo", net_getaddrinfo},
  {NULL,          NULL}
};

static luaL_Reg tcp_meths[] = {
  {"bind",        tcp_bind},
  {"connect",     tcp_connect},
  {"getsockname", tcp_getsockname},
  {"getpeername", tcp_getpeername},
  {"keepalive",   tcp_keepalive},
  {"nodelay",     tcp_nodelay},
  {"__tostring",  tcp_tostring},
  {NULL,          NULL}
};

static luaL_Reg udp_meths[] = {
  {"bind",        udp_bind},
  {"send",        udp_send},
  {"recv",        udp_recv},
  {"membership",  udp_membership},
  {"__gc",        udp_free},
  {"__tostring",  udp_tostring},
  {NULL,          NULL}
};

LUALIB_API int luaopen_ray_net(lua_State* L) {
  rayL_module(L, "ray.net", net_funcs);

  rayL_class (L, RAY_TCP_T, ray_stream_meths);
  luaL_register(L, NULL, tcp_meths);
  lua_pop(L, 1);

  rayL_class (L, RAY_UDP_T, udp_meths);
  lua_pop(L, 1);

  ray_init_main(L);
  return 1;
}

