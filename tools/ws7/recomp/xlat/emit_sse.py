"""SSE/SSE2/SSE3/SSSE3 instructions.

Semantics live in runtime/rc_sse.h as `rc_<mnemonic>(dest, src[, imm])`
helpers over rc_v128; most mnemonics are therefore one table entry here.
rc_v128 is a vector type: x[0]/x[1] are its 64-bit halves, and scalar float
operands are lane 0 (rc_lf32/rc_lf64, rc_setf32/rc_setf64), so the value never
leaves its NEON register.
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
            return "rc_lf32(%s)" % x if x else "%s(float, %s)" % (self.LD(op), self.ea(i, op))

        def f64(op, x):
            return "rc_lf64(%s)" % x if x else "%s(double, %s)" % (self.LD(op), self.ea(i, op))

        def v128(op, x):
            return x if x else "%s(rc_v128, %s)" % (self.LD(op), self.ea(i, op))

        def src(kind):
            if sx:
                return sx
            if kind == "v":
                return "%s(rc_v128, %s)" % (self.LD(s_), self.ea(i, s_))
            return "((rc_v128){%s(uint%d_t, %s), 0})" % (self.LD(s_), 32 if kind == "s4" else 64, self.ea(i, s_))

        set32 = lambda x, v: "%s = rc_setf32(%s, %s);" % (x, x, v)
        set64 = lambda x, v: "%s = rc_setf64(%s, %s);" % (x, x, v)

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
                out.append("%s = rc_movss(%s, %s);" % (dx, dx, sx) if n == 4 else "%s[0] = %s[0];" % (dx, sx))
            elif dx:
                out.append("%s = (rc_v128){%s(%s, %s), 0};" % (dx, self.LD(s_), CTYPE[n], self.ea(i, s_)))
            else:
                out.append("%s(%s, %s, %s[0]);" % (self.ST(d), CTYPE[n], self.ea(i, d), sx))
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
                out.append("%s = (rc_v128){%s[0], 0};" % (dx, sx))
            elif dx:
                out.append("%s = (rc_v128){(%s)%s, 0};" % (dx, CTYPE[n], self.rd(i, s_, n)))
            elif sx:
                out.append(self.wr(i, d, "(%s)%s[0]" % (CTYPE[n], sx)))
            else:
                return False
            return True
        if m in ("movlps", "movlpd", "movhps", "movhpd"):
            part = 0 if m[3] == "l" else 1
            if dx and sx:   # movlps/movhps reg,reg are movhlps/movlhps
                out.append("%s[%d] = %s[%d];" % (dx, part, sx, 1 - part))
            elif dx:
                out.append("%s[%d] = %s(uint64_t, %s);" % (dx, part, self.LD(s_), self.ea(i, s_)))
            elif sx:
                out.append("%s(uint64_t, %s, %s[%d]);" % (self.ST(d), self.ea(i, d), sx, part))
            else:
                return False
            return True
        if m in ("movhlps", "movlhps") and dx and sx:
            out.append("%s[0] = %s[1];" % (dx, sx) if m == "movhlps" else "%s[1] = %s[0];" % (dx, sx))
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
                out.append("%s = (rc_v128){0, 0};" % dx)
            else:
                out.append("%s %s= %s;" % (dx, bitw[m], v128(s_, sx)))
            return True
        if m in ("andnps", "andnpd", "pandn") and dx:
            out.append("%s = ~%s & %s;" % (dx, dx, v128(s_, sx)))
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
