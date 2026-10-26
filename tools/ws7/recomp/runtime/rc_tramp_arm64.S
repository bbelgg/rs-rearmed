// Native-callable entry points for guest functions (see rc_tramp.c).
// Entry k is `mov x9, x30; bl rc_tramp_common`; the common code recovers k
// from the link register, so the pool is generated with a plain .rept.
#define POOL 2048
  .text
  .p2align 4
  .globl _rc_tramp_pool
_rc_tramp_pool:
  .rept POOL
  mov x9, x30
  bl _rc_tramp_common
  .endr

  .globl _rc_tramp_pool_end
_rc_tramp_pool_end:

// frame (192 bytes): [0..63] x0..x7, [64..127] d0..d7, [128] x29, [136] original lr, [144] x8, [152] caller sp
// the dispatcher gets (index, frame) and leaves rax, rdx, xmm0.lo, xmm1.lo in
// frame[0], frame[1], frame[8], frame[9].
  .p2align 4
_rc_tramp_common:
  mov x11, sp
  sub sp, sp, #192
  str x8, [sp, #144]
  str x11, [sp, #152]
  stp x29, x9, [sp, #128]
  add x29, sp, #128
  stp x0, x1, [sp, #0]
  stp x2, x3, [sp, #16]
  stp x4, x5, [sp, #32]
  stp x6, x7, [sp, #48]
  stp d0, d1, [sp, #64]
  stp d2, d3, [sp, #80]
  stp d4, d5, [sp, #96]
  stp d6, d7, [sp, #112]
  adrp x10, _rc_tramp_pool@PAGE
  add x10, x10, _rc_tramp_pool@PAGEOFF
  sub x0, x30, x10
  lsr x0, x0, #3
  sub x0, x0, #1
  mov x1, sp
  bl _rc_tramp_dispatch
  ldp x0, x1, [sp, #0]
  ldp d0, d1, [sp, #64]
  ldp x29, x9, [sp, #128]
  add sp, sp, #192
  ret x9
