#ifndef _RAY_HASH_H_
#define _RAY_HASH_H_

typedef struct ray_node_s ray_node_t;

struct ray_node_s {
  const char* key;
  void*       val;
};

typedef struct ray_hash_s {
  ray_node_t* nodes;
  size_t      count;
  size_t      size;
} ray_hash_t;


ray_hash_t* rayL_hash_new (size_t n);

int   rayL_hash_init   (ray_hash_t* self, size_t size);
void  rayL_hash_free   (ray_hash_t* self);
int   rayL_hash_insert (ray_hash_t* self, const char* key, void* val);
int   rayL_hash_update (ray_hash_t* self, const char* key, void* val);
void* rayL_hash_lookup (ray_hash_t* self, const char* key);
void* rayL_hash_remove (ray_hash_t* self, const char* key);
void  rayL_hash_rehash (ray_hash_t* self);

/* insert if not there, otherwise update */
int rayL_hash_set (ray_hash_t* self, const char* key, void* val);

#define rayL_hash_get(H,K) (rayL_hash_lookup(H,K))

#define rayL_hash_empty(H) ((H)->count == 0)

ray_node_t* rayL_hash_next (ray_hash_t* self, ray_node_t* prev);

#define rayL_hash_foreach(n,H) \
  for ((n) = rayL_hash_next((H), NULL); (n); (n) = rayL_hash_next(H,n))

void ray__hash_self_test(void);

#endif /* _RAY_HASH_H_ */
