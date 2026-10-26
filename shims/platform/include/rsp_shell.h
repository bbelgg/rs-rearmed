// Platform shell rewrite: the ObjC half. The same four classes as the
// original, with the same ivars and methods;
// each method names the original it reproduces. Built without ARC, like the
// original (manual retain/release, [self dealloc] from drawView).
#ifndef RSP_SHELL_H
#define RSP_SHELL_H

#import <AppKit/AppKit.h>
#import <CoreVideo/CoreVideo.h>
#import <IOKit/pwr_mgt/IOPMLib.h>

#include "rsp_platform.h"

@class MainController;

// NSApplicationDelegate; instanceSize 16 in the original (one ivar).
@interface AppDelegate : NSObject <NSApplicationDelegate> {
 @public
  NSWindow *window;
}
@property(assign) NSWindow *window;
@end

// NSView subclass, the window's content view and first responder.
@interface OpenGLView : NSView {
 @public
  NSOpenGLContext *openGLContext;
  NSOpenGLPixelFormat *pixelFormat;
  MainController *controller;
  NSTrackingArea *trackingArea;
  CVDisplayLinkRef displayLink;
  BOOL isAnimating;
  BOOL isFullScreen;
  IOPMAssertionID sleepAssertionID;
  IOReturn sleepDisableSuccess;  // the assertion's IOReturn (0 = created)
  double lastInputTime;
  BOOL cursorVisible;
  BOOL cursorInWindow;
  BOOL done;
}
- (NSOpenGLContext *)openGLContext;
- (NSOpenGLPixelFormat *)pixelFormat;
- (void)setMainController:(MainController *)c;
- (void)setIsFullScreen:(BOOL)fs;
- (CVReturn)getFrameForTime:(const CVTimeStamp *)outputTime;
- (void)setupDisplayLink;
- (id)initWithFrame:(NSRect)frame shareContext:(NSOpenGLContext *)share;
- (void)drawView;
- (void)startAnimation;
- (void)stopAnimation;
@end

// NSResponder subclass; owns window mode and the 10 Hz housekeeping timer.
@interface MainController : NSResponder {
 @public
  BOOL isInFullScreenMode;
  int numSwapBuffers;
  NSWindow *fullScreenWindow;  // unused by the original too
  OpenGLView *fullScreenView;  // unused
  OpenGLView *openGLView;      // nib outlet
  NSUInteger windowStyleMask;
  NSPoint windowOrigin;
  NSInteger windowLevel;
  BOOL isAnimating;
  double renderTime;
  NSTimer *renderTimer;
  BOOL CurrentSetting_NeedsUpdate;
  BOOL CurrentSetting_FullScreen;
  int CurrentSetting_Width;
  int CurrentSetting_Height;
}
- (void)goFullScreen;
- (void)goWindow;
- (double)renderTime;
- (void)setRenderTime:(double)t;
- (void)startAnimation;
- (void)stopAnimation;
- (void)toggleAnimation;
- (void)customEventTimer:(NSTimer *)t;
- (void)updateScreenSize;
- (void)clientMessageBox;
@end

@interface MessageBox : NSObject
- (void)ShowAlert:(const char *)message withDescription:(const char *)description;
@end

// The objects MainMenu.nib instantiates and connects, made in code.
typedef struct rsp_shell {
  NSWindow *window;
  OpenGLView *view;
  MainController *controller;
  AppDelegate *delegate;
} rsp_shell;

// Loads the "nib": window {{320,229},{1280,720}} style 15 "Rocksmith 2014",
// content min 800x600, an OpenGLView content view, AppDelegate as NSApp's
// delegate, MainController with its openGLView outlet, the main menu (Quit,
// Cmd+Q), then -[MainController awakeFromNib] and orders the window front.
// NSApp must exist.
void rsp_shell_load(rsp_shell *out);

// main() (0x100177ff0): the OS version check, then NSApplicationMain with the
// nib above. Does not return.
int rsp_shell_main(int argc, const char **argv);

// Globals the original exports to the engine.
extern NSOpenGLContext *rsp_g_gl_context;     // 0x1018e7488
extern MainController *rsp_g_main_controller;  // 0x1018e7490

// Clipboard text for the UI (0x1001861d0): UTF-8 of
// [[NSPasteboard generalPasteboard] stringForType:NSPasteboardTypeString],
// copied into buf. Returns the length, or -1 if there is none.
int rsp_clipboard_text(char *buf, size_t size);

#endif
