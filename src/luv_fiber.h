#ifndef _LUV_FIBER_H_
#define _LUV_FIBER_H_

#include "luv_state.h"

luv_fiber_t*  luvL_fiber_new (luv_state_t* outer, int narg);

void luvL_fiber_ready  (luv_fiber_t* fiber);
int  luvL_fiber_yield  (luv_fiber_t* fiber, int narg);
int  luvL_fiber_suspend(luv_fiber_t* fiber);
int  luvL_fiber_resume (luv_fiber_t* fiber, int narg);
void luvL_fiber_close  (luv_fiber_t* self);

#endif /* _LUV_FIBER_T_

