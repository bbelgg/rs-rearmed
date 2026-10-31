// Runtime support of rc_ov.h.
#include "rc_ov.h"

#include <dlfcn.h>
#include <stdlib.h>
#include <pthread.h>

#define NALIAS 128
static struct { uint64_t vmaddr; void *native; } alias[NALIAS];
static int nalias;

void ov_alias_add(uint64_t vmaddr, void *native) {
  for (int i = 0; i < nalias; i++)
    if (alias[i].vmaddr == vmaddr) { alias[i].native = native; return; }
  if (nalias == NALIAS) rc_fail("ov_alias_add: table full", vmaddr);
  alias[nalias].vmaddr = vmaddr;
  alias[nalias++].native = native;
}

void *ov_fn(uint64_t p) {
  if (!p || !rc_in_image(p)) return (void *)(uintptr_t)p;
  for (int i = 0; i < nalias; i++)
    if (alias[i].vmaddr == p - rc_slide) return alias[i].native;
  return rc_tramp_get(p);
}

void ov_longjmp(void *buf, int val) { rc_longjmp(rc_thread_ctx(), (uint64_t)(uintptr_t)buf, (uint64_t)val); }

static const void *ljf[4];
static void ljinit(void) {
  static const char *const n[4] = {"longjmp", "_longjmp", "siglongjmp", "__longjmp"};
  for (int i = 0; i < 4; i++) ljf[i] = dlsym(RTLD_DEFAULT, n[i]);
}

int ov_is_libc_longjmp(const void *fn) {
  static pthread_once_t once = PTHREAD_ONCE_INIT;
  const void **f = ljf;
  pthread_once(&once, ljinit);
  for (int i = 0; i < 4; i++)
    if (f[i] && f[i] == fn) return 1;
  return 0;
}

uint64_t ov_vcall_n(uint64_t obj, int slot, const uint64_t *args, int nargs) {
  uint64_t fn = ((const uint64_t *)(uintptr_t)*(const uint64_t *)(uintptr_t)obj)[slot];
  if (rc_in_image(fn)) return rc_call_guest(fn, args, nargs, 0, 0, 0);
  uint64_t a[6] = {0, 0, 0, 0, 0, 0};
  for (int i = 0; i < nargs && i < 6; i++) a[i] = args[i];
  return ((uint64_t(*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t))(uintptr_t)fn)(a[0], a[1], a[2], a[3],
                                                                                              a[4], a[5]);
}

// Code words of the image: __text (the vtable entries of a class are function starts in it), from the
// target's target.toml.
#define OV_TEXT_LO RC_TARGET_TEXT_LO
#define OV_TEXT_HI RC_TARGET_TEXT_HI

void ov_native_vtable(void *obj) {
  uint64_t *vt = *(uint64_t **)obj;
  if (!rc_in_image(vt[0])) return;  // already native (a vtable of native functions, wherever it is)
  size_t n = 0;
  while (n < 512) {
    uint64_t v = vt[n];
    int in_text = v >= RC_A(OV_TEXT_LO) && v < RC_A(OV_TEXT_HI);
    // other native pointers (an imported pure-virtual stub) are entries; 0, small or negative
    // numbers (the next vtable's offset-to-top), image data (its typeinfo) end the table
    if (!in_text && (v < 0x1000 || v >= 0xffff000000000000ull || rc_in_image(v))) break;
    n++;
  }
  uint64_t *copy = malloc(8 * (n + 2));
  copy[0] = vt[-2];
  copy[1] = vt[-1];
  for (size_t i = 0; i < n; i++) copy[2 + i] = (uint64_t)(uintptr_t)ov_fn(vt[i]);
  *(uint64_t **)obj = copy + 2;
}
