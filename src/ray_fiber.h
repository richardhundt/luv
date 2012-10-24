#ifndef _RAY_FIBER_H_
#define _RAY_FIBER_H_

#include "ray_actor.h"

#define RAY_FIBER_START (1 << 0)

ray_actor_t* ray_fiber_new(lua_State* L);

void rayM_fiber_init (ray_actor_t* self, int narg);

int rayM_fiber_send  (ray_actor_t* self, ray_actor_t* from, int info);
int rayM_fiber_close (ray_actor_t* self);

LUALIB_API int luaopen_ray_fiber(lua_State* L);

#endif /* _RAY_FIBER_T_ */

