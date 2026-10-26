// The GL/CGL functions Rocksmith2014 imports from OpenGL.framework, as an
// X-macro table (gl_api.inc, generated from game-gl-imports.txt and the SDK
// headers by tools/gen-gl-api.py).
//
// Including this header declares every function, so code that doesn't include
// the SDK's gl.h/gl3.h (which can't both be included) still sees the legacy
// (glAlphaFunc) and ARB (glDrawArraysInstancedARB, ...) entry points. It also
// defines RS_GL_IDX_<name> indices and RS_GL_COUNT.
#pragma once
#include <OpenGL/gltypes.h>
#include <OpenGL/CGLTypes.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RS_GL_FN(ret, name, params, args) ret name params;
#define RS_GL_VOID(name, params, args) void name params;
#include "gl_api.inc"
#undef RS_GL_FN
#undef RS_GL_VOID

enum {
#define RS_GL_FN(ret, name, params, args) RS_GL_IDX_##name,
#define RS_GL_VOID(name, params, args) RS_GL_IDX_##name,
#include "gl_api.inc"
#undef RS_GL_FN
#undef RS_GL_VOID
    RS_GL_COUNT
};

// Exported by the shim and the spy (rsgl.c): per-function call and error
// counts since load. Returns RS_GL_COUNT. Lets tests measure API coverage.
struct rs_gl_stat {
    const char *name;
    unsigned long calls;
    unsigned long errors;
    GLenum first_error;
};
int rs_gl_stats(struct rs_gl_stat *out);

#ifdef __cplusplus
}
#endif
