// crt_probe's math part: the native overrides of msvcr100's math exports
// (win-x86/shims/win32_crtmath.c) against the translated exports they replace.
//
// - Binding: rc32_lookup of each export's address gives the override, and an import of one in
//   another image (msvcp100's msvcr100.dll!log) runs it through its import thunk.
// - Differential: every override and the translated export run on the same inputs, a grid of
//   ordinary values, edges (+-0, +-inf, NaN, denormals, overflow / underflow, poles, domain
//   errors) and pseudo-random values, with the CRT on its SSE2 paths (as in the game: the CPU
//   reports SSE2) and on its x87 paths. Compared: the result's bits (in ulps), errno, the x87
//   stack depth, the return address and esp, out-parameters.
// - Benchmark (-bench): ns per call, translated vs native.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "rc32.h"
#include "rc32_x87.h"
#include "win32_shims.h"

int crt_math_probe(rc_ctx *c, const rc32_module *crt, const rc32_module *cpp, int verbose, int bench);
void crt_math_probe_setup(void);

#define SENT 0xfffffff0u  // the return address the calls below push

enum { K_CI1, K_CI2, K_D, K_DPTR, K_DINT, K_INT };
static const struct { const char *name; int kind; } fns[] = {
    {"_CIsqrt", K_CI1}, {"_CIlog", K_CI1}, {"_CIlog10", K_CI1}, {"_CIexp", K_CI1},
    {"_CIsin", K_CI1}, {"_CIcos", K_CI1}, {"_CItan", K_CI1}, {"_CIasin", K_CI1},
    {"_CIacos", K_CI1}, {"_CIatan", K_CI1}, {"_CIsinh", K_CI1}, {"_CIcosh", K_CI1},
    {"_CItanh", K_CI1}, {"_CIatan2", K_CI2}, {"_CIpow", K_CI2}, {"_CIfmod", K_CI2},
    {"log", K_D}, {"ceil", K_D}, {"floor", K_D}, {"modf", K_DPTR}, {"frexp", K_DPTR},
    {"ldexp", K_DINT}, {"_isnan", K_INT}, {"_finite", K_INT},
};
#define NFNS ((int)(sizeof fns / sizeof fns[0]))

// --- a counting override of log, to see the import thunk reach the registry's choice ---------
static rc_fn native_log;
static int log_calls;
static void counting_log(rc_ctx *c) { log_calls++; native_log(c); }
void crt_math_probe_setup(void) {
  native_log = w32_crtmath_fn("log");
  rc32_override_add("msvcr100.dll", "log", counting_log);  // replaces w32_crtmath_register's
}

static rc_fn table_fn(const rc32_module *m, uint32_t g) {
  int lo = 0, hi = *m->table_len - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    if (m->table[mid].addr == g) return m->table[mid].fn;
    if (m->table[mid].addr < g) lo = mid + 1;
    else hi = mid - 1;
  }
  return 0;
}

static uint32_t errno_va;
static uint32_t guest_errno_addr(rc_ctx *c) { return rc32_call(c, errno_va, 0, 0, 0, 0, 0); }

typedef struct {
  double r;        // st(0), or the int result as a double (K_INT)
  int depth;       // x87 stack depth after the call
  int err;         // errno after (0 before)
  uint64_t out;    // what an out-parameter received
  int abi_ok;      // returned to the pushed address with esp just above it
} result;

static void run(rc_ctx *c, rc_fn f, int kind, double x, double y, int i, result *o) {
  uint64_t esp0 = c->r[4];
  uint32_t ea = guest_errno_addr(c), zero = 0;
  memcpy(RC32_H(ea), &zero, 4);
  uint32_t outp = (uint32_t)esp0 - 64;  // scratch for out-parameters, above the call's frame
  memset(RC32_H(outp), 0xcc, 8);
  uint32_t sp = (uint32_t)esp0 - 128;
  uint32_t a[4] = {0}, n = 0;
  if (kind >= K_D) {
    memcpy(a, &x, 8);
    n = 2;
    if (kind == K_DPTR) a[n++] = outp;
    if (kind == K_DINT) a[n++] = (uint32_t)i;
  }
  sp -= 4 * n + 4;
  memcpy(RC32_H(sp + 4), a, 4 * n);
  uint32_t ra = SENT;
  memcpy(RC32_H(sp), &ra, 4);
  c->r[4] = sp;
  c->r[0] = 0xdeadbeef;
  c->fpu.cw = 0x27f;  // MSVC's default: 53-bit precision, every exception masked
  c->fpu.sw = 0;
  c->fpu.top = 0;
  c->fpu.tag = 0xff;
  c->mxcsr = 0x1f80;
  if (kind == K_CI1 || kind == K_CI2) rc87_push(c, x);
  if (kind == K_CI2) rc87_push(c, y);
  c->ret_to = 0;
  f(c);
  o->abi_ok = c->ret_to == SENT && (uint32_t)c->r[4] == sp + 4;
  int depth = 0;
  for (int k = 0; k < 8; k++) depth += !(c->fpu.tag & (1u << k));
  o->depth = depth;
  o->r = kind == K_INT ? (double)(uint32_t)c->r[0] : depth ? ST(0) : NAN;
  memcpy(&o->out, RC32_H(outp), 8);
  memcpy(&o->err, RC32_H(ea), 4);
  c->r[4] = esp0;
}

static uint64_t bits(double d) { uint64_t b; memcpy(&b, &d, 8); return b; }
// distance in ulps between two non-NaN doubles (ordered as integers)
static uint64_t ulps(double a, double b) {
  int64_t x = (int64_t)bits(a), y = (int64_t)bits(b);
  if (x < 0) x = (int64_t)0x8000000000000000ull - x;
  if (y < 0) y = (int64_t)0x8000000000000000ull - y;
  return x > y ? (uint64_t)(x - y) : (uint64_t)(y - x);
}

// --- inputs ---------------------------------------------------------------------------------------
static double edge[64];
static int nedge;
static void edges(void) {
  const double v[] = {0.0, -0.0, 1.0, -1.0, 0.5, -0.5, 2.0, -2.0, 3.0, -3.0, 10.0, 0.1, 1.5, -1.5, 2.5,
                      0.75, 100.25, 1e-5, 1e-300, -1e-300, 4.9e-324, 2.2250738585072014e-308, 1e300, -1e300,
                      1.7976931348623157e308, 709.0, 709.9, 710.0, -708.0, -745.0, -746.0, 22.0, 89.4, 711.0,
                      3.141592653589793, -1.5707963267948966, 0.7853981633974483, 1e10, 1e15, 1e22,
                      9.2233720368547758e18, 1.0000000000000002, 0.9999999999999999, 1e-8, 33.0, 64.0,
                      INFINITY, -INFINITY, NAN, -NAN};
  nedge = (int)(sizeof v / sizeof v[0]);
  memcpy(edge, v, sizeof v);
}
static uint64_t rng = 0x243f6a8885a308d3ull;
static double rnd(double lo, double hi) {
  rng = rng * 6364136223846793005ull + 1442695040888963407ull;
  return lo + (hi - lo) * (double)(rng >> 11) / 9007199254740992.0;
}

typedef struct {
  long n, exact, ulp1, ulp4, near, worse, nan_sign, err, abi, depth, out, huge, skip;
  double wx, wy;     // the worst case
  uint64_t wu;
} stats;

// Known differences, not failures:
// - sin / cos / tan of |x| >= 2^63: the CRT reduces such arguments with fprem against its own
//   pi, the runtime's x87 does that reduction in double, libm exactly; the results have nothing
//   in common (and none is "right" to the bit on Windows either). Counted as `huge`; errno is
//   still compared.
// - sinh / cosh: the translated CRT loses up to ~2^9 ulp (it computes through exp in x87 code
//   that the runtime runs at double, not extended, precision); libm is within 1 ulp. Up to
//   `tol` ulp counts as `near`.
static int huge_trig(const char *name, double x) {
  return (!strcmp(name, "_CIsin") || !strcmp(name, "_CIcos") || !strcmp(name, "_CItan")) &&
         fabs(x) >= 9223372036854775808.0 && !isinf(x);
}

static void compare(const char *name, int kind, const result *t, const result *nv, double x, double y, int i,
                    stats *s, int verbose, uint64_t tol) {
  s->n++;
  int bad = 0;
  if (t->abi_ok != 1 || nv->abi_ok != 1) { s->abi++; bad = 1; }
  if (t->depth != nv->depth) { s->depth++; bad = 1; }
  if (t->err != nv->err) { s->err++; bad = 1; }
  if (kind == K_DPTR) {
    double to, no;
    memcpy(&to, &t->out, 8);
    memcpy(&no, &nv->out, 8);
    int same = t->out == nv->out || (!strcmp(name, "modf") && to != to && no != no);
    if (!strcmp(name, "frexp")) same = (uint32_t)t->out == (uint32_t)nv->out;
    if (!same) { s->out++; bad = 1; }
  }
  if (huge_trig(name, x)) {
    s->huge++;
  } else if (bits(t->r) == bits(nv->r)) {
    s->exact++;
  } else if (t->r != t->r && nv->r != nv->r) {
    s->nan_sign++;  // both NaN, other sign or payload
  } else if (t->r != t->r || nv->r != nv->r) {
    s->worse++;
    bad = 1;
    s->wu = ~0ull;
    s->wx = x;
    s->wy = y;
  } else {
    uint64_t u = ulps(t->r, nv->r);
    if (u <= 1) s->ulp1++;
    else if (u <= 4) s->ulp4++;
    else if (u <= tol) s->near++;
    else { s->worse++; bad = 1; }
    if (u > s->wu && s->wu != ~0ull) { s->wu = u; s->wx = x; s->wy = y; }
  }
  static int dump = -1;
  if (dump < 0) dump = getenv("MATH_DUMP") != 0;
  if (dump)
    fprintf(stderr, "  %s(%.17g, %.17g, %d): translated %016llx e%d out %llx | native %016llx e%d out %llx%s\n", name, x,
            y, i, (unsigned long long)bits(t->r), t->err, (unsigned long long)t->out, (unsigned long long)bits(nv->r),
            nv->err, (unsigned long long)nv->out, bad ? "  <--" : "");
  else if (bad && verbose)
    fprintf(stderr, "  %s(%.17g, %.17g, %d): translated %.17g (errno %d, depth %d, abi %d, out %llx)"
            " native %.17g (errno %d, depth %d, abi %d, out %llx)\n", name, x, y, i, t->r, t->err,
            t->depth, t->abi_ok, (unsigned long long)t->out, nv->r, nv->err, nv->depth, nv->abi_ok,
            (unsigned long long)nv->out);
}

static int check_fn(rc_ctx *c, int f, rc_fn tr, rc_fn nv, int verbose) {
  const char *name = fns[f].name;
  int kind = fns[f].kind;
  stats s = {0};
  result rt, rn;
  uint64_t tol = !strcmp(name, "_CIsinh") || !strcmp(name, "_CIcosh") ? 512 : 4;
  // the translated _CItanh of a non-finite argument calls 0x78aaf3b1, _CIatan2 / _CIfmod with
  // a NaN (on the SSE2 paths) 0x78aa429f: pieces of msvcr100 the front end did not find as
  // functions (rc_fail). Only the native ones run those cases.
  int tanh_gap = !strcmp(name, "_CItanh"), atan2_gap = !strcmp(name, "_CIatan2") || !strcmp(name, "_CIfmod");
#define ONE(x, y, i) do { \
    if ((tanh_gap && !isfinite(x)) || (atan2_gap && (x != x || y != y))) { s.skip++; break; } \
    run(c, tr, kind, x, y, i, &rt); run(c, nv, kind, x, y, i, &rn); \
    compare(name, kind, &rt, &rn, x, y, i, &s, verbose, tol); } while (0)
  if (kind == K_CI2) {
    for (int a = 0; a < nedge; a++)
      for (int b = 0; b < nedge; b++) ONE(edge[a], edge[b], 0);
    for (int k = 0; k < 3000; k++) {
      double x = k % 3 == 0 ? rnd(-10, 10) : k % 3 == 1 ? rnd(0, 4) : rnd(-1e6, 1e6);
      double y = k % 3 == 0 ? rnd(-10, 10) : k % 3 == 1 ? rnd(-40, 40) : floor(rnd(-20, 20));
      ONE(x, y, 0);
    }
  } else if (kind == K_DINT) {
    const int is[] = {0, 1, -1, 10, -10, 1023, 1024, -1074, -1075, 2000, -2000};
    for (int a = 0; a < nedge; a++)
      for (size_t b = 0; b < sizeof is / sizeof is[0]; b++) ONE(edge[a], 0, is[b]);
  } else {
    for (int a = 0; a < nedge; a++) ONE(edge[a], 0, 0);
    for (int k = 0; k < 3000; k++) {
      double x = k % 4 == 0 ? rnd(-1, 1) : k % 4 == 1 ? rnd(-10, 10) : k % 4 == 2 ? rnd(-800, 800) : rnd(-1e6, 1e6);
      ONE(x, 0, 0);
    }
  }
#undef ONE
  int bad = s.worse || s.err || s.abi || s.depth || s.out;
  fprintf(stderr, "  %-9s %5ld cases: %5ld exact, %4ld <=1 ulp, %3ld <=4 ulp, %ld worse, %ld NaN sign/payload; "
          "errno %ld, abi %ld, depth %ld, out %ld differ%s", name, s.n, s.exact, s.ulp1, s.ulp4, s.worse, s.nan_sign,
          s.err, s.abi, s.depth, s.out, bad ? "  <--" : "");
  if (s.near) fprintf(stderr, "; %ld within %llu ulp", s.near, (unsigned long long)tol);
  if (s.huge) fprintf(stderr, "; %ld |x| >= 2^63 not compared", s.huge);
  if (s.skip) fprintf(stderr, "; %ld non-finite skipped (translated fails)", s.skip);
  if (s.wu && (s.wu > 1 || bad)) fprintf(stderr, "  (worst %s at %.17g, %.17g)",
                                         s.wu == ~0ull ? "NaN vs number" : "", s.wx, s.wy);
  if (s.wu > 1 && s.wu != ~0ull) fprintf(stderr, " %llu ulp", (unsigned long long)s.wu);
  fprintf(stderr, "\n");
  return bad;
}

static double now_ns(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return t.tv_sec * 1e9 + t.tv_nsec;
}

// ns per call of f over varied inputs (the call as the import thunk makes it: no errno reset)
static double bench_fn(rc_ctx *c, rc_fn f, int kind, long n) {
  uint64_t esp0 = c->r[4];
  uint32_t sp = (uint32_t)esp0 - 128;
  volatile double sink = 0;
  double t0 = now_ns();
  for (long k = 0; k < n; k++) {
    double x = 0.5 + (double)(k & 1023) * (1.0 / 512), y = -3.0 + (double)(k % 977) * (6.0 / 977);
    uint32_t ra = SENT;
    c->fpu.top = 0;
    c->fpu.tag = 0xff;
    if (kind == K_D) {
      memcpy(RC32_H(sp + 4), &x, 8);
    } else {
      rc87_push(c, x);
      if (kind == K_CI2) rc87_push(c, y);
    }
    memcpy(RC32_H(sp), &ra, 4);
    c->r[4] = sp;
    f(c);
    sink += ST(0);
  }
  double t = (now_ns() - t0) / (double)n;
  c->r[4] = esp0;
  (void)sink;
  return t;
}

int crt_math_probe(rc_ctx *c, const rc32_module *crt, const rc32_module *cpp, int verbose, int bench) {
  int fails = 0;
  uint32_t base = rc32_image_base("msvcr100.dll");
  errno_va = rc32_image_export(base, "_errno");
  edges();
  rc_fn tr[NFNS], nv[NFNS];
  // binding: the registry hands out the override for each export's address
  int bound = 0;
  for (int f = 0; f < NFNS; f++) {
    uint32_t a = rc32_image_export(base, fns[f].name);
    tr[f] = a ? table_fn(crt, a) : 0;
    nv[f] = w32_crtmath_fn(fns[f].name);
    rc_fn want = !strcmp(fns[f].name, "log") ? counting_log : nv[f];
    if (!a || !tr[f] || !nv[f]) {
      fprintf(stderr, "FAIL math: %s: export 0x%x, translated %p, native %p\n", fns[f].name, a, (void *)tr[f], (void *)nv[f]);
      fails++;
      continue;
    }
    if (rc32_lookup(a) == want) bound++;
    else { fprintf(stderr, "FAIL math: rc32_lookup(%s) is not the override\n", fns[f].name); fails++; }
  }
  fprintf(stderr, "math: %d/%d overrides bound at their exports\n", bound, NFNS);
  // an import of one in another image runs it through the import thunk
  if (cpp) {
    int k = 0;
    while (cpp->imports[k].name && strcmp(cpp->imports[k].name, "msvcr100.dll!log")) k++;
    if (!cpp->imports[k].name) {
      fprintf(stderr, "FAIL math: msvcp100 does not import msvcr100.dll!log\n");
      fails++;
    } else {
      uint64_t esp0 = c->r[4];
      uint32_t sp = (uint32_t)esp0 - 64;
      double x = 10.0;
      memcpy(RC32_H(sp), &x, 8);  // a thunk runs with the arguments at esp, no return address
      c->r[4] = sp;
      c->fpu.top = 0;
      c->fpu.tag = 0xff;
      int before = log_calls;
      cpp->imports[k].fn(c);
      int ok = log_calls == before + 1 && ST(0) == log(10.0) && (uint32_t)c->r[4] == sp;
      fprintf(stderr, "math: msvcp100's msvcr100.dll!log import -> %s\n", ok ? "the override" : "NOT the override");
      fails += !ok;
      c->r[4] = esp0;
    }
  }
  // the CRT's choice of SSE2 or x87 code: `cmp dword [__use_sse2_mathfcns], 0` opens _CIpow
  uint32_t pw = rc32_image_export(base, "_CIpow");
  uint8_t *ins = RC32_H(pw);
  uint32_t flag = 0;
  if (ins[0] == 0x83 && ins[1] == 0x3d) memcpy(&flag, ins + 2, 4);
  uint32_t saved = flag ? w32_rd32(flag) : 0;
  for (int pass = 0; pass < (flag ? 2 : 1); pass++) {
    if (flag) w32_wr32(flag, pass == 0 ? 1 : 0);
    fprintf(stderr, "math: native vs translated, the CRT on its %s paths\n",
            !flag ? "default" : pass == 0 ? "SSE2 (the game's)" : "x87");
    const char *only = getenv("MATH_ONLY");  // one function (debugging)
    for (int f = 0; f < NFNS; f++)
      if (tr[f] && nv[f] && (!only || !strcmp(only, fns[f].name))) {
        // the x87 paths (not the game's) are shown, not held to: the runtime's x87 runs them at
        // double precision, which they were not written for (_CIpow(3, 10) is 6 ulp off there)
        int bad = check_fn(c, f, tr[f], nv[f], verbose);
        if (pass == 0) fails += bad;
      }
  }
  if (flag) w32_wr32(flag, saved);
  if (bench) {
    static const char *const bn[] = {"_CIpow", "_CIexp", "_CIlog", "_CIsin", "_CIsqrt", "_CIatan2", "ceil", "floor"};
    fprintf(stderr, "math: ns per call (CRT on its %s paths)\n", flag && saved ? "SSE2" : "x87");
    for (size_t b = 0; b < sizeof bn / sizeof bn[0]; b++)
      for (int f = 0; f < NFNS; f++)
        if (!strcmp(fns[f].name, bn[b]) && tr[f] && nv[f]) {
          double tt = bench_fn(c, tr[f], fns[f].kind, 300000), tn = bench_fn(c, nv[f], fns[f].kind, 3000000);
          fprintf(stderr, "  %-9s translated %7.1f ns, native %6.1f ns: %5.1fx\n", bn[b], tt, tn, tt / tn);
        }
    if (flag) {
      w32_wr32(flag, 0);
      for (int f = 0; f < NFNS; f++)
        if (!strcmp(fns[f].name, "_CIpow")) {
          double tt = bench_fn(c, tr[f], K_CI2, 300000);
          fprintf(stderr, "  _CIpow    translated %7.1f ns on the x87 path\n", tt);
        }
      w32_wr32(flag, saved);
    }
  }
  return fails;
}
