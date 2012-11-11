#include "ray_lib.h"
#include "ray_state.h"
#include "ray_stream.h"

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

ray_cond_t* _stream_get_cond(ray_state_t* self) {
  return (ray_cond_t*)self->u.data;
}

/* used by tcp and pipe */
void ray_connect_cb(uv_connect_t* req, int status) {
  ray_state_t* self = (ray_state_t*)req->data;
  if (status) {
    uv_err_t err = uv_last_error(ray_get_loop(self->L));
    lua_pushnil(self->L);
    lua_pushstring(self->L, uv_strerror(err));
    ray_cond_t* cond = _stream_get_cond(self);
    ray_cond_signal(cond, ray_msg(RAY_ERROR,2));
  }
  else {
    ray_cond_signal(cond, ray_msg(RAY_DATA,1));
  }
}

static void _read_cb(uv_stream_t* stream, ssize_t len, uv_buf_t buf) {
  ray_state_t* self = container_of(stream, ray_state_t, h);
  lua_settop(self->L, 0);
  ray_cond_t* cond = _stream_get_cond(self);
  if (len < 0) {
    uv_err_t err = uv_last_error(stream->loop);
    lua_pushnil(self->L);
    lua_pushstring(self->L, uv_strerror(err));
    if (err.code == UV_EOF) {
      ray_stream_stop(self);
      ray_cond_signal(cond, self, ray_msg(RAY_DATA,2));
    }
    else {
      ray_close(self);
    }
  }
  else {
    lua_pushinteger(self->L, len);
    lua_pushlstring(self->L, (char*)buf.base, len);
    ray_cond_signal(cond, self, ray_msg(RAY_DATA,2));
  }
}

static void _write_cb(uv_write_t* req, int status) {
  ray_state_t* curr = (ray_state_t*)req->data;
  lua_settop(self->L, 0);
  ray_cond_t* cond = _stream_get_cond(self);
  if (status == -1) {
    uv_err_t err = uv_last_error(ray_get_loop(self->L));
    lua_pushnil(self->L);
    lua_pushstring(self->L, uv_strerror(err));
    ray_cond_signal(cond, self, ray_msg(RAY_ERROR,2));
  }
  else {
    lua_pushboolean(self->L, 1);
    ray_cond_signal(cond, self, ray_msg(RAY_DATA,1));
  }
}

static void _shutdown_cb(uv_shutdown_t* req, int status) {
  ray_state_t* self = (ray_state_t*)req->data;
  ray_cond_t*  cond = _stream_get_cond(self);
  lua_settop(self->L, 0);
  lua_pushinteger(self->L, status);
  ray_cond_signal(cond, self, ray_msg(RAY_DATA,1));
}

static void _listen_cb(uv_stream_t* server, int status) {
  ray_state_t* self = container_of(server, ray_state_t, h);
  ray_cond_t* cond = _stream_get_cond(self);
  if (lua_gettop(self->L) > 0) {
    luaL_checktype(self->L, 1, LUA_TLIGHTUSERDATA);
    ray_state_t* conn = (ray_state_t*)lua_touserdata(self->L, 1);
    int rc = uv_accept(&self->h.stream, &conn->h.stream);
    if (rc) {
      uv_err_t err = uv_last_error(self->h.stream.loop);
      lua_pushnil(self->L);
      lua_pushstring(self->L, uv_strerror(err));
    }
    ray_cond_signal(cond, self, ray_msg(RAY_DATA,1));
  }
  else {
    lua_pushboolean(self->L, 1);
  }
}

int rayM_stream_react(ray_state_t* self, ray_state_t* from, ray_msg_t* msg) {

  switch (ray_msg_type(msg)) {

    case RAY_STREAM_READ: {
      if (!ray_queue_empty(&self->mbox)) {
        int nval = ray_queue_get_tuple(&self->mbox, self->L);
        ray_send(from, self, ray_msg(RAY_DATA,nval));
        return nval;
      }
      if (self->buf.len != len) {
        self->buf.base = realloc(self->buf.base, len);
        self->buf.len  = len;
      }
      uv_read_start(&self->h.stream, ray_alloc_cb, _read_cb);
      break;
    }

    case RAY_STREAM_WRITE: {
      size_t len;
      const char* data = ray_queue_get_lstring(&self->mbox, &len);
      uv_buf_t buf     = uv_buf_init((char*)data, len);
      from->r.req.data = self;

      if (uv_write(&from->r.write, &self->h.stream, b, n, _write_cb)) {
        lua_pushnil(self->L);
        lua_pushstring(self->L, uv_strerror(uv_last_error(self->h.stream.loop)));
        ray_send(from, self, ray_msg(RAY_ERROR, 2));
        ray_close(self);
        return 2;
      }

      /* stop reading during write, it's racy */
      ray_stream_stop(self);
      break;
    }

    case RAY_STREAM_LISTEN: {

    }

    case RAY_STREAM_ACCEPT: {

    }

    case RAY_STREAM_SHUTDOWN: {

    }

    case RAY_STREAM_CLOSE: {

    }

    default: {
      luaL_error(from->L, "bad message");
    }
  }
}

int rayM_stream_yield(ray_state_t* self) {
  return uv_read_stop(&self->h.stream);
}

int rayM_stream_close(ray_state_t* self) {
  uv_close(&self->h.handle, NULL);
  if (self->buf.len) {
    free(self->buf.base);
    self->buf.base = NULL;
    self->buf.len  = 0;
  }
  return 1;
}

int ray_stream_listen(ray_state_t* self, ray_state_t* from, int backlog) {
  if (uv_listen(&self->h.stream, backlog, _listen_cb)) {
    lua_settop(from->L, 0);
    lua_pushnil(from->L);
    lua_pushstring(from->L, uv_strerror(uv_last_error(self->h.stream.loop)));
    return 2;
  }
  return 1;
}

int ray_stream_accept(ray_state_t* self, ray_state_t* from, ray_state_t* conn) {
  if (lua_gettop(self->L) > 0) {
    luaL_checktype(self->L, 1, LUA_TBOOLEAN);
    lua_pop(self->L, 1);
    int rv = uv_accept(&self->h.stream, &conn->h.stream);
    if (rv) {
      uv_err_t err = uv_last_error(self->h.stream.loop);
      lua_settop(from->L, 0);
      lua_pushnil(from->L);
      lua_pushstring(from->L, uv_strerror(err));
      return 2;
    }
    return 1;
  }
  else {
    lua_pushlightuserdata(self->L, conn);
    return ray_yield(from);
  }
}

int ray_stream_shutdown(ray_state_t* self, ray_state_t* from) {
  from->r.req.data = self;
  uv_shutdown(&from->r.shutdown, &self->h.stream, _shutdown_cb);
  return ray_yield(from);
}

int ray_stream_readable(ray_state_t* self, ray_state_t* from) {
  lua_pushboolean(from->L, uv_is_readable(&self->h.stream));
  return 1;
}

int ray_stream_writable(ray_state_t* self, ray_state_t* from) {
  lua_pushboolean(from->L, uv_is_writable(&self->h.stream));
  return 1;
}

int ray_stream_close(ray_state_t* self, ray_state_t* from) {
  if (!ray_is_closed(self)) {
    ray_close(self);
    return ray_yield(from);
  }
  return 0;
}

int ray_stream_free(ray_state_t* self) {
  if (!ray_is_closed(self)) {
    ray_close(self);
    if (self->buf.len) {
      free(self->buf.base);
      self->buf.base = NULL;
      self->buf.len  = 0;
    }
    return 1;
  }
  return 0;
}

static int stream_read(lua_State* L) {
  ray_state_t* self = (ray_state_t*)lua_touserdata(L, 1);
  ray_channel_t* chan = (ray_channel_t*)ray_hash_get(self->u.hash, "rd_chan");
  return ray_channel_get(chan, L);
}

static int stream_write(lua_State* L) {
  ray_state_t* self = (ray_state_t*)lua_touserdata(L, 1);
  ray_channel_t* chan = (ray_channel_t*)ray_hash_get(self->u.hash, "wr_chan");
  return ray_channel_put(chan, L, 1);
}

static int stream_listen(lua_State* L) {
  ray_state_t* self = (ray_state_t*)lua_touserdata(L, 1);
  if (uv_listen(&self->h.stream, backlog, _listen_cb)) {
    lua_settop(L, 0);
    lua_pushnil(L);
    lua_pushstring(L, uv_strerror(uv_last_error(self->h.stream.loop)));
    return 2;
  }
  return 1;
}

static int stream_accept(lua_State *L) {
  ray_state_t* self = (ray_state_t*)lua_touserdata(L, 1);
  ray_channel_t* chan = (ray_channel_t*)ray_hash_get(self->u.hash, "ac_chan");
  return ray_channel_get(chan, L);
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
  return ray_stream_close(self, curr);
}

static int stream_free(lua_State* L) {
  ray_state_t* self = (ray_state_t*)lua_touserdata(L, 1);
  return ray_stream_free(self);
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

