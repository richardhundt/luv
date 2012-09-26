#include "luv_core.h"
#include "luv_object.h"

void luv__object_init(luv_sched_t* sched, luv_object_t* self) {
  ngx_queue_init(&self->rouse);
  ngx_queue_init(&self->queue);
  self->sched = sched;
  self->data  = NULL;
  self->flags = 0;
}

void luv__object_close_cb(uv_handle_t* handle) {
  (void)handle;
}

void luv__object_close(luv_object_t* self) {
  uv_close(&self->h.handle, luv__object_close_cb);
}
