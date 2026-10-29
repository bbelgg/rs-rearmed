// Metal presentation for the shell: see rsp_present.h. Manual retain/release, like rsp_shell.m.
//
// rs_gfx_* (libRSOpenGL, shims/opengl/src/rs_gfx.h) is looked up at run time, so the shell still
// links and runs against the system OpenGL.framework when it is built without libRSOpenGL (then
// the backend is always GL).
#import "rsp_present.h"

#import <AppKit/AppKit.h>
#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>
#include <CommonCrypto/CommonDigest.h>

#include <dlfcn.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct rs_gfx_context rs_gfx_context;
static struct {
  int (*backend)(void);  // rs_gfx_backend_t: 0 GL, 1 Metal
  rs_gfx_context *(*create_layer)(void *, int, int);
  void (*make_current)(rs_gfx_context *);
  void (*resize)(rs_gfx_context *, int, int);
  void (*present)(rs_gfx_context *);
  void (*lock)(rs_gfx_context *);
  void (*unlock)(rs_gfx_context *);
  void *(*cgl)(rs_gfx_context *);
  int (*prepare)(rs_gfx_context *, double);  // optional: older libRSOpenGL builds lack it
} gfx;
static int gfx_ok;

static void gfx_load(void) {
  int ok = 1;
#define S(f, n) ok &= (*(void **)&gfx.f = dlsym(RTLD_DEFAULT, n)) != NULL
  S(backend, "rs_gfx_backend");
  S(create_layer, "rs_gfx_create_layer");
  S(make_current, "rs_gfx_make_current");
  S(resize, "rs_gfx_resize");
  S(present, "rs_gfx_present");
  S(lock, "rs_gfx_lock");
  S(unlock, "rs_gfx_unlock");
  S(cgl, "rs_gfx_cgl");
#undef S
  gfx_ok = ok;
  *(void **)&gfx.prepare = dlsym(RTLD_DEFAULT, "rs_gfx_prepare");
}

struct rsp_mtl {
  CAMetalLayer *layer;  // retained
  rs_gfx_context *ctx;
  int w, h;                // drawable size; under the lock
  atomic_int want_swap;    // set by any thread
  int swap;                // applied to the layer; frame thread
};

int rsp_mtl_selected(void) {
  static int v = -1;
  if (v < 0) {
    static pthread_once_t once = PTHREAD_ONCE_INIT;
    pthread_once(&once, gfx_load);
    v = gfx_ok && gfx.backend() == 1;
    const char *e = getenv("RS_GL_BACKEND");
    if (!v && e && !strcmp(e, "metal"))
      fprintf(stderr, "[rsp] RS_GL_BACKEND=metal, but libRSOpenGL (rs_gfx) is not loaded: using OpenGL\n");
    if (v) fprintf(stderr, "[rsp] presenting with Metal (CAMetalLayer)\n");
  }
  return v;
}

int rsp_mtl_retina(void) {
  static int v = -1;
  if (v < 0) {
    const char *e = getenv("RS_METAL_RETINA");
    v = e && *e && atoi(e) != 0;
  }
  return v;
}

int rsp_mtl_drawables(int swap_interval) {
  static int forced = -1;
  if (forced < 0) {
    const char *e = getenv("RS_METAL_DRAWABLES");
    int n = e && *e ? atoi(e) : 0;
    forced = n == 2 || n == 3 ? n : 0;
  }
  return forced ? forced : swap_interval ? 3 : 2;
}

// Frame thread or before the first frame: the layer's sync settings for `swap`.
static void apply_swap(rsp_mtl *m, int swap) {
  [CATransaction begin];
  [CATransaction setDisableActions:YES];
  m->layer.displaySyncEnabled = swap != 0;
  m->layer.maximumDrawableCount = (NSUInteger)rsp_mtl_drawables(swap);
  [CATransaction commit];
  m->swap = swap;
}

rsp_mtl *rsp_mtl_create(void *layer, int w, int h, int swap_interval) {
  if (!rsp_mtl_selected() || !layer) return NULL;
  rsp_mtl *m = calloc(1, sizeof *m);
  m->layer = [(CAMetalLayer *)layer retain];
  m->w = w > 0 ? w : 1;
  m->h = h > 0 ? h : 1;
  m->layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
  m->layer.framebufferOnly = YES;
  m->layer.drawableSize = CGSizeMake(m->w, m->h);
  atomic_store(&m->want_swap, swap_interval);
  apply_swap(m, swap_interval);
  m->ctx = gfx.create_layer(layer, m->w, m->h);
  if (!m->ctx) {
    [m->layer release];
    free(m);
    return NULL;
  }
  return m;
}

// The drawable size for the view: points (1x), or backing pixels with RS_METAL_RETINA=1.
static void view_size(NSView *view, int *w, int *h, CGFloat *scale) {
  NSRect b = [view bounds];
  NSSize s = b.size;
  *scale = 1.0;
  if (rsp_mtl_retina()) {
    s = [view convertRectToBacking:b].size;
    *scale = b.size.width > 0 ? s.width / b.size.width : 1.0;
  }
  *w = s.width >= 1 ? (int)(s.width + 0.5) : 1;
  *h = s.height >= 1 ? (int)(s.height + 0.5) : 1;
}

rsp_mtl *rsp_mtl_attach(NSView *view, int swap_interval) {
  CAMetalLayer *layer = [CAMetalLayer layer];
  layer.opaque = YES;
  layer.device = MTLCreateSystemDefaultDevice();  // returned +1
  [layer.device release];
  int w, h;
  CGFloat scale;
  view_size(view, &w, &h, &scale);
  layer.contentsScale = scale;
  // Layer-hosting: AppKit never draws into it; the drawable is scaled to the bounds.
  [view setLayer:layer];
  [view setWantsLayer:YES];
  rsp_mtl *m = rsp_mtl_create(layer, w, h, swap_interval);
  if (!m) {
    fprintf(stderr,
            "[rsp] RS_GL_BACKEND=metal: rs_gfx_create_layer failed (the Metal core in libRSOpenGL has "
            "no layer context, or no Metal device). Not falling back to OpenGL; unset RS_GL_BACKEND "
            "to use it.\n");
    NSLog(@"RS_GL_BACKEND=metal: no Metal layer context; aborting");
    abort();
  }
  fprintf(stderr, "[rsp] Metal drawable %dx%d (%s), %d drawables, display sync %s\n", w, h,
          rsp_mtl_retina() ? "backing pixels" : "points", rsp_mtl_drawables(swap_interval),
          swap_interval ? "on" : "off");
  return m;
}

void rsp_mtl_make_current(rsp_mtl *m) { gfx.make_current(m ? m->ctx : NULL); }
void rsp_mtl_lock(rsp_mtl *m) { gfx.lock(m->ctx); }
void rsp_mtl_unlock(rsp_mtl *m) { gfx.unlock(m->ctx); }

void rsp_mtl_size(rsp_mtl *m, int *w, int *h) {
  *w = m->w;
  *h = m->h;
}

void rsp_mtl_resize(rsp_mtl *m, int w, int h) {
  if (w <= 0 || h <= 0) return;
  gfx.lock(m->ctx);
  if (w != m->w || h != m->h) {
    m->w = w;
    m->h = h;
    gfx.resize(m->ctx, w, h);  // sets the layer's drawableSize
  }
  gfx.unlock(m->ctx);
}

void rsp_mtl_view_resized(rsp_mtl *m, NSView *view) {
  int w, h;
  CGFloat scale;
  view_size(view, &w, &h, &scale);
  if (m->layer.contentsScale != scale) {
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    m->layer.contentsScale = scale;
    [CATransaction commit];
  }
  rsp_mtl_resize(m, w, h);
}

double rsp_mtl_scale(rsp_mtl *m) { return rsp_mtl_retina() ? m->layer.contentsScale : 1.0; }

void rsp_mtl_set_swap_interval(rsp_mtl *m, int interval) { atomic_store(&m->want_swap, interval); }

void rsp_mtl_present(rsp_mtl *m) {
  int s = atomic_load(&m->want_swap);
  if (s != m->swap) apply_swap(m, s);
  gfx.present(m->ctx);
}

void *rsp_mtl_layer(rsp_mtl *m) { return m->layer; }
void *rsp_mtl_cgl(rsp_mtl *m) { return gfx.cgl(m->ctx); }
void *rsp_mtl_context(rsp_mtl *m) { return m->ctx; }

CGDirectDisplayID rsp_mtl_display(NSView *view) {
  NSScreen *s = [[view window] screen];
  NSNumber *n = s ? [[s deviceDescription] objectForKey:@"NSScreenNumber"] : nil;
  return n ? (CGDirectDisplayID)[n unsignedIntValue] : CGMainDisplayID();
}

// --- Safe load ---------------------------------------------------------------------------------
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"  // CC_MD5: a file check, not security
int rsp_psarc_check(const char *path, long long size, const char *md5_hex) {
  struct stat sb;
  if (stat(path, &sb) || !S_ISREG(sb.st_mode)) return -1;  // stat follows a symlink
  if ((long long)sb.st_size != size) return 1;
  int fd = open(path, O_RDONLY);
  if (fd < 0) return -1;
  CC_MD5_CTX md;
  CC_MD5_Init(&md);
  static unsigned char buf[1 << 20];
  ssize_t n;
  while ((n = read(fd, buf, sizeof buf)) > 0) CC_MD5_Update(&md, buf, (CC_LONG)n);
  close(fd);
  if (n < 0) return -1;
  unsigned char d[CC_MD5_DIGEST_LENGTH];
  CC_MD5_Final(d, &md);
  char hex[2 * CC_MD5_DIGEST_LENGTH + 1];
  for (int i = 0; i < CC_MD5_DIGEST_LENGTH; i++) snprintf(hex + 2 * i, 3, "%02x", d[i]);
  return strcasecmp(hex, md5_hex) != 0;
}
#pragma clang diagnostic pop

int rsp_mtl_safe_load(const char *root) {
  const char *e = getenv("RS_METAL_SAFE_LOAD");
  if (e && *e) {
    int on = atoi(e) != 0;
    fprintf(stderr, "[rsp] RS_METAL_SAFE_LOAD=%s: safe load %s\n", e, on ? "on" : "off");
    return on;
  }
  char path[2048];
  size_t n = strlen(root);
  snprintf(path, sizeof path, "%s%scache.psarc", root, n && root[n - 1] == '/' ? "" : "/");
  int r = rsp_psarc_check(path, RSP_CACHE_PSARC_SIZE, RSP_CACHE_PSARC_MD5);
  if (r < 0)
    fprintf(stderr, "[rsp] cache.psarc not found or unreadable: safe load off\n");
  else
    fprintf(stderr, "[rsp] cache.psarc is %s: safe load %s\n", r ? "modded" : "the original",
            r ? "on (shaders compiled before loading, compile pool throttled while loading)" : "off");
  return r > 0;
}

int rsp_mtl_prepare(rsp_mtl *m, double max_seconds) {
  if (!gfx.prepare) {
    fprintf(stderr, "[rsp] safe load: this libRSOpenGL has no rs_gfx_prepare\n");
    return -2;
  }
  return gfx.prepare(m->ctx, max_seconds);
}

void rsp_mtl_hold_black(rsp_mtl *m) {
  [CATransaction begin];
  [CATransaction setDisableActions:YES];
  CGColorRef black = CGColorCreateGenericRGB(0, 0, 0, 1);
  m->layer.backgroundColor = black;
  CGColorRelease(black);
  [CATransaction commit];
}
