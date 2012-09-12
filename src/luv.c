/*
 *  Copyright 2012 The Luvit Authors. All Rights Reserved.
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <limits.h> /* PATH_MAX */

#include "luv.h"
#include "uv.h"
#include "uv-private/ev.h"

#include "luv_fs.h"
#include "luv_handle.h"
#include "luv_udp.h"
#include "luv_fs_watcher.h"
#include "luv_timer.h"
#include "luv_process.h"
#include "luv_stream.h"
#include "luv_tcp.h"
#include "luv_pipe.h"
#include "luv_tty.h"
#include "luv_misc.h"

static const luaL_reg luv_f[] = {

  /* Handle functions */
  {"close", luv_close},
  {"setHandler", luv_set_handler},

  /* UDP functions */
  {"newUdp", luv_new_udp},
  {"udpBind", luv_udp_bind},
  {"udpBind6", luv_udp_bind6},
  {"udpSetMembership", luv_udp_set_membership},
  {"udpGetsockname", luv_udp_getsockname},
  {"udpSend", luv_udp_send},
  {"udpSend6", luv_udp_send6},
  {"udpRecvStart", luv_udp_recv_start},
  {"udpRecvStop", luv_udp_recv_stop},

  /* FS Watcher functions */
  {"newFsWatcher", luv_new_fs_watcher},

  /* Timer functions */
  {"newTimer", luv_new_timer},
  {"timerStart", luv_timer_start},
  {"timerStop", luv_timer_stop},
  {"timerAgain", luv_timer_again},
  {"timerSetRepeat", luv_timer_set_repeat},
  {"timerGetRepeat", luv_timer_get_repeat},
  {"timerGetActive", luv_timer_get_active},

  /* Process functions */
  {"spawn", luv_spawn},
  {"processKill", luv_process_kill},
  {"getpid", luv_getpid},
  
  /* Stream functions */
  {"shutdown", luv_shutdown},
  {"listen", luv_listen},
  {"accept", luv_accept},
  {"readStart", luv_read_start},
  {"readStart2", luv_read_start2},
  {"readStop", luv_read_stop},
  {"writeQueueSize", luv_write_queue_size},
  {"write", luv_write},
  {"write2", luv_write2},

  /* TCP functions */
  {"newTcp", luv_new_tcp},
  {"tcpBind", luv_tcp_bind},
  {"tcpBind6", luv_tcp_bind6},
  {"tcpNodelay", luv_tcp_nodelay},
  {"tcpGetsockname", luv_tcp_getsockname},
  {"tcpGetpeername", luv_tcp_getpeername},
  {"tcpConnect", luv_tcp_connect},
  {"tcpConnect6", luv_tcp_connect6},

  /* Pipe functions */
  {"newPipe", luv_new_pipe},
  {"pipeOpen", luv_pipe_open},
  {"pipeBind", luv_pipe_bind},
  {"pipeConnect", luv_pipe_connect},

  /* TTY functions */
  {"newTty", luv_new_tty},
  {"ttySetMode", luv_tty_set_mode},
  {"ttyResetMode", luv_tty_reset_mode},
  {"ttyGetWinsize", luv_tty_get_winsize},

  /* FS functions */
  {"fsOpen", luv_fs_open},
  {"fsClose", luv_fs_close},
  {"fsRead", luv_fs_read},
  {"fsWrite", luv_fs_write},
  {"fsUnlink", luv_fs_unlink},
  {"fsMkdir", luv_fs_mkdir},
  {"fsRmdir", luv_fs_rmdir},
  {"fsReaddir", luv_fs_readdir},
  {"fsStat", luv_fs_stat},
  {"fsFstat", luv_fs_fstat},
  {"fsRename", luv_fs_rename},
  {"fsFsync", luv_fs_fsync},
  {"fsFdatasync", luv_fs_fdatasync},
  {"fsFtruncate", luv_fs_ftruncate},
  {"fsSendfile", luv_fs_sendfile},
  {"fsChmod", luv_fs_chmod},
  {"fsUtime", luv_fs_utime},
  {"fsFutime", luv_fs_futime},
  {"fsLstat", luv_fs_lstat},
  {"fsLink", luv_fs_link},
  {"fsSymlink", luv_fs_symlink},
  {"fsReadlink", luv_fs_readlink},
  {"fsFchmod", luv_fs_fchmod},
  {"fsChown", luv_fs_chown},
  {"fsFchown", luv_fs_fchown},

  /* Misc functions */
  {"run", luv_run},
  {"ref", luv_ref},
  {"unref", luv_unref},
  {"updateTime", luv_update_time},
  {"now", luv_now},
  {"hrtime", luv_hrtime},
  {"getFreeMemory", luv_get_free_memory},
  {"getTotalMemory", luv_get_total_memory},
  {"loadavg", luv_loadavg},
  {"uptime", luv_uptime},
  {"cpuInfo", luv_cpu_info},
  {"interfaceAddresses", luv_interface_addresses},
  {"execpath", luv_execpath},
  {"handleType", luv_handle_type},
  {"activateSignalHandler", luv_activate_signal_handler},
  {NULL, NULL}
};

/* When the lhandle is freed, do some helpful sanity checks */
static int luv_handle_gc(lua_State* L) {
  luv_handle_t* lhandle = (luv_handle_t*)lua_touserdata(L, 1);
/*  printf("__gc %s lhandle=%p handle=%p\n", lhandle->type, lhandle, lhandle->handle);*/
  /* If the handle is still there, they forgot to close */
  if (lhandle->handle) {
    fprintf(stderr, "WARNING: forgot to close %s lhandle=%p handle=%p\n", lhandle->type, lhandle, lhandle->handle);
    uv_close(lhandle->handle, luv_on_close);
  }
  return 0;
}

static int luvit_exit(lua_State* L) {
  int exit_code = luaL_checkint(L, 1);
  exit(exit_code);
  return 0;
}

static int luvit_print_stderr(lua_State* L) {
  const char* line = luaL_checkstring(L, 1);
  fprintf(stderr, "%s", line);
  return 0;
}

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof((a)) / sizeof((a)[0]))
#endif

#ifndef PATH_MAX
#define PATH_MAX 1024
#endif

static char getbuf[PATH_MAX + 1];

static int luvit_getcwd(lua_State* L) {
  uv_err_t rc;

  rc = uv_cwd(getbuf, ARRAY_SIZE(getbuf) - 1);
  if (rc.code != UV_OK) {
    return luaL_error(L, "luvit_getcwd: %s\n", strerror(errno));
  }

  getbuf[ARRAY_SIZE(getbuf) - 1] = '\0';
  lua_pushstring(L, getbuf);
  return 1;
}


LUALIB_API int luaopen_luv (lua_State* L) {
  int rc;
  uv_loop_t *loop;

  /* metatable for handle userdata types */
  /* It is it's own __index table to save space */
  luaL_newmetatable(L, "luv_handle");
  lua_pushcfunction(L, luv_handle_gc);
  lua_setfield(L, -2, "__gc");
  lua_pop(L, 1);

  /* Create a new exports table with functions and constants */
  lua_newtable (L);

  luaL_register(L, NULL, luv_f);
  lua_pushnumber(L, UV_VERSION_MAJOR);
  lua_setfield(L, -2, "VERSION_MAJOR");
  lua_pushnumber(L, UV_VERSION_MINOR);
  lua_setfield(L, -2, "VERSION_MINOR");

  lua_pushcfunction(L, luvit_exit);
  lua_setglobal(L, "exitProcess");

  lua_pushcfunction(L, luvit_print_stderr);
  lua_setglobal(L, "printStderr");

  lua_pushcfunction(L, luvit_getcwd);
  lua_setglobal(L, "getcwd");

  /* Hold a reference to the main thread in the registry */
  rc = lua_pushthread(L);
  assert(rc == 1);
  lua_setfield(L, LUA_REGISTRYINDEX, "main_thread");

  loop = uv_default_loop();
  luv_set_loop(L, loop);

  return 1;
}

