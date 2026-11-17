// The FFT setups the Mac game's import thunks ask the built-in fixes for (overrides/mods/fftsetup.c), without a
// game: one of each size is made and handed out again, a destroy of a kept one is not the system's to do, and
// with the fixes off, or a size out of the table's, the game's own call makes its setup.
#include <dlfcn.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>

#include "mods/mods_shared.h"

int rs_mods_fftsetup(uint64_t log2n, int32_t radix, void **out);
int rs_mods_fftsetup_kept(const void *setup);

rs_mods_config rs_mods_cfg = {.fixes = 1, .log = 1};
static int g_lines;
void rs_mods_logf(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  g_lines++;
  va_end(ap);
}

static int g_fail;
#define CHECK(c) do { if (!(c)) { g_fail++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static void *ask(void *arg) {
  void *s = 0;
  rs_mods_fftsetup(12, 0, &s);
  return s;
}

int main(void) {
  void *a = 0, *b = 0, *c = 0, *d = 0;
  rs_mods_cfg.fixes = 0;  // off: the game's own call
  CHECK(!rs_mods_fftsetup(10, 0, &a) && !a && !g_lines);
  rs_mods_cfg.fixes = 1;
  CHECK(rs_mods_fftsetup(10, 0, &a) && a && g_lines == 1);
  CHECK(rs_mods_fftsetup(10, 0, &b) && b == a && g_lines == 1);  // a song later: the same one, nothing made
  CHECK(rs_mods_fftsetup(11, 0, &c) && c && c != a && g_lines == 2);
  CHECK(rs_mods_fftsetup(10, 1, &d) && d && d != a);  // another radix is another setup
  CHECK(rs_mods_fftsetup_kept(a) && rs_mods_fftsetup_kept(c) && rs_mods_fftsetup_kept(d));
  CHECK(!rs_mods_fftsetup_kept(&a) && !rs_mods_fftsetup_kept(0));  // one of the game's own, and none
  void *e = (void *)1;
  CHECK(!rs_mods_fftsetup(40, 0, &e) && !rs_mods_fftsetup(10, 3, &e) && !rs_mods_fftsetup(10, -1, &e) && e == (void *)1);
  // every thread that asks for a size gets the one setup
  pthread_t t[8];
  void *got[8];
  int lines = g_lines;
  for (int i = 0; i < 8; i++) pthread_create(&t[i], 0, ask, 0);
  for (int i = 0; i < 8; i++) pthread_join(t[i], &got[i]);
  for (int i = 0; i < 8; i++) CHECK(got[i] && got[i] == got[0]);
  CHECK(g_lines == lines + 1);
  // a kept setup is the system's own, and works: a transform of an impulse is flat
  {
    typedef struct { float *realp, *imagp; } split;
    void (*fft)(void *, const split *, long, unsigned long, int) =
        (void (*)(void *, const split *, long, unsigned long, int))dlsym(RTLD_DEFAULT, "vDSP_fft_zip");
    float re[1024] = {1}, im[1024] = {0};
    split z = {re, im};
    CHECK(fft != 0);
    if (fft) fft(a, &z, 1, 10, 1);
    CHECK(re[0] == 1 && re[511] == 1 && re[1023] == 1 && im[7] == 0);
  }
  printf(g_fail ? "test_fftsetup: %d failure(s)\n" : "test_fftsetup: ok\n", g_fail);
  return g_fail != 0;
}
