#include "ray_lib.h"
#include "ray_state.h"
#include "ray_queue.h"
#include "ray_cond.h"

ray_cond_t* ray_cond_new(lua_State* L) {
  ray_cond_t* self = (ray_cond_t*)malloc(sizeof(ray_cond_t));
  ngx_queue_init(&self->queue);
  return self;
}

int ray_cond_wait(ray_cond_t* self, ray_state_t* that) {
  ray_cond_enqueue(&self->queue, that);
  return ray_yield(that);
}

int ray_cond_signal(ray_cond_t* self, ray_state_t* from, int narg) {
  if (from && narg < 0) narg = lua_gettop(from->L);

  ngx_queue_t* queue = &self->queue;
  if (ngx_queue_empty(queue)) return 0;

  ray_state_t* wait;
  ngx_queue_t* item;
  ngx_queue_t* tail = ngx_queue_last(queue);

  while (!ngx_queue_empty(queue)) {
    item = ngx_queue_head(queue);
    wait = ngx_queue_data(item, ray_state_t, cond);
    ray_cond_dequeue(wait);
    ray_push(from, narg);
    ray_xcopy(from, wait, narg);
    ray_ready(wait);
    if (item == tail) break;
  }

  if (from) lua_pop(from->L, narg);
  return 1;
}

void ray_cond_free(ray_cond_t* self) {
  free(self);
}
