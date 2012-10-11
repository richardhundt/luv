#ifndef _RAY_STREAM_H_
#define _RAY_STREAM_H_

uv_buf_t ray__alloc_cb (uv_handle_t* handle, size_t size);
void ray__connect_cb   (uv_connect_t* conn, int status);

int  rayL_stream_stop (ray_object_t* self);
void rayL_stream_free (ray_object_t* self);
void rayL_stream_close(ray_object_t* self);

#endif /* _RAY_STREAM_H_ */
