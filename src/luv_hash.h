#ifndef _LUV_HASH_H_
#define _LUV_HASH_H_

typedef struct luv_node_s luv_node_t;

struct luv_node_s {
  const char* key;
  void*       val;
};

typedef struct luv_hash_s {
  luv_node_t* nodes;
  size_t      count;
  size_t      size;
} luv_hash_t;


luv_hash_t* luvL_hash_new (size_t n);

int   luvL_hash_init   (luv_hash_t* self, size_t size);
void  luvL_hash_free   (luv_hash_t* self);
int   luvL_hash_insert (luv_hash_t* self, const char* key, void* val);
int   luvL_hash_update (luv_hash_t* self, const char* key, void* val);
void* luvL_hash_lookup (luv_hash_t* self, const char* key);
void* luvL_hash_remove (luv_hash_t* self, const char* key);
void  luvL_hash_rehash (luv_hash_t* self);

/* insert if not there, otherwise update */
int luvL_hash_set (luv_hash_t* self, const char* key, void* val);

#define luvL_hash_get(H,K) (luvL_hash_lookup(H,K))

#define luvL_hash_empty(H) ((H)->count == 0)

luv_node_t* luvL_hash_next (luv_hash_t* self, luv_node_t* prev);

#define luvL_hash_foreach(n,H) \
  for ((n) = luvL_hash_next((H), NULL); (n); (n) = luvL_hash_next(H,n))

void luv__hash_self_test(void);

#endif /* _LUV_HASH_H_ */
