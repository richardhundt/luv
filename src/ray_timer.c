#include "ray_lib.h"
#include "ray_hash.h"
#include "ray_state.h"
#include "ray_timer.h"

static void _sleep_cb(uv_timer_t* handle, int status) {
  rayS_ready((ray_state_t*)handle->data);
  free(handle);
}

static int ray_timer_sleep(lua_State* L) {
  lua_Number timeout = luaL_checknumber(L, 1);
  ray_state_t* state = rayS_get_self(L);
  uv_timer_t*  timer = (uv_timer_t*)malloc(sizeof(uv_timer_t));
  timer->data = state;
  uv_timer_init(rayS_get_loop(L), timer);
  uv_timer_start(timer, _sleep_cb, (long)(timeout * 1000), 0L);
  return rayS_suspend(state);
}

int rayM_timer_suspend (ray_state_t* self);
int rayM_timer_resume  (ray_state_t* self);
int rayM_timer_ready   (ray_state_t* self);

static const ray_vtable_t ray_timer_v = {
  suspend : rayM_timer_suspend,
  resume  : rayM_timer_resume,
  ready   : rayM_timer_ready,
  send    : rayM_state_send,
  recv    : rayM_state_recv,
  close   : rayM_state_close
};

static void _timer_cb(uv_timer_t* h, int status) {
  ray_state_t* self = container_of(h, ray_state_t, h);
  TRACE("tick... %p\n", self->L);
  lua_settop(self->L, 0);
  lua_pushinteger(self->L, status);
  rayS_resume(self);
}
int rayM_timer_suspend(ray_state_t* self) {
  return uv_timer_stop(&self->h.timer);
}
int rayM_timer_resume(ray_state_t* self) {
  return rayS_notify(self, 1);
}
int rayM_timer_ready(ray_state_t* self) {
  int64_t timeout = (int64_t)rayL_hash_get(self->u.hash, "timeout");
  int64_t repeat  = (int64_t)rayL_hash_get(self->u.hash, "repeat" );
  return uv_timer_start(&self->h.timer, _timer_cb, timeout, repeat);
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

  self->u.hash = rayL_hash_new(4);
  rayL_hash_set(self->u.hash, "timeout", (void*)timeout);
  rayL_hash_set(self->u.hash, "repeat",  (void*)repeat );

  rayS_ready(self);
  return 1;
}

static int ray_timer_again(lua_State* L) {
  ray_state_t* self = (ray_state_t*)luaL_checkudata(L, 1, RAY_TIMER_T);
  lua_pushinteger(L, uv_timer_again(&self->h.timer));
  return 1;
}

static int ray_timer_stop(lua_State* L) {
  ray_state_t* self = (ray_state_t*)luaL_checkudata(L, 1, RAY_TIMER_T);
  rayS_suspend(self);
  return 1;
}

static int ray_timer_wait(lua_State *L) {
  ray_state_t* self = (ray_state_t*)luaL_checkudata(L, 1, RAY_TIMER_T);
  ray_state_t* curr = rayS_get_self(L);
  lua_settop(curr->L, 0);
  ngx_queue_insert_tail(&self->rouse, &curr->queue);
  TRACE("suspending... %p\n", curr);
  return rayS_suspend(curr);
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


