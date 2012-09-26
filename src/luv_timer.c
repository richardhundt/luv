#include "luv.h"
#include "luv_core.h"
#include "luv_cond.h"

#include "luv_object.h"

#define LUV_TIMER_T "luv.timer"

static void luv_timer_cb(uv_timer_t* handle, int status) {
  luv_object_t* self = container_of(handle, luv_object_t, h);
  ngx_queue_t* q;
  luv_fiber_t* f;
  ngx_queue_foreach(q, &self->rouse) {
    f = ngx_queue_data(q, luv_fiber_t, cond);
    lua_settop(f->L, 0);
    lua_pushinteger(f->L, status);
  }
  luv__cond_broadcast(&self->rouse);
}

static int luv_new_timer(lua_State* L) {
  luv_sched_t* sched = lua_touserdata(L, lua_upvalueindex(1));
  luv_object_t* self = lua_newuserdata(L, sizeof(luv_object_t));
  luaL_getmetatable(L, LUV_TIMER_T);
  lua_setmetatable(L, -2);

  uv_timer_init(sched->loop, &self->h.timer);
  luv__object_init(sched, self);

  return 1;
}

/* methods */
static int luv_timer_start(lua_State* L) {
  luv_object_t* self = luaL_checkudata(L, 1, LUV_TIMER_T);
  int64_t timeout = luaL_optlong(L, 2, 0L);
  int64_t repeat  = luaL_optlong(L, 3, 0L);
  int rv = uv_timer_start(&self->h.timer, luv_timer_cb, timeout, repeat);
  lua_pushinteger(L, rv);
  return 1;
}

static int luv_timer_again(lua_State* L) {
  luv_object_t* self = luaL_checkudata(L, 1, LUV_TIMER_T);
  lua_pushinteger(L, uv_timer_again(&self->h.timer));
  return 1;
}

static int luv_timer_stop(lua_State* L) {
  luv_object_t* self = luaL_checkudata(L, 1, LUV_TIMER_T);
  lua_pushinteger(L, uv_timer_stop(&self->h.timer));
  return 1;
}

static int luv_timer_next(lua_State *L) {
  luv_object_t* self = luaL_checkudata(L, 1, LUV_TIMER_T);
  luv_sched_t* sched = self->sched;
  luv_state_t* state = luv__sched_current(sched);
  luv__cond_wait(&self->rouse, state);
  return luv__state_yield(state, 1);
}

static int luv_timer_free(lua_State *L) {
  luv_object_t* self = lua_touserdata(L, 1);
  luv__object_close(self);
  return 1;
}
static int luv_timer_tostring(lua_State *L) {
  luv_object_t* self = luaL_checkudata(L, 1, LUV_TIMER_T);
  lua_pushfstring(L, "userdata<%s>: %p", LUV_TIMER_T, self);
  return 1;
}

static luaL_Reg luv_timer_funcs[] = {
  {"create",    luv_new_timer},
  {NULL,        NULL}
};

static luaL_Reg luv_timer_meths[] = {
  {"start",     luv_timer_start},
  {"again",     luv_timer_again},
  {"stop",      luv_timer_stop},
  {"next",      luv_timer_next},
  {"__gc",      luv_timer_free},
  {"__tostring",luv_timer_tostring},
  {NULL,        NULL}
};

LUALIB_API int luaopenL_luv_timer(lua_State *L) {
  luaL_newmetatable(L, LUV_TIMER_T);
  lua_pushvalue(L, -1);
  lua_setfield(L, -2, "__index");
  luaL_openlib(L, NULL, luv_timer_meths, 0);
  lua_pop(L, 1);

  /* timer */
  luv__new_namespace(L, "luv_timer");
  lua_getfield(L, LUA_REGISTRYINDEX, LUV_SCHED_O);
  luaL_openlib(L, NULL, luv_timer_funcs, 1);

  /* luv.timer */
  lua_getfield(L, LUA_REGISTRYINDEX, LUV_REG_KEY);
  lua_pushvalue(L, -2);
  lua_setfield(L, -2, "timer");
  lua_pop(L, 1);

  return 1;
}

