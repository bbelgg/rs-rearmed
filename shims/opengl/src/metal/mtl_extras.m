// Metal core: entry points outside the game's imports (extra-gl-imports.txt):
// EXT / APPLE aliases, the CGL calls the tests and the shell make, and the ARB
// assembly-program calls (the Cg shim uses its GLSL backend on Metal, so these
// only fail cleanly).
#include "mtl_internal.h"

// --- Aliases -------------------------------------------------------------------------
void rsmtl_glGenVertexArraysAPPLE(GLsizei n, GLuint *arrays) { rsmtl_glGenVertexArrays(n, arrays); }
void rsmtl_glBindVertexArrayAPPLE(GLuint array) { rsmtl_glBindVertexArray(array); }

void rsmtl_glBindFramebufferEXT(GLenum target, GLuint framebuffer) { rsmtl_glBindFramebuffer(target, framebuffer); }
void rsmtl_glGenFramebuffersEXT(GLsizei n, GLuint *framebuffers) { rsmtl_glGenFramebuffers(n, framebuffers); }
void rsmtl_glDeleteFramebuffersEXT(GLsizei n, const GLuint *framebuffers) { rsmtl_glDeleteFramebuffers(n, framebuffers); }
GLenum rsmtl_glCheckFramebufferStatusEXT(GLenum target) { return rsmtl_glCheckFramebufferStatus(target); }
void rsmtl_glFramebufferTexture2DEXT(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level) {
    rsmtl_glFramebufferTexture2D(target, attachment, textarget, texture, level);
}
void rsmtl_glFramebufferRenderbufferEXT(GLenum target, GLenum attachment, GLenum renderbuffertarget,
                                        GLuint renderbuffer) {
    rsmtl_glFramebufferRenderbuffer(target, attachment, renderbuffertarget, renderbuffer);
}
void rsmtl_glBindRenderbufferEXT(GLenum target, GLuint renderbuffer) { rsmtl_glBindRenderbuffer(target, renderbuffer); }
void rsmtl_glGenRenderbuffersEXT(GLsizei n, GLuint *renderbuffers) { rsmtl_glGenRenderbuffers(n, renderbuffers); }
void rsmtl_glDeleteRenderbuffersEXT(GLsizei n, const GLuint *renderbuffers) { rsmtl_glDeleteRenderbuffers(n, renderbuffers); }
void rsmtl_glRenderbufferStorageEXT(GLenum target, GLenum internalformat, GLsizei width, GLsizei height) {
    rsmtl_glRenderbufferStorage(target, internalformat, width, height);
}
void rsmtl_glGenerateMipmapEXT(GLenum target) { rsmtl_glGenerateMipmap(target); }

// sampler1D is a texture2d in the translator: a 1D texture is a 2D one of height 1.
void rsmtl_glTexImage1D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLint border,
                        GLenum format, GLenum type, const GLvoid *pixels) {
    RSM_CTX()
    if (target != GL_TEXTURE_1D) {
        rsm_error(ctx, GL_INVALID_ENUM);
        return;
    }
    // Bound as GL_TEXTURE_1D the texture is unit-local like a 2D one: use the 2D slot.
    rsmtl_glTexImage2D(GL_TEXTURE_2D, level, internalformat, width, 1, border, format, type, pixels);
}

// --- CGL -----------------------------------------------------------------------------
// Pixel formats are a token; a context is an offscreen Metal context (its
// default framebuffer is 256 x 256: CGL contexts without a drawable draw into FBOs).
static int g_pixel_format;

CGLError rsmtl_CGLChoosePixelFormat(const CGLPixelFormatAttribute *attribs, CGLPixelFormatObj *pix, GLint *npix) {
    (void)attribs;
    *pix = (CGLPixelFormatObj)&g_pixel_format;
    if (npix)
        *npix = 1;
    return kCGLNoError;
}

CGLError rsmtl_CGLDestroyPixelFormat(CGLPixelFormatObj pix) {
    (void)pix;
    return kCGLNoError;
}

CGLError rsmtl_CGLCreateContext(CGLPixelFormatObj pix, CGLContextObj share, CGLContextObj *ctx) {
    (void)pix;
    if (share)
        rsm_log("CGLCreateContext: shared contexts are not supported");
    rs_gfx_context *c = rsmtl_gfx_create_offscreen(256, 256);
    *ctx = c ? rsmtl_gfx_cgl(c) : NULL;
    return c ? kCGLNoError : kCGLBadAlloc;
}

CGLError rsmtl_CGLDestroyContext(CGLContextObj ctx) {
    if (ctx && (void *)ctx == (__bridge void *)rsm_tls)
        rsmtl_gfx_make_current(NULL);
    rsmtl_gfx_destroy((rs_gfx_context *)ctx);
    return kCGLNoError;
}

CGLError rsmtl_CGLEnable(CGLContextObj ctx, CGLContextEnable pname) {
    (void)ctx;
    (void)pname;  // kCGLCEMPEngine: the Metal core has its own threading
    return kCGLNoError;
}

const char *rsmtl_CGLErrorString(CGLError error) {
    switch (error) {
    case kCGLNoError: return "no error";
    case kCGLBadContext: return "invalid context";
    case kCGLBadAlloc: return "invalid memory allocation";
    default: return "CGL error";
    }
}

// --- ARB assembly programs: not on Metal ------------------------------------------------
void rsmtl_glGenProgramsARB(GLsizei n, GLuint *programs) {
    static GLuint next = 1;
    for (GLsizei i = 0; i < n; i++)
        programs[i] = next++;
}
void rsmtl_glDeleteProgramsARB(GLsizei n, const GLuint *programs) {
    (void)n;
    (void)programs;
}
void rsmtl_glBindProgramARB(GLenum target, GLuint program) {
    (void)target;
    (void)program;
}
void rsmtl_glProgramStringARB(GLenum target, GLenum format, GLsizei len, const GLvoid *string) {
    RSM_CTX()
    (void)target;
    (void)format;
    (void)len;
    (void)string;
    rsm_log("ARB programs are not supported on Metal (use the Cg shim's glsl backend)");
    rsm_error(ctx, GL_INVALID_OPERATION);
}
void rsmtl_glGetProgramivARB(GLenum target, GLenum pname, GLint *params) {
    (void)target;
    (void)pname;
    *params = 0;
}
void rsmtl_glProgramLocalParameter4fvARB(GLenum target, GLuint index, const GLfloat *params) {
    (void)target;
    (void)index;
    (void)params;
}
void rsmtl_glProgramLocalParameters4fvEXT(GLenum target, GLuint index, GLsizei count, const GLfloat *params) {
    (void)target;
    (void)index;
    (void)count;
    (void)params;
}
