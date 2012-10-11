#ifndef _LUV_COMMON_H_
#define _LUV_COMMON_H_

#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>

#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"

#include "uv/include/uv.h"
#include "ngx-queue.h"

#undef LUV_DEBUG

#ifdef LUV_DEBUG
#  define TRACE(fmt, ...) do { \
    fprintf(stderr, "%s: %d: %s: " fmt, \
    __FILE__, __LINE__, __func__, ##__VA_ARGS__); \
  } while (0)
#else
#  define TRACE(fmt, ...) ((void)0)
#endif /* LUV_DEBUG */

typedef union luv_handle_u {
  uv_handle_t     handle;
  uv_stream_t     stream;
  uv_tcp_t        tcp;
  uv_pipe_t       pipe;
  uv_prepare_t    prepare;
  uv_check_t      check;
  uv_idle_t       idle;
  uv_async_t      async;
  uv_timer_t      timer;
  uv_fs_event_t   fs_event;
  uv_fs_poll_t    fs_poll;
  uv_poll_t       poll;
  uv_process_t    process;
  uv_tty_t        tty;
  uv_udp_t        udp;
  uv_file         file;
} luv_handle_t;

typedef union luv_req_u {
  uv_req_t          req;
  uv_write_t        write;
  uv_connect_t      connect;
  uv_shutdown_t     shutdown;
  uv_fs_t           fs;
  uv_work_t         work;
  uv_udp_send_t     udp_send;
  uv_getaddrinfo_t  getaddrinfo;
} luv_req_t;

/* registry table for luv object refs */
#define LUV_REG_KEY "__LUV__"

/* default buffer size for read operations */
#define LUV_BUF_SIZE 4096

/* max path length */
#define LUV_MAX_PATH 1024

/* metatables for various types */
#define LUV_NS_T          "luv.ns"
#define LUV_COND_T        "luv.cond"
#define LUV_FIBER_T       "luv.fiber"
#define LUV_THREAD_T      "luv.thread"
#define LUV_ASYNC_T       "luv.async"
#define LUV_TIMER_T       "luv.timer"
#define LUV_IDLE_T        "luv.idle"
#define LUV_FS_T          "luv.fs"
#define LUV_FS_POLL_T     "luv.fs.poll"
#define LUV_FILE_T        "luv.file"
#define LUV_PIPE_T        "luv.pipe"
#define LUV_TTY_T         "luv.tty"
#define LUV_PROCESS_T     "luv.process"
#define LUV_NET_TCP_T     "luv.net.tcp"
#define LUV_NET_UDP_T     "luv.net.udp"
#define LUV_ZMQ_CTX_T     "luv.zmq.ctx"
#define LUV_ZMQ_SOCKET_T  "luv.zmq.socket"

#define container_of(ptr, type, member) \
  ((type*) ((char*)(ptr) - offsetof(type, member)))

/* lifted from luasys */
#define luv_boxpointer(L,u) \
    (*(void**) (lua_newuserdata(L, sizeof(void*))) = (u))
#define luv_unboxpointer(L,i) \
    (*(void**) (lua_touserdata(L, i)))

/* lifted from luasys */
#define luv_boxinteger(L,n) \
    (*(lua_Integer*) (lua_newuserdata(L, sizeof(lua_Integer))) = (lua_Integer) (n))
#define luv_unboxinteger(L,i) \
    (*(lua_Integer*) (lua_touserdata(L, i)))


#endif /* _LUV_COMMON_H_ */

