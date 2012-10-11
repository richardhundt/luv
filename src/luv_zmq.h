#ifndef _LUV_ZMQ_H_
#define _LUV_ZMQ_H_

/* ØMQ flags */
#define LUV_ZMQ_SCLOSED (1 << 0)
#define LUV_ZMQ_XDUPCTX (1 << 1)
#define LUV_ZMQ_WSEND   (1 << 2)
#define LUV_ZMQ_WRECV   (1 << 3)

int luvL_zmq_ctx_decoder(lua_State* L);

int luvL_zmq_socket_readable(void* socket);
int luvL_zmq_socket_writable(void* socket);
int luvL_zmq_socket_send(luv_object_t* self, luv_state_t* state);
int luvL_zmq_socket_recv(luv_object_t* self, luv_state_t* state);

#endif /* _LUV_ZMQ_H_ */
