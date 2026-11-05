// The guitar input's channel and gain (rs_audio_input.h), for the Mac build's input hook and Learn &
// Play's capture stream (win-x86/shims/audio/wa_engine.c).
#include "rs_audio_input.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <xlocale.h>

#include "rs_audio_host.h"

#define MAX_FILE (64 * 1024)

// ------------------------------------------------------------------------------------------------
// Settings
// ------------------------------------------------------------------------------------------------
static int parse_channel(const char *v, int *out) {
  char *end;
  long n = strtol(v, &end, 10);
  if (end == v || *end || n < 1 || n > RS_AUDIO_INPUT_MAX_CHANNEL) return 0;
  *out = (int)n - 1;
  return 1;
}

// The C locale's number format, whatever the game's locale.
static int parse_gain(const char *v, float *out) {
  char *end;
  float db = strtof_l(v, &end, NULL);
  if (end == v || *end || !isfinite(db)) return 0;
  if (db < RS_AUDIO_INPUT_MIN_DB) db = RS_AUDIO_INPUT_MIN_DB;
  if (db > RS_AUDIO_INPUT_MAX_DB) db = RS_AUDIO_INPUT_MAX_DB;
  *out = powf(10.0f, db / 20.0f);
  return 1;
}

// s[0..n) without the spaces around it; returns the new length.
static size_t trim(const char **s, size_t n) {
  while (n && isspace((unsigned char)**s)) (*s)++, n--;
  while (n && isspace((unsigned char)(*s)[n - 1])) n--;
  return n;
}

static int same_name(const char *a, size_t na, const char *name) {
  size_t nb = trim(&name, strlen(name));
  return na == nb && strncasecmp(a, name, na) == 0;
}

static void parse_key(const char *key, size_t nk, const char *val, size_t nv, rs_audio_input *out) {
  char v[32];
  int ok = 0;
  if (nv < sizeof v) {
    memcpy(v, val, nv);
    v[nv] = 0;
    if (nk == 7 && strncasecmp(key, "Channel", 7) == 0) ok = parse_channel(v, &out->channel);
    else if (nk == 6 && strncasecmp(key, "GainDb", 6) == 0) ok = parse_gain(v, &out->gain);
  }
  if (!ok) rs_audio_log("guitar input: left out \"%.*s=%.*s\"", (int)nk, key, (int)nv, val);
}

void rs_audio_input_parse(const char *text, const char *name, rs_audio_input *out) {
  out->channel = -1;
  out->gain = 1.0f;
  if (!text) return;
  if (strncmp(text, "\xef\xbb\xbf", 3) == 0) text += 3;  // a byte-order mark, as some editors save
  int mine = 0;
  while (*text) {
    const char *eol = strchr(text, '\n');
    size_t len = eol ? (size_t)(eol - text) : strlen(text);
    const char *s = text;
    size_t n = trim(&s, len);
    text += len + (eol ? 1 : 0);
    if (!n || *s == '#' || *s == ';') continue;
    if (*s == '[' && s[n - 1] == ']') {
      const char *sec = s + 1;
      size_t ns = trim(&sec, n - 2);
      mine = same_name(sec, ns, name);
      continue;
    }
    if (!mine) continue;
    const char *eq = memchr(s, '=', n);
    if (!eq) {
      rs_audio_log("guitar input: left out \"%.*s\"", (int)n, s);
      continue;
    }
    const char *key = s, *val = eq + 1;
    size_t nk = trim(&key, (size_t)(eq - s));
    size_t nv = trim(&val, n - (size_t)(val - s));
    parse_key(key, nk, val, nv, out);
  }
}

static char *read_file(const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f) return NULL;
  char *buf = malloc(MAX_FILE + 1);
  size_t n = buf ? fread(buf, 1, MAX_FILE, f) : 0;
  fclose(f);
  if (buf) buf[n] = 0;
  return buf;
}

void rs_audio_input_for(const char *name, rs_audio_input *out) {
  if (!rs_audio_env_on("RS_AUDIO_INPUT", 1)) {
    rs_audio_input_parse(NULL, name, out);
    return;
  }
  char home[1024];
  const char *path = getenv("RS_AUDIO_INPUT_FILE"), *h = getenv("HOME");
  if ((!path || !*path) && h) {
    snprintf(home, sizeof home, "%s/Library/Application Support/RS Re-ARMed/guitar-input.ini", h);
    path = home;
  }
  char *text = path && *path ? read_file(path) : NULL;
  rs_audio_input_parse(text, name, out);
  free(text);
  const char *v = getenv("RS_AUDIO_INPUT_CHANNEL");
  if (v && *v && !parse_channel(v, &out->channel)) rs_audio_log("guitar input: left out RS_AUDIO_INPUT_CHANNEL=%s", v);
  v = getenv("RS_AUDIO_INPUT_GAIN_DB");
  if (v && *v && !parse_gain(v, &out->gain)) rs_audio_log("guitar input: left out RS_AUDIO_INPUT_GAIN_DB=%s", v);
  if (out->channel >= 0 || out->gain != 1.0f)
    rs_audio_log("guitar input \"%s\": channel %d, gain %.1f dB", name, out->channel + 1, 20.0 * log10(out->gain));
}

int rs_audio_input_channel(const rs_audio_input *in, uint32_t channels, const char *name) {
  if (in->channel < 0) return -1;
  if ((uint32_t)in->channel < channels) return in->channel;
  rs_audio_log("guitar input \"%s\": it has %u channel(s), not %d; using the first", name, channels, in->channel + 1);
  return 0;
}

// ------------------------------------------------------------------------------------------------
// Audio
// ------------------------------------------------------------------------------------------------
static uint32_t float_bits(float f) {
  uint32_t u;
  memcpy(&u, &f, sizeof u);
  return u;
}

void rs_audio_input_note(rs_audio_input_stats *st, float peak, uint32_t clips, uint32_t frames) {
  // Positive floats order as their bits do.
  uint32_t bits = float_bits(peak), was = atomic_load_explicit(&st->peak, memory_order_relaxed);
  while (bits > was &&
         !atomic_compare_exchange_weak_explicit(&st->peak, &was, bits, memory_order_relaxed, memory_order_relaxed)) {
  }
  if (clips) atomic_fetch_add_explicit(&st->clips, clips, memory_order_relaxed);
  atomic_fetch_add_explicit(&st->frames, frames, memory_order_relaxed);
}

uint32_t rs_audio_input_take(const AudioBufferList *list, uint32_t channel, float gain, float *out,
                             uint32_t max_frames, rs_audio_input_stats *st) {
  uint32_t first = 0;
  for (UInt32 b = 0; b < list->mNumberBuffers; b++) {
    const AudioBuffer *buf = &list->mBuffers[b];
    uint32_t n = buf->mNumberChannels;
    if (channel >= first + n) {
      first += n;
      continue;
    }
    if (!buf->mData) return 0;
    uint32_t frames = buf->mDataByteSize / (n * (uint32_t)sizeof(float));
    if (frames > max_frames) frames = max_frames;
    const float *src = (const float *)buf->mData + (channel - first);
    float peak = 0;
    uint32_t clips = 0;
    for (uint32_t i = 0; i < frames; i++) out[i] = rs_audio_input_apply(src[(size_t)i * n], gain, &peak, &clips);
    if (st) rs_audio_input_note(st, peak, clips, frames);
    return frames;
  }
  return 0;
}

// ------------------------------------------------------------------------------------------------
// Entries
// ------------------------------------------------------------------------------------------------
#define MAX_ENTRY_DEVS 64

int rs_audio_entries_budget(void) {
  if (!rs_audio_env_on("RS_AUDIO_INPUT", 1)) return 0;
  const char *e = getenv("RS_AUDIO_CHANNEL_ENTRIES");
  return e && *e ? atoi(e) : RS_AUDIO_LIST_DEFAULT;
}

int rs_audio_level_on(void) { return rs_audio_env_on("RS_AUDIO_INPUT", 1) && rs_audio_env_on("RS_AUDIO_INPUT_LEVEL", 1); }

float rs_audio_level_gain(float level) {
  if (level < 0) level = 0;
  if (level > 1) level = 1;
  return powf(10.0f, (level - RS_AUDIO_LEVEL_UNITY) / (1.0f - RS_AUDIO_LEVEL_UNITY) * RS_AUDIO_LEVEL_MAX_DB / 20.0f);
}

int rs_audio_is_cable(AudioObjectID dev) {
  char name[128], model[192] = "";
  CFStringRef s = NULL;
  rs_audio_dev_name(dev, name, sizeof name);
  if (rs_audio_dev_get(dev, kAudioDevicePropertyModelUID, &s, sizeof s) && s) {
    CFStringGetCString(s, model, sizeof model, kCFStringEncodingUTF8);
    CFRelease(s);
  }
  // The model UID is "<product>:<VID>:<PID>" in hex: 12BA is a vendor of other game hardware too.
  const char *vid = strcasestr(model, ":12BA:");
  return strstr(name, "Rocksmith USB Guitar Adapter") || (vid && strtoul(vid + 6, NULL, 16) == 0xff);
}

static int input_rank(AudioObjectID dev) {
  UInt32 transport = 0;
  if (rs_audio_is_cable(dev)) return RS_AUDIO_RANK_CABLE;
  rs_audio_dev_get(dev, kAudioDevicePropertyTransportType, &transport, sizeof transport);
  if (transport == kAudioDeviceTransportTypeVirtual || transport == kAudioDeviceTransportTypeAggregate)
    return RS_AUDIO_RANK_VIRTUAL;
  if (transport == kAudioDeviceTransportTypeBuiltIn) return RS_AUDIO_RANK_BUILT_IN;
  return rs_audio_has_output(dev) ? RS_AUDIO_RANK_INTERFACE : RS_AUDIO_RANK_OTHER;
}

static int entries_wanted(AudioObjectID dev, int channels) {
  char name[128];
  rs_audio_input in;
  rs_audio_dev_name(dev, name, sizeof name);
  rs_audio_input_for(name, &in);
  return in.channel >= 0 || channels < 2 ? 1 : channels;
}

void rs_audio_entry_counts(const int *rank, const int *want, int m, int budget, int *got) {
  int left = budget;
  for (int i = 0; i < m; i++) {
    got[i] = 1;
    if (rank[i] <= RS_AUDIO_RANK_INTERFACE) left--;
  }
  for (int more = 1; more && left > 0;) {
    more = 0;
    for (int i = 0; i < m && left > 0; i++)
      if (rank[i] == RS_AUDIO_RANK_INTERFACE && got[i] < want[i]) {
        got[i]++;
        left--;
        more = 1;
      }
  }
}

int rs_audio_input_entries(const AudioObjectID *devs, int n, int budget, rs_audio_entry *out, int max) {
  AudioObjectID dev[MAX_ENTRY_DEVS];
  int rank[MAX_ENTRY_DEVS], want[MAX_ENTRY_DEVS], got[MAX_ENTRY_DEVS], m = 0, k = 0;
  for (int i = 0; i < n && m < MAX_ENTRY_DEVS; i++) {
    int ch = rs_audio_channels(devs[i], kAudioObjectPropertyScopeInput);
    if (ch <= 0) continue;
    dev[m] = devs[i];
    rank[m] = input_rank(devs[i]);
    want[m] = rank[m] == RS_AUDIO_RANK_INTERFACE ? entries_wanted(devs[i], ch) : 1;
    m++;
  }
  rs_audio_entry_counts(rank, want, m, budget, got);
  for (int r = 0; r < RS_AUDIO_RANKS; r++)
    for (int i = 0; i < m; i++) {
      if (rank[i] != r) continue;
      for (int c = 0; c < got[i] && k < max; c++, k++) out[k] = (rs_audio_entry){dev[i], got[i] < 2 ? -1 : c};
    }
  return k;
}

// ------------------------------------------------------------------------------------------------
// Level
// ------------------------------------------------------------------------------------------------
static _Atomic int g_calibrated = -1;
static rs_audio_level_watch g_watch;

// The edition's folder: Learn & Play's and the Mac's have saves of their own.
static void marker_path(char *buf, size_t n) {
  const char *h = getenv("LP_HOME");
  if (!h || !*h) h = getenv("LP_USER_DIR");  // a build run by hand has no LP_HOME
  if (!h || !*h) h = getenv("RS_NATIVE_HOME");
  if (h && *h) snprintf(buf, n, "%s/input-level-calibrated", h);
  else snprintf(buf, n, "%s/Library/Application Support/RS Re-ARMed/input-level-calibrated", getenv("HOME") ? getenv("HOME") : "");
}

static int calibrated(void) {
  int c = atomic_load_explicit(&g_calibrated, memory_order_relaxed);
  if (c >= 0) return c;
  char path[1024];
  marker_path(path, sizeof path);
  c = access(path, F_OK) == 0;
  atomic_store(&g_calibrated, c);
  return c;
}

int rs_audio_level_watch_note(rs_audio_level_watch *w, uintptr_t who, float level) {
  w->run = who == w->who && level != w->level ? w->run + 1 : 0;
  w->who = who;
  w->level = level;
  return w->run >= RS_AUDIO_LEVEL_RUN;
}

void rs_audio_level_set(uintptr_t who, float level) {
  if (calibrated() || !rs_audio_level_watch_note(&g_watch, who, level)) return;
  atomic_store(&g_calibrated, 1);
  char path[1024];
  marker_path(path, sizeof path);
  FILE *f = fopen(path, "w");
  if (f) {
    fputs("The game's calibration has run with RS Re-ARMed's input level: saved levels count.\n", f);
    fclose(f);
  }
  rs_audio_log("input level: calibration seen, levels count from now on");
}

float rs_audio_level_applied(float level) { return calibrated() ? rs_audio_level_gain(level) : 1.0f; }

void rs_audio_input_report(rs_audio_input_stats *st, uint32_t every, const char *name) {
  if (!rs_audio_log_on() || atomic_load_explicit(&st->frames, memory_order_relaxed) < every) return;
  atomic_store_explicit(&st->frames, 0, memory_order_relaxed);
  uint32_t bits = atomic_exchange_explicit(&st->peak, 0, memory_order_relaxed);
  uint32_t clips = atomic_exchange_explicit(&st->clips, 0, memory_order_relaxed);
  float peak;
  memcpy(&peak, &bits, sizeof peak);
  if (peak > 0) rs_audio_log("guitar input \"%s\": peak %.1f dBFS, %u clipped", name, 20.0 * log10(peak), clips);
}
