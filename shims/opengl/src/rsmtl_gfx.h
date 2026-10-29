// The Metal core's side of rs_gfx.h (docs/METAL-DESIGN.md). With
// RS_GL_BACKEND=metal every rs_gfx_* call goes to the rsmtl_gfx_* of the same
// signature. rs_gfx.c has weak defaults (NULL / no-op); src/metal/ defines the
// real ones. Internal to libRSOpenGL (hidden).
#pragma once
#include "rs_gfx.h"

#ifdef __cplusplus
extern "C" {
#endif

rs_gfx_context *rsmtl_gfx_create_offscreen(int w, int h);
rs_gfx_context *rsmtl_gfx_create_layer(void *layer, int w, int h);
void rsmtl_gfx_destroy(rs_gfx_context *ctx);
void rsmtl_gfx_make_current(rs_gfx_context *ctx);
rs_gfx_context *rsmtl_gfx_current(void);
void rsmtl_gfx_resize(rs_gfx_context *ctx, int w, int h);
void rsmtl_gfx_present(rs_gfx_context *ctx);
void rsmtl_gfx_lock(rs_gfx_context *ctx);
void rsmtl_gfx_unlock(rs_gfx_context *ctx);
CGLContextObj rsmtl_gfx_cgl(rs_gfx_context *ctx);

// rsgl.c: the generated weak rsmtl_<name> stubs report through this.
void rsmtl_unimplemented(int idx, const char *name);

#ifdef __cplusplus
}
#endif
