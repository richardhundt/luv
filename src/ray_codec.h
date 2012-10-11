#ifndef _RAY_CODEC_H_
#define _RAY_CODEC_H_

#define RAY_CODEC_TREF 1
#define RAY_CODEC_TVAL 2
#define RAY_CODEC_TUSR 3

int rayL_codec_decode(lua_State* L);
int rayL_codec_encode(lua_State* L, int narg);

LUALIB_API int luaopen_ray_codec(lua_State* L);

#endif /* _RAY_CODEC_H_ */

