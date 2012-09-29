#include "luv.h"

static void _listen_cb(uv_stream_t* server, int status) {
  TRACE("got client connection...\n");
  luv_object_t* self = container_of(server, luv_object_t, h);
  if (ngx_queue_empty(&self->rouse)) {
    TRACE("queue is empty, increment backlog\n");
    *(int*)&self->data += 1;
  }
  else {
    TRACE("have waiting states, call accept...\n");
    ngx_queue_t* q = ngx_queue_head(&self->rouse);
    luv_state_t* s = ngx_queue_data(q, luv_state_t, cond);
    ngx_queue_remove(q);
    luvL_state_ready(s);
    luv_object_t* conn = luaL_checkudata(s->L, 1, LUV_NET_TCP_T);

    int rv = uv_accept(&self->h.stream, &conn->h.stream);
    if (rv) {
      uv_handle_t* h = &self->h.handle;
      uv_err_t err = uv_last_error(h->loop);
      lua_settop(s->L, 0);
      lua_pushnil(s->L);
      lua_pushstring(s->L, uv_strerror(err));
    }
  }
}

static int luv_new_tcp(lua_State* L) {
  luv_state_t*  curr = luvL_state_self(L);
  luv_object_t* self = lua_newuserdata(L, sizeof(luv_object_t));
  luaL_getmetatable(L, LUV_NET_TCP_T);
  lua_setmetatable(L, -2);

  luvL_object_init(curr, self);
  uv_tcp_init(luvL_event_loop(curr), &self->h.tcp);

  return 1;
}

static int luv_tcp_bind(lua_State* L) {
  luv_object_t *self = luaL_checkudata(L, 1, LUV_NET_TCP_T);

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
  luv_object_t* self = luaL_checkudata(L, 1, LUV_NET_TCP_T);
  int backlog = luaL_optinteger(L, 2, 128);
  if (uv_listen(&self->h.stream, backlog, _listen_cb)) {
    luv_state_t* curr = luvL_state_self(L);
    uv_err_t err = uv_last_error(luvL_event_loop(curr));
    return luaL_error(L, "listen: %s", uv_strerror(err));
  }

  /* backlog (assumes a pointer is at least as big as an int) */
  *(int*)&self->data = 0;

  return 0;
}

static int luv_tcp_accept(lua_State *L) {
  luv_object_t* self = luaL_checkudata(L, 1, LUV_NET_TCP_T);
  luv_object_t* conn = luaL_checkudata(L, 2, LUV_NET_TCP_T);

  lua_insert(L, 1);
  lua_settop(L, 1);
  lua_pushnil(L);

  luv_state_t* curr = luvL_state_self(L);

  if (*(int*)&self->data > 0) {
    TRACE("have backlog, accept sync\n");
    int rv = uv_accept(&self->h.stream, &conn->h.stream);
    TRACE("ok, accepted\n");
    *(int*)&self->data -= 1;
    if (rv) {
      uv_err_t err = uv_last_error(luvL_event_loop(curr));
      lua_settop(L, 0);
      lua_pushboolean(L, 0);
      lua_pushstring(L, uv_strerror(err));
    }
    return 2;
  }
  else {
    /* common case */
    TRACE("wait for connections...\n");
    return luvL_cond_wait(&self->rouse, curr);
  }
}

static int luv_tcp_free(lua_State *L) {
  luv_object_t* self = lua_touserdata(L, 1);
  (void)self;
  /* TODO: shutdown? */
  return 0;
}
static int luv_tcp_tostring(lua_State *L) {
  luv_object_t *self = luaL_checkudata(L, 1, LUV_NET_TCP_T);
  lua_pushfstring(L, "userdata<%s>: %p", LUV_NET_TCP_T, self);
  return 1;
}

luaL_Reg luv_net_funcs[] = {
  {"tcp",       luv_new_tcp},
  /* {"udp",       luv_new_udp}, */
  /* {"getaddrinfo",luv_getaddrinfo}, */
  {NULL,        NULL}
};

luaL_Reg luv_net_tcp_meths[] = {
  {"bind",      luv_tcp_bind},
  {"listen",    luv_tcp_listen},
  {"accept",    luv_tcp_accept},
  /* {"connect",   luv_tcp_connect}, */
  {"__gc",      luv_tcp_free},
  {"__tostring",luv_tcp_tostring},
  {NULL,        NULL}
};

/*
luaL_Reg luv_udp_meths[] = {
  {"bind",      luv_udp_bind},
  {"send",      luv_udp_send},
  {"recv",      luv_udp_recv},
  {"__gc",      luv_udp_free},
  {"__tostring",luv_udp_tostring},
  {NULL,        NULL}
};
*/

