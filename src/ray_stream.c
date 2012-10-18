#include "ray_lib.h"
#include "ray_actor.h"
#include "ray_stream.h"

/* used by udp and stream */
uv_buf_t ray_alloc_cb(uv_handle_t* handle, size_t size) {
  ray_actor_t* self = container_of(handle, ray_actor_t, h);
  memset(self->buf.base, 0, self->buf.len);
  return self->buf;
}

/* used by tcp and pipe */
void ray_connect_cb(uv_connect_t* req, int status) {
  ray_actor_t* self = (ray_actor_t*)req->data;
  if (status) {
    uv_err_t err = uv_last_error(ray_get_loop(self->L));
    lua_pushnil(self->L);
    lua_pushstring(self->L, uv_strerror(err));
    ray_notify(self, 2);
  }
  else {
    ray_notify(self, 1);
  }
}

static void _read_cb(uv_stream_t* stream, ssize_t len, uv_buf_t buf) {
  ray_actor_t* self = container_of(stream, ray_actor_t, h);
  lua_settop(self->L, 0);
  TRACE("read callback\n");
  if (len < 0) {
    uv_err_t err = uv_last_error(stream->loop);
    lua_pushnil(self->L);
    lua_pushstring(self->L, uv_strerror(err));
    if (err.code == UV_EOF) {
      TRACE("got EOF\n");
      ray_stream_stop(self);
      ray_notify(self, 2);
    }
    else {
      TRACE("got ERROR, closing\n");
      ray_close(self);
    }
  }
  else {
    lua_pushinteger(self->L, len);
    lua_pushlstring(self->L, (char*)buf.base, len);
    ray_notify(self, 2);
    assert(lua_gettop(self->L) == 0);
  }
}

static void _write_cb(uv_write_t* req, int status) {
  ray_actor_t* self = (ray_actor_t*)req->data;
  lua_settop(self->L, 0);
  if (status == -1) {
    uv_err_t err = uv_last_error(ray_get_loop(self->L));
    lua_pushnil(self->L);
    lua_pushstring(self->L, uv_strerror(err));
    ray_notify(self, 2);
  }
  else {
    lua_pushboolean(self->L, 1);
    ray_notify(self, 1);
  }
}

static void _shutdown_cb(uv_shutdown_t* req, int status) {
  ray_actor_t* self = (ray_actor_t*)req->data;
  lua_settop(self->L, 0);
  lua_pushinteger(self->L, status);
  ray_notify(self, 1);
}

static void _listen_cb(uv_stream_t* server, int status) {
  ray_actor_t* self = container_of(server, ray_actor_t, h);
  if (lua_gettop(self->L) > 0) {
    luaL_checktype(self->L, 1, LUA_TUSERDATA);
    ray_actor_t* conn = (ray_actor_t*)lua_touserdata(self->L, 1);
    int rv = uv_accept(&self->h.stream, &conn->h.stream);
    if (rv) {
      uv_err_t err = uv_last_error(self->h.stream.loop);
      lua_pushnil(self->L);
      lua_pushstring(self->L, uv_strerror(err));
    }
    ray_notify(self, LUA_MULTRET);
  }
  else {
    lua_pushboolean(self->L, 1);
  }
}

static void _close_cb(uv_handle_t* handle) {
  ray_actor_t* self = container_of(handle, ray_actor_t, h);
  ray_notify(self, lua_gettop(self->L));
  ray_actor_free(self);
}

int rayM_stream_await(ray_actor_t* self, ray_actor_t* that) {
  return ray_stream_stop(self);
}
int rayM_stream_rouse(ray_actor_t* self, ray_actor_t* from) {
  return ray_stream_start(self);
}
int rayM_stream_close(ray_actor_t* self) {
  uv_close(&self->h.handle, _close_cb);
  if (self->buf.len) {
    free(self->buf.base);
    self->buf.base = NULL;
    self->buf.len  = 0;
  }
  return 1;
}

int ray_stream_start(ray_actor_t* self) {
  self->flags |= RAY_START;
  return uv_read_start(&self->h.stream, ray_alloc_cb, _read_cb);
}

int ray_stream_stop(ray_actor_t* self) {
  self->flags &= ~RAY_START;
  return uv_read_stop(&self->h.stream);
}

void ray_stream_free(ray_actor_t* self) {
  if (!ray_is_closed(self)) {
    ray_close(self);
    TRACE("free stream: %p\n", self);
    if (self->buf.len) {
      free(self->buf.base);
      self->buf.base = NULL;
      self->buf.len  = 0;
    }
  }
}

#define STREAM_ERROR(L,fmt,loop) do { \
  uv_err_t err = uv_last_error(loop); \
  lua_settop(L, 0); \
  lua_pushnil(L); \
  lua_pushfstring(L, fmt, uv_strerror(err)); \
  TRACE("STREAM ERROR: %s\n", lua_tostring(L, -1)); \
} while (0)

static int stream_start(lua_State* L) {
  ray_actor_t* self = (ray_actor_t*)lua_touserdata(L, 1);
  if (ray_stream_start(self)) {
    ray_stream_stop(self);
    ray_close(self);
    STREAM_ERROR(L, "read start: %s", ray_get_loop(L));
    return 2;
  }
  lua_pushboolean(L, 1);
  return 1;
}

static int stream_stop(lua_State* L) {
  ray_actor_t* self = (ray_actor_t*)lua_touserdata(L, 1);
  if (ray_stream_stop(self)) {
    STREAM_ERROR(L, "read stop: %s", ray_get_loop(L));
    return 2;
  }
  lua_pushboolean(L, 1);
  return 1;
}

static int stream_read(lua_State* L) {
  ray_actor_t* self = (ray_actor_t*)lua_touserdata(L, 1);
  ray_actor_t* curr = ray_get_self(L);
  int len = luaL_optinteger(L, 2, RAY_BUF_SIZE);
  if (lua_gettop(self->L) > 0) {
    lua_xmove(self->L, L, 2);
    return 2;
  }
  if (self->buf.len != len) {
    self->buf.base = realloc(self->buf.base, len);
    self->buf.len  = len;
  }
  if (!ray_is_start(self)) {
    ray_stream_start(self);
  }
  return ray_await(curr, self);
}

static int stream_write(lua_State* L) {
  ray_actor_t* self = (ray_actor_t*)lua_touserdata(L, 1);
  ray_actor_t* curr = ray_get_self(L);

  size_t len;
  const char* chunk = luaL_checklstring(L, 2, &len);

  uv_buf_t buf = uv_buf_init((char*)chunk, len);
  curr->r.req.data = self;

  if (uv_write(&curr->r.write, &self->h.stream, &buf, 1, _write_cb)) {
    STREAM_ERROR(L, "write: %s", ray_get_loop(L));
    ray_close(self);
    return 2;
  }

  if (ray_is_start(self)) {
    ray_stream_stop(self);
  }

  return ray_await(curr, self);
}

static int stream_listen(lua_State* L) {
  luaL_checktype(L, 1, LUA_TUSERDATA);
  ray_actor_t* self = (ray_actor_t*)lua_touserdata(L, 1);
  int backlog = luaL_optinteger(L, 2, 128);
  if (uv_listen(&self->h.stream, backlog, _listen_cb)) {
    uv_err_t err = uv_last_error(self->h.stream.loop);
    return luaL_error(L, "listen: %s", uv_strerror(err));
  }
  return 1;
}

static int stream_accept(lua_State *L) {
  luaL_checktype(L, 1, LUA_TUSERDATA);
  ray_actor_t* self = (ray_actor_t*)lua_touserdata(L, 1);
  if (lua_gettop(self->L) > 0) {
    ray_actor_t* conn = (ray_actor_t*)lua_touserdata(L, 2);
    luaL_checktype(self->L, 1, LUA_TBOOLEAN);
    lua_pop(self->L, 1);
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
  else {
    ray_actor_t* curr = ray_get_self(L);
    lua_xmove(L, self->L, 1);
    TRACE("calling await: curr %p, self: %p\n", curr, self);
    return ray_await(curr, self);
  }
}

static int stream_shutdown(lua_State* L) {
  ray_actor_t* self = (ray_actor_t*)lua_touserdata(L, 1);
  ray_actor_t* curr = ray_get_self(L);
  curr->r.req.data = self;

  uv_shutdown(&curr->r.shutdown, &self->h.stream, _shutdown_cb);

  return ray_await(curr, self);
}

static int stream_readable(lua_State* L) {
  ray_actor_t* self = (ray_actor_t*)lua_touserdata(L, 1);
  lua_pushboolean(L, uv_is_readable(&self->h.stream));
  return 1;
}

static int stream_writable(lua_State* L) {
  ray_actor_t* self = (ray_actor_t*)lua_touserdata(L, 1);
  lua_pushboolean(L, uv_is_writable(&self->h.stream));
  return 1;
}

static int stream_close(lua_State* L) {
  ray_actor_t* self = (ray_actor_t*)lua_touserdata(L, 1);
  ray_actor_t* curr = ray_get_self(L);
  if (!ray_is_closed(self)) {
    ray_close(self);
    return ray_await(curr, self);
  }
  return 0;
}

static int stream_free(lua_State* L) {
  ray_actor_t* self = (ray_actor_t*)lua_touserdata(L, 1);
  ray_stream_free(self);
  return 1;
}

static int stream_tostring(lua_State* L) {
  ray_actor_t* self = (ray_actor_t*)lua_touserdata(L, 1);
  lua_pushfstring(L, "userdata<ray.stream>: %p", self);
  return 1;
}

luaL_Reg ray_stream_meths[] = {
  {"read",      stream_read},
  {"readable",  stream_readable},
  {"write",     stream_write},
  {"writable",  stream_writable},
  {"start",     stream_start},
  {"stop",      stream_stop},
  {"listen",    stream_listen},
  {"accept",    stream_accept},
  {"shutdown",  stream_shutdown},
  {"close",     stream_close},
  {"__gc",      stream_free},
  {"__tostring",stream_tostring},
  {NULL,        NULL}
};

