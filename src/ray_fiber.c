#include <stdarg.h>

#include "ray_lib.h"
#include "ray_state.h"
#include "ray_fiber.h"

static const ray_vtable_t ray_fiber_v = {
  suspend : rayM_fiber_suspend,
  resume  : rayM_fiber_resume,
  enqueue : rayM_state_enqueue,
  ready   : rayM_fiber_ready,
  send    : rayM_state_send,
  recv    : rayM_state_recv,
  close   : rayM_fiber_close
};

ray_state_t* rayM_fiber_new(lua_State* L) {
  ray_state_t* self = rayS_new(L, RAY_FIBER_T, &ray_fiber_v);

  self->L = lua_newthread(L);
  lua_pushvalue(L, -2);
  lua_rawset(L, LUA_REGISTRYINDEX);

  return self;
}

int rayM_fiber_suspend(ray_state_t* self) {
  if (rayS_is_ready(self)) {
    self->flags &= ~RAY_READY;
    rayS_dequeue(self);
    return 0;
  }
  else {
    return lua_yield(self->L, lua_gettop(self->L));
  }
}

int rayM_fiber_ready(ray_state_t* self) {
  if (!rayS_is_ready(self)) {
    self->flags |= RAY_READY;
    rayS_enqueue(rayS_get_main(self->L), self);
    return 1;
  }
  return 0;
}

int rayM_fiber_resume(ray_state_t* self) {
  int rc, narg;
  if (rayS_is_closed(self)) {
    TRACE("[%p]fiber is closed\n", self);
    luaL_error(self->L, "cannot resume a closed fiber");
  }
  narg = lua_gettop(self->L);

  if (!(self->flags & RAY_START)) {
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
      if (rayS_is_ready(self)) {
        ray_state_t* root = rayS_get_main(self->L);
        ngx_queue_insert_tail(&root->rouse, &self->queue);
      }
      break;
    case 0: {
      /* normal exit, wake up joiners */
      TRACE("normal exit, broadcasting...\n");
      rayS_notify(self, LUA_MULTRET);
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

  return 1;
}

static int ray_fiber_spawn(lua_State* L) {
  ray_fiber_new(L);
  ray_state_t* self = (ray_state_t*)lua_touserdata(L, 1);
  rayM_fiber_ready(self);
  return 1;
}

static int ray_fiber_ready(lua_State* L) {
  ray_state_t* self = (ray_state_t*)lua_touserdata(L, 1);
  return rayM_fiber_ready(self);
}

static int ray_fiber_join(lua_State* L) {
  ray_state_t* self = (ray_state_t*)luaL_checkudata(L, 1, RAY_FIBER_T);
  ray_state_t* from = (ray_state_t*)rayS_get_self(L);
  TRACE("join %p from %p\n", self, from);
  if (rayS_is_closed(self)) {
    return rayS_xcopy(self, from, lua_gettop(self->L));
  }
  TRACE("here 1\n");
  rayS_enqueue(self, from);
  TRACE("here 2\n");
  rayS_ready(self);
  TRACE("here 3\n");
  return rayS_suspend(from);
}

static int ray_fiber_free(lua_State* L) {
  ray_state_t* self = (ray_state_t*)lua_touserdata(L, 1);
  TRACE("free\n");
  /* FIXME: this needs to know what type of stash we're using */
  if (self->u.data) free(self->u.data);
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
  {"ready",     ray_fiber_ready},
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


