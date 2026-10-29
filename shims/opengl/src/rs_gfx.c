// rs_gfx.h for both backends. Metal: every call goes to the Metal core's
// rsmtl_gfx_* (src/metal/; weak no-op defaults below). GL: CGL contexts.
//
// A GL offscreen context is a CGL context with a legacy (2.1) pixel format and
// no drawable. Without a drawable it has no usable default framebuffer, so it
// owns an FBO (RGBA8 colour + depth 24 / stencil 8 renderbuffers, w x h) that
// stands in for framebuffer 0: rs_gfx_make_current binds it, and the wrappers
// for glBindFramebuffer(*, 0) and the framebuffer binding queries (rsgl.c) map
// 0 to it while the context is current, so glReadPixels after binding 0 reads
// it. Everything here calls the system framework directly (not the wrappers).
#include "rs_gfx.h"
#include "rsmtl_gfx.h"

#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>
#include <dlfcn.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#pragma clang diagnostic ignored "-Wdeprecated-declarations"

#define RS_EXPORT __attribute__((visibility("default")))
#define RSMTL_WEAK __attribute__((weak, visibility("hidden")))

// --- Metal core hooks (strong definitions in src/metal/) -------------------------
RSMTL_WEAK rs_gfx_context *rsmtl_gfx_create_offscreen(int w, int h) {
    (void)w, (void)h;
    fprintf(stderr, "[rs-gl] metal: unimplemented rs_gfx_create_offscreen\n");
    return NULL;
}
RSMTL_WEAK rs_gfx_context *rsmtl_gfx_create_layer(void *layer, int w, int h) {
    (void)layer, (void)w, (void)h;
    fprintf(stderr, "[rs-gl] metal: unimplemented rs_gfx_create_layer\n");
    return NULL;
}
RSMTL_WEAK void rsmtl_gfx_destroy(rs_gfx_context *ctx) { (void)ctx; }
RSMTL_WEAK void rsmtl_gfx_make_current(rs_gfx_context *ctx) { (void)ctx; }
RSMTL_WEAK rs_gfx_context *rsmtl_gfx_current(void) { return NULL; }
RSMTL_WEAK void rsmtl_gfx_resize(rs_gfx_context *ctx, int w, int h) { (void)ctx, (void)w, (void)h; }
RSMTL_WEAK void rsmtl_gfx_present(rs_gfx_context *ctx) { (void)ctx; }
RSMTL_WEAK void rsmtl_gfx_lock(rs_gfx_context *ctx) { (void)ctx; }
RSMTL_WEAK void rsmtl_gfx_unlock(rs_gfx_context *ctx) { (void)ctx; }
RSMTL_WEAK CGLContextObj rsmtl_gfx_cgl(rs_gfx_context *ctx) { (void)ctx; return NULL; }

// --- GL ----------------------------------------------------------------------------
struct rs_gfx_context {
    CGLContextObj cgl;
    int w, h;
    GLuint fbo, color, depth_stencil;
    int viewport_set;
};

static __thread rs_gfx_context *rs_gfx_cur;

static struct {
    CGLError (*ChoosePixelFormat)(const CGLPixelFormatAttribute *, CGLPixelFormatObj *, GLint *);
    CGLError (*DestroyPixelFormat)(CGLPixelFormatObj);
    CGLError (*CreateContext)(CGLPixelFormatObj, CGLContextObj, CGLContextObj *);
    CGLError (*DestroyContext)(CGLContextObj);
    CGLError (*SetCurrentContext)(CGLContextObj);
    CGLContextObj (*GetCurrentContext)(void);
    CGLError (*LockContext)(CGLContextObj);
    CGLError (*UnlockContext)(CGLContextObj);
    void (*GenFramebuffers)(GLsizei, GLuint *);
    void (*DeleteFramebuffers)(GLsizei, const GLuint *);
    void (*BindFramebuffer)(GLenum, GLuint);
    void (*GenRenderbuffers)(GLsizei, GLuint *);
    void (*DeleteRenderbuffers)(GLsizei, const GLuint *);
    void (*BindRenderbuffer)(GLenum, GLuint);
    void (*RenderbufferStorage)(GLenum, GLenum, GLsizei, GLsizei);
    void (*FramebufferRenderbuffer)(GLenum, GLenum, GLenum, GLuint);
    GLenum (*CheckFramebufferStatus)(GLenum);
    void (*GetIntegerv)(GLenum, GLint *);
    void (*Viewport)(GLint, GLint, GLsizei, GLsizei);
    void (*Flush)(void);
} sys;
static int sys_ok;

static void sys_load(void) {
    void *h = dlopen("/System/Library/Frameworks/OpenGL.framework/Versions/A/OpenGL", RTLD_NOW | RTLD_LOCAL);
    if (!h)
        return;
    int ok = 1;
#define S(field, name) ok &= (*(void **)&sys.field = dlsym(h, name)) != NULL
    S(ChoosePixelFormat, "CGLChoosePixelFormat");
    S(DestroyPixelFormat, "CGLDestroyPixelFormat");
    S(CreateContext, "CGLCreateContext");
    S(DestroyContext, "CGLDestroyContext");
    S(SetCurrentContext, "CGLSetCurrentContext");
    S(GetCurrentContext, "CGLGetCurrentContext");
    S(LockContext, "CGLLockContext");
    S(UnlockContext, "CGLUnlockContext");
    S(GenFramebuffers, "glGenFramebuffers");
    S(DeleteFramebuffers, "glDeleteFramebuffers");
    S(BindFramebuffer, "glBindFramebuffer");
    S(GenRenderbuffers, "glGenRenderbuffers");
    S(DeleteRenderbuffers, "glDeleteRenderbuffers");
    S(BindRenderbuffer, "glBindRenderbuffer");
    S(RenderbufferStorage, "glRenderbufferStorage");
    S(FramebufferRenderbuffer, "glFramebufferRenderbuffer");
    S(CheckFramebufferStatus, "glCheckFramebufferStatus");
    S(GetIntegerv, "glGetIntegerv");
    S(Viewport, "glViewport");
    S(Flush, "glFlush");
#undef S
    sys_ok = ok;
}

static int sys_get(void) {
    static pthread_once_t once = PTHREAD_ONCE_INIT;
    pthread_once(&once, sys_load);
    if (!sys_ok)
        fprintf(stderr, "[rs-gl] rs_gfx: system OpenGL not available\n");
    return sys_ok;
}

// Used by the rsgl.c wrappers: the FBO standing in for framebuffer 0, or 0.
GLuint rs_gfx_gl_fbo(void) {
    rs_gfx_context *c = rs_gfx_cur;
    return c && c->fbo && sys.GetCurrentContext() == c->cgl ? c->fbo : 0;
}

// (Re)allocate the default framebuffer's storage. ctx must be current.
static int gl_storage(rs_gfx_context *c) {
    GLint fb = 0, rb = 0;
    sys.GetIntegerv(GL_FRAMEBUFFER_BINDING, &fb);
    sys.GetIntegerv(GL_RENDERBUFFER_BINDING, &rb);
    if (!c->fbo) {
        sys.GenFramebuffers(1, &c->fbo);
        sys.GenRenderbuffers(1, &c->color);
        sys.GenRenderbuffers(1, &c->depth_stencil);
    }
    sys.BindRenderbuffer(GL_RENDERBUFFER, c->color);
    sys.RenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, c->w, c->h);
    sys.BindRenderbuffer(GL_RENDERBUFFER, c->depth_stencil);
    sys.RenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, c->w, c->h);
    sys.BindFramebuffer(GL_FRAMEBUFFER, c->fbo);
    sys.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, c->color);
    sys.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, c->depth_stencil);
    sys.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, c->depth_stencil);
    GLenum st = sys.CheckFramebufferStatus(GL_FRAMEBUFFER);
    sys.BindRenderbuffer(GL_RENDERBUFFER, (GLuint)rb);
    // Framebuffer 0 stays "0" (our FBO) when it was bound before.
    sys.BindFramebuffer(GL_FRAMEBUFFER, fb ? (GLuint)fb : c->fbo);
    if (st != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "[rs-gl] rs_gfx: default framebuffer %dx%d incomplete (0x%04x)\n", c->w, c->h, st);
        return 0;
    }
    return 1;
}

// Run f with ctx current on this thread, then restore what was current.
static int with_current(rs_gfx_context *c, int (*f)(rs_gfx_context *)) {
    CGLContextObj prev = sys.GetCurrentContext();
    if (prev != c->cgl)
        sys.SetCurrentContext(c->cgl);
    int r = f(c);
    if (prev != c->cgl)
        sys.SetCurrentContext(prev);
    return r;
}

static int gl_flush(rs_gfx_context *c) {
    (void)c;
    sys.Flush();
    return 1;
}

static int gl_delete(rs_gfx_context *c) {
    sys.DeleteFramebuffers(1, &c->fbo);
    GLuint rbs[2] = {c->color, c->depth_stencil};
    sys.DeleteRenderbuffers(2, rbs);
    return 1;
}

static rs_gfx_context *gl_create_offscreen(int w, int h) {
    if (!sys_get() || w <= 0 || h <= 0)
        return NULL;
    // Legacy profile (no kCGLPFAOpenGLProfile), like the game's context.
    CGLPixelFormatAttribute attrs[] = {
        kCGLPFAAccelerated, kCGLPFAColorSize, 24, kCGLPFAAlphaSize, 8,
        kCGLPFADepthSize, 24, kCGLPFAStencilSize, 8, 0};
    CGLPixelFormatObj pf = NULL;
    GLint npf = 0;
    if (sys.ChoosePixelFormat(attrs, &pf, &npf) || !pf) {
        fprintf(stderr, "[rs-gl] rs_gfx: no pixel format\n");
        return NULL;
    }
    rs_gfx_context *c = calloc(1, sizeof *c);
    CGLError e = sys.CreateContext(pf, NULL, &c->cgl);
    sys.DestroyPixelFormat(pf);
    if (e || !c->cgl) {
        fprintf(stderr, "[rs-gl] rs_gfx: CGLCreateContext failed (%d)\n", e);
        free(c);
        return NULL;
    }
    c->w = w;
    c->h = h;
    if (!with_current(c, gl_storage)) {
        with_current(c, gl_delete);
        sys.DestroyContext(c->cgl);
        free(c);
        return NULL;
    }
    return c;
}

// --- API ---------------------------------------------------------------------------
#define METAL (rs_gfx_backend() == RS_GFX_METAL)

RS_EXPORT rs_gfx_context *rs_gfx_create_offscreen(int w, int h) {
    return METAL ? rsmtl_gfx_create_offscreen(w, h) : gl_create_offscreen(w, h);
}

RS_EXPORT rs_gfx_context *rs_gfx_create_layer(void *layer, int w, int h) {
    return METAL ? rsmtl_gfx_create_layer(layer, w, h) : NULL;
}

RS_EXPORT void rs_gfx_destroy(rs_gfx_context *c) {
    if (METAL) {
        rsmtl_gfx_destroy(c);
        return;
    }
    if (!c)
        return;
    if (rs_gfx_cur == c)
        rs_gfx_make_current(NULL);
    with_current(c, gl_delete);
    sys.DestroyContext(c->cgl);
    free(c);
}

RS_EXPORT void rs_gfx_make_current(rs_gfx_context *c) {
    if (METAL) {
        rsmtl_gfx_make_current(c);
        return;
    }
    if (!sys_get())
        return;
    rs_gfx_cur = c;
    sys.SetCurrentContext(c ? c->cgl : NULL);
    if (!c)
        return;
    if (!c->viewport_set) {  // what a first makeCurrent with a drawable does
        c->viewport_set = 1;
        sys.BindFramebuffer(GL_FRAMEBUFFER, c->fbo);
        sys.Viewport(0, 0, c->w, c->h);
    }
}

RS_EXPORT rs_gfx_context *rs_gfx_current(void) {
    return METAL ? rsmtl_gfx_current() : rs_gfx_cur;
}

RS_EXPORT void rs_gfx_resize(rs_gfx_context *c, int w, int h) {
    if (METAL) {
        rsmtl_gfx_resize(c, w, h);
        return;
    }
    if (!c || w <= 0 || h <= 0 || (w == c->w && h == c->h))
        return;
    c->w = w;
    c->h = h;
    with_current(c, gl_storage);
}

RS_EXPORT void rs_gfx_present(rs_gfx_context *c) {
    if (METAL) {
        rsmtl_gfx_present(c);
        return;
    }
    if (c)
        with_current(c, gl_flush);
}

RS_EXPORT void rs_gfx_lock(rs_gfx_context *c) {
    if (METAL)
        rsmtl_gfx_lock(c);
    else if (c)
        sys.LockContext(c->cgl);
}

RS_EXPORT void rs_gfx_unlock(rs_gfx_context *c) {
    if (METAL)
        rsmtl_gfx_unlock(c);
    else if (c)
        sys.UnlockContext(c->cgl);
}

RS_EXPORT CGLContextObj rs_gfx_cgl(rs_gfx_context *c) {
    if (METAL)
        return rsmtl_gfx_cgl(c);
    return c ? c->cgl : NULL;
}
