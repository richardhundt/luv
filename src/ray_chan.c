#include "ray_lib.h"
#include "ray_chan.h"

/* unbuffered channels */

ray_chan_t* ray_chan_new() {
  ray_chan_t* self = (ray_chan_t*)malloc(sizeof(ray_chan_t));
  return ray_chan_init(self);
}

ray_chan_t* ray_chan_init(ray_chan_t* self) {
  self->nval = 0;
  ngx_queue_init(&self->wput);
  ngx_queue_init(&self->wget);
  return self;
}

int ray_chan_put(ray_chan_t* self, ray_state_t* curr, size_t nval) {
  if (ngx_queue_empty(&self->wget)) {
    self->nval = nval;
    ray_cond_enqueue(&self->wput, curr);
    return ray_yield(curr);
  }
  else {
    ngx_queue_t* q = ngx_queue_head(&self->wget);
    ray_state_t* s = ngx_queue_data(q, ray_state_t, cond);
    ray_cond_dequeue(s);
    lua_settop(s->L, 0);
    lua_checkstack(curr->L, nval);
    lua_xmove(curr->L, s->L, nval);
    ray_ready(s);
    return 0;
  }
}

int ray_chan_get(ray_chan_t* self, ray_state_t* curr) {
  if (ngx_queue_empty(&self->wput)) {
    TRACE("[GET] - none waiting to put, suspending %p...\n", curr);
    ray_cond_enqueue(&self->wget, curr);
    return ray_yield(curr);
  }
  else {
    TRACE("[GET] - have wput sending to %p...\n", curr);
    ngx_queue_t* q = ngx_queue_head(&self->wget);
    ray_state_t* s = ngx_queue_data(q, ray_state_t, cond);
    ray_cond_dequeue(s);
    lua_settop(curr->L, 0);
    lua_checkstack(curr->L, self->nval);
    lua_xmove(s->L, curr->L, self->nval);
    ray_ready(s);
    return self->nval;
  }
}

void ray_chan_free(ray_chan_t* self) {
  /* TODO: wake up waiters */
  assert(ngx_queue_empty(&self->wput));
  assert(ngx_queue_empty(&self->wget));
  free(self);
}

/* Lua API */
static int chan_new(lua_State* L) {
  ray_chan_t* self = (ray_chan_t*)lua_newuserdata(L, sizeof(ray_chan_t));
  ray_chan_init(self);
  luaL_getmetatable(L, RAY_CHAN_T);
  lua_setmetatable(L, -1);
  return 1;
}

static int chan_get(lua_State* L) {
  ray_chan_t*  self = (ray_chan_t*)lua_touserdata(L, 1);
  ray_state_t* curr = ray_current(L);
  return ray_chan_get(self, curr);
}

static int chan_put(lua_State* L) {
  ray_chan_t*  self = (ray_chan_t*)lua_touserdata(L, 1);
  ray_state_t* curr = ray_current(L);
  size_t nval = lua_gettop(L) - 1;
  return ray_chan_put(self, curr, nval);
}

static int chan_free(lua_State* L) {
  ray_chan_t* self = (ray_chan_t*)lua_touserdata(L, 1);
  ray_chan_free(self);
  return 1;
}

static luaL_Reg chan_funcs[] = {
  {"create",  chan_new},
  {NULL,      NULL}
};
static luaL_Reg chan_meths[] = {
  {"put",     chan_put},
  {"get",     chan_get},
  {"__gc",    chan_free},
  {NULL,      NULL}
};

LUALIB_API int luaopen_ray_chan(lua_State* L) {
  rayL_module(L, "ray.channel", chan_funcs);
  rayL_class(L, RAY_CHAN_T, chan_meths);
  lua_pop(L, 1);

  ray_init_main(L);
  return 1;
}
