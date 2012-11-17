#ifndef _RAY_QUEUE_H_
#define _RAY_QUEUE_H_

#include "ray_lib.h"
#include "ray_state.h"

typedef struct ray_item_s {
  int    type;
  union {
    ray_buf_t   buf;
    lua_Number  num;
    int         bit;
    void*       ptr;
  } u;
} ray_item_t;

typedef struct ray_queue_s {
  ray_item_t*   data; /* circular queue data */
  size_t*       info; /* tuples in each logical entry */
  size_t        size; /* number of tuples */
  size_t        nval; /* number of items */
  size_t        ncap; /* item capacity */
  unsigned int  head; /* head index into .info */
  unsigned int  tail; /* tail index into .info */
  ngx_queue_t   wput; /* threads waiting to put */
  ngx_queue_t   wget; /* threads waiting to get */
} ray_queue_t;

#define ray_queue_slot(Q,I) ((I) % (Q)->size + 1)
#define ray_queue_head(Q) ray_queue_slot(Q, (Q)->head)
#define ray_queue_tail(Q) ray_queue_slot(Q, (Q)->tail)
#define ray_queue_last(Q) ray_queue_slot(Q, (Q)->head - 1)

#define ray_queue_foreach(i,Q) \
  for (i  = ray_queue_tail(Q); \
       i != ray_queue_head(Q); \
       i  = ray_queue_slot(Q, i))

#define ray_queue_count(Q) ((Q)->head - (Q)->tail)
#define ray_queue_empty(Q) (ray_queue_count(Q) == 0)
#define ray_queue_full(Q)  (ray_queue_count(Q) == (Q)->size)

ray_queue_t* ray_queue_new(size_t size);
int  ray_queue_put(ray_queue_t* self, ray_state_t* curr, int narg);
int  ray_queue_get(ray_queue_t* self, ray_state_t* curr);
void ray_queue_free(ray_queue_t* self);


#endif /* _RAY_QUEUE_H_ */
