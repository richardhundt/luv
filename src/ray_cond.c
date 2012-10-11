#include "ray_common.h"
#include "ray_state.h"
#include "ray_cond.h"

int rayL_cond_init(ray_cond_t* cond) {
  ngx_queue_init(cond);
  return 1;
}
int rayL_cond_wait(ray_cond_t* cond, ray_state_t* curr) {
  ngx_queue_insert_tail(cond, &curr->cond);
  TRACE("SUSPEND state %p\n", curr);
  return rayL_state_suspend(curr);
}
int rayL_cond_signal(ray_cond_t* cond) {
  ngx_queue_t* q;
  ray_state_t* s;
  if (!ngx_queue_empty(cond)) {
    q = ngx_queue_head(cond);
    s = ngx_queue_data(q, ray_state_t, cond);
    ngx_queue_remove(q);
    TRACE("READY state %p\n", s);
    rayL_state_ready(s);
    return 1;
  }
  return 0;
}
int rayL_cond_broadcast(ray_cond_t* cond) {
  ngx_queue_t* q;
  ray_state_t* s;
  int roused = 0;
  while (!ngx_queue_empty(cond)) {
    q = ngx_queue_head(cond);
    s = ngx_queue_data(q, ray_state_t, cond);
    ngx_queue_remove(q);
    TRACE("READY state %p\n", s);
    rayL_state_ready(s);
    ++roused;
  }
  return roused;
}

static int ray_cond_new(lua_State* L) {
  ray_cond_t* cond = (ray_cond_t*)lua_newuserdata(L, sizeof(ray_cond_t));

  lua_pushvalue(L, 1);
  luaL_getmetatable(L, RAY_COND_T);
  lua_setmetatable(L, -2);

  rayL_cond_init(cond);
  return 1;
}

static int ray_cond_wait(lua_State *L) {
  ray_cond_t*  cond  = (ray_cond_t*)lua_touserdata(L, 1);
  ray_state_t* curr;
  if (!lua_isnoneornil(L, 2)) {
    curr = (ray_state_t*)luaL_checkudata(L, 2, RAY_FIBER_T);
  }
  else {
    curr = (ray_state_t*)rayL_state_self(L);
  }
  rayL_cond_wait(cond, curr);
  return 1;
}
static int ray_cond_signal(lua_State *L) {
  ray_cond_t* cond = (ray_cond_t*)lua_touserdata(L, 1);
  rayL_cond_signal(cond);
  return 1;
}
static int ray_cond_broadcast(lua_State *L) {
  ray_cond_t* cond = (ray_cond_t*)lua_touserdata(L, 1);
  rayL_cond_broadcast(cond);
  return 1;
}

static int ray_cond_free(lua_State *L) {
  ray_cond_t* cond = (ray_cond_t*)lua_touserdata(L, 1);
  (void)cond;
  return 0;
}

static int ray_cond_tostring(lua_State *L) {
  ray_cond_t* cond = (ray_cond_t*)luaL_checkudata(L, 1, RAY_COND_T);
  lua_pushfstring(L, "userdata<%s>: %p", RAY_COND_T, cond);
  return 1;
}

luaL_Reg ray_cond_funcs[] = {
  {"create",    ray_cond_new}
};

luaL_Reg ray_cond_meths[] = {
  {"wait",      ray_cond_wait},
  {"signal",    ray_cond_signal},
  {"broadcast", ray_cond_broadcast},
  {"__gc",      ray_cond_free},
  {"__tostring",ray_cond_tostring},
  {NULL,        NULL}
};

