// Override module `dsp`: native versions of hot guest DSP functions, bit-identical to the
// translation of the original (x86 SSE float semantics, same operation order, no FMA).
//
// 0x10010da10, the note detector's analysis window: (mode edi, float *buf rsi, int n edx) multiplies
// buf[0..n) in place by a window, four samples per step: mode 0 a ramp (k * 2/(n/4), then
// 1 - k * 2/(n/4) from the middle on), mode 1 Hamming (0.54 - 0.46 cos(k * 2pi/(n-1))), mode 2 Hann
// (0.5 - 0.5 cos(...)), anything else nothing. The original evaluates the cosine (the image's vcosf
// import) for every sample on every call, the same n each time; this computes each window once per
// thread and FPCR value with the same operations and keeps it, so a call is one multiply per sample.
// The constants are read from the image's __TEXT,__const (where the original reads them).
#include <arm_neon.h>
#include <stdlib.h>

#include "rc_ov.h"

#define WIN_COS_SLOT RC_ADDR(la_vcosf)  // __la_symbol_ptr slot of _vcosf
#define WIN_TWO_PI RC_ADDR(dsp_two_pi)  // float 2pi (mode 1, 2: step = 2pi / (n - 1))
#define WIN_TWO RC_ADDR(dsp_two)        // float 2 (mode 0: step = 2 / (n / 4))
#define WIN_RAMP0 RC_ADDR(dsp_ramp0)    // {0, 1, 2, 3}
#define WIN_HALF RC_ADDR(dsp_half4)     // {0.5 x4} (mode 2)
#define WIN_FOUR RC_ADDR(dsp_four4)     // {4 x4}, the ramp step
#define WIN_K046 RC_ADDR(dsp_k046)      // {0.46 x4} (mode 1)
#define WIN_K054 RC_ADDR(dsp_k054)      // {0.54 x4} (mode 1)
#define WIN_ONE RC_ADDR(dsp_one4)       // {1 x4} (mode 0, second half)

static inline float32x4_t win_c4(uint64_t vmaddr) {
  float32x4_t v;
  memcpy(&v, (const void *)(uintptr_t)RC_A(vmaddr), 16);
  return v;
}
static inline float win_c1(uint64_t vmaddr) {
  float v;
  memcpy(&v, (const void *)(uintptr_t)RC_A(vmaddr), 4);
  return v;
}

// x86 SSE NaN result for r = a OP b (rc_sse.h, rc_vxnanf): a NaN operand propagates quieted, the
// first one winning; an invalid operation gives the negative default NaN.
static inline float32x4_t win_xnan(float32x4_t r, float32x4_t a, float32x4_t b) {
  if (__builtin_expect(vmaxvq_f32(r) == vmaxvq_f32(r), 1)) return r;
  uint32x4_t q = vdupq_n_u32(0x00400000u);
  uint32x4_t n = vbslq_u32(vceqq_f32(b, b), vdupq_n_u32(0xffc00000u), vorrq_u32(vreinterpretq_u32_f32(b), q));
  n = vbslq_u32(vceqq_f32(a, a), n, vorrq_u32(vreinterpretq_u32_f32(a), q));
  return vbslq_f32(vceqq_f32(r, r), r, vreinterpretq_f32_u32(n));
}
static inline float32x4_t win_mul(float32x4_t a, float32x4_t b) { return win_xnan(vmulq_f32(a, b), a, b); }
static inline float32x4_t win_sub(float32x4_t a, float32x4_t b) { return win_xnan(vsubq_f32(a, b), a, b); }
static inline float32x4_t win_add(float32x4_t a, float32x4_t b) { return win_xnan(vaddq_f32(a, b), a, b); }
static inline float win_div1(float a, float b) {
  float r = a / b;
  if (__builtin_expect(r == r, 1)) return r;
  uint32_t u;
  if (a != a) { memcpy(&u, &a, 4); u |= 0x00400000u; }
  else if (b != b) { memcpy(&u, &b, 4); u |= 0x00400000u; }
  else u = 0xffc00000u;
  memcpy(&r, &u, 4);
  return r;
}

typedef float32x4_t (*win_cos_fn)(float32x4_t);

// The per-step state of the original's loops: the step (broadcast), the ramp, and for modes 1 and 2
// the cosine function.
typedef struct {
  int mode;
  uint32_t n4, half;
  float32x4_t step, ramp, four;
  win_cos_fn cosf4;
} win_gen;

static void win_gen_init(win_gen *g, int mode, int32_t n) {
  // n / 4 rounded toward zero (sar/shr/add/sar), as an unsigned loop bound
  g->mode = mode;
  g->n4 = (uint32_t)(n / 4);
  g->half = g->n4 >> 1;
  g->ramp = win_c4(WIN_RAMP0);
  g->four = win_c4(WIN_FOUR);
  if (mode == 0) {
    g->step = vdupq_n_f32(win_div1(win_c1(WIN_TWO), (float)(int64_t)(uint64_t)g->n4));
  } else {
    g->step = vdupq_n_f32(win_div1(win_c1(WIN_TWO_PI), (float)(int32_t)(uint32_t)(n - 1)));
    g->cosf4 = (win_cos_fn)(uintptr_t)rc_imp(WIN_COS_SLOT, "vcosf");
  }
}

// Window factor of step i (i counts up from 0, one call per step).
static inline float32x4_t win_next(win_gen *g, uint32_t i) {
  float32x4_t w;
  switch (g->mode) {
    case 0:
      w = win_mul(g->step, g->ramp);
      if (i >= g->half) w = win_sub(win_c4(WIN_ONE), w);
      break;
    case 1:
      w = win_sub(win_c4(WIN_K054), win_mul(g->cosf4(win_mul(g->step, g->ramp)), win_c4(WIN_K046)));
      break;
    default: {
      float32x4_t h = win_c4(WIN_HALF);
      w = win_sub(h, win_mul(g->cosf4(win_mul(g->step, g->ramp)), h));
      break;
    }
  }
  g->ramp = win_add(g->ramp, g->four);
  return w;
}

// Steps the original runs: modes 1 and 2 loop do { } while (++i < n4) unless n + 3 <= 6 (unsigned);
// mode 0 runs n4 steps (none for n4 = 0).
static uint32_t win_steps(int mode, int32_t n) {
  uint32_t n4 = (uint32_t)(n / 4);
  if (mode == 0) return n4;
  return (uint32_t)n + 3 <= 6 ? 0 : (n4 ? n4 : 0xffffffffu);
}

// Per-thread cache of computed windows, keyed by mode, n and FPCR (rounding mode, flush-to-zero).
#define WIN_CACHE 4
#define WIN_MAX_STEPS (1u << 20)
typedef struct {
  int mode;
  int32_t n;
  uint64_t fpcr;
  float32x4_t *w;
} win_entry;
static _Thread_local win_entry win_cache[WIN_CACHE];
static _Thread_local unsigned win_next_slot;

static inline uint64_t win_fpcr(void) {
  uint64_t v;
  __asm__ volatile("mrs %0, fpcr" : "=r"(v));
  return v;
}

static const float32x4_t *win_get(int mode, int32_t n, uint32_t steps) {
  uint64_t fpcr = win_fpcr();
  for (unsigned k = 0; k < WIN_CACHE; k++)
    if (win_cache[k].w && win_cache[k].mode == mode && win_cache[k].n == n && win_cache[k].fpcr == fpcr)
      return win_cache[k].w;
  float32x4_t *w = malloc((size_t)steps * sizeof *w);
  if (!w) return 0;
  win_gen g;
  win_gen_init(&g, mode, n);
  for (uint32_t i = 0; i < steps; i++) w[i] = win_next(&g, i);
  win_entry *e = &win_cache[win_next_slot++ % WIN_CACHE];
  free(e->w);
  *e = (win_entry){mode, n, fpcr, w};
  return w;
}

static int win_nocache = -1;

/* analysis window (mode, buf, n) */
void RC_FN(dsp_analysis_window)(rc_ctx *c) {
  OV_ARGS_OF(dsp_analysis_window, a, c);
  int mode = (int)OV_U32(a);
  float *buf = OV_PTR(a, float);
  int32_t n = OV_I32(a);
  if ((unsigned)mode <= 2) {
    uint32_t steps = win_steps(mode, n);
    if (win_nocache < 0) win_nocache = getenv("RC_OV_WIN_NOCACHE") != 0;
    const float32x4_t *w = steps && steps <= WIN_MAX_STEPS && !win_nocache ? win_get(mode, n, steps) : 0;
    if (w) {
      for (uint32_t i = 0; i < steps; i++) {
        float32x4_t x;
        memcpy(&x, buf + 4 * (size_t)i, 16);
        x = win_mul(w[i], x);
        memcpy(buf + 4 * (size_t)i, &x, 16);
      }
    } else if (steps) {
      win_gen g;
      win_gen_init(&g, mode, n);
      for (uint32_t i = 0; i < steps; i++) {
        float32x4_t x;
        memcpy(&x, buf + 4 * (size_t)i, 16);
        x = win_mul(win_next(&g, i), x);
        memcpy(buf + 4 * (size_t)i, &x, 16);
      }
    }
  }
  ov_ret_void(c);
}
