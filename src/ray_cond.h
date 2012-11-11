#ifndef _RAY_COND_H_
#define _RAY_COND_H_

#include "ray_lib.h"
#include "ray_actor.h"

typedef struct ray_cond_s ray_cond_t;

struct ray_cond_s {
  ngx_queue_t queue;
};

ray_cond_t* ray_cond_new(lua_State* L);

int ray_cond_wait   (ray_cond_t* self, ray_actor_t* curr);
int ray_cond_signal (ray_cond_t* self, ray_actor_t* from, ray_msg_t msg);

void ray_cond_free (ray_cond_t* self);

#endif /* _RAY_COND_H_ */
