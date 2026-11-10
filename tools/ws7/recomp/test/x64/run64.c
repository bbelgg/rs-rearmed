// Edits and mid points of the 64-bit front end on a made-up Mach-O (test/x64/innercases.py drives it): the image's
// segments mapped at their own addresses, its functions translated with edits and mid points (generated C linked
// in), each run from a state made from a seed. Prints the end state per function, which innercases.py compares with
// Unicorn's run of the original bytes (or of the edited ones), or with the answers it has for the mid points.
//
// usage: run64 IMAGE off|on|hooks NAME=ADDR:SEED...
//   off    the edit switches off, no hooks;  on  every edit switch on;  hooks  the mid points' hooks below
// Output per function: NAME r0..r15 x0lo x1lo HASH (the scratch buffer's FNV-1a), all hex.
#include <fcntl.h>
#include <mach-o/loader.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "rc.h"
#include "rc_mid.h"

#define BUF 0x10100000000ull
#define BUF_LEN 512
#define STACK 0x10200000000ull
#define STACK_LEN 0x10000
#define SENTINEL 0x10300000000ull

uint64_t rc_slide;  // the image is at its own addresses
_Noreturn void rc_fail(const char *what, uint64_t addr) {
  fprintf(stderr, "run64: rc_fail %s at 0x%llx\n", what, (unsigned long long)addr);
  exit(3);
}

static void *map_at(uint64_t a, uint64_t n) {
  void *p = mmap((void *)a, n, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON | MAP_FIXED, -1, 0);
  if (p != (void *)a) {
    fprintf(stderr, "run64: cannot map 0x%llx\n", (unsigned long long)a);
    exit(2);
  }
  return p;
}

static void load(const char *path) {
  int fd = open(path, O_RDONLY);
  struct stat st;
  if (fd < 0 || fstat(fd, &st)) exit(2);
  uint8_t *f = malloc(st.st_size);
  if (read(fd, f, st.st_size) != st.st_size) exit(2);
  close(fd);
  const struct mach_header_64 *h = (const void *)f;
  // one mapping for all segments (x86-64 segments are 4 KB aligned, the host's pages 16 KB), then their bytes
  uint64_t lo = ~0ull, hi = 0;
  for (int pass = 0; pass < 2; pass++) {
    const uint8_t *lc = f + sizeof *h;
    for (uint32_t i = 0; i < h->ncmds; i++, lc += ((const struct load_command *)lc)->cmdsize) {
      const struct segment_command_64 *s = (const void *)lc;
      if (s->cmd != LC_SEGMENT_64 || !s->vmsize || !strcmp(s->segname, "__PAGEZERO")) continue;
      if (pass) {
        memcpy((void *)(uintptr_t)s->vmaddr, f + s->fileoff, s->filesize);
        continue;
      }
      if (s->vmaddr < lo) lo = s->vmaddr & ~0x3fffull;
      if (s->vmaddr + s->vmsize > hi) hi = (s->vmaddr + s->vmsize + 0x3fff) & ~0x3fffull;
    }
    if (!pass) map_at(lo, hi - lo);
  }
  free(f);
}

static uint64_t mix(uint64_t *s) {  // splitmix64, as innercases.py's
  uint64_t z = (*s += 0x9e3779b97f4a7c15ull);
  z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
  z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
  return z ^ (z >> 31);
}

// --- the hooks (innercases.py MIDS has the answers) -------------------------------------------------------------
static int val(rc_mid_frame *f, const char *n) {
  int v = rc_mid_value(f->point, n);
  if (v < 0) exit(4);
  return v;
}
static void h_reg(rc_mid_frame *f, void *u) { rc_mid_set(f, val(f, "v"), rc_mid_get(f, val(f, "v")) + 0x100); }
static void h_xmm(rc_mid_frame *f, void *u) {
  rc_mid_set_f(f, val(f, "s"), rc_mid_get_f(f, val(f, "s")) * 2);
  rc_mid_set_f(f, val(f, "d"), rc_mid_get_f(f, val(f, "d")) + 10);
}
static void h_mem(rc_mid_frame *f, void *u) { rc_mid_set(f, val(f, "v"), rc_mid_get(f, val(f, "v")) * 2); }
static void h_ptr(rc_mid_frame *f, void *u) { *(uint32_t *)(uintptr_t)rc_mid_get(f, val(f, "p")) += 1; }
static void h_flags(rc_mid_frame *f, void *u) { f->flags &= ~0x40u; }  // ZF off
static void h_cont(rc_mid_frame *f, void *u) { f->go = rc_mid_cont(f->point, "past"); }
static void h_byte(rc_mid_frame *f, void *u) { rc_mid_set(f, val(f, "lo"), 0x7f); }
static void h_wide(rc_mid_frame *f, void *u) {  // a 32-bit value in a 64-bit register: zero-extends it
  rc_mid_set(f, val(f, "w"), 0xffffffffu);
}
// a call into the game from the hook: the helper (rax = 3 * rsi; rcx, r8, r11, xmm1 clobbered) is guest code; the
// point puts back what the hook did not set, keeps what it set (a = a + the helper's answer)
uint64_t g_helper;
static void h_call(rc_mid_frame *f, void *u) {
  rc_ctx *c = f->c;
  uint64_t a = rc_mid_get(f, val(f, "a"));
  c->r[4] -= 8;
  *(uint64_t *)(uintptr_t)c->r[4] = SENTINEL;
  ((rc_fn)(uintptr_t)g_helper)(c);
  rc_mid_set(f, val(f, "a"), a + c->r[0]);
}

int main(int argc, char **argv) {
  if (argc < 3) return 1;
  load(argv[1]);
  map_at(BUF, BUF_LEN < 0x4000 ? 0x4000 : BUF_LEN);
  map_at(STACK, STACK_LEN);
  const char *mode = argv[2];
  if (!strcmp(mode, "on")) memset(rc_edit_on, 1, sizeof rc_edit_on);
  if (!strcmp(mode, "hooks")) {
    static const struct { const char *name; rc_mid_fn fn; } hooks[] = {
        {"m_reg", h_reg}, {"m_xmm", h_xmm}, {"m_mem", h_mem}, {"m_ptr", h_ptr}, {"m_flags", h_flags},
        {"m_cont", h_cont}, {"m_byte", h_byte}, {"m_wide", h_wide}, {"m_call", h_call}};
    for (size_t i = 0; i < sizeof hooks / sizeof *hooks; i++)
      if (rc_mid_add_name(hooks[i].name, hooks[i].fn, 0) < 0) return 1;
  }
  rc_edits_fix();
  static rc_ctx ctx;
  rc_ctx *c = &ctx;
  for (int i = 3; i < argc; i++) {
    char *eq = strchr(argv[i], '='), *colon = eq ? strchr(eq, ':') : 0;
    if (!colon) return 1;
    *eq = 0;
    uint64_t addr = strtoull(eq + 1, 0, 16), seed = strtoull(colon + 1, 0, 16), s = seed;
    extern rc_fn run64_lookup(uint64_t);
    rc_fn fn = run64_lookup(addr);
    if (!strcmp(argv[i], "helper")) g_helper = (uint64_t)(uintptr_t)fn;
    if (!fn) {
      fprintf(stderr, "run64: no function 0x%llx\n", (unsigned long long)addr);
      return 1;
    }
    memset(c, 0, sizeof *c);
    for (int k = 0; k < 16; k++) c->r[k] = mix(&s);
    c->r[4] = STACK + STACK_LEN - 0x100 - 8;
    *(uint64_t *)(uintptr_t)c->r[4] = SENTINEL;
    c->r[7] = BUF;
    c->r[6] = mix(&s) % 9 + 1;  // rsi, rdx: small numbers
    c->r[2] = mix(&s) % 9 + 1;
    for (int k = 0; k < BUF_LEN; k += 8) {
      uint64_t w = mix(&s);
      memcpy((void *)(uintptr_t)(BUF + k), &w, 8);
    }
    fn(c);
    uint64_t hsh = 0xcbf29ce484222325ull;
    for (int k = 0; k < BUF_LEN; k++) hsh = (hsh ^ ((uint8_t *)(uintptr_t)BUF)[k]) * 0x100000001b3ull;
    printf("%s", argv[i]);
    for (int k = 0; k < 16; k++) printf(" %llx", (unsigned long long)c->r[k]);
    printf(" %llx %llx %llx\n", (unsigned long long)c->x[0][0], (unsigned long long)c->x[1][0], (unsigned long long)hsh);
  }
  return 0;
}
