#include "zmq.h"
#include "zmq_utils.h"

#include "ray_common.h"
#include "ray_zmq.h"

int rayL_zmq_socket_readable(void* socket) {
  zmq_pollitem_t items[1];
  items[0].socket = socket;
  items[0].events = ZMQ_POLLIN;
  return zmq_poll(items, 1, 0);
}
int rayL_zmq_socket_writable(void* socket) {
  zmq_pollitem_t items[1];
  items[0].socket = socket;
  items[0].events = ZMQ_POLLOUT;
  return zmq_poll(items, 1, 0);
}

int rayL_zmq_socket_send(ray_object_t* self, ray_state_t* state) {
  size_t    len;
  zmq_msg_t msg;

  const char* data = luaL_checklstring(state->L, 2, &len);
  if (zmq_msg_init_size(&msg, len)) {
    /* ENOMEM */
    return luaL_error(state->L, strerror(errno));
  }

  memcpy(zmq_msg_data(&msg), data, len);
  int rv = zmq_msg_send(&msg, self->data, ZMQ_DONTWAIT);
  zmq_msg_close(&msg);

  return rv;
}

int rayL_zmq_socket_recv(ray_object_t* self, ray_state_t* state) {
  zmq_msg_t msg;
  zmq_msg_init(&msg);

  int rv = zmq_msg_recv(&msg, self->data, ZMQ_DONTWAIT);
  if (rv < 0) {
    zmq_msg_close(&msg);
  }
  else {
    void* data = zmq_msg_data(&msg);
    size_t len = zmq_msg_size(&msg);
    lua_settop(state->L, 0);
    lua_pushlstring(state->L, (const char*)data, len);
    zmq_msg_close(&msg);
  }
  return rv;
}

static void _zmq_poll_cb(uv_poll_t* handle, int status, int events) {
  ray_object_t* self = container_of(handle, ray_object_t, h);

  if (self->flags & RAY_ZMQ_WRECV) {
    int readable = rayL_zmq_socket_readable(self->data);
    if (!readable) goto wsend;

    self->flags &= ~RAY_ZMQ_WRECV;

    ngx_queue_t* queue = ngx_queue_head(&self->rouse);
    ray_state_t* state = ngx_queue_data(queue, ray_state_t, cond);
    ngx_queue_remove(queue);

    if (readable < 0) {
      lua_settop(state->L, 0);
      lua_pushboolean(state->L, 0);
      lua_pushstring(state->L, strerror(errno));
    }
    else if (readable > 0) {
      int rv = rayL_zmq_socket_recv(self, state);
      if (rv < 0) {
        if (!(errno == EAGAIN || errno == EWOULDBLOCK)) {
          lua_settop(state->L, 0);
          lua_pushboolean(state->L, 0);
          lua_pushstring(state->L, strerror(errno));
        }
      }
    }
    rayL_state_ready(state);
    return;
  }

  wsend:
  if (self->flags & RAY_ZMQ_WSEND) {
    int writable = rayL_zmq_socket_writable(self->data);
    if (!writable) return;

    self->flags &= ~RAY_ZMQ_WSEND;

    ngx_queue_t* queue = ngx_queue_head(&self->queue);
    ray_state_t* state = ngx_queue_data(queue, ray_state_t, cond);
    ngx_queue_remove(queue);

    if (writable < 0) {
      lua_settop(state->L, 0);
      lua_pushboolean(state->L, 0);
      lua_pushstring(state->L, zmq_strerror(zmq_errno()));
    }
    else if (writable > 0) {
      int rv = rayL_zmq_socket_send(self, state);
      if (rv < 0) {
        lua_settop(state->L, 0);
        lua_pushboolean(state->L, 0);
        lua_pushstring(state->L, zmq_strerror(zmq_errno()));
      }
    }
    rayL_state_ready(state);
    return;
  }
}


/* Lua API */
static int ray_zmq_new(lua_State* L) {
  ray_thread_t* thread = rayL_thread_self(L);
  int nthreads = luaL_optinteger(L, 2, 1);

  ray_object_t* self = (ray_object_t*)lua_newuserdata(L, sizeof(ray_object_t));
  luaL_getmetatable(L, RAY_ZMQ_CTX_T);
  lua_setmetatable(L, -2);

  rayL_object_init((ray_state_t*)thread, self);

  self->data = zmq_ctx_new();
  zmq_ctx_set(self->data, ZMQ_IO_THREADS, nthreads);

  return 1;
}

/* socket methods */
static int ray_zmq_ctx_socket(lua_State* L) {
  ray_object_t* ctx = (ray_object_t*)lua_touserdata(L, 1);
  int type = luaL_checkint(L, 2);

  ray_state_t*  curr = rayL_state_self(L);
  ray_object_t* self = (ray_object_t*)lua_newuserdata(L, sizeof(ray_object_t));
  luaL_getmetatable(L, RAY_ZMQ_SOCKET_T);
  lua_setmetatable(L, -2);

  rayL_object_init(curr, self);

  self->data = zmq_socket(ctx->data, type);

  uv_os_sock_t socket;
  size_t len = sizeof(uv_os_sock_t);
  zmq_getsockopt(self->data, ZMQ_FD, &socket, &len);

  uv_poll_init_socket(rayL_event_loop(L), &self->h.poll, socket);
  uv_poll_start(&self->h.poll, UV_READABLE, _zmq_poll_cb);

  return 1;
}

static int ray_zmq_socket_bind(lua_State* L) {
  ray_object_t* self = (ray_object_t*)luaL_checkudata(L, 1, RAY_ZMQ_SOCKET_T);
  const char*   addr = luaL_checkstring(L, 2);
  /* XXX: make this async? */
  int rv = zmq_bind(self->data, addr);
  lua_pushinteger(L, rv);
  return 1;
}
static int ray_zmq_socket_connect(lua_State* L) {
  ray_object_t* self = (ray_object_t*)luaL_checkudata(L, 1, RAY_ZMQ_SOCKET_T);
  const char*   addr = luaL_checkstring(L, 2);
  /* XXX: make this async? */
  int rv = zmq_connect(self->data, addr);
  lua_pushinteger(L, rv);
  return 1;
}

static int ray_zmq_socket_send(lua_State* L) {
  ray_object_t* self = (ray_object_t*)luaL_checkudata(L, 1, RAY_ZMQ_SOCKET_T);
  ray_state_t*  curr = rayL_state_self(L);
  int rv = rayL_zmq_socket_send(self, curr);
  if (rv < 0) {
    int err = zmq_errno();
    if (err == EAGAIN || err == EWOULDBLOCK) {
      TRACE("EAGAIN during SEND, polling...\n");
      self->flags |= RAY_ZMQ_WSEND;
      return rayL_cond_wait(&self->queue, curr);
    }
    else {
      lua_settop(L, 0);
      lua_pushboolean(L, 0);
      lua_pushstring(L, strerror(errno));
    }
  }
  return 2;
}
static int ray_zmq_socket_recv(lua_State* L) {
  ray_object_t* self = (ray_object_t*)luaL_checkudata(L, 1, RAY_ZMQ_SOCKET_T);
  ray_state_t*  curr = rayL_state_self(L);
  int rv = rayL_zmq_socket_recv(self, curr);
  if (rv < 0) {
    int err = zmq_errno();
    if (err == EAGAIN || err == EWOULDBLOCK) {
      TRACE("EAGAIN during RECV, polling..\n");
      self->flags |= RAY_ZMQ_WRECV;
      return rayL_cond_wait(&self->rouse, curr);
    }
    else {
      lua_settop(L, 0);
      lua_pushboolean(L, 0);
      lua_pushstring(L, zmq_strerror(err));
      return 2;
    }
  }
  return 1;
}

static int ray_zmq_socket_close(lua_State* L) {
  ray_object_t* self = (ray_object_t*)luaL_checkudata(L, 1, RAY_ZMQ_SOCKET_T);
  if (!rayL_object_is_closing(self)) {
    if (zmq_close(self->data)) {
      /* TODO: linger and error handling */
    }
    uv_poll_stop(&self->h.poll);
    rayL_object_close(self);
  }
  return 1;
}

static const char* RAY_ZMQ_SOCKOPTS[] = {
  "",                     /* 0 */
  "",                     /* 1 */
  "",                     /* 2 */
  "",                     /* 3 */
  "AFFINITY",             /* 4 */
  "IDENTITY",             /* 5 */
  "SUBSCRIBE",            /* 6 */
  "UNSUBSCRIBE",          /* 7 */
  "RATE",                 /* 8 */
  "RECOVERY_IVL",         /* 9 */
  "",                     /* 3 */
  "SNDBUF",               /* 11 */
  "RCVBUF",               /* 12 */
  "RCVMORE",              /* 13 */
  "FD",                   /* 14 */
  "EVENTS",               /* 15 */
  "TYPE",                 /* 16 */
  "LINGER",               /* 17 */
  "RECONNECT_IVL",        /* 18 */
  "BACKLOG",              /* 19 */
  "",                     /* 20 */
  "RECONNECT_IVL_MAX",    /* 21 */
  "MAXMSGSIZE",           /* 22 */
  "SNDHWM",               /* 23 */
  "RCVHWM",               /* 24 */
  "MULTICAST_HOPS",       /* 25 */
  "",                     /* 26 */
  "RCVTIMEO",             /* 27 */
  "SNDTIMEO",             /* 28 */
  "",                     /* 29 */
  "",                     /* 30 */
  "IPV4ONLY",             /* 31 */
  "LAST_ENDPOINT",        /* 32 */
  "ROUTER_BEHAVIOR",      /* 33 */
  "TCP_KEEPALIVE",        /* 34 */
  "TCP_KEEPALIVE_CNT",    /* 35 */
  "TCP_KEEPALIVE_IDLE",   /* 36 */
  "TCP_KEEPALIVE_INTVL",  /* 37 */
  "TCP_ACCEPT_FILTER",    /* 38 */
  NULL
};

static int ray_zmq_socket_setsockopt(lua_State* L) {
  ray_object_t* self = (ray_object_t*)luaL_checkudata(L, 1, RAY_ZMQ_SOCKET_T);
  int opt, rv;
  if (lua_type(L, 2) == LUA_TSTRING) {
    opt = luaL_checkoption(L, 2, NULL, RAY_ZMQ_SOCKOPTS);
  }
  else {
    opt = luaL_checkint(L, 2);
  }
  switch (opt) {
    case ZMQ_SNDHWM:
    case ZMQ_RCVHWM:
    case ZMQ_RATE:
    case ZMQ_RECOVERY_IVL:
    case ZMQ_SNDBUF:
    case ZMQ_RCVBUF:
    case ZMQ_LINGER:
    case ZMQ_RECONNECT_IVL:
    case ZMQ_RECONNECT_IVL_MAX:
    case ZMQ_BACKLOG:
    case ZMQ_MULTICAST_HOPS:
    case ZMQ_RCVTIMEO:
    case ZMQ_SNDTIMEO:
    case ZMQ_ROUTER_BEHAVIOR:
    case ZMQ_TCP_KEEPALIVE:
    case ZMQ_TCP_KEEPALIVE_CNT:
    case ZMQ_TCP_KEEPALIVE_IDLE:
    case ZMQ_TCP_KEEPALIVE_INTVL:
    {
      int val = lua_tointeger(L, 2);
      rv = zmq_setsockopt(self->data, opt, &val, sizeof(val));
      break;
    }

    case ZMQ_AFFINITY:
    {
      uint64_t val = (uint64_t)lua_tointeger(L, 2);
      rv = zmq_setsockopt(self->data, opt, &val, sizeof(val));
      break;
    }

    case ZMQ_MAXMSGSIZE:
    {
      int64_t val = (int64_t)lua_tointeger(L, 2);
      rv = zmq_setsockopt(self->data, opt, &val, sizeof(val));
      break;
    }

    case ZMQ_IPV4ONLY:
    {
      int val = lua_toboolean(L, 2);
      rv = zmq_setsockopt(self->data, opt, &val, sizeof(val));
      break;
    }

    case ZMQ_IDENTITY:
    case ZMQ_SUBSCRIBE:
    case ZMQ_UNSUBSCRIBE:
    case ZMQ_TCP_ACCEPT_FILTER:
    {
      size_t len;
      const char* val = lua_tolstring(L, 2, &len);
      rv = zmq_setsockopt(self->data, opt, &val, len);
      break;
    }

    case ZMQ_RCVMORE:
    case ZMQ_FD:
    case ZMQ_EVENTS:
    case ZMQ_TYPE:
    case ZMQ_LAST_ENDPOINT:
      return luaL_error(L, "readonly option");
    default:
      return luaL_error(L, "invalid option");
  }
  if (rv < 0) {
    lua_pushboolean(L, 0);
    lua_pushstring(L, zmq_strerror(zmq_errno()));
    return 2;
  }
  lua_pushboolean(L, 1);
  return 1;
}
static int ray_zmq_socket_getsockopt(lua_State* L) {
  ray_object_t* self = (ray_object_t*)luaL_checkudata(L, 1, RAY_ZMQ_SOCKET_T);
  size_t len;
  int opt;
  if (lua_type(L, 2) == LUA_TSTRING) {
    opt = luaL_checkoption(L, 2, NULL, RAY_ZMQ_SOCKOPTS);
  }
  else {
    opt = luaL_checkint(L, 2);
  }
  switch (opt) {
    case ZMQ_TYPE:
    case ZMQ_RCVMORE:
    case ZMQ_SNDHWM:
    case ZMQ_RCVHWM:
    case ZMQ_RATE:
    case ZMQ_RECOVERY_IVL:
    case ZMQ_SNDBUF:
    case ZMQ_RCVBUF:
    case ZMQ_LINGER:
    case ZMQ_RECONNECT_IVL:
    case ZMQ_RECONNECT_IVL_MAX:
    case ZMQ_BACKLOG:
    case ZMQ_MULTICAST_HOPS:
    case ZMQ_RCVTIMEO:
    case ZMQ_SNDTIMEO:
    case ZMQ_ROUTER_BEHAVIOR:
    case ZMQ_TCP_KEEPALIVE:
    case ZMQ_TCP_KEEPALIVE_CNT:
    case ZMQ_TCP_KEEPALIVE_IDLE:
    case ZMQ_TCP_KEEPALIVE_INTVL:
    case ZMQ_EVENTS:
    {
      int val;
      len = sizeof(val);
      zmq_getsockopt(self->data, opt, &val, &len);
      lua_pushinteger(L, val);
      break;
    }

    case ZMQ_AFFINITY:
    {
      uint64_t val = (uint64_t)lua_tointeger(L, 2);
      len = sizeof(val);
      zmq_getsockopt(self->data, opt, &val, &len);
      lua_pushinteger(L, (lua_Integer)val);
      break;
    }

    case ZMQ_MAXMSGSIZE:
    {
      int64_t val = (int64_t)lua_tointeger(L, 2);
      len = sizeof(val);
      zmq_getsockopt(self->data, opt, &val, &len);
      lua_pushinteger(L, (lua_Integer)val);
      break;
    }

    case ZMQ_IPV4ONLY:
    {
      int val = lua_toboolean(L, 2);
      len = sizeof(val);
      zmq_getsockopt(self->data, opt, &val, &len);
      lua_pushboolean(L, val);
      break;
    }

    case ZMQ_IDENTITY:
    case ZMQ_LAST_ENDPOINT:
    {
      char val[1024];
      len = sizeof(val);
      zmq_getsockopt(self->data, opt, val, &len);
      lua_pushlstring(L, val, len);
      break;
    }

    case ZMQ_FD:
    {
      uv_os_sock_t socket;
      len = sizeof(uv_os_sock_t);
      zmq_getsockopt(self->data, ZMQ_FD, &socket, &len);
      /* TODO: give these a metatable */
#ifdef _WIN32
      ray_boxpointer(L, (uv_os_sock_t)socket);
#else
      ray_boxinteger(L, socket);
#endif
    }

    case ZMQ_SUBSCRIBE:
    case ZMQ_UNSUBSCRIBE:
    case ZMQ_TCP_ACCEPT_FILTER:
      return luaL_error(L, "writeonly option");
    default:
      return luaL_error(L, "invalid option");
  }
  return 1;
}

static int ray_zmq_socket_tostring(lua_State* L) {
  ray_object_t* self = (ray_object_t*)lua_touserdata(L, 1);
  lua_pushfstring(L, "userdata<%s>: %p", RAY_ZMQ_SOCKET_T, self);
  return 1;
}
static int ray_zmq_socket_free(lua_State* L) {
  ray_object_t* self = (ray_object_t*)lua_touserdata(L, 1);
  if (!rayL_object_is_closing(self)) {
    zmq_close(self->data);
    uv_poll_stop(&self->h.poll);
    rayL_object_close(self);
  }
  return 1;
}

static int ray_zmq_ctx_encoder(lua_State* L) {
  ray_object_t* self = (ray_object_t*)luaL_checkudata(L, 1, RAY_ZMQ_CTX_T);
  lua_pushstring(L, "ray:zmq:decoder");
  lua_pushlightuserdata(L, self->data);
  return 2;
}

int rayL_zmq_ctx_decoder(lua_State* L) {
  TRACE("ZMQ ctx decode hook\n");
  ray_state_t*  curr = rayL_state_self(L);
  luaL_checktype(L, -1, LUA_TLIGHTUSERDATA);

  ray_object_t* copy = (ray_object_t*)lua_newuserdata(L, sizeof(ray_object_t));
  luaL_getmetatable(L, RAY_ZMQ_CTX_T);
  lua_setmetatable(L, -2);

  rayL_object_init(curr, copy);

  copy->data  = lua_touserdata(L, -2);
  copy->flags = RAY_ZMQ_XDUPCTX;

  return 1;
}

static int ray_zmq_ctx_tostring(lua_State* L) {
  ray_object_t* self = (ray_object_t*)lua_touserdata(L, 1);
  lua_pushfstring(L, "userdata<%s>: %p", RAY_ZMQ_CTX_T, self);
  return 1;
}
static int ray_zmq_ctx_free(lua_State* L) {
  ray_object_t* self = (ray_object_t*)luaL_checkudata(L, 1, RAY_ZMQ_CTX_T);
  if (!(self->flags & RAY_ZMQ_XDUPCTX)) {
    zmq_ctx_destroy(self->data);
  }
  self->data = NULL;
  return 1;
}

luaL_Reg ray_zmq_funcs[] = {
  {"create",    ray_zmq_new},
  {NULL,        NULL}
};

luaL_Reg ray_zmq_ctx_meths[] = {
  {"socket",    ray_zmq_ctx_socket},
  {"__codec",   ray_zmq_ctx_encoder},
  {"__gc",      ray_zmq_ctx_free},
  {"__tostring",ray_zmq_ctx_tostring},
  {NULL,        NULL}
};

luaL_Reg ray_zmq_socket_meths[] = {
  {"bind",      ray_zmq_socket_bind},
  {"connect",   ray_zmq_socket_connect},
  {"send",      ray_zmq_socket_send},
  {"recv",      ray_zmq_socket_recv},
  {"close",     ray_zmq_socket_close},
  {"getsockopt",ray_zmq_socket_getsockopt},
  {"setsockopt",ray_zmq_socket_setsockopt},
  {"__gc",      ray_zmq_socket_free},
  {"__tostring",ray_zmq_socket_tostring},
  {NULL,        NULL}
};


