#ifndef _RAY_THREAD_H_
#define _RAY_THREAD_H_

#include "ray_state.h"

ray_state_t* rayL_thread_new(lua_State* L);

void rayL_thread_init(lua_State* L, int narg);

int rayM_thread_close   (ray_state_t* self);

LUALIB_API int luaopen_ray_thread(lua_State* L);

#endif /* _RAY_THREAD_H_ */
