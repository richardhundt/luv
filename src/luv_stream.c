#include "luv.h"

static uv_buf_t _alloc_cb(uv_handle_t* handle, size_t size) {
  return uv_buf_init(malloc(size), size);
}

static void _shutdown_cb(uv_shutdown_t* req, int status) {
  luv_object_t* self = container_of(req->handle, luv_object_t, h);
  luv_state_t* state = container_of(req, luv_state_t, req);
  lua_settop(state->L, 0);
  lua_pushinteger(state->L, status);
  luvL_cond_signal(&self->rouse);
}

static void _close_cb(uv_handle_t* handle) {
  luv_object_t* self = container_of(handle, luv_object_t, h);
  self->flags |= LUV_OCLOSED;
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
      uv_err_t err = uv_last_error(luvL_event_loop(state));
      if (err.code == UV_EOF) {
        lua_settop(s->L, 0);
        lua_pushnil(s->L);
      }
      else {
        uv_close((uv_handle_t*)stream, _close_cb);
        free(buf.base);
        buf.base = NULL;
        lua_settop(s->L, 0);
        lua_pushboolean(s->L, 0);
        lua_pushfstring(s->L, "read: %s", uv_strerror(err));
      }
    }
    TRACE("wake up state: %p\n", s);
    luvL_state_ready(s);
    free(buf.base);
    buf.base = NULL;
  }
}

static void _write_cb(uv_write_t* req, int status) {
  luv_state_t* rouse = container_of(req, luv_state_t, req);
  lua_settop(rouse->L, 0);
  lua_pushinteger(rouse->L, status);
  TRACE("write_cb - wake up: %p\n", rouse);
  luvL_state_ready(rouse);
}

int luvL_stream_start(luv_object_t* self) {
  if (!luvL_object_is_started(self)) {
    self->flags |= LUV_OSTARTED;
    return uv_read_start(&self->h.stream, _alloc_cb, _read_cb);
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
  luv_object_t* self = lua_touserdata(L, 1);
  if (!luvL_object_is_started(self)) {
    if (luvL_stream_start(self)) {
      luv_state_t* curr = luvL_state_self(L);
      STREAM_ERROR(L, "read start: %s", luvL_event_loop(curr));
      return 2;
    }
  }
  lua_pushboolean(L, 1);
  return 1;
}

static int luv_stream_read(lua_State* L) {
  luv_object_t* self = lua_touserdata(L, 1);
  luv_state_t*  curr = luvL_state_self(L);
  if (luvL_object_is_closing(self) || luvL_object_is_closed(self)) {
    return luaL_error(L, "attempt to read from a closed stream");
  }
  if (!luvL_object_is_started(self)) {
    luvL_stream_start(self);
  }
  TRACE("read called... waiting\n");
  return luvL_cond_wait(&self->rouse, curr);
}

static int luv_stream_write(lua_State* L) {
  luv_object_t* self = lua_touserdata(L, 1);

  size_t len;
  const char* chunk = luaL_checklstring(L, 2, &len);

  uv_buf_t buf = uv_buf_init((char*)chunk, len);

  luv_state_t* curr = luvL_state_self(L);
  uv_write_t*  req  = &curr->req.write;
  if (uv_write(req, &self->h.stream, &buf, 1, _write_cb)) {
    STREAM_ERROR(L, "write: %s", luvL_event_loop(curr));
    return 2;
  }
  lua_settop(curr->L, 1);
  TRACE("write called... waiting\n");
  return luvL_state_suspend(curr);
}

static int luv_stream_shutdown(lua_State* L) {
  luv_object_t* self = lua_touserdata(L, 1);
  luv_state_t*  curr = luvL_state_self(L);
  uv_shutdown(&curr->req.shutdown, &self->h.stream, _shutdown_cb);
  return luvL_cond_wait(&self->rouse, curr);
}
static int luv_stream_close(lua_State* L) {
  luv_object_t* self = lua_touserdata(L, 1);
  uv_close((uv_handle_t*)&self->h.stream, _close_cb);
  self->flags |= LUV_OCLOSING;
  return 1;
}
static int luv_stream_readable(lua_State* L) {
  luv_object_t* self = lua_touserdata(L, 1);
  lua_pushboolean(L, uv_is_readable(&self->h.stream));
  return 1;
}

static int luv_stream_writable(lua_State* L) {
  luv_object_t* self = lua_touserdata(L, 1);
  lua_pushboolean(L, uv_is_writable(&self->h.stream));
  return 1;
}

/* TODO: make this generic as uv_close MUST be called on all handles */
static int luv_stream_free(lua_State* L) {
  luv_object_t* self = lua_touserdata(L, 1);
  if (!(self->flags & LUV_OCLOSING)) {
    uv_close((uv_handle_t*)&self->h.stream, _close_cb);
    self->flags |= LUV_OCLOSING;
  }
  return 1;
}

static int luv_stream_tostring(lua_State* L) {
  luv_object_t* self = lua_touserdata(L, 1);
  lua_pushfstring(L, "userdata<luv.stream>: %p", self);
  return 1;
}

luaL_Reg luv_stream_meths[] = {
  {"read",      luv_stream_read},
  {"readable",  luv_stream_readable},
  {"write",     luv_stream_write},
  {"writable",  luv_stream_writable},
  {"start",     luv_stream_start},
  {"shutdown",  luv_stream_shutdown},
  {"close",     luv_stream_close},
  {"__gc",      luv_stream_free},
  {"__tostring",luv_stream_tostring},
  {NULL,        NULL}
};


