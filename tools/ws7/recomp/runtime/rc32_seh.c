// 32-bit guests: frame-based structured exception handling (rc32.h).
//
// x86 SEH is a chain of EXCEPTION_REGISTRATION_RECORDs {next, handler} on the guest stack,
// headed by FS:[0]. RaiseException builds an EXCEPTION_RECORD and a CONTEXT and calls each
// handler in turn (cdecl: record, frame, context, dispatcher context) until one returns
// ExceptionContinueExecution or does not return. The CRT's handlers (_except_handler3/4,
// __CxxFrameHandler3) that find a matching __except / catch call RtlUnwind, which calls the
// handlers of the inner frames again with EXCEPTION_UNWINDING and unlinks them, and then jump
// to the handler code or catch continuation inside the function that owns the frame.
//
// That jump is the part a static translation needs help with: its target is the middle of a
// translated function, whose generated frame is somewhere down the host stack. Such targets
// are known when translating (resume points: xlat/x86_32.py), and a function with resume
// points registers its host frame at entry (rc32_resume_enter, a jmp_buf per invocation). An
// indirect jump to an address that is no function then lands in rc32_resume_try, which picks
// the newest live invocation that has the address as a resume point and whose frame is above
// the target esp, and longjmps there: the generated code reloads every register from rc_ctx and
// jumps to the label. The host frames in between (the CRT handlers, the dispatcher, the
// function that raised) are dropped, as their guest frames are.
#include <setjmp.h>
#include <stdio.h>

#include "rc32.h"

#define EXCEPTION_NONCONTINUABLE 1
#define EXCEPTION_UNWINDING 2
#define EXCEPTION_EXIT_UNWIND 4
#define STATUS_UNWIND 0xc0000027u
#define END_OF_CHAIN 0xffffffffu
#define CONTEXT_SIZE 0x2cc
#define CONTEXT_FULL 0x1003fu  // CONTEXT_i386 | CONTROL | INTEGER | SEGMENTS | FLOATING_POINT | DEBUG

// CONTEXT offsets (x86)
enum { CX_EDI = 0x9c, CX_ESI = 0xa0, CX_EBX = 0xa4, CX_EDX = 0xa8, CX_ECX = 0xac, CX_EAX = 0xb0,
       CX_EBP = 0xb4, CX_EIP = 0xb8, CX_EFLAGS = 0xc0, CX_ESP = 0xc4 };

static uint32_t ld32(uint32_t g) { uint32_t v; memcpy(&v, rc_mem + g, 4); return v; }
static void st32(uint32_t g, uint32_t v) { memcpy(rc_mem + g, &v, 4); }

static void ctx_save(const rc_ctx *c, uint32_t cx, uint32_t esp_after) {
  memset(rc_mem + cx, 0, CONTEXT_SIZE);
  st32(cx, CONTEXT_FULL);
  static const int off[8] = {CX_EAX, CX_ECX, CX_EDX, CX_EBX, CX_ESP, CX_EBP, CX_ESI, CX_EDI};
  for (int k = 0; k < 8; k++) st32(cx + off[k], (uint32_t)c->r[k]);
  st32(cx + CX_ESP, esp_after);
  st32(cx + CX_EFLAGS, 0x202);
}
static void ctx_load(rc_ctx *c, uint32_t cx) {
  static const int off[8] = {CX_EAX, CX_ECX, CX_EDX, CX_EBX, CX_ESP, CX_EBP, CX_ESI, CX_EDI};
  for (int k = 0; k < 8; k++) c->r[k] = ld32(cx + off[k]);
}

void rc32_raise(rc_ctx *c, uint32_t code, uint32_t flags, uint32_t nargs, const uint32_t *args) {
  uint32_t esp_after = (uint32_t)c->r[4];
  // the records live below the raiser's stack; the handlers run below them
  uint32_t rec = (esp_after - 0x100) & ~15u, cx = (rec - CONTEXT_SIZE - 16) & ~15u;
  if (nargs > 15) nargs = 15;
  memset(rc_mem + rec, 0, 80);
  st32(rec, code);
  st32(rec + 4, flags & EXCEPTION_NONCONTINUABLE);
  st32(rec + 16, nargs);
  for (uint32_t k = 0; k < nargs; k++) st32(rec + 20 + 4 * k, args[k]);
  ctx_save(c, cx, esp_after);
  uint32_t node = ld32(c->fs + RC32_TEB_SEH);
  while (node != END_OF_CHAIN && node) {
    uint32_t handler = ld32(node + 4);
    c->r[4] = (cx - 64) & ~15u;
    uint32_t a[4] = {rec, node, cx, 0};
    uint32_t r = rc32_call(c, handler, a, 4, (uint32_t)c->r[1], (uint32_t)c->r[2], 0);
    if (r == 0) {  // ExceptionContinueExecution
      if (flags & EXCEPTION_NONCONTINUABLE) rc_fail("rc32: continue after a noncontinuable exception", code);
      ctx_load(c, cx);
      return;
    }
    node = ld32(node);  // ExceptionContinueSearch (nested/collided are searched on too)
  }
  fprintf(stderr, "rc32: unhandled exception 0x%08x\n", code);
  rc_fail("unhandled guest exception", code);
}

void rc32_unwind(rc_ctx *c, uint32_t target_frame, uint32_t record, uint32_t retval) {
  uint32_t esp = (uint32_t)c->r[4];
  uint32_t rec = (esp - 0x100) & ~15u, cx = (rec - CONTEXT_SIZE - 16) & ~15u;
  if (!record) {
    record = rec;
    memset(rc_mem + rec, 0, 80);
    st32(rec, STATUS_UNWIND);
  }
  st32(record + 4, ld32(record + 4) | EXCEPTION_UNWINDING | (target_frame ? 0 : EXCEPTION_EXIT_UNWIND));
  ctx_save(c, cx, esp);
  uint32_t node = ld32(c->fs + RC32_TEB_SEH);
  while (node != END_OF_CHAIN && node && node != target_frame) {
    uint32_t handler = ld32(node + 4);
    c->r[4] = (cx - 64) & ~15u;
    uint32_t a[4] = {record, node, cx, 0};
    rc32_call(c, handler, a, 4, (uint32_t)c->r[1], (uint32_t)c->r[2], 0);
    node = ld32(node);
    st32(c->fs + RC32_TEB_SEH, node);
  }
  ctx_load(c, cx);
  c->r[0] = retval;
}

// --- resume points ------------------------------------------------------------
#define NRES 1024
struct rc32_resume_rec {
  jmp_buf jb;
  uint32_t func, esp, ip;
  const uint32_t *ips;
  int n;
};
static __thread struct rc32_resume_rec res[NRES];
static __thread int ntop;

rc32_resume_rec *rc32_resume_enter(rc_ctx *c, uint32_t func, const uint32_t *ips, int nips) {
  if (ntop == NRES) rc_fail("rc32: too many nested functions with resume points", func);
  rc32_resume_rec *r = &res[ntop++];
  r->func = func;
  r->esp = (uint32_t)c->r[4];
  r->ips = ips;
  r->n = nips;
  r->ip = 0;
  return r;
}
void rc32_resume_leave(rc_ctx *c, rc32_resume_rec *r) {
  (void)c;
  int k = (int)(r - res);
  if (k >= 0 && k < ntop) ntop = k;
}
void *rc32_resume_jb(rc32_resume_rec *r) { return r->jb; }
uint32_t rc32_resume_ip(rc32_resume_rec *r) { return r->ip; }

void rc32_resume_try(rc_ctx *c, uint32_t ip) {
  uint32_t esp = (uint32_t)c->r[4];
  for (int k = ntop - 1; k >= 0; k--) {
    rc32_resume_rec *r = &res[k];
    if (r->esp <= esp) continue;  // a frame below the target: being unwound
    for (int j = 0; j < r->n; j++) {
      if (r->ips[j] == ip) {
        ntop = k + 1;
        r->ip = ip;
        _longjmp(r->jb, 1);
      }
    }
  }
}

_Noreturn void rc32_resume(rc_ctx *c, uint32_t ip) {
  rc32_resume_try(c, ip);
  rc_fail("rc32: no live frame has this resume point", ip);
}

// --- the imports this file implements (gen_image_pe.py: builtin thunks) ------------
// RaiseException(code, flags, nargs, args): stdcall
void rc32_imp_RaiseException(rc_ctx *c) {
  uint32_t esp = (uint32_t)c->r[4];
  uint32_t code = ld32(esp), flags = ld32(esp + 4), n = ld32(esp + 8), pa = ld32(esp + 12), a[15];
  if (n > 15) n = 15;
  for (uint32_t k = 0; k < n; k++) a[k] = pa ? ld32(pa + 4 * k) : 0;
  c->r[4] = esp + 16;  // the registers of a continued exception are those after the call
  rc32_raise(c, code, flags, n, a);
}
// RtlUnwind(TargetFrame, TargetIp, ExceptionRecord, ReturnValue): stdcall
void rc32_imp_RtlUnwind(rc_ctx *c) {
  uint32_t esp = (uint32_t)c->r[4];
  uint32_t frame = ld32(esp), rec = ld32(esp + 8), val = ld32(esp + 12);
  uint64_t keep[8];
  memcpy(keep, c->r, sizeof keep);
  rc32_unwind(c, frame, rec, val);
  // RtlUnwind keeps the caller's registers (but eax), whatever the handlers did
  memcpy(c->r, keep, sizeof keep);
  c->r[0] = val;
  c->r[4] = esp + 16;
}

// Drop every resume record of this thread (a test runner, after a case that faulted).
void rc32_resume_reset(void) { ntop = 0; }
