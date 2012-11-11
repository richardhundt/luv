#include "ray_lib.h"
#include "ray_actor.h"
#include "ray_queue.h"
#include "ray_cond.h"

ray_cond_t* ray_cond_new(lua_State* L) {
  ray_cond_t* self = (ray_cond_t*)malloc(sizeof(ray_cond_t));
  ngx_queue_init(&self->queue);
  return self;
}

int ray_cond_wait(ray_cond_t* self, ray_actor_t* that) {
  ngx_queue_insert_head(&self->queue, &that->cond);
  ray_incref(that);
  return ray_recv(that);
}

int ray_cond_signal(ray_cond_t* self, ray_actor_t* from, ray_msg_t msg) {
  int narg;
  narg = ray_msg_data(msg);
  if (from && narg < 0) narg = lua_gettop(from->L);

  ngx_queue_t* queue = &self->queue;
  if (ngx_queue_empty(queue)) return 0;

  ngx_queue_t* item;
  ray_actor_t* wait;
  while (!ngx_queue_empty(queue)) {
    item = ngx_queue_last(queue);
    wait = ngx_queue_data(item, ray_actor_t, cond);
    ngx_queue_remove(&wait->cond);
    ray_decref(wait);
    ray_push(from, narg);
    ray_send(wait, from, msg);
  }

  lua_pop(from->L, narg);
  return 1;
}

void ray_cond_free(ray_cond_t* self) {
  free(self);
}
