#include "ray_lib.h"
#include "ray_cond.h"
#include "ray_actor.h"
#include "ray_object.h"

void rayL_object_init(ray_object_t* self, uv_loop_t* loop) {
  rayL_list_init(&self->rouse);
  rayL_list_init(&self->queue);
  self->flags = 0;
  self->loop  = loop;
  self->count = 0;

  uv_buf_init(&self->buf);

  self->hash = NULL;
  self->list = NULL;
  self->data = NULL;
}

ray_object_t* rayL_object_new(lua_State* L, const char* meta) {
  ray_object_t* self = (ray_object_t*)lua_newuserdata(L, sizeof(ray_object_t));
  luaL_getmetatable(L, meta);
  lua_setmetatable(L, -2);
  rayL_object_init(self, rayL_event_loop(L));
  return self;
}

ray_object_t* rayL_object_check(lua_State* L, int idx, const char* meta) {
  return (ray_object_t*)rayL_checkudata(L, idx, meta);
}

static void _close_cb(uv_handle_t* handle) {
  ray_object_t* self = rayL_object_self(handle);
  TRACE("object closed %p\n", self);
  self->flags |= RAY_OCLOSED;
  rayL_cond_signal(&self->rouse);
}

void rayL_object_close(ray_object_t* self) {
  if (!rayL_object_is_closing(self)) {
    TRACE("object closing %p, type: %i\n", self, self->h.handle.type);
    self->flags |= RAY_OCLOSING;
    uv_close(&self->h.handle, _close_cb);
  }
}

int rayL_object_free(ray_object_t* self) {
  if (self->hash) {
    rayL_hash_free(self->hash);
    self->hash = NULL;
  }
  if (self->list) {
    self->list = NULL;
    rayL_list_free(self->list);
  }
}


