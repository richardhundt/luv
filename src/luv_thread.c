#include "zmq.h"
#include "zmq_utils.h"

#include "luv.h"
#include "luv_core.h"
#include "luv_thread.h"

static int luv_writer(lua_State *L, const void* b, size_t size, void* B) {
  (void)L;
  luaL_addlstring((luaL_Buffer*)B, (const char *)b, size);
  return 0;
}

static void luv_thread_enter(void* arg) {
  luv_thread_t* self = (luv_thread_t*)arg;
  luaL_checktype(self->L, 1, LUA_TFUNCTION);
  int nargs = lua_gettop(self->L) - 1;
  lua_call(self->L, nargs, LUA_MULTRET);
  lua_close(self->L);
}

static int luv__thread_xdup(lua_State* src, lua_State* dst) {
  int type = lua_type(src, -1);
  switch (type) {
    case LUA_TNUMBER:
      lua_pushnumber(dst, lua_tonumber(src, -1));
      break;
    case LUA_TSTRING:
      {
        size_t len;
        const char* str = lua_tolstring(src, -1, &len);
        lua_pushlstring(dst, str, len);
      }
      break;
    case LUA_TBOOLEAN:
      lua_pushboolean(dst, lua_toboolean(src, -1));
      break;
    case LUA_TNIL:
      lua_pushnil(dst);
      break;
    case LUA_TTABLE:
    case LUA_TUSERDATA:
      if (luaL_getmetafield(src, -1, "__xdup")) {
        lua_pushvalue(src, -2);
        lua_pushlightuserdata(src, dst);
        lua_call(src, 2, 0);
        break;
      }
    default:
      return luaL_error(src, "cannot xdup a %s to thread", lua_typename(src, type));
  }
  return 0;
}

static int luv_new_thread(lua_State* L) {
  int narg = lua_gettop(L);
  luv_sched_t* sched = lua_touserdata(L, lua_upvalueindex(1));
  luv_thread_t* self = lua_newuserdata(L, sizeof(luv_thread_t));
  luaL_getmetatable(L, LUV_THREAD_T);
  lua_setmetatable(L, -2);

  int i, type;
  size_t len;

  self->L = luaL_newstate();
  luaL_openlibs(self->L);

  /* open luv in the child thread */
  luaopen_luv(self->L);
  lua_settop(self->L, 0);

  lua_Debug ar;
  luaL_Buffer buf;
  luaL_checktype(L, 1, LUA_TFUNCTION);
  lua_pushvalue(L, 1);
  luaL_buffinit(L, &buf);
  if (lua_dump(L, luv_writer, &buf) != 0) {
    luaL_error(L, "unable to dump given function");
  }

  luaL_pushresult(&buf);  

  const char* source = lua_tolstring(L, -1, &len);
  luaL_loadbuffer(self->L, source, len, "=thread");
  luaL_checktype(self->L, 1, LUA_TFUNCTION);
  lua_pop(L, 2); /* function and source */

  lua_settop(self->L, 1);
  for (i = 2; i <= narg; i++) {
    lua_pushvalue(L, i);
    luv__thread_xdup(L, self->L);
    lua_pop(L, 1);
  }

  lua_pushvalue(L, 1);
  lua_getinfo(L, ">nuS", &ar);
  luaL_checktype(L, 1, LUA_TFUNCTION);
  for (i = 1; i <= ar.nups; i++) {
    lua_getupvalue(L, 1, i);
    luv__thread_xdup(L, self->L);
    lua_setupvalue(self->L, 1, i);
  }

  int rv = uv_thread_create(&self->tid, luv_thread_enter, self);
  lua_settop(L, 2);
  return 1;
}

static int luv_thread_join(lua_State* L) {
  luv_thread_t* self = luaL_checkudata(L, 1, LUV_THREAD_T);
  int rv = uv_thread_join(&self->tid);
  lua_pushinteger(L, rv);
  return 1;
}

static int luv_thread_free(lua_State* L) {
  luv_thread_t* self = lua_touserdata(L, 1);
  zmq_close(self->data);
  return 1;
}
static int luv_thread_tostring(lua_State* L) {
  luv_thread_t* self = luaL_checkudata(L, 1, LUV_THREAD_T);
  lua_pushfstring(L, "userdata<%s>: %p", LUV_THREAD_T, self);
  return 1;
}

static luaL_Reg luv_thread_funcs[] = {
  {"create",    luv_new_thread},
  /*
  {"self",      luv_thread_self},
  */
  {NULL,        NULL}
};

static luaL_Reg luv_thread_meths[] = {
  {"join",      luv_thread_join},
  {"__gc",      luv_thread_free},
  {"__tostring",luv_thread_tostring},
  {NULL,        NULL}
};

LUALIB_API int luaopenL_luv_thread(lua_State *L) {
  luaL_newmetatable(L, LUV_THREAD_T);
  lua_pushvalue(L, -1);
  lua_setfield(L, -2, "__index");
  luaL_openlib(L, NULL, luv_thread_meths, 0);
  lua_pop(L, 1);

  /* thread */
  luv__new_namespace(L, "luv_thread");
  lua_getfield(L, LUA_REGISTRYINDEX, LUV_SCHED_O);
  luaL_openlib(L, NULL, luv_thread_funcs, 1);

  /* luv.thread */
  lua_getfield(L, LUA_REGISTRYINDEX, LUV_REG_KEY);
  lua_pushvalue(L, -2);
  lua_setfield(L, -2, "thread");
  lua_pop(L, 1);

  return 1;
}


