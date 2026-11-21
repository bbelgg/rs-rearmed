// Learn & Play on iPhone and iPad (experimental): the app and the game's window, in place of the Mac app's
// win-x86/app/lp_window.m. UIKit has the main thread; lp_main (win-x86/app/lp_main.c) loads, binds and starts the
// game on a thread of its own once the window is up and the microphone has been asked for, and the game's thread
// runs the game as on the Mac.
//
// The screen is the game's desktop (lp_screen_size): the screen in landscape, in points (RS_METAL_RETINA=1: in
// pixels) at RS_METAL_SCALE's percentage, as the Mac edition's main screen. The game always has the whole screen (a
// windowed device is shown the same way); the back buffer is fitted to it keeping its shape, and libRSOpenGL scales
// a back buffer smaller than the screen's pixels up to them (MetalFX), as on the Mac.
//
// Input, for now (a touch layout of its own is open work, docs/windows/PLAN.md):
//   one finger      the mouse: a touch moves the pointer there and holds the left button until it lifts
//   two-finger tap  Escape (back)
//   three-finger tap  the app's menu: the song folder
//   a keyboard      the keys, as on the Mac (UIKit's HID usages to the Mac's key codes, win32_input.c from there)
//
// Songs from outside the app (custom songs AirDropped to the Files app's Downloads, say): a folder the player picks once
// (the menu's Song Folder), and any .psarc opened with the app (AirDrop, Files, the share sheet), are copied into
// game/dlc/ios-import, where the game finds them at its start or at ENUMERATE. The folder is copied again at every start
// and every return to the app; a song gone from it goes from ios-import too.
// The switches (RS_*, LP_*) come from options.env and settings.env in the app's Documents folder (lp_settings.h), as
// the Mac app's do from its folder.
#import <AVFAudio/AVFAudio.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#import <QuartzCore/CAMetalLayer.h>
#import <UIKit/UIKit.h>

#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "CoreAudio/CoreAudio.h"
#include "lp_screen.h"
#include "rs_gfx.h"
#include "rsp_fps.h"
#include "rsp_scale.h"

int lp_main(int argc, char **argv);  // win-x86/app/lp_main.c
void lp_game_shape(int w, int h);    // win32_overrides.c
void d3d9_set_hidden_period(uint64_t ns);
void d3d9_set_fixed_period(uint64_t ns);
void w32_input_release_all(void);
void w32_input_focus(int active);
void w32_input_key(int mac_keycode, int down, uint32_t ch);
void w32_input_mouse_move(int32_t x, int32_t y, int32_t dx, int32_t dy);
void w32_input_mouse_button(int button, int down);
void w32_input_view_height(int32_t h);
void w32_input_script(void);

static int g_argc;
static char **g_argv;

static void on_main(void (^b)(void)) {
  if ([NSThread isMainThread]) b();
  else dispatch_sync(dispatch_get_main_queue(), b);
}

static int retina(void) {
  const char *e = getenv("RS_METAL_RETINA");
  return e && *e && *e != '0';
}
static double game_scale(CGFloat backing) { return (retina() ? backing : 1) * rsp_metal_scale(); }

// the screen in landscape, in points, and its pixels per point
static void screen_info(CGSize *pts, CGFloat *backing) {
  on_main(^{
    UIScreen *s = UIScreen.mainScreen;
    CGSize b = s.bounds.size;
    *pts = CGSizeMake(fmax(b.width, b.height), fmin(b.width, b.height));
    *backing = s.scale;
  });
}

void lp_screen_size(int *w, int *h) {
  static int cw, ch;
  if (!cw) {
    CGSize pts;
    CGFloat backing;
    screen_info(&pts, &backing);
    double sc = game_scale(backing);
    cw = (int)(pts.width * sc);
    ch = (int)(pts.height * sc);
  }
  *w = cw;
  *h = ch;
}

// --- the view ---------------------------------------------------------------------------------
static int any_shape(void) {
  static int on = -1;
  if (on < 0) on = lp_any_shape(getenv("RS_WINDOW_ANY_SHAPE"));
  return on;
}

static UIView *g_view;
static CAMetalLayer *g_layer;
static rs_gfx_context *g_ctx;
static int g_bw, g_bh;  // the back buffer

// Game pixels fewer than the screen's: libRSOpenGL scales each frame up to the screen's pixels (lp_window.m's set_output)
static void set_output(int w, int h) {
  CGSize pts;
  CGFloat backing;
  screen_info(&pts, &backing);
  int ow, oh;
  if (g_ctx && lp_output_size(w, h, backing, game_scale(backing), pts.width, pts.height, &ow, &oh))
    rs_gfx_set_output_size(g_ctx, ow, oh);
}

// The game's window modes are the Mac's (lp_window.m); here every one of them is the whole screen.
void lp_window_style(uint32_t hwnd, uint32_t style) {
  (void)hwnd;
  (void)style;
}
void d3d9_platform_windowed(int windowed) { (void)windowed; }

void d3d9_platform_resize(uint32_t hwnd, int w, int h) {
  (void)hwnd;
  if (any_shape()) lp_game_shape(w, h);
  dispatch_async(dispatch_get_main_queue(), ^{
    if (w == g_bw && h == g_bh) return;
    g_bw = w;
    g_bh = h;
    set_output(w, h);
    fprintf(stderr, "lp: reset: back buffer %dx%d\n", w, h);
  });
}

static void fixed_rate(void) {
  int fps = rsp_fps();
  d3d9_set_fixed_period(fps ? 1000000000ull / (uint64_t)fps : 0);
}

rs_gfx_context *d3d9_platform_context(uint32_t hwnd, int w, int h) {
  if (any_shape()) lp_game_shape(w, h);
  fixed_rate();
  const char *off = getenv("LP_OFFSCREEN");
  if (off && *off && *off != '0') return rs_gfx_create_offscreen(w, h);
  __block CAMetalLayer *layer = nil;
  on_main(^{
    [g_layer removeFromSuperlayer];
    layer = [CAMetalLayer layer];
    layer.frame = g_view.bounds;
    layer.contentsScale = g_view.window.screen.scale ?: UIScreen.mainScreen.scale;
    layer.contentsGravity = kCAGravityResizeAspect;  // another shape than the screen's: letterboxed
    layer.backgroundColor = UIColor.blackColor.CGColor;
    layer.drawableSize = CGSizeMake(w, h);
    if (rsp_fps()) layer.maximumDrawableCount = 2;
    [g_view.layer addSublayer:layer];
    g_layer = layer;
    g_bw = w;
    g_bh = h;
  });
  w32_input_script();
  rs_gfx_context *ctx = rs_gfx_create_layer((__bridge void *)layer, w, h);
  if (ctx)
    on_main(^{
      g_ctx = ctx;
      set_output(w, h);
    });
  int dw, dh;
  lp_screen_size(&dw, &dh);
  fprintf(stderr, "lp: window for HWND 0x%x: back buffer %dx%d (screen %dx%d at %d%%): %s\n", hwnd, w, h, dw, dh,
          (int)lround(rsp_metal_scale() * 100), ctx ? "Metal layer" : "no layer context, offscreen");
  return ctx ? ctx : rs_gfx_create_offscreen(w, h);
}

// --- input -------------------------------------------------------------------------------------
// UIKit's key codes are USB HID usages; win32_input.c takes the Mac's (Events.h).
static int mac_key(long hid) {
  static const uint8_t letters[26] = {0x00, 0x0B, 0x08, 0x02, 0x0E, 0x03, 0x05, 0x04, 0x22, 0x26, 0x28, 0x25, 0x2E,
                                      0x2D, 0x1F, 0x23, 0x0C, 0x0F, 0x01, 0x11, 0x20, 0x09, 0x0D, 0x07, 0x10, 0x06};
  static const uint8_t digits[10] = {0x12, 0x13, 0x14, 0x15, 0x17, 0x16, 0x1A, 0x1C, 0x19, 0x1D};  // 1..9, 0
  static const uint8_t fkeys[12] = {0x7A, 0x78, 0x63, 0x76, 0x60, 0x61, 0x62, 0x64, 0x65, 0x6D, 0x67, 0x6F};
  if (hid >= 0x04 && hid <= 0x1d) return letters[hid - 0x04];
  if (hid >= 0x1e && hid <= 0x27) return digits[hid - 0x1e];
  if (hid >= 0x3a && hid <= 0x45) return fkeys[hid - 0x3a];
  switch (hid) {
    case 0x28: return 0x24;  // Return
    case 0x29: return 0x35;  // Escape
    case 0x2a: return 0x33;  // Backspace (Delete)
    case 0x2b: return 0x30;  // Tab
    case 0x2c: return 0x31;  // Space
    case 0x2d: return 0x1B;  // -
    case 0x2e: return 0x18;  // =
    case 0x2f: return 0x21;  // [
    case 0x30: return 0x1E;  // ]
    case 0x31: return 0x2A;  // backslash
    case 0x33: return 0x29;  // ;
    case 0x34: return 0x27;  // '
    case 0x35: return 0x32;  // `
    case 0x36: return 0x2B;  // ,
    case 0x37: return 0x2F;  // .
    case 0x38: return 0x2C;  // /
    case 0x39: return 0x39;  // Caps Lock
    case 0x49: return 0x72;  // Insert (Help)
    case 0x4a: return 0x73;  // Home
    case 0x4b: return 0x74;  // Page Up
    case 0x4c: return 0x75;  // Forward Delete
    case 0x4d: return 0x77;  // End
    case 0x4e: return 0x79;  // Page Down
    case 0x4f: return 0x7C;  // Right
    case 0x50: return 0x7B;  // Left
    case 0x51: return 0x7D;  // Down
    case 0x52: return 0x7E;  // Up
    case 0xe0: return 0x3B;  // left Control
    case 0xe1: return 0x38;  // left Shift
    case 0xe2: return 0x3A;  // left Option (Alt)
    case 0xe3: return 0x37;  // left Command
    case 0xe4: return 0x3E;  // right Control
    case 0xe5: return 0x3C;  // right Shift
    case 0xe6: return 0x3D;  // right Option
    case 0xe7: return 0x36;  // right Command
  }
  return -1;
}

@interface LPGameView : UIView
@end
@implementation LPGameView
- (void)layoutSubviews {
  [super layoutSubviews];
  [CATransaction begin];
  [CATransaction setDisableActions:YES];
  g_layer.frame = self.bounds;
  [CATransaction commit];
}
- (BOOL)canBecomeFirstResponder {
  return YES;
}

static void touch_at(UIView *v, CGPoint p) {
  CGSize s = v.bounds.size;
  int32_t x, y;
  double k;
  if (!g_bw || !g_bh) return;
  lp_view_to_pixels(p.x, p.y, s.width, s.height, g_bw, g_bh, &x, &y, &k);
  w32_input_view_height(g_bh);
  w32_input_mouse_move(x, y, 0, 0);
}

static UITouch *g_finger;  // the touch that is the mouse
- (void)touchesBegan:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
  if (g_finger || event.allTouches.count != 1) return;
  g_finger = touches.anyObject;
  touch_at(self, [g_finger locationInView:self]);
  w32_input_mouse_button(0, 1);
}
- (void)touchesMoved:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
  if (g_finger && [touches containsObject:g_finger]) touch_at(self, [g_finger locationInView:self]);
}
- (void)touchesEnded:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
  if (!g_finger || ![touches containsObject:g_finger]) return;
  touch_at(self, [g_finger locationInView:self]);
  w32_input_mouse_button(0, 0);
  g_finger = nil;
}
- (void)touchesCancelled:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
  if (!g_finger || ![touches containsObject:g_finger]) return;
  w32_input_mouse_button(0, 0);
  g_finger = nil;
}

- (void)escape:(UITapGestureRecognizer *)g {
  if (g.state != UIGestureRecognizerStateEnded) return;
  if (g_finger) {  // the first finger of the two pressed the button: let it go where it was
    w32_input_mouse_button(0, 0);
    g_finger = nil;
  }
  w32_input_key(0x35, 1, 27);
  dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 80 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
    w32_input_key(0x35, 0, 0);
  });
}

static void press(NSSet<UIPress *> *presses, int down) {
  for (UIPress *p in presses) {
    if (!p.key) continue;
    int k = mac_key(p.key.keyCode);
    if (k < 0) continue;
    NSString *s = p.key.characters;
    uint32_t ch = down && s.length ? [s characterAtIndex:0] : 0;
    if (ch == '\n' || ch == 3) ch = '\r';
    if (ch == 0x7f) ch = 8;
    w32_input_key(k, down, ch);
  }
}
- (void)pressesBegan:(NSSet<UIPress *> *)presses withEvent:(UIPressesEvent *)event {
  press(presses, 1);
}
- (void)pressesEnded:(NSSet<UIPress *> *)presses withEvent:(UIPressesEvent *)event {
  press(presses, 0);
}
- (void)pressesCancelled:(NSSet<UIPress *> *)presses withEvent:(UIPressesEvent *)event {
  press(presses, 0);
}
@end

static void show_menu(void);  // the app's menu (below)

@interface LPViewController : UIViewController
@end
@implementation LPViewController
- (void)loadView {
  LPGameView *v = [LPGameView new];
  v.backgroundColor = UIColor.blackColor;
  v.multipleTouchEnabled = YES;
  UITapGestureRecognizer *esc = [[UITapGestureRecognizer alloc] initWithTarget:v action:@selector(escape:)];
  esc.numberOfTouchesRequired = 2;
  esc.cancelsTouchesInView = NO;
  [v addGestureRecognizer:esc];
  UITapGestureRecognizer *menu = [[UITapGestureRecognizer alloc] initWithTarget:self action:@selector(menu:)];
  menu.numberOfTouchesRequired = 3;
  menu.cancelsTouchesInView = NO;
  [v addGestureRecognizer:menu];
  [esc requireGestureRecognizerToFail:menu];
  self.view = v;
  g_view = v;
}
- (void)menu:(UITapGestureRecognizer *)g {
  if (g.state == UIGestureRecognizerStateEnded) show_menu();
}
- (void)viewDidAppear:(BOOL)animated {
  [super viewDidAppear:animated];
  [self.view becomeFirstResponder];
}
- (BOOL)prefersStatusBarHidden {
  return YES;
}
- (BOOL)prefersHomeIndicatorAutoHidden {
  return YES;
}
- (UIRectEdge)preferredScreenEdgesDeferringSystemGestures {
  return UIRectEdgeAll;
}
- (UIInterfaceOrientationMask)supportedInterfaceOrientations {
  return UIInterfaceOrientationMaskLandscape;
}
@end

// --- songs from outside the app ------------------------------------------------------------------
static UIWindow *g_window;
static NSString *const kSongFolder = @"songFolderBookmark";

static NSURL *documents(void) {
  return [NSFileManager.defaultManager URLsForDirectory:NSDocumentDirectory inDomains:NSUserDomainMask].firstObject;
}
static NSURL *import_dir(void) {
  NSURL *d = [documents() URLByAppendingPathComponent:@"game/dlc/ios-import" isDirectory:YES];
  [NSFileManager.defaultManager createDirectoryAtURL:d withIntermediateDirectories:YES attributes:nil error:nil];
  return d;
}

// src into ios-import unless the copy there has its size and date already; 1 when copied
static int import_file(NSURL *src) {
  NSFileManager *fm = NSFileManager.defaultManager;
  NSURL *dst = [import_dir() URLByAppendingPathComponent:src.lastPathComponent];
  NSDictionary *a = [fm attributesOfItemAtPath:src.path error:nil], *b = [fm attributesOfItemAtPath:dst.path error:nil];
  if (b && [a[NSFileSize] isEqual:b[NSFileSize]] && [a[NSFileModificationDate] isEqual:b[NSFileModificationDate]])
    return 0;
  NSURL *tmp = [dst URLByAppendingPathExtension:@"part"];  // the game never sees half a song
  [fm removeItemAtURL:tmp error:nil];
  NSError *e = nil;
  if (![fm copyItemAtURL:src toURL:tmp error:&e]) {
    fprintf(stderr, "lp: song %s not copied: %s\n", src.lastPathComponent.UTF8String, e.localizedDescription.UTF8String);
    return 0;
  }
  if (a[NSFileModificationDate])
    [fm setAttributes:@{NSFileModificationDate : a[NSFileModificationDate]} ofItemAtPath:tmp.path error:nil];
  [fm removeItemAtURL:dst error:nil];
  [fm moveItemAtURL:tmp toURL:dst error:nil];
  return 1;
}

static int is_song(NSURL *u) { return [u.pathExtension.lowercaseString isEqualToString:@"psarc"]; }

// the picked folder's songs (and its folders'), into ios-import; songs there from the folder that left it go too
static void import_folder(void) {
  @autoreleasepool {
    NSData *bm = [NSUserDefaults.standardUserDefaults dataForKey:kSongFolder];
    if (!bm) return;
    BOOL stale = NO;
    NSError *e = nil;
    NSURL *dir = [NSURL URLByResolvingBookmarkData:bm options:0 relativeToURL:nil bookmarkDataIsStale:&stale error:&e];
    if (!dir || ![dir startAccessingSecurityScopedResource]) {
      fprintf(stderr, "lp: the song folder can't be read now (%s): choose it again in the menu (three-finger tap)\n",
              e ? e.localizedDescription.UTF8String : "no access");
      return;
    }
    if (stale) {
      NSData *again = [dir bookmarkDataWithOptions:0 includingResourceValuesForKeys:nil relativeToURL:nil error:nil];
      if (again) [NSUserDefaults.standardUserDefaults setObject:again forKey:kSongFolder];
    }
    NSMutableSet *names = [NSMutableSet set];
    int copied = 0;
    NSDirectoryEnumerator *en = [NSFileManager.defaultManager enumeratorAtURL:dir
                                                   includingPropertiesForKeys:nil
                                                                      options:NSDirectoryEnumerationSkipsHiddenFiles
                                                                 errorHandler:nil];
    for (NSURL *u in en)
      if (is_song(u)) {
        [names addObject:u.lastPathComponent];
        copied += import_file(u);
      }
    [dir stopAccessingSecurityScopedResource];
    // the ones from the folder that left it (a song opened with the app has a mark of its own: kept)
    NSArray *have = [NSFileManager.defaultManager contentsOfDirectoryAtPath:import_dir().path error:nil];
    NSMutableSet *opened = [NSMutableSet setWithArray:[NSUserDefaults.standardUserDefaults arrayForKey:@"openedSongs"] ?: @[]];
    int removed = 0;
    for (NSString *n in have)
      if ([n.pathExtension.lowercaseString isEqualToString:@"psarc"] && ![names containsObject:n] && ![opened containsObject:n]) {
        [NSFileManager.defaultManager removeItemAtURL:[import_dir() URLByAppendingPathComponent:n] error:nil];
        removed++;
      }
    fprintf(stderr, "lp: song folder %s: %lu songs, %d copied, %d gone\n", dir.lastPathComponent.UTF8String,
            (unsigned long)names.count, copied, removed);
  }
}

// a song opened with the app (AirDrop, Files, the share sheet)
static void import_opened(NSURL *u) {
  if (!is_song(u)) return;
  BOOL scoped = [u startAccessingSecurityScopedResource];
  int done = import_file(u);
  if (scoped) [u stopAccessingSecurityScopedResource];
  NSString *n = u.lastPathComponent;
  NSMutableArray *opened = [[NSUserDefaults.standardUserDefaults arrayForKey:@"openedSongs"] ?: @[] mutableCopy];
  if (![opened containsObject:n]) [opened addObject:n];
  [NSUserDefaults.standardUserDefaults setObject:opened forKey:@"openedSongs"];
  fprintf(stderr, "lp: song %s opened with the app%s\n", n.UTF8String, done ? ", copied" : "");
}

@interface LPFolderPicker : NSObject <UIDocumentPickerDelegate>
@end
@implementation LPFolderPicker
- (void)documentPicker:(UIDocumentPickerViewController *)p didPickDocumentsAtURLs:(NSArray<NSURL *> *)urls {
  NSURL *dir = urls.firstObject;
  if (!dir) return;
  BOOL scoped = [dir startAccessingSecurityScopedResource];
  NSData *bm = [dir bookmarkDataWithOptions:0 includingResourceValuesForKeys:nil relativeToURL:nil error:nil];
  if (scoped) [dir stopAccessingSecurityScopedResource];
  if (!bm) return;
  [NSUserDefaults.standardUserDefaults setObject:bm forKey:kSongFolder];
  fprintf(stderr, "lp: song folder chosen: %s\n", dir.path.UTF8String);
  dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), ^{ import_folder(); });
}
@end
static LPFolderPicker *g_picker;

static void show_menu(void) {
  UIViewController *vc = g_window.rootViewController;
  if (vc.presentedViewController) return;
  UIAlertController *m = [UIAlertController
      alertControllerWithTitle:@"Learn & Play"
                       message:@"Songs in the song folder are copied in at every start and return to the app; then "
                               @"use ENUMERATE in the game's menu (or start it again)."
                preferredStyle:UIAlertControllerStyleActionSheet];
  [m addAction:[UIAlertAction actionWithTitle:@"Choose Song Folder…"
                                        style:UIAlertActionStyleDefault
                                      handler:^(UIAlertAction *x) {
                                        if (!g_picker) g_picker = [LPFolderPicker new];
                                        UIDocumentPickerViewController *p = [[UIDocumentPickerViewController alloc]
                                            initForOpeningContentTypes:@[ UTTypeFolder ]];
                                        p.delegate = g_picker;
                                        [vc presentViewController:p animated:YES completion:nil];
                                      }]];
  if ([NSUserDefaults.standardUserDefaults dataForKey:kSongFolder])
    [m addAction:[UIAlertAction actionWithTitle:@"Copy Songs Now"
                                          style:UIAlertActionStyleDefault
                                        handler:^(UIAlertAction *x) {
                                          dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY, 0),
                                                         ^{ import_folder(); });
                                        }]];
  [m addAction:[UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel handler:nil]];
  m.popoverPresentationController.sourceView = vc.view;  // an iPad's action sheet is a popover
  m.popoverPresentationController.sourceRect = CGRectMake(vc.view.bounds.size.width / 2, vc.view.bounds.size.height / 2, 1, 1);
  [vc presentViewController:m animated:YES completion:nil];
}

// --- failures -----------------------------------------------------------------------------------

static void alert_then_quit(NSString *msg) {
  UIAlertController *a = [UIAlertController alertControllerWithTitle:@"RS Re-ARMed Learn & Play"
                                                             message:msg
                                                      preferredStyle:UIAlertControllerStyleAlert];
  [a addAction:[UIAlertAction actionWithTitle:@"Quit"
                                        style:UIAlertActionStyleDefault
                                      handler:^(UIAlertAction *x) {
                                        fflush(0);
                                        exit(1);
                                      }]];
  [g_window.rootViewController presentViewController:a animated:YES completion:nil];
}

// lp_main.c's fail_alert: the message, then quit; the calling thread waits for it
void lp_ios_fail(const char *msg) {
  NSString *m = [NSString stringWithUTF8String:msg];
  dispatch_async(dispatch_get_main_queue(), ^{ alert_then_quit(m); });
  for (;;) pause();
}

static void *start_game(void *arg) {
  (void)arg;
  pthread_setname_np("lp start");
  // A phone's GPU and its heat: no MSAA, no MetalFX upscaling, the game's own texture filtering (the Mac's defaults
  // are 4, on and 16). options.env in the app's folder sets them otherwise, as on the Mac.
  setenv("RS_METAL_MSAA", "1", 0);
  setenv("RS_METAL_UPSCALE", "0", 0);
  setenv("RS_METAL_ANISO", "1", 0);
  import_folder();  // before the game lists its songs
  int rc = lp_main(g_argc, g_argv);
  if (rc)
    dispatch_async(dispatch_get_main_queue(), ^{
      alert_then_quit(@"The game could not start. The log is logs/last-run.log in the app's folder (the Files app, "
                      @"or the iPhone's Files in the Finder).");
    });
  return NULL;
}

// --- the app --------------------------------------------------------------------------------------
@interface LPSceneDelegate : UIResponder <UIWindowSceneDelegate>
@property(strong, nonatomic) UIWindow *window;
@end
@implementation LPSceneDelegate
- (void)scene:(UIScene *)scene willConnectToSession:(UISceneSession *)session options:(UISceneConnectionOptions *)o {
  UIWindow *w = [[UIWindow alloc] initWithWindowScene:(UIWindowScene *)scene];
  w.rootViewController = [LPViewController new];
  w.backgroundColor = UIColor.blackColor;
  [w makeKeyAndVisible];
  self.window = g_window = w;
  UIApplication.sharedApplication.idleTimerDisabled = YES;  // no screen lock while the game is open
  for (UIOpenURLContext *c in o.URLContexts) import_opened(c.URL);  // started by opening a song
  static dispatch_once_t once;
  dispatch_once(&once, ^{
    // the guitar is a microphone to iOS: ask before the game lists its inputs, whatever the answer
    [AVAudioApplication requestRecordPermissionWithCompletionHandler:^(BOOL granted) {
      fprintf(stderr, "lp: microphone %s\n", granted ? "allowed" : "not allowed: no guitar input");
      rs_hal_ios_session();
      pthread_attr_t at;
      pthread_attr_init(&at);
      pthread_attr_setstacksize(&at, 8u << 20);
      pthread_t t;
      pthread_create(&t, &at, start_game, NULL);
      pthread_detach(t);
    }];
  });
}
// In the background iOS lets no GPU work run: the frames keep the display's pace without the layer, and the
// keys and the button go up, as when the Mac window loses the keyboard.
- (void)sceneDidEnterBackground:(UIScene *)scene {
  w32_input_release_all();
  w32_input_focus(0);
  d3d9_set_hidden_period(1000000000ull / 60);
  fprintf(stderr, "lp: in the background\n");
}
- (void)scene:(UIScene *)scene openURLContexts:(NSSet<UIOpenURLContext *> *)contexts {
  for (UIOpenURLContext *c in contexts) import_opened(c.URL);
}
- (void)sceneWillEnterForeground:(UIScene *)scene {
  dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), ^{ import_folder(); });
  d3d9_set_hidden_period(0);
  w32_input_focus(1);
  fprintf(stderr, "lp: in the foreground\n");
}
@end

@interface LPAppDelegate : UIResponder <UIApplicationDelegate>
@end
@implementation LPAppDelegate
- (UISceneConfiguration *)application:(UIApplication *)app
    configurationForConnectingSceneSession:(UISceneSession *)session
                                   options:(UISceneConnectionOptions *)options {
  UISceneConfiguration *c = [[UISceneConfiguration alloc] initWithName:@"Game" sessionRole:session.role];
  c.delegateClass = LPSceneDelegate.class;
  return c;
}
@end

int main(int argc, char **argv) {
  g_argc = argc;
  g_argv = argv;
  @autoreleasepool {
    return UIApplicationMain(argc, argv, nil, NSStringFromClass(LPAppDelegate.class));
  }
}
