// The overlay API (rsp_overlay.c) on an offscreen legacy GL context: pixels of draw_image, fill_rect and
// read_pixels in an FBO standing in for the window, the game's GL state restored after the callbacks, no
// GL errors, and the calls refusing to work outside a callback. Opens no window.
#define GL_SILENCE_DEPRECATION
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../include/rs_overlay.h"
#include "rsp_platform.h"

#ifndef GL_VERTEX_ARRAY_BINDING
#define GL_VERTEX_ARRAY_BINDING 0x85B5
#endif

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

enum { W = 64, H = 48 };
static GLuint g_fb_window, g_fb_game, g_game_prog, g_game_vao, g_game_buf, g_game_tex;

static int geti(GLenum e) {
  GLint v = 0;
  glGetIntegerv(e, &v);
  return v;
}
static int getb(GLenum e) {
  GLboolean v = 0;
  glGetBooleanv(e, &v);
  return v;
}

static GLuint fbo(void) {
  GLuint fb, rb;
  glGenFramebuffersEXT(1, &fb);
  glGenRenderbuffersEXT(1, &rb);
  glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fb);
  glBindRenderbufferEXT(GL_RENDERBUFFER_EXT, rb);
  glRenderbufferStorageEXT(GL_RENDERBUFFER_EXT, GL_RGBA8, W, H);
  glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_RENDERBUFFER_EXT, rb);
  CHECK(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT) == GL_FRAMEBUFFER_COMPLETE_EXT, "FBO complete");
  return fb;
}

static GLuint game_program(void) {
  const char *vs = "void main() { gl_Position = gl_Vertex; }", *fs = "void main() { gl_FragColor = vec4(1.0); }";
  GLuint v = glCreateShader(GL_VERTEX_SHADER), f = glCreateShader(GL_FRAGMENT_SHADER), p = glCreateProgram();
  glShaderSource(v, 1, &vs, NULL);
  glShaderSource(f, 1, &fs, NULL);
  glCompileShader(v);
  glCompileShader(f);
  glAttachShader(p, v);
  glAttachShader(p, f);
  glLinkProgram(p);
  return p;
}

// Odd values everywhere the overlay touches state: anything left over breaks the checks below.
static void set_game_state(void) {
  glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, g_fb_game);
  glUseProgram(g_game_prog);
  glBindVertexArrayAPPLE(g_game_vao);  // the legacy context has only these
  glBindBuffer(GL_ARRAY_BUFFER, g_game_buf);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, g_game_tex);
  glActiveTexture(GL_TEXTURE3);
  glViewport(1, 2, 3, 4);
  glPolygonMode(GL_FRONT, GL_LINE);
  glPolygonMode(GL_BACK, GL_POINT);
  glEnable(GL_BLEND);
  glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ZERO, GL_DST_COLOR);
  glBlendEquation(GL_FUNC_REVERSE_SUBTRACT);
  glEnable(GL_DEPTH_TEST);
  glEnable(GL_SCISSOR_TEST);
  glScissor(0, 0, 1, 1);
  glEnable(GL_CULL_FACE);
  glCullFace(GL_FRONT_AND_BACK);
  glEnable(GL_STENCIL_TEST);
  glEnable(GL_ALPHA_TEST);
  glAlphaFunc(GL_NEVER, 0);
  glColorMask(GL_FALSE, GL_TRUE, GL_FALSE, GL_TRUE);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glPixelStorei(GL_UNPACK_ROW_LENGTH, 7);
  glPixelStorei(GL_UNPACK_SKIP_ROWS, 2);
  glPixelStorei(GL_UNPACK_SKIP_PIXELS, 3);
  glPixelStorei(GL_PACK_ALIGNMENT, 2);
  glPixelStorei(GL_PACK_ROW_LENGTH, 9);
  glPixelStorei(GL_PACK_SKIP_ROWS, 1);
  glPixelStorei(GL_PACK_SKIP_PIXELS, 1);
}

static void check_game_state(const char *when) {
  CHECK(geti(GL_DRAW_FRAMEBUFFER_BINDING_EXT) == (int)g_fb_game, "%s: draw framebuffer", when);
  CHECK(geti(GL_READ_FRAMEBUFFER_BINDING_EXT) == (int)g_fb_game, "%s: read framebuffer", when);
  CHECK(geti(GL_CURRENT_PROGRAM) == (int)g_game_prog, "%s: program", when);
  CHECK(geti(GL_VERTEX_ARRAY_BINDING) == (int)g_game_vao, "%s: VAO", when);
  CHECK(geti(GL_ARRAY_BUFFER_BINDING) == (int)g_game_buf, "%s: array buffer", when);
  CHECK(geti(GL_ACTIVE_TEXTURE) == GL_TEXTURE3, "%s: active texture", when);
  glActiveTexture(GL_TEXTURE0);
  CHECK(geti(GL_TEXTURE_BINDING_2D) == (int)g_game_tex, "%s: unit 0 texture", when);
  glActiveTexture(GL_TEXTURE3);
  GLint vp[4], pm[2];
  glGetIntegerv(GL_VIEWPORT, vp);
  CHECK(vp[0] == 1 && vp[1] == 2 && vp[2] == 3 && vp[3] == 4, "%s: viewport", when);
  glGetIntegerv(GL_POLYGON_MODE, pm);
  CHECK(pm[0] == GL_LINE && pm[1] == GL_POINT, "%s: polygon mode %x %x", when, pm[0], pm[1]);
  CHECK(getb(GL_BLEND), "%s: blend", when);
  CHECK(geti(GL_BLEND_SRC_RGB) == GL_SRC_ALPHA && geti(GL_BLEND_DST_RGB) == GL_ONE &&
            geti(GL_BLEND_SRC_ALPHA) == GL_ZERO && geti(GL_BLEND_DST_ALPHA) == GL_DST_COLOR,
        "%s: blend func", when);
  CHECK(geti(GL_BLEND_EQUATION_RGB) == GL_FUNC_REVERSE_SUBTRACT, "%s: blend equation", when);
  CHECK(getb(GL_DEPTH_TEST) && getb(GL_SCISSOR_TEST) && getb(GL_CULL_FACE) && getb(GL_STENCIL_TEST) &&
            getb(GL_ALPHA_TEST),
        "%s: enables", when);
  GLboolean cm[4];
  glGetBooleanv(GL_COLOR_WRITEMASK, cm);
  CHECK(!cm[0] && cm[1] && !cm[2] && cm[3], "%s: color mask", when);
  CHECK(geti(GL_UNPACK_ALIGNMENT) == 1 && geti(GL_UNPACK_ROW_LENGTH) == 7 && geti(GL_UNPACK_SKIP_ROWS) == 2 &&
            geti(GL_UNPACK_SKIP_PIXELS) == 3,
        "%s: unpack state", when);
  CHECK(geti(GL_PACK_ALIGNMENT) == 2 && geti(GL_PACK_ROW_LENGTH) == 9 && geti(GL_PACK_SKIP_ROWS) == 1 &&
            geti(GL_PACK_SKIP_PIXELS) == 1,
        "%s: pack state", when);
  GLenum e = glGetError();
  CHECK(e == GL_NO_ERROR, "%s: GL error 0x%x", when, e);
}

// The window's pixel at (x, y), top-left origin.
static const uint8_t *window_px(const uint8_t *buf, int x, int y) { return buf + ((size_t)(H - 1 - y) * W + x) * 4; }

static int near(const uint8_t *p, int r, int g, int b) {
  return abs(p[0] - r) <= 2 && abs(p[1] - g) <= 2 && abs(p[2] - b) <= 2;
}

// --- callbacks ---------------------------------------------------------------
enum { IW = 8, IH = 6 };
static int g_calls, g_run2_calls;
static int g_read_ok, g_read_pixels_ok;
static rs_overlay_image *g_img;

static uint8_t img_px(int x, int y, int c) { return c == 0 ? (uint8_t)(x * 30 + 10) : c == 1 ? (uint8_t)(y * 40 + 5) : c == 2 ? 77 : 255; }

static void draw_cb(rs_overlay_frame *f, void *user) {
  g_calls++;
  CHECK(user == (void *)&g_calls, "user pointer");
  CHECK(f->width == W && f->height == H, "frame size %dx%d", f->width, f->height);
  g_img = rs_overlay_image_create(f, IW, IH);
  CHECK(g_img != NULL, "image_create");
  int iw, ih;
  rs_overlay_image_size(g_img, &iw, &ih);
  CHECK(iw == IW && ih == IH, "image size");
  // a stride that is not a multiple of 4 (row by row), then a padded one (row length)
  for (int pass = 0; pass < 2; pass++) {
    int stride = pass ? IW * 4 + 12 : IW * 4 + 3;
    uint8_t *px = calloc((size_t)stride, IH);
    for (int y = 0; y < IH; y++)
      for (int x = 0; x < IW; x++)
        for (int c = 0; c < 4; c++) px[y * stride + x * 4 + c] = pass ? img_px(x, y, c) : 1;
    CHECK(rs_overlay_image_update(f, g_img, px, stride) == 0, "image_update stride %d", stride);
    free(px);
  }
  CHECK(rs_overlay_draw_image(f, g_img, 10, 5, IW, IH, 1.0f) == 0, "draw_image");
  CHECK(rs_overlay_draw_image(f, g_img, 40, 30, IW, IH, 0.5f) == 0, "draw_image alpha");
  CHECK(rs_overlay_fill_rect(f, 30, 20, 4, 3, 0x00ff0080u) == 0, "fill_rect");
  CHECK(rs_overlay_fill_rect(f, 0, 0, 2, 2, 0xff0000ffu) == 0, "fill_rect opaque");
  uint8_t rd[IW * IH * 4];
  g_read_ok = rs_overlay_read_pixels(f, 10, 5, IW, IH, rd) == 0;
  g_read_pixels_ok = 1;
  for (int y = 0; y < IH; y++)
    for (int x = 0; x < IW; x++) {
      const uint8_t *p = rd + (y * IW + x) * 4;
      if (!near(p, img_px(x, y, 0), img_px(x, y, 1), img_px(x, y, 2)) || p[3] != 255) g_read_pixels_ok = 0;
    }
  CHECK(rs_overlay_read_pixels(f, W - 2, 0, 4, 1, rd) == -1, "read_pixels outside the frame");
}

// Does nothing, except destroying the image on its second call.
static void empty_cb(rs_overlay_frame *f, void *user) {
  if (++g_run2_calls == 2 && g_img) {
    rs_overlay_image_destroy(f, g_img);
    g_img = NULL;
  }
}

int main(void) {
  CGLPixelFormatAttribute attrs[] = {kCGLPFAAccelerated, kCGLPFAAllowOfflineRenderers, 0};
  CGLPixelFormatObj pf;
  GLint npf = 0;
  if (CGLChoosePixelFormat(attrs, &pf, &npf) != kCGLNoError || !pf) {
    printf("no pixel format\n");
    return 1;
  }
  CGLContextObj ctx;
  CGLCreateContext(pf, NULL, &ctx);
  CGLDestroyPixelFormat(pf);
  CGLSetCurrentContext(ctx);
  printf("GL %s, %s\n", glGetString(GL_VERSION), glGetString(GL_RENDERER));

  g_fb_window = fbo();
  glClearColor(0, 0, 1, 1);
  glClear(GL_COLOR_BUFFER_BIT);
  g_fb_game = fbo();
  g_game_prog = game_program();
  glGenVertexArraysAPPLE(1, &g_game_vao);
  glGenBuffers(1, &g_game_buf);
  glGenTextures(1, &g_game_tex);
  glBindTexture(GL_TEXTURE_2D, g_game_tex);
  CHECK(glGetError() == GL_NO_ERROR, "setup");
  rsp_overlay_set_target(g_fb_window);

  CHECK(rs_overlay_api_version() == RS_OVERLAY_API_VERSION, "version");
  rs_overlay_frame fake = {W, H, 0};
  CHECK(rs_overlay_image_create(&fake, 4, 4) == NULL, "image_create outside a callback");
  CHECK(rs_overlay_fill_rect(&fake, 0, 0, 1, 1, 0xffffffffu) == -1, "fill_rect outside a callback");
  CHECK(!rsp_overlay_active(), "inactive with no callbacks");
  CHECK(rs_overlay_register(NULL, NULL) == -1, "register NULL");

  set_game_state();
  rsp_overlay_run(W, H);  // nothing registered
  check_game_state("no callbacks");

  CHECK(rs_overlay_register(draw_cb, &g_calls) == 0, "register");
  CHECK(rs_overlay_register(empty_cb, NULL) == 0, "register 2");
  CHECK(rsp_overlay_active(), "active");
  set_game_state();
  rsp_overlay_run(W, H);
  check_game_state("after drawing");
  CHECK(g_calls == 1 && g_run2_calls == 1, "callbacks ran %d %d", g_calls, g_run2_calls);
  CHECK(g_read_ok && g_read_pixels_ok, "read_pixels returned the image, top row first");

  // the window's pixels
  uint8_t *buf = malloc((size_t)W * H * 4);
  glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, g_fb_window);
  glPixelStorei(GL_PACK_ALIGNMENT, 4);
  glPixelStorei(GL_PACK_ROW_LENGTH, 0);
  glPixelStorei(GL_PACK_SKIP_ROWS, 0);
  glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
  glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, buf);
  int exact = 1, half = 1;
  for (int y = 0; y < IH; y++)
    for (int x = 0; x < IW; x++) {
      if (!near(window_px(buf, 10 + x, 5 + y), img_px(x, y, 0), img_px(x, y, 1), img_px(x, y, 2))) exact = 0;
      // alpha 0.5 over blue: src * 0.5 + dst * 0.5
      if (!near(window_px(buf, 40 + x, 30 + y), img_px(x, y, 0) / 2, img_px(x, y, 1) / 2, img_px(x, y, 2) / 2 + 128))
        half = 0;
    }
  CHECK(exact, "draw_image at its own size is exact");
  CHECK(half, "draw_image with alpha 0.5");
  const uint8_t *p = window_px(buf, 31, 21);
  CHECK(near(p, 0, 128, 127), "fill_rect half green over blue: %d %d %d", p[0], p[1], p[2]);
  p = window_px(buf, 0, 0);
  CHECK(near(p, 255, 0, 0), "fill_rect at the top left: %d %d %d", p[0], p[1], p[2]);
  CHECK(near(window_px(buf, 9, 5), 0, 0, 255) && near(window_px(buf, 18, 5), 0, 0, 255) &&
            near(window_px(buf, 10, 4), 0, 0, 255) && near(window_px(buf, 10, 11), 0, 0, 255) &&
            near(window_px(buf, 2, 0), 0, 0, 255) && near(window_px(buf, 34, 21), 0, 0, 255),
        "nothing drawn outside the rects");
  free(buf);
  CHECK(glGetError() == GL_NO_ERROR, "readback");

  // Unregistering stops the calls; destroying an image restores the state too.
  CHECK(rs_overlay_unregister(draw_cb, &g_calls) == 0, "unregister");
  CHECK(rs_overlay_unregister(draw_cb, &g_calls) == -1, "unregister twice");
  set_game_state();
  rsp_overlay_run(W, H);
  check_game_state("image_destroy");
  CHECK(g_calls == 1 && g_run2_calls == 2, "callbacks after unregister %d %d", g_calls, g_run2_calls);
  CHECK(rs_overlay_unregister(empty_cb, NULL) == 0 && !rsp_overlay_active(), "unregister all");

  CGLSetCurrentContext(NULL);
  CGLDestroyContext(ctx);
  printf("%d passed, %d failed\n", g_pass, g_fail);
  return g_fail != 0;
}
