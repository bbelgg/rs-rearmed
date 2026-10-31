// Differential test runner: the translated side (test/x86/difftest.py drives it).
//
// Linked with the code build/gen_image_pe.py generates for an image and the rc32 runtime (but
// rc32_imports.c: this file traps every import). Reads cases from stdin, runs each translated
// function from the given state and writes the state it ends in to stdout. difftest.py runs the
// same bytes under Unicorn from the same state and compares.
//
// A case ends at the function's return, at its first import call (the import's index; the
// arguments are then at esp), at a fault or rc_fail (both "abnormal": the state is not
// compared), or after a time limit.
//
// Protocol (little-endian u32 unless noted), one case:
//   'C' 'A' 'S' 'E'  func  eax ecx edx ebx esp ebp esi edi  fs  mxcsr
//   fpu: st[8] (f64)  mm[8] (u64)  cw sw (u16)  top tag (u8)  xmm[8] (16 bytes)
//   nwrite { addr len bytes }  ndump { addr len }
// reply: status (0 return, 1 import, 2 abnormal, 3 timeout)  info (import index / fail address)
//   eax..edi  fpu (as above)  xmm[8]  mxcsr  then the bytes of every dump region.
// At start the runner writes 'R' 'U' 'N' '1' imp_area image_base image_size.
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#include "rc32.h"

extern void rc32_resume_reset(void);

static sigjmp_buf env;
static volatile int status;
static volatile uint32_t info;
static rc_ctx *cur;

static int verbose;
_Noreturn void rc_fail(const char *what, uint64_t addr) {
  if (verbose) fprintf(stderr, "rc_fail: %s at 0x%llx\n", what, (unsigned long long)addr);
  info = (uint32_t)addr;
  siglongjmp(env, 2);
}
void rc32_import_call(rc_ctx *c, int k) {
  (void)c;
  info = (uint32_t)k;
  siglongjmp(env, 1);
}
uint64_t rc32_rdtsc(void) { return 0x12345678; }

// RC_TRACE builds (difftest.py --debug): every function entry
void rc_trace_enter(uint64_t addr) { fprintf(stderr, "enter %llx\n", (unsigned long long)addr); }

static void on_signal(int sig, siginfo_t *si, void *uc) {
  (void)uc;
  uint8_t *a = si->si_addr;
  info = (a >= rc_mem && a < rc_mem + (1ull << 32)) ? (uint32_t)(a - rc_mem) : 0xdeadu;
  if (verbose) fprintf(stderr, "signal %d at guest 0x%x\n", sig, info);
  siglongjmp(env, sig == SIGALRM ? 3 : 2);
}

static void rd(void *p, size_t n) {
  if (fread(p, 1, n, stdin) != n) exit(0);
}
static void wr(const void *p, size_t n) { fwrite(p, 1, n, stdout); }
static uint32_t rd32(void) { uint32_t v; rd(&v, 4); return v; }

typedef struct { double st[8]; uint64_t mm[8]; uint16_t cw, sw; uint8_t top, tag; } wire_fpu;

int main(int argc, char **argv) {
  verbose = getenv("RC32_RUNNER_VERBOSE") != 0;
  if (argc < 2 || rc32_mem_init() || rc32_image_load(argv[1])) return 1;
  // the image as loaded, to reset it before every case
  const uint8_t *d = rc_mem;
  uint32_t o, base, size;
  {
    FILE *f = fopen(argv[1], "rb");
    uint8_t h[0x400];
    if (!f || fread(h, 1, sizeof h, f) < 0x200) return 1;
    fclose(f);
    memcpy(&o, h + 0x3c, 4);
    memcpy(&base, h + o + 24 + 28, 4);
    memcpy(&size, h + o + 24 + 56, 4);
  }
  uint8_t *snap = malloc(size);
  memcpy(snap, d + base, size);
  struct sigaction sa;
  memset(&sa, 0, sizeof sa);
  sa.sa_sigaction = on_signal;
  sa.sa_flags = SA_SIGINFO | SA_NODEFER;
  sigaction(SIGSEGV, &sa, 0);
  sigaction(SIGBUS, &sa, 0);
  sigaction(SIGALRM, &sa, 0);
  sigaction(SIGFPE, &sa, 0);
  sigaction(SIGILL, &sa, 0);
  // a host stack large enough for deep guest recursion
  stack_t ss;
  ss.ss_sp = malloc(1 << 16);
  ss.ss_size = 1 << 16;
  ss.ss_flags = 0;
  sigaltstack(&ss, 0);
  sa.sa_flags |= SA_ONSTACK;
  sigaction(SIGSEGV, &sa, 0);
  sigaction(SIGBUS, &sa, 0);
  uint32_t hello[5] = {0x314e5552u, rc32_imp_area, base, size, 0};
  wr(hello, 16);
  fflush(stdout);
  static rc_ctx c;
  cur = &c;
  for (;;) {
    uint32_t magic = rd32();
    if (magic != 0x45534143u) return 2;
    uint32_t func = rd32();
    memset(&c, 0, sizeof c);
    for (int k = 0; k < 8; k++) c.r[k] = rd32();
    c.fs = rd32();
    c.mxcsr = rd32();
    wire_fpu wf;
    rd(&wf, sizeof wf);
    memcpy(c.fpu.st, wf.st, sizeof wf.st);
    memcpy(c.fpu.mm, wf.mm, sizeof wf.mm);
    c.fpu.cw = wf.cw;
    c.fpu.sw = wf.sw;
    c.fpu.top = wf.top;
    c.fpu.tag = wf.tag;
    rd(c.x, 8 * 16);
    memcpy(rc_mem + base, snap, size);
    uint32_t nw = rd32();
    for (uint32_t k = 0; k < nw; k++) {
      uint32_t a = rd32(), n = rd32();
      if (a < base || a >= base + size) rc32_mem_commit(a, n);
      rd(rc_mem + a, n);
    }
    uint32_t nd = rd32(), da[64], dn[64];
    for (uint32_t k = 0; k < nd && k < 64; k++) { da[k] = rd32(); dn[k] = rd32(); }
    rc32_resume_reset();
    rc_fn fn = rc32_lookup(func);
    info = 0;
    struct itimerval it = {{0, 0}, {2, 0}};
    int st = sigsetjmp(env, 1);
    if (st == 0) {
      setitimer(ITIMER_REAL, &it, 0);
      if (!fn) rc_fail("no function", func);
      c.ret_to = 0;
      fn(&c);
      if (c.ret_to != 0xfffffff0u) rc_fail("return to another address", c.ret_to);
    }
    struct itimerval off = {{0, 0}, {0, 0}};
    setitimer(ITIMER_REAL, &off, 0);
    uint32_t rep[2] = {(uint32_t)st, info};
    wr(rep, 8);
    for (int k = 0; k < 8; k++) { uint32_t v = (uint32_t)c.r[k]; wr(&v, 4); }
    memcpy(wf.st, c.fpu.st, sizeof wf.st);
    memcpy(wf.mm, c.fpu.mm, sizeof wf.mm);
    wf.cw = c.fpu.cw;
    wf.sw = c.fpu.sw;
    wf.top = c.fpu.top;
    wf.tag = c.fpu.tag;
    wr(&wf, sizeof wf);
    wr(c.x, 8 * 16);
    wr(&c.mxcsr, 4);
    for (uint32_t k = 0; k < nd; k++) wr(rc_mem + da[k], dn[k]);
    fflush(stdout);
  }
}
