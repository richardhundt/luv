#ifndef _RAY_TIMER_H_
#define _RAY_TIMER_H_

#include "ray_lib.h"

int rayM_timer_send (ray_actor_t* self, ray_actor_t* from, int narg);
ray_actor_t* ray_timer_new(lua_State* L);

#define RAY_TIMER_INIT  1
#define RAY_TIMER_STOP  2
#define RAY_TIMER_AGAIN 3
#define RAY_TIMER_START 4
#define RAY_TIMER_CLOSE 5
#define RAY_TIMER_WAIT  6

LUALIB_API int luaopen_ray_timer(lua_State* L);

#endif /* _RAY_TIMER_H_ */
