#include "ray_common.h"
#include "ray_lib.h"
#include "ray_actor.h"
#include "ray_codec.h"

/*
  The Ray actor system is a C abstraction which functions exclusively by
  message passing between `ray_actor_t' instances. Each actor has a mailbox
  which is a plain lua_State* and can therefore hold any arbitrary stack of
  Lua values.

  Messages are passed by calling `ray_send' which currently looks like this:

  ```
  int ray_send(ray_actor_t* self, ray_actor_t* from, int mesg)
  ```

  The `mesg' parameter is a C-level message if < 0, otherwise assumed to
  be the number of items to move from the sender to the receiver's mailbox.

  You can invent your own protocols in "user space" by pushing first a messsage
  type onto the receiver's stack and then the data, or you can extend the
  control messages negatively by doing something like this:

  #define MY_MESG1 RAY_USER
  #define MY_MESG2 MY_MESG1 - 1

*/

uv_loop_t* ray_get_loop(lua_State* L) {
  lua_getfield(L, LUA_REGISTRYINDEX, RAY_LOOP);
  uv_loop_t* loop = lua_touserdata(L, -1);
  lua_pop(L, 1);
  if (!loop) {
    loop = uv_loop_new();
    lua_pushlightuserdata(L, (void*)loop);
    lua_setfield(L, LUA_REGISTRYINDEX, RAY_LOOP);
  }
  return loop;
}

ray_actor_t* ray_current(lua_State* L) {
  lua_pushthread(L);
  lua_rawget(L, LUA_REGISTRYINDEX);
  ray_actor_t* self = (ray_actor_t*)lua_touserdata(L, -1);
  if (!self) self = ray_get_main(L);
  lua_pop(L, 1);
  return self;
}

ray_actor_t* ray_actor_new(lua_State* L, const char* m, const ray_vtable_t* v) {
  int top = lua_gettop(L);
  ray_actor_t* self = (ray_actor_t*)lua_newuserdata(L, sizeof(ray_actor_t));
  memset(self, 0, sizeof(ray_actor_t));

  if (m) {
    luaL_getmetatable(L, m);
    lua_setmetatable(L, -2);
  }
  if (v) {
    self->v = *v;
  }

  ngx_queue_init(&self->queue);
  ngx_queue_init(&self->cond);

  /* every actor has a lua_State* */
  self->L = lua_newthread(L);

  /* anchored in the registry */
  self->ref = luaL_ref(L, LUA_REGISTRYINDEX);

  /* with a copy of `self' on its stack */
  /* or not...
  lua_pushvalue(L, -1);
  lua_xmove(L, self->L, 1);
  */

  /* and knows its thread boundary */
  self->tid   = (uv_thread_t)uv_thread_self();
  self->flags = 0;

  assert(lua_gettop(L) == top + 1);
  return self;
}

static const ray_vtable_t ray_main_v = {
  send : rayM_main_send
};

int rayM_main_send(ray_actor_t* self, ray_actor_t* from, int info) {
  switch (info) {
    case RAY_ASYNC: {
      TRACE("RAY_ASYNC self: %p, from: %p\n", self, from);
      if (ngx_queue_empty(&self->queue)) {
        uv_async_send(&self->h.async);
      }
      ray_enqueue(self, from);
      break;
    }
    case RAY_YIELD: {
      ngx_queue_t* queue = &self->queue;

      int events = 0;
      lua_State* L = self->L;
      uv_loop_t* loop = ray_get_loop(L);

      ngx_queue_t* q;
      ray_actor_t* a;

      lua_settop(L, 0);

      self->flags &= ~RAY_ACTIVE;

      do {
        while (!ngx_queue_empty(queue)) {
          q = ngx_queue_head(queue);
          a = ngx_queue_data(q, ray_actor_t, cond);
          ray_dequeue(a);
          TRACE("main %p sending RAY_EVAL to %p\n", self, a);
          ray_send(a, self, RAY_EVAL);
        }

        TRACE("RUN EVENT LOOP...\n");
        events = uv_run_once(loop);
        TRACE("EVENTS: %i\n", events);

        if (ray_is_active(self)) {
          /* main has recieved a signal directly */
          TRACE("main activated\n");
          break;
        }
      }
      while (events || !ngx_queue_empty(queue));

      self->flags |= RAY_ACTIVE;

      TRACE("UNLOOP: returning: %i\n", lua_gettop(L));
      rayL_dump_stack(L);
      return lua_gettop(L);
    }
    case RAY_READY: {
      TRACE("RAY_READY\n");
      self->flags |= RAY_ACTIVE;
      uv_async_send(&self->h.async);
      break;
    }
    default: {
      /* got a data payload for main lua_State */
      TRACE("%p GOT DATA from %p\n", self, from);
      assert(info >= RAY_SEND);
      ray_xcopy(from, self, info);
      self->flags |= RAY_ACTIVE;
      uv_async_send(&self->h.async);
    }
  }
  return 0;
}

static void _async_cb(uv_async_t* handle, int status) {
  (void)handle;
  (void)status;
}

int ray_init_main(lua_State* L) {
  lua_getfield(L, LUA_REGISTRYINDEX, RAY_MAIN);
  if (lua_isnil(L, -1)) {

#ifndef WIN32
  signal(SIGPIPE, SIG_IGN);
#endif

    ray_actor_t* self = ray_actor_new(L, NULL, &ray_main_v);
    lua_pushvalue(L, -1);
    lua_setfield(L, LUA_REGISTRYINDEX, RAY_MAIN);

    self->L = L;

    assert(lua_pushthread(L)); /* must be main thread */

    /* set up our reverse lookup */
    lua_pushvalue(L, -2);
    lua_rawset(L, LUA_REGISTRYINDEX);
    assert(ray_current(L) == self);

    self->tid = (uv_thread_t)uv_thread_self();

    uv_async_init(ray_get_loop(L), &self->h.async, _async_cb);
    uv_unref(&self->h.handle);

    lua_pop(L, 1);
  }
  lua_pop(L, 1);
  return 1;
}

ray_actor_t* ray_get_main(lua_State* L) {
  lua_getfield(L, LUA_REGISTRYINDEX, RAY_MAIN);
  ray_actor_t* self = (ray_actor_t*)lua_touserdata(L, -1);
  lua_pop(L, 1);
  return self;
}

int ray_xcopy(ray_actor_t* a, ray_actor_t* b, int narg) {
  int top = lua_gettop(a->L);
  int i, base;
  if (narg == LUA_MULTRET) narg = lua_gettop(a->L);
  base = lua_gettop(a->L) - narg + 1;
  lua_checkstack(a->L, narg);
  lua_checkstack(b->L, narg);
  for (i = base; i < base + narg; i++) {
    lua_pushvalue(a->L, i);
  }
  lua_xmove(a->L, b->L, narg);
  assert(lua_gettop(a->L) == top);
  return narg;
}

int ray_push(ray_actor_t* self, int narg) {
  int i, base;
  if (narg == LUA_MULTRET) narg = lua_gettop(self->L);
  base = lua_gettop(self->L) - narg + 1;
  lua_checkstack(self->L, narg);
  for (i = base; i < base + narg; i++) {
    lua_pushvalue(self->L, i);
  }
  return narg;
}

int ray_notify(ray_actor_t* self, int info) {
  int count = 0;
  ngx_queue_t* q;
  ray_actor_t* a;
  while (!ngx_queue_empty(&self->queue)) {
    q = ngx_queue_head(&self->queue);
    a = ngx_queue_data(q, ray_actor_t, cond);
    ray_dequeue(a);
    ray_send(a, self, info);
    count++;
  }
  return count;
}

/* wake one waiting actor */
int ray_signal(ray_actor_t* self, int info) {
  int seen = 0;
  ngx_queue_t* q;
  ray_actor_t* a;
  if (!ngx_queue_empty(&self->queue)) {
    q = ngx_queue_head(&self->queue);
    a = ngx_queue_data(q, ray_actor_t, cond);
    ray_dequeue(a);
    ray_send(a, self, info);
    seen++;
  }
  return seen;
}

int ray_actor_free(ray_actor_t* self) {
  /* unanchor from registry */
  if (self->ref != LUA_NOREF) {
    lua_settop(self->L, 0);
    luaL_unref(self->L, LUA_REGISTRYINDEX, self->ref);
    self->ref = LUA_NOREF;
  }
  return 1;
}

/* send `self' a message, moving nargs from `from's stack  */
int ray_send(ray_actor_t* self, ray_actor_t* from, int info) {
  TRACE("from: %p, to %p, info: %i\n", from, self, info);
  return self->v.send(self, from, info);
}

/* terminate a state */
int ray_close(ray_actor_t* self) {
  self->flags |= RAY_CLOSED;
  return self->v.close(self);
}
