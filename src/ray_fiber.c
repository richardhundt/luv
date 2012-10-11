#include "ray.h"

void rayL_fiber_close(ray_fiber_t* fiber) {
  if (fiber->flags & RAY_FDEAD) return;

  lua_pushthread(fiber->L);
  lua_pushnil(fiber->L);
  lua_settable(fiber->L, LUA_REGISTRYINDEX);

  fiber->flags |= RAY_FDEAD;
}

void rayL_fiber_ready(ray_fiber_t* fiber) {
  if (!(fiber->flags & RAY_FREADY)) {
    TRACE("insert fiber %p into queue of %p\n", fiber, rayL_thread_self(fiber->L));
    fiber->flags |= RAY_FREADY;
    rayL_thread_enqueue(rayL_thread_self(fiber->L), fiber);
  }
}
int rayL_fiber_yield(ray_fiber_t* self, int narg) {
  rayL_fiber_ready(self);
  return lua_yield(self->L, narg);
}
int rayL_fiber_suspend(ray_fiber_t* self) {
  TRACE("FIBER SUSPEND - READY? %i\n", self->flags & RAY_FREADY);
  if (self->flags & RAY_FREADY) {
    self->flags &= ~RAY_FREADY;
    if (!rayL_state_is_active((ray_state_t*)self)) {
      ngx_queue_remove(&self->queue);
    }
    TRACE("about to yield...\n");
    return lua_yield(self->L, lua_gettop(self->L)); /* keep our stack */
  }
  return 0;
}
int rayL_fiber_resume(ray_fiber_t* self, int narg) {
  rayL_fiber_ready(self);
  return lua_resume(self->L, narg);
}

ray_fiber_t* rayL_fiber_new(ray_state_t* outer, int narg) {
  TRACE("spawn fiber as child of: %p\n", outer);

  ray_fiber_t* self;
  lua_State* L = outer->L;

  int base = lua_gettop(L) - narg + 1;
  luaL_checktype(L, base, LUA_TFUNCTION);

  lua_State* L1 = lua_newthread(L);
  lua_insert(L, base);                             /* [thread, func, ...] */

  lua_checkstack(L1, narg);
  lua_xmove(L, L1, narg);                          /* [thread] */

  self = (ray_fiber_t*)lua_newuserdata(L, sizeof(ray_fiber_t));
  luaL_getmetatable(L, RAY_FIBER_T);               /* [thread, fiber, meta] */
  lua_setmetatable(L, -2);                         /* [thread, fiber] */

  lua_pushvalue(L, -1);                            /* [thread, fiber, fiber] */
  lua_insert(L, base);                             /* [fiber, thread, fiber] */
  lua_rawset(L, LUA_REGISTRYINDEX);                /* [fiber] */

  while (outer->type != RAY_TTHREAD) outer = outer->outer;

  self->type  = RAY_TFIBER;
  self->outer = outer;
  self->L     = L1;
  self->flags = 0;
  self->data  = NULL;
  self->loop  = outer->loop;

  /* fibers waiting for us to finish */
  ngx_queue_init(&self->rouse);
  ngx_queue_init(&self->queue);

  return self;
}

/* Lua API */
static int ray_fiber_new(lua_State* L) {
  ray_state_t* outer = rayL_state_self(L);
  rayL_fiber_new(outer, lua_gettop(L));
  assert(lua_gettop(L) == 1);
  return 1;
}

int rayL_state_xcopy(ray_state_t* a, ray_state_t* b) {
  int i, narg;
  narg = lua_gettop(a->L);
  lua_checkstack(a->L, 1);
  lua_checkstack(b->L, narg);
  for (i = 1; i <= narg; i++) {
    lua_pushvalue(a->L, i);
    lua_xmove(a->L, b->L, 1);
  }
  return narg;
}

static int ray_fiber_join(lua_State* L) {
  ray_fiber_t* self = (ray_fiber_t*)luaL_checkudata(L, 1, RAY_FIBER_T);
  ray_state_t* curr = (ray_state_t*)rayL_state_self(L);
  TRACE("joining fiber[%p], from [%p]\n", self, curr);
  assert((ray_state_t*)self != curr);
  if (self->flags & RAY_FDEAD) {
    /* seen join after termination */
    TRACE("join after termination\n");
    return rayL_state_xcopy((ray_state_t*)self, curr);
  }
  ngx_queue_insert_tail(&self->rouse, &curr->join);
  rayL_fiber_ready(self);
  TRACE("calling rayL_state_suspend on %p\n", curr);
  if (curr->type == RAY_TFIBER) {
    return rayL_state_suspend(curr);
  }
  else {
    rayL_state_suspend(curr);
    return rayL_state_xcopy((ray_state_t*)self, curr);
  }
}

static int ray_fiber_ready(lua_State* L) {
  ray_fiber_t* self = (ray_fiber_t*)lua_touserdata(L, 1);
  rayL_fiber_ready(self);
  return 1;
}
static int ray_fiber_free(lua_State* L) {
  ray_fiber_t* self = (ray_fiber_t*)lua_touserdata(L, 1);
  if (self->data) free(self->data);
  return 1;
}
static int ray_fiber_tostring(lua_State* L) {
  ray_fiber_t* self = (ray_fiber_t*)lua_touserdata(L, 1);
  lua_pushfstring(L, "userdata<%s>: %p", RAY_FIBER_T, self);
  return 1;
}

luaL_Reg ray_fiber_funcs[] = {
  {"create",    ray_fiber_new},
  {NULL,        NULL}
};

luaL_Reg ray_fiber_meths[] = {
  {"join",      ray_fiber_join},
  {"ready",     ray_fiber_ready},
  {"__gc",      ray_fiber_free},
  {"__tostring",ray_fiber_tostring},
  {NULL,        NULL}
};


