#include "ray_lib.h"
#include "ray_common.h"
#include "ray_timer.h"

/* Lua API */
static int ray_timer_new(lua_State* L) {
  ray_object_t* self = rayL_object_new(L, RAY_TIMER_T);
  uv_timer_init(self->loop, &self->h.timer);
  return 1;
}

static void _timer_cb(uv_timer_t* h, int status) {
  ray_object_t* self = rayL_object_self(h);
  ngx_queue_t* q;
  ray_state_t* s;
  ngx_queue_foreach(q, &self->rouse) {
    s = ngx_queue_data(q, ray_state_t, cond);
    TRACE("rouse %p\n", s);
    lua_settop(s->L, 0);
    lua_pushinteger(s->L, status);
  }
  rayL_cond_broadcast(&self->rouse);
}

static int ray_timer_start(lua_State* L) {
  ray_object_t* self = rayL_object_check(L, 1, RAY_TIMER_T);
  int64_t timeout = luaL_optlong(L, 2, 0L);
  int64_t repeat  = luaL_optlong(L, 3, 0L);
  int rv = uv_timer_start(&self->h.timer, _timer_cb, timeout, repeat);
  lua_pushinteger(L, rv);
  return 1;
}

static int ray_timer_again(lua_State* L) {
  ray_object_t* self = rayL_object_check(L, 1, RAY_TIMER_T);
  lua_pushinteger(L, uv_timer_again(&self->h.timer));
  return 1;
}

static int ray_timer_stop(lua_State* L) {
  ray_object_t* self = rayL_object_check(L, 1, RAY_TIMER_T);
  lua_pushinteger(L, uv_timer_stop(&self->h.timer));
  return 1;
}

static int ray_timer_wait(lua_State *L) {
  ray_object_t* self = rayL_object_check(L, 1, RAY_TIMER_T);
  ray_state_t* state = rayL_state_self(L);
  return rayL_cond_wait(&self->rouse, state);
}

static int ray_timer_free(lua_State *L) {
  ray_object_t* self = rayL_object_check(L, 1, RAY_TIMER_T);
  rayL_object_close(self);
  return 1;
}
static int ray_timer_tostring(lua_State *L) {
  ray_object_t* self = rayL_object_check(L, 1, RAY_TIMER_T);
  lua_pushfstring(L, "userdata<%s>: %p", RAY_TIMER_T, self);
  return 1;
}

static luaL_Reg ray_timer_funcs[] = {
  {"create",    ray_timer_new},
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

LUALIB_API int luaopen_timer(lua_State* L) {
  rayL_module_new(L, "ray.timer", ray_timer_funcs);
  rayL_class_new (L, RAY_TIMER_T, ray_timer_meths);
  lua_pop(L, 1);
  return 1;
}


