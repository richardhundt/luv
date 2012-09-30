#include "luv.h"

void luvL_object_init(luv_state_t* state, luv_object_t* self) {
  ngx_queue_init(&self->rouse);
  ngx_queue_init(&self->queue);
  self->state = state;
  self->data  = NULL;
  self->flags = 0;
}

void luvL_object_close_cb(uv_handle_t* handle) {
  luv_object_t* self = container_of(handle, luv_object_t, h);
  self->flags |= LUV_OCLOSED;
}

void luvL_object_close(luv_object_t* self) {
  if (!luvL_object_is_closing(self)) {
    self->flags |= LUV_OCLOSING;
    uv_close(&self->h.handle, luvL_object_close_cb);
  }
}

