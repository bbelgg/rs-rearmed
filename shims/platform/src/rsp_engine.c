// The C layer between the ObjC shell and the engine (originally a C++ file
// next to main, 0x100179dc0-0x10017a3b0), the input queue that lived on
// MainService_MacOS, and the stub engine. See rsp_platform.h.
#include <math.h>
#include "rsp_platform.h"
#include "rsp_renderer.h"

#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// ---------------------------------------------------------------------------
// Logging

static FILE *g_log;
static int g_log_init;
static pthread_mutex_t g_log_mu = PTHREAD_MUTEX_INITIALIZER;

void rsp_log(const char *fmt, ...) {
  pthread_mutex_lock(&g_log_mu);
  if (!g_log_init) {
    g_log_init = 1;
    const char *p = getenv("RSP_LOG");
    if (!p || !*p) g_log = stderr;
    else if (strcmp(p, "0") == 0) g_log = NULL;
    else g_log = fopen(p, "a");
  }
  if (g_log) {
    va_list ap;
    va_start(ap, fmt);
    fputs("[rsp] ", g_log);
    vfprintf(g_log, fmt, ap);
    fputc('\n', g_log);
    fflush(g_log);
    va_end(ap);
  }
  pthread_mutex_unlock(&g_log_mu);
}

// ---------------------------------------------------------------------------
// Stub engine: logs and emulates the bits of engine state the shell reads back.

static rsp_stub_state g_stub = {1280, 720, 0, 1, 0, 0, 0, 0, 0, NULL, NULL, NULL};
rsp_stub_state *rsp_stub(void) { return &g_stub; }

static int stub_bootstrap(void *ctx, const rsp_boot *b) {
  (void)ctx;
  rsp_log("hook bootstrap app_dir=%s cwd=%s argc=%d argv0=%s", b->app_dir, b->cwd, b->argc,
          b->argc > 0 ? b->argv[0] : "");
  g_stub.booted = 1;
  return 0;
}
static int stub_frame(void *ctx) {
  (void)ctx;
  g_stub.frames++;
  if (g_stub.frame_cb && g_stub.frame_cb(g_stub.user)) g_stub.shut_down = 1;
  // The ServiceManager shuts down on the frame after a quit request
  // (ServiceManager slot 21 from GameFrame, then state 7 on the next one).
  if (g_stub.quit_requested >= 2) g_stub.shut_down = 1;
  if (g_stub.quit_requested == 1) g_stub.quit_requested = 2;
  return g_stub.shut_down;
}
static void stub_input(void *ctx, int code, int a, int b) {
  (void)ctx;
  rsp_log("hook input gfx=%d a=%d b=0x%x", code, a, (unsigned)b);
  if (g_stub.input_cb) g_stub.input_cb(g_stub.user, code, a, b);
}
static void stub_app_active(void *ctx, int active) {
  (void)ctx;
  rsp_log("hook app_active %d", active);
}
static void stub_focus_lost(void *ctx) {
  (void)ctx;
  rsp_log("hook focus_lost (UI event 0x98)");
}
// GRDisplaySettings: SetResolution always applies; SetFullscreen applies when
// the mode changes. There is no clamping in either (the 800x600 floor is
// GameBootstrap's).
static void stub_set_resolution(void *ctx, int w, int h) {
  (void)ctx;
  rsp_log("hook set_resolution %dx%d", w, h);
  g_stub.width = w;
  g_stub.height = h;
}
static void stub_set_fullscreen(void *ctx, int mode) {
  (void)ctx;
  if (mode != g_stub.fullscreen) rsp_log("hook set_fullscreen %d", mode);
  g_stub.fullscreen = mode;
}
static int stub_get_fullscreen(void *ctx) {
  (void)ctx;
  return g_stub.fullscreen;
}
static void stub_set_visual_quality(void *ctx, int q) {
  (void)ctx;
  rsp_log("hook set_visual_quality %d", q);
  g_stub.visual_quality = q;
}
static void stub_get_resolution(void *ctx, int *w, int *h) {
  (void)ctx;
  *w = g_stub.width;
  *h = g_stub.height;
}
static int stub_no_vsync(void *ctx) {
  (void)ctx;
  return g_stub.no_vsync;
}
static int stub_request_quit(void *ctx) {
  (void)ctx;
  rsp_log("hook request_quit");
  if (!g_stub.booted) return 0;
  if (!g_stub.quit_requested) g_stub.quit_requested = 1;
  return 1;
}
static void stub_shutdown(void *ctx) {
  (void)ctx;
  rsp_log("hook shutdown after %ld frames", g_stub.frames);
  g_stub.booted = 0;
}
static void stub_gl_context(void *ctx, void *glctx) {
  (void)ctx;
  rsp_log("hook gl_context %p", glctx);
}

static const rsp_engine_hooks g_stub_hooks = {
    NULL, stub_bootstrap, stub_frame, stub_input, stub_app_active, stub_focus_lost,
    stub_set_resolution, stub_set_fullscreen, stub_get_fullscreen, stub_set_visual_quality,
    stub_get_resolution, stub_no_vsync, stub_request_quit, stub_shutdown, stub_gl_context};

const rsp_engine_hooks *rsp_stub_hooks(void) { return &g_stub_hooks; }

static rsp_engine_hooks g_hooks;
static int g_hooks_set;
void rsp_set_hooks(const rsp_engine_hooks *h) {
  g_hooks = *h;
  g_hooks_set = 1;
}
const rsp_engine_hooks *rsp_hooks(void) {
  if (!g_hooks_set) rsp_set_hooks(&g_stub_hooks);
  return &g_hooks;
}
#define H (rsp_hooks())

// ---------------------------------------------------------------------------
// The input queue (MainService_MacOS +0xa8.., mutex +0xc0, 0x1001681b0/0x100168360)

typedef struct { int type, a, b; } rsp_ev;
static pthread_mutex_t g_q_mu;
static pthread_once_t g_q_once = PTHREAD_ONCE_INIT;
static rsp_ev *g_q;
static size_t g_q_len, g_q_cap;
static int g_game_present;
static int g_focused;  // 0x10194b391

static void q_init(void) {
  pthread_mutexattr_t a;
  pthread_mutexattr_init(&a);
  pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE);  // like the original's
  pthread_mutex_init(&g_q_mu, &a);
  pthread_mutexattr_destroy(&a);
}

void rsp_input_queue(int type, int a, int b) {
  pthread_once(&g_q_once, q_init);
  pthread_mutex_lock(&g_q_mu);
  if (g_q_len == g_q_cap) {
    // std::vector growth: max(1, 2n)
    size_t n = g_q_cap ? g_q_cap * 2 : 1;
    rsp_ev *p = realloc(g_q, n * sizeof *p);
    if (!p) {
      pthread_mutex_unlock(&g_q_mu);
      return;
    }
    g_q = p;
    g_q_cap = n;
  }
  g_q[g_q_len].type = type;
  g_q[g_q_len].a = a;
  g_q[g_q_len].b = b;
  g_q_len++;
  pthread_mutex_unlock(&g_q_mu);
}

int rsp_gfx_event_code(int type) { return (unsigned)type < 6 ? type + 1 : -1; }

int rsp_input_flush(void) {
  pthread_once(&g_q_once, q_init);
  pthread_mutex_lock(&g_q_mu);
  // The original holds the lock while it delivers (the GFx calls run inside).
  size_t n = g_q_len;
  for (size_t i = 0; i < g_q_len; i++)
    if (H->input) H->input(H->ctx, rsp_gfx_event_code(g_q[i].type), g_q[i].a, g_q[i].b);
  g_q_len = 0;
  pthread_mutex_unlock(&g_q_mu);
  return (int)n;
}

int rsp_input_pending(void) {
  pthread_once(&g_q_once, q_init);
  pthread_mutex_lock(&g_q_mu);
  int n = (int)g_q_len;
  pthread_mutex_unlock(&g_q_mu);
  return n;
}

void rsp_input_reset(void) {
  pthread_once(&g_q_once, q_init);
  pthread_mutex_lock(&g_q_mu);
  g_q_len = 0;
  pthread_mutex_unlock(&g_q_mu);
}

void rsp_game_set_present(int present) { g_game_present = present; }

// 0x10017a080: types 6/7 toggle audio directly; everything else is queued on
// the game object, and only while the app has focus.
void rsp_post_input(int type, int a, int b) {
  if (type == RSP_EV_APP_INACTIVE) {
    if (H->app_active) H->app_active(H->ctx, 0);
    return;
  }
  if (type == RSP_EV_APP_ACTIVE) {
    if (H->app_active) H->app_active(H->ctx, 1);
    return;
  }
  if (g_game_present && g_focused) rsp_input_queue(type, a, b);
}

// 0x10017a180: the focus byte, audio, and a pause event for the UI on loss.
void rsp_set_focus(int focused) {
  g_focused = focused ? 1 : 0;
  if (focused) {
    if (H->app_active) H->app_active(H->ctx, 1);
    return;
  }
  if (H->app_active) H->app_active(H->ctx, 0);
  if (H->focus_lost) H->focus_lost(H->ctx);
}
int rsp_focused(void) { return g_focused; }

// ---------------------------------------------------------------------------
// Display mode (GRDisplaySettings through the hooks)

static double g_render_scale = 1.0;
void rsp_set_render_scale(double s) { g_render_scale = s > 0 ? s : 1.0; }
double rsp_render_scale(void) { return g_render_scale; }
static int to_engine(int v) { return (int)lround(v * g_render_scale); }
static int from_engine(int v) { return (int)lround(v / g_render_scale); }
int rsp_mouse_engine_coord(double v) { return rsp_mouse_coord(v * g_render_scale); }

void rsp_set_display_mode(int fs, int w, int h) {  // 0x10017a1f0
  if (H->set_resolution) H->set_resolution(H->ctx, to_engine(w), to_engine(h));
  if (H->set_fullscreen) H->set_fullscreen(H->ctx, fs & 0xff);
}
int rsp_is_fullscreen(void) {  // 0x10017a210
  return H->get_fullscreen ? H->get_fullscreen(H->ctx) != 0 : 0;
}
int rsp_no_vsync(void) {  // 0x10017a220
  return H->no_vsync ? H->no_vsync(H->ctx) != 0 : rsp_config_global()->no_vsync != 0;
}
int rsp_screen_width(void) {  // 0x10017a310
  int w = 0, h = 0;
  if (H->get_resolution) H->get_resolution(H->ctx, &w, &h);
  return from_engine(w);
}
int rsp_screen_height(void) {  // 0x10017a330
  int w = 0, h = 0;
  if (H->get_resolution) H->get_resolution(H->ctx, &w, &h);
  return from_engine(h);
}

// ---------------------------------------------------------------------------
// Lifecycle

static void default_exit(int status) { _exit(status); }
static void (*g_exit)(int) = default_exit;
void rsp_set_exit(void (*fn)(int)) { g_exit = fn ? fn : default_exit; }
// Both are clean ends of a session: a Metal session's crash marker goes (rsp_renderer.h).
void rsp_exit(int status) {
  rsp_metal_session_end();
  g_exit(status);
}

void rsp_request_quit(void) {  // 0x10017a350
  if (!H->request_quit || !H->request_quit(H->ctx)) rsp_exit(0);
}

static int g_booted;  // the original guards on the Lua state (0x1018e7478)

int rsp_game_bootstrap(const rsp_boot *boot) {  // 0x100179dc0
  if (g_booted) return 0;
  g_booted = 1;
  // 1. 0x1002a5cf0: chdir to the folder holding the .app, build the path globals.
  rsp_paths_set_root(boot->app_dir);
  rsp_log("paths root=%s tmp=%s data=%s update=%s", rsp_path_root(), rsp_path_tmp(),
          rsp_path_data(), rsp_path_update());
  // 2. 0x1012f6ce0: efd's path hooks (0x1002a5de0, 0x1002a5dc0, 0x1002a5e10,
  //    0x1002a5dd0) -> rsp_path_copy_root / rsp_path_root for the engine.
  // 3. 0x101315840: Rocksmith.ini (load, or write the defaults).
  rsp_config *cfg = rsp_config_global();
  int loaded = rsp_config_load_or_create(cfg);
  rsp_log("config %s: Fullscreen=%u %ux%u VisualQuality=%u", loaded ? "read" : "created",
          cfg->fullscreen, cfg->screen_width, cfg->screen_height, cfg->visual_quality);
  // 4. Display settings from the config, with the 800x600 floor.
  if (H->set_fullscreen) H->set_fullscreen(H->ctx, (int)cfg->fullscreen);
  if (H->set_resolution) H->set_resolution(H->ctx, (int)cfg->screen_width, (int)cfg->screen_height);
  if (H->set_visual_quality) H->set_visual_quality(H->ctx, (int)cfg->visual_quality);
  // The config and the floor are in engine pixels, as in the original (not rsp_screen_width/height,
  // which are window points when a render scale is set).
  int ew = 0, eh = 0;
  if (H->get_resolution) H->get_resolution(H->ctx, &ew, &eh);
  unsigned w = (unsigned)ew, h = (unsigned)eh;
  if (w < 800 || h <= 599)
    if (H->set_resolution) H->set_resolution(H->ctx, 1280, 720);
  // 5. The engine: VFS, MainService_MacOS, Init(1, {"Rocksmith.app"}).
  static const char *const argv0[] = {"Rocksmith.app", NULL};
  rsp_boot b = *boot;
  if (!b.argv) {
    b.argc = 1;
    b.argv = argv0;
  }
  int r = H->bootstrap ? H->bootstrap(H->ctx, &b) : 0;
  rsp_game_set_present(r == 0);
  return r;
}

int rsp_game_frame(void) {  // 0x10017a020
  if (!g_game_present) return 1;
  rsp_input_flush();  // 0x100168360
  int quit = H->frame ? H->frame(H->ctx) : 0;
  if (quit) rsp_game_set_present(0);
  return quit;
}

void rsp_game_shutdown(void) {  // 0x10017a0d0
  if (H->shutdown) H->shutdown(H->ctx);
  rsp_game_set_present(0);
  rsp_input_reset();
}
