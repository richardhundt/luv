#include "luv.h"

int luvL_state_is_thread(luv_state_t* state) {
  return state->type == LUV_TTHREAD;
}
int luvL_state_in_thread(luv_state_t* state) {
  return state->type == LUV_TTHREAD;
}

luv_state_t* luvL_state_self(lua_State* L) {
  lua_pushthread(L);
  lua_rawget(L, LUA_REGISTRYINDEX);
  luv_state_t* self = (luv_state_t*)lua_touserdata(L, -1);
  lua_pop(L, 1);
  return self;
}

int luvL_state_is_active(luv_state_t* state) {
  return state == luvL_thread_self(state->L)->curr;
}

/* resume at the next iteration of the loop */
void luvL_state_ready(luv_state_t* state) {
  if (state->type == LUV_TTHREAD) {
    luvL_thread_ready((luv_thread_t*)state);
  }
  else {
    luvL_fiber_ready((luv_fiber_t*)state);
  }
}

/* yield a timeslice and allow passing values back to caller of resume */
int luvL_state_yield(luv_state_t* state, int narg) {
  assert(luvL_state_is_active(state));
  if (state->type == LUV_TTHREAD) {
    return luvL_thread_yield((luv_thread_t*)state, narg);
  }
  else {
    return luvL_fiber_yield((luv_fiber_t*)state, narg);
  }
}

/* suspend execution of the current state until something wakes us up. */
int luvL_state_suspend(luv_state_t* state) {
  if (state->type == LUV_TTHREAD) {
    return luvL_thread_suspend((luv_thread_t*)state);
  }
  else {
    return luvL_fiber_suspend((luv_fiber_t*)state);
  }
}

/* transfer control directly to another state */
int luvL_state_resume(luv_state_t* state, int narg) {
  if (state->type == LUV_TTHREAD) {
    return luvL_thread_resume((luv_thread_t*)state, narg);
  }
  else {
    return luvL_fiber_resume((luv_fiber_t*)state, narg);
  }
}
