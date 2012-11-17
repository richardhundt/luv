#include "ray_lib.h"
#include "ray_queue.h"

ray_queue_t* ray_queue_new(size_t size) {
  ray_queue_t* self = (ray_queue_t*)calloc(1, sizeof(ray_queue_t));
  self->size = size;
  self->ncap = size * 2;
  self->nval = 0;
  self->head = 0;
  self->tail = 0;
  self->data = (ray_item_t*)calloc(self->ncap, sizeof(ray_item_t));
  self->info = (size_t*)calloc(self->size, sizeof(size_t));
  ngx_queue_init(&self->wput);
  ngx_queue_init(&self->wget);
  return self;
}

static inline int _queue_need(ray_queue_t* self, size_t need) {
  if (need >= self->ncap) {
    int ncap, new_ncap;
    ncap = self->ncap;

    do {
      new_ncap = ncap * 2;
    } while (new_ncap <= need);

    ray_item_t* new_data = realloc(self->data, new_ncap * sizeof(ray_item_t));
    if (!new_data) return 0;

    self->data = new_data;
    self->ncap = new_ncap;
  }
  return 1;
}

static inline int _queue_send(ray_queue_t* self, ray_state_t* curr) {
  lua_State* L = curr->L;

  int slot, narg, i;
  slot = ray_queue_slot(self, self->tail);
  narg = self->info[slot];
  ++self->tail;

  for (i = 0; i < narg; i++) {
    ray_item_t* item = &self->data[slot + i];
    switch (item->type) {
      case LUA_TNIL: {
        lua_pushnil(L);
        break;
      }
      case LUA_TBOOLEAN: {
        lua_pushboolean(L, item->u.bit);
        break;
      }
      case LUA_TNUMBER: {
        lua_pushnumber(L, item->u.num);
        break;
      }
      case LUA_TSTRING: {
        ray_buf_t buf = item->u.buf;
        lua_pushlstring(L, (const char*)buf.base, buf.len);
        free(buf.base);
        buf.base = NULL;
        buf.len  = 0;
        break;
      }
      case LUA_TUSERDATA: {
        lua_rawgeti(L, LUA_REGISTRYINDEX, item->u.bit);
        luaL_unref(L, LUA_REGISTRYINDEX, item->u.bit);
        break;
      }
      case LUA_TLIGHTUSERDATA: {
        lua_pushlightuserdata(L, item->u.ptr);
        break;
      }
      default: {
        return luaL_error(L, "Bad encoded data");
      }
    }
  }

  self->nval -= narg;
  TRACE("moved %i, to %p\n", narg, curr);
  return narg;
}

int ray_queue_put(ray_queue_t* self, ray_state_t* curr, int narg) {

  /* if full then enqueue and park the curr state */
  if (ray_queue_full(self)) {
    if (!ngx_queue_empty(&self->wget)) {
      ngx_queue_t* q = ngx_queue_head(&self->wget);
      ray_state_t* s = ngx_queue_data(q, ray_state_t, cond);
      ray_cond_dequeue(s);
      ray_ready(s);
      _queue_send(self, s);
    }
    ngx_queue_insert_tail(&self->wput, &curr->cond);
    return ray_yield(curr);
  }

  lua_State* L = curr->L;

  /* support LUA_MULTRET */
  if (narg < 0) narg = lua_gettop(L);

  /* make sure we have space to store the tuple */
  _queue_need(self, self->nval + narg);

  int t, slot, i, base;
  base = lua_gettop(L) - narg + 1;

  /* logically it's one entry; record the tuple size */
  slot = ray_queue_head(self);
  self->info[slot] = narg;
  ++self->head;

  for (i = 0; i < narg; i++) {
    ray_item_t* item = &self->data[slot + i];
    t = lua_type(L, base + i);
    item->type = t;
    switch (t) {
      case LUA_TNIL: {
        break;
      }
      case LUA_TBOOLEAN: {
        item->u.bit = lua_toboolean(L, i + base);
        break;
      }
      case LUA_TNUMBER: {
        item->u.num = lua_tonumber(L, i + base);
        break;
      }
      case LUA_TSTRING: {
        size_t len;
        const char* val = lua_tolstring(L, i + base, &len);
        ray_buf_t   buf = item->u.buf;
        buf.len  = len;
        buf.base = (char*)malloc(len);
        memcpy(buf.base, val, len);
        break;
      }
      case LUA_TUSERDATA: {
        lua_pushvalue(L, i + base);
        item->u.bit = luaL_ref(L, LUA_REGISTRYINDEX);
        break;
      }
      case LUA_TLIGHTUSERDATA: {
        item->u.ptr = lua_touserdata(L, i + base);
        break;
      }
      default: {
        return luaL_error(L, "cannot put a %s\n", lua_typename(L, t));
      }
    }
  }

  /* send semantics, so clear them from the sender's stack */
  lua_settop(L, base);

  self->nval += narg;

  if (!ngx_queue_empty(&self->wget)) {
    ngx_queue_t* q = ngx_queue_head(&self->wget);
    ray_state_t* s = ngx_queue_data(q, ray_state_t, cond);
    ray_cond_dequeue(s);
    ray_ready(s);
    _queue_send(self, s);
  }
  return 0;
}

int ray_queue_get(ray_queue_t* self, ray_state_t* curr) {
  if (!ngx_queue_empty(&self->wput)) {
    ngx_queue_t* q = ngx_queue_head(&self->wput);
    ray_state_t* s = ngx_queue_data(q, ray_state_t, cond);
    ray_cond_dequeue(s);
    TRACE("[GET] waking WPUT : %p\n", s);
    ray_ready(s);
  }

  if (ray_queue_empty(self)) {
    TRACE("[GET] queue is empty, yielding %p\n", curr);
    ray_cond_enqueue(&self->wget, curr);
    return ray_yield(curr);
  }

  return _queue_send(self, curr);
}

void ray_queue_free(ray_queue_t* self) {
  free(self->data);
  free(self->info);
  free(self);
}

