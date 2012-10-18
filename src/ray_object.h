#ifndef _RAY_OBJECT_H_
#define _RAY_OBJECT_H_

#include "ray_hash.h"
#include "ray_list.h"

/* ray objects */
#define RAY_OSTARTED  (1 << 0)
#define RAY_OSTOPPED  (1 << 1)
#define RAY_OWAITING  (1 << 2)
#define RAY_OCLOSING  (1 << 3)
#define RAY_OCLOSED   (1 << 4)
#define RAY_OSHUTDOWN (1 << 5)

#define rayL_object_is_started(O)  ((O)->flags & RAY_OSTARTED)
#define rayL_object_is_stopped(O)  ((O)->flags & RAY_OSTOPPED)
#define rayL_object_is_waiting(O)  ((O)->flags & RAY_OWAITING)
#define rayL_object_is_closing(O)  ((O)->flags & RAY_OCLOSING)
#define rayL_object_is_shutdown(O) ((O)->flags & RAY_OSHUTDOWN)
#define rayL_object_is_closed(O)   ((O)->flags & RAY_OCLOSED)

typedef struct ray_object_s ray_object_t;

ray_object_t* rayL_object_new   (lua_State* L, const char* meta);
ray_object_t* rayL_object_check (lua_State* L, const char* meta);

void rayL_object_init  (ray_actor_t* state, ray_object_t* self);
void rayL_object_close (ray_object_t* self);
int  rayL_object_free  (ray_object_t* self);

#define RAY_OBJECT_FIELDS \
  ray_handle_t  h;        \
  int           flags;    \
  int           count;    \
  uv_buf_t      buf;      \
  ngx_queue_t   rouse;    \
  ngx_queue_t   queue;    \
  uv_loop_t*    loop;     \
  ray_hash_t*   hash;     \
  ray_list_t*   list;     \
  void*         data;     \
  int           info;
 
struct ray_object_s {
  RAY_OBJECT_FIELDS;
};

#define rayL_object_self(H) container_of(H, ray_object_t, h)

#endif /* _RAY_OBJECT_H_ */

