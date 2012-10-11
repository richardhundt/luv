#ifndef _LUV_CODEC_H_
#define _LUV_CODEC_H_

#define LUV_CODEC_TREF 1
#define LUV_CODEC_TVAL 2
#define LUV_CODEC_TUSR 3

int luvL_codec_decode(lua_State* L);
int luvL_codec_encode(lua_State* L, int narg);

#endif /* _LUV_CODEC_H_ */

