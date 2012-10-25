#ifndef _RAY_ACTOR_H_
#define _RAY_ACTOR_H_

#include "ray_common.h"
#include "ray_hash.h"
#include "ray_list.h"

#define RAY_LOOP "ray:loop"
#define RAY_MAIN "ray:main"

/* actor flags */
#define RAY_CLOSED (1 << 31)
#define RAY_ACTIVE (1 << 30)
#define RAY_MBDATA (1 << 29)

#define ray_is_closed(A) ((A)->flags & RAY_CLOSED)
#define ray_is_active(A) ((A)->flags & RAY_ACTIVE)

#define ray_dequeue(A) \
  ngx_queue_remove(&(A)->cond); \
  ngx_queue_init(&(A)->cond)

#define ray_enqueue(A,B) \
  ray_dequeue(B); \
  ngx_queue_insert_tail(&(A)->queue, &(B)->cond)


typedef struct ray_actor_s ray_actor_t;

union ray_data_u {
  int           info;
  void*         data;
  ray_hash_t*   hash;
  ray_list_t*   list;
} ray_data_t;

typedef uv_buf_t ray_buf_t;

typedef int (*ray_send_t)(ray_actor_t* self, ray_actor_t* from, int narg);

struct ray_actor_s {
  ray_send_t    send;
  ray_handle_t  h;
  ray_req_t     r;
  uv_thread_t   tid;
  lua_State*    L;   /* mailbox */
  int           flags;
  ngx_queue_t   queue;
  ngx_queue_t   cond;
  ray_buf_t     buf;
  int           ref;
  ray_data_t    u;
};

#define ray_active(A) \
  for ((A)->flags |= RAY_ACTIVE; \
      (A)->flags & RAY_ACTIVE;   \
      (A)->flags &= ~RAY_ACTIVE)

/* control messages to-from the main actor */
#define RAY_DATA  LUA_MULTRET
#define RAY_EVAL  RAY_DATA  - 1
#define RAY_AWAIT RAY_EVAL  - 1
#define RAY_READY RAY_AWAIT - 1
#define RAY_SCHED RAY_READY - 1
#define RAY_CLOSE RAY_SCHED - 1
#define RAY_USER  RAY_CLOSE - 1

int rayM_main_send(ray_actor_t* self, ray_actor_t* from, int narg);

int ray_init_main(lua_State* L);

uv_loop_t*   ray_get_loop(lua_State* L);
ray_actor_t* ray_current(lua_State* L);
ray_actor_t* ray_get_main(lua_State* L);

ray_actor_t* ray_actor_new(lua_State* L, const char* m, const ray_send_t v);

/* send a message to `self' from `from'. The `info' argument may be
either a control message where info < 0 or if info >= 0 then it is
the number of items in the message passed from `from's stack to `self' */
int ray_send (ray_actor_t* self, ray_actor_t* from, int info);

int ray_close(ray_actor_t* self);

/* stack copy utilities */
int ray_push (ray_actor_t* self, int narg);
int ray_xcopy(ray_actor_t* self, ray_actor_t* that, int narg);

/* broadcast nargs from our mailbox to all waiting actors (copies the tuple) */
int ray_notify(ray_actor_t* self, int info);

/* wake one waiting actor and send nargs from our mailbox */
int ray_signal(ray_actor_t* self, int info);

/* call this from __gc to release self->L */
int ray_actor_free(ray_actor_t* self);

#endif /* _RAY_ACTOR_H_ */
