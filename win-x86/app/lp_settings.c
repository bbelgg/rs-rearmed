// settings.env: the built-in mods switched off in the installer (lp_settings.h).
#include "lp_settings.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

// The same keys as the Mac launcher's settings_keys and the installer's builtInMods.
const char *const lp_settings_keys[] = {
    "RS_MODS_BYPASS", "RS_MODS_FIXES", "RS_MODS_INTRO", "RS_MODS_AUTOSAVE",
    "RS_MODS_OVERLAY", "RS_MODS_MULTIPLAYER", "RS_REARMED", NULL,
};
enum { NKEYS = sizeof lp_settings_keys / sizeof lp_settings_keys[0] - 1 };

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

void lp_settings_apply(const char *path) { each(path, NULL); }
void lp_settings_log(const char *path, FILE *out) { each(path, out); }
