// Platform shell rewrite: AppDelegate, OpenGLView, MainController,
// MessageBox, the nib's object graph and main(). Each method behaves like the
// game's method at the address in its comment (unslid, decrypted binary). MRC.
#import "rsp_shell.h"
#import "rsp_present.h"

#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <dlfcn.h>
#include <stdatomic.h>
#include <pthread.h>
#include <mach/mach_time.h>
#include <stdlib.h>

NSOpenGLContext *rsp_g_gl_context;
MainController *rsp_g_main_controller;

// Metal presentation (not in the original; rsp_present.h): set when libRSOpenGL's backend is Metal
// (RS_GL_BACKEND=metal), NULL on GL. One view per process, like the original. Where a method has a
// GL and a Metal branch, the GL one is the original's code.
static rsp_mtl *rsp_g_mtl;

// ===========================================================================
@implementation AppDelegate
@synthesize window;

- (void)dealloc {  // 0x10017a7c0
  [super dealloc];
}
- (void)applicationDidFinishLaunching:(NSNotification *)n {  // 0x10017a7f0
}
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)app {  // 0x10017a800
  // Cmd+Q / Quit: ask the engine to shut down, and let it exit by itself.
  rsp_request_quit();
  return NSTerminateCancel;
}
- (void)applicationWillHide:(NSNotification *)n {  // 0x10017a810
}
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)app {  // 0x10017a820
  return YES;
}
- (void)applicationDidBecomeActive:(NSNotification *)n {  // 0x10017a830
  rsp_set_focus(1);
}
- (void)applicationWillResignActive:(NSNotification *)n {  // 0x10017a840
  rsp_set_focus(0);
}
- (void)applicationWillTerminate:(NSNotification *)n {  // 0x10017a850
}
- (void)windowWillClose:(NSNotification *)n {  // 0x10017a860
  rsp_request_quit();
}
- (void)windowDidBecomeKey:(NSNotification *)n {  // 0x10017a870
}
@end

// ===========================================================================
// Frame-time hook (not in the original): with RS_GL_FRAMETIMES set and
// libRSOpenGL/libglspy loaded, drawView reports when it got the CGL lock and when
// rsp_game_frame returned, as a 'G' event in that library's frame-time log
// (shims/opengl/src/nsgl_hook.m). Otherwise one load
// and branch per frame. On Metal there is no flushBuffer for that library to swizzle (and so it
// never times drawView / getFrameForTime: either): the shell reports 'F' (rs_gfx_present), 'D'
// (drawView) and 'V' (getFrameForTime:) itself, with the same meaning.
typedef void (*rsp_ft_mark_t)(int kind, uint64_t t0, uint64_t t1, uint64_t x, unsigned flags);
static rsp_ft_mark_t rsp_ft_mark_fn(void) {
  static rsp_ft_mark_t fn;
  static int state;  // 0 not looked up, 1 looked up (display-link thread; idempotent)
  if (!state) {
    const char *e = getenv("RS_GL_FRAMETIMES");
    if (e && *e) fn = (rsp_ft_mark_t)dlsym(RTLD_DEFAULT, "rs_gl_frametime_mark");
    state = 1;
  }
  return fn;
}

// Render thread (not in the original; RS_RENDER_THREAD=0 restores the original): the display-link
// callback only signals a thread of ours, which runs getFrameForTime:. In the original the frame runs inside the
// callback, and CVDisplayLink drops the next tick when a callback overruns the refresh period: a
// frame whose flushBuffer waits on the WindowServer (~10 ms now and then) then costs a whole
// refresh. Here the missed tick stays pending and the next frame starts at once; the display link
// targets a vsync ~24 ms ahead, so it is usually still on time. Ticks that pile up while a frame
// runs collapse into one frame. Measured (Peace of Mind, 120 s): refreshes without a new frame
// 13 -> 0-1, intervals > 25 ms 13 -> 1-2.
static dispatch_semaphore_t rsp_rt_sem;
static pthread_mutex_t rsp_rt_mu = PTHREAD_MUTEX_INITIALIZER;
static CVTimeStamp rsp_rt_ts;
// Held while the render thread runs a frame (recursive: dealloc stops the link from inside a frame).
// Stopping the display link takes it once, so no frame runs after CVDisplayLinkStop returns, as with
// the callback itself.
static pthread_mutex_t rsp_rt_frame_mu = PTHREAD_RECURSIVE_MUTEX_INITIALIZER;

static void rsp_render_thread_sync(void) {
  pthread_mutex_lock(&rsp_rt_frame_mu);
  pthread_mutex_unlock(&rsp_rt_frame_mu);
}

static int rsp_render_thread_failed;  // pthread_create failed: frames run in the callback

static int rsp_render_thread_on(void) {
  if (rsp_render_thread_failed) return 0;
  static int v = -1;
  if (v < 0) {
    const char *e = getenv("RS_RENDER_THREAD");
    v = !(e && *e && atoi(e) == 0);
  }
  return v;
}

// RS_FPS=N (not in the original; a test switch): the render thread starts a frame every 1/N s on
// its own clock instead of at each display-link tick, and the swap interval is 0, so the game can
// be run at another rate than the display's (e.g. 120 on a 60 Hz screen). 0 or unset: the display.
static int rsp_fps(void) {
  static int v = -1;
  if (v < 0) {
    const char *e = getenv("RS_FPS");
    v = e && *e ? atoi(e) : 0;
    if (v < 0 || v > 1000) v = 0;
  }
  return v;
}

static void rsp_fps_wait(void) {
  static uint64_t next, period;
  if (!period) {
    mach_timebase_info_data_t tb;
    mach_timebase_info(&tb);
    period = (uint64_t)(1e9 / rsp_fps()) * tb.denom / tb.numer;
    next = mach_absolute_time();
  }
  next += period;
  uint64_t now = mach_absolute_time();
  if (next < now) next = now;  // behind: start at once, don't queue up missed frames
  else mach_wait_until(next);
}

static void *rsp_render_thread(void *ctx) {
  pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
  for (;;) {
    if (rsp_fps()) rsp_fps_wait();
    else dispatch_semaphore_wait(rsp_rt_sem, DISPATCH_TIME_FOREVER);
    while (dispatch_semaphore_wait(rsp_rt_sem, DISPATCH_TIME_NOW) == 0) {
    }
    CVTimeStamp ts;
    pthread_mutex_lock(&rsp_rt_mu);
    ts = rsp_rt_ts;
    pthread_mutex_unlock(&rsp_rt_mu);
    OpenGLView *v = (OpenGLView *)ctx;
    pthread_mutex_lock(&rsp_rt_frame_mu);
    if (v->displayLink && CVDisplayLinkIsRunning(v->displayLink)) [v getFrameForTime:&ts];
    pthread_mutex_unlock(&rsp_rt_frame_mu);
  }
  return NULL;
}

static CVReturn rsp_display_link_cb(CVDisplayLinkRef link, const CVTimeStamp *now,
                                    const CVTimeStamp *outputTime, CVOptionFlags flagsIn,
                                    CVOptionFlags *flagsOut, void *ctx) {  // 0x1001843e0
  if (!rsp_render_thread_on()) return [(OpenGLView *)ctx getFrameForTime:outputTime];
  static dispatch_once_t once;
  dispatch_once(&once, ^{
    rsp_rt_sem = dispatch_semaphore_create(0);
    pthread_attr_t at;
    pthread_attr_init(&at);
    pthread_attr_setstacksize(&at, 8 << 20);
    pthread_t t;
    if (pthread_create(&t, &at, rsp_render_thread, ctx) != 0) {
      NSLog(@"render thread: pthread_create failed; frames run in the display-link callback");
      rsp_render_thread_failed = 1;
    }
    pthread_attr_destroy(&at);
  });
  if (rsp_render_thread_failed) return [(OpenGLView *)ctx getFrameForTime:outputTime];
  pthread_mutex_lock(&rsp_rt_mu);
  rsp_rt_ts = *outputTime;
  pthread_mutex_unlock(&rsp_rt_mu);
  dispatch_semaphore_signal(rsp_rt_sem);
  return kCVReturnSuccess;
}

@implementation OpenGLView

- (NSOpenGLContext *)openGLContext {  // 0x1001841f0
  return openGLContext;
}
- (NSOpenGLPixelFormat *)pixelFormat {  // 0x100184210
  return pixelFormat;
}
- (void)setMainController:(MainController *)c {  // 0x100184230
  controller = c;
}
- (void)setIsFullScreen:(BOOL)fs {  // 0x100184250
  isFullScreen = fs;
}

// Display-link thread, once per refresh.
- (CVReturn)getFrameForTime:(const CVTimeStamp *)outputTime {  // 0x100184260
  uint64_t v0 = rsp_g_mtl && rsp_ft_mark_fn() ? mach_absolute_time() : 0;
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  if (cursorInWindow) {
    double now = CFAbsoluteTimeGetCurrent();
    [controller setRenderTime:now];
    int visible = cursorVisible;
    int act = rsp_cursor_policy(now, lastInputTime, &visible);
    if (act < 0) CGDisplayHideCursor(CGMainDisplayID());
    if (act > 0) CGDisplayShowCursor(CGMainDisplayID());
    cursorVisible = (BOOL)visible;
  }
  [self drawView];
  [pool release];
  if (v0) rsp_ft_mark_fn()('V', v0, mach_absolute_time(), outputTime ? outputTime->hostTime : 0, 0);
  return kCVReturnSuccess;
}

- (void)setupDisplayLink {  // 0x100184350
  CVDisplayLinkCreateWithActiveCGDisplays(&displayLink);
  CVDisplayLinkSetOutputCallback(displayLink, rsp_display_link_cb, self);
  if (rsp_g_mtl) {  // the window's display (the main display until the view is in a window)
    CVDisplayLinkSetCurrentCGDisplay(displayLink, rsp_mtl_display(self));
    return;
  }
  CVDisplayLinkSetCurrentCGDisplayFromOpenGLContext(
      displayLink, (CGLContextObj)[[self openGLContext] CGLContextObj],
      (CGLPixelFormatObj)[[self pixelFormat] CGLPixelFormatObj]);
}

- (id)initWithFrame:(NSRect)frame shareContext:(NSOpenGLContext *)share {  // 0x100184400
  // These four stores happen before [super initWithFrame:] in the original.
  isFullScreen = NO;
  cursorInWindow = YES;
  done = NO;
  lastInputTime = [controller renderTime];  // controller is nil here: 0.0
  self = [super initWithFrame:frame];
  if (!self) return nil;
  if (rsp_mtl_selected()) {
    [self rsp_initMetal];
    return self;
  }
  // Accelerated NoRecovery DoubleBuffer ColorSize 24 DepthSize 16 StencilSize 8,
  // no NSOpenGLPFAOpenGLProfile: a legacy (2.1) context.
  NSOpenGLPixelFormatAttribute attrs[] = {NSOpenGLPFAAccelerated,
                                          NSOpenGLPFANoRecovery,
                                          NSOpenGLPFADoubleBuffer,
                                          NSOpenGLPFAColorSize,
                                          24,
                                          NSOpenGLPFADepthSize,
                                          16,
                                          NSOpenGLPFAStencilSize,
                                          8,
                                          0};
  // Not in the original: the app is high-resolution capable (Info.plist, for Metal's opt-in
  // RS_METAL_RETINA); GL keeps a point-sized surface, which the engine's resolution matches.
  [self setWantsBestResolutionOpenGLSurface:NO];
  pixelFormat = [[NSOpenGLPixelFormat alloc] initWithAttributes:attrs];
  if (!pixelFormat) NSLog(@"No OpenGL pixel format");
  openGLContext = [[NSOpenGLContext alloc] initWithFormat:pixelFormat shareContext:share];
  if (![openGLContext view]) [openGLContext setView:self];
  [[self openGLContext] makeCurrentContext];
  GLint swap = rsp_fps() ? 0 : 1;
  [[self openGLContext] setValues:&swap forParameter:NSOpenGLCPSwapInterval];
  // RS_GL_MTENGINE=1 (not in the original): Apple's multithreaded GL engine, which moves the
  // driver's work for this context to a thread of its own.
  const char *mt = getenv("RS_GL_MTENGINE");
  if (mt && *mt && atoi(mt) != 0) {
    CGLError err = CGLEnable((CGLContextObj)[openGLContext CGLContextObj], kCGLCEMPEngine);
    NSLog(@"multithreaded GL engine: %s", err == kCGLNoError ? "on" : CGLErrorString(err));
  }
  [self setupDisplayLink];
  [[NSNotificationCenter defaultCenter] addObserver:self
                                           selector:@selector(reshape)
                                               name:NSViewGlobalFrameDidChangeNotification
                                             object:self];
  // 0x43 = MouseEnteredAndExited | MouseMoved | ActiveInActiveApp
  trackingArea = [[NSTrackingArea alloc]
      initWithRect:frame
           options:NSTrackingMouseEnteredAndExited | NSTrackingMouseMoved | NSTrackingActiveInActiveApp
             owner:self
          userInfo:nil];
  [self addTrackingArea:trackingArea];
  sleepDisableSuccess = IOPMAssertionCreateWithName(
      kIOPMAssertionTypeNoDisplaySleep, kIOPMAssertionLevelOn,
      CFSTR("Rocksmith 2014 is preventing your Mac from sleeping."), &sleepAssertionID);
  rsp_g_gl_context = openGLContext;
  const rsp_engine_hooks *h = rsp_hooks();
  if (h->gl_context) h->gl_context(h->ctx, openGLContext);
  // _window is still nil when the nib makes the view, so this is a no-op there.
  [[self window] setAspectRatio:NSMakeSize(16.0, 9.0)];
  return self;
}

// The rest of initWithFrame:shareContext: on Metal: the same steps, with a CAMetalLayer and an rs_gfx
// context in place of the pixel format and the NSOpenGLContext.
- (void)rsp_initMetal {
  GLint swap = rsp_fps() ? 0 : 1;
  rsp_g_mtl = rsp_mtl_attach(self, swap);
  rsp_mtl_make_current(rsp_g_mtl);
  if (getenv("RS_GL_MTENGINE")) NSLog(@"RS_GL_MTENGINE: OpenGL only, ignored on Metal");
  [self setupDisplayLink];
  [[NSNotificationCenter defaultCenter] addObserver:self
                                           selector:@selector(reshape)
                                               name:NSViewFrameDidChangeNotification
                                             object:self];
  trackingArea = [[NSTrackingArea alloc]
      initWithRect:[self frame]
           options:NSTrackingMouseEnteredAndExited | NSTrackingMouseMoved | NSTrackingActiveInActiveApp
             owner:self
          userInfo:nil];
  [self addTrackingArea:trackingArea];
  sleepDisableSuccess = IOPMAssertionCreateWithName(
      kIOPMAssertionTypeNoDisplaySleep, kIOPMAssertionLevelOn,
      CFSTR("Rocksmith 2014 is preventing your Mac from sleeping."), &sleepAssertionID);
  // The game's g_glContext (0x1018e7488) is read once, by GRGLRenderer::Initialize, which copies it
  // to a renderer global (0x101920b48) that nothing reads; the game's own CGL calls are all in the
  // shell. So on Metal the game gets nil, and rsp_g_gl_context is nil. Code that needs a
  // CGLContextObj (CGLLockContext etc.) gets libRSOpenGL's token from CGLGetCurrentContext() or
  // rsp_mtl_cgl().
  rsp_g_gl_context = nil;
  const rsp_engine_hooks *h = rsp_hooks();
  if (h->gl_context) h->gl_context(h->ctx, NULL);
  [[self window] setAspectRatio:NSMakeSize(16.0, 9.0)];
}

// Not in the original: on Metal, follow the window's display and backing scale.
- (void)viewDidMoveToWindow {
  [super viewDidMoveToWindow];
  if (!rsp_g_mtl) return;
  [[NSNotificationCenter defaultCenter] removeObserver:self name:NSWindowDidChangeScreenNotification object:nil];
  if ([self window])
    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(rsp_screenChanged:)
                                                 name:NSWindowDidChangeScreenNotification
                                               object:[self window]];
  [self rsp_screenChanged:nil];
}
- (void)rsp_screenChanged:(NSNotification *)n {
  if (displayLink) CVDisplayLinkSetCurrentCGDisplay(displayLink, rsp_mtl_display(self));
  rsp_mtl_view_resized(rsp_g_mtl, self);
  [self rsp_renderScaleChanged];
}
- (void)viewDidChangeBackingProperties {
  [super viewDidChangeBackingProperties];
  if (rsp_g_mtl) {
    rsp_mtl_view_resized(rsp_g_mtl, self);
    [self rsp_renderScaleChanged];
  }
}
// RS_METAL_RETINA=1: the engine renders at the drawable's pixel size. A new backing scale (the
// window moved to another screen) re-sends the current resolution, converted (rsp_set_display_mode).
- (void)rsp_renderScaleChanged {
  double s = rsp_mtl_scale(rsp_g_mtl);
  if (s == rsp_render_scale()) return;
  int w = rsp_screen_width(), h = rsp_screen_height();  // points, at the old scale
  rsp_set_render_scale(s);
  fprintf(stderr, "[rsp] render scale %.1f: engine %dx%d for %dx%d points\n", s, (int)lround(w * s),
          (int)lround(h * s), w, h);
  if (controller && w > 0 && h > 0) {
    controller->CurrentSetting_Width = w;
    controller->CurrentSetting_Height = h;
    controller->CurrentSetting_FullScreen = rsp_is_fullscreen();
    controller->CurrentSetting_NeedsUpdate = YES;  // applied by updateScreenSize on the frame thread
  }
}

- (id)initWithFrame:(NSRect)frame {  // 0x100184700
  return [self initWithFrame:frame shareContext:nil];
}

- (void)lockFocus {  // 0x100184740
  [super lockFocus];
  if ([[self openGLContext] view] != self) [[self openGLContext] setView:self];
}

- (void)reshape {  // 0x1001847c0
  if (rsp_g_mtl) {
    rsp_mtl_lock(rsp_g_mtl);
    rsp_mtl_view_resized(rsp_g_mtl, self);
    [self removeTrackingArea:trackingArea];
    [trackingArea release];
    trackingArea = [[NSTrackingArea alloc]
        initWithRect:[self frame]
             options:NSTrackingMouseEnteredAndExited | NSTrackingMouseMoved | NSTrackingActiveInActiveApp
               owner:self
            userInfo:nil];
    [self addTrackingArea:trackingArea];
    rsp_mtl_unlock(rsp_g_mtl);
    return;
  }
  CGLContextObj cgl = (CGLContextObj)[[self openGLContext] CGLContextObj];
  CGLLockContext(cgl);
  [[self openGLContext] update];
  [self removeTrackingArea:trackingArea];
  [trackingArea release];
  trackingArea = [[NSTrackingArea alloc]
      initWithRect:[self frame]
           options:NSTrackingMouseEnteredAndExited | NSTrackingMouseMoved | NSTrackingActiveInActiveApp
             owner:self
          userInfo:nil];
  [self addTrackingArea:trackingArea];
  CGLUnlockContext((CGLContextObj)[[self openGLContext] CGLContextObj]);
}

- (void)drawRect:(NSRect)r {  // 0x1001848f0
  if (!CVDisplayLinkIsRunning(displayLink)) [self drawView];
}

- (void)drawView {  // 0x100184930
  if (rsp_g_mtl) {
    [self rsp_drawViewMetal];
    return;
  }
  CGLContextObj cgl = (CGLContextObj)[[self openGLContext] CGLContextObj];
  CGLLockContext(cgl);
  uint64_t ft0 = rsp_ft_mark_fn() ? mach_absolute_time() : 0;
  [[self openGLContext] makeCurrentContext];
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);  // 0x4500
  [controller updateScreenSize];
  done = (BOOL)rsp_game_frame();
  if (ft0) rsp_ft_mark_fn()('G', ft0, mach_absolute_time(), 0, 0);
  // Not in the original: the mods' overlays (rs_overlay.h), on the frame about to be shown.
  if (!done && rsp_overlay_active()) {
    // the GL surface is point-sized unless the view asks for the best resolution
    NSSize px = [self wantsBestResolutionOpenGLSurface] ? [self convertRectToBacking:[self bounds]].size
                                                        : [self bounds].size;
    rsp_overlay_run((int)px.width, (int)px.height);
  }
  [[self openGLContext] flushBuffer];
  CGLUnlockContext((CGLContextObj)[[self openGLContext] CGLContextObj]);
  if (done) [self dealloc];  // the original's exit path (ends in _Exit(0))
}

// drawView on Metal: the same frame, through rs_gfx.
- (void)rsp_drawViewMetal {
  rsp_mtl *m = rsp_g_mtl;
  uint64_t d0 = rsp_ft_mark_fn() ? mach_absolute_time() : 0;
  rsp_mtl_lock(m);
  uint64_t ft0 = d0 ? mach_absolute_time() : 0;
  rsp_mtl_make_current(m);
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
  [controller updateScreenSize];
  done = (BOOL)rsp_game_frame();
  if (ft0) rsp_ft_mark_fn()('G', ft0, mach_absolute_time(), 0, 0);
  if (!done && rsp_overlay_active()) {
    int w, h;
    rsp_mtl_size(m, &w, &h);  // the drawable: points, or pixels with RS_METAL_RETINA=1
    rsp_overlay_run(w, h);
  }
  uint64_t f0 = d0 ? mach_absolute_time() : 0;
  rsp_mtl_present(m);
  if (f0) rsp_ft_mark_fn()('F', f0, mach_absolute_time(), 0, 0);
  rsp_mtl_unlock(m);
  if (d0) rsp_ft_mark_fn()('D', d0, mach_absolute_time(), 0, 0);
  if (done) [self dealloc];
}

- (BOOL)acceptsFirstResponder {  // 0x100184a30
  return !done;
}
- (BOOL)becomeFirstResponder {  // 0x100184a50
  if (done) return NO;
  rsp_set_focus(1);
  return YES;
}
- (BOOL)resignFirstResponder {  // 0x100184a80
  if (done) return NO;
  rsp_set_focus(0);
  return YES;
}

- (void)flagsChanged:(NSEvent *)e {  // 0x100184ab0
  if (!controller) return;
  [controller flagsChanged:e];
  if (rsp_flags_changed_forwards([e modifierFlags])) rsp_post_input(RSP_EV_KEYDOWN, [e keyCode], 0);
}

- (void)keyDown:(NSEvent *)e {  // 0x100184b20
  if (!controller) return;
  lastInputTime = [controller renderTime];
  [controller keyDown:e];
  unichar ch = [[e characters] characterAtIndex:0];
  NSUInteger flags = [e modifierFlags];
  rsp_post_input(RSP_EV_KEYDOWN, [e keyCode], (int)rsp_pack_keydown_char(ch, flags));
}

- (void)keyUp:(NSEvent *)e {  // 0x100184be0
  if (!controller) return;
  lastInputTime = [controller renderTime];
  [controller keyUp:e];
  unichar ch = [[e characters] characterAtIndex:0];
  rsp_post_input(RSP_EV_KEYUP, [e keyCode], ch);
}

// Arrow-key action messages: swallowed (no beep).
- (void)moveUp:(id)s {  // 0x100184c80
}
- (void)moveDown:(id)s {  // 0x100184c90
}
- (void)moveLeft:(id)s {  // 0x100184ca0
}
- (void)moveRight:(id)s {  // 0x100184cb0
}

- (void)rsp_button:(NSEvent *)e type:(int)type button:(int)button down:(BOOL)down {
  if (!controller) return;
  lastInputTime = [controller renderTime];
  if (displayLink && CVDisplayLinkIsRunning(displayLink)) {
    // The original forwards right-button events as mouseDown:/mouseUp: too.
    if (down) [controller mouseDown:e];
    else [controller mouseUp:e];
  }
  rsp_post_input(type, button, 0);
}
- (void)mouseDown:(NSEvent *)e {  // 0x100184cc0
  [self rsp_button:e type:RSP_EV_MOUSEDOWN button:0 down:YES];
}
- (void)mouseUp:(NSEvent *)e {  // 0x100184d50
  [self rsp_button:e type:RSP_EV_MOUSEUP button:0 down:NO];
}
- (void)rightMouseDown:(NSEvent *)e {  // 0x100184de0
  [self rsp_button:e type:RSP_EV_MOUSEDOWN button:1 down:YES];
}
- (void)rightMouseUp:(NSEvent *)e {  // 0x100184e70
  [self rsp_button:e type:RSP_EV_MOUSEUP button:1 down:NO];
}

- (void)mouseMoved:(NSEvent *)e {  // 0x100184f00
  if (!controller) return;
  lastInputTime = [controller renderTime];
  if (displayLink && CVDisplayLinkIsRunning(displayLink)) [controller mouseMoved:e];
}
- (void)mouseDragged:(NSEvent *)e {  // 0x100184f80
  [self mouseMoved:e];
}
- (void)mouseEntered:(NSEvent *)e {  // 0x100184fa0
  if (!controller) return;
  lastInputTime = [controller renderTime];
  cursorInWindow = YES;
  [[self window] setAcceptsMouseMovedEvents:YES];
}
- (void)mouseExited:(NSEvent *)e {  // 0x100185010
  if (!controller) return;
  lastInputTime = [controller renderTime];
  cursorInWindow = NO;
  [[self window] setAcceptsMouseMovedEvents:NO];
}
- (void)scrollWheel:(NSEvent *)e {  // 0x100185080
  if (!controller) return;
  lastInputTime = [controller renderTime];
  int a;
  if (rsp_scroll_event([e deltaY], &a)) rsp_post_input(RSP_EV_SCROLL, a, 1);
}

- (void)startAnimation {  // 0x100185110
  if (displayLink && !CVDisplayLinkIsRunning(displayLink)) {
    CVDisplayLinkStart(displayLink);
    if (isFullScreen) [[self window] setAcceptsMouseMovedEvents:YES];
  }
}
- (void)stopAnimation {  // 0x100185180
  if (displayLink && CVDisplayLinkIsRunning(displayLink)) {
    CVDisplayLinkStop(displayLink);
    rsp_render_thread_sync();
    if (isFullScreen) [[self window] setAcceptsMouseMovedEvents:YES];  // sic: YES
  }
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wobjc-missing-super-calls"
- (void)dealloc {  // 0x1001851f0
  if (controller) {
    MainController *c = controller;
    controller = nil;
    [c dealloc];
  }
  if (sleepDisableSuccess == kIOReturnSuccess)
    sleepDisableSuccess = IOPMAssertionRelease(sleepAssertionID);
  CVDisplayLinkRef link = displayLink;
  displayLink = NULL;
  CVDisplayLinkStop(link);
  rsp_render_thread_sync();
  CVDisplayLinkRelease(link);
  [[NSNotificationCenter defaultCenter] removeObserver:self
                                                  name:NSViewGlobalFrameDidChangeNotification
                                                object:self];
  if (rsp_g_mtl) [[NSNotificationCenter defaultCenter] removeObserver:self];
  rsp_exit(0);  // _Exit(0): the process ends here, [super dealloc] never runs
}
#pragma clang diagnostic pop
@end

// ===========================================================================
@implementation MainController

- (void)goFullScreen {  // 0x1001852a0
  isInFullScreenMode = YES;
  NSWindow *w = [openGLView window];
  windowStyleMask = [w styleMask];
  windowOrigin = w ? [w frame].origin : NSZeroPoint;
  windowLevel = [w level];
  NSSize screen = [NSScreen mainScreen] ? [[NSScreen mainScreen] frame].size : NSZeroSize;
  [w setStyleMask:NSWindowStyleMaskBorderless];
  [w setLevel:CGWindowLevelForKey(kCGMainMenuWindowLevelKey) + 1];
  [w setOpaque:YES];
  [w setHidesOnDeactivate:YES];
  [w setContentView:openGLView];
  [w setContentSize:screen];
  [w setFrameOrigin:NSZeroPoint];
  [openGLView setFrame:NSMakeRect(0, 0, screen.width, screen.height)];
  [openGLView setNeedsDisplay:YES];
  [w makeKeyAndOrderFront:openGLView];
  [w makeFirstResponder:openGLView];
  CurrentSetting_NeedsUpdate = YES;
  CurrentSetting_FullScreen = YES;
  CurrentSetting_Width = (int)screen.width;
  CurrentSetting_Height = (int)screen.height;
}

- (void)goWindow {  // 0x1001855a0
  isInFullScreenMode = NO;
  NSWindow *w = [openGLView window];
  [w setStyleMask:windowStyleMask];
  // windowOrigin is only saved by goFullScreen: before that it is (0,0).
  [w setFrameOrigin:windowOrigin];
  [w setLevel:windowLevel];
  [w setContentSize:NSMakeSize(1280.0, 720.0)];
  [openGLView setFrame:NSMakeRect(0, 0, 1280.0, 720.0)];
  [w setContentAspectRatio:NSMakeSize(1280.0, 720.0)];
  NSRect b = openGLView ? [openGLView bounds] : NSZeroRect;
  CurrentSetting_NeedsUpdate = YES;
  CurrentSetting_FullScreen = NO;
  CurrentSetting_Width = (int)b.size.width;
  CurrentSetting_Height = (int)b.size.height;
}

- (void)awakeFromNib {  // 0x100185760
  CurrentSetting_NeedsUpdate = NO;
  CurrentSetting_FullScreen = YES;
  CurrentSetting_Width = 0;
  CurrentSetting_Height = 0;
  numSwapBuffers = 1;
  rsp_g_main_controller = self;
  NSString *appDir = [[[NSBundle mainBundle] bundlePath] stringByDeletingLastPathComponent];
  NSString *cwd = [[NSFileManager defaultManager] currentDirectoryPath];
  rsp_boot boot = {[appDir UTF8String], [cwd UTF8String], 0, NULL};
  rsp_game_bootstrap(&boot);
  [openGLView setMainController:self];
  [[openGLView window] setShowsResizeIndicator:YES];
  int ax, ay;
  rsp_aspect_from_width(rsp_screen_width(), &ax, &ay);
  (void)rsp_screen_height();  // read and ignored, as in the original
  [[openGLView window] setContentAspectRatio:NSMakeSize(ax, ay)];
  [openGLView startAnimation];
  isAnimating = YES;
  renderTimer = [NSTimer timerWithTimeInterval:0.1
                                        target:self
                                      selector:@selector(customEventTimer:)
                                      userInfo:nil
                                       repeats:YES];
  [[NSRunLoop currentRunLoop] addTimer:renderTimer forMode:NSDefaultRunLoopMode];
}

// Set (not in the original) before the engine shuts down on the frame thread, so the main thread's
// customEventTimer: stops calling into it meanwhile.
static atomic_int rsp_quitting;

- (void)dealloc {  // 0x100185950
  atomic_store(&rsp_quitting, 1);
  rsp_game_shutdown();
  [super dealloc];
}

- (double)renderTime {  // 0x100185990
  return renderTime;
}
- (void)setRenderTime:(double)t {  // 0x1001859b0
  renderTime = t;
}
- (void)startAnimation {  // 0x1001859d0
  if (!isAnimating) {
    [openGLView startAnimation];
    isAnimating = YES;
  }
}
- (void)stopAnimation {  // 0x100185a10
  if (isAnimating) {
    [openGLView stopAnimation];
    isAnimating = NO;
  }
}
- (void)toggleAnimation {  // 0x100185a50
  if (isAnimating) [self stopAnimation];
  else [self startAnimation];
}

- (void)keyDown:(NSEvent *)e {  // 0x100185a80
  unichar ch = [[e charactersIgnoringModifiers] characterAtIndex:0];
  if (rsp_is_fullscreen_toggle(ch, [e modifierFlags])) {
    BOOL was = CurrentSetting_FullScreen;
    CurrentSetting_FullScreen = !was;
    rsp_set_display_mode(was != 1, CurrentSetting_Width, CurrentSetting_Height);
  }
}
- (void)keyUp:(NSEvent *)e {  // 0x100185b30
}

- (void)mouseMoved:(NSEvent *)e {  // 0x100185b40
  NSPoint p = [openGLView convertPoint:[e locationInWindow] fromView:nil];
  double s = rsp_render_scale();  // engine pixels per point (not in the original; 1 by default)
  rsp_post_input(RSP_EV_MOUSEMOVE, rsp_mouse_coord(p.x * s), rsp_mouse_coord(p.y * s));
}

- (void)customEventTimer:(NSTimer *)t {  // 0x100185bd0
  if (atomic_load(&rsp_quitting)) return;
  // 1. follow the engine's window mode
  int sw = rsp_timer_mode_switch(isInFullScreenMode, rsp_is_fullscreen());
  if (sw == RSP_TIMER_GO_FULLSCREEN) [self goFullScreen];
  else if (sw == RSP_TIMER_GO_WINDOW) [self goWindow];
  NSWindow *w = [openGLView window];
  if (rsp_is_fullscreen()) {
    // 2. full screen: keep the window the size of the main screen
    NSSize s = [NSScreen mainScreen] ? [[NSScreen mainScreen] frame].size : NSZeroSize;
    NSSize f = w ? [w frame].size : NSZeroSize;
    if (rsp_timer_fullscreen_refit(s.width, s.height, f.width, f.height)) [self goFullScreen];
  } else {
    // 3. windowed: a resized window becomes the engine resolution
    NSRect fr = w ? [w frame] : NSZeroRect;
    NSRect c = w ? [w contentRectForFrameRect:fr] : NSZeroRect;
    if (rsp_timer_window_adopt(c.size.width, c.size.height, rsp_screen_width(), rsp_screen_height())) {
      [w setContentView:openGLView];
      [w setContentSize:c.size];
      [openGLView setFrame:NSMakeRect(0, 0, c.size.width, c.size.height)];
      [openGLView setNeedsDisplay:YES];
      [w makeFirstResponder:openGLView];
      [w makeKeyAndOrderFront:openGLView];
      CurrentSetting_NeedsUpdate = YES;
      CurrentSetting_FullScreen = NO;
      CurrentSetting_Width = (int)c.size.width;
      CurrentSetting_Height = (int)c.size.height;
    }
  }
  // 4. swap interval
  int ns = rsp_timer_swap(rsp_no_vsync() || rsp_fps(), numSwapBuffers);
  if (ns >= 0) {
    numSwapBuffers = ns;
    GLint v = numSwapBuffers;
    if (rsp_g_mtl) rsp_mtl_set_swap_interval(rsp_g_mtl, v);
    else [[openGLView openGLContext] setValues:&v forParameter:NSOpenGLCPSwapInterval];
  }
}

// Display-link thread, from drawView, under the CGL lock.
- (void)updateScreenSize {  // 0x100185fc0
  if (CurrentSetting_NeedsUpdate) {
    rsp_set_display_mode(CurrentSetting_FullScreen, CurrentSetting_Width, CurrentSetting_Height);
    CurrentSetting_NeedsUpdate = NO;
  }
}

- (void)clientMessageBox {  // 0x100186010: "Steam is not running" (unused offline)
  [[openGLView window] setIsVisible:NO];
  NSAlert *a = [[[NSAlert alloc] init] autorelease];
  NSString *lang = [[NSLocale currentLocale] languageCode];
  NSString *msg = @"Start Steam om de game op te starten.";  // falls through to Dutch
  if ([lang caseInsensitiveCompare:@"fr"] == NSOrderedSame)
    msg = @"Vérifie que Steam est en cours d'exécution pour lancer le jeu";
  else if ([lang caseInsensitiveCompare:@"es"] == NSOrderedSame)
    msg = @"Asegúrate de estar ejecutando Steam para abrir el juego.";
  else if ([lang caseInsensitiveCompare:@"de"] == NSOrderedSame)
    msg = @"Bitte achte darauf, dass Steam läuft, um das Spiel starten zu können.";
  else if ([lang caseInsensitiveCompare:@"it"] == NSOrderedSame)
    msg = @"Assicurati che Steam sia in funzione per avviare il gioco";
  else if ([lang caseInsensitiveCompare:@"jp"] == NSOrderedSame)
    msg = @"Steamがゲーム起動のために動作中"
          @"であることを確認してください";
  else if ([lang caseInsensitiveCompare:@"nl"] != NSOrderedSame)
    msg = @"Please make sure Steam is running to launch the game";
  [a setMessageText:msg];
  [a runModal];
  rsp_request_quit();
}
@end

// ===========================================================================
@implementation MessageBox
- (void)ShowAlert:(const char *)message withDescription:(const char *)description {  // 0x10112e610
  NSString *m = [NSString stringWithUTF8String:message];
  NSString *d = [NSString stringWithUTF8String:description];
  NSAlert *a = [[[NSAlert alloc] init] autorelease];
  [a addButtonWithTitle:@"OK"];
  [a setMessageText:m];
  [a setInformativeText:d];
  [a setAlertStyle:NSAlertStyleWarning];  // 0
  [a runModal];
}
@end

// ===========================================================================
int rsp_clipboard_text(char *buf, size_t size) {  // 0x1001861d0
  NSString *s = [[NSPasteboard generalPasteboard] stringForType:NSPasteboardTypeString];
  const char *u = [s UTF8String];
  if (!u) return -1;
  size_t n = strlen(u);
  if (size) {
    size_t k = n < size ? n : size - 1;
    memcpy(buf, u, k);
    buf[k] = 0;
  }
  return (int)n;
}

void rsp_shell_load(rsp_shell *out) {
  // MainMenu.nib: NSWindowTemplate {{320, 229}, {1280, 720}}, style 15,
  // buffered, "Rocksmith 2014", min size {800, 622}, content min {800, 600};
  // content view OpenGLView {1280, 720}; connections AppDelegate.delegate/window,
  // MainController.openGLView; main menu "Quit Rocksmith 2014" (Cmd+Q, terminate:).
  NSWindow *w = [[NSWindow alloc]
      initWithContentRect:NSMakeRect(320, 229, 1280, 720)
                styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                          NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable
                  backing:NSBackingStoreBuffered
                    defer:NO];
  [w setTitle:@"Rocksmith 2014"];
  [w setMinSize:NSMakeSize(800, 622)];
  [w setContentMinSize:NSMakeSize(800, 600)];
  [w setReleasedWhenClosed:NO];
  OpenGLView *v = [[OpenGLView alloc] initWithFrame:NSMakeRect(0, 0, 1280, 720)];
  [v setAutoresizingMask:NSViewMaxXMargin | NSViewMinYMargin];  // NSvFlags 0x10c
  [w setContentView:v];

  AppDelegate *d = [[AppDelegate alloc] init];
  [d setWindow:w];
  [NSApp setDelegate:d];

  MainController *mc = [[MainController alloc] init];
  mc->openGLView = v;

  NSMenu *bar = [[NSMenu alloc] initWithTitle:@"AMainMenu"];
  NSMenuItem *appItem = [[NSMenuItem alloc] initWithTitle:@"Rocksmith 2014" action:NULL keyEquivalent:@""];
  NSMenu *appMenu = [[NSMenu alloc] initWithTitle:@"Rocksmith 2014"];
  [appMenu addItemWithTitle:@"Quit Rocksmith 2014" action:@selector(terminate:) keyEquivalent:@"q"];
  [appItem setSubmenu:appMenu];
  [bar addItem:appItem];
  [NSApp setMainMenu:bar];

  [mc awakeFromNib];
  [w makeKeyAndOrderFront:nil];  // NSVisibleWindows

  if (out) {
    out->window = w;
    out->view = v;
    out->controller = mc;
    out->delegate = d;
  }
}

int rsp_shell_main(int argc, const char **argv) {  // 0x100177ff0
  (void)argc;
  (void)argv;
  @autoreleasepool {
    // Gestalt sys1/sys2/sys3: anything before 10.7.0 gets a localized "not
    // compatible" alert and then runs anyway (the original does not return).
    NSOperatingSystemVersion v = [[NSProcessInfo processInfo] operatingSystemVersion];
    if (v.majorVersion == 10 && v.minorVersion < 7) {
      CFUserNotificationDisplayAlert(0, kCFUserNotificationStopAlertLevel, NULL, NULL, NULL,
                                     CFSTR("Error"),
                                     CFSTR("Rocksmith is not compatible with your version of OS X."),
                                     NULL, NULL, NULL, NULL);
    }
    [NSApplication sharedApplication];
    rsp_shell_load(NULL);
    [NSApp run];
  }
  return 0;
}
