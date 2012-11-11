#include "ray_lib.h"
#include "ray_codec.h"
#include "ray_actor.h"
#include "ray_thread.h"

static void _thread_enter(void* arg) {
  ray_actor_t* self = (ray_actor_t*)arg;
  lua_State* L = (lua_State*)self->u.data;
  ray_codec_decode(L);

  lua_remove(L, 1);
  luaL_checktype(L, 1, LUA_TFUNCTION);
  lua_pushcfunction(L, rayL_traceback);
  lua_insert(L, 1);

  int nargs = lua_gettop(L) - 2;
  int rv = lua_pcall(L, nargs, LUA_MULTRET, 1);
  lua_remove(L, 1); /* traceback */

  if (rv) { /* error */
    lua_pushboolean(L, 0);
    lua_insert(L, 1);
    luaL_error(L, lua_tostring(L, -1));
  }
  else {
    lua_pushboolean(L, 1);
    lua_insert(L, 1);
  }

  uv_async_send(&self->h.async);
}

static void _async_cb(uv_async_t* handle, int status) {
  ray_actor_t* self = container_of(handle, ray_actor_t, h);
  (void)status;
  ray_send(self, NULL, RAY_CLOSE);
}

static int _thread_RAY_AWAIT(ray_actor_t* self, ray_actor_t* from, int info) {
}

static int thread_xdup(ray_actor_t* a, ray_actor_t* b, int narg) {
  lua_State* src = a->L;
  lua_State* dst = b->L;
  int type = lua_type(src, -1);
  switch (type) {
    case LUA_TNUMBER: {
      lua_pushnumber(dst, lua_tonumber(src, -1));
      break;
    }
    case LUA_TSTRING: {
      size_t len;
      const char* str = lua_tolstring(src, -1, &len);
      lua_pushlstring(dst, str, len);
      break;
    }
    case LUA_TBOOLEAN: {
      lua_pushboolean(dst, lua_toboolean(src, -1));
      break;
    }
    case LUA_TNIL: {
      lua_pushnil(dst);
      break;
    }
    case LUA_TLIGHTUSERDATA: {
      void* data = lua_touserdata(src, -1);
      lua_pushlightuserdata(dst, data);
      break;
    }
    case LUA_TTABLE:
    case LUA_TUSERDATA: {
      if (luaL_getmetafield(src, -1, "__xdup")) {
        lua_pushvalue(src, -2);
        lua_pushlightuserdata(src, dst);
        lua_call(src, 2, 0);
        break;
      }
    }
    default: {
      return luaL_error(src, "cannot xdup a %s to thread", lua_typename(src, type));
    }
  }
  return 0;
}

int rayM_thread_send(ray_actor_t* self, ray_actor_t* from, int info) {
  switch (info) {
    case RAY_AWAIT:
      return _thread_RAY_AWAIT(self, from, info);
    case RAY_CLOSE:
      return _thread_RAY_CLOSE(self, from, info);
    default: {

    }
  }
}

ray_actor_t* ray_thread_new(lua_State* L) {
  int narg = lua_gettop(L);
  TRACE("narg: %i\n", narg);

  ray_actor_t* self = ray_actor_new(L, RAY_THREAD_T, rayM_thread_send);
  lua_State*   L1   = luaL_newstate();

  /* udata return value to the bottom of the stack */
  lua_insert(L, 1);

  /* open standard libs */
  luaL_openlibs(L1);

  /* we have access to our child's main state */
  self->u.data = L1;
  ray_init_main(L1);

  /* prep its stack for entry */
  size_t len;
  lua_settop(L1, 0);
  ray_codec_encode(L, narg);
  luaL_checktype(L, -1, LUA_TSTRING);
  const char* data = lua_tolstring(L, -1, &len);
  lua_pushlstring(L1, data, len);

  uv_thread_create(&self->tid, _thread_enter, self);

  /* inserted udata below function, so now just udata on top */
  lua_settop(L, 1);
  return self;
}

/* Lua API */
static int thread_new(lua_State* L) {
  ray_actor_t* self = ray_thread_new(L);
  return 1;
}

static int thread_join(lua_State* L) {
  ray_actor_t* self = (ray_actor_t*)luaL_checkudata(L, 1, RAY_THREAD_T);
  ray_actor_t* from = ray_current(L);

  uv_thread_join(&self->tid);
  return lua_gettop(self->L);
}

static int thread_free(lua_State* L) {
  ray_actor_t* self = lua_touserdata(L, 1);
  ray_send(self, NULL, RAY_CLOSE);
  return 1;
}
static int thread_tostring(lua_State* L) {
  ray_actor_t* self = (ray_actor_t*)luaL_checkudata(L, 1, RAY_THREAD_T);
  lua_pushfstring(L, "userdata<%s>: %p", RAY_THREAD_T, self);
  return 1;
}

static luaL_Reg thread_funcs[] = {
  {"spawn",     thread_new},
  {NULL,        NULL}
};

static luaL_Reg thread_meths[] = {
  {"join",      thread_join},
  {"__gc",      thread_free},
  {"__tostring",thread_tostring},
  {NULL,        NULL}
};

LUALIB_API int luaopen_ray_thread(lua_State* L) {
  rayL_module(L, "ray.thread", thread_funcs);
  rayL_class (L, RAY_THREAD_T, thread_meths);
  lua_pop(L, 1);
  ray_init_main(L);
  return 1;
}

