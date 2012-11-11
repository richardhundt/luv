#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "ray_list.h"
#include "ngx-queue.h"

ray_list_t* ray_list_new(void* data) {
  ray_list_t* self = (ray_list_t*)malloc(sizeof(ray_list_t));
  ngx_queue_init(&self->list);
  self->data = data;
  return self;
}

ray_list_t* ray_list_insert_tail(ray_list_t* self, ray_list_t* item) {
  ngx_queue_insert_tail(&self->list, &item->list);
  return item;
}

ray_list_t* ray_list_insert_head(ray_list_t* self, ray_list_t* item) {
  ngx_queue_insert_head(&self->list, &item->list);
  return item;
}

ray_list_t* ray_list_remove(ray_list_t* item) {
  ngx_queue_remove(&item->list);
  ngx_queue_init(&item->list);
  return item;
}

void ray_list_free(ray_list_t* self) {
  ray_list_t* i;
  while (!ray_list_empty(self)) {
    i = ray_list_head(self);
    ngx_queue_remove(&i->list);
    free(i);
  }
  free(self);
}

void ray__list_self_test(void) {
  printf(" * ray_list: ");

  ray_list_t* list = ray_list_new((void*)0xDEADBEEF);
  assert(list);
  assert(ray_list_empty(list));
  assert(list->data == (void*)0xDEADBEEF);
  assert(ray_list_head(list) == list);
  assert(ray_list_tail(list) == list);
  assert(list->data == (void*)0xDEADBEEF);

  ray_list_t* item = ray_list_new((void*)0xC0DEDBAD);
  ray_list_insert_tail(list, item);

  assert(!ray_list_empty(list));
  assert(!ray_list_empty(item));

  ray_list_remove(item);
  assert(ray_list_empty(list));

  ray_list_insert_tail(list, ray_list_new((void*)0xDEADF00D));
  assert(!ray_list_empty(list));

  ray_list_t* l;
  size_t seen = 0;
  ray_list_foreach(l,list) {
    seen++;
  }

  assert(seen == 1);
  ray_list_free(list);
  printf("OK\n");
}

