// 32-bit guests: per-thread context, guest stack and TEB (rc32.h), and native -> guest calls.
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "rc32.h"
#include "rc32_x87.h"

#define STACK_SIZE (1u << 20)   // at least: a thread asks for more with rc32_thread_stack
#define STACK_MAX (256u << 20)
#define STACK_GUARD 0x4000u     // no access below the stack: an overflow faults, as on Windows
#define RET_SENTINEL 0xfffffff0u  // return address of a native -> guest call (never executed)

static uint32_t peb;
static pthread_key_t key;
static pthread_once_t once = PTHREAD_ONCE_INIT;
static void mk(void) { pthread_key_create(&key, 0); }

static void st32(uint32_t g, uint32_t v) { memcpy(rc_mem + g, &v, 4); }

static uint32_t stack_default = STACK_SIZE;
static __thread uint32_t stack_asked;
void rc32_stack_default(uint32_t size) { if (size > stack_default) stack_default = size; }
void rc32_thread_stack(uint32_t size) { stack_asked = size; }

rc_ctx *rc32_thread_ctx(void) {
  pthread_once(&once, mk);
  rc_ctx *c = pthread_getspecific(key);
  if (c) return c;
  c = calloc(1, sizeof *c);
  uint32_t size = stack_asked > stack_default ? stack_asked : stack_default;
  if (size > STACK_MAX) size = STACK_MAX;
  size = (size + 0xffffu) & ~0xffffu;
  uint32_t page = (uint32_t)getpagesize(), blk = rc32_alloc(size + STACK_GUARD + page), teb = rc32_alloc(RC32_TEB_SIZE);
  if (!peb) peb = rc32_alloc(0x1000);
  if (!c || !blk || !teb || !peb) rc_fail("rc32: no room for a thread in the guest window", 0);
  uint32_t guard = (blk + page - 1) & ~(page - 1), stk = guard + STACK_GUARD;
  rc32_mem_guard(guard, STACK_GUARD);
  c->r[4] = stk + size - 64;
  c->fs = teb;
  c->mxcsr = 0x1f80;
  rc87_init(c);
  c->fpu.cw = 0x27f;  // what a Windows thread starts with: 53-bit precision, exceptions masked
  st32(teb + RC32_TEB_SEH, 0xffffffffu);
  st32(teb + RC32_TEB_STACKBASE, stk + size);
  st32(teb + RC32_TEB_STACKLIMIT, stk);
  st32(teb + RC32_TEB_SELF, teb);
  st32(teb + RC32_TEB_PID, (uint32_t)getpid());
  st32(teb + RC32_TEB_TID, (uint32_t)(uintptr_t)pthread_mach_thread_np(pthread_self()));
  st32(teb + 0x30, peb);
  const rc32_tls_image *tls;
  int ntls = rc32_tls_images(&tls);
  if (ntls) {  // ThreadLocalStoragePointer[index]: this thread's copy of each image's template
    uint32_t arr = rc32_alloc(4 * (uint32_t)ntls);
    if (!arr) rc_fail("rc32: no room for TLS", 0);
    for (int k = 0; k < ntls; k++) {
      uint32_t n = tls[k].end > tls[k].start ? tls[k].end - tls[k].start : 0, blk = rc32_alloc(n + tls[k].zerofill + 1);
      if (!blk) rc_fail("rc32: no room for TLS", 0);
      memcpy(rc_mem + blk, rc_mem + tls[k].start, n);
      st32(arr + 4 * (uint32_t)k, blk);
    }
    st32(teb + RC32_TEB_TLSPTR, arr);
  }
  pthread_setspecific(key, c);
  return c;
}

uint32_t rc32_call(rc_ctx *c, uint32_t g, const uint32_t *args, int nargs, uint32_t ecx,
                   uint32_t edx, uint32_t *hi) {
  uint64_t esp = c->r[4];
  for (int k = nargs - 1; k >= 0; k--) {
    c->r[4] = (uint32_t)(c->r[4] - 4);
    st32((uint32_t)c->r[4], args[k]);
  }
  c->r[4] = (uint32_t)(c->r[4] - 4);
  st32((uint32_t)c->r[4], RET_SENTINEL);
  c->r[1] = ecx;
  c->r[2] = edx;
  rc_fn f = rc32_lookup(g);
  if (!f) rc_fail("rc32_call: no translated function", g);
  uint32_t ret_to = c->ret_to;  // guest code above is waiting for its own (an import called it, RtlUnwind)
  c->ret_to = 0;
  f(c);
  if (c->ret_to != RET_SENTINEL) rc_fail("rc32_call: the guest returned to another address", c->ret_to);
  c->ret_to = ret_to;
  c->r[4] = esp;
  if (hi) *hi = (uint32_t)c->r[2];
  return (uint32_t)c->r[0];
}
