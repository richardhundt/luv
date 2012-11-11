#ifndef RAY_ASYNC_H
#define RAY_ASYNC_H

#include "ray_core.h"
#include "ray_object.h"

#define RAY_ASYNC_T "ray.async"

LUALIB_API int luaopenL_ray_ready(lua_State *L);

#endif /* RAY_ASYNC_H */