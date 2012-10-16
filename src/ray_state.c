#include "ray_common.h"
#include "ray_state.h"

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
  if (v) self->v = *v;

  ngx_queue_init(&self->rouse);
  ngx_queue_init(&self->queue);

  self->ref = LUA_NOREF;

  return self;
}

static const ray_vtable_t ray_main_v = {
  suspend : rayM_main_suspend,
  resume  : rayM_main_resume,
  enqueue : rayM_main_enqueue,
  ready   : rayM_main_ready
};

int rayM_main_suspend(ray_state_t* self) {
  if (rayS_is_ready(self)) {
    self->flags &= ~RAY_READY;
    uv_loop_t* loop = rayS_get_loop(self->L);
    int active = 0;
    do {
      rayS_schedule(self);
      active = uv_run_once(loop);
      if (rayS_is_ready(self)) {
        break;
      }
    }
    while (active);
    /* nothing left to do, back in main */
    self->flags |= RAY_READY;
  }
  return lua_gettop(self->L);
}
int rayM_main_resume(ray_state_t* self) {
  rayS_ready(self);
  return lua_gettop(self->L);
}
int rayM_main_enqueue(ray_state_t* self, ray_state_t* that) {
  TRACE("here\n");
  int interrupt = ngx_queue_empty(&self->rouse);
  ngx_queue_insert_tail(&self->rouse, &that->queue);
  if (interrupt) {
    uv_async_send(&self->h.async);
    uv_ref(&self->h.handle);
  }
  return interrupt;
}
int rayM_main_ready(ray_state_t* self) {
  if (!rayS_is_ready(self)) {
    self->flags |= RAY_READY;
    return uv_async_send(&self->h.async);
  }
  return 0;
}


static void _async_cb(uv_async_t* handle, int status) {
  TRACE("interrupt loop\n");
  (void)handle;
  (void)status;
}

int rayS_init_main(lua_State* L) {
  lua_getfield(L, LUA_REGISTRYINDEX, RAY_STATE_MAIN);
  if (lua_isnil(L, -1)) {
    uv_loop_t* loop = rayS_get_loop(L);

    ray_state_t* self = lua_newuserdata(L, sizeof(ray_state_t));
    memset(self, 0, sizeof(ray_state_t));

    lua_pushvalue(L, -1);
    lua_setfield(L, LUA_REGISTRYINDEX, RAY_STATE_MAIN);

    /* TODO: make this callable from anywhere by recursing up the call stack */
    assert(lua_pushthread(L)); /* lua_pushthread returns 1 if main thread */

    lua_pushvalue(L, -2);
    lua_rawset(L, LUA_REGISTRYINDEX);

    self->v = ray_main_v;
    self->L = L;

    self->flags = RAY_READY;

    uv_async_init(loop, &self->h.async, _async_cb);
    uv_unref(&self->h.handle);

    ngx_queue_init(&self->rouse);
    ngx_queue_init(&self->queue);

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

int rayS_schedule(ray_state_t* self) {
  int seen = 0;
  ngx_queue_t* q;
  ray_state_t* s;
  while (!ngx_queue_empty(&self->rouse)) {
    q = ngx_queue_head(&self->rouse);
    s = ngx_queue_data(q, ray_state_t, queue);
    ngx_queue_remove(q);
    rayS_resume(s);
    ++seen;
  }
  return seen;
}

int rayS_notify(ray_state_t* self, int narg) {
  int seen;
  if (narg < 0 || narg > lua_gettop(self->L)) {
    narg = lua_gettop(self->L);
  }
  ngx_queue_t* q;
  ray_state_t* s;
  seen = 0;
  while (!ngx_queue_empty(&self->rouse)) {
    q = ngx_queue_head(&self->rouse);
    s = ngx_queue_data(q, ray_state_t, queue);
    ngx_queue_remove(q);
    rayS_send(self, s, narg);
    ++seen;
  }
  return seen;
}

/* polymorphic state methods with some default implementations */

static void _close_cb(uv_handle_t* handle) {
  ray_state_t* self = container_of(handle, ray_state_t, h);
  rayS_notify(self, 0);
}

int rayM_state_enqueue(ray_state_t* self, ray_state_t* that) {
  ngx_queue_insert_tail(&self->rouse, &that->queue);
  return 1;
}

int rayM_state_close(ray_state_t* self) {
  if (!rayS_is_closed(self) && self->h.handle.type) {
    self->flags |= RAY_CLOSED;

    /* unanchor from registry */
    lua_pushthread(self->L);
    lua_pushnil(self->L);
    lua_settable(self->L, LUA_REGISTRYINDEX);

    if (self->ref != LUA_NOREF) {
      luaL_unref(self->L, LUA_REGISTRYINDEX, self->ref);
    }
    uv_close(&self->h.handle, _close_cb);
  }
  return 0;
}
int rayM_state_send(ray_state_t* self, ray_state_t* recv, int narg) {
  rayS_xcopy(self, recv, narg);
  rayS_ready(recv);
  return 1;
}
int rayM_state_recv(ray_state_t* self) {
  if (lua_gettop(self->L) == 0) {
    return rayS_suspend(self);
  }
  else {
    return lua_gettop(self->L);
  }
}


/* suspend this state allowing others to run */
int rayS_suspend(ray_state_t* self) {
  return self->v.suspend(self);
}
/* resume this state */
int rayS_resume(ray_state_t* self) {
  return self->v.resume(self);
}
/* add another state to our queue */
int rayS_enqueue(ray_state_t* self, ray_state_t* that) {
  return self->v.enqueue(self, that);
}
/* signal that we're ready to be woken */
int rayS_ready(ray_state_t* self) {
  return self->v.ready(self);
}
/* terminate a state */
int rayS_close(ray_state_t* self) {
  return self->v.close(self);
}

/* send narg values from current stack to another's and ready it */
int rayS_send(ray_state_t* self, ray_state_t* that, int narg) {
  return self->v.send(self, that, narg);
}
/* recv values to our stack, from another and suspend if empty */
int rayS_recv(ray_state_t* self) {
  return self->v.recv(self);
}
