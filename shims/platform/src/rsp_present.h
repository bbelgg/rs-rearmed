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

#ifdef __cplusplus
}
#endif
#endif
