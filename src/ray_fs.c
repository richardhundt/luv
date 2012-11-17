#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#ifndef _WIN32
#include <unistd.h>
#endif

#include "ray_lib.h"
#include "ray_cond.h"
#include "ray_state.h"
#include "ray_fs.h"

/* lifted from luvit */
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

/* lifted from luvit */
static void push_stats_table(lua_State* L, struct stat* s) {
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

/* the rest of this file is mostly stolen from luvit - key difference is that
** we don't run Lua callbacks, but instead suspend and resume Lua threads */

static void fs_result(lua_State* L, uv_fs_t* req) {
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

      case UV_FS_OPEN: {
        TRACE("dumping: %p\n", L);
        rayL_dump_stack(L);
        ray_state_t* self = (ray_state_t*)luaL_checkudata(L, -1, RAY_FILE_T);
        self->h.file = req->result;
        break;
      }

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

      case UV_FS_READDIR: {
        int i;
        char* namep = (char*)req->ptr;
        int   count = req->result;
        lua_newtable(L);
        for (i = 1; i <= count; i++) {
          lua_pushstring(L, namep);
          lua_rawseti(L, -2, i);
          namep += strlen(namep) + 1; /* +1 for '\0' */
        }
        break;
      }

      case UV_FS_STAT:
      case UV_FS_LSTAT:
      case UV_FS_FSTAT:
        push_stats_table(L, (struct stat*)req->ptr);
        break;

      default:
        luaL_error(L, "Unhandled fs_type");
    }
  }
  uv_fs_req_cleanup(req);
}

static void fs_cb(uv_fs_t* req) {
  ray_state_t* curr = container_of(req, ray_state_t, r);
  fs_result(curr->L, req);
  ray_ready(curr);
}

#define RAY_FS_CALL(L, func, misc, ...) do { \
    ray_state_t* curr = ray_current(L); \
    uv_loop_t*   loop = ray_get_loop(L); \
    uv_fs_t*     req = &curr->r.fs; \
    req->data = misc; \
    \
    if (uv_fs_##func(loop, req, __VA_ARGS__, fs_cb) < 0) { \
      uv_err_t err = uv_last_error(loop); \
      lua_settop(L, 0); \
      lua_pushboolean(L, 0); \
      lua_pushstring(L, uv_strerror(err)); \
      return 2; \
    } \
    return ray_yield(curr); \
  } while(0)

static ray_vtable_t fs_v = {

};

static int fs_open(lua_State* L) {
  ray_state_t* curr = ray_current(L);
  const char*  path = luaL_checkstring(L, 1);
  ray_state_t* self;

  int flags = string_to_flags(L, luaL_checkstring(L, 2));
  int mode  = strtoul(luaL_checkstring(L, 3), NULL, 8);

  lua_settop(L, 0);
  self = ray_state_new(L, RAY_FILE_T, &fs_v);
  self->h.file = -1; /* invalid file handle */
  TRACE("dumping: %p\n", L);
  rayL_dump_stack(L);

  RAY_FS_CALL(L, open, NULL, path, flags, mode);
}

static int fs_unlink(lua_State* L) {
  const char* path = luaL_checkstring(L, 1);
  lua_settop(L, 0);
  RAY_FS_CALL(L, unlink, NULL, path);
}

static int fs_mkdir(lua_State* L) {
  const char*  path = luaL_checkstring(L, 1);
  int mode = strtoul(luaL_checkstring(L, 2), NULL, 8);
  lua_settop(L, 0);
  RAY_FS_CALL(L, mkdir, NULL, path, mode);
}

static int fs_rmdir(lua_State* L) {
  const char*  path = luaL_checkstring(L, 1);
  lua_settop(L, 0);
  RAY_FS_CALL(L, rmdir, NULL, path);
}

static int fs_readdir(lua_State* L) {
  const char* path = luaL_checkstring(L, 1);
  lua_settop(L, 0);
  RAY_FS_CALL(L, readdir, NULL, path, 0);
}

static int fs_stat(lua_State* L) {
  const char* path = luaL_checkstring(L, 1);
  lua_settop(L, 0);
  RAY_FS_CALL(L, stat, NULL, path);
}

static int fs_rename(lua_State* L) {
  const char* old_path = luaL_checkstring(L, 1);
  const char* new_path = luaL_checkstring(L, 2);
  lua_settop(L, 0);
  RAY_FS_CALL(L, rename, NULL, old_path, new_path);
}

static int fs_sendfile(lua_State* L) {
  ray_state_t* o_file = (ray_state_t*)luaL_checkudata(L, 1, RAY_FILE_T);
  ray_state_t* i_file = (ray_state_t*)luaL_checkudata(L, 2, RAY_FILE_T);
  off_t  ofs = luaL_checkint(L, 3);
  size_t len = luaL_checkint(L, 4);
  lua_settop(L, 2);
  RAY_FS_CALL(L, sendfile, NULL, o_file->h.file, i_file->h.file, ofs, len);
}

static int fs_chmod(lua_State* L) {
  const char* path = luaL_checkstring(L, 1);
  int mode = strtoul(luaL_checkstring(L, 2), NULL, 8);
  lua_settop(L, 0);
  RAY_FS_CALL(L, chmod, NULL, path, mode);
}

static int fs_utime(lua_State* L) {
  const char* path = luaL_checkstring(L, 1);
  double atime = luaL_checknumber(L, 2);
  double mtime = luaL_checknumber(L, 3);
  lua_settop(L, 0);
  RAY_FS_CALL(L, utime, NULL, path, atime, mtime);
}

static int fs_lstat(lua_State* L) {
  const char* path = luaL_checkstring(L, 1);
  lua_settop(L, 0);
  RAY_FS_CALL(L, lstat, NULL, path);
}

static int fs_link(lua_State* L) {
  const char* src_path = luaL_checkstring(L, 1);
  const char* dst_path = luaL_checkstring(L, 2);
  lua_settop(L, 0);
  RAY_FS_CALL(L, link, NULL, src_path, dst_path);
}

static int fs_symlink(lua_State* L) {
  const char* src_path = luaL_checkstring(L, 1);
  const char* dst_path = luaL_checkstring(L, 2);
  int flags = string_to_flags(L, luaL_checkstring(L, 3));
  lua_settop(L, 0);
  RAY_FS_CALL(L, symlink, NULL, src_path, dst_path, flags);
}

static int fs_readlink(lua_State* L) {
  const char* path = luaL_checkstring(L, 1);
  lua_settop(L, 0);
  RAY_FS_CALL(L, readlink, NULL, path);
}

static int fs_chown(lua_State* L) {
  const char* path = luaL_checkstring(L, 1);
  int uid = luaL_checkint(L, 2);
  int gid = luaL_checkint(L, 3);
  lua_settop(L, 0);
  RAY_FS_CALL(L, chown, NULL, path, uid, gid);
}

static int fs_cwd(lua_State* L) {
  char buffer[RAY_MAX_PATH];
  uv_err_t err = uv_cwd(buffer, RAY_MAX_PATH);
  if (err.code) {
    return luaL_error(L, uv_strerror(err));
  }
  lua_pushstring(L, buffer);
  return 1;
}

static int fs_chdir(lua_State* L) {
  const char* dir = luaL_checkstring(L, 1);
  uv_err_t err = uv_chdir(dir);
  if (err.code) {
    return luaL_error(L, uv_strerror(err));
  }
  return 0;
}

static int fs_exepath(lua_State* L) {
  char buffer[RAY_MAX_PATH];
  size_t len = RAY_MAX_PATH;
  uv_exepath(buffer, &len);
  lua_pushlstring(L, buffer, len);
  return 1;
}


/* file instance methods */
static int file_stat(lua_State* L) {
  ray_state_t* self = (ray_state_t*)luaL_checkudata(L, 1, RAY_FILE_T);
  lua_settop(L, 0);
  RAY_FS_CALL(L, fstat, NULL, self->h.file);
}

static int file_sync(lua_State* L) {
  ray_state_t* self = (ray_state_t*)luaL_checkudata(L, 1, RAY_FILE_T);
  lua_settop(L, 0);
  RAY_FS_CALL(L, fsync, NULL, self->h.file);
}

static int file_datasync(lua_State* L) {
  ray_state_t* self = (ray_state_t*)luaL_checkudata(L, 1, RAY_FILE_T);
  lua_settop(L, 0);
  RAY_FS_CALL(L, fdatasync, NULL, self->h.file);
}

static int file_truncate(lua_State* L) {
  ray_state_t* self = (ray_state_t*)luaL_checkudata(L, 1, RAY_FILE_T);
  off_t ofs = luaL_checkint(L, 2);
  lua_settop(L, 0);
  RAY_FS_CALL(L, ftruncate, NULL, self->h.file, ofs);
}

static int file_utime(lua_State* L) {
  ray_state_t* self = (ray_state_t*)luaL_checkudata(L, 1, RAY_FILE_T);
  double atime = luaL_checknumber(L, 2);
  double mtime = luaL_checknumber(L, 3);
  lua_settop(L, 0);
  RAY_FS_CALL(L, futime, NULL, self->h.file, atime, mtime);
}

static int file_chmod(lua_State* L) {
  ray_state_t* self = (ray_state_t*)luaL_checkudata(L, 1, RAY_FILE_T);
  int mode = strtoul(luaL_checkstring(L, 2), NULL, 8);
  lua_settop(L, 0);
  RAY_FS_CALL(L, fchmod, NULL, self->h.file, mode);
}

static int file_chown(lua_State* L) {
  ray_state_t* self = (ray_state_t*)luaL_checkudata(L, 1, RAY_FILE_T);
  int uid = luaL_checkint(L, 2);
  int gid = luaL_checkint(L, 3);
  lua_settop(L, 0);
  RAY_FS_CALL(L, fchown, NULL, self->h.file, uid, gid);
}

static int file_read(lua_State *L) {
  ray_state_t* self = (ray_state_t*)luaL_checkudata(L, 1, RAY_FILE_T);

  size_t  len = luaL_optint(L, 2, RAY_BUF_SIZE);
  int64_t ofs = luaL_optint(L, 3, -1);
  void*   buf = malloc(len); /* free from ctx->r.fs_req.data in cb */

  lua_settop(L, 0);
  RAY_FS_CALL(L, read, buf, self->h.file, buf, len, ofs);
}

static int file_write(lua_State *L) {
  ray_state_t* self = (ray_state_t*)luaL_checkudata(L, 1, RAY_FILE_T);

  size_t   len;
  void*    buf = (void*)luaL_checklstring(L, 2, &len);
  uint64_t ofs = luaL_optint(L, 3, 0);

  lua_settop(L, 0);
  RAY_FS_CALL(L, write, NULL, self->h.file, buf, len, ofs);
}

static int file_close(lua_State *L) {
  ray_state_t* self = (ray_state_t*)luaL_checkudata(L, 1, RAY_FILE_T);
  lua_settop(L, 0);
  RAY_FS_CALL(L, close, NULL, self->h.file);
}

static int file_free(lua_State *L) {
  ray_state_t* self = (ray_state_t*)lua_touserdata(L, 1);
  if (self->u.data) free(self->u.data);
  return 0;
}

static int file_tostring(lua_State *L) {
  ray_state_t* self = (ray_state_t*)luaL_checkudata(L, 1, RAY_FILE_T);
  lua_pushfstring(L, "userdata<%s>: %p", RAY_FILE_T, self);
  return 1;
}

static luaL_Reg fs_funcs[] = {
  {"open",      fs_open},
  {"unlink",    fs_unlink},
  {"mkdir",     fs_mkdir},
  {"rmdir",     fs_rmdir},
  {"readdir",   fs_readdir},
  {"stat",      fs_stat},
  {"rename",    fs_rename},
  {"sendfile",  fs_sendfile},
  {"chmod",     fs_chmod},
  {"chown",     fs_chown},
  {"utime",     fs_utime},
  {"lstat",     fs_lstat},
  {"link",      fs_link},
  {"symlink",   fs_symlink},
  {"readlink",  fs_readlink},
  {"cwd",       fs_cwd},
  {"chdir",     fs_chdir},
  {"exepath",   fs_exepath},
  {NULL,        NULL}
};

static luaL_Reg file_meths[] = {
  {"read",      file_read},
  {"write",     file_write},
  {"close",     file_close},
  {"stat",      file_stat},
  {"sync",      file_sync},
  {"utime",     file_utime},
  {"chmod",     file_chmod},
  {"chown",     file_chown},
  {"datasync",  file_datasync},
  {"truncate",  file_truncate},
  {"__gc",      file_free},
  {"__tostring",file_tostring},
  {NULL,        NULL}
};

LUALIB_API int luaopen_ray_fs(lua_State* L) {
  rayL_module(L, "ray.fs", fs_funcs);
  rayL_class (L, RAY_FILE_T, file_meths);
  lua_pop(L, 1);

  ray_init_main(L);
  return 1;
}

