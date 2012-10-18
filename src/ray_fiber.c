#include <stdarg.h>

#include "ray_lib.h"
#include "ray_actor.h"
#include "ray_fiber.h"

static const ray_vtable_t fiber_v = {
  await : rayM_fiber_await,
  rouse : rayM_fiber_rouse,
  close : rayM_fiber_close
};

ray_actor_t* ray_fiber_new(lua_State* L) {
  ray_actor_t* self = ray_actor_new(L, RAY_FIBER_T, &fiber_v);

  /* reverse mapping from L to self */
  lua_pushthread(self->L);
  lua_pushvalue(L, -1);
  lua_xmove(L, self->L, 1);
  lua_rawset(self->L, LUA_REGISTRYINDEX);

  assert(ray_get_self(self->L) == self);
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
    /* this is really ugly and breaks encapsulation, but I don't want to add
     * another vtable method just for threads to interrupt the event loop */
    if (ngx_queue_empty(&boss->queue)) {
      /* implies that we're waiting in uv_run_once */
      uv_async_send(&boss->h.async);
    }
    ngx_queue_insert_tail(&boss->queue, &self->cond);
    self->flags &= ~RAY_ACTIVE;
    return 0;
  }

  narg = lua_gettop(self->L);

  if (!ray_is_start(self)) {
    /* first entry, ignore function arg */
    self->flags |= RAY_START;
    --narg;
  }

  rc = lua_resume(self->L, narg);
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
      ray_notify(self, LUA_MULTRET);
      ray_close(self);
      break;
    }
    default:
      TRACE("ERROR: in fiber\n");
      lua_xmove(self->L, boss->L, 1);
      ray_close(self);
      lua_error(boss->L);
  }
  return rc;
}

int rayM_fiber_close(ray_actor_t* self) {
  TRACE("closing %p\n", self);

  /* clear our reverse mapping to allow __gc */
  lua_pushthread(self->L);
  lua_pushnil(self->L);
  lua_settable(self->L, LUA_REGISTRYINDEX);

  return 1;
}

/* Lua API */
static int fiber_new(lua_State* L) {
  int narg = lua_gettop(L);

  luaL_checktype(L, 1, LUA_TFUNCTION);
  ray_actor_t* self = ray_fiber_new(L);
  lua_insert(L, 1);

  lua_checkstack(self->L, narg);
  lua_xmove(L, self->L, narg);

  ray_actor_t* boss = ray_get_main(L);
  ngx_queue_insert_tail(&boss->queue, &self->cond);

  return 1;
}

static int fiber_spawn(lua_State* L) {
  fiber_new(L);
  ray_actor_t* self = (ray_actor_t*)lua_touserdata(L, 1);
  return ray_rouse(self, ray_get_main(L));
}

static int fiber_join(lua_State* L) {
  ray_actor_t* self = (ray_actor_t*)luaL_checkudata(L, 1, RAY_FIBER_T);
  ray_actor_t* from = (ray_actor_t*)ray_get_self(L);
  TRACE("join %p from %p\n", self, from);
  if (ray_is_closed(self)) {
    return ray_xcopy(self, from, lua_gettop(self->L));
  }
  return ray_await(from, self);
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
  {"join",      fiber_join},
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


