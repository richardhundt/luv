#ifndef _RAY_FIBER_H_
#define _RAY_FIBER_H_

#include "ray_state.h"

ray_fiber_t*  rayL_fiber_new (ray_state_t* outer, int narg);

void rayL_fiber_ready  (ray_fiber_t* fiber);
int  rayL_fiber_yield  (ray_fiber_t* fiber, int narg);
int  rayL_fiber_suspend(ray_fiber_t* fiber);
int  rayL_fiber_resume (ray_fiber_t* fiber, int narg);
void rayL_fiber_close  (ray_fiber_t* self);

#endif /* _RAY_FIBER_T_

