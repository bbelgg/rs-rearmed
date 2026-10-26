// Adapters of override module `lua` (generated, checked in).
// Each adapter is F_<address> of the original x86_64 function (see the comment above it).
#include "rc_ov.h"
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include "lua_glue.h"

/* 0x100e0fb10 lua_checkstack */
void F_100e0fb10(rc_ctx *c) {
  OV_ARGS(a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  int r = lua_checkstack(p0, p1);
  OV_PROTECT_END(c);
  OV_RET_I32(c, r);
}

/* 0x100e0fba0 lua_xmove */
void F_100e0fba0(rc_ctx *c) {
  OV_ARGS(a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  lua_State * p1 = (lua_State *)(uintptr_t)OV_U64(a);
  int p2 = (int)OV_U64(a);
  lua_xmove(p0, p1, p2);
  ov_ret_void(c);
}

/* 0x100e0fc90 lua_atpanic */
void F_100e0fc90(rc_ctx *c) {
  OV_ARGS(a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  lua_CFunction p1 = OV_FN(lua_CFunction, OV_U64(a));
  OV_PROTECT_BEGIN(c);
  lua_CFunction r = lua_atpanic(p0, p1);
  OV_PROTECT_END(c);
  OV_RET_PTR(c, r);
}

/* 0x100e0fca0 lua_newthread */
void F_100e0fca0(rc_ctx *c) {
  OV_ARGS(a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  lua_State * r = lua_newthread(p0);
  OV_PROTECT_END(c);
  OV_RET_PTR(c, r);
}

/* 0x100e0fcf0 lua_gettop */
void F_100e0fcf0(rc_ctx *c) {
  OV_ARGS(a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int r = lua_gettop(p0);
  OV_RET_I32(c, r);
}

/* 0x100e0fd10 lua_settop */
void F_100e0fd10(rc_ctx *c) {
  OV_ARGS(a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  lua_settop(p0, p1);
  ov_ret_void(c);
}

/* 0x100e0fdf0 lua_remove */
void F_100e0fdf0(rc_ctx *c) {
  OV_ARGS(a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  lua_remove(p0, p1);
  ov_ret_void(c);
}

/* 0x100e0ff00 lua_insert */
void F_100e0ff00(rc_ctx *c) {
  OV_ARGS(a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  lua_insert(p0, p1);
  ov_ret_void(c);
}

/* 0x100e10000 lua_replace */
void F_100e10000(rc_ctx *c) {
  OV_ARGS(a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  lua_replace(p0, p1);
  ov_ret_void(c);
}

/* 0x100e10180 lua_pushvalue */
void F_100e10180(rc_ctx *c) {
  OV_ARGS(a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  lua_pushvalue(p0, p1);
  ov_ret_void(c);
}

/* 0x100e10260 lua_type */
void F_100e10260(rc_ctx *c) {
  OV_ARGS(a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  int r = lua_type(p0, p1);
  OV_RET_I32(c, r);
}

/* 0x100e10340 lua_typename */
void F_100e10340(rc_ctx *c) {
  OV_ARGS(a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  const char * r = lua_typename(p0, p1);
  OV_RET_PTR(c, r);
}

/* 0x100e10370 lua_iscfunction */
void F_100e10370(rc_ctx *c) {
  OV_ARGS(a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  int r = lua_iscfunction(p0, p1);
  OV_RET_I32(c, r);
}

/* 0x100e10460 lua_isnumber */
void F_100e10460(rc_ctx *c) {
  OV_ARGS(a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  int r = lua_isnumber(p0, p1);
  OV_RET_I32(c, r);
}

/* 0x100e10560 lua_isinteger */
void F_100e10560(rc_ctx *c) {
  OV_ARGS(a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  int r = lua_isinteger(p0, p1);
  OV_RET_I32(c, r);
}

/* 0x100e10790 lua_isstring */
void F_100e10790(rc_ctx *c) {
  OV_ARGS(a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  int r = lua_isstring(p0, p1);
  OV_RET_I32(c, r);
}

/* 0x100e10880 lua_isuserdata */
void F_100e10880(rc_ctx *c) {
  OV_ARGS(a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  int r = lua_isuserdata(p0, p1);
  OV_RET_I32(c, r);
}

/* 0x100e10960 lua_rawequal */
void F_100e10960(rc_ctx *c) {
  OV_ARGS(a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  int p2 = (int)OV_U64(a);
  int r = lua_rawequal(p0, p1, p2);
  OV_RET_I32(c, r);
}

/* 0x100e10b00 lua_lessthan */
void F_100e10b00(rc_ctx *c) {
  OV_ARGS(a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  int p2 = (int)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  int r = lua_lessthan(p0, p1, p2);
  OV_PROTECT_END(c);
  OV_RET_I32(c, r);
}

/* 0x100e10ca0 lua_tonumber */
void F_100e10ca0(rc_ctx *c) {
  OV_ARGS(a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  lua_Number r = lua_tonumber(p0, p1);
  ov_ret_f64(c, r);
}

/* 0x100e10db0 lua_tointeger */
void F_100e10db0(rc_ctx *c) {
  OV_ARGS(a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  lua_Integer r = lua_tointeger(p0, p1);
  OV_RET_U64(c, r);
}

/* 0x100e10ec0 lua_toboolean */
void F_100e10ec0(rc_ctx *c) {
  OV_ARGS(a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  int r = lua_toboolean(p0, p1);
  OV_RET_I32(c, r);
}

/* 0x100e10fb0 lua_tolstring */
void F_100e10fb0(rc_ctx *c) {
  OV_ARGS(a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  size_t * p2 = (size_t *)(uintptr_t)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  const char * r = lua_tolstring(p0, p1, p2);
  OV_PROTECT_END(c);
  OV_RET_PTR(c, r);
}

/* 0x100e111c0 lua_objlen */
void F_100e111c0(rc_ctx *c) {
  OV_ARGS(a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  size_t r = lua_objlen(p0, p1);
  OV_RET_U64(c, r);
}

/* 0x100e11300 lua_tocfunction */
void F_100e11300(rc_ctx *c) {
  OV_ARGS(a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  lua_CFunction r = lua_tocfunction(p0, p1);
  OV_RET_PTR(c, r);
}

/* 0x100e113f0 lua_touserdata */
void F_100e113f0(rc_ctx *c) {
  OV_ARGS(a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  void * r = lua_touserdata(p0, p1);
  OV_RET_PTR(c, r);
}

/* 0x100e114e0 lua_tothread */
void F_100e114e0(rc_ctx *c) {
  OV_ARGS(a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  lua_State * r = lua_tothread(p0, p1);
  OV_RET_PTR(c, r);
}

/* 0x100e115c0 lua_topointer */
void F_100e115c0(rc_ctx *c) {
  OV_ARGS(a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  const void * r = lua_topointer(p0, p1);
  OV_RET_PTR(c, r);
}

/* 0x100e117b0 lua_pushnil */
void F_100e117b0(rc_ctx *c) {
  OV_ARGS(a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  lua_pushnil(p0);
  ov_ret_void(c);
}

/* 0x100e117d0 lua_pushnumber */
void F_100e117d0(rc_ctx *c) {
  OV_ARGS(a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  lua_Number p1 = OV_F64(a);
  lua_pushnumber(p0, p1);
  ov_ret_void(c);
}

/* 0x100e117f0 lua_pushinteger */
void F_100e117f0(rc_ctx *c) {
  OV_ARGS(a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  lua_Integer p1 = (lua_Integer)OV_U64(a);
  lua_pushinteger(p0, p1);
  ov_ret_void(c);
}

/* 0x100e11810 lua_pushlstring */
void F_100e11810(rc_ctx *c) {
  OV_ARGS(a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  const char * p1 = (const char *)(uintptr_t)OV_U64(a);
  size_t p2 = (size_t)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  lua_pushlstring(p0, p1, p2);
  OV_PROTECT_END(c);
  ov_ret_void(c);
}

/* 0x100e11870 lua_pushstring */
void F_100e11870(rc_ctx *c) {
  OV_ARGS(a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  const char * p1 = (const char *)(uintptr_t)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  lua_pushstring(p0, p1);
  OV_PROTECT_END(c);
  ov_ret_void(c);
}

/* 0x100e11a10 lua_pushcclosure */
void F_100e11a10(rc_ctx *c) {
  OV_ARGS(a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  lua_CFunction p1 = OV_FN(lua_CFunction, OV_U64(a));
  int p2 = (int)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  lua_pushcclosure(p0, p1, p2);
  OV_PROTECT_END(c);
  ov_ret_void(c);
}

/* 0x100e11b20 lua_pushboolean */
void F_100e11b20(rc_ctx *c) {
  OV_ARGS(a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  lua_pushboolean(p0, p1);
  ov_ret_void(c);
}

/* 0x100e11b50 lua_pushlightuserdata */
void F_100e11b50(rc_ctx *c) {
  OV_ARGS(a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  void * p1 = (void *)(uintptr_t)OV_U64(a);
  lua_pushlightuserdata(p0, p1);
  ov_ret_void(c);
}

/* 0x100e11b70 lua_pushthread */
void F_100e11b70(rc_ctx *c) {
  OV_ARGS(a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  int r = lua_pushthread(p0);
  OV_PROTECT_END(c);
  OV_RET_I32(c, r);
}

/* 0x100e11ba0 lua_gettable */
void F_100e11ba0(rc_ctx *c) {
  OV_ARGS(a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  lua_gettable(p0, p1);
  OV_PROTECT_END(c);
  ov_ret_void(c);
}

/* 0x100e11c80 lua_getfield */
void F_100e11c80(rc_ctx *c) {
  OV_ARGS(a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  const char * p2 = (const char *)(uintptr_t)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  lua_getfield(p0, p1, p2);
  OV_PROTECT_END(c);
  ov_ret_void(c);
}

/* 0x100e11da0 lua_rawget */
void F_100e11da0(rc_ctx *c) {
  OV_ARGS(a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  lua_rawget(p0, p1);
  OV_PROTECT_END(c);
  ov_ret_void(c);
}

/* 0x100e11ea0 lua_rawgeti */
void F_100e11ea0(rc_ctx *c) {
  OV_ARGS(a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  int p2 = (int)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  lua_rawgeti(p0, p1, p2);
  OV_PROTECT_END(c);
  ov_ret_void(c);
}

/* 0x100e11fa0 lua_createtable */
void F_100e11fa0(rc_ctx *c) {
  OV_ARGS(a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  int p2 = (int)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  lua_createtable(p0, p1, p2);
  OV_PROTECT_END(c);
  ov_ret_void(c);
}

/* 0x100e12000 lua_getmetatable */
void F_100e12000(rc_ctx *c) {
  OV_ARGS(a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  int r = lua_getmetatable(p0, p1);
  OV_PROTECT_END(c);
  OV_RET_I32(c, r);
}

/* 0x100e12120 lua_getfenv */
void F_100e12120(rc_ctx *c) {
  OV_ARGS(a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  lua_getfenv(p0, p1);
  OV_PROTECT_END(c);
  ov_ret_void(c);
}

/* 0x100e12250 lua_settable */
void F_100e12250(rc_ctx *c) {
  OV_ARGS(a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  lua_settable(p0, p1);
  OV_PROTECT_END(c);
  ov_ret_void(c);
}

/* 0x100e12340 lua_setfield */
void F_100e12340(rc_ctx *c) {
  OV_ARGS(a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  const char * p2 = (const char *)(uintptr_t)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  lua_setfield(p0, p1, p2);
  OV_PROTECT_END(c);
  ov_ret_void(c);
}

/* 0x100e12460 lua_rawset */
void F_100e12460(rc_ctx *c) {
  OV_ARGS(a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  lua_rawset(p0, p1);
  OV_PROTECT_END(c);
  ov_ret_void(c);
}

/* 0x100e12590 lua_rawseti */
void F_100e12590(rc_ctx *c) {
  OV_ARGS(a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  int p2 = (int)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  lua_rawseti(p0, p1, p2);
  OV_PROTECT_END(c);
  ov_ret_void(c);
}

/* 0x100e126c0 lua_setmetatable */
void F_100e126c0(rc_ctx *c) {
  OV_ARGS(a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  int r = lua_setmetatable(p0, p1);
  OV_PROTECT_END(c);
  OV_RET_I32(c, r);
}

/* 0x100e12820 lua_setfenv */
void F_100e12820(rc_ctx *c) {
  OV_ARGS(a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  int r = lua_setfenv(p0, p1);
  OV_PROTECT_END(c);
  OV_RET_I32(c, r);
}

/* 0x100e12970 lua_call */
void F_100e12970(rc_ctx *c) {
  OV_ARGS(a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  int p2 = (int)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  lua_call(p0, p1, p2);
  OV_PROTECT_END(c);
  ov_ret_void(c);
}

/* 0x100e129c0 lua_pcall */
void F_100e129c0(rc_ctx *c) {
  OV_ARGS(a, c);
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

/* 0x100e12b20 lua_load */
void F_100e12b20(rc_ctx *c) {
  OV_ARGS(a, c);
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

/* 0x100e12b70 lua_dump */
void F_100e12b70(rc_ctx *c) {
  OV_ARGS(a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  lua_Writer p1 = OV_FN(lua_Writer, OV_U64(a));
  void * p2 = (void *)(uintptr_t)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  int r = lua_dump(p0, p1, p2);
  OV_PROTECT_END(c);
  OV_RET_I32(c, r);
}

/* 0x100e12bb0 lua_status */
void F_100e12bb0(rc_ctx *c) {
  OV_ARGS(a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int r = lua_status(p0);
  OV_RET_I32(c, r);
}

/* 0x100e12bc0 lua_gc */
void F_100e12bc0(rc_ctx *c) {
  OV_ARGS(a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  int p2 = (int)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  int r = lua_gc(p0, p1, p2);
  OV_PROTECT_END(c);
  OV_RET_I32(c, r);
}

/* 0x100e12cb0 lua_error */
void F_100e12cb0(rc_ctx *c) {
  OV_ARGS(a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  ov_lua_trace(p0, -1, "error", NULL);
  OV_PROTECT_BEGIN(c);
  int r = lua_error(p0);
  OV_PROTECT_END(c);
  OV_RET_I32(c, r);
}

/* 0x100e12cc0 lua_next */
void F_100e12cc0(rc_ctx *c) {
  OV_ARGS(a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  int r = lua_next(p0, p1);
  OV_PROTECT_END(c);
  OV_RET_I32(c, r);
}

/* 0x100e12dc0 lua_concat */
void F_100e12dc0(rc_ctx *c) {
  OV_ARGS(a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  lua_concat(p0, p1);
  OV_PROTECT_END(c);
  ov_ret_void(c);
}

/* 0x100e12e40 lua_newuserdata */
void F_100e12e40(rc_ctx *c) {
  OV_ARGS(a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  size_t p1 = (size_t)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  void * r = lua_newuserdata(p0, p1);
  OV_PROTECT_END(c);
  OV_RET_PTR(c, r);
}

/* 0x100e12eb0 lua_getupvalue */
void F_100e12eb0(rc_ctx *c) {
  OV_ARGS(a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  int p2 = (int)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  const char * r = lua_getupvalue(p0, p1, p2);
  OV_PROTECT_END(c);
  OV_RET_PTR(c, r);
}

/* 0x100e12ff0 lua_setupvalue */
void F_100e12ff0(rc_ctx *c) {
  OV_ARGS(a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  int p2 = (int)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  const char * r = lua_setupvalue(p0, p1, p2);
  OV_PROTECT_END(c);
  OV_RET_PTR(c, r);
}

/* 0x100e13170 lua_pushvalue_as_number */
void F_100e13170(rc_ctx *c) {
  OV_ARGS(a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  int r = lua_pushvalue_as_number(p0, p1);
  OV_PROTECT_END(c);
  OV_RET_I32(c, r);
}

/* 0x100e132d0 luaL_argerror */
void F_100e132d0(rc_ctx *c) {
  OV_ARGS(a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  const char * p2 = (const char *)(uintptr_t)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  int r = luaL_argerror(p0, p1, p2);
  OV_PROTECT_END(c);
  OV_RET_I32(c, r);
}

/* 0x100e13510 luaL_where */
void F_100e13510(rc_ctx *c) {
  OV_ARGS(a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  luaL_where(p0, p1);
  OV_PROTECT_END(c);
  ov_ret_void(c);
}

/* 0x100e135b0 luaL_checkoption */
void F_100e135b0(rc_ctx *c) {
  OV_ARGS(a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  const char * p2 = (const char *)(uintptr_t)OV_U64(a);
  const char *const * p3 = (const char *const *)(uintptr_t)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  int r = luaL_checkoption(p0, p1, p2, p3);
  OV_PROTECT_END(c);
  OV_RET_I32(c, r);
}

/* 0x100e136b0 luaL_optlstring */
void F_100e136b0(rc_ctx *c) {
  OV_ARGS(a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  const char * p2 = (const char *)(uintptr_t)OV_U64(a);
  size_t * p3 = (size_t *)(uintptr_t)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  const char * r = luaL_optlstring(p0, p1, p2, p3);
  OV_PROTECT_END(c);
  OV_RET_PTR(c, r);
}

/* 0x100e13770 luaL_checklstring */
void F_100e13770(rc_ctx *c) {
  OV_ARGS(a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  size_t * p2 = (size_t *)(uintptr_t)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  const char * r = luaL_checklstring(p0, p1, p2);
  OV_PROTECT_END(c);
  OV_RET_PTR(c, r);
}

/* 0x100e137f0 luaL_newmetatable */
void F_100e137f0(rc_ctx *c) {
  OV_ARGS(a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  const char * p1 = (const char *)(uintptr_t)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  int r = luaL_newmetatable(p0, p1);
  OV_PROTECT_END(c);
  OV_RET_I32(c, r);
}

/* 0x100e13860 luaL_checkudata */
void F_100e13860(rc_ctx *c) {
  OV_ARGS(a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  const char * p2 = (const char *)(uintptr_t)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  void * r = luaL_checkudata(p0, p1, p2);
  OV_PROTECT_END(c);
  OV_RET_PTR(c, r);
}

/* 0x100e13910 luaL_checkstack */
void F_100e13910(rc_ctx *c) {
  OV_ARGS(a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  const char * p2 = (const char *)(uintptr_t)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  luaL_checkstack(p0, p1, p2);
  OV_PROTECT_END(c);
  ov_ret_void(c);
}

/* 0x100e13950 luaL_checktype */
void F_100e13950(rc_ctx *c) {
  OV_ARGS(a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  int p2 = (int)OV_U64(a);
  luaL_checktype(p0, p1, p2);
  ov_ret_void(c);
}

/* 0x100e139e0 luaL_checkany */
void F_100e139e0(rc_ctx *c) {
  OV_ARGS(a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  luaL_checkany(p0, p1);
  ov_ret_void(c);
}

/* 0x100e13a20 luaL_checknumber */
void F_100e13a20(rc_ctx *c) {
  OV_ARGS(a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  lua_Number r = luaL_checknumber(p0, p1);
  OV_PROTECT_END(c);
  ov_ret_f64(c, r);
}

/* 0x100e13ac0 luaL_optnumber */
void F_100e13ac0(rc_ctx *c) {
  OV_ARGS(a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  lua_Number p2 = OV_F64(a);
  OV_PROTECT_BEGIN(c);
  lua_Number r = luaL_optnumber(p0, p1, p2);
  OV_PROTECT_END(c);
  ov_ret_f64(c, r);
}

/* 0x100e13b10 luaL_checkinteger */
void F_100e13b10(rc_ctx *c) {
  OV_ARGS(a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  lua_Integer r = luaL_checkinteger(p0, p1);
  OV_PROTECT_END(c);
  OV_RET_U64(c, r);
}

/* 0x100e13b90 luaL_optinteger */
void F_100e13b90(rc_ctx *c) {
  OV_ARGS(a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  lua_Integer p2 = (lua_Integer)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  lua_Integer r = luaL_optinteger(p0, p1, p2);
  OV_PROTECT_END(c);
  OV_RET_U64(c, r);
}

/* 0x100e13c30 luaL_getmetafield */
void F_100e13c30(rc_ctx *c) {
  OV_ARGS(a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  const char * p2 = (const char *)(uintptr_t)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  int r = luaL_getmetafield(p0, p1, p2);
  OV_PROTECT_END(c);
  OV_RET_I32(c, r);
}

/* 0x100e13cb0 luaL_callmeta */
void F_100e13cb0(rc_ctx *c) {
  OV_ARGS(a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  const char * p2 = (const char *)(uintptr_t)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  int r = luaL_callmeta(p0, p1, p2);
  OV_PROTECT_END(c);
  OV_RET_I32(c, r);
}

/* 0x100e13f30 luaL_findtable */
void F_100e13f30(rc_ctx *c) {
  OV_ARGS(a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  const char * p2 = (const char *)(uintptr_t)OV_U64(a);
  int p3 = (int)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  const char * r = luaL_findtable(p0, p1, p2, p3);
  OV_PROTECT_END(c);
  OV_RET_PTR(c, r);
}

/* 0x100e14070 luaL_gsub */
void F_100e14070(rc_ctx *c) {
  OV_ARGS(a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  const char * p1 = (const char *)(uintptr_t)OV_U64(a);
  const char * p2 = (const char *)(uintptr_t)OV_U64(a);
  const char * p3 = (const char *)(uintptr_t)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  const char * r = luaL_gsub(p0, p1, p2, p3);
  OV_PROTECT_END(c);
  OV_RET_PTR(c, r);
}

/* 0x100e142b0 luaL_buffinit */
void F_100e142b0(rc_ctx *c) {
  OV_ARGS(a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  luaL_Buffer * p1 = (luaL_Buffer *)(uintptr_t)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  luaL_buffinit(p0, p1);
  OV_PROTECT_END(c);
  ov_ret_void(c);
}

/* 0x100e142d0 luaL_addstring */
void F_100e142d0(rc_ctx *c) {
  OV_ARGS(a, c);
  luaL_Buffer * p0 = (luaL_Buffer *)(uintptr_t)OV_U64(a);
  const char * p1 = (const char *)(uintptr_t)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  luaL_addstring(p0, p1);
  OV_PROTECT_END(c);
  ov_ret_void(c);
}

/* 0x100e14330 luaL_pushresult */
void F_100e14330(rc_ctx *c) {
  OV_ARGS(a, c);
  luaL_Buffer * p0 = (luaL_Buffer *)(uintptr_t)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  luaL_pushresult(p0);
  OV_PROTECT_END(c);
  ov_ret_void(c);
}

/* 0x100e14390 luaL_prepbuffer */
void F_100e14390(rc_ctx *c) {
  OV_ARGS(a, c);
  luaL_Buffer * p0 = (luaL_Buffer *)(uintptr_t)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  char * r = luaL_prepbuffer(p0);
  OV_PROTECT_END(c);
  OV_RET_PTR(c, r);
}

/* 0x100e14460 luaL_addvalue */
void F_100e14460(rc_ctx *c) {
  OV_ARGS(a, c);
  luaL_Buffer * p0 = (luaL_Buffer *)(uintptr_t)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  luaL_addvalue(p0);
  OV_PROTECT_END(c);
  ov_ret_void(c);
}

/* 0x100e14590 luaL_ref */
void F_100e14590(rc_ctx *c) {
  OV_ARGS(a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  int r = luaL_ref(p0, p1);
  OV_PROTECT_END(c);
  OV_RET_I32(c, r);
}

/* 0x100e14650 luaL_unref */
void F_100e14650(rc_ctx *c) {
  OV_ARGS(a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  int p2 = (int)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  luaL_unref(p0, p1, p2);
  OV_PROTECT_END(c);
  ov_ret_void(c);
}

/* 0x100e14ab0 luaL_loadbuffer */
void F_100e14ab0(rc_ctx *c) {
  OV_ARGS(a, c);
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

/* 0x100e14b00 luaL_loadstring */
void F_100e14b00(rc_ctx *c) {
  OV_ARGS(a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  const char * p1 = (const char *)(uintptr_t)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  int r = luaL_loadstring(p0, p1);
  OV_PROTECT_END(c);
  OV_RET_I32(c, r);
}

/* 0x100e1a1a0 lua_sethook */
void F_100e1a1a0(rc_ctx *c) {
  OV_ARGS(a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  lua_Hook p1 = OV_FN(lua_Hook, OV_U64(a));
  int p2 = (int)OV_U64(a);
  int p3 = (int)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  int r = lua_sethook(p0, p1, p2, p3);
  OV_PROTECT_END(c);
  OV_RET_I32(c, r);
}

/* 0x100e1a1e0 lua_gethook */
void F_100e1a1e0(rc_ctx *c) {
  OV_ARGS(a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  lua_Hook r = lua_gethook(p0);
  OV_RET_PTR(c, r);
}

/* 0x100e1a1f0 lua_gethookmask */
void F_100e1a1f0(rc_ctx *c) {
  OV_ARGS(a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int r = lua_gethookmask(p0);
  OV_RET_I32(c, r);
}

/* 0x100e1a200 lua_gethookcount */
void F_100e1a200(rc_ctx *c) {
  OV_ARGS(a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int r = lua_gethookcount(p0);
  OV_RET_I32(c, r);
}

/* 0x100e1a210 lua_getstack */
void F_100e1a210(rc_ctx *c) {
  OV_ARGS(a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  lua_Debug * p2 = (lua_Debug *)(uintptr_t)OV_U64(a);
  int r = lua_getstack(p0, p1, p2);
  OV_RET_I32(c, r);
}

/* 0x100e1a270 lua_getlocal */
void F_100e1a270(rc_ctx *c) {
  OV_ARGS(a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  const lua_Debug * p1 = (const lua_Debug *)(uintptr_t)OV_U64(a);
  int p2 = (int)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  const char * r = lua_getlocal(p0, p1, p2);
  OV_PROTECT_END(c);
  OV_RET_PTR(c, r);
}

/* 0x100e1a360 lua_setlocal */
void F_100e1a360(rc_ctx *c) {
  OV_ARGS(a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  const lua_Debug * p1 = (const lua_Debug *)(uintptr_t)OV_U64(a);
  int p2 = (int)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  const char * r = lua_setlocal(p0, p1, p2);
  OV_PROTECT_END(c);
  OV_RET_PTR(c, r);
}

/* 0x100e1a450 lua_getinfo */
void F_100e1a450(rc_ctx *c) {
  OV_ARGS(a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  const char * p1 = (const char *)(uintptr_t)OV_U64(a);
  lua_Debug * p2 = (lua_Debug *)(uintptr_t)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  int r = lua_getinfo(p0, p1, p2);
  OV_PROTECT_END(c);
  OV_RET_I32(c, r);
}

/* 0x100e1c740 lua_resume */
void F_100e1c740(rc_ctx *c) {
  OV_ARGS(a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  int r = lua_resume(p0, p1);
  OV_PROTECT_END(c);
  ov_lua_trace(p0, r, "resume", NULL);
  OV_RET_I32(c, r);
}

/* 0x100e1c980 lua_yield */
void F_100e1c980(rc_ctx *c) {
  OV_ARGS(a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  int p1 = (int)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  int r = lua_yield(p0, p1);
  OV_PROTECT_END(c);
  OV_RET_I32(c, r);
}

/* 0x100e29dd0 lua_newstate */
void F_100e29dd0(rc_ctx *c) {
  OV_ARGS(a, c);
  lua_Alloc p0 = OV_FN(lua_Alloc, OV_U64(a));
  void * p1 = (void *)(uintptr_t)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  lua_State * r = lua_newstate(p0, p1);
  OV_PROTECT_END(c);
  OV_RET_PTR(c, r);
}

/* 0x100e2a1e0 lua_close */
void F_100e2a1e0(rc_ctx *c) {
  OV_ARGS(a, c);
  lua_State * p0 = (lua_State *)(uintptr_t)OV_U64(a);
  OV_PROTECT_BEGIN(c);
  lua_close(p0);
  OV_PROTECT_END(c);
  ov_ret_void(c);
}
