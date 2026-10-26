"""Calls through __stubs, and imports lowered specially (special imports: calls_special.py).

A call through __stubs becomes

    { extern void I_<slot>(rc_ctx *); SPILL_IMP(); I_<slot>(c); FILL_IMP(); }

with I_<slot> generated per import by tools/ws7/recomp/thunks/gen_thunks.py
(typed from the SDK prototypes) and the macros in runtime/rc_call.h. The call
does not push a return address, so a thunk finds the 7th+ integer arguments at
guest rsp+0, +8, ...

A stub whose lazy pointer is a rebased address inside the image (a weak C++
instance dyld coalesces to the image's own copy) is not an import: it becomes
an ordinary direct call of that function. Setjmp-family imports are lowered
inline by calls_special.SPECIAL.
"""
from .calls_special import SPECIAL
from .common import Unsupported  # noqa: F401


class CallsMixin:
    def stub_local_target(self, slot):
        """Image address a stub's lazy pointer is rebased to when the symbol is
        defined by the image itself (None for a real import)."""
        t = self.img.u64(slot)
        if t in self.fset and self.text_lo <= t < self.text_hi:
            return t
        return None

    def emit_import_call(self, i, slot, sym, out):
        if sym in SPECIAL:
            return SPECIAL[sym](self, i, slot, sym, out)
        local = self.stub_local_target(slot)
        if local is not None:
            out.append("/* %s: defined in the image */ rsp -= 8; RC_STS(uint64_t, rsp, RC_A(0x%xull)); %s"
                       % (sym, i.address + i.size, self.call_internal(local)))
            return
        out.append("/* %s */ { extern void I_%x(rc_ctx *); SPILL_IMP(); I_%x(c); FILL_IMP(); }" % (sym, slot, slot))

    def emit_import_tail(self, i, slot, sym, out):
        """`jmp <stub>`: a tail call of an import. The caller's return address is
        at rsp, the stack arguments above it: pop it (the thunk wants them at rsp)
        and return with the results."""
        local = self.stub_local_target(slot)
        if local is not None:
            out.append(self.call_internal(local, tail=True))
            return
        out.append("/* %s (tail) */ { extern void I_%x(rc_ctx *); rsp += 8; SPILL_IMP(); I_%x(c); FILL_IMP(); "
                   "SPILL_RET(); return; }" % (sym, slot, slot))
