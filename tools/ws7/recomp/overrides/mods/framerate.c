// Frame rate (framerate.h says what and why): the hooks, one source for both editions. Each is at a mid point of
// tools/targets/names/mids.list, whose place and values each game's mids.map gives; the frame's time is the game's
// own, read by name. Built into both apps (overrides/mods/build.sh, win-x86/build/build_shims.sh) and started by
// their built-in fixes (mods.c, win-x86/shims/win32_overrides.c).
#include <stdlib.h>

#include "rc_mod.h"

#include "framerate.h"

// The game's time for this frame, in seconds; a 60 Hz frame where the game has none to read, which leaves it be.
static float frame_dt(void) {
  if (!RC_HAS_OFF(service_holder_frame_dt)) return RS_FR_REF_DT;
  const char *holder = rc_mod_ptr((const void *)(uintptr_t)RC_GA(service_holder));
  return holder ? RC_LD(float, holder + RC_OFF(service_holder_frame_dt)) : RS_FR_REF_DT;
}

// The points, in the order of their values below
enum { P_WINDOW, P_STEP_PLAY, P_STEP_MUTE, P_HOLD, P_MISS, P_VOLUME, P_INSTRUMENT, P_HINT };
static const struct {
  const char *name, *value, *who, *cont;
} g_def[RS_FR_POINTS] = {
    [P_WINDOW] = {"fps_calib_window", "window", NULL, NULL},
    [P_STEP_PLAY] = {"fps_calib_step_play", "step", NULL, NULL},
    [P_STEP_MUTE] = {"fps_calib_step_mute", "step", NULL, NULL},
    [P_HOLD] = {"fps_panel_hold", "step", NULL, NULL},
    [P_MISS] = {"fps_tuner_miss", "misses", "tuner", "grace"},
    [P_VOLUME] = {"fps_tuner_volume", NULL, "tuner", "skip"},
    [P_INSTRUMENT] = {"fps_tuner_instrument", NULL, "tuner", "skip"},
    [P_HINT] = {"fps_tuner_hint", NULL, "menu", "skip"},
};
static struct {
  int on;  // hooked, with every value and continuation its hook reads found in this game's table
  int value, who, cont;
  rs_fr_slots slots;
} g_pt[RS_FR_POINTS];

// The calibration window: within the buffer it averages
static void on_window(rc_mid_frame *f, void *user) {
  if (!g_pt[(intptr_t)user].on) return;
  int v = g_pt[(intptr_t)user].value;
  rc_mid_set(f, v, rs_fr_window((uint32_t)rc_mid_get(f, v)));
}

// A step of the level, or of a panel's timer, the game takes each frame
static void on_step(rc_mid_frame *f, void *user) {
  float dt = frame_dt();
  if (!g_pt[(intptr_t)user].on || !rs_fr_fast(dt)) return;
  int v = g_pt[(intptr_t)user].value;
  rc_mid_set_f(f, v, rs_fr_step((float)rc_mid_get_f(f, v), dt));
}

// The tuner's misses in a row, where the game compares them with its three: too soon for a 60 Hz game, and it goes on
// as with fewer. The count is the game's to keep (in Learn & Play the value is the stored counter), so it is only read.
static void on_miss(rc_mid_frame *f, void *user) {
  intptr_t k = (intptr_t)user;
  if (!g_pt[k].on) return;
  rs_fr_slot *s = rs_fr_slot_of(&g_pt[k].slots, (const void *)(uintptr_t)rc_mid_get(f, g_pt[k].who));
  uint32_t misses = (uint32_t)rc_mid_get(f, g_pt[k].value);
  if (rs_fr_misses(s, misses, frame_dt()) < RS_FR_MISSES && misses >= RS_FR_MISSES) f->go = g_pt[k].cont;
}

// A check the game makes each frame: the ones a 60 Hz game would not have made return at once
static void on_gate(rc_mid_frame *f, void *user) {
  intptr_t k = (intptr_t)user;
  float dt = frame_dt();
  if (!g_pt[k].on || !rs_fr_fast(dt)) return;
  rs_fr_slot *s = rs_fr_slot_of(&g_pt[k].slots, (const void *)(uintptr_t)rc_mid_get(f, g_pt[k].who));
  if (!rs_fr_gate(s, dt)) f->go = g_pt[k].cont;
}

int rs_framerate_init(void) {
  const char *e = getenv("RS_MODS_FRAME_RATE");
  if (e && (*e == '0' || *e == 'n' || *e == 'N' || *e == 'f' || *e == 'F')) return -1;
  static const rc_mid_fn fn[RS_FR_POINTS] = {
      [P_WINDOW] = on_window, [P_STEP_PLAY] = on_step,   [P_STEP_MUTE] = on_step,   [P_HOLD] = on_step,
      [P_MISS] = on_miss,     [P_VOLUME] = on_gate,      [P_INSTRUMENT] = on_gate,  [P_HINT] = on_gate,
  };
  int n = 0;
  for (intptr_t k = 0; k < RS_FR_POINTS; k++) {
    int p = rc_mid_add_name(g_def[k].name, fn[k], (void *)k);  // a point this game hasn't: a log line, and the rest go on
    if (p < 0 || rc_mid_is_none(p)) continue;
    g_pt[k].value = g_def[k].value ? rc_mid_value(p, g_def[k].value) : 0;
    g_pt[k].who = g_def[k].who ? rc_mid_value(p, g_def[k].who) : 0;
    g_pt[k].cont = g_def[k].cont ? rc_mid_cont(p, g_def[k].cont) : 0;
    if (g_pt[k].value < 0 || g_pt[k].who < 0 || g_pt[k].cont < 0) continue;  // a table that says otherwise: logged, left be
    g_pt[k].on = 1;
    n++;
  }
  return n;
}
