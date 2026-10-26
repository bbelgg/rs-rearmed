// Recompiler overrides: adapters between translated guest code and native functions.
//
// An override module defines `void F_<addr>(rc_ctx *c)` for a guest function. The
// translated caller has pushed the return address (c->r[4] points at it), put the
// SysV arguments in rdi rsi rdx rcx r8 r9 / xmm0-7, and further arguments on the guest
// stack above the return address. The adapter reads the arguments, calls the native
// function, writes the result (rax, rdx, xmm0, xmm1) and pops the return address
// (`c->r[4] += 8`), like the callee's `ret` would. It never touches rbx rbp r12-r15
// (a native function preserves them), so the caller's callee-saved state is intact.
//
//   void F_100e0fcf0(rc_ctx *c) {            // lua_gettop
//     OV_ARGS(a, c);
//     lua_State *L = OV_PTR(a, lua_State);
//     OV_RET_I32(c, lua_gettop(L));
//   }
//
// What else a native function can need:
//   * a guest function pointer argument or struct field the native code will call:
//     ov_fn() turns it into a native-callable pointer (the runtime's trampoline pool, or a
//     native alias registered with ov_alias_add);
//   * calls back into translated code: ov_call_guest();
//   * a native function that can longjmp out (Lua errors): OV_PROTECT around it.
#ifndef RC_OV_H
#define RC_OV_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "rc_call.h"
#include "rc_rt.h"

// One adapter, as listed in the generated gen/<module>_adapters.h tables.
typedef struct {
  uint64_t vmaddr;
  const char *name;
  rc_fn fn;
} ov_entry;

// --- arguments -------------------------------------------------------------------
// rc_args_init starts the stack cursor at rsp (right for import thunks, which are
// entered without a pushed return address); an F_<addr> is entered with one.
static inline void ov_args_init(rc_args *a, rc_ctx *c) {
  rc_args_init(a, c);
  a->sp += 8;
}
#define OV_ARGS(a, c) rc_args a; ov_args_init(&a, (c))
#define OV_U64(a) rc_arg_i(&(a))
#define OV_I32(a) ((int32_t)rc_arg_i(&(a)))
#define OV_U32(a) ((uint32_t)rc_arg_i(&(a)))
#define OV_PTR(a, T) ((T *)(uintptr_t)rc_arg_i(&(a)))
#define OV_F64(a) rc_f64(rc_arg_f(&(a)))
#define OV_F32(a) rc_f32((uint32_t)rc_arg_f(&(a)))

// --- results -----------------------------------------------------------------------
// Each pops the return address. A 32-bit result zero-extends into rax, as a write
// to eax does; 8- and 16-bit ones zero-extend as well.
static inline void ov_ret_void(rc_ctx *c) { c->r[4] += 8; }
static inline void ov_ret_u64(rc_ctx *c, uint64_t v) { c->r[0] = v; c->r[4] += 8; }
static inline void ov_ret_u32(rc_ctx *c, uint32_t v) { c->r[0] = v; c->r[4] += 8; }
static inline void ov_ret_f64(rc_ctx *c, double d) { rc_ret_f64(c, d); c->r[4] += 8; }
static inline void ov_ret_f32(rc_ctx *c, float f) { rc_ret_f32(c, f); c->r[4] += 8; }
static inline void ov_ret_rax_rdx(rc_ctx *c, uint64_t a, uint64_t d) { c->r[0] = a; c->r[2] = d; c->r[4] += 8; }
#define OV_RET_I32(c, v) ov_ret_u32((c), (uint32_t)(v))
#define OV_RET_U64(c, v) ov_ret_u64((c), (uint64_t)(v))
#define OV_RET_PTR(c, p) ov_ret_u64((c), (uint64_t)(uintptr_t)(p))

// --- function pointers handed to native code ----------------------------------------
// A native function that stores or calls a callback needs an address it can call. A
// guest function (an address inside the image) is not: the image is mapped as data.
// ov_fn(p) returns p unchanged if it is NULL or outside the image, a registered native
// alias if there is one (ov_alias_add: a guest function that an override replaces by a
// native function with the same C signature, so no trampoline round trip), and else
// rc_tramp_get(p), a native-callable stub that runs the guest function.
void ov_alias_add(uint64_t vmaddr, void *native);
void *ov_fn(uint64_t p);
#define OV_FN(T, p) ((T)ov_fn((uint64_t)(uintptr_t)(p)))
// In-place version for callback fields of a structure that guest code filled in.
static inline void ov_fix_fn(void *field) {
  uint64_t v;
  memcpy(&v, field, 8);
  void *n = ov_fn(v);
  if ((uint64_t)(uintptr_t)n != v) memcpy(field, &n, 8);
}

// A guest `longjmp` target: guest code that was handed the address of the C library's
// longjmp (a bound import) and stored it for a native library to call must instead get
// this, because the jmp_buf was filled by the translated setjmp (rc_setjmp.c).
void ov_longjmp(void *guest_jmp_buf, int val) __attribute__((noreturn));
int ov_is_libc_longjmp(const void *fn);

// --- native code that can longjmp across guest frames --------------------------------
// A native library that unwinds with longjmp (Lua errors) may throw through translated callbacks
// (rc_call_guest frames, trampolines), skipping the code that restores the guest register file, and a
// Lua-level pcall deep inside a call can catch it without any adapter noticing. So every adapter that
// can run user code (any Lua API function that allocates, calls a metamethod or a finalizer) saves
// rsp, rbx, rbp, r12-r15 and MXCSR before the native call and restores them after it, which is what the
// translated caller expects from a callee. (libpng and libjpeg unwind with rc_longjmp, which restores
// the registers from the guest jmp_buf, so their adapters need no protection.)
#define OV_PROTECT_BEGIN(c) \
  const uint64_t ov_sv_[8] = {(c)->r[3], (c)->r[4], (c)->r[5], (c)->r[12], (c)->r[13], (c)->r[14], (c)->r[15], (c)->mxcsr}
// OV_PROTECT_END also drops the rc_call_guest records of the guest calls the longjmp skipped
// (rc_unwind_sync, anchored in this frame). Without it a stale record stays on the thread: at the next
// native -> guest call rc_call_guest_regs "rewinds" the context to the state of the skipped call, which puts
// that call's frame (rsp) over guest frames the translated caller has pushed since.
#define OV_PROTECT_END(c) do { \
    rc_unwind_sync((c), ov_sv_); \
    (c)->r[3] = ov_sv_[0]; (c)->r[4] = ov_sv_[1]; (c)->r[5] = ov_sv_[2]; (c)->r[12] = ov_sv_[3]; \
    (c)->r[13] = ov_sv_[4]; (c)->r[14] = ov_sv_[5]; (c)->r[15] = ov_sv_[6]; (c)->mxcsr = (uint32_t)ov_sv_[7]; } while (0)

// --- native -> guest ------------------------------------------------------------------
// Call the translated function at image address vmaddr (the unslid address, as in the
// comments above each override) with up to 6 integer arguments; returns rax.
static inline uint64_t ov_call_guest(uint64_t vmaddr, const uint64_t *ints, int nint) {
  return rc_call_guest(RC_A(vmaddr), ints, nint, 0, 0, 0);
}
#define OV_GUEST(vmaddr, ...) ({ const uint64_t ov_args_[] = {__VA_ARGS__}; \
    ov_call_guest((vmaddr), ov_args_, (int)(sizeof ov_args_ / sizeof *ov_args_)); })


// A virtual call on an object that is either translated guest code's (vtable entries are image
// addresses: run through rc_call_guest) or native (a rewrite's own object: called directly).
// args[0] is `this`; up to 6 integer arguments. Returns rax.
uint64_t ov_vcall_n(uint64_t obj, int slot, const uint64_t *args, int nargs);
#define OV_VCALL(obj, slot, ...) ({ const uint64_t ov_va_[] = {(uint64_t)(obj), ##__VA_ARGS__}; \
    ov_vcall_n((uint64_t)(obj), (slot), ov_va_, (int)(sizeof ov_va_ / sizeof *ov_va_)); })

// Make a guest object callable from native code: replace its vtable pointer by a copy (with the
// RTTI words in front) whose function entries are ov_fn() pointers. The object keeps working for
// translated callers (an indirect call to a trampoline runs the guest function).
void ov_native_vtable(void *obj);

#endif
