// Recompiler runtime: guest -> native calls.
//
//   void rc_native_call(uint64_t fn, const uint64_t ix[8], const uint64_t dx[8], uint64_t ret[4]);
//   void rc_native_call_x8(fn, ix, dx, ret[4], uint64_t x8);   // indirect-result register
//   void rc_native_call_hfa(fn, ix, dx, ret[6]);               // also returns d2, d3
//
// x0..x7 = ix[0..7], d0..d7 = dx[0..7] (low 64 bits), then `blr fn`. ret gets
// x0, x1, d0, d1 (and d2, d3 for _hfa). The callee sees no stack arguments.

        .text
        .p2align 2

        .macro RC_NATIVE_CALL name, sret, hfa
        .globl _\name
_\name:
        stp     x29, x30, [sp, #-32]!
        mov     x29, sp
        stp     x19, x20, [sp, #16]
        mov     x19, x3                 // ret
        mov     x16, x0                 // fn
        mov     x17, x1                 // ix
        .if \sret
        mov     x8, x4
        .else
        mov     x8, xzr
        .endif
        ldp     d0, d1, [x2]
        ldp     d2, d3, [x2, #16]
        ldp     d4, d5, [x2, #32]
        ldp     d6, d7, [x2, #48]
        ldp     x0, x1, [x17]
        ldp     x2, x3, [x17, #16]
        ldp     x4, x5, [x17, #32]
        ldp     x6, x7, [x17, #48]
        blr     x16
        stp     x0, x1, [x19]
        stp     d0, d1, [x19, #16]
        .if \hfa
        stp     d2, d3, [x19, #32]
        .endif
        ldp     x19, x20, [sp, #16]
        ldp     x29, x30, [sp], #32
        ret
        .endmacro

        RC_NATIVE_CALL rc_native_call, 0, 0
        RC_NATIVE_CALL rc_native_call_x8, 1, 0
        RC_NATIVE_CALL rc_native_call_hfa, 0, 1
