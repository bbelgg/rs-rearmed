// Declarations the Lua adapters need beyond lua.h: the LNUM API extension that Lua's tonumber() uses.
#ifndef OV_LUA_GLUE_H
#define OV_LUA_GLUE_H
#include "lua.h"
int lua_pushvalue_as_number(lua_State *L, int idx);
// RS_LUA_TRACE=1: log chunk loads, failed pcall/resume/load and lua_error to stderr ("[lua] ...")
void ov_lua_trace(lua_State *L, int status, const char *what, const char *chunk);
#endif
