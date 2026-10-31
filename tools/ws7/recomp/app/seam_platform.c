// Engine side of rsp_engine_hooks.
//
// Design:
//  * bootstrap = the translated GameBootstrap 0x100179dc0, whole. It is one
//    monolithic function (SetGameRoot, efd path hooks, Lua owner, LoadConfig,
//    display settings, VFS, MainService_MacOS::Init); splitting it would only
//    re-implement its middle. The native half the shell already ran before the
//    hook (paths, Rocksmith.ini, display hooks) is idempotent with it. Display
//    hooks that arrive before the engine exists are parked, not forwarded.
//  * one config block: the engine's own 0x101873e08 block is authoritative
//    (the translated LoadConfig fills it, SaveIfDirty in RunOnce writes it);
//    after bootstrap it is copied over rsp_config_global() for native readers.
//  * frame = the translated GameFrame 0x10017a020 (state 6/7, quit flag, its
//    own FlushInput of the engine-side queue, RunOnce). Input is queued natively
//    by the shell and flushed into the sink 0x100ba1190 by hooks.input.
//  * the focus byte 0x10194b391 is mirrored by hooks.app_active (SetFocus).
//  * efd path hooks are re-installed with the native rsp_path_* functions,
//    which copy the root verbatim (the original passes it as a format string).
#include <pthread.h>
#include <time.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include "rc.h"
#include "rc_abi.h"
#include "rc_rt.h"
#include "seam.h"

// Game functions and data by name (tools/targets/names/seam.list; addresses, prototypes and offsets in the
// target's symbols.map / offsets.map).

// ---------------------------------------------------------------------------
// Large native stack for non-main threads.

static _Thread_local char *tl_stack_top;
static _Thread_local int tl_on_big;
static size_t stack_bytes(void) {
  static size_t v = (size_t)-1;
  if (v == (size_t)-1) {
    const char *e = getenv("RC_NATIVE_STACK_MB");
    long mb = e && *e ? atol(e) : 256;
    v = mb > 0 ? (size_t)mb << 20 : 0;
  }
  return v;
}

#if defined(__aarch64__)
static void call_on_stack(void *top, void (*fn)(void *), void *arg) {
  register void *x0_ __asm__("x0") = arg;
  register void (*x1_)(void *) __asm__("x1") = fn;
  register void *x2_ __asm__("x2") = top;
  __asm__ volatile(
      "mov x19, sp\n"
      "mov sp, x2\n"
      "blr x1\n"
      "mov sp, x19\n"
      : "+r"(x0_), "+r"(x1_), "+r"(x2_)
      :
      : "x3", "x4", "x5", "x6", "x7", "x8", "x9", "x10", "x11", "x12", "x13", "x14", "x15", "x16",
        "x17", "x19", "x30", "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7", "v16", "v17",
        "v18", "v19", "v20", "v21", "v22", "v23", "v24", "v25", "v26", "v27", "v28", "v29", "v30",
        "v31", "memory", "cc");
}
#endif

void rcapp_on_big_stack(void (*fn)(void *), void *arg) {
  size_t sz = stack_bytes();
#if defined(__aarch64__)
  if (sz && !tl_on_big && !pthread_main_np()) {
    if (!tl_stack_top) {
      size_t pg = 16384;
      char *m = mmap(0, sz + pg, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE | MAP_NORESERVE, -1, 0);
      if (m != MAP_FAILED) {
        mprotect(m, pg, PROT_NONE);  // guard page
        tl_stack_top = m + sz + pg;
      }
    }
    if (tl_stack_top) {
      tl_on_big = 1;
      call_on_stack(tl_stack_top, fn, arg);
      tl_on_big = 0;
      return;
    }
  }
#endif
  (void)sz;
  fn(arg);
}

// ---------------------------------------------------------------------------
// Calls into translated code.

typedef struct {
  uint64_t fn;
  const uint64_t *args;
  int n;
  uint64_t ret;
} gjob;
static void gjob_run(void *p) {
  gjob *j = p;
  j->ret = rc_call_guest(j->fn, j->args, j->n, NULL, 0, NULL);
}
uint64_t rcapp_gcall(uint64_t vmaddr, int nargs, const uint64_t *args) {
  gjob j = {RC_A(vmaddr), args, nargs, 0};
  rcapp_on_big_stack(gjob_run, &j);
  return j.ret;
}
#define G0(name) rcapp_gcall(RC_ADDR_##name, 0, NULL)
#define G(name, ...) ({ const uint64_t a_[] = {__VA_ARGS__}; rcapp_gcall(RC_ADDR_##name, (int)(sizeof a_ / 8), a_); })

// ---------------------------------------------------------------------------
// State

static volatile int g_booted;
// GRDisplaySettings values that arrived before the engine could take them.
static struct { int w, h, fs, q; } g_pend = {1280, 720, 0, 1};

int rcapp_seam_booted(void) { return g_booted; }

// ---------------------------------------------------------------------------
// Hooks

static int h_bootstrap(void *ctx, const rsp_boot *b) {
  (void)ctx;
  if (g_booted) return 0;
  // 0x100179dc0 returns early once the Lua owner is set, so a second call is harmless.
  G(game_bootstrap, (uint64_t)(uintptr_t)b->app_dir, (uint64_t)(uintptr_t)b->cwd);
  if (!*(uint64_t *)RC_GA(lua_owner) || !*(uint64_t *)RC_GA(main_service)) {
    rsp_log("seam: GameBootstrap did not create the Lua owner / MainService_MacOS");
    return -1;
  }
  // efd path hooks with the native functions: copy the root verbatim; the
  // getters return the root. The image calls them through rc_icall, which
  // marshals SysV registers into rc_native_call (signatures are compatible).
  G(efd_set_path_hooks, 0, 0, (uint64_t)(uintptr_t)rsp_path_copy_root, (uint64_t)(uintptr_t)rsp_path_root,
    (uint64_t)(uintptr_t)rsp_path_copy_root, (uint64_t)(uintptr_t)rsp_path_root);
  // The engine's config block is the live one; mirror it for native readers.
  memcpy(rsp_config_global(), (const void *)RC_GA(game_config), sizeof(rsp_config));
  g_booted = 1;
  return 0;
}

static int h_frame(void *ctx) {
  (void)ctx;
  if (!g_booted) return 1;
  return (int)(G0(game_frame) & 0xff);
}

static void h_input(void *ctx, int code, int a, int b) {
  (void)ctx;
  if (!g_booted) return;
  G(input_sink, (uint32_t)code, (uint32_t)a, (uint32_t)b);
}

// SetFocus 0x10017a180, first half: the focus byte, then SetAppFocus.
static void h_app_active(void *ctx, int active) {
  (void)ctx;
  *(uint8_t *)RC_GA(app_focus) = active ? 1 : 0;
  if (g_booted) G(set_app_focus, active ? 1 : 0);
}

// SetFocus second half (focus lost): UI event 0x98 (pause) if the current menu takes it.
static void h_focus_lost(void *ctx) {
  (void)ctx;
  if (!g_booted) return;
  if (!G0(ui_current)) return;
  uint64_t menu = G(ui_menu, G0(ui_current));
  if (!menu) return;
  if (!(G(ui_accepts, menu) & 0xff)) return;
  G(ui_event, menu, 0x98, 0);
}

static void h_set_resolution(void *ctx, int w, int h) {
  (void)ctx;
  g_pend.w = w;
  g_pend.h = h;
  if (g_booted) G(set_resolution, (uint32_t)w, (uint32_t)h);
}
static void h_set_fullscreen(void *ctx, int mode) {
  (void)ctx;
  g_pend.fs = mode;
  if (g_booted) G(set_fullscreen, (uint64_t)(mode & 0xff));
}
static int h_get_fullscreen(void *ctx) {
  (void)ctx;
  return g_booted ? (int)(uint32_t)G0(get_fullscreen) : g_pend.fs;
}
static void h_set_visual_quality(void *ctx, int q) {
  (void)ctx;
  g_pend.q = q;
  if (g_booted) G(set_visual_quality, (uint32_t)q);
}
static void h_get_resolution(void *ctx, int *w, int *h) {
  (void)ctx;
  if (!g_booted) {
    *w = g_pend.w;
    *h = g_pend.h;
    return;
  }
  int tw = 0, th = 0;  // guest memory is host memory: the engine writes through these
  G(get_resolution, (uint64_t)(uintptr_t)&tw, (uint64_t)(uintptr_t)&th);
  *w = tw;
  *h = th;
}

// Block +0x3c; 0x10017a220 minus its guarded initializer (the block is only
// meaningful after LoadConfig).
// RS_NO_VSYNC=1/0 overrides it, starting RS_NO_VSYNC_AFTER seconds (default 30) after the first
// call (customEventTimer:, main thread, every 0.1 s) (1: swap interval 0, the display link alone
// paces the frames). Boot and the loading screens
// keep the original setting: with swap interval 0 from the start, the fast-load cache.psarc stalled
// on its loading screen in 2 of 3 runs.
static int h_no_vsync(void *ctx) {
  (void)ctx;
  static int forced = -2;
  static double after;
  static uint64_t t0;
  if (forced == -2) {
    const char *e = getenv("RS_NO_VSYNC");
    forced = e && *e ? atoi(e) != 0 : -1;
    e = getenv("RS_NO_VSYNC_AFTER");
    after = e && *e ? atof(e) : 30.0;
    t0 = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
  }
  if (!g_booted) return 0;
  if (forced >= 0 && (clock_gettime_nsec_np(CLOCK_UPTIME_RAW) - t0) / 1e9 >= after) return forced;
  uint32_t v;
  memcpy(&v, (const void *)(RC_GA(game_config) + RC_OFF(game_config_vsync)), 4);
  return v != 0;
}

// 0x10017a350: ServiceManager +0x144 = 1; no ServiceManager -> the shell exits.
static int h_request_quit(void *ctx) {
  (void)ctx;
  uint64_t sm = G0(service_manager);
  if (!sm) return 0;
  *(uint8_t *)(sm + RC_OFF(service_manager_quit)) = 1;
  return 1;
}

static void h_shutdown(void *ctx) {
  (void)ctx;
  if (!g_booted) return;
  G0(game_shutdown);
  g_booted = 0;
}

// g_glContext: the NSOpenGLContext, read by GRGLRenderer::Initialize 0x101109810
// (which messages it through objc_msgSend thunks, natively).
static void h_gl_context(void *ctx, void *glctx) {
  (void)ctx;
  *(uint64_t *)RC_GA(gl_context) = (uint64_t)(uintptr_t)glctx;
}

static const rsp_engine_hooks g_hooks = {
    NULL,           h_bootstrap,     h_frame,        h_input,       h_app_active,
    h_focus_lost,   h_set_resolution, h_set_fullscreen, h_get_fullscreen, h_set_visual_quality,
    h_get_resolution, h_no_vsync,    h_request_quit, h_shutdown,    h_gl_context};

const rsp_engine_hooks *rcapp_seam_hooks(void) { return &g_hooks; }
