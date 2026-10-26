// Window-level test of the rewritten platform shell. Runs as an app bundle
// (RSPShellTest.app) so that [[NSBundle mainBundle] bundlePath] behaves like the
// game's: the folder holding the .app is the game root.
//
//   RSPShellTest.app/Contents/MacOS/RSPShellTest            automated test
//   RSPShellTest.app/Contents/MacOS/RSPShellTest --run      just run the shell (stub engine)
//
// The test needs a logged-in GUI session but no focus: it delivers keys with
// -[NSWindow sendEvent:]
// and asserts focus through the app delegate. It checks the window and view
// the "nib" builds, the pixel format and legacy context, the display-link
// render thread (CGL lock, context current, the shell's clear), focus gating,
// key/flagsChanged/mouse/scroll packing, Cmd+Shift+F, window resize -> engine
// resolution, the swap-interval sync, the file paths, and the quit path through
// applicationShouldTerminate: -> engine shutdown -> exit. Exits non-zero on any
// failure; a 60 s watchdog exits 2.
//
// RSP_TEST_FULLSCREEN=1 also lets the engine go full screen for real (the
// window goes borderless over the main screen for a moment). Off by default:
// the request is checked but not honoured.
#import "rsp_shell.h"

#include <OpenGL/gl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <sys/stat.h>
#include <unistd.h>

static int g_pass, g_fail;
#define CHECK(c, ...)                                 \
  do {                                                \
    if (c) g_pass++;                                  \
    else {                                            \
      g_fail++;                                       \
      printf("FAIL %s:%d: ", __FILE__, __LINE__);     \
      printf(__VA_ARGS__);                            \
      printf("\n");                                   \
      fflush(stdout);                                 \
    }                                                 \
  } while (0)

// --- engine hooks: the stub plus recording --------------------------------------
typedef struct { int code, a, b; } rec;
static rec g_rec[512];
static atomic_int g_nrec;
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static int g_real_fullscreen;
static int g_fs_requests[8], g_nfs_requests;
static int g_res_w[16], g_res_h[16];
static atomic_int g_nres;
static atomic_int g_shutdowns, g_gl_ctx_calls;

// GL state seen from the render thread (first frame only)
static atomic_int g_frame_checked;
static int g_on_main_thread = -1, g_ctx_current, g_cleared_black, g_drew;
static char g_gl_version[128], g_gl_renderer[128];
static NSOpenGLContext *g_ctx;

static void rec_input(void *u, int code, int a, int b) {
  pthread_mutex_lock(&g_mu);
  int n = atomic_load(&g_nrec);
  if (n < 512) {
    g_rec[n] = (rec){code, a, b};
    atomic_store(&g_nrec, n + 1);
  }
  pthread_mutex_unlock(&g_mu);
}
static void hk_set_fullscreen(void *ctx, int mode) {
  if (mode != rsp_stub()->fullscreen && g_nfs_requests < 8) g_fs_requests[g_nfs_requests++] = mode;
  if (g_real_fullscreen || mode == 0) rsp_stub_hooks()->set_fullscreen(ctx, mode);
}
static void hk_set_resolution(void *ctx, int w, int h) {
  int n = atomic_load(&g_nres);
  if (n < 16) {
    g_res_w[n] = w;
    g_res_h[n] = h;
    atomic_store(&g_nres, n + 1);
  }
  rsp_stub_hooks()->set_resolution(ctx, w, h);
}
static void hk_shutdown(void *ctx) {
  atomic_fetch_add(&g_shutdowns, 1);
  rsp_stub_hooks()->shutdown(ctx);
}
static void hk_gl_context(void *ctx, void *c) {
  atomic_fetch_add(&g_gl_ctx_calls, 1);
  g_ctx = (NSOpenGLContext *)c;
}
static int frame_cb(void *u) {
  if (atomic_load(&g_frame_checked)) return 0;
  g_on_main_thread = pthread_main_np();
  g_ctx_current = g_ctx && CGLGetCurrentContext() == (CGLContextObj)[g_ctx CGLContextObj];
  const char *v = (const char *)glGetString(GL_VERSION);
  const char *r = (const char *)glGetString(GL_RENDERER);
  snprintf(g_gl_version, sizeof g_gl_version, "%s", v ? v : "(null)");
  snprintf(g_gl_renderer, sizeof g_gl_renderer, "%s", r ? r : "(null)");
  GLubyte px[4] = {1, 1, 1, 1};
  glReadBuffer(GL_BACK);
  glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
  g_cleared_black = px[0] == 0 && px[1] == 0 && px[2] == 0;
  glClearColor(0.2f, 0.4f, 0.6f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
  g_drew = abs(px[0] - 51) <= 1 && abs(px[1] - 102) <= 1 && abs(px[2] - 153) <= 1;
  atomic_store(&g_frame_checked, 1);
  return 0;
}

// --- run loop helpers -------------------------------------------------------------
static void pump(double secs) {
  NSDate *until = [NSDate dateWithTimeIntervalSinceNow:secs];
  while ([until timeIntervalSinceNow] > 0) {
    @autoreleasepool {
      NSEvent *e = [NSApp nextEventMatchingMask:NSEventMaskAny
                                      untilDate:[NSDate dateWithTimeIntervalSinceNow:0.01]
                                         inMode:NSDefaultRunLoopMode
                                        dequeue:YES];
      if (e) [NSApp sendEvent:e];
    }
  }
}
static int wait_for(int (^cond)(void), double secs) {
  NSDate *until = [NSDate dateWithTimeIntervalSinceNow:secs];
  while (!cond()) {
    if ([until timeIntervalSinceNow] <= 0) return 0;
    pump(0.02);
  }
  return 1;
}
static long frames(void) { return rsp_stub()->frames; }
static void wait_frames(long n) {
  long f0 = frames();
  wait_for(^int { return frames() >= f0 + n; }, 5);
}

static NSEvent *key_event(NSWindow *w, NSEventType t, NSString *chars, NSString *ign,
                          unsigned short code, NSEventModifierFlags flags) {
  return [NSEvent keyEventWithType:t
                          location:NSZeroPoint
                     modifierFlags:flags
                         timestamp:[[NSProcessInfo processInfo] systemUptime]
                      windowNumber:[w windowNumber]
                           context:nil
                        characters:chars
       charactersIgnoringModifiers:ign
                         isARepeat:NO
                           keyCode:code];
}
static NSEvent *mouse_event(NSWindow *w, NSEventType t, NSPoint p) {
  return [NSEvent mouseEventWithType:t
                            location:p
                       modifierFlags:0
                           timestamp:[[NSProcessInfo processInfo] systemUptime]
                        windowNumber:[w windowNumber]
                             context:nil
                         eventNumber:0
                          clickCount:1
                            pressure:1];
}
// Waits until n more events reached the engine (or a timeout), returns how many.
static int collect(int base, int n) {
  wait_for(^int { return atomic_load(&g_nrec) >= base + n; }, 3);
  wait_frames(3);  // and make sure nothing else trickles in
  return atomic_load(&g_nrec) - base;
}

static char g_root[1024];

static void test_exit(int status) {
  // The shell's _Exit(0) after OpenGLView dealloc lands here.
  CHECK(atomic_load(&g_shutdowns) == 1, "engine shutdown hook ran %d times", atomic_load(&g_shutdowns));
  printf("rsp_shell_test (%s): %d passed, %d failed\n",
#if defined(__arm64__)
         "arm64",
#else
         "x86_64",
#endif
         g_pass, g_fail);
  fflush(stdout);
  _exit(g_fail ? 1 : (status ? 3 : 0));
}

static void run_tests(rsp_shell *sh) {
  NSWindow *w = sh->window;
  OpenGLView *v = sh->view;
  MainController *mc = sh->controller;

  // --- the nib's objects
  CHECK([NSApp delegate] == (id)sh->delegate && sh->delegate.window == w, "delegate wiring");
  CHECK([w contentView] == v, "content view");
  CHECK(([w styleMask] & 15) == 15, "style mask %lx", (unsigned long)[w styleMask]);
  CHECK([[w title] isEqualToString:@"Rocksmith 2014"], "title");
  NSSize cs = [w contentRectForFrameRect:[w frame]].size;
  CHECK(cs.width == 1280 && cs.height == 720, "content %gx%g", cs.width, cs.height);
  CHECK(rsp_g_main_controller == mc && v->controller == mc && mc->openGLView == v, "controller wiring");
  NSMenuItem *quit = [[[[NSApp mainMenu] itemAtIndex:0] submenu] itemAtIndex:0];
  CHECK(quit && [quit action] == @selector(terminate:) && [[quit keyEquivalent] isEqualToString:@"q"],
        "Quit menu item");

  // --- paths and Rocksmith.ini (bootstrap ran in awakeFromNib)
  char want[1100], cwd[1100];
  snprintf(want, sizeof want, "%s/", g_root);
  CHECK(strcmp(rsp_path_root(), want) == 0, "root %s, want %s", rsp_path_root(), want);
  struct stat st;
  CHECK(stat(rsp_path_tmp(), &st) == 0 && S_ISDIR(st.st_mode), "Tmp/ created");
  CHECK(getcwd(cwd, sizeof cwd) && strcmp(cwd, g_root) == 0, "cwd %s", cwd);
  CHECK(rsp_config_global()->loaded && rsp_config_global()->latency_buffer == 3, "ini read");
  CHECK(rsp_stub()->booted && rsp_stub()->width == 1280 && rsp_stub()->height == 720 &&
            rsp_stub()->fullscreen == 0,
        "engine display state %dx%d fs %d", rsp_stub()->width, rsp_stub()->height, rsp_stub()->fullscreen);
  CHECK(mc->CurrentSetting_FullScreen == YES && mc->numSwapBuffers == 1, "awakeFromNib state");

  // --- GL: pixel format, context, display link
  NSOpenGLPixelFormat *pf = [v pixelFormat];
  GLint val = -1;
  [pf getValues:&val forAttribute:NSOpenGLPFADoubleBuffer forVirtualScreen:0];
  CHECK(val == 1, "double buffer %d", val);
  [pf getValues:&val forAttribute:NSOpenGLPFAAccelerated forVirtualScreen:0];
  CHECK(val == 1, "accelerated %d", val);
  [pf getValues:&val forAttribute:NSOpenGLPFAColorSize forVirtualScreen:0];
  CHECK(val >= 24, "color size %d", val);
  [pf getValues:&val forAttribute:NSOpenGLPFADepthSize forVirtualScreen:0];
  CHECK(val >= 16, "depth size %d", val);
  [pf getValues:&val forAttribute:NSOpenGLPFAStencilSize forVirtualScreen:0];
  CHECK(val >= 8, "stencil size %d", val);
  [pf getValues:&val forAttribute:NSOpenGLPFAOpenGLProfile forVirtualScreen:0];
  CHECK(val == NSOpenGLProfileVersionLegacy, "profile 0x%x (want legacy)", val);
  CHECK(atomic_load(&g_gl_ctx_calls) == 1 && g_ctx == [v openGLContext] &&
            rsp_g_gl_context == [v openGLContext],
        "gl_context hook / global");
  GLint swap = -1;
  [[v openGLContext] getValues:&swap forParameter:NSOpenGLCPSwapInterval];
  CHECK(swap == 1, "swap interval %d", swap);
  CHECK(v->sleepDisableSuccess == kIOReturnSuccess, "no-display-sleep assertion 0x%x", v->sleepDisableSuccess);

  double t0 = CFAbsoluteTimeGetCurrent();
  long f0 = frames();
  wait_for(^int { return frames() >= f0 + 30; }, 10);
  double dt = CFAbsoluteTimeGetCurrent() - t0;
  CHECK(frames() >= f0 + 30, "display link: %ld frames in %.2fs", frames() - f0, dt);
  CHECK(CVDisplayLinkIsRunning(v->displayLink) && mc->isAnimating, "link running");
  CHECK(atomic_load(&g_frame_checked), "frame callback ran");
  CHECK(g_on_main_thread == 0, "frames run off the main thread");
  CHECK(g_ctx_current, "context current in the frame");
  CHECK(strncmp(g_gl_version, "2.1", 3) == 0, "legacy context: GL_VERSION %s", g_gl_version);
  CHECK(g_cleared_black, "the shell clears to black before the engine frame");
  CHECK(g_drew, "the engine frame can draw");
  printf("GL: %s / %s, %.0f fps\n", g_gl_renderer, g_gl_version, (frames() - f0) / dt);

  // --- focus: the view takes first responder -> focused; resigning the app
  // drops keys (the app is never really active here; nobody pressed anything)
  CHECK([w makeFirstResponder:v] && [w firstResponder] == v && rsp_focused(),
        "becomeFirstResponder sets focus");
  [sh->delegate applicationWillResignActive:[NSNotification notificationWithName:@"t" object:nil]];
  int base = atomic_load(&g_nrec);
  [w sendEvent:key_event(w, NSEventTypeKeyDown, @"a", @"a", 0, 0)];
  CHECK(collect(base, 1) == 0 && !rsp_focused(), "unfocused key dropped");
  [sh->delegate applicationDidBecomeActive:[NSNotification notificationWithName:@"t" object:nil]];
  CHECK(rsp_focused(), "applicationDidBecomeActive: sets focus");

  // keyDown / keyUp through -[NSWindow sendEvent:], Command packing
  base = atomic_load(&g_nrec);
  [w sendEvent:key_event(w, NSEventTypeKeyDown, @"a", @"a", 0, 0)];
  [w sendEvent:key_event(w, NSEventTypeKeyUp, @"a", @"a", 0, 0)];
  [w sendEvent:key_event(w, NSEventTypeKeyDown, @"\r", @"\r", 36, NSEventModifierFlagCommand)];
  [w sendEvent:key_event(w, NSEventTypeKeyUp, @"\r", @"\r", 36, NSEventModifierFlagCommand)];
  [w sendEvent:key_event(w, NSEventTypeKeyDown, @"", @"", 126,
                         NSEventModifierFlagFunction | NSEventModifierFlagNumericPad)];
  int got = collect(base, 5);
  CHECK(got == 5, "keys: %d events", got);
  if (got == 5) {
    rec *r = g_rec + base;
    CHECK(r[0].code == 1 && r[0].a == 0 && r[0].b == 'a', "keydown a: %d %d %x", r[0].code, r[0].a, r[0].b);
    CHECK(r[1].code == 2 && r[1].a == 0 && r[1].b == 'a', "keyup a");
    CHECK(r[2].code == 1 && r[2].a == 36 && (unsigned)r[2].b == 0x8000000du, "cmd+return %x", r[2].b);
    CHECK(r[3].code == 2 && r[3].a == 36 && r[3].b == '\r', "keyup has no cmd bit");
    CHECK(r[4].code == 1 && r[4].a == 126 && r[4].b == 0xf700, "up arrow");
  }
  // flagsChanged: only while Control is down, as a key-down with char 0
  base = atomic_load(&g_nrec);
  [w sendEvent:key_event(w, NSEventTypeFlagsChanged, @"", @"", 56, NSEventModifierFlagShift)];
  [w sendEvent:key_event(w, NSEventTypeFlagsChanged, @"", @"", 59, NSEventModifierFlagControl)];
  got = collect(base, 1);
  CHECK(got == 1 && g_rec[base].code == 1 && g_rec[base].a == 59 && g_rec[base].b == 0,
        "flagsChanged: %d events", got);

  // app (de)activation through the delegate
  [sh->delegate applicationWillResignActive:[NSNotification notificationWithName:@"t" object:nil]];
  base = atomic_load(&g_nrec);
  [w sendEvent:key_event(w, NSEventTypeKeyDown, @"b", @"b", 11, 0)];
  CHECK(collect(base, 1) == 0 && !rsp_focused(), "resign active: dropped");
  [sh->delegate applicationDidBecomeActive:[NSNotification notificationWithName:@"t" object:nil]];
  base = atomic_load(&g_nrec);
  [w sendEvent:key_event(w, NSEventTypeKeyDown, @"b", @"b", 11, 0)];
  CHECK(collect(base, 1) == 1 && rsp_focused(), "become active: delivered");

  // mouse (straight to the responder: a click on an inactive window is AppKit's
  // activation click, not the view's)
  base = atomic_load(&g_nrec);
  NSPoint p = NSMakePoint(100.4, 200.6);
  [v mouseDown:mouse_event(w, NSEventTypeLeftMouseDown, p)];
  [v mouseUp:mouse_event(w, NSEventTypeLeftMouseUp, p)];
  [v rightMouseDown:mouse_event(w, NSEventTypeRightMouseDown, p)];
  [v rightMouseUp:mouse_event(w, NSEventTypeRightMouseUp, p)];
  [v mouseMoved:mouse_event(w, NSEventTypeMouseMoved, p)];
  [v mouseDragged:mouse_event(w, NSEventTypeLeftMouseDragged, NSMakePoint(-3.5, 719.5))];
  CGEventRef ce = CGEventCreateScrollWheelEvent(NULL, kCGScrollEventUnitLine, 1, 3);
  [v scrollWheel:[NSEvent eventWithCGEvent:ce]];
  CFRelease(ce);
  ce = CGEventCreateScrollWheelEvent(NULL, kCGScrollEventUnitLine, 1, -2);
  [v scrollWheel:[NSEvent eventWithCGEvent:ce]];
  CFRelease(ce);
  got = collect(base, 8);
  CHECK(got == 8, "mouse: %d events", got);
  if (got == 8) {
    rec *r = g_rec + base;
    CHECK(r[0].code == 5 && r[0].a == 0, "left down");
    CHECK(r[1].code == 4 && r[1].a == 0, "left up");
    CHECK(r[2].code == 5 && r[2].a == 1, "right down");
    CHECK(r[3].code == 4 && r[3].a == 1, "right up");
    CHECK(r[4].code == 3 && r[4].a == 100 && r[4].b == 201, "move %d,%d", r[4].a, r[4].b);
    CHECK(r[5].code == 3 && r[5].a == -3 && r[5].b == 720, "drag %d,%d", r[5].a, r[5].b);
    CHECK(r[6].code == 6 && r[6].a == 1 && r[6].b == 1, "scroll up %d", r[6].a);
    CHECK(r[7].code == 6 && r[7].a == 0 && r[7].b == 1, "scroll down %d", r[7].a);
  }
  CHECK(v->lastInputTime > 0, "input refreshes lastInputTime");

  // --- Cmd+Shift+F. Windowed from launch, CurrentSetting is still {fs 1, 0x0}
  // from awakeFromNib, so the first press asks for windowed 0x0 and the timer
  // puts the window size back: the original's first toggle does nothing.
  atomic_store(&g_nres, 0);
  g_nfs_requests = 0;
  base = atomic_load(&g_nrec);
  [w sendEvent:key_event(w, NSEventTypeKeyDown, @"F", @"F", 3,
                         NSEventModifierFlagCommand | NSEventModifierFlagShift)];
  CHECK(atomic_load(&g_nres) >= 1 && g_res_w[0] == 0 && g_res_h[0] == 0 && mc->CurrentSetting_FullScreen == NO,
        "first toggle: SetDisplayMode(0, 0, 0) (%d calls, %dx%d)", atomic_load(&g_nres), g_res_w[0], g_res_h[0]);
  wait_for(^int { return rsp_stub()->width == 1280; }, 3);
  CHECK(rsp_stub()->width == 1280 && rsp_stub()->height == 720, "timer restores %dx%d", rsp_stub()->width,
        rsp_stub()->height);
  CHECK(collect(base, 1) == 1 && (unsigned)g_rec[base].b == (0x80000000u | 'F'), "the key still reaches the engine");
  [w sendEvent:key_event(w, NSEventTypeKeyDown, @"F", @"F", 3,
                         NSEventModifierFlagCommand | NSEventModifierFlagShift)];
  CHECK(g_nfs_requests >= 1 && g_fs_requests[0] == 1 && mc->CurrentSetting_FullScreen == YES,
        "second toggle requests full screen");
  if (g_real_fullscreen) {
    wait_for(^int { return mc->isInFullScreenMode; }, 3);
    NSSize s = [[NSScreen mainScreen] frame].size;
    CHECK(mc->isInFullScreenMode && [w styleMask] == NSWindowStyleMaskBorderless &&
              NSEqualSizes([w frame].size, s),
          "goFullScreen");
    wait_for(^int { return rsp_stub()->width == (int)s.width; }, 3);
    CHECK(rsp_stub()->width == (int)s.width, "engine at screen size %d", rsp_stub()->width);
    [w sendEvent:key_event(w, NSEventTypeKeyDown, @"F", @"F", 3,
                           NSEventModifierFlagCommand | NSEventModifierFlagShift)];
    wait_for(^int { return !mc->isInFullScreenMode; }, 3);
    CHECK(!mc->isInFullScreenMode && ([w styleMask] & 15) == 15, "goWindow restores the style");
    wait_for(^int { return rsp_stub()->width == 1280; }, 3);
  } else {
    rsp_set_display_mode(0, 1280, 720);
    mc->CurrentSetting_FullScreen = NO;
  }

  // --- resizing the window becomes the engine resolution
  [w setContentSize:NSMakeSize(1024, 576)];
  wait_for(^int { return rsp_stub()->width == 1024; }, 3);
  CHECK(rsp_stub()->width == 1024 && rsp_stub()->height == 576, "resize -> engine %dx%d", rsp_stub()->width,
        rsp_stub()->height);
  CHECK(NSEqualSizes([v frame].size, NSMakeSize(1024, 576)), "view re-seated");
  [w setContentSize:NSMakeSize(1280, 720)];
  wait_for(^int { return rsp_stub()->width == 1280; }, 3);

  // --- swap interval follows the +0x3c flag
  rsp_stub()->no_vsync = 1;
  wait_for(^int { return mc->numSwapBuffers == 0; }, 3);
  [[v openGLContext] getValues:&swap forParameter:NSOpenGLCPSwapInterval];
  CHECK(mc->numSwapBuffers == 0 && swap == 0, "no_vsync -> swap interval %d", swap);
  rsp_stub()->no_vsync = 0;
  wait_for(^int { return mc->numSwapBuffers == 1; }, 3);
  [[v openGLContext] getValues:&swap forParameter:NSOpenGLCPSwapInterval];
  CHECK(mc->numSwapBuffers == 1 && swap == 1, "back to swap interval %d", swap);

  // --- focus lost through the responder chain
  CHECK([w makeFirstResponder:nil] && !rsp_focused(), "resignFirstResponder clears focus");
  [w makeFirstResponder:v];

  // --- clipboard
  // (the general pasteboard is the user's: read-only check)
  char clip[64];
  int cl = rsp_clipboard_text(clip, sizeof clip);
  CHECK(cl >= -1, "clipboard read");

  // --- quit: Cmd+Q -> applicationShouldTerminate: cancels and asks the engine;
  // the engine stops, drawView sees it, OpenGLView dealloc -> exit.
  [NSApp terminate:nil];
  CHECK(rsp_stub()->quit_requested, "terminate: -> request_quit");
  pump(5);
  CHECK(0, "the shell did not exit after the engine shut down");
}

int main(int argc, const char **argv) {
  @autoreleasepool {
    setenv("RSP_LOG", "0", 0);
    g_real_fullscreen = getenv("RSP_TEST_FULLSCREEN") && atoi(getenv("RSP_TEST_FULLSCREEN"));
    int run_only = argc > 1 && strcmp(argv[1], "--run") == 0;

    NSString *appDir = [[[NSBundle mainBundle] bundlePath] stringByDeletingLastPathComponent];
    snprintf(g_root, sizeof g_root, "%s", [appDir fileSystemRepresentation]);

    if (!run_only) {
      // The game root next to the .app: a known Rocksmith.ini, no Tmp/ yet.
      char p[1100];
      snprintf(p, sizeof p, "%s/Tmp", g_root);
      rmdir(p);
      snprintf(p, sizeof p, "%s/Rocksmith.ini", g_root);
      FILE *f = fopen(p, "wb");
      fputs("[Audio]\nLatencyBuffer=3\n[Renderer.MacOS]\nScreenWidth=0\nScreenHeight=0\nFullscreen=0\n"
            "VisualQuality=3\n[Global]\nVersion=1\n",
            f);
      fclose(f);
      rsp_engine_hooks h = *rsp_stub_hooks();
      h.set_fullscreen = hk_set_fullscreen;
      h.set_resolution = hk_set_resolution;
      h.shutdown = hk_shutdown;
      h.gl_context = hk_gl_context;
      rsp_set_hooks(&h);
      rsp_stub()->input_cb = rec_input;
      rsp_stub()->frame_cb = frame_cb;
      rsp_set_exit(test_exit);
      dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 60 * NSEC_PER_SEC), dispatch_get_global_queue(0, 0), ^{
        printf("FAIL watchdog: test did not finish in 60 s (%d passed, %d failed)\n", g_pass, g_fail);
        fflush(stdout);
        _exit(2);
      });
    }

    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    if (run_only) {
      rsp_shell_load(NULL);
      [NSApp run];
      return 0;
    }
    rsp_shell sh;
    rsp_shell_load(&sh);
    [NSApp finishLaunching];
    run_tests(&sh);
  }
  printf("rsp_shell_test: %d passed, %d failed\n", g_pass, g_fail);
  return g_fail ? 1 : 0;
}
