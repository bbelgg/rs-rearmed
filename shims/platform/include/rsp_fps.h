// RS_FPS=N (not in the original; the installer's Frame rate), in both editions: the game starts a frame every 1/N s on a clock
// of ours instead of at the display's refresh, and its frames are not held for the display (vsync off), so it can
// be run at another rate than the display's (e.g. 120 on a 60 Hz screen). 0 or unset: the display's rate.
//
// Each edition waits where its frames start: the Mac's render thread before a frame (src/rsp_shell.m), Learn &
// Play's Present after one (win-x86/shims/d3d9/d3d9_device.c), which keeps a hidden window to its display's rate
// on the same clock.
#ifndef RSP_FPS_H
#define RSP_FPS_H

#include <mach/mach_time.h>
#include <stdint.h>
#include <stdlib.h>

// The rate a value of RS_FPS asks for: a whole number from 1 to 1000, else 0 (a number with something after it,
// "1e3" or "60.5", is not one: its start alone would be another rate than the one asked for).
static inline int rsp_fps_value(const char *e) {
  if (!e || !*e) return 0;
  char *end;
  long v = strtol(e, &end, 10);
  return *end || v < 1 || v > 1000 ? 0 : (int)v;
}

// RS_FPS, as the process started with it.
static inline int rsp_fps(void) {
  static int v = -1;
  if (v < 0) v = rsp_fps_value(getenv("RS_FPS"));
  return v;
}

// The clock of a run of paced frames, in any one unit. *next is when the frame before could start, 0 before the
// first. Returns when the next one may, or 0 for at once: a late frame starts at once and the clock goes on from
// it, so missed frames don't queue up.
static inline uint64_t rsp_fps_step(uint64_t *next, uint64_t period, uint64_t now) {
  if (!*next) *next = now;
  *next += period;
  if (*next < now) {
    *next = now;
    return 0;
  }
  return *next;
}

// Nanoseconds in mach_absolute_time's ticks.
static inline uint64_t rsp_fps_ticks(uint64_t ns) {
  static mach_timebase_info_data_t tb;
  if (!tb.denom) mach_timebase_info(&tb);
  return ns * tb.denom / tb.numer;
}

// Waits for the next frame's start; `period` in ticks, *next the caller's, 0 at first.
static inline void rsp_fps_wait(uint64_t *next, uint64_t period) {
  uint64_t until = rsp_fps_step(next, period, mach_absolute_time());
  if (until) mach_wait_until(until);
}

#endif
