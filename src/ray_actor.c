#include "ray_common.h"
#include "ray_lib.h"
#include "ray_actor.h"
#include "ray_queue.h"
#include "ray_codec.h"

ray_actor_t* ray_get_main(lua_State* L) {
  lua_getfield(L, LUA_REGISTRYINDEX, RAY_MAIN);
  ray_actor_t* self = (ray_actor_t*)lua_touserdata(L, -1);
  lua_pop(L, 1);
  return self;
}

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

void ray_push_actor(lua_State* L, ray_actor_t* actor) {
  lua_checkstack(L, 2);
  lua_getfield(L, LUA_REGISTRYINDEX, RAY_WEAK);
  lua_rawgeti(L, -1, actor->id);
  lua_remove(L, -2);
  assert(lua_type(L, -1) == LUA_TUSERDATA);
}

void ray_push_self(ray_actor_t* self) {
  ray_push_actor(self->L, self);
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

uint32_t ray_genid(void) {
  static uint32_t x = 1;
  x = x * 2621124293u + 1;
  return x;
}

ray_msg_t ray_mailbox_get(ray_actor_t* self) {
  ray_msg_t msg = ray_queue_get_integer(&self->mbox);
  ray_queue_get_tuple(&self->mbox, self->L);
  return msg;
}
void ray_mailbox_put(ray_actor_t* self, lua_State* L, ray_msg_t msg) {
  int narg = ray_msg_data(msg);
  if (narg < 0) narg = lua_gettop(L);
  ray_queue_put_integer(&self->mbox, msg);
  ray_queue_put_tuple(&self->mbox, L, narg);
}
int ray_mailbox_empty(ray_actor_t* self) {
  return ray_queue_empty(&self->mbox);
}

ray_actor_t* ray_actor_new(lua_State* L, const char* m, const ray_vtable_t* v) {
  ray_actor_t* self = (ray_actor_t*)lua_newuserdata(L, sizeof(ray_actor_t));
  memset(self, 0, sizeof(ray_actor_t));

  if (m) {
    luaL_getmetatable(L, m);
  }
  else {
    luaL_getmetatable(L, RAY_ACTOR_T);
  }
  lua_setmetatable(L, -2);

  if (v) {
    self->v = *v;
  }

  self->L     = lua_newthread(L);
  self->L_ref = luaL_ref(L, LUA_REGISTRYINDEX);
  self->id    = ray_genid();
  self->tid   = (uv_thread_t)uv_thread_self();

  /* set up lookup mapping from id to the full userdata */
  lua_getfield(L, LUA_REGISTRYINDEX, RAY_WEAK);
  lua_pushvalue(L, -2);
  lua_rawseti(L, -2, self->id);
  lua_pop(L, 1);

  /* anchored in the current lua_State* with no extra refs */
  self->ref = LUA_NOREF;
  self->ref_count = 0;

  ngx_queue_init(&self->cond);
  ray_queue_init(&self->mbox, L, RAY_MBOX_SIZE);

  return self;
}

typedef uint32_t ray_aid_t;

ray_actor_t* ray_get_actor(lua_State* L, ray_aid_t id) {
  lua_checkstack(L, 2);
  lua_getfield(L, LUA_REGISTRYINDEX, RAY_WEAK);
  lua_rawgeti(L, -1, id);
  ray_actor_t* actor = (ray_actor_t*)lua_touserdata(L, -1);
  lua_pop(L, 2);
  return actor;
}

int ray_yield(ray_actor_t* self) {
  return self->v.yield(self);
}
int ray_close(ray_actor_t* self) {
  return self->v.close(self);
}
int ray_react(ray_actor_t* self, ray_actor_t* from, ray_msg_t msg) {
  return self->v.react(self, from, msg);
}

int ray_send(ray_actor_t* self, ray_actor_t* from, ray_msg_t msg) {
  int narg = ray_msg_data(msg);

  if (narg && from != self) {
    if (narg < 0) narg = lua_gettop(from->L);
    ray_mailbox_put(self, from->L, msg);
  }

  self->v.react(self, from, msg);
  return narg;
}

ray_msg_t ray_recv(ray_actor_t* self) {
  if (!ray_mailbox_empty(self)) {
    return ray_mailbox_get(self);
  }
  return ray_yield(self);
}

int ray_free(ray_actor_t* self) {
  /* unanchor from registry */
  if (self->L_ref != LUA_NOREF) {
    lua_settop(self->L, 0);
    luaL_unref(self->L, LUA_REGISTRYINDEX, self->L_ref);
    self->L_ref = LUA_NOREF;
  }
  return ray_close(self);
}

/* reference counting */
void ray_incref(ray_actor_t* self) {
  //TRACE("refcount: %i\n", self->ref_count);
  if (self->ref_count++ == 0) {
    ray_push_self(self);
    self->ref = luaL_ref(self->L, LUA_REGISTRYINDEX);
  }
}
void ray_decref(ray_actor_t* self) {
  //TRACE("refcount: %i\n", self->ref_count);
  if (--self->ref_count == 0) {
    luaL_unref(self->L, LUA_REGISTRYINDEX, self->ref);
    self->ref = LUA_NOREF;
  }
}

int ray_hash_set_actor(ray_hash_t* self, const char* key, ray_actor_t* val) {
  if (!ray_hash_lookup(self, key)) {
    ray_incref(val);
  }
  return ray_hash_set(self, key, val);
}
ray_actor_t* ray_hash_remove_actor(ray_hash_t* self, const char* key) {
  ray_actor_t* val = (ray_actor_t*)ray_hash_remove(self, key);
  ray_decref(val);
  return val;
}

void ray_queue_put_actor(ray_queue_t* self, ray_actor_t* item) {
  ray_push_actor(self->L, item);
  ray_queue_put(self);
}
ray_actor_t* ray_queue_get_actor(ray_queue_t* self) {
  if (!ray_queue_get(self)) return NULL;
  ray_actor_t* item = (ray_actor_t*)lua_touserdata(self->L, -1);
  lua_pop(self->L, 1);
  return item;
}

/* default actor meta-methods */
static int actor_free(lua_State* L) {
  ray_actor_t* self = lua_touserdata(L, 1);
  ray_free(self);
  return 1;
}
static int actor_tostring(lua_State* L) {
  ray_actor_t* self = (ray_actor_t*)lua_touserdata(L, 1);
  const char* name = RAY_ACTOR_T;
  luaL_getmetafield(L, 1, "__name");
  if (!lua_isnil(L, -1)) {
    lua_pop(L, 1);
    name = lua_tostring(L, -1);
  }
  lua_pushfstring(L, "userdata<%s>: %p", name, self);
  return 1;
}

static luaL_Reg actor_meths[] = {
  {"__gc",        actor_free},
  {"__tostring",  actor_tostring},
  {NULL,          NULL}
};

/* allows us to interrupt the event loop if main gets a message */
static void _main_async_cb(uv_async_t* handle, int status) {
  (void)status;
  (void)handle;
}

static int _main_yield(ray_actor_t* self) {
  int events = 0;
  lua_State* L = self->L;
  uv_loop_t* loop = ray_get_loop(L);
  lua_settop(L, 0);

  ngx_queue_t* ready = (ngx_queue_t*)self->u.data;

  self->flags &= ~RAY_ACTIVE;
  do {
    while (!ngx_queue_empty(ready)) {
      ngx_queue_t* q = ngx_queue_last(ready);
      ray_actor_t* a = ngx_queue_data(q, ray_actor_t, cond);
      ray_decref(a);
      ngx_queue_remove(q);
      a->v.react(a, self, ray_msg(RAY_ASYNC,0));
    }
    events = uv_run_once(loop);
    if (ray_is_active(self)) break;
  }
  while (events || !ngx_queue_empty(ready));

  self->flags |= RAY_ACTIVE;
  return lua_gettop(L);
}

static int _main_react(ray_actor_t* self, ray_actor_t* from, ray_msg_t msg) {
  switch (ray_msg_type(msg)) {
    case RAY_ASYNC: {
      ngx_queue_t* ready = (ngx_queue_t*)self->u.data;
      ngx_queue_insert_head(ready, &from->cond);
      ray_incref(from);
    }
    default: {
      ray_mailbox_get(self);
      self->flags |= RAY_ACTIVE;
    }
  }
  uv_async_send(&self->h.async);
  return 0;
}

static int _main_close(ray_actor_t* self) {
  if (self->u.data) {
    free(self->u.data);
  }
  return 1;
}

static ray_vtable_t main_v = {
  react: _main_react,
  yield: _main_yield,
  close: _main_close
};

/* initialize the main actor */
int ray_init_main(lua_State* L) {
  lua_getfield(L, LUA_REGISTRYINDEX, RAY_MAIN);
  if (lua_isnil(L, -1)) {

#ifndef WIN32
    signal(SIGPIPE, SIG_IGN);
#endif

    /* weak value table for id to udata maps */
    lua_newtable(L);
    lua_newtable(L);
    lua_pushstring(L, "v");
    lua_setfield(L, -2, "__blah");
    lua_setmetatable(L, -2);
    lua_setfield(L, LUA_REGISTRYINDEX, RAY_WEAK);

    /* default actor metatable */
    rayL_class(L, RAY_ACTOR_T, actor_meths);
    lua_pop(L, 1);

    ray_actor_t* self = ray_actor_new(L, RAY_ACTOR_T, &main_v);
    lua_pushvalue(L, -1);
    lua_setfield(L, LUA_REGISTRYINDEX, RAY_MAIN);

    self->L = L;

    assert(lua_pushthread(L)); /* must be main thread */

    /* set up our reverse lookup */
    lua_pushvalue(L, -2);
    lua_rawset(L, LUA_REGISTRYINDEX);
    assert(ray_current(L) == self);

    self->tid = (uv_thread_t)uv_thread_self();

    /* queue for fibers */
    self->u.data = malloc(sizeof(ngx_queue_t));
    ngx_queue_init((ngx_queue_t*)self->u.data);

    uv_async_init(ray_get_loop(L), &self->h.async, _main_async_cb);
    uv_unref(&self->h.handle);

    lua_pop(L, 1);
  }
  lua_pop(L, 1);
  return 1;
}

