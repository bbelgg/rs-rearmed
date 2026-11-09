// Mid points on hand-written code (test/x86/innercases.py drives it): the translated functions of a hand-built PE
// whose mid points (xlat/inner.py) have the hooks below when the second argument is "on", which also switches the
// edits on (the ones that change a range's length, which Unicorn can't be shown). Each function is called
// with ecx = 5, edx = a guest address holding 41; prints `name eax st0` per function.
//
// usage: midrun IMAGE on|off NAME=ADDR...
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rc32.h"
#include "rc_mid.h"

void rc32_import_call(rc_ctx *c, int k) {
  fprintf(stderr, "midrun: import %d\n", k);
  exit(2);
}
uint64_t rc32_rdtsc(void) { return 0; }

static void add(rc_mid_frame *f, void *u) {  // x += 0x100
  int v = rc_mid_value(f->point, "x");
  rc_mid_set(f, v, rc_mid_get(f, v) + 0x100);
}
static void skip(rc_mid_frame *f, void *u) { f->go = rc_mid_cont(f->point, "past"); }
static void flags(rc_mid_frame *f, void *u) { f->flags &= ~0x40u; }  // ZF off
static void mem(rc_mid_frame *f, void *u) {  // the stack slot doubled
  int v = rc_mid_value(f->point, "v");
  rc_mid_set(f, v, rc_mid_get(f, v) * 2);
}
static void x87(rc_mid_frame *f, void *u) { rc_mid_set_f(f, rc_mid_value(f->point, "top"), 5.0); }
static void ptr(rc_mid_frame *f, void *u) {  // a pointer value as a host pointer: what it points to + 1
  uint32_t *p = (uint32_t *)(uintptr_t)rc_mid_get(f, rc_mid_value(f->point, "p"));
  *p += 1;
}
static void byte(rc_mid_frame *f, void *u) { rc_mid_set(f, rc_mid_value(f->point, "lo"), 0x7f); }  // al only

int main(int argc, char **argv) {
  if (argc < 3 || rc32_mem_init() || rc32_image_load(argv[1])) return 1;
  if (!strcmp(argv[2], "on")) {
    memset(rc_edit_on, 1, sizeof rc_edit_on);  // innercases.py's length-changing edits
    static const struct { const char *name; rc_mid_fn fn; } hooks[] = {
        {"mid_add", add}, {"mid_skip", skip}, {"mid_flags", flags}, {"mid_mem", mem},
        {"mid_x87", x87}, {"mid_ptr", ptr}, {"mid_byte", byte}};
    for (size_t i = 0; i < sizeof hooks / sizeof *hooks; i++)
      if (rc_mid_add_name(hooks[i].name, hooks[i].fn, 0) < 0) return 1;
  }
  rc_edits_fix();
  rc_ctx *c = rc32_thread_ctx();
  uint32_t word = (uint32_t)c->r[4] - 0x1000;  // well below the stack the calls use
  for (int i = 3; i < argc; i++) {
    char *eq = strchr(argv[i], '=');
    if (!eq) return 1;
    *eq = 0;
    *(uint32_t *)RC32_H(word) = 41;
    c->fpu.top = 0, c->fpu.tag = 0xff;  // an empty x87 stack for each
    uint32_t eax = rc32_call(c, (uint32_t)strtoul(eq + 1, 0, 16), 0, 0, 5, word, 0);
    double st0 = (c->fpu.tag >> c->fpu.top) & 1 ? 0 : c->fpu.st[c->fpu.top];
    printf("%s 0x%x %g\n", argv[i], eax, st0);
  }
  return 0;
}
