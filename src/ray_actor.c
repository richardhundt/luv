#include "ray.h"

ray_actor_t* rayL_actor_new(ray_state_t* state, char* addr) {
  ray_actor_t* self = lua_newuserdata(state->L, sizeof(ray_actor_t));
  luaL_getmetatable(state->L, RAY_ACTOR_T);
  lua_setmetatable(state->L, -2);

  self->mbox = zmq_socket(state->ctx, ZMQ_REP);
  zmq_bind(self->mailbox, addr);
  zmq_connect(state->monitor, addr);
  return self;
}

int rayL_codec_decode(ray_state_t* state, char* data, size_t len) {
  lua_settop(state->L, 0);
  lua_pushlstring(state->L, data, len);
  return 0;
}

int rayL_actor_send(ray_actor_t* self, char* data) {

}

int rayL_actor_recv(ray_actor_t* self) {
  zmq_msg_t msg;
  zmq_msg_init(&msg);
  int rv = zmq_msg_recv(&msg, self->mailbox, ZMQ_DONTWAIT);
  if (rv >= 0) {
    void* data = zmq_msg_data(&msg);
    size_t len = zmq_msg_size(&msg);
    rayL_codec_decode(self->state, data, len);
  }
  else {
    int e = zmq_errno();
    if (e == EAGAIN || e == EWOULDBLOCK) {
      rayL_state_wait(rayL_state_self(self->state), &self->rouse);
    }
  }
  zmq_msg_close(&msg);
  return rv;
}

int rayL_actor_close(ray_actor_t* self) {
  return zmq_close(self->mailbox);
}
