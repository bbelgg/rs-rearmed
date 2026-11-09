// Hook points and the mods folder, on the two runtimes (Makefile): the same hooks_mod.dylib reads every call
// as the Mac's SysV call. The 32-bit build makes each call as the game would (the arguments where its
// convention puts them, the function removing what its `ret N` removes), and checks what the original and
// the hooks get. The tables below are what targets.py generates and table.c holds in a real build.
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifdef RC_GUEST32
#include "rc32.h"
#include "rc32_hook.h"
#include "rc32_x87.h"
#include "rc_ov.h"
#endif
#include "rc_call.h"
#include "rc_hook.h"
#include "rc_mid.h"

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)
#define CHECK_EQ(a, b) do { uint64_t a_ = (a), b_ = (b); if (a_ != b_) { \
    fprintf(stderr, "FAIL %s:%d: %s = 0x%llx, not 0x%llx\n", __FILE__, __LINE__, #a, (unsigned long long)a_, \
            (unsigned long long)b_); exit(1); } } while (0)

enum { MIX, TWO, OBJ, FAST, REG, FLT, MANY, FOLD, HIDE, RATIO, NPOINTS, UNSAID = NPOINTS, WRONG, OTHER };
#define ADDR(k) (0x401000u + 0x100u * (k))
const char rc_target_id[] = "test";

// --- the target's tables
const rc_target_value rc_target_offsets[] = {{"fld", 0x48}, {"fn_slot", 3}, {0, 0}};
#ifdef RC_GUEST32
const rc_target_value rc_target_symbols[] = {{"glob", 0x500000, -1}, {"obj_thiscall", ADDR(OBJ), RC_CC_THISCALL}, {0, 0}};
const rc_hook_name rc_target_hooks[] = {
    {"mix_cdecl", ADDR(MIX), 1, 0, RC_CC_CDECL, 'i', 0, 7,
     {{'p', 0}, {'b', 1}, {'h', 2}, {'i', 3}, {'q', 4}, {'f', 6}, {'d', 7}}},
    {"two_stdcall", ADDR(TWO), 2, 0, RC_CC_STDCALL, 'q', 12, 2, {{'p', 0}, {'q', 1}}},
    {"obj_thiscall", ADDR(OBJ), 3, 0, RC_CC_THISCALL, 'b', 8, 3, {{'p', ~1}, {'i', 0}, {'i', 1}}},
    {"fast_fastcall", ADDR(FAST), 4, 0, RC_CC_FASTCALL, 'p', 4, 3, {{'p', ~1}, {'i', ~2}, {'i', 0}}},
    {"reg_custom", ADDR(REG), 5, 0, RC_CC_CUSTOM, 'f', -1, 3, {{'p', 0}, {'p', ~1}, {'i', ~7}}},
    {"float_cdecl", ADDR(FLT), 6, 0, RC_CC_CDECL, 'd', 0, 1, {{'f', 0}}},
    {"many_cdecl", ADDR(MANY), 7, 0, RC_CC_CDECL, 'i', 0, 8,
     {{'i', 0}, {'i', 1}, {'i', 2}, {'i', 3}, {'i', 4}, {'i', 5}, {'i', 6}, {'i', 7}}},
    {"folded_stdcall", ADDR(FOLD), 8, 0, RC_CC_STDCALL, 'v', 4, 3, {{'p', 0}, {'c', 0, 0x98}, {'c', 0, 0}}},
    {"hidden_thiscall", ADDR(HIDE), 9, 0, RC_CC_THISCALL, 'e', 0, 1, {{'x', ~1}}, 0x20, 7},
    {"ratio_cdecl", ADDR(RATIO), 10, 0, RC_CC_CDECL, 'r', 0, 1, {{'f', 0}}},
    {"regs_unsaid", ADDR(UNSAID), 8, "its prototype doesn't say which registers it takes", RC_CC_CUSTOM, 'v', -1, -1},
    {"wrong_size", ADDR(WRONG), 9, 0, RC_CC_STDCALL, 'v', 8, 2, {{'i', 0}, {'i', 1}}},
    {"not_the_macs", ADDR(OTHER), 11, "here it is int(void *a, ...), on the Mac bool(void *a, int b)", RC_CC_CUSTOM, 'i', -1, -1},
    {"not_on_this_game", 0, 10, "no such function in this build"},
    {0, 0, 0},
};
int rc_hook_code_pops(int index) {
  static const short pops[] = {0, 12, 8, 4, 4, 0, 0, 4, 0, 0, -1, 4, -1};
  return pops[index];
}
#else
const rc_target_value rc_target_symbols[] = {{"glob", 0x500000, -1}, {"obj_thiscall", ADDR(OBJ), RC_CC_SYSV}, {0, 0}};
const rc_hook_name rc_target_hooks[] = {
    {"mix_cdecl", ADDR(MIX), ADDR(MIX)}, {"two_stdcall", ADDR(TWO), ADDR(TWO)}, {"obj_thiscall", ADDR(OBJ), ADDR(OBJ)},
    {"fast_fastcall", ADDR(FAST), ADDR(FAST)}, {"reg_custom", ADDR(REG), ADDR(REG)}, {"float_cdecl", ADDR(FLT), ADDR(FLT)},
    {"many_cdecl", ADDR(MANY), ADDR(MANY)}, {"folded_stdcall", ADDR(FOLD), ADDR(FOLD)},
    {"hidden_thiscall", ADDR(HIDE), ADDR(HIDE)}, {"ratio_cdecl", ADDR(RATIO), ADDR(RATIO)},
    {"not_on_this_game", 0, 10, "no such function in this build"},
    {0, 0, 0},
};
#endif
// edit groups and mid points (rc_mid.h): one of each here, one of each not on this game; the same values on both,
// in each one's own places
const rc_edit_name rc_target_edits[] = {{"grp_here", 3, 0}, {"grp_gone", 0, "no edits on this target"},
                                        {"grp_late", 1, 0}, {0, 0, 0}};
#ifdef RC_GUEST32
const rc_mid_name rc_target_mids[] = {
    {"mid_here", 0x401800, 0, 3, {{"count", 'i', 6, 0}, {"obj", 'p', RC_MID_MEM | 4, 8}, {"scale", 'd', 32, 0}}, 2,
     {"again", "done"}},
    {"mid_gone", 0, "not in this build"},
    {0}};
#else
const rc_mid_name rc_target_mids[] = {
    {"mid_here", 0x401800, 0, 3, {{"count", 'i', 6, 0}, {"obj", 'p', 7, 0}, {"scale", 'f', 16, 0}}, 2, {"again", "done"}},
    {"mid_gone", 0, "not in this build"},
    {0}};
#endif

const uint64_t rc_hook_points[] = {ADDR(MIX), ADDR(TWO), ADDR(OBJ), ADDR(FAST), ADDR(REG), ADDR(FLT), ADDR(MANY), ADDR(FOLD),
                                   ADDR(HIDE), ADDR(RATIO), ADDR(UNSAID), ADDR(WRONG), ADDR(OTHER), 0};
const int rc_hook_npoints = 13;

// The mod's side (hooks_mod.c)
typedef struct { const char *name, *sig; char ret; } hm_point;
static const hm_point *points;
static int *action, *pre_n, *post_n, *refused, *ptr_size, *order;
static uint64_t *result, (*seen)[8], *seen_result, *ret_addr, *addr;
static int64_t *off, *no_off, *slot;
static const char **target;
static void *(*ptr_at)(const void *), *(*host_of)(uint64_t);
static uint64_t (*guest_of)(const void *);
static void (*missing)(int *, int64_t *, uint64_t *);

// What the runtime logs for a while: log_start, then log_stop gives the lines that have the text
static FILE *log_file;
static int log_stderr;
static void log_start(void) {
  fflush(stderr);
  log_file = tmpfile();
  CHECK(log_file);
  log_stderr = dup(2);
  dup2(fileno(log_file), 2);
}
static int log_stop(const char *text) {
  char line[512];
  int n = 0;
  fflush(stderr);
  dup2(log_stderr, 2);
  close(log_stderr);
  rewind(log_file);
  while (fgets(line, sizeof line, log_file)) n += strstr(line, text) != 0;
  fclose(log_file);
  return n;
}

// --- the original functions: each takes its arguments the way the convention puts them
static const uint32_t RET = 0x12345678;
static uint64_t got[8];  // what the original was given
static int orig_ran;
static uint64_t orig_result = 0xabcd;

#ifdef RC_GUEST32
static uint32_t rd(uint32_t g) { uint32_t v; memcpy(&v, RC32_H(g), 4); return v; }
static void wr(uint32_t g, uint32_t v) { memcpy(RC32_H(g), &v, 4); }
static uint64_t rd64(uint32_t g) { return rd(g) | (uint64_t)rd(g + 4) << 32; }
static void ret_n(rc_ctx *c, uint32_t n) {
  uint32_t sp = (uint32_t)c->r[4];
  c->ret_to = rd(sp);
  c->r[4] = sp + 4 + n;
}
#define ARG(k) (sp + 4 + 4 * (k))
#define BEGIN() uint32_t sp = (uint32_t)c->r[4]; orig_ran++
static void o_mix(rc_ctx *c) {
  BEGIN();
  got[0] = rd(ARG(0)); got[1] = rd(ARG(1)) & 0xff; got[2] = rd(ARG(2)) & 0xffff; got[3] = rd(ARG(3));
  got[4] = rd64(ARG(4)); got[5] = rd(ARG(6)); got[6] = rd64(ARG(7));
  c->r[0] = (uint32_t)orig_result;
  ret_n(c, 0);
}
static void o_two(rc_ctx *c) {
  BEGIN();
  got[0] = rd(ARG(0)); got[1] = rd64(ARG(1));
  c->r[0] = (uint32_t)orig_result; c->r[2] = orig_result >> 32;
  ret_n(c, 12);
}
static void o_obj(rc_ctx *c) {
  BEGIN();
  got[0] = (uint32_t)c->r[1]; got[1] = rd(ARG(0)); got[2] = rd(ARG(1));
  c->r[0] = 0xffffff00u | (uint8_t)orig_result;  // al only, as MSVC leaves a bool
  ret_n(c, 8);
}
static void o_fast(rc_ctx *c) {
  BEGIN();
  got[0] = (uint32_t)c->r[1]; got[1] = (uint32_t)c->r[2]; got[2] = rd(ARG(0));
  c->r[0] = (uint32_t)orig_result;
  ret_n(c, 4);
}
static void o_reg(rc_ctx *c) {
  BEGIN();
  got[0] = rd(ARG(0)); got[1] = (uint32_t)c->r[1]; got[2] = (uint32_t)c->r[7];
  rc87_push(c, 3.5);
  ret_n(c, 4);
}
static void o_flt(rc_ctx *c) {
  BEGIN();
  got[0] = rd(ARG(0));
  rc87_push(c, 6.5);
  ret_n(c, 0);
}
static void o_many(rc_ctx *c) {
  BEGIN();
  for (int k = 0; k < 8; k++) got[k] = rd(ARG(k));
  c->r[0] = (uint32_t)orig_result;
  ret_n(c, 0);
}
// Only the pointer is there: the compiler folded the other two away
static void o_fold(rc_ctx *c) {
  BEGIN();
  got[0] = rd(ARG(0)); got[1] = 0x98; got[2] = 0;
  ret_n(c, 4);
}
// The Mac's has no argument and answers a bool; here `this`, and the answer is a field of it having a value
static void o_hide(rc_ctx *c) {
  BEGIN();
  wr((uint32_t)c->r[1] + 0x20, (uint32_t)orig_result);
  c->r[0] = 0x5555;  // nothing: this one returns void
  ret_n(c, 0);
}
// The Mac's answers 100 / x where this one answers x
static void o_ratio(rc_ctx *c) {
  BEGIN();
  got[0] = rd(ARG(0));
  rc87_push(c, 200.0);
  ret_n(c, 0);
}
#else
#define BEGIN() orig_ran++
static void ret_0(rc_ctx *c) { c->r[4] += 8; }
static void o_mix(rc_ctx *c) {
  BEGIN();
  got[0] = c->r[7]; got[1] = c->r[6] & 0xff; got[2] = c->r[2] & 0xffff; got[3] = (uint32_t)c->r[1]; got[4] = c->r[8];
  got[5] = (uint32_t)c->x[0][0]; got[6] = c->x[1][0];
  c->r[0] = (uint32_t)orig_result;
  ret_0(c);
}
static void o_two(rc_ctx *c) { BEGIN(); got[0] = c->r[7]; got[1] = c->r[6]; c->r[0] = orig_result; ret_0(c); }
static void o_obj(rc_ctx *c) { BEGIN(); got[0] = c->r[7]; got[1] = (uint32_t)c->r[6]; got[2] = (uint32_t)c->r[2]; c->r[0] = (uint8_t)orig_result; ret_0(c); }
static void o_fast(rc_ctx *c) { BEGIN(); got[0] = c->r[7]; got[1] = (uint32_t)c->r[6]; got[2] = (uint32_t)c->r[2]; c->r[0] = orig_result; ret_0(c); }
static void o_reg(rc_ctx *c) { BEGIN(); got[0] = c->r[7]; got[1] = c->r[6]; got[2] = (uint32_t)c->r[2]; rc_ret_f32(c, 3.5f); ret_0(c); }
static void o_flt(rc_ctx *c) { BEGIN(); got[0] = (uint32_t)c->x[0][0]; rc_ret_f64(c, 6.5); ret_0(c); }
static void o_many(rc_ctx *c) {
  BEGIN();
  static const int reg[6] = {7, 6, 2, 1, 8, 9};
  for (int k = 0; k < 6; k++) got[k] = (uint32_t)c->r[reg[k]];
  got[6] = (uint32_t)*(uint64_t *)(uintptr_t)(c->r[4] + 8);
  got[7] = (uint32_t)*(uint64_t *)(uintptr_t)(c->r[4] + 16);
  c->r[0] = (uint32_t)orig_result;
  ret_0(c);
}
static void o_fold(rc_ctx *c) { BEGIN(); got[0] = c->r[7]; got[1] = (uint32_t)c->r[6]; got[2] = (uint32_t)c->r[2]; ret_0(c); }
static void o_hide(rc_ctx *c) { BEGIN(); c->r[0] = orig_result == 7; ret_0(c); }
static void o_ratio(rc_ctx *c) { BEGIN(); got[0] = (uint32_t)c->x[0][0]; rc_ret_f32(c, 0.5f); ret_0(c); }
#endif
static const rc_fn origs[NPOINTS] = {o_mix, o_two, o_obj, o_fast, o_reg, o_flt, o_many, o_fold, o_hide, o_ratio};

static uint32_t f_bits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static uint64_t d_bits(double d) { uint64_t u; memcpy(&u, &d, 8); return u; }

// A pointer as each side holds it. The block is the guest's: a host address on the Mac, a window address here
static uint64_t block;  // what the game holds
static uint64_t block_host(void) { return (uint64_t)(uintptr_t)host_of(block); }

// The arguments of each point, as the game passes them
static void vals_of(int p, uint64_t *v) {
  memset(v, 0, 8 * sizeof *v);
  switch (p) {
    case MIX: v[0] = block; v[1] = 0x7f; v[2] = 0x7fff; v[3] = 0x12345678; v[4] = 0x1122334455667788ull;
      v[5] = f_bits(1.25f); v[6] = d_bits(2.5); break;
    case TWO: v[0] = block; v[1] = 0xdeadbeefcafeull; break;
    case OBJ: case FAST: v[0] = block; v[1] = 11; v[2] = 22; break;
    case REG: v[0] = block; v[1] = block + 64; v[2] = 33; break;
    case FLT: v[0] = f_bits(0.75f); break;
    case FOLD: v[0] = block; v[1] = 0x98; v[2] = 0; break;
    case HIDE: v[0] = block; break;
    case RATIO: v[0] = f_bits(40.0f); break;
    case MANY: for (int k = 0; k < 8; k++) v[k] = 100 + k; break;
  }
}

static rc_ctx *ctx;

#ifdef RC_GUEST32
static uint32_t esp0;
static void put_args(int p, const uint64_t *v) {
  const rc_hook_name *h = &rc_target_hooks[p];
  uint32_t sp = esp0 - 256;
  ctx->r[4] = sp;
  wr(sp, RET);
  ctx->r[1] = ctx->r[2] = 0x5a5a5a5a;
  ctx->r[7] = 0x6b6b6b6b;
  for (int k = 0; k < h->nargs; k++) {
    if (h->arg[k].kind == 'c') continue;
    if (h->arg[k].at < 0) {
      ctx->r[~h->arg[k].at] = (uint32_t)v[k];
    } else {
      wr(sp + 4 + 4 * h->arg[k].at, (uint32_t)v[k]);
      if (h->arg[k].kind == 'q' || h->arg[k].kind == 'd') wr(sp + 8 + 4 * h->arg[k].at, (uint32_t)(v[k] >> 32));
    }
  }
}
static void check_popped(int p) {
  CHECK_EQ(ctx->ret_to, RET);
  CHECK_EQ((uint32_t)ctx->r[4], esp0 - 256 + 4 + rc_hook_code_pops(p));
}
// 8 arguments' worth of guest result in the C types of the point
static uint64_t result_of(int p) {
  switch (p) {
    case FOLD: case HIDE: return 0;
    case MIX: case MANY: return (uint32_t)ctx->r[0];
    case TWO: return (uint32_t)ctx->r[0] | ctx->r[2] << 32;
    case OBJ: return ctx->r[0] & 0xff;
    case FAST: return (uint32_t)ctx->r[0];
    default: {
      rc_ctx *c = ctx;
      double d = ST(0);
      rc87_pop(c);
      return p == REG || p == RATIO ? f_bits((float)d) : d_bits(d);
    }
  }
}
#else
static uint64_t stack_[64];
static void put_args(int p, const uint64_t *v) {
  static const uint8_t reg[6] = {7, 6, 2, 1, 8, 9};
  ctx->r[4] = (uint64_t)(uintptr_t)&stack_[8];
  stack_[8] = RET;
  int gi = 0, fi = 0, st = 1;
  for (const char *s = points[p].sig; *s; s++, v++) {
    if (*s == 'f' || *s == 'd') ctx->x[fi++] = (rc_v128){*v, 0};
    else if (gi < 6) ctx->r[reg[gi++]] = *v;
    else stack_[8 + st++] = *v;
  }
}
static void check_popped(int p) { CHECK_EQ(ctx->r[4], (uint64_t)(uintptr_t)&stack_[9]); }
uint64_t rc_slide;
static uint64_t cg_fn, cg_ints[6], cg_dbl;
static int cg_nint, cg_ndbl;
uint64_t rc_call_guest(uint64_t fn, const uint64_t *ints, int nint, const uint64_t *dbls, int ndbl, uint64_t *xmm0) {
  cg_fn = fn;
  cg_nint = nint;
  cg_ndbl = ndbl;
  memcpy(cg_ints, ints, sizeof *ints * (size_t)(nint < 6 ? nint : 6));
  if (ndbl) cg_dbl = dbls[0];
  return 0;
}
static uint64_t result_of(int p) {
  if (p == REG || p == FLT || p == RATIO) return ctx->x[0][0];
  if (p == FOLD || p == HIDE) return 0;
  return p == OBJ ? ctx->r[0] & 0xff : ctx->r[0];
}
#endif

static void call(int p, const uint64_t *v) {
  put_args(p, v);
  memset(got, 0, sizeof got);
  orig_ran = 0;
  rc_hook_call(p, ctx, origs[p]);
  check_popped(p);
}

static int nargs(int p) { return (int)strlen(points[p].sig); }

// A view's value of an argument the game gave as v: a pointer is the host's
static uint64_t view_of(int p, int k, uint64_t v) { return points[p].sig[k] == 'p' ? (uint64_t)(uintptr_t)host_of(v) : v; }

static void every_convention(void) {
  uint64_t v[8];
  for (int p = 0; p < HIDE; p++) {
    vals_of(p, v);
    // watching: the hooks see the arguments, the original gets them as they were, the result stands
    *action = 0;
    pre_n[p] = post_n[p] = 0;
    orig_result = p == FAST ? block + 128 : p == TWO ? 0x1111222233334444ull : 1;
    call(p, v);
    CHECK_EQ(pre_n[p], 1);
    CHECK_EQ(post_n[p], 1);
    CHECK_EQ(orig_ran, 1);
    CHECK_EQ(ret_addr[p], RET);
    for (int k = 0; k < nargs(p); k++) {
      CHECK_EQ(seen[p][k], view_of(p, k, v[k]));
      CHECK_EQ(got[k], v[k]);
    }
    uint64_t want = p == REG ? f_bits(3.5f) : p == FLT ? d_bits(6.5) : p == FOLD ? 0 : orig_result;
    CHECK_EQ(result_of(p), want);
    if (p != FOLD) CHECK_EQ(seen_result[p], p == FAST ? (uint64_t)(uintptr_t)host_of(orig_result) : want);

    // a pre hook changes them: the original gets what it set
    *action = 1;
    call(p, v);
    for (int k = 0; k < nargs(p); k++) {
      char s = points[p].sig[k];
      uint64_t now = s == 'f' ? f_bits(2.5f) : s == 'd' ? d_bits(-4.25) : k == 0 && s == 'p' ? v[k] + 4
                     : s == 'p' ? v[k] : v[k] + 1;
#ifdef RC_GUEST32
      if (p == FOLD && k) now = v[k];  // nothing to change
#endif
      CHECK_EQ(got[k], now);
    }
    result_of(p);

    // a pre hook does the call: the result it set, the arguments removed, no original, no post hook
    *action = 2;
    *result = p == FAST ? block_host() + 256 : p == TWO ? 0x5555666677778888ull : p == OBJ ? 1 : 0x4242;
    post_n[p] = 0;
    call(p, v);
    CHECK_EQ(orig_ran, 0);
    CHECK_EQ(post_n[p], 0);
    if (p != FOLD) CHECK_EQ(result_of(p), p == REG ? f_bits(1.5f) : p == FLT ? d_bits(2.25) : p == FAST ? block + 256 : *result);

    // a post hook changes the result
    *action = 3;
    *result = p == FAST ? block_host() + 512 : 0x7a;
    call(p, v);
    if (p != FOLD) CHECK_EQ(result_of(p), p == REG ? f_bits(8.5f) : p == FLT ? d_bits(16.5) : p == FAST ? block + 512 : 0x7a);

    // a hook runs game code first: the original still gets its arguments
#ifdef RC_GUEST32
    *action = 4;
    call(p, v);
    for (int k = 0; k < nargs(p); k++) CHECK_EQ(got[k], v[k]);
    result_of(p);
#endif
  }
  *action = 0;
}

// Calls that aren't the same on both games: a hook gets the Mac's all the same
static void adapted(void) {
  uint64_t v[8];
  *action = 0;
  // no argument to see; the answer the Mac gives
  vals_of(HIDE, v);
  for (int quit = 0; quit < 2; quit++) {
    orig_result = quit ? 7 : 3;
    post_n[HIDE] = 0;
    call(HIDE, v);
    CHECK_EQ(orig_ran, 1);
    CHECK_EQ(post_n[HIDE], 1);
    CHECK_EQ(seen_result[HIDE], quit);
  }
  *action = 2;  // the frame skipped
  *result = 0;
  log_start();
  call(HIDE, v);
  CHECK_EQ(log_stop("takes no other result"), 0);
  CHECK_EQ(orig_ran, 0);
#ifdef RC_GUEST32
  *result = 1;  // the Mac's answer to quit: this game has no such result, and the log says so
  log_start();
  call(HIDE, v);
  CHECK_EQ(log_stop("[rc] hook hidden_thiscall: this game takes no other result"), 1);
  CHECK_EQ(orig_ran, 0);
#endif
  // 100 / x in both directions; this game's value stays exact unless a hook changes it
  vals_of(RATIO, v);
  *action = 0;
  call(RATIO, v);
  CHECK_EQ(seen[RATIO][0], f_bits(40.0f));
  CHECK_EQ(seen_result[RATIO], f_bits(0.5f));
#ifdef RC_GUEST32
  CHECK_EQ(result_of(RATIO), f_bits(200.0f));
  *action = 3;  // the post hook says 8.5
  call(RATIO, v);
  CHECK_EQ(result_of(RATIO), f_bits(100.0f / 8.5f));
  *action = 2;  // the pre hook says 1.5
  call(RATIO, v);
  CHECK_EQ(result_of(RATIO), f_bits(100.0f / 1.5f));
#endif
  *action = 0;
}

#ifdef RC_GUEST32
// `bool<zero>`: the Mac's bool is this function's error code being 0; what no hook changes stays the game's
static void zero_result(void) {
  static const rc_hook_name h = {.name = "zero", .ret = 'z'};
  rc32_view w;
  ctx->r[0] = 0;
  rc32_view_result_in(&w, &h, ctx);
  CHECK_EQ(w.ctx.r[0], 1);
  w.ctx.r[0] = 0;  // a hook says it failed
  rc32_view_result_out(&w, &h, ctx);
  CHECK((uint32_t)ctx->r[0] != 0);
  ctx->r[0] = 5;
  rc32_view_result_in(&w, &h, ctx);
  CHECK_EQ(w.ctx.r[0], 0);
  rc32_view_result_out(&w, &h, ctx);
  CHECK_EQ((uint32_t)ctx->r[0], 5);
  w.ctx.r[0] = 1;  // a hook says it opened
  rc32_view_result_out(&w, &h, ctx);
  CHECK_EQ((uint32_t)ctx->r[0], 0);
}
#endif

static void names(void) {
  CHECK_EQ(*off, 0x48);
  CHECK_EQ(*slot, 3);
  CHECK_EQ(*addr, 0x500000);
  CHECK(*no_off == RC_NO_OFF);
  CHECK(rc_addr("no_such_symbol") == 0);
  CHECK(!strcmp(*target, "test"));
  CHECK_EQ(refused[7], 0);
  CHECK(refused[0] == -1);  // `unsupported` on this game
  CHECK(refused[1] == -1);  // not a hook point
#ifdef RC_GUEST32
  CHECK_EQ(*ptr_size, 4);
  CHECK(refused[2] == -1);  // `custom` without its registers
  CHECK(refused[3] == -1);  // a prototype the code disagrees with
  CHECK(refused[4] == -1);  // the same name, another call
  CHECK_EQ(rc_hook_on[UNSAID], 0);
#else
  CHECK_EQ(*ptr_size, 8);
#endif
  // the same reads through RC_HOST, RC_GUEST and rc_mod_ptr
  uint8_t *b = host_of(block);
  uint64_t target_ptr = block + 32;
  memcpy(b + 16, &target_ptr, *ptr_size);
  CHECK(ptr_at(b + 16) == b + 32);
  memset(b + 16, 0, 8);
  CHECK(ptr_at(b + 16) == 0);
  CHECK(host_of(0) == 0);
  CHECK_EQ(guest_of(b), block);
  CHECK_EQ(guest_of(0), 0);
  // a name the game hasn't: a line for each place, once, however often it is asked for
  int has = 1;
  int64_t no_slot = 0;
  uint64_t no_addr = 1;
  log_start();
  for (int k = 0; k < 5; k++) missing(&has, &no_slot, &no_addr);
  CHECK_EQ(log_stop("no_such_field_once: not on this game"), 1);
  CHECK(!has);
  CHECK(no_slot == RC_NO_OFF);
  CHECK_EQ(no_addr, 0);
  log_start();
  missing(&has, &no_slot, &no_addr);
  CHECK_EQ(log_stop("not on this game"), 0);
}

static void order_of_hooks(void) {
  uint64_t v[8];
  vals_of(MANY, v);
  *order = 0;
  call(MANY, v);
  CHECK_EQ(*order, 12);
}

#ifndef RC_GUEST32
// A float among the arguments goes to xmm, the others stay in order
static void by_name(void) {
  uint64_t f[3] = {11, 0x40200000, 22};
  rc_call_name_f("obj_thiscall", f, 3, 2);
  CHECK_EQ(cg_fn, ADDR(OBJ));
  CHECK_EQ(cg_nint, 2);
  CHECK_EQ(cg_ints[1], 22);
  CHECK_EQ(cg_ndbl, 1);
  CHECK_EQ(cg_dbl, 0x40200000);
  rc_call_name("obj_thiscall", f, 3);
  CHECK_EQ(cg_nint, 3);
  CHECK_EQ(cg_ndbl, 0);
  CHECK(rc_guest_alloc(64));
}
#else
// rc32_call and rc_call_name (no image registry here: the lookup is the test's)
rc_fn rc32_lookup(uint32_t g) {
  return g == ADDR(OBJ) ? o_obj : 0;
}
int rc32_tls_images(const rc32_tls_image **out) {
  *out = 0;
  return 0;
}
_Noreturn void rc_fail(const char *what, uint64_t a) {
  fprintf(stderr, "rc_fail %s 0x%llx\n", what, (unsigned long long)a);
  exit(1);
}

static void by_name(void) {
  uint64_t a[3] = {block_host(), 5, 6};
  memset(got, 0, sizeof got);
  orig_result = 1;
  ctx->r[4] = esp0 - 1024;
  CHECK_EQ(rc_call_name("obj_thiscall", a, 3) & 0xff, 1);
  CHECK_EQ(got[0], block);  // a host pointer back as the guest's, in ecx
  CHECK_EQ(got[1], 5);
  CHECK_EQ(got[2], 6);
  CHECK_EQ(rc_call_name("no_such_function", a, 3), 0);
  // a float takes its place on the stack, a -1 stays a word, the mod's own memory reaches the game as null
  static char mine[4];
  uint64_t f[3] = {block_host(), 0x40200000, ~0ull};
  rc_call_name_f("obj_thiscall", f, 3, 2);
  CHECK_EQ(got[1], 0x40200000);
  CHECK_EQ(got[2], 0xffffffff);
  uint64_t m[3] = {block_host(), (uint64_t)(uintptr_t)mine, 1};
  rc_call_name("obj_thiscall", m, 3);
  CHECK_EQ(got[1], 0);
  char *g = rc_guest_alloc(64);
  CHECK(g);
  uint64_t in[3] = {block_host(), (uint64_t)(uintptr_t)g, 1};
  rc_call_name("obj_thiscall", in, 3);
  CHECK_EQ((uint64_t)(uintptr_t)RC32_H((uint32_t)got[1]), (uint64_t)(uintptr_t)g);
}

// rc_args on the guest's own context, as an override reads it (OV_ARGS_CC; rc_hook.h shows the hooks SysV)
static void guest_arguments(void) {
  uint64_t v[8];
  vals_of(OBJ, v);
  put_args(OBJ, v);
  OV_ARGS_OF(steam_dlc_owned, a, ctx);  // thiscall in the target's table
  CHECK_EQ((uint64_t)(uintptr_t)OV_PTR(a, void), (uint64_t)(uintptr_t)RC32_H(block));
  CHECK_EQ(OV_I32(a), 11);
  CHECK_EQ(OV_I32(a), 22);
  vals_of(FAST, v);
  put_args(FAST, v);
  OV_ARGS_CC(b, ctx, RC_CC_FASTCALL);
  CHECK_EQ(OV_U32(b), block);
  CHECK_EQ(OV_U32(b), 11);
  CHECK_EQ(OV_U32(b), 22);
  vals_of(MIX, v);
  put_args(MIX, v);
  OV_ARGS_CC(d, ctx, RC_CC_CDECL);
  OV_PTR(d, void);
  CHECK_EQ(OV_U32(d) & 0xff, 0x7f);
  CHECK_EQ(OV_U32(d) & 0xffff, 0x7fff);
  CHECK_EQ(OV_U32(d), 0x12345678);
  CHECK_EQ(OV_U64(d), 0x1122334455667788ull);
  CHECK(OV_F32(d) == 1.25f);
  CHECK(OV_F64(d) == 2.5);
  vals_of(TWO, v);
  put_args(TWO, v);
  ov32_ret(ctx, 9, 12);
  CHECK_EQ(ctx->r[0], 9);
  check_popped(TWO);
  vals_of(FLT, v);
  put_args(FLT, v);
  ov32_ret_f(ctx, 0.5, 0);
  CHECK_EQ(result_of(FLT), d_bits(0.5));
}

// What a hook point costs the game with no hooks on it: the wrapper gen_image_pe.py writes against the
// function alone.
static volatile uint32_t sink;
__attribute__((noinline)) static void w_bench(rc_ctx *c) { sink++; }
__attribute__((noinline)) static void f_bench(rc_ctx *c) {
  if (__builtin_expect(rc_hook_on[UNSAID], 0)) rc_hook_call(UNSAID, c, w_bench); else w_bench(c);
}
static double ns_of(rc_fn f, long n) {
  struct timespec a, b;
  clock_gettime(CLOCK_MONOTONIC, &a);
  for (long k = 0; k < n; k++) f(ctx);
  clock_gettime(CLOCK_MONOTONIC, &b);
  return ((b.tv_sec - a.tv_sec) * 1e9 + (b.tv_nsec - a.tv_nsec)) / n;
}
static void cost(void) {
  long n = 200000000;
  rc_fn volatile w = w_bench, f = f_bench;
  double bare = 1e9, wrapped = 1e9;
  for (int k = 0; k < 3; k++) {
    double x = ns_of(w, n), y = ns_of(f, n);
    if (x < bare) bare = x;
    if (y < wrapped) wrapped = y;
  }
  printf("a point without hooks: %.2f ns a call more (%.2f against %.2f)\n", wrapped - bare, wrapped, bare);
  // with one watching hook on it
  uint64_t v[8];
  vals_of(OBJ, v);
  struct timespec a, b;
  clock_gettime(CLOCK_MONOTONIC, &a);
  for (int k = 0; k < 1000000; k++) {
    put_args(OBJ, v);
    rc_hook_call(OBJ, ctx, o_obj);
  }
  clock_gettime(CLOCK_MONOTONIC, &b);
  printf("a point with a mod's pre and post hook: %.0f ns a call\n",
         ((b.tv_sec - a.tv_sec) * 1e9 + (b.tv_nsec - a.tv_nsec)) / 1e6);
}
#endif

// Edits and mid points: what the mod's constructor got, the switches fixed once the folder has loaded, and the mid
// hook on its values (a register, a pointer in a register or on the stack, a float in xmm0 or st0), flags and
// continuation; the registers the hook changed without setting them (as a call into the game does) put back
static void inner(void) {
  int *edit = dlsym(RTLD_DEFAULT, "hm_edit"), *point = dlsym(RTLD_DEFAULT, "hm_mid_point"),
      *gone = dlsym(RTLD_DEFAULT, "hm_mid_gone");
  CHECK(edit && point && gone);
  CHECK_EQ(edit[0], 0);
  CHECK_EQ((uint32_t)edit[1], (uint32_t)-1);
  CHECK_EQ((uint32_t)edit[2], (uint32_t)-1);
  CHECK(rc_edit_on[0] && !rc_edit_on[1] && !rc_edit_on[2] && rc_edit_is_on("grp_here") && !rc_edit_is_on("grp_late"));
  CHECK_EQ((uint32_t)rc_edit_enable("grp_late"), (uint32_t)-1);  // the folder has loaded: fixed
  CHECK(!rc_edit_on[2]);
  CHECK_EQ(*point, 0);
  CHECK_EQ((uint32_t)*gone, (uint32_t)-1);
  CHECK(rc_mid_on[0] && !rc_mid_on[1]);
  CHECK_EQ((uint32_t)rc_mid_value(0, "nothing"), (uint32_t)-1);
  CHECK_EQ(rc_mid_cont(0, "again"), 1);
  ctx->r[6] = 41;
  ctx->r[0] = 10, ctx->r[1] = 11, ctx->r[2] = 12;
  ctx->x[1][0] = 13;
#ifdef RC_GUEST32
  uint32_t *word = RC32_H(block);
  *word = 5;
  uint32_t slot = (uint32_t)ctx->r[4] - 0x40;
  *(uint32_t *)RC32_H(slot + 8) = (uint32_t)block;
  uint64_t esp = ctx->r[4];
  ctx->r[4] = slot;
  rc87_push(ctx, 1.5);
#else
  uint32_t *word = (uint32_t *)(uintptr_t)block;
  *word = 5;
  ctx->r[7] = block;
  float f = 1.5f;
  memcpy(&ctx->x[0], &f, 4);
#endif
  uint32_t flags = 0x41;
  CHECK_EQ(rc_mid_call(0, ctx, &flags), 2);
  CHECK_EQ(ctx->r[6], 42);
  CHECK_EQ(ctx->r[0], 10);  // what the callback's game call left in them is undone, what it set is kept
  CHECK_EQ(ctx->r[1], 11);
  CHECK_EQ(ctx->r[2], 12);
  CHECK_EQ(ctx->x[1][0], 13);
  CHECK_EQ(*word, 7);
  CHECK_EQ(flags, 0x1);
#ifdef RC_GUEST32
  CHECK(ctx->fpu.st[ctx->fpu.top] == 3.0);
  rc87_pop(ctx);
  ctx->r[4] = esp;
#else
  memcpy(&f, &ctx->x[0], 4);
  CHECK(f == 3.0f);
#endif
}

// The folder: a library without rc_mod.h is left out on a 32-bit guest, nothing of it run
static void folder(const char *out) {
  char p[1024];
  snprintf(p, sizeof p, "%s/plain", out);
  CHECK_EQ(rc_mods_count(p), 1);
  rc_mods_load(p);
  int *ran = dlsym(RTLD_DEFAULT, "plain_mod_ran");
#ifdef RC_GUEST32
  CHECK(!ran);
#else
  CHECK(ran && *ran == 1);
#endif
  setenv("RS_LOAD_MODS", "0", 1);
  CHECK(!rc_mods_where(p));
  unsetenv("RS_LOAD_MODS");
  CHECK(!strcmp(rc_mods_where(p), p));
  setenv("RS_MODS_PATH", "/nowhere", 1);
  CHECK(!strcmp(rc_mods_where(p), "/nowhere"));
  CHECK_EQ(rc_mods_count("/nowhere"), 0);
  unsetenv("RS_MODS_PATH");
}

#define SYM(var, name) do { *(void **)&var = dlsym(RTLD_DEFAULT, name); CHECK(var); } while (0)

int main(int argc, char **argv) {
  CHECK(argc > 1);
#ifdef RC_GUEST32
  CHECK(!rc32_mem_init());
  ctx = rc32_thread_ctx();
  esp0 = (uint32_t)ctx->r[4];
  block = rc32_alloc(4096);
#else
  static rc_ctx c64;
  ctx = &c64;
  block = (uint64_t)(uintptr_t)calloc(1, 4096);
#endif
  char p[1024];
  snprintf(p, sizeof p, "%s/mods", argv[1]);
  rc_mods_load(p);
  SYM(points, "hm_points"); SYM(action, "hm_action"); SYM(pre_n, "hm_pre"); SYM(post_n, "hm_post");
  SYM(refused, "hm_refused"); SYM(ptr_size, "hm_ptr_size"); SYM(order, "hm_order"); SYM(result, "hm_result");
  SYM(seen, "hm_seen"); SYM(seen_result, "hm_seen_result"); SYM(ret_addr, "hm_ret_addr"); SYM(addr, "hm_addr");
  SYM(off, "hm_off"); SYM(no_off, "hm_no_off"); SYM(slot, "hm_slot"); SYM(target, "hm_target");
  SYM(ptr_at, "hm_ptr_at"); SYM(host_of, "hm_host"); SYM(guest_of, "hm_guest"); SYM(missing, "hm_missing");
  names();
  inner();
#ifdef RC_GUEST32
  zero_result();
#endif
  every_convention();
  adapted();
  order_of_hooks();
  folder(argv[1]);
  by_name();
#ifdef RC_GUEST32
  guest_arguments();
  cost();
#endif
  printf("hooks: ok (%s)\n", rc_guest_ptr_size == 4 ? "32-bit guest" : "64-bit guest");
  return 0;
}
