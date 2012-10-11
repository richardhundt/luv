#ifndef _LUV_OBJECT_H_
#define _LUV_OBJECT_H_

#include "luv_hash.h"
#include "luv_list.h"

/* luv objects */
#define LUV_OSTARTED  (1 << 0)
#define LUV_OSTOPPED  (1 << 1)
#define LUV_OWAITING  (1 << 2)
#define LUV_OCLOSING  (1 << 3)
#define LUV_OCLOSED   (1 << 4)
#define LUV_OSHUTDOWN (1 << 5)

#define luvL_object_is_started(O)  ((O)->flags & LUV_OSTARTED)
#define luvL_object_is_stopped(O)  ((O)->flags & LUV_OSTOPPED)
#define luvL_object_is_waiting(O)  ((O)->flags & LUV_OWAITING)
#define luvL_object_is_closing(O)  ((O)->flags & LUV_OCLOSING)
#define luvL_object_is_shutdown(O) ((O)->flags & LUV_OSHUTDOWN)
#define luvL_object_is_closed(O)   ((O)->flags & LUV_OCLOSED)

typedef struct luv_object_s luv_object_t;

luv_object_t* luvL_object_new   (lua_State* L, const char* meta);
luv_object_t* luvL_object_check (lua_State* L, const char* meta);

void luvL_object_init  (luv_state_t* state, luv_object_t* self);
void luvL_object_close (luv_object_t* self);
int  luvL_object_free  (luv_object_t* self);


#define LUV_OBJECT_FIELDS \
  luv_handle_t  h;        \
  int           flags;    \
  int           count;    \
  ngx_queue_t   rouse;    \
  ngx_queue_t   queue;    \
  uv_loop_t*    loop;     \
  luv_hash_t*   hash;     \
  luv_list_t*   list;     \
  void*         data;     \
  int           info;
 
struct luv_object_s {
  LUV_OBJECT_FIELDS;
};

#endif /* _LUV_OBJECT_H_ */

