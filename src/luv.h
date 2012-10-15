#ifndef LUV_H
#define LUV_H

#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"

#ifdef __cplusplus
}
#endif

#include "uv/include/uv.h"
#include "ngx-queue.h"

#ifdef USE_ZMQ
#include "zmq/include/zmq.h"
#include "zmq/include/zmq_utils.h"
#endif

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

/* state flags */
#define LUV_FSTART (1 << 0)
#define LUV_FREADY (1 << 1)
#define LUV_FMAIN  (1 << 2)
#define LUV_FWAIT  (1 << 3)
#define LUV_FJOIN  (1 << 4)
#define LUV_FDEAD  (1 << 5)

/* ØMQ flags */
#define LUV_ZMQ_SCLOSED (1 << 0)
#define LUV_ZMQ_XDUPCTX (1 << 1)
#define LUV_ZMQ_WSEND   (1 << 2)
#define LUV_ZMQ_WRECV   (1 << 3)

/* luv states */
typedef struct luv_state_s  luv_state_t;
typedef struct luv_fiber_s  luv_fiber_t;
typedef struct luv_thread_s luv_thread_t;

typedef enum {
  LUV_TFIBER,
  LUV_TTHREAD
} luv_state_type;

#define LUV_STATE_FIELDS \
  ngx_queue_t   rouse; \
  ngx_queue_t   queue; \
  ngx_queue_t   join;  \
  ngx_queue_t   cond;  \
  uv_loop_t*    loop;  \
  int           type;  \
  int           flags; \
  luv_state_t*  outer; \
  lua_State*    L;     \
  luv_req_t     req;   \
  void*         data

struct luv_state_s {
  LUV_STATE_FIELDS;
};

struct luv_thread_s {
  LUV_STATE_FIELDS;
  luv_state_t*    curr;
  uv_thread_t     tid;
  uv_async_t      async;
  uv_check_t      check;
};

struct luv_fiber_s {
  LUV_STATE_FIELDS;
};

union luv_any_state {
  luv_state_t  state;
  luv_fiber_t  fiber;
  luv_thread_t thread;
};

/* luv objects */
#define LUV_OSTARTED  (1 << 0)
#define LUV_OSTOPPED  (1 << 1)
#define LUV_OWAITING  (1 << 2)
#define LUV_OCLOSING  (1 << 3)
#define LUV_OCLOSED   (1 << 4)
#define LUV_OSHUTDOWN (1 << 5)

#define luvL_object_is_started(O)  ((O)->flags & LUV_OSTARTED)
#define luvL_object_is_stopped(O)  ((O)->flags & LUV_OSTOPPED)
#define luvL_object_is_waiting(O)  ((O)->flags & LUV_OWAITING)
#define luvL_object_is_closing(O)  ((O)->flags & LUV_OCLOSING)
#define luvL_object_is_shutdown(O) ((O)->flags & LUV_OSHUTDOWN)
#define luvL_object_is_closed(O)   ((O)->flags & LUV_OCLOSED)

#define LUV_OBJECT_FIELDS \
  ngx_queue_t   rouse; \
  ngx_queue_t   queue; \
  luv_state_t*  state; \
  int           flags; \
  int           type;  \
  int           count; \
  int           ref;   \
  void*         data

typedef struct luv_object_s {
  LUV_OBJECT_FIELDS;
  luv_handle_t  h;
  uv_buf_t      buf;
} luv_object_t;

typedef struct luv_chan_s {
  LUV_OBJECT_FIELDS;
  void*         put;
  void*         get;
} luv_chan_t;

union luv_any_object {
  luv_object_t object;
  luv_chan_t   chan;
};

int luvL_traceback(lua_State *L);

uv_loop_t* luvL_event_loop(lua_State* L);

int luvL_state_in_thread(luv_state_t* state);
int luvL_state_is_thread(luv_state_t* state);
int luvL_state_is_active(luv_state_t* state);

void luvL_state_ready  (luv_state_t* state);
int  luvL_state_yield  (luv_state_t* state, int narg);
int  luvL_state_suspend(luv_state_t* state);
int  luvL_state_resume (luv_state_t* state, int narg);

void luvL_fiber_ready  (luv_fiber_t* fiber);
int  luvL_fiber_yield  (luv_fiber_t* fiber, int narg);
int  luvL_fiber_suspend(luv_fiber_t* fiber);
int  luvL_fiber_resume (luv_fiber_t* fiber, int narg);

int  luvL_thread_loop   (luv_thread_t* thread);
int  luvL_thread_once   (luv_thread_t* thread);
void luvL_thread_ready  (luv_thread_t* thread);
int  luvL_thread_yield  (luv_thread_t* thread, int narg);
int  luvL_thread_suspend(luv_thread_t* thread);
int  luvL_thread_resume (luv_thread_t* thread, int narg);
void luvL_thread_enqueue(luv_thread_t* thread, luv_fiber_t* fiber);

luv_state_t*  luvL_state_self (lua_State* L);
luv_thread_t* luvL_thread_self(lua_State* L);

void luvL_thread_init_main(lua_State* L);

luv_fiber_t*  luvL_fiber_create (luv_state_t* outer, int narg);
luv_thread_t* luvL_thread_create(luv_state_t* outer, int narg);

void luvL_fiber_close (luv_fiber_t* self);

int  luvL_thread_loop (luv_thread_t* self);
int  luvL_thread_once (luv_thread_t* self);

void luvL_object_init (luv_state_t* state, luv_object_t* self);
void luvL_object_close(luv_object_t* self);

int  luvL_stream_stop (luv_object_t* self);
void luvL_stream_free (luv_object_t* self);
void luvL_stream_close(luv_object_t* self);

typedef ngx_queue_t luv_cond_t;

int luvL_cond_init      (luv_cond_t* cond);
int luvL_cond_wait      (luv_cond_t* cond, luv_state_t* curr);
int luvL_cond_signal    (luv_cond_t* cond);
int luvL_cond_broadcast (luv_cond_t* cond);

int luvL_codec_encode(lua_State* L, int narg);
int luvL_codec_decode(lua_State* L);

int luvL_lib_decoder(lua_State* L);
int luvL_zmq_ctx_decoder(lua_State* L);

uv_buf_t luvL_alloc_cb   (uv_handle_t* handle, size_t size);
void     luvL_connect_cb (uv_connect_t* conn, int status);

int luvL_new_class (lua_State* L, const char* name, luaL_Reg* meths);
int luvL_new_module(lua_State* L, const char* name, luaL_Reg* funcs);

typedef struct luv_const_reg_s {
  const char*   key;
  int           val;
} luv_const_reg_t;

extern luaL_Reg luv_thread_funcs[32];
extern luaL_Reg luv_thread_meths[32];

extern luaL_Reg luv_fiber_funcs[32];
extern luaL_Reg luv_fiber_meths[32];

extern luaL_Reg luv_cond_funcs[32];
extern luaL_Reg luv_cond_meths[32];

extern luaL_Reg luv_codec_funcs[32];

extern luaL_Reg luv_timer_funcs[32];
extern luaL_Reg luv_timer_meths[32];

extern luaL_Reg luv_idle_funcs[32];
extern luaL_Reg luv_idle_meths[32];

extern luaL_Reg luv_fs_funcs[32];
extern luaL_Reg luv_file_meths[32];

extern luaL_Reg luv_stream_meths[32];

extern luaL_Reg luv_net_funcs[32];
extern luaL_Reg luv_net_tcp_meths[32];
extern luaL_Reg luv_net_udp_meths[32];

extern luaL_Reg luv_pipe_funcs[32];
extern luaL_Reg luv_pipe_meths[32];

extern luaL_Reg luv_process_funcs[32];
extern luaL_Reg luv_process_meths[32];

#ifdef USE_ZMQ
extern luaL_Reg luv_zmq_funcs[32];
extern luaL_Reg luv_zmq_ctx_meths[32];
extern luaL_Reg luv_zmq_socket_meths[32];
#endif

#ifdef WIN32
#  ifdef LUV_EXPORT
#    define LUALIB_API __declspec(dllexport)
#  else
#    define LUALIB_API __declspec(dllimport)
#  endif
#else
#  define LUALIB_API LUA_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

LUALIB_API int luaopen_luv(lua_State *L);

#ifdef __cplusplus
}
#endif

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

#endif /* LUV_H */
