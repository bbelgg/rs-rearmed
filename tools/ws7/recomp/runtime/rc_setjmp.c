// Recompiler runtime: guest setjmp/longjmp.
//
// setjmp is lowered inline in the generated function (xlat/calls_special.py):
// a host setjmp inside a thunk would return when the thunk returns. The
// generated code calls rc_jmp_enter, which snapshots the guest registers that
// a real setjmp saves, then `if (_setjmp(host)) { FILL(); rax = val; }`.
//
// A record is kept per thread, found through an index stored in the guest
// jmp_buf, so a copied jmp_buf still works. The guest buffer also holds the
// guest registers (what x86 setjmp would have stored), which longjmp restores.
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rc_rt.h"

#define NREC 256
#define MAGIC 0x524a4d5000000000ull  // "RJMP" in the high word

typedef struct {
  uint64_t w[RC_JMP_HOST_WORDS + 2];  // host jmp_buf (256 bytes) ; val ; spare
  uint64_t addr;                       // guest jmp_buf it belongs to
  uint64_t gen;                        // generation (0 = free)
  uint64_t rsp;                        // guest rsp at the setjmp call
  uint32_t ncg;                        // live native->guest call records at the setjmp (rc_thread.c)
} rec;
struct rc_jmp_table {
  rec r[NREC];
  uint64_t next, gen;
};

_Static_assert(sizeof(jmp_buf) <= RC_JMP_HOST_WORDS * 8, "host jmp_buf too large");

void rc_jmp_table_free(rc_jmp_table *t) { free(t); }

// guest buffer layout (uint64 words): 0 magic|index, 1 generation, 2 rbx, 3 rbp,
// 4 r12, 5 r13, 6 r14, 7 r15, 8 rsp. 148 bytes (int[37]) hold 18 words; this uses 9.
uint64_t *rc_jmp_enter(rc_ctx *c, uint64_t gb) {
  rc_thread *t = rc_thread_self();
  if (!t->jmps) t->jmps = calloc(1, sizeof *t->jmps);
  rc_jmp_table *tb = t->jmps;
  int k = -1;
  for (int i = 0; i < NREC; i++)
    if (tb->r[i].gen && tb->r[i].addr == gb) { k = i; break; }  // re-armed buffer: reuse
  if (k < 0) k = (int)(tb->next++ % NREC);
  rec *r = &tb->r[k];
  r->addr = gb;
  r->gen = ++tb->gen;
  r->rsp = c->r[4];
  r->ncg = t->ncg;
  uint64_t *g = (uint64_t *)gb;
  g[0] = MAGIC | (uint64_t)k;
  g[1] = r->gen;
  g[2] = c->r[3];
  g[3] = c->r[5];
  g[4] = c->r[12];
  g[5] = c->r[13];
  g[6] = c->r[14];
  g[7] = c->r[15];
  g[8] = c->r[4];
  return r->w;
}

_Noreturn void rc_longjmp(rc_ctx *c, uint64_t gb, uint64_t val) {
  uint64_t g[9];
  memcpy(g, (void *)gb, sizeof g);
  rc_thread *t = rc_thread_self();
  if ((g[0] >> 32) != (MAGIC >> 32)) rc_fail("longjmp: jmp_buf was not set by setjmp", gb - rc_slide);
  unsigned k = (unsigned)(g[0] & 0xffffffffu);
  rec *r = t->jmps && k < NREC ? &t->jmps->r[k] : 0;
  if (!r || r->gen != g[1]) rc_fail("longjmp: stale jmp_buf (record overwritten or other thread)", gb - rc_slide);
  if (c->r[4] > r->rsp) rc_fail("longjmp: the setjmp frame has returned", gb - rc_slide);
  c->r[3] = g[2];
  c->r[5] = g[3];
  c->r[12] = g[4];
  c->r[13] = g[5];
  c->r[14] = g[6];
  c->r[15] = g[7];
  c->r[4] = g[8];
  // Native->guest calls made since the setjmp and still open are skipped by this jump (their frames
  // are below the setjmp's): drop their records. Their saved contexts are not wanted, the setjmp's
  // registers above are; left behind, the next rc_call_guest would restore one of them.
  if (t->ncg > r->ncg) { t->ncg = r->ncg; rc_cg_changed(t); }
  r->w[RC_JMP_VAL] = (uint32_t)val ? (uint32_t)val : 1;
  _longjmp(*(jmp_buf *)r->w, 1);
}
