#ifndef _RAY_STREAM_H_
#define _RAY_STREAM_H_

uv_buf_t ray_alloc_cb  (uv_handle_t* handle, size_t size);
void     ray_connect_cb(uv_connect_t* req, int status);

int  ray_stream_stop (ray_actor_t* self);
int  ray_stream_start(ray_actor_t* self);
void ray_stream_free (ray_actor_t* self);
void ray_stream_close(ray_actor_t* self);

int rayM_stream_rouse(ray_actor_t* self, ray_actor_t* from);
int rayM_stream_await(ray_actor_t* self, ray_actor_t* that);
int rayM_stream_close(ray_actor_t* self);

luaL_Reg ray_stream_meths[16];

#endif /* _RAY_STREAM_H_ */
