#include "luv.h"

void luvL_object_init(luv_state_t* state, luv_object_t* self) {
  ngx_queue_init(&self->rouse);
  ngx_queue_init(&self->queue);
  self->state = state;
  self->data  = NULL;
  self->flags = 0;
  self->count = 0;
  self->buf.base = NULL;
  self->buf.len  = 0;
}

void luvL_object_close_cb(uv_handle_t* handle) {
  luv_object_t* self = container_of(handle, luv_object_t, h);
  TRACE("object closed %p\n", self);
  self->flags |= LUV_OCLOSED;
  self->state = NULL;
  luvL_cond_signal(&self->rouse);
}

void luvL_object_close(luv_object_t* self) {
  if (!luvL_object_is_closing(self)) {
    TRACE("object closing %p, type: %i\n", self, self->h.handle.type);
    self->flags |= LUV_OCLOSING;
    uv_close(&self->h.handle, luvL_object_close_cb);
  }
}

