#include "luv.h"

int luvL_cond_init(luv_cond_t* cond) {
  ngx_queue_init(cond);
  return 1;
}
int luvL_cond_wait(luv_cond_t* cond, luv_state_t* curr) {
  ngx_queue_insert_tail(cond, &curr->cond);
  TRACE("SUSPEND state %p\n", curr);
  return luvL_state_suspend(curr);
}
int luvL_cond_signal(luv_cond_t* cond) {
  ngx_queue_t* q;
  luv_state_t* s;
  if (!ngx_queue_empty(cond)) {
    q = ngx_queue_head(cond);
    s = ngx_queue_data(q, luv_state_t, cond);
    ngx_queue_remove(q);
    TRACE("READY state %p\n", s);
    luvL_state_ready(s);
    return 1;
  }
  return 0;
}
int luvL_cond_broadcast(luv_cond_t* cond) {
  ngx_queue_t* q;
  luv_state_t* s;
  int roused = 0;
  while (!ngx_queue_empty(cond)) {
    q = ngx_queue_head(cond);
    s = ngx_queue_data(q, luv_state_t, cond);
    ngx_queue_remove(q);
    TRACE("READY state %p\n", s);
    luvL_state_ready(s);
    ++roused;
  }
  return roused;
}

static int luv_new_cond(lua_State* L) {
  luv_cond_t* cond = (luv_cond_t*)lua_newuserdata(L, sizeof(luv_cond_t));

  lua_pushvalue(L, 1);
  luaL_getmetatable(L, LUV_COND_T);
  lua_setmetatable(L, -2);

  luvL_cond_init(cond);
  return 1;
}

static int luv_cond_wait(lua_State *L) {
  luv_cond_t*  cond  = (luv_cond_t*)lua_touserdata(L, 1);
  luv_state_t* curr;
  if (!lua_isnoneornil(L, 2)) {
    curr = (luv_state_t*)luaL_checkudata(L, 2, LUV_FIBER_T);
  }
  else {
    curr = (luv_state_t*)luvL_state_self(L);
  }
  luvL_cond_wait(cond, curr);
  return 1;
}
static int luv_cond_signal(lua_State *L) {
  luv_cond_t* cond = (luv_cond_t*)lua_touserdata(L, 1);
  luvL_cond_signal(cond);
  return 1;
}
static int luv_cond_broadcast(lua_State *L) {
  luv_cond_t* cond = (luv_cond_t*)lua_touserdata(L, 1);
  luvL_cond_broadcast(cond);
  return 1;
}

static int luv_cond_free(lua_State *L) {
  luv_cond_t* cond = (luv_cond_t*)lua_touserdata(L, 1);
  (void)cond;
  return 0;
}

static int luv_cond_tostring(lua_State *L) {
  luv_cond_t* cond = (luv_cond_t*)luaL_checkudata(L, 1, LUV_COND_T);
  lua_pushfstring(L, "userdata<%s>: %p", LUV_COND_T, cond);
  return 1;
}

luaL_Reg luv_cond_funcs[] = {
  {"create",    luv_new_cond}
};

luaL_Reg luv_cond_meths[] = {
  {"wait",      luv_cond_wait},
  {"signal",    luv_cond_signal},
  {"broadcast", luv_cond_broadcast},
  {"__gc",      luv_cond_free},
  {"__tostring",luv_cond_tostring},
  {NULL,        NULL}
};

