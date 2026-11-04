// The game window's rules (lp_window.m), in plain C so the tests run them without AppKit.
#pragma once
#include <stdint.h>

// RS_FULLSCREEN: "1" full screen, "0" windowed, whatever the game asks; unset or empty: -1, the
// game decides.
int lp_force_mode(const char *env);

// A window style the game sets (SetWindowLong GWL_STYLE) is its full screen: WS_POPUP without a
// caption (Fullscreen=1: 0x90000000; Fullscreen=0: WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX).
int lp_style_full(uint32_t style);

// The mode a new device's window starts in: forced, else full screen for a full-screen device
// (Fullscreen=2: the game's own resolution, never a display mode change here) or a back buffer the
// size of the desktop (Fullscreen=1 makes a windowed device the size of the desktop).
int lp_initial_full(int force, int windowed, int w, int h, int dw, int dh);

// The content size in points of a window showing a w x h back buffer at `scale` pixels per point,
// shrunk (aspect kept, whole points) to fit a vw x vh content area; never below 1x1.
void lp_fit_window(int w, int h, double scale, double vw, double vh, double *ow, double *oh);

// A point in a vw x vh view (points, origin top left) -> the pixel of a w x h back buffer shown
// aspect-fit (letterboxed) in it, clamped to the back buffer; *k is the points per pixel.
void lp_view_to_pixels(double px, double py, double vw, double vh, int w, int h, int32_t *x, int32_t *y,
                       double *k);
