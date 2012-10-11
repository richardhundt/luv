#ifndef _RAY_LIB_H_
#define _RAY_LIB_H_

#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
#include "ray_common.h"

int rayL_traceback(lua_State *L);

int rayL_lib_decoder(lua_State* L);
int rayL_lib_encoder(lua_State* L);

int rayL_module (lua_State* L, const char* name, luaL_Reg* funcs);
int rayL_class  (lua_State* L, const char* name, luaL_Reg* meths);

void* rayL_checkudata(lua_State* L, int idx, const char* name);

LUALIB_API int rayL_core_init(lua_State* L);

typedef struct ray_const_reg_s {
  const char*   key;
  int           val;
} ray_const_reg_t;


#endif /* _RAY_LIB_H_ */

