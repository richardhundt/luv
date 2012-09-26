#include "luv.h"
#include "luv_core.h"
#include "luv_cond.h"

static void luv__fiber_release(luv_fiber_t* fiber) {
  if (fiber->flags & LUV_FDEAD) return;
  if (fiber->ref != LUA_NOREF) {
    luaL_unref(fiber->L, LUA_REGISTRYINDEX, fiber->ref);
    fiber->ref = LUA_NOREF;
  }
  if (fiber->coref != LUA_NOREF) {
    luaL_unref(fiber->L, LUA_REGISTRYINDEX, fiber->coref);
    fiber->coref = LUA_NOREF;
  }
  fiber->flags |= LUV_FDEAD;
}

int luv__sched_in_main(luv_sched_t* sched) {
  return sched->curr == (luv_state_t*)sched;
}
int luv__state_is_main(luv_state_t* state) {
  return state->outer == state;
}
luv_state_t* luv__sched_current(luv_sched_t* sched) {
  return sched->curr;
}

static void luv__sched_init(lua_State* L, luv_sched_t* sched) {
  sched->flags = LUV_FREADY;
  sched->loop  = uv_loop_new();
  sched->curr  = (luv_state_t*)sched;
  sched->L     = L;
  sched->sched = sched;
  sched->outer = (luv_state_t*)sched;
  sched->data  = NULL;

  ngx_queue_init(&sched->ready);

  uv_prepare_init(sched->loop, &sched->hook);
  uv_unref((uv_handle_t*)&sched->hook);
}

int luv__sched_once(luv_sched_t* sched) {
  if (!ngx_queue_empty(&sched->ready)) {
    ngx_queue_t* q;
    luv_fiber_t* fiber;

    ngx_queue_t* ready = &sched->ready;
    q = ngx_queue_head(ready);
    fiber = ngx_queue_data(q, luv_fiber_t, queue);
    ngx_queue_remove(q);
    if (fiber->flags & LUV_FDEAD) {
      return luaL_error(sched->L, "cannot resume a dead fiber");
    }
    else {
      int stat, narg;
      narg = lua_gettop(fiber->L);

      if (!(fiber->flags & LUV_FSTART)) {
        /* first entry, ignore function arg */
        fiber->flags |= LUV_FSTART;
        --narg;
      }

      sched->curr = (luv_state_t*)fiber;
      stat = lua_resume(fiber->L, narg);
      sched->curr = (luv_state_t*)sched;

      switch (stat) {
        case LUA_YIELD:
          /* if called via coroutine.yield() then we're still in the queue */
          if (fiber->flags & LUV_FREADY) {
            ngx_queue_insert_tail(&sched->ready, &fiber->queue);
          }
          break;
        case 0:
          /* normal exit, return values and drop refs */
          if (fiber->flags & LUV_FJOIN) {
            int nret = lua_gettop(fiber->L);
            luv_state_t* outer = fiber->outer;
            lua_settop(outer->L, 0);
            lua_checkstack(outer->L, nret);
            lua_xmove(fiber->L, outer->L, nret);
            luv__state_resume(outer);
          }
          luv__cond_broadcast(&fiber->rouse);
          luv__fiber_release(fiber);
          break;
        default:
          lua_pushvalue(fiber->L, -1);  /* error message */
          lua_xmove(fiber->L, sched->L, 1);
          luv__fiber_release(fiber);
          lua_error(sched->L);
      }
    }
  }
  return !ngx_queue_empty(&sched->ready);
}

int luv__sched_loop(luv_sched_t* sched) {
  while (luv__sched_once(sched));
  return 0;
}

/* API */
static int luv_new_sched(lua_State* L) {
  luv_sched_t* sched = lua_newuserdata(L, sizeof(luv_sched_t));
  luaL_getmetatable(L, LUV_SCHED_T);
  lua_setmetatable(L, -2);
  luv__sched_init(L, sched);
  return 1;
}
static int luv_sched_tostring(lua_State* L) {
  luv_sched_t* sched = lua_touserdata(L, 1);
  lua_pushfstring(L, "userdata<%s>: %p", LUV_SCHED_T, sched);
  return 1;
}

static int luv_new_fiber(lua_State* L) {
  lua_State*   NL;
  luv_sched_t* sched = luaL_checkudata(L, lua_upvalueindex(1), LUV_SCHED_T);
  luv_fiber_t* fiber;

  int ref, coref, narg, type;

  narg = lua_gettop(L);
  type = lua_type(L, 1);

  switch (type) {
    case LUA_TFUNCTION:
      NL = lua_newthread(L);
      lua_insert(L, 1);                             /* [thread, func, ...] */
      break;
    case LUA_TTHREAD:
      NL = lua_tothread(L, 1);
      --narg;
      break;
    default:
      return luaL_argerror(L, 2, "function or thread expected");
  }

  lua_checkstack(NL, narg);
  lua_xmove(L, NL, narg);                           /* [thread] */

  /* stash the state */
  coref = luaL_ref(L, LUA_REGISTRYINDEX);           /* [ ] */

  fiber = lua_newuserdata(L, sizeof(luv_fiber_t));  /* [fiber] */
  luaL_getmetatable(L, LUV_FIBER_T);                /* [fiber, meta] */
  lua_setmetatable(L, -2);                          /* [fiber] */

  /* stash the fiber */
  lua_pushvalue(L, -1);
  ref = luaL_ref(L, LUA_REGISTRYINDEX);             /* [fiber] */

  fiber->sched = sched;
  fiber->L     = NL;
  fiber->ref   = ref;
  fiber->coref = coref;
  fiber->flags = type == LUA_TTHREAD ? LUV_FSTART : 0;
  fiber->data  = NULL;
  fiber->outer = sched->curr;

  /* fibers waiting for us to finish */
  ngx_queue_init(&fiber->rouse);
  ngx_queue_init(&fiber->queue);

  return 1;
}


static void luv__sched_hook_cb(uv_prepare_t* handle, int status) {
  /* unblock scheduler handle */
  luv_sched_t* sched = container_of(handle, luv_sched_t, hook);
  luv__sched_loop(sched);
}

void luv__state_suspend(luv_state_t* state) {
  if (state->flags & LUV_FREADY) {
    if (luv__state_is_main(state)) {
      uv_prepare_start(&state->sched->hook, luv__sched_hook_cb);
    }
    else if (state != state->sched->curr) {
      /* current is already dequeued */
      ngx_queue_remove(&state->queue);
    }
    state->flags &= ~LUV_FREADY;
  }
}
void luv__state_resume(luv_state_t* state) {
  if (!(state->flags & LUV_FREADY)) {
    if (luv__state_is_main(state)) {
      uv_prepare_stop(&state->sched->hook);
    }
    else {
      ngx_queue_insert_tail(&state->sched->ready, &state->queue);
    }
    state->flags |= LUV_FREADY;
  }
}
int luv__state_yield(luv_state_t* state, int narg) {
  if (luv__state_is_main(state)) {
    while (!(state->flags & LUV_FREADY)) {
      uv_run_once(state->sched->loop);
    }
    if (narg == LUA_MULTRET) narg = lua_gettop(state->L);
    return narg;
  }
  else {
    return lua_yield(state->L, narg);
  }
}

static int luv_fiber_loop(lua_State* L) {
  luv_sched_t* sched = lua_touserdata(L, lua_upvalueindex(1));
  uv_prepare_start(&sched->hook, luv__sched_hook_cb);
  uv_run(sched->loop);
  return 1;
}
static int luv_fiber_suspend(lua_State* L) {
  luv_fiber_t* fiber = luaL_checkudata(L, 1, LUV_FIBER_T);
  luv__state_suspend((luv_state_t*)fiber);
  return 1;
}
static int luv_fiber_resume(lua_State* L) {
  luv_fiber_t* fiber = luaL_checkudata(L, 1, LUV_FIBER_T);
  luv__state_resume((luv_state_t*)fiber);
  return 1;
}

static int luv_fiber_join(lua_State* L) {
  luv_fiber_t* fiber = luaL_checkudata(L, 1, LUV_FIBER_T);
  luv_sched_t* sched = fiber->sched;
  if (!(fiber->flags & (LUV_FJOIN|LUV_FDEAD))) {
    fiber->flags |= LUV_FJOIN;
    luv__state_resume((luv_state_t*)fiber);
    luv__state_suspend(sched->curr);
    return luv__state_yield(sched->curr, LUA_MULTRET);
  }
  return 0;
}

static int luv_fiber_ready(lua_State* L) {
  luv_fiber_t* fiber = lua_touserdata(L, 1);
  luv__state_resume((luv_state_t*)fiber);
  return 1;
}

static int luv_fiber_free(lua_State* L) {
  luv_fiber_t* fiber = lua_touserdata(L, 1);
  if (fiber->data) free(fiber->data);
  return 1;
}
static int luv_fiber_tostring(lua_State* L) {
  luv_fiber_t* fiber = lua_touserdata(L, 1);
  lua_pushfstring(L, "userdata<%s>: %p", LUV_FIBER_T, fiber);
  return 1;
}
static int luv_fiber_current(lua_State* L) {
  luv_sched_t* sched = lua_touserdata(L, lua_upvalueindex(1));
  luv_state_t* curr  = luv__sched_current(sched);
  if (curr->type == LUV_TFIBER) {
    lua_rawgeti(L, LUA_REGISTRYINDEX, ((luv_fiber_t*)curr)->ref);
  }
  return 1;
}

static int luv_sched_free(lua_State* L) {
  luv_sched_t* sched = lua_touserdata(L, 1);
  uv_loop_delete(sched->loop);
  return 1;
}

static luaL_Reg luv_sched_meths[] = {
  {"__tostring",luv_sched_tostring},
  {"__gc",      luv_sched_free},
  {NULL,        NULL}
};

static luaL_Reg luv_fiber_funcs[] = {
  {"create",    luv_new_fiber},
  {"loop",      luv_fiber_loop},
  {"cond",      luv_cond_create},
  {"current",   luv_fiber_current},
  {NULL,        NULL}
};

static luaL_Reg luv_fiber_meths[] = {
  {"join",      luv_fiber_join},
  {"ready",     luv_fiber_ready},
  {"suspend",   luv_fiber_suspend},
  {"resume",    luv_fiber_resume},
  {"__gc",      luv_fiber_free},
  {"__tostring",luv_fiber_tostring},
  {NULL,        NULL}
};

LUALIB_API int luaopenL_luv_core(lua_State *L) {

  /* set up sched metatable */
  luaL_newmetatable(L, LUV_SCHED_T);
  luaL_register(L, NULL, luv_sched_meths);
  lua_pushvalue(L, -1);
  lua_setfield(L, -2, "__index");
  lua_pop(L, 1);

  /* create sched singleton */
  luv_new_sched(L);
  lua_pushvalue(L, -1);
  lua_setfield(L, LUA_REGISTRYINDEX, LUV_SCHED_O);

  /* set up fiber metatable */
  luaL_newmetatable(L, LUV_FIBER_T);
  lua_pushvalue(L, -1);
  lua_setfield(L, -2, "__index");
  luaL_openlib(L, NULL, luv_fiber_meths, 0);
  lua_pop(L, 1);

  /* get the 'luv' lib table */
  lua_getfield(L, LUA_REGISTRYINDEX, LUV_REG_KEY);

  /* new 'fiber' table for exported funcs */
  luv__new_namespace(L, "luv_fiber");
  lua_pushvalue(L, -3); /* sched upvalue */
  luaL_openlib(L, NULL, luv_fiber_funcs, 1);

  /* LJ2 compiles coroutine.yield, so we borrow that */
  lua_getglobal(L, "coroutine");
  lua_getfield(L, -1, "yield");
  lua_setfield(L, -3, "yield");
  lua_pop(L, 1);

  /* luv.fiber */
  lua_setfield(L, -2, "fiber");

  lua_pop(L, 1);
  return 1;
}


