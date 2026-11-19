// RS_METAL_SCALE=N (not in the original; the installer's Render scale), in both editions: the game renders at N% of
// its size in each direction, 50 to 100, and each frame is scaled up to the screen's pixels (MetalFX), as a Retina
// screen's points already are. Less GPU work for a softer picture, menus and text included. With RS_METAL_RETINA=1
// it is N% of the backing pixels. Unset, or any other value: 100.
//
// The Mac edition sizes its drawable and the engine's resolution by it (src/rsp_present_metal.m), Learn & Play the
// desktop and the window the game sees (win-x86/app/lp_window.m).
#ifndef RSP_SCALE_H
#define RSP_SCALE_H

#include <stdlib.h>

// The fraction a value of RS_METAL_SCALE asks for: a whole number from 50 to 100, else 1.
static inline double rsp_scale_value(const char *e) {
  if (!e || !*e) return 1.0;
  char *end;
  long v = strtol(e, &end, 10);
  return *end || v < 50 || v > 100 ? 1.0 : v / 100.0;
}

// RS_METAL_SCALE, as the process started with it.
static inline double rsp_metal_scale(void) {
  static double v;
  if (v == 0) v = rsp_scale_value(getenv("RS_METAL_SCALE"));
  return v;
}

#endif
