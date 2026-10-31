// 32-bit guests: mapping a PE image into the window, the import area, and dispatch of indirect
// calls (rc32.h).
#include <mach/mach_time.h>
#include <stdio.h>
#include <stdlib.h>

#include "rc32.h"

uint32_t rc32_imp_area;
static uint32_t imp_count;
// the image's static TLS (IMAGE_TLS_DIRECTORY32): template [start, end) + zero fill; rc32_teb.c
// gives every thread its own copy at ThreadLocalStoragePointer[0]
uint32_t rc32_tls_start, rc32_tls_end, rc32_tls_zerofill;

static uint32_t rd32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static uint16_t rd16(const uint8_t *p) { uint16_t v; memcpy(&v, p, 2); return v; }

int rc32_image_load(const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f) { perror(path); return -1; }
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  uint8_t *d = malloc((size_t)n);
  if (!d || fread(d, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(d); return -1; }
  fclose(f);
  int rc = -1;
  if (n < 0x40 || d[0] != 'M' || d[1] != 'Z') goto out;
  uint32_t o = rd32(d + 0x3c);
  if ((long)o + 0xf8 > n || memcmp(d + o, "PE\0\0", 4) || rd16(d + o + 4) != 0x14c) goto out;
  uint16_t nsect = rd16(d + o + 6), optsz = rd16(d + o + 20);
  const uint8_t *opt = d + o + 24;
  uint32_t base = rd32(opt + 28), size = rd32(opt + 56), hdrs = rd32(opt + 60);
  if (rc32_mem_commit(base, size)) { fprintf(stderr, "rc32: cannot map the image at 0x%x\n", base); goto out; }
  memcpy(rc_mem + base, d, hdrs < (uint32_t)n ? hdrs : (uint32_t)n);
  const uint8_t *s = opt + optsz;
  for (int k = 0; k < nsect; k++, s += 40) {
    uint32_t vs = rd32(s + 8), va = rd32(s + 12), rs = rd32(s + 16), ro = rd32(s + 20);
    uint32_t cp = rs < vs || !vs ? rs : vs;
    if ((uint64_t)ro + cp > (uint64_t)n) goto out;
    memcpy(rc_mem + base + va, d + ro, cp);
  }
  if (rd32(opt + 92) > 9 && rd32(opt + 96 + 8 * 9)) {
    const uint8_t *t = rc_mem + base + rd32(opt + 96 + 8 * 9);
    rc32_tls_start = rd32(t);
    rc32_tls_end = rd32(t + 4);
    rc32_tls_zerofill = rd32(t + 16);
    uint32_t zero = 0;
    if (rd32(t + 8)) memcpy(rc_mem + rd32(t + 8), &zero, 4);  // the image's TLS index: 0
  }
  // the IAT: slot of import k -> rc32_imp_area + 4 * k (the area holds `ret` bytes: never run)
  imp_count = (uint32_t)rc32_imports_len;
  rc32_imp_area = rc32_alloc(4 * (imp_count ? imp_count : 1));
  if (!rc32_imp_area) goto out;
  memset(rc_mem + rc32_imp_area, 0xc3, 4 * imp_count);
  for (uint32_t k = 0; k < imp_count; k++) {
    uint32_t v = rc32_imp_area + 4 * k;
    memcpy(rc_mem + rc32_imports[k].slot, &v, 4);
  }
  rc = 0;
out:
  free(d);
  return rc;
}

rc_fn rc32_lookup(uint32_t g) {
  int lo = 0, hi = rc32_table_len - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    uint32_t a = rc32_table[mid].addr;
    if (a == g) return rc32_table[mid].fn;
    if (a < g) lo = mid + 1;
    else hi = mid - 1;
  }
  return 0;
}

// rc32_seh.c: a resume point of a live generated frame (does not return when it is one)
void rc32_resume_try(rc_ctx *c, uint32_t g);

void rc32_icall(rc_ctx *c, uint32_t g) {
  rc_fn f = rc32_lookup(g);
  if (f) {
    f(c);
    return;
  }
  uint32_t k = (g - rc32_imp_area) / 4;
  if (g >= rc32_imp_area && k < imp_count && !(g & 3)) {
    memcpy(&c->ret_to, rc_mem + (uint32_t)c->r[4], 4);
    c->r[4] = (uint32_t)(c->r[4] + 4);  // the thunk wants the arguments at esp
    rc32_imports[k].fn(c);
    return;
  }
  rc32_resume_try(c, g);
  rc_fail("call of an address that is no translated function", g);
}

__attribute__((weak)) uint64_t rc32_rdtsc(void) { return mach_absolute_time(); }

// The 32-bit runtime's rc_fail (rc_image.c has the 64-bit one; a build links one of them). A
// test runner may define its own instead and leave this file's copy out with RC32_NO_FAIL.
#ifndef RC32_NO_FAIL
void (*rc_fail_hook)(const char *why);
_Noreturn void rc_fail(const char *what, uint64_t addr) {
  if (rc_fail_hook) rc_fail_hook(what);
  fprintf(stderr, "rc_fail: %s at 0x%llx\n", what, (unsigned long long)addr);
  abort();
}
#endif
