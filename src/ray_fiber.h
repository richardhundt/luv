#ifndef _RAY_FIBER_H_
#define _RAY_FIBER_H_

#include "ray_state.h"

ray_state_t* rayM_fiber_new(lua_State* L);

void rayM_fiber_init (ray_state_t* self, int narg);

int rayM_fiber_suspend (ray_state_t* self);
int rayM_fiber_resume  (ray_state_t* self);
int rayM_fiber_ready   (ray_state_t* self);
int rayM_fiber_enqueue (ray_state_t* self, ray_state_t* that);
int rayM_fiber_send    (ray_state_t* self, ray_state_t* recv, int narg);
int rayM_fiber_recv    (ray_state_t* self);
int rayM_fiber_close   (ray_state_t* self);

LUALIB_API int luaopen_ray_fiber(lua_State* L);

#endif /* _RAY_FIBER_T_ */

