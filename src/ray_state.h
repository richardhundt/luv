#ifndef _RAY_STATE_H_
#define _RAY_STATE_H_

#include "ray_common.h"

#define rayL_state_is_thread(S) ((S)->type == RAY_TTHREAD)
#define rayL_state_is_active(S) ((S) == rayL_thread_self((S)->L)->curr)

/* state flags */
#define RAY_FSTART (1 << 0)
#define RAY_FREADY (1 << 1)
#define RAY_FMAIN  (1 << 2)
#define RAY_FWAIT  (1 << 3)
#define RAY_FJOIN  (1 << 4)
#define RAY_FDEAD  (1 << 5)

/* ray states */
typedef struct ray_state_s  ray_state_t;
typedef struct ray_fiber_s  ray_fiber_t;
typedef struct ray_thread_s ray_thread_t;

typedef enum {
  RAY_TFIBER,
  RAY_TTHREAD
} ray_state_type;

#define RAY_STATE_FIELDS \
  ngx_queue_t   rouse; \
  ngx_queue_t   queue; \
  ngx_queue_t   join;  \
  ngx_queue_t   cond;  \
  uv_loop_t*    loop;  \
  int           type;  \
  int           flags; \
  ray_state_t*  outer; \
  lua_State*    L;     \
  ray_req_t     req;   \
  void*         data

struct ray_state_s {
  RAY_STATE_FIELDS;
};

struct ray_thread_s {
  RAY_STATE_FIELDS;
  ray_state_t*    curr;
  uv_thread_t     tid;
  uv_async_t      async;
  uv_check_t      check;
};

struct ray_fiber_s {
  RAY_STATE_FIELDS;
};

union ray_any_state {
  ray_state_t  state;
  ray_fiber_t  fiber;
  ray_thread_t thread;
};

ray_state_t* rayL_state_self(lua_State* L);

void rayL_state_ready   (ray_state_t* state);
int  rayL_state_yield   (ray_state_t* state, int narg);
int  rayL_state_suspend (ray_state_t* state);
int  rayL_state_resume  (ray_state_t* state, int narg);

uv_loop_t* rayL_event_loop(lua_State* L);

ray_state_t* rayL_state_self(lua_State* L);

#endif /* _RAY_STATE_H_ */
