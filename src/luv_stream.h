#ifndef _LUV_STREAM_H_
#define _LUV_STREAM_H_

uv_buf_t luv__alloc_cb (uv_handle_t* handle, size_t size);
void luv__connect_cb   (uv_connect_t* conn, int status);

int  luvL_stream_stop (luv_object_t* self);
void luvL_stream_free (luv_object_t* self);
void luvL_stream_close(luv_object_t* self);

#endif /* _LUV_STREAM_H_ */
