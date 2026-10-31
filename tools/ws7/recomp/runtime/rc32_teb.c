// 32-bit guests: per-thread context, guest stack and TEB (rc32.h), and native -> guest calls.
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "rc32.h"
#include "rc32_x87.h"

#define STACK_SIZE (1u << 20)
#define RET_SENTINEL 0xfffffff0u  // return address of a native -> guest call (never executed)

extern uint32_t rc32_tls_start, rc32_tls_end, rc32_tls_zerofill;  // rc32_image.c

static uint32_t peb;
static pthread_key_t key;
static pthread_once_t once = PTHREAD_ONCE_INIT;
static void mk(void) { pthread_key_create(&key, 0); }

static void st32(uint32_t g, uint32_t v) { memcpy(rc_mem + g, &v, 4); }

rc_ctx *rc32_thread_ctx(void) {
  pthread_once(&once, mk);
  rc_ctx *c = pthread_getspecific(key);
  if (c) return c;
  c = calloc(1, sizeof *c);
  uint32_t stk = rc32_alloc(STACK_SIZE), teb = rc32_alloc(RC32_TEB_SIZE);
  if (!peb) peb = rc32_alloc(0x1000);
  if (!c || !stk || !teb || !peb) rc_fail("rc32: no room for a thread in the guest window", 0);
  c->r[4] = stk + STACK_SIZE - 64;
  c->fs = teb;
  c->mxcsr = 0x1f80;
  rc87_init(c);
  c->fpu.cw = 0x27f;  // what a Windows thread starts with: 53-bit precision, exceptions masked
  st32(teb + RC32_TEB_SEH, 0xffffffffu);
  st32(teb + RC32_TEB_STACKBASE, stk + STACK_SIZE);
  st32(teb + RC32_TEB_STACKLIMIT, stk);
  st32(teb + RC32_TEB_SELF, teb);
  st32(teb + RC32_TEB_PID, (uint32_t)getpid());
  st32(teb + RC32_TEB_TID, (uint32_t)(uintptr_t)pthread_mach_thread_np(pthread_self()));
  st32(teb + 0x30, peb);
  if (rc32_tls_end > rc32_tls_start) {
    uint32_t n = rc32_tls_end - rc32_tls_start, blk = rc32_alloc(n + rc32_tls_zerofill), arr = rc32_alloc(64);
    if (!blk || !arr) rc_fail("rc32: no room for TLS", 0);
    memcpy(rc_mem + blk, rc_mem + rc32_tls_start, n);
    st32(arr, blk);
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
  c->ret_to = 0;
  f(c);
  if (c->ret_to != RET_SENTINEL) rc_fail("rc32_call: the guest returned to another address", c->ret_to);
  c->r[4] = esp;
  if (hi) *hi = (uint32_t)c->r[2];
  return (uint32_t)c->r[0];
}
