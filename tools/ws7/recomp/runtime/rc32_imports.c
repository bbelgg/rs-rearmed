// 32-bit guests: the default rc32_import_call, which every generated import thunk calls
// (build/gen_image_pe.py: imports.c). The runtime implements the few imports that belong to it
// (exception dispatch, TLS slots, the last-error value); the rest of the Win32 surface is the
// platform seams' job, and an import nothing implements fails with its name.
//
// A thunk runs with the arguments at guest esp (no return address) and removes them itself for
// stdcall functions, which all of the ones here are.
#include <stdio.h>
#include <string.h>

#include "rc32.h"

void rc32_imp_RaiseException(rc_ctx *c);  // rc32_seh.c
void rc32_imp_RtlUnwind(rc_ctx *c);

static uint32_t arg(rc_ctx *c, int k) { uint32_t v; memcpy(&v, rc_mem + (uint32_t)(c->r[4] + 4 * k), 4); return v; }
static void ret(rc_ctx *c, uint32_t v, int nargs) { c->r[0] = v; c->r[4] = (uint32_t)(c->r[4] + 4 * nargs); }
static uint32_t *teb32(rc_ctx *c, uint32_t off) { return (uint32_t *)(rc_mem + c->fs + off); }

static uint64_t tls_used;  // TlsAlloc slots in use (process-wide, like Windows)
static void imp_TlsAlloc(rc_ctx *c) {
  for (int k = 0; k < 64; k++)
    if (!(__atomic_fetch_or(&tls_used, 1ull << k, __ATOMIC_SEQ_CST) & (1ull << k))) { ret(c, k, 0); return; }
  ret(c, 0xffffffffu, 0);
}
static void imp_TlsFree(rc_ctx *c) {
  uint32_t k = arg(c, 0);
  if (k < 64) __atomic_fetch_and(&tls_used, ~(1ull << k), __ATOMIC_SEQ_CST);
  ret(c, k < 64, 1);
}
static void imp_TlsGetValue(rc_ctx *c) {
  uint32_t k = arg(c, 0);
  if (k < 64) *teb32(c, RC32_TEB_LASTERR) = 0;
  ret(c, k < 64 ? *teb32(c, RC32_TEB_TLSSLOTS + 4 * k) : 0, 1);
}
static void imp_TlsSetValue(rc_ctx *c) {
  uint32_t k = arg(c, 0);
  if (k < 64) *teb32(c, RC32_TEB_TLSSLOTS + 4 * k) = arg(c, 1);
  ret(c, k < 64, 2);
}
static void imp_GetLastError(rc_ctx *c) { ret(c, *teb32(c, RC32_TEB_LASTERR), 0); }
static void imp_SetLastError(rc_ctx *c) { *teb32(c, RC32_TEB_LASTERR) = arg(c, 0); ret(c, 0, 1); }
static void imp_GetCurrentThreadId(rc_ctx *c) { ret(c, *teb32(c, RC32_TEB_TID), 0); }
static void imp_GetCurrentProcessId(rc_ctx *c) { ret(c, *teb32(c, RC32_TEB_PID), 0); }

static const struct { const char *name; rc_fn fn; } builtin[] = {
    {"kernel32.dll!RaiseException", rc32_imp_RaiseException},
    {"kernel32.dll!RtlUnwind", rc32_imp_RtlUnwind},
    {"ntdll.dll!RtlUnwind", rc32_imp_RtlUnwind},
    {"kernel32.dll!TlsAlloc", imp_TlsAlloc},
    {"kernel32.dll!TlsFree", imp_TlsFree},
    {"kernel32.dll!TlsGetValue", imp_TlsGetValue},
    {"kernel32.dll!TlsSetValue", imp_TlsSetValue},
    {"kernel32.dll!GetLastError", imp_GetLastError},
    {"kernel32.dll!SetLastError", imp_SetLastError},
    {"kernel32.dll!GetCurrentThreadId", imp_GetCurrentThreadId},
    {"kernel32.dll!GetCurrentProcessId", imp_GetCurrentProcessId},
};

void rc32_import_call(rc_ctx *c, int k) {
  static rc_fn cache[4096];
  rc_fn f = k < 4096 ? __atomic_load_n(&cache[k], __ATOMIC_RELAXED) : 0;
  if (!f) {
    for (size_t j = 0; j < sizeof builtin / sizeof builtin[0]; j++)
      if (!strcmp(builtin[j].name, rc32_imports[k].name)) f = builtin[j].fn;
    if (!f) {
      fprintf(stderr, "rc32: import not implemented: %s\n", rc32_imports[k].name);
      rc_fail("import not implemented", rc32_imports[k].slot);
    }
    if (k < 4096) __atomic_store_n(&cache[k], f, __ATOMIC_RELAXED);
  }
  f(c);
}
