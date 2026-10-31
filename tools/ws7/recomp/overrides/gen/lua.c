// Adapters of override module `lua` (generated, checked in).
// Each adapter is F_<address> of the original x86_64 function (see the comment above it).
#include "rc_ov.h"
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include "lua_glue.h"

void RC_FN(lua_checkstack)(rc_ctx *c) {
  OV_ARGS_OF(lua_checkstack, a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  int r = lua_checkstack(p0, p1);
  OV_PROTECT_END(c);
  OV_RET_I32(c, r);
}

void RC_FN(lua_xmove)(rc_ctx *c) {
  OV_ARGS_OF(lua_xmove, a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  lua_State * p1 = (lua_State *)(uintptr_t)OV_U64(a);
  int p2 = (int)OV_U64(a);
  lua_xmove(p0, p1, p2);
  ov_ret_void(c);
}

void RC_FN(lua_atpanic)(rc_ctx *c) {
  OV_ARGS_OF(lua_atpanic, a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  lua_CFunction p1 = OV_FN(lua_CFunction, OV_U64(a));
  OV_PROTECT_BEGIN(c);
  lua_CFunction r = lua_atpanic(p0, p1);
  OV_PROTECT_END(c);
  OV_RET_PTR(c, r);
}

void RC_FN(lua_newthread)(rc_ctx *c) {
  OV_ARGS_OF(lua_newthread, a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  lua_State * r = lua_newthread(p0);
  OV_PROTECT_END(c);
  OV_RET_PTR(c, r);
}

void RC_FN(lua_gettop)(rc_ctx *c) {
  OV_ARGS_OF(lua_gettop, a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int r = lua_gettop(p0);
  OV_RET_I32(c, r);
}

void RC_FN(lua_settop)(rc_ctx *c) {
  OV_ARGS_OF(lua_settop, a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  lua_settop(p0, p1);
  ov_ret_void(c);
}

void RC_FN(lua_remove)(rc_ctx *c) {
  OV_ARGS_OF(lua_remove, a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  lua_remove(p0, p1);
  ov_ret_void(c);
}

void RC_FN(lua_insert)(rc_ctx *c) {
  OV_ARGS_OF(lua_insert, a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  lua_insert(p0, p1);
  ov_ret_void(c);
}

void RC_FN(lua_replace)(rc_ctx *c) {
  OV_ARGS_OF(lua_replace, a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  lua_replace(p0, p1);
  ov_ret_void(c);
}

void RC_FN(lua_pushvalue)(rc_ctx *c) {
  OV_ARGS_OF(lua_pushvalue, a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  lua_pushvalue(p0, p1);
  ov_ret_void(c);
}

void RC_FN(lua_type)(rc_ctx *c) {
  OV_ARGS_OF(lua_type, a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  int r = lua_type(p0, p1);
  OV_RET_I32(c, r);
}

void RC_FN(lua_typename)(rc_ctx *c) {
  OV_ARGS_OF(lua_typename, a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  const char * r = lua_typename(p0, p1);
  OV_RET_PTR(c, r);
}

void RC_FN(lua_iscfunction)(rc_ctx *c) {
  OV_ARGS_OF(lua_iscfunction, a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  int r = lua_iscfunction(p0, p1);
  OV_RET_I32(c, r);
}

void RC_FN(lua_isnumber)(rc_ctx *c) {
  OV_ARGS_OF(lua_isnumber, a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  int r = lua_isnumber(p0, p1);
  OV_RET_I32(c, r);
}

void RC_FN(lua_isinteger)(rc_ctx *c) {
  OV_ARGS_OF(lua_isinteger, a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  int r = lua_isinteger(p0, p1);
  OV_RET_I32(c, r);
}

void RC_FN(lua_isstring)(rc_ctx *c) {
  OV_ARGS_OF(lua_isstring, a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  int r = lua_isstring(p0, p1);
  OV_RET_I32(c, r);
}

void RC_FN(lua_isuserdata)(rc_ctx *c) {
  OV_ARGS_OF(lua_isuserdata, a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  int r = lua_isuserdata(p0, p1);
  OV_RET_I32(c, r);
}

void RC_FN(lua_rawequal)(rc_ctx *c) {
  OV_ARGS_OF(lua_rawequal, a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  int p2 = (int)OV_U64(a);
  int r = lua_rawequal(p0, p1, p2);
  OV_RET_I32(c, r);
}

void RC_FN(lua_lessthan)(rc_ctx *c) {
  OV_ARGS_OF(lua_lessthan, a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  int p2 = (int)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  int r = lua_lessthan(p0, p1, p2);
  OV_PROTECT_END(c);
  OV_RET_I32(c, r);
}

void RC_FN(lua_tonumber)(rc_ctx *c) {
  OV_ARGS_OF(lua_tonumber, a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  lua_Number r = lua_tonumber(p0, p1);
  ov_ret_f64(c, r);
}

void RC_FN(lua_tointeger)(rc_ctx *c) {
  OV_ARGS_OF(lua_tointeger, a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  lua_Integer r = lua_tointeger(p0, p1);
  OV_RET_U64(c, r);
}

void RC_FN(lua_toboolean)(rc_ctx *c) {
  OV_ARGS_OF(lua_toboolean, a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  int r = lua_toboolean(p0, p1);
  OV_RET_I32(c, r);
}

void RC_FN(lua_tolstring)(rc_ctx *c) {
  OV_ARGS_OF(lua_tolstring, a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  size_t * p2 = (size_t *)(uintptr_t)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  const char * r = lua_tolstring(p0, p1, p2);
  OV_PROTECT_END(c);
  OV_RET_PTR(c, r);
}

void RC_FN(lua_objlen)(rc_ctx *c) {
  OV_ARGS_OF(lua_objlen, a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  size_t r = lua_objlen(p0, p1);
  OV_RET_U64(c, r);
}

void RC_FN(lua_tocfunction)(rc_ctx *c) {
  OV_ARGS_OF(lua_tocfunction, a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  lua_CFunction r = lua_tocfunction(p0, p1);
  OV_RET_PTR(c, r);
}

void RC_FN(lua_touserdata)(rc_ctx *c) {
  OV_ARGS_OF(lua_touserdata, a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  void * r = lua_touserdata(p0, p1);
  OV_RET_PTR(c, r);
}

void RC_FN(lua_tothread)(rc_ctx *c) {
  OV_ARGS_OF(lua_tothread, a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  lua_State * r = lua_tothread(p0, p1);
  OV_RET_PTR(c, r);
}

void RC_FN(lua_topointer)(rc_ctx *c) {
  OV_ARGS_OF(lua_topointer, a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  const void * r = lua_topointer(p0, p1);
  OV_RET_PTR(c, r);
}

void RC_FN(lua_pushnil)(rc_ctx *c) {
  OV_ARGS_OF(lua_pushnil, a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  lua_pushnil(p0);
  ov_ret_void(c);
}

void RC_FN(lua_pushnumber)(rc_ctx *c) {
  OV_ARGS_OF(lua_pushnumber, a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  lua_Number p1 = OV_F64(a);
  lua_pushnumber(p0, p1);
  ov_ret_void(c);
}

void RC_FN(lua_pushinteger)(rc_ctx *c) {
  OV_ARGS_OF(lua_pushinteger, a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  lua_Integer p1 = (lua_Integer)OV_U64(a);
  lua_pushinteger(p0, p1);
  ov_ret_void(c);
}

void RC_FN(lua_pushlstring)(rc_ctx *c) {
  OV_ARGS_OF(lua_pushlstring, a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  const char * p1 = (const char *)(uintptr_t)OV_U64(a);
  size_t p2 = (size_t)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  lua_pushlstring(p0, p1, p2);
  OV_PROTECT_END(c);
  ov_ret_void(c);
}

void RC_FN(lua_pushstring)(rc_ctx *c) {
  OV_ARGS_OF(lua_pushstring, a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  const char * p1 = (const char *)(uintptr_t)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  lua_pushstring(p0, p1);
  OV_PROTECT_END(c);
  ov_ret_void(c);
}

void RC_FN(lua_pushcclosure)(rc_ctx *c) {
  OV_ARGS_OF(lua_pushcclosure, a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  lua_CFunction p1 = OV_FN(lua_CFunction, OV_U64(a));
  int p2 = (int)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  lua_pushcclosure(p0, p1, p2);
  OV_PROTECT_END(c);
  ov_ret_void(c);
}

void RC_FN(lua_pushboolean)(rc_ctx *c) {
  OV_ARGS_OF(lua_pushboolean, a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  lua_pushboolean(p0, p1);
  ov_ret_void(c);
}

void RC_FN(lua_pushlightuserdata)(rc_ctx *c) {
  OV_ARGS_OF(lua_pushlightuserdata, a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  void * p1 = (void *)(uintptr_t)OV_U64(a);
  lua_pushlightuserdata(p0, p1);
  ov_ret_void(c);
}

void RC_FN(lua_pushthread)(rc_ctx *c) {
  OV_ARGS_OF(lua_pushthread, a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  int r = lua_pushthread(p0);
  OV_PROTECT_END(c);
  OV_RET_I32(c, r);
}

void RC_FN(lua_gettable)(rc_ctx *c) {
  OV_ARGS_OF(lua_gettable, a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  lua_gettable(p0, p1);
  OV_PROTECT_END(c);
  ov_ret_void(c);
}

void RC_FN(lua_getfield)(rc_ctx *c) {
  OV_ARGS_OF(lua_getfield, a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  const char * p2 = (const char *)(uintptr_t)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  lua_getfield(p0, p1, p2);
  OV_PROTECT_END(c);
  ov_ret_void(c);
}

void RC_FN(lua_rawget)(rc_ctx *c) {
  OV_ARGS_OF(lua_rawget, a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  lua_rawget(p0, p1);
  OV_PROTECT_END(c);
  ov_ret_void(c);
}

void RC_FN(lua_rawgeti)(rc_ctx *c) {
  OV_ARGS_OF(lua_rawgeti, a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  int p2 = (int)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  lua_rawgeti(p0, p1, p2);
  OV_PROTECT_END(c);
  ov_ret_void(c);
}

void RC_FN(lua_createtable)(rc_ctx *c) {
  OV_ARGS_OF(lua_createtable, a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  int p2 = (int)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  lua_createtable(p0, p1, p2);
  OV_PROTECT_END(c);
  ov_ret_void(c);
}

void RC_FN(lua_getmetatable)(rc_ctx *c) {
  OV_ARGS_OF(lua_getmetatable, a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  int r = lua_getmetatable(p0, p1);
  OV_PROTECT_END(c);
  OV_RET_I32(c, r);
}

void RC_FN(lua_getfenv)(rc_ctx *c) {
  OV_ARGS_OF(lua_getfenv, a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  lua_getfenv(p0, p1);
  OV_PROTECT_END(c);
  ov_ret_void(c);
}

void RC_FN(lua_settable)(rc_ctx *c) {
  OV_ARGS_OF(lua_settable, a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  lua_settable(p0, p1);
  OV_PROTECT_END(c);
  ov_ret_void(c);
}

void RC_FN(lua_setfield)(rc_ctx *c) {
  OV_ARGS_OF(lua_setfield, a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  const char * p2 = (const char *)(uintptr_t)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  lua_setfield(p0, p1, p2);
  OV_PROTECT_END(c);
  ov_ret_void(c);
}

void RC_FN(lua_rawset)(rc_ctx *c) {
  OV_ARGS_OF(lua_rawset, a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  lua_rawset(p0, p1);
  OV_PROTECT_END(c);
  ov_ret_void(c);
}

void RC_FN(lua_rawseti)(rc_ctx *c) {
  OV_ARGS_OF(lua_rawseti, a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  int p2 = (int)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  lua_rawseti(p0, p1, p2);
  OV_PROTECT_END(c);
  ov_ret_void(c);
}

void RC_FN(lua_setmetatable)(rc_ctx *c) {
  OV_ARGS_OF(lua_setmetatable, a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  int r = lua_setmetatable(p0, p1);
  OV_PROTECT_END(c);
  OV_RET_I32(c, r);
}

void RC_FN(lua_setfenv)(rc_ctx *c) {
  OV_ARGS_OF(lua_setfenv, a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  int r = lua_setfenv(p0, p1);
  OV_PROTECT_END(c);
  OV_RET_I32(c, r);
}

void RC_FN(lua_call)(rc_ctx *c) {
  OV_ARGS_OF(lua_call, a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  int p2 = (int)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  lua_call(p0, p1, p2);
  OV_PROTECT_END(c);
  ov_ret_void(c);
}

void RC_FN(lua_pcall)(rc_ctx *c) {
  OV_ARGS_OF(lua_pcall, a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  int p2 = (int)OV_U64(a);
  int p3 = (int)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  int r = lua_pcall(p0, p1, p2, p3);
  OV_PROTECT_END(c);
  ov_lua_trace(p0, r, "pcall", NULL);
  OV_RET_I32(c, r);
}

void RC_FN(lua_load)(rc_ctx *c) {
  OV_ARGS_OF(lua_load, a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  lua_Reader p1 = OV_FN(lua_Reader, OV_U64(a));
  void * p2 = (void *)(uintptr_t)OV_U64(a);
  const char * p3 = (const char *)(uintptr_t)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  int r = lua_load(p0, p1, p2, p3);
  OV_PROTECT_END(c);
  ov_lua_trace(p0, r, "load", p3);
  OV_RET_I32(c, r);
}

void RC_FN(lua_dump)(rc_ctx *c) {
  OV_ARGS_OF(lua_dump, a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  lua_Writer p1 = OV_FN(lua_Writer, OV_U64(a));
  void * p2 = (void *)(uintptr_t)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  int r = lua_dump(p0, p1, p2);
  OV_PROTECT_END(c);
  OV_RET_I32(c, r);
}

void RC_FN(lua_status)(rc_ctx *c) {
  OV_ARGS_OF(lua_status, a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int r = lua_status(p0);
  OV_RET_I32(c, r);
}

void RC_FN(lua_gc)(rc_ctx *c) {
  OV_ARGS_OF(lua_gc, a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  int p2 = (int)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  int r = lua_gc(p0, p1, p2);
  OV_PROTECT_END(c);
  OV_RET_I32(c, r);
}

void RC_FN(lua_error)(rc_ctx *c) {
  OV_ARGS_OF(lua_error, a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  ov_lua_trace(p0, -1, "error", NULL);
  OV_PROTECT_BEGIN(c);
  int r = lua_error(p0);
  OV_PROTECT_END(c);
  OV_RET_I32(c, r);
}

void RC_FN(lua_next)(rc_ctx *c) {
  OV_ARGS_OF(lua_next, a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  int r = lua_next(p0, p1);
  OV_PROTECT_END(c);
  OV_RET_I32(c, r);
}

void RC_FN(lua_concat)(rc_ctx *c) {
  OV_ARGS_OF(lua_concat, a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  lua_concat(p0, p1);
  OV_PROTECT_END(c);
  ov_ret_void(c);
}

void RC_FN(lua_newuserdata)(rc_ctx *c) {
  OV_ARGS_OF(lua_newuserdata, a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  size_t p1 = (size_t)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  void * r = lua_newuserdata(p0, p1);
  OV_PROTECT_END(c);
  OV_RET_PTR(c, r);
}

void RC_FN(lua_getupvalue)(rc_ctx *c) {
  OV_ARGS_OF(lua_getupvalue, a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  int p2 = (int)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  const char * r = lua_getupvalue(p0, p1, p2);
  OV_PROTECT_END(c);
  OV_RET_PTR(c, r);
}

void RC_FN(lua_setupvalue)(rc_ctx *c) {
  OV_ARGS_OF(lua_setupvalue, a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  int p2 = (int)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  const char * r = lua_setupvalue(p0, p1, p2);
  OV_PROTECT_END(c);
  OV_RET_PTR(c, r);
}

void RC_FN(lua_pushvalue_as_number)(rc_ctx *c) {
  OV_ARGS_OF(lua_pushvalue_as_number, a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  int r = lua_pushvalue_as_number(p0, p1);
  OV_PROTECT_END(c);
  OV_RET_I32(c, r);
}

void RC_FN(luaL_argerror)(rc_ctx *c) {
  OV_ARGS_OF(luaL_argerror, a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  const char * p2 = (const char *)(uintptr_t)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  int r = luaL_argerror(p0, p1, p2);
  OV_PROTECT_END(c);
  OV_RET_I32(c, r);
}

void RC_FN(luaL_where)(rc_ctx *c) {
  OV_ARGS_OF(luaL_where, a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  luaL_where(p0, p1);
  OV_PROTECT_END(c);
  ov_ret_void(c);
}

void RC_FN(luaL_checkoption)(rc_ctx *c) {
  OV_ARGS_OF(luaL_checkoption, a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  const char * p2 = (const char *)(uintptr_t)OV_U64(a);
  const char *const * p3 = (const char *const *)(uintptr_t)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  int r = luaL_checkoption(p0, p1, p2, p3);
  OV_PROTECT_END(c);
  OV_RET_I32(c, r);
}

void RC_FN(luaL_optlstring)(rc_ctx *c) {
  OV_ARGS_OF(luaL_optlstring, a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  const char * p2 = (const char *)(uintptr_t)OV_U64(a);
  size_t * p3 = (size_t *)(uintptr_t)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  const char * r = luaL_optlstring(p0, p1, p2, p3);
  OV_PROTECT_END(c);
  OV_RET_PTR(c, r);
}

void RC_FN(luaL_checklstring)(rc_ctx *c) {
  OV_ARGS_OF(luaL_checklstring, a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  size_t * p2 = (size_t *)(uintptr_t)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  const char * r = luaL_checklstring(p0, p1, p2);
  OV_PROTECT_END(c);
  OV_RET_PTR(c, r);
}

void RC_FN(luaL_newmetatable)(rc_ctx *c) {
  OV_ARGS_OF(luaL_newmetatable, a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  const char * p1 = (const char *)(uintptr_t)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  int r = luaL_newmetatable(p0, p1);
  OV_PROTECT_END(c);
  OV_RET_I32(c, r);
}

void RC_FN(luaL_checkudata)(rc_ctx *c) {
  OV_ARGS_OF(luaL_checkudata, a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  const char * p2 = (const char *)(uintptr_t)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  void * r = luaL_checkudata(p0, p1, p2);
  OV_PROTECT_END(c);
  OV_RET_PTR(c, r);
}

void RC_FN(luaL_checkstack)(rc_ctx *c) {
  OV_ARGS_OF(luaL_checkstack, a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  const char * p2 = (const char *)(uintptr_t)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  luaL_checkstack(p0, p1, p2);
  OV_PROTECT_END(c);
  ov_ret_void(c);
}

void RC_FN(luaL_checktype)(rc_ctx *c) {
  OV_ARGS_OF(luaL_checktype, a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  int p2 = (int)OV_U64(a);
  luaL_checktype(p0, p1, p2);
  ov_ret_void(c);
}

void RC_FN(luaL_checkany)(rc_ctx *c) {
  OV_ARGS_OF(luaL_checkany, a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  luaL_checkany(p0, p1);
  ov_ret_void(c);
}

void RC_FN(luaL_checknumber)(rc_ctx *c) {
  OV_ARGS_OF(luaL_checknumber, a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  lua_Number r = luaL_checknumber(p0, p1);
  OV_PROTECT_END(c);
  ov_ret_f64(c, r);
}

void RC_FN(luaL_optnumber)(rc_ctx *c) {
  OV_ARGS_OF(luaL_optnumber, a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  lua_Number p2 = OV_F64(a);
  OV_PROTECT_BEGIN(c);
  lua_Number r = luaL_optnumber(p0, p1, p2);
  OV_PROTECT_END(c);
  ov_ret_f64(c, r);
}

void RC_FN(luaL_checkinteger)(rc_ctx *c) {
  OV_ARGS_OF(luaL_checkinteger, a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  lua_Integer r = luaL_checkinteger(p0, p1);
  OV_PROTECT_END(c);
  OV_RET_U64(c, r);
}

void RC_FN(luaL_optinteger)(rc_ctx *c) {
  OV_ARGS_OF(luaL_optinteger, a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  lua_Integer p2 = (lua_Integer)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  lua_Integer r = luaL_optinteger(p0, p1, p2);
  OV_PROTECT_END(c);
  OV_RET_U64(c, r);
}

void RC_FN(luaL_getmetafield)(rc_ctx *c) {
  OV_ARGS_OF(luaL_getmetafield, a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  const char * p2 = (const char *)(uintptr_t)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  int r = luaL_getmetafield(p0, p1, p2);
  OV_PROTECT_END(c);
  OV_RET_I32(c, r);
}

void RC_FN(luaL_callmeta)(rc_ctx *c) {
  OV_ARGS_OF(luaL_callmeta, a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  const char * p2 = (const char *)(uintptr_t)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  int r = luaL_callmeta(p0, p1, p2);
  OV_PROTECT_END(c);
  OV_RET_I32(c, r);
}

void RC_FN(luaL_findtable)(rc_ctx *c) {
  OV_ARGS_OF(luaL_findtable, a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  const char * p2 = (const char *)(uintptr_t)OV_U64(a);
  int p3 = (int)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  const char * r = luaL_findtable(p0, p1, p2, p3);
  OV_PROTECT_END(c);
  OV_RET_PTR(c, r);
}

void RC_FN(luaL_gsub)(rc_ctx *c) {
  OV_ARGS_OF(luaL_gsub, a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  const char * p1 = (const char *)(uintptr_t)OV_U64(a);
  const char * p2 = (const char *)(uintptr_t)OV_U64(a);
  const char * p3 = (const char *)(uintptr_t)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  const char * r = luaL_gsub(p0, p1, p2, p3);
  OV_PROTECT_END(c);
  OV_RET_PTR(c, r);
}

void RC_FN(luaL_buffinit)(rc_ctx *c) {
  OV_ARGS_OF(luaL_buffinit, a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  luaL_Buffer * p1 = (luaL_Buffer *)(uintptr_t)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  luaL_buffinit(p0, p1);
  OV_PROTECT_END(c);
  ov_ret_void(c);
}

void RC_FN(luaL_addstring)(rc_ctx *c) {
  OV_ARGS_OF(luaL_addstring, a, c);
  luaL_Buffer * p0 = (luaL_Buffer *)(uintptr_t)OV_U64(a);
  const char * p1 = (const char *)(uintptr_t)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  luaL_addstring(p0, p1);
  OV_PROTECT_END(c);
  ov_ret_void(c);
}

void RC_FN(luaL_pushresult)(rc_ctx *c) {
  OV_ARGS_OF(luaL_pushresult, a, c);
  luaL_Buffer * p0 = (luaL_Buffer *)(uintptr_t)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  luaL_pushresult(p0);
  OV_PROTECT_END(c);
  ov_ret_void(c);
}

void RC_FN(luaL_prepbuffer)(rc_ctx *c) {
  OV_ARGS_OF(luaL_prepbuffer, a, c);
  luaL_Buffer * p0 = (luaL_Buffer *)(uintptr_t)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  char * r = luaL_prepbuffer(p0);
  OV_PROTECT_END(c);
  OV_RET_PTR(c, r);
}

void RC_FN(luaL_addvalue)(rc_ctx *c) {
  OV_ARGS_OF(luaL_addvalue, a, c);
  luaL_Buffer * p0 = (luaL_Buffer *)(uintptr_t)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  luaL_addvalue(p0);
  OV_PROTECT_END(c);
  ov_ret_void(c);
}

void RC_FN(luaL_ref)(rc_ctx *c) {
  OV_ARGS_OF(luaL_ref, a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  int r = luaL_ref(p0, p1);
  OV_PROTECT_END(c);
  OV_RET_I32(c, r);
}

void RC_FN(luaL_unref)(rc_ctx *c) {
  OV_ARGS_OF(luaL_unref, a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  int p2 = (int)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  luaL_unref(p0, p1, p2);
  OV_PROTECT_END(c);
  ov_ret_void(c);
}

void RC_FN(luaL_loadbuffer)(rc_ctx *c) {
  OV_ARGS_OF(luaL_loadbuffer, a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  const char * p1 = (const char *)(uintptr_t)OV_U64(a);
  size_t p2 = (size_t)OV_U64(a);
  const char * p3 = (const char *)(uintptr_t)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  int r = luaL_loadbuffer(p0, p1, p2, p3);
  OV_PROTECT_END(c);
  ov_lua_trace(p0, r, "loadbuffer", p3);
  OV_RET_I32(c, r);
}

void RC_FN(luaL_loadstring)(rc_ctx *c) {
  OV_ARGS_OF(luaL_loadstring, a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  const char * p1 = (const char *)(uintptr_t)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  int r = luaL_loadstring(p0, p1);
  OV_PROTECT_END(c);
  OV_RET_I32(c, r);
}

void RC_FN(lua_sethook)(rc_ctx *c) {
  OV_ARGS_OF(lua_sethook, a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  lua_Hook p1 = OV_FN(lua_Hook, OV_U64(a));
  int p2 = (int)OV_U64(a);
  int p3 = (int)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  int r = lua_sethook(p0, p1, p2, p3);
  OV_PROTECT_END(c);
  OV_RET_I32(c, r);
}

void RC_FN(lua_gethook)(rc_ctx *c) {
  OV_ARGS_OF(lua_gethook, a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  lua_Hook r = lua_gethook(p0);
  OV_RET_PTR(c, r);
}

void RC_FN(lua_gethookmask)(rc_ctx *c) {
  OV_ARGS_OF(lua_gethookmask, a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int r = lua_gethookmask(p0);
  OV_RET_I32(c, r);
}

void RC_FN(lua_gethookcount)(rc_ctx *c) {
  OV_ARGS_OF(lua_gethookcount, a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int r = lua_gethookcount(p0);
  OV_RET_I32(c, r);
}

void RC_FN(lua_getstack)(rc_ctx *c) {
  OV_ARGS_OF(lua_getstack, a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  lua_Debug * p2 = (lua_Debug *)(uintptr_t)OV_U64(a);
  int r = lua_getstack(p0, p1, p2);
  OV_RET_I32(c, r);
}

void RC_FN(lua_getlocal)(rc_ctx *c) {
  OV_ARGS_OF(lua_getlocal, a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  const lua_Debug * p1 = (const lua_Debug *)(uintptr_t)OV_U64(a);
  int p2 = (int)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  const char * r = lua_getlocal(p0, p1, p2);
  OV_PROTECT_END(c);
  OV_RET_PTR(c, r);
}

void RC_FN(lua_setlocal)(rc_ctx *c) {
  OV_ARGS_OF(lua_setlocal, a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  const lua_Debug * p1 = (const lua_Debug *)(uintptr_t)OV_U64(a);
  int p2 = (int)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  const char * r = lua_setlocal(p0, p1, p2);
  OV_PROTECT_END(c);
  OV_RET_PTR(c, r);
}

void RC_FN(lua_getinfo)(rc_ctx *c) {
  OV_ARGS_OF(lua_getinfo, a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  const char * p1 = (const char *)(uintptr_t)OV_U64(a);
  lua_Debug * p2 = (lua_Debug *)(uintptr_t)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  int r = lua_getinfo(p0, p1, p2);
  OV_PROTECT_END(c);
  OV_RET_I32(c, r);
}

void RC_FN(lua_resume)(rc_ctx *c) {
  OV_ARGS_OF(lua_resume, a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  int r = lua_resume(p0, p1);
  OV_PROTECT_END(c);
  ov_lua_trace(p0, r, "resume", NULL);
  OV_RET_I32(c, r);
}

void RC_FN(lua_yield)(rc_ctx *c) {
  OV_ARGS_OF(lua_yield, a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  int r = lua_yield(p0, p1);
  OV_PROTECT_END(c);
  OV_RET_I32(c, r);
}

void RC_FN(lua_newstate)(rc_ctx *c) {
  OV_ARGS_OF(lua_newstate, a, c);
  lua_Alloc p0 = OV_FN(lua_Alloc, OV_U64(a));
  void * p1 = (void *)(uintptr_t)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  lua_State * r = lua_newstate(p0, p1);
  OV_PROTECT_END(c);
  OV_RET_PTR(c, r);
}

void RC_FN(lua_close)(rc_ctx *c) {
  OV_ARGS_OF(lua_close, a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  lua_close(p0);
  OV_PROTECT_END(c);
  ov_ret_void(c);
}
