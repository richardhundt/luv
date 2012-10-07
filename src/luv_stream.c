#include "luv.h"

/* used by udp and stream */
uv_buf_t luvL_alloc_cb(uv_handle_t* handle, size_t size) {
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
  luv_state_t*  state = self->state;

  if (!ngx_queue_empty(&self->rouse)) {
    TRACE("have states waiting...\n");
    ngx_queue_t* q;
    luv_state_t* s;

    q = ngx_queue_head(&self->rouse);
    s = ngx_queue_data(q, luv_state_t, cond);
    ngx_queue_remove(q);

    lua_settop(s->L, 0);
    if (len >= 0) {
      lua_pushinteger(s->L, len);
      lua_pushlstring(s->L, buf.base, len);
    }
    else {
      uv_err_t err = uv_last_error(state->loop);
      if (err.code == UV_EOF) {
        lua_settop(s->L, 0);
        lua_pushnil(s->L);
      }
      else {
        lua_settop(s->L, 0);
        lua_pushboolean(s->L, 0);
        lua_pushfstring(s->L, "read: %s", uv_strerror(err));
      }
    }
    TRACE("wake up state: %p\n", s);
    luvL_state_ready(s);
  }
  free(buf.base);
  buf.base = NULL;
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
    lua_State* L = self->state->L;
    luv_object_t* conn = (luv_object_t*)lua_touserdata(L, 2);
    int rv = uv_accept(&self->h.stream, &conn->h.stream);
    if (rv) {
      uv_err_t err = uv_last_error(self->h.stream.loop);
      lua_settop(L, 0);
      lua_pushnil(L);
      lua_pushstring(L, uv_strerror(err));
    }
    self->flags &= ~LUV_OWAITING;
    luvL_cond_signal(&self->rouse);
  }
  else {
    self->count++;
  }
}

static int luv_stream_listen(lua_State* L) {
  luaL_checktype(L, 1, LUA_TUSERDATA);
  luv_object_t* self = (luv_object_t*)lua_touserdata(L, 1);
  int backlog = luaL_optinteger(L, 2, 128);
  if (uv_listen(&self->h.stream, backlog, _listen_cb)) {
    uv_err_t err = uv_last_error(self->h.stream.loop);
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
} while (0)

static int luv_stream_start(lua_State* L) {
  luv_object_t* self = (luv_object_t*)lua_touserdata(L, 1);
  if (!luvL_object_is_started(self)) {
    if (luvL_stream_start(self)) {
      STREAM_ERROR(L, "read start: %s", luvL_event_loop(L));
      return 2;
    }
  }
  lua_pushboolean(L, 1);
  return 1;
}

static int luv_stream_read(lua_State* L) {
  luv_object_t* self = (luv_object_t*)lua_touserdata(L, 1);
  luv_state_t*  curr = luvL_state_self(L);
  if (luvL_object_is_closing(self)) {
    return luaL_error(L, "attempt to read from a closed stream");
  }
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
    STREAM_ERROR(L, "write: %s", luvL_event_loop(L));
    return 2;
  }
  lua_settop(curr->L, 1);
  TRACE("write called... waiting\n");
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

static int luv_stream_close(lua_State* L) {
  TRACE("close stream\n");
  luv_object_t* self = (luv_object_t*)lua_touserdata(L, 1);
  if (luvL_object_is_started(self)) {
    luvL_stream_stop(self);
  }
  luvL_object_close(self);
  return 1;

}
static int luv_stream_free(lua_State* L) {
  luv_object_t* self = (luv_object_t*)lua_touserdata(L, 1);
  luvL_object_close(self);
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
  {"listen",    luv_stream_listen},
  {"accept",    luv_stream_accept},
  {"shutdown",  luv_stream_shutdown},
  {"close",     luv_stream_close},
  {"__gc",      luv_stream_free},
  {"__tostring",luv_stream_tostring},
  {NULL,        NULL}
};


