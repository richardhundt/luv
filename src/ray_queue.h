#ifndef _RAY_QUEUE_H_
#define _RAY_QUEUE_H_

#include "ray_lib.h"

typedef struct ray_queue_s {
  lua_State*  L;
  int         L_ref;
  int         head;
  int         tail;
  size_t      size;
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

ray_queue_t* ray_queue_new(lua_State* L, int size);
void ray_queue_init(ray_queue_t* self, lua_State* L, size_t size);

void ray_queue_put (ray_queue_t* self);
int  ray_queue_get (ray_queue_t* self);
void ray_queue_free(ray_queue_t* self);

void ray_queue_put_value(ray_queue_t* self, lua_State* L);
int  ray_queue_get_value(ray_queue_t* self, lua_State* L);

void ray_queue_put_tuple(ray_queue_t* self, lua_State* L, int nval);
int  ray_queue_get_tuple(ray_queue_t* self, lua_State* L);

void ray_queue_put_number(ray_queue_t* self, lua_Number val);
lua_Number ray_queue_get_number(ray_queue_t* self);

void ray_queue_put_integer(ray_queue_t* self, lua_Integer val);
lua_Integer ray_queue_get_integer(ray_queue_t* self);

void ray_queue_put_boolean(ray_queue_t* self, int val);
int  ray_queue_get_boolean(ray_queue_t* self);

void ray_queue_put_string(ray_queue_t* self, const char* str);
const char* ray_queue_get_string(ray_queue_t* self);

void ray_queue_put_lstring(ray_queue_t* self, const char* str, size_t len);
const char* ray_queue_get_lstring(ray_queue_t* self, size_t* len);

void ray_queue_put_cfunction(ray_queue_t* self, lua_CFunction fun);
lua_CFunction ray_queue_get_cfunction(ray_queue_t* self);

int ray_queue_peek(ray_queue_t* self, int idx);
int ray_queue_peek_value(ray_queue_t* self, lua_State* L, int idx);

#endif /* _RAY_QUEUE_H_ */
