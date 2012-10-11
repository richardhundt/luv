#ifndef _RAY_H_
#define _RAY_H_

#ifdef __cplusplus
extern "C" {
#endif

#ifdef WIN32
#  ifdef RAY_EXPORT
#    define LUALIB_API __declspec(dllexport)
#  else
#    define LUALIB_API __declspec(dllimport)
#  endif
#else
#  define LUALIB_API LUA_API
#endif

#include "ray_lib.h"

LUALIB_API int luaopen_ray(lua_State *L);

#ifdef __cplusplus
}
#endif

#endif /* _RAY_H_ */
