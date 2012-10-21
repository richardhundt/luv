#ifndef _RAY_STREAM_H_
#define _RAY_STREAM_H_

uv_buf_t ray_alloc_cb  (uv_handle_t* handle, size_t size);
void     ray_connect_cb(uv_connect_t* req, int status);

int ray_stream_start(ray_actor_t* self);
int ray_stream_stop (ray_actor_t* self);
int ray_stream_read (ray_actor_t* self, ray_actor_t* from, int len);
int ray_stream_write(ray_actor_t* self, ray_actor_t* from, uv_buf_t* b, int n);

int ray_stream_listen(ray_actor_t* self, ray_actor_t* from, int backlog);
int ray_stream_accept(ray_actor_t* self, ray_actor_t* from, ray_actor_t* conn);

int ray_stream_shutdown(ray_actor_t* self, ray_actor_t* from);
int ray_stream_readable(ray_actor_t* self, ray_actor_t* from);
int ray_stream_writable(ray_actor_t* self, ray_actor_t* from);

int ray_stream_close(ray_actor_t* self, ray_actor_t* from);
int ray_stream_free (ray_actor_t* self);

int rayM_stream_send(ray_actor_t* self, ray_actor_t* from);
int rayM_stream_recv(ray_actor_t* self, ray_actor_t* that);
int rayM_stream_close(ray_actor_t* self);

luaL_Reg ray_stream_meths[16];

#endif /* _RAY_STREAM_H_ */
