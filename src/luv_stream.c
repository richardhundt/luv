#include "luv.h"

/* used by udp and stream */
uv_buf_t luvL_alloc_cb(uv_handle_t* handle, size_t size) {
  luv_object_t* self = container_of(handle, luv_object_t, h);
  size = (size_t)self->buf.len;
  return uv_buf_init((char*)malloc(size), size);
}

/* used by tcp and pipe */
void luvL_connect_cb(uv_connect_t* req, int status) {
  luv_state_t* state = container_of(req, luv_state_t, req);
  luvL_state_ready(state);
}

static void _read_cb(uv_stream_t* stream, ssize_t len, uv_buf_t buf) {
  TRACE("got data\n");
  luv_object_t* self  = container_of(stream, luv_object_t, h);

  if (ngx_queue_empty(&self->rouse)) {
    TRACE("empty read queue, save buffer and stop read\n");
    luvL_stream_stop(self);
    if (len >= 0) {
      self->buf   = buf;
      self->count = len;
    }
    else {
      if (buf.base) {
        free(buf.base);
        buf.base = NULL;
        buf.len  = 0;
      }
    }
  }
  else {
    TRACE("have states waiting...\n");
    ngx_queue_t* q;
    luv_state_t* s;

    q = ngx_queue_head(&self->rouse);
    s = ngx_queue_data(q, luv_state_t, cond);
    ngx_queue_remove(q);

    TRACE("data - len: %i\n", (int)len);

    lua_settop(s->L, 0);
    if (len >= 0) {
      lua_pushinteger(s->L, len);
      lua_pushlstring(s->L, (char*)buf.base, len);
      if (len == 0) luvL_stream_stop(self);
    }
    else {
      uv_err_t err = uv_last_error(s->loop);
      if (err.code == UV_EOF) {
        TRACE("GOT EOF\n");
        lua_settop(s->L, 0);
        lua_pushnil(s->L);
      }
      else {
        lua_settop(s->L, 0);
        lua_pushboolean(s->L, 0);
        lua_pushfstring(s->L, "read: %s", uv_strerror(err));
        TRACE("READ ERROR, CLOSING STREAM\n");
        luvL_stream_stop(self);
        luvL_object_close(self);
      }
    }
    if (buf.base) {
      free(buf.base);
      buf.len  = 0;
      buf.base = NULL;
    }
    TRACE("wake up state: %p\n", s);
    luvL_state_ready(s);
  }
}

static void _write_cb(uv_write_t* req, int status) {
  luv_state_t* rouse = container_of(req, luv_state_t, req);
  lua_settop(rouse->L, 0);
  lua_pushinteger(rouse->L, status);
  TRACE("write_cb - wake up: %p\n", rouse);
  luvL_state_ready(rouse);
}

static void _shutdown_cb(uv_shutdown_t* req, int status) {
  luv_object_t* self = container_of(req->handle, luv_object_t, h);
  luv_state_t* state = container_of(req, luv_state_t, req);
  lua_settop(state->L, 0);
  lua_pushinteger(state->L, status);
  luvL_cond_signal(&self->rouse);
}

static void _listen_cb(uv_stream_t* server, int status) {
  TRACE("got client connection...\n");
  luv_object_t* self = container_of(server, luv_object_t, h);
  if (luvL_object_is_waiting(self)) {
    ngx_queue_t* q = ngx_queue_head(&self->rouse);
    luv_state_t* s = ngx_queue_data(q, luv_state_t, cond);
    lua_State* L = s->L;

    TRACE("is waiting..., lua_State*: %p\n", L);
    luaL_checktype(L, 2, LUA_TUSERDATA);
    luv_object_t* conn = (luv_object_t*)lua_touserdata(L, 2);
    TRACE("got client conn: %p\n", conn);
    luvL_object_init(s, conn);
    int rv = uv_accept(&self->h.stream, &conn->h.stream);
    TRACE("accept returned ok\n");
    if (rv) {
      uv_err_t err = uv_last_error(self->h.stream.loop);
      TRACE("ERROR: %s\n", uv_strerror(err));
      lua_settop(L, 0);
      lua_pushnil(L);
      lua_pushstring(L, uv_strerror(err));
    }
    self->flags &= ~LUV_OWAITING;
    luvL_cond_signal(&self->rouse);
  }
  else {
    TRACE("increment backlog count\n");
    self->count++;
  }
}

static int luv_stream_listen(lua_State* L) {
  luaL_checktype(L, 1, LUA_TUSERDATA);
  luv_object_t* self = (luv_object_t*)lua_touserdata(L, 1);
  int backlog = luaL_optinteger(L, 2, 128);
  if (uv_listen(&self->h.stream, backlog, _listen_cb)) {
    uv_err_t err = uv_last_error(self->h.stream.loop);
    TRACE("listen error\n");
    return luaL_error(L, "listen: %s", uv_strerror(err));
  }
  return 0;
}

static int luv_stream_accept(lua_State *L) {
  luaL_checktype(L, 1, LUA_TUSERDATA);
  luv_object_t* self = (luv_object_t*)lua_touserdata(L, 1);
  luv_object_t* conn = (luv_object_t*)lua_touserdata(L, 2);

  luv_state_t* curr = luvL_state_self(L);

  if (self->count) {
    self->count--;
    int rv = uv_accept(&self->h.stream, &conn->h.stream);
    if (rv) {
      uv_err_t err = uv_last_error(self->h.stream.loop);
      lua_settop(L, 0);
      lua_pushnil(L);
      lua_pushstring(L, uv_strerror(err));
      return 2;
    }
    return 1;
  }
  self->flags |= LUV_OWAITING;
  return luvL_cond_wait(&self->rouse, curr);
}

int luvL_stream_start(luv_object_t* self) {
  if (!luvL_object_is_started(self)) {
    self->flags |= LUV_OSTARTED;
    return uv_read_start(&self->h.stream, luvL_alloc_cb, _read_cb);
  }
  return 0;
}
int luvL_stream_stop(luv_object_t* self) {
  if (luvL_object_is_started(self)) {
    self->flags &= ~LUV_OSTARTED;
    return uv_read_stop(&self->h.stream);
  }
  return 0;
}


#define STREAM_ERROR(L,fmt,loop) do { \
  uv_err_t err = uv_last_error(loop); \
  lua_settop(L, 0); \
  lua_pushboolean(L, 0); \
  lua_pushfstring(L, fmt, uv_strerror(err)); \
  TRACE("STREAM ERROR: %s\n", lua_tostring(L, -1)); \
} while (0)

static int luv_stream_start(lua_State* L) {
  luv_object_t* self = (luv_object_t*)lua_touserdata(L, 1);
  if (luvL_stream_start(self)) {
    luvL_stream_stop(self);
    luvL_object_close(self);
    STREAM_ERROR(L, "read start: %s", luvL_event_loop(L));
    return 2;
  }
  lua_pushboolean(L, 1);
  return 1;
}

static int luv_stream_stop(lua_State* L) {
  luv_object_t* self = (luv_object_t*)lua_touserdata(L, 1);
  if (luvL_stream_stop(self)) {
    STREAM_ERROR(L, "read stop: %s", luvL_event_loop(L));
    return 2;
  }
  lua_pushboolean(L, 1);
  return 1;
}

static int luv_stream_read(lua_State* L) {
  luv_object_t* self = (luv_object_t*)lua_touserdata(L, 1);
  luv_state_t*  curr = luvL_state_self(L);
  int len = luaL_optinteger(L, 2, 4096);
  if (luvL_object_is_closing(self)) {
    TRACE("error: reading from closed stream\n");
    lua_pushnil(L);
    lua_pushstring(L, "attempt to read from a closed stream");
    return 2;
  }
  if (self->buf.base) {
    /* we have a buffer use that */
    TRACE("have pending data\n");
    lua_pushinteger(L, self->count);
    lua_pushlstring(L, (char*)self->buf.base, self->count);
    free(self->buf.base);
    self->buf.base = NULL;
    self->buf.len  = 0;
    self->count    = 0;
    return 2;
  }
  self->buf.len = len;
  if (!luvL_object_is_started(self)) {
    luvL_stream_start(self);
  }
  TRACE("read called... waiting\n");
  return luvL_cond_wait(&self->rouse, curr);
}

static int luv_stream_write(lua_State* L) {
  luv_object_t* self = (luv_object_t*)lua_touserdata(L, 1);

  size_t len;
  const char* chunk = luaL_checklstring(L, 2, &len);

  uv_buf_t buf = uv_buf_init((char*)chunk, len);

  luv_state_t* curr = luvL_state_self(L);
  uv_write_t*  req  = &curr->req.write;

  if (uv_write(req, &self->h.stream, &buf, 1, _write_cb)) {
    luvL_stream_stop(self);
    luvL_object_close(self);
    STREAM_ERROR(L, "write: %s", luvL_event_loop(L));
    return 2;
  }

  lua_settop(curr->L, 1);
  return luvL_state_suspend(curr);
}

static int luv_stream_shutdown(lua_State* L) {
  luv_object_t* self = (luv_object_t*)lua_touserdata(L, 1);
  if (!luvL_object_is_shutdown(self)) {
    self->flags |= LUV_OSHUTDOWN;
    luv_state_t* curr = luvL_state_self(L);
    uv_shutdown(&curr->req.shutdown, &self->h.stream, _shutdown_cb);
    return luvL_cond_wait(&self->rouse, curr);
  }
  return 1;
}
static int luv_stream_readable(lua_State* L) {
  luv_object_t* self = (luv_object_t*)lua_touserdata(L, 1);
  lua_pushboolean(L, uv_is_readable(&self->h.stream));
  return 1;
}

static int luv_stream_writable(lua_State* L) {
  luv_object_t* self = (luv_object_t*)lua_touserdata(L, 1);
  lua_pushboolean(L, uv_is_writable(&self->h.stream));
  return 1;
}

void luvL_stream_close(luv_object_t* self) {
  TRACE("close stream\n");
  if (luvL_object_is_started(self)) {
    luvL_stream_stop(self);
  }
  luvL_object_close(self);
  if (self->buf.base) {
    free(self->buf.base);
    self->buf.base = NULL;
    self->buf.len  = 0;
  }
}

static int luv_stream_close(lua_State* L) {
  luv_object_t* self = (luv_object_t*)lua_touserdata(L, 1);
  luvL_stream_close(self);
  return luvL_cond_wait(&self->rouse, luvL_state_self(L));
}

void luvL_stream_free(luv_object_t* self) {
  luvL_object_close(self);
  TRACE("free stream: %p\n", self);
  if (self->buf.base) {
    free(self->buf.base);
    self->buf.base = NULL;
    self->buf.len  = 0;
  }
}

static int luv_stream_free(lua_State* L) {
  luv_object_t* self = (luv_object_t*)lua_touserdata(L, 1);
  luvL_stream_free(self);
  return 1;
}

static int luv_stream_tostring(lua_State* L) {
  luv_object_t* self = (luv_object_t*)lua_touserdata(L, 1);
  lua_pushfstring(L, "userdata<luv.stream>: %p", self);
  return 1;
}

luaL_Reg luv_stream_meths[] = {
  {"read",      luv_stream_read},
  {"readable",  luv_stream_readable},
  {"write",     luv_stream_write},
  {"writable",  luv_stream_writable},
  {"start",     luv_stream_start},
  {"stop",      luv_stream_stop},
  {"listen",    luv_stream_listen},
  {"accept",    luv_stream_accept},
  {"shutdown",  luv_stream_shutdown},
  {"close",     luv_stream_close},
  {"__gc",      luv_stream_free},
  {"__tostring",luv_stream_tostring},
  {NULL,        NULL}
};


