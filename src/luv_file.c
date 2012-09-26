#include <fcntl.h>
#include <sys/stat.h>

#include "luv_core.h"
#include "luv_fs.h"
#include "luv_fiber.h"

static int string_to_flags(lua_State* L, const char* str) {
  if (strcmp(str, "r") == 0)
    return O_RDONLY;
  if (strcmp(str, "r+") == 0)
    return O_RDWR;
  if (strcmp(str, "w") == 0)
    return O_CREAT | O_TRUNC | O_WRONLY;
  if (strcmp(str, "w+") == 0)
    return O_CREAT | O_TRUNC | O_RDWR;
  if (strcmp(str, "a") == 0)
    return O_APPEND | O_CREAT | O_WRONLY;
  if (strcmp(str, "a+") == 0)
    return O_APPEND | O_CREAT | O_RDWR;
  return luaL_error(L, "Unknown file open flag: '%s'", str);
}

static void luv_file_cb(uv_fs_t* req) {
  luv_Fiber* fiber = req->data;
  if (fiber) {
    lua_State* state = fiber->state;
    luv__fiber_resume(fiber);
    luv_File* file = luaL_checkudata(state, 1, LUV_FILE_T);

    if (req->result == -1) {
      lua_pop(state, 1); /* file object */
      lua_pushnil(state);
      lua_pushinteger(state, (uv_err_code)req->errorno);
    }
    else {
      switch (req->fs_type) {
      case UV_FS_CLOSE:
        lua_pop(state, 1); /* file object */
        lua_pushinteger(state, req->result);
        break;
      case UV_FS_OPEN:
        file->fh = req->result;
        break;
      case UV_FS_READ:
        lua_pop(state, 1); /* file object */
        lua_pushinteger(state, req->result);
        lua_pushlstring(state, file->buf, req->result);
        free(file->buf);
        file->buf = NULL;
        break;
      case UV_FS_WRITE:
        lua_pop(state, 1); /* file object */
        lua_pushinteger(state, req->result);
        break;
      default:
        luaL_error(state, "Unhandled fs_type");
      }
    }
    uv_fs_req_cleanup(req);
    free(req);
  }
}

static int luv_File_new(lua_State* L) {
  luv_Sched* sched = lua_touserdata(L, lua_upvalueindex(1));
  luv_File*  file  = lua_newuserdata(L, sizeof(luv_File));

  lua_pushvalue(L, 1);
  lua_setmetatable(L, -2);

  file->sched = sched;
  file->fh    = 0;
  file->buf   = NULL;

  return 1;
}

static int luv_file_open(lua_State* L) {
  luv_File*  file  = luaL_checkudata(L, 1, LUV_FILE_T);
  luv_Sched* sched = file->sched;
  luv_Fiber* fiber = sched->curr;
  uv_loop_t* loop  = sched->loop;

  const char* path = luaL_checkstring(L, 2);
  int flags  = string_to_flags(L, luaL_checkstring(L, 3));
  int mode   = strtoul(luaL_checkstring(L, 4), NULL, 8);
  int rv = 0;

  uv_fs_t* req = malloc(sizeof(uv_fs_t));
  req->data    = sched->curr;

  lua_settop(L, 1);

  rv = uv_fs_open(loop, req, path, flags, mode, luv_file_cb);
  if (rv) {
    uv_err_t err = uv_last_error(loop);
    lua_pushstring(L, uv_strerror(err));
    return lua_error(L);
  }

  if (fiber) {
    luv__fiber_suspend(fiber);
    return lua_yield(fiber->state, 1);
  }
  else {
    luv__sched_loop(L, sched);
    return 1;
  }
}

static int luv_fs_open(lua_State* L) {
  luv_Sched* sched = lua_touserdata(L, lua_upvalueindex(1));
  uv_file*   file  = lua_newuserdata(sizeof(uv_file));

  luv_Fiber* curr = sched->curr;
  uv_loop_t* loop = sched->loop;
  luv_fs_req* req = malloc(sizeof(luv_fs_req));

  const char* path = luaL_checkstring(L, 2);
  int flags  = string_to_flags(L, luaL_checkstring(L, 3));
  int mode   = strtoul(luaL_checkstring(L, 4), NULL, 8);
  int rv = 0;

  req->rouse = curr;
  lua_settop(L, 1);

  rv = uv_fs_open(loop, &req->uv_req, path, flags, mode, fs_cb);
  if (rv) {
    uv_err_t err = uv_last_error(loop);
    lua_pushstring(L, uv_strerror(err));
    free(req);
    return lua_error(L);
  }

  if (curr) {
    luv__fiber_suspend(curr);
    return lua_yield(curr->state, 1);
  }
  else {
    luv__sched_loop(L, sched);
    return 1;
  }
}

static int luv_fs_read(lua_State* L) {
  luv_Sched* sched = lua_touserdata(L, lua_upvalueindex(1));
  uv_file*   file  = lua_touserdata(L, 1);

  luv_Fiber* curr  = sched->curr;
  uv_loop_t* loop  = sched->loop;

  size_t  len = luaL_optint(L, 2, LUV_BUF_SIZE);
  int64_t ofs = luaL_optint(L, 3, -1);

  uv_fs_t* req = malloc(sizeof(uv_fs_t));
  req->data    = curr;

  if (uv_fs_read(loop, req, *file, buf, len, ofs, fs_cb)) {
    uv_err_t err = uv_last_error(loop);
    lua_pushstring(L, uv_strerror(err));
    return lua_error(L);
  }

  lua_settop(L, 1);
  if (curr) {
    luv__fiber_suspend(curr);
    return lua_yield(curr->state, 2);
  }
  else {
    luv__sched_loop(L, sched);
    return 2;
  }
}

static int luv_file_read(lua_State *L) {
  luv_File*  file  = luaL_checkudata(L, 1, LUV_FILE_T);
  luv_Sched* sched = file->sched;
  luv_Fiber* fiber = sched->curr;
  uv_loop_t* loop  = sched->loop;

  int rv;
  size_t len  = luaL_optint(L, 2, LUV_BUF_SIZE);
  int64_t ofs = luaL_optint(L, 3, -1);

  file->buf    = malloc(len);
  uv_fs_t* req = malloc(sizeof(uv_fs_t));
  req->data    = fiber;

  rv = uv_fs_read(loop, req, file->fh, file->buf, len, ofs, luv_file_cb);
  if (rv) {
    uv_err_t err = uv_last_error(loop);
    lua_pushstring(L, uv_strerror(err));
    return lua_error(L);
  }

  lua_settop(L, 1);
  if (fiber) {
    luv__fiber_suspend(fiber);
    return lua_yield(fiber->state, 2);
  }
  else {
    luv__sched_loop(L, sched);
    return 2;
  }
}

static int luv_file_write(lua_State *L) {
  luv_File*  file  = luaL_checkudata(L, 1, LUV_FILE_T);
  luv_Sched* sched = file->sched;
  luv_Fiber* fiber = sched->curr;
  uv_loop_t* loop  = sched->loop;

  int rv;
  size_t len;

  const char* buf = luaL_checklstring(L, 2, &len);
  uint64_t    ofs = luaL_optint(L, 3, 0);
  uv_fs_t*    req = malloc(sizeof(uv_fs_t));

  req->data = fiber;

  rv = uv_fs_write(loop, req, file->fh, buf, len, ofs, luv_file_cb);
  if (rv) {
    uv_err_t err = uv_last_error(loop);
    lua_pushstring(L, uv_strerror(err));
    return lua_error(L);
  }

  lua_settop(L, 1);
  if (fiber) {
    luv__fiber_suspend(fiber);
    return lua_yield(fiber->state, 1);
  }
  else {
    luv__sched_loop(L, sched);
    return 1;
  }
}

static int luv_file_close(lua_State *L) {
  luv_File*  file  = luaL_checkudata(L, 1, LUV_FILE_T);
  luv_Sched* sched = file->sched;
  luv_Fiber* fiber = sched->curr;
  uv_loop_t* loop  = sched->loop;

  int rv;
  uv_fs_t* req = malloc(sizeof(uv_fs_t));

  req->data = fiber;

  rv = uv_fs_close(loop, req, file->fh, luv_file_cb);
  if (rv) {
    uv_err_t err = uv_last_error(loop);
    lua_pushstring(L, uv_strerror(err));
    return lua_error(L);
  }

  lua_settop(L, 1);
  if (fiber) {
    luv__fiber_suspend(fiber);
    return lua_yield(fiber->state, 1);
  }
  else {
    luv__sched_loop(L, sched);
    return 1;
  }
}

static int luv_file_free(lua_State *L) {
  luv_File *file = lua_touserdata(L, 1);
  if (file->buf) free(file->buf);
  return 0;
}

static int luv_file_tostring(lua_State *L) {
  luv_File *file = luaL_checkudata(L, 1, LUV_FILE_T);
  lua_pushfstring(L, "userdata<%s>: %p", LUV_FILE_T, file);
  return 1;
}

static luaL_Reg luv_fs_funcs[] = {
  {"open",      luv_fs_open},
  {NULL,        NULL}
};

static luaL_Reg luv_File_meths[] = {
  {"read",      luv_file_read},
  {"write",     luv_file_write},
  {"close",     luv_file_close},
  {"__gc",      luv_file_free},
  {"__tostring",luv_file_tostring},
  {NULL,        NULL}
};

LUALIB_API int luaopenL_luv_fs(lua_State *L) {
  luaL_newmetatable(L, LUV_FILE_T);
  luaL_register(L, NULL, luv_File_meths);

  lua_pushvalue(L, -1);
  lua_setfield(L, -2, "__index");

  lua_getfield(L, LUA_REGISTRYINDEX, LUV_SCHED_O);
  lua_pushcclosure(L, luv_File_new, 1);
  lua_setfield(L, -2, "new");
  lua_setfield(L, -2, "File");

  lua_newtable(L);
  lua_getfield(L, LUA_REGISTRYINDEX, LUV_SCHED_O);

  lua_pushvalue(L, -1);
  lua_pushcclosure(L, luv_fs_open, 1);
  lua_setfield(L, -2, "open");

  lua_setfield(L, -2, "fs");

  return 1;
}

