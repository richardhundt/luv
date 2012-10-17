#ifndef _RAY_FIBER_H_
#define _RAY_FIBER_H_

#include "ray_state.h"

ray_state_t* rayM_fiber_new(lua_State* L);

void rayM_fiber_init (ray_state_t* self, int narg);

int rayM_fiber_await (ray_state_t* self, ray_state_t* that);
int rayM_fiber_rouse (ray_state_t* self, ray_state_t* from);
int rayM_fiber_close (ray_state_t* self);

LUALIB_API int luaopen_ray_fiber(lua_State* L);

#endif /* _RAY_FIBER_T_ */

