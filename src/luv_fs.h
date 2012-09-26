#ifndef LUV_FS_H
#define LUV_FS_H

#include "luv_core.h"
#include "luv_object.h"

#define LUV_FILE_T "luv.file"
#define LUV_BUF_SIZE 4096

LUALIB_API int luaopenL_luv_fs(lua_State *L);

#endif /* LUV_FS_H */

