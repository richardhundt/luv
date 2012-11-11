#ifndef _RAY_STREAM_H_
#define _RAY_STREAM_H_

uv_buf_t ray_alloc_cb  (uv_handle_t* handle, size_t size);
void     ray_connect_cb(uv_connect_t* req, int status);

int ray_stream_start(ray_state_t* self);
int ray_stream_stop (ray_state_t* self);
int ray_stream_read (ray_state_t* self, ray_state_t* from, int len);
int ray_stream_write(ray_state_t* self, ray_state_t* from, uv_buf_t* b, int n);

int ray_stream_listen(ray_state_t* self, ray_state_t* from, int backlog);
int ray_stream_accept(ray_state_t* self, ray_state_t* from, ray_state_t* conn);

int ray_stream_shutdown(ray_state_t* self, ray_state_t* from);
int ray_stream_readable(ray_state_t* self, ray_state_t* from);
int ray_stream_writable(ray_state_t* self, ray_state_t* from);

int ray_stream_close(ray_state_t* self, ray_state_t* from);
int ray_stream_free (ray_state_t* self);

int rayM_stream_react(ray_state_t* self, ray_state_t* from, ray_msg_t msg);
int rayM_stream_yield(ray_state_t* self);
int rayM_stream_close(ray_state_t* self);

luaL_Reg ray_stream_meths[16];

#endif /* _RAY_STREAM_H_ */
