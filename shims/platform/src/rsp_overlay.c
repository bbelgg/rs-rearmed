// The overlay API for mods (shims/include/rs_overlay.h) on OpenGL.
//
// drawView (rsp_shell.m) calls rsp_overlay_run after the game's frame, before flushBuffer, with the GL
// context current and CGL-locked; the callbacks draw through the calls below. It uses only GL calls that
// the game itself imports (shims/opengl/game-gl-imports.txt): textures, one GLSL 1.20 program, one VBO in
// a VAO of its own, glDrawArrays and a few enables (plus the GL_APPLE_vertex_array_object calls where the
// context has that extension, see pick_vao). No immediate mode, no fixed function, no glPushAttrib, so a
// GL implementation that covers the game (libRSOpenGL's Metal core) covers this too.
//
// The game's state is read with glGet* the first time a callback uses the API in a frame and written
// back after the last callback; frames whose callbacks draw nothing touch no GL state.
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../include/rs_overlay.h"
#include "rsp_platform.h"

#define RSO_EXPORT __attribute__((visibility("default"), used))

// Core names of the VAO and FBO calls (the game's imports); Apple's gl.h declares only the
// APPLE/EXT variants.
extern void glGenVertexArrays(GLsizei n, GLuint *arrays);
extern void glBindVertexArray(GLuint array);
extern void glBindFramebuffer(GLenum target, GLuint fb);
#ifndef GL_VERTEX_ARRAY_BINDING
#define GL_VERTEX_ARRAY_BINDING 0x85B5
#endif
#ifndef GL_UNPACK_CLIENT_STORAGE_APPLE
#define GL_UNPACK_CLIENT_STORAGE_APPLE 0x85B2
#endif

// ---------------------------------------------------------------------------
// Registration

#define MAX_CB 16
typedef struct {
  rs_overlay_fn fn;
  void *user;
  uint64_t id;  // unique per registration
} cb_t;
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_cv = PTHREAD_COND_INITIALIZER;  // signalled when g_running changes
static cb_t g_cb[MAX_CB];
static int g_ncb;
static uint64_t g_next_id = 1;
static uint64_t g_running;   // id of the callback running now (0: none); under g_mu
static pthread_t g_run_thread;  // the thread inside rsp_overlay_run; valid while g_in_run
static volatile int g_in_run;
static volatile int g_any;  // g_ncb != 0, read without the lock once per frame

static int on_run_thread(void) { return g_in_run && pthread_equal(pthread_self(), g_run_thread); }

RSO_EXPORT int rs_overlay_api_version(void) { return RS_OVERLAY_API_VERSION; }

RSO_EXPORT int rs_overlay_register(rs_overlay_fn fn, void *user) {
  if (!fn) return -1;
  pthread_mutex_lock(&g_mu);
  int ok = g_ncb < MAX_CB;
  if (ok) {
    g_cb[g_ncb].fn = fn;
    g_cb[g_ncb].user = user;
    g_cb[g_ncb].id = g_next_id++;
    g_ncb++;
    g_any = 1;
  }
  pthread_mutex_unlock(&g_mu);
  return ok ? 0 : -1;
}

// Once this returns, the registration's callback is not running and will not run again (unless
// called from inside that very callback, which then finishes normally), so the mod may free `user`.
RSO_EXPORT int rs_overlay_unregister(rs_overlay_fn fn, void *user) {
  int r = -1;
  int self = on_run_thread();  // from a callback: never wait for ourselves
  pthread_mutex_lock(&g_mu);
  for (int i = 0; i < g_ncb; i++)
    if (g_cb[i].fn == fn && g_cb[i].user == user) {
      uint64_t id = g_cb[i].id;
      memmove(&g_cb[i], &g_cb[i + 1], (size_t)(g_ncb - i - 1) * sizeof g_cb[0]);
      g_ncb--;
      r = 0;
      while (!self && g_running == id) pthread_cond_wait(&g_cv, &g_mu);
      break;
    }
  g_any = g_ncb != 0;
  pthread_mutex_unlock(&g_mu);
  return r;
}

int rsp_overlay_active(void) { return g_any; }

// ---------------------------------------------------------------------------
// The game's state, saved on first use in a frame

typedef struct {
  GLint draw_fb, read_fb, program, vao, array_buf, active_tex, tex0, unpack_buf, pack_buf;
  GLint viewport[4], polygon_mode[2];
  GLint blend_src_rgb, blend_dst_rgb, blend_src_a, blend_dst_a, blend_eq;
  GLint unpack_align, unpack_row, unpack_skip_rows, unpack_skip_px, unpack_client;
  GLint pack_align, pack_row, pack_skip_rows, pack_skip_px;
  GLboolean blend, depth, scissor, cull, stencil, alpha_test;
  GLboolean color_mask[4];
} saved_t;

static saved_t g_saved;
static int g_in_frame;  // inside rsp_overlay_run
static int g_touched;   // the state is saved and ours is set
static rs_overlay_frame *g_frame;
static GLuint g_target;  // the framebuffer drawn to: 0, the window's (tests set an FBO)

void rsp_overlay_set_target(unsigned fb) { g_target = fb; }

// GL objects of ours, made on first use and kept
static GLuint g_prog, g_vao, g_vbo, g_white;
static GLint g_u_scale, g_u_color, g_u_tex;
static int g_gl_failed;

// Apple's legacy (2.1) context rejects the core VAO calls (GL_INVALID_OPERATION, which is also what the
// game's own glGenVertexArrays gets) and has GL_APPLE_vertex_array_object instead; a context without
// that extension (libRSOpenGL's Metal core) gets the core calls.
static void (*p_gen_vao)(GLsizei, GLuint *);
static void (*p_bind_vao)(GLuint);
static void pick_vao(void) {
  if (p_bind_vao) return;
  const char *ext = (const char *)glGetString(GL_EXTENSIONS);
  if (ext && strstr(ext, "GL_APPLE_vertex_array_object")) {
    p_gen_vao = glGenVertexArraysAPPLE;
    p_bind_vao = glBindVertexArrayAPPLE;
  } else {
    p_gen_vao = glGenVertexArrays;
    p_bind_vao = glBindVertexArray;
  }
}

struct rs_overlay_image {
  GLuint tex;
  int w, h;
};

static void save_state(saved_t *s) {
  glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING_EXT, &s->draw_fb);
  glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING_EXT, &s->read_fb);
  glGetIntegerv(GL_CURRENT_PROGRAM, &s->program);
  glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &s->vao);
  glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &s->array_buf);
  glGetIntegerv(GL_ACTIVE_TEXTURE, &s->active_tex);
  glActiveTexture(GL_TEXTURE0);
  glGetIntegerv(GL_TEXTURE_BINDING_2D, &s->tex0);
  glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &s->unpack_buf);
  glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &s->pack_buf);
  glGetIntegerv(GL_VIEWPORT, s->viewport);
  glGetIntegerv(GL_POLYGON_MODE, s->polygon_mode);
  glGetIntegerv(GL_BLEND_SRC_RGB, &s->blend_src_rgb);
  glGetIntegerv(GL_BLEND_DST_RGB, &s->blend_dst_rgb);
  glGetIntegerv(GL_BLEND_SRC_ALPHA, &s->blend_src_a);
  glGetIntegerv(GL_BLEND_DST_ALPHA, &s->blend_dst_a);
  glGetIntegerv(GL_BLEND_EQUATION_RGB, &s->blend_eq);  // the game sets one equation (glBlendEquation)
  glGetIntegerv(GL_UNPACK_ALIGNMENT, &s->unpack_align);
  glGetIntegerv(GL_UNPACK_ROW_LENGTH, &s->unpack_row);
  glGetIntegerv(GL_UNPACK_SKIP_ROWS, &s->unpack_skip_rows);
  glGetIntegerv(GL_UNPACK_SKIP_PIXELS, &s->unpack_skip_px);
  glGetIntegerv(GL_UNPACK_CLIENT_STORAGE_APPLE, &s->unpack_client);
  glGetIntegerv(GL_PACK_ALIGNMENT, &s->pack_align);
  glGetIntegerv(GL_PACK_ROW_LENGTH, &s->pack_row);
  glGetIntegerv(GL_PACK_SKIP_ROWS, &s->pack_skip_rows);
  glGetIntegerv(GL_PACK_SKIP_PIXELS, &s->pack_skip_px);
  // glIsEnabled is not among the game's imports; glGetBooleanv answers the same
  glGetBooleanv(GL_BLEND, &s->blend);
  glGetBooleanv(GL_DEPTH_TEST, &s->depth);
  glGetBooleanv(GL_SCISSOR_TEST, &s->scissor);
  glGetBooleanv(GL_CULL_FACE, &s->cull);
  glGetBooleanv(GL_STENCIL_TEST, &s->stencil);
  glGetBooleanv(GL_ALPHA_TEST, &s->alpha_test);  // fixed-function, but applies to GLSL programs too
  glGetBooleanv(GL_COLOR_WRITEMASK, s->color_mask);
}

static void enable(GLenum cap, GLboolean on) {
  if (on) glEnable(cap);
  else glDisable(cap);
}

static void restore_state(const saved_t *s) {
  if (s->draw_fb == s->read_fb) glBindFramebuffer(GL_FRAMEBUFFER_EXT, (GLuint)s->draw_fb);
  else {
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER_EXT, (GLuint)s->draw_fb);
    glBindFramebuffer(GL_READ_FRAMEBUFFER_EXT, (GLuint)s->read_fb);
  }
  glUseProgram((GLuint)s->program);
  p_bind_vao((GLuint)s->vao);
  glBindBuffer(GL_ARRAY_BUFFER, (GLuint)s->array_buf);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, (GLuint)s->tex0);
  glActiveTexture((GLenum)s->active_tex);
  glBindBuffer(GL_PIXEL_UNPACK_BUFFER, (GLuint)s->unpack_buf);
  glBindBuffer(GL_PIXEL_PACK_BUFFER, (GLuint)s->pack_buf);
  glViewport(s->viewport[0], s->viewport[1], s->viewport[2], s->viewport[3]);
  glPolygonMode(GL_FRONT, (GLenum)s->polygon_mode[0]);
  glPolygonMode(GL_BACK, (GLenum)s->polygon_mode[1]);
  glBlendFuncSeparate((GLenum)s->blend_src_rgb, (GLenum)s->blend_dst_rgb, (GLenum)s->blend_src_a,
                      (GLenum)s->blend_dst_a);
  glBlendEquation((GLenum)s->blend_eq);
  glPixelStorei(GL_UNPACK_ALIGNMENT, s->unpack_align);
  glPixelStorei(GL_UNPACK_ROW_LENGTH, s->unpack_row);
  glPixelStorei(GL_UNPACK_SKIP_ROWS, s->unpack_skip_rows);
  glPixelStorei(GL_UNPACK_SKIP_PIXELS, s->unpack_skip_px);
  glPixelStorei(GL_UNPACK_CLIENT_STORAGE_APPLE, s->unpack_client);
  glPixelStorei(GL_PACK_ALIGNMENT, s->pack_align);
  glPixelStorei(GL_PACK_ROW_LENGTH, s->pack_row);
  glPixelStorei(GL_PACK_SKIP_ROWS, s->pack_skip_rows);
  glPixelStorei(GL_PACK_SKIP_PIXELS, s->pack_skip_px);
  enable(GL_BLEND, s->blend);
  enable(GL_DEPTH_TEST, s->depth);
  enable(GL_SCISSOR_TEST, s->scissor);
  enable(GL_CULL_FACE, s->cull);
  enable(GL_STENCIL_TEST, s->stencil);
  enable(GL_ALPHA_TEST, s->alpha_test);
  glColorMask(s->color_mask[0], s->color_mask[1], s->color_mask[2], s->color_mask[3]);
}

// ---------------------------------------------------------------------------
// Our GL objects

static const char *k_vs =
    "#version 120\n"
    "attribute vec2 a_pos;\n"
    "attribute vec2 a_uv;\n"
    "uniform vec2 u_scale;\n"
    "varying vec2 v_uv;\n"
    "void main() {\n"
    "  v_uv = a_uv;\n"
    "  gl_Position = vec4(a_pos * u_scale + vec2(-1.0, 1.0), 0.0, 1.0);\n"
    "}\n";
static const char *k_fs =
    "#version 120\n"
    "uniform sampler2D u_tex;\n"
    "uniform vec4 u_color;\n"
    "varying vec2 v_uv;\n"
    "void main() { gl_FragColor = texture2D(u_tex, v_uv) * u_color; }\n";

static GLuint compile(GLenum type, const char *src) {
  GLuint sh = glCreateShader(type);
  glShaderSource(sh, 1, &src, NULL);
  glCompileShader(sh);
  GLint ok = 0;
  glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[512] = "";
    glGetShaderInfoLog(sh, sizeof log, NULL, log);
    fprintf(stderr, "rs_overlay: shader: %s\n", log);
    glDeleteShader(sh);
    return 0;
  }
  return sh;
}

static void tex_params(void) {
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

static int init_gl(void) {
  if (g_prog) return 1;
  if (g_gl_failed) return 0;
  GLuint vs = compile(GL_VERTEX_SHADER, k_vs), fs = compile(GL_FRAGMENT_SHADER, k_fs);
  GLuint p = vs && fs ? glCreateProgram() : 0;
  GLint ok = 0;
  if (p) {
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glBindAttribLocation(p, 0, "a_pos");
    glBindAttribLocation(p, 1, "a_uv");
    glLinkProgram(p);
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
      char log[512] = "";
      glGetProgramInfoLog(p, sizeof log, NULL, log);
      fprintf(stderr, "rs_overlay: link: %s\n", log);
      glDeleteProgram(p);
    }
  }
  if (vs) glDeleteShader(vs);
  if (fs) glDeleteShader(fs);
  if (!ok) {
    g_gl_failed = 1;
    return 0;
  }
  g_prog = p;
  g_u_scale = glGetUniformLocation(p, "u_scale");
  g_u_color = glGetUniformLocation(p, "u_color");
  g_u_tex = glGetUniformLocation(p, "u_tex");
  glUseProgram(p);
  glUniform1i(g_u_tex, 0);

  glGenBuffers(1, &g_vbo);
  p_gen_vao(1, &g_vao);
  p_bind_vao(g_vao);
  glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
  glBufferData(GL_ARRAY_BUFFER, 16 * sizeof(float), NULL, GL_STREAM_DRAW);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (const void *)0);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (const void *)(2 * sizeof(float)));
  glEnableVertexAttribArray(0);
  glEnableVertexAttribArray(1);

  static const uint8_t white[4] = {255, 255, 255, 255};
  glGenTextures(1, &g_white);
  glBindTexture(GL_TEXTURE_2D, g_white);
  tex_params();
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
  return 1;
}

// Saves the game's state and sets ours, once per frame. 0 if the API cannot be used now.
// Inside a callback: the frame being drawn, on the thread drawing it.
static int in_callback(const rs_overlay_frame *f) { return f && g_in_frame && f == g_frame && on_run_thread(); }

static int begin(rs_overlay_frame *f) {
  if (!in_callback(f) || g_gl_failed) return 0;
  if (g_touched) return 1;
  pick_vao();
  save_state(&g_saved);
  g_touched = 1;
  // pixel stores first: init_gl uploads a texture
  glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
  glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
  glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
  glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
  glPixelStorei(GL_UNPACK_CLIENT_STORAGE_APPLE, GL_FALSE);  // the mod's pixels are copied
  glPixelStorei(GL_PACK_ALIGNMENT, 4);
  glPixelStorei(GL_PACK_ROW_LENGTH, 0);
  glPixelStorei(GL_PACK_SKIP_ROWS, 0);
  glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
  glActiveTexture(GL_TEXTURE0);
  if (!init_gl()) return 0;  // what init_gl touched is restored at the end of the frame
  glBindFramebuffer(GL_FRAMEBUFFER_EXT, g_target);
  glUseProgram(g_prog);
  p_bind_vao(g_vao);
  glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
  glViewport(0, 0, f->width, f->height);
  glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
  glBlendEquation(GL_FUNC_ADD);
  glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);  // premultiplied "over"
  glEnable(GL_BLEND);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_SCISSOR_TEST);
  glDisable(GL_CULL_FACE);
  glDisable(GL_STENCIL_TEST);
  glDisable(GL_ALPHA_TEST);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  glUniform2f(g_u_scale, 2.0f / (float)f->width, -2.0f / (float)f->height);
  return 1;
}

// Still registered (it may have been unregistered by an earlier callback of this frame, or by another
// thread); if so it is marked running, so that rs_overlay_unregister waits for it.
static int claim(uint64_t id) {
  pthread_mutex_lock(&g_mu);
  int found = 0;
  for (int i = 0; i < g_ncb && !found; i++) found = g_cb[i].id == id;
  g_running = found ? id : 0;
  pthread_mutex_unlock(&g_mu);
  return found;
}
static void unclaim(void) {
  pthread_mutex_lock(&g_mu);
  g_running = 0;
  pthread_cond_broadcast(&g_cv);
  pthread_mutex_unlock(&g_mu);
}

void rsp_overlay_run(int width, int height) {
  if (!g_any || width <= 0 || height <= 0) return;
  cb_t cb[MAX_CB];
  pthread_mutex_lock(&g_mu);
  int n = g_ncb;
  memcpy(cb, g_cb, (size_t)n * sizeof cb[0]);
  pthread_mutex_unlock(&g_mu);
  static uint32_t index;
  rs_overlay_frame f = {width, height, index++};
  g_run_thread = pthread_self();
  g_in_run = 1;
  g_frame = &f;
  g_in_frame = 1;
  g_touched = 0;
  for (int i = 0; i < n; i++) {
    if (!claim(cb[i].id)) continue;
    cb[i].fn(&f, cb[i].user);
    unclaim();
  }
  if (g_touched) restore_state(&g_saved);
  g_in_frame = 0;
  g_frame = NULL;
  g_in_run = 0;
}

// ---------------------------------------------------------------------------
// Images and drawing

static void upload(int w, int h, const void *px, int stride, int create) {
  // whole rows of 4-byte pixels: the row length is in pixels, alignment 4 fits every stride
  if (stride % 4 == 0) {
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, stride / 4 == w ? 0 : stride / 4);
    if (create) glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    else glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px);
    return;
  }
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
  for (int y = 0; y < h; y++)
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, y, w, 1, GL_RGBA, GL_UNSIGNED_BYTE, (const uint8_t *)px + (size_t)y * stride);
}

RSO_EXPORT rs_overlay_image *rs_overlay_image_create(rs_overlay_frame *f, int w, int h) {
  if (w <= 0 || h <= 0 || w > 16384 || h > 16384 || !begin(f)) return NULL;
  rs_overlay_image *img = calloc(1, sizeof *img);
  void *zero = calloc((size_t)w * h, 4);
  if (!img || !zero) {
    free(img);
    free(zero);
    return NULL;
  }
  img->w = w;
  img->h = h;
  glGenTextures(1, &img->tex);
  glBindTexture(GL_TEXTURE_2D, img->tex);
  tex_params();
  upload(w, h, zero, w * 4, 1);
  free(zero);
  return img;
}

RSO_EXPORT int rs_overlay_image_update(rs_overlay_frame *f, rs_overlay_image *img, const void *rgba, int stride) {
  if (!img || !rgba || !begin(f)) return -1;
  if (!stride) stride = img->w * 4;
  if (stride < img->w * 4) return -1;
  glBindTexture(GL_TEXTURE_2D, img->tex);
  upload(img->w, img->h, rgba, stride, 0);
  return 0;
}

RSO_EXPORT void rs_overlay_image_destroy(rs_overlay_frame *f, rs_overlay_image *img) {
  if (!img || !begin(f)) return;
  glDeleteTextures(1, &img->tex);
  free(img);
}

RSO_EXPORT void rs_overlay_image_size(const rs_overlay_image *img, int *w, int *h) {
  if (w) *w = img ? img->w : 0;
  if (h) *h = img ? img->h : 0;
}

static void quad(GLuint tex, float x, float y, float w, float h, float r, float g, float b, float a) {
  const float v[16] = {x, y, 0, 0, x + w, y, 1, 0, x, y + h, 0, 1, x + w, y + h, 1, 1};
  glBindTexture(GL_TEXTURE_2D, tex);
  glBufferData(GL_ARRAY_BUFFER, sizeof v, v, GL_STREAM_DRAW);
  const float color[4] = {r, g, b, a};
  glUniform4fv(g_u_color, 1, color);  // glUniform4f is not among the game's imports
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

RSO_EXPORT int rs_overlay_draw_image(rs_overlay_frame *f, rs_overlay_image *img, float x, float y, float w, float h,
                                     float alpha) {
  if (!img || !begin(f)) return -1;
  if (alpha < 0) alpha = 0;
  if (alpha > 1) alpha = 1;
  quad(img->tex, x, y, w, h, alpha, alpha, alpha, alpha);
  return 0;
}

RSO_EXPORT int rs_overlay_fill_rect(rs_overlay_frame *f, float x, float y, float w, float h, uint32_t rgba) {
  if (!begin(f)) return -1;
  float a = (rgba & 0xff) / 255.0f;
  quad(g_white, x, y, w, h, (rgba >> 24 & 0xff) / 255.0f * a, (rgba >> 16 & 0xff) / 255.0f * a,
       (rgba >> 8 & 0xff) / 255.0f * a, a);
  return 0;
}

RSO_EXPORT int rs_overlay_read_pixels(rs_overlay_frame *f, int x, int y, int w, int h, void *out) {
  // f is only dereferenced once it is known to be the live frame; the bounds are checked without
  // overflowing (x + w can exceed INT_MAX)
  if (!out || !in_callback(f) || w <= 0 || h <= 0 || x < 0 || y < 0 || x > f->width - w || y > f->height - h ||
      !begin(f))
    return -1;
  size_t row = (size_t)w * 4;
  uint8_t *p = out, *tmp = malloc(row);
  if (!tmp) return -1;
  glReadPixels(x, f->height - y - h, w, h, GL_RGBA, GL_UNSIGNED_BYTE, out);
  for (int i = 0; i < h / 2; i++) {  // GL's rows are bottom first
    memcpy(tmp, p + i * row, row);
    memcpy(p + i * row, p + (h - 1 - i) * row, row);
    memcpy(p + (h - 1 - i) * row, tmp, row);
  }
  free(tmp);
  for (size_t i = 3; i < row * h; i += 4) p[i] = 255;
  return 0;
}
