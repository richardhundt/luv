#ifndef _RAY_COND_H_
#define _RAY_COND_H_

#include "ray_lib.h"
#include "ray_state.h"

typedef struct ray_cond_s ray_cond_t;

struct ray_cond_s {
  ngx_queue_t queue;
};

ray_cond_t* ray_cond_new();

int ray_cond_wait   (ray_cond_t* self, ray_state_t* curr);
int ray_cond_signal (ray_cond_t* self, ray_state_t* from, int narg);

void ray_cond_free (ray_cond_t* self);

#endif /* _RAY_COND_H_ */
