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
// SysV argument registers. The win-x86 layer (RC_TARGET_ABI_WIN32, not built yet) changes only what these
// macros and rc_ov.h's expand to: guest pointers are 32-bit offsets into the reserved 4 GB guest window
// (RC_HOST adds its base, RC_GUEST subtracts it), arguments are read from the stack (plus ecx for thiscall,
// ecx/edx for fastcall, by RC_CC_<name>), float results go to x87 st0, and a stdcall / thiscall
// function's callee-cleaned argument bytes are popped by the per-target wrapper generated from symbols.map,
// so callers written with these macros do not change.
#ifndef RC_ABI_H
#define RC_ABI_H

#include <stdint.h>

#include "rc.h"
#include "rc_target_names.h"

// Calling conventions (RC_CC_<name> in rc_target_names.h)
#define RC_CC_SYSV 0
#define RC_CC_CDECL 1
#define RC_CC_STDCALL 2
#define RC_CC_THISCALL 3
#define RC_CC_FASTCALL 4

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
#else
#error "rc_abi.h: no ABI layer for this target yet (only RC_TARGET_ABI_SYSV64)"
#endif

#endif
