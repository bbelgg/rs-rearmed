// rs_audio_fake: the switch, the tone, WAV files and the test input as a device, through the HAL calls of
// rs_audio_host.h, its I/O function run in real time. No game. --off: the same calls with the switch unset.
#include "rs_audio_fake.h"

#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "rs_audio_host.h"
#include "rs_audio_input.h"

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)
#define NEAR(a, b, e) (fabs((double)(a) - (double)(b)) < (e))

// ------------------------------------------------------------------------------------------------
// The switch
// ------------------------------------------------------------------------------------------------
static void test_parse(void) {
  rs_audio_fake_source s[RS_AUDIO_FAKE_MAX_CHANNELS];
  CHECK(rs_audio_fake_parse(NULL, s, 8) == 0);
  CHECK(rs_audio_fake_parse("", s, 8) == 0);
  CHECK(rs_audio_fake_parse("  ", s, 8) == 0);
  CHECK(rs_audio_fake_parse("0", s, 8) == 0);
  CHECK(rs_audio_fake_parse("tone:35,silence", s, 8) == 2);
  CHECK(s[0].kind == RS_AUDIO_FAKE_TONE && s[0].midi == 35 && s[0].seconds == 2);
  CHECK(s[1].kind == RS_AUDIO_FAKE_SILENCE);
  CHECK(rs_audio_fake_parse(" tone:40.5:3 , /some/where/take 1.wav ,SILENCE", s, 8) == 3);
  CHECK(s[0].kind == RS_AUDIO_FAKE_TONE && s[0].midi == 40.5f && s[0].seconds == 3);
  CHECK(s[1].kind == RS_AUDIO_FAKE_WAV && !strcmp(s[1].path, "/some/where/take 1.wav"));
  CHECK(s[2].kind == RS_AUDIO_FAKE_SILENCE);
  // what isn't understood is silence, and keeps its place
  CHECK(rs_audio_fake_parse("tone:abc,tone:200,tone:40:0,tone:40:x,tone:,,tone:28", s, 8) == 7);
  for (int i = 0; i < 6; i++) CHECK(s[i].kind == RS_AUDIO_FAKE_SILENCE);
  CHECK(s[6].kind == RS_AUDIO_FAKE_TONE && s[6].midi == 28);
  // at most max
  CHECK(rs_audio_fake_parse("silence,silence,silence,silence,silence,silence,silence,silence,silence,tone:40", s, 8) ==
        8);
  char d[256];
  rs_audio_fake_parse("tone:35,tone:40.5:3,silence", s, 8);
  rs_audio_fake_describe(&s[0], d, sizeof d);
  CHECK(!strcmp(d, "tone:35 (B1, 61.7 Hz, every 2 s)"));
  rs_audio_fake_describe(&s[1], d, sizeof d);
  CHECK(strstr(d, "tone:40.5 (F2 -50 cents, 84.8 Hz, every 3 s)"));
  rs_audio_fake_describe(&s[2], d, sizeof d);
  CHECK(!strcmp(d, "silence"));
  printf("parse: ok\n");
}

// ------------------------------------------------------------------------------------------------
// The tone
// ------------------------------------------------------------------------------------------------
// The size of the sound's component at f over [from, from + n).
static double level_at(const float *s, uint32_t from, uint32_t n, double f, double rate) {
  double re = 0, im = 0;
  for (uint32_t i = 0; i < n; i++) {
    re += s[from + i] * cos(2 * M_PI * f * i / rate);
    im += s[from + i] * sin(2 * M_PI * f * i / rate);
  }
  return sqrt(re * re + im * im) / n;
}

// The cents from f of the strongest component within 30 cents of it.
static int strongest_cents(const float *s, uint32_t from, uint32_t n, double f, double rate) {
  int best = -99;
  double top = -1;
  for (int c = -30; c <= 30; c++) {
    double v = level_at(s, from, n, f * pow(2.0, c / 1200.0), rate);
    if (v > top) top = v, best = c;
  }
  return best;
}

static double rms(const float *s, uint32_t from, uint32_t n) {
  double a = 0;
  for (uint32_t i = 0; i < n; i++) a += (double)s[from + i] * s[from + i];
  return sqrt(a / n);
}

static void test_tone(void) {
  static const float notes[] = {23, 35, 40, 40.5f, 69, 88};
  for (int r = 0; r < 2; r++) {
    double rate = r ? 48000 : 44100;
    for (size_t k = 0; k < sizeof notes / sizeof *notes; k++) {
      rs_audio_fake_loop l;
      CHECK(rs_audio_fake_tone(notes[k], 2, rate, &l));
      CHECK(l.n == (uint32_t)(2 * rate));
      float peak = 0;
      for (uint32_t i = 0; i < l.n; i++) peak = fmaxf(peak, fabsf(l.s[i]));
      CHECK(NEAR(peak, RS_AUDIO_FAKE_PEAK, 1e-4));
      // no click where it starts again
      CHECK(l.s[0] == 0 && fabsf(l.s[l.n - 1]) < 1e-3f);
      // the note, in tune: the strongest component near it is at it, within a cent
      double f = 440.0 * pow(2.0, (notes[k] - 69) / 12.0);
      uint32_t from = (uint32_t)(0.01 * rate), n = (uint32_t)(0.5 * rate);
      int cents = strongest_cents(l.s, from, n, f, rate);
      CHECK(cents >= -1 && cents <= 1);
      // above a semitone off either way
      double at = level_at(l.s, from, n, f, rate);
      CHECK(at > 5 * level_at(l.s, from, n, f * pow(2.0, 1 / 12.0), rate));
      CHECK(at > 5 * level_at(l.s, from, n, f / pow(2.0, 1 / 12.0), rate));
      // it decays, and is still heard at the end
      double first = rms(l.s, from, (uint32_t)(0.2 * rate)), last = rms(l.s, l.n - (uint32_t)(0.3 * rate), (uint32_t)(0.2 * rate));
      CHECK(first > 2 * last && last > 0.01);
      rs_audio_fake_loop_free(&l);
      CHECK(!l.s && !l.n);
    }
  }
  rs_audio_fake_loop l;
  CHECK(rs_audio_fake_tone(35, 0.5f, 48000, &l) && l.n == 24000);
  rs_audio_fake_loop_free(&l);
  CHECK(!rs_audio_fake_tone(127, 2, 48000, &l) && !l.s);  // above the harmonics' limit
  CHECK(!rs_audio_fake_tone(-1, 2, 48000, &l));
  CHECK(!rs_audio_fake_tone(40, 0.01f, 48000, &l));
  CHECK(!rs_audio_fake_tone(40, 61, 48000, &l));
  printf("tone: ok\n");
}

// ------------------------------------------------------------------------------------------------
// WAV files
// ------------------------------------------------------------------------------------------------
static size_t put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); return 2; }
static size_t put32(uint8_t *p, uint32_t v) { put16(p, (uint16_t)v); put16(p + 2, (uint16_t)(v >> 16)); return 4; }

typedef struct {
  uint16_t tag, ch, bits;
  uint32_t rate, frames;
  int extensible, junk, streamed;  // a WAVE_FORMAT_EXTENSIBLE header, an odd chunk before it, a data size of ~0
} wav_spec;

// A WAV file in buf; sample(i, c) gives each sample. Returns its size.
static size_t make_wav(uint8_t *buf, const wav_spec *w, float (*sample)(uint32_t, uint16_t)) {
  uint8_t *p = buf;
  uint16_t align = (uint16_t)(w->ch * w->bits / 8);
  memcpy(p, "RIFF", 4), p += 8;
  memcpy(p, "WAVE", 4), p += 4;
  if (w->junk) {
    memcpy(p, "LIST", 4), p += 4;
    p += put32(p, 3);
    memcpy(p, "abc\0", 4), p += 4;  // three bytes and the pad
  }
  memcpy(p, "fmt ", 4), p += 4;
  p += put32(p, w->extensible ? 40 : 16);
  p += put16(p, w->extensible ? 0xfffe : w->tag);
  p += put16(p, w->ch);
  p += put32(p, w->rate);
  p += put32(p, w->rate * align);
  p += put16(p, align);
  p += put16(p, w->bits);
  if (w->extensible) {
    p += put16(p, 22);
    p += put16(p, w->bits);
    p += put32(p, w->ch == 1 ? 4 : 3);
    p += put16(p, w->tag);
    static const uint8_t tail[14] = {0, 0, 0, 0, 0x10, 0, 0x80, 0, 0, 0xaa, 0, 0x38, 0x9b, 0x71};
    memcpy(p, tail, 14), p += 14;
  }
  memcpy(p, "data", 4), p += 4;
  p += put32(p, w->streamed ? 0xffffffffu : w->frames * align);
  for (uint32_t i = 0; i < w->frames; i++)
    for (uint16_t c = 0; c < w->ch; c++) {
      float v = sample(i, c);
      if (w->tag == 3) memcpy(p, &v, 4), p += 4;
      else if (w->bits == 16) p += put16(p, (uint16_t)(int16_t)lrintf(v * 32767));
      else if (w->bits == 24) {
        int32_t q = (int32_t)lrintf(v * 8388607);
        p[0] = (uint8_t)q, p[1] = (uint8_t)(q >> 8), p[2] = (uint8_t)(q >> 16), p += 3;
      } else *p++ = (uint8_t)(lrintf(v * 127) + 128);
    }
  put32(buf + 4, (uint32_t)(p - buf - 8));
  return (size_t)(p - buf);
}

static float sine441(uint32_t i, uint16_t c) { (void)c; return 0.5f * (float)sin(2 * M_PI * 441 * i / 44100.0); }
static float left_right(uint32_t i, uint16_t c) { (void)i; return c ? 0.2f : 0.4f; }
static float quarter(uint32_t i, uint16_t c) { (void)i; (void)c; return 0.25f; }

static uint8_t g_wav[1 << 20];

static void wav_fails(const wav_spec *w, const char *why) {
  rs_audio_fake_loop l;
  char err[128] = "";
  size_t n = make_wav(g_wav, w, quarter);
  CHECK(!rs_audio_fake_wav(g_wav, n, 48000, &l, err, sizeof err) && !l.s);
  if (!strstr(err, why)) fprintf(stderr, "wanted \"%s\", got \"%s\"\n", why, err);
  CHECK(strstr(err, why));
}

static void test_wav(const char *dir) {
  rs_audio_fake_loop l;
  char err[128];
  // 16-bit mono at 44.1 kHz: as it is at 44.1, interpolated at 48
  wav_spec w = {1, 1, 16, 44100, 22050, 0, 0, 0};
  size_t n = make_wav(g_wav, &w, sine441);
  CHECK(rs_audio_fake_wav(g_wav, n, 44100, &l, err, sizeof err) && l.n == 22050);
  for (uint32_t i = 0; i < l.n; i++) CHECK(NEAR(l.s[i], sine441(i, 0), 1e-4));
  rs_audio_fake_loop_free(&l);
  CHECK(rs_audio_fake_wav(g_wav, n, 48000, &l, err, sizeof err) && l.n == 24000);
  CHECK(strongest_cents(l.s, 0, 24000, 441, 48000) == 0);
  CHECK(NEAR(level_at(l.s, 0, 24000, 441, 48000), 0.25, 0.01));
  rs_audio_fake_loop_free(&l);
  // stereo float, mixed to one channel; an odd chunk before the format, and a data size never filled in
  w = (wav_spec){3, 2, 32, 48000, 4800, 0, 1, 1};
  n = make_wav(g_wav, &w, left_right);
  CHECK(rs_audio_fake_wav(g_wav, n, 48000, &l, err, sizeof err) && l.n == 4800);
  for (uint32_t i = 0; i < l.n; i++) CHECK(NEAR(l.s[i], 0.3, 1e-6));
  rs_audio_fake_loop_free(&l);
  // 24-bit, and WAVE_FORMAT_EXTENSIBLE
  w = (wav_spec){1, 1, 24, 48000, 480, 1, 0, 0};
  n = make_wav(g_wav, &w, quarter);
  CHECK(rs_audio_fake_wav(g_wav, n, 44100, &l, err, sizeof err) && l.n == 441);
  for (uint32_t i = 0; i < l.n; i++) CHECK(NEAR(l.s[i], 0.25, 1e-5));
  rs_audio_fake_loop_free(&l);
  w = (wav_spec){3, 2, 32, 44100, 441, 1, 0, 0};
  n = make_wav(g_wav, &w, quarter);
  CHECK(rs_audio_fake_wav(g_wav, n, 44100, &l, err, sizeof err) && l.n == 441 && l.s[100] == 0.25f);
  rs_audio_fake_loop_free(&l);
  // what it doesn't take
  CHECK(!rs_audio_fake_wav("RIFX....WAVE", 12, 48000, &l, err, sizeof err) && strstr(err, "not a WAV file"));
  wav_fails(&(wav_spec){1, 1, 16, 22050, 100, 0, 0, 0}, "not at 44.1 or 48 kHz");
  wav_fails(&(wav_spec){1, 1, 8, 48000, 100, 0, 0, 0}, "not 16-bit, 24-bit or 32-bit float");
  wav_fails(&(wav_spec){3, 1, 16, 48000, 100, 0, 0, 0}, "not 16-bit, 24-bit or 32-bit float");
  wav_fails(&(wav_spec){1, 3, 16, 48000, 100, 0, 0, 0}, "not mono or stereo");
  wav_fails(&(wav_spec){1, 1, 16, 48000, 0, 0, 0, 0}, "no samples");
  n = make_wav(g_wav, &(wav_spec){1, 1, 16, 48000, 100, 0, 0, 0}, quarter);
  CHECK(!rs_audio_fake_wav(g_wav, 36, 48000, &l, err, sizeof err) && strstr(err, "no data chunk"));
  // from a file
  char path[1024];
  snprintf(path, sizeof path, "%s/rs_audio_fake_test.wav", dir);
  FILE *f = fopen(path, "wb");
  CHECK(f && fwrite(g_wav, 1, n, f) == n && !fclose(f));
  CHECK(rs_audio_fake_wav_file(path, 48000, &l, err, sizeof err) && l.n == 100 && NEAR(l.s[0], 0.25, 1e-4));
  rs_audio_fake_loop_free(&l);
  snprintf(path, sizeof path, "%s/not-there.wav", dir);
  CHECK(!rs_audio_fake_wav_file(path, 48000, &l, err, sizeof err) && strstr(err, "can't be opened"));
  printf("wav: ok\n");
}

// ------------------------------------------------------------------------------------------------
// The device
// ------------------------------------------------------------------------------------------------
static int system_devices(AudioObjectID *devs, int max) {
  AudioObjectPropertyAddress a = {kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal,
                                  kAudioObjectPropertyElementMain};
  UInt32 sz = (UInt32)(max * sizeof *devs);
  CHECK(rs_audio_hal_get(kAudioObjectSystemObject, &a, 0, NULL, &sz, devs) == noErr);
  UInt32 want = 0;
  CHECK(rs_audio_hal_get_size(kAudioObjectSystemObject, &a, 0, NULL, &want) == noErr);
  CHECK(want == sz || (sz == max * sizeof *devs && want > sz));
  return (int)(sz / sizeof *devs);
}

static void test_off(void) {
  CHECK(rs_audio_fake_channels() == 0);
  AudioObjectID devs[128];
  int n = system_devices(devs, 128);
  for (int i = 0; i < n; i++) CHECK(devs[i] != RS_AUDIO_FAKE_DEV);
  AudioObjectPropertyAddress a = {kAudioObjectPropertyName, kAudioObjectPropertyScopeGlobal,
                                  kAudioObjectPropertyElementMain};
  CFStringRef s = NULL;
  UInt32 sz = sizeof s;
  CHECK(rs_audio_hal_get(RS_AUDIO_FAKE_DEV, &a, 0, NULL, &sz, &s) != noErr);
  CHECK(!rs_audio_has_input(RS_AUDIO_FAKE_DEV) && !rs_audio_hal_has(RS_AUDIO_FAKE_DEV, &a));
  printf("off: %d devices, none the test input: ok\n", n);
}

static _Atomic uint32_t g_calls, g_frames, g_bad;
static _Atomic uint32_t g_peak[3];  // float bits
static _Atomic uint64_t g_last_host;

static void note_peak(int c, float v) {
  uint32_t bits, was;
  memcpy(&bits, &v, sizeof bits);
  was = atomic_load(&g_peak[c]);
  while (bits > was && !atomic_compare_exchange_weak(&g_peak[c], &was, bits)) {
  }
}

static OSStatus io_proc(AudioObjectID dev, const AudioTimeStamp *now, const AudioBufferList *in,
                        const AudioTimeStamp *in_time, AudioBufferList *out, const AudioTimeStamp *out_time,
                        void *client) {
  (void)in_time;
  (void)out_time;
  if (dev != RS_AUDIO_FAKE_DEV || client != &g_calls || !in || in->mNumberBuffers != 1 ||
      in->mBuffers[0].mNumberChannels != 3 || !out || out->mNumberBuffers != 0 ||
      !(now->mFlags & kAudioTimeStampHostTimeValid) || now->mHostTime < atomic_load(&g_last_host))
    atomic_fetch_add(&g_bad, 1);
  atomic_store(&g_last_host, now->mHostTime);
  const float *x = in->mBuffers[0].mData;
  uint32_t n = in->mBuffers[0].mDataByteSize / (3 * sizeof(float));
  for (uint32_t i = 0; i < n; i++)
    for (int c = 0; c < 3; c++) note_peak(c, fabsf(x[i * 3 + c]));
  atomic_fetch_add(&g_frames, n);
  atomic_fetch_add(&g_calls, 1);
  return noErr;
}

static float peak_of(int c) {
  uint32_t bits = atomic_load(&g_peak[c]);
  float v;
  memcpy(&v, &bits, sizeof v);
  return v;
}

static double now_s(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return t.tv_sec + t.tv_nsec / 1e9;
}

static void test_device(void) {
  CHECK(rs_audio_fake_channels() == 3);
  // first in the system's list, an input of 3 channels and no outputs
  AudioObjectID devs[128];
  int n = system_devices(devs, 128);
  CHECK(n >= 1 && devs[0] == RS_AUDIO_FAKE_DEV);
  AudioObjectID two[2];
  CHECK(system_devices(two, 2) == (n < 2 ? n : 2) && two[0] == RS_AUDIO_FAKE_DEV);
  char name[128];
  rs_audio_dev_name(RS_AUDIO_FAKE_DEV, name, sizeof name);
  CHECK(!strcmp(name, RS_AUDIO_FAKE_NAME));
  CHECK(rs_audio_has_input(RS_AUDIO_FAKE_DEV) && !rs_audio_has_output(RS_AUDIO_FAKE_DEV));
  CHECK(rs_audio_channels(RS_AUDIO_FAKE_DEV, kAudioObjectPropertyScopeInput) == 3);
  CHECK(rs_audio_channels(RS_AUDIO_FAKE_DEV, kAudioObjectPropertyScopeOutput) == 0);
  CHECK(!rs_audio_is_cable(RS_AUDIO_FAKE_DEV));
  AudioObjectPropertyAddress a = {kAudioDevicePropertyDeviceUID, kAudioObjectPropertyScopeGlobal,
                                  kAudioObjectPropertyElementMain};
  CFStringRef uid = NULL;
  UInt32 sz = sizeof uid;
  CHECK(rs_audio_hal_get(RS_AUDIO_FAKE_DEV, &a, 0, NULL, &sz, &uid) == noErr && uid);
  a.mSelector = kAudioHardwarePropertyTranslateUIDToDevice;
  AudioObjectID found = 0;
  sz = sizeof found;
  CHECK(rs_audio_hal_get(kAudioObjectSystemObject, &a, sizeof uid, &uid, &sz, &found) == noErr &&
        found == RS_AUDIO_FAKE_DEV);
  CFRelease(uid);
  // an entry per channel in the list of inputs, as an interface, before the built-in ones
  rs_audio_entry es[128];
  int m = rs_audio_input_entries(devs, n, 64, es, 128), first = -1, mine = 0;
  for (int i = 0; i < m; i++)
    if (es[i].dev == RS_AUDIO_FAKE_DEV) {
      if (first < 0) first = i;
      CHECK(es[i].channel == mine);
      mine++;
    }
  CHECK(mine == 3 && first >= 0);
  for (int i = 0; i < first; i++) CHECK(rs_audio_is_cable(es[i].dev) || rs_audio_has_output(es[i].dev));
  // rates and the I/O buffer as the game sets them
  a.mSelector = kAudioDevicePropertyNominalSampleRate;
  Float64 rate = 44100;
  CHECK(rs_audio_hal_set(RS_AUDIO_FAKE_DEV, &a, 0, NULL, sizeof rate, &rate) == noErr);
  rate = 0;
  CHECK(rs_audio_dev_get(RS_AUDIO_FAKE_DEV, kAudioDevicePropertyNominalSampleRate, &rate, sizeof rate) && rate == 44100);
  rate = 22050;
  CHECK(rs_audio_hal_set(RS_AUDIO_FAKE_DEV, &a, 0, NULL, sizeof rate, &rate) != noErr);
  rate = 48000;
  CHECK(rs_audio_hal_set(RS_AUDIO_FAKE_DEV, &a, 0, NULL, sizeof rate, &rate) == noErr);
  AudioValueRange range;
  CHECK(rs_audio_dev_get(RS_AUDIO_FAKE_DEV, kAudioDevicePropertyBufferFrameSizeRange, &range, sizeof range) &&
        range.mMinimum == 32 && range.mMaximum == 4096);
  CHECK(rs_audio_set_device_buffer(RS_AUDIO_FAKE_DEV, 128, "test") == 128);
  // its stream: float, its channels, the rate set
  a = (AudioObjectPropertyAddress){kAudioDevicePropertyStreams, kAudioObjectPropertyScopeInput,
                                   kAudioObjectPropertyElementMain};
  AudioStreamID st[4];
  sz = sizeof st;
  CHECK(rs_audio_hal_get(RS_AUDIO_FAKE_DEV, &a, 0, NULL, &sz, st) == noErr && sz == 4 && st[0] == RS_AUDIO_FAKE_STREAM);
  a.mScope = kAudioObjectPropertyScopeOutput;
  sz = sizeof st;
  CHECK(rs_audio_hal_get(RS_AUDIO_FAKE_DEV, &a, 0, NULL, &sz, st) == noErr && sz == 0);
  a = (AudioObjectPropertyAddress){kAudioStreamPropertyPhysicalFormat, kAudioObjectPropertyScopeGlobal,
                                   kAudioObjectPropertyElementMain};
  AudioStreamBasicDescription f;
  sz = sizeof f;
  CHECK(rs_audio_hal_get(RS_AUDIO_FAKE_STREAM, &a, 0, NULL, &sz, &f) == noErr && f.mFormatID == kAudioFormatLinearPCM &&
        f.mChannelsPerFrame == 3 && f.mSampleRate == 48000 && f.mBitsPerChannel == 32);
  a.mScope = kAudioObjectPropertyScopeInput;
  sz = sizeof f;
  CHECK(rs_audio_hal_get(RS_AUDIO_FAKE_DEV, &a, 0, NULL, &sz, &f) == noErr && f.mChannelsPerFrame == 3);
  a.mSelector = kAudioDevicePropertyVolumeScalar;
  CHECK(!rs_audio_hal_has(RS_AUDIO_FAKE_DEV, &a));
  CHECK(rs_audio_hal_listen(RS_AUDIO_FAKE_DEV, &a, NULL, NULL, 1) == noErr);

  // I/O: the function's address is its id the first time; another registration has one of its own
  AudioDeviceIOProcID id = NULL, other = NULL;
  CHECK(rs_audio_hal_io_create(RS_AUDIO_FAKE_DEV, io_proc, &g_calls, &id) == noErr && id == io_proc);
  CHECK(rs_audio_hal_io_create(RS_AUDIO_FAKE_DEV, io_proc, &g_calls, &other) == noErr && other && other != id);
  CHECK(rs_audio_hal_io_destroy(RS_AUDIO_FAKE_DEV, other) == noErr);
  CHECK(rs_audio_hal_start(RS_AUDIO_FAKE_DEV, (AudioDeviceIOProcID)(void *)&g_bad) != noErr);
  CHECK(rs_audio_hal_start(RS_AUDIO_FAKE_DEV, NULL) == noErr);
  UInt32 running = 9;
  CHECK(rs_audio_dev_get(RS_AUDIO_FAKE_DEV, kAudioDevicePropertyDeviceIsRunning, &running, sizeof running) && !running);
  double t0 = now_s();
  CHECK(rs_audio_hal_start(RS_AUDIO_FAKE_DEV, id) == noErr);
  CHECK(rs_audio_dev_get(RS_AUDIO_FAKE_DEV, kAudioDevicePropertyDeviceIsRunning, &running, sizeof running) && running);
  usleep(600000);
  CHECK(rs_audio_hal_stop(RS_AUDIO_FAKE_DEV, id) == noErr);
  double secs = now_s() - t0;
  uint32_t calls = atomic_load(&g_calls), frames = atomic_load(&g_frames);
  printf("device: %u calls, %u frames in %.3f s (%.0f frames/s); peaks %.3f %.3f %.3f\n", calls, frames, secs,
         frames / secs, peak_of(0), peak_of(1), peak_of(2));
  // in real time at 48 kHz, 128 frames a call
  CHECK(frames == calls * 128);
  CHECK(frames > 0.85 * 48000 * secs && frames < 1.05 * 48000 * secs);
  CHECK(!atomic_load(&g_bad));
  // the tone at -6 dB, silence, and the file's 0.25 at -6 dB
  CHECK(NEAR(peak_of(0), RS_AUDIO_FAKE_PEAK * 0.501, 0.01));
  CHECK(peak_of(1) == 0);
  CHECK(NEAR(peak_of(2), 0.25 * 0.501, 0.002));
  // stopped is stopped
  usleep(50000);
  CHECK(atomic_load(&g_calls) == calls);
  // again, at 44.1 kHz and another buffer
  rate = 44100;
  a = (AudioObjectPropertyAddress){kAudioDevicePropertyNominalSampleRate, kAudioObjectPropertyScopeGlobal,
                                   kAudioObjectPropertyElementMain};
  CHECK(rs_audio_hal_set(RS_AUDIO_FAKE_DEV, &a, 0, NULL, sizeof rate, &rate) == noErr);
  CHECK(rs_audio_set_device_buffer(RS_AUDIO_FAKE_DEV, 64, "test") == 64);
  atomic_store(&g_frames, 0);
  atomic_store(&g_calls, 0);
  t0 = now_s();
  CHECK(rs_audio_hal_start(RS_AUDIO_FAKE_DEV, id) == noErr);
  usleep(300000);
  CHECK(rs_audio_hal_io_destroy(RS_AUDIO_FAKE_DEV, id) == noErr);  // stops it too
  secs = now_s() - t0;
  frames = atomic_load(&g_frames);
  printf("device: at 44.1 kHz, %u frames in %.3f s (%.0f frames/s)\n", frames, secs, frames / secs);
  CHECK(frames == atomic_load(&g_calls) * 64);
  CHECK(frames > 0.85 * 44100 * secs && frames < 1.05 * 44100 * secs);
  CHECK(rs_audio_hal_start(RS_AUDIO_FAKE_DEV, id) != noErr);
  CHECK(!atomic_load(&g_bad));
  printf("device: ok\n");
}

int main(int argc, char **argv) {
  if (argc > 1 && !strcmp(argv[1], "--off")) {
    unsetenv("RS_AUDIO_FAKE_INPUT");
    test_off();
    printf("ok\n");
    return 0;
  }
  const char *tmp = getenv("TMPDIR");
  char dir[1024];
  snprintf(dir, sizeof dir, "%s", tmp && *tmp ? tmp : "/tmp");
  test_parse();
  test_tone();
  test_wav(dir);
  // the device: a tone, silence and a file of 0.25 (the last one test_wav wrote), at -6 dB
  char sw[1200];
  snprintf(sw, sizeof sw, "tone:57,silence,%s/rs_audio_fake_test.wav", dir);
  setenv("RS_AUDIO_FAKE_INPUT", sw, 1);
  setenv("RS_AUDIO_FAKE_GAIN_DB", "-6", 1);
  test_device();
  printf("ok\n");
  return 0;
}
