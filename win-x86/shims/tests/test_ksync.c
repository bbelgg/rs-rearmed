// The kernel32 waits of the Win32 seam (win32_ksync.c) from host threads (w32_wait_multiple,
// w32_event_signal): a change of an object wakes the threads waiting for it and no others, an
// auto-reset event hands each SetEvent to one wait, wait-any reports the object that ended it,
// wait-all waits for all, a timeout times out, a wait keeps the objects it waits for when their
// handles are closed, PulseEvent ends the waits there are and no later one. `test_ksync herd` runs with LP_WAKE_ALL=1 and expects the other threads to be
// woken (the old behaviour, kept as a switch).
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "win32_handles.h"

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)
#define TIMEOUT 0x102u

static uint32_t event(int manual) {
  w32_obj *o;
  uint32_t h = w32_handle_new(H_EVENT, &o);
  CHECK(h && o);
  o->u.ev.manual = manual;
  return h;
}
static unsigned long wakeups(void) {
  pthread_mutex_lock(&w32_hlock);
  unsigned long n = w32_wait_wakeups;
  pthread_mutex_unlock(&w32_hlock);
  return n;
}
static double now_ms(void) { return clock_gettime_nsec_np(CLOCK_MONOTONIC) / 1e6; }

// a thread in one long wait on its own event, as most of the game's threads are in a song
static uint32_t idle_ev;
static atomic_int idle_returns;
static void *idle_waiter(void *arg) {
  (void)arg;
  uint32_t r = w32_wait_multiple(1, &idle_ev, 0, 0xffffffffu);
  CHECK(r == 0);
  atomic_fetch_add(&idle_returns, 1);
  return 0;
}

// hand-off: N SetEvents of an auto-reset event, each taken by one wait, acknowledged on another
#define HANDOFFS 2000
static uint32_t ping, pong;
static void *ponger(void *arg) {
  (void)arg;
  for (int k = 0; k < HANDOFFS; k++) {
    CHECK(w32_wait_multiple(1, &ping, 0, 5000) == 0);
    CHECK(w32_event_signal(pong));
  }
  return 0;
}

struct multi { uint32_t hs[2]; int all; uint32_t ms; uint32_t r; double took; };
static void *multi_waiter(void *arg) {
  struct multi *m = arg;
  double t0 = now_ms();
  m->r = w32_wait_multiple(2, m->hs, m->all, m->ms);
  m->took = now_ms() - t0;
  return 0;
}

struct one { uint32_t h, ms, r; };
static void *one_waiter(void *arg) {
  struct one *w = arg;
  w->r = w32_wait_multiple(1, &w->h, 0, w->ms);
  return 0;
}
// n waits on h, begun in order, ended by one pulse or by their timeout of ms
static void pulse_waits(uint32_t h, struct one *w, int n, uint32_t ms) {
  pthread_t t[8];
  for (int k = 0; k < n; k++) {
    w[k] = (struct one){h, ms, 0};
    pthread_create(&t[k], 0, one_waiter, &w[k]);
    usleep(20000);
  }
  CHECK(w32_event_pulse(h));
  for (int k = 0; k < n; k++) pthread_join(t[k], 0);
}

int main(int argc, char **argv) {
  int herd = argc > 1 && !strcmp(argv[1], "herd");
  if (herd) setenv("LP_WAKE_ALL", "1", 1);

  // 1. idle waiters are not woken by other objects' changes
  idle_ev = event(0);
  pthread_t idle[4];
  for (int k = 0; k < 4; k++) pthread_create(&idle[k], 0, idle_waiter, 0);
  usleep(100000);
  uint32_t busy = event(0);
  unsigned long w0 = wakeups();
  for (int k = 0; k < 1000; k++) CHECK(w32_event_signal(busy));
  usleep(50000);
  unsigned long spurious = wakeups() - w0;
  printf("1000 SetEvents of an event nobody waits for: %lu wake-ups of 4 waiting threads\n", spurious);
  if (herd) CHECK(spurious >= 4);  // each waiting thread at least once (signals coalesce)
  else CHECK(spurious == 0);
  CHECK(atomic_load(&idle_returns) == 0);

  // 2. an auto-reset event hands each SetEvent to one wait, with the idle threads still waiting
  ping = event(0);
  pong = event(0);
  pthread_t p;
  pthread_create(&p, 0, ponger, 0);
  double t0 = now_ms();
  w0 = wakeups();
  for (int k = 0; k < HANDOFFS; k++) {
    CHECK(w32_event_signal(ping));
    CHECK(w32_wait_multiple(1, &pong, 0, 5000) == 0);
  }
  pthread_join(p, 0);
  unsigned long hw = wakeups() - w0;
  printf("%d hand-offs in %.1f ms, %lu wake-ups\n", HANDOFFS, now_ms() - t0, hw);
  if (!herd) CHECK(hw <= 3 * HANDOFFS);  // the two hand-off threads only (about 2 per hand-off)
  CHECK(atomic_load(&idle_returns) == 0);

  // 3. wait-any: the index of the object that ended it
  struct multi m = {{event(0), event(1)}, 0, 5000, 0, 0};
  pthread_t t;
  pthread_create(&t, 0, multi_waiter, &m);
  usleep(50000);
  CHECK(w32_event_signal(m.hs[1]));
  pthread_join(t, 0);
  printf("wait-any: %u after %.1f ms\n", m.r, m.took);
  CHECK(m.r == 1 && m.took < 1000);

  // 4. wait-all: not before both are set
  struct multi a = {{event(1), event(1)}, 1, 5000, 0, 0};
  pthread_create(&t, 0, multi_waiter, &a);
  usleep(50000);
  CHECK(w32_event_signal(a.hs[0]));
  usleep(100000);
  CHECK(a.took == 0);  // still waiting
  CHECK(w32_event_signal(a.hs[1]));
  pthread_join(t, 0);
  printf("wait-all: %u after %.1f ms\n", a.r, a.took);
  CHECK(a.r == 0 && a.took >= 140 && a.took < 1000);

  // 5. a timeout
  uint32_t never = event(0);
  t0 = now_ms();
  CHECK(w32_wait_multiple(1, &never, 0, 60) == TIMEOUT);
  double took = now_ms() - t0;
  printf("timeout of 60 ms after %.1f ms\n", took);
  CHECK(took >= 59 && took < 1000);

  // 6. a wait keeps its objects: closing their last handles frees nothing until it ends, and
  // new objects are not mistaken for them
  struct multi cl = {{event(0), event(0)}, 0, 200, 0, 0};
  w32_obj *held = w32_handle_get(cl.hs[0], H_EVENT);
  pthread_create(&t, 0, multi_waiter, &cl);
  usleep(50000);
  CHECK(w32_handle_close(cl.hs[0]) && w32_handle_close(cl.hs[1]));
  CHECK(!w32_handle_close(cl.hs[0]));
  pthread_mutex_lock(&w32_hlock);
  CHECK(held->refs == 1 && held->type == H_EVENT);  // the wait's own
  pthread_mutex_unlock(&w32_hlock);
  for (int k = 0; k < 8; k++) CHECK(w32_event_signal(event(1)));
  pthread_join(t, 0);
  printf("wait on closed handles: 0x%x after %.1f ms\n", cl.r, cl.took);
  CHECK(cl.r == TIMEOUT && cl.took >= 190);

  // 7. a wait goes on through another handle of its object when the one it named is closed
  struct multi dp = {{event(0), event(0)}, 0, 5000, 0, 0};
  uint32_t second = w32_handle_dup(dp.hs[0]);
  CHECK(second);
  pthread_create(&t, 0, multi_waiter, &dp);
  usleep(50000);
  CHECK(w32_handle_close(dp.hs[0]));
  CHECK(w32_event_signal(second));
  pthread_join(t, 0);
  printf("wait through a duplicate: %u after %.1f ms\n", dp.r, dp.took);
  CHECK(dp.r == 0 && dp.took < 1000);

  // 8. PulseEvent, manual-reset: every wait there is ends, and the event is left reset
  struct one w[3];
  uint32_t pm = event(1);
  pulse_waits(pm, w, 3, 5000);
  for (int k = 0; k < 3; k++) CHECK(w[k].r == 0);
  CHECK(w32_wait_multiple(1, &pm, 0, 0) == TIMEOUT);

  // 9. PulseEvent, auto-reset: the oldest wait ends, the others time out
  uint32_t pa = event(0);
  pulse_waits(pa, w, 3, 200);
  printf("pulse of an auto-reset event: %x %x %x\n", w[0].r, w[1].r, w[2].r);
  CHECK(w[0].r == 0 && w[1].r == TIMEOUT && w[2].r == TIMEOUT);
  CHECK(w32_wait_multiple(1, &pa, 0, 0) == TIMEOUT);

  // 10. a pulse with nobody waiting leaves nothing behind, and an event set stays reset after it
  CHECK(w32_event_pulse(pa));
  CHECK(w32_wait_multiple(1, &pa, 0, 0) == TIMEOUT);
  CHECK(w32_event_signal(pm));
  CHECK(w32_event_pulse(pm));
  CHECK(w32_wait_multiple(1, &pm, 0, 0) == TIMEOUT);
  CHECK(!w32_event_pulse(0x1000u + 4 * 16000));

  // 11. a wait-all ends on a pulse only when the other objects are set at that moment, and then
  // takes them
  struct multi pw = {{event(0), event(0)}, 1, 200, 0, 0};
  pthread_create(&t, 0, multi_waiter, &pw);
  usleep(50000);
  CHECK(w32_event_pulse(pw.hs[0]));
  pthread_join(t, 0);
  CHECK(pw.r == TIMEOUT);
  pw.ms = 5000;
  pthread_create(&t, 0, multi_waiter, &pw);
  usleep(50000);
  CHECK(w32_event_signal(pw.hs[1]));  // not enough alone
  usleep(50000);
  CHECK(w32_event_pulse(pw.hs[0]));
  pthread_join(t, 0);
  printf("wait-all ended by a pulse: %u after %.1f ms\n", pw.r, pw.took);
  CHECK(pw.r == 0 && pw.took < 1000);
  CHECK(w32_wait_multiple(1, &pw.hs[1], 0, 0) == TIMEOUT);  // taken: auto-reset

  // 12. the idle threads end when their event is set
  for (int k = 0; k < 4; k++) {
    CHECK(w32_event_signal(idle_ev));
    usleep(20000);
  }
  for (int k = 0; k < 4; k++) pthread_join(idle[k], 0);
  CHECK(atomic_load(&idle_returns) == 4);
  printf("ok%s\n", herd ? " (LP_WAKE_ALL=1)" : "");
  return 0;
}
