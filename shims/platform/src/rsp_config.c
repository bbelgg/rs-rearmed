// Rocksmith.ini, as efd reads and writes it (0x1013140f0 and friends).
//
// The original works on an efd::File: ReadLine (0x1012b42c0) reads one byte at
// a time, drops '\r', stops at '\n' or after size-1 stored bytes, and returns
// the number of bytes consumed (0 = EOF). A section is found by reading from
// the start until a line equals "[Name]" exactly; its offset is Tell() after
// that line (-1 if absent). A key is found by reading from the section offset
// until a line starts with "Key=" (strncmp), a line starts with '[', or EOF;
// the value is strtoul(rest, NULL, 0) truncated to 32 bits. Absent keys keep
// their value. Booleans are "== 1".
#include "rsp_platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void rsp_config_defaults(rsp_config *c) {
  memset(c, 0, sizeof *c);
  c->latency_buffer = 4;
  c->fullscreen = 2;
  c->visual_quality = 1;
  c->enable_post_effects = 1;
  c->enable_shadows = 1;
  c->enable_high_res_scope = 1;
  c->enable_depth_of_field = 1;
  c->enable_per_pixel_lighting = 1;
  c->msaa_samples = 4;
  c->use_proxy = 1;
  c->auto_login = 1;
}

static rsp_config g_cfg;
static int g_cfg_init;  // the cxa guard at 0x101873e58
rsp_config *rsp_config_global(void) {
  if (!g_cfg_init) {
    rsp_config_defaults(&g_cfg);
    g_cfg_init = 1;
  }
  return &g_cfg;
}

// --- reading ---------------------------------------------------------------

typedef struct {
  const char *p;
  size_t len, pos;
} mfile;

#define LINE_MAX_ 0x1000

static unsigned read_line(mfile *f, char *buf, unsigned size) {
  unsigned consumed = 0, n = 0;
  if (size < 2) {
    buf[0] = 0;
    return 0;
  }
  for (;;) {
    if (f->pos >= f->len) break;
    char c = f->p[f->pos++];
    consumed++;
    if (c == '\r') continue;
    if (c == '\n') break;
    buf[n++] = c;
    if (n + 1 >= size) break;
  }
  buf[n] = 0;
  return consumed;
}

static long find_section(mfile *f, const char *name) {
  char line[LINE_MAX_];
  f->pos = 0;
  while (read_line(f, line, sizeof line))
    if (strcmp(name, line) == 0) return (long)f->pos;
  return -1;
}

static void read_key(mfile *f, long section, const char *key, uint32_t *out) {
  char line[LINE_MAX_];
  if (section == -1) return;
  f->pos = (size_t)section;
  size_t kl = strlen(key);
  while (read_line(f, line, sizeof line)) {
    if (line[0] == '[') return;
    if (strncmp(key, line, kl) == 0) {
      *out = (uint32_t)strtoul(line + kl, NULL, 0);
      return;
    }
  }
}

static void read_bool(mfile *f, long section, const char *key, uint8_t *b) {
  uint32_t v = *b;
  read_key(f, section, key, &v);
  *b = v == 1;
}

void rsp_config_parse(rsp_config *c, const char *text, size_t len) {
  mfile f = {text, len, 0};
  long s = find_section(&f, "[Audio]");
  read_bool(&f, s, "EnableMicrophone=", &c->enable_microphone);
  read_key(&f, s, "LatencyBuffer=", &c->latency_buffer);
  read_bool(&f, s, "DumpAudioLog=", &c->dump_audio_log);
  read_bool(&f, s, "RealToneCableOnly=", &c->real_tone_cable_only);
  read_key(&f, s, "MonoToStereoChannel=", &c->mono_to_stereo);

  s = find_section(&f, "[Renderer.MacOS]");
  read_key(&f, s, "ScreenWidth=", &c->screen_width);
  read_key(&f, s, "ScreenHeight=", &c->screen_height);
  read_key(&f, s, "Fullscreen=", &c->fullscreen);
  read_key(&f, s, "VisualQuality=", &c->visual_quality);
  read_key(&f, s, "RenderingWidth=", &c->rendering_width);
  read_key(&f, s, "RenderingHeight=", &c->rendering_height);
  read_key(&f, s, "EnablePostEffects=", &c->enable_post_effects);
  read_key(&f, s, "EnableShadows=", &c->enable_shadows);
  read_key(&f, s, "EnableHighResScope=", &c->enable_high_res_scope);
  read_key(&f, s, "EnableDepthOfField=", &c->enable_depth_of_field);
  read_key(&f, s, "EnablePerPixelLighting=", &c->enable_per_pixel_lighting);
  read_key(&f, s, "MsaaSamples=", &c->msaa_samples);
  read_bool(&f, s, "DisableBrowser=", &c->disable_browser);

  // 0x101315020 (both directions): Fullscreen is 0 or 2; VisualQuality 0..3.
  if (c->fullscreen >= 3 || c->fullscreen == 1) c->fullscreen = 2;
  if (c->visual_quality >= 4) c->visual_quality = 1;

  s = find_section(&f, "[Net]");
  read_bool(&f, s, "UseProxy=", &c->use_proxy);
  read_bool(&f, s, "AutoLogin=", &c->auto_login);

  s = find_section(&f, "[Global]");
  read_key(&f, s, "Version=", &c->version);
  if (c->version == 0) {
    c->version = 1;
    c->dirty = 1;
  }
}

// --- writing ---------------------------------------------------------------

typedef struct {
  char *buf;
  size_t size, len;
} sbuf;

static void put(sbuf *b, const char *s) {
  size_t n = strlen(s);
  if (b->len + n < b->size) memcpy(b->buf + b->len, s, n);
  b->len += n;
}
static void put_kv(sbuf *b, const char *k, unsigned v) {
  char t[96];
  snprintf(t, sizeof t, "%s=%u\n", k, v);
  put(b, t);
}

size_t rsp_config_write(const rsp_config *c, char *buf, size_t size) {
  sbuf b = {buf, size, 0};
  put(&b, "[Audio]\n");
  put_kv(&b, "EnableMicrophone", c->enable_microphone);
  put_kv(&b, "LatencyBuffer", c->latency_buffer);
  put_kv(&b, "DumpAudioLog", c->dump_audio_log);
  put_kv(&b, "RealToneCableOnly", c->real_tone_cable_only);
  put_kv(&b, "MonoToStereoChannel", c->mono_to_stereo);
  put(&b, "[Renderer.MacOS]\n");
  put_kv(&b, "ScreenWidth", c->screen_width);
  put_kv(&b, "ScreenHeight", c->screen_height);
  put_kv(&b, "Fullscreen", c->fullscreen);
  put_kv(&b, "VisualQuality", c->visual_quality);
  put_kv(&b, "RenderingWidth", c->rendering_width);
  put_kv(&b, "RenderingHeight", c->rendering_height);
  put_kv(&b, "EnablePostEffects", c->enable_post_effects);
  put_kv(&b, "EnableShadows", c->enable_shadows);
  put_kv(&b, "EnableHighResScope", c->enable_high_res_scope);
  put_kv(&b, "EnableDepthOfField", c->enable_depth_of_field);
  put_kv(&b, "EnablePerPixelLighting", c->enable_per_pixel_lighting);
  put_kv(&b, "MsaaSamples", c->msaa_samples);
  put_kv(&b, "DisableBrowser", c->disable_browser);
  put(&b, "[Net]\n");
  put_kv(&b, "UseProxy", c->use_proxy);
  if (!c->auto_login) put_kv(&b, "AutoLogin", c->auto_login);  // only when off
  put(&b, "[Global]\n");
  put_kv(&b, "Version", c->version);
  if (size) buf[b.len < size ? b.len : size - 1] = 0;
  return b.len;
}

// --- files -------------------------------------------------------------------

static char *slurp(const char *path, size_t *len) {
  FILE *f = fopen(path, "rb");
  if (!f) return NULL;
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  char *p = malloc(n > 0 ? (size_t)n : 1);
  *len = n > 0 && p ? fread(p, 1, (size_t)n, f) : 0;
  fclose(f);
  return p;
}

int rsp_config_save(const rsp_config *c) {
  char path[RSP_PATH_MAX + 32];
  if (!rsp_path_ini(path, sizeof path)) return 0;
  char text[2048];
  size_t n = rsp_config_write(c, text, sizeof text);
  FILE *f = fopen(path, "wb");
  if (!f) return 0;
  fwrite(text, 1, n, f);
  fclose(f);
  return 1;
}

int rsp_config_load_or_create(rsp_config *c) {
  char path[RSP_PATH_MAX + 32];
  if (!rsp_path_ini(path, sizeof path)) return 0;
  size_t len;
  char *text = slurp(path, &len);
  if (!text) {
    rsp_config d;
    rsp_config_defaults(&d);
    rsp_config_save(&d);
    return 0;
  }
  rsp_config_parse(c, text, len);
  free(text);
  c->loaded = 1;
  return 1;
}

void rsp_config_save_if_dirty(rsp_config *c) {
  if (c->dirty) {
    rsp_config_save(c);
    c->dirty = 0;
  }
}
