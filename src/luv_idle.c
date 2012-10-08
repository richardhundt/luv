#include "luv.h"

static void _idle_cb(uv_idle_t* handle, int status) {
  luv_object_t* self = container_of(handle, luv_object_t, h);
  ngx_queue_t* q;
  luv_state_t* s;
  ngx_queue_foreach(q, &self->rouse) {
    s = ngx_queue_data(q, luv_state_t, cond);
    lua_settop(s->L, 0);
    lua_pushinteger(s->L, status);
  }
  luvL_cond_broadcast(&self->rouse);
}

static int luv_new_idle(lua_State* L) {
  luv_object_t* self = (luv_object_t*)lua_newuserdata(L, sizeof(luv_object_t));
  luaL_getmetatable(L, LUV_IDLE_T);
  lua_setmetatable(L, -2);

  luv_state_t* curr = luvL_state_self(L);
  uv_idle_init(luvL_event_loop(L), &self->h.idle);
  luvL_object_init(curr, self);

  return 1;
}

/* methods */
static int luv_idle_start(lua_State* L) {
  luv_object_t* self = (luv_object_t*)luaL_checkudata(L, 1, LUV_IDLE_T);
  int rv = uv_idle_start(&self->h.idle, _idle_cb);
  lua_pushinteger(L, rv);
  return 1;
}

static int luv_idle_stop(lua_State* L) {
  luv_object_t* self = (luv_object_t*)luaL_checkudata(L, 1, LUV_IDLE_T);
  lua_pushinteger(L, uv_idle_stop(&self->h.idle));
  return 1;
}

static int luv_idle_wait(lua_State *L) {
  luv_object_t* self = (luv_object_t*)luaL_checkudata(L, 1, LUV_IDLE_T);
  luv_state_t* state = luvL_state_self(L);
  return luvL_cond_wait(&self->rouse, state);
}

static int luv_idle_free(lua_State *L) {
  luv_object_t* self = (luv_object_t*)lua_touserdata(L, 1);
  luvL_object_close(self);
  return 1;
}
static int luv_idle_tostring(lua_State *L) {
  luv_object_t* self = (luv_object_t*)luaL_checkudata(L, 1, LUV_IDLE_T);
  lua_pushfstring(L, "userdata<%s>: %p", LUV_IDLE_T, self);
  return 1;
}

luaL_Reg luv_idle_funcs[] = {
  {"create",    luv_new_idle},
  {NULL,        NULL}
};

luaL_Reg luv_idle_meths[] = {
  {"start",     luv_idle_start},
  {"stop",      luv_idle_stop},
  {"wait",      luv_idle_wait},
  {"__gc",      luv_idle_free},
  {"__tostring",luv_idle_tostring},
  {NULL,        NULL}
};
