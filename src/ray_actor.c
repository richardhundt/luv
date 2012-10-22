#include "ray_common.h"
#include "ray_lib.h"
#include "ray_actor.h"
#include "ray_codec.h"

/*
  The Ray actor system is a C abstraction which functions by synchronous
  message passing between `ray_actor_t' instances. Each actor has a mailbox
  which is conceptually unbuffered, i.e. it can contain at most one logical
  message. However, the mailbox is a plain lua_State* pointer and a message
  is a Lua tuple, and so may be of any size.

  Each actor also has a signal queue of actors waiting on notifications
  from it. While each actor may have many actors waiting on it, an actor
  can only be waiting on one other actor (which may be itself).

  Messages are sent by calling `ray_send`, and awaited by calling `ray_recv`,
  which have the following semantics:

  * `ray_send' copies messages to the receiving actor's mailbox and calls
    its `send' hook to notify it that it has mail.

  * `ray_recv' puts the receiving actor in the target actor's notification
    queue, and calls the receiving actor's `recv' hook.

  Although messages are delivered synchronously, an actor may choose to
  react asynchronously. This means that the actor needs to have a way to
  `yield' to allow other actors of which it knows nothing, to run, while
  ensuring that it is given a timeslice back at a later time.

  To make this work, we have a "main" actor which acts as a trampoline.

  Asynchronous reaction then has the following pattern:

  if from == main then
    -- resume
  else
    main:wait(self)
    -- yield
  end

  That is: to `yield' to other actors, the `main' actor is told to wait for
  us. The `main' actor's waiting behaviour is then to simply signal back to
  actors in its signal queue, until the queue is exhausted, or it has been
  sent a message directly, to pass back to the main Lua state.

  The point of all this is to decouple the actor system from the Lua VM, to
  allow graphs of C level actors to communicate without entering the Lua VM.

  This makes it possible, for instance, to place an HTTP "filters" around
  a TCP stream and a fiber reading from the filter is only resumed once the
  HTTP filter has parsed and constructed a full HTTP message.

  So in general you can build any kind of C level sources, filters and sinks
  and only wire up the end points into Lua at the Lua-C boundary (i.e. the
  userdata interface presented to Lua by its metatable).

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

ray_actor_t* ray_get_self(lua_State* L) {
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
  /*
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
  recv : rayM_main_recv,
  send : rayM_main_send
};

int rayM_main_send(ray_actor_t* self, ray_actor_t* from, int narg) {
  TRACE("%p sending to main, narg: %i...\n", from, narg);
  lua_State* L = (lua_State*)self->u.data;
  if (ngx_queue_empty(&self->queue)) {
    uv_async_send(&self->h.async);
  }
  lua_xmove(self->L, L, narg);
  return 0;
}

int rayM_main_recv(ray_actor_t* self, ray_actor_t* from) {
  TRACE("ENTER MAIN AWAIT, queue empty? %i\n", ngx_queue_empty(&self->queue));

  uv_loop_t*   loop  = self->h.handle.loop;
  ngx_queue_t* queue = &self->queue;

  if (self->flags & RAY_MAIN_ACTIVE) {
    /* flatten recursion, we have a queue */
    int interrupt = ngx_queue_empty(queue);
    ray_enqueue(self, from);
    if (interrupt) {
      uv_async_send(&self->h.async);
    }
    return 0;
  }

  ray_enqueue(self, from);

  int events = 0;
  lua_State* L = (lua_State*)self->u.data;

  ngx_queue_t* q;
  ray_actor_t* a;

  lua_settop(L, 0);

  self->flags |= RAY_MAIN_ACTIVE;

  do {
    TRACE("MAIN sending signal\n");
    while (!ngx_queue_empty(queue)) {
      q = ngx_queue_head(queue);
      a = ngx_queue_data(q, ray_actor_t, cond);
      ray_send(a, a, 0);
      if (a == from) break;
    }

    events = uv_run_once(loop);

    /* ray_is_active tests the actor's active state, not the main lua_State */
    if (ray_is_active(self)) {
      /* main has recieved a signal directly */
      TRACE("main activated\n");
      break;
    }
  }
  while (events || !ngx_queue_empty(queue));

  /* main lua_State* not pretending to be running */
  self->flags &= ~RAY_MAIN_ACTIVE;

  if (!ray_is_active(self)) {
    return luaL_error(L, "FATAL: deadlock detected");
  }

  TRACE("UNLOOP: returning: %i\n", lua_gettop(L));
  rayL_dump_stack(L);
  return lua_gettop(L);
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

    self->u.data = L;

    assert(lua_pushthread(L)); /* must be main thread */

    /* set up our reverse lookup */
    lua_pushvalue(L, -2);
    lua_rawset(L, LUA_REGISTRYINDEX);
    assert(ray_get_self(L) == self);

    self->tid = (uv_thread_t)uv_thread_self();

    uv_async_init(ray_get_loop(L), &self->h.async, _async_cb);
    uv_unref(&self->h.handle);

    lua_settop(self->L, 0);
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
  base = lua_gettop(a->L) - narg + 1;
  lua_checkstack(a->L, narg);
  lua_checkstack(b->L, narg);
  lua_settop(b->L, 0);
  for (i = base; i < base + narg; i++) {
    lua_pushvalue(a->L, i);
  }
  lua_xmove(a->L, b->L, narg);
  assert(lua_gettop(a->L) == top);
  return narg;
}

int ray_push(ray_actor_t* self, int narg) {
  int i, base;
  base = lua_gettop(self->L) - narg + 1;
  lua_checkstack(self->L, narg);
  for (i = base; i < base + narg; i++) {
    lua_pushvalue(self->L, i);
  }
  return narg;
}

/* broadcast to all waiting actors */
int ray_notify(ray_actor_t* self, int narg) {
  int count = 0;
  ngx_queue_t* q;
  ray_actor_t* s;
  if (narg == LUA_MULTRET) narg = lua_gettop(self->L);
  while (!ngx_queue_empty(&self->queue)) {
    q = ngx_queue_head(&self->queue);
    s = ngx_queue_data(q, ray_actor_t, cond);
    ray_push(self, narg);
    ray_send(s, self, narg);
    count++;
  }
  if (narg) lua_pop(self->L, narg);
  return count;
}

/* wake one waiting actor */
int ray_signal(ray_actor_t* self, int narg) {
  int count = 0;
  ngx_queue_t* q;
  ray_actor_t* s;
  if (narg == LUA_MULTRET) narg = lua_gettop(self->L);
  if (!ngx_queue_empty(&self->queue)) {
    q = ngx_queue_head(&self->queue);
    s = ngx_queue_data(q, ray_actor_t, cond);
    ray_push(self, narg);
    ray_send(s, self, narg);
    count++;
  }
  if (narg) lua_pop(self->L, narg);
  return count;
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

void ray_schedule(ray_actor_t* that) {
  ray_actor_t* boss = ray_get_main(that->L);
  int interrupt = ngx_queue_empty(&boss->queue);
  ray_enqueue(boss, that);
  if (interrupt) {
    uv_async_send(&boss->h.async);
  }
}

/* send `self' a message, moving nargs from `from's stack  */
int ray_send(ray_actor_t* self, ray_actor_t* from, int narg) {
  if (narg == LUA_MULTRET) narg = lua_gettop(from->L);
  if (self->tid == from->tid) {
    /* same global state */
    if (from != self) {
      lua_xmove(from->L, self->L, narg);
    }
  }
  else {
    size_t len;
    ray_codec_encode(from->L, narg);
    const char* data = lua_tolstring(from->L, -1, &len);
    lua_pushlstring(self->L, data, len);
    ray_codec_decode(self->L);
    lua_pop(from->L, narg);
  }

  ray_dequeue(self);
  return self->v.send(self, from, narg);
}

/* wait for a message from `from' */
int ray_recv(ray_actor_t* self, ray_actor_t* from) {
  ray_enqueue(from, self);
  return self->v.recv(self, from);
}
/* terminate a state */
int ray_close(ray_actor_t* self) {
  self->flags |= RAY_CLOSED;
  return self->v.close(self);
}
