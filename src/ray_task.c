#include "ray.h"

static void _work_cb(uv_work_t* req) {
  rayL_codec_decode(self->L);
  lua_remove(self->L, 1);

  luaL_checktype(self->L, 1, LUA_TFUNCTION);
  lua_pushcfunction(self->L, rayL_traceback);
  lua_insert(self->L, 1);
  int nargs = lua_gettop(self->L) - 2;

  int rv = lua_pcall(self->L, nargs, LUA_MULTRET, 1);
  lua_remove(self->L, 1); /* traceback */

  if (rv) { /* error */
    lua_pushboolean(self->L, 0);
    lua_insert(self->L, 1);
    rayL_thread_ready(self);
    luaL_error(self->L, lua_tostring(self->L, -1));
  }
  else {
    lua_pushboolean(self->L, 1);
    lua_insert(self->L, 1);
  }
}

static void _done_cb(uv_work_t* req) {
  ray_task_t* self = container_of(req, ray_task_t, req);
  rayL_state_ready(self->wait);
}

static int ray_task_await(lua_State* L) {
  ray_task_t*  self  = (ray_task_t*)lua_newuserdata(L, sizeof(ray_task_t));
  ray_actor_t* outer = rayL_state_self(L);

  self->type  = RAY_TTASK;
  self->outer = outer;
  self->loop  = outer->loop;
  self->L     = lua_newstate();

  ngx_queue_init(&self->rouse);
  ngx_queue_init(&self->queue);

  uv_queue_work(rayL_event_loop(L), &self->h.work, _work_cb, _done_cb);
}

luaL_Reg ray_task_funcs[] = {
  {"await",     ray_task_await},
  {NULL,        NULL}
};


