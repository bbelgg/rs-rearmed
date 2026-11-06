// Native overrides of msvcr100's math exports (docs/windows/DESIGN.md "Libraries", option (c)).
//
// msvcr100 is translated like the game, so its math runs as translated x87 / SSE2 code: a
// _CIpow is ~100 guest instructions through the x87 helpers of rc32_x87.h. These overrides run
// the host's libm instead, bound through the image registry (rc32_override_add), so the game's
// imports, d3dx9_42's and msvcp100's, and pointers to the exports, all reach them; msvcr100's
// own internal calls keep the translated code.
//
// Conventions (as the translated export is called: the return address at guest esp):
//   _CIxxx    the "intrinsic" entry points MSVC calls for inline math: the argument in st(0)
//             (binary ones: the first argument in st(1), the second in st(0)), the result
//             replaces them in st(0): unary ones keep the stack depth, binary ones pop one.
//   xxx       cdecl: double arguments on the stack (8 bytes each), the result in st(0)
//             (pushed), an int result in eax; the caller removes the arguments.
// The x87 stack holds doubles (rc32_x87.h), and the CRT runs x87 at 53-bit precision, so a
// double result is what the CRT computes, to the last bit or so: libm's are correctly rounded
// or within 1 ulp, the CRT's x87 / SSE2 code within about the same.
//
// Errors are reported as msvcr100 reports them (crt_probe compares the two over a grid of
// inputs): the result the CRT returns, and errno (EDOM 33, ERANGE 34, the same numbers on
// Windows and here) in the guest's per-thread errno, written through msvcr100's own _errno.
// Not modelled: the x87 exception flags in the status word (the runtime's x87 never sets them)
// and _matherr (the game installs none of its own). The rules are listed below (crt1, crt2).
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "rc32.h"
#include "rc32_x87.h"
#include "win32_shims.h"

#define W32_EINVAL 22
#define W32_EDOM 33
#define W32_ERANGE 34

// --- the guest's errno (msvcr100's per-thread data, through its own _errno) --------------------
static uint32_t errno_fn;  // guest address of msvcr100!_errno, found on first use

static void set_errno(rc_ctx *c, int e) {
  uint32_t f = __atomic_load_n(&errno_fn, __ATOMIC_RELAXED);
  if (!f) {
    f = rc32_image_export(rc32_image_base("msvcr100.dll"), "_errno");
    if (!f) return;
    __atomic_store_n(&errno_fn, f, __ATOMIC_RELAXED);
  }
  // a nested guest call on this context: it uses the stack below esp and eax/ecx/edx, which a
  // math export clobbers anyway; the caller's ret_to is set after this (crt_ret)
  uint32_t p = rc32_call(c, f, 0, 0, 0, 0, 0);
  if (p) memcpy(RC32_H(p), &e, 4);
}

// MSVC's "indefinite" quiet NaN (sign set), what the CRT returns for a domain error
static double nan_ind(void) {
  uint64_t b = 0xfff8000000000000ull;
  double d;
  memcpy(&d, &b, 8);
  return d;
}

// --- the C runtime's error rules ---------------------------------------------------------------
// What msvcr100 does (its SSE2 paths, which the game takes; crt_probe's differential test, the
// translated CRT against these, found each rule):
//   - a NaN argument: EDOM, the NaN comes back (quieted). sqrt only for a NaN with the sign
//     bit set (it tests the sign: "negative"). pow(NaN, 0) too is NaN
//     with EDOM (C99 says 1); pow(x, 0) is 1 for every other x.
//   - a domain error (sqrt / log of a negative, asin / acos outside [-1, 1], fmod(x, 0), fmod
//     with an infinite argument (fmod(1, inf) is 1 in C99), atan2(+-inf, +-inf) (C99: +-pi/4,
//     +-3pi/4), pow of a negative base to a non-integer): EDOM, the indefinite NaN.
//   - a pole (log(0), pow(0, negative)): ERANGE, -inf / +inf.
//   - overflow: ERANGE, +-inf. exp / sinh / cosh overflow once exp(|x|) does (|x| > ln DBL_MAX),
//     as the CRT computes them through exp (sinh(709.9) is finite in libm, inf in the CRT).
//   - underflow to 0: no errno.
//   - sin / cos / tan of an infinity: the indefinite NaN, EDOM.
//   - pow(+-1, +-inf): 1 and EDOM; pow(0, -inf): +inf, no errno.
enum { F_SQRT, F_LOG, F_LOG10, F_EXP, F_SIN, F_COS, F_TAN, F_ASIN, F_ACOS, F_ATAN, F_SINH,
       F_COSH, F_TANH, F_ATAN2, F_POW, F_FMOD };

#define LN_DBL_MAX 709.78271289338397  // ln(DBL_MAX): exp overflows above it

static double crt1(rc_ctx *c, int op, double x) {
  double r;
  int e = 0;
  if (x != x) {
    if (op != F_SQRT || signbit(x)) set_errno(c, W32_EDOM);
    return x + x;
  }
  switch (op) {
    case F_SQRT:
      if (x < 0) { r = nan_ind(); e = W32_EDOM; } else r = sqrt(x);
      break;
    case F_LOG:
    case F_LOG10:
      if (x < 0) { r = nan_ind(); e = W32_EDOM; }
      else if (x == 0) { r = -INFINITY; e = W32_ERANGE; }
      else r = op == F_LOG ? log(x) : log10(x);
      break;
    case F_EXP:
      r = exp(x);
      if (isinf(r) && !isinf(x)) e = W32_ERANGE;
      break;
    case F_SIN:
    case F_COS:
    case F_TAN:
      if (isinf(x)) { r = nan_ind(); e = W32_EDOM; }
      else r = op == F_SIN ? sin(x) : op == F_COS ? cos(x) : tan(x);
      break;
    case F_ASIN:
    case F_ACOS:
      if (fabs(x) > 1) { r = nan_ind(); e = W32_EDOM; }
      else r = op == F_ASIN ? asin(x) : acos(x);
      break;
    case F_ATAN: r = atan(x); break;
    case F_SINH:
    case F_COSH:
      if (fabs(x) > LN_DBL_MAX && !isinf(x)) {
        r = op == F_SINH ? copysign(INFINITY, x) : INFINITY;
        e = W32_ERANGE;
      } else {
        r = op == F_SINH ? sinh(x) : cosh(x);
      }
      break;
    default: r = tanh(x); break;
  }
  if (e) set_errno(c, e);
  return r;
}

static double crt2(rc_ctx *c, int op, double x, double y) {
  double r;
  int e = 0;
  if (op == F_POW && y == 0 && x == x) return 1.0;
  if (x != x || y != y) {
    set_errno(c, W32_EDOM);
    return x + y;
  }
  switch (op) {
    case F_ATAN2:
      // x = the y coordinate, y = the x coordinate (atan2's own order)
      if (isinf(x) && isinf(y)) { r = nan_ind(); e = W32_EDOM; }
      else r = atan2(x, y);
      break;
    case F_FMOD:
      if (y == 0 || isinf(x) || isinf(y)) { r = nan_ind(); e = W32_EDOM; }
      else r = fmod(x, y);
      break;
    default:  // F_POW
      if (x < 0 && !isinf(x) && !isinf(y) && y != nearbyint(y)) { r = nan_ind(); e = W32_EDOM; break; }
      r = pow(x, y);
      if (fabs(x) == 1 && isinf(y)) e = W32_EDOM;
      else if (x == 0 && y < 0 && !isinf(y)) e = W32_ERANGE;
      else if (isinf(r) && !isinf(x) && !isinf(y)) e = W32_ERANGE;
      break;
  }
  if (e) set_errno(c, e);
  return r;
}


// --- the entry points ----------------------------------------------------------------------------
// `ret`: the return address into c->ret_to, as a guest ret records it (rc32_gen.h)
static inline void crt_ret(rc_ctx *c) {
  memcpy(&c->ret_to, RC32_H((uint32_t)c->r[4]), 4);
  c->r[4] = (uint32_t)(c->r[4] + 4);
}
// a cdecl double argument: 8 bytes at esp + 4 + off (past the return address)
static inline double carg_d(rc_ctx *c, uint32_t off) {
  double d;
  memcpy(&d, RC32_H((uint32_t)c->r[4] + 4 + off), 8);
  return d;
}
static inline uint32_t carg_u(rc_ctx *c, uint32_t off) { return w32_rd32((uint32_t)c->r[4] + 4 + off); }

#define CI1(nm, op) \
  static void ci_##nm(rc_ctx *c) { ST(0) = crt1(c, op, ST(0)); crt_ret(c); }
#define CI2(nm, op) \
  static void ci_##nm(rc_ctx *c) { \
    double y = ST(0), x = ST(1); \
    rc87_pop(c); \
    ST(0) = crt2(c, op, x, y); \
    crt_ret(c); \
  }
CI1(sqrt, F_SQRT)
CI1(log, F_LOG)
CI1(log10, F_LOG10)
CI1(exp, F_EXP)
CI1(sin, F_SIN)
CI1(cos, F_COS)
CI1(tan, F_TAN)
CI1(asin, F_ASIN)
CI1(acos, F_ACOS)
CI1(atan, F_ATAN)
CI1(sinh, F_SINH)
CI1(cosh, F_COSH)
CI1(tanh, F_TANH)
CI2(atan2, F_ATAN2)
CI2(pow, F_POW)
CI2(fmod, F_FMOD)

// cdecl, a double result pushed on the x87 stack. A NaN argument sets EDOM here too.
static inline double nan_arg(rc_ctx *c, double x) {
  if (x != x) set_errno(c, W32_EDOM);
  return x;
}
static void c_log(rc_ctx *c) { double r = crt1(c, F_LOG, carg_d(c, 0)); rc87_push(c, r); crt_ret(c); }
static void c_ceil(rc_ctx *c) { rc87_push(c, ceil(nan_arg(c, carg_d(c, 0)))); crt_ret(c); }
static void c_floor(rc_ctx *c) { rc87_push(c, floor(nan_arg(c, carg_d(c, 0)))); crt_ret(c); }
// modf(x, double *ip): the fraction; the integral part through ip
static void c_modf(rc_ctx *c) {
  double ip, f = modf(nan_arg(c, carg_d(c, 0)), &ip);
  uint32_t p = carg_u(c, 8);
  if (p) memcpy(RC32_H(p), &ip, 8);
  rc87_push(c, f);
  crt_ret(c);
}
// frexp(x, int *e); NaN and +-inf: EDOM, *e = -1, an infinity gives the indefinite NaN
static void c_frexp(rc_ctx *c) {
  int e = 0;
  double x = carg_d(c, 0), f = frexp(x, &e);
  uint32_t p = carg_u(c, 8);
  if (x != x || isinf(x)) {
    e = -1;
    f = x != x ? x + x : nan_ind();
    set_errno(c, W32_EDOM);
  }
  if (p) w32_wr32(p, (uint32_t)e);
  rc87_push(c, f);
  crt_ret(c);
}
// ldexp(x, int e): ERANGE on overflow
static void c_ldexp(rc_ctx *c) {
  double x = nan_arg(c, carg_d(c, 0)), r = ldexp(x, (int)carg_u(c, 8));
  if (isinf(r) && !isinf(x) && x == x) set_errno(c, W32_ERANGE);
  rc87_push(c, r);
  crt_ret(c);
}
// int results in eax
static void c_isnan(rc_ctx *c) { double x = carg_d(c, 0); c->r[0] = x != x; crt_ret(c); }
static void c_finite(rc_ctx *c) { double x = carg_d(c, 0); c->r[0] = isfinite(x) != 0; crt_ret(c); }

static const struct { const char *name; rc_fn fn; } overrides[] = {
    {"_CIsqrt", ci_sqrt}, {"_CIlog", ci_log}, {"_CIlog10", ci_log10}, {"_CIexp", ci_exp},
    {"_CIsin", ci_sin}, {"_CIcos", ci_cos}, {"_CItan", ci_tan}, {"_CIasin", ci_asin},
    {"_CIacos", ci_acos}, {"_CIatan", ci_atan}, {"_CIsinh", ci_sinh}, {"_CIcosh", ci_cosh},
    {"_CItanh", ci_tanh}, {"_CIatan2", ci_atan2}, {"_CIpow", ci_pow}, {"_CIfmod", ci_fmod},
    {"log", c_log}, {"ceil", c_ceil}, {"floor", c_floor}, {"modf", c_modf}, {"frexp", c_frexp},
    {"ldexp", c_ldexp}, {"_isnan", c_isnan}, {"_finite", c_finite},
};

// --- memory -------------------------------------------------------------------------------------
// The CRT's copy and fill as host calls: the translated ones go a dword at a time through jump tables, with a fence
// for each access, and the game copies through memcpy_s all the time. Fenced once around the call, as a
// `rep movs` is (rc32_gen.h): each RC_LD is followed by a load fence and each RC_ST preceded by a store fence,
// so the two ends need only the other half. The CRT's memcpy copes with overlap, as memmove does.
static inline void mem_fence_in(void) { __asm__ volatile("dmb ishst" ::: "memory"); }
static inline void mem_fence_out(void) { __asm__ volatile("dmb ishld" ::: "memory"); }

static void c_memmove(rc_ctx *c) {
  uint32_t d = carg_u(c, 0), s = carg_u(c, 4), n = carg_u(c, 8);
  mem_fence_in();
  memmove(RC32_H(d), RC32_H(s), n);
  mem_fence_out();
  c->r[0] = d;
  crt_ret(c);
}
static void c_memset(rc_ctx *c) {
  uint32_t d = carg_u(c, 0), v = carg_u(c, 4), n = carg_u(c, 8);
  mem_fence_in();
  memset(RC32_H(d), (int)(v & 0xff), n);
  mem_fence_out();
  c->r[0] = d;
  crt_ret(c);
}
// memcpy_s: as the CRT does (its invalid parameter handler aside, which ends a Windows process with the defaults),
// nothing for a count of 0, EINVAL for a null, the destination cleared and ERANGE or EINVAL for what doesn't fit.
static void c_memcpy_s(rc_ctx *c) {
  uint32_t d = carg_u(c, 0), size = carg_u(c, 4), s = carg_u(c, 8), n = carg_u(c, 12), r = 0;
  if (n) {
    if (!d) r = W32_EINVAL;
    else if (!s || size < n) {
      memset(RC32_H(d), 0, size);
      r = s ? W32_ERANGE : W32_EINVAL;
    } else {
      mem_fence_in();
      memmove(RC32_H(d), RC32_H(s), n);
      mem_fence_out();
    }
    if (r) set_errno(c, (int)r);
  }
  c->r[0] = r;
  crt_ret(c);
}

static const struct { const char *name; rc_fn fn; } mem_overrides[] = {
    {"memcpy", c_memmove}, {"memmove", c_memmove}, {"memset", c_memset}, {"memcpy_s", c_memcpy_s},
};

int w32_crtmem_register(void) {
  const char *off = getenv("LP_NATIVE_MEM");
  if (off && off[0] == '0') {
    fprintf(stderr, "lp: msvcr100 memory exports run translated (LP_NATIVE_MEM=0)\n");
    return 0;
  }
  int n = 0;
  for (size_t k = 0; k < sizeof mem_overrides / sizeof mem_overrides[0]; k++)
    n += !rc32_override_add("msvcr100.dll", mem_overrides[k].name, mem_overrides[k].fn);
  return n;
}

rc_fn w32_crtmath_fn(const char *name) {
  for (size_t k = 0; k < sizeof overrides / sizeof overrides[0]; k++)
    if (!strcmp(overrides[k].name, name)) return overrides[k].fn;
  return 0;
}

int w32_crtmath_register(void) {
  const char *off = getenv("LP_NATIVE_MATH");
  if (off && off[0] == '0') {
    fprintf(stderr, "lp: msvcr100 math runs translated (LP_NATIVE_MATH=0)\n");
    return 0;
  }
  int n = 0;
  for (size_t k = 0; k < sizeof overrides / sizeof overrides[0]; k++)
    n += !rc32_override_add("msvcr100.dll", overrides[k].name, overrides[k].fn);
  return n;
}
