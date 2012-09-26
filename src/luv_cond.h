#ifndef LUV_COND_H
#define LUV_COND_H

#include "luv_core.h"

#define LUV_COND_T "luv.fiber.cond"

LUALIB_API int luaopenL_luv_cond(lua_State *L);

typedef ngx_queue_t luv_cond_t;

int luv__cond_init(luv_cond_t* cond);
int luv__cond_wait(luv_cond_t* cond, luv_state_t* curr);
int luv__cond_signal(luv_cond_t* cond);
int luv__cond_broadcast(luv_cond_t* cond);

int luv_cond_create(lua_State* L);

#endif /* LUV_COND_H */

