#include "ray_lib.h"
#include "ray_state.h"
#include "ray_queue.h"

ray_queue_t* ray_queue_new(lua_State* L, int size) {
  ray_queue_t* self = (ray_queue_t*)malloc(sizeof(ray_queue_t));
  ray_queue_init(self, L, size);
  return self;
}

void ray_queue_init(ray_queue_t* self, lua_State* L, size_t size) {
  self->L     = lua_newthread(L);
  self->L_ref = luaL_ref(L, LUA_REGISTRYINDEX);
  self->size  = size;
  self->head  = 0;
  self->tail  = 0;
  lua_checkstack(self->L, self->size);
  lua_settop(self->L, self->size);
  ngx_queue_init(&self->wput);
  ngx_queue_init(&self->wget);
}

int ray_queue_put(ray_queue_t* self, ray_state_t* curr) {
  lua_checkstack(self->L, 1);
  lua_xmove(curr->L, self->L, 1);

  if (!ngx_queue_empty(&self->wget)) {
    ngx_queue_t* q = ngx_queue_head(&self->wget);
    ray_state_t* s = ngx_queue_data(q, ray_state_t, cond);
    ray_cond_dequeue(s);
    ray_ready(s);
  }

  if (ray_queue_full(self)) {
    ray_cond_enqueue(&self->wput, curr);
    return ray_yield(curr);
  }

  lua_replace(self->L, ray_queue_head(self));
  ++self->head;
  return 1;
}

int ray_queue_get(ray_queue_t* self, ray_state_t* curr) {
  if (!ngx_queue_empty(&self->wput)) {
    ngx_queue_t* q = ngx_queue_head(&self->wput);
    ray_state_t* s = ngx_queue_data(q, ray_state_t, cond);
    ray_cond_dequeue(s);
    ray_ready(s);
  }

  if (ray_queue_empty(self)) {
    ray_cond_enqueue(&self->wget, curr);
    return ray_yield(curr);
  }

  int slot = ray_queue_slot(self, self->tail);

  lua_pushvalue(self->L, slot);
  lua_pushnil(self->L);
  lua_replace(self->L, slot);

  lua_xmove(self->L, curr->L, 1);

  ++self->tail;

  int top = lua_gettop(self->L);
  if (top > self->size) {
    lua_checkstack(self->L, 1);
    while (top-- > self->size && !ray_queue_full(self)) {
      lua_pushvalue(self->L, self->size + 1);
      lua_replace(self->L, ray_queue_head(self));
      lua_remove(self->L, self->size + 1);
      ++self->head;
    }
  }

  return 1;
}

void ray_queue_free(ray_queue_t* self) {
  lua_settop(self->L, 0);
  luaL_unref(self->L, LUA_REGISTRYINDEX, self->L_ref);
  free(self);
}
