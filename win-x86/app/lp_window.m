// The game's window on the host: the main thread runs AppKit, the game runs on its own thread
// (lp_main.c), and the D3D9 layer gets a Metal layer context for the window the game made its
// device on (d3d9_platform_context, the strong definition of shims/d3d9's weak one).
//
// Sizes follow the Mac build's shell (shims/platform/src/rsp_shell.m): the desktop the game sees
// (lp_screen_size: GetSystemMetrics, EnumDisplaySettings, the D3D9 display mode) is the main
// screen in points, or in backing pixels with RS_METAL_RETINA=1. The real display mode never
// changes (ChangeDisplaySettings does nothing).
//
// Window modes: Rocksmith.ini's [Renderer.Win32] Fullscreen, the game's Options > Visual Settings >
// Windowed Mode:
//   1  Non-Exclusive Fullscreen: a windowed device the size of the desktop, then a WS_POPUP style
//      and a MoveWindow over the desktop: here a borderless window over the whole screen, as the
//      Mac build's full screen (its default too);
//   2  Exclusive Fullscreen: a full-screen device of ScreenWidth x ScreenHeight (0: 1280x720) and
//      the popup style: the same borderless window, the back buffer letterboxed in it; the
//      display mode never changes;
//   0  Windowed: a windowed device of ScreenWidth x ScreenHeight (0: 1280x720), then a caption
//      style and a MoveWindow centring it: here a titled, resizable window of that size (in points
//      at the desktop's scale), shrunk to fit the screen's visible frame.
// A new device's window starts full screen for a full-screen device or a back buffer the size of
// the desktop (lp_initial_full); after that the game's style changes (lp_window_style, from
// SetWindowLong) switch the mode and its Resets (d3d9_platform_resize) the back buffer and a
// window's size, so changing the setting in the game takes effect at once. RS_FULLSCREEN=1 / 0
// forces one mode whatever the game asks; the game then renders its own resolution into it.
//
// The drawable is the back buffer; the layer scales it to the window keeping its aspect ratio
// (never cropped, letterboxed in black), so the user can resize the window, and enter and leave
// macOS full screen (the green button, View > Enter Full Screen, Control-Command-F), without the
// game noticing. In the borderless full screen Control-Command-F goes to a window instead.
//
// The window's keys and pointer go to the game (win32_input.c): DirectInput, the virtual-key state
// and the window messages; the pointer in back-buffer pixels. LP_INPUT_SCRIPT plays keys.
//
// env: LP_OFFSCREEN=1     no window: the device renders offscreen
//      RS_METAL_RETINA=1  the desktop, and so the game's resolution, in backing pixels
//      RS_FULLSCREEN=1|0  always full screen / always a window, whatever the game's setting
//      LP_WINDOW_SCRIPT   window resizes and full-screen toggles for unattended checks
#import <Cocoa/Cocoa.h>
#import <IOKit/pwr_mgt/IOPMLib.h>
#import <QuartzCore/CAMetalLayer.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "lp_screen.h"
#include "rs_gfx.h"

#ifndef LP_VERSION
#define LP_VERSION "dev"  // build.sh passes the repository's VERSION
#endif

// a borderless window takes the keyboard too
@interface LPWindow : NSWindow
@end
@implementation LPWindow
- (BOOL)canBecomeKeyWindow { return YES; }
- (BOOL)canBecomeMainWindow { return YES; }
@end

static void on_main(void (^b)(void)) {
  if ([NSThread isMainThread]) b();
  else dispatch_sync(dispatch_get_main_queue(), b);
}

static int retina(void) {
  const char *e = getenv("RS_METAL_RETINA");
  return e && *e && *e != '0';
}

// the main screen's frame and visible frame in points, and the scale the game's pixels have
static void screen_info(NSRect *frame, NSRect *visible, CGFloat *scale) {
  on_main(^{
    NSScreen *s = [NSScreen mainScreen];
    *frame = s ? s.frame : NSMakeRect(0, 0, 1920, 1080);
    *visible = s ? s.visibleFrame : *frame;
    *scale = s && retina() ? s.backingScaleFactor : 1;
  });
}

void lp_screen_size(int *w, int *h) {
  static int cw, ch;
  if (!cw) {
    NSRect f, v;
    CGFloat sc;
    screen_info(&f, &v, &sc);
    cw = (int)(f.size.width * sc);
    ch = (int)(f.size.height * sc);
  }
  *w = cw;
  *h = ch;
}

// --- the window and its modes (main thread) --------------------------------------------------
static const NSUInteger kTitled = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                  NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable;
static LPWindow *g_win;
static NSView *g_view;      // the game's content view
static uint32_t g_hwnd;     // the game's window the device is on
static int g_bw, g_bh;      // the back buffer the layer shows, aspect-fit
static int g_full;          // the borderless window over the screen (not macOS full screen)
static int g_force = -1;    // RS_FULLSCREEN
static int g_windowed = 1;  // the device being made is windowed (d3d9_platform_windowed)

static int native_full(void) { return (g_win.styleMask & NSWindowStyleMaskFullScreen) != 0; }

// the titled window's content size for the back buffer
static NSSize window_size(void) {
  NSRect frame, visible;
  CGFloat sc;
  screen_info(&frame, &visible, &sc);
  NSRect fit = [NSWindow contentRectForFrameRect:visible styleMask:kTitled];
  double w, h;
  lp_fit_window(g_bw, g_bh, sc, fit.size.width, fit.size.height, &w, &h);
  return NSMakeSize(w, h);
}

static void log_mode(const char *why) {
  NSSize c = g_view.bounds.size;
  fprintf(stderr, "lp: window %s: %s %.0fx%.0f points, back buffer %dx%d\n", why,
          native_full() ? "macOS full screen" : g_full ? "full screen" : "windowed", c.width, c.height, g_bw, g_bh);
}

static void go_full(void) {
  if (g_full || native_full()) return;  // macOS full screen is full screen already
  NSRect frame, visible;
  CGFloat sc;
  screen_info(&frame, &visible, &sc);
  g_win.styleMask = NSWindowStyleMaskBorderless;
  g_win.collectionBehavior = NSWindowCollectionBehaviorFullScreenNone;
  g_win.level = CGWindowLevelForKey(kCGMainMenuWindowLevelKey) + 1;
  g_win.hidesOnDeactivate = YES;
  [g_win setFrame:frame display:YES];
  g_full = 1;
}

// a titled window; resize: (again) the back buffer's size
static void go_window(int resize) {
  if (native_full()) {
    [g_win toggleFullScreen:nil];  // back to the window it was
    return;
  }
  if (g_full) {
    g_win.styleMask = kTitled;
    g_win.level = NSNormalWindowLevel;
    g_win.hidesOnDeactivate = NO;
    g_full = 0;
    resize = 1;
  }
  g_win.collectionBehavior = NSWindowCollectionBehaviorFullScreenPrimary;
  if (resize) {
    [g_win setContentSize:window_size()];
    [g_win center];
  }
}

static void bring_front(void) {
  [g_win makeKeyAndOrderFront:nil];
  [g_win makeFirstResponder:g_view];
  [NSApp activateIgnoringOtherApps:YES];
}

// The game set its window's style (win32_user32.c's SetWindowLong): popup is its full screen.
void lp_window_style(uint32_t hwnd, uint32_t style) {
  int full = lp_style_full(style);
  dispatch_async(dispatch_get_main_queue(), ^{
    if (!g_win || hwnd != g_hwnd || g_force >= 0) return;
    if (full == (g_full || native_full())) return;
    if (full) go_full();
    else go_window(1);
    bring_front();
    log_mode("style");
  });
}

void d3d9_platform_windowed(int windowed) { g_windowed = windowed; }

// The game reset its device (d3d9_device.c's Reset) to a w x h back buffer.
void d3d9_platform_resize(uint32_t hwnd, int w, int h) {
  dispatch_async(dispatch_get_main_queue(), ^{
    if (!g_win || (w == g_bw && h == g_bh)) return;
    g_bw = w;
    g_bh = h;
    if (!g_full && !native_full()) go_window(1);
    log_mode("reset");
  });
}

// LP_WINDOW_SCRIPT, for unattended checks: comma-separated "seconds:action" steps, the seconds
// after the window appeared: WxH sets the window's content size in points, "toggle" is
// Control-Command-F. E.g. LP_WINDOW_SCRIPT=20:900x700,30:toggle,40:toggle
static void window_script(void);

// Control-Command-F and View > Enter Full Screen: the borderless full screen goes to a window,
// a window in and out of macOS full screen. Command-Q and the close button ask the game to quit, as
// the Mac edition does (rsp_request_quit): WM_CLOSE, which its window procedure takes like the
// close button on Windows, so the game shuts down by its own path; the process ends by itself
// when it has. A game that does not answer (a screen that holds the message loop) goes after
// LP_QUIT_WAIT seconds.
#define LP_QUIT_WAIT 8
void wa_default_changed(void);  // the system's default output changed (wa_com.c)
void d3d9_set_hidden_period(uint64_t ns);  // d3d9_device.c
void w32_input_release_all(void);
void w32_input_focus(int active);
void w32_post_input(uint32_t msg, uint32_t wp, uint32_t lp);
@interface LPController : NSObject <NSWindowDelegate>
@end
@implementation LPController
- (void)toggleFullScreen:(id)sender {
  if (g_full) {
    go_window(1);
    bring_front();
  } else {
    [g_win toggleFullScreen:sender];
  }
}
- (void)quit:(id)sender {
  fprintf(stderr, "lp: quit from the window\n");
  w32_post_input(0x10, 0, 0);  // WM_CLOSE
  dispatch_after(dispatch_time(DISPATCH_TIME_NOW, LP_QUIT_WAIT * NSEC_PER_SEC), dispatch_get_main_queue(), ^{
    fprintf(stderr, "lp: the game did not quit in %d s\n", LP_QUIT_WAIT);
    rs_gfx_flush_cache(2.0);
    fflush(0);
    _exit(0);
  });
}
- (BOOL)windowShouldClose:(NSWindow *)w {
  [self quit:w];
  return NO;
}
// Losing the keyboard, as the Mac edition's applicationWillResignActive (rsp_set_focus): every key
// goes up here, since its key-up would go to the application in front, and the game is told.
- (void)windowDidResignKey:(NSNotification *)n {
  w32_input_release_all();
  w32_input_focus(0);
}
- (void)windowDidBecomeKey:(NSNotification *)n { w32_input_focus(1); }
// Hidden (another app in front of the full-screen window, which hides on deactivate; minimised;
// covered) the layer paces nothing, so Present keeps to the display's rate itself.
- (void)windowDidChangeOcclusionState:(NSNotification *)n {
  static NSInteger hz = 60;  // a hidden window can have no screen: the rate it last showed on
  NSInteger now = g_win.screen.maximumFramesPerSecond;
  if (now > 0) hz = now;
  int shown = (g_win.occlusionState & NSWindowOcclusionStateVisible) != 0;
  d3d9_set_hidden_period(shown ? 0 : 1000000000ull / (uint64_t)hz);
  fprintf(stderr, "lp: the window is %s\n", shown ? "visible" : "hidden");
}
- (void)windowDidEnterFullScreen:(NSNotification *)n { log_mode("macOS"); }
- (void)windowDidExitFullScreen:(NSNotification *)n { log_mode("macOS"); }
- (void)windowDidEndLiveResize:(NSNotification *)n { log_mode("resized"); }
@end
static LPController *g_ctl;

static void window_script(void) {
  const char *e = getenv("LP_WINDOW_SCRIPT");
  if (!e || !*e) return;
  char *s = strdup(e);
  for (char *t = strtok(s, ","); t; t = strtok(NULL, ",")) {
    double at = 0;
    char act[64] = "";
    if (sscanf(t, "%lf:%63s", &at, act) != 2) continue;
    NSString *a = [NSString stringWithUTF8String:act];
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(at * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
      int w, h;
      if ([a isEqualToString:@"toggle"]) [g_ctl toggleFullScreen:nil];
      else if ([a isEqualToString:@"quit"]) [g_ctl quit:nil];
      else if ([a isEqualToString:@"audio"]) wa_default_changed();
      else if ([a isEqualToString:@"unfocus"]) [g_ctl windowDidResignKey:nil];
      else if ([a isEqualToString:@"focus"]) [g_ctl windowDidBecomeKey:nil];
      else if (sscanf(a.UTF8String, "%dx%d", &w, &h) == 2) [g_win setContentSize:NSMakeSize(w, h)];
      fprintf(stderr, "lp: window script [%.0f] %s\n", at, a.UTF8String);
      dispatch_after(dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC), dispatch_get_main_queue(), ^{ log_mode("script"); });
    });
  }
  free(s);
}

static void make_menu(void) {
  NSMenu *bar = [[NSMenu alloc] initWithTitle:@""];
  NSString *name = @"Rocksmith 2014 Learn & Play";
  NSMenuItem *app = [[NSMenuItem alloc] initWithTitle:name action:NULL keyEquivalent:@""];
  NSMenu *am = [[NSMenu alloc] initWithTitle:name];
  NSMenuItem *q = [am addItemWithTitle:[@"Quit " stringByAppendingString:name] action:@selector(quit:) keyEquivalent:@"q"];
  q.target = g_ctl;
  app.submenu = am;
  [bar addItem:app];
  NSMenuItem *view = [[NSMenuItem alloc] initWithTitle:@"View" action:NULL keyEquivalent:@""];
  NSMenu *vm = [[NSMenu alloc] initWithTitle:@"View"];
  NSMenuItem *f = [vm addItemWithTitle:@"Enter Full Screen" action:@selector(toggleFullScreen:) keyEquivalent:@"f"];
  f.keyEquivalentModifierMask = NSEventModifierFlagControl | NSEventModifierFlagCommand;
  f.target = g_ctl;
  view.submenu = vm;
  [bar addItem:view];
  NSApp.mainMenu = bar;
}

// --- input: the window's keys and pointer to the game (win32_input.c) ------------------------
void w32_input_key(int mac_keycode, int down, uint32_t ch);
void w32_input_mouse_move(int32_t x, int32_t y, int32_t dx, int32_t dy);
void w32_input_mouse_wheel(int32_t delta);
void w32_input_mouse_button(int button, int down);
void w32_input_view_height(int32_t h);
void w32_input_script(void);

// a point in the view (points, origin bottom left) -> back-buffer pixels, and the scale between
static void to_pixels(NSPoint p, int32_t *x, int32_t *y, CGFloat *k) {
  NSSize v = g_view.bounds.size;
  double s;
  lp_view_to_pixels(p.x, v.height - p.y, v.width, v.height, g_bw, g_bh, x, y, &s);
  *k = s;
}

// Whether a modifier key is down, from the event's flags: the left and right keys have device bits
// of their own (IOLLEvent.h). The flags stay right when the key's other edge went to another
// application, as Command-Tab's does.
static int mod_key_down(int key, NSEventModifierFlags f) {
  switch (key) {
    case 0x36: return (f & 0x10) != 0;  // right Command
    case 0x37: return (f & 0x08) != 0;  // left Command
    case 0x38: return (f & 0x02) != 0;  // left Shift
    case 0x3c: return (f & 0x04) != 0;  // right Shift
    case 0x3a: return (f & 0x20) != 0;  // left Option
    case 0x3d: return (f & 0x40) != 0;  // right Option
    case 0x3b: return (f & 0x01) != 0;  // left Control
    case 0x3e: return (f & 0x2000) != 0;  // right Control
    case 0x39: return (f & NSEventModifierFlagCapsLock) != 0;
  }
  return 0;
}

static void input_monitor(void) {
  NSEventMask m = NSEventMaskKeyDown | NSEventMaskKeyUp | NSEventMaskFlagsChanged | NSEventMaskMouseMoved |
                  NSEventMaskLeftMouseDragged | NSEventMaskRightMouseDragged | NSEventMaskOtherMouseDragged |
                  NSEventMaskLeftMouseDown | NSEventMaskLeftMouseUp | NSEventMaskRightMouseDown |
                  NSEventMaskRightMouseUp | NSEventMaskOtherMouseDown | NSEventMaskOtherMouseUp |
                  NSEventMaskScrollWheel;
  [NSEvent addLocalMonitorForEventsMatchingMask:m handler:^NSEvent *(NSEvent *e) {
    if (!g_view || e.window != g_view.window) return e;
    switch (e.type) {
      case NSEventTypeKeyDown:
      case NSEventTypeKeyUp: {
        if (e.modifierFlags & NSEventModifierFlagCommand) {  // the system's shortcuts, the menu
          if (e.type == NSEventTypeKeyUp) w32_input_key(e.keyCode, 0, 0);  // pressed before Command went down
          return e;
        }
        NSString *s = e.characters;
        uint32_t ch = s.length ? [s characterAtIndex:0] : 0;
        if (ch == '\n' || ch == 3) ch = '\r';  // Return / Enter types CR on Windows
        if (ch == 0x7f) ch = 8;                // Delete is Backspace
        w32_input_key(e.keyCode, e.type == NSEventTypeKeyDown, ch);
        return nil;  // no beep
      }
      case NSEventTypeFlagsChanged: {  // a modifier went down or up: the key code says which
        int k = e.keyCode & 127;
        w32_input_key(k, mod_key_down(k, e.modifierFlags), 0);
        return e;
      }
      case NSEventTypeScrollWheel:
        if (e.scrollingDeltaY) w32_input_mouse_wheel((int32_t)(e.scrollingDeltaY > 0 ? 120 : -120));
        return e;
      default: {
        NSPoint p = [g_view convertPoint:e.locationInWindow fromView:nil];
        int inside = NSPointInRect(p, g_view.bounds);
        int32_t x, y;
        CGFloat k;
        to_pixels(p, &x, &y, &k);
        w32_input_view_height(g_bh);
        switch (e.type) {
          // a press on the title bar or the frame is the window's (move, resize), not the game's
          case NSEventTypeLeftMouseDown: if (inside) w32_input_mouse_button(0, 1); break;
          case NSEventTypeLeftMouseUp: w32_input_mouse_button(0, 0); break;
          case NSEventTypeRightMouseDown: if (inside) w32_input_mouse_button(1, 1); break;
          case NSEventTypeRightMouseUp: w32_input_mouse_button(1, 0); break;
          case NSEventTypeOtherMouseDown: if (inside) w32_input_mouse_button(2, 1); break;
          case NSEventTypeOtherMouseUp: w32_input_mouse_button(2, 0); break;
          default: w32_input_mouse_move(x, y, (int32_t)lround(e.deltaX / k), (int32_t)lround(e.deltaY / k)); break;
        }
        return e;
      }
    }
  }];
}

rs_gfx_context *d3d9_platform_context(uint32_t hwnd, int w, int h) {
  const char *off = getenv("LP_OFFSCREEN");
  if (off && *off && *off != '0') return rs_gfx_create_offscreen(w, h);
  int dw, dh;
  lp_screen_size(&dw, &dh);
  g_force = lp_force_mode(getenv("RS_FULLSCREEN"));
  int full = lp_initial_full(g_force, g_windowed, w, h, dw, dh);
  __block CAMetalLayer *layer = nil;
  on_main(^{
    if (!g_win) {
      if (!g_ctl) g_ctl = [LPController new];
      g_win = [[LPWindow alloc] initWithContentRect:NSMakeRect(0, 0, 640, 360)
                                          styleMask:kTitled
                                            backing:NSBackingStoreBuffered
                                              defer:NO];
      g_win.releasedWhenClosed = NO;
      g_win.opaque = YES;
      g_win.backgroundColor = NSColor.blackColor;
      g_win.acceptsMouseMovedEvents = YES;
      g_win.contentMinSize = NSMakeSize(320, 180);
      g_win.delegate = g_ctl;
      g_win.title = @"Rocksmith 2014 Learn & Play Re-ARMed " LP_VERSION;
      // No screen saver or display sleep while the game is open, in a window or full screen, as the Mac
      // game's view does (rsp_shell.m). Held until the process ends.
      IOPMAssertionID awake;
      IOPMAssertionCreateWithName(kIOPMAssertionTypeNoDisplaySleep, kIOPMAssertionLevelOn,
                                  CFSTR("Rocksmith 2014 is preventing your Mac from sleeping."), &awake);
      input_monitor();
      window_script();
    }
    NSView *v = g_win.contentView;
    layer = [CAMetalLayer layer];
    layer.frame = v.bounds;
    layer.autoresizingMask = kCALayerWidthSizable | kCALayerHeightSizable;
    layer.contentsScale = g_win.backingScaleFactor;
    layer.contentsGravity = kCAGravityResizeAspect;  // a back buffer of another shape: letterboxed
    layer.backgroundColor = CGColorGetConstantColor(kCGColorBlack);
    layer.drawableSize = CGSizeMake(w, h);
    v.wantsLayer = YES;
    v.layer = layer;
    g_view = v;
    g_hwnd = hwnd;
    g_bw = w;
    g_bh = h;
    if (full) go_full();
    else go_window(1);
    bring_front();
  });
  w32_input_script();
  rs_gfx_context *ctx = rs_gfx_create_layer((__bridge void *)layer, w, h);
  __block NSSize content;
  on_main(^{ content = g_view.bounds.size; });
  fprintf(stderr, "lp: window for HWND 0x%x: %s %.0fx%.0f points, back buffer %dx%d (desktop %dx%d%s): %s\n", hwnd,
          full ? "full screen" : "windowed", content.width, content.height, w, h, dw, dh,
          g_force < 0 ? "" : g_force ? ", RS_FULLSCREEN=1" : ", RS_FULLSCREEN=0",
          ctx ? "Metal layer" : "no layer context, offscreen");
  return ctx ? ctx : rs_gfx_create_offscreen(w, h);
}

static id g_activity;  // the process' activity assertion (no App Nap), never ended

// The main thread's run loop (never returns: lp_main exits when the game's thread ends).
void lp_window_loop(void) {
  @autoreleasepool {
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    if (!g_ctl) g_ctl = [LPController new];
    make_menu();
    // a game: no App Nap, no timer coalescing, latency matters (held for the process' life)
    g_activity = [[NSProcessInfo processInfo]
        beginActivityWithOptions:NSActivityUserInitiated | NSActivityLatencyCritical
                          reason:@"game"];
    [NSApp run];
  }
}
