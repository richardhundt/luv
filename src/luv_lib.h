#ifndef _LUV_LIB_H_
#define _LUV_LIB_H_

int luvL_traceback(lua_State *L);

int luvL_lib_decoder(lua_State* L);
int luvL_lib_encoder(lua_State* L);

int luvL_module_new (lua_State* L, const char* name, luaL_Reg* funcs);
int luvL_class_new  (lua_State* L, const char* name, luaL_Reg* meths);

int luvL_class_mixin  (lua_State* L, const char* from);
int luvL_class_extend (lua_State* L, const char* b, const char* n, luaL_Reg* m);

void* luvL_checkudata(lua_State* L, int idx, const char* name);

typedef struct luv_const_reg_s {
  const char*   key;
  int           val;
} luv_const_reg_t;


#endif /* _LUV_LIB_H_ */

