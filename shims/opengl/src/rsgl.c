// Wrapper layer over every GL/CGL function the game imports (gl_api.inc).
//
// Built two ways from this one file:
//
//   RS_GL_SPY defined   libglspy.dylib: injected with DYLD_INSERT_LIBRARIES; a
//                       __interpose entry per function rebinds the game *and*
//                       Cg.framework (which calls GL itself) to the wrappers.
//   otherwise           libRSOpenGL.dylib: the OpenGL.framework replacement. It
//                       exports the 107 names itself, forwards to the system
//                       framework (Apple's Metal-backed GL) through dlsym, and
//                       re-exports the rest of it. This is where a different
//                       backend (ANGLE) or per-call compatibility fixes go.
//
// Both count calls per function and, with RS_GL_CHECK=1, check glGetError after
// every call. An error found that way is handed back on the caller's next
// glGetError, so the game still sees it.
//
// Environment:
//   RS_GL_LOG=<file>          write a report there every 2 s and at exit
//   RS_GL_CHECK=1             glGetError after every call
//   RS_GL_DUMP_SHADERS=<dir>  save each glShaderSource as <dir>/<seq>_<id>_<type>.glsl
//   RS_GL_SNAPSHOT_DIR=<dir>  save the back buffer as PNG before a flushBuffer,
//                             every RS_GL_SNAPSHOT_SECS (default 5) seconds
//   RS_GL_VERBOSE=1           log load and every first error to stderr
//   RS_GL_TRACE_DIR=<dir>     per-frame call trace (gltrace.c; RS_GL_TRACE_START / _KEY / _FRAMES)
//   RS_GL_FRAMETIMES=<file>   timestamp every present, drawView and display-link callback
//                             (nsgl_hook.m). No call tracing
#include "gl_api.h"
#include "gltrace.h"

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

// rs_real_<name>: the system implementation. rs_impl_<name>: what the wrapper
// calls; the same unless a function below needs special handling.
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
static _Atomic unsigned long rs_errors[RS_GL_COUNT];
static _Atomic GLenum rs_first_error[RS_GL_COUNT];
static int rs_check, rs_verbose;
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

static void rs_leave(int i) {
    // CGL calls have no GL error state; glGetError reports its own.
    if (!rs_check || i == RS_GL_IDX_glGetError || i == RS_GL_IDX_CGLLockContext ||
        i == RS_GL_IDX_CGLUnlockContext)
        return;
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

// Remember the pname so rs_leave can say which query failed (the game's only
// GL error is a glGetIntegerv).
static void rs_glGetIntegerv(GLenum pname, GLint *params) {
    rs_last_pname = pname;
    rs_real_glGetIntegerv(pname, params);
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

// The tracer stashes an error it found while reading state back (it must not
// swallow one the game hasn't asked for yet).
void rs_tr_stash_error(unsigned e) {
    if (rs_pending_error == GL_NO_ERROR)
        rs_pending_error = e;
}

// --- Wrappers -----------------------------------------------------------------
#define RS_GL_FN(ret, name, params, args)            \
    RS_WRAP_LINKAGE ret RS_WRAP(name) params {       \
        rs_enter(RS_GL_IDX_##name);                  \
        ret r = rs_impl_##name args;                 \
        rs_leave(RS_GL_IDX_##name);                  \
        if (__builtin_expect(rs_tr_on, 0))           \
            rs_tr_call(RS_GL_IDX_##name, #params, RS_TR_PACK args, (uint64_t)(uintptr_t)r); \
        return r;                                    \
    }
#define RS_GL_VOID(name, params, args)               \
    RS_WRAP_LINKAGE void RS_WRAP(name) params {      \
        rs_enter(RS_GL_IDX_##name);                  \
        rs_impl_##name args;                         \
        rs_leave(RS_GL_IDX_##name);                  \
        if (__builtin_expect(rs_tr_on, 0))           \
            rs_tr_call(RS_GL_IDX_##name, #params, RS_TR_PACK args, 0); \
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
    int order[RS_GL_COUNT], used = 0;
    for (int i = 0; i < RS_GL_COUNT; i++)
        order[i] = i;
    qsort(order, RS_GL_COUNT, sizeof order[0], rs_by_calls);
    for (int k = 0; k < RS_GL_COUNT; k++) {
        int i = order[k];
        if (!rs_calls[i])
            continue;
        used++;
        fprintf(f, "call %-40s %12lu", rs_names[i], (unsigned long)rs_calls[i]);
        if (rs_errors[i])
            fprintf(f, "  errors %lu first %s", (unsigned long)rs_errors[i],
                    rs_error_name(rs_first_error[i]));
        fputc('\n', f);
    }
    fprintf(f, "used %d/%d\n", used, RS_GL_COUNT);
    for (int i = 0; i < RS_GL_COUNT; i++)
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

// --- Load ---------------------------------------------------------------------
__attribute__((constructor)) static void rs_gl_init(void) {
    const char *e;
    rs_check = (e = getenv("RS_GL_CHECK")) && *e == '1';
    rs_verbose = (e = getenv("RS_GL_VERBOSE")) && *e == '1';
    rs_log_path = getenv("RS_GL_LOG");
    rs_shader_dir = getenv("RS_GL_DUMP_SHADERS");
    if (rs_shader_dir)
        mkdir(rs_shader_dir, 0755);

#ifdef RS_GL_SPY
#define RS_GL_FN(ret, name, params, args) rs_real_##name = name;
#else
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
    rs_tr_init(real_tab, rs_names);
    rs_impl_glGetError = rs_glGetError;
    rs_impl_glGetString = rs_glGetString;
    rs_impl_glGetIntegerv = rs_glGetIntegerv;
    rs_impl_glCreateShader = rs_glCreateShader;
    rs_impl_glShaderSource = rs_glShaderSource;

    if (rs_verbose || rs_log_path)
        fprintf(stderr, "[rs-gl] %s loaded (%s), %d functions, check=%d\n", RS_MODE,
#if defined(__arm64__)
                "arm64",
#else
                "x86_64",
#endif
                RS_GL_COUNT, rs_check);
    if (rs_log_path) {
        pthread_t t;
        pthread_create(&t, NULL, rs_report_thread, NULL);
        pthread_detach(t);
        atexit(rs_write_report);
    }
}
