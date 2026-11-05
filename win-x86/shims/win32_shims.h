// The Win32 seam of the Learn & Play build: the host side of every import no translated image
// exports (kernel32, user32, ws2_32, ...), found by name.
//
// A shim is an rc_fn: it runs with the arguments at guest esp (no return address: the import
// thunk or the GetProcAddress cell took it) and removes them itself when the function is stdcall
// (every Win32 API but the few cdecl ones, wsprintf and the like).
//
// Each DLL's shims are one w32_dll table (win32_<dll>.c); win32_dispatch.c finds an import
// "dll!name" in them, plus Steam (steam32_import), and serves both the game's rc32_import_call
// and the DLLs' rc32_host_import. GetProcAddress of a DLL the host provides hands out a cell
// that runs the same shim (w32_proc_cell).
#ifndef WIN32_SHIMS_H
#define WIN32_SHIMS_H

#include <stdint.h>
#include <string.h>

#include "rc32.h"
#include "win32_guestobj.h"

typedef struct { const char *name; rc_fn fn; } w32_export;
typedef struct w32_dll {
  const char *dll;            // lower case, with the extension: "kernel32.dll"
  const w32_export *fns;
  int n;
  const struct w32_dll *more;  // the rest of the DLL's shims, from another file (kernel32)
} w32_dll;
#define W32_DLL(var, dllname, table, more) \
  const w32_dll var = {dllname, table, (int)(sizeof table / sizeof table[0]), more}

// --- dispatch (win32_dispatch.c)
// The shim of an import "dll!name" (DLL part without case), or 0.
rc_fn w32_import(const char *import_name);
// Install rc32_host_import and the process-wide hooks; once, before any guest code runs.
void w32_init(void);
// End the process from the game's side: the shader cache saved, the streams flushed, _exit.
_Noreturn void w32_host_exit(int code);
// A guest address that runs fn when the guest calls it (GetProcAddress of a host DLL's function).
uint32_t w32_proc_cell(const char *name, rc_fn fn);

// --- modules (win32_kernel32.c)
// The handle of a DLL the host provides ("user32.dll", "d3d9"), 0 when it provides none of that
// name. A handle is a small block of guest memory with an MZ header, as code may peek at it.
uint32_t w32_host_module(const char *dll);
// The DLL name ("user32.dll") of a host module handle, or 0.
const char *w32_host_module_name(uint32_t h);

// --- the display (win32_user32.c)
// The desktop size the game sees: the host's main screen in points, or in pixels with
// RS_METAL_RETINA=1 (lp_screen_size, win-x86/app/lp_window.m); RS_D3D9_DISPLAY=WxH overrides.
void w32_display_size(int32_t *w, int32_t *h);

// --- input (win32_input.c): from the host window (win-x86/app/lp_window.m) to the game
void w32_input_key(int mac_keycode, int down, uint32_t ch);
void w32_input_mouse_move(int32_t x, int32_t y, int32_t dx, int32_t dy);  // client pixels
void w32_input_mouse_wheel(int32_t delta);
void w32_input_mouse_button(int button, int down);
void w32_input_release_all(void);  // focus lost: every key and button up
void w32_input_focus(int active);  // the window gained or lost the keyboard
void w32_user_focus(int active);   // win32_user32.c: WM_ACTIVATEAPP, WM_ACTIVATE
void w32_input_view_height(int32_t h);  // the back buffer's: input_sink has the Mac's y, from the bottom
void w32_input_frame(void);  // the game thread, once a frame: what the mods' input_sink and set_app_focus hold back
void w32_input_script(void);  // LP_INPUT_SCRIPT
void w32_input_keyboard_state(uint32_t g, uint32_t cb);
void w32_input_mouse_state(uint32_t g, uint32_t cb);
uint32_t w32_input_events(int mouse, uint32_t dst, uint32_t cb, uint32_t n, uint32_t flags);
uint32_t w32_input_vk(uint32_t vk, int async);
void w32_input_keyboard_vk(uint32_t g);
void w32_input_cursor(int32_t *x, int32_t *y);

// --- paths (win32_kernel32.c)
// The host path of a Windows path the guest names: "C:\Rocksmith2014\..." (the game folder, also
// the starting current directory) maps into LP_GAME_DIR, every other drive path into
// LP_USER_DIR/<drive letter>/...; relative paths are taken from the current directory. A host
// path (starting with '/') is kept as it is (the Steam shim hands those out). Returns host.
char *w32_path(const char *win, char *host, size_t n);
// The Windows path of the game's own folder, "C:\Rocksmith2014" (no trailing separator).
const char *w32_game_dir_win(void);

// --- strings
// UTF-16 guest string -> UTF-8 host buffer (n bytes, NUL-terminated). Returns out.
char *w32_wstr(uint32_t g, char *out, size_t n);
// UTF-8 -> UTF-16 into guest memory at g (cap code units, NUL-terminated when it fits); returns
// the code units written, not counting the NUL.
uint32_t w32_put_wstr(uint32_t g, uint32_t cap, const char *s);

// --- argument helpers
static inline uint32_t w32_arg(rc_ctx *c, int k) { return w32_rd32((uint32_t)(c->r[4] + 4 * (uint32_t)k)); }
// eax = v; a stdcall shim removes its n argument words
static inline void w32_ret(rc_ctx *c, uint32_t v, int n) { c->r[0] = v; c->r[4] = (uint32_t)(c->r[4] + 4 * (uint32_t)n); }
static inline uint32_t *w32_teb(rc_ctx *c, uint32_t off) { return (uint32_t *)RC32_H(c->fs + off); }
static inline void w32_set_error(rc_ctx *c, uint32_t e) { *w32_teb(c, RC32_TEB_LASTERR) = e; }
static inline const char *w32_cstr(uint32_t g) { return g ? (const char *)RC32_H(g) : 0; }

// Windows error codes the shims use
#define W32_ERROR_FILE_NOT_FOUND 2
#define W32_ERROR_PATH_NOT_FOUND 3
#define W32_ERROR_ACCESS_DENIED 5
#define W32_ERROR_INVALID_HANDLE 6
#define W32_ERROR_NOT_ENOUGH_MEMORY 8
#define W32_ERROR_NO_MORE_FILES 18
#define W32_ERROR_NOT_SUPPORTED 50
#define W32_ERROR_FILE_EXISTS 80
#define W32_ERROR_INVALID_PARAMETER 87
#define W32_ERROR_INSUFFICIENT_BUFFER 122
#define W32_ERROR_MOD_NOT_FOUND 126
#define W32_ERROR_PROC_NOT_FOUND 127
#define W32_ERROR_ALREADY_EXISTS 183
#define W32_ERROR_ENVVAR_NOT_FOUND 203
#define W32_ERROR_NO_MORE_ITEMS 259
#define W32_ERROR_DEVICE_NOT_CONNECTED 1167
#define W32_ERROR_CLASS_ALREADY_EXISTS 1410

// Native overrides of msvcr100's math exports (win32_crtmath.c): _CIpow & co., ceil, floor, ...
// Registers them with rc32_override_add; call before rc32_images_bind. LP_NATIVE_MATH=0 leaves
// them out (the translated CRT runs). Returns how many it registered. w32_crtmath_fn: one of
// them by export name, 0 when it has none (tests).
int w32_crtmath_register(void);
rc_fn w32_crtmath_fn(const char *name);

// Debug output (LP_LOG): 0 quiet, 1 first call of each import, 2 every call
extern int w32_log;

// Frame-time log (win32_frame.c, LP_FRAME_LOG=MS): a frame longer than MS ms is logged with the
// events of its time. lp_frame_event(kind, ns, amount): one event (amount 0 counts 1) that took
// ns; lp_frame_present at the start of each Present. LP_FRAME_ON() guards the timing calls.
enum {
  LPF_PROGRAM, LPF_SHADER, LPF_TEXTURE, LPF_UPLOAD, LPF_BUFFER, LPF_FILE_READ, LPF_FILE_OPEN, LPF_WAIT,
  LPF_SLEEP, LPF_LOCK, LPF_IMPORT, LPF_LOOKUP_MISS, LPF_RAISE, LPF_PRESENT, LPF_DRAW, LPF_ALLOC, LPF_KINDS
};
extern int lp_frame_on;  // -1 until first used
void lp_frame_event(int kind, uint64_t ns, uint64_t amount);
void lp_frame_present(void);
void lp_frame_call(const char *name, uint64_t ns);  // one call (COM method, import) that took ns
uint64_t lp_frame_now(void);
#define LP_FRAME_ON() (lp_frame_on != 0)

// Events from host code (win32_kernel32.c), for host threads that stand in for a Windows driver
// (the CoreAudio I/O thread of the WASAPI face, win-x86/shims/audio): SetEvent / ResetEvent /
// PulseEvent on a guest event HANDLE from any thread, without a guest context. 1 when handle is an
// event.
// Safe on a real-time thread in the sense SetEvent is: two short mutex sections, no allocation.
int w32_event_signal(uint32_t handle);
int w32_event_reset(uint32_t handle);
int w32_event_pulse(uint32_t handle);
// WaitForMultipleObjects on host values (events; other handles count as signalled).
uint32_t w32_wait_multiple(uint32_t n, const uint32_t *handles, int all, uint32_t timeout);

#endif
