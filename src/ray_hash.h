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


ray_hash_t* ray_hash_new (size_t n);

int   ray_hash_init   (ray_hash_t* self, size_t size);
void  ray_hash_free   (ray_hash_t* self);
int   ray_hash_insert (ray_hash_t* self, const char* key, void* val);
int   ray_hash_update (ray_hash_t* self, const char* key, void* val);
void* ray_hash_lookup (ray_hash_t* self, const char* key);
void* ray_hash_remove (ray_hash_t* self, const char* key);
void  ray_hash_rehash (ray_hash_t* self);

/* insert if not there, otherwise update */
int ray_hash_set (ray_hash_t* self, const char* key, void* val);

#define ray_hash_get(H,K) (ray_hash_lookup(H,K))

#define ray_hash_empty(H) ((H)->count == 0)

ray_node_t* ray_hash_next (ray_hash_t* self, ray_node_t* prev);

#define ray_hash_foreach(n,H) \
  for ((n) = ray_hash_next((H), NULL); (n); (n) = ray_hash_next(H,n))

void ray__hash_self_test(void);

#endif /* _RAY_HASH_H_ */
