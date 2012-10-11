#ifndef _LUV_LIST_H_
#define _LUV_LIST_H_

#include <stddef.h>

#include "ngx-queue.h"

typedef struct luv_list_s luv_list_t;

struct luv_list_s {
  ngx_queue_t list;
  void*       data;
};

luv_list_t* luvL_list_new(void* data);

luv_list_t* luvL_list_append(luv_list_t* self, void* data);
luv_list_t* luvL_list_insert(luv_list_t* self, void* data);

/* note this does not free() the item */
void luvL_list_remove(luv_list_t* item);

/* this *does* free the item after removing it */
void luvL_list_delete(luv_list_t* self);

/* frees all members in the list, then frees self */
void luvL_list_free(luv_list_t* self);

#define luvL_list_head(I) \
  ngx_queue_data(ngx_queue_head(&(I)->list), luv_list_t, list)

#define luvL_list_tail(I) \
  ngx_queue_data(ngx_queue_last(&(I)->list), luv_list_t, list)

#define luvL_list_next(I) \
  ngx_queue_data(ngx_queue_next(&(I)->list), luv_list_t, list)

#define luvL_list_prev(I) \
  ngx_queue_data(ngx_queue_prev(&(I)->list), luv_list_t, list)

#define luvL_list_data(I) ((I)->data)

#define luvL_list_empty(I) (ngx_queue_empty(&(I)->list))

#define luvL_list_foreach(i,h) \
  for ((i) = luvL_list_head(h); (i) != (h); (i) = luvL_list_next(i))

void luv__list_self_test(void);

#endif /* _LUV_LIST_H_ */
