// Game names and the guest ABI, for native code that touches the game (overrides, the seam, fixes).
//
// Every game function, global, struct field and vtable slot native code uses has a name; its value for the
// game being built comes from the target's tables (tools/targets/<target>/symbols.map, offsets.map), which
// the build turns into rc_target_names.h (tools/targets/targets.py gen). So the same source builds for
// every target, and the rule for native code is: no literal address, no literal offset, no direct register
// access.
//
//   RC_ADDR(name)        the unslid image address of a function or global (uint64_t constant)
//   RC_GA(name)          its host address now (uint64_t): the image is mapped at RC_A(RC_ADDR(name))
//   RC_FN(name)          the F_ symbol of a function: `void RC_FN(lua_gettop)(rc_ctx *c) { ... }` overrides it
//   RC_OFF(name)         a struct field offset (or a size) from offsets.map
//   RC_SLOT(name)        a vtable slot index (offsets.map: NAME_slot)
//   RC_HOST(g), RC_GUEST(h)  guest pointer -> host pointer, and back
//   RC_RET_ADDR(c)       the return address a translated caller pushed (at the guest stack pointer)
//   RC_FRAME_PTR(c)      the guest frame pointer register
// A name a target has no counterpart for (`unsupported` in its map) has no value there: code that needs it
// does not compile for that target unless it tests `#ifndef RC_UNSUPPORTED_<name>`.
//
// Target ABIs. mac-x64 (RC_TARGET_ABI_SYSV64): guest pointers are host pointers, 8-byte stack slots, the
// SysV argument registers. win-x86 (RC_TARGET_ABI_WIN32) changes only what these macros and rc_ov.h's expand
// to: guest pointers are 32-bit offsets into the reserved 4 GB guest window (RC_HOST adds its base, RC_GUEST
// subtracts it), arguments are read from the stack (plus ecx for thiscall, ecx/edx for fastcall, by
// RC_CC_<name>), and float results go to x87 st0. A hook is shown the SysV call on either (rc_hook.h).
//
// A mod (rc_mod.h, RC_MOD) is built once for every target, so it knows no table: the same macros look the name
// up when the mod runs (rc_off, rc_addr), and a guest pointer's width and base are read from the runtime.
#ifndef RC_ABI_H
#define RC_ABI_H

#include <stdint.h>

#include "rc.h"
#include "rc_cc.h"
#ifndef RC_MOD
#include "rc_target_names.h"
#endif

#ifdef RC_MOD
#include "rc_hook.h"
// Each lookup once per place it is used. A name the target doesn't have logs a line (rc_hook.h) and gives
// RC_NO_OFF or 0: test RC_HAS_OFF(name) first, where the mod can go on without it.
#define RC_MOD_ONCE_(T, get) ({ static T v_; static int got_; \
  if (!__atomic_load_n(&got_, __ATOMIC_ACQUIRE)) { v_ = (get); __atomic_store_n(&got_, 1, __ATOMIC_RELEASE); } v_; })
#define RC_ADDR(name) RC_MOD_ONCE_(uint64_t, rc_addr(#name))
#define RC_OFF(name) RC_MOD_ONCE_(int64_t, rc_off(#name))
#define RC_SLOT(name) RC_MOD_ONCE_(int64_t, rc_off(#name "_slot"))
#define RC_HAS_OFF(name) (RC_OFF(name) != RC_NO_OFF)
#define RC_GA(name) RC_A(RC_ADDR(name))
static inline void *RC_HOST(uint64_t g) {
  if (rc_guest_ptr_size == 8) return (void *)(uintptr_t)g;
  return (uint32_t)g ? (void *)(uintptr_t)(rc_slide + (uint32_t)g) : (void *)0;
}
static inline uint64_t RC_GUEST(const void *h) {
  if (rc_guest_ptr_size == 8 || !h) return (uint64_t)(uintptr_t)h;
  return (uint64_t)(uintptr_t)h - rc_slide;
}
#define RC_RET_ADDR(c) RC_LD(uint64_t, (c)->r[4])
#define RC_FRAME_PTR(c) ((c)->r[5])
#else
// Each takes the name itself (pasted, never macro-expanded first: a name may also be a C macro).
#define RC_ADDR(name) RC_ADDR_##name
#define RC_FN(name) RC_FN_##name
#define RC_CC(name) RC_CC_##name
#define RC_OFF(name) RC_OFF_##name
#define RC_SLOT(name) RC_OFF_##name##_slot
#define RC_GA(name) RC_A(RC_ADDR_##name)

#if defined(RC_TARGET_ABI_SYSV64)
#define RC_HOST(g) ((void *)(uintptr_t)(g))
#define RC_GUEST(h) ((uint64_t)(uintptr_t)(h))
#define RC_RET_ADDR(c) RC_LD(uint64_t, (c)->r[4])
#define RC_FRAME_PTR(c) ((c)->r[5])
#elif defined(RC_TARGET_ABI_WIN32)
#include "rc32.h"
#define RC_HOST(g) RC32_H(g)
#define RC_GUEST(h) ((uint64_t)RC32_G(h))
#define RC_RET_ADDR(c) RC_LD(uint32_t, RC32_H((c)->r[4]))
#define RC_FRAME_PTR(c) ((c)->r[5])
#else
#error "rc_abi.h: no ABI layer for this target"
#endif
#endif

#endif
