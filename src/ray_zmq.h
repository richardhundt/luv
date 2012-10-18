#ifndef _RAY_ZMQ_H_
#define _RAY_ZMQ_H_

/* ØMQ flags */
#define RAY_ZMQ_SCLOSED (1 << 0)
#define RAY_ZMQ_XDUPCTX (1 << 1)
#define RAY_ZMQ_WSEND   (1 << 2)
#define RAY_ZMQ_WRECV   (1 << 3)

int rayL_zmq_ctx_decoder(lua_State* L);

int rayL_zmq_socket_readable(void* socket);
int rayL_zmq_socket_writable(void* socket);
int rayL_zmq_socket_send(ray_object_t* self, ray_actor_t* state);
int rayL_zmq_socket_recv(ray_object_t* self, ray_actor_t* state);

#endif /* _RAY_ZMQ_H_ */
