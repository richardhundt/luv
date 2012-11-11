#include <stdarg.h>

#include "ray_lib.h"
#include "ray_actor.h"
#include "ray_fiber.h"
#include "ray_cond.h"

static int _fiber_yield(ray_actor_t* self) {
  lua_State* L = self->L;
  self->flags &= ~RAY_ACTIVE;
  TRACE("YIELD, narg: %i\n", lua_gettop(L));
  return lua_yield(L, lua_gettop(L));
}

static int _fiber_react(ray_actor_t* self, ray_actor_t* from, ray_msg_t msg) {
  int narg = ray_msg_data(msg);
  lua_State* L = self->L;

  if (ray_msg_type(msg) != RAY_ASYNC) {
    return ray_send(ray_get_main(L), self, ray_msg(RAY_ASYNC,0));
  }

  TRACE("HERE... reacting...\n");
  if (!(self->flags & RAY_FIBER_STARTED)) {
    self->flags |= RAY_FIBER_STARTED;
    narg = lua_gettop(L) - 1;
  }
  else {
    ray_msg_t orig = ray_mailbox_get(self);
    narg = ray_msg_data(orig);
    if (narg < 0) narg = lua_gettop(self->L);
  }

  self->flags |= RAY_ACTIVE;
  self->flags &= ~RAY_FIBER_READY;

  int rc = lua_resume(L, narg);

  switch (rc) {
    case LUA_YIELD: {
      if (self->flags & RAY_ACTIVE) {
        /* detected coroutine.yield */
        self->flags &= ~RAY_ACTIVE;
        ray_send(ray_get_main(L), self, ray_msg(RAY_ASYNC,0));
      }
      break;
    }
    case 0: {
      /* normal exit, notify waiters, and close */
      ray_free(self);
      break;
    }
    default: {
      /* propagate the error back to the caller */
      ray_send(from, self, ray_msg(RAY_ERROR,1));
      ray_free(self);
    }
  }

  return narg;
}

static int _fiber_close(ray_actor_t* self) {
  if (!ray_is_closed(self)) {
    lua_State* L = self->L;

    ray_cond_t* cond = (ray_cond_t*)self->u.data;
    ray_cond_signal(cond, self, ray_msg(RAY_DATA,lua_gettop(self->L)));

    /* clear our reverse mapping to allow __gc */
    lua_pushthread(L);
    lua_pushnil(L);
    lua_settable(L, LUA_REGISTRYINDEX);

    lua_settop(self->L, 0);
    self->flags |= RAY_CLOSED;
  }

  return 1;
}

static ray_vtable_t fiber_v = {
  react: _fiber_react,
  yield: _fiber_yield,
  close: _fiber_close
};

ray_actor_t* ray_fiber_new(lua_State* L) {
  int top = lua_gettop(L);
  ray_actor_t* self = ray_actor_new(L, RAY_FIBER_T, &fiber_v);

  /* join conditional */
  self->u.data = ray_cond_new(L);

  /* reverse mapping from L to self */
  lua_pushthread(self->L);
  lua_pushlightuserdata(self->L, self);
  lua_rawset(self->L, LUA_REGISTRYINDEX);

  assert(ray_current(self->L) == self);
  assert(lua_gettop(L) == top + 1);

  return self;
}

static int ray_fiber_ready(ray_actor_t* self) {
  if (!(self->flags & RAY_FIBER_READY)) {
    ray_actor_t* boss = ray_get_main(self->L);
    self->flags |= RAY_FIBER_READY;
    ray_send(boss, self, ray_msg(RAY_ASYNC,0));
  }
  return 1;
}

static int ray_fiber_join(ray_actor_t* self, ray_actor_t* from) {
  ray_cond_t* cond = (ray_cond_t*)self->u.data;
  ray_fiber_ready(self);
  return ray_cond_wait(cond, from);
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

  return 1;
}

static int fiber_ready(lua_State* L) {
  ray_actor_t* self = (ray_actor_t*)lua_touserdata(L, 1);
  ray_fiber_ready(self);
  lua_settop(L, 1);
  return 1;
}

static int fiber_join(lua_State* L) {
  ray_actor_t* self = (ray_actor_t*)luaL_checkudata(L, 1, RAY_FIBER_T);
  ray_actor_t* from = ray_current(L);
  return ray_fiber_join(self, from);
}

static int fiber_free(lua_State* L) {
  ray_actor_t* self = (ray_actor_t*)lua_touserdata(L, 1);
  ray_free(self);
  return 1;
}
static int fiber_tostring(lua_State* L) {
  ray_actor_t* self = (ray_actor_t*)lua_touserdata(L, 1);
  lua_pushfstring(L, "userdata<%s>: %p", RAY_FIBER_T, self);
  return 1;
}

static luaL_Reg fiber_funcs[] = {
  {"create",    fiber_new},
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


