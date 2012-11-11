#ifndef _RAY_FIBER_H_
#define _RAY_FIBER_H_

#include "ray_actor.h"

/* messages */
#define RAY_FIBER_JOIN 1

/* flags */
#define RAY_FIBER_STARTED (1 << 0)
#define RAY_FIBER_READY   (1 << 1)

ray_actor_t* ray_fiber_new(lua_State* L);

LUALIB_API int luaopen_ray_fiber(lua_State* L);

#endif /* _RAY_FIBER_T_ */

