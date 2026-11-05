// Hook points on a 32-bit guest: the view of a call that rc_hook.h describes, and the runtime calls a mod has
// on the Mac. One mod binary runs on every target, so everything 32-bit stays on this side.
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "rc32_hook.h"
#include "rc32_x87.h"

// A mod is built without RC_GUEST32: what it reads of rc_ctx has to be where a 64-bit guest's is
_Static_assert(offsetof(rc_ctx, x) == 128 && offsetof(rc_ctx, mxcsr) == 384 && offsetof(rc_ctx, cg_fp) == 392,
               "rc_ctx: the part a mod sees moved");

// Generated (table.c): what point `index`'s code removes with its `ret N`, -1 unknown
__attribute__((weak)) int rc_hook_code_pops(int index) {
  (void)index;
  return -1;
}

static inline uint32_t rd32(uint32_t g) {
  uint32_t v;
  memcpy(&v, RC32_H(g), 4);
  return v;
}

static inline void wr32(uint32_t g, uint32_t v) { memcpy(RC32_H(g), &v, 4); }

static uint64_t to_host(uint32_t g) { return g ? (uint64_t)(uintptr_t)RC32_H(g) : 0; }

static int in_window(uint64_t h) {
  uint64_t lo = (uint64_t)(uintptr_t)rc_mem;
  return h >= lo && h - lo < (1ull << 32);
}

// A pointer the guest is to get. It can't hold one outside its window (the mod's own memory): 0 and a log line.
static uint32_t to_guest(const char *name, uint64_t h) {
  if (!h) return 0;
  if (in_window(h)) return RC32_G((void *)(uintptr_t)h);
  fprintf(stderr, "[rc] hook %s: 0x%llx is not in guest memory: the game gets a null pointer\n", name,
          (unsigned long long)h);
  return 0;
}

int rc32_hook_shown(const rc_hook_name *h, int index, int *pops) {
  if (!h || h->nargs < 0) {
    fprintf(stderr, "[rc] hook %s: refused on this game (%s): %s\n", h ? h->name : "?", rc_target(),
            h && h->why ? h->why : "it is not in the target's tables");
    return -1;
  }
  int code = rc_hook_code_pops(index);
  if (code >= 0 && h->pops >= 0 && code != h->pops) {
    fprintf(stderr, "[rc] hook %s: refused: its prototype's arguments are %d bytes, the function removes %d (%s "
            "symbols.map)\n", h->name, h->pops, code, rc_target());
    return -1;
  }
  *pops = h->pops >= 0 ? h->pops : code;
  return 0;
}

// Where each argument goes in the view: integers rdi rsi rdx rcx r8 r9 then the stack, floats xmm0-7
// then the stack, as rc_arg_i and rc_arg_f read them. The same walk finds them again afterwards.
typedef struct {
  int gi, fi, st;
} sysv_walk;
#define IN_R 0
#define IN_X 16
#define IN_FRAME 32
static int next_place(sysv_walk *p, char kind) {
  static const uint8_t order[6] = {7, 6, 2, 1, 8, 9};
  if (kind == 'f' || kind == 'd')
    return p->fi < 8 ? IN_X + p->fi++ : IN_FRAME + p->st++;
  return p->gi < 6 ? IN_R + order[p->gi++] : IN_FRAME + p->st++;
}

static uint64_t place_get(const rc32_view *w, int at) {
  if (at >= IN_FRAME) return w->frame[at - IN_FRAME];
  return at >= IN_X ? w->ctx.x[at - IN_X][0] : w->ctx.r[at];
}

static void place_set(rc32_view *w, int at, uint64_t v) {
  if (at >= IN_FRAME) w->frame[at - IN_FRAME] = v;
  else if (at >= IN_X) w->ctx.x[at - IN_X] = (rc_v128){v, 0};
  else w->ctx.r[at] = v;
}

void rc32_view_in(rc32_view *w, const rc_hook_name *h, rc_ctx *c) {
  w->ctx = *c;
  memcpy(w->regs, c->r, sizeof w->regs);
  w->ret_flags = c->ret_flags;
  uint32_t sp = (uint32_t)c->r[4];
  for (int k = 0; k < 8; k++)
    if (k != 3) w->ctx.r[k] = 0;  // ebx stays, as a callee-saved register does; esp and ebp below
  memset(w->frame, 0, sizeof w->frame);
  w->frame[0] = rd32(sp);
  w->ctx.r[4] = (uint64_t)(uintptr_t)w->frame;
  w->ctx.r[5] = to_host((uint32_t)c->r[5]);
  sysv_walk p = {0, 0, 1};
  for (int k = 0; k < h->nargs; k++) {
    rc_hook_arg a = h->arg[k];
    uint64_t v;
    if (a.kind == 'c') {
      v = (uint32_t)a.value;
    } else if (a.at < 0) {
      v = (uint32_t)c->r[~a.at];
    } else {
      uint32_t g = sp + 4 + 4 * (uint32_t)a.at;
      v = rd32(g);
      if (a.kind == 'q' || a.kind == 'd') v |= (uint64_t)rd32(g + 4) << 32;
    }
    if (a.kind == 'b') v &= 0xff;
    else if (a.kind == 'h') v &= 0xffff;
    else if (a.kind == 'p') v = to_host((uint32_t)v);
    w->in[k] = v;
    if (a.kind != 'x') place_set(w, w->at[k] = next_place(&p, a.kind), v);  // the Mac's call hasn't this one
  }
}

void rc32_view_args_out(rc32_view *w, const rc_hook_name *h, rc_ctx *c) {
  memcpy(c->r, w->regs, sizeof w->regs);
  c->ret_flags = w->ret_flags;
  for (int k = 0; k < h->nargs; k++) {
    rc_hook_arg a = h->arg[k];
    if (a.kind == 'c' || a.kind == 'x') continue;
    uint64_t v = place_get(w, w->at[k]);
    if (v == w->in[k]) continue;
    if (a.kind == 'p') v = to_guest(h->name, v);
    if (a.at < 0) {
      c->r[~a.at] = (uint32_t)v;
    } else {
      uint32_t g = (uint32_t)c->r[4] + 4 + 4 * (uint32_t)a.at;
      wr32(g, (uint32_t)v);
      if (a.kind == 'q' || a.kind == 'd') wr32(g + 4, (uint32_t)(v >> 32));
    }
  }
}

static int in_xmm(char ret) { return ret == 'f' || ret == 'd' || ret == 'r'; }

static void result_set(rc32_view *w, char ret, uint64_t v) {
  if (in_xmm(ret)) w->ctx.x[0] = (rc_v128){v, 0};
  else w->ctx.r[0] = v;
}

static uint64_t result_get(const rc32_view *w, char ret) {
  return in_xmm(ret) ? w->ctx.x[0][0] : w->ctx.r[0];
}

void rc32_view_result_in(rc32_view *w, const rc_hook_name *h, rc_ctx *c) {
  memcpy(w->regs, c->r, sizeof w->regs);
  w->ret_flags = c->ret_flags;
  uint64_t v = 0;
  switch (h->ret) {
    case 'b': v = c->r[0] & 0xff; break;
    case 'h': v = c->r[0] & 0xffff; break;
    case 'i': v = (uint32_t)c->r[0]; break;
    case 'q': v = (uint32_t)c->r[0] | c->r[2] << 32; break;
    case 'p': v = to_host((uint32_t)c->r[0]); break;
    case 'f': v = rc_u32f((float)ST(0)); break;
    case 'd': v = rc_u64f(ST(0)); break;
    case 'r': v = rc_u32f(100.0f / (float)ST(0)); break;
    case 'e': v = rd32((uint32_t)w->in[0] + (uint32_t)h->ret_off) == (uint32_t)h->ret_is; break;
  }
  result_set(w, h->ret, v);
  w->result = v;
  w->ctx.r[4] = (uint64_t)(uintptr_t)&w->frame[1];  // the return address popped
}

// The view's result into the guest's registers. fresh: nothing of the function's is there (a pre hook did the
// call): a float goes on the x87 stack; else it replaces the one the function left.
static void result_out(const rc32_view *w, const rc_hook_name *h, rc_ctx *c, int fresh) {
  uint64_t v = result_get(w, h->ret);
  switch (h->ret) {
    case 'b': case 'h': case 'i': c->r[0] = (uint32_t)v; break;
    case 'q': c->r[0] = (uint32_t)v; c->r[2] = v >> 32; break;
    case 'p': c->r[0] = to_guest(h->name, v); break;
    case 'f': case 'd': case 'r': {
      double d = h->ret == 'f' ? (double)rc_f32((uint32_t)v) : h->ret == 'd' ? rc_f64(v)
                                                             : (double)(100.0f / rc_f32((uint32_t)v));
      if (fresh) rc87_push(c, d);
      else ST(0) = d;
      break;
    }
    case 'e':  // the game has no such result to give: the frame was skipped, or goes as it went
      if (!fresh || v) fprintf(stderr, "[rc] hook %s: this game takes no other result from a hook\n", h->name);
      break;
  }
}

void rc32_view_result_out(rc32_view *w, const rc_hook_name *h, rc_ctx *c) {
  memcpy(c->r, w->regs, sizeof w->regs);
  c->ret_flags = w->ret_flags;
  if (result_get(w, h->ret) != w->result) result_out(w, h, c, 0);
}

void rc32_view_return(rc32_view *w, const rc_hook_name *h, int pops, rc_ctx *c) {
  if (pops < 0) rc_fail("a hook did the call of a function whose `ret N` is not known", h->vmaddr);
  memcpy(c->r, w->regs, sizeof w->regs);
  c->ret_flags = w->ret_flags;
  result_out(w, h, c, 1);
  uint32_t sp = (uint32_t)c->r[4];
  c->ret_to = rd32(sp);
  c->r[4] = sp + 4 + (uint32_t)pops;
}

// --- RC_HOOK_TRACE: a table's address and prototype checked in a run ---------------------------------
void rc32_trace_in(const char *name, int n, rc_ctx *c) {
  uint32_t sp = (uint32_t)c->r[4];
  fprintf(stderr, "[rc] trace %s #%d: return 0x%x ecx 0x%x edx 0x%x esi 0x%x edi 0x%x stack 0x%x 0x%x 0x%x 0x%x\n", name, n,
          rd32(sp), (uint32_t)c->r[1], (uint32_t)c->r[2], (uint32_t)c->r[6], (uint32_t)c->r[7], rd32(sp + 4), rd32(sp + 8),
          rd32(sp + 12), rd32(sp + 16));
}

void rc32_trace_out(const char *name, int n, rc_ctx *c) {
  fprintf(stderr, "[rc] trace %s #%d: eax 0x%x, st0 %g\n", name, n, (uint32_t)c->r[0], ST(0));
}

// --- native -> guest, for a mod ---------------------------------------------------------------------
// An argument to the guest: a host pointer into its window as the guest's address, anything else cut to a word
static uint32_t guest_word(uint64_t v) { return in_window(v) ? RC32_G((void *)(uintptr_t)v) : (uint32_t)v; }

uint64_t rc32_call_cc(uint32_t g, int cc, const char *name, const uint64_t *ints, int nint) {
  uint32_t a[16], ecx = 0, edx = 0;
  int k = 0, n = 0;
  if (cc == RC_CC_CUSTOM || nint > 16) {
    fprintf(stderr, "[rc] call %s: %s\n", name, cc == RC_CC_CUSTOM ? "it takes its arguments in its own registers"
                                                                   : "more than 16 arguments");
    return 0;
  }
  if ((cc == RC_CC_THISCALL || cc == RC_CC_FASTCALL) && k < nint) ecx = guest_word(ints[k++]);
  if (cc == RC_CC_FASTCALL && k < nint) edx = guest_word(ints[k++]);
  while (k < nint) a[n++] = guest_word(ints[k++]);
  return rc32_call(rc32_thread_ctx(), g, a, n, ecx, edx, 0);
}

// The Mac runtime's calls a mod can link. A guest function given by address has no convention to go by:
// arguments on the stack, cdecl and stdcall; rc_call_name knows the others.
uint64_t rc_call_guest(uint64_t fn, const uint64_t *ints, int nint, const uint64_t *dbls, int ndbl,
                       uint64_t *xmm0_out) {
  (void)dbls;
  if (ndbl) fprintf(stderr, "[rc] call: 0x%llx: floating point arguments are not passed on a 32-bit guest\n",
                    (unsigned long long)fn);
  if (xmm0_out) *xmm0_out = 0;
  return ndbl ? 0 : rc32_call_cc(guest_word(fn), RC_CC_CDECL, "?", ints, nint);
}

rc_ctx *rc_thread_ctx(void) { return rc32_thread_ctx(); }
