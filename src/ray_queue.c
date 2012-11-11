#include "ray_lib.h"
#include "ray_actor.h"

ray_queue_t* ray_queue_new(lua_State* L, int size) {
  ray_queue_t* self = (ray_queue_t*)malloc(sizeof(ray_queue_t));
  ray_queue_init(self, L, size);
  return self;
}

void ray_queue_init(ray_queue_t* self, lua_State* L, size_t size) {
  self->L     = lua_newthread(L);
  self->L_ref = luaL_ref(L, LUA_REGISTRYINDEX);
  self->size  = size || 32;
  self->head  = 0;
  self->tail  = 0;
  lua_checkstack(self->L, self->size);
  lua_settop(self->L, self->size);
}

void ray_queue_put(ray_queue_t* self) {
  if (ray_queue_full(self)) {
    int size = self->size;
    int new_size = size * 2;

    lua_checkstack(self->L, size + 1);
    lua_settop(self->L, new_size - 1);

    int i;
    for (i = size; i > self->tail % size; i--) {
      lua_pushvalue(self->L, i);
      lua_replace(self->L, i + size);
      lua_pushnil(self->L);
      lua_replace(self->L, i);
    }

    self->tail += size;
    self->head += size;
    self->size = new_size;
  }

  lua_replace(self->L, ray_queue_head(self));
  ++self->head;
}

int ray_queue_get(ray_queue_t* self) {
  int slot = ray_queue_slot(self, self->tail);
  if (lua_isnoneornil(self->L, slot)) return 0;

  lua_pushvalue(self->L, slot);
  lua_pushnil(self->L);
  lua_replace(self->L, slot);

  ++self->tail;
  return 1;
}


void ray_queue_put_value(ray_queue_t* self, lua_State* L) {
  lua_xmove(L, self->L, 1);
  ray_queue_put(self);
}
int ray_queue_get_value(ray_queue_t* self, lua_State* L) {
  if (!ray_queue_get(self)) return 0;
  lua_xmove(self->L, L, 1);
  return 1;
}

void ray_queue_put_tuple(ray_queue_t* self, lua_State* L, int nval) {
  if (nval == LUA_MULTRET) nval = lua_gettop(L);
  lua_pushinteger(self->L, nval);
  ray_queue_put(self);
  while (--nval >= 0) {
    lua_xmove(L, self->L, 1);
    ray_queue_put(self);
  }
}
int ray_queue_get_tuple(ray_queue_t* self, lua_State* L) {
  if (!ray_queue_get(self)) return 0;

  int nval = luaL_checkint(self->L, -1);
  lua_pop(self->L, 1);

  lua_checkstack(L, nval);
  lua_checkstack(self->L, nval);

  while (--nval >= 0) {
    ray_queue_get(self);
  }

  lua_xmove(self->L, L, nval);
  return nval;
}

void ray_queue_put_number(ray_queue_t* self, lua_Number val) {
  lua_pushnumber(self->L, val);
  ray_queue_put(self);
}
lua_Number ray_queue_get_number(ray_queue_t* self) {
  if (!ray_queue_get(self)) return 0;
  lua_Number val = lua_tonumber(self->L, -1);
  lua_pop(self->L, 1);
  return val;
}

void ray_queue_put_integer(ray_queue_t* self, lua_Integer val) {
  lua_pushinteger(self->L, val);
  ray_queue_put(self);
}
lua_Integer ray_queue_get_integer(ray_queue_t* self) {
  if (!ray_queue_get(self)) return 0;
  lua_Integer val = lua_tointeger(self->L, -1);
  lua_pop(self->L, 1);
  return val;
}

void ray_queue_put_boolean(ray_queue_t* self, int val) {
  lua_pushboolean(self->L, val);
  ray_queue_put(self);
}
int ray_queue_get_boolean(ray_queue_t* self) {
  if (!ray_queue_get(self)) return 0;
  int val = lua_toboolean(self->L, -1);
  lua_pop(self->L, 1);
  return val;
}

void ray_queue_put_string(ray_queue_t* self, const char* str) {
  lua_pushstring(self->L, str);
  ray_queue_put(self);
}
const char* ray_queue_get_string(ray_queue_t* self) {
  if (!ray_queue_get(self)) return NULL;
  const char* str = lua_tostring(self->L, -1);
  lua_pop(self->L, 1);
  return str;
}

void ray_queue_put_lstring(ray_queue_t* self, const char* str, size_t len) {
  lua_pushlstring(self->L, str, len);
  ray_queue_put(self);
}
const char* ray_queue_get_lstring(ray_queue_t* self, size_t* len) {
  if (!ray_queue_get(self)) return NULL;
  const char* str = lua_tolstring(self->L, -1, len);
  lua_pop(self->L, 1);
  return str;
}

void ray_queue_put_cfunction(ray_queue_t* self, lua_CFunction fun) {
  lua_pushcfunction(self->L, fun);
  ray_queue_put(self);
}
lua_CFunction ray_queue_get_cfunction(ray_queue_t* self) {
  if (!ray_queue_get(self)) return NULL;
  lua_CFunction fun = lua_tocfunction(self->L, -1);
  lua_pop(self->L, 1);
  return fun;
}

void ray_queue_put_pointer(ray_queue_t* self, void* ptr) {
  lua_pushlightuserdata(self->L, ptr);
  ray_queue_put(self);
}
void* ray_queue_get_pointer(ray_queue_t* self) {
  if (!ray_queue_get(self)) return NULL;
  void* ptr = lua_touserdata(self->L, -1);
  lua_pop(self->L, 1);
  return ptr;
}

int ray_queue_peek(ray_queue_t* self, int idx) {
  int slot = ray_queue_slot(self, idx);
  if (lua_isnoneornil(self->L, slot)) return 0;
  lua_pushvalue(self->L, slot);
  return 1;
}
int ray_queue_peek_value(ray_queue_t* self, lua_State* L, int idx) {
  if (!ray_queue_peek(self, idx)) return 0;
  lua_xmove(self->L, L, 1);
  return 1;
}

void ray_queue_free(ray_queue_t* self) {
  lua_settop(self->L, 0);
  luaL_unref(self->L, LUA_REGISTRYINDEX, self->L_ref);
  free(self);
}
