#ifndef _RAY_CHAN_H_
#define _RAY_CHAN_H_

#include "ray_lib.h"
#include "ray_state.h"

#define RAY_CHAN_T "ray.channel.Channel"

typedef struct ray_chan_s {
  int         nval;
  size_t      size;
  int         head;
  int         tail;
  ngx_queue_t wput;
  ngx_queue_t wget;
} ray_chan_t;

ray_chan_t* ray_chan_new();
ray_chan_t* ray_chan_init(ray_chan_t* self);
int  ray_chan_put(ray_chan_t* self, ray_state_t* curr, size_t narg);
int  ray_chan_get(ray_chan_t* self, ray_state_t* curr);
void ray_chan_free(ray_chan_t* self);

#endif /* _RAY_CHAN_H_ */

