// Backend-neutral context and presentation API, exported by libRSOpenGL
// (docs/METAL-DESIGN.md). The shell creates its context through this on Metal;
// tests use the offscreen contexts to run the same GL calls on both backends.
#pragma once
#include <OpenGL/CGLTypes.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { RS_GFX_GL = 0, RS_GFX_METAL = 1 } rs_gfx_backend_t;

// RS_GL_BACKEND=gl|metal, read once when libRSOpenGL loads (default metal).
rs_gfx_backend_t rs_gfx_backend(void);

typedef struct rs_gfx_context rs_gfx_context;

// A context with no window. Its default framebuffer (name 0) is w x h RGBA8
// with depth 24 / stencil 8 on both backends. Not current until made current.
rs_gfx_context *rs_gfx_create_offscreen(int w, int h);
// Metal: a context presenting into `layer` (a CAMetalLayer *). GL: NULL.
rs_gfx_context *rs_gfx_create_layer(void *layer, int w, int h);
void rs_gfx_destroy(rs_gfx_context *ctx);

void rs_gfx_make_current(rs_gfx_context *ctx);  // NULL: none
rs_gfx_context *rs_gfx_current(void);
// Default framebuffer size in pixels (the drawable size for a layer context).
void rs_gfx_resize(rs_gfx_context *ctx, int w, int h);
// End of frame: what flushBuffer does on GL. A layer context presents.
void rs_gfx_present(rs_gfx_context *ctx);
// The CGLLockContext / CGLUnlockContext pair (recursive).
void rs_gfx_lock(rs_gfx_context *ctx);
void rs_gfx_unlock(rs_gfx_context *ctx);
// Safe load: call once, before the game starts loading, from any thread (blocks; the context
// need not be current). Metal: compiles every shader library and pipeline the disk cache's
// manifest lists (libraries linked in earlier runs) on the whole compile pool and waits for them,
// at most max_seconds; on a first run (a small manifest, or the offline shader corpus never
// compiled completely with this cache directory) also the library of every corpus program (a
// few minutes cold; at the timeout those not started are skipped); then throttles the compile pool for the rest of the run, so compiles of
// libraries not known in advance cannot starve the game's loading threads (at most 2 workers at
// utility QoS, or RS_MTL_COMPILE_THREADS; a draw that needs a library at once still compiles it
// on its own thread). Returns 0 when everything is ready (or there was nothing to warm), 1 on
// timeout (the rest goes on in the throttled pool), -1 for no context. GL: no-op, 0.
int rs_gfx_prepare(rs_gfx_context *ctx, double max_seconds);
// rs_gfx_prepare's progress, from any thread: libraries compiled so far and to compile (total can
// grow once, when the corpus warm is decided; both only grow; done <= total). Returns 1 while
// rs_gfx_prepare runs, 0 otherwise (before, after, GL), -1 for no context.
int rs_gfx_prepare_progress(rs_gfx_context *ctx, int *done, int *total);
// The CGLContextObj the game and CGL calls see: the real one on GL, a token on Metal.
CGLContextObj rs_gfx_cgl(rs_gfx_context *ctx);

// The program glUseProgram last bound in the calling thread's current context
// (0 for none), tracked by libRSOpenGL's wrappers on both backends, so no GL
// query is made. -1 when not known exactly: nothing bound through the shim yet
// in this context, a program that wasn't created and successfully linked
// through the shim, or the bound program was deleted or relinked since. Never
// stale. For the Cg shim (skip a redundant glUseProgram).
int rs_gl_current_program(void);

#ifdef __cplusplus
}
#endif
