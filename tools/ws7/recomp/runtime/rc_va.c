// Recompiler runtime: variadic arguments across the x86-64 / arm64 ABI gap.
//
// A guest printf-style call passes its variadic arguments in rsi.. / xmm0..7 /
// stack (x86-64 SysV). A native arm64 Darwin function takes them in 8-byte
// stack slots, which is what its va_list points to. We parse the format to
// learn the type of every argument, fetch each from the guest, and lay them
// out as slots; the thunk then calls the native v* function.
#include <dlfcn.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rc_call.h"

size_t rc_va_max(const char *fmt) {
  size_t n = 1;
  for (; *fmt; fmt++)
    if (*fmt == '%') n += 3;  // "%*.*d" takes three
  return n;
}

// Where the arguments come from.
typedef struct {
  rc_args *a;           // guest registers + stack (a variadic call) or
  uint32_t gp, fp;      // an x86-64 va_list (copied, so the guest's stays untouched)
  const uint8_t *reg_save;
  const uint8_t *overflow;
  int from_va;
} src;

static uint64_t fetch_i(src *s) {
  if (!s->from_va) return rc_arg_i(s->a);
  uint64_t v;
  if (s->gp < 48) { memcpy(&v, s->reg_save + s->gp, 8); s->gp += 8; }
  else { memcpy(&v, s->overflow, 8); s->overflow += 8; }
  return v;
}
static uint64_t fetch_d(src *s) {
  if (!s->from_va) return rc_arg_f(s->a);
  uint64_t v;
  if (s->fp < 176) { memcpy(&v, s->reg_save + s->fp, 8); s->fp += 16; }
  else { memcpy(&v, s->overflow, 8); s->overflow += 8; }
  return v;
}
// x87 80-bit extended -> double (arm64 Darwin's long double is a double).
static double x87_to_double(const uint8_t *p) {
  uint64_t mant;
  uint16_t se;
  memcpy(&mant, p, 8);
  memcpy(&se, p + 8, 2);
  int e = se & 0x7fff;
  double sign = (se & 0x8000) ? -1.0 : 1.0;
  if (e == 0 && mant == 0) return sign * 0.0;
  if (e == 0x7fff) return (mant << 1) ? NAN : sign * INFINITY;
  return sign * ldexp((double)mant, e - 16383 - 63);
}
static uint64_t fetch_ld(src *s) {
  uint8_t raw[16];
  if (!s->from_va) {  // class MEMORY: always on the stack, 16-aligned
    rc_args *a = s->a;
    a->sp = (a->sp + 15) & ~15ull;
    memcpy(raw, (const void *)(uintptr_t)a->sp, 16);
    a->sp += 16;
  } else {
    uintptr_t p = ((uintptr_t)s->overflow + 15) & ~(uintptr_t)15;
    memcpy(raw, (const void *)p, 16);
    s->overflow = (const uint8_t *)(p + 16);
  }
  double d = x87_to_double(raw);
  uint64_t v;
  memcpy(&v, &d, 8);
  return v;
}

static void *build(src *s, const char *fmt, int kind, uint64_t *slots) {
  size_t n = 0;
  const char *p = fmt;
  while (*p) {
    if (*p++ != '%') continue;
    if (kind == RC_VA_SCANF) {
      int suppress = 0;
      if (*p == '%') { p++; continue; }
      if (*p == '*') { suppress = 1; p++; }
      while (*p >= '0' && *p <= '9') p++;
      while (*p == 'h' || *p == 'l' || *p == 'j' || *p == 'z' || *p == 't' || *p == 'L' || *p == 'q') p++;
      if (*p == '[') {  // scanset: "]" first (after an optional ^) is literal
        p++;
        if (*p == '^') p++;
        if (*p == ']') p++;
        while (*p && *p != ']') p++;
      }
      if (*p) p++;
      if (!suppress) slots[n++] = fetch_i(s);
      continue;
    }
    if (*p == '%') { p++; continue; }
    for (const char *q = p; *q >= '0' && *q <= '9'; q++)
      if (q[1] == '$') rc_fail("printf positional argument", 0);
    while (*p == '-' || *p == '+' || *p == ' ' || *p == '#' || *p == '0' || *p == '\'') p++;
    if (*p == '*') { slots[n++] = fetch_i(s); p++; }
    else while (*p >= '0' && *p <= '9') p++;
    if (*p == '.') {
      p++;
      if (*p == '*') { slots[n++] = fetch_i(s); p++; }
      else while (*p >= '0' && *p <= '9') p++;
    }
    int ldbl = 0;
    for (;; p++) {
      if (*p == 'L') ldbl = 1;
      else if (!(*p == 'h' || *p == 'l' || *p == 'j' || *p == 'z' || *p == 't' || *p == 'q')) break;
    }
    switch (*p) {
      case 'e': case 'E': case 'f': case 'F': case 'g': case 'G': case 'a': case 'A':
        slots[n++] = ldbl ? fetch_ld(s) : fetch_d(s);
        break;
      case 0: continue;
      default:  // d i u o x X c C s S p n @ ...: integer class
        slots[n++] = fetch_i(s);
    }
    p++;
  }
  return slots;
}

void *rc_va_from_regs(rc_args *a, const char *fmt, int kind, uint64_t *slots) {
  src s = {.a = a, .from_va = 0};
  return build(&s, fmt, kind, slots);
}

void *rc_va_from_x86(const void *va, const char *fmt, int kind, uint64_t *slots) {
  struct { uint32_t gp, fp; const uint8_t *overflow, *reg_save; } l;
  memcpy(&l, va, sizeof l);
  src s = {.gp = l.gp, .fp = l.fp, .reg_save = l.reg_save, .overflow = l.overflow, .from_va = 1};
  return build(&s, fmt, kind, slots);
}

// CFString -> UTF-8 through CoreFoundation, looked up at run time so that builds without it link.
const char *rc_va_nsfmt(uint64_t cfstring, char *buf, size_t cap) {
  typedef unsigned char (*get_cstring)(const void *, char *, long, unsigned);
  static get_cstring fn;
  if (!fn) fn = (get_cstring)dlsym(RTLD_DEFAULT, "CFStringGetCString");
  buf[0] = 0;
  if (cfstring && fn) fn((const void *)(uintptr_t)cfstring, buf, (long)cap, 0x08000100 /* kCFStringEncodingUTF8 */);
  return buf;
}
