// The Metal layer's switches (not in the original; the installer's and Adv Display Settings'), in both editions:
//
//   RS_METAL_DRAWABLES=2|3  the layer's drawables, else 3 with vsync (Apple's default: a late frame does not block
//                           nextDrawable for a whole refresh) and 2 without (RS_FPS: presents are not held for the
//                           display, so a third only adds latency). 2 with vsync is low-latency presentation: a
//                           frame is on the screen a refresh sooner, and one that is late shows as a repeated one.
//   RS_METAL_HUD=1          Apple's Metal Performance HUD over the game: its frame rate, frame times and GPU
//                           memory, with Metal only (the Mac edition's OpenGL renderer has none). It's enabled before
//                           Metal starts (MTL_HUD_ENABLED, rsp_layer_env, first in each main) with RS_METAL_HUD=1
//                           only: enabled, Metal loads its HUD library (and its menu items), and the HUD costs CPU
//                           time each frame even hidden. The layer's developerHUDProperties (macOS 13 and later,
//                           where it's documented: rsp_layer_show_hud) show or hide it, so a game started with it can
//                           hide it and show it again as it runs (the Mac's Adv Display Settings); one started
//                           without it has it from its next start.
//
// Each edition's presenter applies them to its CAMetalLayer: the Mac's src/rsp_present_metal.m, Learn & Play's
// win-x86/app/lp_window.m.
#ifndef RSP_LAYER_H
#define RSP_LAYER_H

#include <stdlib.h>
#include <string.h>

// The drawables a value of RS_METAL_DRAWABLES asks for: 2 or 3, else 0 (the default's).
static inline int rsp_layer_drawables_value(const char *e) {
  if (!e || !*e) return 0;
  char *end;
  long n = strtol(e, &end, 10);
  return !*end && (n == 2 || n == 3) ? (int)n : 0;
}

// The layer's drawables for presents held for the display (sync) or not.
static inline int rsp_layer_drawables(int sync) {
  static int forced = -1;
  if (forced < 0) forced = rsp_layer_drawables_value(getenv("RS_METAL_DRAWABLES"));
  return forced ? forced : sync ? 3 : 2;
}

// Whether a value of RS_METAL_HUD turns it on: unless unset, empty, or starting with 0, n, N, f or F (as the launchers
// read toggles).
static inline int rsp_layer_hud_value(const char *e) { return e && *e && !strchr("0nNfF", *e); }

// RS_METAL_HUD.
static inline int rsp_layer_hud(void) {
  static int on = -1;
  if (on < 0) on = rsp_layer_hud_value(getenv("RS_METAL_HUD"));
  return on;
}

// What rsp_layer_env does with MTL_HUD_ENABLED for the HUD asked for or not (HUD) in a process that presents with
// OpenGL (GL) or Metal: 1 sets it, -1 takes it out (OpenGL: main's restart for it has the first start's), 0 leaves it
// as it is (Apple's own switch).
static inline int rsp_layer_hud_env(int hud, int gl) { return !hud ? 0 : gl ? -1 : 1; }

// First in main, before anything starts Metal: the HUD's own switch for RS_METAL_HUD, with Metal only (never for a
// start without it: see RS_METAL_HUD above). GL: the Mac edition's OpenGL renderer, RS_GL_BACKEND=gl as the launcher
// passes it for OpenGL and main sets it to start again with OpenGL (rsp_renderer.h); Learn & Play's renderer is Metal.
static inline void rsp_layer_env(int gl) {
  int e = rsp_layer_hud_env(rsp_layer_hud(), gl);
  if (e > 0) setenv("MTL_HUD_ENABLED", "1", 1);
  if (e < 0) unsetenv("MTL_HUD_ENABLED");
}

#ifdef __OBJC__
#import <QuartzCore/CAMetalLayer.h>

// The HUD on LAYER shown, or hidden.
static inline void rsp_layer_show_hud(CAMetalLayer *layer, int on) {
  if (@available(macOS 13.0, *))
    layer.developerHUDProperties = @{@"mode" : on ? @"default" : @"disabled", @"logging" : @"disabled"};
}
#endif

#endif
