#ifndef _RAY_THREAD_H_
#define _RAY_THREAD_H_

ray_thread_t* rayL_thread_new(ray_state_t* outer, int narg);

int  rayL_thread_loop   (ray_thread_t* thread);
int  rayL_thread_once   (ray_thread_t* thread);
void rayL_thread_ready  (ray_thread_t* thread);
int  rayL_thread_yield  (ray_thread_t* thread, int narg);
int  rayL_thread_suspend(ray_thread_t* thread);
int  rayL_thread_resume (ray_thread_t* thread, int narg);
void rayL_thread_enqueue(ray_thread_t* thread, ray_fiber_t* fiber);

void rayL_thread_init_main(lua_State* L);

ray_thread_t* rayL_thread_self(lua_State* L);

#endif /* _RAY_THREAD_H_
