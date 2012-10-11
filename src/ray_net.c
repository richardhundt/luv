#include "ray.h"
#include <string.h>

static int ray_new_tcp(lua_State* L) {
  ray_state_t*  curr = rayL_state_self(L);
  ray_object_t* self = (ray_object_t*)lua_newuserdata(L, sizeof(ray_object_t));
  luaL_getmetatable(L, RAY_NET_TCP_T);
  lua_setmetatable(L, -2);

  rayL_object_init(curr, self);

  uv_tcp_init(rayL_event_loop(L), &self->h.tcp);
  return 1;
}

static void _getaddrinfo_cb(uv_getaddrinfo_t* req, int s, struct addrinfo* ai) {
  ray_state_t* curr = container_of(req, ray_state_t, req);
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
  lua_settop(curr->L, 0);
  lua_pushstring(curr->L, host);
  lua_pushinteger(curr->L, port);

  uv_freeaddrinfo(ai);

  rayL_state_ready(curr);
}

static int ray_getaddrinfo(lua_State* L) {
  ray_state_t* curr = rayL_state_self(L);
  uv_loop_t*   loop = rayL_event_loop(L);
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

  return rayL_state_suspend(curr);
}

static int ray_tcp_bind(lua_State* L) {
  ray_object_t *self = (ray_object_t*)luaL_checkudata(L, 1, RAY_NET_TCP_T);

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

static int ray_tcp_connect(lua_State *L) {
  ray_object_t* self = (ray_object_t*)luaL_checkudata(L, 1, RAY_NET_TCP_T);
  ray_state_t*  curr = rayL_state_self(L);

  struct sockaddr_in addr;
  const char* host;
  int port, rv;

  host = luaL_checkstring(L, 2);
  port = luaL_checkint(L, 3);
  addr = uv_ip4_addr(host, port);

  lua_settop(L, 2);

  rv = uv_tcp_connect(&curr->req.connect, &self->h.tcp, addr, rayL_connect_cb);
  if (rv) {
    uv_err_t err = uv_last_error(self->h.handle.loop);
    lua_settop(L, 0);
    lua_pushnil(L);
    lua_pushstring(L, uv_strerror(err));
    return 2;
  }

  return rayL_state_suspend(curr);
}

static int ray_tcp_nodelay(lua_State* L) {
  ray_object_t* self = (ray_object_t*)luaL_checkudata(L, 1, RAY_NET_TCP_T);
  luaL_checktype(L, 2, LUA_TBOOLEAN);
  int enable = lua_toboolean(L, 2);
  lua_settop(L, 2);
  int rv = uv_tcp_nodelay(&self->h.tcp, enable);
  lua_pushinteger(L, rv);
  return 1;
}
static int ray_tcp_keepalive(lua_State* L) {
  ray_object_t* self = (ray_object_t*)luaL_checkudata(L, 1, RAY_NET_TCP_T);
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
static int ray_tcp_getsockname(lua_State* L) {
  ray_object_t* self = (ray_object_t*)luaL_checkudata(L, 1, RAY_NET_TCP_T);

  int port = 0;
  char ip[INET6_ADDRSTRLEN];
  int family;

  struct sockaddr_storage addr;
  int len = sizeof(addr);

  if (uv_tcp_getsockname(&self->h.tcp, (struct sockaddr*)&addr, &len)) {
    uv_err_t err = uv_last_error(rayL_event_loop(L));
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
static int ray_tcp_getpeername(lua_State* L) {
  ray_object_t* self = (ray_object_t*)luaL_checkudata(L, 1, RAY_NET_TCP_T);

  int port = 0;
  char ip[INET6_ADDRSTRLEN];
  int family;

  struct sockaddr_storage addr;
  int len = sizeof(addr);

  if (uv_tcp_getpeername(&self->h.tcp, (struct sockaddr*)&addr, &len)) {
    uv_err_t err = uv_last_error(rayL_event_loop(L));
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

static int ray_tcp_tostring(lua_State *L) {
  ray_object_t *self = (ray_object_t*)luaL_checkudata(L, 1, RAY_NET_TCP_T);
  lua_pushfstring(L, "userdata<%s>: %p", RAY_NET_TCP_T, self);
  return 1;
}

static int ray_new_udp(lua_State* L) {
  ray_state_t*  curr = rayL_state_self(L);
  ray_object_t* self = (ray_object_t*)lua_newuserdata(L, sizeof(ray_object_t));
  luaL_getmetatable(L, RAY_NET_UDP_T);
  lua_setmetatable(L, -2);
  rayL_object_init(curr, self);

  uv_udp_init(rayL_event_loop(L), &self->h.udp);
  return 1;
}

static int ray_udp_bind(lua_State* L) {
  ray_object_t* self = (ray_object_t*)luaL_checkudata(L, 1, RAY_NET_UDP_T);
  const char*   host = luaL_checkstring(L, 2);
  int           port = luaL_checkint(L, 3);

  int flags = 0;

  struct sockaddr_in address = uv_ip4_addr(host, port);

  if (uv_udp_bind(&self->h.udp, address, flags)) {
    uv_err_t err = uv_last_error(rayL_event_loop(L));
    return luaL_error(L, uv_strerror(err));
  }

  return 0;
}

static void _send_cb(uv_udp_send_t* req, int status) {
  ray_state_t* curr = container_of(req, ray_state_t, req);
  rayL_state_ready(curr);
}

static int ray_udp_send(lua_State* L) {
  ray_object_t* self = (ray_object_t*)luaL_checkudata(L, 1, RAY_NET_UDP_T);
  ray_state_t*  curr = rayL_state_self(L);

  size_t len;

  const char* host = luaL_checkstring(L, 2);
  int         port = luaL_checkint(L, 3);
  const char* mesg = luaL_checklstring(L, 4, &len);

  uv_buf_t buf = uv_buf_init((char*)mesg, len);
  struct sockaddr_in addr = uv_ip4_addr(host, port);

  if (uv_udp_send(&curr->req.udp_send, &self->h.udp, &buf, 1, addr, _send_cb)) {
    /* TODO: this shouldn't be fatal */
    uv_err_t err = uv_last_error(rayL_event_loop(L));
    return luaL_error(L, uv_strerror(err));
  }

  return rayL_state_suspend(curr);
}

static void _recv_cb(uv_udp_t* handle, ssize_t nread, uv_buf_t buf, struct sockaddr* peer, unsigned flags) {
  ray_object_t* self = container_of(handle, ray_object_t, h);
  ngx_queue_t* q;
  ray_state_t* s;

  char host[INET6_ADDRSTRLEN];
  int  port = 0;

  ngx_queue_foreach(q, &self->rouse) {
    s = ngx_queue_data(q, ray_state_t, cond);

    lua_settop(s->L, 0);
    lua_pushlstring(s->L, buf.base, buf.len);

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

    lua_pushstring(s->L, host);
    lua_pushinteger(s->L, port);
    /* [ mesg, host, port ] */
  }
  rayL_cond_signal(&self->rouse);
}

static int ray_udp_recv(lua_State* L) {
  ray_object_t* self = (ray_object_t*)luaL_checkudata(L, 1, RAY_NET_UDP_T);
  if (!rayL_object_is_started(self)) {
    self->flags |= RAY_OSTARTED;
    uv_udp_recv_start(&self->h.udp, rayL_alloc_cb, _recv_cb);
  }
  return rayL_cond_wait(&self->rouse, rayL_state_self(L));
}

static const char* RAY_UDP_MEMBERSHIP_OPTS[] = { "join", "leave", NULL };

int ray_udp_membership(lua_State* L) {
  ray_object_t* self = (ray_object_t*)luaL_checkudata(L, 1, RAY_NET_UDP_T);
  const char*  iaddr = luaL_checkstring(L, 3);
  const char*  maddr = luaL_checkstring(L, 2);

  int option = luaL_checkoption(L, 4, NULL, RAY_UDP_MEMBERSHIP_OPTS);
  uv_membership membership = option ? UV_LEAVE_GROUP : UV_JOIN_GROUP;

  if (uv_udp_set_membership(&self->h.udp, maddr, iaddr, membership)) {
    uv_err_t err = uv_last_error(rayL_event_loop(L));
    return luaL_error(L, uv_strerror(err));
  }

  return 0;
}

static int ray_udp_free(lua_State *L) {
  ray_object_t* self = (ray_object_t*)lua_touserdata(L, 1);
  rayL_object_close(self);
  return 1;
}
static int ray_udp_tostring(lua_State *L) {
  ray_object_t *self = (ray_object_t*)luaL_checkudata(L, 1, RAY_NET_UDP_T);
  lua_pushfstring(L, "userdata<%s>: %p", RAY_NET_UDP_T, self);
  return 1;
}

luaL_Reg ray_net_funcs[] = {
  {"tcp",         ray_new_tcp},
  {"udp",         ray_new_udp},
  {"getaddrinfo", ray_getaddrinfo},
  {NULL,          NULL}
};

luaL_Reg ray_net_tcp_meths[] = {
  {"bind",        ray_tcp_bind},
  {"connect",     ray_tcp_connect},
  {"getsockname", ray_tcp_getsockname},
  {"getpeername", ray_tcp_getpeername},
  {"keepalive",   ray_tcp_keepalive},
  {"nodelay",     ray_tcp_nodelay},
  {"__tostring",  ray_tcp_tostring},
  {NULL,          NULL}
};

luaL_Reg ray_net_udp_meths[] = {
  {"bind",      ray_udp_bind},
  {"send",      ray_udp_send},
  {"recv",      ray_udp_recv},
  {"membership",ray_udp_membership},
  {"__gc",      ray_udp_free},
  {"__tostring",ray_udp_tostring},
  {NULL,        NULL}
};

