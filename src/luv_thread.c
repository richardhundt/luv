#include "luv.h"

void luvL_thread_ready(luv_thread_t* self) {
  if (!(self->flags & LUV_FREADY)) {
    TRACE("SET READY\n");
    self->flags |= LUV_FREADY;
    uv_async_send(&self->async);
  }
}

int luvL_thread_yield(luv_thread_t* self, int narg) {
  TRACE("calling uv_run_once\n");
  uv_run_once(self->loop);
  TRACE("done\n");
  if (narg == LUA_MULTRET) {
    narg = lua_gettop(self->L);
  }
  return narg;
}

int luvL_thread_suspend(luv_thread_t* self) {
  if (self->flags & LUV_FREADY) {
    self->flags &= ~LUV_FREADY;
    int active = 0;
    do {
      TRACE("loop top\n");
      luvL_thread_loop(self);
      active = uv_run_once(self->loop);
      TRACE("uv_run_once returned, active: %i\n", active);
      if (self->flags & LUV_FREADY) {
        TRACE("main ready, breaking\n");
        break;
      }
    }
    while (active);
    TRACE("back in main\n");
    /* nothing left to do, back in main */
    self->flags |= LUV_FREADY;
  }
  return lua_gettop(self->L);
}

int luvL_thread_resume(luv_thread_t* self, int narg) {
  /* interrupt the uv_run_once loop in luvL_thread_schedule */
  TRACE("resuming...\n");
  luvL_thread_ready(self);
  if (narg) {
    /* pass arguments from current state to self */
    luv_state_t* curr = self->curr;
    if (narg == LUA_MULTRET) {
      narg = lua_gettop(curr->L);
    }
    /* but only if it is not to ourselves */
    if (curr != (luv_state_t*)self) {
      assert(lua_gettop(curr->L) >= narg);
      lua_checkstack(self->L, narg);
      lua_xmove(curr->L, self->L, narg);
    }
  }
  else {
    /* lua_settop(self->L, 0) ??? */
  }
  return narg;
}
void luvL_thread_enqueue(luv_thread_t* self, luv_fiber_t* fiber) {
  int need_async = ngx_queue_empty(&self->rouse);
  ngx_queue_insert_tail(&self->rouse, &fiber->queue);
  if (need_async) {
    TRACE("need async\n");
    /* interrupt the event loop (the sequence of these two calls matters) */
    uv_async_send(&self->async);
    /* make sure we loop at least once more */
    uv_ref((uv_handle_t*)&self->async);
  }
}
luv_thread_t* luvL_thread_self(lua_State* L) {
  luv_state_t* self = luvL_state_self(L);
  if (self->type == LUV_TTHREAD) {
    return (luv_thread_t*)self;
  }
  else {
    while (self->type != LUV_TTHREAD) self = self->outer;
    return (luv_thread_t*)self;
  }
}

int luvL_thread_once(luv_thread_t* self) {
  if (!ngx_queue_empty(&self->rouse)) {
    ngx_queue_t* q;
    luv_fiber_t* fiber;
    q = ngx_queue_head(&self->rouse);
    fiber = ngx_queue_data(q, luv_fiber_t, queue);
    ngx_queue_remove(q);
    TRACE("[%p] rouse fiber: %p\n", self, fiber);
    if (fiber->flags & LUV_FDEAD) {
      TRACE("[%p] fiber is dead: %p\n", self, fiber);
      luaL_error(self->L, "cannot resume a dead fiber");
    }
    else {
      int stat, narg;
      narg = lua_gettop(fiber->L);

      if (!(fiber->flags & LUV_FSTART)) {
        /* first entry, ignore function arg */
        fiber->flags |= LUV_FSTART;
        --narg;
      }

      self->curr = (luv_state_t*)fiber;
      TRACE("[%p] calling lua_resume on: %p\n", self, fiber);
      stat = lua_resume(fiber->L, narg);
      TRACE("resume returned\n");
      self->curr = (luv_state_t*)self;

      switch (stat) {
        case LUA_YIELD:
          TRACE("[%p] seen LUA_YIELD\n", self);
          /* if called via coroutine.yield() then we're still in the queue */
          if (fiber->flags & LUV_FREADY) {
            TRACE("%p is still ready, back in the queue\n", fiber);
            ngx_queue_insert_tail(&self->rouse, &fiber->queue);
          }
          break;
        case 0: {
          /* normal exit, wake up joining states */
          int i, narg;
          narg = lua_gettop(fiber->L);
          TRACE("[%p] normal exit - fiber: %p, narg: %i\n", self, fiber, narg);
          ngx_queue_t* q;
          luv_state_t* s;
          while (!ngx_queue_empty(&fiber->rouse)) {
            q = ngx_queue_head(&fiber->rouse);
            s = ngx_queue_data(q, luv_state_t, join);
            ngx_queue_remove(q);
            TRACE("calling luvL_state_ready(%p)\n", s);
            luvL_state_ready(s);
            if (s->type == LUV_TFIBER) {
              lua_checkstack(fiber->L, 1);
              lua_checkstack(s->L, narg);
              for (i = 1; i <= narg; i++) {
                lua_pushvalue(fiber->L, i);
                lua_xmove(fiber->L, s->L, 1);
              }
            }
          }
          TRACE("closing fiber %p\n", fiber);
          luvL_fiber_close(fiber);
          break;
        }
        default:
          TRACE("ERROR: in fiber\n");
          lua_pushvalue(fiber->L, -1);  /* error message */
          lua_xmove(fiber->L, self->L, 1);
          luvL_fiber_close(fiber);
          lua_error(self->L);
      }
    }
  }
  return !ngx_queue_empty(&self->rouse);
}
int luvL_thread_loop(luv_thread_t* self) {
  while (luvL_thread_once(self));
  return 0;
}

static void _async_cb(uv_async_t* handle, int status) {
  TRACE("interrupt loop\n");
  (void)handle;
  (void)status;
}

void luvL_thread_init_main(lua_State* L) {
  luv_thread_t* self = (luv_thread_t*)lua_newuserdata(L, sizeof(luv_thread_t));
  luaL_getmetatable(L, LUV_THREAD_T);
  lua_setmetatable(L, -2);

  self->type  = LUV_TTHREAD;
  self->flags = LUV_FREADY;
  self->loop  = uv_default_loop();
  self->curr  = (luv_state_t*)self;
  self->L     = L;
  self->outer = (luv_state_t*)self;
  self->data  = NULL;
  self->tid   = (uv_thread_t)uv_thread_self();

  ngx_queue_init(&self->rouse);

  uv_async_init(self->loop, &self->async, _async_cb);
  uv_unref((uv_handle_t*)&self->async);

  lua_pushthread(L);
  lua_pushvalue(L, -2);
  lua_rawset(L, LUA_REGISTRYINDEX);
}

static void _thread_enter(void* arg) {
  luv_thread_t* self = (luv_thread_t*)arg;

  luvL_codec_decode(self->L);
  lua_remove(self->L, 1);

  luaL_checktype(self->L, 1, LUA_TFUNCTION);
  lua_pushcfunction(self->L, luvL_traceback);
  lua_insert(self->L, 1);
  int nargs = lua_gettop(self->L) - 2;

  int rv = lua_pcall(self->L, nargs, LUA_MULTRET, 1);
  lua_remove(self->L, 1); /* traceback */

  if (rv) { /* error */
    lua_pushboolean(self->L, 0);
    lua_insert(self->L, 1);
    luvL_thread_ready(self);
    luaL_error(self->L, lua_tostring(self->L, -1));
  }
  else {
    lua_pushboolean(self->L, 1);
    lua_insert(self->L, 1);
  }

  self->flags |= LUV_FDEAD;
}

luv_thread_t* luvL_thread_create(luv_state_t* outer, int narg) {
  lua_State* L = outer->L;
  int base;

  /* ..., func, arg1, ..., argN */
  base = lua_gettop(L) - narg + 1;

  luv_thread_t* self = (luv_thread_t*)lua_newuserdata(L, sizeof(luv_thread_t));
  luaL_getmetatable(L, LUV_THREAD_T);
  lua_setmetatable(L, -2);
  lua_insert(L, base++);

  self->type  = LUV_TTHREAD;
  self->flags = LUV_FREADY;
  self->loop  = uv_loop_new();
  self->curr  = (luv_state_t*)self;
  self->L     = luaL_newstate();
  self->outer = outer;
  self->data  = NULL;

  ngx_queue_init(&self->rouse);

  uv_async_init(self->loop, &self->async, _async_cb);
  uv_unref((uv_handle_t*)&self->async);

  luaL_openlibs(self->L);
  luaopen_luv(self->L);

  lua_settop(self->L, 0);
  luvL_codec_encode(L, narg);
  luaL_checktype(L, -1, LUA_TSTRING);
  lua_xmove(L, self->L, 1);

  /* keep a reference for reverse lookup in child */
  lua_pushthread(self->L);
  lua_pushlightuserdata(self->L, (void*)self);
  lua_rawset(self->L, LUA_REGISTRYINDEX);

  uv_thread_create(&self->tid, _thread_enter, self);

  /* inserted udata below function, so now just udata on top */
  TRACE("HERE TOP: %i, base: %i\n", lua_gettop(L), base);
  lua_settop(L, base - 1);
  return self;
}

/* Lua API */
static int luv_new_thread(lua_State* L) {
  luv_state_t* outer = luvL_state_self(L);
  int narg = lua_gettop(L);
  luvL_thread_create(outer, narg);
  return 1;
}
static int luv_thread_join(lua_State* L) {
  luv_thread_t* self = (luv_thread_t*)luaL_checkudata(L, 1, LUV_THREAD_T);
  luv_thread_t* curr = luvL_thread_self(L);

  luvL_thread_ready(self);
  luvL_thread_suspend(curr);
  uv_thread_join(&self->tid); /* XXX: use async instead, this blocks hard */

  lua_settop(L, 0);

  int nret = lua_gettop(self->L);
  luvL_codec_encode(self->L, nret);
  lua_xmove(self->L, L, 1);
  luvL_codec_decode(L);

  return nret;
}
static int luv_thread_free(lua_State* L) {
  luv_thread_t* self = lua_touserdata(L, 1);
  TRACE("free thread\n");
  uv_loop_delete(self->loop);
  TRACE("ok\n");
  return 1;
}
static int luv_thread_tostring(lua_State* L) {
  luv_thread_t* self = (luv_thread_t*)luaL_checkudata(L, 1, LUV_THREAD_T);
  lua_pushfstring(L, "userdata<%s>: %p", LUV_THREAD_T, self);
  return 1;
}

luaL_Reg luv_thread_funcs[] = {
  {"spawn",     luv_new_thread},
  {NULL,        NULL}
};

luaL_Reg luv_thread_meths[] = {
  {"join",      luv_thread_join},
  {"__gc",      luv_thread_free},
  {"__tostring",luv_thread_tostring},
  {NULL,        NULL}
};

