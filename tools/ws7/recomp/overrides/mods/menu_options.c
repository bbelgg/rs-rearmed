// Adv Display Settings: RS Re-ARMed's own options in the game's own menu (not in the original).
//
// The overlay (tools/play-native/cache_overlay.py, MENU_OPTIONS) adds a button for each option to
// AdvancedDisplaySettings' definition (manifests/ui_menu_pillar_options.database.json), its label and description
// rows of the game's text table, and the game lists, sorts and shows them with its own. Its code knows only its own
// settings, so four of its methods have a native one in front of them, set in the classes' vtables at the first frame
// (each slot only while it holds the game's own function): a vtable entry outside the image is called as a native
// function with the guest's arguments (rc_icall_native), and each calls the game's own first, or instead of it for
// what isn't ours.
//
//   AdvancedDisplaySettings, menu_refresh_slot  after the game's (as the list is made, and when Edit Setting closes):
//                                               our buttons' CurrentSettingValue, the value the list's "CURRENT
//                                               SETTING:" line shows, and those of the renderer not chosen locked,
//                                               greyed by the overlay's edit of the movie (Multithreaded GL with
//                                               Metal; the Metal options with OpenGL), as are those the session sets
//   AdvancedDisplaySettings, menu_press_slot    a press on one of our buttons ("Buttons.<id>.States"): the edited
//                                               setting is ours and Edit Setting opens, as for the game's own (not
//                                               for a locked one); any other press is the game's
//   EditSetting, menu_build_slot                after the game's (the setting's object, its ID the control's name,
//                                               and the common fields): ours, a checkbox, or a notched slider of its
//                                               choices as the game makes Visual Quality's
//   EditSetting, menu_input_slot                after the game's: a change of our control, saved, and a slider's
//                                               Value (the choice it shows) set
//
// An option is an RS Re-ARMed switch (the renderer: its file). Its value is the one this run started with (the
// launcher passes options.env), and a change is written to options.env, the file the installer's Settings and
// switches... writes, so the two never differ; the default takes its line out. It takes effect when the game next
// starts. A switch set for the session (launchctl setenv; for the renderer RS_GL_BACKEND) wins over the file at
// every start, as it does over the installer's page, so its row shows the session's value, locked ("SET FOR THIS
// SESSION": session_set). RS_MODS_OVERLAY=0: none of this, the menu as the game has it. Mac only for now: Learn &
// Play's menus are the Windows game's own code, and its overlay leaves the options out (docs/windows/PLAN.md).
#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "mods.h"
#include "rc_hook.h"
#include "rc_rt.h"

// --- the game's string (32 bytes) ---------------------------------------------------------------------------
// Its text is inline when the last word points at the third (the end pointer's own place); otherwise the first word
// points at it and the last at the end of its buffer, which efd_free frees by its size.
typedef struct {
  char buf[16];
  char *end;
  void *cap;
} gstr;

static void gstr_set(gstr *s, const char *text) {  // inline: up to 15 bytes
  size_t n = strlen(text);
  if (n > 15) n = 15;
  memcpy(s->buf, text, n);
  s->buf[n] = 0;
  s->end = s->buf + n;
  s->cap = &s->end;
}
static const char *gstr_text(const gstr *s) {
  return s->cap == (const void *)&s->end ? s->buf : *(char *const *)(const void *)s->buf;
}
static void gstr_free(gstr *s) {
  if (s->cap == (void *)&s->end) return;
  char *p = *(char **)(void *)s->buf;
  if (!p) return;
  volatile uint8_t *flag = (volatile uint8_t *)(uintptr_t)RC_GA(efd_free_flag);
  *flag = 1;
  OV_GUEST(efd_free, (uint64_t)(uintptr_t)p, (uint64_t)((char *)s->cap - p));
  *flag = 0;
}

// --- the menus' JSON --------------------------------------------------------------------------------------------
static void ref_release(uint64_t ref) {
  if (ref) OV_VCALL(ref, RC_SLOT(json_node_release));
}
typedef struct {
  uint64_t w[2];
} jkey;
static jkey key_of(const char *text) {
  jkey k = {{0, 0}};
  OV_GUEST(json_key_make, (uint64_t)(uintptr_t)&k, (uint64_t)(uintptr_t)text);
  return k;
}
static void key_free(jkey *k) { OV_GUEST(json_key_free, (uint64_t)(uintptr_t)k); }

static void set_text(uint64_t obj, const char *name, const char *text) {
  jkey k = key_of(name);
  uint64_t out = 0;
  OV_GUEST(json_set_text, (uint64_t)(uintptr_t)&out, obj, (uint64_t)(uintptr_t)&k, (uint64_t)(uintptr_t)text);
  ref_release(out);
  key_free(&k);
}
static void set_bool(uint64_t obj, const char *name, int v) {
  jkey k = key_of(name);
  uint64_t out = 0;
  OV_GUEST(json_set_bool, (uint64_t)(uintptr_t)&out, obj, (uint64_t)(uintptr_t)&k, (uint64_t)(v != 0));
  ref_release(out);
  key_free(&k);
}
static void set_number(uint64_t obj, const char *name, double v) {
  jkey k = key_of(name);
  uint64_t out = 0, d;
  memcpy(&d, &v, sizeof d);
  const uint64_t ints[] = {(uint64_t)(uintptr_t)&out, obj, (uint64_t)(uintptr_t)&k};
  rc_call_guest(RC_GA(json_set_number), ints, 3, &d, 1, NULL);
  ref_release(out);
  key_free(&k);
}
static uint64_t number(double v) {  // a number value (a reference)
  uint64_t out = 0, d;
  memcpy(&d, &v, sizeof d);
  const uint64_t ints[] = {(uint64_t)(uintptr_t)&out};
  rc_call_guest(RC_GA(num_intern), ints, 1, &d, 1, NULL);
  return out;
}
static void set_range(uint64_t obj, const char *name, double lo, double hi) {  // [lo, hi], as AcceptedRange is
  jkey k = key_of(name);
  uint64_t arr = 0;
  OV_GUEST(json_set_array, (uint64_t)(uintptr_t)&arr, obj, (uint64_t)(uintptr_t)&k);
  key_free(&k);
  for (int i = 0; arr && i < 2; i++) {
    uint64_t n = number(i ? hi : lo);
    if (n) OV_GUEST(json_array_add, arr, n);
    ref_release(n);
  }
  ref_release(arr);
}
// A text of the game's text table in the game's language ("$[row]": the row's own text), to the caller to free
static void loc(gstr *out, const char *row) {
  gstr in;
  gstr_set(&in, row);
  OV_GUEST(loc_text, (uint64_t)(uintptr_t)out, (uint64_t)(uintptr_t)&in);
}
static void set_loc(uint64_t obj, const char *name, const char *text) {  // "$[row]" in the game's language
  if (strncmp(text, "$[", 2)) {
    set_text(obj, name, text);
    return;
  }
  gstr t;
  loc(&t, text);
  set_text(obj, name, gstr_text(&t));
  gstr_free(&t);
}

// --- the options ------------------------------------------------------------------------------------------------
// A checkbox has two choices, off and on (its switch 0 or 1), shown as the game's own Off and On; a slider has one
// notch for each of its choices. Each choice is a value of the switch ("" none) and the text it shows. The order of
// the buttons is cache_overlay.py's MENU_OPTIONS (their ids match).
#define CHOICES 6
typedef struct {
  const char *id;   // the button's ID in the definition, and the setting's name
  const char *env;  // its switch, or NULL for the renderer's file
  int slider;       // 0: a checkbox
  int only;         // the renderer it's for: METAL, OPENGL, or 0 both
  int n, dflt;      // the choices, and which one is the default (without the switch)
  const char *value[CHOICES], *text[CHOICES];
  const char *path;  // "buttons.<id>.states"
  const char *name;  // its fixed string, what menu_current_setting holds while Edit Setting edits it
  int now;           // the choice this run has (-1: a value of the switch no choice has)
  int session;       // the session sets its switch (session_set): locked
} option;

#define OFF_ON {"0", "1"}, {"$[31679]", "$[31678]"}  // the game's own Off and On
enum { METAL = 1, OPENGL };                         // the renderer's choices, after 0
static option g_opts[] = {
    {"rsFrameRate", "RS_FPS", 1, 0, 4, 0, {"", "30", "60", "120"}, {"DISPLAY", "30", "60", "120"}, NULL, NULL, 0},
    {"rsAnyWindowShape", "RS_WINDOW_ANY_SHAPE", 0, 0, 2, 1, OFF_ON, NULL, NULL, 0},
    {"rsRenderer", NULL, 1, 0, 2, 0, {"metal", "opengl"}, {"METAL", "OPENGL"}, NULL, NULL, 0},
    {"rsMultithreadedGL", "RS_GL_MTENGINE", 0, OPENGL, 2, 0, OFF_ON, NULL, NULL, 0},
    {"rsMetalFXUpscaling", "RS_METAL_UPSCALE", 0, METAL, 2, 1, OFF_ON, NULL, NULL, 0},
    {"rsFullRetina", "RS_METAL_RETINA", 0, METAL, 2, 0, OFF_ON, NULL, NULL, 0},
    {"rsRenderScale", "RS_METAL_SCALE", 1, METAL, 4, 3, {"50", "75", "90", ""}, {"50%", "75%", "90%", "100%"}, NULL,
     NULL, 0},
    {"rsAntiAliasing", "RS_METAL_MSAA", 1, METAL, 4, 2, {"1", "2", "4", "8"}, {"OFF", "2X", "4X", "8X"}, NULL, NULL,
     0},
    {"rsTextureFiltering", "RS_METAL_ANISO", 1, METAL, 5, 4, {"1", "2", "4", "8", "16"},
     {"1X", "2X", "4X", "8X", "16X"}, NULL, NULL, 0},
    {"rsColor", "RS_METAL_COLORS", 1, METAL, 2, 0, {"", "vivid"}, {"SRGB", "VIVID"}, NULL, NULL, 0},
};
#define NOPTS (sizeof g_opts / sizeof *g_opts)

// The game's own compare is in any case (it has "buttons.<id>.states"; a press gives "Buttons.<id>.States").
static option *opt_of_path(const char *path) {
  for (size_t i = 0; i < NOPTS; i++)
    if (path && !strcasecmp(path, g_opts[i].path)) return &g_opts[i];
  return NULL;
}
static option *opt_current(void) {
  const char *cur = RC_LD(const char *, RC_GA(menu_current_setting));
  for (size_t i = 0; i < NOPTS; i++)
    if (cur && (cur == g_opts[i].name || !strcmp(cur, g_opts[i].id))) return &g_opts[i];
  return NULL;
}

static const char *home(void) {
  const char *h = getenv("RS_NATIVE_HOME");
  return h && *h ? h : NULL;
}

// The choice a value of the option's switch is: a checkbox's off when it starts with 0, n, N, f or F (as the
// launchers read toggles), a slider's by its value in any case; the default without one, -1 for another value.
static int choice_of(const option *o, const char *v) {
  if (!v || !*v) return o->dflt;
  if (!o->slider) return !strchr("0nNfF", v[0]);
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
static const char *choice_text(const option *o, int c) {
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
static int is_toggle(const option *o) { return !o->slider && !strcmp(o->value[1], "1"); }

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

// The Mac shell's (shims/platform/include/rsp_renderer.h).
extern int rsp_renderer_env_given(void) __attribute__((weak_import));

static int session_set(const option *o) {
  if (!o->env) {
    int env = renderer_named(getenv("RS_GL_BACKEND"));
    if (env < 0 || (rsp_renderer_env_given && !rsp_renderer_env_given())) return 0;
    return !(env == OPENGL - 1 && renderer_file(o) == OPENGL - 1);
  }
  const char *v = getenv(o->env);
  char line[1024];
  if (!v || !*v) return 0;
  file_value(o->env, is_toggle(o), line, sizeof line);
  return strcmp(v, line) != 0;
}

static void set_choice(option *o, int c) {
  o->now = c;
  const char *v = o->value[c];
  int r = o->env ? save_switch(o->env, c == o->dflt || !*v ? NULL : v) : save_renderer(v);
  rs_mods_logf("menu options: %s %s%s", o->env ? o->env : "renderer", *v ? v : "(default)",
               r ? " (not written)" : "");
}

// --- the methods in front of the game's ---------------------------------------------------------------------------
static uint64_t g_refresh, g_press, g_build, g_input;  // the game's own (slid addresses)

static uint64_t call1(uint64_t fn, uint64_t a) {
  const uint64_t args[] = {a};
  return rc_call_guest(fn, args, 1, NULL, 0, NULL);
}

// An option of one renderer can be changed while that renderer is the chosen one, the one the game starts with next.
static int usable(const option *o) {
  const option *renderer = NULL;  // (the option without a switch; its choice is METAL - 1 or OPENGL - 1)
  for (size_t i = 0; i < NOPTS && !renderer; i++)
    if (!g_opts[i].env) renderer = &g_opts[i];
  int r = renderer->now >= 0 ? renderer->now : renderer->dflt;
  return !o->only || o->only - 1 == r;
}

// A row the session sets: its value, and that it is the session's ("8X, SET FOR THIS SESSION").
static void set_session_value(uint64_t btn, const option *o) {
  const char *v = choice_text(o, o->now);
  char text[128];
  if (strncmp(v, "$[", 2)) {
    snprintf(text, sizeof text, "%s, SET FOR THIS SESSION", v);
  } else {
    gstr t;
    loc(&t, v);
    snprintf(text, sizeof text, "%s, SET FOR THIS SESSION", gstr_text(&t));
    gstr_free(&t);
  }
  set_text(btn, "CurrentSettingValue", text);
}

// The list's buttons after the game's (it calls it as the list is made, and again when Edit Setting closes): ours with
// their CurrentSettingValue, and those of the renderer not chosen locked (States.Locked: the overlay's edit of the
// movie greys the row) with the renderer they are for as their value, and those the session sets with its value.
static uint64_t my_refresh(uint64_t menu) {
  uint64_t r = call1(g_refresh, menu);
  uint64_t defn = RC_LD(uint64_t, menu + RC_OFF(menu_definition));
  for (size_t i = 0; defn && i < NOPTS; i++) {
    const option *o = &g_opts[i];
    uint64_t btn = OV_GUEST(json_find, defn, (uint64_t)(uintptr_t)o->id);
    if (!btn) continue;
    uint64_t states = OV_GUEST(json_find, btn, (uint64_t)(uintptr_t)"States");
    if (states) set_bool(states, "Locked", !usable(o) || o->session);
    if (usable(o) && o->session)
      set_session_value(btn, o);
    else
      set_loc(btn, "CurrentSettingValue",
              usable(o) ? choice_text(o, o->now) : o->only == METAL ? "METAL ONLY" : "OPENGL ONLY");
  }
  return r;
}

static uint64_t my_press(uint64_t menu) {
  const gstr *path = (const gstr *)(uintptr_t)(menu + RC_OFF(menu_pressed_path));
  option *o = opt_of_path(gstr_text(path));
  if (!o) return call1(g_press, menu);
  if (!usable(o) || o->session) return 0;  // locked: nothing to edit
  RC_ST(const char *, RC_GA(menu_current_setting), o->name);
  uint64_t screens = OV_GUEST(menu_screens, OV_GUEST(ui_current));
  gstr cls, alias;
  gstr_set(&cls, "EditSetting");
  gstr_set(&alias, "editsetting");
  uint64_t out = 0;
  OV_GUEST(menu_push, screens, (uint64_t)(uintptr_t)&cls, (uint64_t)(uintptr_t)&alias, (uint64_t)(uintptr_t)&out);
  ref_release(out);
  return 0;
}

// The edited setting's control, as the game's own builder makes Depth of Field's checkbox and Visual Quality's
// notched slider: on the setting's object the game's has made (its ID, the control's name, and the common fields).
static uint64_t my_build(uint64_t menu) {
  uint64_t r = call1(g_build, menu);
  option *o = opt_current();
  uint64_t defn = RC_LD(uint64_t, menu + RC_OFF(menu_definition));
  uint64_t obj = o && defn ? OV_GUEST(json_find, defn, (uint64_t)(uintptr_t)o->name) : 0;
  if (!obj) return r;
  if (!o->slider) {
    set_text(obj, "Component", "RSCheckBox");
    set_loc(obj, "Label", o->text[1]);
    set_bool(obj, "IsChecked", o->now == 1);
  } else {
    int c = o->now >= 0 ? o->now : o->dflt;
    set_text(obj, "Component", "RSSlider");
    set_loc(obj, "Label", choice_text(o, o->now));
    set_loc(obj, "Value", choice_text(o, o->now));
    set_number(obj, "InitialValue", c);
    set_number(obj, "XScale", RC_LD(double, RC_GA(menu_slider_xscale)));
    set_number(obj, "YScale", RC_LD(double, RC_GA(menu_slider_yscale)));
    set_bool(obj, "Notched", 1);
    set_range(obj, "AcceptedRange", 0, o->n - 1);
  }
  return r;
}

static int is_class(uint64_t ctl, uint64_t cls) {
  for (uint64_t c = OV_VCALL(ctl, RC_SLOT(ui_class)); c; c = RC_LD(uint64_t, c + RC_OFF(ui_class_parent)))
    if (c == cls) return 1;
  return 0;
}

// After the game's (a slider's change: its Value set to the slider's number): a change of our control saved, and a
// slider's Value the text of its choice, as the game's change sets Visual Quality's.
static uint64_t my_input(uint64_t menu, uint64_t player, uint64_t event, uint64_t a, uint64_t b, uint64_t c,
                         uint64_t d, uint64_t e) {
  const uint64_t args[] = {menu, player, event, a, b, c, d, e};
  uint64_t r = rc_call_guest(g_input, args, 8, NULL, 0, NULL);
  option *o = opt_current();
  uint64_t ctl = RC_LD(uint64_t, menu + RC_OFF(menu_focus));
  if (!o || !ctl) return r;
  if (!o->slider && is_class(ctl, RC_GA(ui_checkbox_class))) {
    int on = RC_LD(uint8_t, ctl + RC_OFF(ui_checkbox_checked)) != 0;
    if (on != (o->now == 1)) set_choice(o, on);
  } else if (o->slider && is_class(ctl, RC_GA(ui_slider_class))) {
    long at = lrintf(RC_LD(float, ctl + RC_OFF(ui_slider_value)));
    int ch = at < 0 ? 0 : at >= o->n ? o->n - 1 : (int)at;
    if (ch != o->now) set_choice(o, ch);
    uint64_t defn = RC_LD(uint64_t, menu + RC_OFF(menu_definition));
    uint64_t obj = defn ? OV_GUEST(json_find, defn, (uint64_t)(uintptr_t)o->name) : 0;
    if (obj) set_loc(obj, "Value", choice_text(o, ch));
  }
  return r;
}

// --- the start ----------------------------------------------------------------------------------------------------
// The slot SLOT of the vtable at VT, from the game's FN to NATIVE: 0 when it holds something else (left as it is).
static int put(uint64_t vt, int slot, uint64_t fn, uint64_t native, uint64_t *orig) {
  if (!vt) return 0;
  uint64_t at = vt + 8 * (uint64_t)slot, was = RC_LD(uint64_t, at);
  if (was != fn) return 0;
  *orig = was;
  RC_ST(uint64_t, at, native);
  return 1;
}

static int before_frame(rc_ctx *c, void *user) {
  (void)c;
  (void)user;
  static atomic_int done;
  if (atomic_exchange(&done, 1) || !rs_mods_cfg.overlay) return 0;
  for (size_t i = 0; i < NOPTS; i++) {
    option *o = &g_opts[i];
    char path[96];
    snprintf(path, sizeof path, "buttons.%s.states", o->id);
    o->path = strdup(path);
    o->name = (const char *)(uintptr_t)OV_GUEST(fixed_string_create, (uint64_t)(uintptr_t)o->id);
    o->session = session_set(o);
    o->now = choice_now(o);
    if (o->session)
      rs_mods_logf("menu options: %s=%s set for this session, its row locked", o->env ? o->env : "RS_GL_BACKEND",
                   getenv(o->env ? o->env : "RS_GL_BACKEND"));
  }
  uint64_t ads = RC_GA(menu_ads_vtable), copy = RC_GA(menu_ads_vtable_copy), edit = RC_GA(menu_edit_vtable);
  uint64_t refresh = RC_A(RC_ADDR(menu_ads_refresh)), press = RC_A(RC_ADDR(menu_ads_press));
  int n = put(ads, RC_SLOT(menu_refresh), refresh, (uint64_t)(uintptr_t)my_refresh, &g_refresh) +
          put(copy, RC_SLOT(menu_refresh), refresh, (uint64_t)(uintptr_t)my_refresh, &g_refresh) +
          put(ads, RC_SLOT(menu_press), press, (uint64_t)(uintptr_t)my_press, &g_press) +
          put(copy, RC_SLOT(menu_press), press, (uint64_t)(uintptr_t)my_press, &g_press) +
          put(edit, RC_SLOT(menu_build), RC_A(RC_ADDR(menu_edit_build)), (uint64_t)(uintptr_t)my_build, &g_build) +
          put(edit, RC_SLOT(menu_input), RC_A(RC_ADDR(menu_edit_input)), (uint64_t)(uintptr_t)my_input, &g_input);
  rs_mods_logf("menu options: %zu in Adv Display Settings, %d of 6 vtable slots set", NOPTS, n);
  return 0;
}

void rs_menu_options_start(void) {  // mods.c's start
  if (rc_hook_add_name("game_frame", before_frame, NULL, NULL))
    rs_mods_logf("no frame hook point: Adv Display Settings without RS Re-ARMed's options");
}
