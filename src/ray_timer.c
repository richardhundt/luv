#include "ray_lib.h"
#include "ray_state.h"
#include "ray_timer.h"

int rayM_timer_await (ray_state_t* self, ray_state_t* that);
int rayM_timer_rouse (ray_state_t* self, ray_state_t* from);
int rayM_timer_close (ray_state_t* self);

static const ray_vtable_t ray_timer_v = {
  await : rayM_timer_await,
  rouse : rayM_timer_rouse,
  close : rayM_state_close
};

static void _sleep_cb(uv_timer_t* handle, int status) {
  ray_state_t* self = container_of(handle, ray_state_t, h);
  lua_pushboolean(self->L, 1);
  rayS_notify(self, 1);
  rayS_close(self);
}

static int ray_timer_sleep(lua_State* L) {
  lua_Number timeout = luaL_checknumber(L, 1);
  ray_state_t* curr = rayS_get_self(L);
  ray_state_t* self = rayS_new(L, RAY_TIMER_T, &ray_timer_v);
  self->L   = lua_newthread(L);
  self->ref = luaL_ref(L, LUA_REGISTRYINDEX);
  lua_xmove(L, self->L, 1); /* keep self in our stack to avoid GC */
  uv_timer_init(rayS_get_loop(L), &self->h.timer);
  uv_timer_start(&self->h.timer, _sleep_cb, (long)(timeout * 1000), 0L);
  return rayS_await(curr, self);
}

static void _timer_cb(uv_timer_t* h, int status) {
  ray_state_t* self = container_of(h, ray_state_t, h);
  lua_settop(self->L, 0);
  lua_pushboolean(self->L, 1);
  rayS_notify(self, 1);
}
int rayM_timer_await(ray_state_t* self, ray_state_t* that) {
  uv_timer_stop(&self->h.timer);
  return rayS_rouse(that, self);
}
int rayM_timer_rouse(ray_state_t* self, ray_state_t* from) {
  uv_timer_again(&self->h.timer);
  return 0;
}

/* Lua API */
static int ray_timer_new(lua_State* L) {
  ray_state_t* self = rayS_new(L, RAY_TIMER_T, &ray_timer_v);
  self->L   = lua_newthread(L);
  self->ref = luaL_ref(L, LUA_REGISTRYINDEX);
  uv_timer_init(rayS_get_loop(L), &self->h.timer);
  return 1;
}

static int ray_timer_start(lua_State* L) {
  ray_state_t* self = (ray_state_t*)luaL_checkudata(L, 1, RAY_TIMER_T);
  int64_t timeout = luaL_optlong(L, 2, 0L);
  int64_t repeat  = luaL_optlong(L, 3, 0L);
  uv_timer_start(&self->h.timer, _timer_cb, timeout, repeat);
  return 1;
}

static int ray_timer_again(lua_State* L) {
  ray_state_t* self = (ray_state_t*)luaL_checkudata(L, 1, RAY_TIMER_T);
  lua_pushinteger(L, uv_timer_again(&self->h.timer));
  return 1;
}

static int ray_timer_stop(lua_State* L) {
  ray_state_t* self = (ray_state_t*)luaL_checkudata(L, 1, RAY_TIMER_T);
  uv_timer_stop(&self->h.timer);
  return 1;
}

static int ray_timer_wait(lua_State *L) {
  ray_state_t* self = (ray_state_t*)luaL_checkudata(L, 1, RAY_TIMER_T);
  ray_state_t* curr = rayS_get_self(L);
  lua_settop(curr->L, 0);
  return rayS_await(curr, self);
}

static int ray_timer_free(lua_State *L) {
  ray_state_t* self = (ray_state_t*)luaL_checkudata(L, 1, RAY_TIMER_T);
  rayS_close(self);
  return 1;
}
static int ray_timer_tostring(lua_State *L) {
  ray_state_t* self = (ray_state_t*)luaL_checkudata(L, 1, RAY_TIMER_T);
  lua_pushfstring(L, "userdata<%s>: %p", RAY_TIMER_T, self);
  return 1;
}

static luaL_Reg ray_timer_funcs[] = {
  {"create",    ray_timer_new},
  {"sleep",     ray_timer_sleep},
  {NULL,        NULL}
};

static luaL_Reg ray_timer_meths[] = {
  {"start",     ray_timer_start},
  {"again",     ray_timer_again},
  {"stop",      ray_timer_stop},
  {"wait",      ray_timer_wait},
  {"__gc",      ray_timer_free},
  {"__tostring",ray_timer_tostring},
  {NULL,        NULL}
};

LUALIB_API int luaopen_ray_timer(lua_State* L) {
  rayL_module(L, "ray.timer", ray_timer_funcs);
  rayL_class (L, RAY_TIMER_T, ray_timer_meths);
  lua_pop(L, 1);
  return 1;
}


