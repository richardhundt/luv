#ifndef _RAY_ACTOR_H_
#define _RAY_ACTOR_H_

#include "ray_common.h"

#include "ray_hash.h"
#include "ray_list.h"
#include "ray_queue.h"

#define RAY_LOOP "ray:loop"
#define RAY_MAIN "ray:main"
#define RAY_WEAK "ray:weak"

/* initial mailbox queue size */
#define RAY_MBOX_SIZE 16

/* actor flags */
#define RAY_CLOSED (1 << 31)
#define RAY_ACTIVE (1 << 30)

#define ray_is_closed(A) ((A)->flags & RAY_CLOSED)
#define ray_is_active(A) ((A)->flags & RAY_ACTIVE)

typedef uv_buf_t ray_buf_t;

typedef int32_t ray_msg_t;

typedef struct ray_actor_s ray_actor_t;

typedef int (*ray_react_t)(ray_actor_t* self, ray_actor_t* from, ray_msg_t msg);
typedef int (*ray_yield_t)(ray_actor_t* self);
typedef int (*ray_close_t)(ray_actor_t* self);

typedef struct ray_vtable_s {
  ray_react_t react;
  ray_yield_t yield;
  ray_close_t close;
} ray_vtable_t;

typedef union ray_data_u {
  ray_hash_t* hash;
  ray_list_t* list;
  void*       data;
} ray_data_t;

struct ray_actor_s {
  /* behavior */
  ray_vtable_t  v;

  /* identity */
  int           id;
  uv_thread_t   tid;

  /* mailbox and scratch stack */
  ray_queue_t   mbox;
  lua_State*    L;
  int           L_ref;

  /* monitor */
  uv_mutex_t    mtx;

  /* state information */
  int           flags;

  /* libuv interaction */
  ray_handle_t  h;
  ray_req_t     r;
  ray_buf_t     buf;

  /* reference counting */
  int           ref_count;
  int           ref;

  /* conditional queue handle */
  ngx_queue_t   cond;

  /* user data */
  ray_data_t    u;
};

#define ray_active(A) \
  for ((A)->flags |= RAY_ACTIVE; \
      (A)->flags & RAY_ACTIVE;   \
      (A)->flags &= ~RAY_ACTIVE)

#define RAY_DATA_MASK (0xFFFFFF00)
#define RAY_TYPE_MASK (0x000000FF)

#define RAY_DATA_BIAS 255
#define RAY_TYPE_BIAS 127

#define ray_msg_data(M) ((((M) & RAY_DATA_MASK) >> 8) - RAY_DATA_BIAS)
#define ray_msg_type(M) (((M) & RAY_TYPE_MASK) - RAY_TYPE_BIAS)
#define ray_msg(T,D) \
  (((T) + RAY_TYPE_BIAS) | ((((D) + RAY_DATA_BIAS) << 8) & RAY_DATA_MASK))

/* system message types */
#define RAY_YIELD LUA_YIELD
#define RAY_DATA  LUA_MULTRET
#define RAY_ERROR RAY_DATA  - 1
#define RAY_ABORT RAY_ERROR - 1
#define RAY_ASYNC RAY_ABORT - 1

/* default actor metatable */
#define RAY_ACTOR_T "ray.actor.Actor"

int ray_init_main(lua_State* L);

uv_loop_t*   ray_get_loop(lua_State* L);
ray_actor_t* ray_get_main(lua_State* L);
ray_actor_t* ray_current (lua_State* L);

ray_actor_t* ray_actor_new(lua_State* L, const char* m, const ray_vtable_t* v);

int ray_yield(ray_actor_t* self);
int ray_close(ray_actor_t* self);
int ray_react(ray_actor_t* self, ray_actor_t* from, ray_msg_t msg);

int ray_send(ray_actor_t* self, ray_actor_t* from, ray_msg_t msg);
int ray_recv(ray_actor_t* self);

/* stack utilities */
int  ray_xcopy (ray_actor_t* self, ray_actor_t* that, int narg);
int  ray_push  (ray_actor_t* self, int narg);

void ray_push_actor (lua_State* L, ray_actor_t* actor);
void ray_push_self  (ray_actor_t* self);

void ray_incref(ray_actor_t* self);
void ray_decref(ray_actor_t* self);

/* call this from __gc to release self->L */
int ray_free(ray_actor_t* self);

ray_msg_t ray_mailbox_get  (ray_actor_t* self);
void      ray_mailbox_put  (ray_actor_t* self, lua_State* L, ray_msg_t msg);
int       ray_mailbox_empty(ray_actor_t* self);

/* these do refcounting on actors */
int ray_hash_set_actor(ray_hash_t* self, const char* key, ray_actor_t* val);
ray_actor_t* ray_hash_remove_actor(ray_hash_t* self, const char* key);
#define ray_hash_get_actor(H,K) ((ray_actor_t*)ray_hash_get(H,K))

void ray_queue_put_actor(ray_queue_t* self, ray_actor_t* item);
ray_actor_t* ray_queue_get_actor(ray_queue_t* self);

#endif /* _RAY_ACTOR_H_ */
