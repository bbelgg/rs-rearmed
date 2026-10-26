// Runtime-internal interfaces (image loader, threads, trampolines,
// setjmp). Not included by generated code; generated code declares what it
// needs inline (see xlat/calls_special.py).
#ifndef RC_RT_H
#define RC_RT_H
#include "rc.h"

// --- image (rc_image.c)
extern uint64_t rc_img_lo, rc_img_hi;  // host range of the mapped image
static inline int rc_in_image(uint64_t host) { return host >= rc_img_lo && host < rc_img_hi; }

typedef struct {
  uint64_t binds, lazy_binds, weak_binds;  // bound slots per table
  uint64_t bound_self;                     // resolved from the image's own export trie
  uint64_t bound_dl;                       // resolved with dlsym
  uint64_t unresolved;                     // slots left unbound (weak: left at their own address)
  uint64_t unresolved_syms;                // distinct unresolved symbols
  uint64_t selrefs;                        // __objc_selrefs entries replaced by sel_registerName
  uint64_t classrefs, classrefs_internal, classrefs_external, classrefs_null;  // __objc_classrefs
  uint64_t superrefs, superrefs_internal, superrefs_external, superrefs_null;  // __objc_superrefs
  uint64_t cfstrings, cfstring_isa_bound;  // __cfstring entries / those whose isa is non-null
  uint64_t mod_inits, mod_terms;
} rc_image_stats;
extern rc_image_stats rc_stats;

// __mod_init_func / __mod_term_func: host addresses of the initializers. Each
// is `void (*)(int argc, char **argv, char **envp, char **apple)` on x86; run
// them translated with rc_call_guest. Returns the count.
int rc_image_inits(const uint64_t **list);
int rc_image_terms(const uint64_t **list);

// --- threads (rc_thread.c)
typedef struct rc_jmp_table rc_jmp_table;
typedef struct rc_thread {
  rc_ctx ctx;  // first member: rc_thread_ctx() == (rc_ctx *)rc_thread_self()
  uint8_t *stack_lo;  // mapping (guard page at the bottom)
  size_t stack_size;
  rc_jmp_table *jmps;
  uint64_t tid;
  struct rc_cgrec *cg;  // live native->guest calls of this thread, outermost first
  uint32_t ncg, capcg;
  uint64_t nstack_lo, nstack_hi;  // native stack of the thread (frame-chain sanity check)
} rc_thread;
rc_thread *rc_thread_self(void);

// Call the translated function at host address guest_fn from native code.
// Integer args: ints[0..5] -> rdi rsi rdx rcx r8 r9, ints[6..] on the guest
// stack; dbls[0..7] (raw 64-bit patterns; a float goes in the low 32 bits) ->
// xmm0..7. Returns rax; *xmm0_out receives the low 64 bits of xmm0.
uint64_t rc_call_guest(uint64_t guest_fn, const uint64_t *ints, int nint, const uint64_t *dbls,
                       int ndbl, uint64_t *xmm0_out);
// Same, also returning rdx and xmm1: out[0..3] = rax, rdx, xmm0.lo, xmm1.lo.
void rc_call_guest_ex(uint64_t guest_fn, const uint64_t *ints, int nint, const uint64_t *dbls,
                      int ndbl, uint64_t out[4]);

// The general form: integer arguments 0..5 in `gi` (rdi..r9, ng of them given), SSE arguments in
// dv[0..7], `ns` 8-byte words on the guest stack (in argument order, the first at rsp+8 as seen by
// the callee), al = nd. out[0..3] as above.
void rc_call_guest_regs(uint64_t guest_fn, const uint64_t gi[6], int ng, const uint64_t dv[8], int nd,
                        const uint64_t *stk, int ns, uint64_t out[4]);

// --- native longjmp over guest frames (rc_thread.c)
// rc_call_guest_ex keeps a per-thread stack of (native frame address, saved guest context). A native
// longjmp that skips such a frame leaves its record behind (and the guest context the abandoned guest
// code left). Records are reconciled, exactly, at every point where native code hands control back:
//  * rc_call_guest_ex entry: the arm64 frame-pointer chain tells which records are still ancestors;
//    when none is left the whole context is reset to what it was before the first skipped call;
//  * rc_call_guest_ex exit: everything above this call's record is dead;
//  * rc_unwind_sync(c, anchor): called after a native function returns to a thunk (anchor = address
//    of a local of the thunk's frame). Records below the anchor were skipped by a longjmp whose target
//    was inside the native function: rsp, rbx, rbp, r12-r15 and MXCSR are restored from the oldest
//    skipped record (the state at the moment the native function called back). rc_native_generic and
//    rc_icall_native do this themselves; typed thunks of imports that can run guest code get
//    `rc_unwind_end` as a cleanup (gen_thunks.py: UNWIND_GUARD).
void rc_unwind_sync(rc_ctx *c, const void *anchor);
void rc_unwind_end(rc_ctx **pc);
void rc_cg_changed(rc_thread *t);  // t->ncg changed: update t->ctx.cg_fp

// --- trampoline variants (rc_tramp.c)
// rc_tramp_get(fn) passes x0..x7 / d0..d7 straight through and returns rax/xmm0. The _ex form says
// more about the native caller's view of the function:
#define RC_TR_RET_MASK 15
#define RC_TR_RET_U8 1    // return value narrower than 32 bits: x86 leaves bits 8.. of eax undefined,
#define RC_TR_RET_S8 2    // Apple arm64 callers (clang) assume the callee extended it to 32 bits
#define RC_TR_RET_U16 3
#define RC_TR_RET_S16 4
#define RC_TR_RET_BOOL 5
#define RC_TR_SRET 16     // returns a struct through a hidden pointer: arm64 x8, x86 rdi (args shift by one)
#define RC_TR_VA 32       // variadic callee: arm64 Darwin puts the `...` arguments in stack slots; the
                          // guest gets them per SysV. Bits 8..15 = index of the printf-style format
                          // argument (all named arguments are integers/pointers)
#define RC_TR_VA_FMT(i) (RC_TR_VA | ((unsigned)(i) << 8))
void *rc_tramp_get_ex(uint64_t guest_fn, unsigned flags);
// A pointer that is already a trampoline is re-wrapped from the guest function it runs.
static inline void *rc_tramp_get_sret(uint64_t fn) { return rc_tramp_get_ex(fn, RC_TR_SRET); }
// The guest function a trampoline runs (0 if host is not a trampoline); and the identity otherwise.
uint64_t rc_tramp_guest_of(uint64_t host);
uint64_t rc_tramp_unwrap(uint64_t host);
// `fn` is the open callback of an I/O structure (BINKIO): after it returns, the first `nfields`
// 8-byte fields of the structure at its first argument are guest function pointers the game filled in;
// they are replaced by trampolines so that native code can call them.
void *rc_tramp_get_fixfn(uint64_t fn, int nfields);
// cleanup helpers for generated thunks: unwrap the guest function behind a pointer written by native
// code into *pp (sigaction's old handler), or into the result register (signal).
void rc_unwrap_ptr_end(uint64_t **pp);
void rc_unwrap_ret_end(rc_ctx **pc);

// --- native functions that return a struct through x8 (rc_native.c)
// Guest code calls such a function with the x86 convention (hidden pointer in rdi, other arguments
// shifted). rc_icall_native uses the arm64 one for every registered function.
void rc_native_register_sret(const void *fn);
int rc_native_is_sret(uint64_t fn);

// --- setjmp (rc_setjmp.c); called from code emitted by calls_special.py
// Returns the record whose first RC_JMP_HOST_WORDS words are the host jmp_buf
// and word RC_JMP_VAL the value passed to longjmp.
#define RC_JMP_HOST_WORDS 32
#define RC_JMP_VAL 32
uint64_t *rc_jmp_enter(rc_ctx *c, uint64_t guest_buf);
_Noreturn void rc_longjmp(rc_ctx *c, uint64_t guest_buf, uint64_t val);

// Give a guest C++ object a native shadow vtable of trampolines (rc_tramp.c).
void rc_shadow_vtable(uint64_t obj, int nslots);

// Native stack size given to threads the game creates (rc_thread.c).
size_t rc_thread_min_stack(void);

#endif
