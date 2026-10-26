// Decisions and arithmetic from the ObjC methods, kept free of AppKit so they
// can be unit-tested and compared. Each names the method it comes from.
#include <math.h>

#include "rsp_platform.h"

uint32_t rsp_pack_keydown_char(uint16_t ch, uint64_t flags) {
  // The character in the low bits; modifierFlags bit 20 (Command) moves to
  // bit 31 of the 32-bit value (0x100184bb7).
  return (uint32_t)ch | (((uint32_t)flags << 11) & 0x80000000u);
}

int rsp_flags_changed_forwards(uint64_t flags) { return (flags & RSP_MOD_CONTROL) != 0; }

int rsp_is_fullscreen_toggle(uint16_t ch, uint64_t flags) {
  return ch == 'F' && (flags & (RSP_MOD_COMMAND | RSP_MOD_SHIFT)) == (RSP_MOD_COMMAND | RSP_MOD_SHIFT);
}

int rsp_mouse_coord(double v) {
  // Rounded as v + 0.5 narrowed to float, floored, converted to a 64-bit
  // integer; the low 32 bits are passed on.
  float f = floorf((float)(v + 0.5));
  if (!(f > -9.2233720368547758e18f && f < 9.2233720368547758e18f))
    return (int)(int64_t)0x8000000000000000ull;  // x86's out-of-range result ("integer indefinite")
  return (int)(int64_t)f;
}

int rsp_scroll_event(double dy, int *a) {
  float f = (float)dy;  // compared as a float
  if (f == 0.0f) return 0;
  *a = f > 0.0f;  // false for NaN too, but NaN is "!= 0" and still posts
  return 1;
}

void rsp_aspect_from_width(int w, int *ax, int *ay) {
  int v = w;
  while (v & 0xf) v++;
  *ax = v;
  *ay = (v * 9) / 16;  // truncating signed division
}

int rsp_cursor_policy(double now, double last_input, int *visible) {
  if (now - last_input > RSP_CURSOR_IDLE_SECS) {  // false for NaN
    if (*visible) {
      *visible = 0;
      return -1;
    }
    return 0;
  }
  if (!*visible) {
    *visible = 1;
    return 1;
  }
  return 0;
}

int rsp_timer_mode_switch(int in_fs_mode, int engine_fs) {
  if ((signed char)in_fs_mode == (engine_fs != 0)) return RSP_TIMER_NONE;
  return engine_fs ? RSP_TIMER_GO_FULLSCREEN : RSP_TIMER_GO_WINDOW;
}

int rsp_timer_fullscreen_refit(double sw, double sh, double ww, double wh) {
  return (sw == ww && sh == wh) ? RSP_TIMER_NONE : RSP_TIMER_GO_FULLSCREEN;
}

int rsp_timer_window_adopt(double cw, double ch, int ew, int eh) {
  return !((double)ew == cw && (double)eh == ch);
}

int rsp_timer_swap(int no_vsync, int num_swap) {
  int flip = (num_swap == 0) ^ (no_vsync != 0);
  return flip ? (num_swap ^ 1) : -1;
}
