// Recompiler runtime: per-thread guest contexts and native -> guest calls.
//
// Every thread that enters guest code gets an rc_ctx and a guest stack (mmap,
// guard page below), created on first use. rc_call_guest is re-entrant: a
// native function called from guest code may call back into guest code on the
// same thread; the whole ctx is saved and restored around the call, and the
// callback's frame is placed below the guest rsp at the time of the native call.
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "rc_rt.h"

static _Thread_local rc_thread *tls_thread;
static pthread_key_t thread_key;
static pthread_once_t key_once = PTHREAD_ONCE_INIT;
static size_t stack_bytes;

extern void rc_jmp_table_free(rc_jmp_table *);

static void thread_free(void *p) {
  rc_thread *t = p;
  if (!t) return;
  tls_thread = 0;
  rc_jmp_table_free(t->jmps);
  free(t->cg);
  munmap(t->stack_lo, t->stack_size);
  free(t);
}

static void key_init(void) {
  pthread_key_create(&thread_key, thread_free);
  const char *e = getenv("RC_GUEST_STACK_MB");
  long mb = e ? atol(e) : 8;
  if (mb < 1) mb = 8;
  stack_bytes = (size_t)mb << 20;
}

rc_thread *rc_thread_self(void) {
  rc_thread *t = tls_thread;
  if (t) return t;
  pthread_once(&key_once, key_init);
  t = calloc(1, sizeof *t);
  size_t pg = (size_t)getpagesize();
  size_t sz = (stack_bytes + pg - 1) & ~(pg - 1);
  uint8_t *m = mmap(0, sz + pg, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
  if (!t || m == MAP_FAILED) rc_fail("cannot allocate guest stack", 0);
  mprotect(m, pg, PROT_NONE);  // guard page
  t->stack_lo = m;
  t->stack_size = sz + pg;
  t->ctx.r[4] = ((uint64_t)m + sz + pg) & ~15ull;
  t->ctx.mxcsr = 0x1f80;
  t->ctx.cg_fp = ~0ull;
  uint64_t id;
  pthread_threadid_np(0, &id);
  t->tid = id;
  pthread_t self = pthread_self();
  t->nstack_hi = (uint64_t)pthread_get_stackaddr_np(self);
  t->nstack_lo = t->nstack_hi - pthread_get_stacksize_np(self);
  tls_thread = t;
  pthread_setspecific(thread_key, t);
  return t;
}

rc_ctx *rc_thread_ctx(void) { return &rc_thread_self()->ctx; }

#define SENTINEL_RET 0x5e47e1ull  // return address of an outermost guest frame

static const int argreg[6] = {7, 6, 2, 1, 8, 9};  // rdi rsi rdx rcx r8 r9

// --- live native->guest calls ----------------------------------------------------------------
// One record per active rc_call_guest_regs of the thread: the address of its native frame (the
// frame-pointer value, so every descendant frame is below it) and the guest context it must restore.
typedef struct rc_cgrec {
  uint64_t fp;
  uint64_t *canary;  // guest stack word, written at entry: intact => no guest code ran there since
  uint64_t canary_val;
  rc_ctx saved;
} rc_cgrec;
#define CANARY 0xc4a7a21e00000000ull
static uint64_t cg_seq;

static int on_native_stack(const rc_thread *t, uint64_t a) { return a >= t->nstack_lo && a < t->nstack_hi; }

// After every change of t->ncg: the newest record's frame, for rc_unwind_guard (rc.h: cg_fp).
void rc_cg_changed(rc_thread *t) { t->ctx.cg_fp = t->ncg ? t->cg[t->ncg - 1].fp : ~0ull; }

// Number of records that are still ancestors of the frame at `fp`. Walks the arm64 frame-pointer chain
// (mandatory on Darwin) up from `fp`; a record whose frame the walk passes over is dead (its frames
// were discarded by a native longjmp). If the chain cannot be followed, the remaining records are
// assumed alive: that only keeps a dead record around, it never discards a live one.
static uint32_t live_records(const rc_thread *t, uint64_t fp) {
  uint32_t live = t->ncg;
  uint64_t cur = fp;
  while (live > 0 && t->cg[live - 1].fp <= fp) live--;  // at or below this very frame: a dead call's
  while (live > 0) {
    uint64_t a = t->cg[live - 1].fp;
    while (cur < a) {
      if ((cur & 15) || !on_native_stack(t, cur)) return live;
      uint64_t next = *(const uint64_t *)cur;
      if (next <= cur || !on_native_stack(t, next)) return live;
      cur = next;
    }
    if (cur == a) break;
    live--;
  }
  return live;
}

static void restore_regs(rc_ctx *c, const rc_ctx *s) {
  c->r[3] = s->r[3]; c->r[4] = s->r[4]; c->r[5] = s->r[5];
  c->r[12] = s->r[12]; c->r[13] = s->r[13]; c->r[14] = s->r[14]; c->r[15] = s->r[15];
  c->mxcsr = s->mxcsr;
}

void rc_unwind_sync(rc_ctx *c, const void *anchor) {
  rc_thread *t = tls_thread;
  if (!t || !t->ncg) return;
  uint64_t a = (uint64_t)anchor;
  if (t->cg[t->ncg - 1].fp >= a || !on_native_stack(t, a)) return;  // fast path: the newest record is an ancestor
  uint32_t n = t->ncg;
  while (n && t->cg[n - 1].fp < a) n--;
  restore_regs(c, &t->cg[n].saved);  // the oldest skipped call: the state at the native function's call
  t->ncg = n;
  rc_cg_changed(t);
}

void rc_unwind_end(rc_ctx **pc) { rc_unwind_sync(*pc, pc); }

__attribute__((noinline)) void rc_call_guest_regs(uint64_t fn, const uint64_t gi[6], int ng, const uint64_t dv[8],
                                                  int nd, const uint64_t *stk, int ns, uint64_t out[4]) {
  rc_fn f = rc_lookup(fn);
  if (!f) rc_fail("native call into untranslated guest code", fn - rc_slide);
  rc_thread *t = rc_thread_self();
  rc_ctx *c = &t->ctx;
  uint64_t fp = (uint64_t)__builtin_frame_address(0);
  if (t->ncg && on_native_stack(t, fp)) {
    uint32_t live = live_records(t, fp);
    if (live < t->ncg) {  // a native longjmp skipped calls that were never unwound
      // Native code has run since, and (every return into guest code reconciles, see rc_unwind_sync)
      // the guest has not: the context is what the oldest skipped call found. With no live call left
      // that is everything; otherwise the callee-saved registers and rsp, provided the guest stack
      // above the skipped call's frame is untouched (guest code that did run there would have
      // overwritten the canary, and its rsp is the one to keep).
      const rc_cgrec *old = &t->cg[live];
      if (live == 0) *c = old->saved;
      else if (*old->canary == old->canary_val) restore_regs(c, &old->saved);
      t->ncg = live;
      rc_cg_changed(t);
    }
  }
  if (t->ncg == t->capcg) {
    t->capcg = t->capcg ? t->capcg * 2 : 16;
    t->cg = realloc(t->cg, t->capcg * sizeof *t->cg);
    if (!t->cg) rc_fail("out of memory", 0);
  }
  uint32_t idx = t->ncg++;
  t->cg[idx].fp = fp;
  t->cg[idx].saved = *c;
  // below the red zone, so a frame that is live (a native call from guest code) is untouched; 16 bytes
  // above the stack arguments hold the canary
  uint64_t sp = (c->r[4] - 128 - 16 - 8ull * (uint64_t)ns) & ~15ull;
  for (int i = 0; i < ns; i++) *(uint64_t *)(sp + 8ull * i) = stk[i];
  t->cg[idx].canary = (uint64_t *)(sp + 8ull * (uint64_t)ns);
  t->cg[idx].canary_val = CANARY ^ __atomic_add_fetch(&cg_seq, 1, __ATOMIC_RELAXED);
  *t->cg[idx].canary = t->cg[idx].canary_val;
  rc_cg_changed(t);
  sp -= 8;  // x86 function entry: rsp == 8 (mod 16), [rsp] = return address
  *(uint64_t *)sp = SENTINEL_RET;
  c->r[4] = sp;
  for (int i = 0; i < 6 && i < ng; i++) c->r[argreg[i]] = gi[i];
  c->r[0] = (uint64_t)nd;  // al = number of vector registers used (variadic callees)
  for (int i = 0; i < 8; i++) c->x[i] = (rc_v128){i < nd ? dv[i] : 0, 0};
  f(c);
  if (c->r[4] != sp + 8) rc_fail("guest stack imbalance after native call", fn - rc_slide);
  out[0] = c->r[0];
  out[1] = c->r[2];
  out[2] = c->x[0][0];
  out[3] = c->x[1][0];
  *c = t->cg[idx].saved;
  t->ncg = idx;  // also drops the records of calls a native longjmp skipped inside this one
  rc_cg_changed(t);
}

void rc_call_guest_ex(uint64_t fn, const uint64_t *ints, int nint, const uint64_t *dbls, int ndbl,
                      uint64_t out[4]) {
  rc_call_guest_regs(fn, ints, nint < 6 ? nint : 6, dbls, ndbl, ints ? ints + 6 : 0, nint > 6 ? nint - 6 : 0, out);
}

uint64_t rc_call_guest(uint64_t fn, const uint64_t *ints, int nint, const uint64_t *dbls, int ndbl,
                       uint64_t *xmm0_out) {
  uint64_t o[4];
  rc_call_guest_ex(fn, ints, nint, dbls, ndbl, o);
  if (xmm0_out) *xmm0_out = o[2];
  return o[0];
}

// Native stack size for threads the game creates (pthread_create thunk):
// RC_THREAD_STACK_MB, default 64 (lazily committed).
size_t rc_thread_min_stack(void) {
  static size_t sz;
  if (!sz) {
    const char *e = getenv("RC_THREAD_STACK_MB");
    size_t mb = e && atoi(e) > 0 ? (size_t)atoi(e) : 64;
    sz = mb << 20;
  }
  return sz;
}
