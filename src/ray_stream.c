#include "ray_common.h"
#include "ray_object.h"
#include "ray_state.h"
#include "ray_stream.h"

/* used by udp and stream */
uv_buf_t rayL_alloc_cb(uv_handle_t* handle, size_t size) {
  ray_object_t* self = container_of(handle, ray_object_t, h);
  memset(self->buf.base, 0, self->buf.len);
  return self->buf;
}

/* used by tcp and pipe */
void rayL_connect_cb(uv_connect_t* req, int status) {
  ray_state_t* state = container_of(req, ray_state_t, req);
  rayL_state_ready(state);
}

static void _read_cb(uv_stream_t* stream, ssize_t len, uv_buf_t buf) {
  ray_object_t* self = container_of(stream, ray_object_t, h);

  if (ngx_queue_empty(&self->rouse)) {
    TRACE("empty read queue, save buffer and stop read\n");
    rayL_stream_stop(self);
    if (len >= 0) {
      self->count = len;
    }
  }
  else {
    TRACE("have states waiting...\n");
    ngx_queue_t* q;
    ray_state_t* s;

    q = ngx_queue_head(&self->rouse);
    s = ngx_queue_data(q, ray_state_t, cond);
    ngx_queue_remove(q);

    TRACE("data - len: %i\n", (int)len);

    lua_settop(s->L, 0);
    if (len >= 0) {
      lua_pushinteger(s->L, len);
      lua_pushlstring(s->L, (char*)buf.base, len);
      if (len == 0) rayL_stream_stop(self);
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
        rayL_stream_stop(self);
        rayL_object_close(self);
      }
    }
    TRACE("wake up state: %p\n", s);
    rayL_state_ready(s);
  }
}

static void _write_cb(uv_write_t* req, int status) {
  ray_state_t* rouse = container_of(req, ray_state_t, req);
  lua_settop(rouse->L, 0);
  lua_pushinteger(rouse->L, status);
  TRACE("write_cb - wake up: %p\n", rouse);
  rayL_state_ready(rouse);
}

static void _shutdown_cb(uv_shutdown_t* req, int status) {
  ray_object_t* self = container_of(req->handle, ray_object_t, h);
  ray_state_t* state = container_of(req, ray_state_t, req);
  lua_settop(state->L, 0);
  lua_pushinteger(state->L, status);
  rayL_cond_signal(&self->rouse);
}

static void _listen_cb(uv_stream_t* server, int status) {
  TRACE("got client connection...\n");
  ray_object_t* self = container_of(server, ray_object_t, h);
  if (rayL_object_is_waiting(self)) {
    ngx_queue_t* q = ngx_queue_head(&self->rouse);
    ray_state_t* s = ngx_queue_data(q, ray_state_t, cond);
    lua_State* L = s->L;

    TRACE("is waiting..., lua_State*: %p\n", L);
    luaL_checktype(L, 2, LUA_TUSERDATA);
    ray_object_t* conn = (ray_object_t*)lua_touserdata(L, 2);
    TRACE("got client conn: %p\n", conn);
    rayL_object_init(s, conn);
    int rv = uv_accept(&self->h.stream, &conn->h.stream);
    TRACE("accept returned ok\n");
    if (rv) {
      uv_err_t err = uv_last_error(self->h.stream.loop);
      TRACE("ERROR: %s\n", uv_strerror(err));
      lua_settop(L, 0);
      lua_pushnil(L);
      lua_pushstring(L, uv_strerror(err));
    }
    self->flags &= ~RAY_OWAITING;
    rayL_cond_signal(&self->rouse);
  }
  else {
    TRACE("increment backlog count\n");
    self->count++;
  }
}

static int ray_stream_listen(lua_State* L) {
  luaL_checktype(L, 1, LUA_TUSERDATA);
  ray_object_t* self = (ray_object_t*)lua_touserdata(L, 1);
  int backlog = luaL_optinteger(L, 2, 128);
  if (uv_listen(&self->h.stream, backlog, _listen_cb)) {
    uv_err_t err = uv_last_error(self->h.stream.loop);
    TRACE("listen error\n");
    return luaL_error(L, "listen: %s", uv_strerror(err));
  }
  return 0;
}

static int ray_stream_accept(lua_State *L) {
  luaL_checktype(L, 1, LUA_TUSERDATA);
  ray_object_t* self = (ray_object_t*)lua_touserdata(L, 1);
  ray_object_t* conn = (ray_object_t*)lua_touserdata(L, 2);

  ray_state_t* curr = rayL_state_self(L);

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
  self->flags |= RAY_OWAITING;
  return rayL_cond_wait(&self->rouse, curr);
}

int rayL_stream_start(ray_object_t* self) {
  if (!rayL_object_is_started(self)) {
    self->flags |= RAY_OSTARTED;
    return uv_read_start(&self->h.stream, ray__alloc_cb, _read_cb);
  }
  return 0;
}
int rayL_stream_stop(ray_object_t* self) {
  if (rayL_object_is_started(self)) {
    self->flags &= ~RAY_OSTARTED;
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

static int ray_stream_start(lua_State* L) {
  ray_object_t* self = (ray_object_t*)lua_touserdata(L, 1);
  if (rayL_stream_start(self)) {
    rayL_stream_stop(self);
    rayL_object_close(self);
    STREAM_ERROR(L, "read start: %s", rayL_event_loop(L));
    return 2;
  }
  lua_pushboolean(L, 1);
  return 1;
}

static int ray_stream_stop(lua_State* L) {
  ray_object_t* self = (ray_object_t*)lua_touserdata(L, 1);
  if (rayL_stream_stop(self)) {
    STREAM_ERROR(L, "read stop: %s", rayL_event_loop(L));
    return 2;
  }
  lua_pushboolean(L, 1);
  return 1;
}

static int ray_stream_read(lua_State* L) {
  ray_object_t* self = (ray_object_t*)lua_touserdata(L, 1);
  ray_state_t*  curr = rayL_state_self(L);
  int len = luaL_optinteger(L, 2, RAY_BUF_SIZE);
  if (rayL_object_is_closing(self)) {
    TRACE("error: reading from closed stream\n");
    lua_pushnil(L);
    lua_pushstring(L, "attempt to read from a closed stream");
    return 2;
  }
  if (self->count) {
    /* we have a buffer use that */
    TRACE("have pending data\n");
    lua_pushinteger(L, self->count);
    lua_pushlstring(L, (char*)self->buf.base, self->count);
    self->count = 0;
    return 2;
  }
  if (self->buf.len != len) {
    self->buf.base = realloc(self->buf.base, len);
    self->buf.len  = len;
  }
  if (!rayL_object_is_started(self)) {
    rayL_stream_start(self);
  }
  TRACE("read called... waiting\n");
  return rayL_cond_wait(&self->rouse, curr);
}

static int ray_stream_write(lua_State* L) {
  ray_object_t* self = (ray_object_t*)lua_touserdata(L, 1);

  size_t len;
  const char* chunk = luaL_checklstring(L, 2, &len);

  uv_buf_t buf = uv_buf_init((char*)chunk, len);

  ray_state_t* curr = rayL_state_self(L);
  uv_write_t*  req  = &curr->req.write;

  if (uv_write(req, &self->h.stream, &buf, 1, _write_cb)) {
    rayL_stream_stop(self);
    rayL_object_close(self);
    STREAM_ERROR(L, "write: %s", rayL_event_loop(L));
    return 2;
  }

  lua_settop(curr->L, 1);
  return rayL_state_suspend(curr);
}

static int ray_stream_shutdown(lua_State* L) {
  ray_object_t* self = (ray_object_t*)lua_touserdata(L, 1);
  if (!rayL_object_is_shutdown(self)) {
    self->flags |= RAY_OSHUTDOWN;
    ray_state_t* curr = rayL_state_self(L);
    uv_shutdown(&curr->req.shutdown, &self->h.stream, _shutdown_cb);
    return rayL_cond_wait(&self->rouse, curr);
  }
  return 1;
}
static int ray_stream_readable(lua_State* L) {
  ray_object_t* self = (ray_object_t*)lua_touserdata(L, 1);
  lua_pushboolean(L, uv_is_readable(&self->h.stream));
  return 1;
}

static int ray_stream_writable(lua_State* L) {
  ray_object_t* self = (ray_object_t*)lua_touserdata(L, 1);
  lua_pushboolean(L, uv_is_writable(&self->h.stream));
  return 1;
}

void rayL_stream_close(ray_object_t* self) {
  TRACE("close stream\n");
  if (rayL_object_is_started(self)) {
    rayL_stream_stop(self);
  }
  rayL_object_close(self);
  if (self->buf.len) {
    free(self->buf.base);
    self->buf.base = NULL;
    self->buf.len  = 0;
  }
}

static int ray_stream_close(lua_State* L) {
  ray_object_t* self = (ray_object_t*)lua_touserdata(L, 1);
  rayL_stream_close(self);
  return rayL_cond_wait(&self->rouse, rayL_state_self(L));
}

void rayL_stream_free(ray_object_t* self) {
  rayL_object_close(self);
  TRACE("free stream: %p\n", self);
  if (self->buf.len) {
    free(self->buf.base);
    self->buf.base = NULL;
    self->buf.len  = 0;
  }
}

static int ray_stream_free(lua_State* L) {
  ray_object_t* self = (ray_object_t*)lua_touserdata(L, 1);
  rayL_stream_free(self);
  return 1;
}

static int ray_stream_tostring(lua_State* L) {
  ray_object_t* self = (ray_object_t*)lua_touserdata(L, 1);
  lua_pushfstring(L, "userdata<ray.stream>: %p", self);
  return 1;
}

luaL_Reg ray_stream_meths[] = {
  {"read",      ray_stream_read},
  {"readable",  ray_stream_readable},
  {"write",     ray_stream_write},
  {"writable",  ray_stream_writable},
  {"start",     ray_stream_start},
  {"stop",      ray_stream_stop},
  {"listen",    ray_stream_listen},
  {"accept",    ray_stream_accept},
  {"shutdown",  ray_stream_shutdown},
  {"close",     ray_stream_close},
  {"__gc",      ray_stream_free},
  {"__tostring",ray_stream_tostring},
  {NULL,        NULL}
};


