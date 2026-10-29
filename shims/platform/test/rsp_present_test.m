// The shell's Metal presentation (rsp_present_metal.m) on a CAMetalLayer that is in no window: a
// layer still hands out drawables, so the layer settings, the rs_gfx layer context, clears,
// presents (frames-in-flight limit, no nextDrawable timeouts), resize, the swap interval and the
// lock can be checked without opening anything. Run with RS_GL_BACKEND=metal; with
// RS_GL_BACKEND=gl it checks that the shell stays on GL. MRC, like the shell.
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <mach/mach_time.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../src/rsp_present.h"

static int g_pass, g_fail;
#define CHECK(cond, ...)                          \
  do {                                            \
    if (cond) g_pass++;                           \
    else {                                        \
      g_fail++;                                   \
      printf("FAIL %s:%d: ", __FILE__, __LINE__); \
      printf(__VA_ARGS__);                        \
      printf("\n");                               \
    }                                             \
  } while (0)

static double now_s(void) {
  static mach_timebase_info_data_t tb;
  if (!tb.denom) mach_timebase_info(&tb);
  return (double)mach_absolute_time() * tb.numer / tb.denom / 1e9;
}

static rsp_mtl *g_m;
static atomic_int g_locked;
static void *locker(void *arg) {
  (void)arg;
  rsp_mtl_lock(g_m);
  atomic_store(&g_locked, 1);
  rsp_mtl_unlock(g_m);
  return NULL;
}

// Clear to (r, g, b), read the default framebuffer's centre back, present. Returns the present time.
static double frame(float r, float g, float b, int w, int h, unsigned char px[4]) {
  glClearColor(r, g, b, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
  if (px) glReadPixels(w / 2, h / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
  double t = now_s();
  rsp_mtl_present(g_m);
  return now_s() - t;
}

static int test_gl(void) {
  CHECK(!rsp_mtl_selected(), "RS_GL_BACKEND=gl: the shell must stay on GL");
  CAMetalLayer *layer = [CAMetalLayer layer];
  CHECK(rsp_mtl_create(layer, 64, 48, 1) == NULL, "no Metal context on GL");
  return 0;
}

static int test_metal(void) {
  CHECK(rsp_mtl_selected(), "RS_GL_BACKEND=metal: Metal presentation selected");
  if (!rsp_mtl_selected()) return 1;
  CAMetalLayer *layer = [CAMetalLayer layer];
  layer.device = MTLCreateSystemDefaultDevice();
  [layer.device release];
  g_m = rsp_mtl_create(layer, 64, 48, 1);
  CHECK(g_m != NULL, "rs_gfx_create_layer");
  if (!g_m) return 1;
  CHECK(rsp_mtl_layer(g_m) == layer, "layer");
  CHECK(layer.pixelFormat == MTLPixelFormatBGRA8Unorm, "pixelFormat %d", (int)layer.pixelFormat);
  CHECK(layer.framebufferOnly, "framebufferOnly");
  CHECK(layer.drawableSize.width == 64 && layer.drawableSize.height == 48, "drawableSize %gx%g",
        layer.drawableSize.width, layer.drawableSize.height);
  CHECK(layer.displaySyncEnabled, "displaySyncEnabled with swap interval 1");
  CHECK(layer.maximumDrawableCount == (NSUInteger)rsp_mtl_drawables(1), "maximumDrawableCount %d",
        (int)layer.maximumDrawableCount);
  CHECK(rsp_mtl_drawables(1) == 3 && rsp_mtl_drawables(0) == 2, "drawable counts 3 (sync) / 2");

  rsp_mtl_make_current(g_m);
  CHECK(rsp_mtl_cgl(g_m) != NULL, "rs_gfx_cgl token");
  CHECK((void *)CGLGetCurrentContext() == rsp_mtl_cgl(g_m), "CGLGetCurrentContext is the token");

  // The lock is recursive and excludes other threads.
  rsp_mtl_lock(g_m);
  rsp_mtl_lock(g_m);
  pthread_t t;
  pthread_create(&t, NULL, locker, NULL);
  usleep(50 * 1000);
  CHECK(!atomic_load(&g_locked), "another thread waits for the lock");
  rsp_mtl_unlock(g_m);
  usleep(20 * 1000);
  CHECK(!atomic_load(&g_locked), "still held once");
  rsp_mtl_unlock(g_m);
  pthread_join(t, NULL);
  CHECK(atomic_load(&g_locked), "lock released");

  // Frames, as drawView makes them: lock, current, clear, present, unlock.
  int w, h;
  rsp_mtl_size(g_m, &w, &h);
  CHECK(w == 64 && h == 48, "size %dx%d", w, h);
  double worst = 0, t0 = now_s();
  int bad_px = 0;
  for (int i = 0; i < 30; i++) {
    @autoreleasepool {
      rsp_mtl_lock(g_m);
      unsigned char px[4] = {0};
      float c = (i & 1) ? 1.0f : 0.0f;
      double d = frame(c, 0.5f, 1.0f - c, w, h, px);
      if (d > worst) worst = d;
      int er = (i & 1) ? 255 : 0;
      if (abs(px[0] - er) > 1 || abs(px[1] - 128) > 1 || abs(px[2] - (255 - er)) > 1 || px[3] != 255) bad_px++;
      rsp_mtl_unlock(g_m);
    }
  }
  double total = now_s() - t0;
  CHECK(glGetError() == GL_NO_ERROR, "no GL errors");
  CHECK(bad_px == 0, "cleared colour read back in %d of 30 frames", 30 - bad_px);
  // nextDrawable times out after 1 s when no drawable comes back; frames in flight are capped at 2.
  CHECK(worst < 0.5, "slowest present %.3f s", worst);
  CHECK(total < 5.0, "30 frames in %.3f s", total);

  // Swap interval 0 (RS_FPS, no_vsync): applied at the next present.
  rsp_mtl_set_swap_interval(g_m, 0);
  CHECK(layer.displaySyncEnabled, "not before the next present");
  frame(0, 0, 0, w, h, NULL);
  CHECK(!layer.displaySyncEnabled, "displaySyncEnabled off");
  CHECK(layer.maximumDrawableCount == (NSUInteger)rsp_mtl_drawables(0), "maximumDrawableCount %d",
        (int)layer.maximumDrawableCount);
  for (int i = 0; i < 10; i++) {
    @autoreleasepool {
      frame(0, 0, 0, w, h, NULL);
    }
  }
  rsp_mtl_set_swap_interval(g_m, 1);
  frame(0, 0, 0, w, h, NULL);
  CHECK(layer.displaySyncEnabled && layer.maximumDrawableCount == (NSUInteger)rsp_mtl_drawables(1),
        "back to sync");

  // Resize (reshape): drawable and default framebuffer follow.
  rsp_mtl_resize(g_m, 128, 96);
  rsp_mtl_size(g_m, &w, &h);
  CHECK(w == 128 && h == 96, "size after resize %dx%d", w, h);
  CHECK(layer.drawableSize.width == 128 && layer.drawableSize.height == 96, "drawableSize %gx%g",
        layer.drawableSize.width, layer.drawableSize.height);
  unsigned char px[4] = {0};
  rsp_mtl_lock(g_m);
  frame(1, 1, 1, w, h, px);
  rsp_mtl_unlock(g_m);
  CHECK(px[0] == 255 && px[1] == 255 && px[2] == 255, "clear after resize %d %d %d", px[0], px[1], px[2]);
  CHECK(glGetError() == GL_NO_ERROR, "no GL errors after resize");
  return 0;
}

int main(void) {
  @autoreleasepool {
    const char *b = getenv("RS_GL_BACKEND");
    if (b && !strcmp(b, "metal")) test_metal();
    else test_gl();
  }
  printf("%d passed, %d failed\n", g_pass, g_fail);
  return g_fail != 0;
}
