#ifndef _RAY_STREAM_H_
#define _RAY_STREAM_H_

uv_buf_t rayL_alloc_cb (uv_handle_t* handle, size_t size);
void rayL_connect_cb   (uv_connect_t* conn, int status);

int  rayL_stream_stop (ray_state_t* self);
int  rayL_stream_start(ray_state_t* self);
void rayL_stream_free (ray_state_t* self);
void rayL_stream_close(ray_state_t* self);

int rayM_stream_rouse(ray_state_t* self, ray_state_t* from);
int rayM_stream_await(ray_state_t* self, ray_state_t* that);
int rayM_stream_close(ray_state_t* self);

luaL_Reg ray_stream_meths[16];

#endif /* _RAY_STREAM_H_ */
