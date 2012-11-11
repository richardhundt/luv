#include "ray_lib.h"

static int ray_util_mem_free(lua_State* L) {
  lua_pushinteger(L, uv_get_free_memory());
  return 1;
}

static int ray_util_mem_total(lua_State* L) {
  lua_pushinteger(L, uv_get_total_memory());
  return 1;
}

static int ray_util_hrtime(lua_State* L) {
  lua_pushinteger(L, uv_hrtime());
  return 1;
}

static int ray_util_cpu_info(lua_State* L) {
  int size, i;
  uv_cpu_info_t* info;
  uv_err_t err = uv_cpu_info(&info, &size);

  lua_settop(L, 0);

  if (err.code) {
    lua_pushboolean(L, 0);
    luaL_error(L, uv_strerror(err));
    return 2;
  }

  lua_newtable(L);

  for (i = 0; i < size; i++) {
    lua_newtable(L);

    lua_pushstring(L, info[i].model);
    lua_setfield(L, -2, "model");

    lua_pushinteger(L, (lua_Integer)info[i].speed);
    lua_setfield(L, -2, "speed");

    lua_newtable(L); /* times */

    lua_pushinteger(L, (lua_Integer)info[i].cpu_times.user);
    lua_setfield(L, -2, "user");

    lua_pushinteger(L, (lua_Integer)info[i].cpu_times.nice);
    lua_setfield(L, -2, "nice");

    lua_pushinteger(L, (lua_Integer)info[i].cpu_times.sys);
    lua_setfield(L, -2, "sys");

    lua_pushinteger(L, (lua_Integer)info[i].cpu_times.idle);
    lua_setfield(L, -2, "idle");

    lua_pushinteger(L, (lua_Integer)info[i].cpu_times.irq);
    lua_setfield(L, -2, "irq");

    lua_setfield(L, -2, "times");

    lua_rawseti(L, 1, i + 1);
  }

  uv_free_cpu_info(info, size);
  return 1;
}

static int ray_util_interfaces(lua_State* L) {
  int size, i;
  char buf[INET6_ADDRSTRLEN];

  uv_interface_address_t* info;
  uv_err_t err = uv_interface_addresses(&info, &size);

  lua_settop(L, 0);

  if (err.code) {
    lua_pushboolean(L, 0);
    luaL_error(L, uv_strerror(err));
    return 2;
  }

  lua_newtable(L);

  for (i = 0; i < size; i++) {
    uv_interface_address_t addr = info[i];

    lua_newtable(L);

    lua_pushstring(L, addr.name);
    lua_setfield(L, -2, "name");

    lua_pushboolean(L, addr.is_internal);
    lua_setfield(L, -2, "is_internal");

    if (addr.address.address4.sin_family == PF_INET) {
      uv_ip4_name(&addr.address.address4, buf, sizeof(buf));
    }
    else if (addr.address.address4.sin_family == PF_INET6) {
      uv_ip6_name(&addr.address.address6, buf, sizeof(buf));
    }

    lua_pushstring(L, buf);
    lua_setfield(L, -2, "address");

    lua_rawseti(L, -2, i + 1);
  }

  uv_free_interface_addresses(info, size);

  return 1;
}

static luaL_Reg ray_util_funcs[] = {
  {"cpu_info",      ray_util_cpu_info},
  {"mem_free",      ray_util_mem_free},
  {"mem_total",     ray_util_mem_total},
  {"hrtime",        ray_util_hrtime},
  {"interfaces",    ray_util_interfaces},
  {NULL,            NULL}
};

LUALIB_API int luaopen_ray_util(lua_State* L) {
  rayL_module(L, "ray.util", ray_util_funcs);
  return 1;
}

