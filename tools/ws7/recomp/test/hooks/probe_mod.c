// The mod of the game runs: one dylib, built once, in both editions' mods folders ([rc] mod loaded:
// probe_mod.dylib). It reads every kind of call there is on Learn & Play: a stdcall one with an RC_OFF read, a
// thiscall one, one with a register argument of its own (`custom`), cdecl ones with a string and with a float,
// a fastcall one. It tries every hook point and counts the calls of each. It changes nothing. `[probe]` lines in the
// log show what it saw: the first few of each, then powers of two.
#include <CommonCrypto/CommonDigest.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "rc_mod.h"
#include "rs_overlay.h"

// The SHA-256 of this library's file, in the log: which build ran is then a fact of the run
static void say_hash(void) {
  Dl_info info;
  struct stat st;
  int fd = dladdr((void *)say_hash, &info) ? open(info.dli_fname, O_RDONLY) : -1;
  if (fd < 0 || fstat(fd, &st)) return;
  void *m = mmap(0, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
  unsigned char d[CC_SHA256_DIGEST_LENGTH];
  if (m != MAP_FAILED) {
    CC_SHA256(m, (CC_LONG)st.st_size, d);
    fprintf(stderr, "[probe] file sha256 ");
    for (int k = 0; k < CC_SHA256_DIGEST_LENGTH; k++) fprintf(stderr, "%02x", d[k]);
    fputc('\n', stderr);
    munmap(m, (size_t)st.st_size);
  }
  close(fd);
}

static int often(unsigned long *n) {
  unsigned long k = ++*n;
  return k <= 3 || !(k & (k - 1));
}

// How often each point is called, printed at every power of two frames
static const char *const points[] = {"game_frame", "input_sink", "ak_post_event_name", "too_many_inputs", "set_app_focus",
                                     "ui_event", "rr_speed_ratio", "ak_set_rtpc_name", "camera_set_frustum", "ak_init",
                                     "si_open", "boot_intro_update", "steam_dlc_owned", "frontend_open_file", 0};
static unsigned long counts[16];
static int count_call(rc_ctx *c, void *user) {
  counts[(intptr_t)user]++;
  return 0;
}

// RSBootService's tick: stdcall on Learn & Play, a field by name
static int before_boot(rc_ctx *c, void *user) {
  static uint32_t last = ~0u;
  OV_ARGS(a, c);
  const char *boot = OV_PTR(a, const char);
  uint32_t phase = RC_LD(uint32_t, boot + RC_OFF(boot_phase));
  if (phase != last) fprintf(stderr, "[probe] boot phase %u\n", phase);
  if (last == ~0u) {  // a virtual call: the service is an efd object, and these two leave its count as it was
    OV_VCALL(boot, RC_SLOT(efd_inc_ref));
    OV_VCALL(boot, RC_SLOT(efd_dec_ref));
    fprintf(stderr, "[probe] OV_VCALL: IncRefCount and DecRefCount called\n");
  }
  last = phase;
  return 0;
}

// The Steam wrapper's DLC check: thiscall
static unsigned long dlc_calls;
static int dlc_app, dlc_show;
static int before_dlc(rc_ctx *c, void *user) {
  OV_ARGS(a, c);
  void *steam = OV_PTR(a, void);
  dlc_app = OV_I32(a);
  dlc_show = often(&dlc_calls);
  if (dlc_show) fprintf(stderr, "[probe] steam_dlc_owned #%lu: this %s, app %d\n", dlc_calls, steam ? "set" : "null", dlc_app);
  return 0;
}
static void after_dlc(rc_ctx *c, void *user) {
  if (dlc_show) fprintf(stderr, "[probe] steam_dlc_owned: app %d -> %d\n", dlc_app, (int)(c->r[0] & 0xff));
}

// FrontEnd_OpenFile: its path is in ecx on Learn & Play (`custom`, named in its prototype)
static unsigned long open_calls;
static int before_open(rc_ctx *c, void *user) {
  OV_ARGS(a, c);
  void *out = OV_PTR(a, void);
  const char *path = OV_PTR(a, const char);
  if (often(&open_calls)) fprintf(stderr, "[probe] frontend_open_file #%lu: %s, stream slot %s\n", open_calls, path, out ? "set" : "null");
  return 0;
}
static void after_open(rc_ctx *c, void *user) {
  static unsigned long n;
  const void *out = (const void *)(uintptr_t)c->r[0];
  if (often(&n)) fprintf(stderr, "[probe] frontend_open_file: stream %s\n", out && rc_mod_ptr(out) ? "open" : "none");
}

// Wwise: cdecl, a string; and one with a float among its arguments
static int before_event(rc_ctx *c, void *user) {
  static unsigned long n;
  OV_ARGS(a, c);
  const char *event = OV_PTR(a, const char);
  if (often(&n)) fprintf(stderr, "[probe] ak_post_event_name #%lu: %s\n", n, event);
  return 0;
}
static int before_rtpc(rc_ctx *c, void *user) {
  static unsigned long n;
  OV_ARGS(a, c);
  const char *name = OV_PTR(a, const char);
  float value = OV_F32(a);
  if (often(&n)) fprintf(stderr, "[probe] ak_set_rtpc_name #%lu: %s = %g\n", n, name, value);
  return 0;
}

// Input: the Mac's call on both, whatever carries it
static int before_input(rc_ctx *c, void *user) {
  static unsigned long n;
  OV_ARGS(a, c);
  int code = OV_I32(a), x = OV_I32(a), y = OV_I32(a);
  if (code != 3 || often(&n)) fprintf(stderr, "[probe] input_sink: code %d, %d, 0x%x\n", code, x, y);
  return 0;
}
static int before_focus(rc_ctx *c, void *user) {
  OV_ARGS(a, c);
  fprintf(stderr, "[probe] set_app_focus: %d\n", OV_I32(a) & 0xff);
  return 0;
}

// The guitar input opening: a bool on both, an error code here
static int si_slot;
static int before_si(rc_ctx *c, void *user) {
  OV_ARGS(a, c);
  void *self = OV_PTR(a, void);
  si_slot = OV_I32(a);
  fprintf(stderr, "[probe] si_open: input %s, slot %d\n", self ? "set" : "null", si_slot);
  return 0;
}
static void after_si(rc_ctx *c, void *user) {
  fprintf(stderr, "[probe] si_open: slot %d -> %d\n", si_slot, (int)(c->r[0] & 0xff));
}

// The overlay: a square that shows, read back once
static void overlay(rs_overlay_frame *f, void *user) {
  static int said;
  rs_overlay_fill_rect(f, 40, 40, 32, 32, 0xff00ffffu);
  if (said++) return;
  uint8_t px[4];
  int r = rs_overlay_read_pixels(f, 50, 50, 1, 1, px);
  fprintf(stderr, "[probe] overlay: %dx%d, read %d: %02x %02x %02x\n", f->width, f->height, r, px[0], px[1], px[2]);
}

static unsigned long frames;
static void after_frame(rc_ctx *c, void *user) {  // the Mac's bool, on both: the game is ending
  static int said;
  if ((c->r[0] & 0xff) && !said++) fprintf(stderr, "[probe] game_frame: quit, after %lu frames\n", frames);
}
static int before_frame(rc_ctx *c, void *user) {
  unsigned long n = frames;
  frames = n + 1;
  if (often(&n)) {
    fprintf(stderr, "[probe] game_frame #%lu:", n);
    for (int k = 1; points[k]; k++)
      if (counts[k]) fprintf(stderr, " %s %lu", points[k], counts[k]);
    fputc('\n', stderr);
  }
  return 0;
}

static int before_inputs(rc_ctx *c, void *user) {
  static unsigned long n;
  OV_ARGS(a, c);
  void *manager = OV_PTR(a, void);
  if (often(&n)) fprintf(stderr, "[probe] too_many_inputs #%lu: manager %s\n", n, manager ? "set" : "null");
  return 0;
}

static int before_camera(rc_ctx *c, void *user) {
  static unsigned long n;
  OV_ARGS(a, c);
  void *camera = OV_PTR(a, void);
  const float *f = OV_PTR(a, const float);  // NiFrustum: left, right, top, bottom, near, far: floats in both
  if (often(&n))
    fprintf(stderr, "[probe] camera_set_frustum #%lu: camera %s, %g %g %g %g near %g far %g\n", n, camera ? "set" : "null",
            f[0], f[1], f[2], f[3], f[4], f[5]);
  return 0;
}

__attribute__((constructor)) static void init(void) {
  say_hash();
  fprintf(stderr, "[probe] on %s, guest pointers of %d bytes, boot_phase at 0x%llx\n", rc_target(), rc_guest_ptr_size,
          (unsigned long long)RC_OFF(boot_phase));
  for (intptr_t k = 0; points[k]; k++) rc_hook_add_name(points[k], count_call, NULL, (void *)k);  // some may not be there: a log line, no more
  rc_hook_add_name("boot_intro_update", before_boot, NULL, NULL);
  rc_hook_add_name("steam_dlc_owned", before_dlc, after_dlc, NULL);
  rc_hook_add_name("frontend_open_file", before_open, after_open, NULL);
  rc_hook_add_name("ak_post_event_name", before_event, NULL, NULL);
  rc_hook_add_name("ak_set_rtpc_name", before_rtpc, NULL, NULL);
  rc_hook_add_name("camera_set_frustum", before_camera, NULL, NULL);
  rc_hook_add_name("too_many_inputs", before_inputs, NULL, NULL);
  rc_hook_add_name("game_frame", before_frame, after_frame, NULL);
  rc_hook_add_name("input_sink", before_input, NULL, NULL);
  rc_hook_add_name("set_app_focus", before_focus, NULL, NULL);
  rc_hook_add_name("si_open", before_si, after_si, NULL);
  rs_overlay_register(overlay, NULL);
}
