// Lua adapters that are not a plain argument-to-call translation.
//   lua_pushfstring (0x100e11940) and luaL_error (0x100e133c0) are variadic: the guest passes
//   the arguments in registers and on its stack, the native vsprintf-style functions want an
//   arm64 va_list (rc_va_from_regs builds one from the format).
//   0x100e10680 is an API function of the game's LNUM version that ours lacks (see lua_isintvalue).
//   luaL_register (0x100e13d70) takes an array of {name, lua_CFunction} in guest memory whose
//   function pointers are guest functions.
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "lauxlib.h"
#include "luaconf_internal.h"  // LUAI_FUNC for the internal headers
#include "lstate.h"
#include "lua.h"
#include "lvm.h"
#include "lua_glue.h"
#include "rc_ov.h"

static int trace_on = -1;

static int tracing(void) {
  if (trace_on < 0) {
    const char *e = getenv("RS_LUA_TRACE");
    trace_on = e && *e && *e != '0';
  }
  return trace_on;
}

void ov_lua_trace(lua_State *L, int status, const char *what, const char *chunk) {
  if (!tracing()) return;
  if (status == LUA_YIELD) return;  // lua_resume: the coroutine yielded
  if (status == 0) {  // a successful load is logged too: which scripts run when
    if (chunk) fprintf(stderr, "[lua] %s %s\n", what, chunk);
    return;
  }
  const char *msg = lua_type(L, -1) == LUA_TSTRING ? lua_tostring(L, -1) : lua_typename(L, lua_type(L, -1));
  fprintf(stderr, "[lua] %s%s%s -> %d: %s\n", what, chunk ? " " : "", chunk ? chunk : "", status, msg);
}

/* 0x100e10680 (no name in the image; one caller, 0x1001767f0): true if the value converts to a
   number (tonumber) and the result is an integer-typed one (LUA_TINT). Unlike lua_isinteger, an
   integral float such as 2.0 does not count. */
void F_100e10680(rc_ctx *c) {
  OV_ARGS(a, c);
  lua_State *L = OV_PTR(a, lua_State);
  int idx = (int)OV_U64(a);
  TValue tmp;
  lua_pushvalue(L, idx);
  const TValue *n = luaV_tonumber(L->top - 1, &tmp);
  int r = n && ttisint(n);
  lua_settop(L, -2);
  OV_RET_I32(c, r);
}

/* 0x100e11940 lua_pushfstring */
void F_100e11940(rc_ctx *c) {
  OV_ARGS(a, c);
  lua_State *L = OV_PTR(a, lua_State);
  const char *fmt = OV_PTR(a, const char);
  uint64_t slots[rc_va_max(fmt)];
  void *vl = rc_va_from_regs(&a, fmt, RC_VA_PRINTF, slots);
  va_list ap;
  memcpy(&ap, &vl, sizeof ap);
  OV_PROTECT_BEGIN(c);  // may collect garbage: finalizers can unwind through guest frames
  const char *r = lua_pushvfstring(L, fmt, ap);
  OV_PROTECT_END(c);
  OV_RET_PTR(c, r);
}

/* 0x100e133c0 luaL_error: luaL_where(L, 1), the formatted message, lua_concat, lua_error (never returns) */
void F_100e133c0(rc_ctx *c) {
  OV_ARGS(a, c);
  lua_State *L = OV_PTR(a, lua_State);
  const char *fmt = OV_PTR(a, const char);
  uint64_t slots[rc_va_max(fmt)];
  void *vl = rc_va_from_regs(&a, fmt, RC_VA_PRINTF, slots);
  va_list ap;
  memcpy(&ap, &vl, sizeof ap);
  luaL_where(L, 1);
  lua_pushvfstring(L, fmt, ap);
  lua_concat(L, 2);
  ov_lua_trace(L, -1, "luaL_error", NULL);
  lua_error(L);
  ov_ret_u32(c, 0);
}

/* 0x100e13d70 luaL_register(L, libname, const luaL_Reg *l) */
void F_100e13d70(rc_ctx *c) {
  OV_ARGS(a, c);
  lua_State *L = OV_PTR(a, lua_State);
  const char *libname = OV_PTR(a, const char);
  const luaL_Reg *l = OV_PTR(a, const luaL_Reg);
  size_t n = 0;
  if (l)
    while (l[n].name) n++;
  luaL_Reg tmp[n + 1];
  for (size_t i = 0; i < n; i++) {
    tmp[i].name = l[i].name;
    tmp[i].func = OV_FN(lua_CFunction, l[i].func);
  }
  tmp[n].name = NULL;
  tmp[n].func = NULL;
  OV_PROTECT_BEGIN(c);
  luaL_register(L, libname, l ? tmp : NULL);
  OV_PROTECT_END(c);
  ov_ret_void(c);
}
