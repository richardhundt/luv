#include "luv.h"

static int luv_new_pipe(lua_State* L) {
  luv_object_t* self = (luv_object_t*)lua_newuserdata(L, sizeof(luv_object_t));
  luaL_getmetatable(L, LUV_PIPE_T);
  lua_setmetatable(L, -2);
  int ipc = 0;
  if (!lua_isnoneornil(L, 2)) {
    luaL_checktype(L, 2, LUA_TBOOLEAN);
    ipc = lua_toboolean(L, 2);
  }
  uv_pipe_init(luvL_event_loop(L), &self->h.pipe, ipc);
  return 1;
}

static int luv_pipe_open(lua_State* L) {
  luv_object_t* self = (luv_object_t*)luaL_checkudata(L, 1, LUV_PIPE_T);
  uv_file fh;
  if (lua_isuserdata(L, 2)) {
    luv_object_t* file = (luv_object_t*)luaL_checkudata(L, 2, LUV_FILE_T);
    fh = file->h.file;
  }
  else {
    fh = luaL_checkint(L, 2);
  }
  uv_pipe_open(&self->h.pipe, fh);
  return 0;
}

static int luv_pipe_bind(lua_State* L) {
  luv_object_t* self = (luv_object_t*)luaL_checkudata(L, 1, LUV_PIPE_T);
  const char*   path = luaL_checkstring(L, 2);

  if (uv_pipe_bind(&self->h.pipe, path)) {
    uv_err_t err = uv_last_error(self->h.pipe.loop);
    return luaL_error(L, uv_strerror(err));
  }

  return 0;
}

static int luv_pipe_connect(lua_State* L) {
  luv_object_t* self = (luv_object_t*)luaL_checkudata(L, 1, LUV_PIPE_T);
  const char*   path = luaL_checkstring(L, 2);
  luv_state_t*  curr = luvL_state_self(L);

  uv_pipe_connect(&curr->req.connect, &self->h.pipe, path, luvL_connect_cb);

  return 0;
}

static int luv_pipe_tostring(lua_State *L) {
  luv_object_t *self = (luv_object_t*)luaL_checkudata(L, 1, LUV_PIPE_T);
  lua_pushfstring(L, "userdata<%s>: %p", LUV_PIPE_T, self);
  return 1;
}

luaL_Reg luv_pipe_funcs[] = {
  {"create",      luv_new_pipe},
  {NULL,          NULL}
};

luaL_Reg luv_pipe_meths[] = {
  {"open",        luv_pipe_open},
  {"bind",        luv_pipe_bind},
  {"connect",     luv_pipe_connect},
  {"__tostring",  luv_pipe_tostring},
  {NULL,          NULL}
};

