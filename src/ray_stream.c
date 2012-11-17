#include "ray_lib.h"
#include "ray_state.h"
#include "ray_stream.h"
#include "ray_hash.h"
#include "ray_chan.h"
#include "ray_cond.h"
#include "ray_queue.h"

/* used by udp and stream */
uv_buf_t ray_alloc_cb(uv_handle_t* handle, size_t size) {
  ray_state_t* self = container_of(handle, ray_state_t, h);
  if (self->buf.len == 0) {
    self->buf.base = malloc(RAY_BUF_SIZE);
    self->buf.len  = RAY_BUF_SIZE;
  }
  memset(self->buf.base, 0, self->buf.len);
  return self->buf;
}

/* used by tcp and pipe */
void ray_connect_cb(uv_connect_t* req, int status) {
  ray_state_t* self = (ray_state_t*)req->data;
  ray_cond_t*  cond = (ray_cond_t*)ray_hash_get(self->u.hash, "cond");
  if (status) {
    uv_err_t err = uv_last_error(ray_get_loop(self->L));
    lua_pushnil(self->L);
    lua_pushstring(self->L, uv_strerror(err));
    ray_cond_signal(cond, self, 2);
  }
  else {
    lua_pushboolean(self->L, 1);
    ray_cond_signal(cond, self, 1);
  }
}

static void _read_cb(uv_stream_t* stream, ssize_t len, uv_buf_t buf) {
  TRACE("read: len: %i\n", (int)len);
  ray_state_t* self = container_of(stream, ray_state_t, h);
  ray_chan_t*  chan = (ray_chan_t*)ray_hash_get(self->u.hash, "chan");
  if (len < 0) {
    uv_err_t err = uv_last_error(stream->loop);
    lua_pushnil(self->L);
    lua_pushstring(self->L, uv_strerror(err));
    ray_chan_put(chan, self, 2);
    TRACE("closing %p\n", self);
    ray_close(self);
  }
  else {
    lua_pushinteger(self->L, len);
    lua_pushlstring(self->L, (char*)buf.base, len);
    ray_chan_put(chan, self, 2);
  }
}

static void _write_cb(uv_write_t* req, int status) {
  ray_state_t* self = (ray_state_t*)req->data;
  ray_cond_t*  cond = (ray_cond_t*)ray_hash_get(self->u.hash, "cond");
  if (status == -1) {
    uv_err_t err = uv_last_error(ray_get_loop(self->L));
    lua_pushnil(self->L);
    lua_pushstring(self->L, uv_strerror(err));
    ray_cond_signal(cond, self, 2);
  }
  else {
    lua_pushboolean(self->L, 1);
    ray_cond_signal(cond, self, 1);
  }
}

static void _shutdown_cb(uv_shutdown_t* req, int status) {
  ray_state_t* self = (ray_state_t*)req->data;
  ray_cond_t*  cond = (ray_cond_t*)ray_hash_get(self->u.hash, "cond");
  lua_pushinteger(self->L, status);
  ray_cond_signal(cond, self, 1);
}

static void _listen_cb(uv_stream_t* server, int status) {
  ray_state_t* self  = container_of(server, ray_state_t, h);
  ray_queue_t* queue = (ray_queue_t*)ray_hash_get(self->u.hash, "queue");

  const char*  meta = lua_tostring(self->L, 1);
  ray_state_t* conn = ray_stream_new(self->L, meta, &self->v);

  TRACE("new conn: %p, meta: %s\n", conn, meta);

  int rc = uv_accept(&self->h.stream, &conn->h.stream);
  if (rc) {
    uv_err_t err = uv_last_error(self->h.stream.loop);
    lua_pushnil(self->L);
    lua_pushstring(self->L, uv_strerror(err));
    ray_queue_put(queue, self, 2);
    ray_close(conn);
    lua_pop(self->L, 1);
  }
  else {
    ray_queue_put(queue, self, 1);
  }
}

int ray_stream_react(ray_state_t* self) {
  /* we only ray_yield during read, accept uses a queue as large as the backlog,
  meaning that we don't suspend during listen... not all that pretty though */
  TRACE("stream reacting - calling read_start...\n");
  self->flags |= RAY_STREAM_READING;
  return uv_read_start(&self->h.stream, ray_alloc_cb, _read_cb);
}
int ray_stream_yield(ray_state_t* self) {
  TRACE("stream yielding ...\n");
  if (self->flags & RAY_STREAM_READING) {
    self->flags &= ~RAY_STREAM_READING;
    TRACE("calling read stop...\n");
    uv_read_stop(&self->h.stream);
  }
  return 0;
}
int ray_stream_close(ray_state_t* self) {
  if (!ray_is_closed(self)) {
    self->flags |= RAY_CLOSED;
    uv_close(&self->h.handle, NULL);
    if (self->buf.len) {
      free(self->buf.base);
      self->buf.base = NULL;
      self->buf.len  = 0;

      ray_cond_t* cond = (ray_cond_t*)ray_hash_get(self->u.hash, "cond");
      ray_cond_free(cond);

      ray_chan_t* chan = (ray_chan_t*)ray_hash_get(self->u.hash, "chan");
      ray_chan_free(chan);

      ray_queue_t* queue = (ray_queue_t*)ray_hash_get(self->u.hash, "queue");
      if (queue) ray_queue_free(queue);

      ray_hash_free(self->u.hash);
    }
  }
  return 1;
}

ray_state_t* ray_stream_new(lua_State* L, const char* meta, ray_vtable_t* vtab) {
  ray_state_t* self = ray_state_new(L, meta, vtab);
  self->u.hash = ray_hash_new(4);
  ray_hash_set(self->u.hash, "chan", ray_chan_new());
  ray_hash_set(self->u.hash, "cond", ray_cond_new());
  lua_pushstring(self->L, meta);
  return self;
}

/* Lua API */
static int stream_read(lua_State* L) {
  ray_state_t* self = (ray_state_t*)lua_touserdata(L, 1);
  ray_state_t* curr = ray_current(L);
  ray_chan_t*  chan = (ray_chan_t*)ray_hash_get(self->u.hash, "chan");
  int len = luaL_optint(L, 2, RAY_BUF_SIZE);
  if (self->buf.len != len) {
    self->buf.base = realloc(self->buf.base, len);
    self->buf.len  = len;
  }
  if (!(self->flags & RAY_STREAM_READING)) {
    if (uv_read_start(&self->h.stream, ray_alloc_cb, _read_cb)) {
      lua_pushnil(L);
      lua_pushstring(L, uv_strerror(uv_last_error(self->h.stream.loop)));
      ray_close(self);
      return 2;
    }
    self->flags |= RAY_STREAM_READING;
  }
  TRACE("reading...\n");
  return ray_chan_get(chan, curr);
}

static int stream_write(lua_State* L) {
  ray_state_t* self = (ray_state_t*)lua_touserdata(L, 1);
  ray_state_t* curr = ray_current(L);

  size_t len;
  const char* data = lua_tolstring(L, 2, &len);
  uv_buf_t buf = uv_buf_init((char*)data, len);

  curr->r.req.data = self;
  ray_cond_t* cond = (ray_cond_t*)ray_hash_get(self->u.hash, "cond");

  if (uv_write(&curr->r.write, &self->h.stream, &buf, 1, _write_cb)) {
    lua_pushnil(L);
    lua_pushstring(L, uv_strerror(uv_last_error(self->h.stream.loop)));
    ray_close(self);
    return 2;
  }

  return ray_cond_wait(cond, curr);
}

static int stream_listen(lua_State* L) {
  ray_state_t* self = (ray_state_t*)lua_touserdata(L, 1);
  int backlog = luaL_optint(L, 2, 128);

  if (uv_listen(&self->h.stream, backlog, _listen_cb)) {
    lua_pushnil(L);
    lua_pushstring(L, uv_strerror(uv_last_error(self->h.stream.loop)));
    return 2;
  }

  ray_hash_set(self->u.hash, "queue", ray_queue_new(backlog));
  return 1;
}

static int stream_accept(lua_State *L) {
  ray_state_t* self  = (ray_state_t*)lua_touserdata(L, 1);
  ray_state_t* curr  = ray_current(L);
  ray_queue_t* queue = (ray_queue_t*)ray_hash_get(self->u.hash, "queue");
  if (!queue) return luaL_error(L, "not listening");
  return ray_queue_get(queue, curr);
}

static int stream_shutdown(lua_State* L) {
  ray_state_t* self = (ray_state_t*)lua_touserdata(L, 1);
  ray_state_t* curr = ray_current(L);
  curr->r.req.data = self;
  uv_shutdown(&curr->r.shutdown, &self->h.stream, _shutdown_cb);
  return ray_yield(curr);
}

static int stream_readable(lua_State* L) {
  ray_state_t* self = (ray_state_t*)lua_touserdata(L, 1);
  lua_pushboolean(L, uv_is_readable(&self->h.stream));
  return 1;
}

static int stream_writable(lua_State* L) {
  ray_state_t* self = (ray_state_t*)lua_touserdata(L, 1);
  lua_pushboolean(L, uv_is_writable(&self->h.stream));
  return 1;
}

static int stream_close(lua_State* L) {
  ray_state_t* self = (ray_state_t*)lua_touserdata(L, 1);
  ray_state_t* curr = ray_current(L);
  if (!ray_is_closed(self)) {
    ray_close(self);
    return ray_yield(curr);
  }
  return 0;
}

static int stream_free(lua_State* L) {
  ray_state_t* self = (ray_state_t*)lua_touserdata(L, 1);
  if (!ray_is_closed(self)) {
    return stream_close(L);
  }
  return 0;
}

static int stream_tostring(lua_State* L) {
  ray_state_t* self = (ray_state_t*)lua_touserdata(L, 1);
  lua_pushfstring(L, "userdata<ray.stream>: %p", self);
  return 1;
}

luaL_Reg ray_stream_meths[] = {
  {"read",      stream_read},
  {"readable",  stream_readable},
  {"write",     stream_write},
  {"writable",  stream_writable},
  {"listen",    stream_listen},
  {"accept",    stream_accept},
  {"shutdown",  stream_shutdown},
  {"close",     stream_close},
  {"__gc",      stream_free},
  {"__tostring",stream_tostring},
  {NULL,        NULL}
};

