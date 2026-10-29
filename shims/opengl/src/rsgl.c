// Wrapper layer over every GL/CGL function the game imports (gl_api.inc).
//
// Built two ways from this one file:
//
//   RS_GL_SPY defined   libglspy.dylib: injected with DYLD_INSERT_LIBRARIES; a
//                       __interpose entry per function rebinds the game *and*
//                       Cg.framework (which calls GL itself) to the wrappers.
//   otherwise           libRSOpenGL.dylib: the OpenGL.framework replacement. It
//                       exports the table's names itself (the game's 107 imports
//                       plus extra-gl-imports.txt), forwards them to the backend
//                       and re-exports the rest of the system framework.
//                       Backends (RS_GL_BACKEND, read once at load):
//                         gl     the system framework (Apple's Metal-backed GL),
//                                through dlsym ("opengl" is accepted too)
//                         metal  rsmtl_<name> (src/metal/; a weak generated stub
//                                reports "unimplemented <name>" and returns 0);
//                                the default (unset, empty or unknown). The app's
//                                main() may start again with RS_GL_BACKEND=gl
//                                (no Metal device, Metal crash fallback:
//                                shims/platform/include/rsp_renderer.h)
//                       The spy wraps only the game's imports and always uses
//                       the system framework.
//
// With RS_GL_LOG, RS_GL_CHECK, RS_GL_TRACE_DIR or RS_GL_STATS set, every call
// takes a slow path that counts it per function (rs_gl_stats) and, with
// RS_GL_CHECK=1, checks glGetError after it; an error found that way is handed
// back on the caller's next glGetError, so the game still sees it. Otherwise a
// wrapper is one flag test and a tail call (nothing is counted).
//
// The shim also tracks the program glUseProgram bound in each context, for the
// Cg shim: rs_gl_current_program() (rs_gfx.h).
//
// Environment:
//   RS_GL_LOG=<file>          write a report there every 2 s and at exit
//   RS_GL_CHECK=1             glGetError after every call
//   RS_GL_STATS=1             count calls (rs_gl_stats) without a report
//   RS_GL_DUMP_SHADERS=<dir>  save each glShaderSource as <dir>/<seq>_<id>_<type>.glsl
//   RS_GL_SNAPSHOT_DIR=<dir>  save the back buffer as PNG before a flushBuffer,
//                             every RS_GL_SNAPSHOT_SECS (default 5) seconds; once
//                             <dir>/stop exists, no more (checked when one is due)
//   RS_GL_VERBOSE=1           log load and every first error to stderr
//   RS_GL_TRACE_DIR=<dir>     per-frame call trace (gltrace.c; RS_GL_TRACE_START / _KEY / _FRAMES)
//   RS_GL_FRAMETIMES=<file>   timestamp every present, drawView and display-link callback
//                             (nsgl_hook.m). No call tracing
//   RS_GL_BACKEND=gl|metal    the shim's backend (default metal; the spy: always gl);
//                             rs_gfx_backend() reports it
#include "gl_api.h"
#include "gltrace.h"
#include "rs_gfx.h"

#include <ctype.h>
#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define GL_NO_ERROR 0
#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_GEOMETRY_SHADER 0x8DD9
#define GL_EXTENSIONS 0x1F03
#define GL_FRAMEBUFFER_BINDING 0x8CA6
#define GL_READ_FRAMEBUFFER_BINDING 0x8CAA
#define GL_FRAMEBUFFER 0x8D40
#define GL_READ_FRAMEBUFFER 0x8CA8
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#define GL_LINK_STATUS 0x8B82
#define GL_FALSE 0
#define GL_TRUE 1

#ifdef RS_GL_SPY
#include "../../include/interpose.h"
#define RS_WRAP(name) rs_spy_##name
#define RS_WRAP_LINKAGE static
#define RS_MODE "spy"
#else
#define RS_WRAP(name) name
#define RS_WRAP_LINKAGE __attribute__((visibility("default")))
#define RS_MODE "shim"
static const char kSystemGL[] = "/System/Library/Frameworks/OpenGL.framework/Versions/A/OpenGL";
#endif

// rs_real_<name>: the backend's implementation (the system framework, or
// rsmtl_<name> on Metal). rs_impl_<name>: what the wrapper calls; the same
// unless a function below needs special handling (the specials call rs_real_).
#define RS_GL_FN(ret, name, params, args) \
    static ret(*rs_real_##name) params;   \
    static ret(*rs_impl_##name) params;
#define RS_GL_VOID(name, params, args) RS_GL_FN(void, name, params, args)
#include "gl_api.inc"
#undef RS_GL_FN
#undef RS_GL_VOID

static const char *const rs_names[RS_GL_COUNT] = {
#define RS_GL_FN(ret, name, params, args) #name,
#define RS_GL_VOID(name, params, args) #name,
#include "gl_api.inc"
#undef RS_GL_FN
#undef RS_GL_VOID
};

static _Atomic unsigned long rs_calls[RS_GL_COUNT];
static unsigned char rs_is_cgl[RS_GL_COUNT];  // CGL*: no GL error state
static _Atomic unsigned long rs_errors[RS_GL_COUNT];
static _Atomic GLenum rs_first_error[RS_GL_COUNT];
static int rs_check, rs_verbose;
// Set once at load, before any call: count, check and trace (see the wrappers).
static unsigned char rs_slow;
static const char *rs_log_path, *rs_shader_dir;
static __thread GLenum rs_pending_error;
static __thread GLenum rs_last_pname;
_Atomic unsigned long rs_gl_frames;  // flushBuffer count, kept by nsgl_hook.m

// --- Notes: one-off facts for the report (pixel format, strings, ...) ---------
#define RS_MAX_NOTES 256
static char *rs_notes[RS_MAX_NOTES];
static _Atomic int rs_note_count;

__attribute__((visibility("default"))) void rs_gl_note(const char *fmt, ...) {
    char *s;
    va_list ap;
    va_start(ap, fmt);
    if (vasprintf(&s, fmt, ap) < 0)
        s = NULL;
    va_end(ap);
    if (!s)
        return;
    if (rs_verbose)
        fprintf(stderr, "[rs-gl] %s\n", s);
    int i = atomic_fetch_add(&rs_note_count, 1);
    if (i < RS_MAX_NOTES)
        rs_notes[i] = s;
    else
        free(s);
}

static const char *rs_error_name(GLenum e) {
    switch (e) {
    case 0x0500: return "GL_INVALID_ENUM";
    case 0x0501: return "GL_INVALID_VALUE";
    case 0x0502: return "GL_INVALID_OPERATION";
    case 0x0505: return "GL_OUT_OF_MEMORY";
    case 0x0506: return "GL_INVALID_FRAMEBUFFER_OPERATION";
    default: return "?";
    }
}

// --- Per-call bookkeeping -----------------------------------------------------
static inline void rs_enter(int i) {
    atomic_fetch_add_explicit(&rs_calls[i], 1, memory_order_relaxed);
}

static __attribute__((noinline)) void rs_leave_check(int i) {
    GLenum e = rs_real_glGetError();
    if (e == GL_NO_ERROR)
        return;
    if (atomic_fetch_add(&rs_errors[i], 1) == 0) {
        rs_first_error[i] = e;
        if (rs_verbose)
            fprintf(stderr, "[rs-gl] first error in %s: %s (0x%04x)\n", rs_names[i],
                    rs_error_name(e), e);
    }
    if (i == RS_GL_IDX_glGetIntegerv) {
        static _Atomic int logged;
        if (atomic_fetch_add(&logged, 1) < 16)
            rs_gl_note("glGetIntegerv(0x%04x) -> %s", rs_last_pname, rs_error_name(e));
    }
    if (rs_pending_error == GL_NO_ERROR)
        rs_pending_error = e;
}

static inline void rs_leave(int i) {
    // CGL calls have no GL error state; glGetError reports its own.
    if (rs_check && i != RS_GL_IDX_glGetError && !rs_is_cgl[i])
        rs_leave_check(i);
}

// --- Specials -----------------------------------------------------------------
static GLenum rs_glGetError(void) {
    GLenum e = rs_pending_error;
    if (e != GL_NO_ERROR) {
        rs_pending_error = GL_NO_ERROR;
        return e;
    }
    return rs_real_glGetError();
}

// Record each distinct string the game queries (renderer, version, ...).
static const GLubyte *rs_glGetString(GLenum name) {
    static _Atomic unsigned seen[8];
    const GLubyte *s = rs_real_glGetString(name);
    unsigned slot = name & 7, key = name | 0x80000000u, expected = 0;
    if (name != GL_EXTENSIONS && atomic_compare_exchange_strong(&seen[slot], &expected, key))
        rs_gl_note("glGetString(0x%04x) = \"%s\"", name, s ? (const char *)s : "(null)");
    return s;
}

#ifndef RS_GL_SPY
// Offscreen GL contexts (rs_gfx.c) have no drawable, so their framebuffer 0
// is an FBO of their own: binding 0 binds it, and the binding queries report it
// as 0. rs_gfx_gl_fbo() is that FBO when such a context is current, else 0.
GLuint rs_gfx_gl_fbo(void);

static void rs_glBindFramebuffer(GLenum target, GLuint fb) {
    rs_real_glBindFramebuffer(target, fb ? fb : rs_gfx_gl_fbo());
}

static void rs_glBindFramebufferEXT(GLenum target, GLuint fb) {
    rs_real_glBindFramebufferEXT(target, fb ? fb : rs_gfx_gl_fbo());
}

// Deleting the bound framebuffer reverts the binding to the real 0, which has
// no storage here: put the stand-in back. The stand-in itself is never deleted
// (the caller can't have a name for it).
static void rs_glDeleteFramebuffers(GLsizei n, const GLuint *fbs) {
    GLuint own = rs_gfx_gl_fbo();
    if (!own || n <= 0 || !fbs) {
        rs_real_glDeleteFramebuffers(n, fbs);
        return;
    }
    for (GLsizei i = 0; i < n; i++)
        if (fbs[i] != own)
            rs_real_glDeleteFramebuffers(1, &fbs[i]);
    GLint draw = -1, read = -1;
    rs_real_glGetIntegerv(GL_FRAMEBUFFER_BINDING, &draw);
    rs_real_glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read);
    if (draw == 0 && read == 0)
        rs_real_glBindFramebuffer(GL_FRAMEBUFFER, own);
    else if (draw == 0)
        rs_real_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, own);
    else if (read == 0)
        rs_real_glBindFramebuffer(GL_READ_FRAMEBUFFER, own);
}

static int rs_is_fb_binding(GLenum pname) {
    return pname == GL_FRAMEBUFFER_BINDING || pname == GL_READ_FRAMEBUFFER_BINDING;
}

static void rs_glGetFloatv(GLenum pname, GLfloat *params) {
    rs_real_glGetFloatv(pname, params);
    GLuint own;
    if (rs_is_fb_binding(pname) && params && *params != 0 && (own = rs_gfx_gl_fbo()) &&
        *params == (GLfloat)own)
        *params = 0;
}

static void rs_glGetBooleanv(GLenum pname, GLboolean *params) {
    if (rs_is_fb_binding(pname) && params && rs_gfx_gl_fbo()) {
        GLint v = 0;
        rs_real_glGetIntegerv(pname, &v);
        *params = v && (GLuint)v != rs_gfx_gl_fbo() ? GL_TRUE : GL_FALSE;
        return;
    }
    rs_real_glGetBooleanv(pname, params);
}
#endif

// Remember the pname so rs_leave can say which query failed (the game's only
// GL error is a glGetIntegerv).
static void rs_glGetIntegerv(GLenum pname, GLint *params) {
    rs_last_pname = pname;
    rs_real_glGetIntegerv(pname, params);
#ifndef RS_GL_SPY
    if (rs_is_fb_binding(pname) && params && *params && (GLuint)*params == rs_gfx_gl_fbo())
        *params = 0;
#endif
}

static _Atomic GLenum rs_shader_types[4096];
static _Atomic unsigned rs_shader_seq;

static GLuint rs_glCreateShader(GLenum type) {
    GLuint id = rs_real_glCreateShader(type);
    rs_shader_types[id & 4095] = type;
    return id;
}

static void rs_glShaderSource(GLuint shader, GLsizei count, const GLchar *const *string,
                              const GLint *length) {
    if (rs_shader_dir && string) {
        unsigned seq = atomic_fetch_add(&rs_shader_seq, 1);
        GLenum t = rs_shader_types[shader & 4095];
        const char *ts = t == GL_VERTEX_SHADER     ? "vs"
                         : t == GL_FRAGMENT_SHADER ? "fs"
                         : t == GL_GEOMETRY_SHADER ? "gs"
                                                   : "xx";
        char path[1024];
        snprintf(path, sizeof path, "%s/%05u_%u_%s.glsl", rs_shader_dir, seq, shader, ts);
        FILE *f = seq < 100000 ? fopen(path, "w") : NULL;
        if (f) {
            for (GLsizei i = 0; i < count; i++) {
                if (!string[i])
                    continue;
                if (length && length[i] >= 0)
                    fwrite(string[i], 1, (size_t)length[i], f);
                else
                    fputs(string[i], f);
            }
            fclose(f);
        }
    }
    rs_real_glShaderSource(shader, count, string, length);
}

// --- Current program (rs_gl_current_program) --------------------------------------
// The program glUseProgram bound, per GL context, so the Cg shim can skip
// redundant binds without a glGetIntegerv (a sync with the multithreaded GL
// engine). Exact or -1, never stale:
//  - Keyed by the context current at the query (the backend's
//    CGLGetCurrentContext), so a context switch the shim never sees
//    (-[NSOpenGLContext makeCurrentContext] calls CGL inside AppKit) can't leave
//    an old answer: a context nothing was bound in yet answers -1.
//  - glUseProgram(p) records p only when it can't fail: 0, or a program created
//    by glCreateProgram in this share group whose last glLinkProgram /
//    glProgramBinary succeeded (GL_LINK_STATUS read right after it) and that
//    wasn't deleted. Anything else (a failed bind keeps the old program) -> -1.
//  - glDeleteProgram(p) and a relink of p turn p into -1 in every context that
//    has it current (a deleted program stays current in GL until replaced, and
//    its name can come back from glCreateProgram).
//  - A created context (CGLCreateContext, rs_gfx_create_*, NSOpenGLContext
//    init) starts unknown, and a new share group starts with no programs, so
//    a reused address can't inherit state; a destroyed one frees its slot.
// A context's prog is written only by the thread it's current on (GL allows one
// at a time), except the delete/relink invalidation, a compare-and-swap; slots
// and groups change under rs_pt_mu.
// Assumes every GL call of the process goes through libRSOpenGL.
#ifndef RS_GL_SPY
#define RS_PT_CTX 64          // live contexts tracked; more answer -1
#define RS_PT_GROUPS 32       // share groups
#define RS_PT_NAMES 65536     // program names tracked per group; higher ones answer -1
#define RS_PT_CREATED 1
#define RS_PT_LINKED 2
#define RS_PT_BUSY ((void *)1)

#define RS_PT_DEAD ((void *)2)

// Share groups and slots are claimed and released under rs_pt_mu. A group is
// reassigned only when no slot refers to it, and cleared then.
struct rs_pt_group {
    void *key;                 // the CGLShareGroupObj (Metal: the context); RS_PT_DEAD
    int refs;                  // slots pointing here
    _Atomic unsigned char *st; // RS_PT_* per program name
};
struct rs_pt_ctx {
    _Atomic(void *) ctx;      // NULL free, RS_PT_BUSY being claimed
    _Atomic int prog;         // -1 unknown
    struct rs_pt_group *g;
};
static struct rs_pt_group rs_pt_groups[RS_PT_GROUPS];
static struct rs_pt_ctx rs_pt_ctxs[RS_PT_CTX];
static pthread_mutex_t rs_pt_mu = PTHREAD_MUTEX_INITIALIZER;  // claiming slots
static __thread struct rs_pt_ctx *rs_pt_tl;                     // this thread's last slot

static void *rs_pt_current_ctx(void) { return (void *)rs_real_CGLGetCurrentContext(); }

// rs_pt_mu held. The group record for key, or a cleared unreferenced one.
static struct rs_pt_group *rs_pt_group_get(void *key) {
    struct rs_pt_group *free_g = NULL;
    for (int i = 0; i < RS_PT_GROUPS; i++) {
        struct rs_pt_group *g = &rs_pt_groups[i];
        if (g->key == key && key)
            return g;
        if (!g->refs && (!free_g || !g->key))
            free_g = g;
    }
    if (!free_g)
        return NULL;
    if (!free_g->st)
        free_g->st = calloc(RS_PT_NAMES, 1);
    else
        for (int n = 0; n < RS_PT_NAMES; n++)
            atomic_store_explicit(&free_g->st[n], 0, memory_order_relaxed);
    if (!free_g->st)
        return NULL;
    free_g->key = key;
    return free_g;
}

// The slot of the context current on this thread; claim one if `claim`.
static struct rs_pt_ctx *rs_pt_slot(void *ctx, int claim) {
    struct rs_pt_ctx *t = rs_pt_tl;
    if (t && atomic_load_explicit(&t->ctx, memory_order_relaxed) == ctx)
        return t;
    for (int i = 0; i < RS_PT_CTX; i++)
        if (atomic_load_explicit(&rs_pt_ctxs[i].ctx, memory_order_acquire) == ctx)
            return rs_pt_tl = &rs_pt_ctxs[i];
    if (!claim)
        return NULL;
    pthread_mutex_lock(&rs_pt_mu);
    struct rs_pt_ctx *s = NULL;
    for (int i = 0; i < RS_PT_CTX && !s; i++)
        if (!atomic_load_explicit(&rs_pt_ctxs[i].ctx, memory_order_relaxed))
            s = &rs_pt_ctxs[i];
    struct rs_pt_group *g = s ? rs_pt_group_get((void *)rs_real_CGLGetShareGroup(ctx)) : NULL;
    if (g) {
        atomic_store_explicit(&s->ctx, RS_PT_BUSY, memory_order_relaxed);
        g->refs++;
        s->g = g;
        atomic_store_explicit(&s->prog, -1, memory_order_relaxed);
        atomic_store_explicit(&s->ctx, ctx, memory_order_release);
    } else {
        s = NULL;
    }
    pthread_mutex_unlock(&rs_pt_mu);
    return s ? (rs_pt_tl = s) : NULL;
}

static void rs_pt_forget(void *ctx) {
    if (!ctx)
        return;
    pthread_mutex_lock(&rs_pt_mu);
    for (int i = 0; i < RS_PT_CTX; i++)
        if (atomic_load_explicit(&rs_pt_ctxs[i].ctx, memory_order_relaxed) == ctx) {
            atomic_store_explicit(&rs_pt_ctxs[i].prog, -1, memory_order_relaxed);
            atomic_store_explicit(&rs_pt_ctxs[i].ctx, NULL, memory_order_release);
            if (rs_pt_ctxs[i].g && !--rs_pt_ctxs[i].g->refs)
                rs_pt_ctxs[i].g->key = NULL;
            rs_pt_ctxs[i].g = NULL;
        }
    pthread_mutex_unlock(&rs_pt_mu);
}

// p may no longer be what it was in group g: no context of g has it current.
static void rs_pt_invalidate(struct rs_pt_group *g, GLuint p) {
    pthread_mutex_lock(&rs_pt_mu);
    for (int i = 0; i < RS_PT_CTX; i++) {
        struct rs_pt_ctx *s = &rs_pt_ctxs[i];
        int want = (int)p;
        if (s->g == g)
            atomic_compare_exchange_strong(&s->prog, &want, -1);
    }
    pthread_mutex_unlock(&rs_pt_mu);
}

static struct rs_pt_group *rs_pt_cur_group(void) {
    void *c = rs_pt_current_ctx();
    struct rs_pt_ctx *s = c ? rs_pt_slot(c, 1) : NULL;
    return s ? s->g : NULL;
}

// Called for every context created / destroyed (rs_gfx.c, nsgl_hook.m, CGL wrappers).
void rs_gl_ctx_created(void *ctx, int new_group) {
    if (!ctx)
        return;
    rs_pt_forget(ctx);
    if (!new_group)
        return;
    // A new group at the address of a dead one: never match the old record again.
    void *key = (void *)rs_real_CGLGetShareGroup((CGLContextObj)ctx);
    pthread_mutex_lock(&rs_pt_mu);
    for (int i = 0; i < RS_PT_GROUPS; i++)
        if (key && rs_pt_groups[i].key == key)
            rs_pt_groups[i].key = rs_pt_groups[i].refs ? RS_PT_DEAD : NULL;
    pthread_mutex_unlock(&rs_pt_mu);
}
void rs_gl_ctx_destroyed(void *ctx) { rs_pt_forget(ctx); }

__attribute__((visibility("default"))) int rs_gl_current_program(void) {
    void *c = rs_pt_current_ctx();
    struct rs_pt_ctx *s = c ? rs_pt_slot(c, 0) : NULL;
    return s ? atomic_load_explicit(&s->prog, memory_order_relaxed) : -1;
}

static void rs_glUseProgram(GLuint p) {
    rs_real_glUseProgram(p);
    void *c = rs_pt_current_ctx();
    struct rs_pt_ctx *s = c ? rs_pt_slot(c, 1) : NULL;
    if (!s)
        return;
    int v = -1;
    if (p == 0)
        v = 0;
    else if (p < RS_PT_NAMES &&
             atomic_load_explicit(&s->g->st[p], memory_order_relaxed) == (RS_PT_CREATED | RS_PT_LINKED))
        v = (int)p;
    atomic_store_explicit(&s->prog, v, memory_order_relaxed);
}

static GLuint rs_glCreateProgram(void) {
    GLuint p = rs_real_glCreateProgram();
    struct rs_pt_group *g;
    if (p && p < RS_PT_NAMES && (g = rs_pt_cur_group())) {
        rs_pt_invalidate(g, p);  // a name nothing should still have current
        atomic_store_explicit(&g->st[p], RS_PT_CREATED, memory_order_relaxed);
    }
    return p;
}

// After a link or a binary load: is p usable now? Only asked for programs this
// group created, so the query itself can't raise an error.
static void rs_pt_linked(GLuint p) {
    struct rs_pt_group *g;
    if (!p || p >= RS_PT_NAMES || !(g = rs_pt_cur_group()))
        return;
    rs_pt_invalidate(g, p);  // a relinked current program: conservatively unknown
    if (!(atomic_load_explicit(&g->st[p], memory_order_relaxed) & RS_PT_CREATED))
        return;
    GLint ok = GL_FALSE;
    rs_real_glGetProgramiv(p, GL_LINK_STATUS, &ok);
    atomic_store_explicit(&g->st[p], ok ? RS_PT_CREATED | RS_PT_LINKED : RS_PT_CREATED,
                          memory_order_relaxed);
}

static void rs_glLinkProgram(GLuint p) {
    rs_real_glLinkProgram(p);
    rs_pt_linked(p);
}

static void rs_glProgramBinary(GLuint p, GLenum format, const GLvoid *binary, GLsizei length) {
    rs_real_glProgramBinary(p, format, binary, length);
    rs_pt_linked(p);
}

static void rs_glDeleteProgram(GLuint p) {
    rs_real_glDeleteProgram(p);
    struct rs_pt_group *g;
    if (p && p < RS_PT_NAMES && (g = rs_pt_cur_group())) {
        atomic_store_explicit(&g->st[p], 0, memory_order_relaxed);
        rs_pt_invalidate(g, p);
    }
}

static CGLError rs_CGLCreateContext(CGLPixelFormatObj pix, CGLContextObj share, CGLContextObj *ctx) {
    CGLError e = rs_real_CGLCreateContext(pix, share, ctx);
    if (!e && ctx)
        rs_gl_ctx_created(*ctx, share == NULL);
    return e;
}

static CGLError rs_CGLDestroyContext(CGLContextObj ctx) {
    rs_gl_ctx_destroyed(ctx);
    return rs_real_CGLDestroyContext(ctx);
}
#else
void rs_gl_ctx_created(void *ctx, int new_group) { (void)ctx, (void)new_group; }
void rs_gl_ctx_destroyed(void *ctx) { (void)ctx; }
#endif

// The tracer stashes an error it found while reading state back (it must not
// swallow one the game hasn't asked for yet).
void rs_tr_stash_error(unsigned e) {
    if (rs_pending_error == GL_NO_ERROR)
        rs_pending_error = e;
}

// --- Wrappers -----------------------------------------------------------------
// The fast path is a load of rs_slow and a tail call into the backend; the
// slow one (out of line, so the fast one needs no stack frame) counts, checks
// and traces.
#define RS_GL_FN(ret, name, params, args)                                                  \
    static __attribute__((noinline)) ret rs_slow_##name params {                           \
        rs_enter(RS_GL_IDX_##name);                                                        \
        ret r = rs_impl_##name args;                                                       \
        rs_leave(RS_GL_IDX_##name);                                                        \
        if (rs_tr_on)                                                                      \
            rs_tr_call(RS_GL_IDX_##name, #params, RS_TR_PACK args, (uint64_t)(uintptr_t)r); \
        return r;                                                                          \
    }                                                                                      \
    RS_WRAP_LINKAGE ret RS_WRAP(name) params {                                             \
        if (__builtin_expect(rs_slow, 0))                                                  \
            return rs_slow_##name args;                                                    \
        return rs_impl_##name args;                                                        \
    }
#define RS_GL_VOID(name, params, args)                                                     \
    static __attribute__((noinline)) void rs_slow_##name params {                          \
        rs_enter(RS_GL_IDX_##name);                                                        \
        rs_impl_##name args;                                                               \
        rs_leave(RS_GL_IDX_##name);                                                        \
        if (rs_tr_on)                                                                      \
            rs_tr_call(RS_GL_IDX_##name, #params, RS_TR_PACK args, 0);                     \
    }                                                                                      \
    RS_WRAP_LINKAGE void RS_WRAP(name) params {                                            \
        if (__builtin_expect(rs_slow, 0)) {                                                \
            rs_slow_##name args;                                                           \
            return;                                                                        \
        }                                                                                  \
        rs_impl_##name args;                                                               \
    }
#include "gl_api.inc"
#undef RS_GL_FN
#undef RS_GL_VOID

#ifdef RS_GL_SPY
#define RS_GL_FN(ret, name, params, args) RS_INTERPOSE(rs_spy_##name, name);
#define RS_GL_VOID(name, params, args) RS_INTERPOSE(rs_spy_##name, name);
#include "gl_api.inc"
#undef RS_GL_FN
#undef RS_GL_VOID
#endif

// --- Report -------------------------------------------------------------------
__attribute__((visibility("default"))) int rs_gl_stats(struct rs_gl_stat *out) {
    for (int i = 0; out && i < RS_GL_COUNT; i++) {
        out[i].name = rs_names[i];
        out[i].calls = rs_calls[i];
        out[i].errors = rs_errors[i];
        out[i].first_error = rs_first_error[i];
    }
    return RS_GL_COUNT;
}

static int rs_by_calls(const void *a, const void *b) {
    unsigned long ca = rs_calls[*(const int *)a], cb = rs_calls[*(const int *)b];
    return ca < cb ? 1 : ca > cb ? -1 : *(const int *)a - *(const int *)b;
}

static void rs_write_report(void) {
    if (!rs_log_path)
        return;
    char tmp[1100];
    snprintf(tmp, sizeof tmp, "%s.tmp", rs_log_path);
    FILE *f = fopen(tmp, "w");
    if (!f)
        return;
    fprintf(f, "# rs-gl %s report, pid %d, check=%d\n", RS_MODE, getpid(), rs_check);
    fprintf(f, "frames %lu\n", (unsigned long)rs_gl_frames);
    int notes = rs_note_count < RS_MAX_NOTES ? rs_note_count : RS_MAX_NOTES;
    for (int i = 0; i < notes; i++)
        if (rs_notes[i])
            fprintf(f, "note %s\n", rs_notes[i]);
    int order[RS_GL_COUNT], used = 0, used_extra = 0;
    for (int i = 0; i < RS_GL_COUNT; i++)
        order[i] = i;
    qsort(order, RS_GL_COUNT, sizeof order[0], rs_by_calls);
    for (int k = 0; k < RS_GL_COUNT; k++) {
        int i = order[k];
        if (!rs_calls[i])
            continue;
        if (i < RS_GL_GAME_COUNT)
            used++;
        else
            used_extra++;
        fprintf(f, "call %-40s %12lu", rs_names[i], (unsigned long)rs_calls[i]);
        if (rs_errors[i])
            fprintf(f, "  errors %lu first %s", (unsigned long)rs_errors[i],
                    rs_error_name(rs_first_error[i]));
        fputc('\n', f);
    }
    fprintf(f, "used %d/%d\n", used, RS_GL_GAME_COUNT);  // the game's imports
    if (RS_GL_COUNT > RS_GL_GAME_COUNT)
        fprintf(f, "used-extra %d/%d\n", used_extra, RS_GL_COUNT - RS_GL_GAME_COUNT);
    for (int i = 0; i < RS_GL_GAME_COUNT; i++)
        if (!rs_calls[i])
            fprintf(f, "unused %s\n", rs_names[i]);
    fclose(f);
    rename(tmp, rs_log_path);
}

static void *rs_report_thread(void *arg) {
    (void)arg;
    for (;;) {
        sleep(2);
        rs_write_report();
    }
    return NULL;
}

// --- Backend --------------------------------------------------------------------
static _Atomic int rs_backend = -1;

// RS_GL_BACKEND, parsed once (also when asked before the constructor ran). libRSOpenGL: Metal
// unless it says gl (or opengl); the spy wraps the system framework only.
static rs_gfx_backend_t rs_backend_get(void) {
    int b = rs_backend;
    if (b < 0) {
#ifdef RS_GL_SPY
        b = RS_GFX_GL;
#else
        const char *e = getenv("RS_GL_BACKEND");
        b = RS_GFX_METAL;
        // As the shell's rsp_renderer_parse reads it: any case, white space around it.
        char v[16] = "";
        if (e) {
            while (isspace((unsigned char)*e))
                e++;
            size_t n = 0;
            for (; e[n] && n < sizeof v - 1; n++)
                v[n] = (char)tolower((unsigned char)e[n]);
            while (n && isspace((unsigned char)v[n - 1]))
                n--;
            v[n] = 0;
        }
        if (!strcmp(v, "gl") || !strcmp(v, "opengl"))
            b = RS_GFX_GL;
        else if (*v && strcmp(v, "metal"))
            fprintf(stderr, "[rs-gl] unknown RS_GL_BACKEND=%s, using metal\n", e);
#endif
        rs_backend = b;
    }
    return (rs_gfx_backend_t)b;
}

#ifndef RS_GL_SPY
__attribute__((visibility("default"))) rs_gfx_backend_t rs_gfx_backend(void) {
    return rs_backend_get();
}

// Called by the generated weak rsmtl_<name> stubs (rsmtl_stubs.c).
void rsmtl_unimplemented(int idx, const char *name) {
    static _Atomic unsigned char told[RS_GL_COUNT];
    if (idx < 0 || idx >= RS_GL_COUNT || atomic_exchange(&told[idx], 1))
        return;
    fprintf(stderr, "[rs-gl] metal: unimplemented %s\n", name);
    rs_gl_note("metal: unimplemented %s", name);
}
#endif

// --- Load ---------------------------------------------------------------------
__attribute__((constructor)) static void rs_gl_init(void) {
    const char *e;
    rs_gfx_backend_t backend = rs_backend_get();
    rs_check = (e = getenv("RS_GL_CHECK")) && *e == '1';
    rs_verbose = (e = getenv("RS_GL_VERBOSE")) && *e == '1';
    rs_log_path = getenv("RS_GL_LOG");
    rs_shader_dir = getenv("RS_GL_DUMP_SHADERS");
    if (rs_shader_dir)
        mkdir(rs_shader_dir, 0755);

    for (int i = 0; i < RS_GL_COUNT; i++)
        rs_is_cgl[i] = !strncmp(rs_names[i], "CGL", 3);

#ifdef RS_GL_SPY
    (void)backend;
#define RS_GL_FN(ret, name, params, args) rs_real_##name = name;
#else
    if (backend == RS_GFX_METAL) {
#define RS_GL_FN(ret, name, params, args) rs_real_##name = rsmtl_##name;
#define RS_GL_VOID(name, params, args) RS_GL_FN(void, name, params, args)
#include "gl_api.inc"
#undef RS_GL_FN
#undef RS_GL_VOID
    } else {
    void *sys = dlopen(kSystemGL, RTLD_NOW | RTLD_LOCAL);
    if (!sys) {
        fprintf(stderr, "[rs-gl] can't load %s: %s\n", kSystemGL, dlerror());
        abort();
    }
#define RS_GL_FN(ret, name, params, args)                                      \
    if (!(*(void **)&rs_real_##name = dlsym(sys, #name))) {                    \
        fprintf(stderr, "[rs-gl] system OpenGL has no %s\n", #name);           \
        abort();                                                               \
    }
#endif
#define RS_GL_VOID(name, params, args) RS_GL_FN(void, name, params, args)
#include "gl_api.inc"
#undef RS_GL_FN
#undef RS_GL_VOID
#ifndef RS_GL_SPY
    }
#endif

#define RS_GL_FN(ret, name, params, args) rs_impl_##name = rs_real_##name;
#define RS_GL_VOID(name, params, args) RS_GL_FN(void, name, params, args)
#include "gl_api.inc"
#undef RS_GL_FN
#undef RS_GL_VOID
    static void *real_tab[RS_GL_COUNT];
#define RS_GL_FN(ret, name, params, args) real_tab[RS_GL_IDX_##name] = (void *)rs_real_##name;
#define RS_GL_VOID(name, params, args) RS_GL_FN(void, name, params, args)
#include "gl_api.inc"
#undef RS_GL_FN
#undef RS_GL_VOID
#ifdef RS_GL_SPY
    rs_tr_init(real_tab, rs_names, 1);
#else
    rs_tr_init(real_tab, rs_names, backend == RS_GFX_GL);
#endif
    rs_impl_glGetError = rs_glGetError;
    rs_impl_glGetString = rs_glGetString;
    rs_impl_glGetIntegerv = rs_glGetIntegerv;
    rs_impl_glCreateShader = rs_glCreateShader;
    rs_impl_glShaderSource = rs_glShaderSource;
#ifndef RS_GL_SPY
    rs_impl_glBindFramebuffer = rs_glBindFramebuffer;
    rs_impl_glBindFramebufferEXT = rs_glBindFramebufferEXT;
    rs_impl_glDeleteFramebuffers = rs_glDeleteFramebuffers;
    rs_impl_glGetFloatv = rs_glGetFloatv;
    rs_impl_glGetBooleanv = rs_glGetBooleanv;
    rs_impl_glUseProgram = rs_glUseProgram;
    rs_impl_glCreateProgram = rs_glCreateProgram;
    rs_impl_glLinkProgram = rs_glLinkProgram;
    rs_impl_glProgramBinary = rs_glProgramBinary;
    rs_impl_glDeleteProgram = rs_glDeleteProgram;
    rs_impl_CGLCreateContext = rs_CGLCreateContext;
    rs_impl_CGLDestroyContext = rs_CGLDestroyContext;
#endif
    rs_slow = rs_check || rs_log_path || rs_tr_on || ((e = getenv("RS_GL_STATS")) && *e == '1');

    if (rs_verbose || rs_log_path)
        fprintf(stderr, "[rs-gl] %s loaded (%s), %d functions, backend %s, check=%d\n", RS_MODE,
#if defined(__arm64__)
                "arm64",
#else
                "x86_64",
#endif
                RS_GL_COUNT, backend == RS_GFX_METAL ? "metal" : "gl", rs_check);
    if (rs_log_path) {
        pthread_t t;
        pthread_create(&t, NULL, rs_report_thread, NULL);
        pthread_detach(t);
        atexit(rs_write_report);
    }
}
