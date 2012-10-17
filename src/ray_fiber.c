#include <stdarg.h>

#include "ray_lib.h"
#include "ray_state.h"
#include "ray_fiber.h"

static const ray_vtable_t ray_fiber_v = {
  await : rayM_fiber_await,
  rouse : rayM_fiber_rouse,
  close : rayM_fiber_close
};

ray_state_t* rayM_fiber_new(lua_State* L) {
  ray_state_t* self = rayS_new(L, RAY_FIBER_T, &ray_fiber_v);

  self->L = lua_newthread(L);
  lua_pushvalue(L, -2);
  lua_rawset(L, LUA_REGISTRYINDEX);

  return self;
}

int rayM_fiber_await(ray_state_t* self, ray_state_t* that) {
  TRACE("await %p that %p\n", self, that);
  ngx_queue_insert_tail(&that->queue, &self->cond);
  if (rayS_is_active(self)) {
    self->flags &= ~RAY_ACTIVE;
    TRACE("calling lua_yield\n");
    return lua_yield(self->L, lua_gettop(self->L));
  }
  else {
    TRACE("rousing...\n");
    return rayS_rouse(that, self);
  }
}

int rayM_fiber_rouse(ray_state_t* self, ray_state_t* from) {
  ray_state_t* boss = rayS_get_main(self->L);
  TRACE("rouse %p from %p, boss: %p\n", self, from, boss);

  int rc, narg;
  if (rayS_is_closed(self)) {
    TRACE("[%p]fiber is closed\n", self);
    luaL_error(self->L, "cannot resume a closed fiber");
  }

  if (from != boss) {
    ngx_queue_insert_tail(&boss->queue, &self->cond);
    return 0;
  }

  narg = lua_gettop(self->L);

  if (!rayS_is_start(self)) {
    /* first entry, ignore function arg */
    self->flags |= RAY_START;
    --narg;
  }
  self->flags |= RAY_ACTIVE;

  TRACE("calling lua_resume on: %p\n", self);
  rc = lua_resume(self->L, narg);
  TRACE("resume returned\n");

  switch (rc) {
    case LUA_YIELD:
      narg = lua_gettop(self->L);
      TRACE("[%p] seen LUA_YIELD, narg: %i\n", self, narg);
      if (rayS_is_active(self)) {
        ray_state_t* boss = rayS_get_main(self->L);
        self->flags &= ~RAY_ACTIVE;
        TRACE("still active...\n");
        ngx_queue_insert_tail(&boss->queue, &self->cond);
      }
      break;
    case 0: {
      TRACE("normal exit, wake joiners\n");
      /* normal exit, wake up joiners and pass our stack */
      rayS_notify(self, lua_gettop(self->L));
      rayS_close(self);
      break;
    }
    default:
      TRACE("ERROR: in fiber\n");
      lua_pushvalue(self->L, -1);  /* error message */
      rayS_close(self);
      lua_error(self->L);
  }
  return rc;
}

int rayM_fiber_close(ray_state_t* self) {
  if (self->flags & RAY_CLOSED) return 0;
  TRACE("closing %p\n", self);
  lua_pushthread(self->L);
  lua_pushnil(self->L);
  lua_settable(self->L, LUA_REGISTRYINDEX);
  self->flags |= RAY_CLOSED;
  return 1;
}



/* Lua API */
static int ray_fiber_new(lua_State* L) {
  int narg = lua_gettop(L);

  luaL_checktype(L, 1, LUA_TFUNCTION);
  ray_state_t* self = rayM_fiber_new(L);
  lua_insert(L, 1);

  lua_checkstack(self->L, narg);
  lua_xmove(L, self->L, narg);

  ray_state_t* curr = rayS_get_main(L);
  ngx_queue_insert_tail(&curr->queue, &self->cond);

  return 1;
}

static int ray_fiber_spawn(lua_State* L) {
  ray_fiber_new(L);
  ray_state_t* self = (ray_state_t*)lua_touserdata(L, 1);
  ngx_queue_remove(&self->cond);
  return rayS_rouse(self, rayS_get_main(L));
}

static int ray_fiber_join(lua_State* L) {
  ray_state_t* self = (ray_state_t*)luaL_checkudata(L, 1, RAY_FIBER_T);
  ray_state_t* from = (ray_state_t*)rayS_get_self(L);
  TRACE("join %p from %p\n", self, from);
  if (rayS_is_closed(self)) {
    return rayS_xcopy(self, from, lua_gettop(self->L));
  }
  return rayS_await(from, self);
}

static int ray_fiber_free(lua_State* L) {
  ray_state_t* self = (ray_state_t*)lua_touserdata(L, 1);
  (void)self;
  return 1;
}
static int ray_fiber_tostring(lua_State* L) {
  ray_state_t* self = (ray_state_t*)lua_touserdata(L, 1);
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

  rayS_init_main(L);

  return 1;
}


