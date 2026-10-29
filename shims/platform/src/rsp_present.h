// Metal presentation for the shell (docs/METAL-PLAN.md step 6), rsp_present_metal.m. Not in the
// original. With libRSOpenGL loaded and RS_GL_BACKEND=metal (rs_gfx_backend() == RS_GFX_METAL),
// OpenGLView draws into a CAMetalLayer through an rs_gfx layer context instead of an NSOpenGLContext;
// otherwise nothing here is used and the GL path is the original's.
//
// What replaces what (rsp_shell.m):
//   NSOpenGLContext + pixel format     rsp_mtl_attach: a CAMetalLayer hosted by the view, and
//                                      rs_gfx_create_layer(layer, w, h)
//   makeCurrentContext                 rsp_mtl_make_current (rs_gfx_make_current; per thread)
//   CGLLockContext / CGLUnlockContext  rsp_mtl_lock / rsp_mtl_unlock (rs_gfx_lock: the context's
//                                      recursive mutex)
//   -[NSOpenGLContext update]          rsp_mtl_view_resized -> rs_gfx_resize (drawable size)
//   flushBuffer                        rsp_mtl_present -> rs_gfx_present
//   NSOpenGLCPSwapInterval             rsp_mtl_set_swap_interval -> displaySyncEnabled and
//                                      maximumDrawableCount, applied before the next present
//   SetCurrentCGDisplayFromOpenGLContext  CVDisplayLinkSetCurrentCGDisplay(rsp_mtl_display(view))
//   gl_context hook / rsp_g_gl_context nil: the game only stores the pointer (see rsp_shell.m)
//
// Drawable size: the view's size in points (1x, what the GL surface is without
// wantsBestResolutionOpenGLSurface), or with RS_METAL_RETINA=1 its backing size in pixels.
// Drawables: RS_METAL_DRAWABLES=2|3, else 3 with vsync (Apple's default: a late frame does not
// block nextDrawable for a whole refresh) and 2 without (RS_FPS / no_vsync: presents are not
// held for the display, so a third drawable only adds latency).
#ifndef RSP_PRESENT_H
#define RSP_PRESENT_H

#include <CoreGraphics/CoreGraphics.h>
#include <stdio.h>

#ifdef __OBJC__
@class NSView;
#else
typedef struct objc_object NSView;
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct rsp_mtl rsp_mtl;

// libRSOpenGL is loaded and its backend is Metal. Read once. With RS_GL_BACKEND=metal but no
// libRSOpenGL (the shell linked against the system framework) this logs and returns 0.
int rsp_mtl_selected(void);

// A context presenting into `layer` (a CAMetalLayer *, configured here: BGRA8Unorm,
// framebufferOnly, drawable w x h, sync per swap_interval). NULL if rs_gfx_create_layer fails.
rsp_mtl *rsp_mtl_create(void *layer, int w, int h, int swap_interval);
// The view becomes layer-hosting with a new CAMetalLayer, sized from the view (see above).
// Main thread. Aborts with a message when the Metal core cannot make the context: no silent
// fallback to GL, which would hide a broken backend.
rsp_mtl *rsp_mtl_attach(NSView *view, int swap_interval);

void rsp_mtl_make_current(rsp_mtl *m);
void rsp_mtl_lock(rsp_mtl *m);
void rsp_mtl_unlock(rsp_mtl *m);
// Drawable / default framebuffer size in pixels: what the frame and the overlay draw to.
void rsp_mtl_size(rsp_mtl *m, int *w, int *h);
void rsp_mtl_resize(rsp_mtl *m, int w, int h);
// The size the drawable should have for `view` now, then rsp_mtl_resize (takes the lock).
void rsp_mtl_view_resized(rsp_mtl *m, NSView *view);
// Drawable pixels per view point: the backing scale with RS_METAL_RETINA=1, else 1.
double rsp_mtl_scale(rsp_mtl *m);
// Any thread; the layer is changed by the next rsp_mtl_present (the frame thread).
void rsp_mtl_set_swap_interval(rsp_mtl *m, int interval);
// End of frame (with the lock held, like flushBuffer).
void rsp_mtl_present(rsp_mtl *m);
void *rsp_mtl_layer(rsp_mtl *m);         // the CAMetalLayer
void *rsp_mtl_cgl(rsp_mtl *m);           // rs_gfx_cgl: the CGLContextObj token CGL calls accept
void *rsp_mtl_context(rsp_mtl *m);       // the rs_gfx_context
// The display the view's window is on (the main display before it has one).
CGDirectDisplayID rsp_mtl_display(NSView *view);
// RS_METAL_RETINA=1.
int rsp_mtl_retina(void);
// Drawable count for a swap interval (RS_METAL_DRAWABLES, else 3 with sync, 2 without).
int rsp_mtl_drawables(int swap_interval);

// Safe load (not in the original). With a modded cache.psarc (a community "fast-load" cache) the
// game's loading can stall for good on its loading screen when the CPU is busy, and a cold Metal
// start (shader compiles on every core) makes it busy. So, with one, the shell has libRSOpenGL
// compile the shaders it knows from earlier runs before the game starts loading and keep the
// compile pool small afterwards (rs_gfx_prepare). With the original cache.psarc nothing changes.
#define RSP_CACHE_PSARC_SIZE 15614243LL
#define RSP_CACHE_PSARC_MD5 "ef4682b1ce7c9009e1a32b7720137da7"
// 0: the file at `path` (symlinks followed) is `size` bytes with that MD5; 1: other content;
// -1: missing or unreadable. The hash is only computed when the size matches.
int rsp_psarc_check(const char *path, long long size, const char *md5_hex);
// Whether to use safe load for the game in `root` (the folder holding the .app): RS_METAL_SAFE_LOAD=1
// or 0 decides; else on when <root>/cache.psarc is not the original. Logs which case applied.
int rsp_mtl_safe_load(const char *root);
// rs_gfx_prepare(context, max_seconds): blocks; any thread. -2 when libRSOpenGL lacks it.
int rsp_mtl_prepare(rsp_mtl *m, double max_seconds);
// Black behind the drawable until the first frame is presented (main thread).
void rsp_mtl_hold_black(rsp_mtl *m);
// rs_gfx_prepare_progress (any thread): 1 while rs_gfx_prepare runs, 0 not, -2 when libRSOpenGL
// lacks it (done = total = 0 then).
int rsp_mtl_prepare_progress(rsp_mtl *m, int *done, int *total);
// The progress line: "Preparing shaders... <done> of <total>" (a second line from RSP_SAFE_LOAD_LONG
// libraries on, the first start's corpus warm: a few minutes).
#define RSP_SAFE_LOAD_LONG 1000
void rsp_mtl_progress_text(char *buf, size_t n, int done, int total);
// A centred line of white text (system font) on the layer, over the black; NULL removes it. Main
// thread. rsp_mtl_status_layer: the CATextLayer, or NULL.
void rsp_mtl_status_text(rsp_mtl *m, const char *text);
void *rsp_mtl_status_layer(rsp_mtl *m);
// Main thread: rs_gfx_prepare(max_seconds) on a background queue, then `then(rc)` on the main
// queue. While it runs longer than show_after seconds the progress line is shown (updated 4 times
// a second); it is removed before `then` runs.
void rsp_mtl_prepare_async(rsp_mtl *m, double max_seconds, double show_after, void (^then)(int rc));

// Warm-only mode (not in the original): the shader warm-up of safe load with no window and no game,
// for the installer (the app's --warm-shaders, tools/play-native/warm-shaders.sh). Metal's own shader
// cache belongs to the app (bundle identifier and bundle path), so the warm-up must run in the
// installed game executable for the game's later compiles to hit it. Needs the Metal backend
// (RS_GL_BACKEND=metal). An offscreen context opens the disk cache and starts warming its manifest;
// rs_gfx_prepare(max_seconds) compiles that and, by its corpus rule (a small manifest or no
// corpus-warmed record; RS_METAL_CORPUS_WARM overrides), every library of the bundled corpus.
// Writes to `out` "progress F Preparing shaders… N of M" lines (F = N/M, never decreasing) at most
// every `interval` seconds while the counts change, one with F = 1 when it ends (also on a
// timeout), then a
// "warm-shaders: ..." summary. Returns an exit status: 0 ready (also: nothing to compile), 1 timed out
// (the rest compiles in the game), 2 no Metal context (no libRSOpenGL, or not the Metal backend).
int rsp_mtl_warm_only(double max_seconds, double interval, FILE *out);
// The progress line's fraction and label as rsp_mtl_warm_only prints them: "progress F LABEL" with
// F = max(prev, done/total) (1 when total is 0); returns F.
double rsp_mtl_warm_line(char *buf, size_t n, int done, int total, double prev);

#ifdef __cplusplus
}
#endif
#endif
