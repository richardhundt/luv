#ifndef _RAY_TIMER_H_
#define _RAY_TIMER_H_

#include "ray_lib.h"

int rayM_timer_send (ray_actor_t* self, ray_actor_t* from, int narg);
int rayM_timer_close (ray_actor_t* self);

#define RAY_TIMER_INIT  RAY_USER
#define RAY_TIMER_STOP  RAY_TIMER_INIT  - 1
#define RAY_TIMER_AGAIN RAY_TIMER_STOP  - 1
#define RAY_TIMER_START RAY_TIMER_AGAIN - 1
#define RAY_TIMER_CLOSE RAY_TIMER_START - 1
#define RAY_TIMER_WAIT  RAY_TIMER_CLOSE - 1

LUALIB_API int luaopen_ray_timer(lua_State* L);

#endif /* _RAY_TIMER_H_ */
