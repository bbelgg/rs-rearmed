// The test input (rs_audio_fake.h) and the HAL calls that list it with the system's devices (rs_audio_host.h), for
// the Mac build's audio overrides and Learn & Play's faces (win-x86/shims/audio).
//
// The device answers its properties here and keeps its I/O functions in a table. While one of them is started a
// real-time thread of its own calls each, once per I/O buffer at the device's rate: it waits for the end of the
// buffer's time on the host clock, as a device's I/O thread wakes when the hardware has filled one. The time of a
// buffer is the host's at the device's rate, scaled by the default output device's measured rate against its
// nominal one (read every 2 s, on another thread), so it keeps the output's pace as the game's queue needs; with no
// output running, the host's.
#include "rs_audio_fake.h"

#include <ctype.h>
#include <mach/mach_time.h>
#include <math.h>
#include <os/lock.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <xlocale.h>

#include "rs_audio_host.h"

#define MAX_PROCS 16
#define MAX_WAV_FRAMES (48000u * 60u * 20u)  // 20 minutes
#define MAX_WAV_BYTES (256u << 20)
#define MIN_FRAMES 32u
#define DEFAULT_FRAMES 512u  // the HAL's, until the game sets its own
#define RATIO_EVERY_S 2

static const double k_rates[RS_AUDIO_FAKE_RATES] = {44100.0, 48000.0};

static void say(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void say(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  fputs("[audio] fake input: ", stderr);
  vfprintf(stderr, fmt, ap);
  fputc('\n', stderr);
  va_end(ap);
}

// ------------------------------------------------------------------------------------------------
// The switch
// ------------------------------------------------------------------------------------------------
static size_t trim(const char **s, size_t n) {
  while (n && isspace((unsigned char)**s)) (*s)++, n--;
  while (n && isspace((unsigned char)(*s)[n - 1])) n--;
  return n;
}

// A number of the C locale's format, all of s.
static int number(const char *s, size_t n, float *out) {
  char buf[32], *end;
  if (!n || n >= sizeof buf) return 0;
  memcpy(buf, s, n);
  buf[n] = 0;
  float v = strtof_l(buf, &end, NULL);
  if (end == buf || *end || !isfinite(v)) return 0;
  *out = v;
  return 1;
}

static int parse_tone(const char *s, size_t n, rs_audio_fake_source *out) {
  const char *colon = memchr(s, ':', n);
  size_t nm = colon ? (size_t)(colon - s) : n;
  out->seconds = 2.0f;
  if (!number(s, nm, &out->midi) || out->midi < 0 || out->midi > 127) return 0;
  if (colon && (!number(colon + 1, n - nm - 1, &out->seconds) || out->seconds < 0.05f || out->seconds > 60))
    return 0;
  return 1;
}

static void parse_one(const char *s, size_t n, rs_audio_fake_source *out) {
  memset(out, 0, sizeof *out);
  out->kind = RS_AUDIO_FAKE_SILENCE;
  if (!n) {
    say("an empty entry is silence");
    return;
  }
  if (n == 7 && strncasecmp(s, "silence", 7) == 0) return;
  if (n >= 5 && strncasecmp(s, "tone:", 5) == 0) {
    if (parse_tone(s + 5, n - 5, out)) out->kind = RS_AUDIO_FAKE_TONE;
    else say("\"%.*s\" is not tone:MIDI[:SECONDS] (MIDI 0-127, 0.05-60 s): silence in its place", (int)n, s);
    return;
  }
  if ((n == 9 || (n > 9 && s[9] == ':')) && strncasecmp(s, "calibrate", 9) == 0) {
    out->midi = 40;
    if (n == 9 || (number(s + 10, n - 10, &out->midi) && out->midi >= 0 && out->midi <= 127))
      out->kind = RS_AUDIO_FAKE_CALIBRATE;
    else
      say("\"%.*s\" is not calibrate[:MIDI] (MIDI 0-127): silence in its place", (int)n, s);
    return;
  }
  if (n >= sizeof out->path) {
    say("a path of %zu characters is too long: silence in its place", n);
    return;
  }
  memcpy(out->path, s, n);
  out->path[n] = 0;
  out->kind = RS_AUDIO_FAKE_WAV;
}

int rs_audio_fake_parse(const char *text, rs_audio_fake_source *out, int max) {
  if (!text) return 0;
  const char *t = text;
  size_t len = trim(&t, strlen(text));
  if (!len || (len == 1 && *t == '0')) return 0;
  int k = 0;
  for (const char *end = t + len; k < max;) {
    const char *comma = memchr(t, ',', (size_t)(end - t));
    const char *item = t;
    size_t n = trim(&item, (size_t)((comma ? comma : end) - t));
    parse_one(item, n, &out[k++]);
    if (!comma) return k;
    t = comma + 1;
  }
  say("more than %d channels named: the first %d are used", max, max);
  return k;
}

void rs_audio_fake_describe(const rs_audio_fake_source *src, char *buf, size_t n) {
  static const char *names[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
  if (src->kind == RS_AUDIO_FAKE_TONE) {
    int note = (int)lroundf(src->midi), cents = (int)lroundf((src->midi - (float)note) * 100);
    char off[16] = "";
    if (cents) snprintf(off, sizeof off, " %+d cents", cents);
    snprintf(buf, n, "tone:%g (%s%d%s, %.1f Hz, every %g s)", src->midi, names[note % 12], note / 12 - 1, off,
             440.0 * pow(2.0, (src->midi - 69) / 12.0), src->seconds);
  } else if (src->kind == RS_AUDIO_FAKE_CALIBRATE) {
    snprintf(buf, n, "calibrate:%g (%g s of plucks, %g s of silence)", src->midi, RS_AUDIO_FAKE_CALIBRATE_PLAY,
             RS_AUDIO_FAKE_CALIBRATE_QUIET);
  } else if (src->kind == RS_AUDIO_FAKE_WAV) {
    snprintf(buf, n, "%s", src->path);
  } else {
    snprintf(buf, n, "silence");
  }
}

// ------------------------------------------------------------------------------------------------
// A plucked string: harmonics at 1/k, each decaying, the higher ones sooner; a 3 ms attack and a fade at the end
// so the note starts again without a click. Peak at RS_AUDIO_FAKE_PEAK.
// ------------------------------------------------------------------------------------------------
int rs_audio_fake_tone(float midi, float seconds, double rate, rs_audio_fake_loop *out) {
  out->s = NULL;
  out->n = 0;
  if (!(midi >= 0 && midi <= 127) || !(seconds >= 0.05f && seconds <= 60) || rate < 8000) return 0;
  double f = 440.0 * pow(2.0, (midi - 69) / 12.0), top = fmin(8000.0, 0.45 * rate);
  if (f >= top) return 0;
  uint32_t n = (uint32_t)lround(seconds * rate);
  float *s = calloc(n, sizeof *s);
  if (!s) return 0;
  double tau1 = fmin(fmax(0.5 * seconds, 0.05), 4.0);
  for (int k = 1; k <= 10 && k * f < top; k++) {
    double w = 2 * M_PI * k * f / rate, e = 1.0 / k, d = exp(-(1.0 + 0.6 * (k - 1)) / (tau1 * rate));
    for (uint32_t i = 0; i < n; i++, e *= d) s[i] += (float)(e * sin(w * i));
  }
  uint32_t attack = (uint32_t)(0.003 * rate), release = (uint32_t)fmin(0.02 * rate, n / 4.0);
  float peak = 0;
  for (uint32_t i = 0; i < n; i++) {
    if (i < attack) s[i] *= (float)i / (float)attack;
    if (n - i <= release) s[i] *= (float)(n - 1 - i) / (float)release;
    if (fabsf(s[i]) > peak) peak = fabsf(s[i]);
  }
  for (uint32_t i = 0; peak > 0 && i < n; i++) s[i] *= RS_AUDIO_FAKE_PEAK / peak;
  out->s = s;
  out->n = n;
  return 1;
}

// The game's calibration raises the level while the player plays loud, then wants the strings muted: a pluck every half
// second near full scale, then silence, so each round of the loop has both.
int rs_audio_fake_calibrate(float midi, double rate, rs_audio_fake_loop *out) {
  rs_audio_fake_loop note;
  if (!rs_audio_fake_tone(midi, 0.5f, rate, &note)) {
    out->s = NULL;
    out->n = 0;
    return 0;
  }
  uint32_t plucks = (uint32_t)(RS_AUDIO_FAKE_CALIBRATE_PLAY / 0.5f);
  uint32_t n = plucks * note.n + (uint32_t)lround(RS_AUDIO_FAKE_CALIBRATE_QUIET * rate);
  float *s = calloc(n, sizeof *s);
  if (s)
    for (uint32_t i = 0; i < plucks * note.n; i++)
      s[i] = note.s[i % note.n] * (RS_AUDIO_FAKE_CALIBRATE_PEAK / RS_AUDIO_FAKE_PEAK);
  rs_audio_fake_loop_free(&note);
  out->s = s;
  out->n = s ? n : 0;
  return s != NULL;
}

// ------------------------------------------------------------------------------------------------
// WAV files
// ------------------------------------------------------------------------------------------------
static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t le32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

static int fail(char *err, size_t nerr, const char *why) {
  if (err && nerr) snprintf(err, nerr, "%s", why);
  return 0;
}

int rs_audio_fake_wav(const void *data, size_t size, double rate, rs_audio_fake_loop *out, char *err, size_t nerr) {
  const uint8_t *p = data, *fmt = NULL, *body = NULL;
  size_t nfmt = 0, nbody = 0;
  out->s = NULL;
  out->n = 0;
  if (size < 12 || memcmp(p, "RIFF", 4) || memcmp(p + 8, "WAVE", 4)) return fail(err, nerr, "not a WAV file");
  for (size_t at = 12; at + 8 <= size;) {
    size_t len = le32(p + at + 4), left = size - at - 8;
    if (len > left) len = left;  // a writer that never filled in the size, or a file cut short
    if (!memcmp(p + at, "fmt ", 4)) fmt = p + at + 8, nfmt = len;
    else if (!memcmp(p + at, "data", 4)) body = p + at + 8, nbody = len;
    at += 8 + len + (len & 1);
  }
  if (!fmt || nfmt < 16) return fail(err, nerr, "no format chunk");
  if (!body) return fail(err, nerr, "no data chunk");
  uint16_t tag = le16(fmt), ch = le16(fmt + 2), align = le16(fmt + 12), bits = le16(fmt + 14);
  uint32_t srate = le32(fmt + 4);
  if (tag == 0xfffe && nfmt >= 40) tag = le16(fmt + 24);  // WAVE_FORMAT_EXTENSIBLE: its sub-format
  if (ch != 1 && ch != 2) return fail(err, nerr, "not mono or stereo");
  if (srate != 44100 && srate != 48000) return fail(err, nerr, "not at 44.1 or 48 kHz");
  if (!((tag == 1 && (bits == 16 || bits == 24)) || (tag == 3 && bits == 32)))
    return fail(err, nerr, "not 16-bit, 24-bit or 32-bit float samples");
  if (align != ch * bits / 8) return fail(err, nerr, "its block size doesn't match its format");
  uint32_t frames = (uint32_t)(nbody / align);
  if (!frames) return fail(err, nerr, "no samples");
  if (frames > MAX_WAV_FRAMES) return fail(err, nerr, "longer than 20 minutes");
  float *mono = malloc(frames * sizeof *mono);
  if (!mono) return fail(err, nerr, "out of memory");
  for (uint32_t i = 0; i < frames; i++) {
    float sum = 0;
    for (uint16_t c = 0; c < ch; c++) {
      const uint8_t *q = body + (size_t)i * align + (size_t)c * (bits / 8);
      float v;
      if (tag == 3) memcpy(&v, q, 4);
      else if (bits == 16) v = (int16_t)le16(q) / 32768.0f;
      else v = (float)(int32_t)((uint32_t)q[0] << 8 | (uint32_t)q[1] << 16 | (uint32_t)q[2] << 24) / 2147483648.0f;
      sum += isfinite(v) ? v : 0;
    }
    mono[i] = sum / ch;
  }
  if (srate == rate) {
    out->s = mono;
    out->n = frames;
    return 1;
  }
  // At the other rate: interpolated, the loop's end leading into its start.
  uint32_t n = (uint32_t)llround((double)frames * rate / srate);
  float *s = n ? malloc(n * sizeof *s) : NULL;
  if (!s) {
    free(mono);
    return fail(err, nerr, "out of memory");
  }
  for (uint32_t i = 0; i < n; i++) {
    double x = (double)i * srate / rate;
    uint32_t a = (uint32_t)x % frames, b = (a + 1) % frames;
    float t = (float)(x - floor(x));
    s[i] = mono[a] + t * (mono[b] - mono[a]);
  }
  free(mono);
  out->s = s;
  out->n = n;
  return 1;
}

int rs_audio_fake_wav_file(const char *path, double rate, rs_audio_fake_loop *out, char *err, size_t nerr) {
  out->s = NULL;
  out->n = 0;
  FILE *f = fopen(path, "rb");
  if (!f) return fail(err, nerr, "can't be opened");
  uint8_t *buf = NULL;
  size_t n = 0, cap = 0;
  for (;;) {
    if (n == cap) {
      if (cap >= MAX_WAV_BYTES) break;
      cap = cap ? cap * 2 : 1u << 20;
      uint8_t *b = realloc(buf, cap);
      if (!b) break;
      buf = b;
    }
    size_t got = fread(buf + n, 1, cap - n, f);
    if (!got) break;
    n += got;
  }
  fclose(f);
  int ok = buf ? rs_audio_fake_wav(buf, n, rate, out, err, nerr) : fail(err, nerr, "out of memory");
  free(buf);
  return ok;
}

void rs_audio_fake_loop_free(rs_audio_fake_loop *l) {
  free(l->s);
  l->s = NULL;
  l->n = 0;
}

// ------------------------------------------------------------------------------------------------
// The device
// ------------------------------------------------------------------------------------------------
typedef struct {
  AudioDeviceIOProc proc;
  void *client;
  AudioDeviceIOProcID id;
  _Atomic int state;  // 0 free, 1 registered, 2 started
} fake_proc;

static _Atomic int g_channels = -1;
static pthread_once_t g_once = PTHREAD_ONCE_INIT;
static rs_audio_fake_source g_src[RS_AUDIO_FAKE_MAX_CHANNELS];
static rs_audio_fake_loop g_loop[RS_AUDIO_FAKE_MAX_CHANNELS][RS_AUDIO_FAKE_RATES];
static float g_gain = 1.0f;
static _Atomic uint32_t g_rate = 48000, g_frames = DEFAULT_FRAMES;

static fake_proc g_procs[MAX_PROCS];
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;  // the table and the thread
static pthread_cond_t g_cv = PTHREAD_COND_INITIALIZER;     // a function started
static os_unfair_lock g_cycle = OS_UNFAIR_LOCK_INIT;        // held while the functions are called
static _Atomic int g_started;
static int g_thread_made;
static pthread_t g_thread;
static _Atomic uint64_t g_ratio_bits;  // a double: the length of a buffer's time against the nominal one

static void init(void) {
  const char *v = getenv("RS_AUDIO_FAKE_INPUT");
  int n = rs_audio_fake_parse(v, g_src, RS_AUDIO_FAKE_MAX_CHANNELS);
  if (!n) {
    atomic_store(&g_channels, 0);
    return;
  }
  const char *gv = getenv("RS_AUDIO_FAKE_GAIN_DB");
  float db = 0;
  if (gv && *gv && !number(gv, strlen(gv), &db)) say("RS_AUDIO_FAKE_GAIN_DB=%s is not a number: no gain", gv);
  if (db < -60) db = -60;
  if (db > 36) db = 36;
  g_gain = powf(10.0f, db / 20.0f);
  double one = 1.0;
  uint64_t bits;
  memcpy(&bits, &one, sizeof bits);
  atomic_store(&g_ratio_bits, bits);
  char list[1024] = "", what[1100];
  for (int c = 0; c < n; c++) {
    rs_audio_fake_describe(&g_src[c], what, sizeof what);
    snprintf(list + strlen(list), sizeof list - strlen(list), "%s%s", c ? ", " : "", what);
    for (int r = 0; r < RS_AUDIO_FAKE_RATES && g_src[c].kind != RS_AUDIO_FAKE_SILENCE; r++) {
      char err[128] = "";
      int ok = g_src[c].kind == RS_AUDIO_FAKE_TONE
                   ? rs_audio_fake_tone(g_src[c].midi, g_src[c].seconds, k_rates[r], &g_loop[c][r])
               : g_src[c].kind == RS_AUDIO_FAKE_CALIBRATE
                   ? rs_audio_fake_calibrate(g_src[c].midi, k_rates[r], &g_loop[c][r])
                   : rs_audio_fake_wav_file(g_src[c].path, k_rates[r], &g_loop[c][r], err, sizeof err);
      if (ok) continue;
      if (g_src[c].kind == RS_AUDIO_FAKE_WAV) say("channel %d: %s: %s: silence in its place", c + 1, g_src[c].path, err);
      else say("channel %d: %s is above what %.0f Hz holds: silence in its place", c + 1, what, k_rates[r]);
      for (int k = 0; k < RS_AUDIO_FAKE_RATES; k++) rs_audio_fake_loop_free(&g_loop[c][k]);
      g_src[c].kind = RS_AUDIO_FAKE_SILENCE;
    }
    if (g_src[c].kind != RS_AUDIO_FAKE_SILENCE)
      rs_audio_log("test input: channel %d, %s: %u frames at 48 kHz (%.2f s), looped", c + 1, what,
                   g_loop[c][1].n, g_loop[c][1].n / 48000.0);
  }
  if (db != 0) say("%d channel%s: %s; gain %+.1f dB", n, n == 1 ? "" : "s", list, db);
  else say("%d channel%s: %s", n, n == 1 ? "" : "s", list);
  atomic_store(&g_channels, n);
}

int rs_audio_fake_channels(void) {
  int n = atomic_load_explicit(&g_channels, memory_order_acquire);
  if (n >= 0) return n;
  pthread_once(&g_once, init);
  return atomic_load(&g_channels);
}

static int on_fake(AudioObjectID obj) { return rs_audio_fake_is(obj) && rs_audio_fake_channels() > 0; }

// ------------------------------------------------------------------------------------------------
// Properties
// ------------------------------------------------------------------------------------------------
typedef struct {
  UInt32 size;
  UInt32 elem;      // an array of these: a smaller buffer takes what fits; 0: exactly size
  const char *str;  // a CFString, made for each read
  union {
    UInt32 u32[64];
    Float64 f64[16];
    AudioValueRange range[8];
    AudioStreamBasicDescription asbd;
    AudioStreamRangedDescription ranged[4];
    AudioBufferList abl;
    uint8_t b[256];
  } v;
} answer;

static void u32(answer *x, UInt32 v) {
  x->size = sizeof(UInt32);
  x->v.u32[0] = v;
}

static void asbd(AudioStreamBasicDescription *f, double rate) {
  UInt32 ch = (UInt32)rs_audio_fake_channels();
  memset(f, 0, sizeof *f);
  f->mSampleRate = rate;
  f->mFormatID = kAudioFormatLinearPCM;
  f->mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
  f->mBytesPerPacket = f->mBytesPerFrame = 4 * ch;
  f->mFramesPerPacket = 1;
  f->mChannelsPerFrame = ch;
  f->mBitsPerChannel = 32;
}

// The stream's own, also asked of the device (the HAL answers them for its first stream of a scope).
static OSStatus stream_answer(AudioObjectPropertySelector sel, answer *x) {
  switch (sel) {
    case kAudioStreamPropertyVirtualFormat:
    case kAudioStreamPropertyPhysicalFormat:
      x->size = sizeof(AudioStreamBasicDescription);
      asbd(&x->v.asbd, atomic_load(&g_rate));
      return noErr;
    case kAudioStreamPropertyAvailableVirtualFormats:
    case kAudioStreamPropertyAvailablePhysicalFormats:
      x->elem = sizeof(AudioStreamRangedDescription);
      x->size = RS_AUDIO_FAKE_RATES * x->elem;
      for (int r = 0; r < RS_AUDIO_FAKE_RATES; r++) {
        asbd(&x->v.ranged[r].mFormat, k_rates[r]);
        x->v.ranged[r].mSampleRateRange = (AudioValueRange){k_rates[r], k_rates[r]};
      }
      return noErr;
    default:
      return kAudioHardwareUnknownPropertyError;
  }
}

static OSStatus fake_answer(AudioObjectID obj, const AudioObjectPropertyAddress *a, answer *x) {
  memset(x, 0, sizeof *x);
  if (!a) return kAudioHardwareIllegalOperationError;
  int out = a->mScope == kAudioObjectPropertyScopeOutput;
  UInt32 ch = (UInt32)rs_audio_fake_channels();
  if (obj == RS_AUDIO_FAKE_STREAM) {
    switch (a->mSelector) {
      case kAudioObjectPropertyClass: u32(x, kAudioStreamClassID); return noErr;
      case kAudioObjectPropertyBaseClass: u32(x, kAudioObjectClassID); return noErr;
      case kAudioObjectPropertyOwner: u32(x, RS_AUDIO_FAKE_DEV); return noErr;
      case kAudioObjectPropertyName: x->str = RS_AUDIO_FAKE_NAME " input"; return noErr;
      case kAudioStreamPropertyIsActive: u32(x, 1); return noErr;
      case kAudioStreamPropertyDirection: u32(x, 1); return noErr;
      case kAudioStreamPropertyTerminalType: u32(x, kAudioStreamTerminalTypeLine); return noErr;
      case kAudioStreamPropertyStartingChannel: u32(x, 1); return noErr;
      case kAudioStreamPropertyLatency: u32(x, 0); return noErr;
      default: return stream_answer(a->mSelector, x);
    }
  }
  switch (a->mSelector) {
    case kAudioObjectPropertyClass: u32(x, kAudioDeviceClassID); return noErr;
    case kAudioObjectPropertyBaseClass: u32(x, kAudioObjectClassID); return noErr;
    case kAudioObjectPropertyOwner: u32(x, kAudioObjectSystemObject); return noErr;
    case kAudioObjectPropertyName:
    case kAudioObjectPropertyModelName: x->str = RS_AUDIO_FAKE_NAME; return noErr;
    case kAudioObjectPropertyManufacturer: x->str = "RS Re-ARMed"; return noErr;
    case kAudioDevicePropertyDeviceUID: x->str = RS_AUDIO_FAKE_UID; return noErr;
    case kAudioDevicePropertyModelUID: x->str = RS_AUDIO_FAKE_UID "-model"; return noErr;
    case kAudioDevicePropertyTransportType: u32(x, kAudioDeviceTransportTypeUSB); return noErr;
    case kAudioDevicePropertyClockDomain: u32(x, 0); return noErr;
    case kAudioDevicePropertyDeviceIsAlive: u32(x, 1); return noErr;
    case kAudioDevicePropertyDeviceIsRunning:
    case kAudioDevicePropertyDeviceIsRunningSomewhere: u32(x, atomic_load(&g_started) > 0); return noErr;
    case kAudioDevicePropertyDeviceCanBeDefaultDevice:
    case kAudioDevicePropertyDeviceCanBeDefaultSystemDevice:
    case kAudioDevicePropertyIsHidden: u32(x, 0); return noErr;
    case kAudioDevicePropertyHogMode: u32(x, (UInt32)-1); return noErr;
    case kAudioDevicePropertyRelatedDevices: x->elem = 4; u32(x, RS_AUDIO_FAKE_DEV); return noErr;
    case kAudioObjectPropertyOwnedObjects: x->elem = 4; u32(x, RS_AUDIO_FAKE_STREAM); return noErr;
    case kAudioDevicePropertyStreams:
      x->elem = 4;
      if (!out) u32(x, RS_AUDIO_FAKE_STREAM);
      return noErr;
    case kAudioDevicePropertyStreamConfiguration:
      x->size = (UInt32)offsetof(AudioBufferList, mBuffers);
      if (out) return noErr;
      x->size += sizeof(AudioBuffer);
      x->v.abl.mNumberBuffers = 1;
      x->v.abl.mBuffers[0].mNumberChannels = ch;
      x->v.abl.mBuffers[0].mDataByteSize = atomic_load(&g_frames) * 4 * ch;
      return noErr;
    case kAudioDevicePropertyNominalSampleRate:
    case kAudioDevicePropertyActualSampleRate:
      x->size = sizeof(Float64);
      x->v.f64[0] = atomic_load(&g_rate);
      return noErr;
    case kAudioDevicePropertyAvailableNominalSampleRates:
      x->elem = sizeof(AudioValueRange);
      x->size = RS_AUDIO_FAKE_RATES * x->elem;
      for (int r = 0; r < RS_AUDIO_FAKE_RATES; r++) x->v.range[r] = (AudioValueRange){k_rates[r], k_rates[r]};
      return noErr;
    case kAudioDevicePropertyBufferFrameSize: u32(x, atomic_load(&g_frames)); return noErr;
    case kAudioDevicePropertyBufferFrameSizeRange:
      x->size = sizeof(AudioValueRange);
      x->v.range[0] = (AudioValueRange){MIN_FRAMES, RS_AUDIO_FAKE_MAX_FRAMES};
      return noErr;
    case kAudioDevicePropertyLatency:
    case kAudioDevicePropertySafetyOffset: u32(x, 0); return noErr;
    case kAudioDevicePropertyPreferredChannelsForStereo:
      x->size = 8;
      x->v.u32[0] = 1;
      x->v.u32[1] = ch > 1 ? 2 : 1;
      return noErr;
    case kAudioDevicePropertyIOCycleUsage:
      x->size = sizeof(Float32);
      {
        Float32 one = 1;
        memcpy(x->v.b, &one, sizeof one);
      }
      return noErr;
    default:
      return out ? kAudioHardwareUnknownPropertyError : stream_answer(a->mSelector, x);
  }
}

static OSStatus fake_get(AudioObjectID obj, const AudioObjectPropertyAddress *a, UInt32 *size, void *data) {
  answer x;
  OSStatus st = fake_answer(obj, a, &x);
  if (st) return st;
  if (!size || !data) return kAudioHardwareIllegalOperationError;
  if (x.str) {
    if (*size < sizeof(CFStringRef)) return kAudioHardwareBadPropertySizeError;
    *(CFStringRef *)data = CFStringCreateWithCString(NULL, x.str, kCFStringEncodingUTF8);
    *size = sizeof(CFStringRef);
    return noErr;
  }
  UInt32 n = x.size;
  if (x.elem) {
    UInt32 fits = *size / x.elem * x.elem;
    if (fits < n) n = fits;
  } else if (*size < n) {
    return kAudioHardwareBadPropertySizeError;
  }
  memcpy(data, x.v.b, n);
  *size = n;
  return noErr;
}

static OSStatus set_rate(double rate) {
  for (int r = 0; r < RS_AUDIO_FAKE_RATES; r++)
    if (rate == k_rates[r]) {
      if (atomic_exchange(&g_rate, (uint32_t)rate) != (uint32_t)rate) rs_audio_log("test input: %.0f Hz", rate);
      return noErr;
    }
  return kAudioDeviceUnsupportedFormatError;
}

static OSStatus fake_set(AudioObjectID obj, const AudioObjectPropertyAddress *a, UInt32 size, const void *data) {
  answer x;
  OSStatus st = fake_answer(obj, a, &x);
  if (st) return st;
  if (!data) return kAudioHardwareIllegalOperationError;
  switch (a->mSelector) {
    case kAudioDevicePropertyNominalSampleRate:
      if (obj != RS_AUDIO_FAKE_DEV) break;
      if (size != sizeof(Float64)) return kAudioHardwareBadPropertySizeError;
      return set_rate(*(const Float64 *)data);
    case kAudioStreamPropertyVirtualFormat:
    case kAudioStreamPropertyPhysicalFormat: {
      if (size != sizeof(AudioStreamBasicDescription)) return kAudioHardwareBadPropertySizeError;
      const AudioStreamBasicDescription *f = data;
      if (f->mFormatID != kAudioFormatLinearPCM || f->mChannelsPerFrame != x.v.asbd.mChannelsPerFrame)
        return kAudioDeviceUnsupportedFormatError;
      return set_rate(f->mSampleRate);
    }
    case kAudioDevicePropertyBufferFrameSize: {
      if (obj != RS_AUDIO_FAKE_DEV) break;
      if (size != sizeof(UInt32)) return kAudioHardwareBadPropertySizeError;
      UInt32 n = *(const UInt32 *)data;
      if (n < MIN_FRAMES) n = MIN_FRAMES;
      if (n > RS_AUDIO_FAKE_MAX_FRAMES) n = RS_AUDIO_FAKE_MAX_FRAMES;
      if (atomic_exchange(&g_frames, n) != n) rs_audio_log("test input: I/O buffer %u frames", n);
      return noErr;
    }
    case kAudioDevicePropertyIOCycleUsage:
    case kAudioDevicePropertyHogMode:
      if (obj == RS_AUDIO_FAKE_DEV) return noErr;  // nothing to share it with
      break;
    default:
      break;
  }
  return kAudioHardwareIllegalOperationError;
}

// ------------------------------------------------------------------------------------------------
// The HAL calls (rs_audio_host.h)
// ------------------------------------------------------------------------------------------------
#define MAX_SYS_DEVS 256

static int is_devices(AudioObjectID obj, const AudioObjectPropertyAddress *a) {
  return obj == kAudioObjectSystemObject && a && a->mSelector == kAudioHardwarePropertyDevices &&
         rs_audio_fake_channels() > 0;
}

OSStatus rs_audio_hal_get(AudioObjectID obj, const AudioObjectPropertyAddress *a, UInt32 qsize, const void *qdata,
                          UInt32 *size, void *data) {
  if (on_fake(obj)) return fake_get(obj, a, size, data);
  if (is_devices(obj, a) && size && data) {
    AudioObjectID devs[MAX_SYS_DEVS];
    UInt32 sz = sizeof devs - sizeof *devs;
    OSStatus st = AudioObjectGetPropertyData(obj, a, qsize, qdata, &sz, devs + 1);
    if (st) return st;
    devs[0] = RS_AUDIO_FAKE_DEV;
    UInt32 n = sz + (UInt32)sizeof *devs, fits = *size / sizeof *devs * sizeof *devs;
    if (n > fits) n = fits;
    memcpy(data, devs, n);
    *size = n;
    return noErr;
  }
  if (obj == kAudioObjectSystemObject && a && a->mSelector == kAudioHardwarePropertyTranslateUIDToDevice &&
      rs_audio_fake_channels() > 0 && qsize == sizeof(CFStringRef) && qdata && *(CFStringRef const *)qdata &&
      size && *size >= sizeof(AudioObjectID) && data) {
    char uid[64] = "";
    CFStringGetCString(*(CFStringRef const *)qdata, uid, sizeof uid, kCFStringEncodingUTF8);
    if (!strcmp(uid, RS_AUDIO_FAKE_UID)) {
      *(AudioObjectID *)data = RS_AUDIO_FAKE_DEV;
      *size = sizeof(AudioObjectID);
      return noErr;
    }
  }
  return AudioObjectGetPropertyData(obj, a, qsize, qdata, size, data);
}

OSStatus rs_audio_hal_get_size(AudioObjectID obj, const AudioObjectPropertyAddress *a, UInt32 qsize,
                               const void *qdata, UInt32 *size) {
  if (on_fake(obj)) {
    answer x;
    OSStatus st = fake_answer(obj, a, &x);
    if (!st && size) *size = x.str ? (UInt32)sizeof(CFStringRef) : x.size;
    return st;
  }
  OSStatus st = AudioObjectGetPropertyDataSize(obj, a, qsize, qdata, size);
  if (!st && size && is_devices(obj, a)) *size += sizeof(AudioObjectID);
  return st;
}

OSStatus rs_audio_hal_set(AudioObjectID obj, const AudioObjectPropertyAddress *a, UInt32 qsize, const void *qdata,
                          UInt32 size, const void *data) {
  if (on_fake(obj)) return fake_set(obj, a, size, data);
  return AudioObjectSetPropertyData(obj, a, qsize, qdata, size, data);
}

Boolean rs_audio_hal_has(AudioObjectID obj, const AudioObjectPropertyAddress *a) {
  if (on_fake(obj)) {
    answer x;
    return fake_answer(obj, a, &x) == noErr;
  }
  return AudioObjectHasProperty(obj, a);
}

OSStatus rs_audio_hal_listen(AudioObjectID obj, const AudioObjectPropertyAddress *a,
                             AudioObjectPropertyListenerProc fn, void *client, int add) {
  if (on_fake(obj)) return noErr;
  return add ? AudioObjectAddPropertyListener(obj, a, fn, client) : AudioObjectRemovePropertyListener(obj, a, fn, client);
}

// ------------------------------------------------------------------------------------------------
// I/O
// ------------------------------------------------------------------------------------------------
static double ratio(void) {
  uint64_t bits = atomic_load_explicit(&g_ratio_bits, memory_order_relaxed);
  double r;
  memcpy(&r, &bits, sizeof r);
  return r;
}

static uint64_t to_abs(double ns) {
  static mach_timebase_info_data_t tb;
  if (!tb.denom) mach_timebase_info(&tb);
  return (uint64_t)(ns * tb.denom / tb.numer);
}

// The default output's measured rate against its nominal one, while it runs.
static void *ratio_main(void *arg) {
  (void)arg;
  pthread_setname_np("rs-audio test input clock");
  double was = 1.0;
  for (;;) {
    sleep(RATIO_EVERY_S);
    if (!atomic_load(&g_started)) continue;
    AudioObjectPropertyAddress a = {kAudioHardwarePropertyDefaultOutputDevice, kAudioObjectPropertyScopeGlobal,
                                    kAudioObjectPropertyElementMain};
    AudioObjectID dev = kAudioObjectUnknown;
    UInt32 sz = sizeof dev;
    Float64 nominal = 0, actual = 0;
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &a, 0, NULL, &sz, &dev) || dev == kAudioObjectUnknown)
      continue;
    a.mSelector = kAudioDevicePropertyNominalSampleRate;
    sz = sizeof nominal;
    AudioObjectGetPropertyData(dev, &a, 0, NULL, &sz, &nominal);
    a.mSelector = kAudioDevicePropertyActualSampleRate;
    sz = sizeof actual;
    AudioObjectGetPropertyData(dev, &a, 0, NULL, &sz, &actual);
    double r = nominal > 0 && actual > 0 ? nominal / actual : 1.0;
    if (fabs(r - 1.0) > 0.005) r = 1.0;  // not running, or not a rate to go by
    uint64_t bits;
    memcpy(&bits, &r, sizeof bits);
    atomic_store(&g_ratio_bits, bits);
    if (fabs(r - was) > 2e-6) {
      if (r == 1.0) rs_audio_log("test input: its clock is the host's");
      else rs_audio_log("test input: its clock follows device %u's, %.3f Hz of %.0f", dev, actual, nominal);
      was = r;
    }
  }
  return NULL;
}

static int rate_index(uint32_t rate) { return rate == 44100 ? 0 : 1; }

// One buffer of every channel, interleaved, from the loops at the rate's index; pos is each channel's place.
static void render(float *buf, uint32_t n, int ri, uint32_t *pos, int *last_ri) {
  int ch = atomic_load_explicit(&g_channels, memory_order_relaxed);
  if (*last_ri != ri) {
    for (int c = 0; c < ch && *last_ri >= 0; c++)
      if (g_loop[c][*last_ri].n && g_loop[c][ri].n)
        pos[c] = (uint32_t)((uint64_t)pos[c] * g_loop[c][ri].n / g_loop[c][*last_ri].n) % g_loop[c][ri].n;
    *last_ri = ri;
  }
  for (int c = 0; c < ch; c++) {
    const rs_audio_fake_loop *l = &g_loop[c][ri];
    if (!l->s) {
      for (uint32_t i = 0; i < n; i++) buf[(size_t)i * ch + c] = 0;
      continue;
    }
    uint32_t p = pos[c];
    for (uint32_t i = 0; i < n; i++) {
      float v = l->s[p] * g_gain;
      buf[(size_t)i * ch + c] = v > 1 ? 1 : v < -1 ? -1 : v;
      if (++p >= l->n) p = 0;
    }
    pos[c] = p;
  }
}

static void *clock_main(void *arg) {
  (void)arg;
  pthread_setname_np("rs-audio test input");
  static float buf[RS_AUDIO_FAKE_MAX_FRAMES * RS_AUDIO_FAKE_MAX_CHANNELS];
  uint32_t pos[RS_AUDIO_FAKE_MAX_CHANNELS] = {0};
  int last_ri = -1, ch = rs_audio_fake_channels();
  double sample = 0;
  for (;;) {
    pthread_mutex_lock(&g_mu);
    while (!atomic_load(&g_started)) pthread_cond_wait(&g_cv, &g_mu);
    pthread_mutex_unlock(&g_mu);
    uint32_t frames = 0, rate = 0;
    double r = 0;
    uint64_t base = mach_absolute_time(), done = 0;
    while (atomic_load(&g_started)) {
      uint32_t n = atomic_load(&g_frames), sr = atomic_load(&g_rate);
      double rn = ratio();
      if (n != frames || sr != rate || rn != r) {
        base += to_abs(done * 1e9 / (rate ? rate : sr) * (r ? r : rn));
        done = 0;
        if (n != frames || sr != rate) rs_audio_make_realtime(n, sr, "test input");
        frames = n;
        rate = sr;
        r = rn;
      }
      uint64_t period = to_abs(frames * 1e9 / rate * r), due = base + to_abs((done + frames) * 1e9 / rate * r);
      uint64_t now = mach_absolute_time();
      if (now < due) {
        mach_wait_until(due);
      } else if (now - due > 4 * period) {  // held up (a sleep, a debugger): from now on
        base = now - period;
        done = 0;
        due = now;
      }
      render(buf, frames, rate_index(rate), pos, &last_ri);
      AudioBufferList in = {1, {{(UInt32)ch, frames * 4 * (UInt32)ch, buf}}};
      AudioBufferList out = {0, {{0, 0, NULL}}};
      UInt32 flags = kAudioTimeStampSampleHostTimeValid | kAudioTimeStampRateScalarValid;
      AudioTimeStamp t_now = {.mSampleTime = sample + frames, .mHostTime = due, .mRateScalar = 1, .mFlags = flags};
      AudioTimeStamp t_in = {.mSampleTime = sample, .mHostTime = due - period, .mRateScalar = 1, .mFlags = flags};
      AudioTimeStamp t_out = t_now;
      os_unfair_lock_lock(&g_cycle);
      for (int i = 0; i < MAX_PROCS; i++)
        if (atomic_load_explicit(&g_procs[i].state, memory_order_acquire) == 2)
          g_procs[i].proc(RS_AUDIO_FAKE_DEV, &t_now, &in, &t_in, &out, &t_out, g_procs[i].client);
      os_unfair_lock_unlock(&g_cycle);
      done += frames;
      sample += frames;
    }
  }
  return NULL;
}

static int on_clock(void) { return g_thread_made && pthread_equal(pthread_self(), g_thread); }
static void cycle_lock(void) {
  if (!on_clock()) os_unfair_lock_lock(&g_cycle);
}
static void cycle_unlock(void) {
  if (!on_clock()) os_unfair_lock_unlock(&g_cycle);
}

static fake_proc *proc_of(AudioDeviceIOProcID id) {
  for (int i = 0; i < MAX_PROCS; i++)
    if (atomic_load(&g_procs[i].state) && g_procs[i].id == id) return &g_procs[i];
  return NULL;
}

OSStatus rs_audio_hal_io_create(AudioObjectID dev, AudioDeviceIOProc proc, void *client, AudioDeviceIOProcID *out) {
  if (!on_fake(dev)) return AudioDeviceCreateIOProcID(dev, proc, client, out);
  if (!proc || !out) return kAudioHardwareIllegalOperationError;
  pthread_mutex_lock(&g_mu);
  int k = -1;
  for (int i = 0; i < MAX_PROCS && k < 0; i++)
    if (!atomic_load(&g_procs[i].state)) k = i;
  if (k >= 0) {
    g_procs[k].proc = proc;
    g_procs[k].client = client;
    // the function itself, as the HAL gives it the first time; another registration of it gets an id of its own
    g_procs[k].id = proc_of(proc) ? (AudioDeviceIOProcID)(void *)&g_procs[k] : proc;
    *out = g_procs[k].id;
    atomic_store(&g_procs[k].state, 1);
  }
  pthread_mutex_unlock(&g_mu);
  if (k < 0) {
    rs_audio_log("test input: more than %d I/O functions", MAX_PROCS);
    return kAudioHardwareIllegalOperationError;
  }
  rs_audio_log("test input: I/O function %d registered", k);
  return noErr;
}

static OSStatus stop(AudioDeviceIOProcID id) {
  fake_proc *p = proc_of(id);
  if (!p) return id ? kAudioHardwareIllegalOperationError : noErr;
  cycle_lock();  // when it returns, the function isn't running and won't be called again
  int was = atomic_exchange(&p->state, 1);
  cycle_unlock();
  if (was == 2) {
    atomic_fetch_sub(&g_started, 1);
    rs_audio_log("test input: I/O function %d stopped", (int)(p - g_procs));
  }
  return noErr;
}

OSStatus rs_audio_hal_io_destroy(AudioObjectID dev, AudioDeviceIOProcID id) {
  if (!on_fake(dev)) return AudioDeviceDestroyIOProcID(dev, id);
  int mine = on_clock();  // from its own function, as rs_audio_hal_stop
  if (!mine) pthread_mutex_lock(&g_mu);
  fake_proc *p = proc_of(id);
  if (p) {
    stop(id);
    atomic_store(&p->state, 0);
  }
  if (!mine) pthread_mutex_unlock(&g_mu);
  return p ? noErr : kAudioHardwareIllegalOperationError;
}

OSStatus rs_audio_hal_start(AudioObjectID dev, AudioDeviceIOProcID id) {
  if (!on_fake(dev)) return AudioDeviceStart(dev, id);
  if (!id) return noErr;  // the device without a function of the caller's: nothing to do here
  pthread_mutex_lock(&g_mu);
  fake_proc *p = proc_of(id);
  OSStatus st = kAudioHardwareIllegalOperationError;
  if (p) {
    st = noErr;
    if (!g_thread_made) {
      pthread_t t;
      g_thread_made = !pthread_create(&g_thread, NULL, clock_main, NULL);
      if (g_thread_made && !pthread_create(&t, NULL, ratio_main, NULL)) pthread_detach(t);
      if (!g_thread_made) st = kAudioHardwareUnspecifiedError;
    }
    if (!st && atomic_exchange(&p->state, 2) != 2) {
      atomic_fetch_add(&g_started, 1);
      pthread_cond_broadcast(&g_cv);
      rs_audio_log("test input: I/O function %d started, %u frames at %u Hz", (int)(p - g_procs),
                   atomic_load(&g_frames), atomic_load(&g_rate));
    }
  }
  pthread_mutex_unlock(&g_mu);
  return st;
}

OSStatus rs_audio_hal_stop(AudioObjectID dev, AudioDeviceIOProcID id) {
  if (!on_fake(dev)) return AudioDeviceStop(dev, id);
  if (on_clock()) return stop(id);  // from its own function: the table's lock may be held by a caller waiting
  pthread_mutex_lock(&g_mu);
  OSStatus st = stop(id);
  pthread_mutex_unlock(&g_mu);
  return st;
}
