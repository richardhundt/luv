#include "ray_common.h"
#include "ray_buf.h"
#include "ray_codec.h"

static int encode_table(lua_State* L, ray_buf_t *buf, int seen);
static int decode_table(lua_State* L, ray_buf_t* buf, int seen);

#define encoder_seen(L, idx, seen) do {\
  int ref = lua_objlen(L, seen) + 1; \
  lua_pushboolean(L, 1); \
  lua_rawseti(L, seen, ref); \
  lua_pushvalue(L, idx); \
  lua_pushinteger(L, ref); \
  lua_rawset(L, seen); \
} while (0)

#define encoder_hook(L, buf, seen) do { \
  rayL_buf_put(buf, RAY_CODEC_TUSR); \
  lua_pushvalue(L, -2); \
  lua_call(L, 1, 2); \
  int cbt = lua_type(L, -2); \
  if (!(cbt == LUA_TFUNCTION || cbt == LUA_TSTRING)) { \
    luaL_error(L, "__codec must return either a function or a string"); \
  } \
  encode_value(L, buf, -2, seen); \
  encode_value(L, buf, -1, seen); \
  lua_pop(L, 2); \
} while (0)

static void encode_value(lua_State* L, ray_buf_t* buf, int val, int seen) {
  size_t len;
  int val_type = lua_type(L, val);

  lua_pushvalue(L, val);

  rayL_buf_put(buf, (uint8_t)val_type);

  switch (val_type) {
  case LUA_TBOOLEAN: {
    int v = lua_toboolean(L, -1);
    rayL_buf_put(buf, (uint8_t)v);
    break;
  }
  case LUA_TSTRING: {
    const char *str_val = lua_tolstring(L, -1, &len);
    rayL_buf_write_uleb128(buf, (uint32_t)len);
    rayL_buf_write(buf, (uint8_t*)str_val, len);
    break;
  }
  case LUA_TNUMBER: {
    lua_Number v = lua_tonumber(L, -1);
    rayL_buf_write(buf, (uint8_t*)(void*)&v, sizeof v);
    break;
  }
  case LUA_TTABLE: {
    int tag, ref;
    lua_pushvalue(L, -1);
    lua_rawget(L, seen);
    if (!lua_isnil(L, -1)) {
      /* already seen */
      ref = lua_tointeger(L, -1);
      tag = RAY_CODEC_TREF;
      rayL_buf_put(buf, tag);
      rayL_buf_write_uleb128(buf, (uint32_t)ref);
      lua_pop(L, 1); /* pop ref */
    }
    else {
      lua_pop(L, 1); /* pop nil */
      encoder_seen(L, -1, seen);
      if (luaL_getmetafield(L, -1, "__codec")) {
        encoder_hook(L, buf, seen);
      }
      else {
        tag = RAY_CODEC_TVAL;
        rayL_buf_put(buf, tag);
        encode_table(L, buf, seen);
      }
    }
    break;
  }
  case LUA_TFUNCTION: {
    int tag, ref;
    lua_pushvalue(L, -1);
    lua_rawget(L, seen);
    if (!lua_isnil(L, -1)) {
      ref = lua_tointeger(L, -1);
      tag = RAY_CODEC_TREF;
      rayL_buf_put(buf, tag);
      rayL_buf_write_uleb128(buf, (uint32_t)ref);
      lua_pop(L, 1); /* pop ref */
    }
    else {
      int i;
      ray_buf_t* b = rayL_buf_new(64);
      lua_Debug ar;

      lua_pop(L, 1); /* pop nil */

      lua_pushvalue(L, -1);
      lua_getinfo(L, ">nuS", &ar);
      if (ar.what[0] != 'L') {
        luaL_error(L, "attempt to persist a C function '%s'", ar.name);
      }

      encoder_seen(L, -1, seen);

      tag = RAY_CODEC_TVAL;
      rayL_buf_put(buf, tag);

      lua_dump(L, (lua_Writer)rayL_writer, b);

      len = (size_t)(b->head - b->base);
      rayL_buf_write_uleb128(buf, (uint32_t)len);
      rayL_buf_write(buf, b->base, len);
      rayL_buf_free(b);

      lua_newtable(L);
      for (i = 1; i <= ar.nups; i++) {
        lua_getupvalue(L, -2, i);
        lua_rawseti(L, -2, i);
      }
      assert(lua_objlen(L, -1) == ar.nups);
      encode_table(L, buf, seen);
      lua_pop(L, 1);
    }

    break;
  }
  case LUA_TUSERDATA:
    if (luaL_getmetafield(L, -1, "__codec")) {
      encoder_hook(L, buf, seen);
      break;
    }
    else {
      luaL_error(L, "cannot encode userdata\n");
    }
  case LUA_TNIL:
    /* type tag already written */
    break;
  case LUA_TLIGHTUSERDATA: {
    void* ptr = lua_touserdata(L, -1);
    rayL_buf_write(buf, (uint8_t*)(void*)&ptr, sizeof(void*));
    break;
  }
  case LUA_TTHREAD:
  default:
    luaL_error(L, "cannot encode a `%s'", lua_typename(L, val_type));
  }
  lua_pop(L, 1);
}

static int encode_table(lua_State* L, ray_buf_t* buf, int seen) {
  lua_pushnil(L);
  while (lua_next(L, -2) != 0) {
    int top = lua_gettop(L);
    encode_value(L, buf, -2, seen);
    encode_value(L, buf, -1, seen);
    assert(lua_gettop(L) == top);
    lua_pop(L, 1);
  }

  /* sentinel */
  lua_pushnil(L);
  encode_value(L, buf, -1, seen);
  lua_pop(L, 1);

  return 1;
}

static void find_decoder(lua_State* L, ray_buf_t* buf, int seen) {
  int i;
  int lookup[2] = {
    LUA_REGISTRYINDEX,
    LUA_GLOBALSINDEX
  };
  for (i = 0; i < 2; i++) {
    lua_pushvalue(L, -1);
    lua_gettable(L, lookup[i]);
    if (lua_isnil(L, -1)) {
      lua_pop(L, 1);
    }
    else {
      lua_replace(L, -2);
      break;
    }
  }
  if (!lua_isfunction(L, -1)) {
    const char* key = lua_tostring(L, -1);
    luaL_error(L, "failed to find a valid decoder for `%s'", key);
  }
}

#define decoder_seen(L, idx, seen) do { \
  int ref = lua_objlen(L, seen) + 1; \
  lua_pushvalue(L, idx); \
  lua_rawseti(L, seen, ref); \
} while (0)

static void decode_value(lua_State* L, ray_buf_t* buf, int seen) {
  uint8_t val_type = rayL_buf_get(buf);
  size_t  len;
  switch (val_type) {
  case LUA_TBOOLEAN: {
    int val = rayL_buf_get(buf);
    lua_pushboolean(L, val);
    break;
  }
  case LUA_TNUMBER: {
    uint8_t* ptr = rayL_buf_read(buf, sizeof(lua_Number));
    lua_pushnumber(L, *(lua_Number*)(void*)ptr);
    break;
  }
  case LUA_TSTRING: {
    len = (size_t)rayL_buf_read_uleb128(buf);
    uint8_t* ptr = rayL_buf_read(buf, len);
    lua_pushlstring(L, (const char *)ptr, len);
    break;
  }
  case LUA_TTABLE: {
    uint8_t  tag = rayL_buf_get(buf);
    uint32_t ref;
    if (tag == RAY_CODEC_TREF) {
      ref = rayL_buf_read_uleb128(buf);
      lua_rawgeti(L, seen, ref);
    }
    else {
      if (tag == RAY_CODEC_TUSR) {
        decode_value(L, buf, seen); /* hook */
        if (lua_type(L, -1) == LUA_TSTRING) {
          find_decoder(L, buf, seen);
        }
        decode_value(L, buf, seen); /* any value */
        lua_call(L, 1, 1);          /* result */
        decoder_seen(L, -1, seen);
      }
      else {
        lua_newtable(L);
        decoder_seen(L, -1, seen);
        decode_table(L, buf, seen);
      }
    }
    break;
  }
  case LUA_TFUNCTION: {
    size_t nups;
    uint8_t tag = rayL_buf_get(buf);
    if (tag == RAY_CODEC_TREF) {
      uint32_t ref = rayL_buf_read_uleb128(buf);
      lua_rawgeti(L, seen, ref);
    }
    else {
      size_t i;
      len = rayL_buf_read_uleb128(buf);
      const char* code = (char *)rayL_buf_read(buf, len);
      if (luaL_loadbuffer(L, code, len, "=chunk")) {
        luaL_error(L, "failed to load chunk\n");
      }

      decoder_seen(L, -1, seen);
      lua_newtable(L);
      decode_table(L, buf, seen);
      nups = lua_objlen(L, -1);
      for (i=1; i <= nups; i++) {
        lua_rawgeti(L, -1, i);
        lua_setupvalue(L, -3, i);
      }
      lua_pop(L, 1);
    }
    break;
  }
  case LUA_TUSERDATA: {
    uint8_t tag = rayL_buf_get(buf);
    assert(tag == RAY_CODEC_TUSR);
    decode_value(L, buf, seen); /* hook */
    if (lua_type(L, -1) == LUA_TSTRING) {
      find_decoder(L, buf, seen);
    }
    decode_value(L, buf, seen); /* any value */
    luaL_checktype(L, -2, LUA_TFUNCTION);
    lua_call(L, 1, 1);          /* result */
    break;
  }
  case LUA_TLIGHTUSERDATA: {
    uint8_t* ptr = rayL_buf_read(buf, sizeof(void*));
    lua_pushlightuserdata(L, *(void**)ptr);
    break;
  }
  case LUA_TNIL:
    lua_pushnil(L);
    break;
  case LUA_TTHREAD:
  default:
    luaL_error(L, "bad code");
  }
}

static int decode_table(lua_State* L, ray_buf_t* buf, int seen) {
  for (;rayL_buf_peek(buf) != LUA_TNIL;) {
    decode_value(L, buf, seen);
    decode_value(L, buf, seen);
    lua_settable(L, -3);
  }

  /* sentinel */
  decode_value(L, buf, seen);
  assert(lua_type(L, -1) == LUA_TNIL);
  lua_pop(L, 1);
  return 1;
}

int rayL_codec_encode(lua_State* L, int narg) {
  int i, base, seen;
  ray_buf_t* buf = rayL_buf_new(64);

  base = lua_gettop(L) - narg + 1;

  lua_newtable(L);
  lua_insert(L, base);  /* seen */
  seen = base++;

  rayL_buf_write_uleb128(buf, narg);

  for (i = base; i < base + narg; i++) {
    encode_value(L, buf, i, seen);
  }

  lua_remove(L, seen);
  lua_settop(L, seen);

  lua_pushlstring(L, (char *)buf->base, buf->head - buf->base);
  rayL_buf_free(buf);

  return 1;
}

int rayL_codec_decode(lua_State* L) {
  size_t len;
  int nval, seen, i;
  int top = lua_gettop(L);

  ray_buf_t* buf = rayL_buf_new(64);

  const char* data = luaL_checklstring(L, top, &len);
  rayL_buf_init_data(buf, (uint8_t*)data, len);

  buf->head = buf->base;

  lua_newtable(L);
  seen = lua_gettop(L);
  nval = rayL_buf_read_uleb128(buf);

  lua_checkstack(L, nval);

  for (i = 0; i < nval; i++) {
    decode_value(L, buf, seen);
  }
  lua_remove(L, seen);

  rayL_buf_free(buf);

  assert(lua_gettop(L) == top + nval);
  return nval;
}

static int ray_codec_encode(lua_State* L) {
  return rayL_codec_encode(L, lua_gettop(L));
}
static int ray_codec_decode(lua_State* L) {
  return rayL_codec_decode(L);
}

luaL_Reg ray_codec_funcs[] = {
  {"encode", ray_codec_encode},
  {"decode", ray_codec_decode},
};

