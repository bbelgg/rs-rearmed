// Adv Display Settings' own options, the part that is one source for both editions (menu_options.h): the options,
// their values, where a change is written and why one can't be changed now. Nothing here touches the game.
//
// An option is an RS Re-ARMed switch (the renderer: its file). Its value is the one this run started with (the
// launcher passes options.env), and a change is written to options.env, the file the installer's Settings and
// switches... writes, so the two never differ; the default takes its line out. It takes effect when the game next
// starts. A switch set for the session (launchctl setenv; for the renderer RS_GL_BACKEND) wins over the file at
// every start, as it does over the installer's page, so its row shows the session's value, locked ("SET FOR THIS
// SESSION": session_set). Learn & Play has Metal only: its list leaves out the renderer and the OpenGL engine.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "menu_options.h"
#include "mods_shared.h"

#define OFF_ON {"0", "1"}, {"$[31679]", "$[31678]"}  // the game's own Off and On
enum { METAL = RS_MENU_METAL, OPENGL = RS_MENU_OPENGL };
rs_menu_option rs_menu_opts[] = {
    {"rsFrameRate", "RS_FPS", 1, 0, 4, 0, {"", "30", "60", "120"}, {"DISPLAY", "30", "60", "120"}, 0},
    {"rsAnyWindowShape", "RS_WINDOW_ANY_SHAPE", 0, 0, 2, 1, OFF_ON, 0},
    {"rsRenderer", NULL, 1, 0, 2, 0, {"metal", "opengl"}, {"METAL", "OPENGL"}, 1},
    {"rsMultithreadedGL", "RS_GL_MTENGINE", 0, OPENGL, 2, 0, OFF_ON, 1},
    {"rsLowLatency", "RS_METAL_DRAWABLES", 0, METAL, 2, 0, {"", "2"}, {"$[31679]", "$[31678]"}, 0},
    {"rsMetalFXUpscaling", "RS_METAL_UPSCALE", 0, METAL, 2, 1, OFF_ON, 0},
    {"rsFullRetina", "RS_METAL_RETINA", 0, METAL, 2, 0, OFF_ON, 0},
    {"rsRenderScale", "RS_METAL_SCALE", 1, METAL, 4, 3, {"50", "75", "90", ""}, {"50%", "75%", "90%", "100%"}, 0},
    {"rsAntiAliasing", "RS_METAL_MSAA", 1, METAL, 4, 2, {"1", "2", "4", "8"}, {"OFF", "2X", "4X", "8X"}, 0},
    {"rsTextureFiltering", "RS_METAL_ANISO", 1, METAL, 5, 4, {"1", "2", "4", "8", "16"},
     {"1X", "2X", "4X", "8X", "16X"}, 0},
    {"rsColor", "RS_METAL_COLORS", 1, METAL, 2, 0, {"", "vivid"}, {"SRGB", "VIVID"}, 0},
    {"rsPerformanceOverlay", "RS_METAL_HUD", 0, METAL, 2, 0, OFF_ON, 0},
};
const size_t rs_menu_nopts = sizeof rs_menu_opts / sizeof *rs_menu_opts;
#define NOPTS rs_menu_nopts
typedef rs_menu_option option;

static int g_lp;  // Learn & Play's list

int rs_menu_opt_here(const option *o) { return !(g_lp && o->mac_only); }

// The game's own compare is in any case (it has "buttons.<id>.states"; a press gives "Buttons.<id>.States").
option *rs_menu_opt_of_path(const char *path) {
  for (size_t i = 0; i < NOPTS; i++)
    if (path && rs_menu_opts[i].path && rs_menu_opt_here(&rs_menu_opts[i]) && !strcasecmp(path, rs_menu_opts[i].path))
      return &rs_menu_opts[i];
  return NULL;
}
option *rs_menu_opt_of_id(const char *id) {
  for (size_t i = 0; i < NOPTS; i++)
    if (rs_menu_opt_here(&rs_menu_opts[i]) && !strcmp(rs_menu_opts[i].id, id)) return &rs_menu_opts[i];
  return NULL;
}
option *rs_menu_opt_named(const char *cur) {
  for (size_t i = 0; cur && i < NOPTS; i++)
    if (rs_menu_opt_here(&rs_menu_opts[i]) && (cur == rs_menu_opts[i].name || !strcmp(cur, rs_menu_opts[i].id)))
      return &rs_menu_opts[i];
  return NULL;
}

// Where options.env is: the Mac launcher's RS_NATIVE_HOME, Learn & Play's LP_HOME (win-x86/app/lp_main.c; a build
// started without the app has none, and a change is then not written).
static const char *home(void) {
  const char *h = getenv(g_lp ? "LP_HOME" : "RS_NATIVE_HOME");
  return h && *h ? h : NULL;
}

// A checkbox whose switch is 0 or 1, a toggle as the launchers read it (not Low Latency's none or 2).
static int is_toggle(const option *o) { return !o->slider && !strcmp(o->value[1], "1"); }

// The choice a value of the option's switch is: a toggle's off when it starts with 0, n, N, f or F (as the launchers
// read toggles), another checkbox's (Low Latency's) and a slider's by its value in any case; the default without one,
// -1 for another value.
static int choice_of(const option *o, const char *v) {
  if (!v || !*v) return o->dflt;
  if (is_toggle(o)) return !strchr("0nNfF", v[0]);
  for (int i = 0; i < o->n; i++)
    if (!strcasecmp(v, o->value[i])) return i;
  return -1;
}
// A renderer's name as the shell reads it (rsp_renderer_parse: any case, white space around it): METAL - 1 or
// OPENGL - 1, -1 for anything else.
static int renderer_named(const char *s) {
  char v[8], more[2];
  if (!s || sscanf(s, " %7s %1s", v, more) != 1) return -1;
  if (!strcasecmp(v, "metal")) return METAL - 1;
  return !strcasecmp(v, "gl") || !strcasecmp(v, "opengl") ? OPENGL - 1 : -1;
}
static int renderer_file(const option *o) {  // the renderer's choice in its file
  char path[1024], v[32] = "";
  if (!home()) return o->dflt;
  snprintf(path, sizeof path, "%s/renderer", home());  // the renderer's file (docs/mac/INSTALL.md, Renderer)
  FILE *f = fopen(path, "r");
  if (f) {
    if (fscanf(f, "%31s", v) != 1) v[0] = 0;
    fclose(f);
  }
  if (!strcasecmp(v, "gl")) return OPENGL - 1;  // as the launcher and the game read the file: gl is OpenGL too
  return choice_of(o, v);
}
static int choice_now(const option *o) {
  if (o->env) return choice_of(o, getenv(o->env));
  return o->session ? renderer_named(getenv("RS_GL_BACKEND")) : renderer_file(o);
}
const char *rs_menu_choice_text(const option *o, int c) {
  if (c >= 0 && c < o->n) return o->text[c];
  const char *v = o->env ? getenv(o->env) : NULL;
  return v && *v ? v : o->text[o->dflt];
}

// options.env with KEY's line set to KEY=VALUE, or taken out (VALUE NULL): the other lines kept as they are.
static int save_switch(const char *key, const char *value) {
  if (!home()) return -1;
  char path[1024], part[1100];
  snprintf(path, sizeof path, "%s/options.env", home());
  snprintf(part, sizeof part, "%s.part", path);
  FILE *in = fopen(path, "r"), *out = fopen(part, "w");
  if (!out) {
    if (in) fclose(in);
    return -1;
  }
  size_t klen = strlen(key);
  int done = 0;
  char line[4096];
  while (in && fgets(line, sizeof line, in)) {
    const char *p = line;
    while (*p == ' ' || *p == '\t') p++;
    if (!strncmp(p, key, klen) && (p[klen] == '=' || p[klen] == ' ' || p[klen] == '\t')) {
      if (!done && value) fprintf(out, "%s=%s\n", key, value);
      done = 1;
      continue;
    }
    fputs(line, out);
  }
  if (!done && value) fprintf(out, "%s=%s\n", key, value);
  if (in) fclose(in);
  int ok = !ferror(out);
  if (fclose(out) || !ok || rename(part, path)) {
    unlink(part);
    return -1;
  }
  return 0;
}
static int save_renderer(const char *value) {
  if (!home()) return -1;
  char path[1024], part[1100];
  snprintf(path, sizeof path, "%s/renderer", home());
  snprintf(part, sizeof part, "%s.part", path);
  FILE *f = fopen(part, "w");
  if (!f) return -1;
  int ok = fprintf(f, "%s\n", value) > 0;
  if (fclose(f) || !ok || rename(part, path)) {
    unlink(part);
    return -1;
  }
  return 0;
}

// --- the session's switches ------------------------------------------------------------------------------------
// The launcher passes options.env's lines as the game's environment, but a switch set for the session (launchctl
// setenv) wins over the file's line (launch.sh, options_env), and the game also has the session's variables the file
// doesn't name. So the session sets a switch when the environment has it with a value options.env's line doesn't
// have, or the file has no line for it. Each run asks once, at its start (a change made in the menu is in the file
// only). For the renderer: an RS_GL_BACKEND from outside the game (rsp_renderer_env_given: not the shell's own
// restart), except the gl the launcher passes for a renderer file of OpenGL. One the file has the same can't be told
// apart.

// options.env's value for KEY as the launcher reads it: the first line KEY=VALUE with a value (for a toggle, 0 or 1),
// the ends' white space taken off both; "" without one.
static void file_value(const char *key, int toggle, char *out, size_t n) {
  char path[1024], line[4096];
  out[0] = 0;
  if (!home()) return;
  snprintf(path, sizeof path, "%s/options.env", home());
  FILE *f = fopen(path, "r");
  if (!f) return;
  while (fgets(line, sizeof line, f)) {
    char *k = line, *eq = strchr(line, '='), *v, *e;
    if (!eq) continue;
    *eq = 0;
    v = eq + 1;
    while (*k == ' ' || *k == '\t') k++;
    for (e = eq; e > k && strchr(" \t", e[-1]); e--) e[-1] = 0;
    while (*v == ' ' || *v == '\t') v++;
    for (e = v + strlen(v); e > v && strchr(" \t\r\n\v\f", e[-1]); e--) e[-1] = 0;
    if (strcmp(k, key) || !*v || (toggle && strcmp(v, "0") && strcmp(v, "1"))) continue;
    snprintf(out, n, "%s", v);
    break;
  }
  fclose(f);
}

// The Mac shell's rsp_renderer_env_given (shims/platform/include/rsp_renderer.h), which menu_options.c sets; Learn &
// Play has no renderer choice.
int (*rs_menu_renderer_env_given)(void);

static int session_set(const option *o) {
  if (!o->env) {
    int env = renderer_named(getenv("RS_GL_BACKEND"));
    if (env < 0 || (rs_menu_renderer_env_given && !rs_menu_renderer_env_given())) return 0;
    return !(env == OPENGL - 1 && renderer_file(o) == OPENGL - 1);
  }
  const char *v = getenv(o->env);
  char line[1024];
  if (!v || !*v) return 0;
  file_value(o->env, is_toggle(o), line, sizeof line);
  return strcmp(v, line) != 0;
}

void rs_menu_set_choice(option *o, int c) {
  o->now = c;
  const char *v = o->value[c];
  int r = o->env ? save_switch(o->env, c == o->dflt || !*v ? NULL : v) : save_renderer(v);
  rs_mods_logf("menu options: %s %s%s", o->env ? o->env : "renderer", *v ? v : "(default)",
               r ? " (not written)" : "");
}

void rs_menu_opts_init(int lp) {
  g_lp = lp;
  for (size_t i = 0; i < NOPTS; i++) {
    option *o = &rs_menu_opts[i];
    char path[96];
    snprintf(path, sizeof path, "buttons.%s.states", o->id);
    o->path = strdup(path);
    o->session = session_set(o);
    o->now = choice_now(o);
    if (o->session && rs_menu_opt_here(o))
      rs_mods_logf("menu options: %s=%s set for this session, its row locked", o->env ? o->env : "RS_GL_BACKEND",
                   getenv(o->env ? o->env : "RS_GL_BACKEND"));
  }
}

int rs_menu_choice(const option *o) { return o->now >= 0 ? o->now : o->dflt; }

// Whether the game starts next at a fixed frame rate: the Frame Rate slider's choice, or for a rate it has no notch for
// (90 or 144, which the installer allows) RS_FPS as the shell reads it (rsp_fps_value: a whole number from 1 to 1000).
static int fixed_rate(const option *fps) {
  if (fps->now >= 0) return fps->now != fps->dflt;
  const char *v = getenv(fps->env);
  if (!v) return 0;
  char *end;
  long n = strtol(v, &end, 10);
  return !*end && n >= 1 && n <= 1000;
}

// Why an option can't be changed now, as its row says it (NULL: it can), by the choices the game starts with next:
// an option of one renderer while the other is chosen (Learn & Play: Metal); Low Latency with a fixed frame rate,
// whose presents are not held for the display and have the two drawables already; a switch the session sets
// (session_set).
const char RS_MENU_SESSION[] = "SET FOR THIS SESSION";
const char *rs_menu_locked(const option *o) {
  const option *renderer = rs_menu_opt_of_id("rsRenderer"), *fps = rs_menu_opt_of_id("rsFrameRate");
  int r = renderer ? rs_menu_choice(renderer) : METAL - 1;
  if (o->only && o->only - 1 != r) return o->only == METAL ? "METAL ONLY" : "OPENGL ONLY";
  // (one whose switch has a value it has no choice for, RS_METAL_DRAWABLES=3, stays open: the row is the way to
  // take it out)
  if (!strcmp(o->id, "rsLowLatency") && o->now >= 0 && fixed_rate(fps)) return "DISPLAY RATE ONLY";
  return o->session ? RS_MENU_SESSION : NULL;
}

int rs_menu_bar_row(int n, double y) {
  double thumb = fmax((double)PANEL_H / n, THUMB_LEAST);
  double f = (y - PANEL_Y - thumb / 2) / (PANEL_H - thumb);
  return (int)lround((f < 0 ? 0 : f > 1 ? 1 : f) * (n - 1));
}
int rs_menu_on_bar(double x, double y) {
  int bar = PANEL_X + PANEL_W + BAR_GAP;
  return x >= bar - BAR_REACH / 4 && x < bar + BAR_REACH && y >= PANEL_Y && y < PANEL_Y + PANEL_H;
}
