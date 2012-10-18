#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#ifndef _WIN32
#include <unistd.h>
#endif

#include "ray_common.h"

/* lifted from rayit */
static int ray_string_to_flags(lua_State* L, const char* str) {
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

/* lifted from rayit */
static void ray_push_stats_table(lua_State* L, struct stat* s) {
  lua_newtable(L);
  lua_pushinteger(L, s->st_dev);
  lua_setfield(L, -2, "dev");
  lua_pushinteger(L, s->st_ino);
  lua_setfield(L, -2, "ino");
  lua_pushinteger(L, s->st_mode);
  lua_setfield(L, -2, "mode");
  lua_pushinteger(L, s->st_nlink);
  lua_setfield(L, -2, "nlink");
  lua_pushinteger(L, s->st_uid);
  lua_setfield(L, -2, "uid");
  lua_pushinteger(L, s->st_gid);
  lua_setfield(L, -2, "gid");
  lua_pushinteger(L, s->st_rdev);
  lua_setfield(L, -2, "rdev");
  lua_pushinteger(L, s->st_size);
  lua_setfield(L, -2, "size");
#ifdef __POSIX__
  lua_pushinteger(L, s->st_blksize);
  lua_setfield(L, -2, "blksize");
  lua_pushinteger(L, s->st_blocks);
  lua_setfield(L, -2, "blocks");
#endif
  lua_pushinteger(L, s->st_atime);
  lua_setfield(L, -2, "atime");
  lua_pushinteger(L, s->st_mtime);
  lua_setfield(L, -2, "mtime");
  lua_pushinteger(L, s->st_ctime);
  lua_setfield(L, -2, "ctime");
#ifndef _WIN32
  lua_pushboolean(L, S_ISREG(s->st_mode));
  lua_setfield(L, -2, "is_file");
  lua_pushboolean(L, S_ISDIR(s->st_mode));
  lua_setfield(L, -2, "is_directory");
  lua_pushboolean(L, S_ISCHR(s->st_mode));
  lua_setfield(L, -2, "is_character_device");
  lua_pushboolean(L, S_ISBLK(s->st_mode));
  lua_setfield(L, -2, "is_block_device");
  lua_pushboolean(L, S_ISFIFO(s->st_mode));
  lua_setfield(L, -2, "is_fifo");
  lua_pushboolean(L, S_ISLNK(s->st_mode));
  lua_setfield(L, -2, "is_symbolic_link");
  lua_pushboolean(L, S_ISSOCK(s->st_mode));
  lua_setfield(L, -2, "is_socket");
#endif
}

/* the rest of this file is mostly stolen from rayit - key difference is that
** we don't run Lua callbacks, but instead suspend and resume Lua threads */

static void ray_fs_result(lua_State* L, uv_fs_t* req) {
  TRACE("enter fs result...\n");
  if (req->result == -1) {
    lua_pushnil(L);
    lua_pushinteger(L, (uv_err_code)req->errorno);
  }
  else {
    switch (req->fs_type) {
      case UV_FS_RENAME:
      case UV_FS_UNLINK:
      case UV_FS_RMDIR:
      case UV_FS_MKDIR:
      case UV_FS_FSYNC:
      case UV_FS_FTRUNCATE:
      case UV_FS_FDATASYNC:
      case UV_FS_LINK:
      case UV_FS_SYMLINK:
      case UV_FS_CHMOD:
      case UV_FS_FCHMOD:
      case UV_FS_CHOWN:
      case UV_FS_FCHOWN:
      case UV_FS_UTIME:
      case UV_FS_FUTIME:
      case UV_FS_CLOSE:
        lua_pushinteger(L, req->result);
        break;

      case UV_FS_OPEN:
        {
          ray_object_t* self = (ray_object_t*)luaL_checkudata(L, -1, RAY_FILE_T);
          self->h.file = req->result;
        }
        break;

      case UV_FS_READ:
        lua_pushinteger(L, req->result);
        lua_pushlstring(L, (const char*)req->data, req->result);
        free(req->data);
        req->data = NULL;
        break;

      case UV_FS_WRITE:
        lua_pushinteger(L, req->result);
        break;

      case UV_FS_READLINK:
        lua_pushstring(L, (char*)req->ptr);
        break;

      case UV_FS_READDIR:
        {
          int i;
          char* namep = (char*)req->ptr;
          int   count = req->result;
          lua_newtable(L);
          for (i = 1; i <= count; i++) {
            lua_pushstring(L, namep);
            lua_rawseti(L, -2, i);
            namep += strlen(namep) + 1; /* +1 for '\0' */
          }
        }
        break;

      case UV_FS_STAT:
      case UV_FS_LSTAT:
      case UV_FS_FSTAT:
        ray_push_stats_table(L, (struct stat*)req->ptr);
        break;

      default:
        luaL_error(L, "Unhandled fs_type");
    }
  }
  uv_fs_req_cleanup(req);
}

static void ray_fs_cb(uv_fs_t* req) {
  ray_actor_t* state = container_of(req, ray_actor_t, req);
  ray_fs_result(state->L, req);
  rayL_state_ready(state);
}

#define RAY_FS_CALL(L, func, misc, ...) do { \
    ray_actor_t* curr = rayL_state_self(L); \
    uv_loop_t*   loop = rayL_event_loop(L); \
    uv_fs_t*     req; \
    uv_fs_cb     cb; \
    req = &curr->req.fs; \
    if (curr->type == RAY_TTHREAD) { \
      /* synchronous in main */ \
      cb = NULL; \
    } \
    else { \
      cb = ray_fs_cb; \
    } \
    req->data = misc; \
    \
    if (uv_fs_##func(loop, req, __VA_ARGS__, cb) < 0) { \
      uv_err_t err = uv_last_error(loop); \
      lua_settop(L, 0); \
      lua_pushboolean(L, 0); \
      lua_pushstring(L, uv_strerror(err)); \
    } \
    if (curr->type == RAY_TTHREAD) { \
      ray_fs_result(L, req); \
      return lua_gettop(L); \
    } \
    else { \
      TRACE("suspending...\n"); \
      return rayL_state_suspend(curr); \
    } \
  } while(0)

static int ray_fs_open(lua_State* L) {
  ray_actor_t*  curr = rayL_state_self(L);
  const char*   path = luaL_checkstring(L, 1);
  ray_object_t* self;

  int flags = ray_string_to_flags(L, luaL_checkstring(L, 2));
  int mode  = strtoul(luaL_checkstring(L, 3), NULL, 8);

  lua_settop(L, 0);

  self = (ray_object_t*)lua_newuserdata(L, sizeof(ray_object_t));
  luaL_getmetatable(L, RAY_FILE_T);
  lua_setmetatable(L, -2);
  rayL_object_init(curr, self);

  self->h.file = -1; /* invalid file handle */
  RAY_FS_CALL(L, open, NULL, path, flags, mode);
}

static int ray_fs_unlink(lua_State* L) {
  const char* path = luaL_checkstring(L, 1);
  lua_settop(L, 0);
  RAY_FS_CALL(L, unlink, NULL, path);
}

static int ray_fs_mkdir(lua_State* L) {
  const char*  path = luaL_checkstring(L, 1);
  int mode = strtoul(luaL_checkstring(L, 2), NULL, 8);
  lua_settop(L, 0);
  RAY_FS_CALL(L, mkdir, NULL, path, mode);
}

static int ray_fs_rmdir(lua_State* L) {
  const char*  path = luaL_checkstring(L, 1);
  lua_settop(L, 0);
  RAY_FS_CALL(L, rmdir, NULL, path);
}

static int ray_fs_readdir(lua_State* L) {
  const char* path = luaL_checkstring(L, 1);
  lua_settop(L, 0);
  RAY_FS_CALL(L, readdir, NULL, path, 0);
}

static int ray_fs_stat(lua_State* L) {
  const char* path = luaL_checkstring(L, 1);
  lua_settop(L, 0);
  RAY_FS_CALL(L, stat, NULL, path);
}

static int ray_fs_rename(lua_State* L) {
  const char* old_path = luaL_checkstring(L, 1);
  const char* new_path = luaL_checkstring(L, 2);
  lua_settop(L, 0);
  RAY_FS_CALL(L, rename, NULL, old_path, new_path);
}

static int ray_fs_sendfile(lua_State* L) {
  ray_object_t* o_file = (ray_object_t*)luaL_checkudata(L, 1, RAY_FILE_T);
  ray_object_t* i_file = (ray_object_t*)luaL_checkudata(L, 2, RAY_FILE_T);
  off_t  ofs = luaL_checkint(L, 3);
  size_t len = luaL_checkint(L, 4);
  lua_settop(L, 2);
  RAY_FS_CALL(L, sendfile, NULL, o_file->h.file, i_file->h.file, ofs, len);
}

static int ray_fs_chmod(lua_State* L) {
  const char* path = luaL_checkstring(L, 1);
  int mode = strtoul(luaL_checkstring(L, 2), NULL, 8);
  lua_settop(L, 0);
  RAY_FS_CALL(L, chmod, NULL, path, mode);
}

static int ray_fs_utime(lua_State* L) {
  const char* path = luaL_checkstring(L, 1);
  double atime = luaL_checknumber(L, 2);
  double mtime = luaL_checknumber(L, 3);
  lua_settop(L, 0);
  RAY_FS_CALL(L, utime, NULL, path, atime, mtime);
}

static int ray_fs_lstat(lua_State* L) {
  const char* path = luaL_checkstring(L, 1);
  lua_settop(L, 0);
  RAY_FS_CALL(L, lstat, NULL, path);
}

static int ray_fs_link(lua_State* L) {
  const char* src_path = luaL_checkstring(L, 1);
  const char* dst_path = luaL_checkstring(L, 2);
  lua_settop(L, 0);
  RAY_FS_CALL(L, link, NULL, src_path, dst_path);
}

static int ray_fs_symlink(lua_State* L) {
  const char* src_path = luaL_checkstring(L, 1);
  const char* dst_path = luaL_checkstring(L, 2);
  int flags = ray_string_to_flags(L, luaL_checkstring(L, 3));
  lua_settop(L, 0);
  RAY_FS_CALL(L, symlink, NULL, src_path, dst_path, flags);
}

static int ray_fs_readlink(lua_State* L) {
  const char* path = luaL_checkstring(L, 1);
  lua_settop(L, 0);
  RAY_FS_CALL(L, readlink, NULL, path);
}

static int ray_fs_chown(lua_State* L) {
  const char* path = luaL_checkstring(L, 1);
  int uid = luaL_checkint(L, 2);
  int gid = luaL_checkint(L, 3);
  lua_settop(L, 0);
  RAY_FS_CALL(L, chown, NULL, path, uid, gid);
}

static int ray_fs_cwd(lua_State* L) {
  char buffer[RAY_MAX_PATH];
  uv_err_t err = uv_cwd(buffer, RAY_MAX_PATH);
  if (err.code) {
    return luaL_error(L, uv_strerror(err));
  }
  lua_pushstring(L, buffer);
  return 1;
}

static int ray_fs_chdir(lua_State* L) {
  const char* dir = luaL_checkstring(L, 1);
  uv_err_t err = uv_chdir(dir);
  if (err.code) {
    return luaL_error(L, uv_strerror(err));
  }
  return 0;
}

static int ray_fs_exepath(lua_State* L) {
  char buffer[RAY_MAX_PATH];
  size_t len = RAY_MAX_PATH;
  uv_exepath(buffer, &len);
  lua_pushlstring(L, buffer, len);
  return 1;
}


/* file instance methods */
static int ray_file_stat(lua_State* L) {
  ray_object_t* self = (ray_object_t*)luaL_checkudata(L, 1, RAY_FILE_T);
  lua_settop(L, 0);
  RAY_FS_CALL(L, fstat, NULL, self->h.file);
}

static int ray_file_sync(lua_State* L) {
  ray_object_t* self = (ray_object_t*)luaL_checkudata(L, 1, RAY_FILE_T);
  lua_settop(L, 0);
  RAY_FS_CALL(L, fsync, NULL, self->h.file);
}

static int ray_file_datasync(lua_State* L) {
  ray_object_t* self = (ray_object_t*)luaL_checkudata(L, 1, RAY_FILE_T);
  lua_settop(L, 0);
  RAY_FS_CALL(L, fdatasync, NULL, self->h.file);
}

static int ray_file_truncate(lua_State* L) {
  ray_object_t* self = (ray_object_t*)luaL_checkudata(L, 1, RAY_FILE_T);
  off_t ofs = luaL_checkint(L, 2);
  lua_settop(L, 0);
  RAY_FS_CALL(L, ftruncate, NULL, self->h.file, ofs);
}

static int ray_file_utime(lua_State* L) {
  ray_object_t* self = (ray_object_t*)luaL_checkudata(L, 1, RAY_FILE_T);
  double atime = luaL_checknumber(L, 2);
  double mtime = luaL_checknumber(L, 3);
  lua_settop(L, 0);
  RAY_FS_CALL(L, futime, NULL, self->h.file, atime, mtime);
}

static int ray_file_chmod(lua_State* L) {
  ray_object_t* self = (ray_object_t*)luaL_checkudata(L, 1, RAY_FILE_T);
  int mode = strtoul(luaL_checkstring(L, 2), NULL, 8);
  lua_settop(L, 0);
  RAY_FS_CALL(L, fchmod, NULL, self->h.file, mode);
}

static int ray_file_chown(lua_State* L) {
  ray_object_t* self = (ray_object_t*)luaL_checkudata(L, 1, RAY_FILE_T);
  int uid = luaL_checkint(L, 2);
  int gid = luaL_checkint(L, 3);
  lua_settop(L, 0);
  RAY_FS_CALL(L, fchown, NULL, self->h.file, uid, gid);
}

static int ray_file_read(lua_State *L) {
  ray_object_t* self = (ray_object_t*)luaL_checkudata(L, 1, RAY_FILE_T);

  size_t  len = luaL_optint(L, 2, RAY_BUF_SIZE);
  int64_t ofs = luaL_optint(L, 3, -1);
  void*   buf = malloc(len); /* free from ctx->req.fs_req.data in cb */

  lua_settop(L, 0);
  RAY_FS_CALL(L, read, buf, self->h.file, buf, len, ofs);
}

static int ray_file_write(lua_State *L) {
  ray_object_t* self = (ray_object_t*)luaL_checkudata(L, 1, RAY_FILE_T);

  size_t   len;
  void*    buf = (void*)luaL_checklstring(L, 2, &len);
  uint64_t ofs = luaL_optint(L, 3, 0);

  lua_settop(L, 0);
  RAY_FS_CALL(L, write, NULL, self->h.file, buf, len, ofs);
}

static int ray_file_close(lua_State *L) {
  ray_object_t* self = (ray_object_t*)luaL_checkudata(L, 1, RAY_FILE_T);
  lua_settop(L, 0);
  RAY_FS_CALL(L, close, NULL, self->h.file);
}

static int ray_file_free(lua_State *L) {
  ray_object_t* self = (ray_object_t*)lua_touserdata(L, 1);
  if (self->data) free(self->data);
  return 0;
}

static int ray_file_tostring(lua_State *L) {
  ray_object_t* self = (ray_object_t*)luaL_checkudata(L, 1, RAY_FILE_T);
  lua_pushfstring(L, "userdata<%s>: %p", RAY_FILE_T, self);
  return 1;
}

luaL_Reg ray_fs_funcs[] = {
  {"open",      ray_fs_open},
  {"unlink",    ray_fs_unlink},
  {"mkdir",     ray_fs_mkdir},
  {"rmdir",     ray_fs_rmdir},
  {"readdir",   ray_fs_readdir},
  {"stat",      ray_fs_stat},
  {"rename",    ray_fs_rename},
  {"sendfile",  ray_fs_sendfile},
  {"chmod",     ray_fs_chmod},
  {"chown",     ray_fs_chown},
  {"utime",     ray_fs_utime},
  {"lstat",     ray_fs_lstat},
  {"link",      ray_fs_link},
  {"symlink",   ray_fs_symlink},
  {"readlink",  ray_fs_readlink},
  {"cwd",       ray_fs_cwd},
  {"chdir",     ray_fs_chdir},
  {"exepath",   ray_fs_exepath},
  {NULL,        NULL}
};

luaL_Reg ray_file_meths[] = {
  {"read",      ray_file_read},
  {"write",     ray_file_write},
  {"close",     ray_file_close},
  {"stat",      ray_file_stat},
  {"sync",      ray_file_sync},
  {"utime",     ray_file_utime},
  {"chmod",     ray_file_chmod},
  {"chown",     ray_file_chown},
  {"datasync",  ray_file_datasync},
  {"truncate",  ray_file_truncate},
  {"__gc",      ray_file_free},
  {"__tostring",ray_file_tostring},
  {NULL,        NULL}
};


