#ifndef LUV_OBJECT_H
#define LUV_OBJECT_H

#include "luv_core.h"

#define LUV_OBJ_STARTED (1 << 0);
#define LUV_OBJ_STOPPED (1 << 1);
#define LUV_OBJ_CLOSING (1 << 2);
#define LUV_OBJ_CLOSED  (1 << 3);

typedef struct luv_const_reg_s {
  const char*   key;
  int           val;
} luv_const_reg_t;

typedef struct luv_object_s {
  luv_handle_t    h;
  uv_buf_t        buf;
  ngx_queue_t     rouse;
  ngx_queue_t     queue;
  luv_sched_t*    sched;
  int             flags;
  void*           data;
} luv_object_t;

void luv__object_init(luv_sched_t* sched, luv_object_t* self);
void luv__object_close(luv_object_t* self);

#endif /* LUV_OBJECT_H */
