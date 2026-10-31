// Override module `audio`, input-device trace (src/audio.h): why a device is missing from Direct Connect.
//
// The device scan (0x10133ccb0) skips some devices (see audio_devs.c) and opens the others in a free slot.
// SoundInput's open (0x10133bee0, a hook point: (this, entry) -> bool) takes `entry` of the device list and
// drops it on the first of a dozen checks: input stream configuration, buffer-size range, a 128-frame buffer,
// the switch to 48 kHz (polled up to 50 s), stream and physical format (linear PCM), the I/O proc and the
// start. Only devices it opens are listed under Direct Connect. With RS_AUDIO_LOG=1 this logs each device it
// tries and the result, and for a failed open the CoreAudio property calls it made (the import thunks send
// the game's AudioObjectGetPropertyData/GetPropertyDataSize/SetPropertyData calls here); an entry is logged
// again only when its device or result changes (the game scans the devices again on hot-plug). A device the
// scan skips is not logged.
#include <CoreAudio/CoreAudio.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "audio.h"
#include "rc_hook.h"

#define SI_STATE RC_OFF(si_state)  // u32: 2 = closed, to be opened
#define SI_DEV RC_OFF(si_dev)      // u8 selected, u32 AudioObjectID at +4 (0x10133ab30)
#define ENTRIES 64             // device list entries traced

static _Thread_local struct {
  int active;
  uint32_t slot;
  uint8_t *self;
  size_t len;
  char steps[1024];
} t_open;

static struct {
  uint32_t dev;
  int result;  // -1: not logged yet
} g_entry[ENTRIES] = {[0 ... ENTRIES - 1] = {0, -1}};

static void fourcc(uint32_t v, char out[5]) {
  for (int i = 0; i < 4; i++) {
    char ch = (char)(v >> (24 - 8 * i));
    out[i] = ch >= 32 && ch < 127 ? ch : '?';
  }
  out[4] = 0;
}

static void step(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void step(const char *fmt, ...) {
  if (t_open.len >= sizeof t_open.steps - 1) return;
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(t_open.steps + t_open.len, sizeof t_open.steps - t_open.len, fmt, ap);
  va_end(ap);
  if (n > 0) t_open.len += (size_t)n;
  if (t_open.len > sizeof t_open.steps - 1) t_open.len = sizeof t_open.steps - 1;
}

// One call of the game's, made inside an open: selector, scope, status and the value read or written.
static void trace(char op, uint32_t obj, const AudioObjectPropertyAddress *a, OSStatus st, uint32_t size,
                  const void *data) {
  if (!t_open.active || !a) return;
  char sel[5], sc[5];
  fourcc(a->mSelector, sel);
  fourcc(a->mScope, sc);
  step(" %c%s%s%s", op, sel, a->mScope == kAudioObjectPropertyScopeGlobal ? "" : "/",
       a->mScope == kAudioObjectPropertyScopeGlobal ? "" : sc);
  if (obj != kAudioObjectSystemObject) step("@%u", obj);
  if (st) {
    char s[5];
    fourcc((uint32_t)st, s);
    step("=err %d '%s'", (int)st, s);
    return;
  }
  if (!data || !size) return;
  if (a->mSelector == kAudioDevicePropertyStreamConfiguration && size >= 8) {
    const AudioBufferList *l = data;
    step("=%u buf:", l->mNumberBuffers);
    for (uint32_t i = 0; i < l->mNumberBuffers && 8 + 16 * (i + 1) <= size && i < 4; i++)
      step(" %uch", l->mBuffers[i].mNumberChannels);
  } else if (size == sizeof(AudioStreamBasicDescription) && a->mSelector != kAudioHardwarePropertyDevices) {
    const AudioStreamBasicDescription *f = data;
    char fm[5];
    fourcc(f->mFormatID, fm);
    step("=%.0f %s fl %#x %ub/f %uch %ubit", f->mSampleRate, fm, f->mFormatFlags, f->mBytesPerFrame,
         f->mChannelsPerFrame, f->mBitsPerChannel);
  } else if (size == sizeof(AudioValueRange)) {
    const AudioValueRange *r = data;
    step("=%.0f-%.0f", r->mMinimum, r->mMaximum);
  } else if (size == 8 && a->mSelector != kAudioHardwarePropertyDevices) {
    step("=%.0f", *(const double *)data);
  } else if (size == 4) {
    step("=%u", *(const uint32_t *)data);
  } else {
    step("=(%u bytes)", size);
  }
}

static void device_name(uint32_t dev, char *out, size_t n) {
  AudioObjectPropertyAddress a = {kAudioObjectPropertyName, kAudioObjectPropertyScopeGlobal,
                                  kAudioObjectPropertyElementMain};
  CFStringRef s = NULL;
  UInt32 size = sizeof s;
  out[0] = 0;
  if (AudioObjectGetPropertyData(dev, &a, 0, NULL, &size, &s) == noErr && s) {
    CFStringGetCString(s, out, (CFIndex)n, kCFStringEncodingUTF8);
    CFRelease(s);
  }
}

static int before_open(rc_ctx *c, void *user) {
  (void)user;
  OV_ARGS(a, c);
  uint8_t *self = OV_PTR(a, uint8_t);
  uint32_t slot = (uint32_t)OV_U64(a);
  t_open.active = self && slot < ENTRIES && *(uint32_t *)(self + SI_STATE) == 2;
  t_open.self = self;
  t_open.slot = slot;
  t_open.len = 0;
  t_open.steps[0] = 0;
  return 0;
}

static void after_open(rc_ctx *c, void *user) {
  (void)user;
  if (!t_open.active) return;
  t_open.active = 0;
  int ok = (uint8_t)c->r[0] != 0;
  uint8_t *self = t_open.self;
  uint32_t dev = self[SI_DEV] ? *(uint32_t *)(self + SI_DEV + 4) : 0;
  uint32_t slot = t_open.slot;
  if (g_entry[slot].dev == dev && g_entry[slot].result == ok) return;
  g_entry[slot].dev = dev;
  g_entry[slot].result = ok;
  char name[128];
  device_name(dev, name, sizeof name);
  if (ok)
    rs_audio_log("input device %u \"%s\" (list entry %u) opened, %u channel(s)", dev, name, slot,
                 *(uint32_t *)(self + RC_OFF(si_channels)));
  else
    rs_audio_log("input device %u \"%s\" (list entry %u) not opened; its calls:%s", dev, name, slot,
                 t_open.steps);
}

__attribute__((constructor)) static void trace_init(void) {
  if (!rs_audio_log_on()) return;
  if (rc_hook_add(RC_ADDR(si_open), before_open, after_open, NULL)) rs_audio_log("no SoundInput open hook point");
}

int32_t rs_audio_get_property_data_size(uint32_t obj, const void *addr, uint32_t qsize, const void *qdata,
                                        uint32_t *size) {
  OSStatus st = AudioObjectGetPropertyDataSize(obj, addr, qsize, qdata, size);
  if (t_open.active) trace('#', obj, addr, st, size ? *size : 0, NULL);
  return st;
}

int32_t rs_audio_set_property_data(uint32_t obj, const void *addr, uint32_t qsize, const void *qdata,
                                   uint32_t size, const void *data) {
  OSStatus st = AudioObjectSetPropertyData(obj, addr, qsize, qdata, size, data);
  if (t_open.active) trace('s', obj, addr, st, size, data);
  return st;
}

void rs_audio_trace_get(uint32_t obj, const void *addr, int32_t st, uint32_t size, const void *data) {
  if (t_open.active) trace('g', obj, addr, st, size, data);
}
