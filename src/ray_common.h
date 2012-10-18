#ifndef _RAY_COMMON_H_
#define _RAY_COMMON_H_

#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>

#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"

#include "uv/include/uv.h"
#include "ngx-queue.h"

#ifdef WIN32
# define UNUSED /* empty */
# define INLINE __inline
#else
# define UNUSED __attribute__((unused))
# define INLINE inline
#endif

#define RAY_DEBUG

#ifdef RAY_DEBUG
#  define TRACE(fmt, ...) do { \
    fprintf(stderr, "%s: %d: %s: " fmt, \
    __FILE__, __LINE__, __func__, ##__VA_ARGS__); \
  } while (0)
#else
#  define TRACE(fmt, ...) ((void)0)
#endif /* RAY_DEBUG */

typedef union ray_handle_u {
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
} ray_handle_t;

typedef union ray_req_u {
  uv_req_t          req;
  uv_write_t        write;
  uv_connect_t      connect;
  uv_shutdown_t     shutdown;
  uv_fs_t           fs;
  uv_work_t         work;
  uv_udp_send_t     udp_send;
  uv_getaddrinfo_t  getaddrinfo;
} ray_req_t;

/* registry table for ray object refs */
#define RAY_REG_KEY "__RAY__"

/* default buffer size for read operations */
#define RAY_BUF_SIZE 4096

/* max path length */
#define RAY_MAX_PATH 1024

/* metatables for various types */
#define RAY_COND_T        "ray.cond.CondVar"
#define RAY_FIBER_T       "ray.fiber.Fiber"
#define RAY_THREAD_T      "ray.thread.Thread"
#define RAY_IDLE_T        "ray.idle.Watcher"
#define RAY_TIMER_T       "ray.timer.Watcher"
#define RAY_FS_POLL_T     "ray.fs.Watcher"
#define RAY_FILE_T        "ray.fs.File"
#define RAY_PIPE_T        "ray.pipe.Stream"
#define RAY_PROCESS_T     "ray.process.Process"
#define RAY_TTY_T         "ray.tty.Stream"
#define RAY_TCP_T         "ray.tcp.Stream"
#define RAY_UDP_T         "ray.udp.Socket"
#define RAY_ZMQ_CTX_T     "ray.zmq.Context"
#define RAY_ZMQ_SOCKET_T  "ray.zmq.Socket"


#define container_of(ptr, type, member) \
  ((type*) ((char*)(ptr) - offsetof(type, member)))

/* lifted from luasys */
#define ray_boxpointer(L,u) \
    (*(void**) (lua_newuserdata(L, sizeof(void*))) = (u))
#define ray_unboxpointer(L,i) \
    (*(void**) (lua_touserdata(L, i)))

/* lifted from luasys */
#define ray_boxinteger(L,n) \
    (*(lua_Integer*) (lua_newuserdata(L, sizeof(lua_Integer))) = (lua_Integer)(n))
#define ray_unboxinteger(L,i) \
    (*(lua_Integer*) (lua_touserdata(L, i)))

#define ray_absindex(L, i) \
  ((i) > 0 || (i) <= LUA_REGISTRYINDEX ? (i) : lua_gettop(L) + (i) + 1)


#endif /* _RAY_COMMON_H_ */

