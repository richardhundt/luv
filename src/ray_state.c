#include "ray_common.h"
#include "ray_state.h"
#include "ray_hash.h"

uv_loop_t* rayS_get_loop(lua_State* L) {
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

ray_state_t* rayS_get_self(lua_State* L) {
  lua_pushthread(L);
  lua_rawget(L, LUA_REGISTRYINDEX);
  ray_state_t* self = (ray_state_t*)lua_touserdata(L, -1);
  if (!self) self = rayS_get_main(L);
  lua_pop(L, 1);
  return self;
}

ray_state_t* rayS_new(lua_State* L, const char* m, const ray_vtable_t* v) {
  ray_state_t* self = (ray_state_t*)lua_newuserdata(L, sizeof(ray_state_t));
  memset(self, 0, sizeof(ray_state_t));

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

static const ray_vtable_t ray_idle_v = {
  await : rayM_idle_await,
  rouse : rayM_idle_rouse,
  close : rayM_idle_close
};

static const ray_vtable_t ray_main_v = {
  await : rayM_main_await,
  rouse : rayM_main_rouse
};

int rayM_idle_await(ray_state_t* self, ray_state_t* that) {
  TRACE("idle await\n");
  ngx_queue_insert_tail(&that->queue, &self->cond);
  return rayS_rouse(that, self);
}
int rayM_idle_rouse(ray_state_t* self, ray_state_t* from) {
  if (rayS_is_active(self)) return 0;
  self->flags |= RAY_ACTIVE;
  TRACE("rousing idle\n");
  uv_loop_t* loop = (uv_loop_t*)self->u.data;
  if (uv_run_once(loop)) {
    ngx_queue_insert_tail(&from->queue, &self->cond);
  }

  self->flags &= ~RAY_ACTIVE;
  return 1;
}
int rayM_idle_close(ray_state_t* self) {
  if (!rayS_is_closed(self)) {
    self->flags |= RAY_CLOSED;
    uv_loop_t* loop = (uv_loop_t*)self->u.data;
    uv_loop_delete(loop);
    return 1;
  }
  return 0;
}

int rayM_main_rouse(ray_state_t* self, ray_state_t* from) {
  TRACE("main rouse %p from %p\n", self, from);
  if (rayS_is_active(self)) {
    self->flags &= ~RAY_ACTIVE;
    uv_async_send(&self->h.async);
  }
  return 1;
}
int rayM_main_await(ray_state_t* self, ray_state_t* that) {
  TRACE("main await\n");
  if (rayS_is_active(self)) return 0;
  TRACE("main is active...\n");
  self->flags |= RAY_ACTIVE;

  ngx_queue_insert_tail(&that->queue, &self->cond);

  ray_state_t* idle = (ray_state_t*)rayL_hash_get(self->u.hash, "idle");
  ngx_queue_t* q;
  ray_state_t* s;
  if (ngx_queue_empty(&self->queue) && idle) {
    rayS_rouse(idle, self);
  }
  while(!ngx_queue_empty(&self->queue)) {
    TRACE("loop top\n");
    q = ngx_queue_head(&self->queue);
    s = ngx_queue_data(q, ray_state_t, cond);
    ngx_queue_remove(q);
    rayS_rouse(s, self);
    if (!rayS_is_active(self)) break;
    if (ngx_queue_empty(&self->queue) && idle) {
      rayS_rouse(idle, self);
    }
  }

  self->flags &= ~RAY_ACTIVE;
  TRACE("unloop\n");
  return 1;
}

static void _async_cb(uv_async_t* handle, int status) {
  (void)handle;
  (void)status;
}

int rayS_init_main(lua_State* L) {
  lua_getfield(L, LUA_REGISTRYINDEX, RAY_STATE_MAIN);
  if (lua_isnil(L, -1)) {
    ray_state_t* self = rayS_new(L, NULL, &ray_main_v);
    lua_pushvalue(L, -1);
    lua_setfield(L, LUA_REGISTRYINDEX, RAY_STATE_MAIN);

    assert(lua_pushthread(L));
    lua_pushvalue(L, -2);
    lua_rawset(L, LUA_REGISTRYINDEX);

    ray_state_t* idle = rayS_new(L, NULL, &ray_idle_v);
    idle->u.data      = rayS_get_loop(L);
    idle->ref         = luaL_ref(L, LUA_REGISTRYINDEX);

    self->L = L;
    self->u.hash = rayL_hash_new(4);
    rayL_hash_set(self->u.hash, "idle", idle);

    ngx_queue_init(&self->queue);
    ngx_queue_init(&self->cond);

    uv_async_init(rayS_get_loop(L), &self->h.async, _async_cb);
    uv_unref(&self->h.handle);

    lua_pop(L, 1);
  }
  lua_pop(L, 1);
  return 1;
}

ray_state_t* rayS_get_main(lua_State* L) {
  lua_getfield(L, LUA_REGISTRYINDEX, RAY_STATE_MAIN);
  ray_state_t* self = (ray_state_t*)lua_touserdata(L, -1);
  lua_pop(L, 1);
  return self;
}

int rayS_xcopy(ray_state_t* a, ray_state_t* b, int narg) {
  int i, base;
  base = lua_gettop(a->L) - narg + 1;
  lua_checkstack(a->L, narg);
  lua_checkstack(b->L, narg);
  for (i = base; i <= base + narg; i++) {
    lua_pushvalue(a->L, i);
  }
  lua_xmove(a->L, b->L, narg);
  return narg;
}

int rayS_notify(ray_state_t* self, int narg) {
  int count = 0;
  ngx_queue_t* q;
  ray_state_t* s;
  while (!ngx_queue_empty(&self->queue)) {
    q = ngx_queue_head(&self->queue);
    s = ngx_queue_data(q, ray_state_t, cond);
    ngx_queue_remove(q);
    rayS_xcopy(self, s, narg);
    rayS_rouse(s, self);
    count++;
  }
  return count;
}

/* polymorphic state methods with some default implementations */
int rayM_state_close(ray_state_t* self) {
  if (!rayS_is_closed(self)) {
    self->flags |= RAY_CLOSED;
    /* unanchor from registry */
    if (self->L) lua_settop(self->L, 0);
    if (self->ref != LUA_NOREF) {
      luaL_unref(self->L, LUA_REGISTRYINDEX, self->ref);
      self->ref = LUA_NOREF;
    }
    else {
      lua_pushthread(self->L);
      lua_pushnil(self->L);
      lua_settable(self->L, LUA_REGISTRYINDEX);
    }

    if (self->h.handle.type) {
      uv_close(&self->h.handle, NULL);
    }
    return 1;
  }
  return 0;
}

/* resume this state */
int rayS_rouse(ray_state_t* self, ray_state_t* from) {
  return self->v.rouse(self, from);
}
/* wait for signal */
int rayS_await(ray_state_t* self, ray_state_t* that) {
  return self->v.await(self, that);
}
/* terminate a state */
int rayS_close(ray_state_t* self) {
  return self->v.close(self);
}
