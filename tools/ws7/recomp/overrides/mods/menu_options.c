// Adv Display Settings: RS Re-ARMed's own options in the game's own menu (not in the original).
//
// The overlay (tools/play-native/cache_overlay.py, MENU_OPTIONS) adds a button for each option to
// AdvancedDisplaySettings' definition (manifests/ui_menu_pillar_options.database.json), its label and description
// rows of the game's text table, and the game lists, sorts and shows them with its own. Its code knows only its own
// settings, so five of its methods have a native one in front of them, set in the classes' vtables at the first frame
// (each slot only while it holds the game's own function): a vtable entry outside the image is called as a native
// function with the guest's arguments (rc_icall_native), and each calls the game's own first, or instead of it for
// what isn't ours.
//
//   AdvancedDisplaySettings, menu_refresh_slot  after the game's (as the list is made, and when Edit Setting closes):
//                                               our buttons' CurrentSettingValue, the value the list's "CURRENT
//                                               SETTING:" line shows, and those that can't be changed now locked,
//                                               greyed by the overlay's edit of the movie, with why (locked: the
//                                               other renderer's, Low Latency at a fixed rate, MetalFX with nothing
//                                               upscaled, the session's switches)
//   AdvancedDisplaySettings, menu_press_slot    a press on one of our buttons ("Buttons.<id>.States"): the edited
//                                               setting is ours and Edit Setting opens, as for the game's own (not
//                                               for a locked one); any other press is the game's
//   EditSetting, menu_build_slot                after the game's (the setting's object, its ID the control's name,
//                                               and the common fields): ours, a checkbox, or a notched slider of its
//                                               choices as the game makes Visual Quality's
//   EditSetting, menu_input_slot                after the game's: a change of our control, saved, and a slider's
//                                               Value (the choice it shows) set
//   AdvancedDisplaySettings, menu_input_slot    before the game's: a drag of the list's scroll bar (below)
//
// The options themselves, their switches and files and why one is locked, are one source with Learn & Play's
// (menu_options_common.c; its methods: win-x86/shims/win32_menu_options.c). RS_MODS_OVERLAY=0: none of this, the menu
// as the game has it.
#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "menu_options.h"
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

// --- the options (menu_options_common.c, one source with Learn & Play) ----------------------------------------
typedef rs_menu_option option;
#define g_opts rs_menu_opts
#define NOPTS rs_menu_nopts
#define opt_of_path rs_menu_opt_of_path
#define choice_text rs_menu_choice_text
#define set_choice rs_menu_set_choice
#define locked rs_menu_locked
#define SESSION RS_MENU_SESSION
// The Mac shell's (shims/platform/include/rsp_renderer.h).
extern int rsp_renderer_env_given(void) __attribute__((weak_import));
// The Mac shell's (shims/platform/src/rsp_present.h): a switch changed as the game runs, taken from the next frame
// on where it can be (Low Latency, Sharpening, Anti-Aliasing, Texture Filtering, Color, Performance Overlay).
extern int rsp_live_switch(const char *name, const char *value) __attribute__((weak_import));

static option *opt_current(void) { return rs_menu_opt_named(RC_LD(const char *, RC_GA(menu_current_setting))); }

// --- the methods in front of the game's ---------------------------------------------------------------------------
static uint64_t g_refresh, g_press, g_build, g_input;  // the game's own (slid addresses)
// A press on the list's scroll bar is being dragged (my_ads_input). The list made again ends one whose release went
// to another menu (Esc while the button was held), so a reopened list doesn't follow the pointer.
static int g_dragging;

static uint64_t call1(uint64_t fn, uint64_t a) {
  const uint64_t args[] = {a};
  return rc_call_guest(fn, args, 1, NULL, 0, NULL);
}

// A row the session sets: its value, and that it is the session's ("8X, SET FOR THIS SESSION").
static void set_session_value(uint64_t btn, const option *o) {
  const char *v = choice_text(o, o->now);
  char text[128];
  if (strncmp(v, "$[", 2)) {
    snprintf(text, sizeof text, "%s, %s", v, SESSION);
  } else {
    gstr t;
    loc(&t, v);
    snprintf(text, sizeof text, "%s, %s", gstr_text(&t), SESSION);
    gstr_free(&t);
  }
  set_text(btn, "CurrentSettingValue", text);
}

// The list's buttons after the game's (it calls it as the list is made, and again when Edit Setting closes): ours with
// their CurrentSettingValue, and those that can't be changed now locked (States.Locked: the overlay's edit of the
// movie greys the row) with why as their value (the session's: its value too).
static uint64_t my_refresh(uint64_t menu) {
  uint64_t r = call1(g_refresh, menu);
  g_dragging = 0;
  uint64_t defn = RC_LD(uint64_t, menu + RC_OFF(menu_definition));
  for (size_t i = 0; defn && i < NOPTS; i++) {
    const option *o = &g_opts[i];
    uint64_t btn = OV_GUEST(json_find, defn, (uint64_t)(uintptr_t)o->id);
    if (!btn) continue;
    uint64_t states = OV_GUEST(json_find, btn, (uint64_t)(uintptr_t)"States");
    const char *why = locked(o);
    if (states) set_bool(states, "Locked", why != NULL);
    if (why == SESSION)
      set_session_value(btn, o);
    else
      set_loc(btn, "CurrentSettingValue", why ? why : choice_text(o, o->now));
  }
  return r;
}

static uint64_t my_press(uint64_t menu) {
  const gstr *path = (const gstr *)(uintptr_t)(menu + RC_OFF(menu_pressed_path));
  option *o = opt_of_path(gstr_text(path));
  if (!o) return call1(g_press, menu);
  if (locked(o)) return 0;  // nothing to edit
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

// --- dragging the list's scroll bar ------------------------------------------------------------------------------
// The overlay's edit of SettingsScene puts the game's own scroll bar beside the panel (its numbers: menu_options.h).
// The game's
// lists drag theirs in their own input (ListScene's thumb: the row whose place the pointer is at, the focus moved
// there); Adv Display Settings' has no such code, so its input has this in front of it: a press on the bar starts
// a drag, the game's own Up and Down move the focus to the row under the pointer as the bar shows rows (the thumb's
// middle), and the release ends it. Neither reaches the game, so a row isn't clicked. Its events: the pointer's
// place in the game's resolution, the movie's 1280 x 720 scaled to it (as its hit test has it).
enum { EV_UP = 0xb, EV_DOWN = 0xc, EV_BUTTON = 0x1f, EV_POINTER = 0x22 };  // a: 0 press, 1 release

static uint64_t g_ads_input;

static void ads_key(uint64_t menu, uint64_t player, uint64_t event) {  // a press and release, as the keys make them
  for (uint64_t a = 0; a < 2; a++) {
    const uint64_t args[] = {menu, player, event, a, 0, 0, 0, 0};
    rc_call_guest(g_ads_input, args, 8, NULL, 0, NULL);
  }
}

// The list's rows (its buttons), and the place of the focused one in their order (SortOrder; -1: none).
static int ads_rows(uint64_t menu, int *focus) {
  uint64_t defn = RC_LD(uint64_t, menu + RC_OFF(menu_definition));
  uint64_t at = RC_LD(uint64_t, menu + RC_OFF(menu_button_names));
  uint64_t end = RC_LD(uint64_t, menu + RC_OFF(menu_button_names) + 8);
  double order[256], focused = -1;
  int n = 0;
  for (; defn && at < end && n < 256; at += 32, n++) {
    uint64_t btn = OV_GUEST(json_find, defn, (uint64_t)(uintptr_t)gstr_text((const gstr *)(uintptr_t)at));
    uint64_t states = btn ? OV_GUEST(json_find, btn, (uint64_t)(uintptr_t)"States") : 0;
    uint8_t on = 0;
    const char *sort = "SortOrder", *focus_key = "Focus";
    if (!btn || !(uint8_t)OV_GUEST(json_get_number, (uint64_t)(uintptr_t)&order[n], btn, (uint64_t)(uintptr_t)sort))
      order[n] = n;
    if (states && (uint8_t)OV_GUEST(json_get_bool, (uint64_t)(uintptr_t)&on, states, (uint64_t)(uintptr_t)focus_key) &&
        on)
      focused = order[n];
  }
  *focus = -1;
  for (int k = 0; focused >= 0 && k < n; k++) *focus += order[k] <= focused;
  return n;
}

// The focus moved to the row whose thumb place is at Y (the movie's).
static void drag_to(uint64_t menu, uint64_t player, double y) {
  int focus, n = ads_rows(menu, &focus);
  if (n < 2 || focus < 0) return;
  int row = rs_menu_bar_row(n, y);
  for (; focus < row; focus++) ads_key(menu, player, EV_DOWN);
  for (; focus > row; focus--) ads_key(menu, player, EV_UP);
}

static uint64_t my_ads_input(uint64_t menu, uint64_t player, uint64_t event, uint64_t a, uint64_t b, uint64_t c,
                             uint64_t d, uint64_t e) {
  if (event == EV_BUTTON || (event == EV_POINTER && g_dragging)) {
    int w = 0, h = 0;
    OV_GUEST(get_resolution, (uint64_t)(uintptr_t)&w, (uint64_t)(uintptr_t)&h);
    double x = w > 0 ? (double)(int)b * STAGE_W / w : -1, y = h > 0 ? (double)(int)c * STAGE_H / h : -1;
    int was = g_dragging;
    if (event == EV_BUTTON) g_dragging = a == 0 && rs_menu_on_bar(x, y);
    if (g_dragging) drag_to(menu, player, y);
    if (g_dragging || was) return 0;
  }
  const uint64_t args[] = {menu, player, event, a, b, c, d, e};
  return rc_call_guest(g_ads_input, args, 8, NULL, 0, NULL);
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
  rs_menu_renderer_env_given = rsp_renderer_env_given;
  rs_menu_live_switch = rsp_live_switch;
  rs_menu_opts_init(0);
  for (size_t i = 0; i < NOPTS; i++)
    g_opts[i].name = (const char *)(uintptr_t)OV_GUEST(fixed_string_create, (uint64_t)(uintptr_t)g_opts[i].id);
  uint64_t ads = RC_GA(menu_ads_vtable), copy = RC_GA(menu_ads_vtable_copy), edit = RC_GA(menu_edit_vtable);
  uint64_t refresh = RC_A(RC_ADDR(menu_ads_refresh)), press = RC_A(RC_ADDR(menu_ads_press));
  int n = put(ads, RC_SLOT(menu_refresh), refresh, (uint64_t)(uintptr_t)my_refresh, &g_refresh) +
          put(copy, RC_SLOT(menu_refresh), refresh, (uint64_t)(uintptr_t)my_refresh, &g_refresh) +
          put(ads, RC_SLOT(menu_press), press, (uint64_t)(uintptr_t)my_press, &g_press) +
          put(copy, RC_SLOT(menu_press), press, (uint64_t)(uintptr_t)my_press, &g_press) +
          put(edit, RC_SLOT(menu_build), RC_A(RC_ADDR(menu_edit_build)), (uint64_t)(uintptr_t)my_build, &g_build) +
          put(edit, RC_SLOT(menu_input), RC_A(RC_ADDR(menu_edit_input)), (uint64_t)(uintptr_t)my_input, &g_input);
  uint64_t input = RC_A(RC_ADDR(menu_ads_input));
  n += put(ads, RC_SLOT(menu_input), input, (uint64_t)(uintptr_t)my_ads_input, &g_ads_input) +
       put(copy, RC_SLOT(menu_input), input, (uint64_t)(uintptr_t)my_ads_input, &g_ads_input);
  rs_mods_logf("menu options: %zu in Adv Display Settings, %d of 8 vtable slots set", NOPTS, n);
  return 0;
}

void rs_menu_options_start(void) {  // mods.c's start
  if (rc_hook_add_name("game_frame", before_frame, NULL, NULL))
    rs_mods_logf("no frame hook point: Adv Display Settings without RS Re-ARMed's options");
}
