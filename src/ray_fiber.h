#ifndef _RAY_FIBER_H_
#define _RAY_FIBER_H_

#include "ray_actor.h"

ray_actor_t* ray_fiber_new(lua_State* L);

void rayM_fiber_init (ray_actor_t* self, int narg);

int rayM_fiber_await (ray_actor_t* self, ray_actor_t* that);
int rayM_fiber_rouse (ray_actor_t* self, ray_actor_t* from);
int rayM_fiber_close (ray_actor_t* self);

LUALIB_API int luaopen_ray_fiber(lua_State* L);

#endif /* _RAY_FIBER_T_ */

