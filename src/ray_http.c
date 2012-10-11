#include "ray_common.h"

static void _http_read_cb(ray_object_t* self, void* arg) {
  
}

static int ray_http_new(lua_State* L) {
  ray_object_t* self   = rayL_new_object(L, RAY_HTTP_T);
  ray_stream_t* stream = rayL_checkudata(L, RAY_STREAM_T);
  rayL_stream_on_read (stream, _http_read_cb);
  rayL_stream_on_error(stream, _http_error_cb);
  rayL_stream_on_close(stream, _http_close_cb);
  rayL_stream_start(stream);
}

