#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>

#include "luv_list.h"
#include "ngx-queue.h"

luv_list_t* luvL_list_new(void* data) {
  luv_list_t* self = (luv_list_t*)malloc(sizeof(luv_list_t));
  ngx_queue_init(&self->list);
  self->data = data;
  return self;
}

luv_list_t* luvL_list_append(luv_list_t* self, void* data) {
  luv_list_t* item = luvL_list_new(data);
  ngx_queue_insert_tail(&self->list, &item->list);
  return item;
}

luv_list_t* luvL_list_insert(luv_list_t* self, void* data) {
  luv_list_t* item = luvL_list_new(data);
  ngx_queue_insert_head(&self->list, &item->list);
  return item;
}

void luvL_list_remove(luv_list_t* item) {
  ngx_queue_remove(&item->list);
  ngx_queue_init(&item->list);
}

void luvL_list_delete(luv_list_t* item) {
  ngx_queue_remove(&item->list);
  free(item);
}

void luvL_list_free(luv_list_t* self) {
  luv_list_t* i;
  while (!luvL_list_empty(self)) {
    i = luvL_list_head(self);
    luvL_list_delete(i);
  }
  free(self);
}

void luv__list_self_test(void) {
  printf(" * luv_list: ");

  luv_list_t* list = luvL_list_new((void*)0xDEADBEEF);
  assert(list);
  assert(luvL_list_empty(list));
  assert(list->data == (void*)0xDEADBEEF);
  assert(luvL_list_head(list) == list);
  assert(luvL_list_tail(list) == list);
  assert(luvL_list_data(list) == (void*)0xDEADBEEF);

  luv_list_t* item = luvL_list_append(list, (void*)0xC0DEDBAD);
  assert(!luvL_list_empty(list));
  assert(!luvL_list_empty(item));

  luvL_list_delete(item);
  assert(luvL_list_empty(list));

  luvL_list_insert(list, (void*)0xDEADF00D);
  assert(!luvL_list_empty(list));

  luv_list_t* l;
  size_t seen = 0;
  luvL_list_foreach(l,list) {
    seen++;
  }

  assert(seen == 1);
  luvL_list_free(list);
  printf("OK\n");
}
 
