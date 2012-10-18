#include "ray_common.h"
#include "ray_actor.h"

uv_loop_t* ray_get_loop(lua_State* L) {
  lua_getfield(L, LUA_REGISTRYINDEX, RAY_EVENT_LOOP);
  uv_loop_t* loop = lua_touserdata(L, -1);
  lua_pop(L, 1);
  if (!loop) {
    loop = uv_loop_new();
    lua_pushlightuserdata(L, (void*)loop);
    lua_setfield(L, LUA_REGISTRYINDEX, RAY_EVENT_LOOP);
  }
  return loop;
}

ray_actor_t* ray_get_self(lua_State* L) {
  lua_pushthread(L);
  lua_rawget(L, LUA_REGISTRYINDEX);
  ray_actor_t* self = (ray_actor_t*)lua_touserdata(L, -1);
  if (!self) self = ray_get_main(L);
  lua_pop(L, 1);
  return self;
}

ray_actor_t* rayL_actor_new(lua_State* L, const char* m, const ray_vtable_t* v) {
  ray_actor_t* self = (ray_actor_t*)lua_newuserdata(L, sizeof(ray_actor_t));
  memset(self, 0, sizeof(ray_actor_t));

  if (m) {
    luaL_getmetatable(L, m);
    lua_setmetatable(L, -2);
  }
  if (v) {
    self->v = *v;
  }

  ngx_queue_init(&self->queue);
  ngx_queue_init(&self->cond);

  self->ref = LUA_NOREF;

  return self;
}

static const ray_vtable_t ray_main_v = {
  await : rayM_main_await,
  rouse : rayM_main_rouse
};

int rayM_main_rouse(ray_actor_t* self, ray_actor_t* from) {
  TRACE("main rouse %p from %p\n", self, from);
  if (ngx_queue_empty(&self->queue)) {
    uv_async_send(&self->h.async);
  }
  return 1;
}
int rayM_main_await(ray_actor_t* self, ray_actor_t* that) {
  TRACE("ENTER MAIN AWAIT, queue empty? %i\n", ngx_queue_empty(&self->queue));

  uv_loop_t* loop = ray_get_loop(self->L);

  int events = 0;
  do {
    ray_notify(self, 0);
    events = uv_run_once(loop);
    if (ray_is_active(self)) break;
  }
  while (events);

  self->flags |= RAY_ACTIVE;
  ngx_queue_remove(&self->cond);

  TRACE("UNLOOP\n");
  return lua_gettop(self->L);
}

static void _async_cb(uv_async_t* handle, int status) {
  (void)handle;
  (void)status;
}

int ray_init_main(lua_State* L) {
  lua_getfield(L, LUA_REGISTRYINDEX, RAY_STATE_MAIN);
  if (lua_isnil(L, -1)) {
    ray_actor_t* self = rayL_actor_new(L, NULL, &ray_main_v);
    lua_pushvalue(L, -1);
    lua_setfield(L, LUA_REGISTRYINDEX, RAY_STATE_MAIN);

    assert(lua_pushthread(L));
    lua_pushvalue(L, -2);
    lua_rawset(L, LUA_REGISTRYINDEX);

    self->flags = RAY_ACTIVE;
    self->L = L;

    ngx_queue_init(&self->queue);
    ngx_queue_init(&self->cond);

    uv_async_init(ray_get_loop(L), &self->h.async, _async_cb);
    uv_unref(&self->h.handle);

    lua_pop(L, 1);
  }
  lua_pop(L, 1);
  return 1;
}

ray_actor_t* ray_get_main(lua_State* L) {
  lua_getfield(L, LUA_REGISTRYINDEX, RAY_STATE_MAIN);
  ray_actor_t* self = (ray_actor_t*)lua_touserdata(L, -1);
  lua_pop(L, 1);
  return self;
}

int ray_xcopy(ray_actor_t* a, ray_actor_t* b, int narg) {
  int top = lua_gettop(a->L);
  int i, base;
  base = lua_gettop(a->L) - narg + 1;
  lua_checkstack(a->L, narg);
  lua_checkstack(b->L, narg);
  lua_settop(b->L, 0);
  for (i = base; i < base + narg; i++) {
    lua_pushvalue(a->L, i);
  }
  lua_xmove(a->L, b->L, narg);
  assert(lua_gettop(a->L) == top);
  return narg;
}

int ray_notify(ray_actor_t* self, int narg) {
  int count = 0;
  ngx_queue_t* q;
  ray_actor_t* s;
  if (narg == LUA_MULTRET) narg = lua_gettop(self->L);
  while (!ngx_queue_empty(&self->queue)) {
    q = ngx_queue_head(&self->queue);
    s = ngx_queue_data(q, ray_actor_t, cond);
    if (narg) ray_xcopy(self, s, narg);
    ray_rouse(s, self);
    count++;
  }
  if (narg) lua_pop(self->L, narg);
  return count;
}

/* polymorphic state methods with some default implementations */
int rayM_state_close(ray_actor_t* self) {
  /* unanchor from registry */
  if (self->L) {
    lua_settop(self->L, 0);
    if (self->ref != LUA_NOREF) {
      luaL_unref(self->L, LUA_REGISTRYINDEX, self->ref);
      self->ref = LUA_NOREF;
    }
    else {
      lua_pushthread(self->L);
      lua_pushnil(self->L);
      lua_settable(self->L, LUA_REGISTRYINDEX);
    }
    self->L = NULL;
  }
  return 1;
}

/* resume this state */
int ray_rouse(ray_actor_t* self, ray_actor_t* from) {
  TRACE("rouse %p, from %p\n", self, from);
  assert(!ray_is_active(self));
  self->flags |= RAY_ACTIVE;
  ngx_queue_remove(&self->cond);
  return self->v.rouse(self, from);
}
/* wait for signal */
int ray_await(ray_actor_t* self, ray_actor_t* that) {
  TRACE("await %p that %p\n", self, that);
  assert(ray_is_active(self));
  self->flags &= ~RAY_ACTIVE;
  ngx_queue_insert_tail(&that->queue, &self->cond);
  return self->v.await(self, that);
}
/* terminate a state */
int ray_close(ray_actor_t* self) {
  assert(!ray_is_closed(self));
  self->flags |= RAY_CLOSED;
  return self->v.close(self);
}
