#include <stdarg.h>

#include "ray_lib.h"
#include "ray_actor.h"
#include "ray_fiber.h"
/*
  A fiber is an actor which runs a Lua function as a coroutine. The
  mailbox lua_State* on the actor is *not* the same as the coroutine
  state, but is used, instead to pass messages to and fro.

  The Lua side of the fiber udata presents a channel interface.
  The channel supports `send' and `recv' methods which translate to
  send and recv calls on the underlying actor. The actor itself is
  responsible for maintaining the lua_State* of the coroutine it's
  currently running.

  There are two complications to this.

  The first complication is that a udata's `send' and 'recv' methods
  are callable from both "within" the coroutine, or from the "outside".

  The semantics of `recv' is to wait for something to send me a message.
  Calling `recv' from "within" the fiber implies that a fiber can wait
  for input. To avoid this from leading to madness, the call to `recv'
  returns immediately if there is a message in our mailbox. If not, then
  the actor suspends (it enqueues itself) and awaits a `send'.

  The semantics of `send' is to move n arguments from my mailbox which
  I've prepared as a message, to another actor's mailbox, and transfer
  control to that actor. If `send' is called from "within" the coroutine
  then the self send call just moves the arguments to self's mailbox and
  returns. Moreover, if this chain of `send' signals
  were to run synchronously through the tree of actors, then it would mean
  recursing up the C stack. And to avoid that madness, a fiber will delay
  all `send'signals which don't come from the `main' actor, by placing
  itself in the `main' actors notification queue.

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
  if (lua_gettop(self->L) > 0) {
    lua_settop(L, 0);
    int narg = lua_gettop(self->L) - 1;
    lua_xmove(self->L, L, narg);
    TRACE("NOTIFY SHORT: narg %i\n", narg);
    ray_notify(self, narg);
    return narg;
  }
  TRACE("calling LUA_YIELD - top: %i\n", lua_gettop(L));
  self->flags &= ~RAY_FIBER_ACTIVE;
  return lua_yield(L, lua_gettop(L));
}

int rayM_fiber_send(ray_actor_t* self, ray_actor_t* from, int narg) {
  lua_State* L = (lua_State*)self->u.data;

  ray_actor_t* boss = ray_get_main(L);
  TRACE("send %p from %p, boss: %p, self active?: %i\n", self, from, boss, ray_is_active(self));

  if (ray_is_closed(self)) {
    TRACE("IS CLOSED\n");
    ray_notify(self, LUA_MULTRET);
    return 0;
  }

  if (from != boss) {
    TRACE("send not from boss, enqueue...\n");
    self->h.handle.data = from;
    ray_recv(boss, self);
    return 0;
  }
  else {
    from = (ray_actor_t*)self->h.handle.data;
  }

  if (!(self->flags & RAY_FIBER_START)) {
    self->flags |= RAY_FIBER_START;
    TRACE("first entry\n");
    narg = lua_gettop(L) - 1;
  }
  else {
    narg = lua_gettop(self->L);
    lua_settop(L, 0);
    lua_xmove(self->L, L, narg);
  }

  self->flags |= RAY_FIBER_ACTIVE;
  int rc = lua_resume(L, narg);

  TRACE("AFTER RESUME\n");
  switch (rc) {
    case LUA_YIELD: {
      TRACE("seen LUA_YIELD, active? %i\n", ray_is_active(self));
      if (self->flags & RAY_FIBER_ACTIVE) {
        /* uses coroutine.yield to reply to sender */
        self->flags &= ~RAY_FIBER_ACTIVE;
        ray_recv(boss, self);

        narg = lua_gettop(L);
        lua_xmove(L, self->L, narg);

        ray_signal(self, narg);
      }
      break;
    }
    case 0: {
      /* normal exit, notify waiters, and close */
      TRACE("normal exit\n");
      narg = lua_gettop(L);
      lua_xmove(L, self->L, narg);
      ray_notify(self, narg);
      ray_close(self);
      break;
    }
    default:
      TRACE("ERROR: in fiber\n");
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
  lua_settop(self->L, 0);
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


