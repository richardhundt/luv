#include "luv_core.h"
#include "luv_cond.h"

int luv__cond_init(luv_cond_t* cond) {
  ngx_queue_init(cond);
  return 1;
}
int luv__cond_wait(luv_cond_t* cond, luv_state_t* curr) {
  luv__state_suspend(curr);
  ngx_queue_insert_tail(cond, &curr->cond);
  return 1;
}
int luv__cond_signal(luv_cond_t* cond) {
  ngx_queue_t* q;
  luv_state_t* s;
  if (!ngx_queue_empty(cond)) {
    q = ngx_queue_head(cond);
    s = ngx_queue_data(q, luv_state_t, cond);
    ngx_queue_remove(q);
    luv__state_resume(s);
    return 1;
  }
  return 0;
}
int luv__cond_broadcast(luv_cond_t* cond) {
  ngx_queue_t* q;
  luv_state_t* s;
  int roused = 0;
  while (!ngx_queue_empty(cond)) {
    q = ngx_queue_head(cond);
    s = ngx_queue_data(q, luv_state_t, cond);
    ngx_queue_remove(q);
    luv__state_resume(s);
    ++roused;
  }
  return roused;
}

int luv_cond_create(lua_State* L) {
  luv_cond_t* cond = lua_newuserdata(L, sizeof(luv_cond_t));

  lua_pushvalue(L, 1);
  luaL_getmetatable(L, LUV_COND_T);
  lua_setmetatable(L, -2);

  luv__cond_init(cond);
  return 1;
}

static int luv_cond_wait(lua_State *L) {
  luv_cond_t*  cond  = lua_touserdata(L, 1);
  luv_sched_t* sched = lua_touserdata(L, lua_upvalueindex(1));
  luv_state_t* curr;
  if (!lua_isnoneornil(L, 2)) {
    curr = luaL_checkudata(L, 2, LUV_FIBER_T);
  }
  else {
    curr = luv__sched_current(sched);
  }
  luv__cond_wait(cond, curr);
  return 1;
}
static int luv_cond_signal(lua_State *L) {
  luv_cond_t* cond = lua_touserdata(L, 1);
  luv__cond_signal(cond);
  return 1;
}
static int luv_cond_broadcast(lua_State *L) {
  luv_cond_t* cond = lua_touserdata(L, 1);
  luv__cond_broadcast(cond);
  return 1;
}

static int luv_cond_free(lua_State *L) {
  luv_cond_t* cond = lua_touserdata(L, 1);
  (void)cond;
  return 0;
}

static int luv_cond_tostring(lua_State *L) {
  luv_cond_t* cond = luaL_checkudata(L, 1, LUV_COND_T);
  lua_pushfstring(L, "userdata<%s>: %p", LUV_COND_T, cond);
  return 1;
}

static luaL_Reg luv_cond_meths[] = {
  {"wait",      luv_cond_wait},
  {"signal",    luv_cond_signal},
  {"broadcast", luv_cond_broadcast},
  {"__gc",      luv_cond_free},
  {"__tostring",luv_cond_tostring},
  {NULL,        NULL}
};

LUALIB_API int luaopenL_luv_cond(lua_State *L) {
  luaL_newmetatable(L, LUV_COND_T);
  lua_pushvalue(L, -1);
  lua_setfield(L, -2, "__index");
  lua_getfield(L, LUA_REGISTRYINDEX, LUV_SCHED_O);
  luaL_openlib(L, NULL, luv_cond_meths, 1);
  lua_pop(L, 1);
  return 1;
}

