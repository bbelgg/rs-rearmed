// CloseHandle against the kernel32 calls that change an object (win32_ksync.c): SetEvent, ResetEvent,
// ReleaseMutex, ReleaseSemaphore, SetWaitableTimer, CancelWaitableTimer, SignalObjectAndWait,
// w32_event_signal, DuplicateHandle. Each looks its object up under the handle lock, so a close of
// the last handle on another thread ends the call with an invalid handle, not with a write to freed
// memory. Built with the address sanitizer, which reports the write this test provokes when the
// lookup is outside the lock.
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "rc32.h"
#include "win32_handles.h"

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)
#define SECONDS 2
#define NSLOTS 8

extern const w32_dll w32_dll_kernel32_sync;
static rc_fn fn(const char *name) {
  for (int k = 0; k < w32_dll_kernel32_sync.n; k++)
    if (!strcmp(w32_dll_kernel32_sync.fns[k].name, name)) return w32_dll_kernel32_sync.fns[k].fn;
  fprintf(stderr, "FAIL: no %s\n", name);
  exit(1);
}
// a kernel32 call in the import convention: arguments at esp, no return address
static uint32_t call(rc_ctx *c, rc_fn f, int n, const uint32_t *a) {
  uint32_t esp0 = (uint32_t)c->r[4];
  c->r[4] -= 4u * (uint32_t)n;
  for (int k = 0; k < n; k++) w32_wr32((uint32_t)c->r[4] + 4 * k, a[k]);
  f(c);
  c->r[4] = esp0;
  return (uint32_t)c->r[0];
}

static _Atomic uint32_t slots[NSLOTS];
static atomic_int running = 1;
static _Atomic unsigned long ok_calls;

static void *caller(void *arg) {
  rc_ctx *c = rc32_thread_ctx();
  rc_fn set = fn("SetEvent"), reset = fn("ResetEvent"), relmu = fn("ReleaseMutex"), relsem = fn("ReleaseSemaphore");
  rc_fn settm = fn("SetWaitableTimer"), cancel = fn("CancelWaitableTimer"), sow = fn("SignalObjectAndWait");
  uint32_t due = w32_galloc(8), r = 0;
  uint64_t d = (uint64_t)-1000;
  memcpy(RC32_H(due), &d, 8);
  unsigned k = (unsigned)(uintptr_t)arg;
  uint32_t self_ev = w32_handle_new(H_EVENT, 0);
  CHECK(self_ev);
  while (atomic_load(&running)) {
    uint32_t h = atomic_load(&slots[k++ % NSLOTS]);
    if (!h) continue;
    uint32_t a[6] = {h, 1, 0, 0, 0, 0};
    switch (k % 8) {
    case 0: r = call(c, set, 1, a); break;
    case 1: r = call(c, reset, 1, a); break;
    case 2: r = call(c, relmu, 1, a); break;
    case 3: r = call(c, relsem, 3, a); break;
    case 4: a[1] = due; a[2] = 0; r = call(c, settm, 6, a); break;
    case 5: r = call(c, cancel, 1, a); break;
    case 6: a[1] = self_ev; a[2] = 0; r = call(c, sow, 4, a); break;
    case 7: r = (uint32_t)w32_event_signal(h); break;
    }
    if (r) atomic_fetch_add(&ok_calls, 1);
    uint32_t dup = w32_handle_dup(h);
    if (dup) w32_handle_close(dup);
  }
  return 0;
}

int main(void) {
  CHECK(rc32_mem_init() == 0);
  pthread_t t[4];
  for (uintptr_t k = 0; k < 4; k++) pthread_create(&t[k], 0, caller, (void *)k);
  static const int types[] = {H_EVENT, H_MUTEX, H_SEM, H_TIMER};
  unsigned long closes = 0;
  uint64_t end = clock_gettime_nsec_np(CLOCK_MONOTONIC) + (uint64_t)SECONDS * 1000000000;
  for (unsigned k = 0; clock_gettime_nsec_np(CLOCK_MONOTONIC) < end; k++) {
    w32_obj *o;
    uint32_t h = w32_handle_new(types[k % 4], &o);
    CHECK(h);
    if (types[k % 4] == H_SEM) o->u.sem.max = 1 << 30;
    uint32_t old = atomic_exchange(&slots[k % NSLOTS], h);
    if (old) {
      CHECK(w32_handle_close(old));
      closes++;
    }
  }
  atomic_store(&running, 0);
  for (int k = 0; k < 4; k++) pthread_join(t[k], 0);
  printf("%lu closes against %lu calls on live objects\n", closes, (unsigned long)atomic_load(&ok_calls));
  CHECK(closes > 1000 && atomic_load(&ok_calls) > 1000);
  printf("ok\n");
  return 0;
}
