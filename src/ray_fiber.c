#include <stdarg.h>

#include "ray_lib.h"
#include "ray_actor.h"
#include "ray_fiber.h"

static const ray_vtable_t ray_fiber_v = {
  await : rayM_fiber_await,
  rouse : rayM_fiber_rouse,
  close : rayM_fiber_close
};

ray_actor_t* rayM_fiber_new(lua_State* L) {
  ray_actor_t* self = rayL_actor_new(L, RAY_FIBER_T, &ray_fiber_v);

  self->L = lua_newthread(L);
  lua_pushvalue(L, -2);
  lua_rawset(L, LUA_REGISTRYINDEX);

  return self;
}

int rayM_fiber_await(ray_actor_t* self, ray_actor_t* that) {
  TRACE("await %p that %p, calling lua_yield\n", self, that);
  return lua_yield(self->L, lua_gettop(self->L));
}

int rayM_fiber_rouse(ray_actor_t* self, ray_actor_t* from) {
  ray_actor_t* boss = ray_get_main(self->L);
  TRACE("rouse %p from %p, boss: %p\n", self, from, boss);

  int rc, narg;
  if (ray_is_closed(self)) {
    TRACE("[%p]fiber is closed\n", self);
    luaL_error(self->L, "cannot resume a closed fiber");
  }

  if (from != boss) {
    TRACE("not the boss, enqueue...\n");
    ngx_queue_insert_tail(&boss->queue, &self->cond);
    self->flags &= ~RAY_ACTIVE;
    uv_async_send(&boss->h.async);
    return 0;
  }

  narg = lua_gettop(self->L);

  if (!ray_is_start(self)) {
    /* first entry, ignore function arg */
    self->flags |= RAY_START;
    --narg;
  }

  TRACE("calling lua_resume on: %p\n", self);
  rc = lua_resume(self->L, narg);
  TRACE("resume returned\n");

  switch (rc) {
    case LUA_YIELD:
      narg = lua_gettop(self->L);
      TRACE("[%p] seen LUA_YIELD, narg: %i\n", self, narg);
      if (ray_is_active(self)) {
        self->flags &= ~RAY_ACTIVE;
        TRACE("still active...\n");
        ngx_queue_insert_tail(&boss->queue, &self->cond);
      }
      break;
    case 0: {
      TRACE("normal exit, wake joiners\n");
      /* normal exit, wake up joiners and pass our stack */
      ray_notify(self, lua_gettop(self->L));
      ray_close(self);
      break;
    }
    default:
      TRACE("ERROR: in fiber\n");
      lua_pushvalue(self->L, -1);  /* error message */
      ray_close(self);
      lua_error(self->L);
  }
  return rc;
}

int rayM_fiber_close(ray_actor_t* self) {
  TRACE("closing %p\n", self);
  lua_pushthread(self->L);
  lua_pushnil(self->L);
  lua_settable(self->L, LUA_REGISTRYINDEX);
  return 1;
}



/* Lua API */
static int ray_fiber_new(lua_State* L) {
  int narg = lua_gettop(L);

  luaL_checktype(L, 1, LUA_TFUNCTION);
  ray_actor_t* self = rayM_fiber_new(L);
  lua_insert(L, 1);

  lua_checkstack(self->L, narg);
  lua_xmove(L, self->L, narg);

  ray_actor_t* boss = ray_get_main(L);
  ngx_queue_insert_tail(&boss->queue, &self->cond);

  return 1;
}

static int ray_fiber_spawn(lua_State* L) {
  ray_fiber_new(L);
  ray_actor_t* self = (ray_actor_t*)lua_touserdata(L, 1);
  return ray_rouse(self, ray_get_main(L));
}

static int ray_fiber_join(lua_State* L) {
  ray_actor_t* self = (ray_actor_t*)luaL_checkudata(L, 1, RAY_FIBER_T);
  ray_actor_t* from = (ray_actor_t*)ray_get_self(L);
  TRACE("join %p from %p\n", self, from);
  if (ray_is_closed(self)) {
    return ray_xcopy(self, from, lua_gettop(self->L));
  }
  return ray_await(from, self);
}

static int ray_fiber_free(lua_State* L) {
  ray_actor_t* self = (ray_actor_t*)lua_touserdata(L, 1);
  (void)self;
  return 1;
}
static int ray_fiber_tostring(lua_State* L) {
  ray_actor_t* self = (ray_actor_t*)lua_touserdata(L, 1);
  lua_pushfstring(L, "userdata<%s>: %p", RAY_FIBER_T, self);
  return 1;
}

static luaL_Reg ray_fiber_funcs[] = {
  {"create",    ray_fiber_new},
  {"spawn",     ray_fiber_spawn},
  {NULL,        NULL}
};

static luaL_Reg ray_fiber_meths[] = {
  {"join",      ray_fiber_join},
  {"__gc",      ray_fiber_free},
  {"__tostring",ray_fiber_tostring},
  {NULL,        NULL}
};

LUALIB_API int luaopen_ray_fiber(lua_State* L) {
  rayL_module(L, "ray.fiber", ray_fiber_funcs);

  /* borrow coroutine.yield (fast on LJ2) */
  lua_getglobal(L, "coroutine");
  lua_getfield(L, -1, "yield");
  lua_setfield(L, -3, "yield");
  lua_pop(L, 1); /* coroutine */

  rayL_class(L, RAY_FIBER_T, ray_fiber_meths);
  lua_pop(L, 1);

  ray_init_main(L);

  return 1;
}


