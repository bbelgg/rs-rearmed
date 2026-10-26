"""SSE/SSE2/SSE3/SSSE3 instructions.

Semantics live in runtime/rc_sse.h as `rc_<mnemonic>(dest, src[, imm])`
helpers over rc_v128; most mnemonics are therefore one table entry here.
"""
import re

from capstone import x86

from .common import CTYPE, MASK, REGS, STYPE, COND, Unsupported  # noqa: F401

# mnemonic -> source kind: "v" full 128-bit operand, "s4"/"s8" scalar float/double
# (memory form loads only 4/8 bytes), "q" low 64 bits (cvtps2pd, movddup ...)
KIND = {}
for _o in ("add", "sub", "mul", "div", "min", "max"):
    for _s, _k in (("ps", "v"), ("pd", "v"), ("ss", "s4"), ("sd", "s8")):
        KIND[_o + _s] = _k
for _o in ("sqrt", "rcp", "rsqrt"):
    KIND.update({_o + "ps": "v", _o + "ss": "s4"})
KIND["sqrtpd"] = "v"
KIND["sqrtsd"] = "s8"
for _m in ("haddps hsubps haddpd hsubpd addsubps addsubpd shufps shufpd pshufd pshufhw pshuflw "
           "pshufb palignr unpcklps unpckhps unpcklpd unpckhpd punpcklbw punpckhbw punpcklwd "
           "punpckhwd punpckldq punpckhdq punpcklqdq punpckhqdq movshdup movsldup cvtdq2ps "
           "cvttps2dq cvtps2dq cvtpd2ps cvttpd2dq cvtpd2dq phaddd phaddw phsubd phsubw "
           "pmuludq pmullw pmulhw pmulhuw pmaddwd psadbw packsswb packuswb packssdw").split():
    KIND[_m] = "v"
for _o in ("add", "sub"):
    for _s in ("b", "w", "d", "q"):
        KIND["p" + _o + _s] = "v"
    for _s in ("sb", "sw", "usb", "usw"):
        KIND["p" + _o + _s] = "v"
for _o in ("eq", "gt"):
    for _s in ("b", "w", "d"):
        KIND["pcmp" + _o + _s] = "v"
for _m in "pminub pmaxub pminsw pmaxsw pavgb pavgw".split():
    KIND[_m] = "v"
KIND.update({"cvtps2pd": "q", "cvtdq2pd": "q", "movddup": "q"})
SHIFTS = "psllw pslld psllq psrlw psrld psrlq psraw psrad pslldq psrldq".split()
for _m in SHIFTS:
    KIND[_m] = "v"
WITH_IMM = {"shufps", "shufpd", "pshufd", "pshufhw", "pshuflw", "palignr"}

CMP_PRED = ["eq", "lt", "le", "unord", "neq", "nlt", "nle", "ord"]
CMP_RE = re.compile(r"^cmp(%s)?(ps|pd|ss|sd)$" % "|".join(CMP_PRED))
PREFETCH = {"prefetcht0", "prefetcht1", "prefetcht2", "prefetchnta", "prefetchw"}
FENCES = {"sfence", "lfence", "mfence"}
MISC = {"movss", "movsd", "movaps", "movups", "movapd", "movupd", "movdqa", "movdqu", "lddqu",
        "movntps", "movntpd", "movntdq", "movd", "movq", "movlps", "movlpd", "movhps", "movhpd",
        "movhlps", "movlhps", "movmskps", "movmskpd", "pmovmskb", "pextrw", "pinsrw",
        "xorps", "xorpd", "pxor", "andps", "andpd", "pand", "orps", "orpd", "por",
        "andnps", "andnpd", "pandn", "ucomiss", "comiss", "ucomisd", "comisd",
        "cvtsi2ss", "cvtsi2sd", "cvttss2si", "cvttsd2si", "cvtss2si", "cvtsd2si",
        "cvtss2sd", "cvtsd2ss", "ldmxcsr", "stmxcsr"}


class SseMixin:
    def xr(self, i, op):
        name = i.reg_name(op.reg)
        if not name.startswith("xmm"):
            return None
        return "x" + name[3:]

    def emit_sse(self, i, out):
        m = i.mnemonic
        ops = i.operands
        if m in PREFETCH:
            return True
        if m in FENCES:
            out.append("__atomic_thread_fence(__ATOMIC_SEQ_CST);")
            return True
        cm = CMP_RE.match(m)
        if not (m in KIND or m in MISC or cm):
            return False
        d = ops[0]
        s_ = ops[1] if len(ops) > 1 else None
        dx = self.xr(i, d) if d.type == x86.X86_OP_REG else None
        sx = self.xr(i, s_) if s_ is not None and s_.type == x86.X86_OP_REG else None

        def f32(op, x):
            return "rc_f32((uint32_t)%s.lo)" % x if x else "rc_f32(%s(uint32_t, %s))" % (self.LD(op), self.ea(i, op))

        def f64(op, x):
            return "rc_f64(%s.lo)" % x if x else "rc_f64(%s(uint64_t, %s))" % (self.LD(op), self.ea(i, op))

        def v128(op, x):
            return x if x else "%s(rc_v128, %s)" % (self.LD(op), self.ea(i, op))

        def src(kind):
            if sx:
                return sx
            if kind == "v":
                return "%s(rc_v128, %s)" % (self.LD(s_), self.ea(i, s_))
            return "((rc_v128){%s(uint%d_t, %s), 0})" % (self.LD(s_), 32 if kind == "s4" else 64, self.ea(i, s_))

        set32 = lambda x, v: "%s.lo = (%s.lo & 0xffffffff00000000ull) | rc_u32f(%s);" % (x, x, v)
        set64 = lambda x, v: "%s.lo = rc_u64f(%s);" % (x, v)

        # --- table-driven: rc_<mnemonic>(dest, src[, imm]) ---------------------
        if cm and dx and s_ is not None:
            pred, kind = cm.group(1), cm.group(2)
            if pred is None:
                if len(ops) != 3 or ops[2].type != x86.X86_OP_IMM:
                    return False
                p = ops[2].imm & 7
            else:
                p = CMP_PRED.index(pred)
            k = {"ps": "v", "pd": "v", "ss": "s4", "sd": "s8"}[kind]
            out.append("%s = rc_cmp%s(%s, %s, %d);" % (dx, kind, dx, src(k), p))
            return True
        if m in KIND and dx and s_ is not None:
            if m in SHIFTS and s_.type == x86.X86_OP_IMM:
                out.append("%s = rc_%s(%s, (rc_v128){0x%x, 0});" % (dx, m, dx, s_.imm & 0xff))
            elif m in WITH_IMM:
                out.append("%s = rc_%s(%s, %s, 0x%x);" % (dx, m, dx, src("v"), ops[2].imm & 0xff))
            elif s_.type == x86.X86_OP_IMM:
                return False
            else:
                out.append("%s = rc_%s(%s, %s);" % (dx, m, dx, src(KIND[m])))
            return True

        # --- moves ---------------------------------------------------------------
        if m in ("movss", "movsd") and (dx or sx):
            n = 4 if m == "movss" else 8
            if dx and sx:
                out.append("%s.lo = %s;" % (dx, "(%s.lo & 0xffffffff00000000ull) | (uint32_t)%s.lo" % (dx, sx)
                                           if n == 4 else "%s.lo" % sx))
            elif dx:
                out.append("%s.lo = %s(%s, %s); %s.hi = 0;" % (dx, self.LD(s_), CTYPE[n], self.ea(i, s_), dx))
            else:
                out.append("%s(%s, %s, %s.lo);" % (self.ST(d), CTYPE[n], self.ea(i, d), sx))
            return True
        if m in ("movaps", "movups", "movapd", "movupd", "movdqa", "movdqu", "lddqu",
                 "movntps", "movntpd", "movntdq"):
            if dx:
                out.append("%s = %s;" % (dx, v128(s_, sx)))
            elif sx:
                out.append("%s(rc_v128, %s, %s);" % (self.ST(d), self.ea(i, d), sx))
            else:
                return False
            return True
        if m in ("movd", "movq"):
            n = 4 if m == "movd" else 8
            if dx and sx:
                out.append("%s.lo = %s.lo; %s.hi = 0;" % (dx, sx, dx))
            elif dx:
                out.append("%s.lo = (%s)%s; %s.hi = 0;" % (dx, CTYPE[n], self.rd(i, s_, n), dx))
            elif sx:
                out.append(self.wr(i, d, "(%s)%s.lo" % (CTYPE[n], sx)))
            else:
                return False
            return True
        if m in ("movlps", "movlpd", "movhps", "movhpd"):
            part = "lo" if m[3] == "l" else "hi"
            if dx and sx:   # movlps/movhps reg,reg are movhlps/movlhps
                out.append("%s.%s = %s.%s;" % (dx, "lo" if part == "lo" else "hi", sx, "hi" if part == "lo" else "lo"))
            elif dx:
                out.append("%s.%s = %s(uint64_t, %s);" % (dx, part, self.LD(s_), self.ea(i, s_)))
            elif sx:
                out.append("%s(uint64_t, %s, %s.%s);" % (self.ST(d), self.ea(i, d), sx, part))
            else:
                return False
            return True
        if m in ("movhlps", "movlhps") and dx and sx:
            out.append("%s.lo = %s.hi;" % (dx, sx) if m == "movhlps" else "%s.hi = %s.lo;" % (dx, sx))
            return True
        if m in ("movmskps", "movmskpd", "pmovmskb") and sx:
            out.append(self.wr(i, d, "rc_%s(%s)" % (m, sx)))
            return True
        if m == "pextrw" and sx:
            out.append(self.wr(i, d, "rc_pextrw(%s, %d)" % (sx, ops[2].imm & 7)))
            return True
        if m == "pinsrw" and dx:
            out.append("%s = rc_pinsrw(%s, %s, %d);" % (dx, dx, self.rd(i, s_, 2), ops[2].imm & 7))
            return True

        # --- bitwise ---------------------------------------------------------------
        bitw = {"xorps": "^", "xorpd": "^", "pxor": "^", "andps": "&", "andpd": "&", "pand": "&",
                "orps": "|", "orpd": "|", "por": "|"}
        if m in bitw and dx:
            if sx == dx and bitw[m] == "^":
                out.append("%s.lo = 0; %s.hi = 0;" % (dx, dx))
            else:
                out.append("{ rc_v128 b = %s; %s.lo %s= b.lo; %s.hi %s= b.hi; }" % (v128(s_, sx), dx, bitw[m], dx, bitw[m]))
            return True
        if m in ("andnps", "andnpd", "pandn") and dx:
            out.append("{ rc_v128 b = %s; %s.lo = ~%s.lo & b.lo; %s.hi = ~%s.hi & b.hi; }" % (v128(s_, sx), dx, dx, dx, dx))
            return True

        # --- scalar compare / conversions ------------------------------------------------
        if m in ("ucomiss", "comiss", "ucomisd", "comisd") and dx:
            a, b = (f32(d, dx), f32(s_, sx)) if "ss" in m else (f64(d, dx), f64(s_, sx))
            out.append("{ double a = %s, b = %s; int u = a != a || b != b; zf = u | (a == b); pf = u; "
                       "cf = u | (a < b); of = 0; sf = 0; }" % (a, b))
            return True
        if m in ("cvtsi2ss", "cvtsi2sd") and dx:
            n = s_.size
            v = "(%s)(%s)%s" % ("float" if m.endswith("ss") else "double", STYPE[n], self.rd(i, s_))
            out.append(set32(dx, v) if m.endswith("ss") else set64(dx, v))
            return True
        if m in ("cvttss2si", "cvttsd2si", "cvtss2si", "cvtsd2si"):
            n = d.size
            src_ = f32(s_, sx) if "ss" in m else f64(s_, sx)
            fn = "rc_%s_%s_i%d" % ("cvtt" if m.startswith("cvtt") else "cvt", "f32" if "ss" in m else "f64", 8 * n)
            out.append(self.wr(i, d, "(uint64_t)(%s)%s(%s)" % (CTYPE[n], fn, src_)))
            return True
        if m == "cvtss2sd" and dx:
            out.append(set64(dx, "(double)%s" % f32(s_, sx)))
            return True
        if m == "cvtsd2ss" and dx:
            out.append(set32(dx, "(float)%s" % f64(s_, sx)))
            return True

        # --- MXCSR: only the control word is stored; rounding/FTZ are not modelled ----
        if m == "stmxcsr" and d.type == x86.X86_OP_MEM:
            out.append("RC_ST(uint32_t, %s, c->mxcsr ? c->mxcsr : 0x1f80);" % self.ea(i, d))
            return True
        if m == "ldmxcsr" and d.type == x86.X86_OP_MEM:
            out.append("c->mxcsr = RC_LD(uint32_t, %s);" % self.ea(i, d))
            return True
        return False
