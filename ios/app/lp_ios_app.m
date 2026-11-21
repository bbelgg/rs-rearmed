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
// Input (ios/README.md, Playing):
//   one finger        the mouse: a touch moves the pointer there and holds the left button until it lifts
//   two-finger swipe  the mouse wheel (a list's rows)
//   two-finger tap    Escape (back, the pause menu)
//   the key bar       the keys the menus name, at the right edge, hidden while a song or a game is played
//   three-finger tap  shows or hides the key bar; its MORE button is the app's menu (the song folder)
//   a keyboard        the keys, as on the Mac (UIKit's HID usages to the Mac's key codes, win32_input.c from there)
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
void w32_input_mouse_wheel(int32_t delta);
void w32_input_view_height(int32_t h);
void w32_input_script(void);
int lp_current_menu(char *out, int n);  // win32_overrides.c
int rc_hook_add_name(const char *name, int (*pre)(void *, void *), void (*post)(void *, void *), void *user);

static void follow_menus(void);  // the key bar follows the game's menus (below)

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
    [g_view.layer insertSublayer:layer atIndex:0];  // under the key bar
    g_layer = layer;
    g_bw = w;
    g_bh = h;
  });
  w32_input_script();
  follow_menus();
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

// The finger that is the mouse. Its button goes down a moment after the touch (PRESS_MS), so that the first finger of
// a two-finger gesture that lands a little before the second clicks nothing; a tap shorter than that is a click
// of its own (CLICK_MS down).
enum { PRESS_MS = 60, CLICK_MS = 50 };
static UITouch *g_finger;
static int g_pressed;  // the button is down for g_finger

static void after_ms(int ms, dispatch_block_t b) {
  dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)ms * NSEC_PER_MSEC), dispatch_get_main_queue(), b);
}

// a second finger came, or a gesture took over: the mouse lets go where it was
static void finger_drop(void) {
  if (g_finger && g_pressed) w32_input_mouse_button(0, 0);
  g_finger = nil;
  g_pressed = 0;
}

- (void)touchesBegan:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
  // A finger whose end never came (a sheet the app put over the game took it) is let go here: the first touch
  // of a new gesture is the only one on the screen.
  if (event.allTouches.count != 1 || (g_finger && g_finger.phase != UITouchPhaseEnded &&
                                      g_finger.phase != UITouchPhaseCancelled && [event.allTouches containsObject:g_finger])) {
    finger_drop();
    return;
  }
  finger_drop();
  UITouch *t = g_finger = touches.anyObject;
  touch_at(self, [t locationInView:self]);
  after_ms(PRESS_MS, ^{
    if (g_finger != t || g_pressed) return;
    g_pressed = 1;
    w32_input_mouse_button(0, 1);
  });
}
- (void)touchesMoved:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
  if (g_finger && [touches containsObject:g_finger]) touch_at(self, [g_finger locationInView:self]);
}
- (void)touchesEnded:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
  if (!g_finger || ![touches containsObject:g_finger]) return;
  touch_at(self, [g_finger locationInView:self]);
  if (g_pressed) {
    w32_input_mouse_button(0, 0);
  } else {  // a quick tap: a click
    w32_input_mouse_button(0, 1);
    after_ms(CLICK_MS, ^{ w32_input_mouse_button(0, 0); });
  }
  g_finger = nil;
  g_pressed = 0;
}
- (void)touchesCancelled:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
  if (g_finger && [touches containsObject:g_finger]) finger_drop();
}

static void tap_key(int mac, uint32_t ch) {
  w32_input_key(mac, 1, ch);
  after_ms(80, ^{ w32_input_key(mac, 0, 0); });
}

- (void)escape:(UITapGestureRecognizer *)g {
  if (g.state != UIGestureRecognizerStateEnded) return;
  finger_drop();
  tap_key(0x35, 27);
}

// Two fingers up or down: the mouse wheel, a step (one row of a list) every WHEEL_PT, at the fingers. Fingers going up
// bring up what is below, as on the Mac's trackpad with natural scrolling.
enum { WHEEL_PT = 28 };
- (void)wheel:(UIPanGestureRecognizer *)g {
  static CGFloat acc;
  if (g.state == UIGestureRecognizerStateBegan) {
    finger_drop();
    acc = 0;
    touch_at(self, [g locationInView:self]);
  }
  if (g.state != UIGestureRecognizerStateBegan && g.state != UIGestureRecognizerStateChanged) return;
  acc += [g translationInView:self].y;
  [g setTranslation:CGPointZero inView:self];
  for (; acc <= -WHEEL_PT; acc += WHEEL_PT) w32_input_mouse_wheel(-120);
  for (; acc >= WHEEL_PT; acc -= WHEEL_PT) w32_input_mouse_wheel(120);
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

// --- the keys a menu asks for ---------------------------------------------------------------------
// The game's menus name keys (ENTER, ESC, SPACE Tools, DELETE Skip Tuner, CTRL Path/Input, SHIFT Search, the arrows on
// a slider). The key bar has them: two columns of buttons at the screen's right edge, shown in the menus and hidden
// while a song (or another mode's play) is on, which the game's current menu tells (lp_current_menu); a three-finger
// tap shows or hides it until the next menu. A button is the key held as long as it is touched. KEYBOARD brings up
// iOS's keyboard for the song search and a profile's name; MORE is the app's menu.
static UIView *g_bar;
static UIView *g_typing;  // the responder that takes iOS's keyboard (LPTyping)
static int g_playing;     // the current menu is one where a song or a game is played
static int g_bar_flip;    // the player's three-finger tap since the last menu change

static void bar_update(void) {
  int show = !g_playing != !g_bar_flip;
  if (!show && g_typing.isFirstResponder) [g_view becomeFirstResponder];
  if ((g_bar.alpha > 0) == show) return;
  g_bar.userInteractionEnabled = show;
  [UIView animateWithDuration:0.2 animations:^{ g_bar.alpha = show ? 1 : 0; }];
}

// A menu where the picture is the game's: a song (Learn a Song, Nonstop Play, Score Attack, the two players' songs end
// in _Game too), Session Mode's band, a Guitarcade game.
static int playing_menu(const char *m) {
  static const char *const play[] = {"SessionMode_Game", NULL};
  size_t n = strlen(m);
  if (n > 5 && !strcmp(m + n - 5, "_Game")) return 1;
  for (const char *const *p = play; *p; p++)
    if (!strcmp(m, *p)) return 1;
  return 0;
}

// game_frame's post hook, on the game thread: the current menu, looked at every few frames
static void frame_post(void *c, void *user) {
  static unsigned frames;
  static char seen[64];
  (void)c;
  (void)user;
  if (frames++ % 6) return;
  char m[64];
  lp_current_menu(m, sizeof m);
  if (!strcmp(m, seen)) return;
  memcpy(seen, m, sizeof seen);
  int play = playing_menu(m);
  fprintf(stderr, "lp: menu: %s%s\n", m, play ? " (the key bar hides)" : "");
  dispatch_async(dispatch_get_main_queue(), ^{
    g_playing = play;
    g_bar_flip = 0;
    bar_update();
  });
}

static void follow_menus(void) {
  static int done;
  if (done++) return;
  if (rc_hook_add_name("game_frame", NULL, frame_post, NULL))
    fprintf(stderr, "lp: no game_frame hook point: the key bar stays up\n");
}

@interface LPKeyButton : UIButton
@property(nonatomic) int mac;      // the Mac key code (win32_input.c), -1: an action of the app's
@property(nonatomic) uint32_t ch;  // the character it types
@property(nonatomic, copy) void (^action)(void);
@end
@implementation LPKeyButton
@end

static UIImpactFeedbackGenerator *g_haptic;

@interface LPKeyBar : UIView
@property(nonatomic, strong) NSArray<LPKeyButton *> *outer, *inner;
@end
@implementation LPKeyBar
// touches between the buttons go to the game
- (UIView *)hitTest:(CGPoint)p withEvent:(UIEvent *)e {
  UIView *v = [super hitTest:p withEvent:e];
  return v == self ? nil : v;
}
- (void)layoutSubviews {
  [super layoutSubviews];
  CGRect safe = UIEdgeInsetsInsetRect(self.bounds, self.safeAreaInsets);
  CGFloat gap = 6, n = self.outer.count;
  CGFloat h = fmin(40, floor((safe.size.height - 2 * gap - (n - 1) * gap) / n));
  CGFloat wo = 66, wi = 46, top = CGRectGetMinY(safe) + (safe.size.height - (n * h + (n - 1) * gap)) / 2;
  CGFloat xo = CGRectGetMaxX(safe) - gap - wo, xi = xo - gap - wi;
  for (NSUInteger i = 0; i < self.outer.count; i++) self.outer[i].frame = CGRectMake(xo, top + i * (h + gap), wo, h);
  for (NSUInteger i = 0; i < self.inner.count; i++) self.inner[i].frame = CGRectMake(xi, top + i * (h + gap), wi, h);
}
- (void)down:(LPKeyButton *)b {
  [g_haptic impactOccurred];
  if (b.mac >= 0) {
    finger_drop();
    w32_input_key(b.mac, 1, b.ch);
  }
}
- (void)up:(LPKeyButton *)b {
  if (b.mac >= 0) w32_input_key(b.mac, 0, 0);
}
- (void)tapped:(LPKeyButton *)b {
  if (b.action) b.action();
}
- (LPKeyButton *)key:(NSString *)title symbol:(NSString *)symbol mac:(int)mac ch:(uint32_t)ch {
  LPKeyButton *b = [LPKeyButton buttonWithType:UIButtonTypeCustom];
  b.mac = mac;
  b.ch = ch;
  if (symbol)
    [b setImage:[UIImage systemImageNamed:symbol
                         withConfiguration:[UIImageSymbolConfiguration configurationWithPointSize:15
                                                                                           weight:UIImageSymbolWeightSemibold]]
       forState:UIControlStateNormal];
  else
    [b setTitle:title forState:UIControlStateNormal];
  b.accessibilityLabel = title;
  b.tintColor = UIColor.whiteColor;
  b.titleLabel.font = [UIFont systemFontOfSize:12 weight:UIFontWeightBold];
  b.titleLabel.adjustsFontSizeToFitWidth = YES;
  [b setTitleColor:UIColor.whiteColor forState:UIControlStateNormal];
  [b setTitleColor:[UIColor colorWithWhite:1 alpha:0.5] forState:UIControlStateHighlighted];
  b.backgroundColor = [UIColor colorWithWhite:0 alpha:0.45];
  b.layer.cornerRadius = 8;
  b.layer.borderWidth = 1;
  b.layer.borderColor = [UIColor colorWithWhite:1 alpha:0.35].CGColor;
  [b addTarget:self action:@selector(down:) forControlEvents:UIControlEventTouchDown];
  [b addTarget:self
                action:@selector(up:)
      forControlEvents:UIControlEventTouchUpInside | UIControlEventTouchUpOutside | UIControlEventTouchCancel];
  [b addTarget:self action:@selector(tapped:) forControlEvents:UIControlEventTouchUpInside];
  [self addSubview:b];
  return b;
}
- (LPKeyButton *)action:(NSString *)title symbol:(NSString *)symbol do:(void (^)(void))action {
  LPKeyButton *b = [self key:title symbol:symbol mac:-1 ch:0];
  b.action = action;
  return b;
}
- (instancetype)initWithFrame:(CGRect)f {
  if (!(self = [super initWithFrame:f])) return nil;
  g_haptic = [[UIImpactFeedbackGenerator alloc] initWithStyle:UIImpactFeedbackStyleLight];
  self.outer = @[
    [self action:@"KEYBOARD" symbol:@"keyboard" do:^{
      if (g_typing.isFirstResponder) [g_view becomeFirstResponder];
      else [g_typing becomeFirstResponder];
    }],
    [self key:@"ENTER" symbol:nil mac:0x24 ch:'\r'],
    [self key:@"ESC" symbol:nil mac:0x35 ch:27],
    [self key:@"SPACE" symbol:nil mac:0x31 ch:' '],
    [self key:@"DELETE" symbol:nil mac:0x75 ch:0],  // forward delete: the game's DELETE (Skip Tuner)
    [self key:@"CTRL" symbol:nil mac:0x3B ch:0],
    [self key:@"SHIFT" symbol:nil mac:0x38 ch:0],
    [self action:@"MORE" symbol:@"ellipsis" do:^{ show_menu(); }],
  ];
  self.inner = @[
    [self key:@"UP" symbol:@"chevron.up" mac:0x7E ch:0],
    [self key:@"DOWN" symbol:@"chevron.down" mac:0x7D ch:0],
    [self key:@"LEFT" symbol:@"chevron.left" mac:0x7B ch:0],
    [self key:@"RIGHT" symbol:@"chevron.right" mac:0x7C ch:0],
    [self key:@"TAB" symbol:nil mac:0x30 ch:'\t'],
    [self key:@"ALT" symbol:nil mac:0x3A ch:0],
  ];
  return self;
}
@end

// iOS's keyboard: each character typed is its key on a Mac keyboard (US layout) with that character, one at a time,
// as a typist's are; Return is Return and puts the keyboard away.
static int char_key(unichar c) {
  static const char *const rows = "asdfhgzxcv\0bqweryt123465=97-80]ou[ip\0lj'k;\\,/nm.\0\0`";
  static const char shifted[] = "!@#$%^&*()_+{}|:\"<>?~", plain[] = "1234567890-=[]\;',./`";
  if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
  const char *s = c < 0x80 ? strchr(shifted, (int)c) : NULL;
  if (s && c) c = (unichar)plain[s - shifted];
  if (c == ' ') return 0x31;
  for (int i = 0; i < 51; i++)
    if (rows[i] && rows[i] == c) return i;
  return -1;
}

static NSMutableArray<NSNumber *> *g_typed;  // key << 32 | character
static int g_typing_on;
static void type_next(void) {
  if (!g_typed.count) {
    g_typing_on = 0;
    return;
  }
  g_typing_on = 1;
  uint64_t v = g_typed.firstObject.unsignedLongLongValue;
  [g_typed removeObjectAtIndex:0];
  int mac = (int)(v >> 32);
  w32_input_key(mac, 1, (uint32_t)v);
  after_ms(50, ^{
    w32_input_key(mac, 0, 0);
    after_ms(50, ^{ type_next(); });
  });
}
static void type_key(int mac, uint32_t ch) {
  if (!g_typed) g_typed = [NSMutableArray array];
  [g_typed addObject:@((uint64_t)mac << 32 | ch)];
  if (!g_typing_on) type_next();
}

@interface LPTyping : UIView <UIKeyInput>
@property(nonatomic) UITextAutocorrectionType autocorrectionType;
@property(nonatomic) UITextAutocapitalizationType autocapitalizationType;
@property(nonatomic) UITextSpellCheckingType spellCheckingType;
@property(nonatomic) UITextSmartQuotesType smartQuotesType;
@property(nonatomic) UITextSmartDashesType smartDashesType;
@property(nonatomic) UITextSmartInsertDeleteType smartInsertDeleteType;
@property(nonatomic) UIKeyboardType keyboardType;
@property(nonatomic) UIReturnKeyType returnKeyType;
@end
@implementation LPTyping
- (instancetype)initWithFrame:(CGRect)f {
  if (!(self = [super initWithFrame:f])) return nil;
  self.autocorrectionType = UITextAutocorrectionTypeNo;
  self.autocapitalizationType = UITextAutocapitalizationTypeNone;
  self.spellCheckingType = UITextSpellCheckingTypeNo;
  self.smartQuotesType = UITextSmartQuotesTypeNo;
  self.smartDashesType = UITextSmartDashesTypeNo;
  self.smartInsertDeleteType = UITextSmartInsertDeleteTypeNo;
  self.keyboardType = UIKeyboardTypeASCIICapable;
  self.returnKeyType = UIReturnKeyDone;
  return self;
}
- (BOOL)canBecomeFirstResponder {
  return YES;
}
- (BOOL)hasText {
  return YES;
}
- (void)insertText:(NSString *)text {
  for (NSUInteger i = 0; i < text.length; i++) {
    unichar c = [text characterAtIndex:i];
    if (c == '\n' || c == '\r') {
      type_key(0x24, '\r');
      [g_view becomeFirstResponder];
      continue;
    }
    int k = char_key(c);
    if (k >= 0) type_key(k, c);
  }
}
- (void)deleteBackward {
  type_key(0x33, 8);  // Backspace
}
@end

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
  UIPanGestureRecognizer *wheel = [[UIPanGestureRecognizer alloc] initWithTarget:v action:@selector(wheel:)];
  wheel.minimumNumberOfTouches = 2;
  wheel.maximumNumberOfTouches = 2;
  wheel.cancelsTouchesInView = NO;
  [v addGestureRecognizer:wheel];
  self.view = v;
  g_view = v;
  g_typing = [[LPTyping alloc] initWithFrame:CGRectMake(0, 0, 1, 1)];
  [v addSubview:g_typing];
  g_bar = [[LPKeyBar alloc] initWithFrame:v.bounds];
  g_bar.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
  [v addSubview:g_bar];
}
- (void)menu:(UITapGestureRecognizer *)g {
  if (g.state != UIGestureRecognizerStateEnded) return;
  finger_drop();
  g_bar_flip = !g_bar_flip;
  bar_update();
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
      fprintf(stderr, "lp: the song folder can't be read now (%s): choose it again in the menu (the key bar's MORE)\n",
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

static void back_to_game(void);

@interface LPFolderPicker : NSObject <UIDocumentPickerDelegate>
@end
@implementation LPFolderPicker
- (void)documentPickerWasCancelled:(UIDocumentPickerViewController *)p {
  back_to_game();
}
- (void)documentPicker:(UIDocumentPickerViewController *)p didPickDocumentsAtURLs:(NSArray<NSURL *> *)urls {
  back_to_game();
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

// the game's view takes the keys again once a sheet of the app's has gone
static void back_to_game(void) {
  finger_drop();
  [g_view becomeFirstResponder];
}

static void show_menu(void) {
  UIViewController *vc = g_window.rootViewController;
  if (vc.presentedViewController) return;
  finger_drop();
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
                                          back_to_game();
                                        }]];
  [m addAction:[UIAlertAction actionWithTitle:@"Cancel"
                                        style:UIAlertActionStyleCancel
                                      handler:^(UIAlertAction *x) { back_to_game(); }]];
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
