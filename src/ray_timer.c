#include "ray_lib.h"
#include "ray_actor.h"
#include "ray_timer.h"
#include "ray_cond.h"

static void _sleep_cb(uv_timer_t* handle, int status) {
  ray_actor_t* self = container_of(handle, ray_actor_t, h);
  ray_actor_t* wait = (ray_actor_t*)self->data;
  lua_pushboolean(self->L, 1);
  ray_send(wait, self, ray_msg(RAY_DATA,1));
  uv_close(&self->h.handle, NULL);
  ray_free(self);
}

static void _timer_cb(uv_timer_t* h, int status) {
  ray_actor_t* self = container_of(h, ray_actor_t, h);
  ray_cond_t*  cond = (ray_cond_t*)self->data;
  lua_pushboolean(self->L, 1);
  ray_cond_signal(cond, ray_msg(RAY_DATA,1));
}

static int _timer_yield(ray_actor_t* self) {
  uv_timer_stop(&self->h.timer);
  return 1;
}

static int _timer_react(ray_actor_t* self, ray_msg_t msg) {
  switch (ray_msg_type(msg)) {
    case RAY_DATA: {
      if (self->flags & RAY_TIMER_STARTED) {
        ray_timer_stop(self);
      }

      lua_settop(self->L, 0);
      ray_mailbox_get(self);

      uint64_t timeout, repeat;
      timeout = luaL_optlong(self->L, 1, 0L);
      repeat  = luaL_optlong(self->L, 2, 0L);

      ray_timer_start(self, timeout, repeat);
      lua_settop(self->L, 0);

      self->flags |= RAY_TIMER_STARTED;
    }
    default: {
      luaL_error(self->L, "bad message");
    }
  }
  return 0;
}

static int _timer_close(ray_actor_t* self) {
  uv_close(&self->h.handle, NULL);
  if (self->data) ray_cond_free((ray_cond_t*)self->data);
  ray_cond_t* cond = (ray_cond_t*)self->data;
  ray_cond_signal(cond, ray_msg(RAY_ABORT,0));
  return 1;
}

static ray_vtable_t timer_v = {
  react: _timer_react,
  yield: _timer_yield,
  close: _timer_close
};
ray_actor_t* ray_timer_new(lua_State* L) {
  ray_actor_t* self = ray_actor_new(L, RAY_TIMER_T, &timer_v);
  ray_cond_t*  cond = ray_cond_new(L);
  self->data = cond;
  return self;
}

int ray_timer_wait(ray_actor_t* self, ray_actor_t* wait) {
  ray_cond_t* cond = (ray_cond_t*)self->data;
  return ray_cond_wait(cond, wait);
}

int ray_timer_again(ray_actor_t* self) {
  return uv_timer_again(&self->h.timer);
}

/* Lua API */
static int timer_new(lua_State* L) {
  ray_timer_new(L);
  return 1;
}

static int timer_sleep(lua_State* L) {
  lua_Number timeout = luaL_checknumber(L, 1);

  ray_actor_t* curr = ray_current(L);
  ray_actor_t* self = ray_actor_new(L, RAY_TIMER_T, &timer_v);

  lua_xmove(L, self->L, 1); /* keep self in our stack to avoid GC */

  uv_timer_init(ray_get_loop(L), &self->h.timer);
  uv_timer_start(&self->h.timer, _sleep_cb, (long)(timeout * 1000), 0L);

  self->data = curr;

  return ray_recv(curr);
}

static int timer_start(lua_State* L) {
  ray_actor_t* self = (ray_actor_t*)luaL_checkudata(L, 1, RAY_TIMER_T);
  int64_t timeout = luaL_optlong(self->L, 1, 0L);
  int64_t repeat  = luaL_optlong(self->L, 2, 0L);
  ray_timer_start(self, curr, timeout, repeat);
  return 1;
}

static int timer_again(lua_State* L) {
  ray_actor_t* self = (ray_actor_t*)luaL_checkudata(L, 1, RAY_TIMER_T);
  ray_timer_again(self, ray_current(L));
  return 1;
}

static int timer_stop(lua_State* L) {
  ray_actor_t* self = (ray_actor_t*)luaL_checkudata(L, 1, RAY_TIMER_T);
  ray_timer_stop(self);
  return 1;
}

static int timer_wait(lua_State *L) {
  ray_actor_t* self = (ray_actor_t*)luaL_checkudata(L, 1, RAY_TIMER_T);
  ray_actor_t* curr = ray_current(L);
  return ray_timer_wait(self, curr);
}

static int timer_free(lua_State *L) {
  ray_actor_t* self = (ray_actor_t*)luaL_checkudata(L, 1, RAY_TIMER_T);
  ray_free(self);
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


