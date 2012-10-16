#ifndef _RAY_STATE_H_
#define _RAY_STATE_H_

#include "ray_common.h"
#include "ray_hash.h"
#include "ray_list.h"

#define RAY_EVENT_LOOP "ray:event:loop"
#define RAY_STATE_MAIN "ray:state:main"

/* state flags */
#define RAY_START  (1 << 0) /* initial state */
#define RAY_READY  (1 << 1) /* in ready queue */
#define RAY_ACTIVE (1 << 2) /* currently running */
#define RAY_CLOSED (1 << 3) /* not among the living */

#define rayS_is_start(S)  ((S)->flags & RAY_START)
#define rayS_is_ready(S)  ((S)->flags & RAY_READY)
#define rayS_is_active(S) ((S)->flags & RAY_ACTIVE)
#define rayS_is_closed(S) ((S)->flags & RAY_CLOSED)

/* ray states */
typedef struct ray_state_s ray_state_t;

typedef union ray_data_u {
  void*       data;
  ray_hash_t* hash;
  ray_list_t* list;
} ray_data_t;

typedef uv_buf_t ray_buf_t;

typedef struct ray_vtable_s {
  int (*suspend)(ray_state_t* self);
  int (*ready  )(ray_state_t* self);
  int (*enqueue)(ray_state_t* self, ray_state_t* that);
  int (*resume )(ray_state_t* self);
  int (*close  )(ray_state_t* self);
  int (*send   )(ray_state_t* self, ray_state_t* that, int narg);
  int (*recv   )(ray_state_t* self);
} ray_vtable_t;

struct ray_state_s {
  ray_vtable_t  v;
  ray_handle_t  h;
  ray_req_t     req;
  ray_buf_t     buf;

  lua_State*    L;
  int           flags;
  int           ref;

  ngx_queue_t   rouse;
  ngx_queue_t   queue;

  int           narg;
  ray_data_t    u;
};

int rayM_main_suspend (ray_state_t* self);
int rayM_main_resume  (ray_state_t* self);
int rayM_main_enqueue (ray_state_t* self, ray_state_t* that);
int rayM_main_ready   (ray_state_t* self);

int rayS_init_main(lua_State* L);

uv_loop_t*   rayS_get_loop(lua_State* L);
ray_state_t* rayS_get_self(lua_State* L);
ray_state_t* rayS_get_main(lua_State* L);


ray_state_t* rayS_new (lua_State* L, const char* m, const ray_vtable_t* v);

int rayS_suspend (ray_state_t* self);
int rayS_resume  (ray_state_t* self);
int rayS_enqueue (ray_state_t* self, ray_state_t* that);
int rayS_ready   (ray_state_t* self);
int rayS_send    (ray_state_t* self, ray_state_t* that, int narg);
int rayS_recv    (ray_state_t* self);
int rayS_close   (ray_state_t* self);

int rayS_xcopy (ray_state_t* self, ray_state_t* that, int narg);

#define rayS_dequeue(S) ngx_queue_remove(&(S)->queue)

int rayS_schedule (ray_state_t* self);
int rayS_notify   (ray_state_t* self, int narg);

/* default state methods */
int rayM_state_enqueue (ray_state_t* self, ray_state_t* that);
int rayM_state_send    (ray_state_t* self, ray_state_t* recv, int narg);
int rayM_state_recv    (ray_state_t* self);
int rayM_state_close   (ray_state_t* self);

#endif /* _RAY_STATE_H_ */
