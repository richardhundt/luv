#ifndef _RAY_FIBER_H_
#define _RAY_FIBER_H_

#include "ray_actor.h"

#define RAY_FIBER_START   (1 << 0)
#define RAY_FIBER_ACTIVE  (1 << 1)
#define RAY_FIBER_SUSPEND (1 << 2)
#define RAY_FIBER_DEAD    (1 << 3)

#define ray_fiber_active(A) \
  for ((A)->flags |= RAY_FIBER_ACTIVE; \
      (A)->flags & RAY_FIBER_ACTIVE;   \
      (A)->flags &= ~RAY_FIBER_ACTIVE)

/* for detecting coroutine.yield */
#define ray_fiber_is_active(F) ((F)->flags & RAY_FIBER_ACTIVE)

ray_actor_t* ray_fiber_new(lua_State* L);

void rayM_fiber_init (ray_actor_t* self, int narg);

int rayM_fiber_recv  (ray_actor_t* self, ray_actor_t* that);
int rayM_fiber_send  (ray_actor_t* self, ray_actor_t* from, int narg);
int rayM_fiber_close (ray_actor_t* self);

LUALIB_API int luaopen_ray_fiber(lua_State* L);

#endif /* _RAY_FIBER_T_ */

