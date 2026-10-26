"""Imports lowered inline instead of through a thunk.

SPECIAL maps an import symbol (as bound, e.g. "_setjmp") to an emitter
`fn(translator, insn, slot, sym, out)` that appends C statements to out, like
the other emit_* methods. calls.py consults it before the generic lowering.

setjmp family (the game imports _setjmp and _longjmp only): setjmp is inlined
so that the host setjmp lives in the generated function's own frame. See
runtime/rc_setjmp.c. Every guest register is re-read from rc_ctx after a
longjmp (FILL), also with RC_ABI_TRUST: the setjmp "call" has no pushed return
address, so nothing is popped.
"""

_DECL = ("extern uint64_t *rc_jmp_enter(rc_ctx *, uint64_t); "
         "extern int _setjmp(void *) __attribute__((returns_twice)); ")


def _setjmp(tr, i, slot, sym, out):
    # rdi = guest jmp_buf; the host jmp_buf and the longjmp value live in the record j
    out.append("/* %s */ { %sSPILL(); uint64_t *volatile j = rc_jmp_enter(c, rdi); "
               "if (_setjmp(j)) { FILL(); rax = (uint32_t)j[32]; } else { rax = 0; } }" % (sym, _DECL))


def _longjmp(tr, i, slot, sym, out):
    out.append("/* %s */ { extern _Noreturn void rc_longjmp(rc_ctx *, uint64_t, uint64_t); "
               "SPILL(); rc_longjmp(c, rdi, rsi); }" % sym)


SPECIAL = {}
for _s in ("_setjmp", "setjmp", "_sigsetjmp", "sigsetjmp", "__setjmp"):
    SPECIAL[_s] = _setjmp
for _s in ("_longjmp", "longjmp", "_siglongjmp", "siglongjmp", "__longjmp"):
    SPECIAL[_s] = _longjmp
