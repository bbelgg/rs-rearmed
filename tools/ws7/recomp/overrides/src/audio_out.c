// Override module `audio`, output side (src/audio.h): the output device's I/O buffer and the Wwise
// LEngine thread.
//
// rs_audio_output_device: the Wwise sink (setup 0x100aa6a10) sets its AUHAL's current device
// (AudioUnitSetProperty 2000 at 0x100aa6c3f) but never the device's buffer size, so the HAL callback
// runs at the system default (512 frames, 10.7 ms). The AudioUnitSetProperty import thunk
// (thunks/gen_thunks.py) calls this first; it sets kAudioDevicePropertyBufferFrameSize (a per-process
// setting: other apps keep theirs) to RS_AUDIO_FRAMES (default 256; 0 leaves the device alone). The size
// is kept within the device's range and at most half the sink's ring (LatencyBuffer x 256 frames): the
// HAL callback can take no more than the ring holds, and with no block to spare (a 256-frame ring and a
// 256-frame buffer) the game lists no audio interface under Direct Connect. So LatencyBuffer=1 gets a
// 128-frame buffer (the ring's count, audio_ring.c: at 1 the engine itself runs with 2). The sink is rebuilt
// on a device change, which calls this again.
//
// F_100b049b0, the LEngine thread entry (the thread Wwise renders on, 256 frames per block, woken by the
// HAL callback through a mach semaphore): the original is
//   g_lengine_thread = pthread_self(); g_start_time = time(0);
//   do { render(g_engine); semaphore_wait(this->sem); } while (!this->stop); return 0;
// The native version does the same, and first makes the thread a time-constraint (real-time) thread and
// keeps it in the current output device's audio workgroup (kAudioDevicePropertyIOThreadOSWorkgroup), so
// the scheduler treats a render as part of the device's I/O cycle. With a small ring there is little slack:
// a block must be ready within one HAL period of the wake-up.
// RS_AUDIO_RT=0 runs the thread as the original does.
#include <CoreAudio/CoreAudio.h>
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <mach/semaphore.h>
#include <mach/thread_policy.h>
#include <os/workgroup.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "audio.h"

#define LENGINE_THREAD 0x101908418ull  // pthread_t of the LEngine thread (written by the entry)
#define LENGINE_START 0x101907c10ull   // u32 time(0) at thread start
#define AK_RENDER_LOOP 0x100ab7200ull  // renders the blocks the sink needs: (engine)
#define AK_ENGINE_PTR 0x101907bb0ull   // its argument
#define SAMPLE_RATE 48000u             // the sink forces 48 kHz ('nsrt', 0x100aa6a10)
#define DEFAULT_FRAMES 256u
#define MIN_FRAMES 32u

_Atomic uint32_t rs_audio_out_frames;

// The output device as last set by the sink; the LEngine thread joins its workgroup when gen changes.
static _Atomic uint32_t g_out_dev;
static _Atomic uint32_t g_out_gen;

static int g_log = -1;

int rs_audio_env_on(const char *name, int dflt) {
  const char *v = getenv(name);
  if (!v || !*v) return dflt;
  return !(v[0] == '0' || v[0] == 'n' || v[0] == 'N' || v[0] == 'f' || v[0] == 'F');
}

int rs_audio_log_on(void) {
  if (g_log < 0) g_log = rs_audio_env_on("RS_AUDIO_LOG", 0);
  return g_log;
}

void rs_audio_log(const char *fmt, ...) {
  if (!rs_audio_log_on()) return;
  va_list ap;
  va_start(ap, fmt);
  fputs("[rs-audio] ", stderr);
  vfprintf(stderr, fmt, ap);
  fputc('\n', stderr);
  va_end(ap);
}

// ------------------------------------------------------------------------------------------------
// Output device buffer
// ------------------------------------------------------------------------------------------------
static int dev_get(AudioObjectID dev, AudioObjectPropertySelector sel, void *out, UInt32 size) {
  AudioObjectPropertyAddress a = {sel, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
  UInt32 sz = size;
  return AudioObjectGetPropertyData(dev, &a, 0, NULL, &sz, out) == noErr && sz == size;
}

static void dev_name(AudioObjectID dev, char *buf, size_t n) {
  CFStringRef s = NULL;
  snprintf(buf, n, "?");
  if (dev_get(dev, kAudioObjectPropertyName, &s, sizeof s) && s) {
    CFStringGetCString(s, buf, (CFIndex)n, kCFStringEncodingUTF8);
    CFRelease(s);
  }
}

// RS_AUDIO_FRAMES: unset or empty = dflt, 0 = leave the device alone
static uint32_t frames_wanted(uint32_t dflt) {
  const char *v = getenv("RS_AUDIO_FRAMES");
  if (!v || !*v) return dflt;
  char *end;
  unsigned long n = strtoul(v, &end, 10);
  if (end == v || n > 8192) return dflt;
  return (uint32_t)n;
}

void rs_audio_output_device(uint32_t dev) {
  if (dev == kAudioObjectUnknown) return;
  uint32_t refills = rs_audio_ring_refills();
  uint32_t ring = refills * AK_REFILL_FRAMES, cap = ring / 2;
  uint32_t want = frames_wanted(DEFAULT_FRAMES < cap ? DEFAULT_FRAMES : cap), n = want;
  char name[128];
  dev_name(dev, name, sizeof name);
  AudioValueRange r = {0, 0};
  int have_range = dev_get(dev, kAudioDevicePropertyBufferFrameSizeRange, &r, sizeof r);
  if (want) {
    if (n > cap) {
      rs_audio_log("warning: RS_AUDIO_FRAMES %u is more than half the ring (%u x %u = %u frames); using %u",
                   want, refills, AK_REFILL_FRAMES, ring, cap);
      n = cap;
    }
    if (have_range) {
      if (n < (uint32_t)r.mMinimum) n = (uint32_t)r.mMinimum;
      if (n > (uint32_t)r.mMaximum) n = (uint32_t)r.mMaximum;
    }
    if (n < MIN_FRAMES) n = MIN_FRAMES;
    AudioObjectPropertyAddress a = {kAudioDevicePropertyBufferFrameSize, kAudioObjectPropertyScopeGlobal,
                                    kAudioObjectPropertyElementMain};
    UInt32 v = n;
    OSStatus st = AudioObjectSetPropertyData(dev, &a, 0, NULL, sizeof v, &v);
    if (st != noErr) rs_audio_log("output device %u: setting the buffer to %u frames failed (%d)", dev, n, (int)st);
  }
  UInt32 applied = 0;
  dev_get(dev, kAudioDevicePropertyBufferFrameSize, &applied, sizeof applied);
  atomic_store(&rs_audio_out_frames, applied);
  rs_audio_log("output device %u \"%s\": buffer %u frames (requested %u%s, range %.0f-%.0f), LatencyBuffer %u = ring %u frames",
               dev, name, (unsigned)applied, want, want ? "" : " = keep", r.mMinimum, r.mMaximum, refills, ring);
  if (applied > ring)
    rs_audio_log("warning: the device buffer (%u) is larger than the ring (%u): expect dropouts; raise LatencyBuffer",
                 (unsigned)applied, ring);
  atomic_store(&g_out_dev, dev);
  atomic_fetch_add(&g_out_gen, 1);
}

// ------------------------------------------------------------------------------------------------
// LEngine thread
// ------------------------------------------------------------------------------------------------
static uint64_t ns_to_abs(uint64_t ns) {
  static mach_timebase_info_data_t tb;
  if (!tb.denom) mach_timebase_info(&tb);
  return ns * tb.denom / tb.numer;
}

// Time-constraint policy: one block period (256 frames, or the device buffer if larger: a callback then
// asks for several blocks at once), up to 30% of it computing, done within the period.
static void make_realtime(void) {
  uint32_t frames = atomic_load(&rs_audio_out_frames);
  if (frames < AK_REFILL_FRAMES) frames = AK_REFILL_FRAMES;
  uint64_t period_ns = (uint64_t)frames * 1000000000ull / SAMPLE_RATE;
  thread_time_constraint_policy_data_t p;
  p.period = (uint32_t)ns_to_abs(period_ns);
  p.computation = (uint32_t)ns_to_abs(period_ns * 3 / 10);
  p.constraint = (uint32_t)ns_to_abs(period_ns);
  p.preemptible = 1;
  kern_return_t kr = thread_policy_set(pthread_mach_thread_np(pthread_self()), THREAD_TIME_CONSTRAINT_POLICY,
                                       (thread_policy_t)&p, THREAD_TIME_CONSTRAINT_POLICY_COUNT);
  if (kr == KERN_SUCCESS)
    rs_audio_log("Wwise thread: real-time, period %.2f ms, computation %.2f ms", period_ns / 1e6, period_ns * 0.3 / 1e6);
  else
    rs_audio_log("Wwise thread: time-constraint policy failed (%d)", kr);
}

typedef struct {
  os_workgroup_t wg;
  os_workgroup_join_token_s token;
  int joined;
} workgroup_state;

static void wg_leave(workgroup_state *s) {
  if (s->joined) os_workgroup_leave(s->wg, &s->token);
  if (s->wg) os_release(s->wg);
  s->wg = NULL;
  s->joined = 0;
}

static void wg_join(workgroup_state *s, uint32_t dev) {
  wg_leave(s);
  if (dev == kAudioObjectUnknown) return;
  os_workgroup_t wg = NULL;
  if (!dev_get(dev, kAudioDevicePropertyIOThreadOSWorkgroup, &wg, sizeof wg) || !wg) {
    rs_audio_log("Wwise thread: output device %u has no I/O workgroup", dev);
    return;
  }
  s->wg = wg;
  int err = os_workgroup_join(wg, &s->token);
  s->joined = err == 0;
  if (err) rs_audio_log("Wwise thread: joining output device %u's workgroup failed (%d)", dev, err);
  else rs_audio_log("Wwise thread: joined output device %u's workgroup", dev);
}

void F_100b049b0(rc_ctx *c) {
  OV_ARGS(a, c);
  uint8_t *self = OV_PTR(a, uint8_t);  // +0 mach semaphore, +4 stop flag
  RC_ST(uint64_t, RC_A(LENGINE_THREAD), (uint64_t)(uintptr_t)pthread_self());
  RC_ST(uint32_t, RC_A(LENGINE_START), (uint32_t)time(NULL));
  int rt = rs_audio_env_on("RS_AUDIO_RT", 1);
  if (rt) make_realtime();
  workgroup_state wg = {0};
  uint32_t gen = 0;
  do {
    if (rt) {
      uint32_t g = atomic_load_explicit(&g_out_gen, memory_order_acquire);
      if (g != gen) {
        gen = g;
        make_realtime();  // the period and budget follow the new device's buffer
        wg_join(&wg, atomic_load(&g_out_dev));
      }
    }
    OV_GUEST(AK_RENDER_LOOP, RC_LD(uint64_t, RC_A(AK_ENGINE_PTR)));
    semaphore_wait(RC_LD(uint32_t, self));
  } while (!RC_LD(uint8_t, self + 4));
  wg_leave(&wg);
  ov_ret_u32(c, 0);
}
