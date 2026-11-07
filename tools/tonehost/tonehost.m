// rs-tonehost: plays the guitar through an Audio Unit in place of the game's tones. A process of its own, so a plug-in's
// licence check, window or crash stays out of the game. The mod (tonehost_mod.c) reports the gear of each tone as the
// game applies it, about two seconds ahead of the change in the songs tried. Two instances take turns: one plays, the
// other gets the next tone's saved state, and a crossfade joins them, because a load is audible and an instance
// standing by is not. Nothing here knows a plug-in: a template is a saved state, parameter values and a gain.
//
//   rs-tonehost --config FILE     FILE is JSON: see README.md
//
// Commands, one per line, on the FIFO named by "fifo" in the config:
//   gear KEY=VALUE ...            a tone's knobs as the game sets them (the mod writes this)
//   use TEMPLATE                  play a template of the config by hand
//   mute                          fade everything out
//   quit
#import <Foundation/Foundation.h>
#import <AudioToolbox/AudioToolbox.h>
#import <CoreAudio/CoreAudio.h>
#include <math.h>
#include <errno.h>
#include <float.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define SLOTS 2
#define MAX_FRAMES 1024
#define RING 8192  // input samples, a power of two
#define RATE 48000.0

typedef struct {
  AudioUnit unit;
  _Atomic int ready;     // the audio thread renders it
  _Atomic float gain;    // linear
  _Atomic int probing;   // hears the reference pluck in place of the guitar
  _Atomic uint32_t probe_count;
  double probe_sum;      // audio thread: output power while probing
  uint32_t probe_pos;
  double sample_time;
  float left[MAX_FRAMES], right[MAX_FRAMES];
} slot_t;

static slot_t slots[SLOTS];
static AudioComponentDescription g_plugin;
static float g_ring[RING];
static _Atomic uint64_t g_ring_write;
static uint64_t g_ring_read;
static int g_channel;               // input channel, from 0
static float g_input_gain = 1.f;
static const float *g_block;        // the input of the block being rendered
static _Atomic int g_target = -1;   // the slot asked for; -1 none
static int g_current = -1;          // audio thread: the slot playing
static int g_previous = -1;         // audio thread: the slot fading out
static uint32_t g_fade_left, g_fade_frames = 960;  // 20 ms
static _Atomic uint32_t g_late_blocks, g_empty_blocks, g_peak_in, g_peak_out;  // peaks: float bits
static uint32_t g_target_fill = 128;
static _Atomic int g_quit;
static _Atomic float g_system_gain = 1.f, g_mixer_gain = 1.f;  // the system's volume, and the game's guitar mixer
static float g_master = 1.f;        // audio thread: both, smoothed
static float g_probe_peak = 0.25f;
static _Atomic uint64_t g_last_heard;  // when the mod last wrote, in seconds; 0: not yet
static _Atomic int g_standby = -1;  // the slot being made ready; it renders though nobody hears it

// A gate on the input, ahead of the plug-in: a high-gain amp makes a loud hiss of a quiet string or a hum. How much noise
// there is depends on the guitar, so it learns that: the floor is the quietest 100 ms lately, and the gate opens a margin
// above it. How tight it is depends on the tone: a clean tone keeps its note tails, a high-gain one stops between riffs.
static int g_gate_on;
static float g_gate_env, g_gate_gain = 1.f, g_gate_attack, g_floor_db = -70.f, g_window_peak;
static float g_gate_margin = 12.f, g_gate_tight_db = 8.f, g_gate_min = -72.f, g_gate_max = -40.f;
static uint32_t g_gate_hold_left, g_window_left;
static int g_gate_is_open;
static float g_hold_loose = 150.f, g_hold_tight = 40.f, g_release_loose = 120.f, g_release_tight = 30.f;
static _Atomic float g_tightness, g_gate_at_least = -200.f;  // of the tone: 0 to 1, and the game's own gate, if it has one
static _Atomic float g_open_db = -60.f;

static void say(const char *format, ...) {
  va_list a;
  va_start(a, format);
  fputs("[tonehost] ", stderr);
  vfprintf(stderr, format, a);
  fputc('\n', stderr);
  va_end(a);
}

// ---- Devices ----
static AudioObjectID find_device(NSString *name) {
  AudioObjectPropertyAddress a = {kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
  UInt32 size = 0;
  if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &a, 0, NULL, &size)) return 0;
  AudioObjectID ids[size / sizeof(AudioObjectID)];
  if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &a, 0, NULL, &size, ids)) return 0;
  for (UInt32 i = 0; i < size / sizeof(AudioObjectID); i++) {
    AudioObjectPropertyAddress n = {kAudioObjectPropertyName, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
    CFStringRef s = NULL;
    UInt32 ss = sizeof s;
    if (AudioObjectGetPropertyData(ids[i], &n, 0, NULL, &ss, &s) || !s) continue;
    BOOL same = [[(__bridge NSString *)s stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet] isEqualToString:name];  // some end in a space
    CFRelease(s);
    if (same) return ids[i];
  }
  return 0;
}

static AudioObjectID default_output(void) {
  AudioObjectPropertyAddress a = {kAudioHardwarePropertyDefaultOutputDevice, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
  AudioObjectID id = 0;
  UInt32 size = sizeof id;
  AudioObjectGetPropertyData(kAudioObjectSystemObject, &a, 0, NULL, &size, &id);
  return id;
}

static void set_buffer(AudioObjectID dev, UInt32 frames) {
  AudioObjectPropertyAddress a = {kAudioDevicePropertyBufferFrameSize, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
  AudioObjectSetPropertyData(dev, &a, 0, NULL, sizeof frames, &frames);
  UInt32 got = 0, size = sizeof got;
  AudioObjectGetPropertyData(dev, &a, 0, NULL, &size, &got);
  say("device %u: buffer %u frames", dev, got);
}

static void set_rate(AudioObjectID dev) {
  AudioObjectPropertyAddress a = {kAudioDevicePropertyNominalSampleRate, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
  Float64 r = RATE;
  AudioObjectSetPropertyData(dev, &a, 0, NULL, sizeof r, &r);  // the game asks for the same
}

static double device_rate(AudioObjectID dev) {
  AudioObjectPropertyAddress a = {kAudioDevicePropertyNominalSampleRate, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
  Float64 r = 0;
  UInt32 size = sizeof r;
  AudioObjectGetPropertyData(dev, &a, 0, NULL, &size, &r);
  return r;
}

// ---- Audio ----
static OSStatus input_proc(AudioObjectID dev, const AudioTimeStamp *now, const AudioBufferList *in, const AudioTimeStamp *in_time,
                           AudioBufferList *out, const AudioTimeStamp *out_time, void *user) {
  // Finds the buffer that holds the channel: a device may give one interleaved buffer or several
  int base = 0;
  for (UInt32 b = 0; b < in->mNumberBuffers; b++) {
    const AudioBuffer *buf = &in->mBuffers[b];
    int ch = (int)buf->mNumberChannels;
    if (g_channel < base + ch) {
      const float *s = (const float *)buf->mData + (g_channel - base);
      uint32_t frames = buf->mDataByteSize / (uint32_t)(ch * sizeof(float));
      uint64_t w = atomic_load_explicit(&g_ring_write, memory_order_relaxed);
      for (uint32_t i = 0; i < frames; i++) g_ring[w++ & (RING - 1)] = s[i * ch] * g_input_gain;
      atomic_store_explicit(&g_ring_write, w, memory_order_release);
      break;
    }
    base += ch;
  }
  return noErr;
}

// What a tone's loudness is measured with: a low plucked string every 250 ms, heard by nobody
static void probe_signal(slot_t *s, float *x, UInt32 n) {
  for (UInt32 i = 0; i < n; i++) {
    uint32_t p = s->probe_pos++ % 12000;
    double t = p / RATE, w = 2 * M_PI * 110.0 * t;
    x[i] = (float)(g_probe_peak * exp(-t * 9.0) * (sin(w) + 0.5 * sin(2 * w) + 0.3 * sin(3 * w) + 0.2 * sin(5 * w)) / 1.6);
  }
}

static OSStatus plugin_input(void *ref, AudioUnitRenderActionFlags *flags, const AudioTimeStamp *t, UInt32 bus, UInt32 n, AudioBufferList *io) {
  slot_t *s = ref;
  if (atomic_load_explicit(&s->probing, memory_order_relaxed)) {
    probe_signal(s, io->mBuffers[0].mData, n);
    for (UInt32 c = 1; c < io->mNumberBuffers; c++) memcpy(io->mBuffers[c].mData, io->mBuffers[0].mData, n * sizeof(float));
  } else
    for (UInt32 c = 0; c < io->mNumberBuffers; c++) memcpy(io->mBuffers[c].mData, g_block, n * sizeof(float));
  return noErr;
}

static void render_slot(slot_t *s, UInt32 n) {
  struct { UInt32 count; AudioBuffer b[2]; } list = {2, {{1, n * 4, s->left}, {1, n * 4, s->right}}};
  AudioTimeStamp ts = {0};
  ts.mFlags = kAudioTimeStampSampleTimeValid;
  ts.mSampleTime = s->sample_time;
  s->sample_time += n;
  AudioUnitRenderActionFlags flags = 0;
  AudioUnitRender(s->unit, &flags, &ts, 0, n, (AudioBufferList *)&list);
  if (atomic_load_explicit(&s->probing, memory_order_relaxed)) {
    for (UInt32 i = 0; i < n; i++) s->probe_sum += (double)s->left[i] * s->left[i];
    atomic_fetch_add_explicit(&s->probe_count, n, memory_order_relaxed);
  }
}

static void gate(float *x, UInt32 n) {
  if (!g_gate_on) return;
  float tight = atomic_load_explicit(&g_tightness, memory_order_relaxed);
  float open_db = fminf(fmaxf(g_floor_db + g_gate_margin + tight * g_gate_tight_db, g_gate_min), g_gate_max);
  open_db = fmaxf(open_db, atomic_load_explicit(&g_gate_at_least, memory_order_relaxed));
  atomic_store_explicit(&g_open_db, open_db, memory_order_relaxed);
  float open = powf(10.f, open_db / 20.f), close = open * 0.5f;
  uint32_t hold = (uint32_t)((g_hold_loose + (g_hold_tight - g_hold_loose) * tight) * RATE / 1000);
  float release = 1.f - expf(-1000.f / ((g_release_loose + (g_release_tight - g_release_loose) * tight) * (float)RATE));
  for (UInt32 i = 0; i < n; i++) {
    float a = fabsf(x[i]);
    if (a > g_window_peak) g_window_peak = a;
    if (!g_window_left--) {  // 100 ms done: the floor falls at once and rises quickly, but not under a note
      float db = 20.f * log10f(g_window_peak + 1e-9f);
      if (db < g_floor_db) g_floor_db = fmaxf(db, -85.f);
      else if (db < open_db + 6.f) g_floor_db = fminf(fminf(g_floor_db + 0.3f, db), -45.f);  // no note: 3 dB a second
      g_window_peak = 0;
      g_window_left = (uint32_t)(RATE / 10);
    }
    g_gate_env = a > g_gate_env ? a : g_gate_env * 0.9995f;
    if (g_gate_env > open) { g_gate_is_open = 1; g_gate_hold_left = hold; }
    else if (g_gate_env < close) {
      if (g_gate_hold_left) g_gate_hold_left--;
      else g_gate_is_open = 0;
    }
    g_gate_gain += ((float)g_gate_is_open - g_gate_gain) * (g_gate_is_open ? g_gate_attack : release);
    x[i] *= g_gate_gain;
  }
}

static void note_peak(_Atomic uint32_t *peak, const float *x, UInt32 n, int stride) {
  float m = 0;
  for (UInt32 i = 0; i < n; i++) { float a = fabsf(x[i * stride]); if (a > m) m = a; }
  uint32_t bits, old = atomic_load_explicit(peak, memory_order_relaxed);
  memcpy(&bits, &m, 4);
  if (bits > old) atomic_store_explicit(peak, bits, memory_order_relaxed);  // positive floats order as integers
}

static OSStatus output_proc(AudioObjectID dev, const AudioTimeStamp *now, const AudioBufferList *in, const AudioTimeStamp *in_time,
                            AudioBufferList *out, const AudioTimeStamp *out_time, void *user) {
  AudioBuffer *ob = &out->mBuffers[0];
  int och = (int)ob->mNumberChannels;
  UInt32 n = ob->mDataByteSize / (UInt32)(och * sizeof(float));
  if (n > MAX_FRAMES) n = MAX_FRAMES;
  for (UInt32 b = 0; b < out->mNumberBuffers; b++) memset(out->mBuffers[b].mData, 0, out->mBuffers[b].mDataByteSize);
  // The input, kept just ahead of the output: clocks of two devices drift apart
  static float block[MAX_FRAMES];
  uint64_t w = atomic_load_explicit(&g_ring_write, memory_order_acquire);
  uint64_t fill = w - g_ring_read;
  if (fill > g_target_fill + n + 256) { g_ring_read = w - g_target_fill - n; fill = w - g_ring_read; atomic_fetch_add(&g_late_blocks, 1); }
  if (fill < n) { memset(block, 0, sizeof block); atomic_fetch_add(&g_empty_blocks, 1); }
  else for (UInt32 i = 0; i < n; i++) block[i] = g_ring[g_ring_read++ & (RING - 1)];
  note_peak(&g_peak_in, block, n, 1);
  gate(block, n);
  g_block = block;

  int want = atomic_load_explicit(&g_target, memory_order_acquire);
  if (want != g_current) { g_previous = g_current; g_current = want; g_fade_left = g_fade_frames; }
  for (int i = 0; i < SLOTS; i++)
    if (atomic_load_explicit(&slots[i].ready, memory_order_acquire) &&
        (i == g_current || (i == g_previous && g_fade_left) || i == atomic_load_explicit(&g_standby, memory_order_relaxed)))
      render_slot(&slots[i], n);

  float *dst = ob->mData;
  float *second = out->mNumberBuffers > 1 && och == 1 ? out->mBuffers[1].mData : NULL;  // a buffer for each channel
  float gc = g_current >= 0 ? atomic_load_explicit(&slots[g_current].gain, memory_order_relaxed) : 0.f;
  float gp = g_previous >= 0 ? atomic_load_explicit(&slots[g_previous].gain, memory_order_relaxed) : 0.f;
  float want_master = atomic_load_explicit(&g_system_gain, memory_order_relaxed) * atomic_load_explicit(&g_mixer_gain, memory_order_relaxed);
  for (UInt32 i = 0; i < n; i++) {
    float l = 0, r = 0;
    double t = g_fade_left ? 1.0 - (double)g_fade_left / g_fade_frames : 1.0;
    if (g_fade_left) g_fade_left--;
    g_master += (want_master - g_master) * 0.002f;
    if (g_current >= 0) { float a = (float)sin(t * M_PI_2) * gc * g_master; l += a * slots[g_current].left[i]; r += a * slots[g_current].right[i]; }
    if (g_previous >= 0 && t < 1.0) { float a = (float)cos(t * M_PI_2) * gp * g_master; l += a * slots[g_previous].left[i]; r += a * slots[g_previous].right[i]; }
    dst[i * och] = l;
    if (och > 1) dst[i * och + 1] = r;
    if (second) second[i] = r;
  }
  note_peak(&g_peak_out, dst, n, och);
  return noErr;
}

// Input and output on one device: one callback, one clock, and nothing held between them
static OSStatus duplex_proc(AudioObjectID dev, const AudioTimeStamp *now, const AudioBufferList *in, const AudioTimeStamp *in_time,
                            AudioBufferList *out, const AudioTimeStamp *out_time, void *user) {
  input_proc(dev, now, in, in_time, out, out_time, user);
  return output_proc(dev, now, in, in_time, out, out_time, user);
}

static AudioDeviceIOProc g_proc = output_proc;

static UInt32 device_frames(AudioObjectID dev, UInt32 selector, UInt32 scope) {
  AudioObjectPropertyAddress a = {selector, scope, kAudioObjectPropertyElementMain};
  UInt32 v = 0, size = sizeof v;
  AudioObjectGetPropertyData(dev, &a, 0, NULL, &size, &v);
  return v;
}

// With no "output" in the config the sound follows the system's output, as the game does.
static AudioObjectID g_out_dev;
static AudioDeviceIOProcID g_out_id;
static UInt32 g_frames;

static void start_output(AudioObjectID dev) {
  if (g_out_dev) { AudioDeviceStop(g_out_dev, g_out_id); AudioDeviceDestroyIOProcID(g_out_dev, g_out_id); g_out_id = NULL; }
  g_out_dev = dev;
  set_rate(dev);
  set_buffer(dev, g_frames);
  if (AudioDeviceCreateIOProcID(dev, g_proc, NULL, &g_out_id)) { say("device %u: no output", dev); return; }
  AudioDeviceStart(dev, g_out_id);
  say("output on device %u", dev);
}

static void follow_output(void) {
  AudioObjectPropertyAddress a = {kAudioHardwarePropertyDefaultOutputDevice, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
  AudioObjectAddPropertyListenerBlock(kAudioObjectSystemObject, &a, dispatch_queue_create("output", NULL),
    ^(UInt32 count, const AudioObjectPropertyAddress *addresses) {
      AudioObjectID dev = default_output();
      if (dev && dev != g_out_dev) start_output(dev);
    });
}

// The volume of the system's output, as a gain: a device with no volume of its own counts as full
static float system_gain(void) {
  AudioObjectID dev = default_output();
  for (UInt32 element = 0; element < 3; element++) {
    AudioObjectPropertyAddress a = {kAudioDevicePropertyVolumeDecibels, kAudioObjectPropertyScopeOutput, element};
    Float32 db = 0, scalar = 1;
    UInt32 size = sizeof db;
    if (AudioObjectGetPropertyData(dev, &a, 0, NULL, &size, &db)) continue;
    a.mSelector = kAudioDevicePropertyVolumeScalar;
    AudioObjectGetPropertyData(dev, &a, 0, NULL, &size, &scalar);
    return scalar <= 0 ? 0.f : powf(10.f, db / 20.f);
  }
  return 1.f;
}

// ---- Plug-in instances ----
static int make_slot(slot_t *s) {
  AudioComponent c = AudioComponentFindNext(NULL, &g_plugin);
  if (!c || AudioComponentInstanceNew(c, &s->unit)) return -1;
  AudioStreamBasicDescription f = {RATE, kAudioFormatLinearPCM, kAudioFormatFlagsNativeFloatPacked | kAudioFormatFlagIsNonInterleaved, 4, 1, 4, 2, 32, 0};
  AudioUnitSetProperty(s->unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &f, sizeof f);
  AudioUnitSetProperty(s->unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &f, sizeof f);
  UInt32 max = MAX_FRAMES;
  AudioUnitSetProperty(s->unit, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &max, sizeof max);
  AURenderCallbackStruct cb = {plugin_input, s};
  AudioUnitSetProperty(s->unit, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &cb, sizeof cb);
  if (AudioUnitInitialize(s->unit)) return -1;
  s->gain = 1.f;
  return 0;
}

static void load_state(int n, const char *path) {
  slot_t *s = &slots[n];
  atomic_store(&s->ready, 0);
  usleep(30000);  // the audio thread is past any render of it
  NSData *data = [NSData dataWithContentsOfFile:[NSString stringWithUTF8String:path]];
  id plist = data ? [NSPropertyListSerialization propertyListWithData:data options:0 format:NULL error:NULL] : nil;
  if (!plist) { say("no state in %s", path); return; }
  CFPropertyListRef p = (__bridge CFPropertyListRef)plist;
  OSStatus st = AudioUnitSetProperty(s->unit, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &p, sizeof p);
  if (st) { say("slot %d: state refused (%d)", n, (int)st); return; }
  atomic_store(&s->ready, 1);
  say("slot %d: %s", n, path);
}

// ---- Tones ----
// A template of the config: a saved state, parameter values and an output gain. Which template a tone gets, and what
// of the game's knobs reaches which parameter, is the "map" of the config, so another plug-in needs no code.
static NSDictionary *g_templates, *g_map;
static NSString *g_dir, *g_loaded[SLOTS];
static int g_playing = -1;
static float g_target_db = -200.f;

static float number_of(NSDictionary *gear, NSString *suffix, NSString *prefix, NSString *contains) {
  float best = -FLT_MAX;
  for (NSString *k in gear)
    if ([k hasSuffix:suffix] && (!prefix || [k hasPrefix:prefix]) && (!contains || [k rangeOfString:contains].location != NSNotFound))
      best = fmaxf(best, [gear[k] floatValue]);
  return best;
}

// The game's gain knob of the amp (a Plexi has two), and whether a drive pedal is in the tone, 0 to 1
static float amp_gain(NSDictionary *gear) {
  float g = -1;
  for (NSString *k in gear)
    if ([k rangeOfString:@"Amp_"].location != NSNotFound)
      for (NSString *s in g_map[@"gain_knobs"])
        if ([k hasSuffix:s]) g = fmaxf(g, [gear[k] floatValue]);
  return g < 0 ? 0 : g / 100.f;
}

static NSString *pick(NSDictionary *gear) {
  for (NSString *k in gear)
    for (NSString *prefix in g_map[@"by_prefix"])
      if ([k hasPrefix:prefix]) return g_map[@"by_prefix"][prefix];
  float g = amp_gain(gear);
  for (NSString *k in gear)
    for (NSString *p in g_map[@"drive_pedals"])
      if ([k hasPrefix:p]) return g_map[@"driven"];
  return g >= [g_map[@"driven_from_gain"] floatValue] ? g_map[@"driven"] : g_map[@"clean"];
}

static void apply(int n, NSDictionary *t, NSDictionary *gear) {
  NSDictionary *fixed = t[@"params"];
  for (NSString *id in fixed) AudioUnitSetParameter(slots[n].unit, id.intValue, kAudioUnitScope_Global, 0, [fixed[id] floatValue], 0);
  // {"param": P, "from": "_Gain", "scale": 0.01, "offset": 0}: a knob of the game onto a parameter. The "map" has the
  // rules every template shares; a template can add its own.
  float gain = amp_gain(gear);
  for (NSArray *rules in @[g_map[@"follow"] ?: @[], t[@"follow"] ?: @[]])
    for (NSDictionary *f in rules) {
      if (f[@"present"]) {  // a switch that is on when the tone has one of this gear
        BOOL on = NO;
        for (NSString *k in gear)
          for (NSString *p in f[@"present"]) on = on || [k hasPrefix:p];
        AudioUnitSetParameter(slots[n].unit, [f[@"param"] intValue], kAudioUnitScope_Global, 0, on ? 1.f : 0.f, 0);
        continue;
      }
      BOOL has = NO;
      for (NSString *k in gear)
        for (NSString *p in f[@"with"] ?: f[@"without"]) has = has || [k hasPrefix:p];
      if ((f[@"with"] && !has) || (f[@"without"] && has)) continue;
      BOOL is_gain = [f[@"from"] isEqual:@"amp_gain"];
      float v = is_gain ? gain : number_of(gear, f[@"from"], f[@"prefix"], f[@"contains"]);
      if (v == -FLT_MAX) continue;
      AudioUnitSetParameter(slots[n].unit, [f[@"param"] intValue], kAudioUnitScope_Global, 0, v * [f[@"scale"] floatValue] + [f[@"offset"] floatValue], 0);
    }
}

// Plays the reference pluck through a slot nobody hears and returns the level it comes out at
static float measure(int n) {
  slot_t *s = &slots[n];
  atomic_store(&s->probing, 1);
  usleep(150000);  // parameters settle, compressors find their level
  atomic_store(&s->probe_count, 0);
  s->probe_sum = 0;
  usleep(250000);  // one pluck
  double sum = s->probe_sum;
  uint32_t count = atomic_load(&s->probe_count);
  atomic_store(&s->probing, 0);
  return count ? 10 * log10f((float)(sum / count) + 1e-12f) : -120.f;
}

static void use_template(NSString *name, NSDictionary *gear) {
  NSDictionary *t = g_templates[name];
  if (!t) { say("no template %s", name.UTF8String); return; }
  // Always the slot nobody hears, even for the same template: new knobs change the level, and that is measured first
  int n = g_playing == 0 ? 1 : 0;
  atomic_store(&g_standby, n);
  if (![g_loaded[n] isEqual:name]) {
    NSString *path = [t[@"state"] hasPrefix:@"/"] ? t[@"state"] : [g_dir stringByAppendingPathComponent:t[@"state"]];
    load_state(n, path.UTF8String);
    g_loaded[n] = name;
  }
  apply(n, t, gear);
  float level = measure(n), out = [t[@"output_db"] floatValue];
  if (g_target_db > -100) out = g_target_db + [t[@"trim_db"] floatValue] - level;
  atomic_store(&slots[n].gain, powf(10.f, out / 20.f));
  usleep(30000);  // the guitar reaches it before anyone hears it
  float game_gate = number_of(gear, @"_Thresh", @"Pedal_NoiseGate", nil);
  atomic_store(&g_gate_at_least, game_gate == -FLT_MAX ? -200.f : game_gate);
  atomic_store(&g_tightness, fminf(amp_gain(gear) * [t[@"tight"] ?: @1 floatValue], 1.f));
  atomic_store(&g_target, n);
  g_playing = n;
  usleep((useconds_t)(g_fade_frames / RATE * 1e6) + 50000);
  atomic_store(&g_standby, -1);
  say("play %s in slot %d: measured %.1f dB, output %+.1f dB", name.UTF8String, n, level, out);
}

static void command(char *line) {
  if (!strncmp(line, "gear ", 5)) {
    NSMutableDictionary *gear = [NSMutableDictionary dictionary];
    for (NSString *pair in [[NSString stringWithUTF8String:line + 5] componentsSeparatedByString:@" "]) {
      NSArray *kv = [pair componentsSeparatedByString:@"="];
      if (kv.count == 2) gear[kv[0]] = kv[1];
    }
    NSString *name = pick(gear);
    say("tone: gain %.2f, balance %s, %lu knobs -> %s", amp_gain(gear), [gear[@"Meter_Tone_Balance_Return"] UTF8String], (unsigned long)gear.count, name.UTF8String);
    use_template(name, gear);
  } else if (!strncmp(line, "mixer ", 6)) {  // the game's guitar fader, 0 to 100
    float v = strtof(line + 6, NULL);
    atomic_store(&g_mixer_gain, v / 100.f);
    say("game's guitar mixer %.0f", v);
  } else if (!strncmp(line, "use ", 4)) use_template([NSString stringWithUTF8String:line + 4], @{});
  else if (!strncmp(line, "mute", 4)) { atomic_store(&g_target, -1); g_playing = -1; }
  else if (!strncmp(line, "quit", 4)) atomic_store(&g_quit, 1);
  else if (*line) say("unknown command: %s", line);
}

static uint32_t fourcc(NSString *s) {
  const char *c = s.UTF8String;
  return (uint32_t)c[0] << 24 | (uint32_t)c[1] << 16 | (uint32_t)c[2] << 8 | (uint32_t)c[3];
}

int main(int argc, char **argv) { @autoreleasepool {
  if (argc < 3 || strcmp(argv[1], "--config")) { fprintf(stderr, "usage: rs-tonehost --config FILE\n"); return 2; }
  NSData *cfg = [NSData dataWithContentsOfFile:[NSString stringWithUTF8String:argv[2]]];
  NSDictionary *c = cfg ? [NSJSONSerialization JSONObjectWithData:cfg options:0 error:NULL] : nil;
  if (!c) { say("can't read the config"); return 2; }
  NSDictionary *pl = c[@"plugin"];
  g_plugin = (AudioComponentDescription){fourcc(pl[@"type"]), fourcc(pl[@"subtype"]), fourcc(pl[@"manufacturer"]), 0, 0};
  g_channel = [c[@"input"][@"channel"] intValue];
  g_input_gain = powf(10.f, [c[@"input"][@"gain_db"] floatValue] / 20.f);
  if (c[@"fade_ms"]) g_fade_frames = (uint32_t)([c[@"fade_ms"] doubleValue] * RATE / 1000);
  UInt32 frames = c[@"buffer"] ? [c[@"buffer"] unsignedIntValue] : 128;
  g_target_fill = frames;
  g_templates = c[@"templates"];
  g_map = c[@"map"];
  g_dir = [[NSString stringWithUTF8String:argv[2]] stringByDeletingLastPathComponent];
  if (c[@"target_db"]) g_target_db = [c[@"target_db"] floatValue];
  if (c[@"probe_peak_db"]) g_probe_peak = powf(10.f, [c[@"probe_peak_db"] floatValue] / 20.f);

  // First of all: the instances take seconds to open, and a write before the FIFO exists would make an ordinary file
  const char *fifo = [c[@"fifo"] UTF8String];
  unlink(fifo);
  if (mkfifo(fifo, 0600)) { say("no command FIFO: %s", strerror(errno)); return 1; }
  int fd = open(fifo, O_RDONLY | O_NONBLOCK);
  int keep = open(fifo, O_WRONLY);  // a writer of our own: without one a read ends when the mod's goes away
  if (fd < 0 || keep < 0) { say("no command FIFO"); return 1; }
  fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) & ~O_NONBLOCK);

  AudioObjectID in_dev = find_device(c[@"input"][@"device"]);
  AudioObjectID out_dev = c[@"output"][@"device"] ? find_device(c[@"output"][@"device"]) : default_output();
  if (!in_dev || !out_dev) { say("device not found (input %u, output %u)", in_dev, out_dev); return 1; }
  set_rate(in_dev);
  usleep(200000);
  if (device_rate(in_dev) != RATE) { say("the input must run at 48 kHz (%g)", device_rate(in_dev)); return 1; }
  for (int i = 0; i < SLOTS; i++)
    if (make_slot(&slots[i])) { say("the plug-in didn't open"); return 1; }
  say("%d instances open", SLOTS);
  set_buffer(in_dev, frames);

  NSDictionary *gt = c[@"gate"];
  if (gt) {
    g_gate_on = 1;
    g_gate_attack = 1.f - expf(-1.f / (0.0005f * (float)RATE));
    if (gt[@"margin_db"]) g_gate_margin = [gt[@"margin_db"] floatValue];
    if (gt[@"tight_db"]) g_gate_tight_db = [gt[@"tight_db"] floatValue];
    if (gt[@"min_db"]) g_gate_min = [gt[@"min_db"] floatValue];
    if (gt[@"max_db"]) g_gate_max = [gt[@"max_db"] floatValue];
    if (gt[@"hold_ms"]) { g_hold_loose = [gt[@"hold_ms"][0] floatValue]; g_hold_tight = [gt[@"hold_ms"][1] floatValue]; }
    if (gt[@"release_ms"]) { g_release_loose = [gt[@"release_ms"][0] floatValue]; g_release_tight = [gt[@"release_ms"][1] floatValue]; }
    say("gate on: %.0f dB above the guitar's noise", g_gate_margin);
  }
  AudioDeviceIOProcID in_id = NULL;
  BOOL duplex = in_dev == out_dev;
  if (duplex) { g_proc = duplex_proc; g_target_fill = 0; }
  else {
    if (AudioDeviceCreateIOProcID(in_dev, input_proc, NULL, &in_id)) { say("no input"); return 1; }
    AudioDeviceStart(in_dev, in_id);
  }
  g_frames = frames;
  start_output(out_dev);
  if (!c[@"output"][@"device"]) follow_output();
  UInt32 total = device_frames(in_dev, kAudioDevicePropertyLatency, kAudioObjectPropertyScopeInput) + device_frames(in_dev, kAudioDevicePropertySafetyOffset, kAudioObjectPropertyScopeInput) +
                 device_frames(out_dev, kAudioDevicePropertyLatency, kAudioObjectPropertyScopeOutput) + device_frames(out_dev, kAudioDevicePropertySafetyOffset, kAudioObjectPropertyScopeOutput) +
                 2 * frames + g_target_fill;
  say("%s, about %.1f ms in to out as the devices report it", duplex ? "one device" : "two devices", total / RATE * 1000);
  say("running");

  dispatch_source_t meter = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, dispatch_get_global_queue(QOS_CLASS_UTILITY, 0));
  dispatch_source_set_timer(meter, dispatch_time(DISPATCH_TIME_NOW, 0), 5 * NSEC_PER_SEC, NSEC_PER_SEC);
  dispatch_source_set_event_handler(meter, ^{
    uint32_t a = atomic_exchange(&g_peak_in, 0), b = atomic_exchange(&g_peak_out, 0);
    float i, o;
    memcpy(&i, &a, 4);
    memcpy(&o, &b, 4);
    say("peak in %.1f dB, out %.1f dB, floor %.1f dB, gate %.1f dB, late %u, empty %u", 20 * log10f(i + 1e-9f), 20 * log10f(o + 1e-9f), g_floor_db,
        atomic_load(&g_open_db), atomic_load(&g_late_blocks), atomic_load(&g_empty_blocks));
  });
  dispatch_resume(meter);
  dispatch_source_t volume = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, dispatch_get_global_queue(QOS_CLASS_UTILITY, 0));
  dispatch_source_set_timer(volume, dispatch_time(DISPATCH_TIME_NOW, 0), NSEC_PER_SEC / 10, NSEC_PER_SEC / 50);
  // A device has one buffer size and the last to set it wins: the game sets 128 when it opens its input, and with its
  // queue sized by then it takes ours, so ours is set again
  BOOL follow_volume = ![c[@"ignore_system_volume"] boolValue], quit_with_game = ![c[@"stay_after_game"] boolValue];
  __block int ticks = 0;
  dispatch_source_set_event_handler(volume, ^{
    if (follow_volume) atomic_store(&g_system_gain, system_gain());
    if (++ticks % 10) return;
    // The mod writes once a second: a silent mod means the game has gone, crashed or not
    uint64_t heard = atomic_load(&g_last_heard);
    if (quit_with_game && heard && (uint64_t)time(NULL) > heard + 5) {
      say("the game has gone: quitting");
      AudioDeviceStop(g_out_dev, g_out_id);
      _exit(0);
    }
    if (device_frames(g_out_dev, kAudioDevicePropertyBufferFrameSize, kAudioObjectPropertyScopeGlobal) != frames) {
      say("the buffer changed: setting it back");
      set_buffer(g_out_dev, frames);
    }
  });
  dispatch_resume(volume);
  static char line[16384];
  size_t have = 0;
  while (!atomic_load(&g_quit)) {
    ssize_t got = read(fd, line + have, sizeof line - 1 - have);
    if (got < 0 && errno == EINTR) continue;
    if (got <= 0) { say("command FIFO: %s", got ? strerror(errno) : "closed"); break; }
    have += (size_t)got;
    atomic_store(&g_last_heard, (uint64_t)time(NULL));
    char *start = line, *end;
    while ((end = memchr(start, '\n', line + have - start))) {
      *end = 0;
      @autoreleasepool { command(start); }
      start = end + 1;
    }
    have -= (size_t)(start - line);
    memmove(line, start, have);
    if (have == sizeof line - 1) have = 0;  // no line is this long
  }
  AudioDeviceStop(g_out_dev, g_out_id);
  if (in_id) AudioDeviceStop(in_dev, in_id);
  say("late blocks %u, empty blocks %u", atomic_load(&g_late_blocks), atomic_load(&g_empty_blocks));
  return 0;
}}
