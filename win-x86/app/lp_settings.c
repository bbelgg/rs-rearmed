// settings.env: the built-in mods switched off in the installer (lp_settings.h).
// options.env: every other switch the installer's sheet offers (lp_options_keys).
#include "lp_settings.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

// The same keys as the Mac launcher's settings_keys and the installer's builtInMods.
const char *const lp_settings_keys[] = {
    "RS_MODS_BYPASS", "RS_MODS_FIXES", "RS_MODS_INTRO", "RS_MODS_AUTOSAVE",
    "RS_MODS_OVERLAY", "RS_MODS_MULTIPLAYER", "RS_REARMED", NULL,
};
// The same keys as the Mac launcher's options_keys and the installer's optionSpecs, in the same order.
const char *const lp_options_keys[] = {
    "RS_AUDIO_FRAMES", "RS_LATENCY_BUFFER", "RS_AUDIO_RT", "RS_AUDIO_INPUT_TRIM", "RS_AUDIO_INPUT",
    "RS_AUDIO_INPUT_CHANNEL", "RS_AUDIO_INPUT_GAIN_DB", "RS_AUDIO_INPUT_FILE", "RS_AUDIO_INPUT_LEVEL",
    "RS_AUDIO_CHANNEL_ENTRIES", "RS_AUDIO_SET_RATE", "RS_AUDIO_RS_NAMES", "RS_AUDIO_STABLE_UIDS",
    "RS_AUDIO_INPUTS_FIRST", "RS_FPS", "RS_WINDOW_ANY_SHAPE", "RS_GL_MTENGINE", "RS_METAL_RETINA",
    "RS_METAL_UPSCALE", "RS_METAL_COLORS", "RS_METAL_ANISO", "RS_METAL_MSAA", "RS_FULLSCREEN",
    "RS_D3D9_DISPLAY", "RS_STEAM_DLC", "RS_STEAM_OWNED", "RS_STEAM_LANGUAGE", "RS_STEAM_ID",
    "RS_STEAM_PERSONA", "RS_MODS_BUDGET_MS", "RS_MODS_LOG", "RS_LOAD_MODS", "RS_AUDIO_LOG", "RS_STEAM_LOG",
    "RS_FILE_LOG", NULL,
};
enum { NKEYS = sizeof lp_settings_keys / sizeof lp_settings_keys[0] - 1 };
enum { NOKEYS = sizeof lp_options_keys / sizeof lp_options_keys[0] - 1 };

// Toggles: only 0 or 1 counts, as in settings.env. Every other options key takes any non-empty value.
// The key is line's first klen bytes (its ends already trimmed by the caller).
static int is_toggle(const char *line, size_t klen) {
  static const char *const toggles[] = {
      "RS_AUDIO_RT", "RS_AUDIO_INPUT_TRIM", "RS_AUDIO_INPUT", "RS_AUDIO_INPUT_LEVEL", "RS_AUDIO_SET_RATE",
      "RS_AUDIO_RS_NAMES", "RS_AUDIO_STABLE_UIDS", "RS_AUDIO_INPUTS_FIRST", "RS_WINDOW_ANY_SHAPE",
      "RS_GL_MTENGINE", "RS_METAL_RETINA", "RS_METAL_UPSCALE", "RS_LOAD_MODS", "RS_MODS_LOG", "RS_AUDIO_LOG",
      "RS_STEAM_LOG", "RS_FILE_LOG", NULL,
  };
  for (int i = 0; toggles[i]; i++)
    if (strlen(toggles[i]) == klen && !memcmp(line, toggles[i], klen)) return 1;
  return 0;
}

static int key_index(const char *k) {
  for (int i = 0; i < NKEYS; i++)
    if (!strcmp(k, lp_settings_keys[i])) return i;
  return -1;
}

typedef enum { SKIP, SWITCH, IGNORED, REPEATED } kind;

// One line, its whitespace removed in place: a comment or a blank line, a switch (*k, *v), or ignored.
static kind parse(char *line, int seen[NKEYS], const char **k, const char **v) {
  char *w = line;
  for (char *r = line; *r; r++)
    if (!isspace((unsigned char)*r)) *w++ = *r;
  *w = 0;
  if (!*line || *line == '#') return SKIP;
  char *eq = strchr(line, '=');
  if (!eq) return IGNORED;
  *eq = 0;
  int i = key_index(line);
  kind what = i < 0 || (strcmp(eq + 1, "0") && strcmp(eq + 1, "1")) ? IGNORED : seen[i] ? REPEATED : SWITCH;
  if (what != SWITCH) {
    *eq = '=';  // the line as it was, for the log
    return what;
  }
  seen[i] = 1;
  *k = line;
  *v = eq + 1;
  return SWITCH;
}

// One line of options.env: only the ends' whitespace goes, so values may hold spaces (a persona name).
// A toggle other than 0 or 1 is ignored; any other key takes any non-empty value. The line is trimmed in
// place, as the Mac launcher trims its own, so the log reads the same; a switch logs as KEY=value.
static kind parse_opt(char *line, int seen[NOKEYS], size_t *klen, const char **v) {
  line[strcspn(line, "\r\n")] = 0;
  char *s = line;
  while (*s && isspace((unsigned char)*s)) s++;
  if (s != line) memmove(line, s, strlen(s) + 1);
  size_t n = strlen(line);
  while (n > 0 && isspace((unsigned char)line[n - 1])) line[--n] = 0;
  if (!*line || *line == '#') return SKIP;
  char *eq = strchr(line, '=');
  if (!eq) return IGNORED;
  char *ve = eq + 1;
  while (*ve && isspace((unsigned char)*ve)) ve++;
  char *vend = ve + strlen(ve);
  while (vend > ve && isspace((unsigned char)vend[-1])) *--vend = 0;
  size_t len = (size_t)(eq - line);
  while (len > 0 && isspace((unsigned char)line[len - 1])) len--;
  int i = -1;
  for (int j = 0; j < NOKEYS; j++)
    if (strlen(lp_options_keys[j]) == len && !memcmp(line, lp_options_keys[j], len)) i = j;
  kind what = SKIP;
  if (i < 0 || !*ve)
    what = IGNORED;
  else if (is_toggle(line, len))
    what = (!strcmp(ve, "0") || !strcmp(ve, "1")) ? SWITCH : IGNORED;
  else
    what = SWITCH;
  if (what != SWITCH) return IGNORED;
  if (seen[i]) return REPEATED;
  seen[i] = 1;
  *klen = len;
  *v = ve;
  return SWITCH;
}

static void each(const char *path, FILE *out) {
  FILE *f = fopen(path, "r");
  if (!f) return;
  char *line = NULL;
  size_t cap = 0;
  int seen[NKEYS] = {0};
  while (getline(&line, &cap, f) > 0) {
    const char *k = NULL, *v = NULL;
    kind what = parse(line, seen, &k, &v);
    const char *now = what == SWITCH ? getenv(k) : NULL;
    int other = now && *now && strcmp(now, v);
    if (!out) {
      if (what == SWITCH && !other) setenv(k, v, 1);
    } else if (what == IGNORED) {
      fprintf(out, "[settings] ignored: %s\n", line);
    } else if (what == REPEATED) {
      fprintf(out, "[settings] ignored: %s (repeated)\n", line);
    } else if (what == SWITCH && other) {
      fprintf(out, "[settings] %s=%s not used: the environment has %s=%s\n", k, v, k, now);
    } else if (what == SWITCH) {
      fprintf(out, "[settings] %s=%s\n", k, v);
    }
  }
  free(line);
  fclose(f);
}

static void each_opt(const char *path, FILE *out) {
  FILE *f = fopen(path, "r");
  if (!f) return;
  char *line = NULL;
  size_t cap = 0;
  int seen[NOKEYS] = {0};
  while (getline(&line, &cap, f) > 0) {
    size_t klen = 0;
    const char *v = NULL;
    kind what = parse_opt(line, seen, &klen, &v);
    char key[64];
    if (what == SWITCH) {
      if (klen >= sizeof key) return;
      memcpy(key, line, klen);
      key[klen] = 0;
    }
    const char *now = what == SWITCH ? getenv(key) : NULL;
    int other = now && *now && strcmp(now, v);
    if (!out) {
      if (what == SWITCH && !other) setenv(key, v, 1);
    } else if (what == IGNORED) {
      fprintf(out, "[options] ignored: %s\n", line);
    } else if (what == REPEATED) {
      fprintf(out, "[options] ignored: %s (repeated)\n", line);
    } else if (what == SWITCH && other) {
      fprintf(out, "[options] %s=%s not used: the environment has %s=%s\n", key, v, key, now);
    } else if (what == SWITCH) {
      fprintf(out, "[options] %s=%s\n", key, v);
    }
  }
  free(line);
  fclose(f);
}

void lp_settings_apply(const char *path) { each(path, NULL); }
void lp_settings_log(const char *path, FILE *out) { each(path, out); }
void lp_options_apply(const char *path) { each_opt(path, NULL); }
void lp_options_log(const char *path, FILE *out) { each_opt(path, out); }
