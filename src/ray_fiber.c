#include <stdarg.h>

#include "ray_lib.h"
#include "ray_actor.h"
#include "ray_fiber.h"

static int _fiber_RAY_AWAIT(ray_actor_t* self, ray_actor_t* from, int info) {
  lua_State* L = self->L;
  self->flags &= ~RAY_ACTIVE;
  return lua_yield(L, lua_gettop(L));
}

static int _fiber_RAY_READY(ray_actor_t* self, ray_actor_t* from, int info) {
  lua_State* L = self->L;
  self->flags |= RAY_ACTIVE;
  lua_pushlightuserdata(L, from);
  return ray_send(ray_get_main(L), self, RAY_SCHED);
}

static int _fiber_RAY_EVAL(ray_actor_t* self, ray_actor_t* from, int info) {
  int narg;
  lua_State* L = self->L;
  if (!(self->flags & RAY_FIBER_START)) {
    self->flags |= RAY_FIBER_START;
    TRACE("first entry\n");
    narg = lua_gettop(L) - 1;
  }
  else {
    /* saved from RAY_READY call */
    from = lua_touserdata(L, -1);
    lua_pop(L, -1);
    narg = lua_gettop(L);
  }

  self->flags |= RAY_ACTIVE;
  TRACE("ENTER VM\n");
  int rc = lua_resume(L, narg);
  TRACE("LEAVE VM\n");

  switch (rc) {
    case LUA_YIELD: {
      TRACE("seen LUA_YIELD, active? %i\n", ray_is_active(self));
      if (self->flags & RAY_ACTIVE) {
        /* detected coroutine.yield back in queue */
        self->flags &= ~RAY_ACTIVE;
        ray_send(self, from, RAY_READY);
      }
      break;
    }
    case 0: {
      /* normal exit, notify waiters, and close */
      TRACE("normal exit, notify waiting...\n");
      narg = lua_gettop(L);
      ray_send(from, self, narg);
      ray_send(self, NULL, RAY_CLOSE);
      break;
    }
    default: {
      TRACE("ERROR: in fiber\n");
      /* propagate the error back to the caller */
      ray_send(from, self, 1);
      ray_send(self, NULL, RAY_CLOSE);
      lua_error(from->L);
    }
  }
  return narg;
}

static int _fiber_RAY_CLOSE(ray_actor_t* self, ray_actor_t* from, int info) {
  lua_State* L = self->L;

  /* clear our reverse mapping to allow __gc */
  lua_pushthread(L);
  lua_pushnil(L);
  lua_settable(L, LUA_REGISTRYINDEX);

  self->flags |= RAY_CLOSED;
  break;
}

static int _fiber_RAY_DATA(ray_actor_t* self, ray_actor_t* from, int info) {
  return lua_gettop(self->L);
}

int rayM_fiber_send(ray_actor_t* self, ray_actor_t* from, int info) {
  switch (info) {
    case RAY_AWAIT:
      return _fiber_RAY_AWAIT(self, from, info);
    case RAY_READY:
      return _fiber_RAY_READY(self, from, info);
    case RAY_EVAL:
      return _fiber_RAY_EVAL(self, from, info);
    case RAY_CLOSE:
      return _fiber_RAY_CLOSE(self, from, info);
    default: {
      assert(info >= RAY_DATA);
      return _fiber_RAY_DATA(self, from, info);
    }
  }
  return 0;
}

ray_actor_t* ray_fiber_new(lua_State* L) {
  int top = lua_gettop(L);
  ray_actor_t* self = ray_actor_new(L, RAY_FIBER_T, rayM_fiber_send);

  lua_State* L2 = self->L;

  /* reverse mapping from L2 to self */
  lua_pushthread(L2);
  lua_pushlightuserdata(L2, self);
  lua_rawset(L2, LUA_REGISTRYINDEX);

  assert(ray_current(L2) == self);
  assert(lua_gettop(L) == top + 1);
  return self;
}

/* Lua API */
static int fiber_new(lua_State* L) {
  luaL_checktype(L, 1, LUA_TFUNCTION);
  int narg = lua_gettop(L);
  ray_actor_t* self = ray_fiber_new(L);
  lua_State*   L2   = self->L;

  /* return self to caller */
  lua_insert(L, 1);

  /* move the remaining arguments */
  lua_checkstack(L2, narg);
  lua_xmove(L, L2, narg);

  assert(lua_gettop(L) == 1);
  return 1;
}

static int fiber_ready(lua_State* L) {
  ray_actor_t* self = (ray_actor_t*)lua_touserdata(L, 1);
  ray_actor_t* curr = ray_current(L);
  ray_send(self, curr, RAY_READY);
  return 1;
}

static int fiber_spawn(lua_State* L) {
  fiber_new(L);
  ray_actor_t* self = (ray_actor_t*)lua_touserdata(L, 1);
  ray_actor_t* curr = ray_current(L);
  ray_send(self, curr, RAY_READY);
  return 1;
}

static int fiber_join(lua_State* L) {
  ray_actor_t* self = (ray_actor_t*)luaL_checkudata(L, 1, RAY_FIBER_T);
  ray_actor_t* from = ray_current(L);

  if (ray_is_closed(self)) {
    int narg = lua_gettop(self->L);
    ray_send(from, self, narg);
    return narg;
  }

  /* we're ready for a timeslice */
  ray_send(self, from, RAY_READY);

  /* tell the current thread to wait for us */
  return ray_send(from, self, RAY_AWAIT);
}

static int fiber_free(lua_State* L) {
  ray_actor_t* self = (ray_actor_t*)lua_touserdata(L, 1);
  if (!(self->flags & RAY_CLOSED)) {
    ray_notify(self, LUA_MULTRET);
  }
  lua_settop(self->L, 0);
  ray_actor_free(self);
  return 1;
}
static int fiber_tostring(lua_State* L) {
  ray_actor_t* self = (ray_actor_t*)lua_touserdata(L, 1);
  lua_pushfstring(L, "userdata<%s>: %p", RAY_FIBER_T, self);
  return 1;
}

static luaL_Reg fiber_funcs[] = {
  {"create",    fiber_new},
  {"spawn",     fiber_spawn},
  {NULL,        NULL}
};

static luaL_Reg fiber_meths[] = {
  {"join",      fiber_join},
  {"ready",     fiber_ready},
  {"__gc",      fiber_free},
  {"__tostring",fiber_tostring},
  {NULL,        NULL}
};

LUALIB_API int luaopen_ray_fiber(lua_State* L) {
  rayL_module(L, "ray.fiber", fiber_funcs);

  /* borrow coroutine.yield (fast on LJ2) */
  lua_getglobal(L, "coroutine");
  lua_getfield(L, -1, "yield");
  lua_setfield(L, -3, "yield");
  lua_pop(L, 1); /* coroutine */

  rayL_class(L, RAY_FIBER_T, fiber_meths);
  lua_pop(L, 1);

  ray_init_main(L);

  return 1;
}


