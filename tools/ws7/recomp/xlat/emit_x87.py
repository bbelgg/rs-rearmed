"""x87 and MMX instructions (32-bit front end, xlat/x86_32.py).

x87 instructions are decoded from their own bytes (escape opcode D8..DF and the ModRM byte):
Capstone's operand lists for the register forms differ from form to form, the encoding does
not. The memory operand, when there is one, still comes from Capstone (ea).

The state lives in rc_ctx (c->fpu, runtime/rc32_x87.h): ST(i), rc87_push / rc87_pop. The
semantics, and what is not modelled, are described there. Register moves (fxch, fcmov,
fst st(i)) move values and leave the tags alone, as the reference (QEMU) does; on code that
never touches an empty register that is what hardware does.

MMX instructions reuse the SSE helpers of rc_sse.h on a 128-bit value whose low half is the
mm register; the forms whose MMX meaning is not the low half of the SSE one (pack*, punpckh*)
are composed from the SSE helpers.
"""
from capstone import x86

from .common import COND, CTYPE, STYPE, Unsupported  # noqa: F401

PREFIXES = {0x26, 0x2e, 0x36, 0x3e, 0x64, 0x65, 0x66, 0x67, 0x9b, 0xf0, 0xf2, 0xf3}

ARITH = ["fadd", "fmul", "fcom", "fcomp", "fsub", "fsubr", "fdiv", "fdivr"]
OPS = {"fadd": "a + b", "fmul": "a * b", "fsub": "a - b", "fsubr": "b - a", "fdiv": "a / b",
       "fdivr": "b / a"}
CONSTS = {0xe8: "1.0", 0xe9: "3.32192809488736234787", 0xea: "1.44269504088896340736",
          0xeb: "3.14159265358979323846", 0xec: "0.301029995663981195214",
          0xed: "0.693147180559945309417", 0xee: "0.0"}
FCMOV = {(0xda, 0): "cf", (0xda, 1): "zf", (0xda, 2): "(cf | zf)", (0xda, 3): "pf",
         (0xdb, 0): "!cf", (0xdb, 1): "!zf", (0xdb, 2): "!(cf | zf)", (0xdb, 3): "!pf"}

MMX_PLAIN = set(("paddb paddw paddd paddq psubb psubw psubd psubq paddsb paddsw paddusb paddusw "
                 "psubsb psubsw psubusb psubusw pmullw pmulhw pmulhuw pmuludq pmaddwd psadbw "
                 "pcmpeqb pcmpeqw pcmpeqd pcmpgtb pcmpgtw pcmpgtd pminub pmaxub pminsw pmaxsw "
                 "pavgb pavgw punpcklbw punpcklwd punpckldq pshufb palignr").split())
MMX_SHIFTS = set("psllw pslld psllq psrlw psrld psrlq psraw psrad".split())
MMX_PACK = {"packsswb", "packuswb", "packssdw"}
MMX_UNPCKH = {"punpckhbw": "punpcklbw", "punpckhwd": "punpcklwd", "punpckhdq": "punpckldq"}
MMX_BITW = {"pand": "a & b", "por": "a | b", "pxor": "a ^ b", "pandn": "~a & b"}


def _fp(n):
    return {4: "float", 8: "double"}[n]


class X87Mixin:
    # --- x87 ----------------------------------------------------------------
    def x87_opcode(self, i):
        b = bytes(i.bytes)
        k = 0
        while k < len(b) and b[k] in PREFIXES:
            k += 1
        if k + 1 >= len(b) or not 0xd8 <= b[k] <= 0xdf:
            return None
        return b[k], b[k + 1]

    def x87_mem(self, i):
        for op in i.operands:
            if op.type == x86.X86_OP_MEM:
                return op
        raise Unsupported("x87 memory operand missing at 0x%x" % i.address)

    def emit_x87(self, i, out):
        m = i.mnemonic
        if m in ("wait", "fwait"):
            return True
        oc = self.x87_opcode(i)
        if oc is None:
            return False
        op, modrm = oc
        mod, reg, rm = modrm >> 6, (modrm >> 3) & 7, modrm & 7
        A = i.address
        if mod != 3:
            s = self.x87_memform(i, op, reg)
        else:
            s = self.x87_regform(i, op, reg, rm, modrm)
        if s is None:
            raise Unsupported("x87 %s %s (%02x %02x) at 0x%x" % (m, i.op_str, op, modrm, A))
        out.append(s)
        return True

    def x87_memform(self, i, op, reg):
        mo = self.x87_mem(i)
        p = self.ea(i, mo)
        L, S = self.LD(mo), self.ST(mo)
        if op in (0xd8, 0xdc, 0xda, 0xde):      # arithmetic with a memory operand
            if op in (0xd8, 0xdc):
                b = "(double)%s(%s, %s)" % (L, _fp(4 if op == 0xd8 else 8), p)
            else:
                b = "(double)%s(%s, %s)" % (L, "int32_t" if op == 0xda else "int16_t", p)
            name = ARITH[reg]
            if name in ("fcom", "fcomp"):
                return "{ double b = %s; rc87_cmp(c, ST(0), b);%s }" % (b, " rc87_pop(c);" if name == "fcomp" else "")
            return "{ double a = ST(0), b = %s; ST(0) = %s; }" % (b, OPS[name])
        if op == 0xd9:
            if reg == 0:
                return "rc87_push(c, (double)%s(float, %s));" % (L, p)
            if reg in (2, 3):
                return "%s(float, %s, (float)ST(0));%s" % (S, p, " rc87_pop(c);" if reg == 3 else "")
            if reg == 4:
                return "rc87_fldenv(c, (const uint8_t *)(uintptr_t)%s);" % p
            if reg == 5:
                return "c->fpu.cw = %s(uint16_t, %s);" % (L, p)
            if reg == 6:
                return "rc87_fnstenv(c, (uint8_t *)(uintptr_t)%s);" % p
            if reg == 7:
                return "%s(uint16_t, %s, c->fpu.cw);" % (S, p)
            return None
        if op == 0xdb:
            if reg == 0:
                return "rc87_push(c, (double)%s(int32_t, %s));" % (L, p)
            if reg in (1, 2, 3):
                r = "trunc(ST(0))" if reg == 1 else "rc87_round(c, ST(0))"
                return "%s(int32_t, %s, rc87_i32(%s));%s" % (S, p, r, "" if reg == 2 else " rc87_pop(c);")
            if reg == 5:
                return "rc87_push(c, rc87_from80((const uint8_t *)(uintptr_t)%s));" % p
            if reg == 7:
                return "rc87_to80((uint8_t *)(uintptr_t)%s, ST(0)); rc87_pop(c);" % p
            return None
        if op == 0xdd:
            if reg == 0:
                return "rc87_push(c, %s(double, %s));" % (L, p)
            if reg == 1:
                return "%s(int64_t, %s, rc87_i64(trunc(ST(0)))); rc87_pop(c);" % (S, p)
            if reg in (2, 3):
                return "%s(double, %s, ST(0));%s" % (S, p, " rc87_pop(c);" if reg == 3 else "")
            if reg == 4:
                return "rc87_frstor(c, (const uint8_t *)(uintptr_t)%s);" % p
            if reg == 6:
                return "rc87_fnsave(c, (uint8_t *)(uintptr_t)%s);" % p
            if reg == 7:
                return "%s(uint16_t, %s, rc87_fnstsw(c));" % (S, p)
            return None
        if op == 0xdf:
            if reg == 0:
                return "rc87_push(c, (double)%s(int16_t, %s));" % (L, p)
            if reg in (1, 2, 3):
                r = "trunc(ST(0))" if reg == 1 else "rc87_round(c, ST(0))"
                return "%s(int16_t, %s, rc87_i16(%s));%s" % (S, p, r, "" if reg == 2 else " rc87_pop(c);")
            if reg == 5:
                return "rc87_push(c, (double)%s(int64_t, %s));" % (L, p)
            if reg == 7:
                return "%s(int64_t, %s, rc87_i64(rc87_round(c, ST(0)))); rc87_pop(c);" % (S, p)
            return None   # fbld / fbstp (BCD)
        return None

    def x87_regform(self, i, op, reg, rm, modrm):
        if op == 0xd8:          # st0 = st0 op st(i)
            name = ARITH[reg]
            if name in ("fcom", "fcomp"):
                return "rc87_cmp(c, ST(0), ST(%d));%s" % (rm, " rc87_pop(c);" if name == "fcomp" else "")
            return "{ double a = ST(0), b = ST(%d); ST(0) = %s; }" % (rm, OPS[name])
        if op in (0xdc, 0xde):  # st(i) = st(i) op st0 (sub/div: the reversed encodings), [pop]
            pop = " rc87_pop(c);" if op == 0xde else ""
            if op == 0xde and modrm == 0xd9:
                return "rc87_cmp(c, ST(0), ST(1)); rc87_pop(c); rc87_pop(c);"   # fcompp
            if reg in (2, 3):
                if op == 0xdc:  # aliases of fcom/fcomp
                    return "rc87_cmp(c, ST(0), ST(%d));%s" % (rm, " rc87_pop(c);" if reg == 3 else "")
                return None
            # dc/de: E0 fsubr  E8 fsub  F0 fdivr  F8 fdiv  (st(i) = st0 - st(i) for E0, ...)
            e = {0: "a + b", 1: "a * b", 4: "b - a", 5: "a - b", 6: "b / a", 7: "a / b"}[reg]
            return "{ double a = ST(%d), b = ST(0); ST(%d) = %s; }%s" % (rm, rm, e, pop)
        if op == 0xd9:
            if reg == 0:
                return "{ double v = ST(%d); rc87_push(c, v); }" % rm
            if reg == 1:
                return "{ double t = ST(0); ST(0) = ST(%d); ST(%d) = t; }" % (rm, rm)
            if modrm == 0xd0:
                return ";"
            if reg == 3:   # fstp1 (undocumented alias of fstp st(i))
                return "ST(%d) = ST(0); rc87_pop(c);" % rm
            if modrm in CONSTS:
                return "rc87_push(c, %s);" % CONSTS[modrm]
            t = {
                0xe0: "ST(0) = -ST(0);",
                0xe1: "ST(0) = fabs(ST(0));",
                0xe4: "rc87_cmp(c, ST(0), 0.0);",
                0xe5: "rc87_fxam(c);",
                0xf0: "ST(0) = expm1(ST(0) * 0.693147180559945309417);",
                0xf1: "ST(1) = ST(1) * log2(ST(0)); rc87_pop(c);",
                0xf2: "if (rc87_trig_ok(c)) { ST(0) = tan(ST(0)); rc87_push(c, 1.0); }",
                0xf3: "ST(1) = atan2(ST(1), ST(0)); rc87_pop(c);",
                0xf4: "rc87_fxtract(c);",
                0xf5: "rc87_fprem(c, 1);",
                0xf6: "c->fpu.top = (c->fpu.top - 1) & 7;",
                0xf7: "c->fpu.top = (c->fpu.top + 1) & 7;",
                0xf8: "rc87_fprem(c, 0);",
                0xf9: "ST(1) = ST(1) * (log1p(ST(0)) / 0.693147180559945309417); rc87_pop(c);",
                0xfa: "ST(0) = sqrt(ST(0));",
                0xfb: "if (rc87_trig_ok(c)) { double v = ST(0); ST(0) = sin(v); rc87_push(c, cos(v)); }",
                0xfc: "ST(0) = rc87_round(c, ST(0));",
                0xfd: "{ double s = trunc(ST(1)); ST(0) = s != s ? s : ldexp(ST(0), "
                      "s > 100000 ? 100000 : s < -100000 ? -100000 : (int)s); }",
                0xfe: "if (rc87_trig_ok(c)) ST(0) = sin(ST(0));",
                0xff: "if (rc87_trig_ok(c)) ST(0) = cos(ST(0));",
            }
            return t.get(modrm)
        if op in (0xda, 0xdb) and reg < 4 and (op, reg) in FCMOV:
            return "if (%s) { double v = ST(%d); ST(0) = v; }" % (FCMOV[(op, reg)], rm)
        if op == 0xda and modrm == 0xe9:
            return "rc87_cmp(c, ST(0), ST(1)); rc87_pop(c); rc87_pop(c);"   # fucompp
        if op == 0xdb:
            if modrm == 0xe2:
                return "c->fpu.sw &= 0x7f00;"   # fnclex
            if modrm == 0xe3:
                return "rc87_init(c);"
            if modrm in (0xe0, 0xe1, 0xe4):     # fneni / fndisi / fnsetpm: no-ops
                return ";"
            if reg in (5, 6):                   # fucomi / fcomi
                return self.x87_comi(rm, False)
            return None
        if op == 0xdd:
            if reg == 0:
                return "c->fpu.tag |= 1u << ((c->fpu.top + %d) & 7);" % rm      # ffree
            if reg in (2, 3):
                return "{ double v = ST(0); ST(%d) = v; }%s" % (rm, " rc87_pop(c);" if reg == 3 else "")
            if reg in (4, 5):
                return "rc87_cmp(c, ST(0), ST(%d));%s" % (rm, " rc87_pop(c);" if reg == 5 else "")
            return None
        if op == 0xdf:
            if modrm == 0xe0:
                return self.wr_reg("ax", "rc87_fnstsw(c)")
            if reg in (5, 6):                   # fucomip / fcomip
                return self.x87_comi(rm, True)
            if reg == 0:                        # ffreep
                return "c->fpu.tag |= 1u << ((c->fpu.top + %d) & 7); rc87_pop(c);" % rm
            return None
        return None

    @staticmethod
    def x87_comi(rm, pop):
        return ("{ double a = ST(0), b = ST(%d); int u = a != a || b != b; zf = u | (a == b); pf = u; "
                "cf = u | (a < b); of = 0; sf = 0; c->fpu.sw &= ~RC87_C1; }%s" % (rm, " rc87_pop(c);" if pop else ""))

    # --- MMX ------------------------------------------------------------------
    def mmx_reg(self, i, op):
        if op.type != x86.X86_OP_REG:
            return None
        n = i.reg_name(op.reg)
        return int(n[2:]) if n.startswith("mm") and n[2:].isdigit() else None

    def emit_mmx(self, i, out):
        m = i.mnemonic
        if m == "emms" or m == "femms":
            out.append("c->fpu.tag = 0xff;")
            return True
        ops = i.operands
        if not any(self.mmx_reg(i, o) is not None for o in ops):
            return False
        d, s = ops[0], ops[1] if len(ops) > 1 else None
        dm = self.mmx_reg(i, d)
        sm = self.mmx_reg(i, s) if s is not None else None
        pre = "rc87_mmx(c); "

        def src64():
            if sm is not None:
                return "c->fpu.mm[%d]" % sm
            if s.type == x86.X86_OP_MEM:
                return "%s(uint64_t, %s)" % (self.LD(s), self.ea(i, s))
            raise Unsupported("mmx source at 0x%x" % i.address)

        if m in ("movq", "movd") or m == "movntq":
            n = 8 if m != "movd" else 4
            if dm is not None:
                v = src64() if (sm is not None or s.type == x86.X86_OP_MEM) and n == 8 else \
                    "(uint64_t)(%s)%s" % (CTYPE[n], self.rd(i, s, n))
                out.append(pre + "c->fpu.mm[%d] = %s;" % (dm, v))
            elif sm is not None:
                out.append(pre + self.wr(i, d, "(%s)c->fpu.mm[%d]" % (CTYPE[n], sm)))
            else:
                return False
            return True
        if m == "movq2dq" or m == "movdq2q":
            if m == "movq2dq":
                out.append(pre + "%s = (rc_v128){c->fpu.mm[%d], 0};" % (self.xr(i, d), sm))
            else:
                out.append(pre + "c->fpu.mm[%d] = %s[0];" % (dm, self.xr(i, s)))
            return True
        if dm is None:
            if m in ("pmovmskb", "pextrw") and sm is not None:
                v = "rc_pmovmskb((rc_v128){c->fpu.mm[%d], 0}) & 0xff" % sm if m == "pmovmskb" else \
                    "(c->fpu.mm[%d] >> %d) & 0xffff" % (sm, 16 * (ops[2].imm & 3))
                out.append(pre + self.wr(i, d, v))
                return True
            return False
        D = "c->fpu.mm[%d]" % dm
        if m in MMX_PLAIN:
            imm = ", 0x%x" % (ops[2].imm & 0xff) if m == "palignr" else ""
            if m == "palignr":   # 16-byte concatenation of the 8-byte operands
                out.append(pre + "{ unsigned __int128 v = ((unsigned __int128)%s << 64) | %s; unsigned n = %d; "
                           "%s = n >= 16 ? 0 : (uint64_t)(v >> (8 * n)); }" % (D, src64(), ops[2].imm & 0xff, D))
                return True
            if m == "pshufb":
                out.append(pre + "{ uint64_t a = %s, b = %s, r = 0; for (int k = 0; k < 8; k++) { uint8_t x = b >> (8 * k); "
                           "if (!(x & 0x80)) r |= ((a >> (8 * (x & 7))) & 0xff) << (8 * k); } %s = r; }" % (D, src64(), D))
                return True
            out.append(pre + "%s = rc_%s((rc_v128){%s, 0}, (rc_v128){%s, 0}%s)[0];" % (D, m, D, src64(), imm))
            return True
        if m in MMX_SHIFTS:
            cnt = "0x%x" % (s.imm & 0xff) if s.type == x86.X86_OP_IMM else src64()
            out.append(pre + "%s = rc_%s((rc_v128){%s, 0}, (rc_v128){%s, 0})[0];" % (D, m, D, cnt))
            return True
        if m in MMX_PACK:
            out.append(pre + "%s = rc_%s((rc_v128){%s, %s}, (rc_v128){0, 0})[0];" % (D, m, D, src64()))
            return True
        if m in MMX_UNPCKH:
            out.append(pre + "%s = rc_%s((rc_v128){%s >> 32, 0}, (rc_v128){%s >> 32, 0})[0];"
                       % (D, MMX_UNPCKH[m], D, src64()))
            return True
        if m in MMX_BITW:
            out.append(pre + "{ uint64_t a = %s, b = %s; %s = %s; }" % (D, src64(), D, MMX_BITW[m]))
            return True
        if m == "pshufw":
            out.append(pre + "%s = rc_pshufw64(%s, %d);" % (D, src64(), ops[2].imm & 0xff))
            return True
        if m == "pinsrw":
            k = 16 * (ops[2].imm & 3)
            out.append(pre + "%s = (%s & ~(0xffffull << %d)) | ((uint64_t)(uint16_t)%s << %d);"
                       % (D, D, k, self.rd(i, s, 2), k))
            return True
        if m in ("cvtpi2ps", "cvtpi2pd", "cvtps2pi", "cvttps2pi", "cvtpd2pi", "cvttpd2pi"):
            return False
        raise Unsupported("mmx %s at 0x%x" % (m, i.address))
