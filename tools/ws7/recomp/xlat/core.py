#!/usr/bin/env python3
"""x86_64 machine code -> portable C.

Static recompilation in the XenonRecomp/N64Recomp style:

  * one C function `void F_<addr>(rc_ctx *c)` per original function;
  * guest registers are C locals inside a function (filled from / spilled to
    rc_ctx at entry, exit and around calls), so clang keeps them in arm64
    registers;
  * every flag-setting instruction assigns the flags it defines to C locals
    (zf sf cf of pf); clang's dead-store elimination removes the ones no
    jcc/setcc/cmov/adc reads;
  * guest memory is host memory (runtime/rc_image.c maps the image and
    rebases it); RIP-relative operands become RC_A(vmaddr);
  * the guest stack is a host buffer; call/ret push and pop the original
    return addresses, so stack frames are bit-identical to x86;
  * calls through __stubs load the image's own (natively bound) lazy pointer
    and call it with the six integer argument registers (enough for
    integer/pointer-only, non-variadic imports; other signatures need a
    per-signature thunk);
  * direct calls/tail calls to other translated functions are C calls;
    anything not translated is a runtime failure (rc_fail).

Unsupported instructions are a translation-time error listing the
mnemonics, so coverage is explicit.

Modules:
  core.py      discovery, operands, function emission, CLI
  emit_int.py  integer instructions
  emit_sse.py  SSE instructions
  jumptab.py   jump-table resolution
  calls.py     calls through __stubs and special imports
The C side lives in ../runtime (rc.h, rc_gen.h and the helper headers).
build/gen_image.py drives it for the whole image.

Usage (from tools/ws7/recomp): python3 -m xlat.core IMAGE OUT.c ADDR[=name] ...
"""
import bisect
import json
import os
import struct
import sys

import capstone
from capstone import x86

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "vtables"))
import macho  # noqa: E402

# RC_EMIT_TRACE=1: every generated function starts with RC_ENTER(addr) (see
# runtime/rc_gen.h; a no-op unless compiled with -DRC_TRACE). Off by default so
# the normal build's output is unchanged.
TRACE = os.environ.get("RC_EMIT_TRACE") == "1"

from .calls import CallsMixin  # noqa: E402
from .common import COND, CTYPE, MASK, NOPS, NORETURN, REGS, Unsupported  # noqa: E402,F401
from .emit_int import IntMixin  # noqa: E402
from .emit_sse import SseMixin  # noqa: E402
from .jumptab import JumpTableMixin  # noqa: E402
from .relaxed import PURE_IMPORTS, RELAXED  # noqa: E402

# Sections that hold nothing a guest thread writes after rc_image_load has rebased and bound the
# image (before any guest code runs): all of __TEXT, and the constant and pointer sections of __DATA.
# A RIP-relative load from them needs no TSO fence (RC_LDC): its value never changes, so there is
# no store of another thread it could be ordered against.
RO_SECTS = {("__DATA", "__const"), ("__DATA", "__got"), ("__DATA", "__nl_symbol_ptr")}

# RC_RELAXED=0: translate the RELAXED functions with the fences like everything else.
RELAXED_ON = os.environ.get("RC_RELAXED", "1") != "0"

class Translator(IntMixin, SseMixin, JumpTableMixin, CallsMixin):
    # mnemonics after which a path ends (x86_32.py adds int3 and the far returns)
    STOP = ("ret", "ud2", "hlt")
    LOOPS = ()     # branches Capstone does not put in X86_GRP_JUMP (x86_32.py: loop*, jecxz)

    def __init__(self, image_path):
        self.img = macho.Image(image_path)
        for line in self.img._objdump("--lazy-bind").splitlines():
            p = line.split()
            if len(p) >= 5 and p[0].startswith("__") and p[2].lower().startswith("0x"):
                self.img.binds.setdefault(int(p[2], 16), p[-1])
        a, sz, fo = self.img.sects[("__TEXT", "__text")]
        self.text_lo, self.text_hi, self.text_fo = a, a + sz, fo
        sa, ssz, sfo = self.img.sects[("__TEXT", "__stubs")]
        self.stubs = {}
        for k in range(ssz // 6):
            disp = struct.unpack_from("<i", self.img.data, sfo + 6 * k + 2)[0]
            slot = sa + 6 * k + 6 + disp
            self.stubs[sa + 6 * k] = (slot, self.img.binds.get(slot, "?"))
        self.la_lo = self.img.sects.get(("__DATA", "__la_symbol_ptr"), (0,))[0]
        self.md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
        self.md.detail = True
        self.fset = set(self.img.fstarts)
        self.fsorted = sorted(self.fset)
        self.translated = set()
        self.rbp_frame = False
        self.relaxed = False
        self.cur = None
        self.ro = sorted((a, a + sz) for (seg, sect), (a, sz, _) in self.img.sects.items()
                         if seg == "__TEXT" or (seg, sect) in RO_SECTS)

    def insn(self, a):
        o = self.text_fo + (a - self.text_lo)
        for i in self.md.disasm(self.img.data[o:o + 16], a, 1):
            return i
        raise Unsupported("undecodable at 0x%x" % a)


    # --- discovery ---------------------------------------------------------
    def discover(self, entry):
        seen, work, labels = {}, [entry], {entry}
        self.tables = {}
        self.noret = set()   # calls to in-image functions that never return
        while work:
            a = work.pop()
            path = []
            while a not in seen:
                i = self.insn(a)
                seen[a] = i
                path.append(i)
                m = i.mnemonic
                nxt = a + i.size
                if m in self.STOP:
                    break
                if x86.X86_GRP_JUMP in i.groups or m in self.LOOPS:
                    op = i.operands[0]
                    if op.type != x86.X86_OP_IMM:
                        if m != "jmp":
                            raise Unsupported("indirect jcc at 0x%x" % a)
                        tg = self.jump_table(path, i, entry)
                        if tg:
                            self.tables[a] = tg
                            for t in tg:
                                labels.add(t)
                                work.append(t)
                        break                                # else: indirect tail jump
                    t = op.imm
                    if t != entry and t in self.fset:      # tail call
                        if m == "jmp":
                            break
                        a = nxt
                        continue
                    labels.add(t)
                    work.append(t)
                    if m == "jmp":
                        break
                if x86.X86_GRP_CALL in i.groups and i.operands[0].type == x86.X86_OP_IMM:
                    t = i.operands[0].imm
                    if t in self.stubs and self.stubs[t][1] in NORETURN:
                        break
                    if t in self.fset and self.is_noreturn(t):
                        self.noret.add(a)     # data may follow (libpng 0x100b61c90)
                        break
                a = nxt
        return seen, labels

    def is_noreturn(self, f):
        """True if no path through in-image function f returns: no `ret`, no
        indirect tail jump, no tail call to a returning function. Cached;
        functions on the current recursion stack count as returning."""
        cache = self.__dict__.setdefault("_noret_cache", {})
        if f in cache:
            return cache[f]
        cache[f] = False
        saved = (self.tables, self.noret)
        try:
            seen, _ = self.discover(f)
        except Unsupported:
            self.tables, self.noret = saved
            return False
        tables = self.tables
        self.tables, self.noret = saved
        res = True
        for a, i in seen.items():
            m = i.mnemonic
            if m == "ret":
                res = False
            elif x86.X86_GRP_JUMP in i.groups:
                op = i.operands[0]
                if op.type != x86.X86_OP_IMM:
                    res = a in tables
                elif op.imm != f and op.imm in self.fset and not self.is_noreturn(op.imm):
                    res = False
            if not res:
                break
        cache[f] = res
        return res

    # --- operands ----------------------------------------------------------
    def reg(self, i, r):
        name = i.reg_name(r)
        if name not in REGS:
            raise Unsupported("register %s at 0x%x" % (name, i.address))
        return REGS[name]

    def ea(self, i, op):
        m = op.mem
        if m.segment != 0:
            raise Unsupported("segment override at 0x%x" % i.address)
        if m.base == x86.X86_REG_RIP:
            return "RC_A(0x%xull)" % ((i.address + i.size + m.disp) & 0xffffffffffffffff)
        parts = []
        if m.base:
            parts.append(self.reg(i, m.base)[0])
        if m.index:
            ix = self.reg(i, m.index)[0]
            parts.append(ix if m.scale == 1 else "%s * %d" % (ix, m.scale))
        if m.disp:
            parts.append("0x%xull" % (m.disp & 0xffffffffffffffff))
        return "(uint64_t)(" + (" + ".join(parts) or "0") + ")"

    def on_stack(self, op):
        """A memory operand addressed off rsp, or off rbp in a function that keeps rbp as its frame
        pointer: the thread's own stack (RC_LDS/RC_STS, no TSO fences)."""
        b = op.mem.base
        return b == x86.X86_REG_RSP or (b == x86.X86_REG_RBP and self.rbp_frame)

    RBP_REGS = (x86.X86_REG_RBP, x86.X86_REG_EBP, x86.X86_REG_BP, x86.X86_REG_BPL)

    def frame_pointer(self, entry, seen):
        """True if the function starts with `push rbp; mov rbp, rsp` and nothing else writes rbp
        (only the epilogue's pop rbp / leave): every rbp-based operand is then in its own frame or
        its stack arguments. RC_RBP_FENCED=1 turns this off (rbp accesses fenced again, as before
        2026-10-26), for A/B tests of a suspected memory-order problem."""
        if os.environ.get("RC_RBP_FENCED") == "1":
            return False
        i0 = seen.get(entry)
        if not i0 or i0.mnemonic != "push" or i0.op_str != "rbp":
            return False
        i1 = seen.get(entry + i0.size)
        if not i1 or i1.mnemonic != "mov" or i1.op_str != "rbp, rsp":
            return False
        for a, i in seen.items():
            if a == i1.address or i.mnemonic == "leave" or (i.mnemonic == "pop" and i.op_str == "rbp"):
                continue
            try:
                w = i.regs_access()[1]
            except capstone.CsError:
                return False
            if any(r in w for r in self.RBP_REGS):
                return False
        return True

    def read_only(self, op):
        """A RIP-relative operand of the current instruction inside a read-only section (RO_SECTS)."""
        m = op.mem
        if m.base != x86.X86_REG_RIP or m.index or self.cur is None:
            return False
        if not any(o.type == x86.X86_OP_MEM and o.mem.base == m.base and o.mem.disp == m.disp
                   for o in self.cur.operands):
            raise Unsupported("operand not of the current instruction at 0x%x" % self.cur.address)
        a = (self.cur.address + self.cur.size + m.disp) & 0xffffffffffffffff
        k = bisect.bisect_right(self.ro, (a, 1 << 64)) - 1
        return k >= 0 and self.ro[k][0] <= a and a + max(op.size, 1) <= self.ro[k][1]

    def LD(self, op):
        if self.on_stack(op):
            return "RC_LDS"
        if self.read_only(op):
            return "RC_LDC"
        return "RC_LDR" if self.relaxed else "RC_LD"

    def ST(self, op):
        if self.on_stack(op):
            return "RC_STS"
        return "RC_STR" if self.relaxed else "RC_ST"

    # --- relaxed functions (relaxed.py) ------------------------------------
    def relax_ok(self, entry, seen):
        """The automatic part of the RELAXED check: no lock-prefixed, xchg or fence instruction,
        and calls only to other relaxed functions or PURE_IMPORTS. None if fine, else the reason."""
        for a, i in seen.items():
            m = i.mnemonic
            if i.prefix[0] == 0xf0 or m in ("xchg", "cmpxchg", "cmpxchg8b", "cmpxchg16b", "xadd", "mfence",
                                             "lfence", "sfence", "pause"):
                return "%s at 0x%x" % (m, a)
            if m == "call" or x86.X86_GRP_JUMP in i.groups:
                op = i.operands[0]
                if op.type != x86.X86_OP_IMM:
                    if m == "call" or a not in self.tables:
                        return "indirect %s at 0x%x" % (m, a)
                    continue
                t = op.imm
                if t in self.stubs:
                    if self.stubs[t][1] not in PURE_IMPORTS or self.stub_local_target(self.stubs[t][0]) is not None:
                        return "call of %s at 0x%x" % (self.stubs[t][1], a)
                elif (m == "call" or (t in self.fset and t != entry)) and t not in RELAXED:
                    return "call of 0x%x at 0x%x" % (t, a)
        return None

    def relax_exit(self):
        return "RC_RELAX_EXIT(); " if self.relaxed else ""

    def rd(self, i, op, size=None):
        """C expression (uint64_t, zero-extended) for an operand's value."""
        size = size or op.size
        if op.type == x86.X86_OP_REG:
            r, s, sh = self.reg(i, op.reg)
            if s == 8:
                return r
            return "(uint64_t)(%s)(%s >> %d)" % (CTYPE[s], r, sh) if sh else "(uint64_t)(%s)%s" % (CTYPE[s], r)
        if op.type == x86.X86_OP_IMM:
            return "0x%xull" % (op.imm & ((1 << (8 * size)) - 1))
        return "(uint64_t)%s(%s, %s)" % (self.LD(op), CTYPE[size], self.ea(i, op))

    def wr(self, i, op, val):
        """C statement writing val (any integer expr) to an operand."""
        if op.type == x86.X86_OP_REG:
            r, s, sh = self.reg(i, op.reg)
            if s == 8:
                return "%s = (uint64_t)(%s);" % (r, val)
            if s == 4:
                return "%s = (uint32_t)(%s);" % (r, val)
            if s == 2:
                return "%s = (%s & ~0xffffull) | (uint16_t)(%s);" % (r, r, val)
            if sh:
                return "%s = (%s & ~0xff00ull) | ((uint64_t)(uint8_t)(%s) << 8);" % (r, r, val)
            return "%s = (%s & ~0xffull) | (uint8_t)(%s);" % (r, r, val)
        if op.type == x86.X86_OP_MEM:
            return "%s(%s, %s, %s);" % (self.ST(op), CTYPE[op.size], self.ea(i, op), val)
        raise Unsupported("write to imm at 0x%x" % i.address)

    # --- flags -------------------------------------------------------------
    @staticmethod
    def szp(n, res="t"):
        return ("zf = (%s & %s) == 0; sf = (%s >> %d) & 1; pf = rc_parity(%s);"
                % (res, MASK[n], res, 8 * n - 1, res))

    # --- one instruction ---------------------------------------------------

    # --- one instruction ---------------------------------------------------
    def emit(self, i, out):
        self.cur = i
        m = i.mnemonic
        if m in NOPS:
            return
        if self.emit_flow(i, out) or self.emit_int(i, out) or self.emit_int2(i, out) or self.emit_sse(i, out):
            return
        raise Unsupported("%s %s at 0x%x" % (m, i.op_str, i.address))

    # --- control flow: branches, jump tables, calls, returns ----------------
    def emit_flow(self, i, out):
        m = i.mnemonic
        ops = i.operands
        A = i.address
        if m == "jmp" and ops[0].type != x86.X86_OP_IMM:
            tgt = self.rd(i, ops[0], 8)
            if A in self.tables:
                cases = " ".join("case 0x%xull: goto L_%x;" % (t, t) for t in sorted(set(self.tables[A])))
                out.append("switch (%s - rc_slide) { %s default: rc_fail(\"jump table\", 0x%xull); }"
                           % (tgt, cases, A))
            else:   # tail jump through a register or memory (vtable / function pointer)
                out.append("{ uint64_t tgt = %s; SPILL_CALL(); %s return; }" % (tgt, self.icall(i, ops[0])))
        elif m == "jmp":
            t = ops[0].imm
            if t in self.fset and t != self.entry:
                out.append(self.call_internal(t, tail=True))
            else:
                out.append("goto L_%x;" % t)
        elif x86.X86_GRP_JUMP in i.groups:
            t = ops[0].imm
            cc = COND[m[1:]]
            if t in self.fset and t != self.entry:
                out.append("if (%s) { %s }" % (cc, self.call_internal(t, tail=True)))
            else:
                out.append("if (%s) goto L_%x;" % (cc, t))
        elif m == "call":
            ret = A + i.size
            if ops[0].type != x86.X86_OP_IMM:
                out.append("{ uint64_t tgt = %s; rsp -= 8; RC_STS(uint64_t, rsp, RC_A(0x%xull)); "
                           "SPILL_CALL(); %s FILL_CALL(); }" % (self.rd(i, ops[0], 8), ret, self.icall(i, ops[0])))
                return True
            t = ops[0].imm
            if t in self.stubs:
                slot, sym = self.stubs[t]
                self.emit_import_call(i, slot, sym, out)
            else:
                out.append("rsp -= 8; RC_STS(uint64_t, rsp, RC_A(0x%xull)); %s" % (ret, self.call_internal(t)))
        elif m == "ret":
            out.append("rsp += 8; %sSPILL_RET(); return;" % self.relax_exit())
        elif m in ("ud2", "hlt"):
            out.append('rc_fail("%s", 0x%xull);' % (m, A))
        else:
            return False
        return True

    def call_internal(self, t, tail=False):
        """In a relaxed function a call or tail call leaves it (relax_ok admits only other relaxed
        functions, which fence themselves): nothing is needed around it, except the exit fence before
        a tail call, which ends this function."""
        self.callees.add(t)
        s = "SPILL_CALL(); F_%x(c); FILL_CALL();" % t
        return ("%sSPILL_CALL(); F_%x(c); return;" % (self.relax_exit(), t)) if tail else s

    # --- one function ------------------------------------------------------
    def function(self, entry, name=None):
        self.entry = entry
        self.callees = set()
        seen, labels = self.discover(entry)
        self.rbp_frame = self.frame_pointer(entry, seen)
        self.relaxed = False
        if RELAXED_ON and entry in RELAXED:
            why = self.relax_ok(entry, seen)
            if why:
                print("warning: 0x%x stays fenced (not relaxed): %s" % (entry, why), file=sys.stderr)
            self.relaxed = not why
        body = []
        order = sorted(seen)
        bad = []
        for k, a in enumerate(order):
            i = seen[a]
            if a in labels:
                body.append("L_%x:" % a)
            stmts = []
            try:
                self.emit(i, stmts)
            except Unsupported as e:
                bad.append(str(e))
                continue
            for s in stmts:
                body.append("  %s  // %x: %s %s" % (s, a, i.mnemonic, i.op_str) if s is stmts[0]
                            else "  " + s)
            # falls through to an address that is not emitted next: jump there
            nxt = a + i.size
            ends = i.mnemonic in ("jmp",) + self.STOP or (
                i.mnemonic == "call" and i.operands[0].type == x86.X86_OP_IMM
                and self.stubs.get(i.operands[0].imm, (0, ""))[1] in NORETURN)
            if a in self.noret:
                body.append('  rc_fail("return from no-return call", 0x%xull);' % a)
                ends = True
            if not ends and (k + 1 == len(order) or order[k + 1] != nxt):
                body.append("  goto L_%x;" % nxt)
                labels.add(nxt)
        if bad:
            raise Unsupported("0x%x: " % entry + "; ".join(bad))
        # labels nobody jumps to compile to nothing; unused ones warn only
        self.translated.add(entry)
        head = ["// 0x%x%s: %d instructions" % (entry, " " + name if name else "", len(order)),
                "void F_%x(rc_ctx *c) {" % entry,
                "  FILL_DECL();"] + (["  RC_ENTER(0x%xull);" % entry] if TRACE else []) + [
                "  uint8_t zf = 0, sf = 0, cf = 0, of = 0, pf = 0;",
                "  (void)zf; (void)sf; (void)cf; (void)of; (void)pf;"] + (
                ["  RC_RELAX_ENTER();  // relaxed: %s" % RELAXED[entry]] if self.relaxed else [])
        self.relaxed = False
        return head + body + ["}", ""]


PRELUDE = r'''// Generated by tools/ws7/recomp/xlat/core.py from the original x86_64 machine code.
// Game-derived: build output only, never commit. Do not edit; regenerate.
#include "rc_gen.h"

'''


def survey(img, out):
    """Try every function start; tally what blocks translation (by first
    unsupported construct), and emit the translatable ones of a component
    range to OUT.c if RC_SURVEY_EMIT=lo-hi is set (compile-size experiment)."""
    import collections
    import re
    tr = Translator(img)
    ok, blockers, fblock = 0, collections.Counter(), collections.Counter()
    emit_lo, emit_hi = 0, 0
    if os.environ.get("RC_SURVEY_EMIT"):
        lo, hi = os.environ["RC_SURVEY_EMIT"].split("-")
        emit_lo, emit_hi = int(lo, 16), int(hi, 16)
    a, sz, fo = tr.img.sects[("__TEXT", "__text")]
    starts = [f for f in tr.img.fstarts if a <= f < a + sz]
    lines, done = [], []
    per_fn = {}
    for k, f in enumerate(starts):
        try:
            body = tr.function(f)
            ok += 1
            per_fn[f] = None
            if emit_lo <= f < emit_hi:
                lines += body
                done.append(f)
        except Unsupported as e:
            reasons = set()
            for r in str(e).split(": ", 1)[-1].split("; "):
                r = re.sub(r" at 0x[0-9a-f]+$", "", r)
                r = re.sub(r"0x[0-9a-f]+", "N", r)
                key = r.split()[0] if not r.startswith(("indirect", "shift", "segment", "one-operand", "register", "mul size", "undecodable")) else " ".join(r.split()[:2])
                reasons.add(key)
            for r in reasons:
                blockers[r] += 1
            if len(reasons) == 1:
                fblock[next(iter(reasons))] += 1
            per_fn[f] = sorted(reasons)
        if k % 5000 == 0:
            print("  %d/%d" % (k, len(starts)), file=sys.stderr)
    res = {"functions": len(starts), "translatable": ok,
           "blocked_by": blockers.most_common(80),
           "single_blocker": fblock.most_common(40)}
    json.dump(res, open(out, "w"), indent=1)
    with open(out.replace(".json", ".blocked.jsonl"), "w") as fo_:
        for f, r in per_fn.items():
            if r:
                fo_.write(json.dumps({"a": f, "why": r}) + "\n")
    if done:
        cout = out.replace(".json", ".c")
        callees = sorted(set(re.findall(r"F_([0-9a-f]+)\(c\)", "\n".join(lines))) - {"%x" % d for d in done})
        with open(cout, "w") as f_:
            f_.write(PRELUDE)
            f_.write("\n".join("void F_%s(rc_ctx *c);" % x for x in callees) + "\n")
            f_.write("\n".join("void F_%x(rc_ctx *c);" % d for d in done) + "\n\n")
            f_.write("\n".join(lines))
    print(json.dumps({k: v for k, v in res.items() if k != "blocked_by"}, indent=1))


def main():
    if sys.argv[1] == "--survey":
        return survey(sys.argv[2], sys.argv[3])
    img, out = sys.argv[1], sys.argv[2]
    reqs = []
    for arg in sys.argv[3:]:
        a, _, n = arg.partition("=")
        reqs.append((int(a, 16), n or None))
    tr = Translator(img)
    lines, done, queue = [], set(), list(reqs)
    names = dict(reqs)
    skipped = set()
    while queue:
        a, n = queue.pop(0)
        if a in done or a in skipped:
            continue
        done.add(a)
        try:
            lines += tr.function(a, n)
        except Unsupported as e:
            if not os.environ.get("RC_SKIP_UNSUPPORTED"):
                raise
            print("skipped: %s" % e, file=sys.stderr)
            done.discard(a)
            skipped.add(a)
            continue
        for t in sorted(tr.callees):
            if t not in done:
                queue.append((t, names.get(t)))
    decl = ["void F_%x(rc_ctx *c);" % a for a in sorted(done)]
    table = ["const rc_entry rc_table[] = {"] + ["  {0x%xull, F_%x}," % (a, a) for a in sorted(done)] + \
            ["};", "const int rc_table_len = %d;" % len(done), ""]
    with open(out, "w") as f:
        f.write(PRELUDE)
        f.write("\n".join(decl) + "\n\n")
        f.write("\n".join(lines))
        f.write("typedef struct { uint64_t vmaddr; rc_fn fn; } rc_entry;\n")
        f.write("\n".join(table))
    print("translated %d functions -> %s" % (len(done), out))


