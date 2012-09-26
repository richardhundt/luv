#include "luv_core.h"
#include "luv_object.h"
#include "luv_stream.h"

static uv_buf_t luv_alloc_cb(uv_handle_t* handle, size_t size) {
  return uv_buf_init(malloc(size), size);
}

static void luv_shutdown_cb(uv_shutdown_t* req, int status) {
  luv_object_t* self = container_of(req->handle, luv_object_t, h);
  luv_state_t* state = container_of(req, luv_state_t, req);
  self->flags |= LUV_SSHUTDOWN;
  lua_settop(state->L, 0);
  lua_pushinteger(state->L, status);
  luv__cond_signal(&state->rouse);
}

static void luv_close_cb(uv_handle_t* handle) {
  luv_object_t* self = container_of(handle, luv_object_t, h);
  self->flags |= LUV_SCLOSED;
}

static void luv_read_cb(uv_stream_t* stream, ssize_t len, uv_buf_t buf) {
  luv_object_t* self  = container_of(stream, luv_object_t, h);
  luv_sched_t*  sched = self->sched;

  if (!ngx_queue_empty(&self->rouse)) {
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
      uv_err_t err = uv_last_error(sched->loop);
      if (err.code == UV_EOF) {
        lua_settop(s->L, 0);
        lua_pushnil(s->L);
      }
      else {
        uv_close((uv_handle_t*)stream, luv_close_cb);
        free(buf.base);
        buf.base = NULL;
        lua_settop(s->L, 0);
        lua_pushboolean(s->L, 0);
        lua_pushfstring(s->L, "read: %s", uv_strerror(err));
      }
    }
    uv_read_stop(stream);
    luv__state_resume(s);
    free(buf.base);
    buf.base = NULL;
  }
}

static void luv_write_cb(uv_write_t* req, int status) {
  luv_state_t* rouse = container_of(req, luv_state_t, req);
  lua_settop(rouse->L, 0);
  lua_pushinteger(rouse->L, status);
}

#define luv__stream_error(L,fmt,loop) \
  do { \
    uv_err_t err = uv_last_error(loop); \
    lua_settop(L, 0); \
    lua_pushboolean(L, 0); \
    lua_pushfstring(L, fmt, uv_strerror(err)); \
  } while (0)

static int luv_stream_read(lua_State* L) {
  luv_object_t* self = lua_touserdata(L, 1);
  luv_state_t*  curr = luv__sched_current(self->sched);
  if (uv_read_start(&self->h.stream, luv_alloc_cb, luv_read_cb)) {
    luv__stream_error(L, "read start: %s", self->sched->loop);
    return 2;
  }
  luv__cond_wait(&self->rouse, curr);
  return luv__state_yield(curr, 2);
}

static int luv_stream_write(lua_State* L) {
  luv_object_t* self = lua_touserdata(L, 1);
  luv_sched_t* sched = self->sched;

  size_t len;
  const char* chunk = luaL_checklstring(L, 2, &len);
  
  uv_buf_t buf = uv_buf_init((char*)chunk, len);

  luv_state_t* curr = luv__sched_current(sched);
  uv_write_t*  req  = &curr->req.write;
  if (uv_write(req, &self->h.stream, &buf, 1, luv_write_cb)) {
    luv__stream_error(L, "write: %s", sched->loop);
    return 2;
  }
  lua_settop(curr->L, 1);
  return luv__state_yield(curr, 1);
}

static int luv_stream_shutdown(lua_State* L) {
  luv_object_t* self = lua_touserdata(L, 1);
  luv_state_t*  curr = luv__sched_current(self->sched);
  uv_shutdown(&curr->req.shutdown, &self->h.stream, luv_shutdown_cb);
  luv__cond_wait(&self->rouse, curr);
  return luv__state_yield(curr, 1);
}
static int luv_stream_close(lua_State* L) {
  luv_object_t* self = lua_touserdata(L, 1);
  uv_close((uv_handle_t*)&self->h.stream, luv_close_cb);
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
  if (!(self->flags | LUV_SCLOSED)) {
    uv_close((uv_handle_t*)&self->h.stream, luv_close_cb);
    self->flags |= LUV_SCLOSED;
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
  {"shutdown",  luv_stream_shutdown},
  {"close",     luv_stream_close},
  {"__gc",      luv_stream_free},
  {"__tostring",luv_stream_tostring},
  {NULL,        NULL}
};


