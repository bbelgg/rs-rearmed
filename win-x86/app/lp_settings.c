// settings.env: the built-in mods switched off in the installer (lp_settings.h).
// options.env: every other switch the installer's sheet offers (lp_options_keys).
// The copy's steam.env: the Steam facts the copy recorded (lp_steam_env_apply).
#include "lp_settings.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

// The same keys as the Mac launcher's settings_keys and the installer's builtInMods.
const char *const lp_settings_keys[] = {
    "RS_MODS_BYPASS", "RS_MODS_FIXES", "RS_MODS_INTRO", "RS_MODS_AUTOSAVE",
    "RS_MODS_OVERLAY", "RS_MODS_MULTIPLAYER", "RS_MODS_GUITARCADE_SKIP", "RS_MODS_MENU_RETRY",
    "RS_MODS_ARRANGEMENTS", "RS_MODS_FRAME_RATE", "RS_REARMED", NULL,
};
// The same keys as the Mac launcher's options_keys and the installer's optionSpecs, in the same order.
const char *const lp_options_keys[] = {
    "RS_AUDIO_FRAMES", "RS_LATENCY_BUFFER", "RS_AUDIO_RT", "RS_AUDIO_INPUT_TRIM", "RS_AUDIO_INPUT",
    "RS_AUDIO_INPUT_CHANNEL", "RS_AUDIO_INPUT_GAIN_DB", "RS_AUDIO_INPUT_FILE", "RS_AUDIO_INPUT_LEVEL",
    "RS_AUDIO_CHANNEL_ENTRIES", "RS_AUDIO_SET_RATE", "RS_AUDIO_RS_NAMES", "RS_AUDIO_STABLE_UIDS",
    "RS_AUDIO_INPUTS_FIRST", "RS_FPS", "RS_WINDOW_ANY_SHAPE", "RS_GL_MTENGINE", "RS_METAL_RETINA",
    "RS_METAL_SCALE", "RS_METAL_UPSCALE", "RS_METAL_COLORS", "RS_METAL_ANISO", "RS_METAL_MSAA",
    "RS_METAL_SHARPEN", "RS_METAL_DRAWABLES", "RS_METAL_HUD", "RS_FULLSCREEN",
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
      "RS_GL_MTENGINE", "RS_METAL_RETINA", "RS_METAL_UPSCALE", "RS_METAL_HUD", "RS_LOAD_MODS", "RS_MODS_LOG",
      "RS_AUDIO_LOG", "RS_STEAM_LOG", "RS_FILE_LOG", NULL,
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
  line[strcspn(line, "\n")] = 0;  // (a line's end alone: the trim below takes a CR before it, as the Mac launcher does)
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

// The first Rocksmith's DLC that count as owned (lp_steam_env_apply): the copy's, and the ones set by hand that the
// copy's list lacks. steam_by_hand is RS_STEAM_OWNED as it was set by hand, before the copy's AppIDs joined it:
// what options.env's line is compared with for the log.
static int steam_found, steam_hand;
static char *steam_by_hand;

// The AppIDs of a list: the numbers in it, however they are separated (as the Steam layer reads one).
static int ids_count(const char *list) {
  int n = 0;
  for (const char *p = list; *p;) {
    p += strcspn(p, "0123456789");
    size_t len = strspn(p, "0123456789");
    n += len > 0;
    p += len;
  }
  return n;
}

static int ids_have(const char *list, const char *id, size_t n) {
  for (const char *p = list; *p;) {
    p += strcspn(p, "0123456789");
    size_t len = strspn(p, "0123456789");
    if (len == n && !memcmp(p, id, n)) return 1;
    p += len;
  }
  return 0;
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
    if (out && now && steam_by_hand && !strcmp(key, "RS_STEAM_OWNED")) now = steam_by_hand;
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

void lp_steam_env_apply(const char *path) {
  const char *now = getenv("RS_STEAM_OWNED");
  char *line = NULL;
  size_t cap = 0;
  free(steam_by_hand);
  steam_by_hand = strdup(now ? now : "");
  steam_found = 0;
  steam_hand = ids_count(steam_by_hand);
  FILE *f = fopen(path, "r");
  while (f && getline(&line, &cap, f) > 0) {
    line[strcspn(line, "\r\n")] = 0;
    char *eq = strchr(line, '=');
    if (!eq || !eq[1]) continue;
    *eq = 0;
    const char *v = eq + 1;
    if (!strcmp(line, "RS_STEAM_OWNED") && !strcmp(steam_by_hand, "all")) continue;
    if (!strcmp(line, "RS_STEAM_OWNED")) {
      char *all = malloc(strlen(v) + strlen(steam_by_hand) + 2);
      strcpy(all, v);
      steam_found = ids_count(v);
      steam_hand = 0;
      for (const char *p = steam_by_hand; *p;) {
        size_t skip = strcspn(p, "0123456789"), len = strspn(p + skip, "0123456789");
        p += skip;
        if (len && !ids_have(all, p, len)) {
          strcat(all, ",");
          strncat(all, p, len);
          steam_hand++;
        }
        p += len;
      }
      setenv("RS_STEAM_OWNED", all, 1);
      free(all);
      continue;
    }
    if (strcmp(line, "RS_STEAM_DLC") && strcmp(line, "RS_STEAM_LANGUAGE") && strcmp(line, "RS_STEAM_ID")) continue;
    now = getenv(line);
    if (!now || !*now) setenv(line, v, 1);
  }
  if (f) fclose(f);
  free(line);
}

void lp_steam_env_log(FILE *out) {
  if (steam_by_hand && !strcmp(steam_by_hand, "all")) {
    fprintf(out, "[steam] first Rocksmith DLC counted as owned: all (set by hand)\n");
    return;
  }
  fprintf(out, "[steam] first Rocksmith DLC counted as owned: %d (%d found by the copy, %d more set by hand)\n",
          steam_found + steam_hand, steam_found, steam_hand);
}
