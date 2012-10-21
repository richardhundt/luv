#include "ray_common.h"
#include "ray_object.h"
#include "ray_pipe.h"

static void _idle_cb(uv_idle_t* handle, int status) {
  ray_object_t* self = container_of(handle, ray_object_t, h);
  ngx_queue_t* q;
  ray_actor_t* s;
  ngx_queue_foreach(q, &self->send) {
    s = ngx_queue_data(q, ray_actor_t, cond);
    lua_settop(s->L, 0);
    lua_pushinteger(s->L, status);
  }
  rayL_cond_broadcast(&self->send);
}

static int ray_idle_new(lua_State* L) {
  ray_object_t* self = rayL_object_new(L, RAY_IDLE_T);
  rayL_object_listen(self, idle, _idle_cb);
  uv_idle_init(curr->loop, &self->h.idle);
  return 1;
}

/* methods */
static int ray_idle_start(lua_State* L) {
  ray_object_t* self = (ray_object_t*)luaL_checkudata(L, 1, RAY_IDLE_T);
  int rv = uv_idle_start(&self->h.idle, _idle_cb);
  lua_pushinteger(L, rv);
  return 1;
}

static int ray_idle_stop(lua_State* L) {
  ray_object_t* self = (ray_object_t*)luaL_checkudata(L, 1, RAY_IDLE_T);
  lua_pushinteger(L, uv_idle_stop(&self->h.idle));
  return 1;
}

static int ray_idle_wait(lua_State *L) {
  ray_object_t* self = (ray_object_t*)luaL_checkudata(L, 1, RAY_IDLE_T);
  ray_actor_t* state = rayL_state_self(L);
  return rayL_cond_wait(&self->send, state);
}

static int ray_idle_free(lua_State *L) {
  ray_object_t* self = (ray_object_t*)lua_touserdata(L, 1);
  rayL_object_close(self);
  return 1;
}
static int ray_idle_tostring(lua_State *L) {
  ray_object_t* self = (ray_object_t*)luaL_checkudata(L, 1, RAY_IDLE_T);
  lua_pushfstring(L, "userdata<%s>: %p", RAY_IDLE_T, self);
  return 1;
}

luaL_Reg ray_idle_funcs[] = {
  {"create",    ray_idle_new},
  {NULL,        NULL}
};

luaL_Reg ray_idle_meths[] = {
  {"start",     ray_idle_start},
  {"stop",      ray_idle_stop},
  {"wait",      ray_idle_wait},
  {"__gc",      ray_idle_free},
  {"__tostring",ray_idle_tostring},
  {NULL,        NULL}
};
