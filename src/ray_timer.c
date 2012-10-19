#include "ray_lib.h"
#include "ray_actor.h"
#include "ray_timer.h"

static const ray_vtable_t timer_v = {
  await : rayM_timer_await,
  rouse : rayM_timer_rouse,
  close : rayM_timer_close
};

static void _sleep_cb(uv_timer_t* handle, int status) {
  ray_actor_t* self = container_of(handle, ray_actor_t, h);
  lua_pushboolean(self->L, 1);
  ray_notify(self, 1);
  ray_close(self);
}

static int timer_sleep(lua_State* L) {
  lua_Number timeout = luaL_checknumber(L, 1);
  ray_actor_t* curr = ray_get_self(L);
  ray_actor_t* self = ray_actor_new(L, RAY_TIMER_T, &timer_v);
  lua_xmove(L, self->L, 1); /* keep self in our stack to avoid GC */
  uv_timer_init(ray_get_loop(L), &self->h.timer);
  uv_timer_start(&self->h.timer, _sleep_cb, (long)(timeout * 1000), 0L);
  return ray_await(curr, self);
}

static void _timer_cb(uv_timer_t* h, int status) {
  ray_actor_t* self = container_of(h, ray_actor_t, h);
  lua_settop(self->L, 0);
  lua_pushboolean(self->L, 1);
  ray_notify(self, 1);
}
int rayM_timer_await(ray_actor_t* self, ray_actor_t* that) {
  uv_timer_stop(&self->h.timer);
  return ray_rouse(that, self);
}
int rayM_timer_rouse(ray_actor_t* self, ray_actor_t* from) {
  uv_timer_again(&self->h.timer);
  return 0;
}
int rayM_timer_close(ray_actor_t* self) {
  uv_close(&self->h.handle, NULL);
  return 0;
}

/* Lua API */
static int timer_new(lua_State* L) {
  ray_actor_t* self = ray_actor_new(L, RAY_TIMER_T, &timer_v);
  uv_timer_init(ray_get_loop(L), &self->h.timer);
  return 1;
}

static int timer_start(lua_State* L) {
  ray_actor_t* self = (ray_actor_t*)luaL_checkudata(L, 1, RAY_TIMER_T);
  int64_t timeout = luaL_optlong(L, 2, 0L);
  int64_t repeat  = luaL_optlong(L, 3, 0L);
  uv_timer_start(&self->h.timer, _timer_cb, timeout, repeat);
  return 1;
}

static int timer_again(lua_State* L) {
  ray_actor_t* self = (ray_actor_t*)luaL_checkudata(L, 1, RAY_TIMER_T);
  lua_pushinteger(L, uv_timer_again(&self->h.timer));
  return 1;
}

static int timer_stop(lua_State* L) {
  ray_actor_t* self = (ray_actor_t*)luaL_checkudata(L, 1, RAY_TIMER_T);
  uv_timer_stop(&self->h.timer);
  return 1;
}

static int timer_wait(lua_State *L) {
  ray_actor_t* self = (ray_actor_t*)luaL_checkudata(L, 1, RAY_TIMER_T);
  ray_actor_t* curr = ray_get_self(L);
  lua_settop(curr->L, 0);
  return ray_await(curr, self);
}

static int timer_free(lua_State *L) {
  ray_actor_t* self = (ray_actor_t*)luaL_checkudata(L, 1, RAY_TIMER_T);
  ray_close(self);
  ray_actor_free(self);
  return 1;
}
static int timer_tostring(lua_State *L) {
  ray_actor_t* self = (ray_actor_t*)luaL_checkudata(L, 1, RAY_TIMER_T);
  lua_pushfstring(L, "userdata<%s>: %p", RAY_TIMER_T, self);
  return 1;
}

static luaL_Reg timer_funcs[] = {
  {"create",    timer_new},
  {"sleep",     timer_sleep},
  {NULL,        NULL}
};

static luaL_Reg timer_meths[] = {
  {"start",     timer_start},
  {"again",     timer_again},
  {"stop",      timer_stop},
  {"wait",      timer_wait},
  {"__gc",      timer_free},
  {"__tostring",timer_tostring},
  {NULL,        NULL}
};

LUALIB_API int luaopen_ray_timer(lua_State* L) {
  rayL_module(L, "ray.timer", timer_funcs);
  rayL_class (L, RAY_TIMER_T, timer_meths);
  lua_pop(L, 1);

  ray_init_main(L);

  return 1;
}


