#include "luv_core.h"
#include "luv_cond.h"
#include "luv_object.h"
#include "luv_stream.h"
#include "luv_net.h"

static void luv_listen_cb(uv_stream_t* server, int status) {
  puts(__func__);
  luv_object_t* self = container_of(server, luv_object_t, h);
  if (ngx_queue_empty(&self->rouse)) {
    printf("empty queue, increment count...");
    *(int*)&self->data += 1;
    printf("ok\n");
  }
  else {
    ngx_queue_t* q = ngx_queue_head(&self->rouse);
    luv_state_t* s = ngx_queue_data(q, luv_state_t, cond);
    ngx_queue_remove(q);
    printf("resuming state...");
    luv__state_resume(s);
    printf("ok\n");
    luv_object_t* conn = luaL_checkudata(s->L, 1, LUV_TCP_T);

    printf("call accept...");
    int rv = uv_accept(&self->h.stream, &conn->h.stream);
    printf("ok\n");
    if (rv) {
      uv_err_t err = uv_last_error(self->sched->loop);
      lua_settop(s->L, 0);
      lua_pushnil(s->L);
      lua_pushstring(s->L, uv_strerror(err));
    }
  }
}

static int luv_new_tcp(lua_State* L) {
  luv_sched_t*  sched = lua_touserdata(L, lua_upvalueindex(1));
  luv_object_t* self  = lua_newuserdata(L, sizeof(luv_object_t));
  luaL_getmetatable(L, LUV_TCP_T);
  lua_setmetatable(L, -2);

  luv__object_init(sched, self);
  uv_tcp_init(sched->loop, &self->h.tcp);

  return 1;
}

static int luv_tcp_bind(lua_State* L) {
  luv_object_t *self = luaL_checkudata(L, 1, LUV_TCP_T);

  struct sockaddr_in addr;
  const char* host;
  int port, rv;

  host = luaL_checkstring(L, 2);
  port = luaL_checkint(L, 3);
  addr = uv_ip4_addr(host, port);

  rv = uv_tcp_bind(&self->h.tcp, addr);
  lua_pushinteger(L, rv);

  return 1;
}

static int luv_tcp_listen(lua_State* L) {
  luv_object_t* self = luaL_checkudata(L, 1, LUV_TCP_T);
  int backlog = luaL_optinteger(L, 2, 128);

  if (uv_listen(&self->h.stream, backlog, luv_listen_cb)) {
    uv_err_t err = uv_last_error(self->sched->loop);
    return luaL_error(L, "listen: %s", uv_strerror(err));
  }

  /* backlog (assumes a pointer is at least as big as an int) */
  *(int*)&self->data = 0;

  return 0;
}

static int luv_tcp_accept(lua_State *L) {
  puts(__func__);
  luv_object_t* self = luaL_checkudata(L, 1, LUV_TCP_T);
  luv_object_t* conn = luaL_checkudata(L, 2, LUV_TCP_T);

  lua_insert(L, 1);
  lua_settop(L, 1);
  lua_pushnil(L);

  luv_sched_t* sched = self->sched;

  if (*(int*)&self->data > 0) {
    int rv = uv_accept(&self->h.stream, &conn->h.stream);
    *(int*)&self->data -= 1;
    if (rv) {
      uv_err_t err = uv_last_error(sched->loop);
      lua_settop(L, 0);
      lua_pushboolean(L, 0);
      lua_pushstring(L, uv_strerror(err));
    }
    return 2;
  }
  else {
    /* common case */
    luv_state_t* curr = luv__sched_current(sched);
    luv__state_suspend(curr);
    ngx_queue_insert_tail(&self->rouse, &curr->cond);
    return luv__state_yield(curr, 2);
  }
}

static int luv_tcp_free(lua_State *L) {
  luv_object_t* self = lua_touserdata(L, 1);
  (void)self;
  /* TODO: shutdown? */
  return 0;
}
static int luv_tcp_tostring(lua_State *L) {
  luv_object_t *self = luaL_checkudata(L, 1, LUV_TCP_T);
  lua_pushfstring(L, "userdata<%s>: %p", LUV_TCP_T, self);
  return 1;
}

static luaL_Reg luv_net_funcs[] = {
  {"tcp",       luv_new_tcp},
  /* {"udp",       luv_new_udp}, */
  /* {"getaddrinfo",luv_getaddrinfo}, */
  {NULL,        NULL}
};

static luaL_Reg luv_tcp_meths[] = {
  {"bind",      luv_tcp_bind},
  {"listen",    luv_tcp_listen},
  {"accept",    luv_tcp_accept},
  /* {"connect",   luv_tcp_connect}, */
  {"__gc",      luv_tcp_free},
  {"__tostring",luv_tcp_tostring},
  {NULL,        NULL}
};

/*
static luaL_Reg luv_udp_meths[] = {
  {"bind",      luv_udp_bind},
  {"send",      luv_udp_send},
  {"recv",      luv_udp_recv},
  {"__gc",      luv_udp_free},
  {"__tostring",luv_udp_tostring},
  {NULL,        NULL}
};
*/

LUALIB_API int luaopenL_luv_net(lua_State *L) {
  luaL_newmetatable(L, LUV_TCP_T);
  luaL_register(L, NULL, luv_stream_meths);
  luaL_register(L, NULL, luv_tcp_meths);
  lua_pushvalue(L, -1);
  lua_setfield(L, -2, "__index");
  lua_pop(L, 1);

/*
  luaL_newmetatable(L, LUV_UDP_T);
  luaL_register(L, NULL, luv_udp_meths);
  lua_pushvalue(L, -1);
  lua_setfield(L, -2, "__index");
  lua_pop(L, 1);
*/

  /* luv.net */
  luv__new_namespace(L, "luv_net");
  lua_getfield(L, LUA_REGISTRYINDEX, LUV_REG_KEY);
  lua_pushvalue(L, -2);
  lua_setfield(L, -2, "net");
  lua_pop(L, 1);

  /* luv.net.[funcs] */
  lua_getfield(L, LUA_REGISTRYINDEX, LUV_SCHED_O);
  luaL_openlib(L, NULL, luv_net_funcs, 1);
  lua_pop(L, 1);

  return 1;
}

