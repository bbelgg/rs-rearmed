// Platform shell rewrite: the portable (C) half.
//
// The original shell is the ObjC code in game.shell (AppDelegate, OpenGLView,
// MainController; 0x10017a7c0-0x100186200) plus a thin C++ layer next to it
// (0x100179dc0-0x10017a3b0) through which it reaches the engine, the file-path
// globals of 0x1002a5ad0-0x1002a5ea0, and the Rocksmith.ini code in efd
// (0x1013140f0-0x101315c90).
//
// This header is that C layer, rewritten. Everything the engine does is behind
// rsp_engine_hooks, a table of function pointers: in the native app they call
// the translated engine (tools/ws7/recomp/app/seam_platform.c); the tests use
// stubs that log (rsp_stub_hooks). Addresses
// in comments are the unslid originals each function reproduces.
#ifndef RSP_PLATFORM_H
#define RSP_PLATFORM_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// Input events. The shell posts (type, a, b) triples; 0x10017a080.

enum {
  RSP_EV_KEYDOWN = 0,     // a = keyCode, b = UTF-16 char | 0x80000000 if Command
  RSP_EV_KEYUP = 1,       // a = keyCode, b = UTF-16 char (no Command bit)
  RSP_EV_MOUSEMOVE = 2,   // a = x, b = y (view coordinates, origin bottom-left)
  RSP_EV_MOUSEUP = 3,     // a = button (0 left, 1 right)
  RSP_EV_MOUSEDOWN = 4,   // a = button
  RSP_EV_SCROLL = 5,      // a = 1 if deltaY > 0 else 0, b = 1
  RSP_EV_APP_ACTIVE = 6,  // not queued: calls hooks.app_active(1) (the shell never posts it)
  RSP_EV_APP_INACTIVE = 7 // not queued: calls hooks.app_active(0) (nor this)
};

// GFx code the queue flush hands on (0x100168360): type + 1 for 0..5, else -1.
int rsp_gfx_event_code(int type);

// ---------------------------------------------------------------------------
// The engine boundary.

typedef struct rsp_boot {
  const char *app_dir;  // [[NSBundle mainBundle] bundlePath] minus the last component
  const char *cwd;      // [NSFileManager currentDirectoryPath] (the original ignores it)
  int argc;             // the original passes argc 1, argv {"Rocksmith.app"}
  const char *const *argv;
} rsp_boot;

typedef struct rsp_engine_hooks {
  void *ctx;
  // GameBootstrap's engine half (0x100179dc0 after the path/config steps):
  // efd allocator + ServiceManager singletons, Lua state, VFS (0x1003c8560),
  // MainService_MacOS (ctor 0x100167c60) and its Init (slot 26, 0x100168080).
  // Returns 0 on success.
  int (*bootstrap)(void *ctx, const rsp_boot *boot);
  // One engine frame (0x10017a020 without the input flush): ServiceManager
  // RunOnce (slot 19, 0x1012fd800). Called on the display-link thread with the
  // GL context current and CGL-locked. Returns 1 when the engine has shut
  // down (state 7 / no ServiceManager): the shell then exits.
  int (*frame)(void *ctx);
  // A flushed input event: code (rsp_gfx_event_code), a, b. The original sink
  // 0x100ba1190 updates the engine's key/mouse state tables (engine.nimain
  // 0x101135520/0x101135560/0x101135940/0x101135a00, keyboard table at
  // 0x101922060) and hands the event to the Scaleform movies (0x100b7e6c0/f0).
  void (*input)(void *ctx, int gfx_code, int a, int b);
  // Focus changed, 0x1000e1e70: GRMusicService (0x1018e3538) +0x191 = active,
  // and when InputService (ClassID 0x700000c) has a keyboard (+0xf8 -> +0x58),
  // its key table is cleared (0x101135500).
  void (*app_active)(void *ctx, int active);
  // Focus lost: the current UI menu gets event 0x98 if it accepts it
  // (0x100365e30 → 0x100365e50 → 0x1003151a0 → 0x100314f70).
  void (*focus_lost)(void *ctx);
  // GRDisplaySettings (0x1008bb1a0/0x1008bb350/0x1008bb390/0x1008bb850).
  void (*set_resolution)(void *ctx, int w, int h);
  void (*set_fullscreen)(void *ctx, int mode);  // 0 windowed, else fullscreen
  int (*get_fullscreen)(void *ctx);
  void (*set_visual_quality)(void *ctx, int q);  // 0x1008bb670 (quality presets)
  void (*get_resolution)(void *ctx, int *w, int *h);
  // Rocksmith.ini block +0x3c (not in the file). Nonzero turns the swap
  // interval *off*: customEventTimer keeps numSwapBuffers == !no_vsync.
  int (*no_vsync)(void *ctx);
  // ServiceManager +0x144 = 1 (0x10017a350); return 0 if there is no
  // ServiceManager (the original then _Exit(0)s).
  int (*request_quit)(void *ctx);
  // 0x10017a0d0: MainService slot 16, delete, VFS teardown, lua_close, Cg.
  void (*shutdown)(void *ctx);
  // The NSOpenGLContext (g_glContext 0x1018e7488, read by GRGLRenderer::
  // Initialize 0x101109810). Called once from -[OpenGLView initWithFrame:shareContext:].
  void (*gl_context)(void *ctx, void *nsopenglcontext);
} rsp_engine_hooks;

// The active hook table (defaults to rsp_stub_hooks()).
void rsp_set_hooks(const rsp_engine_hooks *h);
const rsp_engine_hooks *rsp_hooks(void);

// Stub hooks: log every call (RSP_LOG=<file>, default stderr; RSP_LOG=0 off)
// and emulate GRDisplaySettings, the quit flag and the frame counter so the
// shell runs standalone.
const rsp_engine_hooks *rsp_stub_hooks(void);
typedef struct rsp_stub_state {
  int width, height, fullscreen, visual_quality, no_vsync;
  int quit_requested, shut_down, booted;
  long frames;
  int (*frame_cb)(void *user);  // optional extra per-frame work; nonzero = quit
  void (*input_cb)(void *user, int gfx_code, int a, int b);
  void *user;
} rsp_stub_state;
rsp_stub_state *rsp_stub(void);
void rsp_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

// ---------------------------------------------------------------------------
// The C layer the ObjC shell calls (0x100179dc0-0x10017a3b0).

int rsp_game_bootstrap(const rsp_boot *boot);      // 0x100179dc0
int rsp_game_frame(void);                          // 0x10017a020 (1 = quit)
void rsp_post_input(int type, int a, int b);       // 0x10017a080
void rsp_game_shutdown(void);                      // 0x10017a0d0
void rsp_set_focus(int focused);                   // 0x10017a180
int rsp_focused(void);                             // the byte at 0x10194b391
void rsp_set_display_mode(int fs, int w, int h);   // 0x10017a1f0
int rsp_is_fullscreen(void);                       // 0x10017a210
int rsp_no_vsync(void);                            // 0x10017a220
int rsp_screen_width(void);                        // 0x10017a310
int rsp_screen_height(void);                       // 0x10017a330
void rsp_request_quit(void);                       // 0x10017a350 / 0x10017a380
void rsp_set_exit(void (*fn)(int status));         // _Exit by default
void rsp_exit(int status);                         // the shell's _Exit(0)s go here

// The input queue on MainService_MacOS (+0xa8 vector of {type,a,b}, recursive
// mutex at +0xc0): 0x1001681b0 appends, 0x100168360 flushes into hooks.input.
// rsp_post_input only queues while a game exists and the app is focused.
void rsp_input_queue(int type, int a, int b);
int rsp_input_flush(void);  // returns the number of events delivered
int rsp_input_pending(void);
void rsp_input_reset(void);
void rsp_game_set_present(int present);  // 0x1018e7470 != NULL (set by bootstrap)

// ---------------------------------------------------------------------------
// File paths (0x1002a5ad0-0x1002a5e40). One root, three subfolders; efd's
// path hooks (0x1012f6ce0) all answer the root.

#define RSP_PATH_MAX 0x400  // each global is a 1 KiB buffer
// SetGameRoot (0x1002a5cf0): chdir(dir); root = dir + "/"; Tmp/, Data/, Update/
// under it; mkdir(Tmp, 0777). Returns the mkdir result (0 or -1).
int rsp_paths_set_root(const char *dir);
const char *rsp_path_root(void);    // 0x1002a5ad0, 0x1002a5b30, 0x1002a5dc0, 0x1002a5dd0
const char *rsp_path_data(void);    // 0x1002a5ae0, 0x1002a5b00
const char *rsp_path_tmp(void);     // 0x1002a5af0, 0x1002a5b10, 0x1002a5b20
const char *rsp_path_update(void);  // 0x1002a5d80
// efd's GetCurrentDir/GetConfigDir hooks (0x1002a5de0, 0x1002a5e10): root
// into buf; true if it fit. The original passes the root as the *format* of
// efd::Snprintf (0x10130b200), so a '%' in the install path breaks it; this
// copies it verbatim.
int rsp_path_copy_root(char *buf, int size);
// efd IsRelativePath (0x10130a9e0): not NULL and not starting with / or \
// (the empty string is relative).
int rsp_path_is_relative(const char *p);
// Rocksmith.ini location: root + "Rocksmith.ini" (0x101315840).
int rsp_path_ini(char *buf, size_t size);

// ---------------------------------------------------------------------------
// Rocksmith.ini (efd 0x1013140f0 serializer, 0x101315620 key reader,
// 0x101313ea0 load, 0x101315410 save, 0x101315840 load-or-create,
// 0x101315c60 save-if-dirty). Layout = the static block at 0x101873e08.

#pragma pack(push, 1)
typedef struct rsp_config {
  uint8_t enable_microphone;     // +0x00 [Audio] EnableMicrophone
  uint8_t dump_audio_log;        // +0x01 DumpAudioLog
  uint8_t real_tone_cable_only;  // +0x02 RealToneCableOnly
  uint8_t pad3;
  uint32_t latency_buffer;         // +0x04 LatencyBuffer (4)
  uint32_t mono_to_stereo;         // +0x08 MonoToStereoChannel
  uint32_t fullscreen;             // +0x0c [Renderer.MacOS] Fullscreen (2)
  uint32_t screen_width;           // +0x10 ScreenWidth
  uint32_t screen_height;          // +0x14 ScreenHeight
  uint32_t rendering_width;        // +0x18 RenderingWidth
  uint32_t rendering_height;       // +0x1c RenderingHeight
  uint32_t visual_quality;         // +0x20 VisualQuality (1)
  uint32_t enable_post_effects;    // +0x24 (1)
  uint32_t enable_shadows;         // +0x28 (1)
  uint32_t enable_high_res_scope;  // +0x2c (1)
  uint32_t enable_depth_of_field;  // +0x30 (1)
  uint32_t enable_per_pixel_lighting;  // +0x34 (1)
  uint32_t msaa_samples;           // +0x38 (4)
  uint32_t no_vsync;               // +0x3c not in the file (0); see hooks.no_vsync
  uint32_t version;                // +0x40 [Global] Version
  uint8_t use_proxy;               // +0x44 [Net] UseProxy (1)
  uint8_t auto_login;              // +0x45 AutoLogin (1)
  uint8_t dirty;                   // +0x46 save on the next frame
  uint8_t loaded;                  // +0x47 read from a file
  uint8_t disable_browser;         // +0x48 [Renderer.MacOS] DisableBrowser
  uint8_t pad49[3];
} rsp_config;
#pragma pack(pop)
_Static_assert(sizeof(rsp_config) == 0x4c, "rsp_config must match the original block");

void rsp_config_defaults(rsp_config *c);  // the guarded initializer
// Parse an ini held in memory onto *c (keys that are absent keep their
// values), then normalize (Fullscreen 1 or >=3 -> 2, VisualQuality >=4 -> 1,
// Version 0 -> 1 + dirty). Exactly the read path of 0x1013140f0 + 0x101313ea0.
void rsp_config_parse(rsp_config *c, const char *text, size_t len);
// Serialize like the write path; returns the length (excluding the NUL) or
// the size needed if buf is too small.
size_t rsp_config_write(const rsp_config *c, char *buf, size_t size);
// 0x101315840: if root/Rocksmith.ini exists, parse it into *c (from defaults)
// and set c->loaded; otherwise write the defaults there. Returns 1 if a file
// was read.
int rsp_config_load_or_create(rsp_config *c);
int rsp_config_save(const rsp_config *c);       // 0x101315410
void rsp_config_save_if_dirty(rsp_config *c);   // 0x101315c60
rsp_config *rsp_config_global(void);            // the block at 0x101873e08

// ---------------------------------------------------------------------------
// Pure helpers lifted out of the ObjC methods, so they can be unit-tested.

// NSEvent modifier bits the shell looks at.
#define RSP_MOD_SHIFT 0x20000u
#define RSP_MOD_CONTROL 0x40000u
#define RSP_MOD_COMMAND 0x100000u

// -[OpenGLView keyDown:] 0x100184b20: b = char | ((flags << 11) & 0x80000000).
uint32_t rsp_pack_keydown_char(uint16_t ch, uint64_t modifier_flags);
// -[OpenGLView flagsChanged:] 0x100184ab0: forward only while Control is down.
int rsp_flags_changed_forwards(uint64_t modifier_flags);
// -[MainController keyDown:] 0x100185a80: charactersIgnoringModifiers[0] ==
// 'F' and Command+Shift both down toggles full screen.
int rsp_is_fullscreen_toggle(uint16_t ch_ignoring_mods, uint64_t modifier_flags);
// -[MainController mouseMoved:] 0x100185b40: (int64)floorf((float)(v + 0.5)).
int rsp_mouse_coord(double v);
// -[OpenGLView scrollWheel:] 0x100185080: a = deltaY > 0; nothing if deltaY == 0
// (as float). Returns 1 and sets *a if an event is posted.
int rsp_scroll_event(double delta_y, int *a);
// -[MainController awakeFromNib] 0x100185760: aspect ratio from the engine width:
// w16 = the first multiple of 16 >= w, ratio = w16 : (w16 * 9) / 16.
void rsp_aspect_from_width(int w, int *ax, int *ay);
// -[OpenGLView getFrameForTime:] 0x100184260 cursor policy, only while the
// cursor is in the window: returns -1 hide, +1 show, 0 nothing, and updates
// *cursor_visible.
int rsp_cursor_policy(double now, double last_input, int *cursor_visible);
#define RSP_CURSOR_IDLE_SECS 10.0
// -[MainController customEventTimer:] 0x100185bd0 runs four steps on fresh
// window state each; these are their decisions.
enum { RSP_TIMER_NONE = 0, RSP_TIMER_GO_FULLSCREEN = 1, RSP_TIMER_GO_WINDOW = 2 };
// 1. Mode switch: isInFullScreenMode != IsFullscreen() -> goFullScreen/goWindow.
int rsp_timer_mode_switch(int in_fullscreen_mode, int engine_fullscreen);
// 2. Engine full screen: the window frame differs from the main screen -> goFullScreen.
int rsp_timer_fullscreen_refit(double screen_w, double screen_h, double win_w, double win_h);
// 3. Windowed: the content size differs from the engine resolution -> re-seat
//    the view at the content size and push that size to the engine.
int rsp_timer_window_adopt(double content_w, double content_h, int engine_w, int engine_h);
// 4. Swap interval: returns the new numSwapBuffers, or -1 if unchanged.
int rsp_timer_swap(int no_vsync, int num_swap_buffers);

#ifdef __cplusplus
}
#endif
#endif
