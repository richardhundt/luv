#ifndef _RAY_LIST_H_
#define _RAY_LIST_H_

#include <stddef.h>

#include "ngx-queue.h"

typedef struct ray_list_s ray_list_t;

struct ray_list_s {
  ngx_queue_t list;
  void*       data;
};

ray_list_t* rayL_list_new(void* data);

ray_list_t* rayL_list_append(ray_list_t* self, void* data);
ray_list_t* rayL_list_insert(ray_list_t* self, void* data);

/* note this does not free() the item */
void rayL_list_remove(ray_list_t* item);

/* this *does* free the item after removing it */
void rayL_list_delete(ray_list_t* self);

/* frees all members in the list, then frees self */
void rayL_list_free(ray_list_t* self);

#define rayL_list_head(I) \
  ngx_queue_data(ngx_queue_head(&(I)->list), ray_list_t, list)

#define rayL_list_tail(I) \
  ngx_queue_data(ngx_queue_last(&(I)->list), ray_list_t, list)

#define rayL_list_next(I) \
  ngx_queue_data(ngx_queue_next(&(I)->list), ray_list_t, list)

#define rayL_list_prev(I) \
  ngx_queue_data(ngx_queue_prev(&(I)->list), ray_list_t, list)

#define rayL_list_data(I) ((I)->data)

#define rayL_list_empty(I) (ngx_queue_empty(&(I)->list))

#define rayL_list_foreach(i,h) \
  for ((i) = rayL_list_head(h); (i) != (h); (i) = rayL_list_next(i))

void ray__list_self_test(void);

#endif /* _RAY_LIST_H_ */
