#ifndef _RAY_STATE_H_
#define _RAY_STATE_H_

#include "ray_common.h"

#include "ray_hash.h"
#include "ray_list.h"

#define RAY_LOOP "ray:loop"
#define RAY_MAIN "ray:main"

/* state flags */
#define RAY_CLOSED (1 << 31)
#define RAY_ACTIVE (1 << 30)

#define ray_is_closed(A) ((A)->flags & RAY_CLOSED)
#define ray_is_active(A) ((A)->flags & RAY_ACTIVE)

typedef uv_buf_t ray_buf_t;

typedef struct ray_state_s ray_state_t;

typedef int (*ray_alloc_t)(ray_state_t* self);
typedef int (*ray_react_t)(ray_state_t* self);
typedef int (*ray_yield_t)(ray_state_t* self);
typedef int (*ray_close_t)(ray_state_t* self);

typedef struct ray_vtable_s {
  ray_alloc_t alloc;
  ray_react_t react;
  ray_yield_t yield;
  ray_close_t close;
} ray_vtable_t;

typedef union ray_data_u {
  ray_hash_t* hash;
  ray_list_t* list;
  void*       data;
} ray_data_t;

struct ray_state_s {
  ray_vtable_t  v;
  ray_handle_t  h;
  ray_req_t     r;
  lua_State*    L;
  int           L_ref;
  int           flags;
  ngx_queue_t   cond;
  ray_buf_t     buf;
  ray_data_t    u;
  uv_thread_t   tid;
};

#define ray_active(A) \
  for ((A)->flags |=  RAY_ACTIVE; \
       (A)->flags &   RAY_ACTIVE; \
       (A)->flags &= ~RAY_ACTIVE)

#define ray_cond_enqueue(Q,S) \
  ngx_queue_remove(&(S)->cond); \
  ngx_queue_insert_tail(Q, &(S)->cond)

#define ray_cond_dequeue(S) \
  ngx_queue_remove(&(S)->cond); \
  ngx_queue_init(&(S)->cond)

/* default state metatable */
#define RAY_STATE_T "ray.state.State"

int ray_init_main(lua_State* L);

uv_loop_t*   ray_get_loop(lua_State* L);
ray_state_t* ray_get_main(lua_State* L);
ray_state_t* ray_current (lua_State* L);

ray_state_t* ray_state_new(lua_State* L, const char* m, const ray_vtable_t* v);

int ray_yield(ray_state_t* self);
int ray_react(ray_state_t* self);
int ray_close(ray_state_t* self);

int ray_ready(ray_state_t* self);
int ray_error(ray_state_t* self);

/* stack utilities */
int  ray_xcopy (ray_state_t* self, ray_state_t* that, int narg);
int  ray_push  (ray_state_t* self, int narg);

void ray_push_state (lua_State* L, ray_state_t* state);
void ray_push_self  (ray_state_t* self);

#endif /* _RAY_STATE_H_ */
