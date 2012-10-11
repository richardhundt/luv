#ifndef _LUV_STATE_H_
#define _LUV_STATE_H_

#include "luv_common.h"

#define luvL_state_is_thread(S) ((S)->type == LUV_TTHREAD)
#define luvL_state_is_active(S) ((S) == luvL_thread_self((S)->L)->curr)

/* state flags */
#define LUV_FSTART (1 << 0)
#define LUV_FREADY (1 << 1)
#define LUV_FMAIN  (1 << 2)
#define LUV_FWAIT  (1 << 3)
#define LUV_FJOIN  (1 << 4)
#define LUV_FDEAD  (1 << 5)

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

luv_state_t* luvL_state_self(lua_State* L);

void luvL_state_ready   (luv_state_t* state);
int  luvL_state_yield   (luv_state_t* state, int narg);
int  luvL_state_suspend (luv_state_t* state);
int  luvL_state_resume  (luv_state_t* state, int narg);

uv_loop_t* luvL_event_loop(lua_State* L);

luv_state_t* luvL_state_self(lua_State* L);

#endif /* _LUV_STATE_H_
