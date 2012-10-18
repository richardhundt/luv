#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "ray_list.h"
#include "ngx-queue.h"

ray_list_t* rayL_list_new(void* data) {
  ray_list_t* self = (ray_list_t*)malloc(sizeof(ray_list_t));
  ngx_queue_init(&self->list);
  self->data = data;
  return self;
}

ray_list_t* rayL_list_append(ray_list_t* self, void* data) {
  ray_list_t* item = rayL_list_new(data);
  ngx_queue_insert_tail(&self->list, &item->list);
  return item;
}

ray_list_t* rayL_list_insert(ray_list_t* self, void* data) {
  ray_list_t* item = rayL_list_new(data);
  ngx_queue_insert_head(&self->list, &item->list);
  return item;
}

void rayL_list_remove(ray_list_t* item) {
  ngx_queue_remove(&item->list);
  ngx_queue_init(&item->list);
}

void rayL_list_delete(ray_list_t* item) {
  ngx_queue_remove(&item->list);
  free(item);
}

void rayL_list_free(ray_list_t* self) {
  ray_list_t* i;
  while (!rayL_list_empty(self)) {
    i = rayL_list_head(self);
    rayL_list_delete(i);
  }
  free(self);
}

void ray__list_self_test(void) {
  printf(" * ray_list: ");

  ray_list_t* list = rayL_list_new((void*)0xDEADBEEF);
  assert(list);
  assert(rayL_list_empty(list));
  assert(list->data == (void*)0xDEADBEEF);
  assert(rayL_list_head(list) == list);
  assert(rayL_list_tail(list) == list);
  assert(rayL_list_data(list) == (void*)0xDEADBEEF);

  ray_list_t* item = rayL_list_append(list, (void*)0xC0DEDBAD);
  assert(!rayL_list_empty(list));
  assert(!rayL_list_empty(item));

  rayL_list_delete(item);
  assert(rayL_list_empty(list));

  rayL_list_insert(list, (void*)0xDEADF00D);
  assert(!rayL_list_empty(list));

  ray_list_t* l;
  size_t seen = 0;
  rayL_list_foreach(l,list) {
    seen++;
  }

  assert(seen == 1);
  rayL_list_free(list);
  printf("OK\n");
}
 
