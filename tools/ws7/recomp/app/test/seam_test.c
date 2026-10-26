// Unit test of the engine seam (seam_platform.c) without the game: rc_call_guest
// is replaced by a recorder that emulates just enough engine state, and the real
// platform C layer (rsp_engine.c/paths/config/logic) drives the hooks. Checks the
// argument marshaling of every hook, the pre-boot parking of display settings,
// the focus byte, the config mirror, the efd path-hook pointers (including a
// real rc_icall_native call of them), and the big-stack switch.
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "rc_rt.h"
#include "seam.h"

// ---- fakes for the runtime pieces seam_platform.c links against -------------
uint64_t rc_slide;
rc_fn rc_import_thunk(uint64_t h) { (void)h; return 0; }  // rc_native.c's weak reference

#define GA(a) (rc_slide + (uint64_t)(a))
typedef struct { uint64_t va; int n; uint64_t a[8]; } call_t;
static call_t calls[256];
static int ncalls;
static uint8_t sm_mem[0x200];  // fake ServiceManager (quit flag at +0x144)
static int have_sm, frame_quit, fs_val = 2, res_w = 1920, res_h = 1080, ui_accepts = 1;

uint64_t rc_call_guest(uint64_t fn, const uint64_t *ints, int nint, const uint64_t *dbls, int ndbl,
                       uint64_t *xmm0_out) {
  (void)dbls; (void)ndbl; (void)xmm0_out;
  call_t *c = &calls[ncalls++];
  c->va = fn - rc_slide;
  c->n = nint;
  for (int i = 0; i < nint && i < 8; i++) c->a[i] = ints[i];
  switch (c->va) {
    case 0x100179dc0:  // GameBootstrap: Lua owner + MainService + the config block
      *(uint64_t *)GA(0x1018e7478) = 0x1111;
      *(uint64_t *)GA(0x1018e7470) = 0x2222;
      *(uint32_t *)GA(0x101873e14) = 2;      // Fullscreen
      *(uint32_t *)GA(0x101873e18) = 1920;   // ScreenWidth
      *(uint32_t *)GA(0x101873e1c) = 1080;
      *(uint32_t *)GA(0x101873e44) = 1;      // +0x3c no-vsync
      *(uint8_t *)GA(0x101873e4f) = 1;       // +0x47 loaded
      return 0;
    case 0x10017a020: return frame_quit;
    case 0x1002a74f0: return have_sm ? (uint64_t)(uintptr_t)sm_mem : 0;
    case 0x1008bb390: return (uint64_t)fs_val;
    case 0x1008bb850: *(int *)(uintptr_t)ints[0] = res_w; *(int *)(uintptr_t)ints[1] = res_h; return 0;
    case 0x100365e30: return 0xA0;
    case 0x100365e50: return 0xB0;
    case 0x1003151a0: return (uint64_t)ui_accepts | 0xdead00;  // only al counts
    default: return 0;
  }
}

static int checks, fails;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static int nexp;
static void expect(uint64_t va, int n, ...) {  // asserts calls[nexp] and advances
  call_t *c = &calls[nexp];
  CHECK(nexp < ncalls, "missing call 0x%llx", (unsigned long long)va);
  if (nexp >= ncalls) return;
  CHECK(c->va == va, "call %d: va 0x%llx want 0x%llx", nexp, (unsigned long long)c->va, (unsigned long long)va);
  CHECK(c->n == n, "call 0x%llx: nargs %d want %d", (unsigned long long)va, c->n, n);
  __builtin_va_list ap;
  __builtin_va_start(ap, n);
  for (int i = 0; i < n; i++) {
    uint64_t w = __builtin_va_arg(ap, uint64_t);
    CHECK(c->a[i] == w, "call 0x%llx arg %d = 0x%llx want 0x%llx", (unsigned long long)va, i,
          (unsigned long long)c->a[i], (unsigned long long)w);
  }
  __builtin_va_end(ap);
  nexp++;
}
static void done(const char *what) {
  CHECK(nexp == ncalls, "%s: %d unexpected extra guest calls (first va 0x%llx)", what, ncalls - nexp,
        nexp < ncalls ? (unsigned long long)calls[nexp].va : 0ull);
  ncalls = nexp = 0;
}

static int rsp_exit_called;
static void fake_exit(int s) { (void)s; rsp_exit_called++; }

// ---- big stack ----------------------------------------------------------------
static int deep(int n) { volatile char pad[256]; pad[0] = (char)n; return n ? deep(n - 1) + pad[0] * 0 + 1 : 0; }
static struct { uintptr_t sp_a, sp_b; int depth; } st;
static void on_stack_inner(void *p) { (void)p; char x; st.sp_b = (uintptr_t)&x; }
static void on_stack(void *p) {
  char x;
  st.sp_a = (uintptr_t)&x;
  st.depth = deep(200000);  // ~50 MB of frames: far beyond a 512 KB thread stack
  rcapp_on_big_stack(on_stack_inner, p);  // already switched: runs in place
}
static void *thread_main(void *p) { (void)p; rcapp_on_big_stack(on_stack, 0); return 0; }
static void *small_thread(void *p) {
  pthread_attr_t a;
  pthread_attr_init(&a);
  (void)a;
  return thread_main(p);
}

int main(void) {
  size_t map = 0x4000000;
  uint8_t *base = mmap(0, map, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
  rc_slide = (uint64_t)base - 0x101800000ull;  // covers 0x101800000..0x103800000
  rsp_set_exit(fake_exit);

  char root[1024];
  snprintf(root, sizeof root, "%s/seam_test_XXXXXX", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");
  if (!mkdtemp(root)) { perror("mkdtemp"); return 2; }
  rsp_set_hooks(rcapp_seam_hooks());
  const rsp_engine_hooks *H = rsp_hooks();

  // --- before the engine exists: nothing reaches the guest
  rsp_set_display_mode(0, 1600, 900);
  int w, h;
  H->get_resolution(0, &w, &h);
  CHECK(w == 1600 && h == 900, "pre-boot resolution parked: %dx%d", w, h);
  CHECK(rsp_is_fullscreen() == 0, "pre-boot fullscreen parked");
  CHECK(rsp_no_vsync() == 0 || rsp_config_global()->no_vsync == 0, "pre-boot no_vsync");
  H->gl_context(0, (void *)0x1234abcd);
  CHECK(*(uint64_t *)GA(0x1018e7488) == 0x1234abcd, "g_glContext stored");
  rsp_set_focus(1);  // before bootstrap: byte only
  CHECK(*(uint8_t *)GA(0x10194b391) == 1 && rsp_focused() == 1, "focus byte set pre-boot");
  rsp_set_focus(0);
  CHECK(*(uint8_t *)GA(0x10194b391) == 0, "focus byte cleared pre-boot");
  CHECK(!rcapp_seam_booted(), "not booted");
  have_sm = 0;
  rsp_request_quit();
  CHECK(rsp_exit_called == 1, "no ServiceManager: shell exits");
  expect(0x1002a74f0, 0);  // the only guest call so far: the ServiceManager probe of RequestQuit
  done("pre-boot");

  // --- bootstrap
  rsp_boot b = {root, "/cwd", 1, NULL, };
  int r = rsp_game_bootstrap(&b);
  CHECK(r == 0 && rcapp_seam_booted(), "bootstrap ok (%d)", r);
  expect(0x100179dc0, 2, (uint64_t)(uintptr_t)root, (uint64_t)(uintptr_t)"/cwd" == 0 ? 0 : calls[0].a[1]);
  CHECK(strcmp((const char *)(uintptr_t)calls[0].a[1], "/cwd") == 0, "cwd string passed");
  expect(0x1012f6ce0, 6, 0, 0, (uint64_t)(uintptr_t)rsp_path_copy_root, (uint64_t)(uintptr_t)rsp_path_root,
         (uint64_t)(uintptr_t)rsp_path_copy_root, (uint64_t)(uintptr_t)rsp_path_root);
  CHECK(rsp_config_global()->screen_width == 1920 && rsp_config_global()->no_vsync == 1 && rsp_config_global()->loaded,
        "engine config block mirrored into rsp_config_global");
  CHECK(rsp_no_vsync() == 1, "no_vsync reads block +0x3c");
  done("bootstrap");

  // --- the efd path hooks as the image calls them: native pointers through rc_icall_native
  {
    rc_ctx c;
    memset(&c, 0, sizeof c);
    static uint64_t gstack[64];
    c.r[4] = (uint64_t)&gstack[32];
    gstack[32] = 0x5e47e1;  // return address pushed by the guest `call rax`
    char buf[2048];
    c.r[7] = (uint64_t)buf;
    c.r[6] = sizeof buf;
    rc_icall_native(&c, (uint64_t)rsp_path_copy_root);
    char want[1100];
    snprintf(want, sizeof want, "%s/", root);
    CHECK((c.r[0] & 0xff) == 1 && !strcmp(buf, want) && c.r[4] == (uint64_t)&gstack[33],
          "copy-root via rc_icall_native: al=%llu '%s' (want '%s')", (unsigned long long)(c.r[0] & 0xff), buf, want);
    c.r[4] = (uint64_t)&gstack[32];
    c.r[7] = (uint64_t)buf;
    c.r[6] = 3;
    rc_icall_native(&c, (uint64_t)rsp_path_copy_root);
    CHECK((c.r[0] & 0xff) == 0, "copy-root: too small -> false");
    c.r[4] = (uint64_t)&gstack[32];
    rc_icall_native(&c, (uint64_t)rsp_path_root);
    CHECK(!strcmp((const char *)c.r[0], want), "root getter via rc_icall_native");
  }

  // --- display settings after boot
  rsp_set_display_mode(1, 2560, 1440);
  expect(0x1008bb1a0, 2, 2560, 1440);
  expect(0x1008bb350, 1, 1);
  rsp_set_display_mode(0x1ff, 800, 600);  // the byte truncation of 0x10017a1f0 (movzx edi, bl)
  expect(0x1008bb1a0, 2, 800, 600);
  expect(0x1008bb350, 1, 0xff);
  H->set_visual_quality(0, 3);
  expect(0x1008bb670, 1, 3);
  fs_val = 2;
  CHECK(rsp_is_fullscreen() == 1, "IsFullscreen: GetFullscreen != 0");
  expect(0x1008bb390, 0);
  fs_val = 0;
  CHECK(rsp_is_fullscreen() == 0, "IsFullscreen: 0");
  expect(0x1008bb390, 0);
  res_w = 1234; res_h = 567;
  CHECK(rsp_screen_width() == 1234 && rsp_screen_height() == 567, "GetWidth/GetHeight via GetResolution(int*,int*)");
  CHECK(ncalls == nexp + 2 && calls[nexp].va == 0x1008bb850 && calls[nexp + 1].va == 0x1008bb850, "resolution calls");
  CHECK(calls[nexp].n == 2 && calls[nexp].a[0] != 0 && calls[nexp].a[1] != 0 && calls[nexp].a[0] != calls[nexp].a[1], "two out pointers");
  nexp += 2;
  done("display");

  // --- input: native queue, focus gating, GFx code mapping, flush into the sink
  rsp_set_focus(1);
  expect(0x1000e1e70, 1, 1);
  rsp_post_input(RSP_EV_KEYDOWN, 0x00, 0x80000061u);  // Command+a
  rsp_post_input(RSP_EV_MOUSEMOVE, 100, 200);
  rsp_post_input(RSP_EV_SCROLL, 1, 1);
  rsp_post_input(9, 5, 6);  // type >= 6: code -1
  CHECK(ncalls == nexp, "queueing makes no guest call");
  CHECK(rsp_input_pending() == 4, "4 queued");
  r = rsp_game_frame();  // flush, then GameFrame
  CHECK(r == 0, "frame continues");
  expect(0x100ba1190, 3, 1, 0, 0x80000061u);
  expect(0x100ba1190, 3, 3, 100, 200);
  expect(0x100ba1190, 3, 6, 1, 1);
  expect(0x100ba1190, 3, (uint64_t)(uint32_t)-1, 5, 6);
  expect(0x10017a020, 0);
  done("input+frame");

  rsp_set_focus(0);  // SetFocus(0): focus byte, SetAppFocus(0), then the pause chain
  CHECK(*(uint8_t *)GA(0x10194b391) == 0, "focus byte 0");
  expect(0x1000e1e70, 1, 0);
  expect(0x100365e30, 0);
  expect(0x100365e30, 0);
  expect(0x100365e50, 1, 0xA0);
  expect(0x1003151a0, 1, 0xB0);
  expect(0x100314f70, 3, 0xB0, 0x98, 0);
  done("focus lost");
  rsp_post_input(RSP_EV_KEYDOWN, 1, 2);
  CHECK(rsp_input_pending() == 0, "keys dropped without focus");
  ui_accepts = 0;
  rsp_set_focus(0);
  expect(0x1000e1e70, 1, 0);
  expect(0x100365e30, 0);
  expect(0x100365e30, 0);
  expect(0x100365e50, 1, 0xA0);
  expect(0x1003151a0, 1, 0xB0);
  done("focus lost, menu declines");

  // --- quit and shutdown
  have_sm = 1;
  rsp_request_quit();
  expect(0x1002a74f0, 0);
  CHECK(sm_mem[0x144] == 1 && rsp_exit_called == 1, "quit flag set at sm+0x144, shell not exited");
  frame_quit = 1;
  CHECK(rsp_game_frame() == 1, "GameFrame returned quit");
  expect(0x10017a020, 0);
  done("quit");
  rsp_game_shutdown();
  expect(0x10017a0d0, 0);
  done("shutdown");
  CHECK(!rcapp_seam_booted(), "shut down");
  CHECK(rsp_game_frame() == 1 && ncalls == 0, "no frames after shutdown");

  // --- big native stack on a small thread
  pthread_attr_t at;
  pthread_attr_init(&at);
  pthread_attr_setstacksize(&at, 512 * 1024);
  pthread_t t;
  pthread_create(&t, &at, small_thread, 0);
  pthread_join(t, 0);
  CHECK(st.depth == 200000, "deep recursion on the big stack (%d)", st.depth);
  CHECK(st.sp_a && st.sp_b && (st.sp_a > st.sp_b) && (st.sp_a - st.sp_b) < 64 * 1024 * 1024, "inner call stays on the big stack");
  char p[1100];
  snprintf(p, sizeof p, "%s/Rocksmith.ini", root);
  unlink(p);
  snprintf(p, sizeof p, "%s/Tmp", root);
  rmdir(p);
  rmdir(root);
  printf("%d checks, %d failed\n", checks, fails);
  return fails != 0;
}
