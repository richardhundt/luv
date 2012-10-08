#include "luv.h"

void luvL_fiber_close(luv_fiber_t* fiber) {
  if (fiber->flags & LUV_FDEAD) return;

  lua_pushthread(fiber->L);
  lua_pushnil(fiber->L);
  lua_settable(fiber->L, LUA_REGISTRYINDEX);

  fiber->flags |= LUV_FDEAD;
}

void luvL_fiber_ready(luv_fiber_t* fiber) {
  if (!(fiber->flags & LUV_FREADY)) {
    TRACE("insert fiber %p into queue of %p\n", fiber, luvL_thread_self(fiber->L));
    fiber->flags |= LUV_FREADY;
    luvL_thread_enqueue(luvL_thread_self(fiber->L), fiber);
  }
}
int luvL_fiber_yield(luv_fiber_t* self, int narg) {
  luvL_fiber_ready(self);
  return lua_yield(self->L, narg);
}
int luvL_fiber_suspend(luv_fiber_t* self) {
  TRACE("FIBER SUSPEND - READY? %i\n", self->flags & LUV_FREADY);
  if (self->flags & LUV_FREADY) {
    self->flags &= ~LUV_FREADY;
    if (!luvL_state_is_active((luv_state_t*)self)) {
      ngx_queue_remove(&self->queue);
    }
    TRACE("about to yield...\n");
    return lua_yield(self->L, lua_gettop(self->L)); /* keep our stack */
  }
  return 0;
}
int luvL_fiber_resume(luv_fiber_t* self, int narg) {
  luvL_fiber_ready(self);
  return lua_resume(self->L, narg);
}

luv_fiber_t* luvL_fiber_create(luv_state_t* outer, int narg) {
  TRACE("spawn fiber as child of: %p\n", outer);

  luv_fiber_t* self;
  lua_State* L = outer->L;

  int base = lua_gettop(L) - narg + 1;
  luaL_checktype(L, base, LUA_TFUNCTION);

  lua_State* L1 = lua_newthread(L);
  lua_insert(L, base);                             /* [thread, func, ...] */

  lua_checkstack(L1, narg);
  lua_xmove(L, L1, narg);                          /* [thread] */

  self = (luv_fiber_t*)lua_newuserdata(L, sizeof(luv_fiber_t));
  luaL_getmetatable(L, LUV_FIBER_T);               /* [thread, fiber, meta] */
  lua_setmetatable(L, -2);                         /* [thread, fiber] */

  lua_pushvalue(L, -1);                            /* [thread, fiber, fiber] */
  lua_insert(L, base);                             /* [fiber, thread, fiber] */
  lua_rawset(L, LUA_REGISTRYINDEX);                /* [fiber] */

  while (outer->type != LUV_TTHREAD) outer = outer->outer;

  self->type  = LUV_TFIBER;
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
static int luv_new_fiber(lua_State* L) {
  luv_state_t* outer = luvL_state_self(L);
  luvL_fiber_create(outer, lua_gettop(L));
  assert(lua_gettop(L) == 1);
  return 1;
}

int luvL_state_xcopy(luv_state_t* a, luv_state_t* b) {
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

static int luv_fiber_join(lua_State* L) {
  luv_fiber_t* self = (luv_fiber_t*)luaL_checkudata(L, 1, LUV_FIBER_T);
  luv_state_t* curr = (luv_state_t*)luvL_state_self(L);
  TRACE("joining fiber[%p], from [%p]\n", self, curr);
  assert((luv_state_t*)self != curr);
  if (self->flags & LUV_FDEAD) {
    /* seen join after termination */
    TRACE("join after termination\n");
    return luvL_state_xcopy((luv_state_t*)self, curr);
  }
  ngx_queue_insert_tail(&self->rouse, &curr->join);
  luvL_fiber_ready(self);
  TRACE("calling luvL_state_suspend on %p\n", curr);
  if (curr->type == LUV_TFIBER) {
    return luvL_state_suspend(curr);
  }
  else {
    luvL_state_suspend(curr);
    return luvL_state_xcopy((luv_state_t*)self, curr);
  }
}

static int luv_fiber_ready(lua_State* L) {
  luv_fiber_t* self = (luv_fiber_t*)lua_touserdata(L, 1);
  luvL_fiber_ready(self);
  return 1;
}
static int luv_fiber_free(lua_State* L) {
  luv_fiber_t* self = (luv_fiber_t*)lua_touserdata(L, 1);
  if (self->data) free(self->data);
  return 1;
}
static int luv_fiber_tostring(lua_State* L) {
  luv_fiber_t* self = (luv_fiber_t*)lua_touserdata(L, 1);
  lua_pushfstring(L, "userdata<%s>: %p", LUV_FIBER_T, self);
  return 1;
}

luaL_Reg luv_fiber_funcs[] = {
  {"create",    luv_new_fiber},
  {NULL,        NULL}
};

luaL_Reg luv_fiber_meths[] = {
  {"join",      luv_fiber_join},
  {"ready",     luv_fiber_ready},
  {"__gc",      luv_fiber_free},
  {"__tostring",luv_fiber_tostring},
  {NULL,        NULL}
};


