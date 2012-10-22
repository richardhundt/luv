#ifndef _RAY_ACTOR_H_
#define _RAY_ACTOR_H_

#include "ray_common.h"
#include "ray_hash.h"
#include "ray_list.h"

#define RAY_LOOP "ray:loop"
#define RAY_MAIN "ray:main"

/* actor flags */
#define RAY_CLOSED (1 << 31)

#define ray_is_closed(A) ((A)->flags & RAY_CLOSED)
#define ray_is_active(A) ngx_queue_empty(&(A)->cond)

#define ray_dequeue(A) \
  ngx_queue_remove(&(A)->cond); \
  ngx_queue_init(&(A)->cond)

#define ray_enqueue(A,B) \
  ray_dequeue(B); \
  ngx_queue_insert_tail(&(A)->queue, &(B)->cond)

#define RAY_MAIN_ACTIVE (1 << 0)

typedef struct ray_actor_s ray_actor_t;

typedef union ray_data_u {
  void*       data;
  ray_hash_t* hash;
  ray_list_t* list;
} ray_data_t;

typedef uv_buf_t ray_buf_t;

typedef struct ray_vtable_s {
  int (*send )(ray_actor_t* self, ray_actor_t* from, int narg);
  int (*recv )(ray_actor_t* self, ray_actor_t* that);
  int (*close)(ray_actor_t* self);
} ray_vtable_t;

struct ray_actor_s {
  ray_vtable_t  v;
  ray_handle_t  h;
  ray_req_t     r;
  lua_State*    L;   /* mailbox */
  uv_thread_t   tid;
  int           flags;
  ngx_queue_t   queue;
  ngx_queue_t   cond;
  ray_buf_t     buf;
  int           ref;
  ray_data_t    u;
};

int rayM_main_recv(ray_actor_t* self, ray_actor_t* that);
int rayM_main_send(ray_actor_t* self, ray_actor_t* from, int narg);

int ray_init_main(lua_State* L);

uv_loop_t*   ray_get_loop(lua_State* L);
ray_actor_t* ray_get_self(lua_State* L);
ray_actor_t* ray_get_main(lua_State* L);

ray_actor_t* ray_actor_new(lua_State* L, const char* m, const ray_vtable_t* v);

/* wait for from to send us a message */
int ray_recv (ray_actor_t* self, ray_actor_t* from);

/* send nargs from `from's mailbox, and notify self */
int ray_send (ray_actor_t* self, ray_actor_t* from, int narg);

int ray_close(ray_actor_t* self);

/* stack copy utilities */
int ray_push (ray_actor_t* self, int narg);
int ray_xcopy(ray_actor_t* self, ray_actor_t* that, int narg);

/* broadcast nargs from our mailbox to all waiting actors (copies the tuple) */
int ray_notify(ray_actor_t* self, int narg);

/* wake one waiting actor and send nargs from our mailbox */
int ray_signal(ray_actor_t* self, int narg);

/* call this from __gc to release self->L */
int ray_actor_free(ray_actor_t* self);

#endif /* _RAY_ACTOR_H_ */
