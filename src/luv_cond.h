#ifndef _LUV_COND_H_
#define _LUV_COND_H_

#include "luv_common.h"

typedef ngx_queue_t luv_cond_t;

int luvL_cond_init      (luv_cond_t* cond);
int luvL_cond_wait      (luv_cond_t* cond, luv_state_t* curr);
int luvL_cond_signal    (luv_cond_t* cond);
int luvL_cond_broadcast (luv_cond_t* cond);

#endif /* _LUV_COND_H_ */
