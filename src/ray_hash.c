/* ============================================================================
   A tiny fixed size probing hash table - the goal is to provide fast access
   to a limited number of entries. Not a general purpose hash table. Use Lua's
   tables for that. This should be for storing named callbacks, etc.

   Also note that it only deals with C style strings. So make sure they're
   null terminated.

   On a 1.7GHz i5 CPU this can do about 45 million lookups/updates per second 
   on 8 byte keys when compiled with -O1
   ============================================================================
*/

#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
 
#include "ray_hash.h"

#define streq(s1,s2)  (!strcmp ((s1), (s2)))

static unsigned int _node_hash(const char *key, size_t limit) {
  unsigned int key_hash = 0;
  while (*key) {
    key_hash *= 33;
    key_hash += *key;
    key++;
  }
  key_hash %= limit;
  return key_hash;
}

ray_hash_t* rayL_hash_new(size_t size) {
  ray_hash_t* self = (ray_hash_t*)calloc(1, sizeof(ray_hash_t));
  if (!self) return NULL;
  self->size  = 0;
  self->count = 0;
  self->nodes = NULL;
  if (size) {
    if (rayL_hash_init(self, size)) {
      return NULL;
    }
  }
  return self;
}

int rayL_hash_init(ray_hash_t* self, size_t size) {
  assert(!self->size);
  self->size  = size;
  self->nodes = (ray_node_t*)calloc(size + 1, sizeof(ray_node_t));
  if (!self->nodes) return -1;
  self->count = 0;
  return 0;
}

ray_node_t* rayL_hash_next(ray_hash_t* self, ray_node_t* n) {
  size_t s = self->size;
  size_t i;
  unsigned int h;
  if (n) {
    h = _node_hash(n->key, s);
    n = n + 1;
  }
  else {
    h = 0;
    n = self->nodes;
  }
  for (i = h; i < s; i++) {
    if (n->key) return n;
    ++n;
  }
  return NULL;
}

int rayL_hash_insert(ray_hash_t* self, const char* key, void* val) {
  assert(key);
  if (self->count >= self->size) return -1;
  unsigned int hash = _node_hash(key, self->size);
  ray_node_t*  next = &self->nodes[hash];
  if (next->key) { /* collision, so probe */
    size_t i, size;
    size = self->size;
    for (i = 0; i < size; i++) {
      next = &self->nodes[++hash % size];
      if (!next->key) break; /* okay, we've found a free slot */
    }
  }

  ++self->count;
  next->key = key;
  next->val = val;
  return 0;
}

int rayL_hash_update(ray_hash_t* self, const char* key, void* val) {
  assert(key);

  unsigned int hash = _node_hash(key, self->size);
  ray_node_t*  node = &self->nodes[hash];

  if (node->key && streq(node->key, key)) {
    node->val = val;
    return 0;
  }
  else {
    /* key taken, but not ours, so probe */
    size_t i, size;
    size = self->size;
    for (i = 0; i < size; i++) {
      node = &self->nodes[++hash % size];
      if (node->key && streq(node->key, key)) {
        node->val = val;
        return 0;
      }
    }
  }

  return -1;
}

int rayL_hash_set(ray_hash_t* self, const char* key, void* val) {
  if (rayL_hash_update(self, key, val) == -1) {
    return rayL_hash_insert(self, key, val);
  }
  return 0;
}

void* rayL_hash_lookup(ray_hash_t* self, const char* key) {
  unsigned int hash = _node_hash(key, self->size);
  ray_node_t*  node = &self->nodes[hash];
  if (node->key && streq(node->key, key)) {
    return node->val;
  }
  else {
    size_t i, size;
    size = self->size;
    for (i = 0; i < size; i++) {
      node = &self->nodes[++hash % size];
      if (node->key && streq(node->key, key)) {
        return node->val;
      }
    }
    /* not found */
  }
  return NULL;
}

void rayL_hash_rehash(ray_hash_t* self) {
  size_t i, size, count;
  ray_node_t* base = self->nodes;
  ray_node_t* p = base;
  size  = self->size;
  count = self->count;
  self->nodes = NULL;
  self->count = 0;
  self->size  = 0;
  rayL_hash_init(self, size);
  for (i = 1; i < size; p++, i++) {
    if (p->key) {
      rayL_hash_insert(self, p->key, p->val);
    }
  }
  free(base);
}

void* rayL_hash_remove(ray_hash_t* self, const char* key) {
  unsigned int hash = _node_hash(key, self->size);
  ray_node_t*  node = &self->nodes[hash];
  void* val = NULL;
  if (node->key && streq(node->key, key)) {
    --self->count;
    val = node->val;
    node->key = NULL;
    node->val = NULL;
  }
  else {
    size_t i, size;
    size = self->size;
    for (i = 0; i < size; i++) {
      node = &self->nodes[++hash % size];
      if (node->key && streq(node->key, key)) {
        --self->count;
        val = node->val;
        node->key = NULL;
        node->val = NULL;
        break;
      }
    }
  }

  return val;
}

size_t rayL_hash_size(ray_hash_t* self) {
  return self->count;
}

void rayL_hash_free(ray_hash_t* self) {
  if (self->size) free(self->nodes);
  free(self);
}

void ray__hash_self_test(void) {
  printf(" * ray_hash: ");

  int rc;
  ray_hash_t* hash = rayL_hash_new(32);
  assert(hash);
  rc = rayL_hash_insert(hash, "DEADBEEF", (void*)0xDEADBEEF);
  assert(rc == 0);
  rc = rayL_hash_insert(hash, "ABADCAFE", (void*)0xABADCAFE);
  assert(rc == 0);
  rc = rayL_hash_insert(hash, "C0DEDBAD", (void*)0xC0DEDBAD);
  assert(rc == 0);
  rc = rayL_hash_insert(hash, "DEADF00D", (void*)0xDEADF00D);
  assert(rc == 0);
  assert(rayL_hash_size(hash) == 4);

  void* v;
  v = rayL_hash_lookup(hash, "DEADBEEF");
  assert(v == (void*)0xDEADBEEF);
  v = rayL_hash_lookup(hash, "ABADCAFE");
  assert(v == (void*)0xABADCAFE);
  v = rayL_hash_lookup(hash, "C0DEDBAD");
  assert(v == (void*)0xC0DEDBAD);
  v = rayL_hash_lookup(hash, "DEADF00D");
  assert(v == (void*)0xDEADF00D);

  assert(rayL_hash_size(hash) == 4);

  ray_node_t* n;
  size_t count = 0;
  rayL_hash_foreach(n, hash) {
    assert(n->key);
    assert(n->val);
    ++count;
  }
  assert(count == rayL_hash_size(hash));

  rayL_hash_update(hash, "DEADBEEF", (void*)0xB00BCAFE);
  v = rayL_hash_lookup(hash, "DEADBEEF");
  assert(v == (void*)0xB00BCAFE);

  rayL_hash_remove(hash, "DEADBEEF");
  assert(rayL_hash_size(hash) == 3);
  assert(!rayL_hash_lookup(hash, "DEADBEEF"));

  rayL_hash_free(hash);
  hash = rayL_hash_new(2);

  /* collisions */
  rayL_hash_insert(hash, "ab", (void*)0xAAAAAAAA);
  rayL_hash_insert(hash, "ba", (void*)0xBBBBBBBB);
  assert(rayL_hash_lookup(hash, "ab") == (void*)0xAAAAAAAA);
  assert(rayL_hash_lookup(hash, "ba") == (void*)0xBBBBBBBB);

  rayL_hash_remove(hash, "ab");
  assert(rayL_hash_lookup(hash, "ba") == (void*)0xBBBBBBBB);

  rayL_hash_set(hash, "ba", (void*)0xDEADBEEF);
  assert(rayL_hash_get(hash, "ba") == (void*)0xDEADBEEF);

  printf("OK\n");
}

#undef streq

