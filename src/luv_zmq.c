#include "zmq.h"
#include "zmq_utils.h"

#include "luv.h"

int luvL_zmq_socket_readable(void* socket) {
  zmq_pollitem_t items[1];
  items[0].socket = socket;
  items[0].events = ZMQ_POLLIN;
  return zmq_poll(items, 1, 0);
}
int luvL_zmq_socket_writable(void* socket) {
  zmq_pollitem_t items[1];
  items[0].socket = socket;
  items[0].events = ZMQ_POLLOUT;
  return zmq_poll(items, 1, 0);
}

int luvL_zmq_socket_send(luv_zmqobj_t* self, luv_state_t* state) {
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

int luvL_zmq_socket_recv(luv_zmqobj_t* self, luv_state_t* state) {
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
    lua_pushlstring(state->L, data, len);
    zmq_msg_close(&msg);
  }
  return rv;
}

void luvL_zmq_poll_recv_cb(luv_zmqobj_t* self) {
  ngx_queue_t* wrecv = ngx_queue_head(&self->wrecv);
  luv_state_t* state = ngx_queue_data(wrecv, luv_state_t, cond);

  int rv = luvL_zmq_socket_recv(self, state);
  if (rv) {
    TRACE("RECV rv => %i, error => %s\n", rv, zmq_strerror(rv));
  }
  if (rv < 0) {
    lua_settop(state->L, 0);
    lua_pushboolean(state->L, 0);
    lua_pushstring(state->L, zmq_strerror(zmq_errno()));
  }

  ngx_queue_remove(wrecv);
  luvL_state_ready(state);
}

void luvL_zmq_poll_send_cb(luv_zmqobj_t* self) {
  ngx_queue_t* wsend = ngx_queue_head(&self->wsend);
  luv_state_t* state = ngx_queue_data(wsend, luv_state_t, cond);

  int rv = luvL_zmq_socket_send(self, state);
  if (rv) {
    TRACE("SEND rv => %i, error => %s\n", rv, zmq_strerror(rv));
  }
  if (rv < 0) {
    lua_settop(state->L, 0);
    lua_pushboolean(state->L, 0);
    lua_pushstring(state->L, zmq_strerror(zmq_errno()));
  }

  ngx_queue_remove(wsend);
  luvL_state_ready(state);
}

static void _prep_cb(uv_prepare_t* handle, int status) {
  TRACE("here\n");
  luv_zmqctx_t* self = container_of(handle, luv_zmqctx_t, h);

  uv_unref((uv_handle_t*)&self->h.prepare);

  if (self->flags & LUV_ZDIRTY) {
    self->flags &= ~LUV_ZDIRTY;
    free(self->items);
    free(self->socks);
    if (self->count > 0) {
      self->items = calloc(self->count, sizeof(zmq_pollitem_t));
      self->socks = calloc(self->count, sizeof(luv_zmqobj_t*));
    }
  }

  if (ngx_queue_empty(&self->rouse)) {
    TRACE("QUEUE EMPTY: ref and return\n");
    return;
  }

  size_t seen = 0;
  ngx_queue_t*  q;
  luv_zmqobj_t* sock;
  ngx_queue_foreach(q, &self->rouse) {
    sock = ngx_queue_data(q, luv_zmqobj_t, queue);
    self->items[seen].socket = sock->data;
    self->items[seen].events = sock->poll;
    self->socks[seen] = sock;
    seen++;
  }

  TRACE("polling for %i items...\n", seen);

  size_t i;
  if (zmq_poll(self->items, seen, 0)) {
    TRACE("HAVE POLL ITEMS %i\n", seen);
    sock = self->socks[i];
    ngx_queue_remove(&sock->queue);
    for (i = 0; i < self->count; i++) {
      if (self->items[i].revents & ZMQ_POLLIN) {
        luvL_zmq_poll_recv_cb(sock);
      }
      if (self->items[i].revents & ZMQ_POLLOUT) {
        luvL_zmq_poll_send_cb(sock);
      }
      sock->poll = 0;
    }
  }
}

void _check_cb(uv_check_t* handle, int status) {
  luv_zmqctx_t* self = container_of(handle, luv_zmqctx_t, check);
  if (!ngx_queue_empty(&self->rouse)) {
    uv_ref((uv_handle_t*)&self->h.prepare);
  }
}

/* Lua API */
static int luv_new_zmq(lua_State* L) {
  luv_thread_t* thread = luvL_thread_self(L);
  int nthreads = luaL_optinteger(L, 2, 1);

  luv_zmqctx_t* self = lua_newuserdata(L, sizeof(luv_zmqctx_t));
  luaL_getmetatable(L, LUV_ZMQ_CTX_T);
  lua_setmetatable(L, -2);

  luvL_object_init((luv_state_t*)thread, (luv_object_t*)self);

  self->count = 0;
  self->data = zmq_ctx_new();
  zmq_ctx_set(self->data, ZMQ_IO_THREADS, nthreads);

  uv_prepare_init(thread->loop, &self->h.prepare);
  uv_prepare_start(&self->h.prepare, _prep_cb);
  uv_unref((uv_handle_t*)&self->h.handle);

  uv_check_init(thread->loop, &self->check);
  uv_check_start(&self->check, _check_cb);
  uv_unref((uv_handle_t*)&self->check);

  ngx_queue_init(&self->rouse);

  return 1;
}

/* socket methods */
static int luv_zmq_ctx_socket(lua_State* L) {
  luv_zmqctx_t* self = lua_touserdata(L, 1);
  int type = luaL_checkint(L, 2);

  luv_state_t*  curr = luvL_state_self(L);
  luv_zmqobj_t* sock = lua_newuserdata(L, sizeof(luv_zmqobj_t));
  luaL_getmetatable(L, LUV_ZMQ_SOCKET_T);
  lua_setmetatable(L, -2);

  luvL_object_init(curr, (luv_object_t*)sock);

  sock->data = zmq_socket(self->data, type);
  sock->ctx  = self;

  self->count++;
  self->flags |= LUV_ZDIRTY;

  ngx_queue_init(&sock->queue);
  ngx_queue_init(&sock->wsend);
  ngx_queue_init(&sock->wrecv);

  return 1;
}

static int luv_zmq_socket_bind(lua_State* L) {
  luv_zmqobj_t* self = luaL_checkudata(L, 1, LUV_ZMQ_SOCKET_T);
  const char*   addr = luaL_checkstring(L, 2);
  /* XXX: make this async? */
  int rv = zmq_bind(self->data, addr);
  lua_pushinteger(L, rv);
  return 1;
}
static int luv_zmq_socket_connect(lua_State* L) {
  luv_zmqobj_t* self = luaL_checkudata(L, 1, LUV_ZMQ_SOCKET_T);
  const char*   addr = luaL_checkstring(L, 2);
  /* XXX: make this async? */
  int rv = zmq_connect(self->data, addr);
  lua_pushinteger(L, rv);
  return 1;
}

static int luv_zmq_socket_send(lua_State* L) {
  luv_zmqobj_t* self = luaL_checkudata(L, 1, LUV_ZMQ_SOCKET_T);
  luv_state_t*  curr = luvL_state_self(L);
  int rv = luvL_zmq_socket_send(self, curr);
  if (rv < 0) {
    if (zmq_errno() == EAGAIN) {
      TRACE("EAGAIN during SEND, polling...\n");
      self->poll |= ZMQ_POLLOUT;
      luv_zmqctx_t* ctx = self->ctx;
      TRACE("enqueue...\n");
      ngx_queue_insert_tail(&ctx->rouse, &self->queue);
      TRACE("done, waiting...\n");
      return luvL_cond_wait(&self->wsend, curr);
    }
    else {
      lua_settop(L, 0);
      lua_pushboolean(L, 0);
      lua_pushstring(L, strerror(errno));
    }
  }
  return 2;
}
static int luv_zmq_socket_recv(lua_State* L) {
  luv_zmqobj_t* self = luaL_checkudata(L, 1, LUV_ZMQ_SOCKET_T);
  luv_state_t*  curr = luvL_state_self(L);
  int rv = luvL_zmq_socket_recv(self, curr);
  if (rv < 0) {
    if (zmq_errno() == EAGAIN) {
      TRACE("EAGAIN during RECV, polling..\n");
      self->poll |= ZMQ_POLLIN;
      luv_zmqctx_t* ctx = self->ctx;
      ngx_queue_insert_tail(&ctx->rouse, &self->queue);
      return luvL_cond_wait(&self->wrecv, curr);
    }
    else {
      lua_settop(L, 0);
      lua_pushboolean(L, 0);
      lua_pushstring(L, zmq_strerror(zmq_errno()));
      return 2;
    }
  }
  return 1;
}

static int luv_zmq_socket_close(lua_State* L) {
  luv_zmqobj_t* self = luaL_checkudata(L, 1, LUV_ZMQ_SOCKET_T);
  if (!luvL_object_is_closed((luv_object_t*)self)) {
    self->ctx->count--;
    self->ctx->flags |= LUV_ZDIRTY;
    if (zmq_close(self->data)) {
      /* TODO: linger and error handling */
    }
    self->flags |= LUV_OCLOSED;
  }
  return 1;
}

static const char* LUV_ZMQ_SOCKOPTS[] = {
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

static int luv_zmq_socket_setsockopt(lua_State* L) {
  luv_zmqobj_t* self = luaL_checkudata(L, 1, LUV_ZMQ_SOCKET_T);
  int opt, rv;
  if (lua_isstring(L, 2)) {
    opt = luaL_checkoption(L, 2, NULL, LUV_ZMQ_SOCKOPTS);
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
static int luv_zmq_socket_getsockopt(lua_State* L) {
  luv_zmqobj_t* self = luaL_checkudata(L, 1, LUV_ZMQ_SOCKET_T);
  size_t len;
  int opt;
  if (lua_isstring(L, 2)) {
    opt = luaL_checkoption(L, 2, NULL, LUV_ZMQ_SOCKOPTS);
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
      luv_boxpointer(L, socket);
#else
      luv_boxinteger(L, socket);
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

static int luv_zmq_socket_tostring(lua_State* L) {
  luv_zmqobj_t* self = lua_touserdata(L, 1);
  lua_pushfstring(L, "userdata<%s>: %p", LUV_ZMQ_SOCKET_T, self);
  return 1;
}
static int luv_zmq_socket_free(lua_State* L) {
  luv_zmqobj_t* self = lua_touserdata(L, 1);
  if (!luvL_object_is_closed(self)) {
    self->ctx->count--;
    self->ctx->flags |= LUV_ZDIRTY;
    zmq_close(self->data);
    self->flags |= LUV_OCLOSED;
  }
  return 1;
}

static int luv_zmq_ctx_encoder(lua_State* L) {
  luv_object_t* self = luaL_checkudata(L, 1, LUV_ZMQ_CTX_T);
  lua_pushstring(L, "luv:zmq:decoder");
  lua_pushlightuserdata(L, self->data);
  return 2;
}

int luvL_zmq_ctx_decoder(lua_State* L) {
  TRACE("ZMQ ctx decode hook\n");
  luv_state_t*  curr = luvL_state_self(L);
  luaL_checktype(L, -1, LUA_TLIGHTUSERDATA);

  luv_zmqctx_t* copy = lua_newuserdata(L, sizeof(luv_zmqctx_t));
  luaL_getmetatable(L, LUV_ZMQ_CTX_T);
  lua_setmetatable(L, -2);

  luvL_object_init(curr, (luv_object_t*)copy);

  copy->data  = lua_touserdata(L, -2);
  copy->flags = LUV_ZMQ_XDUPCTX;
  copy->count = 0;

  uv_prepare_init(luvL_event_loop(L), &copy->h.prepare);
  uv_prepare_start(&copy->h.prepare, _prep_cb);
  uv_unref((uv_handle_t*)&copy->h.handle);

  uv_check_init(luvL_event_loop(L), &copy->check);
  uv_check_start(&copy->check, _check_cb);
  uv_unref((uv_handle_t*)&copy->check);

  ngx_queue_init(&copy->rouse);

  return 1;
}

static int luv_zmq_ctx_tostring(lua_State* L) {
  luv_object_t* self = lua_touserdata(L, 1);
  lua_pushfstring(L, "userdata<%s>: %p", LUV_ZMQ_CTX_T, self);
  return 1;
}
static int luv_zmq_ctx_free(lua_State* L) {
  luv_object_t* self = luaL_checkudata(L, 1, LUV_ZMQ_CTX_T);
  if (!(self->flags & LUV_ZMQ_XDUPCTX)) {
    zmq_ctx_destroy(self->data);
  }
  self->data = NULL;
  return 1;
}

luaL_Reg luv_zmq_funcs[] = {
  {"create",    luv_new_zmq},
  {NULL,        NULL}
};

luaL_Reg luv_zmq_ctx_meths[] = {
  {"socket",    luv_zmq_ctx_socket},
  {"__codec",   luv_zmq_ctx_encoder},
  {"__gc",      luv_zmq_ctx_free},
  {"__tostring",luv_zmq_ctx_tostring},
  {NULL,        NULL}
};

luaL_Reg luv_zmq_socket_meths[] = {
  {"bind",      luv_zmq_socket_bind},
  {"connect",   luv_zmq_socket_connect},
  {"send",      luv_zmq_socket_send},
  {"recv",      luv_zmq_socket_recv},
  {"close",     luv_zmq_socket_close},
  {"getsockopt",luv_zmq_socket_getsockopt},
  {"setsockopt",luv_zmq_socket_setsockopt},
  {"__gc",      luv_zmq_socket_free},
  {"__tostring",luv_zmq_socket_tostring},
  {NULL,        NULL}
};


