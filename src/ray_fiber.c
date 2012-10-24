#include <stdarg.h>

#include "ray_lib.h"
#include "ray_actor.h"
#include "ray_fiber.h"

static const ray_vtable_t fiber_v = {
  send  : rayM_fiber_send,
  close : rayM_fiber_close
};

int rayM_fiber_send(ray_actor_t* self, ray_actor_t* from, int info) {
  lua_State* L = self->L;
  switch (info) {
    case RAY_YIELD: {
      TRACE("RAY_YIELD\n");
      self->flags &= ~RAY_ACTIVE;
      return lua_yield(L, lua_gettop(L));
    }
    case RAY_READY: {
      TRACE("RAY_READY\n");
      self->flags |= RAY_ACTIVE;
      lua_pushlightuserdata(self->L, from);
      TRACE("sending RAY_ASYNC to main\n");
      return ray_send(ray_get_main(L), self, RAY_ASYNC);
    }
    case RAY_EVAL: {
      TRACE("RAY_EVAL\n");
      rayL_dump_stack(L);

      int narg;
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
          ray_close(self);
          break;
        }
        default: {
          TRACE("ERROR: in fiber\n");
          /* propagate the error back to the caller */
          ray_send(from, self, 1);
          ray_close(self);
          lua_error(from->L);
        }
      }
      return narg;
    }
    default: {
      TRACE("RAY_SEND from: %p, to %p\n", from, self);
      /* somebody handed me data */
      if (self->flags & RAY_ACTIVE) {
        return lua_gettop(self->L);
      }
      else {
        TRACE("NEED A WAKEUP\n");
        ray_send(self, from, RAY_READY);
      }
    }
  }
  return 0;
}

int rayM_fiber_close(ray_actor_t* self) {
  TRACE("closing %p\n", self);

  lua_State* L = self->L;

  /* clear our reverse mapping to allow __gc */
  lua_pushthread(L);
  lua_pushnil(L);
  lua_settable(L, LUA_REGISTRYINDEX);

  return 1;
}

ray_actor_t* ray_fiber_new(lua_State* L) {
  int top = lua_gettop(L);
  ray_actor_t* self = ray_actor_new(L, RAY_FIBER_T, &fiber_v);

  lua_State* L2 = self->L;

  /* reverse mapping from L2 to self */
  lua_pushthread(L2);
  lua_pushlightuserdata(L2, self);
  lua_rawset(L2, LUA_REGISTRYINDEX);

  assert(ray_get_self(L2) == self);
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
  ray_actor_t* curr = ray_get_self(L);
  ray_send(self, curr, RAY_READY);
  return 1;
}

static int fiber_spawn(lua_State* L) {
  fiber_new(L);
  ray_actor_t* self = (ray_actor_t*)lua_touserdata(L, 1);
  ray_actor_t* curr = ray_get_self(L);
  ray_send(self, curr, RAY_READY);
  return 1;
}

static int fiber_join(lua_State* L) {
  ray_actor_t* self = (ray_actor_t*)luaL_checkudata(L, 1, RAY_FIBER_T);
  ray_actor_t* from = ray_get_self(L);

  if (ray_is_closed(self)) {
    int narg = lua_gettop(self->L);
    ray_send(from, self, narg);
    return narg;
  }

  /* we're ready for a timeslice */
  ray_send(self, from, RAY_READY);

  /* tell the current thread to wait for us */
  return ray_send(from, self, RAY_YIELD);
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


