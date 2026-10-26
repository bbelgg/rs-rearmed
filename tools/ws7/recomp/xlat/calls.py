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
from capstone import x86

from .calls_special import SPECIAL
from .common import Unsupported  # noqa: F401

RC_LA_MAX = 4096  # runtime/rc.h


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

    def icall(self, i, op):
        """The dispatch of an indirect call/jump whose target is in `tgt` (registers spilled; the
        caller handles the return address). Through a stub's lazy pointer (`call [rip+slot]`, how
        the game calls operator delete, pthread_mutex_unlock, ...): the slot's import thunk while
        the slot holds what rc_image_load bound it to, else (or for a slot bound into the image,
        not known until load) rc_icall, whose native branch rc_icall_native would pick the same
        thunk through rc_import_thunk. The thunk pops the return address, as rc_icall_native does."""
        slot = self.la_slot(i, op)
        if slot is None:
            return "rc_icall(c, tgt);"
        return ("if (tgt == rc_la_bound[%d]) { extern void I_%x(rc_ctx *); c->r[4] += 8; I_%x(c); } "
                "else rc_icall(c, tgt);" % ((slot - self.la_lo) // 8, slot, slot))

    def la_slot(self, i, op):
        if op.type != x86.X86_OP_MEM or op.mem.base != x86.X86_REG_RIP or op.mem.index or op.mem.segment:
            return None
        slot = (i.address + i.size + op.mem.disp) & 0xffffffffffffffff
        if not self.la_lo or not 0 <= slot - self.la_lo < 8 * RC_LA_MAX or (slot - self.la_lo) % 8:
            return None
        syms = self.__dict__.get("_la_syms")
        if syms is None:
            syms = self._la_syms = {s: n for s, n in self.stubs.values()}
        sym = syms.get(slot)
        if sym is None or sym in SPECIAL or self.stub_local_target(slot) is not None:
            return None
        return slot

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
