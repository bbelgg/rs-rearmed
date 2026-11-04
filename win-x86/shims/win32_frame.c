// LP_FRAME_LOG=MS: every frame (Present to Present) longer than MS milliseconds is logged with
// what happened in it (win32_shims.h, lp_frame_*): per kind of event, how many and how long, on
// the presenting thread ("main") and on all threads. Off (one load and a branch per event) when
// the variable is not set.
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/resource.h>
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <unistd.h>

#include "win32_shims.h"

int lp_frame_on = -1;
// the runtime's (rc32_image.c); weak here for tests that link the seam without the runtime
__attribute__((weak)) void (*rc32_note)(int what, uint64_t ticks);
static double threshold_ms;
static pthread_t main_thread;
static int have_main;
static _Atomic uint64_t cnt[LPF_KINDS][2], ns[LPF_KINDS][2], big[LPF_KINDS];  // [kind][main?]
static uint64_t last_present, frames, slow, last_cpu;
static struct rusage last_ru;
// the slowest single calls of the frame (COM methods, imports), by name
#define TOP 4
static struct { const char *name; uint64_t ns; int main; } top[TOP];
static pthread_mutex_t top_mu = PTHREAD_MUTEX_INITIALIZER;

void lp_frame_call(const char *name, uint64_t t_ns) {
  if (!lp_frame_on || t_ns < 500000) return;
  int m = have_main && pthread_equal(pthread_self(), main_thread);
  // other threads' sleeps and waits are their idle time, not the frame's
  if (!m && (strstr(name, "Sleep") || strstr(name, "WaitFor") || strstr(name, "GetQueuedCompletion"))) return;
  pthread_mutex_lock(&top_mu);
  int k = 0;
  for (int i = 1; i < TOP; i++)
    if (top[i].ns < top[k].ns) k = i;
  if (t_ns > top[k].ns) {
    top[k].name = name;
    top[k].ns = t_ns;
    top[k].main = m;
  }
  pthread_mutex_unlock(&top_mu);
}

static const char *const kind_name[LPF_KINDS] = {
    [LPF_PROGRAM] = "program link",   [LPF_SHADER] = "shader create",   [LPF_TEXTURE] = "texture create",
    [LPF_UPLOAD] = "texture upload",  [LPF_BUFFER] = "buffer create",   [LPF_FILE_READ] = "file read",
    [LPF_FILE_OPEN] = "file open",    [LPF_WAIT] = "wait",              [LPF_SLEEP] = "sleep",
    [LPF_LOCK] = "lock contended",    [LPF_IMPORT] = "first import call", [LPF_LOOKUP_MISS] = "icall lookup miss",
    [LPF_RAISE] = "guest exception",  [LPF_PRESENT] = "present",        [LPF_DRAW] = "draw",
    [LPF_ALLOC] = "large alloc",
};

uint64_t lp_frame_now(void) { return clock_gettime_nsec_np(CLOCK_UPTIME_RAW); }

static double tick_ns;
static void note(int what, uint64_t ticks) {
  lp_frame_event(what == RC32_NOTE_RAISE ? LPF_RAISE : LPF_LOOKUP_MISS, (uint64_t)(ticks * tick_ns), 0);
}

// LP_THREAD_CPU=S: every S seconds, each thread's CPU over the interval (user + system, % of one
// core) with its name: the guest's threads are "w32 game" and "w32 thread <start address>"
// (win32_ksync.c), the others carry the system's names (CoreAudio, Metal). Threads under 0.5%
// are left out. A second line says how much of the guest's memory has gone: the arena (it never
// shrinks), VirtualAlloc's and VirtualFree's calls, the threads made, the handles open.
#define TCPU_MAX 256
extern unsigned long w32_valloc_calls, w32_valloc_bytes, w32_vfree_calls, w32_threads_made, d3d9_programs_made;
unsigned w32_handles_open(void);
static void *thread_cpu(void *arg) {
  unsigned secs = (unsigned)(uintptr_t)arg;
  pthread_setname_np("lp thread-cpu");
  static struct { uint64_t id, ns; } last[TCPU_MAX], now[TCPU_MAX];
  int nlast = 0;
  uint64_t t_last = lp_frame_now();
  for (;;) {
    sleep(secs);
    thread_act_array_t th;
    mach_msg_type_number_t n;
    if (task_threads(mach_task_self(), &th, &n) != KERN_SUCCESS) return 0;
    uint64_t t = lp_frame_now();
    double dt = (double)(t - t_last);
    char line[4096];
    int o = snprintf(line, sizeof line, "lp: thread cpu (%.0f s):", dt / 1e9);
    double total = 0;
    int nnow = 0;
    for (mach_msg_type_number_t k = 0; k < n; k++) {
      thread_extended_info_data_t x;
      mach_msg_type_number_t cnt = THREAD_EXTENDED_INFO_COUNT;
      thread_identifier_info_data_t idi;
      mach_msg_type_number_t icnt = THREAD_IDENTIFIER_INFO_COUNT;
      if (thread_info(th[k], THREAD_EXTENDED_INFO, (thread_info_t)&x, &cnt) == KERN_SUCCESS &&
          thread_info(th[k], THREAD_IDENTIFIER_INFO, (thread_info_t)&idi, &icnt) == KERN_SUCCESS && nnow < TCPU_MAX) {
        uint64_t ns = x.pth_user_time + x.pth_system_time, prev = 0;
        int seen = 0;
        for (int j = 0; j < nlast; j++)
          if (last[j].id == idi.thread_id) {
            prev = last[j].ns;
            seen = 1;
            break;
          }
        now[nnow].id = idi.thread_id;
        now[nnow++].ns = ns;
        double pc = 100.0 * (double)(ns - (seen ? prev : 0)) / dt;
        total += pc;
        if (pc >= 0.5 && o < (int)sizeof line - 80)
          o += snprintf(line + o, sizeof line - (size_t)o, " [%s %.1f%%]", x.pth_name[0] ? x.pth_name : "?", pc);
      }
      mach_port_deallocate(mach_task_self(), th[k]);
    }
    vm_deallocate(mach_task_self(), (vm_address_t)th, n * sizeof *th);
    fprintf(stderr, "%s total %.1f%%\n", line, total);
    fprintf(stderr, "lp: memory: arena %u MB, VirtualAlloc %lu (%lu MB), VirtualFree %lu, %lu threads made, %u handles open, %lu GL programs\n",
            rc32_arena_used() >> 20, w32_valloc_calls, w32_valloc_bytes >> 20, w32_vfree_calls, w32_threads_made,
            w32_handles_open(), d3d9_programs_made);
    memcpy(last, now, sizeof now);
    nlast = nnow;
    t_last = t;
  }
  return 0;
}

static void init(void) {
  const char *tc = getenv("LP_THREAD_CPU");
  if (tc && atoi(tc) > 0) {
    static int started;
    if (!started++) {
      pthread_t th;
      if (!pthread_create(&th, 0, thread_cpu, (void *)(uintptr_t)atoi(tc))) pthread_detach(th);
    }
  }
  const char *e = getenv("LP_FRAME_LOG");
  threshold_ms = e ? atof(e) : 0;
  mach_timebase_info_data_t tb;
  mach_timebase_info(&tb);
  tick_ns = (double)tb.numer / tb.denom;
  if (threshold_ms > 0) {
    rc32_note = note;
    w32_call_timed = lp_frame_call;  // COM methods and other host functions (win32_guestobj.c)
  }
  lp_frame_on = threshold_ms > 0;
}

void lp_frame_event(int kind, uint64_t t_ns, uint64_t amount) {
  if (lp_frame_on < 0) init();
  if (!lp_frame_on) return;
  int m = have_main && pthread_equal(pthread_self(), main_thread);
  cnt[kind][m] += amount ? amount : 1;
  ns[kind][m] += t_ns;
  if (t_ns > big[kind]) big[kind] = t_ns;  // racy max: good enough for a log
}

void lp_frame_present(void) {
  if (lp_frame_on < 0) init();
  if (!lp_frame_on) return;
  uint64_t now = lp_frame_now();
  if (!have_main) {
    main_thread = pthread_self();
    have_main = 1;
  }
  frames++;
  uint64_t cpu = clock_gettime_nsec_np(CLOCK_THREAD_CPUTIME_ID);
  struct rusage ru;
  getrusage(RUSAGE_SELF, &ru);
  double ms = last_present ? (now - last_present) / 1e6 : 0;
  if (last_present && ms > threshold_ms) {
    slow++;
    char line[2048];
    int o = snprintf(line, sizeof line,
                     "lp: frame %llu: %.1f ms (%llu slow), main thread on CPU %.1f ms, process switches %ld/%ld "
                     "(vol/invol), page faults %ld:",
                     (unsigned long long)frames, ms, (unsigned long long)slow, (cpu - last_cpu) / 1e6,
                     ru.ru_nvcsw - last_ru.ru_nvcsw, ru.ru_nivcsw - last_ru.ru_nivcsw, ru.ru_majflt - last_ru.ru_majflt);
    for (int k = 0; k < LPF_KINDS; k++) {
      uint64_t c0 = cnt[k][0], c1 = cnt[k][1];
      if (!c0 && !c1) continue;
      o += snprintf(line + o, sizeof line - (size_t)o, " %s main %llu/%.1fms other %llu/%.1fms max %.1fms;",
                    kind_name[k], (unsigned long long)c1, ns[k][1] / 1e6, (unsigned long long)c0, ns[k][0] / 1e6,
                    big[k] / 1e6);
      if (o >= (int)sizeof line - 200) break;
    }
    pthread_mutex_lock(&top_mu);
    for (int k = 0; k < TOP && o < (int)sizeof line - 120; k++)
      if (top[k].ns)
        o += snprintf(line + o, sizeof line - (size_t)o, " [%s %.1fms%s]", top[k].name, top[k].ns / 1e6,
                      top[k].main ? "" : " other");
    pthread_mutex_unlock(&top_mu);
    fprintf(stderr, "%s\n", line);
  }
  pthread_mutex_lock(&top_mu);
  for (int k = 0; k < TOP; k++) top[k].ns = 0;
  pthread_mutex_unlock(&top_mu);
  for (int k = 0; k < LPF_KINDS; k++) {
    cnt[k][0] = cnt[k][1] = ns[k][0] = ns[k][1] = 0;
    big[k] = 0;
  }
  static uint64_t period_start, period_frames, period_slow;
  if (!period_start) period_start = now;
  period_frames++;
  if (last_present && ms > threshold_ms) period_slow++;
  if (now - period_start >= 10000000000ull) {  // every 10 s: the rate, and that frames still come
    fprintf(stderr, "lp: frames: %llu in %.1f s (%.1f fps), %llu over %.0f ms\n", (unsigned long long)period_frames,
            (now - period_start) / 1e9, period_frames * 1e9 / (now - period_start), (unsigned long long)period_slow,
            threshold_ms);
    period_start = now;
    period_frames = period_slow = 0;
  }
  last_present = now;
  last_cpu = cpu;
  last_ru = ru;
}
