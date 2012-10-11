#ifndef _LUV_THREAD_H_
#define _LUV_THREAD_H_

luv_thread_t* luvL_thread_new(luv_state_t* outer, int narg);

int  luvL_thread_loop   (luv_thread_t* thread);
int  luvL_thread_once   (luv_thread_t* thread);
void luvL_thread_ready  (luv_thread_t* thread);
int  luvL_thread_yield  (luv_thread_t* thread, int narg);
int  luvL_thread_suspend(luv_thread_t* thread);
int  luvL_thread_resume (luv_thread_t* thread, int narg);
void luvL_thread_enqueue(luv_thread_t* thread, luv_fiber_t* fiber);

void luvL_thread_init_main(lua_State* L);

luv_thread_t* luvL_thread_self(lua_State* L);

#endif /* _LUV_THREAD_H_
