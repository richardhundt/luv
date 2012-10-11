#ifndef _RAY_COND_H_
#define _RAY_COND_H_

#include "ray_common.h"

typedef ngx_queue_t ray_cond_t;

int rayL_cond_init      (ray_cond_t* cond);
int rayL_cond_wait      (ray_cond_t* cond, ray_state_t* curr);
int rayL_cond_signal    (ray_cond_t* cond);
int rayL_cond_broadcast (ray_cond_t* cond);

#endif /* _RAY_COND_H_ */
