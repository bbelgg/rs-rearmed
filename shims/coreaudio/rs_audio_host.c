// The game-independent half of the audio module (rs_audio_host.h): switches, log, device buffer,
// real-time thread, workgroup, stable UIDs and inputs first, for the Mac build's overrides
// (tools/ws7/recomp/overrides/src/audio_*.c) and Learn & Play's faces (win-x86/shims/audio).
#include "rs_audio_host.h"

#include <mach/mach.h>
#include <mach/mach_time.h>
#include <mach/thread_policy.h>
#include <os/lock.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
// Device properties
// ------------------------------------------------------------------------------------------------
int rs_audio_dev_get(AudioObjectID dev, AudioObjectPropertySelector sel, void *out, UInt32 size) {
  AudioObjectPropertyAddress a = {sel, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
  UInt32 sz = size;
  return AudioObjectGetPropertyData(dev, &a, 0, NULL, &sz, out) == noErr && sz == size;
}

void rs_audio_dev_name(AudioObjectID dev, char *buf, size_t n) {
  CFStringRef s = NULL;
  snprintf(buf, n, "?");
  if (rs_audio_dev_get(dev, kAudioObjectPropertyName, &s, sizeof s) && s) {
    CFStringGetCString(s, buf, (CFIndex)n, kCFStringEncodingUTF8);
    CFRelease(s);
  }
}

static int has_streams(AudioObjectID dev, AudioObjectPropertyScope scope) {
  AudioObjectPropertyAddress a = {kAudioDevicePropertyStreams, scope, kAudioObjectPropertyElementMain};
  UInt32 size = 0;
  return AudioObjectGetPropertyDataSize(dev, &a, 0, NULL, &size) == noErr && size > 0;
}
int rs_audio_has_input(AudioObjectID dev) { return has_streams(dev, kAudioObjectPropertyScopeInput); }
int rs_audio_has_output(AudioObjectID dev) { return has_streams(dev, kAudioObjectPropertyScopeOutput); }

int rs_audio_channels(AudioObjectID dev, AudioObjectPropertyScope scope) {
  AudioObjectPropertyAddress a = {kAudioDevicePropertyStreamConfiguration, scope, kAudioObjectPropertyElementMain};
  UInt32 sz = 0;
  if (AudioObjectGetPropertyDataSize(dev, &a, 0, NULL, &sz) != noErr || !sz) return 0;
  AudioBufferList *l = malloc(sz);
  int n = 0;
  if (l && AudioObjectGetPropertyData(dev, &a, 0, NULL, &sz, l) == noErr)
    for (UInt32 i = 0; i < l->mNumberBuffers; i++) n += (int)l->mBuffers[i].mNumberChannels;
  free(l);
  return n;
}

// ------------------------------------------------------------------------------------------------
// Output device buffer
// ------------------------------------------------------------------------------------------------
uint32_t rs_audio_frames_wanted(uint32_t dflt) {
  const char *v = getenv("RS_AUDIO_FRAMES");
  if (!v || !*v) return dflt;
  char *end;
  unsigned long n = strtoul(v, &end, 10);
  if (end == v || n > 8192) return dflt;
  return (uint32_t)n;
}

uint32_t rs_audio_set_device_buffer(AudioObjectID dev, uint32_t cap, const char *who) {
  if (dev == kAudioObjectUnknown) return 0;
  // The size is the device's, not a direction's, so an interface used for guitar and output has one,
  // and the last to set it wins. The Mac game's input sizes its queue slots at open from the 128 it
  // sets, and its IOProc queues one slot per I/O buffer: of a larger buffer the rest is lost, and the
  // guitar breaks up. The sink can come after the input (it is rebuilt on a device change).
  if (cap > RS_AUDIO_INPUT_FRAMES && rs_audio_has_input(dev)) cap = RS_AUDIO_INPUT_FRAMES;
  uint32_t want = rs_audio_frames_wanted(RS_AUDIO_DEFAULT_FRAMES < cap ? RS_AUDIO_DEFAULT_FRAMES : cap), n = want;
  char name[128];
  rs_audio_dev_name(dev, name, sizeof name);
  AudioValueRange r = {0, 0};
  int have_range = rs_audio_dev_get(dev, kAudioDevicePropertyBufferFrameSizeRange, &r, sizeof r);
  if (want) {
    if (n > cap) {
      rs_audio_log("warning: RS_AUDIO_FRAMES %u is more than %s can take per callback (%u); using %u", want, who,
                   cap, cap);
      n = cap;
    }
    if (have_range) {
      if (n < (uint32_t)r.mMinimum) n = (uint32_t)r.mMinimum;
      if (n > (uint32_t)r.mMaximum) n = (uint32_t)r.mMaximum;
    }
    if (n < RS_AUDIO_MIN_FRAMES) n = RS_AUDIO_MIN_FRAMES;
    AudioObjectPropertyAddress a = {kAudioDevicePropertyBufferFrameSize, kAudioObjectPropertyScopeGlobal,
                                    kAudioObjectPropertyElementMain};
    UInt32 v = n;
    OSStatus st = AudioObjectSetPropertyData(dev, &a, 0, NULL, sizeof v, &v);
    if (st != noErr) rs_audio_log("%s: device %u: setting the buffer to %u frames failed (%d)", who, dev, n, (int)st);
  }
  UInt32 applied = 0;
  rs_audio_dev_get(dev, kAudioDevicePropertyBufferFrameSize, &applied, sizeof applied);
  rs_audio_log("%s: device %u \"%s\": buffer %u frames (requested %u%s, range %.0f-%.0f, at most %u)", who, dev, name,
               (unsigned)applied, want, want ? "" : " = keep", r.mMinimum, r.mMaximum, cap);
  if (applied > cap)
    rs_audio_log("warning: %s: the device buffer (%u) is larger than %u: expect dropouts", who, (unsigned)applied, cap);
  return applied;
}

// ------------------------------------------------------------------------------------------------
// Real-time thread, workgroup
// ------------------------------------------------------------------------------------------------
static uint64_t ns_to_abs(uint64_t ns) {
  static mach_timebase_info_data_t tb;
  if (!tb.denom) mach_timebase_info(&tb);
  return ns * tb.denom / tb.numer;
}

void rs_audio_make_realtime(uint32_t frames, double rate, const char *who) {
  if (!frames || rate <= 0) return;
  uint64_t period_ns = (uint64_t)((double)frames * 1e9 / rate);
  thread_time_constraint_policy_data_t p;
  p.period = (uint32_t)ns_to_abs(period_ns);
  p.computation = (uint32_t)ns_to_abs(period_ns * 3 / 10);
  p.constraint = (uint32_t)ns_to_abs(period_ns);
  p.preemptible = 1;
  kern_return_t kr = thread_policy_set(pthread_mach_thread_np(pthread_self()), THREAD_TIME_CONSTRAINT_POLICY,
                                       (thread_policy_t)&p, THREAD_TIME_CONSTRAINT_POLICY_COUNT);
  if (kr == KERN_SUCCESS)
    rs_audio_log("%s: real-time, period %.2f ms, computation %.2f ms", who, period_ns / 1e6, period_ns * 0.3 / 1e6);
  else
    rs_audio_log("%s: time-constraint policy failed (%d)", who, kr);
}

void rs_audio_wg_leave(rs_audio_workgroup *s) {
  if (s->joined) os_workgroup_leave(s->wg, &s->token);
  if (s->wg) os_release(s->wg);
  s->wg = NULL;
  s->joined = 0;
}

void rs_audio_wg_join(rs_audio_workgroup *s, AudioObjectID dev, const char *who) {
  rs_audio_wg_leave(s);
  if (dev == kAudioObjectUnknown) return;
  os_workgroup_t wg = NULL;
  if (!rs_audio_dev_get(dev, kAudioDevicePropertyIOThreadOSWorkgroup, &wg, sizeof wg) || !wg) {
    rs_audio_log("%s: device %u has no I/O workgroup", who, dev);
    return;
  }
  s->wg = wg;
  int err = os_workgroup_join(wg, &s->token);
  s->joined = err == 0;
  if (err) rs_audio_log("%s: joining device %u's workgroup failed (%d)", who, dev, err);
  else rs_audio_log("%s: joined device %u's workgroup", who, dev);
}

// ------------------------------------------------------------------------------------------------
// Device list
// ------------------------------------------------------------------------------------------------
#define MAX_UIDS 64
#define MAX_DEVS 128

CFStringRef rs_audio_stable_uid(CFStringRef uid) {
  static os_unfair_lock lock = OS_UNFAIR_LOCK_INIT;
  static CFStringRef kept[MAX_UIDS];
  static int n;
  CFStringRef r = uid;
  os_unfair_lock_lock(&lock);
  int i = 0;
  while (i < n && !CFEqual(kept[i], uid)) i++;
  if (i < n) {
    r = (CFStringRef)CFRetain(kept[i]);
    CFRelease(uid);
  } else if (n < MAX_UIDS) {
    kept[n++] = (CFStringRef)CFRetain(uid);
  }
  os_unfair_lock_unlock(&lock);
  return r;
}

int rs_audio_inputs_first(AudioObjectID *devs, uint32_t n) {
  if (n < 2 || n > MAX_DEVS) return 0;
  AudioObjectID in[MAX_DEVS], out[MAX_DEVS];
  uint32_t ni = 0, no = 0;
  for (uint32_t i = 0; i < n; i++) {
    if (rs_audio_has_input(devs[i])) in[ni++] = devs[i];
    else out[no++] = devs[i];
  }
  int moved = memcmp(devs, in, ni * sizeof *in) != 0;
  memcpy(devs, in, ni * sizeof *in);
  memcpy(devs + ni, out, no * sizeof *out);
  return moved;
}
