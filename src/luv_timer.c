#include "luv_common.h"
#include "luv_timer.h"

int luvL_timer_init(luv_object_t* self) {
  self->hash = luvL_hash_new(16);
  return uv_timer_init(self->loop, &self->h.timer);
}

static void _timer_cb(uv_timer_t* handle, int status) {
  luv_object_t* self = container_of(handle, luv_object_t, h);
  luv_event_cb  cb   = (luv_event_cb)luvL_hash_lookup(self->hash, "on_timer");
  cb(self, NULL);
}

int luvL_timer_start(luv_object_t* self, luv_event_cb cb, int64_t timeout, int64_t repeat) {
  luvL_hash_set(self->hash, "on_timer", (void*)cb);
  luvL_hash_set(self->hash, "timeout",  (void*)timeout);
  luvL_hash_set(self->hash, "repeat",   (void*)repeat);
  return uv_timer_start(&self->h.timer, _timer_cb, timeout, repeat);
}


/* Lua API */
static int luv_timer_new(lua_State* L) {
  luv_object_t* self = luvL_object_new(L, LUV_TIMER_T);
  luvL_timer_init(L, self);
  return 1;
}

static void _luv_timer_cb(luv_object_t* self, void* arg) {
  luv_list_t*  q;
  luv_state_t* s;
  luvL_list_foreach(q, &self->rouse) {
    s = luvL_list_data(q, luv_state_t, cond);
    TRACE("rouse %p\n", s);
    lua_settop(s->L, 0);
    lua_pushinteger(s->L, status);
  }
  luvL_cond_broadcast(&self->rouse);
}

static int luv_timer_start(lua_State* L) {
  luv_object_t* self = luvL_object_check(L, 1, LUV_TIMER_T);
  int64_t timeout = luaL_optlong(L, 2, 0L);
  int64_t repeat  = luaL_optlong(L, 3, 0L);
  int rv = luvL_timer_start(self, _luv_timer_cb, timeout, repeat);
  lua_pushinteger(L, rv);
  return 1;
}

static int luv_timer_again(lua_State* L) {
  luv_object_t* self = (luv_object_t*)luaL_checkudata(L, 1, LUV_TIMER_T);
  lua_pushinteger(L, uv_timer_again(&self->h.timer));
  return 1;
}

static int luv_timer_stop(lua_State* L) {
  luv_object_t* self = (luv_object_t*)luaL_checkudata(L, 1, LUV_TIMER_T);
  lua_pushinteger(L, uv_timer_stop(&self->h.timer));
  return 1;
}

static int luv_timer_wait(lua_State *L) {
  luv_object_t* self = (luv_object_t*)luaL_checkudata(L, 1, LUV_TIMER_T);
  luv_state_t* state = luvL_state_self(L);
  return luvL_cond_wait(&self->rouse, state);
}

static int luv_timer_free(lua_State *L) {
  luv_object_t* self = (luv_object_t*)lua_touserdata(L, 1);
  luvL_object_close(self);
  return 1;
}
static int luv_timer_tostring(lua_State *L) {
  luv_object_t* self = (luv_object_t*)luaL_checkudata(L, 1, LUV_TIMER_T);
  lua_pushfstring(L, "userdata<%s>: %p", LUV_TIMER_T, self);
  return 1;
}

luaL_Reg luv_timer_funcs[] = {
  {"create",    luv_timer_new},
  {NULL,        NULL}
};

luaL_Reg luv_timer_meths[] = {
  {"start",     luv_timer_start},
  {"again",     luv_timer_again},
  {"stop",      luv_timer_stop},
  {"wait",      luv_timer_wait},
  {"__gc",      luv_timer_free},
  {"__tostring",luv_timer_tostring},
  {NULL,        NULL}
};

