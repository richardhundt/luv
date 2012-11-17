#include "ray_common.h"
#include "ray_lib.h"
#include "ray_state.h"
#include "ray_codec.h"

ray_state_t* ray_get_main(lua_State* L) {
  lua_getfield(L, LUA_REGISTRYINDEX, RAY_MAIN);
  ray_state_t* self = (ray_state_t*)lua_touserdata(L, -1);
  lua_pop(L, 1);
  return self;
}

uv_loop_t* ray_get_loop(lua_State* L) {
  lua_getfield(L, LUA_REGISTRYINDEX, RAY_LOOP);
  uv_loop_t* loop = lua_touserdata(L, -1);
  lua_pop(L, 1);
  if (!loop) {
    loop = uv_loop_new();
    lua_pushlightuserdata(L, (void*)loop);
    lua_setfield(L, LUA_REGISTRYINDEX, RAY_LOOP);
  }
  return loop;
}

ray_state_t* ray_current(lua_State* L) {
  lua_pushthread(L);
  lua_rawget(L, LUA_REGISTRYINDEX);
  ray_state_t* self = (ray_state_t*)lua_touserdata(L, -1);
  if (!self) self = ray_get_main(L);
  lua_pop(L, 1);
  return self;
}

int ray_push(ray_state_t* self, int narg) {
  int i, base;
  if (narg == LUA_MULTRET) narg = lua_gettop(self->L);
  base = lua_gettop(self->L) - narg + 1;
  lua_checkstack(self->L, narg);
  for (i = base; i < base + narg; i++) {
    lua_pushvalue(self->L, i);
  }
  return narg;
}

int ray_xcopy(ray_state_t* a, ray_state_t* b, int narg) {
  int top = lua_gettop(a->L);
  int i, base;
  if (narg == LUA_MULTRET) narg = lua_gettop(a->L);
  base = lua_gettop(a->L) - narg + 1;
  lua_checkstack(a->L, narg);
  lua_checkstack(b->L, narg);
  for (i = base; i < base + narg; i++) {
    lua_pushvalue(a->L, i);
  }
  lua_xmove(a->L, b->L, narg);
  assert(lua_gettop(a->L) == top);
  return narg;
}

ray_state_t* ray_state_new(lua_State* L, const char* m, const ray_vtable_t* v) {
  ray_state_t* self = (ray_state_t*)lua_newuserdata(L, sizeof(ray_state_t));
  memset(self, 0, sizeof(ray_state_t));

  if (m) {
    luaL_getmetatable(L, m);
  }
  else {
    luaL_getmetatable(L, RAY_STATE_T);
  }
  lua_setmetatable(L, -2);

  self->v     = *v;
  self->tid   = (uv_thread_t)uv_thread_self();
  self->L     = lua_newthread(L);
  self->L_ref = luaL_ref(L, LUA_REGISTRYINDEX);

  ngx_queue_init(&self->cond);

  if (self->v.alloc) self->v.alloc(self);

  return self;
}

int ray_yield(ray_state_t* self) {
  return self->v.yield(self);
}
int ray_react(ray_state_t* self) {
  return self->v.react(self);
}
int ray_close(ray_state_t* self) {
  self->v.close(self);
  if (self->L_ref != LUA_NOREF) {
    luaL_unref(self->L, LUA_REGISTRYINDEX, self->L_ref);
    self->L_ref = LUA_NOREF;
  }
  return 1;
}

int ray_ready(ray_state_t* that) {
  ray_state_t* boss = ray_get_main(that->L);
  TRACE("boss: %p, that: %p\n", boss, that);
  if (that == boss) {
    return ray_react(that);
  }
  ngx_queue_t* ready = (ngx_queue_t*)boss->u.data;
  ray_cond_enqueue(ready, that);
  uv_async_send(&boss->h.async);
  return 1;
}

int ray_error(ray_state_t* from) {
  ray_state_t* boss  = ray_get_main(from->L);
  lua_xmove(from->L, boss->L, 1);
  return lua_error(boss->L);
}

/* default state meta-methods */
static int state_free(lua_State* L) {
  ray_state_t* self = lua_touserdata(L, 1);
  ray_close(self);
  return 1;
}
static int state_tostring(lua_State* L) {
  ray_state_t* self = (ray_state_t*)lua_touserdata(L, 1);
  const char* name = RAY_STATE_T;
  luaL_getmetafield(L, 1, "__name");
  if (!lua_isnil(L, -1)) {
    lua_pop(L, 1);
    name = lua_tostring(L, -1);
  }
  lua_pushfstring(L, "userdata<%s>: %p", name, self);
  return 1;
}

static luaL_Reg state_meths[] = {
  {"__gc",        state_free},
  {"__tostring",  state_tostring},
  {NULL,          NULL}
};

/* allows us to interrupt the event loop if main gets a message */
static void _main_async_cb(uv_async_t* handle, int status) {
  TRACE("main seen async\n");
  (void)status;
  (void)handle;
}

static int _main_yield(ray_state_t* self) {
  int events = 0;
  lua_State* L = self->L;
  uv_loop_t* loop = ray_get_loop(L);
  lua_settop(L, 0);

  ngx_queue_t* ready = (ngx_queue_t*)self->u.data;

  self->flags &= ~RAY_ACTIVE;
  do {
    while (!ngx_queue_empty(ready)) {
      ngx_queue_t* q = ngx_queue_head(ready);
      ray_state_t* a = ngx_queue_data(q, ray_state_t, cond);
      ray_cond_dequeue(a);
      a->v.react(a);
    }
    TRACE("enter uv_run_once...\n");
    events = uv_run_once(loop);
    TRACE("leave uv_run_once, events: %i\n", events);

    if (ray_is_active(self)) break;
  }
  while (events || !ngx_queue_empty(ready));
  TRACE("main unloop\n");
  self->flags |= RAY_ACTIVE;
  return lua_gettop(L);
}

static int _main_react(ray_state_t* self) {
  self->flags |= RAY_ACTIVE;
  return uv_async_send(&self->h.async);
}

static int _main_close(ray_state_t* self) {
  if (self->u.data) {
    free(self->u.data);
  }
  return 1;
}

static ray_vtable_t main_v = {
  react: _main_react,
  yield: _main_yield,
  close: _main_close
};

/* initialize the main state */
int ray_init_main(lua_State* L) {
  lua_getfield(L, LUA_REGISTRYINDEX, RAY_MAIN);
  if (lua_isnil(L, -1)) {

#ifndef WIN32
    signal(SIGPIPE, SIG_IGN);
#endif

    /* default state metatable */
    rayL_class(L, RAY_STATE_T, state_meths);
    lua_pop(L, 1);

    ray_state_t* self = ray_state_new(L, RAY_STATE_T, &main_v);
    lua_pushvalue(L, -1);
    lua_setfield(L, LUA_REGISTRYINDEX, RAY_MAIN);

    self->L = L;

    assert(lua_pushthread(L)); /* must be main thread */

    /* set up our reverse lookup */
    lua_pushvalue(L, -2);
    lua_rawset(L, LUA_REGISTRYINDEX);
    assert(ray_current(L) == self);

    self->tid = (uv_thread_t)uv_thread_self();

    /* queue for fibers */
    self->u.data = malloc(sizeof(ngx_queue_t));
    ngx_queue_init((ngx_queue_t*)self->u.data);

    uv_async_init(ray_get_loop(L), &self->h.async, _main_async_cb);
    uv_unref(&self->h.handle);

    lua_pop(L, 1);
  }
  lua_pop(L, 1);
  return 1;
}

