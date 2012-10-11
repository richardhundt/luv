#include "lua.h"
#include "luv_cond.h"
#include "luv_state.h"
#include "luv_object.h"

void luvL_object_init(luv_state_t* state, luv_object_t* self) {
  luvL_list_init(&self->rouse);
  luvL_list_init(&self->queue);
  self->data  = NULL;
  self->flags = 0;
  self->loop  = state->loop;
  self->count = 0;
  self->buf.base = NULL;
  self->buf.len  = 0;
  self->hash = NULL;
  self->list = NULL;
}

luv_object_t* luvL_object_new(lua_State* L, const char* meta) {
  luv_object_t* self = (luv_object_t*)lua_newuserdata(L, sizeof(luv_object_t));
  luaL_getmetatable(L, meta);
  lua_setmetatable(L, -2);
  luvL_object_init(luvL_state_self(L), self);
  return self;
}

luv_object_t* luvL_object_check(lua_State* L, int idx, const char* meta) {
  return (luv_object_t*)luvL_checkudata(L, idx, meta);
}

void luvL_object_set_cb(luv_object_t* self, size_t ev, luv_event_cb cb) {
  self->event[ev] = cb;
}

void luvL_object_close_cb(luv_object_t* self) {
  TRACE("object closed %p\n", self);
  self->flags |= LUV_OCLOSED;
  luvL_cond_signal(&self->rouse);
}

void luvL_object_close(luv_object_t* self) {
  if (!luvL_object_is_closing(self)) {
    TRACE("object closing %p, type: %i\n", self, self->h.handle.type);
    self->flags |= LUV_OCLOSING;
    uv_close(&self->h.handle, luvL_object_close_cb);
  }
}

int luvL_object_free(luv_object_t* self) {
  if (self->hash) luvL_hash_free(self->hash);
  if (self->list) luvL_list_free(self->list);
}


