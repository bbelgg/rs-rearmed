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
static uint32_t ld32(uint32_t g) { uint32_t v; memcpy(&v, rc_mem + g, 4); return v; }

static uint32_t stack_default = STACK_SIZE;
static __thread uint32_t stack_asked;
void rc32_stack_default(uint32_t size) { if (size > stack_default) stack_default = size; }
void rc32_thread_stack(uint32_t size) { stack_asked = size; }

// What a thread has in the window. A thread that ends hands it to the next one that asks for a
// stack of the same size (rc32_thread_end): the arena never shrinks, so without that a session
// that makes many short threads would use the window up.
typedef struct {
  uint32_t stk, size, teb, arr;  // the stack [stk, stk + size), the TEB, the TLS pointer array
  int ntls;                      // and how many images' blocks it points to
} thread_mem;
typedef struct {
  rc_ctx c;  // first: what rc32_thread_ctx returns
  thread_mem m;
} thread_rec;
#define SPARE 64
static thread_mem spare[SPARE];
static int nspare;
static pthread_mutex_t spare_mu = PTHREAD_MUTEX_INITIALIZER;

rc_ctx *rc32_thread_ctx(void) {
  pthread_once(&once, mk);
  thread_rec *t = pthread_getspecific(key);
  if (t) return &t->c;
  t = calloc(1, sizeof *t);
  if (!t) rc_fail("rc32: no room for a thread in the guest window", 0);
  rc_ctx *c = &t->c;
  uint32_t size = stack_asked > stack_default ? stack_asked : stack_default;
  if (size > STACK_MAX) size = STACK_MAX;
  size = (size + 0xffffu) & ~0xffffu;
  const rc32_tls_image *tls;
  int ntls = rc32_tls_images(&tls);
  thread_mem m = {0, 0, 0, 0, 0};
  pthread_mutex_lock(&spare_mu);
  for (int k = 0; k < nspare; k++)
    if (spare[k].size == size) {
      m = spare[k];
      spare[k] = spare[--nspare];
      break;
    }
  pthread_mutex_unlock(&spare_mu);
  uint32_t teb = m.teb, stk = m.stk;
  if (stk) {
    // an ended thread's: its stack is fresh pages already, the TEB and the TLS blocks are cleared here
    memset(rc_mem + teb, 0, RC32_TEB_SIZE);
  } else {
    uint32_t page = (uint32_t)getpagesize(), blk = rc32_alloc(size + STACK_GUARD + page);
    teb = rc32_alloc(RC32_TEB_SIZE);
    if (!blk || !teb) rc_fail("rc32: no room for a thread in the guest window", 0);
    uint32_t guard = (blk + page - 1) & ~(page - 1);
    stk = guard + STACK_GUARD;
    rc32_mem_guard(guard, STACK_GUARD);
    m = (thread_mem){stk, size, teb, 0, 0};
  }
  if (!peb) peb = rc32_alloc(0x1000);
  if (!peb) rc_fail("rc32: no room for a thread in the guest window", 0);
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
  if (ntls) {  // ThreadLocalStoragePointer[index]: this thread's copy of each image's template
    if (m.ntls != ntls) m.arr = 0;  // an image with TLS came since: a new array and blocks
    uint32_t arr = m.arr ? m.arr : rc32_alloc(4 * (uint32_t)ntls);
    if (!arr) rc_fail("rc32: no room for TLS", 0);
    for (int k = 0; k < ntls; k++) {
      uint32_t n = tls[k].end > tls[k].start ? tls[k].end - tls[k].start : 0, full = n + tls[k].zerofill + 1;
      uint32_t blk = m.arr ? ld32(arr + 4 * (uint32_t)k) : rc32_alloc(full);
      if (!blk) rc_fail("rc32: no room for TLS", 0);
      if (m.arr) memset(rc_mem + blk, 0, full);
      memcpy(rc_mem + blk, rc_mem + tls[k].start, n);
      st32(arr + 4 * (uint32_t)k, blk);
    }
    st32(teb + RC32_TEB_TLSPTR, arr);
    m.arr = arr;
    m.ntls = ntls;
  }
  t->m = m;
  pthread_setspecific(key, t);
  return c;
}

void rc32_thread_end(void) {
  pthread_once(&once, mk);
  thread_rec *t = pthread_getspecific(key);
  if (!t) return;
  pthread_setspecific(key, 0);
  // the stack's pages go back to the system and read as zero for the next thread; kept as it is
  // (and so left out of the spares) only if that fails
  int ok = rc32_mem_fresh(t->m.stk, t->m.size) == 0;
  pthread_mutex_lock(&spare_mu);
  if (ok && nspare < SPARE) spare[nspare++] = t->m;
  pthread_mutex_unlock(&spare_mu);
  free(t);
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
