// Adv Display Settings: RS Re-ARMed's own options in the game's own menu, the counterpart of the Mac's
// (tools/ws7/recomp/overrides/mods/menu_options.c, which says what each method does and why). The options, their
// switches and files and why one is locked are one source with it (menu_options_common.c); Learn & Play's list
// leaves out the renderer and the OpenGL engine, as its overlay does (cache_overlay.py, LP_MENU_OPTIONS).
//
// The Mac sets its methods in the classes' vtables; here the same five functions are `wrap` lines of the win32
// override module (tools/targets/names/overrides/win32.list), so the native one below runs in front of the
// translated one, whichever vtable or caller reaches it. Each is thiscall (ecx = the menu) and runs the game's own
// first, or instead of it only for a press on one of ours and for the scroll bar's drag:
//
//   menu_ads_refresh  after the game's: our buttons' CurrentSettingValue, and those that can't be changed now locked
//   menu_ads_press    a press on one of our buttons: the edited setting is ours and Edit Setting opens
//   menu_edit_build   after the game's: our control, a checkbox, or a notched slider of its choices
//   menu_edit_input   after the game's: a change of our control saved, and a slider's Value set
//   menu_ads_input    before the game's: a drag of the list's scroll bar
//
// What the game's helpers take differs from the Mac's in ways the tables say (tools/targets/win-x86/symbols.map):
// some have their own register arguments (rc32_call with the register set first), a JSON key is one word, the game's
// string is 24 bytes, json_set_array takes its out second, menu_push's last argument is an owner by value, and
// json_get_number, json_get_bool, menu_screens and get_resolution are inlined (made here from the same pieces as the
// game's code). What the game is given (a name, a key's or a result's place, a string) is guest memory: the
// options' names once (rc32_alloc), the rest on the guest stack below the caller's frame. The callee-saved registers
// are kept, as the game's callers expect. RS_MODS_OVERLAY=0: the originals only.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rc_abi.h"
#include "menu_options.h"
#include "mods_shared.h"
#include "rs_gfx.h"

enum { EAX = 0, ECX = 1, EDX = 2, EBX = 3, ESP = 4, EBP = 5, ESI = 6, EDI = 7 };

static inline uint32_t ld32(uint32_t g) {
  uint32_t v;
  memcpy(&v, RC32_H(g), 4);
  return v;
}
static inline void st32(uint32_t g, uint32_t v) { memcpy(RC32_H(g), &v, 4); }
// `ret` with eax (win32_overrides.c's): the return address into c->ret_to, esp past it.
static inline void ov_ret32(rc_ctx *c, uint32_t eax) {
  c->r[0] = eax;
  uint32_t sp = (uint32_t)c->r[4];
  memcpy(&c->ret_to, RC32_H(sp), 4);
  c->r[4] = sp + 4;
}

// --- a call's frame: the callee-saved registers kept, scratch on the guest stack ------------------------------
typedef struct {
  rc_ctx *c;
  uint64_t r[8];
  uint32_t top, at;  // scratch: from at up to top, below the stack pointer the frame was opened at
} frame;

static void frame_open(frame *f, rc_ctx *c) {
  f->c = c;
  memcpy(f->r, c->r, sizeof f->r);
  uint32_t sp = (uint32_t)c->r[ESP];
  f->top = (sp - 64) & ~15u;
  f->at = (sp - 0x1000) & ~15u;  // (4 KB: names, keys, strings, the settings record)
  c->r[ESP] = f->at - 16;        // the calls' own stack below it
}
static void frame_close(frame *f) {
  rc_ctx *c = f->c;
  // (ecx and edx too: rc32_call sets them, and menu_ads_input passes its event on to the game's thiscall after a
  // frame, ecx the menu)
  c->r[EAX] = f->r[EAX];
  c->r[ECX] = f->r[ECX];
  c->r[EDX] = f->r[EDX];
  c->r[EBX] = f->r[EBX];
  c->r[ESP] = f->r[ESP];
  c->r[EBP] = f->r[EBP];
  c->r[ESI] = f->r[ESI];
  c->r[EDI] = f->r[EDI];
}
static uint32_t scratch(frame *f, uint32_t n) {
  uint32_t g = f->at;
  f->at += (n + 15) & ~15u;
  if (f->at > f->top) rc_fail("menu options: no scratch left", n);
  memset(RC32_H(g), 0, n);
  return g;
}
static uint32_t gtext(frame *f, const char *s) {  // a C string the game can read
  size_t n = strlen(s) + 1;
  uint32_t g = scratch(f, (uint32_t)n);
  memcpy(RC32_H(g), s, n);
  return g;
}
static uint32_t call(frame *f, uint32_t fn, const uint32_t *args, int n, uint32_t ecx) {
  return rc32_call(f->c, fn, args, n, ecx, 0, 0);
}
static uint32_t vcall(frame *f, uint32_t obj, int slot, const uint32_t *args, int n) {
  return call(f, ld32(ld32(obj) + 4 * (uint32_t)slot), args, n, obj);
}

// --- the game's string (24 bytes) -------------------------------------------------------------------------------
// Its text is inline when +0x14 points at +0x10 (the end pointer's own place); otherwise the first word points at it.
static const char *gstr_text(uint32_t s) {
  return ld32(s + 0x14) == s + 0x10 ? (const char *)RC32_H(s) : (const char *)RC32_H(ld32(s));
}
static uint32_t gstr_inline(frame *f, const char *text) {  // up to 15 bytes, as the game makes them (0xf878f0)
  uint32_t s = scratch(f, 24);
  size_t n = strlen(text);
  if (n > 15) n = 15;
  memcpy(RC32_H(s), text, n);
  st32(s + 0x10, s + (uint32_t)n);
  st32(s + 0x14, s + 0x10);
  return s;
}

// --- the menus' JSON --------------------------------------------------------------------------------------------
static void ref_release(frame *f, uint32_t ref) {
  if (ref) vcall(f, ref, RC_SLOT(json_node_release), 0, 0);
}
static uint32_t json_find(frame *f, uint32_t obj, uint32_t name) {
  if (!obj) return 0;
  f->c->r[EAX] = name;
  return call(f, RC_ADDR(json_find), 0, 0, obj);
}
static uint32_t key_of(frame *f, const char *name) {  // the key's place
  uint32_t k = scratch(f, 4), args[] = {k};
  call(f, RC_ADDR(json_key_intern), args, 1, gtext(f, name));
  return k;
}
static void key_free(frame *f, uint32_t k) {
  if (!ld32(k)) return;
  f->c->r[ESI] = ld32(k);
  call(f, RC_ADDR(json_key_free), 0, 0, 0);
}
static void set_text(frame *f, uint32_t obj, const char *name, const char *text) {
  uint32_t k = key_of(f, name), out = scratch(f, 4), args[] = {obj, k, gtext(f, text)};
  f->c->r[EDI] = out;
  call(f, RC_ADDR(json_set_text), args, 3, 0);
  ref_release(f, ld32(out));
  key_free(f, k);
}
static void set_bool(frame *f, uint32_t obj, const char *name, int v) {
  uint32_t k = key_of(f, name), out = scratch(f, 4), args[] = {obj, k, v != 0};
  f->c->r[EDI] = out;
  call(f, RC_ADDR(json_set_bool), args, 3, 0);
  ref_release(f, ld32(out));
  key_free(f, k);
}
static void set_number(frame *f, uint32_t obj, const char *name, double v) {
  uint32_t k = key_of(f, name), out = scratch(f, 4), d[2];
  memcpy(d, &v, 8);
  uint32_t args[] = {obj, k, d[0], d[1]};
  f->c->r[EAX] = out;
  call(f, RC_ADDR(json_set_number), args, 4, 0);
  ref_release(f, ld32(out));
  key_free(f, k);
}
static uint32_t number(frame *f, double v) {  // a number value (a reference)
  uint32_t out = scratch(f, 4), d[2];
  memcpy(d, &v, 8);
  f->c->r[EDI] = out;
  call(f, RC_ADDR(num_intern), d, 2, 0);
  return ld32(out);
}
static void set_range(frame *f, uint32_t obj, const char *name, double lo, double hi) {  // [lo, hi]
  uint32_t k = key_of(f, name), out = scratch(f, 4), args[] = {obj, out, k};
  call(f, RC_ADDR(json_set_array), args, 3, 0);
  key_free(f, k);
  uint32_t arr = ld32(out);
  for (int i = 0; arr && i < 2; i++) {
    uint32_t n = number(f, i ? hi : lo);
    if (n) {
      f->c->r[ESI] = n;
      call(f, RC_ADDR(json_array_add), &arr, 1, 0);
    }
    ref_release(f, n);
  }
  ref_release(f, arr);
}
// The member NAME of OBJ (borrowed), as json_find reaches it, then its number or bool (json_get_number, _bool).
static uint32_t member(frame *f, uint32_t obj, const char *name) {
  uint32_t args[] = {gtext(f, name)};
  return obj ? vcall(f, obj, RC_SLOT(json_node_member), args, 1) : 0;
}
static int get_number(frame *f, double *out, uint32_t obj, const char *name) {
  uint32_t m = member(f, obj, name), n = m ? vcall(f, m, RC_SLOT(json_node_number), 0, 0) : 0;
  if (n) memcpy(out, RC32_H(n + RC_OFF(json_number_value)), 8);
  return n != 0;
}
static int get_bool(frame *f, uint8_t *out, uint32_t obj, const char *name) {
  uint32_t m = member(f, obj, name), b = m ? vcall(f, m, RC_SLOT(json_node_bool), 0, 0) : 0;
  if (b) *out = *(uint8_t *)RC32_H(b + 8);
  return b != 0;
}

// A text of the game's text table in the game's language ("$[row]": the row's own text), into OUT (n bytes).
static void loc(frame *f, const char *row, char *out, size_t n) {
  uint32_t s = scratch(f, 24), args[] = {ld32(RC_ADDR(service_holder)), 1, gtext(f, row)};
  call(f, RC_ADDR(localize_texts), args, 3, s);
  snprintf(out, n, "%s", gstr_text(s));
  call(f, RC_ADDR(efd_str_destroy), 0, 0, s);
}
static void set_loc(frame *f, uint32_t obj, const char *name, const char *text) {
  char t[256];
  if (strncmp(text, "$[", 2)) {
    set_text(f, obj, name, text);
    return;
  }
  loc(f, text, t, sizeof t);
  set_text(f, obj, name, t);
}

// --- the options ------------------------------------------------------------------------------------------------
typedef rs_menu_option option;
static uint32_t g_id[32], g_name[32];  // each option's id and its fixed string, in the guest
static int g_ready;
// A press on the list's scroll bar is being dragged (menu_ads_input); the list made again ends one whose release went
// to another menu, as the Mac's my_refresh does.
static int g_dragging;

static size_t index_of(const option *o) { return (size_t)(o - rs_menu_opts); }

// Once, at the first call: the options' values, and their names in the guest. 0 with RS_MODS_OVERLAY=0.
static int ready(rc_ctx *c) {
  if (g_ready) return g_ready > 0;
  g_ready = -1;
  if (!rs_mods_cfg.overlay || rs_menu_nopts > sizeof g_id / sizeof *g_id) return 0;
  rs_menu_opts_init(1);
  // A change taking effect at once where it can, as on the Mac: Sharpening, Anti-Aliasing, Texture Filtering and
  // Color are libRSOpenGL's (from the next frame); it takes no other. Low Latency and the Performance Overlay are the
  // window's layer's, set once as the game starts (lp_window.m): those at the next start.
  rs_menu_live_switch = rs_gfx_set_switch;
  frame f;
  frame_open(&f, c);
  for (size_t i = 0; i < rs_menu_nopts; i++) {
    size_t n = strlen(rs_menu_opts[i].id) + 1;
    g_id[i] = rc32_alloc((uint32_t)n);
    memcpy(RC32_H(g_id[i]), rs_menu_opts[i].id, n);
    g_name[i] = call(&f, RC_ADDR(fixed_string_create), &g_id[i], 1, 0);
    rs_menu_opts[i].name = g_name[i] ? (const char *)RC32_H(g_name[i]) : NULL;
  }
  frame_close(&f);
  size_t here = 0;
  for (size_t i = 0; i < rs_menu_nopts; i++) here += rs_menu_opt_here(&rs_menu_opts[i]);
  rs_mods_logf("menu options: %zu in Adv Display Settings", here);
  g_ready = 1;
  return 1;
}
static option *opt_current(void) {
  uint32_t cur = ld32(RC_ADDR(menu_current_setting));
  return cur ? rs_menu_opt_named((const char *)RC32_H(cur)) : NULL;
}

// A row the session sets: its value, and that it is the session's ("8X, SET FOR THIS SESSION").
static void set_session_value(frame *f, uint32_t btn, const option *o) {
  const char *v = rs_menu_choice_text(o, o->now);
  char t[128], text[200];
  if (!strncmp(v, "$[", 2)) {
    loc(f, v, t, sizeof t);
    v = t;
  }
  snprintf(text, sizeof text, "%s, %s", v, RS_MENU_SESSION);
  set_text(f, btn, "CurrentSettingValue", text);
}

// --- the methods in front of the game's ---------------------------------------------------------------------------
void RC_WFN(menu_ads_refresh)(rc_ctx *c);
void RC_WFN(menu_ads_press)(rc_ctx *c);
void RC_WFN(menu_ads_input)(rc_ctx *c);
void RC_WFN(menu_edit_build)(rc_ctx *c);
void RC_WFN(menu_edit_input)(rc_ctx *c);

void RC_FN(menu_ads_refresh)(rc_ctx *c) {
  uint32_t menu = (uint32_t)c->r[ECX];
  RC_WFN(menu_ads_refresh)(c);
  g_dragging = 0;
  if (!ready(c)) return;
  frame f;
  frame_open(&f, c);
  uint32_t defn = ld32(menu + RC_OFF(menu_definition)), mark = f.at;
  for (size_t i = 0; defn && i < rs_menu_nopts; i++) {
    const option *o = &rs_menu_opts[i];
    f.at = mark;  // (the scratch again for each button)
    if (!rs_menu_opt_here(o)) continue;
    uint32_t btn = json_find(&f, defn, g_id[i]);
    if (!btn) continue;
    uint32_t states = json_find(&f, btn, gtext(&f, "States"));
    const char *why = rs_menu_locked(o);
    if (states) set_bool(&f, states, "Locked", why != NULL);
    if (why == RS_MENU_SESSION)
      set_session_value(&f, btn, o);
    else
      set_loc(&f, btn, "CurrentSettingValue", why ? why : rs_menu_choice_text(o, o->now));
  }
  frame_close(&f);
}

void RC_FN(menu_ads_press)(rc_ctx *c) {
  uint32_t menu = (uint32_t)c->r[ECX];
  option *o = ready(c) ? rs_menu_opt_of_path(gstr_text(menu + RC_OFF(menu_pressed_path))) : NULL;
  if (!o) {
    RC_WFN(menu_ads_press)(c);
    return;
  }
  if (!rs_menu_locked(o) && g_name[index_of(o)]) {  // (a locked one: nothing to edit)
    frame f;
    frame_open(&f, c);
    st32(RC_ADDR(menu_current_setting), g_name[index_of(o)]);
    uint32_t screens = ld32(ld32(RC_ADDR(ui_manager)) + RC_OFF(ui_current_menu));
    uint32_t args[] = {gstr_inline(&f, "EditSetting"), gstr_inline(&f, "editsetting"), 0};
    call(&f, RC_ADDR(menu_push), args, 3, screens);
    frame_close(&f);
  }
  ov_ret32(c, 0);
}

void RC_FN(menu_edit_build)(rc_ctx *c) {
  uint32_t menu = (uint32_t)c->r[ECX];
  RC_WFN(menu_edit_build)(c);
  option *o = ready(c) ? opt_current() : NULL;
  if (!o) return;
  frame f;
  frame_open(&f, c);
  uint32_t defn = ld32(menu + RC_OFF(menu_definition));
  uint32_t obj = defn ? json_find(&f, defn, g_name[index_of(o)]) : 0;
  if (obj && !o->slider) {
    set_text(&f, obj, "Component", "RSCheckBox");
    set_loc(&f, obj, "Label", o->text[1]);
    set_bool(&f, obj, "IsChecked", o->now == 1);
  } else if (obj) {
    double xs, ys;
    memcpy(&xs, RC32_H(RC_ADDR(menu_slider_xscale)), 8);
    memcpy(&ys, RC32_H(RC_ADDR(menu_slider_yscale)), 8);
    set_text(&f, obj, "Component", "RSSlider");
    set_loc(&f, obj, "Label", rs_menu_choice_text(o, o->now));
    set_loc(&f, obj, "Value", rs_menu_choice_text(o, o->now));
    set_number(&f, obj, "InitialValue", rs_menu_choice(o));
    set_number(&f, obj, "XScale", xs);
    set_number(&f, obj, "YScale", ys);
    set_bool(&f, obj, "Notched", 1);
    set_range(&f, obj, "AcceptedRange", 0, o->n - 1);
  }
  frame_close(&f);
}

static int is_class(frame *f, uint32_t ctl, uint32_t cls) {
  for (uint32_t k = vcall(f, ctl, RC_SLOT(ui_class), 0, 0); k; k = ld32(k + RC_OFF(ui_class_parent)))
    if (k == cls) return 1;
  return 0;
}

void RC_FN(menu_edit_input)(rc_ctx *c) {
  uint32_t menu = (uint32_t)c->r[ECX];
  RC_WFN(menu_edit_input)(c);
  option *o = ready(c) ? opt_current() : NULL;
  uint32_t ctl = o ? ld32(menu + RC_OFF(menu_focus)) : 0;
  if (!ctl) return;
  frame f;
  frame_open(&f, c);
  if (!o->slider && is_class(&f, ctl, RC_ADDR(ui_checkbox_class))) {
    int on = *(uint8_t *)RC32_H(ctl + RC_OFF(ui_checkbox_checked)) != 0;
    if (on != (o->now == 1)) rs_menu_set_choice(o, on);
  } else if (o->slider && is_class(&f, ctl, RC_ADDR(ui_slider_class))) {
    float v;
    memcpy(&v, RC32_H(ctl + RC_OFF(ui_slider_value)), 4);
    long at = lrintf(v);
    int ch = at < 0 ? 0 : at >= o->n ? o->n - 1 : (int)at;
    if (ch != o->now) rs_menu_set_choice(o, ch);
    uint32_t defn = ld32(menu + RC_OFF(menu_definition));
    uint32_t obj = defn ? json_find(&f, defn, g_name[index_of(o)]) : 0;
    if (obj) set_loc(&f, obj, "Value", rs_menu_choice_text(o, ch));
  }
  frame_close(&f);
}

// --- dragging the list's scroll bar (the Mac's my_ads_input) -----------------------------------------------------
enum { EV_UP = 0xb, EV_DOWN = 0xc, EV_BUTTON = 0x1f, EV_POINTER = 0x22 };  // a: 0 press, 1 release

static void ads_key(frame *f, uint32_t menu, uint32_t player, uint32_t event) {  // a press and release
  for (uint32_t a = 0; a < 2; a++) {
    uint32_t args[] = {player, event, a, 0, 0, 0, 0};
    call(f, RC_ADDR(menu_ads_input), args, 7, menu);  // (through RC_FN below: not a button or a pointer)
  }
}

// The list's rows (its buttons), and the place of the focused one in their order (SortOrder; -1: none).
static int ads_rows(frame *f, uint32_t menu, int *focus) {
  uint32_t defn = ld32(menu + RC_OFF(menu_definition));
  uint32_t at = ld32(menu + RC_OFF(menu_button_names)), end = ld32(menu + RC_OFF(menu_button_names) + 4);
  double order[256], focused = -1;
  int n = 0;
  uint32_t mark = f->at;
  for (; defn && at < end && n < 256; at += 24, n++) {
    f->at = mark;
    uint32_t btn = json_find(f, defn, gtext(f, gstr_text(at)));
    uint32_t states = btn ? json_find(f, btn, gtext(f, "States")) : 0;
    uint8_t on = 0;
    if (!btn || !get_number(f, &order[n], btn, "SortOrder")) order[n] = n;
    if (states && get_bool(f, &on, states, "Focus") && on) focused = order[n];
  }
  f->at = mark;
  *focus = -1;
  for (int k = 0; focused >= 0 && k < n; k++) *focus += order[k] <= focused;
  return n;
}

static void drag_to(frame *f, uint32_t menu, uint32_t player, double y) {
  int focus, n = ads_rows(f, menu, &focus);
  if (n < 2 || focus < 0) return;
  int row = rs_menu_bar_row(n, y);
  for (; focus < row; focus++) ads_key(f, menu, player, EV_DOWN);
  for (; focus > row; focus--) ads_key(f, menu, player, EV_UP);
}

void RC_FN(menu_ads_input)(rc_ctx *c) {
  uint32_t menu = (uint32_t)c->r[ECX], sp = (uint32_t)c->r[ESP];
  uint32_t player = ld32(sp + 4), event = ld32(sp + 8), a = ld32(sp + 12);
  int32_t b = (int32_t)ld32(sp + 16), y_ = (int32_t)ld32(sp + 20);
  if (!(event == EV_BUTTON || (event == EV_POINTER && g_dragging)) || !ready(c)) {
    RC_WFN(menu_ads_input)(c);
    return;
  }
  frame f;
  frame_open(&f, c);
  uint32_t rec = scratch(&f, 0x44);  // the display settings (settings_read: esi = the record)
  f.c->r[ESI] = rec;
  call(&f, RC_ADDR(settings_read), 0, 0, 0);
  int w = (int)ld32(rec + RC_OFF(settings_width)), h = (int)ld32(rec + RC_OFF(settings_height));
  double x = w > 0 ? (double)b * STAGE_W / w : -1, y = h > 0 ? (double)y_ * STAGE_H / h : -1;
  int was = g_dragging;
  if (event == EV_BUTTON) g_dragging = a == 0 && rs_menu_on_bar(x, y);
  if (g_dragging) drag_to(&f, menu, player, y);
  frame_close(&f);
  if (!g_dragging && !was) {
    RC_WFN(menu_ads_input)(c);
    return;
  }
  ov_ret32(c, 0);
  c->r[ESP] += 0x1c;  // ret 0x1c
}
