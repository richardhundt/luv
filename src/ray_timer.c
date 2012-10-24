#include "ray_lib.h"
#include "ray_actor.h"
#include "ray_timer.h"

static const ray_vtable_t timer_v = {
  send  : rayM_timer_send,
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
  ray_actor_t* curr = ray_current(L);
  ray_actor_t* self = ray_actor_new(L, RAY_TIMER_T, &timer_v);
  lua_xmove(L, self->L, 1); /* keep self in our stack to avoid GC */
  uv_timer_init(ray_get_loop(L), &self->h.timer);
  uv_timer_start(&self->h.timer, _sleep_cb, (long)(timeout * 1000), 0L);
  return ray_send(curr, self, RAY_YIELD);
}

static void _timer_cb(uv_timer_t* h, int status) {
  ray_actor_t* self = container_of(h, ray_actor_t, h);
  ray_send(self, self, 0);
}

int rayM_timer_recv(ray_actor_t* self, ray_actor_t* that) {
  uv_timer_stop(&self->h.timer);
  return 0;
}
int rayM_timer_send(ray_actor_t* self, ray_actor_t* from, int info) {
  switch (info) {
    case RAY_TIMER_INIT: {
      uv_loop_t* loop = ray_get_loop(self->L);
      uv_timer_init(loop, &self->h.timer);
      break;
    }
    case RAY_TIMER_STOP: {
      uv_timer_stop(&self->h.timer);
      break;
    }
    case RAY_TIMER_AGAIN: {
      uv_timer_again(&self->h.timer);
      break;
    }
    case RAY_TIMER_START: {
      int64_t timeout = luaL_optlong(self->L, 0, 0L);
      int64_t repeat  = luaL_optlong(self->L, 1, 0L);
      uv_timer_start(&self->h.timer, _timer_cb, timeout, repeat);
      lua_settop(self->L, 0);
      break;
    }
    case RAY_TIMER_CLOSE: {
      uv_close(&self->h.handle, NULL);
      break;
    }
    case RAY_TIMER_WAIT: {
      ray_enqueue(self, from);
      break;
    }
    default: {
      ray_notify(self, 0);
    }
  }

  return 0;
}
int rayM_timer_close(ray_actor_t* self) {
  uv_close(&self->h.handle, NULL);
  return 0;
}

/* Lua API */
static int timer_new(lua_State* L) {
  ray_actor_t* self = ray_actor_new(L, RAY_TIMER_T, &timer_v);
  ray_send(self, ray_current(L), RAY_TIMER_INIT);
  return 1;
}

static int timer_start(lua_State* L) {
  ray_actor_t* self = (ray_actor_t*)luaL_checkudata(L, 1, RAY_TIMER_T);
  ray_actor_t* from = ray_current(L);
  int64_t timeout = luaL_optlong(L, 2, 0L);
  int64_t repeat  = luaL_optlong(L, 3, 0L);
  lua_xmove(self->L, lua_gettop(self->L) - 1);
  ray_send(self, from, RAY_TIMER_START);
  return 1;
}

static int timer_again(lua_State* L) {
  ray_actor_t* self = (ray_actor_t*)luaL_checkudata(L, 1, RAY_TIMER_T);
  ray_send(self, ray_current(L), RAY_TIMER_AGAIN);
  return 1;
}

static int timer_stop(lua_State* L) {
  ray_actor_t* self = (ray_actor_t*)luaL_checkudata(L, 1, RAY_TIMER_T);
  ray_send(self, ray_current(L), RAY_TIMER_STOP);
  return 1;
}

static int timer_wait(lua_State *L) {
  ray_actor_t* self = (ray_actor_t*)luaL_checkudata(L, 1, RAY_TIMER_T);
  ray_actor_t* from = ray_current(L);
  ray_send(self, from, RAY_TIMER_WAIT);
  return ray_send(from, self, RAY_YIELD);
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


