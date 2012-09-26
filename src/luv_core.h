#ifndef LUV_CORE_H
#define LUV_CORE_H

#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>

#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"

#include "uv.h"
#include "uv-private/ngx-queue.h"

#include "luv.h"

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

#define LUV_FIBER_T "luv.fiber"
#define LUV_SCHED_T "luv.sched"
#define LUV_SCHED_O "luv.sched.instance"

#define LUV_FSTART (1 << 0)
#define LUV_FREADY (1 << 1)
#define LUV_FMAIN  (1 << 2)
#define LUV_FWAIT  (1 << 3)
#define LUV_FJOIN  (1 << 4)
#define LUV_FDEAD  (1 << 5)

LUALIB_API int luaopenL_luv_core(lua_State *L);

typedef struct luv_state_s  luv_state_t;
typedef struct luv_fiber_s  luv_fiber_t;
typedef struct luv_sched_s  luv_sched_t;
typedef struct luv_thread_s luv_thread_t;

typedef enum {
  LUV_TSCHED,
  LUV_TFIBER,
  LUV_TTHREAD
} luv_state_type;

int  luv__sched_in_main(luv_sched_t* sched);
int  luv__sched_loop   (luv_sched_t* sched);
int  luv__state_is_main(luv_state_t* state);
int  luv__state_yield  (luv_state_t* state, int narg);
void luv__state_resume (luv_state_t* state);
void luv__state_suspend(luv_state_t* state);

luv_state_t* luv__sched_current(luv_sched_t* sched);

#define LUV_STATE_FIELDS \
  ngx_queue_t   rouse; \
  ngx_queue_t   queue; \
  ngx_queue_t   cond;  \
  int           type;  \
  int           flags; \
  luv_state_t*  outer; \
  luv_sched_t*  sched; \
  lua_State*    L;     \
  luv_req_t     req;   \
  void*         data

struct luv_state_s {
  LUV_STATE_FIELDS;
};

struct luv_sched_s {
  LUV_STATE_FIELDS;
  ngx_queue_t     ready;
  uv_prepare_t    hook;
  uv_loop_t*      loop;
  luv_state_t*    curr;
};

struct luv_fiber_s {
  LUV_STATE_FIELDS;
  int   ref, coref;
};

struct luv_thread_s {
  LUV_STATE_FIELDS;
  uv_thread_t tid;
  int         ref;
};

union luv_any_state {
  luv_state_t  state;
  luv_fiber_t  fiber;
  luv_sched_t  sched;
  luv_thread_t thread;
};

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


#endif /* LUV_CORE_H */
