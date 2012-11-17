#ifndef _RAY_STREAM_H_
#define _RAY_STREAM_H_

#define RAY_STREAM_T "ray.stream.Stream"

#define RAY_STREAM_READING 1

uv_buf_t ray_alloc_cb  (uv_handle_t* handle, size_t size);
void     ray_connect_cb(uv_connect_t* req, int status);

ray_state_t* ray_stream_new(lua_State* L, const char* meta, ray_vtable_t* vtab);

int ray_stream_alloc(ray_state_t* self);
int ray_stream_close(ray_state_t* self);
int ray_stream_yield(ray_state_t* self);
int ray_stream_react(ray_state_t* self);

ray_vtable_t ray_stream_v;

luaL_Reg ray_stream_meths[16];

#endif /* _RAY_STREAM_H_ */
