#include "ray_lib.h"
#include "ray_state.h"

static int ray_pipe_new(lua_State* L) {
  ray_object_t* self = rayL_object_new(L, RAY_PIPE_T);
  int ipc = 0;
  if (!lua_isnoneornil(L, 2)) {
    luaL_checktype(L, 2, LUA_TBOOLEAN);
    ipc = lua_toboolean(L, 2);
  }
  uv_pipe_init(rayL_event_loop(L), &self->h.pipe, ipc);
  return 1;
}

static int ray_pipe_open(lua_State* L) {
  ray_object_t* self = (ray_object_t*)luaL_checkudata(L, 1, RAY_PIPE_T);
  uv_file fh;
  if (lua_isuserdata(L, 2)) {
    ray_object_t* file = (ray_object_t*)luaL_checkudata(L, 2, RAY_FILE_T);
    fh = file->h.file;
  }
  else {
    fh = luaL_checkint(L, 2);
  }
  uv_pipe_open(&self->h.pipe, fh);
  return 0;
}

static int ray_pipe_bind(lua_State* L) {
  ray_object_t* self = (ray_object_t*)luaL_checkudata(L, 1, RAY_PIPE_T);
  const char*   path = luaL_checkstring(L, 2);

  if (uv_pipe_bind(&self->h.pipe, path)) {
    uv_err_t err = uv_last_error(self->h.pipe.loop);
    return luaL_error(L, uv_strerror(err));
  }

  return 0;
}

static int ray_pipe_connect(lua_State* L) {
  ray_object_t* self = (ray_object_t*)luaL_checkudata(L, 1, RAY_PIPE_T);
  const char*   path = luaL_checkstring(L, 2);
  ray_state_t*  curr = rayL_state_self(L);

  uv_pipe_connect(&curr->req.connect, &self->h.pipe, path, rayL_connect_cb);

  return 0;
}

static int ray_pipe_tostring(lua_State *L) {
  ray_object_t *self = (ray_object_t*)luaL_checkudata(L, 1, RAY_PIPE_T);
  lua_pushfstring(L, "userdata<%s>: %p", RAY_PIPE_T, self);
  return 1;
}

luaL_Reg ray_pipe_funcs[] = {
  {"create",      ray_pipe_new},
  {NULL,          NULL}
};

luaL_Reg ray_pipe_meths[] = {
  {"open",        ray_pipe_open},
  {"bind",        ray_pipe_bind},
  {"connect",     ray_pipe_connect},
  {"__tostring",  ray_pipe_tostring},
  {NULL,          NULL}
};

