// CoreAudio side of the Learn & Play audio faces (wa_engine.h).
#include "wa_engine.h"

#include <AudioToolbox/AudioToolbox.h>
#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "rs_audio_host.h"
#include "rs_audio_input.h"
#include "win32_shims.h"

#define MAX_DEVS 64
#define MAX_CH 8
#define MAX_DEV_CH 32  // a device's channels read, of which the game gets at most MAX_CH
#define WA_RATE 48000u  // the rate the game opens every stream at (sink and guitar input)

// ------------------------------------------------------------------------------------------------
// Formats
// ------------------------------------------------------------------------------------------------
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

int wa_fmt_parse(const void *wfx, wa_fmt *f) {
  const uint8_t *p = wfx;
  uint16_t tag = rd16(p);
  memset(f, 0, sizeof *f);
  f->channels = rd16(p + 2);
  f->rate = rd32(p + 4);
  f->block = rd16(p + 12);
  f->bits = rd16(p + 14);
  f->valid_bits = f->bits;
  if (tag == 0xfffe && rd16(p + 16) >= 22) {
    f->valid_bits = rd16(p + 18);
    f->mask = rd32(p + 20);
    uint32_t sub = rd32(p + 24);  // KSDATAFORMAT_SUBTYPE_PCM / _IEEE_FLOAT: {0000000x-0000-0010-8000-00aa00389b71}
    static const uint8_t tail[12] = {0, 0, 0x10, 0, 0x80, 0, 0, 0xaa, 0, 0x38, 0x9b, 0x71};
    if (memcmp(p + 28, tail, 12) != 0) return 0;
    tag = (uint16_t)sub;
  }
  if (!f->valid_bits || f->valid_bits > f->bits) f->valid_bits = f->bits;
  if (tag == 3) f->is_float = 1;
  else if (tag != 1) return 0;
  if (f->is_float ? f->bits != 32 : (f->bits != 8 && f->bits != 16 && f->bits != 24 && f->bits != 32)) return 0;
  if (!f->channels || f->channels > MAX_CH || f->rate < 8000 || f->rate > 192000) return 0;
  if (f->block != f->channels * f->bits / 8) return 0;
  return 1;
}

static void wr16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void wr32(uint8_t *p, uint32_t v) { wr16(p, (uint16_t)v); wr16(p + 2, (uint16_t)(v >> 16)); }

static uint32_t default_mask(uint16_t ch) {
  static const uint32_t m[9] = {0, 0x4, 0x3, 0x7, 0x33, 0x37, 0x3f, 0x13f, 0x63f};
  return ch <= 8 ? m[ch] : 0;
}

void wa_fmt_write(const wa_fmt *f, void *wfx) {
  uint8_t *p = wfx;
  memset(p, 0, 40);
  wr16(p, 0xfffe);
  wr16(p + 2, f->channels);
  wr32(p + 4, f->rate);
  wr32(p + 8, f->rate * f->block);
  wr16(p + 12, f->block);
  wr16(p + 14, f->bits);
  wr16(p + 16, 22);
  wr16(p + 18, f->valid_bits);
  wr32(p + 20, f->mask ? f->mask : default_mask(f->channels));
  static const uint8_t tail[12] = {0, 0, 0x10, 0, 0x80, 0, 0, 0xaa, 0, 0x38, 0x9b, 0x71};
  wr32(p + 24, f->is_float ? 3 : 1);
  memcpy(p + 28, tail, 12);
}

void wa_fmt_mix(uint16_t channels, wa_fmt *f) {
  memset(f, 0, sizeof *f);
  f->rate = WA_RATE;
  f->channels = channels ? channels : 2;
  f->bits = f->valid_bits = 32;
  f->is_float = 1;
  f->block = (uint16_t)(4 * f->channels);
  f->mask = default_mask(f->channels);
}

static float clampf(float v) { return v > 1.f ? 1.f : v < -1.f ? -1.f : v; }

void wa_to_float(const wa_fmt *f, const void *src, float *dst, uint32_t frames, float gain) {
  uint32_t n = frames * f->channels;
  const uint8_t *p = src;
  if (f->is_float) {
    for (uint32_t i = 0; i < n; i++) { float v; memcpy(&v, p + 4 * i, 4); dst[i] = v * gain; }
  } else if (f->bits == 16) {
    for (uint32_t i = 0; i < n; i++) dst[i] = (int16_t)rd16(p + 2 * i) * (gain / 32768.f);
  } else if (f->bits == 24) {
    for (uint32_t i = 0; i < n; i++) {
      int32_t v = (int32_t)((uint32_t)p[3 * i] << 8 | (uint32_t)p[3 * i + 1] << 16 | (uint32_t)p[3 * i + 2] << 24);
      dst[i] = (float)v * (gain / 2147483648.f);
    }
  } else if (f->bits == 32) {
    for (uint32_t i = 0; i < n; i++) dst[i] = (float)(int32_t)rd32(p + 4 * i) * (gain / 2147483648.f);
  } else {
    for (uint32_t i = 0; i < n; i++) dst[i] = ((int)p[i] - 128) * (gain / 128.f);
  }
}

void wa_from_float(const wa_fmt *f, const float *src, void *dst, uint32_t frames, float gain) {
  uint32_t n = frames * f->channels;
  uint8_t *p = dst;
  if (f->is_float) {
    for (uint32_t i = 0; i < n; i++) { float v = src[i] * gain; memcpy(p + 4 * i, &v, 4); }
  } else if (f->bits == 16) {
    for (uint32_t i = 0; i < n; i++) wr16(p + 2 * i, (uint16_t)(int16_t)lrintf(clampf(src[i] * gain) * 32767.f));
  } else if (f->bits == 24) {
    for (uint32_t i = 0; i < n; i++) {
      int32_t v = (int32_t)lrintf(clampf(src[i] * gain) * 8388607.f);
      p[3 * i] = (uint8_t)v; p[3 * i + 1] = (uint8_t)(v >> 8); p[3 * i + 2] = (uint8_t)(v >> 16);
    }
  } else if (f->bits == 32) {
    for (uint32_t i = 0; i < n; i++) {
      double v = (double)clampf(src[i] * gain) * 2147483647.0;
      wr32(p + 4 * i, (uint32_t)(int32_t)lrint(v));
    }
  } else {
    for (uint32_t i = 0; i < n; i++) p[i] = (uint8_t)(lrintf(clampf(src[i] * gain) * 127.f) + 128);
  }
}

// ------------------------------------------------------------------------------------------------
// Endpoints
// ------------------------------------------------------------------------------------------------
static void dev_string(AudioObjectID dev, AudioObjectPropertySelector sel, char *buf, size_t n) {
  CFStringRef s = NULL;
  buf[0] = 0;
  if (rs_audio_dev_get(dev, sel, &s, sizeof s) && s) {
    CFStringGetCString(s, buf, (CFIndex)n, kCFStringEncodingUTF8);
    CFRelease(s);
  }
}

// USB vendor and product: a USB audio device's model UID is "<product>:<VID>:<PID>" in hex
// (AppleUSBAudio). The Real Tone cable is 12BA:00FF; a cable whose model UID can't be read is
// still known by its name, the one Windows gives it.
static void usb_ids(AudioObjectID dev, const char *name, uint16_t *vid, uint16_t *pid) {
  *vid = *pid = 0;
  UInt32 transport = 0;
  rs_audio_dev_get(dev, kAudioDevicePropertyTransportType, &transport, sizeof transport);
  if (transport == kAudioDeviceTransportTypeUSB) {
    char model[192];
    dev_string(dev, kAudioDevicePropertyModelUID, model, sizeof model);
    char *b = strrchr(model, ':');
    if (b && b != model) {
      *b = 0;
      char *a = strrchr(model, ':');
      if (a) {
        char *e1, *e2;
        unsigned long v = strtoul(a + 1, &e1, 16), p = strtoul(b + 1, &e2, 16);
        if (*e1 == 0 && *e2 == 0 && e1 != a + 1 && e2 != b + 1 && v <= 0xffff && p <= 0xffff) {
          *vid = (uint16_t)v;
          *pid = (uint16_t)p;
        }
      }
    }
  }
  if (!*vid && strstr(name, "Rocksmith USB Guitar Adapter")) {
    *vid = 0x12ba;
    *pid = 0x00ff;
  }
}

// The name the game sees. Every input the game opens whose name contains "Rocksmith" but not
// "Rocksmith USB Guitar Adapter" sets the "Your Rocksmith Real Tone Cable is not configured for
// best performance" warning (DetectInputDevices_PortAudio, 0xc430c9; shown at 0x7c1e4b when the
// player picks the cable): on Windows that is a cable whose endpoint was renamed. On a Mac it is a
// virtual device such as Loopback's "Rocksmith Game (OBS)", which the Mac game, matching
// "Rocksmith USB" only, never warns about. So the cable always carries the Windows name, and any
// other device has "Rocksmith" spelled "RS" (RS_AUDIO_RS_NAMES=0 keeps the names as they are).
void wa_game_name(char *name, size_t n, uint16_t vid, uint16_t pid) {
  static const char cable[] = "Rocksmith USB Guitar Adapter";
  if (!rs_audio_env_on("RS_AUDIO_RS_NAMES", 1)) return;
  if (vid == 0x12ba && pid == 0x00ff) {
    if (!strstr(name, cable)) snprintf(name, n, "%s", cable);
    return;
  }
  char *p;
  while ((p = strstr(name, "Rocksmith")) != NULL) {
    p[0] = 'R';
    p[1] = 'S';
    memmove(p + 2, p + 9, strlen(p + 9) + 1);
  }
}

static int fill_endpoint(AudioObjectID dev, int flow, wa_endpoint *ep) {
  int ch = rs_audio_channels(dev, flow == WA_CAPTURE ? kAudioObjectPropertyScopeInput : kAudioObjectPropertyScopeOutput);
  if (ch <= 0) return 0;
  memset(ep, 0, sizeof *ep);
  ep->dev = dev;
  ep->flow = flow;
  ep->channels = (uint16_t)(ch > MAX_CH ? MAX_CH : ch);
  rs_audio_dev_name(dev, ep->name, sizeof ep->name);
  dev_string(dev, kAudioDevicePropertyDeviceUID, ep->uid, sizeof ep->uid);
  if (!ep->uid[0]) snprintf(ep->uid, sizeof ep->uid, "coreaudio-%u", dev);
  rs_audio_dev_get(dev, kAudioDevicePropertyNominalSampleRate, &ep->rate, sizeof ep->rate);
  usb_ids(dev, ep->name, &ep->vid, &ep->pid);
  wa_game_name(ep->name, sizeof ep->name, ep->vid, ep->pid);
  return 1;
}

static int device_list(AudioObjectID *devs, int max) {
  AudioObjectPropertyAddress a = {kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal,
                                  kAudioObjectPropertyElementMain};
  UInt32 sz = (UInt32)(max * sizeof *devs);
  if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &a, 0, NULL, &sz, devs) != noErr) return 0;
  return (int)(sz / sizeof *devs);
}

int wa_endpoints(int flow, wa_endpoint *out, int max) {
  AudioObjectID devs[MAX_DEVS];
  int n = device_list(devs, MAX_DEVS), k = 0;
  for (int pass = WA_CAPTURE; pass >= WA_RENDER; pass--) {
    if (flow != WA_ALL && flow != pass) continue;
    for (int i = 0; i < n && k < max; i++)
      if (fill_endpoint(devs[i], pass, &out[k])) k++;
  }
  return k;
}

int wa_default_endpoint(int flow, wa_endpoint *out) {
  AudioObjectPropertyAddress a = {flow == WA_CAPTURE ? kAudioHardwarePropertyDefaultInputDevice
                                                     : kAudioHardwarePropertyDefaultOutputDevice,
                                  kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
  AudioObjectID dev = kAudioObjectUnknown;
  UInt32 sz = sizeof dev;
  if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &a, 0, NULL, &sz, &dev) != noErr ||
      dev == kAudioObjectUnknown)
    return 0;
  return fill_endpoint(dev, flow == WA_CAPTURE ? WA_CAPTURE : WA_RENDER, out);
}

static void (*g_default_fn)(void);
static OSStatus default_changed(AudioObjectID obj, UInt32 n, const AudioObjectPropertyAddress *a, void *arg) {
  (void)obj; (void)n; (void)a; (void)arg;
  g_default_fn();
  return noErr;
}
void wa_watch_default_output(void (*fn)(void)) {
  if (g_default_fn) return;
  g_default_fn = fn;
  AudioObjectPropertyAddress a = {kAudioHardwarePropertyDefaultOutputDevice, kAudioObjectPropertyScopeGlobal,
                                  kAudioObjectPropertyElementMain};
  AudioObjectAddPropertyListener(kAudioObjectSystemObject, &a, default_changed, NULL);
}

void wa_endpoint_id(const wa_endpoint *ep, char *buf, size_t n) {
  snprintf(buf, n, "{0.0.%d.00000000}.{%s}", ep->flow == WA_CAPTURE ? 1 : 0, ep->uid);
}

int wa_endpoint_by_id(const char *id, wa_endpoint *out) {
  wa_endpoint eps[MAX_DEVS * 2];
  int n = wa_endpoints(WA_ALL, eps, MAX_DEVS * 2);
  char buf[256];
  for (int i = 0; i < n; i++) {
    wa_endpoint_id(&eps[i], buf, sizeof buf);
    if (!strcmp(buf, id)) {
      *out = eps[i];
      return 1;
    }
  }
  return 0;
}

// ------------------------------------------------------------------------------------------------
// Streams
// ------------------------------------------------------------------------------------------------
struct wa_stream {
  wa_endpoint ep;
  wa_fmt fmt;
  AudioUnit unit;
  int event_mode;
  uint32_t buffer_frames, period_frames, ring_frames, io_frames, latency_frames;
  double dev_rate;
  // ring: interleaved float, fmt.channels wide; w by the producer, r by the consumer
  float *ring;
  _Atomic uint64_t w, r;
  _Atomic uint32_t event;
  void (*notify)(void *);
  void *notify_arg;
  _Atomic int running, fed;
  _Atomic float gain;
  _Atomic uint64_t played;
  _Atomic uint32_t underruns, overruns;
  // capture: the device's channels at its rate, and the resampler's state
  uint16_t dev_ch;
  float *in_buf;
  uint32_t in_max;
  double step, phase;
  float last[MAX_DEV_CH];
  // the guitar input's settings (rs_audio_input.h): in_ch -1 = the game's channels as they are
  int in_ch;
  float in_gain;
  rs_audio_input_stats in_stats;
  char in_name[128];
  float *conv;  // guest-format conversions on the guest side (ring_frames x channels)
};

static void ring_read(wa_stream *s, uint64_t pos, float *dst, uint32_t frames) {
  uint32_t ch = s->fmt.channels, at = (uint32_t)(pos % s->ring_frames);
  uint32_t first = s->ring_frames - at < frames ? s->ring_frames - at : frames;
  memcpy(dst, s->ring + (size_t)at * ch, (size_t)first * ch * sizeof(float));
  if (first < frames) memcpy(dst + (size_t)first * ch, s->ring, (size_t)(frames - first) * ch * sizeof(float));
}

static void ring_write(wa_stream *s, uint64_t pos, const float *src, uint32_t frames) {
  uint32_t ch = s->fmt.channels, at = (uint32_t)(pos % s->ring_frames);
  uint32_t first = s->ring_frames - at < frames ? s->ring_frames - at : frames;
  if (src) {
    memcpy(s->ring + (size_t)at * ch, src, (size_t)first * ch * sizeof(float));
    if (first < frames) memcpy(s->ring, src + (size_t)first * ch, (size_t)(frames - first) * ch * sizeof(float));
  } else {
    memset(s->ring + (size_t)at * ch, 0, (size_t)first * ch * sizeof(float));
    if (first < frames) memset(s->ring, 0, (size_t)(frames - first) * ch * sizeof(float));
  }
}

static void signal_guest(wa_stream *s) {
  uint32_t h = atomic_load_explicit(&s->event, memory_order_relaxed);
  if (h) w32_event_signal(h);
  if (s->notify) s->notify(s->notify_arg);
}

static OSStatus render_cb(void *ref, AudioUnitRenderActionFlags *flags, const AudioTimeStamp *ts, UInt32 bus,
                          UInt32 frames, AudioBufferList *io) {
  (void)ts; (void)bus;
  wa_stream *s = ref;
  float *out = io->mBuffers[0].mData;
  uint64_t r = atomic_load_explicit(&s->r, memory_order_relaxed);
  uint64_t w = atomic_load_explicit(&s->w, memory_order_acquire);
  uint32_t avail = (uint32_t)(w - r), n = avail < frames ? avail : frames;
  if (n) ring_read(s, r, out, n);
  if (n < frames) {
    memset(out + (size_t)n * s->fmt.channels, 0, (size_t)(frames - n) * s->fmt.channels * sizeof(float));
    if (atomic_load_explicit(&s->fed, memory_order_relaxed))
      atomic_fetch_add_explicit(&s->underruns, 1, memory_order_relaxed);
    if (!n) *flags |= kAudioUnitRenderAction_OutputIsSilence;
  }
  // a CAS: the guest side moves r itself when it overfills the ring (wa_render_write)
  atomic_compare_exchange_strong_explicit(&s->r, &r, r + n, memory_order_release, memory_order_relaxed);
  atomic_fetch_add_explicit(&s->played, frames, memory_order_relaxed);
  if (s->ring_frames - (uint32_t)(w - r - n) >= s->period_frames) signal_guest(s);
  return noErr;
}

// Capture: the device's frames, its channels at its rate, to the stream's channels at the
// stream's rate (linear interpolation when the rates differ), into the ring.
static OSStatus input_cb(void *ref, AudioUnitRenderActionFlags *flags, const AudioTimeStamp *ts, UInt32 bus,
                         UInt32 frames, AudioBufferList *unused) {
  (void)unused;
  wa_stream *s = ref;
  if (frames > s->in_max) frames = s->in_max;
  AudioBufferList abl;
  abl.mNumberBuffers = 1;
  abl.mBuffers[0].mNumberChannels = s->dev_ch;
  abl.mBuffers[0].mDataByteSize = frames * s->dev_ch * sizeof(float);
  abl.mBuffers[0].mData = s->in_buf;
  if (AudioUnitRender(s->unit, flags, ts, bus, frames, &abl) != noErr) return noErr;
  uint32_t ch = s->fmt.channels, dch = s->dev_ch;
  uint64_t w = atomic_load_explicit(&s->w, memory_order_relaxed);
  uint64_t r = atomic_load_explicit(&s->r, memory_order_acquire);
  float frame[MAX_CH], peak = 0;
  uint32_t pushed = 0, dropped = 0, clips = 0;
  for (uint32_t i = 0; i < frames;) {
    // one output frame: at phase between last (i - 1) and in_buf[i]
    const float *cur = s->in_buf + (size_t)i * dch;
    for (uint32_t c = 0; c < ch; c++) {
      uint32_t sc = s->in_ch >= 0 ? (uint32_t)s->in_ch : c < dch ? c : 0;
      float a = s->last[sc], b = cur[sc];
      frame[c] = rs_audio_input_apply(s->step == 1.0 ? b : a + (float)s->phase * (b - a), s->in_gain, &peak, &clips);
    }
    if ((uint32_t)(w - r) + pushed < s->ring_frames) {
      ring_write(s, w + pushed, frame, 1);
      pushed++;
    } else {
      dropped++;
    }
    s->phase += s->step;
    while (s->phase >= 1.0 && i < frames) {
      s->phase -= 1.0;
      for (uint32_t c = 0; c < dch; c++) s->last[c] = cur[c];
      i++;
      cur += dch;
    }
  }
  atomic_store_explicit(&s->w, w + pushed, memory_order_release);
  rs_audio_input_note(&s->in_stats, peak, clips, pushed);
  if (dropped) atomic_fetch_add_explicit(&s->overruns, dropped, memory_order_relaxed);
  if ((uint32_t)(w + pushed - r) >= s->period_frames) signal_guest(s);
  return noErr;
}

// The Mac game switches the devices it opens to 48 kHz (its Wwise sink and SoundInput do); so
// does this layer, unless RS_AUDIO_SET_RATE=0. Then the HAL unit converts on output, and the
// capture side interpolates.
static double use_rate(AudioObjectID dev, double want) {
  Float64 cur = 0;
  rs_audio_dev_get(dev, kAudioDevicePropertyNominalSampleRate, &cur, sizeof cur);
  if (cur == want || !rs_audio_env_on("RS_AUDIO_SET_RATE", 1)) return cur;
  AudioObjectPropertyAddress a = {kAudioDevicePropertyNominalSampleRate, kAudioObjectPropertyScopeGlobal,
                                  kAudioObjectPropertyElementMain};
  Float64 v = want;
  if (AudioObjectSetPropertyData(dev, &a, 0, NULL, sizeof v, &v) != noErr) return cur;
  for (int i = 0; i < 50; i++) {  // the switch is asynchronous
    rs_audio_dev_get(dev, kAudioDevicePropertyNominalSampleRate, &cur, sizeof cur);
    if (cur == want) break;
    usleep(10000);
  }
  rs_audio_log("device %u: nominal rate %.0f (asked %.0f)", dev, cur, want);
  return cur;
}

static uint32_t scope_u32(AudioObjectID dev, AudioObjectPropertySelector sel, AudioObjectPropertyScope scope) {
  AudioObjectPropertyAddress a = {sel, scope, kAudioObjectPropertyElementMain};
  UInt32 v = 0, sz = sizeof v;
  return AudioObjectGetPropertyData(dev, &a, 0, NULL, &sz, &v) == noErr ? v : 0;
}

static uint32_t stream_latency(AudioObjectID dev, AudioObjectPropertyScope scope) {
  AudioObjectPropertyAddress a = {kAudioDevicePropertyStreams, scope, kAudioObjectPropertyElementMain};
  AudioStreamID st[8];
  UInt32 sz = sizeof st;
  if (AudioObjectGetPropertyData(dev, &a, 0, NULL, &sz, st) != noErr || !sz) return 0;
  return scope_u32(st[0], kAudioStreamPropertyLatency, kAudioObjectPropertyScopeGlobal);
}

wa_stream *wa_stream_open(const wa_endpoint *ep, const wa_fmt *fmt, uint32_t buffer_frames, uint32_t period_frames,
                          int event_mode) {
  wa_stream *s = calloc(1, sizeof *s);
  if (!s) return NULL;
  s->ep = *ep;
  s->fmt = *fmt;
  s->event_mode = event_mode;
  s->buffer_frames = buffer_frames ? buffer_frames : 1024;
  s->period_frames = period_frames ? period_frames : s->buffer_frames;
  // event mode double-buffers (the guest fills one period while the device plays the other)
  s->ring_frames = s->buffer_frames + (event_mode ? s->period_frames : 0);
  atomic_store(&s->gain, 1.f);
  int capture = ep->flow == WA_CAPTURE;
  const char *who = capture ? "WASAPI capture" : "WASAPI render";
  s->ring = calloc((size_t)s->ring_frames * fmt->channels, sizeof(float));
  s->conv = calloc((size_t)s->ring_frames * fmt->channels, sizeof(float));
  if (!s->ring || !s->conv) goto fail;

  s->dev_rate = use_rate(ep->dev, fmt->rate);
  if (s->dev_rate <= 0) s->dev_rate = fmt->rate;

  AudioComponentDescription d = {kAudioUnitType_Output, kAudioUnitSubType_HALOutput, kAudioUnitManufacturer_Apple, 0,
                                 0};
  AudioComponent comp = AudioComponentFindNext(NULL, &d);
  if (!comp || AudioComponentInstanceNew(comp, &s->unit) != noErr) goto fail;
  UInt32 one = 1, zero = 0;
  if (capture) {
    AudioUnitSetProperty(s->unit, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Input, 1, &one, sizeof one);
    AudioUnitSetProperty(s->unit, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Output, 0, &zero, sizeof zero);
  }
  // The device's I/O buffer, before the unit takes the device (the Mac build sets it at the same
  // point): at most half the ring; the guitar input at most a period and 128 frames, the buffer
  // the Mac game gives the cable.
  uint32_t cap = s->ring_frames / 2;
  if (capture && cap > s->period_frames) cap = s->period_frames;
  if (capture && cap > 128) cap = 128;
  s->io_frames = rs_audio_set_device_buffer(ep->dev, cap, who);
  AudioObjectID dev = ep->dev;
  if (AudioUnitSetProperty(s->unit, kAudioOutputUnitProperty_CurrentDevice, kAudioUnitScope_Global, 0, &dev,
                           sizeof dev) != noErr)
    goto fail;
  AudioStreamBasicDescription c = {0};
  c.mFormatID = kAudioFormatLinearPCM;
  c.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
  c.mFramesPerPacket = 1;
  c.mBitsPerChannel = 32;
  if (capture) {
    s->dev_ch = (uint16_t)rs_audio_channels(ep->dev, kAudioObjectPropertyScopeInput);
    if (!s->dev_ch) goto fail;
    if (s->dev_ch > MAX_DEV_CH) s->dev_ch = MAX_DEV_CH;
    // By the device's name in the system: ep->name is the one the game sees.
    rs_audio_input in;
    rs_audio_dev_name(ep->dev, s->in_name, sizeof s->in_name);
    rs_audio_input_for(s->in_name, &in);
    s->in_ch = rs_audio_input_channel(&in, s->dev_ch, s->in_name);
    s->in_gain = in.gain;
    c.mSampleRate = s->dev_rate;  // the input side of a HAL unit does not convert rates
    c.mChannelsPerFrame = s->dev_ch;
    c.mBytesPerFrame = c.mBytesPerPacket = 4u * s->dev_ch;
    if (AudioUnitSetProperty(s->unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 1, &c, sizeof c) !=
        noErr)
      goto fail;
    s->in_max = 8192;
    s->in_buf = calloc((size_t)s->in_max * s->dev_ch, sizeof(float));
    if (!s->in_buf) goto fail;
    s->step = s->dev_rate / fmt->rate;
    s->phase = s->step == 1.0 ? 1.0 : 0.0;
    AURenderCallbackStruct cb = {input_cb, s};
    if (AudioUnitSetProperty(s->unit, kAudioOutputUnitProperty_SetInputCallback, kAudioUnitScope_Global, 0, &cb,
                             sizeof cb) != noErr)
      goto fail;
  } else {
    c.mSampleRate = fmt->rate;
    c.mChannelsPerFrame = fmt->channels;
    c.mBytesPerFrame = c.mBytesPerPacket = 4u * fmt->channels;
    if (AudioUnitSetProperty(s->unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &c, sizeof c) !=
        noErr)
      goto fail;
    AURenderCallbackStruct cb = {render_cb, s};
    if (AudioUnitSetProperty(s->unit, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &cb,
                             sizeof cb) != noErr)
      goto fail;
  }
  if (AudioUnitInitialize(s->unit) != noErr) goto fail;
  rs_audio_dev_get(ep->dev, kAudioDevicePropertyBufferFrameSize, &s->io_frames, sizeof s->io_frames);
  AudioObjectPropertyScope sc = capture ? kAudioObjectPropertyScopeInput : kAudioObjectPropertyScopeOutput;
  s->latency_frames = scope_u32(ep->dev, kAudioDevicePropertyLatency, sc) +
                      scope_u32(ep->dev, kAudioDevicePropertySafetyOffset, sc) + stream_latency(ep->dev, sc) +
                      s->io_frames;
  rs_audio_log("%s: \"%s\" %u Hz x %u %s%u, buffer %u, period %u, ring %u, device I/O %u at %.0f Hz, latency %u frames%s",
               who, ep->name, fmt->rate, fmt->channels, fmt->is_float ? "float" : "int", fmt->bits, s->buffer_frames,
               s->period_frames, s->ring_frames, s->io_frames, s->dev_rate, s->latency_frames,
               event_mode ? ", event mode" : "");
  return s;
fail:
  rs_audio_log("%s: opening \"%s\" failed", who, ep->name);
  wa_stream_close(s);
  return NULL;
}

void wa_stream_close(wa_stream *s) {
  if (!s) return;
  if (s->unit) {
    AudioOutputUnitStop(s->unit);
    AudioUnitUninitialize(s->unit);
    AudioComponentInstanceDispose(s->unit);
  }
  free(s->ring);
  free(s->conv);
  free(s->in_buf);
  free(s);
}

void wa_stream_info_get(wa_stream *s, wa_stream_info *i) {
  i->buffer_frames = s->buffer_frames;
  i->period_frames = s->period_frames;
  i->ring_frames = s->ring_frames;
  i->io_frames = s->io_frames;
  i->latency_frames = s->latency_frames;
  i->device_rate = s->dev_rate;
}

void wa_stream_set_event(wa_stream *s, uint32_t handle) { atomic_store(&s->event, handle); }
void wa_stream_set_notify(wa_stream *s, void (*fn)(void *), void *arg) {
  s->notify_arg = arg;
  s->notify = fn;
}
void wa_stream_set_gain(wa_stream *s, float gain) { atomic_store(&s->gain, gain); }
AudioObjectID wa_stream_device(wa_stream *s) { return s->ep.dev; }
int wa_stream_running(wa_stream *s) { return atomic_load(&s->running); }

int wa_stream_start(wa_stream *s) {
  if (atomic_load(&s->running)) return 0;
  OSStatus st = AudioOutputUnitStart(s->unit);
  if (st != noErr) {
    rs_audio_log("starting \"%s\" failed (%d)", s->ep.name, (int)st);
    return -1;
  }
  atomic_store(&s->running, 1);
  return 0;
}

int wa_stream_stop(wa_stream *s) {
  if (!atomic_load(&s->running)) return 0;
  AudioOutputUnitStop(s->unit);
  atomic_store(&s->running, 0);
  return 0;
}

void wa_stream_reset(wa_stream *s) {
  atomic_store(&s->r, atomic_load(&s->w));
  atomic_store(&s->fed, 0);
}

uint32_t wa_render_padding(wa_stream *s) {
  uint64_t w = atomic_load_explicit(&s->w, memory_order_relaxed);
  uint64_t r = atomic_load_explicit(&s->r, memory_order_acquire);
  uint32_t q = (uint32_t)(w - r);
  return q > s->buffer_frames ? s->buffer_frames : q;
}

void wa_render_write(wa_stream *s, const void *src, uint32_t frames) {
  if (frames > s->ring_frames) frames = s->ring_frames;
  uint64_t w = atomic_load_explicit(&s->w, memory_order_relaxed);
  uint64_t r = atomic_load_explicit(&s->r, memory_order_acquire);
  // No room: drop the oldest queued frames (the newest audio is what the player hears next). The
  // I/O thread may move r at the same time; whichever is further ahead wins.
  while (s->ring_frames - (uint32_t)(w - r) < frames) {
    uint64_t want = w + frames - s->ring_frames;
    if (atomic_compare_exchange_weak(&s->r, &r, want)) {
      atomic_fetch_add_explicit(&s->overruns, 1, memory_order_relaxed);
      break;
    }
  }
  if (src) {
    wa_to_float(&s->fmt, src, s->conv, frames, atomic_load_explicit(&s->gain, memory_order_relaxed));
    ring_write(s, w, s->conv, frames);
  } else {
    ring_write(s, w, NULL, frames);
  }
  atomic_store_explicit(&s->w, w + frames, memory_order_release);
  atomic_store_explicit(&s->fed, 1, memory_order_relaxed);
}

uint64_t wa_render_played(wa_stream *s) { return atomic_load(&s->r); }
uint32_t wa_render_underruns(wa_stream *s) { return atomic_load(&s->underruns); }

// The stale-input trim, as the Mac build's RS_AUDIO_INPUT_TRIM (audio_in.c): the guest reads a
// period per wake-up; when more than two periods wait (start-up phase, clock drift between the
// cable and the output device), the oldest are dropped down to one so the guitar is never heard
// late for the rest of the session.
uint32_t wa_capture_available(wa_stream *s, int trim) {
  rs_audio_input_report(&s->in_stats, 5 * s->fmt.rate, s->in_name);
  uint64_t r = atomic_load_explicit(&s->r, memory_order_relaxed);
  uint64_t w = atomic_load_explicit(&s->w, memory_order_acquire);
  uint32_t avail = (uint32_t)(w - r);
  if (trim && avail > 2 * s->period_frames) {
    uint32_t drop = avail - s->period_frames;
    atomic_store_explicit(&s->r, r + drop, memory_order_release);
    unsigned long t = atomic_fetch_add_explicit(&s->overruns, drop, memory_order_relaxed);
    if (t < 4 * s->period_frames) rs_audio_log("WASAPI capture: dropped %u stale frames", drop);
    avail -= drop;
  }
  return avail;
}

uint32_t wa_capture_peek(wa_stream *s, void *dst, uint32_t frames) {
  uint64_t r = atomic_load_explicit(&s->r, memory_order_relaxed);
  uint64_t w = atomic_load_explicit(&s->w, memory_order_acquire);
  uint32_t avail = (uint32_t)(w - r);
  if (frames > avail) frames = avail;
  if (frames > s->ring_frames) frames = s->ring_frames;
  if (!frames) return 0;
  ring_read(s, r, s->conv, frames);
  wa_from_float(&s->fmt, s->conv, dst, frames, atomic_load_explicit(&s->gain, memory_order_relaxed));
  return frames;
}

void wa_capture_consume(wa_stream *s, uint32_t frames) {
  uint64_t r = atomic_load_explicit(&s->r, memory_order_relaxed);
  uint64_t w = atomic_load_explicit(&s->w, memory_order_acquire);
  if (frames > (uint32_t)(w - r)) frames = (uint32_t)(w - r);
  atomic_store_explicit(&s->r, r + frames, memory_order_release);
}

uint64_t wa_capture_position(wa_stream *s) { return atomic_load(&s->r); }
uint32_t wa_capture_overruns(wa_stream *s) { return atomic_load(&s->overruns); }
