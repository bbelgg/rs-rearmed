// kernel32: the handle table, waits, events, mutexes, semaphores, waitable timers, threads,
// critical sections, interlocked operations and SLists, TLS and FLS (win32_handles.h).
#include <dirent.h>
#include <errno.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>
#include <pthread/qos.h>

#include "win32_handles.h"

#define MAXH 16384
#define HBASE 0x1000u
static w32_obj *objs[MAXH];
static uint32_t hint;
int w32_log;  // LP_LOG, set by w32_init (defined here: the audio test links this file without the rest)
pthread_mutex_t w32_hlock = PTHREAD_RECURSIVE_MUTEX_INITIALIZER;
pthread_cond_t w32_hwake = PTHREAD_COND_INITIALIZER;

// The waits (wait_tid) sleep on a condition of their own, listed with the objects they wait for,
// and a change of an object wakes only the threads waiting for it (w32_hwake_obj). With one
// condition for every wait, each SetEvent woke every waiting thread of the game, a dozen of them,
// most in a wait that would not end for minutes, to re-check and sleep again: in a song the
// process used 69% of a core with it and 52% without (docs/windows/PERFORMANCE.md). w32_hwake stays for the
// thread start-up handshakes. LP_WAKE_ALL=1 wakes every waiting thread on every change again (for
// telling a missed wake-up from other hangs; it costs more than the old broadcast did).
typedef struct waiter {
  pthread_cond_t cv;
  w32_obj *const *o;
  int n, all;
  uint32_t tid;
  uint32_t r;  // WAIT_TIMEOUT, or how a PulseEvent ended the wait
  struct waiter *next, *prev;
} waiter;
static waiter *waiters;                                           // under w32_hlock
static __thread waiter self_waiter = {PTHREAD_COND_INITIALIZER};  // this thread's, while it waits
unsigned long w32_wait_wakeups;                                   // under w32_hlock: for tests
static int wake_all = -1;

void w32_hwake_obj(w32_obj *o) {
  if (wake_all < 0) {
    const char *e = getenv("LP_WAKE_ALL");
    wake_all = e && *e == '1';
  }
  for (waiter *w = waiters; w; w = w->next) {
    int hit = wake_all;
    for (int k = 0; !hit && k < w->n; k++) hit = w->o[k] == o;
    if (hit) pthread_cond_signal(&w->cv);
  }
}
static w32_obj *slot_obj[MAXH];  // the object each handle slot refers to (several slots may share one)

unsigned w32_handles_open(void) {
  unsigned n = 0;
  pthread_mutex_lock(&w32_hlock);
  for (uint32_t k = 0; k < MAXH; k++) n += slot_obj[k] != 0;
  pthread_mutex_unlock(&w32_hlock);
  return n;
}
unsigned long w32_threads_made;

uint32_t w32_handle_new(int type, w32_obj **out) {
  if (out) *out = 0;
  w32_obj *o = calloc(1, sizeof *o);
  if (!o) return 0;
  o->type = type;
  o->refs = 1;
  pthread_mutex_lock(&w32_hlock);
  uint32_t h = 0;
  for (uint32_t n = 0; n < MAXH; n++) {
    uint32_t k = (hint + n) % MAXH;
    if (!slot_obj[k]) {
      slot_obj[k] = o;
      objs[k] = o;
      hint = k + 1;
      h = HBASE + 4 * k;
      break;
    }
  }
  pthread_mutex_unlock(&w32_hlock);
  if (!h) {
    free(o);
    fprintf(stderr, "w32: out of handles\n");
    return 0;
  }
  if (out) *out = o;
  return h;
}

static uint32_t slot_of(uint32_t h) { return (h - HBASE) / 4; }

w32_obj *w32_handle_get(uint32_t h, int type) {
  if (h < HBASE || (h & 3) || slot_of(h) >= MAXH) return 0;
  pthread_mutex_lock(&w32_hlock);
  w32_obj *o = slot_obj[slot_of(h)];
  if (o && type >= 0 && o->type != type) o = 0;
  pthread_mutex_unlock(&w32_hlock);
  return o;
}

// a new slot on an existing object (refs counted by the caller's lock)
static uint32_t slot_for(w32_obj *o) {
  for (uint32_t n = 0; n < MAXH; n++) {
    uint32_t k = (hint + n) % MAXH;
    if (!slot_obj[k]) {
      slot_obj[k] = o;
      hint = k + 1;
      o->refs++;
      return HBASE + 4 * k;
    }
  }
  return 0;
}

uint32_t w32_handle_dup(uint32_t h) {
  pthread_mutex_lock(&w32_hlock);
  w32_obj *o = w32_handle_get(h, -1);
  uint32_t r = o ? slot_for(o) : 0;
  pthread_mutex_unlock(&w32_hlock);
  return r;
}

static void obj_free(w32_obj *o) {
  switch (o->type) {
  case H_FILE:
    if (!o->u.file.console && o->u.file.fd >= 0) close(o->u.file.fd);
    free(o->u.file.path);
    break;
  case H_FIND:
    if (o->u.find.dir) closedir((DIR *)o->u.find.dir);
    if (o->u.find.dir2) closedir((DIR *)o->u.find.dir2);
    free(o->u.find.hostdir);
    free(o->u.find.hostdir2);
    free(o->u.find.over);
    free(o->u.find.pattern);
    break;
  case H_MAPPING:
    if (o->u.map.fd >= 0) close(o->u.map.fd);
    break;
  case H_THREAD:
    if (!o->u.th.done) return;  // the thread holds it until it ends (thread_main frees it then)
    break;
  }
  free(o->name);
  free(o);
}

// --- console handles ---------------------------------------------------------------------------
static uint32_t std_h[3];
static uint32_t std_handle(int k) {
  pthread_mutex_lock(&w32_hlock);
  if (!std_h[k]) {
    w32_obj *o;
    std_h[k] = w32_handle_new(H_FILE, &o);
    if (o) {
      o->u.file.fd = k;
      o->u.file.console = 1;
      o->refs = 1 << 30;  // never closed for good
    }
  }
  pthread_mutex_unlock(&w32_hlock);
  return std_h[k];
}

int w32_file_fd(uint32_t h) {
  w32_obj *o = w32_handle_get(h, H_FILE);
  return o ? o->u.file.fd : -1;
}

static void k_GetStdHandle(rc_ctx *c) {
  uint32_t id = w32_arg(c, 0);
  int k = id == 0xfffffff6u ? 0 : id == 0xfffffff5u ? 1 : id == 0xfffffff4u ? 2 : -1;
  w32_ret(c, k < 0 ? 0xffffffffu : std_handle(k), 1);
}
static void k_SetStdHandle(rc_ctx *c) { w32_ret(c, 1, 2); }

// --- CloseHandle / DuplicateHandle -------------------------------------------------------------
int w32_handle_close(uint32_t h) {
  pthread_mutex_lock(&w32_hlock);
  w32_obj *o = h >= HBASE && !(h & 3) && slot_of(h) < MAXH ? slot_obj[slot_of(h)] : 0;
  if (o) {
    slot_obj[slot_of(h)] = 0;
    if (--o->refs == 0) obj_free(o);
  }
  pthread_mutex_unlock(&w32_hlock);
  return o != 0;
}
static void k_CloseHandle(rc_ctx *c) {
  uint32_t h = w32_arg(c, 0);
  if (h == 0xffffffffu || h == 0xfffffffeu) { w32_ret(c, 1, 1); return; }  // pseudo handles
  int ok = w32_handle_close(h);
  if (!ok) w32_set_error(c, W32_ERROR_INVALID_HANDLE);
  w32_ret(c, (uint32_t)ok, 1);
}

static uint32_t thread_self_handle(rc_ctx *c);
static void k_DuplicateHandle(rc_ctx *c) {
  uint32_t src = w32_arg(c, 1), out = w32_arg(c, 3), opts = w32_arg(c, 6);
  uint32_t r = 0;
  if (src == 0xfffffffeu) r = thread_self_handle(c);  // a real handle on the calling thread
  else if (src == 0xffffffffu) r = 0xffffffffu;
  else r = w32_handle_dup(src);
  if (r && (opts & 1)) w32_handle_close(src);  // DUPLICATE_CLOSE_SOURCE (a pseudo handle is none: ignored)
  if (out) w32_wr32(out, r);
  if (!r) w32_set_error(c, W32_ERROR_INVALID_HANDLE);
  w32_ret(c, r != 0, 7);
}

// --- named objects -----------------------------------------------------------------------------
// The handle of an existing object of this name and type (ERROR_ALREADY_EXISTS), else 0
static uint32_t open_named(rc_ctx *c, const char *name, int type) {
  if (!name || !*name) return 0;
  uint32_t r = 0;
  pthread_mutex_lock(&w32_hlock);
  for (uint32_t k = 0; k < MAXH && !r; k++)
    if (slot_obj[k] && slot_obj[k]->name && slot_obj[k]->type == type && !strcmp(slot_obj[k]->name, name))
      r = slot_for(slot_obj[k]);
  pthread_mutex_unlock(&w32_hlock);
  if (r) w32_set_error(c, W32_ERROR_ALREADY_EXISTS);
  return r;
}

static const char *name_a(uint32_t g) { return g ? (const char *)RC32_H(g) : 0; }
static const char *name_w(uint32_t g, char *buf, size_t n) {  // object names: bytes, '?' past ASCII
  if (!g) return 0;
  size_t k = 0;
  for (uint16_t ch; k < n - 1 && (ch = *(uint16_t *)RC32_H(g + 2 * k)); k++) buf[k] = (char)(ch < 128 ? ch : '?');
  buf[k] = 0;
  return buf;
}

static uint32_t new_named(rc_ctx *c, int type, const char *name, w32_obj **o) {
  uint32_t h = open_named(c, name, type);
  if (h) { *o = 0; return h; }
  w32_set_error(c, 0);
  h = w32_handle_new(type, o);
  if (h && name && *name) (*o)->name = strdup(name);
  return h;
}

// --- events ------------------------------------------------------------------------------------
static uint32_t create_event(rc_ctx *c, int manual, int initial, const char *name) {
  w32_obj *o;
  uint32_t h = new_named(c, H_EVENT, name, &o);
  if (o) {
    o->u.ev.manual = manual;
    o->u.ev.signaled = initial;
  }
  return h;
}
static void k_CreateEventA(rc_ctx *c) {
  w32_ret(c, create_event(c, w32_arg(c, 1) != 0, w32_arg(c, 2) != 0, name_a(w32_arg(c, 3))), 4);
}
void shim_CreateEventW(rc_ctx *c) {  // also the audio layer's test entry
  char n[260];
  w32_ret(c, create_event(c, w32_arg(c, 1) != 0, w32_arg(c, 2) != 0, name_w(w32_arg(c, 3), n, sizeof n)), 4);
}
static void k_OpenEventA(rc_ctx *c) {
  uint32_t h = open_named(c, name_a(w32_arg(c, 2)), H_EVENT);
  if (!h) w32_set_error(c, W32_ERROR_FILE_NOT_FOUND);
  else w32_set_error(c, 0);
  w32_ret(c, h, 3);
}
static void set_event(rc_ctx *c, int nargs, int sig) {
  pthread_mutex_lock(&w32_hlock);
  w32_obj *o = w32_handle_get(w32_arg(c, 0), H_EVENT);
  if (o) {
    o->u.ev.signaled = sig;
    w32_hwake_obj(o);
  }
  pthread_mutex_unlock(&w32_hlock);
  if (!o) w32_set_error(c, W32_ERROR_INVALID_HANDLE);
  w32_ret(c, o != 0, nargs);
}
static void k_SetEvent(rc_ctx *c) { set_event(c, 1, 1); }
static void k_ResetEvent(rc_ctx *c) { set_event(c, 1, 0); }

// --- mutexes and semaphores --------------------------------------------------------------------
static uint32_t me(rc_ctx *c) { return *w32_teb(c, RC32_TEB_TID); }
static uint32_t create_mutex(rc_ctx *c, int own, const char *name) {
  w32_obj *o;
  uint32_t h = new_named(c, H_MUTEX, name, &o);
  if (o && own) {
    o->u.mu.owner = me(c);
    o->u.mu.count = 1;
  }
  return h;
}
static void k_CreateMutexA(rc_ctx *c) { w32_ret(c, create_mutex(c, w32_arg(c, 1) != 0, name_a(w32_arg(c, 2))), 3); }
static void k_CreateMutexW(rc_ctx *c) {
  char n[260];
  w32_ret(c, create_mutex(c, w32_arg(c, 1) != 0, name_w(w32_arg(c, 2), n, sizeof n)), 3);
}
static void k_ReleaseMutex(rc_ctx *c) {
  int ok = 0;
  pthread_mutex_lock(&w32_hlock);
  w32_obj *o = w32_handle_get(w32_arg(c, 0), H_MUTEX);
  if (o && o->u.mu.owner == me(c) && o->u.mu.count > 0) {
    ok = 1;
    if (--o->u.mu.count == 0) {
      o->u.mu.owner = 0;
      w32_hwake_obj(o);
    }
  }
  pthread_mutex_unlock(&w32_hlock);
  if (!ok) w32_set_error(c, 288);  // ERROR_NOT_OWNER
  w32_ret(c, ok, 1);
}
static uint32_t create_sem(rc_ctx *c, int32_t init, int32_t max, const char *name) {
  if (max <= 0 || init < 0 || init > max) {
    w32_set_error(c, W32_ERROR_INVALID_PARAMETER);
    return 0;
  }
  w32_obj *o;
  uint32_t h = new_named(c, H_SEM, name, &o);
  if (o) {
    o->u.sem.count = init;
    o->u.sem.max = max;
  }
  return h;
}
static void k_CreateSemaphoreA(rc_ctx *c) {
  w32_ret(c, create_sem(c, (int32_t)w32_arg(c, 1), (int32_t)w32_arg(c, 2), name_a(w32_arg(c, 3))), 4);
}
static void k_CreateSemaphoreW(rc_ctx *c) {
  char n[260];
  w32_ret(c, create_sem(c, (int32_t)w32_arg(c, 1), (int32_t)w32_arg(c, 2), name_w(w32_arg(c, 3), n, sizeof n)), 4);
}
static void k_ReleaseSemaphore(rc_ctx *c) {
  int32_t n = (int32_t)w32_arg(c, 1);
  uint32_t prev = w32_arg(c, 2);
  int ok = 0;
  pthread_mutex_lock(&w32_hlock);
  w32_obj *o = w32_handle_get(w32_arg(c, 0), H_SEM);
  if (o && n > 0 && o->u.sem.count + n <= o->u.sem.max) {
    if (prev) w32_wr32(prev, (uint32_t)o->u.sem.count);
    o->u.sem.count += n;
    ok = 1;
    w32_hwake_obj(o);
  }
  pthread_mutex_unlock(&w32_hlock);
  if (!ok) w32_set_error(c, o ? 298 : W32_ERROR_INVALID_HANDLE);  // ERROR_TOO_MANY_POSTS
  w32_ret(c, ok, 3);
}

// --- waitable timers ---------------------------------------------------------------------------
static uint64_t now_ns(void) { return clock_gettime_nsec_np(CLOCK_MONOTONIC); }
static void k_CreateWaitableTimerA(rc_ctx *c) {
  w32_obj *o;
  uint32_t h = new_named(c, H_TIMER, name_a(w32_arg(c, 2)), &o);
  if (o) o->u.tm.manual = w32_arg(c, 1) != 0;
  w32_ret(c, h, 3);
}
static void k_SetWaitableTimer(rc_ctx *c) {
  pthread_mutex_lock(&w32_hlock);
  w32_obj *o = w32_handle_get(w32_arg(c, 0), H_TIMER);
  if (o) {
    int64_t due;
    memcpy(&due, RC32_H(w32_arg(c, 1)), 8);
    uint64_t t = now_ns();
    if (due < 0) {
      t += (uint64_t)(-due) * 100;  // relative, in 100 ns
    } else {  // absolute FILETIME (UTC)
      struct timespec r;
      clock_gettime(CLOCK_REALTIME, &r);
      int64_t nowft = ((int64_t)r.tv_sec + 11644473600ll) * 10000000ll + r.tv_nsec / 100;
      if (due > nowft) t += (uint64_t)(due - nowft) * 100;
    }
    o->u.tm.due_ns = t;
    o->u.tm.period_ms = w32_arg(c, 2);
    o->u.tm.armed = 1;
    o->u.tm.signaled = 0;
    w32_hwake_obj(o);
  }
  pthread_mutex_unlock(&w32_hlock);
  w32_ret(c, o != 0, 6);
}
static void k_CancelWaitableTimer(rc_ctx *c) {
  pthread_mutex_lock(&w32_hlock);
  w32_obj *o = w32_handle_get(w32_arg(c, 0), H_TIMER);
  if (o) o->u.tm.armed = 0;
  pthread_mutex_unlock(&w32_hlock);
  w32_ret(c, o != 0, 1);
}

// --- waits -------------------------------------------------------------------------------------
// under w32_hlock: is o signaled for thread `tid`; with take, apply the wait's side effect
static int ready(w32_obj *o, uint32_t tid, int take, uint64_t now) {
  switch (o->type) {
  case H_EVENT:
    if (!o->u.ev.signaled) return 0;
    if (take && !o->u.ev.manual) o->u.ev.signaled = 0;
    return 1;
  case H_MUTEX:
    if (o->u.mu.owner && o->u.mu.owner != tid) return 0;
    if (take) {
      o->u.mu.owner = tid;
      o->u.mu.count++;
    }
    return 1;
  case H_SEM:
    if (o->u.sem.count <= 0) return 0;
    if (take) o->u.sem.count--;
    return 1;
  case H_TIMER:
    if (o->u.tm.armed && now >= o->u.tm.due_ns) {
      o->u.tm.signaled = 1;
      if (o->u.tm.period_ms) o->u.tm.due_ns += (uint64_t)o->u.tm.period_ms * 1000000;
      else o->u.tm.armed = 0;
    }
    if (!o->u.tm.signaled) return 0;
    if (take && !o->u.tm.manual) o->u.tm.signaled = 0;
    return 1;
  case H_THREAD:
    return o->u.th.done;
  case H_FILE:  // a file handle is signaled (its I/O is synchronous)
    return 1;
  }
  return 0;
}

// --- LP_DUMP_WAITS: every N seconds, what each guest thread is waiting for -------------------------
#define NWAITREC 256
static struct {
  uint32_t tid, n, all, ms, caller;
  uint32_t hs[8];
  uint64_t since, count;
} waitrec[NWAITREC];  // under w32_hlock; tid 0 = free
static int dump_waits = -1;
static __thread uint32_t w32_wait_caller;  // set by the guest-facing waits: ret_to of the import call

static const char *type_name(int t) {
  switch (t) {
  case H_EVENT: return "event";
  case H_MUTEX: return "mutex";
  case H_SEM: return "sem";
  case H_TIMER: return "timer";
  case H_THREAD: return "thread";
  case H_FILE: return "file";
  }
  return "other";
}
// under w32_hlock
static void dump_obj(uint32_t h) {
  w32_obj *o = h >= 0x1000u && !(h & 3) && (h - 0x1000u) / 4 < MAXH ? slot_obj[(h - 0x1000u) / 4] : 0;
  if (!o) { fprintf(stderr, " 0x%x(?)", h); return; }
  fprintf(stderr, " 0x%x(%s", h, type_name(o->type));
  if (o->name) fprintf(stderr, " \"%s\"", o->name);
  switch (o->type) {
  case H_EVENT: fprintf(stderr, " %s %s", o->u.ev.manual ? "manual" : "auto", o->u.ev.signaled ? "set" : "unset"); break;
  case H_SEM: fprintf(stderr, " %d/%d", o->u.sem.count, o->u.sem.max); break;
  case H_MUTEX: fprintf(stderr, " owner %x x%d", o->u.mu.owner, o->u.mu.count); break;
  case H_THREAD: fprintf(stderr, " tid %x%s", o->u.th.tid, o->u.th.done ? " done" : ""); break;
  case H_TIMER: fprintf(stderr, "%s%s", o->u.tm.armed ? " armed" : "", o->u.tm.signaled ? " set" : ""); break;
  }
  fprintf(stderr, ")");
}
static uint64_t now_ns(void);
static void *wait_dumper(void *arg) {
  (void)arg;
  for (;;) {
    sleep((unsigned)dump_waits);
    pthread_mutex_lock(&w32_hlock);
    uint64_t now = now_ns();
    fprintf(stderr, "w32: waits:\n");
    for (int k = 0; k < NWAITREC; k++) {
      if (!waitrec[k].tid) continue;
      fprintf(stderr, "w32: waits   tid %x from 0x%x for %.1f s (%llu waits) %s ms %d:", waitrec[k].tid, waitrec[k].caller,
              (now - waitrec[k].since) / 1e9, (unsigned long long)waitrec[k].count, waitrec[k].all ? "all" : "any",
              (int)waitrec[k].ms);
      for (uint32_t j = 0; j < waitrec[k].n && j < 8; j++) dump_obj(waitrec[k].hs[j]);
      fprintf(stderr, "\n");
    }
    pthread_mutex_unlock(&w32_hlock);
  }
  return 0;
}
// under w32_hlock: the record of thread tid (made when absent)
static int waitrec_of(uint32_t tid) {
  int fr = -1;
  for (int k = 0; k < NWAITREC; k++) {
    if (waitrec[k].tid == tid) return k;
    if (fr < 0 && !waitrec[k].tid) fr = k;
  }
  return fr;
}

#define WAIT_TIMEOUT 0x102u
#define WAIT_FAILED 0xffffffffu
#define HOST_TID 0xffffffffu  // the owner of what a host thread acquires (w32_wait_multiple)
// under w32_hlock: the end of a wait if its objects allow it now (taking them), else WAIT_TIMEOUT
static uint32_t try_wait(w32_obj *const *o, int n, int all, uint32_t tid, uint64_t now) {
  int k = 0;
  if (all) {
    while (k < n && o[k] && ready(o[k], tid, 0, now)) k++;
    if (k < n) return WAIT_TIMEOUT;
    for (k = 0; k < n; k++) ready(o[k], tid, 1, now);
    return 0;
  }
  while (k < n && !(o[k] && ready(o[k], tid, 1, now))) k++;
  return k < n ? (uint32_t)k : WAIT_TIMEOUT;
}

// the wait of thread tid; *err: the error of a WAIT_FAILED
static uint32_t wait_tid(uint32_t tid, int n, const uint32_t *hs, int all, uint32_t ms, uint32_t *err) {
  w32_obj *o[64];
  if (n <= 0 || n > 64) {
    *err = W32_ERROR_INVALID_PARAMETER;
    return WAIT_FAILED;
  }
  uint64_t t0 = now_ns(), end = ms == 0xffffffffu ? UINT64_MAX : t0 + (uint64_t)ms * 1000000;
  uint32_t r = WAIT_TIMEOUT;
  pthread_mutex_lock(&w32_hlock);
  for (int k = 0; k < n; k++) {
    o[k] = w32_handle_get(hs[k], -1);
    if (!o[k] && hs[k] != 0xffffffffu && hs[k] != 0xfffffffeu) {
      pthread_mutex_unlock(&w32_hlock);
      *err = W32_ERROR_INVALID_HANDLE;
      return WAIT_FAILED;
    }
  }
  // the wait's own references: as on Windows, a CloseHandle of the last handle on another thread
  // leaves the object until the wait ends (which then ends only by its timeout, or never)
  for (int k = 0; k < n; k++)
    if (o[k]) o[k]->refs++;
  int wr = dump_waits > 0 && tid != HOST_TID ? waitrec_of(tid) : -1;
  if (wr >= 0) {
    int same = waitrec[wr].tid == tid && waitrec[wr].n == (uint32_t)n && waitrec[wr].caller == w32_wait_caller;
    for (int k = 0; same && k < n && k < 8; k++) same = waitrec[wr].hs[k] == hs[k];
    if (!same) {
      waitrec[wr].since = t0;
      waitrec[wr].count = 0;
    }
    waitrec[wr].tid = tid;
    waitrec[wr].n = (uint32_t)n;
    waitrec[wr].all = (uint32_t)all;
    waitrec[wr].ms = ms;
    waitrec[wr].caller = w32_wait_caller;
    for (int k = 0; k < n && k < 8; k++) waitrec[wr].hs[k] = hs[k];
    waitrec[wr].count++;
  }
  waiter *w = &self_waiter;
  w->o = o;
  w->n = n;
  w->all = all;
  w->tid = tid;
  w->r = WAIT_TIMEOUT;
  w->prev = 0;
  w->next = waiters;
  if (waiters) waiters->prev = w;
  waiters = w;
  for (;;) {
    uint64_t now = now_ns(), wake = end;
    r = w->r != WAIT_TIMEOUT ? w->r : try_wait(o, n, all, tid, now);
    if (r != WAIT_TIMEOUT || now >= end) break;
    for (int k = 0; k < n; k++)
      if (o[k] && o[k]->type == H_TIMER && o[k]->u.tm.armed && o[k]->u.tm.due_ns < wake) wake = o[k]->u.tm.due_ns;
    if (wake == UINT64_MAX) {
      pthread_cond_wait(&w->cv, &w32_hlock);
    } else {
      struct timespec ts;
      clock_gettime(CLOCK_REALTIME, &ts);
      uint64_t d = wake > now ? wake - now : 0, ns = (uint64_t)ts.tv_nsec + d % 1000000000;
      ts.tv_sec += (time_t)(d / 1000000000 + ns / 1000000000);
      ts.tv_nsec = (long)(ns % 1000000000);
      pthread_cond_timedwait(&w->cv, &w32_hlock, &ts);
    }
    w32_wait_wakeups++;
  }
  if (w->prev) w->prev->next = w->next;
  else waiters = w->next;
  if (w->next) w->next->prev = w->prev;
  if (wr >= 0 && r != WAIT_TIMEOUT) waitrec[wr].tid = 0;  // satisfied: no longer waiting
  for (int k = 0; k < n; k++)
    if (o[k] && --o[k]->refs == 0) obj_free(o[k]);
  pthread_mutex_unlock(&w32_hlock);
  return r;
}
static uint32_t wait_objs(rc_ctx *c, int n, const uint32_t *hs, int all, uint32_t ms) {
  if (dump_waits < 0) {
    const char *e = getenv("LP_DUMP_WAITS");
    int v = e ? atoi(e) : 0;
    static pthread_mutex_t once = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_lock(&once);
    if (dump_waits < 0) {
      if (v > 0) {
        pthread_t th;
        if (!pthread_create(&th, 0, wait_dumper, 0)) pthread_detach(th);
      }
      dump_waits = v;
    }
    pthread_mutex_unlock(&once);
  }
  w32_wait_caller = c->ret_to;
  uint32_t err = 0, r = wait_tid(me(c), n, hs, all, ms, &err);
  if (r == WAIT_FAILED) w32_set_error(c, err);
  return r;
}

// --- events and waits from host threads (win32_shims.h) -----------------------------------------
static int host_set(uint32_t h, int sig) {
  pthread_mutex_lock(&w32_hlock);
  w32_obj *o = w32_handle_get(h, H_EVENT);
  if (o) {
    o->u.ev.signaled = sig;
    if (sig) w32_hwake_obj(o);
  }
  pthread_mutex_unlock(&w32_hlock);
  return o != 0;
}
int w32_event_signal(uint32_t h) { return host_set(h, 1); }
int w32_event_reset(uint32_t h) { return host_set(h, 0); }
// PulseEvent: the waits on the event at this moment end as SetEvent would end them, every one for a
// manual-reset event and the oldest that can for an auto-reset one, and it is left reset. A
// waiter would find it reset again when it runs, so the wait is ended here, in its name, and
// reads how from its waiter. Waits that start later see nothing of the pulse.
int w32_event_pulse(uint32_t h) {
  pthread_mutex_lock(&w32_hlock);
  w32_obj *o = w32_handle_get(h, H_EVENT);
  if (o) {
    uint64_t now = now_ns();
    waiter *w = waiters;
    while (w && w->next) w = w->next;  // new waits go to the head
    o->u.ev.signaled = 1;
    for (; w && o->u.ev.signaled; w = w->prev) {
      int hit = 0;
      for (int k = 0; !hit && k < w->n; k++) hit = w->o[k] == o;
      if (hit && w->r == WAIT_TIMEOUT) w->r = try_wait(w->o, w->n, w->all, w->tid, now);
    }
    w32_hwake_obj(o);
    o->u.ev.signaled = 0;
  }
  pthread_mutex_unlock(&w32_hlock);
  return o != 0;
}
static void k_PulseEvent(rc_ctx *c) {
  int ok = w32_event_pulse(w32_arg(c, 0));
  if (!ok) w32_set_error(c, W32_ERROR_INVALID_HANDLE);
  w32_ret(c, ok, 1);
}
uint32_t w32_wait_multiple(uint32_t n, const uint32_t *hs, int all, uint32_t ms) {
  uint32_t err;
  return wait_tid(HOST_TID, (int)n, hs, all, ms, &err);
}

static void k_WaitForSingleObject(rc_ctx *c) {
  uint32_t h = w32_arg(c, 0);
  w32_ret(c, wait_objs(c, 1, &h, 0, w32_arg(c, 1)), 2);
}
static void k_WaitForSingleObjectEx(rc_ctx *c) {
  uint32_t h = w32_arg(c, 0);
  w32_ret(c, wait_objs(c, 1, &h, 0, w32_arg(c, 1)), 3);
}
static void wait_multi(rc_ctx *c, int nargs) {
  uint32_t n = w32_arg(c, 0), hs[64];
  if (n > 64) n = 0;
  for (uint32_t k = 0; k < n; k++) hs[k] = w32_rd32(w32_arg(c, 1) + 4 * k);
  w32_ret(c, wait_objs(c, (int)n, hs, w32_arg(c, 2) != 0, w32_arg(c, 3)), nargs);
}
static void k_WaitForMultipleObjects(rc_ctx *c) { wait_multi(c, 4); }
static void k_WaitForMultipleObjectsEx(rc_ctx *c) { wait_multi(c, 5); }
static void k_SignalObjectAndWait(rc_ctx *c) {
  uint32_t s = w32_arg(c, 0), h = w32_arg(c, 1), ms = w32_arg(c, 2);
  pthread_mutex_lock(&w32_hlock);
  w32_obj *o = w32_handle_get(s, -1);
  if (o) {
    if (o->type == H_EVENT) o->u.ev.signaled = 1;
    else if (o->type == H_SEM && o->u.sem.count < o->u.sem.max) o->u.sem.count++;
    else if (o->type == H_MUTEX && o->u.mu.owner == me(c) && --o->u.mu.count == 0) o->u.mu.owner = 0;
    w32_hwake_obj(o);
  }
  pthread_mutex_unlock(&w32_hlock);
  w32_ret(c, o ? wait_objs(c, 1, &h, 0, ms) : WAIT_FAILED, 4);
}

static void sleep_ms(uint32_t ms) {
  if (!ms) {
    sched_yield();
    return;
  }
  struct timespec t = {(time_t)(ms / 1000), (long)(ms % 1000) * 1000000};
  while (nanosleep(&t, &t) && errno == EINTR) {}
}
static void k_Sleep(rc_ctx *c) { sleep_ms(w32_arg(c, 0)); w32_ret(c, 0, 1); }
static void k_SleepEx(rc_ctx *c) { sleep_ms(w32_arg(c, 0)); w32_ret(c, 0, 2); }
static void k_SwitchToThread(rc_ctx *c) { sched_yield(); w32_ret(c, 1, 0); }

// --- threads -----------------------------------------------------------------------------------
typedef struct {
  w32_obj *o;
  uint32_t start, param, stack;
} thread_arg;
static __thread jmp_buf *exit_jb;   // ExitThread's way out of the guest frames
static __thread w32_obj *self_obj;  // this thread's object (threads made by CreateThread)
static qos_class_t qos_of(int prio);
static int qos_on(void);
#define HOST_STACK (64u << 20)      // translated code nests deeper on the host stack than on x86

static void *thread_main(void *p) {
  thread_arg a = *(thread_arg *)p;
  free(p);
  {
    char name[32];  // the guest's start address: for sample, Instruments and LP_THREAD_CPU
    snprintf(name, sizeof name, "w32 thread %x", a.start);
    pthread_setname_np(name);
  }
  rc32_thread_stack(a.stack);
  rc_ctx *c = rc32_thread_ctx();
  pthread_mutex_lock(&w32_hlock);
  a.o->u.th.tid = me(c);
  self_obj = a.o;
  pthread_cond_broadcast(&w32_hwake);
  while (a.o->u.th.suspended) pthread_cond_wait(&w32_hwake, &w32_hlock);
  int prio = a.o->u.th.prio, prio_set = a.o->u.th.prio_set;
  pthread_mutex_unlock(&w32_hlock);
  if (prio_set && qos_on()) pthread_set_qos_class_self_np(qos_of(prio), 0);
  jmp_buf jb;
  uint32_t code;
  exit_jb = &jb;
  if (!setjmp(jb)) {
    uint32_t arg = a.param;
    code = rc32_call(c, a.start, &arg, 1, 0, 0, 0);
  } else {
    code = a.o->u.th.code;
  }
  pthread_mutex_lock(&w32_hlock);
  a.o->u.th.code = code;
  a.o->u.th.done = 1;
  w32_hwake_obj(a.o);
  pthread_cond_broadcast(&w32_hwake);
  if (--a.o->refs == 0) obj_free(a.o);
  pthread_mutex_unlock(&w32_hlock);
  rc32_thread_end();  // its stack, TEB and TLS blocks: the next thread's
  return 0;
}

static void k_CreateThread(rc_ctx *c) {
  uint32_t stack = w32_arg(c, 1), start = w32_arg(c, 2), param = w32_arg(c, 3), flags = w32_arg(c, 4), idp = w32_arg(c, 5);
  w32_obj *o;
  uint32_t h = w32_handle_new(H_THREAD, &o);
  thread_arg *a = malloc(sizeof *a);
  if (!h || !a) {
    free(a);
    if (h) {
      o->u.th.done = 1;  // no thread: the object goes with its handle
      w32_handle_close(h);
    }
    w32_ret(c, 0, 6);
    return;
  }
  o->refs++;  // the thread's own reference
  w32_threads_made++;
  o->u.th.suspended = (flags & 4) != 0;  // CREATE_SUSPENDED
  *a = (thread_arg){o, start, param, stack};
  pthread_attr_t at;
  pthread_attr_init(&at);
  pthread_attr_setstacksize(&at, HOST_STACK);
  if (qos_on()) pthread_attr_set_qos_class_np(&at, QOS_CLASS_USER_INITIATED, 0);  // THREAD_PRIORITY_NORMAL
  int rc = pthread_create(&o->u.th.pt, &at, thread_main, a);
  pthread_attr_destroy(&at);
  if (rc) {
    fprintf(stderr, "w32: CreateThread: pthread_create failed (%d)\n", rc);
    free(a);
    o->refs--;           // the thread's reference: there is no thread
    o->u.th.done = 1;    // so the object is freed with its handle
    w32_handle_close(h);
    w32_ret(c, 0, 6);
    return;
  }
  pthread_detach(o->u.th.pt);
  pthread_mutex_lock(&w32_hlock);
  while (!o->u.th.tid) pthread_cond_wait(&w32_hwake, &w32_hlock);
  pthread_mutex_unlock(&w32_hlock);
  if (w32_log) fprintf(stderr, "w32: CreateThread(0x%x, 0x%x) -> tid 0x%x%s\n", start, param, o->u.th.tid, flags & 4 ? " (suspended)" : "");
  if (idp) w32_wr32(idp, o->u.th.tid);
  w32_ret(c, h, 6);
}

// a real handle on the calling thread (DuplicateHandle of GetCurrentThread())
static uint32_t thread_self_handle(rc_ctx *c) {
  if (self_obj) {
    pthread_mutex_lock(&w32_hlock);
    uint32_t h = slot_for(self_obj);
    pthread_mutex_unlock(&w32_hlock);
    return h;
  }
  w32_obj *o;  // the main thread, or one the host made: never ends while the game runs
  uint32_t h = w32_handle_new(H_THREAD, &o);
  if (h) {
    o->u.th.tid = me(c);
    self_obj = o;
    o->refs++;
  }
  return h;
}

static void k_ExitThread(rc_ctx *c) {
  uint32_t code = w32_arg(c, 0);
  if (!exit_jb) {
    fprintf(stderr, "w32: ExitThread(%u) on a thread CreateThread did not make\n", code);
    rc_fail("ExitThread", code);
  }
  self_obj->u.th.code = code;
  longjmp(*exit_jb, 1);
}
static void k_GetExitCodeThread(rc_ctx *c) {
  pthread_mutex_lock(&w32_hlock);
  w32_obj *o = w32_handle_get(w32_arg(c, 0), H_THREAD);
  if (o && w32_arg(c, 1)) w32_wr32(w32_arg(c, 1), o->u.th.done ? o->u.th.code : 259);  // STILL_ACTIVE
  pthread_mutex_unlock(&w32_hlock);
  w32_ret(c, o != 0, 2);
}
static void k_ResumeThread(rc_ctx *c) {
  uint32_t prev = 0xffffffffu;
  pthread_mutex_lock(&w32_hlock);
  w32_obj *o = w32_handle_get(w32_arg(c, 0), H_THREAD);
  if (o) {
    prev = (uint32_t)o->u.th.suspended;
    if (o->u.th.suspended > 0) o->u.th.suspended--;
    pthread_cond_broadcast(&w32_hwake);
  }
  pthread_mutex_unlock(&w32_hlock);
  w32_ret(c, prev, 1);
}
static void k_SuspendThread(rc_ctx *c) {
  // a running thread cannot be stopped from outside here; the game only suspends threads to
  // sample them (profilers, crash dumps)
  static int warned;
  if (!warned++) fprintf(stderr, "w32: SuspendThread not supported (ignored)\n");
  w32_ret(c, 0, 1);
}
static void k_TerminateThread(rc_ctx *c) {
  fprintf(stderr, "w32: TerminateThread(0x%x) not supported (ignored)\n", w32_arg(c, 0));
  w32_ret(c, 0, 2);
}
static void k_GetCurrentThread(rc_ctx *c) { w32_ret(c, 0xfffffffeu, 0); }
static void k_GetCurrentThreadId(rc_ctx *c) { w32_ret(c, me(c), 0); }
// Thread priorities become QoS classes, which macOS schedules by: TIME_CRITICAL / HIGHEST
// user-interactive, the others user-initiated. Lower classes are not used: utility and background
// threads are throttled hard, and a loader the game runs below normal then holds the game up for
// seconds. A thread can only set its own QoS, so a priority set on another thread is applied
// when that thread starts (CREATE_SUSPENDED, SetThreadPriority, ResumeThread) or else not at all.
static int qos_on(void) {
  static int on = -1;
  if (on < 0) {
    const char *e = getenv("LP_QOS");
    on = !e || *e != '0';
  }
  return on;
}
static qos_class_t qos_of(int prio) {
  return prio >= 2 ? QOS_CLASS_USER_INTERACTIVE : QOS_CLASS_USER_INITIATED;
}
static void k_GetThreadPriority(rc_ctx *c) {
  pthread_mutex_lock(&w32_hlock);
  w32_obj *o = w32_handle_get(w32_arg(c, 0), H_THREAD);
  if (!o && w32_arg(c, 0) == 0xfffffffeu) o = self_obj;
  uint32_t r = o && o->u.th.prio_set ? (uint32_t)o->u.th.prio : 0;
  pthread_mutex_unlock(&w32_hlock);
  w32_ret(c, r, 1);
}
static void k_SetThreadPriority(rc_ctx *c) {
  uint32_t h = w32_arg(c, 0);
  int prio = (int)w32_arg(c, 1);
  pthread_mutex_lock(&w32_hlock);
  w32_obj *o = h == 0xfffffffeu ? self_obj : w32_handle_get(h, H_THREAD);
  if (o) {
    o->u.th.prio = prio;
    o->u.th.prio_set = 1;
  }
  pthread_mutex_unlock(&w32_hlock);
  // the game's own thread (not made by CreateThread) keeps the QoS lp_main gives it
  if (o && o == self_obj && qos_on()) pthread_set_qos_class_self_np(qos_of(prio), 0);
  w32_ret(c, 1, 2);
}
static void k_SetThreadPriorityBoost(rc_ctx *c) { w32_ret(c, 1, 2); }
static void k_SetThreadAffinityMask(rc_ctx *c) { w32_ret(c, 0xff, 2); }
static void k_SetThreadIdealProcessor(rc_ctx *c) { w32_ret(c, 0, 2); }
static void k_GetThreadTimes(rc_ctx *c) {  // creation, exit, kernel, user (FILETIMEs): user time only
  struct timespec t;
  clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t);
  for (int k = 1; k < 5; k++) if (w32_arg(c, k)) w32_wr64(w32_arg(c, k), 0);
  if (w32_arg(c, 4)) w32_wr64(w32_arg(c, 4), (uint64_t)t.tv_sec * 10000000ull + (uint64_t)t.tv_nsec / 100);
  w32_ret(c, 1, 5);
}

// --- critical sections -------------------------------------------------------------------------
// CRITICAL_SECTION (24 bytes): word 0 (DebugInfo) holds the index of a host recursive mutex;
// OwningThread (word 3) and RecursionCount (word 2) are kept for code that reads them. A deleted
// section's mutex goes on a free list for the next InitializeCriticalSection (objects with a
// section of their own come and go all the time).
#define CS_CHUNK 4096
#define CS_CHUNKS 256
static pthread_mutex_t *cs_chunks[CS_CHUNKS];
static uint32_t cs_n, *cs_free, cs_nfree, cs_capfree;
static pthread_mutex_t cs_mu = PTHREAD_MUTEX_INITIALIZER;

// under cs_mu: a mutex index for cs, written to its word 0
static void cs_init_locked(uint32_t cs) {
  uint32_t k;
  if (cs_nfree) {
    k = cs_free[--cs_nfree];  // already initialised (and unlocked, unless the game deleted it held)
  } else {
    if (cs_n == CS_CHUNK * CS_CHUNKS) rc_fail("w32: too many critical sections", cs);
    k = cs_n++;
    if (!cs_chunks[k / CS_CHUNK]) cs_chunks[k / CS_CHUNK] = calloc(CS_CHUNK, sizeof(pthread_mutex_t));
    if (!cs_chunks[k / CS_CHUNK]) rc_fail("w32: no memory for critical sections", cs);
    pthread_mutex_t *m = &cs_chunks[k / CS_CHUNK][k % CS_CHUNK];
    pthread_mutexattr_t a;
    pthread_mutexattr_init(&a);
    pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(m, &a);
    pthread_mutexattr_destroy(&a);
  }
  memset(RC32_H(cs), 0, 24);
  w32_wr32(cs + 4, 0xffffffffu);  // LockCount: -1, free
  __atomic_store_n((uint32_t *)RC32_H(cs), k + 1, __ATOMIC_RELEASE);
}
static void cs_init(uint32_t cs) {
  pthread_mutex_lock(&cs_mu);
  cs_init_locked(cs);
  pthread_mutex_unlock(&cs_mu);
}
static pthread_mutex_t *cs_mutex(uint32_t cs) {
  uint32_t k = __atomic_load_n((uint32_t *)RC32_H(cs), __ATOMIC_ACQUIRE);
  if (!k || k > __atomic_load_n(&cs_n, __ATOMIC_RELAXED)) {  // used without InitializeCriticalSection: zero-filled statics
    pthread_mutex_lock(&cs_mu);
    k = w32_rd32(cs);
    if (!k || k > cs_n) {  // still not: this thread initialises it (another may have meanwhile)
      fprintf(stderr, "w32: critical section 0x%x used before InitializeCriticalSection\n", cs);
      cs_init_locked(cs);
      k = w32_rd32(cs);
    }
    pthread_mutex_unlock(&cs_mu);
  }
  k--;
  return &cs_chunks[k / CS_CHUNK][k % CS_CHUNK];
}
static void cs_delete(uint32_t cs) {
  pthread_mutex_lock(&cs_mu);
  uint32_t k = w32_rd32(cs);
  // only a mutex nobody holds is handed out again (a section deleted while held keeps its own)
  pthread_mutex_t *m = k && k <= cs_n ? &cs_chunks[(k - 1) / CS_CHUNK][(k - 1) % CS_CHUNK] : 0;
  if (m && !w32_rd32(cs + 8) && !pthread_mutex_trylock(m)) {
    pthread_mutex_unlock(m);
    if (cs_nfree == cs_capfree) {
      cs_capfree = cs_capfree ? 2 * cs_capfree : 1024;
      cs_free = realloc(cs_free, cs_capfree * sizeof *cs_free);
    }
    cs_free[cs_nfree++] = k - 1;
    w32_wr32(cs, 0);
  }
  pthread_mutex_unlock(&cs_mu);
}
static void k_InitializeCriticalSection(rc_ctx *c) { cs_init(w32_arg(c, 0)); w32_ret(c, 0, 1); }
static void k_InitializeCriticalSectionAndSpinCount(rc_ctx *c) { cs_init(w32_arg(c, 0)); w32_ret(c, 1, 2); }
static void k_InitializeCriticalSectionEx(rc_ctx *c) { cs_init(w32_arg(c, 0)); w32_ret(c, 1, 3); }
static void k_SetCriticalSectionSpinCount(rc_ctx *c) { w32_ret(c, 0, 2); }
static void k_DeleteCriticalSection(rc_ctx *c) { cs_delete(w32_arg(c, 0)); w32_ret(c, 0, 1); }
void w32_cs_enter(rc_ctx *c, uint32_t cs) {
  pthread_mutex_lock(cs_mutex(cs));
  w32_wr32(cs + 8, w32_rd32(cs + 8) + 1);
  w32_wr32(cs + 12, me(c));
}
void w32_cs_leave(uint32_t cs) {
  uint32_t n = w32_rd32(cs + 8);
  if (n) w32_wr32(cs + 8, n - 1);
  if (n <= 1) w32_wr32(cs + 12, 0);
  pthread_mutex_unlock(cs_mutex(cs));
}
static void k_EnterCriticalSection(rc_ctx *c) {
  w32_cs_enter(c, w32_arg(c, 0));
  w32_ret(c, 0, 1);
}
static void k_LeaveCriticalSection(rc_ctx *c) {
  w32_cs_leave(w32_arg(c, 0));
  w32_ret(c, 0, 1);
}
static void k_TryEnterCriticalSection(rc_ctx *c) {
  uint32_t cs = w32_arg(c, 0);
  int ok = pthread_mutex_trylock(cs_mutex(cs)) == 0;
  if (ok) {
    w32_wr32(cs + 8, w32_rd32(cs + 8) + 1);
    w32_wr32(cs + 12, me(c));
  }
  w32_ret(c, ok, 1);
}

// --- interlocked, SLists -----------------------------------------------------------------------
static uint32_t *g32p(uint32_t g) { return (uint32_t *)RC32_H(g); }
static void k_InterlockedIncrement(rc_ctx *c) { w32_ret(c, __atomic_add_fetch(g32p(w32_arg(c, 0)), 1, __ATOMIC_SEQ_CST), 1); }
static void k_InterlockedDecrement(rc_ctx *c) { w32_ret(c, __atomic_sub_fetch(g32p(w32_arg(c, 0)), 1, __ATOMIC_SEQ_CST), 1); }
static void k_InterlockedExchange(rc_ctx *c) { w32_ret(c, __atomic_exchange_n(g32p(w32_arg(c, 0)), w32_arg(c, 1), __ATOMIC_SEQ_CST), 2); }
static void k_InterlockedExchangeAdd(rc_ctx *c) { w32_ret(c, __atomic_fetch_add(g32p(w32_arg(c, 0)), w32_arg(c, 1), __ATOMIC_SEQ_CST), 2); }
static void k_InterlockedCompareExchange(rc_ctx *c) {
  uint32_t want = w32_arg(c, 2);
  __atomic_compare_exchange_n(g32p(w32_arg(c, 0)), &want, w32_arg(c, 1), 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
  w32_ret(c, want, 3);
}
// SLIST_HEADER: Next (4), Depth (2), Sequence (2); one lock for all lists is enough here
static pthread_mutex_t sl_mu = PTHREAD_MUTEX_INITIALIZER;
static void k_InitializeSListHead(rc_ctx *c) { w32_wr64(w32_arg(c, 0), 0); w32_ret(c, 0, 1); }
static void k_InterlockedPushEntrySList(rc_ctx *c) {
  uint32_t h = w32_arg(c, 0), e = w32_arg(c, 1);
  pthread_mutex_lock(&sl_mu);
  uint32_t old = w32_rd32(h), d = w32_rd32(h + 4);
  w32_wr32(e, old);
  w32_wr32(h, e);
  w32_wr32(h + 4, ((d & 0xffff) + 1) | (((d >> 16) + 1) << 16));
  pthread_mutex_unlock(&sl_mu);
  w32_ret(c, old, 2);
}
static void k_InterlockedPopEntrySList(rc_ctx *c) {
  uint32_t h = w32_arg(c, 0);
  pthread_mutex_lock(&sl_mu);
  uint32_t e = w32_rd32(h), d = w32_rd32(h + 4);
  if (e) {
    w32_wr32(h, w32_rd32(e));
    w32_wr32(h + 4, ((d & 0xffff) - 1) | (((d >> 16) + 1) << 16));
  }
  pthread_mutex_unlock(&sl_mu);
  w32_ret(c, e, 1);
}
static void k_InterlockedFlushSList(rc_ctx *c) {
  uint32_t h = w32_arg(c, 0);
  pthread_mutex_lock(&sl_mu);
  uint32_t e = w32_rd32(h);
  w32_wr32(h, 0);
  w32_wr32(h + 4, w32_rd32(h + 4) & 0xffff0000u);
  pthread_mutex_unlock(&sl_mu);
  w32_ret(c, e, 1);
}
static void k_QueryDepthSList(rc_ctx *c) { w32_ret(c, w32_rd32(w32_arg(c, 0) + 4) & 0xffff, 1); }

// --- TLS and FLS (FLS on the same slots: its callbacks are not run) -----------------------------
// A new index reads 0 in every thread (TlsAlloc's guarantee): the slot is cleared in each TEB that
// ever stored a value, so an index freed and handed out again carries no stale pointers.
static uint64_t tls_used;
static uint32_t *tls_tebs, tls_ntebs, tls_captebs;  // TEBs that called TlsSetValue (under tls_mu)
static pthread_mutex_t tls_mu = PTHREAD_MUTEX_INITIALIZER;
static __thread int tls_noted;
static uint32_t tls_alloc(void) {
  for (int k = 0; k < 64; k++)
    if (!(__atomic_fetch_or(&tls_used, 1ull << k, __ATOMIC_SEQ_CST) & (1ull << k))) {
      pthread_mutex_lock(&tls_mu);
      for (uint32_t t = 0; t < tls_ntebs; t++) w32_wr32(tls_tebs[t] + RC32_TEB_TLSSLOTS + 4 * (uint32_t)k, 0);
      pthread_mutex_unlock(&tls_mu);
      return (uint32_t)k;
    }
  return 0xffffffffu;
}
static void tls_note(rc_ctx *c) {
  if (tls_noted) return;
  tls_noted = 1;
  pthread_mutex_lock(&tls_mu);
  uint32_t t = 0;
  while (t < tls_ntebs && tls_tebs[t] != c->fs) t++;  // there already: an ended thread's TEB (rc32_thread_end)
  if (t == tls_ntebs) {
    if (tls_ntebs == tls_captebs) {
      tls_captebs = tls_captebs ? 2 * tls_captebs : 64;
      tls_tebs = realloc(tls_tebs, tls_captebs * sizeof *tls_tebs);
    }
    tls_tebs[tls_ntebs++] = c->fs;
  }
  pthread_mutex_unlock(&tls_mu);
}
static void k_TlsAlloc(rc_ctx *c) { w32_ret(c, tls_alloc(), 0); }
static void k_FlsAlloc(rc_ctx *c) { w32_ret(c, tls_alloc(), 1); }
static void tls_free(rc_ctx *c, int n) {
  uint32_t k = w32_arg(c, 0);
  if (k < 64) __atomic_fetch_and(&tls_used, ~(1ull << k), __ATOMIC_SEQ_CST);
  w32_ret(c, k < 64, n);
}
static void k_TlsFree(rc_ctx *c) { tls_free(c, 1); }
static void k_FlsFree(rc_ctx *c) { tls_free(c, 1); }
static void k_TlsGetValue(rc_ctx *c) {
  uint32_t k = w32_arg(c, 0);
  if (k < 64) w32_set_error(c, 0);
  w32_ret(c, k < 64 ? *w32_teb(c, RC32_TEB_TLSSLOTS + 4 * k) : 0, 1);
}
static void k_TlsSetValue(rc_ctx *c) {
  uint32_t k = w32_arg(c, 0);
  if (k < 64) {
    tls_note(c);
    *w32_teb(c, RC32_TEB_TLSSLOTS + 4 * k) = w32_arg(c, 1);
  }
  w32_ret(c, k < 64, 2);
}

#define E(n) {#n, k_##n}
static const w32_export fns[] = {
    E(GetStdHandle), E(SetStdHandle), E(CloseHandle), E(DuplicateHandle), E(CreateEventA), {"CreateEventW", shim_CreateEventW},
    E(OpenEventA), E(SetEvent), E(ResetEvent), E(PulseEvent), E(CreateMutexA), E(CreateMutexW),
    E(ReleaseMutex), E(CreateSemaphoreA), E(CreateSemaphoreW), E(ReleaseSemaphore),
    E(CreateWaitableTimerA), E(SetWaitableTimer), E(CancelWaitableTimer), E(WaitForSingleObject),
    E(WaitForSingleObjectEx), E(WaitForMultipleObjects), E(WaitForMultipleObjectsEx),
    E(SignalObjectAndWait), E(Sleep), E(SleepEx), E(SwitchToThread), E(CreateThread), E(ExitThread),
    E(GetExitCodeThread), E(ResumeThread), E(SuspendThread), E(TerminateThread), E(GetCurrentThread),
    E(GetCurrentThreadId), E(GetThreadPriority), E(SetThreadPriority), E(SetThreadPriorityBoost),
    E(SetThreadAffinityMask), E(SetThreadIdealProcessor), E(GetThreadTimes), E(InitializeCriticalSection),
    E(InitializeCriticalSectionAndSpinCount), E(InitializeCriticalSectionEx), E(SetCriticalSectionSpinCount),
    E(DeleteCriticalSection), E(EnterCriticalSection), E(LeaveCriticalSection), E(TryEnterCriticalSection),
    E(InterlockedIncrement), E(InterlockedDecrement), E(InterlockedExchange), E(InterlockedExchangeAdd),
    E(InterlockedCompareExchange), E(InitializeSListHead), E(InterlockedPushEntrySList),
    E(InterlockedPopEntrySList), E(InterlockedFlushSList), E(QueryDepthSList), E(TlsAlloc), E(TlsFree),
    E(TlsGetValue), E(TlsSetValue), E(FlsAlloc), E(FlsFree),
    {"FlsGetValue", k_TlsGetValue}, {"FlsSetValue", k_TlsSetValue},
};
W32_DLL(w32_dll_kernel32_sync, "kernel32.dll", fns, 0);
