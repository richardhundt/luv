#include "ray_lib.h"
#include "ray_state.h"
#include "ray_actor.h"

ray_actor_t* rayL_actor_new(lua_State* L, const char* meta) {
  ray_actor_t* self = (ray_actor_t*)lua_newuserdata(L, sizeof(ray_actor_t));
  memset(self, 0, sizeof(ray_actor_t));

  self->L = lua_newthread(L);
  lua_pushvalue(L, -2);
  lua_xmove(L, self->L, 1);
  lua_replace(self->L, lua_upvalueindex(1));

  if (meta) {
    luaL_getmetatable(L, meta);
    lua_setmetatable(L, -2);
  }

  ngx_queue_init(&self->rouse);
  ngx_queue_init(&self->queue);

  return self;
}

uv_loop_t* rayL_event_loop(lua_State* L) {
  lua_getfield(L, LUA_REGISTRYINDEX, "ray:event:loop");
  uv_loop_t* loop = (uv_loop_t*)lua_touserdata(L, -1);
  lua_pop(L, 1);
  return loop;
}

ray_actor_t* rayL_actor_self(lua_State* L) {
  return (ray_actor_t*)lua_touserdata(L, 1);
}

int rayL_actor_xcopy(ray_actor_t* a, ray_actor_t* b) {
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

void rayL_actor_ready(ray_actor_t* self) {
  self->ready(self);
}

int rayL_actor_yield(ray_actor_t* self) {
  return self->yield(self);
}

int rayL_actor_suspend(ray_actor_t* self) {
  return self->suspend(self);
}

int rayL_actor_resume(ray_actor_t* self) {
  return self->resume(self);
}

int rayL_actor_close(ray_actor_t* self) {
  return self->close(self);
}

int rayL_actor_send(ray_actor_t* self, ray_actor_t* recv, int narg) {
  //return self->send(self, recv, narg);
  rayL_actor_xcopy(self->L, recv->L, narg);
  rayL_actor_ready(recv);
}

int rayL_actor_recv(ray_actor_t* self) {
  int narg = lua_gettop(self->L);
  if (!narg) {
    return rayL_actor_suspend(self);
  }
  return narg;
}

