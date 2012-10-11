#include "ray_common.h"
#include "ray_state.h"

uv_loop_t* rayL_event_loop(lua_State* L) {
  return rayL_state_self(L)->loop;
}

ray_state_t* rayL_state_self(lua_State* L) {
  lua_pushthread(L);
  lua_rawget(L, LUA_REGISTRYINDEX);
  ray_state_t* self = (ray_state_t*)lua_touserdata(L, -1);
  lua_pop(L, 1);
  return self;
}

/* resume at the next iteration of the loop */
void rayL_state_ready(ray_state_t* state) {
  if (state->type == RAY_TTHREAD) {
    rayL_thread_ready((ray_thread_t*)state);
  }
  else {
    rayL_fiber_ready((ray_fiber_t*)state);
  }
}

/* yield a timeslice and allow passing values back to caller of resume */
int rayL_state_yield(ray_state_t* state, int narg) {
  assert(rayL_state_is_active(state));
  if (state->type == RAY_TTHREAD) {
    return rayL_thread_yield((ray_thread_t*)state, narg);
  }
  else {
    return rayL_fiber_yield((ray_fiber_t*)state, narg);
  }
}

/* suspend execution of the current state until something wakes us up. */
int rayL_state_suspend(ray_state_t* state) {
  if (state->type == RAY_TTHREAD) {
    return rayL_thread_suspend((ray_thread_t*)state);
  }
  else {
    return rayL_fiber_suspend((ray_fiber_t*)state);
  }
}

/* transfer control directly to another state */
int rayL_state_resume(ray_state_t* state, int narg) {
  if (state->type == RAY_TTHREAD) {
    return rayL_thread_resume((ray_thread_t*)state, narg);
  }
  else {
    return rayL_fiber_resume((ray_fiber_t*)state, narg);
  }
}

