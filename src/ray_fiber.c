#include <stdarg.h>

#include "ray_lib.h"
#include "ray_actor.h"
#include "ray_fiber.h"
/*
  A fiber is an actor which runs a Lua function as a coroutine. The
  mailbox lua_State* on the actor is *not* the same as the coroutine
  state, but is used, instead to pass messages to and fro.

  The Lua interface of the fiber udata presents a channel interface.
  `send' and `recv' operations interact with the main actor, which,
  does the scheduling during its own `recv' state.


  The channel supports `send' and `recv' methods which interact with
  the main actor. scheduler.
  A fiber may `recv' from any fiber, including itself.

  A fiber may `send' to any actor, including itself.

  If a fiber called `recv' on a different fiber, then the current
  fiber is added to that fiber's notification queue, and self then
  suspends waiting for a `send' signal.

  If the fiber calls `recv' on itself, then it may be in one of two
  states:

  1) we have been given a `send' before our call to `recv', so we have mail
  2) we have called `recv' before being sent any mail, so suspend

*/

static const ray_vtable_t fiber_v = {
  recv  : rayM_fiber_recv,
  send  : rayM_fiber_send,
  close : rayM_fiber_close
};

int rayM_fiber_recv(ray_actor_t* self, ray_actor_t* from) {
  /* from could also be self, but never main */
  /* suspend and keep our stack */
  lua_State* L = (lua_State*)self->u.data;
  TRACE("self[%p]->L\n", self);
  rayL_dump_stack(self->L);
  TRACE("from[%p]->L\n", from);
  rayL_dump_stack(from->L);
  if (lua_gettop(self->L) > 0) {
    int narg = lua_gettop(self->L);
    TRACE("NOTIFY SHORT: narg %i\n", narg);
    ray_notify(self, narg);
    return narg;
  }
  TRACE("calling LUA_YIELD - top: %i\n", lua_gettop(L));
  return lua_yield(L, lua_gettop(L));
}

int rayM_fiber_send(ray_actor_t* self, ray_actor_t* from, int narg) {
  lua_State* L = (lua_State*)self->u.data;

  ray_actor_t* boss = ray_get_main(L);
  TRACE("send %p from %p, boss: %p, self active?: %i\n", self, from, boss, ray_is_active(self));

  if (ray_is_closed(self)) {
    ray_notify(self, lua_gettop(L));
    return 0;
  }

  if (from == self) {
    narg = lua_gettop(self->L);
    lua_xmove(self->L, L, narg);
    return narg;
  }
  if (from != boss) {
    TRACE("not from boss, enqueue and return\n");
    if (ngx_queue_empty(&boss->queue)) {
      uv_async_send(&boss->h.async);
    }
    ray_enqueue(boss, self);
    return 0;
  }

  if (!(self->flags & RAY_FIBER_START)) {
    TRACE("first entry\n");
    narg = lua_gettop(L) - 1;
  }
  else {
    narg = lua_gettop(self->L);
    lua_xmove(self->L, L, narg);
  }

  /* resume while keeping our stack intact */
  TRACE("RESUME WITH: %i\n", narg);
  rayL_dump_stack(L);
  int rc = lua_resume(L, narg);
  TRACE("AFTER RESUME\n");
  rayL_dump_stack(L);

  narg = lua_gettop(L);
  if (!(self->flags & RAY_FIBER_START)) {
    lua_settop(self->L, 0);
    self->flags |= RAY_FIBER_START;
    narg--;
  }
  lua_xmove(L, self->L, narg);

  switch (rc) {
    case LUA_YIELD: {
      TRACE("seen LUA_YIELD\n");
      if (ray_is_active(self)) {
        /* seen coroutine.yield */
        TRACE("still active, notify\n");
        ray_notify(self, LUA_MULTRET);
      }
      break;
    }
    case 0: {
      /* normal exit, notify waiters, and close */
      TRACE("normal exit\n");
      ray_notify(self, LUA_MULTRET);
      ray_close(self);
      break;
    }
    default:
      TRACE("ERROR: in fiber\n");
      exit(1);
      /* propagate the error back to the caller */
      ray_send(from, self, 1);
      ray_close(self);
      lua_error(from->L);
  }
  return rc;
}

int rayM_fiber_close(ray_actor_t* self) {
  TRACE("closing %p\n", self);

  lua_State* L = (lua_State*)self->u.data;

  /* clear our reverse mapping to allow __gc */
  lua_pushthread(L);
  lua_pushnil(L);
  lua_settable(L, LUA_REGISTRYINDEX);

  return 1;
}

ray_actor_t* ray_fiber_new(lua_State* L) {
  int top = lua_gettop(L);
  ray_actor_t* self = ray_actor_new(L, RAY_FIBER_T, &fiber_v);

  lua_State* L2 = lua_newthread(L);
  self->u.data  = L2;
  lua_pop(L, 1);

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
  lua_State*   L2   = (lua_State*)self->u.data;

  /* return self to caller */
  lua_insert(L, 1);

  /* move the remaining arguments */
  lua_checkstack(L2, narg);
  lua_xmove(L, L2, narg);

  assert(lua_gettop(L) == 1);

  /* place self as first argument */
  lua_pushvalue(L, 1);
  lua_xmove(L, L2, 1);
  lua_insert(L2, 1);
  lua_pushvalue(L2, 2);
  lua_insert(L2, 1);
  lua_remove(L2, 3);

  ray_enqueue(ray_get_main(L), self);

  return 1;
}

static int fiber_spawn(lua_State* L) {
  fiber_new(L);
  ray_actor_t* self = (ray_actor_t*)lua_touserdata(L, 1);
  ray_actor_t* from = ray_get_self(L);
  ray_send(self, from, LUA_MULTRET);
  return 1;
}

static int fiber_send(lua_State* L) {
  ray_actor_t* self = (ray_actor_t*)luaL_checkudata(L, 1, RAY_FIBER_T);
  ray_actor_t* from = (ray_actor_t*)ray_get_self(L);
  TRACE("send %p from %p\n", self, from);
  int narg = lua_gettop(L) - 1;
  lua_xmove(L, self->L, narg);
  return ray_send(self, from, 0);
}
static int fiber_recv(lua_State* L) {
  ray_actor_t* self = (ray_actor_t*)luaL_checkudata(L, 1, RAY_FIBER_T);
  ray_actor_t* from = (ray_actor_t*)ray_get_self(L);
  TRACE("recv %p from %p\n", self, from);
  return ray_recv(from, self);
}
static int fiber_free(lua_State* L) {
  ray_actor_t* self = (ray_actor_t*)lua_touserdata(L, 1);
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
  {"send",      fiber_send},
  {"recv",      fiber_recv},
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


