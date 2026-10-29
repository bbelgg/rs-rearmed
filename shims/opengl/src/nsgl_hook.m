// Records how the game sets up its NSOpenGL context: the pixel-format
// attributes, the share context and setValues:forParameter: (swap interval
// etc.). The game binds NSOpenGLContext/NSOpenGLPixelFormat from AppKit, so
// these are method swizzles, not symbol interposition. Notes go to the
// rs-gl report (rsgl.c).
#import <AppKit/AppKit.h>
#import <ImageIO/ImageIO.h>
#import <objc/runtime.h>
#include <CoreVideo/CVBase.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <mach/mach_time.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "gltrace.h"

#pragma clang diagnostic ignored "-Wdeprecated-declarations"

void rs_gl_note(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

static const char *rs_pfa_name(NSOpenGLPixelFormatAttribute a, int *has_value) {
    *has_value = 1;
    switch (a) {
    case NSOpenGLPFADoubleBuffer: *has_value = 0; return "DoubleBuffer";
    case NSOpenGLPFAStereo: *has_value = 0; return "Stereo";
    case NSOpenGLPFAColorSize: return "ColorSize";
    case NSOpenGLPFAAlphaSize: return "AlphaSize";
    case NSOpenGLPFADepthSize: return "DepthSize";
    case NSOpenGLPFAStencilSize: return "StencilSize";
    case NSOpenGLPFAAccumSize: return "AccumSize";
    case NSOpenGLPFAMinimumPolicy: *has_value = 0; return "MinimumPolicy";
    case NSOpenGLPFAMaximumPolicy: *has_value = 0; return "MaximumPolicy";
    case NSOpenGLPFASampleBuffers: return "SampleBuffers";
    case NSOpenGLPFASamples: return "Samples";
    case NSOpenGLPFAAuxBuffers: return "AuxBuffers";
    case NSOpenGLPFAColorFloat: *has_value = 0; return "ColorFloat";
    case NSOpenGLPFAMultisample: *has_value = 0; return "Multisample";
    case NSOpenGLPFASupersample: *has_value = 0; return "Supersample";
    case NSOpenGLPFASampleAlpha: *has_value = 0; return "SampleAlpha";
    case NSOpenGLPFARendererID: return "RendererID";
    case NSOpenGLPFANoRecovery: *has_value = 0; return "NoRecovery";
    case NSOpenGLPFAAccelerated: *has_value = 0; return "Accelerated";
    case NSOpenGLPFAClosestPolicy: *has_value = 0; return "ClosestPolicy";
    case NSOpenGLPFABackingStore: *has_value = 0; return "BackingStore";
    case NSOpenGLPFAScreenMask: return "ScreenMask";
    case NSOpenGLPFAAllowOfflineRenderers: *has_value = 0; return "AllowOfflineRenderers";
    case NSOpenGLPFAAcceleratedCompute: *has_value = 0; return "AcceleratedCompute";
    case NSOpenGLPFAOpenGLProfile: return "OpenGLProfile";
    case NSOpenGLPFAVirtualScreenCount: return "VirtualScreenCount";
    // Values from CGLTypes.h (kCGLPFA*); AppKit dropped the deprecated names.
    case 3: *has_value = 0; return "TripleBuffer";
    case 53: *has_value = 0; return "OffScreen";
    case 54: *has_value = 0; return "FullScreen";
    case 57: *has_value = 0; return "AuxDepthStencil";
    case 71: *has_value = 0; return "SingleRenderer";
    case 75: *has_value = 0; return "Robust";
    case 78: *has_value = 0; return "MPSafe";
    case 80: *has_value = 0; return "Window";
    case 81: *has_value = 0; return "MultiScreen";
    case 83: *has_value = 0; return "Compliant";
    default: *has_value = 0; return NULL;
    }
}

static id (*rs_orig_pf_init)(id, SEL, const NSOpenGLPixelFormatAttribute *);
static id rs_pf_init(id self, SEL _cmd, const NSOpenGLPixelFormatAttribute *attrs) {
    char buf[1024];
    size_t n = 0;
    buf[0] = 0;
    for (const NSOpenGLPixelFormatAttribute *a = attrs; a && *a && n < sizeof buf - 64; a++) {
        int has_value;
        const char *name = rs_pfa_name(*a, &has_value);
        if (name && has_value && a[1]) {
            n += snprintf(buf + n, sizeof buf - n, " %s=0x%x", name, a[1]);
            a++;
        } else if (name) {
            n += snprintf(buf + n, sizeof buf - n, " %s", name);
        } else {
            n += snprintf(buf + n, sizeof buf - n, " attr%u", *a);
        }
    }
    id r = rs_orig_pf_init(self, _cmd, attrs);
    rs_gl_note("NSOpenGLPixelFormat initWithAttributes:%s -> %s", buf, r ? "ok" : "nil");
    return r;
}

static id (*rs_orig_ctx_init)(id, SEL, NSOpenGLPixelFormat *, NSOpenGLContext *);
static id rs_ctx_init(id self, SEL _cmd, NSOpenGLPixelFormat *pf, NSOpenGLContext *share) {
    id r = rs_orig_ctx_init(self, _cmd, pf, share);
    rs_gl_note("NSOpenGLContext initWithFormat:%p shareContext:%p -> %p (CGL %p)", pf, share, r,
               r ? [(NSOpenGLContext *)r CGLContextObj] : NULL);
    return r;
}

static void (*rs_orig_ctx_setvals)(id, SEL, const GLint *, NSOpenGLContextParameter);
static void rs_ctx_setvals(id self, SEL _cmd, const GLint *vals, NSOpenGLContextParameter p) {
    rs_gl_note("NSOpenGLContext %p setValues:%d forParameter:%ld", self, vals ? vals[0] : -1,
               (long)p);
    rs_orig_ctx_setvals(self, _cmd, vals, p);
}

static void (*rs_orig_ctx_setview)(id, SEL, NSView *);
static void rs_ctx_setview(id self, SEL _cmd, NSView *view) {
    rs_gl_note("NSOpenGLContext %p setView:%s wantsBestResolutionOpenGLSurface=%d", self,
               view ? object_getClassName(view) : "nil",
               view ? (int)[view wantsBestResolutionOpenGLSurface] : -1);
    rs_orig_ctx_setview(self, _cmd, view);
}

// --- Frame count + snapshots ---------------------------------------------------
// Snapshot GL calls go straight to the system framework: in the shim the gl*
// names resolve to rsgl.c's wrappers, which would count them as game calls.
extern _Atomic unsigned long rs_gl_frames;
static const char *rs_snap_dir;
static double rs_snap_every = 5, rs_snap_last;
static struct {
    void (*GetIntegerv)(GLenum, GLint *);
    void (*BindFramebuffer)(GLenum, GLuint);
    void (*ReadBuffer)(GLenum);
    void (*PixelStorei)(GLenum, GLint);
    void (*ReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *);
    GLenum (*GetError)(void);
} rs_snap_gl;

static void rs_snapshot_to(NSOpenGLContext *ctx, NSString *path) {
    NSView *view = [ctx view];
    if (!view || !rs_snap_gl.ReadPixels)
        return;
    NSRect r = [view wantsBestResolutionOpenGLSurface] ? [view convertRectToBacking:[view bounds]]
                                                       : [view bounds];
    size_t w = (size_t)r.size.width, h = (size_t)r.size.height;
    if (!w || !h || w > 16384 || h > 16384)
        return;
    GLint fbo = 0, rbuf = 0, pack = 4;
    rs_snap_gl.GetIntegerv(0x8CAA /* GL_READ_FRAMEBUFFER_BINDING */, &fbo);
    rs_snap_gl.GetIntegerv(0x0C02 /* GL_READ_BUFFER */, &rbuf);
    rs_snap_gl.GetIntegerv(0x0D05 /* GL_PACK_ALIGNMENT */, &pack);
    rs_snap_gl.BindFramebuffer(0x8CA8 /* GL_READ_FRAMEBUFFER */, 0);
    rs_snap_gl.ReadBuffer(0x0405 /* GL_BACK */);
    rs_snap_gl.PixelStorei(0x0D05, 1);
    NSMutableData *px = [NSMutableData dataWithLength:w * h * 4];
    rs_snap_gl.ReadPixels(0, 0, (GLsizei)w, (GLsizei)h, 0x1908 /* GL_RGBA */,
                          0x1401 /* GL_UNSIGNED_BYTE */, px.mutableBytes);
    rs_snap_gl.PixelStorei(0x0D05, pack);
    rs_snap_gl.ReadBuffer((GLenum)rbuf);
    rs_snap_gl.BindFramebuffer(0x8CA8, (GLuint)fbo);
    rs_snap_gl.GetError();  // a snapshot must not leave errors for the game

    // GL rows are bottom-up.
    unsigned char *p = px.mutableBytes, *tmp = malloc(w * 4);
    for (size_t y = 0; y < h / 2; y++) {
        memcpy(tmp, p + y * w * 4, w * 4);
        memcpy(p + y * w * 4, p + (h - 1 - y) * w * 4, w * 4);
        memcpy(p + (h - 1 - y) * w * 4, tmp, w * 4);
    }
    free(tmp);
    for (size_t i = 0; i < w * h; i++)
        p[i * 4 + 3] = 255;  // the game's alpha channel is not coverage

    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    CGDataProviderRef dp = CGDataProviderCreateWithCFData((__bridge CFDataRef)px);
    CGImageRef img = CGImageCreate(w, h, 8, 32, w * 4, cs, (CGBitmapInfo)kCGImageAlphaNoneSkipLast,
                                   dp, NULL, false, kCGRenderingIntentDefault);
    CGImageDestinationRef dst = CGImageDestinationCreateWithURL(
        (__bridge CFURLRef)[NSURL fileURLWithPath:path], CFSTR("public.png"), 1, NULL);
    if (dst && img) {
        CGImageDestinationAddImage(dst, img, NULL);
        CGImageDestinationFinalize(dst);
        rs_gl_note("snapshot %s (%zux%zu)", path.UTF8String, w, h);
    }
    if (dst)
        CFRelease(dst);
    CGImageRelease(img);
    CGDataProviderRelease(dp);
    CGColorSpaceRelease(cs);
}

static void rs_snapshot(NSOpenGLContext *ctx, unsigned long frame) {
    rs_snapshot_to(ctx, [NSString stringWithFormat:@"%s/frame_%06lu.png", rs_snap_dir, frame]);
}

static void rs_trace_snap(void *ctx, const char *path) {
    @autoreleasepool {
        rs_snapshot_to((__bridge NSOpenGLContext *)ctx, [NSString stringWithUTF8String:path]);
    }
}


// --- Frame times (RS_GL_FRAMETIMES=<file>) ---------------------------------------
// A timestamp per present, cheap enough to leave on for a whole run: each event
// is four mach_absolute_time() reads at most and a store into a preallocated
// ring; a utility-QoS thread writes the ring out once a second (and at exit),
// so the frame thread makes no syscalls. Independent of RS_GL_TRACE_DIR.
// Events (both builds, the original's OpenGLView and the native shell's have
// the same methods):
//   F  -[NSOpenGLContext flushBuffer] (this swizzle; flags bit 0: a
//      RS_GL_SNAPSHOT_DIR snapshot was taken inside it)
//   D  -[OpenGLView drawView]: CGLLockContext .. game frame .. flushBuffer .. unlock
//   V  -[OpenGLView getFrameForTime:], the CVDisplayLink callback; x = the
//      CVTimeStamp hostTime of the frame it is asked for (the vsync target)
//   G  native shell only (shims/platform rsp_shell.m, through
//      rs_gl_frametime_mark): t0 = CGL lock acquired, t1 = rsp_game_frame returned
// Line format (times in ns since the library loaded, CLOCK_UPTIME_RAW base):
//   K t0 dur x tid flags        W now unix_ns (wall clock, every write-out)
typedef struct {
    uint64_t t0, t1, x, tid;
    _Atomic uint64_t seq;  // ring index + 1 once the slot is written
    uint32_t kind, flags;
} rs_ft_ev;
#define RS_FT_N (1u << 15)  // ~2 min of events at 60 fps; written out every second
static rs_ft_ev *rs_ft_ring;
static _Atomic uint64_t rs_ft_w;
static uint64_t rs_ft_r, rs_ft_lost;
static int rs_ft_fd = -1;
static mach_timebase_info_data_t rs_ft_tb;
static uint64_t rs_ft_base;
static pthread_mutex_t rs_ft_mu = PTHREAD_MUTEX_INITIALIZER;
static __thread uint64_t rs_ft_tid;

static inline uint64_t rs_ft_ns(uint64_t ticks) {
    return (uint64_t)((__uint128_t)ticks * rs_ft_tb.numer / rs_ft_tb.denom);
}
static inline uint64_t rs_ft_rel(uint64_t ticks) {
    return ticks > rs_ft_base ? rs_ft_ns(ticks - rs_ft_base) : 0;
}

// Also called by the native shell (dlsym), hence exported. Times in mach ticks.
__attribute__((visibility("default"))) void rs_gl_frametime_mark(int kind, uint64_t t0, uint64_t t1,
                                                                  uint64_t x, unsigned flags) {
    if (!rs_ft_ring)
        return;
    if (!rs_ft_tid)
        pthread_threadid_np(NULL, &rs_ft_tid);
    uint64_t i = atomic_fetch_add_explicit(&rs_ft_w, 1, memory_order_relaxed);
    rs_ft_ev *e = &rs_ft_ring[i & (RS_FT_N - 1)];
    atomic_store_explicit(&e->seq, 0, memory_order_relaxed);
    atomic_thread_fence(memory_order_release);
    e->t0 = t0;
    e->t1 = t1;
    e->x = x;
    e->tid = rs_ft_tid;
    e->kind = (uint32_t)kind;
    e->flags = flags;
    atomic_store_explicit(&e->seq, i + 1, memory_order_release);
}

static void rs_ft_put(char *buf, size_t *n, size_t cap, const char *fmt, ...)
    __attribute__((format(printf, 4, 5)));
static void rs_ft_put(char *buf, size_t *n, size_t cap, const char *fmt, ...) {
    if (cap - *n < 160) {
        (void)!write(rs_ft_fd, buf, *n);
        *n = 0;
    }
    va_list ap;
    va_start(ap, fmt);
    int k = vsnprintf(buf + *n, cap - *n, fmt, ap);
    va_end(ap);
    if (k > 0)
        *n += (size_t)k < cap - *n ? (size_t)k : cap - *n - 1;
}

static void rs_ft_drain(void) {
    static char buf[1 << 16];
    pthread_mutex_lock(&rs_ft_mu);
    size_t n = 0;
    uint64_t w = atomic_load_explicit(&rs_ft_w, memory_order_acquire);
    if (w - rs_ft_r > RS_FT_N) {  // the writer lapped us
        rs_ft_lost += w - rs_ft_r - RS_FT_N;
        rs_ft_put(buf, &n, sizeof buf, "X lost %llu\n", (unsigned long long)(w - rs_ft_r - RS_FT_N));
        rs_ft_r = w - RS_FT_N;
    }
    for (; rs_ft_r < w; rs_ft_r++) {
        rs_ft_ev *e = &rs_ft_ring[rs_ft_r & (RS_FT_N - 1)];
        uint64_t s = atomic_load_explicit(&e->seq, memory_order_acquire);
        if (s < rs_ft_r + 1)  // not written yet: take it next time
            break;
        if (s > rs_ft_r + 1) {  // already overwritten by a later event
            rs_ft_lost++;
            continue;
        }
        rs_ft_ev c = {e->t0, e->t1, e->x, e->tid, 0, e->kind, e->flags};
        atomic_thread_fence(memory_order_acquire);
        if (atomic_load_explicit(&e->seq, memory_order_relaxed) != rs_ft_r + 1) {
            rs_ft_lost++;
            continue;
        }
        uint64_t t0 = rs_ft_rel(c.t0);
        uint64_t dur = c.t1 > c.t0 ? rs_ft_ns(c.t1 - c.t0) : 0;
        uint64_t x = c.x ? rs_ft_rel(c.x) : 0;
        rs_ft_put(buf, &n, sizeof buf, "%c %llu %llu %llu %llu %u\n", (char)c.kind,
                  (unsigned long long)t0, (unsigned long long)dur, (unsigned long long)x,
                  (unsigned long long)c.tid, c.flags);
    }
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    rs_ft_put(buf, &n, sizeof buf, "W %llu %llu\n", (unsigned long long)rs_ft_rel(mach_absolute_time()),
              (unsigned long long)ts.tv_sec * 1000000000ull + (unsigned long long)ts.tv_nsec);
    if (n)
        (void)!write(rs_ft_fd, buf, n);
    pthread_mutex_unlock(&rs_ft_mu);
}

static void *rs_ft_thread(void *arg) {
    (void)arg;
    for (;;) {
        sleep(1);
        rs_ft_drain();
    }
    return NULL;
}

static void (*rs_ft_orig_draw)(id, SEL);
static void rs_ft_draw(id self, SEL _cmd) {
    uint64_t t0 = mach_absolute_time();
    rs_ft_orig_draw(self, _cmd);  // the native shell may free self at the very end: not touched after
    rs_gl_frametime_mark('D', t0, mach_absolute_time(), 0, 0);
}
static CVReturn (*rs_ft_orig_gfft)(id, SEL, const CVTimeStamp *);
static CVReturn rs_ft_gfft(id self, SEL _cmd, const CVTimeStamp *out) {
    uint64_t t0 = mach_absolute_time();
    CVReturn r = rs_ft_orig_gfft(self, _cmd, out);
    rs_gl_frametime_mark('V', t0, mach_absolute_time(), out ? out->hostTime : 0, 0);
    return r;
}

// At the first present: the game's classes are certainly there, and this runs on
// the thread that calls drawView, so no call can see a half-installed swizzle.
static void rs_ft_hook_view(void) {
    static int done;
    if (done)
        return;
    done = 1;
    Class v = objc_getClass("OpenGLView");
    Method m;
    if (v && (m = class_getInstanceMethod(v, @selector(drawView)))) {
        rs_ft_orig_draw = (void (*)(id, SEL))method_getImplementation(m);
        method_setImplementation(m, (IMP)rs_ft_draw);
    }
    if (v && (m = class_getInstanceMethod(v, @selector(getFrameForTime:)))) {
        rs_ft_orig_gfft = (CVReturn(*)(id, SEL, const CVTimeStamp *))method_getImplementation(m);
        method_setImplementation(m, (IMP)rs_ft_gfft);
    }
    rs_gl_note("frametimes: OpenGLView %s, drawView %s, getFrameForTime: %s", v ? "found" : "missing",
               rs_ft_orig_draw ? "timed" : "-", rs_ft_orig_gfft ? "timed" : "-");
}

static void rs_ft_init(const char *path) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) {
        fprintf(stderr, "[rs-gl] frametimes: can't open %s\n", path);
        return;
    }
    mach_timebase_info(&rs_ft_tb);
    rs_ft_base = mach_absolute_time();
    rs_ft_ring = calloc(RS_FT_N, sizeof *rs_ft_ring);
    if (!rs_ft_ring) {
        close(fd);
        return;
    }
    rs_ft_fd = fd;
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    char h[256];
    int k = snprintf(h, sizeof h,
                     "# rs-gl frametimes v1 pid=%d arch=%s timebase=%u/%u\n"
                     "W 0 %llu\n",
                     getpid(),
#if defined(__arm64__)
                     "arm64",
#else
                     "x86_64",
#endif
                     rs_ft_tb.numer, rs_ft_tb.denom,
                     (unsigned long long)ts.tv_sec * 1000000000ull + (unsigned long long)ts.tv_nsec);
    (void)!write(fd, h, (size_t)k);
    pthread_attr_t a;
    pthread_attr_init(&a);
    pthread_attr_set_qos_class_np(&a, QOS_CLASS_UTILITY, 0);
    pthread_attr_setdetachstate(&a, PTHREAD_CREATE_DETACHED);
    pthread_t t;
    pthread_create(&t, &a, rs_ft_thread, NULL);
    pthread_attr_destroy(&a);
    atexit(rs_ft_drain);
    rs_gl_note("frametimes -> %s", path);
}

static void (*rs_orig_flush)(id, SEL);
static void rs_flush(id self, SEL _cmd) {
    uint64_t ft0 = rs_ft_ring ? mach_absolute_time() : 0;
    unsigned ftflags = 0;
    unsigned long frame = atomic_fetch_add(&rs_gl_frames, 1) + 1;
    if (rs_snap_dir) {
        double now = CFAbsoluteTimeGetCurrent();
        if (now - rs_snap_last >= rs_snap_every) {
            rs_snap_last = now;
            // <dir>/stop ends snapshots (benchmarks); one stat per due snapshot.
            char stop[1100];
            struct stat st;
            snprintf(stop, sizeof stop, "%s/stop", rs_snap_dir);
            if (stat(stop, &st) == 0) {
                rs_gl_note("snapshots stopped (%s exists)", stop);
                rs_snap_dir = NULL;
            } else {
                ftflags |= 1;
                @autoreleasepool {
                    rs_snapshot(self, frame);
                }
            }
        }
    }
    rs_tr_frame_end((__bridge void *)self, frame, rs_trace_snap);
    rs_orig_flush(self, _cmd);
    if (ft0) {
        rs_gl_frametime_mark('F', ft0, mach_absolute_time(), 0, ftflags);
        rs_ft_hook_view();
    }
}

static void rs_swizzle(Class c, SEL sel, IMP imp, void *orig) {
    Method m = class_getInstanceMethod(c, sel);
    if (m)
        *(IMP *)orig = method_setImplementation(m, imp);
}

__attribute__((constructor)) static void rs_nsgl_init(void) {
    rs_swizzle([NSOpenGLPixelFormat class], @selector(initWithAttributes:), (IMP)rs_pf_init,
               &rs_orig_pf_init);
    rs_swizzle([NSOpenGLContext class], @selector(initWithFormat:shareContext:),
               (IMP)rs_ctx_init, &rs_orig_ctx_init);
    rs_swizzle([NSOpenGLContext class], @selector(setValues:forParameter:),
               (IMP)rs_ctx_setvals, &rs_orig_ctx_setvals);
    rs_swizzle([NSOpenGLContext class], @selector(setView:), (IMP)rs_ctx_setview,
               &rs_orig_ctx_setview);
    rs_swizzle([NSOpenGLContext class], @selector(flushBuffer), (IMP)rs_flush, &rs_orig_flush);

    const char *ft = getenv("RS_GL_FRAMETIMES");
    if (!ft || !*ft)
        ft = getenv("RS_GLSPY_FRAMETIMES");
    if (ft && *ft)
        rs_ft_init(ft);

    rs_snap_dir = getenv("RS_GL_SNAPSHOT_DIR");
    if (rs_snap_dir && !*rs_snap_dir)
        rs_snap_dir = NULL;  // set but empty: off
    if (rs_snap_dir || getenv("RS_GL_TRACE_DIR")) {
        if (rs_snap_dir) mkdir(rs_snap_dir, 0755);
        const char *secs = getenv("RS_GL_SNAPSHOT_SECS");
        if (secs && atof(secs) > 0)
            rs_snap_every = atof(secs);
        rs_snap_last = CFAbsoluteTimeGetCurrent() - rs_snap_every;  // first frame counts
        void *sys = dlopen("/System/Library/Frameworks/OpenGL.framework/Versions/A/OpenGL",
                           RTLD_NOW | RTLD_LOCAL);
        rs_snap_gl.GetIntegerv = dlsym(sys, "glGetIntegerv");
        rs_snap_gl.BindFramebuffer = dlsym(sys, "glBindFramebuffer");
        rs_snap_gl.ReadBuffer = dlsym(sys, "glReadBuffer");
        rs_snap_gl.PixelStorei = dlsym(sys, "glPixelStorei");
        rs_snap_gl.ReadPixels = dlsym(sys, "glReadPixels");
        rs_snap_gl.GetError = dlsym(sys, "glGetError");
    }
}
