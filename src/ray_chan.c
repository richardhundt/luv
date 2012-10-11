#include "ray.h"

static int ray_new_chan(lua_State* L) {
  ray_state_t* curr = rayL_state_self(L);
  ray_chan_t*  self = lua_newuserdata(L, sizeof(ray_chan_t));

  luaL_getmetatable(L, RAY_CHAN_T);
  lua_setmetatable(L, -2);

  rayL_object_init(curr, (ray_object_t*)self);

  self->data = rayL_thread_self(L)->ctx;
  self->put  = zmq_socket(self->data, ZMQ_PAIR);
  self->get  = zmq_socket(self->data, ZMQ_PAIR);

  lua_pushfstring(L, "inproc://%p", self);
  const char* addr = lua_tostring(L, -1);
  lua_pop(L, 1);

  zmq_bind(self->put, "inproc://%s");
  zmq_bind(self->get, "inproc://%s");

  return 1;
}

static int ray_chan_put(lua_State* L) {
  ray_object_t* self = luaL_checkudata(L, 1, RAY_CHAN_T);

  return 0;
}
luaL_Reg ray_chan_funcs[] = {
  {"create",      ray_new_chan},
  {NULL,          NULL}
};

luaL_Reg ray_chan_meths[] = {
  {"put",         ray_chan_put},
  {"get",         ray_chan_get},
  {NULL,          NULL}
};