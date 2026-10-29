// Unit tests for the portable half of the platform shell (no AppKit, no GL):
// paths, Rocksmith.ini, the input queue and focus gating, the boot sequence
// against the stub engine, and the helpers lifted out of the ObjC methods.
// Exits 1 on any failure. Runs on arm64 and x86_64.
#include <limits.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <mach-o/dyld.h>
#include <signal.h>

#include "rsp_platform.h"
#include "rsp_renderer.h"

static int g_pass, g_fail;
#define CHECK(c, ...)                         \
  do {                                        \
    if (c) g_pass++;                          \
    else {                                    \
      g_fail++;                               \
      printf("FAIL %s:%d: ", __FILE__, __LINE__); \
      printf(__VA_ARGS__);                    \
      printf("\n");                           \
    }                                         \
  } while (0)

static char g_tmpdir[512];

// --- paths -------------------------------------------------------------------
static void test_paths(void) {
  char want[600], cwd[600];
  int r = rsp_paths_set_root(g_tmpdir);
  CHECK(r == 0, "mkdir Tmp returned %d", r);
  snprintf(want, sizeof want, "%s/", g_tmpdir);
  CHECK(strcmp(rsp_path_root(), want) == 0, "root %s", rsp_path_root());
  snprintf(want, sizeof want, "%s/Tmp/", g_tmpdir);
  CHECK(strcmp(rsp_path_tmp(), want) == 0, "tmp %s", rsp_path_tmp());
  snprintf(want, sizeof want, "%s/Data/", g_tmpdir);
  CHECK(strcmp(rsp_path_data(), want) == 0, "data %s", rsp_path_data());
  snprintf(want, sizeof want, "%s/Update/", g_tmpdir);
  CHECK(strcmp(rsp_path_update(), want) == 0, "update %s", rsp_path_update());
  struct stat st;
  CHECK(stat(rsp_path_tmp(), &st) == 0 && S_ISDIR(st.st_mode), "Tmp/ not created");
  CHECK(getcwd(cwd, sizeof cwd) && strstr(cwd, strrchr(g_tmpdir, '/')), "cwd %s", cwd);
  r = rsp_paths_set_root(g_tmpdir);
  CHECK(r == -1, "second mkdir should fail with EEXIST, got %d", r);

  char buf[600];
  size_t rl = strlen(rsp_path_root());
  CHECK(rsp_path_copy_root(buf, (int)sizeof buf) && strcmp(buf, rsp_path_root()) == 0, "copy");
  CHECK(!rsp_path_copy_root(buf, (int)rl), "copy into exactly strlen must fail");
  CHECK(rsp_path_copy_root(buf, (int)rl + 1), "copy into strlen+1 must fit");
  memset(buf, 'x', 4);
  CHECK(!rsp_path_copy_root(buf, 0) && buf[0] == 'x', "size 0 leaves buf alone");
  snprintf(want, sizeof want, "%s/Rocksmith.ini", g_tmpdir);
  CHECK(rsp_path_ini(buf, sizeof buf) && strcmp(buf, want) == 0, "ini path %s", buf);

  CHECK(rsp_path_is_relative("a/b"), "a/b");
  CHECK(rsp_path_is_relative(""), "empty is relative");
  CHECK(rsp_path_is_relative("./x"), "./x");
  CHECK(!rsp_path_is_relative("/x"), "/x");
  CHECK(!rsp_path_is_relative("\\x"), "\\x");
  CHECK(!rsp_path_is_relative(NULL), "NULL");
}

// --- Rocksmith.ini -----------------------------------------------------------
static const char k_installed[] =  // the user's installed file, as the game wrote it
    "[Audio]\nEnableMicrophone=0\nLatencyBuffer=2\nDumpAudioLog=0\nRealToneCableOnly=0\n"
    "MonoToStereoChannel=0\n[Renderer.MacOS]\nScreenWidth=0\nScreenHeight=0\nFullscreen=0\n"
    "VisualQuality=3\nRenderingWidth=0\nRenderingHeight=0\nEnablePostEffects=1\nEnableShadows=1\n"
    "EnableHighResScope=0\nEnableDepthOfField=1\nEnablePerPixelLighting=1\nMsaaSamples=4\n"
    "DisableBrowser=0\n[Net]\nUseProxy=1\n[Global]\nVersion=1\n";

static void test_config(void) {
  rsp_config c, d;
  rsp_config_defaults(&c);
  CHECK(c.latency_buffer == 4 && c.fullscreen == 2 && c.visual_quality == 1 && c.msaa_samples == 4 &&
            c.use_proxy == 1 && c.auto_login == 1 && c.enable_shadows == 1 && c.version == 0,
        "defaults");

  // The installed file parses and writes back byte for byte.
  rsp_config_parse(&c, k_installed, strlen(k_installed));
  CHECK(c.latency_buffer == 2 && c.fullscreen == 0 && c.visual_quality == 3 &&
            c.enable_high_res_scope == 0 && c.version == 1 && !c.dirty,
        "installed values");
  char out[2048];
  size_t n = rsp_config_write(&c, out, sizeof out);
  CHECK(n == strlen(k_installed) && strcmp(out, k_installed) == 0, "rewrite differs:\n%s", out);

  // Defaults written: AutoLogin is omitted while it is on, Version 0.
  rsp_config_defaults(&c);
  n = rsp_config_write(&c, out, sizeof out);
  CHECK(strstr(out, "Fullscreen=2\n") && strstr(out, "Version=0\n") && !strstr(out, "AutoLogin"),
        "defaults text");
  c.auto_login = 0;
  rsp_config_write(&c, out, sizeof out);
  CHECK(strstr(out, "[Net]\nUseProxy=1\nAutoLogin=0\n[Global]\n") != NULL, "AutoLogin=0 written");
  // Too small a buffer reports the size needed.
  size_t need = rsp_config_write(&c, out, 8);
  CHECK(need > 100 && strlen(out) == 7, "truncated write");

  // CRLF, base-0 numbers, normalisation, Version 0 -> 1 + dirty.
  const char *t1 =
      "[Renderer.MacOS]\r\nScreenWidth=0x780\r\nScreenHeight=010\r\nFullscreen=1\r\nVisualQuality=7\r\n"
      "[Global]\r\nVersion=0\r\n";
  rsp_config_defaults(&c);
  rsp_config_parse(&c, t1, strlen(t1));
  CHECK(c.screen_width == 1920 && c.screen_height == 8, "base 0: %u %u", c.screen_width,
        c.screen_height);
  CHECK(c.fullscreen == 2 && c.visual_quality == 1, "normalise fs=%u vq=%u", c.fullscreen,
        c.visual_quality);
  CHECK(c.version == 1 && c.dirty == 1, "version %u dirty %u", c.version, c.dirty);
  const char *t2 = "[Renderer.MacOS]\nFullscreen=5\n";
  rsp_config_defaults(&c);
  rsp_config_parse(&c, t2, strlen(t2));
  CHECK(c.fullscreen == 2, "fs 5 -> 2");

  // Exact matching: section names, key prefixes, '[' ends a section, the
  // first matching line wins, absent keys keep their value.
  const char *t3 =
      "[Audio] \nLatencyBuffer=9\n[Audio]\nLatencyBufferX=7\n LatencyBuffer=6\nLatencyBuffer=5\n"
      "LatencyBuffer=3\n[x\nMonoToStereoChannel=2\n";
  rsp_config_defaults(&c);
  c.mono_to_stereo = 77;
  rsp_config_parse(&c, t3, strlen(t3));
  CHECK(c.latency_buffer == 5, "LatencyBuffer %u", c.latency_buffer);
  CHECK(c.mono_to_stereo == 77, "key after '[' must not be read (%u)", c.mono_to_stereo);
  // Booleans are "== 1"; "2" is false; absent keeps.
  const char *t4 = "[Audio]\nEnableMicrophone=2\nDumpAudioLog=1\n[Net]\nUseProxy=0\n";
  rsp_config_defaults(&c);
  c.real_tone_cable_only = 1;
  rsp_config_parse(&c, t4, strlen(t4));
  CHECK(c.enable_microphone == 0 && c.dump_audio_log == 1 && c.real_tone_cable_only == 1 &&
            c.use_proxy == 0 && c.auto_login == 1,
        "bools");
  // Values: garbage -> 0, negative -> 0xffffffff, overflow saturates.
  const char *t5 = "[Renderer.MacOS]\nScreenWidth=abc\nScreenHeight=-1\nMsaaSamples=99999999999999\n";
  rsp_config_defaults(&c);
  rsp_config_parse(&c, t5, strlen(t5));
  CHECK(c.screen_width == 0 && c.screen_height == 0xffffffffu, "w %u h %u", c.screen_width,
        c.screen_height);
  CHECK(c.msaa_samples == (uint32_t)strtoul("99999999999999", NULL, 0), "msaa %u", c.msaa_samples);
  // Over-long lines are split at 0xfff bytes; the tail is a line of its own.
  char *big = malloc(0x3000);
  int p = sprintf(big, "[Audio]\n");
  memset(big + p, 'a', 0xfff);
  p += 0xfff;
  p += sprintf(big + p, "LatencyBuffer=12\n");
  rsp_config_defaults(&c);
  rsp_config_parse(&c, big, (size_t)p);
  CHECK(c.latency_buffer == 12, "split line %u", c.latency_buffer);
  free(big);
  // A missing trailing newline and an embedded NUL.
  const char t6[] = "[Audio]\nLatencyBuffer=8";
  rsp_config_defaults(&c);
  rsp_config_parse(&c, t6, sizeof t6 - 1);
  CHECK(c.latency_buffer == 8, "no trailing newline");
  const char t7[] = "[Audio]\0junk\nLatencyBuffer=6\n";
  rsp_config_defaults(&c);
  rsp_config_parse(&c, t7, sizeof t7 - 1);
  CHECK(c.latency_buffer == 6, "NUL ends the compared line");

  // Round trip: write then parse gives the same block (normalised values).
  for (int i = 0; i < 200; i++) {
    rsp_config_defaults(&c);
    unsigned s = (unsigned)i * 2654435761u;
    c.enable_microphone = s & 1;
    c.dump_audio_log = (s >> 1) & 1;
    c.latency_buffer = s % 17;
    c.fullscreen = (s >> 3) & 1 ? 2 : 0;
    c.screen_width = s % 5000;
    c.screen_height = (s >> 7) % 3000;
    c.visual_quality = (s >> 9) & 3;
    c.msaa_samples = (s >> 11) % 9;
    c.use_proxy = (s >> 13) & 1;
    c.auto_login = (s >> 14) & 1;
    c.disable_browser = (s >> 15) & 1;
    c.version = 1 + (s >> 16) % 4;
    rsp_config_write(&c, out, sizeof out);
    rsp_config_defaults(&d);
    rsp_config_parse(&d, out, strlen(out));
    CHECK(memcmp(&c, &d, sizeof c) == 0, "round trip %d", i);
    if (g_fail > 20) break;
  }

  // Load-or-create in the test root.
  char path[700];
  rsp_path_ini(path, sizeof path);
  unlink(path);
  rsp_config_defaults(&c);
  c.latency_buffer = 99;
  CHECK(rsp_config_load_or_create(&c) == 0 && c.latency_buffer == 99 && !c.loaded,
        "missing file: c untouched");
  FILE *f = fopen(path, "rb");
  CHECK(f != NULL, "defaults file not created");
  if (f) {
    size_t len = fread(out, 1, sizeof out - 1, f);
    out[len] = 0;
    fclose(f);
    rsp_config_defaults(&d);
    char want[2048];
    rsp_config_write(&d, want, sizeof want);
    CHECK(strcmp(out, want) == 0, "created file is the defaults");
  }
  rsp_config_defaults(&c);
  CHECK(rsp_config_load_or_create(&c) == 1 && c.loaded == 1 && c.version == 1 && c.dirty == 1,
        "load: version 0 -> 1 + dirty");
  rsp_config_save_if_dirty(&c);
  CHECK(c.dirty == 0, "save_if_dirty clears");
  rsp_config_defaults(&d);
  rsp_config_load_or_create(&d);
  CHECK(d.version == 1 && d.dirty == 0, "saved Version=1");
}

// --- input -------------------------------------------------------------------
typedef struct { int code, a, b; } rec;
static rec g_rec[4096];
static int g_nrec;
static pthread_mutex_t g_rec_mu = PTHREAD_MUTEX_INITIALIZER;
static void rec_input(void *u, int code, int a, int b) {
  (void)u;
  pthread_mutex_lock(&g_rec_mu);
  if (g_nrec < 4096) g_rec[g_nrec++] = (rec){code, a, b};
  pthread_mutex_unlock(&g_rec_mu);
}
static int g_active_calls[2], g_focus_lost;
static void rec_active(void *u, int on) {
  (void)u;
  g_active_calls[on ? 1 : 0]++;
}
static void rec_focus_lost(void *u) {
  (void)u;
  g_focus_lost++;
}

static void *producer(void *arg) {
  int base = (int)(intptr_t)arg;
  for (int i = 0; i < 500; i++) rsp_post_input(RSP_EV_KEYDOWN, base + i, i);
  return NULL;
}

static void test_input(void) {
  rsp_engine_hooks h = *rsp_stub_hooks();
  h.app_active = rec_active;
  h.focus_lost = rec_focus_lost;
  rsp_set_hooks(&h);
  rsp_stub()->input_cb = rec_input;

  for (int t = 0; t < 8; t++) CHECK(rsp_gfx_event_code(t) == (t < 6 ? t + 1 : -1), "code %d", t);
  CHECK(rsp_gfx_event_code(-1) == -1, "code -1");

  rsp_input_reset();
  rsp_game_set_present(1);
  rsp_set_focus(0);
  CHECK(g_active_calls[0] == 1 && g_focus_lost == 1, "focus loss: audio off + UI pause");
  rsp_post_input(RSP_EV_KEYDOWN, 36, 13);
  CHECK(rsp_input_pending() == 0, "unfocused keys are dropped");
  rsp_set_focus(1);
  CHECK(g_active_calls[1] == 1 && g_focus_lost == 1 && rsp_focused(), "focus gain: audio on");
  rsp_post_input(RSP_EV_KEYDOWN, 36, 13);
  rsp_post_input(RSP_EV_KEYUP, 36, 13);
  rsp_post_input(RSP_EV_MOUSEMOVE, -5, 700);
  rsp_post_input(RSP_EV_SCROLL, 1, 1);
  rsp_post_input(RSP_EV_APP_INACTIVE, 0, 0);  // not queued, audio off
  rsp_post_input(RSP_EV_APP_ACTIVE, 0, 0);    // audio on
  rsp_post_input(9, 1, 2);                    // unknown types are queued -> -1
  CHECK(g_active_calls[0] == 2 && g_active_calls[1] == 2, "types 6/7 hit audio directly");
  CHECK(rsp_input_pending() == 5, "pending %d", rsp_input_pending());
  g_nrec = 0;
  CHECK(rsp_input_flush() == 5 && rsp_input_pending() == 0, "flush");
  CHECK(g_nrec == 5 && g_rec[0].code == 1 && g_rec[0].a == 36 && g_rec[1].code == 2 &&
            g_rec[2].code == 3 && g_rec[2].a == -5 && g_rec[2].b == 700 && g_rec[3].code == 6 &&
            g_rec[4].code == -1,
        "delivered in order with GFx codes");
  rsp_game_set_present(0);
  rsp_post_input(RSP_EV_KEYDOWN, 1, 1);
  CHECK(rsp_input_pending() == 0, "no game: dropped");

  // Two producers and the flusher at once.
  rsp_game_set_present(1);
  g_nrec = 0;
  pthread_t a, b;
  pthread_create(&a, NULL, producer, (void *)(intptr_t)0);
  pthread_create(&b, NULL, producer, (void *)(intptr_t)10000);
  int got = 0;
  for (int i = 0; i < 2000 && got < 1000; i++) got += rsp_input_flush();
  pthread_join(a, NULL);
  pthread_join(b, NULL);
  got += rsp_input_flush();
  CHECK(got == 1000 && g_nrec == 1000, "threads: %d delivered", got);
  int lasta = -1, lastb = 9999, order = 1;
  for (int i = 0; i < g_nrec; i++) {
    if (g_rec[i].a < 10000) order &= g_rec[i].a == lasta + 1, lasta = g_rec[i].a;
    else order &= g_rec[i].a == lastb + 1, lastb = g_rec[i].a;
  }
  CHECK(order, "per-producer order kept");
  rsp_stub()->input_cb = NULL;
  rsp_set_focus(0);
  rsp_set_hooks(rsp_stub_hooks());
}

// --- boot sequence and display mode -------------------------------------------
static void write_ini(const char *text) {
  char path[700];
  rsp_path_ini(path, sizeof path);
  FILE *f = fopen(path, "wb");
  fputs(text, f);
  fclose(f);
}

// The engine works in pixels, the shell in window points; rsp_set_render_scale converts at the boundary.
static void test_render_scale(void) {
  rsp_stub_state *s = rsp_stub();
  CHECK(rsp_render_scale() == 1.0, "scale 1 by default");
  rsp_set_display_mode(0, 1280, 720);
  CHECK(s->width == 1280 && s->height == 720 && rsp_screen_width() == 1280 && rsp_screen_height() == 720,
        "scale 1: identity");
  CHECK(rsp_mouse_engine_coord(639.5) == rsp_mouse_coord(639.5) && rsp_mouse_engine_coord(-0.51) == -1,
        "scale 1: mouse as the original");
  rsp_set_render_scale(2.0);
  rsp_set_display_mode(0, 1280, 720);
  CHECK(s->width == 2560 && s->height == 1440, "scale 2: engine %dx%d", s->width, s->height);
  CHECK(rsp_screen_width() == 1280 && rsp_screen_height() == 720, "scale 2: points %dx%d", rsp_screen_width(),
        rsp_screen_height());
  rsp_set_display_mode(1, 1279, 719);  // odd point sizes stay exact
  CHECK(s->width == 2558 && s->height == 1438 && rsp_screen_width() == 1279 && rsp_screen_height() == 719,
        "scale 2: odd sizes %dx%d", s->width, s->height);
  s->width = 2561;  // an engine size no point size maps to: nearest point
  CHECK(rsp_screen_width() == 1281, "scale 2: odd engine width -> %d", rsp_screen_width());
  // Mouse: view points (origin bottom left) -> engine pixels (origin bottom left, frame 2x the view):
  // the view's corners map to the engine frame's corners, half points to whole pixels.
  struct { double v; int want; } mc[] = {{0, 0}, {0.25, 1}, {0.5, 1}, {0.75, 2}, {639.5, 1279},
                                         {1279.75, 2560}, {720, 1440}, {-0.5, -1}};
  for (size_t i = 0; i < sizeof mc / sizeof *mc; i++)
    CHECK(rsp_mouse_engine_coord(mc[i].v) == mc[i].want, "scale 2: mouse %g -> %d (want %d)", mc[i].v,
          rsp_mouse_engine_coord(mc[i].v), mc[i].want);
  rsp_set_render_scale(0);
  CHECK(rsp_render_scale() == 1.0, "scale 0 -> 1");
  rsp_set_render_scale(-2);
  CHECK(rsp_render_scale() == 1.0, "negative scale -> 1");
  rsp_set_render_scale(NAN);
  CHECK(rsp_render_scale() == 1.0, "NaN scale -> 1");
  rsp_set_display_mode(0, 1280, 720);
  CHECK(s->width == 1280 && rsp_screen_width() == 1280, "back to scale 1");
}

// Bootstrap at render scale 2 (in a child: the bootstrap runs once per process): Rocksmith.ini and the
// 800x600 floor are engine pixels, so 1024x600 is kept, not forced to 1280x720 as half of it would be.
static void test_boot_scaled(void) {
  pid_t pid = fork();
  if (pid == 0) {
    rsp_stub_state *s = rsp_stub();
    write_ini("[Renderer.MacOS]\nScreenWidth=1024\nScreenHeight=600\nFullscreen=0\nVisualQuality=2\n");
    rsp_boot b = {g_tmpdir, "/", 0, NULL};
    *rsp_config_global() = (rsp_config){0};
    rsp_config_defaults(rsp_config_global());
    rsp_set_render_scale(2.0);
    int ok = rsp_game_bootstrap(&b) == 0 && s->width == 1024 && s->height == 600;
    if (!ok) printf("FAIL: scaled bootstrap: engine %dx%d\n", s->width, s->height);
    fflush(stdout);
    _exit(ok ? 0 : 1);
  }
  int st = 0;
  CHECK(pid > 0 && waitpid(pid, &st, 0) == pid && WIFEXITED(st) && WEXITSTATUS(st) == 0,
        "bootstrap at scale 2 keeps the config's engine resolution (status %d)", st);
}

static void test_boot(void) {
  rsp_stub_state *s = rsp_stub();
  write_ini("[Renderer.MacOS]\nScreenWidth=640\nScreenHeight=480\nFullscreen=0\nVisualQuality=2\n");
  rsp_boot b = {g_tmpdir, "/", 0, NULL};
  *rsp_config_global() = (rsp_config){0};
  rsp_config_defaults(rsp_config_global());
  CHECK(rsp_game_bootstrap(&b) == 0 && s->booted, "bootstrap");
  CHECK(s->width == 1280 && s->height == 720, "640x480 is below the floor -> %dx%d", s->width,
        s->height);
  CHECK(s->fullscreen == 0 && s->visual_quality == 2 && rsp_config_global()->loaded, "fs/vq from ini");
  CHECK(rsp_game_bootstrap(&b) == 0, "second bootstrap is a no-op");
  CHECK(!rsp_is_fullscreen() && rsp_screen_width() == 1280 && rsp_screen_height() == 720, "getters");
  rsp_set_display_mode(1, 1920, 1080);
  CHECK(rsp_is_fullscreen() && rsp_screen_width() == 1920 && rsp_screen_height() == 1080, "mode set");
  rsp_set_display_mode(0x100, 800, 600);  // only the low byte of fs is used
  CHECK(!rsp_is_fullscreen(), "fs byte");
  s->no_vsync = 1;
  CHECK(rsp_no_vsync() == 1, "no_vsync");
  s->no_vsync = 0;

  long f0 = s->frames;
  CHECK(rsp_game_frame() == 0 && s->frames == f0 + 1, "frame");
  rsp_request_quit();
  CHECK(rsp_game_frame() == 0, "frame after quit request still runs (ServiceManager shuts down)");
  CHECK(rsp_game_frame() == 1, "then the engine is gone");
  CHECK(rsp_game_frame() == 1, "and stays gone");
  rsp_game_shutdown();
  CHECK(!s->booted, "shutdown");
}

// --- helpers from the ObjC methods ----------------------------------------------
static void test_logic(void) {
  CHECK(rsp_pack_keydown_char('a', 0) == 'a', "plain");
  CHECK(rsp_pack_keydown_char('a', RSP_MOD_COMMAND) == (0x80000000u | 'a'), "cmd -> bit 31");
  CHECK(rsp_pack_keydown_char('a', RSP_MOD_SHIFT | RSP_MOD_CONTROL) == 'a', "shift/ctrl ignored");
  CHECK(rsp_pack_keydown_char(0xf700, 0x00a00100) == 0xf700, "fn+numpad bits do not reach bit 31");
  CHECK(rsp_pack_keydown_char('x', 0x1ffffffffull) == (0x80000000u | 'x'), "64-bit flags truncated");
  CHECK(rsp_flags_changed_forwards(RSP_MOD_CONTROL) && !rsp_flags_changed_forwards(RSP_MOD_SHIFT),
        "flagsChanged");
  CHECK(rsp_is_fullscreen_toggle('F', RSP_MOD_COMMAND | RSP_MOD_SHIFT), "Cmd+Shift+F");
  CHECK(rsp_is_fullscreen_toggle('F', RSP_MOD_COMMAND | RSP_MOD_SHIFT | RSP_MOD_CONTROL), "+Ctrl");
  CHECK(!rsp_is_fullscreen_toggle('f', RSP_MOD_COMMAND | RSP_MOD_SHIFT), "lower f");
  CHECK(!rsp_is_fullscreen_toggle('F', RSP_MOD_COMMAND | RSP_MOD_CONTROL), "Cmd+Ctrl+F is not it");

  struct { double v; int want; } mc[] = {{0, 0}, {0.49, 0}, {0.5, 1}, {1.5, 2}, {-0.5, 0},
                                         {-0.51, -1}, {-1.5, -1}, {1279.999, 1280}, {719.4, 719},
                                         {16777217.0, 16777218}, {1e30, 0}, {NAN, 0}};
  for (size_t i = 0; i < sizeof mc / sizeof *mc; i++)
    CHECK(rsp_mouse_coord(mc[i].v) == mc[i].want, "mouse %g -> %d", mc[i].v, rsp_mouse_coord(mc[i].v));

  int a = -1;
  CHECK(!rsp_scroll_event(0.0, &a) && a == -1, "no scroll at 0");
  CHECK(!rsp_scroll_event(1e-50, &a), "below float range counts as 0");
  CHECK(rsp_scroll_event(0.1, &a) && a == 1, "up");
  CHECK(rsp_scroll_event(-3, &a) && a == 0, "down");
  CHECK(rsp_scroll_event(NAN, &a) && a == 0, "NaN posts 0");

  int ax, ay;
  rsp_aspect_from_width(1280, &ax, &ay);
  CHECK(ax == 1280 && ay == 720, "1280");
  rsp_aspect_from_width(1366, &ax, &ay);
  CHECK(ax == 1376 && ay == 774, "1366 -> %d:%d", ax, ay);
  rsp_aspect_from_width(0, &ax, &ay);
  CHECK(ax == 0 && ay == 0, "0");
  rsp_aspect_from_width(-7, &ax, &ay);
  CHECK(ax == 0 && ay == 0, "-7 -> %d:%d", ax, ay);
  rsp_aspect_from_width(-17, &ax, &ay);
  CHECK(ax == -16 && ay == -9, "-17 -> %d:%d", ax, ay);

  int vis = 0;
  CHECK(rsp_cursor_policy(100, 95, &vis) == 1 && vis == 1, "show");
  CHECK(rsp_cursor_policy(100, 95, &vis) == 0 && vis == 1, "stay");
  CHECK(rsp_cursor_policy(105, 95, &vis) == 0, "exactly 10 s: still shown");
  CHECK(rsp_cursor_policy(105.01, 95, &vis) == -1 && vis == 0, "hide");
  CHECK(rsp_cursor_policy(200, 95, &vis) == 0 && vis == 0, "stay hidden");
  CHECK(rsp_cursor_policy(1e9, 0, &vis) == 0, "first frame: lastInputTime 0, hidden stays");

  CHECK(rsp_timer_mode_switch(0, 0) == RSP_TIMER_NONE && rsp_timer_mode_switch(1, 2) == RSP_TIMER_NONE,
        "mode equal");
  CHECK(rsp_timer_mode_switch(0, 2) == RSP_TIMER_GO_FULLSCREEN, "-> fs");
  CHECK(rsp_timer_mode_switch(1, 0) == RSP_TIMER_GO_WINDOW, "-> window");
  CHECK(rsp_timer_fullscreen_refit(1920, 1080, 1920, 1080) == 0, "fits");
  CHECK(rsp_timer_fullscreen_refit(1920, 1080, 1920, 1102) == RSP_TIMER_GO_FULLSCREEN, "refit");
  CHECK(!rsp_timer_window_adopt(1280, 720, 1280, 720) && rsp_timer_window_adopt(1024, 576, 1280, 720),
        "adopt");
  CHECK(rsp_timer_window_adopt(1280.5, 720, 1280, 720), "fractional content adopts");
  CHECK(rsp_timer_swap(0, 1) == -1 && rsp_timer_swap(1, 0) == -1, "swap steady");
  CHECK(rsp_timer_swap(0, 0) == 1 && rsp_timer_swap(1, 1) == 0, "swap flips: interval = !no_vsync");
  CHECK(rsp_timer_swap(7, 1) == 0, "no_vsync is a bool");
}

// --- renderer choice and the Metal crash fallback (rsp_renderer.h) -----------------------------
static rsp_renderer_out decide(const char *env, const char *setting, int abnormal, int metal_ok, int loaded) {
  rsp_renderer_in in = {env, setting, abnormal, metal_ok, loaded};
  return rsp_renderer_decide(&in);
}
#define DECIDES(o, be, why, re) ((o).backend == (be) && (o).reason == (why) && (o).reexec == (re))

static void test_renderer_decide(void) {
  enum { G = RSP_GFX_GL, M = RSP_GFX_METAL, N = RSP_GFX_NONE };
  CHECK(rsp_renderer_parse("metal") == M && rsp_renderer_parse(" Metal\n") == M, "parse metal");
  CHECK(rsp_renderer_parse("gl") == G && rsp_renderer_parse("opengl\r\n") == G && rsp_renderer_parse("OpenGL") == G,
        "parse gl");
  CHECK(rsp_renderer_parse(NULL) == N && rsp_renderer_parse("") == N && rsp_renderer_parse("vulkan") == N &&
            rsp_renderer_parse("metal x") == N && rsp_renderer_parse("metalx") == N,
        "parse other");
  rsp_renderer_out o;
  // Nothing set: Metal, which libRSOpenGL loaded with by default.
  o = decide(NULL, NULL, 0, 1, M);
  CHECK(DECIDES(o, M, RSP_RENDERER_DEFAULT, 0), "default metal (%d %d %d)", o.backend, o.reason, o.reexec);
  o = decide("", "", 1, 1, M);
  CHECK(DECIDES(o, M, RSP_RENDERER_DEFAULT, 0), "empty env + empty setting + one crash: metal");
  o = decide("vulkan", "garbage", 0, 1, M);
  CHECK(DECIDES(o, M, RSP_RENDERER_DEFAULT, 0), "unknown values: default");
  // The setting.
  o = decide(NULL, "opengl\n", 0, 1, M);
  CHECK(DECIDES(o, G, RSP_RENDERER_SETTING, 1), "setting opengl: restart with gl");
  o = decide(NULL, "metal", 0, 1, M);
  CHECK(DECIDES(o, M, RSP_RENDERER_SETTING, 0), "setting metal");
  // The environment wins, and needs no restart (libRSOpenGL read the same variable).
  o = decide("gl", "metal", 0, 1, G);
  CHECK(DECIDES(o, G, RSP_RENDERER_ENV, 0), "env gl over setting metal");
  o = decide("metal", "opengl", 0, 1, M);
  CHECK(DECIDES(o, M, RSP_RENDERER_ENV, 0), "env metal over setting opengl");
  o = decide("metal", NULL, 7, 1, M);
  CHECK(DECIDES(o, M, RSP_RENDERER_ENV, 0), "env metal forces Metal over the crash count");
  o = decide("opengl", NULL, 0, 1, G);
  CHECK(DECIDES(o, G, RSP_RENDERER_ENV, 0), "env opengl");
  // Crashes: two abnormal endings in a row.
  o = decide(NULL, NULL, 1, 1, M);
  CHECK(DECIDES(o, M, RSP_RENDERER_DEFAULT, 0), "one abnormal ending: still metal");
  o = decide(NULL, NULL, 2, 1, M);
  CHECK(DECIDES(o, G, RSP_RENDERER_CRASHES, 1), "two: opengl, restart");
  o = decide(NULL, "metal", 3, 1, M);
  CHECK(DECIDES(o, G, RSP_RENDERER_CRASHES, 1), "setting metal does not force over crashes");
  o = decide(NULL, "opengl", 5, 1, M);
  CHECK(DECIDES(o, G, RSP_RENDERER_SETTING, 1), "setting opengl: the count does not matter");
  // No Metal.
  o = decide(NULL, NULL, 0, 0, M);
  CHECK(DECIDES(o, G, RSP_RENDERER_NO_METAL, 1), "no Metal: opengl, restart");
  o = decide("metal", NULL, 0, 0, M);
  CHECK(DECIDES(o, G, RSP_RENDERER_NO_METAL, 1), "env metal without Metal: opengl anyway");
  o = decide("gl", NULL, 0, 0, G);
  CHECK(DECIDES(o, G, RSP_RENDERER_ENV, 0), "gl without Metal");
  // No restart loop: when RS_GL_BACKEND already says it, a restart changes nothing.
  o = decide("gl", NULL, 0, 1, M);
  CHECK(o.backend == G && !o.reexec, "env gl but metal loaded: no restart");
  // No libRSOpenGL: nothing to choose.
  o = decide("metal", "metal", 0, 1, N);
  CHECK(DECIDES(o, G, RSP_RENDERER_NO_RSGL, 0), "no libRSOpenGL");
  CHECK(!strcmp(rsp_renderer_name(M), "metal") && !strcmp(rsp_renderer_name(G), "opengl"), "names");
}

static int read_file(const char *path, char *buf, size_t n) {
  FILE *f = fopen(path, "r");
  if (!f) return 0;
  size_t k = fread(buf, 1, n - 1, f);
  buf[k] = 0;
  fclose(f);
  return 1;
}

static void test_renderer_files(void) {
  char home[600], p[700], b[256];
  snprintf(home, sizeof home, "%s/rhome-files", g_tmpdir);
  mkdir(home, 0755);
  CHECK(!rsp_renderer_read_setting(home, b, sizeof b), "no setting file");
  CHECK(rsp_renderer_write_setting(home, RSP_GFX_GL) == 0 && rsp_renderer_read_setting(home, b, sizeof b) &&
            !strcmp(b, "opengl"),
        "setting written: %s", b);
  CHECK(rsp_renderer_write_setting(home, RSP_GFX_METAL) == 0 && rsp_renderer_read_setting(home, b, sizeof b) &&
            !strcmp(b, "metal"),
        "setting rewritten: %s", b);
  // Marker: a session that ends cleanly leaves none, one that does not counts.
  CHECK(rsp_metal_abnormal_count(home) == 0, "no marker: 0");
  CHECK(rsp_metal_session_begin(home, 0) == 0, "begin");
  snprintf(p, sizeof p, "%s/metal-session", home);
  CHECK(access(p, F_OK) == 0, "marker written");
  CHECK(rsp_metal_abnormal_count(home) == 1, "marker of a running session: it counts as one if found later");
  rsp_metal_session_end();
  CHECK(access(p, F_OK) != 0 && rsp_metal_abnormal_count(home) == 0, "clean end removes it");
  rsp_metal_session_end();  // twice: nothing
  CHECK(rsp_metal_session_begin(home, 1) == 0 && rsp_metal_abnormal_count(home) == 2, "N = 1: two in a row");
  rsp_metal_session_forget(home);
  CHECK(rsp_metal_abnormal_count(home) == 0, "forget");
  rsp_metal_session_end();
  // A marker a clean exit of another process removed is not removed twice (no error either).
  FILE *f = fopen(p, "w");
  fputs("garbage\n", f);
  fclose(f);
  CHECK(rsp_metal_abnormal_count(home) == 1, "unreadable count: one");
  unlink(p);
  // rsp_exit is a clean end.
  rsp_metal_session_begin(home, 0);
  rsp_set_exit(NULL);
  pid_t pid = fork();
  if (pid == 0) rsp_exit(0);
  int st = 0;
  waitpid(pid, &st, 0);
  CHECK(WIFEXITED(st) && access(p, F_OK) != 0, "rsp_exit removes the marker");
  rsp_metal_session_end();

  // <home>: RS_NATIVE_HOME, the install layout, else the caches folder.
  char got[700], app[700];
  setenv("RS_NATIVE_HOME", home, 1);
  CHECK(rsp_renderer_home(got, sizeof got, "/x/game/R.app") && !strcmp(got, home), "RS_NATIVE_HOME");
  unsetenv("RS_NATIVE_HOME");
  snprintf(app, sizeof app, "%s/game/Rocksmith2014.app", home);
  snprintf(p, sizeof p, "%s/game", home);
  mkdir(p, 0755);
  CHECK(rsp_renderer_home(got, sizeof got, app) && strstr(got, "/Library/Caches/rs-rearmed"),
        "game/ without game-dir: not an install (%s)", got);
  snprintf(p, sizeof p, "%s/game-dir", home);
  fclose(fopen(p, "w"));
  CHECK(rsp_renderer_home(got, sizeof got, app) && !strcmp(got, home), "install layout: %s", got);
  snprintf(app, sizeof app, "%s/game/Rocksmith2014.app/", home);
  CHECK(rsp_renderer_home(got, sizeof got, app) && !strcmp(got, home), "trailing slash: %s", got);
  CHECK(rsp_renderer_home(got, sizeof got, "/x/R.app") && strstr(got, "/Library/Caches/rs-rearmed"), "elsewhere");
  (void)read_file;
}

// The startup in a child process (this executable with --renderer-child): restarts, the crash
// fallback across sessions, clean and abnormal ends.
static char g_self[1024];
static int g_child_gen;

static int child_metal_ok(void) {
  const char *e = getenv("RSP_TEST_NO_METAL");
  return !(e && *e == '1');
}

// argv: self --renderer-child <how>; how = check | clean | exit | crash (SIGKILL) | term
static int renderer_child(int argc, char **argv) {
  const char *g = getenv("RSP_TEST_GEN");
  g_child_gen = (g ? atoi(g) : 0) + 1;
  char gs[16];
  snprintf(gs, sizeof gs, "%d", g_child_gen);
  setenv("RSP_TEST_GEN", gs, 1);
  // What libRSOpenGL would have loaded with.
  int loaded = rsp_renderer_parse(getenv("RS_GL_BACKEND")) == RSP_GFX_GL ? RSP_GFX_GL : RSP_GFX_METAL;
  const char *how = argc > 2 ? argv[2] : "check";
  int session = strcmp(how, "check") != 0;
  rsp_renderer_out o = rsp_renderer_startup(argv, NULL, loaded, child_metal_ok, session ? RSP_RENDERER_SESSION : 0);
  printf("gen %d backend %s reason %d\n", g_child_gen, rsp_renderer_name(o.backend), o.reason);
  fflush(stdout);
  if (!strcmp(how, "clean")) rsp_exit(0);
  if (!strcmp(how, "exit")) exit(0);
  if (!strcmp(how, "crash")) raise(SIGKILL);  // as a force quit (an abort would leave a crash report)
  if (!strcmp(how, "term")) raise(SIGTERM);
  return 0;
}

// Runs the child with `env` (prefix for env(1)); its output into out.
static int run_child(const char *home, const char *env, const char *how, char *out, size_t n) {
  char cmd[3000];
  snprintf(cmd, sizeof cmd,
           "exec env -u RS_GL_BACKEND -u RSP_TEST_GEN -u RSP_TEST_NO_METAL RS_RENDERER_NOTIFY=0 RS_NATIVE_HOME='%s' %s "
           "'%s' --renderer-child %s 2>/dev/null",
           home, env, g_self, how);
  FILE *p = popen(cmd, "r");
  if (!p) return -1;
  size_t k = fread(out, 1, n - 1, p);
  out[k] = 0;
  return pclose(p);
}

static void test_renderer_startup(void) {
  char home[600], p[700], out[512], b[2048];
  snprintf(home, sizeof home, "%s/rhome-startup", g_tmpdir);
  mkdir(home, 0755);
  snprintf(p, sizeof p, "%s/metal-session", home);
  char fb[700], set[700];
  snprintf(fb, sizeof fb, "%s/renderer-fallback.txt", home);
  snprintf(set, sizeof set, "%s/renderer", home);

  run_child(home, "", "check", out, sizeof out);
  CHECK(strstr(out, "gen 1 backend metal") != NULL, "default: metal, no restart: %s", out);
  run_child(home, "RSP_TEST_NO_METAL=1", "check", out, sizeof out);
  CHECK(strstr(out, "gen 2 backend opengl") != NULL, "no Metal: restarted once with gl: %s", out);
  run_child(home, "RS_GL_BACKEND=gl", "check", out, sizeof out);
  CHECK(strstr(out, "gen 1 backend opengl") != NULL, "env gl: %s", out);

  // A clean Metal session leaves no marker; exit() and SIGTERM are clean too.
  run_child(home, "", "clean", out, sizeof out);
  CHECK(strstr(out, "gen 1 backend metal") && access(p, F_OK) != 0, "clean: no marker (%s)", out);
  run_child(home, "", "exit", out, sizeof out);
  CHECK(access(p, F_OK) != 0, "exit(): no marker");
  run_child(home, "", "term", out, sizeof out);
  CHECK(access(p, F_OK) != 0, "SIGTERM: no marker");
  // --renderer-check style starts (no session) write none.
  run_child(home, "", "check", out, sizeof out);
  CHECK(access(p, F_OK) != 0, "no session: no marker");

  // Crash, clean, crash: the clean exit resets the count; still Metal.
  run_child(home, "", "crash", out, sizeof out);
  CHECK(read_file(p, b, sizeof b) && atoi(b) == 0, "first crash: marker 0 (%s)", b);
  run_child(home, "", "clean", out, sizeof out);
  CHECK(strstr(out, "backend metal") && access(p, F_OK) != 0, "one abnormal: metal, clean end resets");
  run_child(home, "", "crash", out, sizeof out);
  run_child(home, "", "crash", out, sizeof out);
  CHECK(strstr(out, "gen 1 backend metal") && read_file(p, b, sizeof b) && atoi(b) == 1,
        "second crash in a row: marker 1 (%s / %s)", out, b);
  // An explicit RS_GL_BACKEND=metal: Metal anyway (and it counts on).
  run_child(home, "RS_GL_BACKEND=metal", "crash", out, sizeof out);
  CHECK(strstr(out, "gen 1 backend metal") && read_file(p, b, sizeof b) && atoi(b) == 2, "forced: %s / %s", out, b);
  // The next start: OpenGL, restarted, the setting switched, a note, the count reset.
  CHECK(access(fb, F_OK) != 0, "no note yet");
  run_child(home, "", "clean", out, sizeof out);
  CHECK(strstr(out, "gen 2 backend opengl") != NULL, "after two abnormal endings: opengl (%s)", out);
  CHECK(read_file(set, b, sizeof b) && !strcmp(b, "opengl\n"), "setting -> opengl (%s)", b);
  CHECK(access(p, F_OK) != 0, "marker gone");
  CHECK(read_file(fb, b, sizeof b) && strstr(b, "echo metal >"), "note written");
  // From now on OpenGL by the setting (a GL session writes no marker), until it says metal again.
  run_child(home, "", "crash", out, sizeof out);
  CHECK(strstr(out, "gen 2 backend opengl") && access(p, F_OK) != 0, "setting opengl: %s", out);
  FILE *f = fopen(set, "w");
  fputs("metal\n", f);
  fclose(f);
  run_child(home, "", "clean", out, sizeof out);
  CHECK(strstr(out, "gen 1 backend metal") != NULL, "setting metal again: metal (%s)", out);
}

int main(int argc, char **argv) {
  if (argc > 1 && !strcmp(argv[1], "--renderer-child")) return renderer_child(argc, argv);
  uint32_t sn = sizeof g_self;
  if (_NSGetExecutablePath(g_self, &sn) != 0) return 2;
  setenv("RSP_LOG", "0", 0);
  snprintf(g_tmpdir, sizeof g_tmpdir, "%s/rsp_core_test.XXXXXX", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");
  for (char *p = g_tmpdir; (p = strstr(p, "//"));) memmove(p, p + 1, strlen(p));
  if (!mkdtemp(g_tmpdir)) {
    perror("mkdtemp");
    return 2;
  }
  test_paths();
  test_config();
  test_input();
  test_logic();
  test_render_scale();
  test_boot_scaled();
  test_boot();
  test_renderer_decide();
  test_renderer_files();
  test_renderer_startup();
  printf("rsp_core_test (%s): %d passed, %d failed\n",
#if defined(__arm64__)
         "arm64",
#else
         "x86_64",
#endif
         g_pass, g_fail);
  return g_fail ? 1 : 0;
}
