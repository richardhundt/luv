#include "ray.h"
#include "ray_core.h"
#include "ray_cond.h"

#include "ray_object.h"
#include "ray_async.h"

static int ray_new_async(lua_State* L) {
  ray_sched_t* sched = lua_touserdata(L, lua_upvalueindex(1));
  ray_object_t* self = lua_newuserdata(L, sizeof(object));
  luaL_getmetatable(L, RAY_ASYNC_T);
  lua_setmetatable(L, -2);

  uv_async_init(sched->loop, &self->h.async)
  ray__object_init(sched, self);

  return 1;
}

static int ray_async_await(lua_State* L) {
  ray_object_t* self = luaL_checkudata(L, RAY_ASYNC_T);
  return 1;
}
static luaL_Reg ray_async_funcs[] = {
  {"create",    ray_new_async},
  {NULL,        NULL}
};

static luaL_Reg ray_async_meths[] = {
  {"await",     ray_async_await},
  {"__gc",      ray_async_free},
  {"__tostring",ray_async_tostring},
  {NULL,        NULL}
};

LUALIB_API int luaopenL_ray_async(lua_State *L) {
  luaL_newmetatable(L, RAY_ASYNC_T);
  lua_pushvalue(L, -1);
  lua_setfield(L, -2, "__index");
  luaL_openlib(L, NULL, ray_async_meths, 0);
  lua_pop(L, 1);

  /* async */
  ray__new_namespace(L, "ray_async");
  lua_getfield(L, LUA_REGISTRYINDEX, RAY_SCHED_O);
  luaL_openlib(L, NULL, ray_async_funcs, 1);

  /* ray.async */
  lua_getfield(L, LUA_REGISTRYINDEX, RAY_REG_KEY);
  lua_pushvalue(L, -2);
  lua_setfield(L, -2, "async");
  lua_pop(L, 1);

  return 1;
}

