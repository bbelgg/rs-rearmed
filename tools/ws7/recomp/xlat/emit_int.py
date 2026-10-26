"""Integer and system instructions: data movement, ALU and flags,
shifts/rotates, multiply/divide, bit operations, atomics, string operations,
flag moves, cpuid and the MXCSR/fence/prefetch odds and ends.

Every flag an instruction defines is assigned to the C locals zf sf cf of pf
(clang removes the dead ones); AF and DF are not modelled (DF is assumed
clear). Flags x86 leaves undefined are still given a deterministic value.
"""
from capstone import x86

from .common import CTYPE, MASK, REGS, STYPE, COND, Unsupported  # noqa: F401

ALU = ("add", "sub", "cmp", "adc", "sbb", "and", "or", "xor", "test")
SHIFTS = ("shl", "sal", "shr", "sar", "rol", "ror", "rcl", "rcr")
PREFETCH = {"prefetcht0", "prefetcht1", "prefetcht2", "prefetchnta", "prefetchw", "prefetch"}
FENCES = {"lfence", "sfence", "mfence"}
FAIL = {"retf", "lret", "iret", "iretd", "iretq", "in", "out", "sti", "cli", "int3", "int1",
        "int", "syscall", "sysenter", "insb", "insd", "outsb", "outsd", "std"}
STRING = {"movs", "stos", "lods", "scas", "cmps"}


def _is_xmm(i, op):
    return op.type == x86.X86_OP_REG and i.reg_name(op.reg).startswith(("xmm", "mm"))


class IntMixin:
    # ---------------------------------------------------------------------
    # shared pieces
    # ---------------------------------------------------------------------
    @staticmethod
    def alu(m, n):
        """C statements computing `t` and the flags of ALU op `m` at n bytes
        from uint64_t a, b (already masked to n bytes). Declares t."""
        M, top = MASK[n], 8 * n - 1
        szp = "zf = (t & %s) == 0; sf = (t >> %d) & 1; pf = rc_parity(t);" % (M, top)
        if m in ("add", "adc"):
            cin = " + cf" if m == "adc" else ""
            cf = "(t < a) | ((t == a) & (b != 0))" if m == "adc" else "t < a"
            return ("uint64_t t = (a + b%s) & %s; cf = %s; of = (((a ^ t) & (b ^ t)) >> %d) & 1; %s"
                    % (cin, M, cf, top, szp))
        if m in ("sub", "cmp", "sbb"):
            cin = " - cf" if m == "sbb" else ""
            cf = "(a < b) | ((a == b) & cf)" if m == "sbb" else "a < b"
            return ("uint64_t t = (a - b%s) & %s; cf = %s; of = (((a ^ b) & (a ^ t)) >> %d) & 1; %s"
                    % (cin, M, cf, top, szp))
        if m in ("and", "or", "xor", "test"):
            c = {"and": "&", "test": "&", "or": "|", "xor": "^"}[m]
            return "uint64_t t = (a %s b) & %s; cf = 0; of = 0; %s" % (c, M, szp)
        if m == "inc":
            return "uint64_t t = (a + 1) & %s; of = a == %s - (1ull << %d); %s" % (M, M, top, szp)
        if m == "dec":
            return "uint64_t t = (a - 1) & %s; of = a == (1ull << %d); %s" % (M, top, szp)
        if m == "neg":
            return "uint64_t t = (0 - a) & %s; cf = a != 0; of = a == (1ull << %d); %s" % (M, top, szp)
        if m == "not":
            return "uint64_t t = ~a & %s;" % M
        raise Unsupported("alu " + m)

    def wr_reg(self, name, val):
        r, s, sh = REGS[name]
        if s == 8:
            return "%s = (uint64_t)(%s);" % (r, val)
        if s == 4:
            return "%s = (uint32_t)(%s);" % (r, val)
        if s == 2:
            return "%s = (%s & ~0xffffull) | (uint16_t)(%s);" % (r, r, val)
        return "%s = (%s & ~0xffull) | (uint8_t)(%s);" % (r, r, val)

    # ---------------------------------------------------------------------
    def emit_int2(self, i, out):      # kept for core.py's call chain; all handled in emit_int
        return False

    def emit_int(self, i, out):
        m = i.mnemonic
        ops = i.operands
        A = i.address
        parts = m.split()
        base = parts[-1]
        if parts[0] == "lock":
            return self.emit_lock(i, out, base)
        isrep = parts[0] in ("rep", "repe", "repz", "repne", "repnz")
        if len(base) == 5 and base[:4] in STRING and base[4] in "bwdq":
            if self.string_op(i, out, parts):
                return True
        if isrep:
            return False
        if m == "mov" or m == "movabs":
            out.append(self.wr(i, ops[0], self.rd(i, ops[1], ops[0].size)))
        elif m == "movzx":
            out.append(self.wr(i, ops[0], self.rd(i, ops[1])))
        elif m in ("movsx", "movsxd"):
            s = ops[1].size
            out.append(self.wr(i, ops[0], "(uint64_t)(int64_t)(%s)%s" % (STYPE[s], self.rd(i, ops[1]))))
        elif m == "lea":
            out.append(self.wr(i, ops[0], self.ea(i, ops[1])))
        elif m == "push":
            out.append("{ uint64_t v = %s; rsp -= 8; RC_STS(uint64_t, rsp, v); }" % self.rd(i, ops[0], 8))
        elif m == "pop":
            out.append("{ uint64_t v = RC_LDS(uint64_t, rsp); rsp += 8; %s }" % self.wr(i, ops[0], "v"))
        elif m == "leave":
            out.append("rsp = rbp; rbp = RC_LDS(uint64_t, rsp); rsp += 8;")
        elif m in ALU:
            n = ops[0].size
            if m == "xor" and ops[0].type == x86.X86_OP_REG and ops[1].type == x86.X86_OP_REG \
                    and ops[0].reg == ops[1].reg:
                out.append("{ uint64_t t = 0; cf = 0; of = 0; %s %s }"
                           % (self.szp(n), self.wr(i, ops[0], "t")))
                return True
            s = "{ uint64_t a = %s & %s, b = %s & %s; %s" % (
                self.rd(i, ops[0]), MASK[n], self.rd(i, ops[1], n), MASK[n], self.alu(m, n))
            if m not in ("cmp", "test"):
                s += " " + self.wr(i, ops[0], "t")
            out.append(s + " }")
        elif m in ("inc", "dec", "neg", "not"):
            n = ops[0].size
            out.append("{ uint64_t a = %s & %s; %s %s }" % (self.rd(i, ops[0]), MASK[n], self.alu(m, n),
                                                           self.wr(i, ops[0], "t")))
        elif m in SHIFTS:
            self.shift(i, out)
        elif m in ("shld", "shrd"):
            self.shiftd(i, out)
        elif m == "imul":
            self.imul(i, out)
        elif m == "mul":
            self.mul(i, out)
        elif m in ("div", "idiv"):
            self.div(i, out)
        elif m.startswith("cmov") and m[4:] in COND:
            v = "(%s ? %s : %s)" % (COND[m[4:]], self.rd(i, ops[1]), self.rd(i, ops[0]))
            out.append(self.wr(i, ops[0], v))
        elif m.startswith("set") and m[3:] in COND:
            out.append(self.wr(i, ops[0], "(%s) ? 1 : 0" % COND[m[3:]]))
        elif m in ("cdqe", "cdq", "cqo", "cwde", "cbw", "cwd"):
            out.append({"cdqe": "rax = (uint64_t)(int64_t)(int32_t)rax;",
                        "cwde": "rax = (uint32_t)(int32_t)(int16_t)rax;",
                        "cbw": "rax = (rax & ~0xffffull) | (uint16_t)(int16_t)(int8_t)rax;",
                        "cwd": "rdx = (rdx & ~0xffffull) | (uint16_t)((int16_t)rax >> 15);",
                        "cdq": "rdx = (uint32_t)((int32_t)rax >> 31);",
                        "cqo": "rdx = (uint64_t)((int64_t)rax >> 63);"}[m])
        elif m in ("bt", "bts", "btr", "btc"):
            self.bittest(i, out, m, False)
        elif m in ("bsf", "bsr", "tzcnt", "lzcnt", "popcnt"):
            self.bitscan(i, out)
        elif m == "bswap":
            n = ops[0].size
            out.append(self.wr(i, ops[0], "__builtin_bswap%d((%s)%s)" % (8 * n, CTYPE[n], self.rd(i, ops[0]))))
        elif m == "xchg":
            self.xchg(i, out)
        elif m == "xadd":
            n = ops[0].size
            out.append("{ uint64_t a = %s & %s, b = %s & %s; %s %s %s }"
                       % (self.rd(i, ops[0]), MASK[n], self.rd(i, ops[1]), MASK[n], self.alu("add", n),
                          self.wr(i, ops[1], "a"), self.wr(i, ops[0], "t")))
        elif m == "cmpxchg":
            # 32-bit forms write eax (and a register destination) back even when
            # the compare succeeds, clearing the upper halves, as Rosetta does
            n = ops[0].size
            acc = {1: "al", 2: "ax", 4: "eax", 8: "rax"}[n]
            keep = (" " + self.wr(i, ops[0], "b")) if n == 4 and ops[0].type == x86.X86_OP_REG else ""
            ok = (" rax = (uint32_t)rax;" if n == 4 else "")
            out.append("{ uint64_t a = (%s)rax, b = %s & %s; %s if (zf) { %s%s } else { %s%s } }"
                       % (CTYPE[n], self.rd(i, ops[0]), MASK[n], self.alu("cmp", n),
                          self.wr(i, ops[0], self.rd(i, ops[1])), ok, self.wr_reg(acc, "b"), keep))
        elif m == "pushfq":
            out.append("{ uint64_t f = rc_flags_pack(cf, pf, zf, sf, of); rsp -= 8; RC_STS(uint64_t, rsp, f); }")
        elif m == "popfq":
            out.append("{ uint64_t f = RC_LDS(uint64_t, rsp); rsp += 8; "
                       "cf = f & 1; pf = (f >> 2) & 1; zf = (f >> 6) & 1; sf = (f >> 7) & 1; of = (f >> 11) & 1; }")
        elif m == "lahf":
            out.append("rax = (rax & ~0xff00ull) | ((uint64_t)(cf | 2 | (pf << 2) | (zf << 6) | (sf << 7)) << 8);")
        elif m == "sahf":
            out.append("{ uint64_t f = rax >> 8; cf = f & 1; pf = (f >> 2) & 1; zf = (f >> 6) & 1; sf = (f >> 7) & 1; }")
        elif m == "clc":
            out.append("cf = 0;")
        elif m == "stc":
            out.append("cf = 1;")
        elif m == "cmc":
            out.append("cf = !cf;")
        elif m == "cld" or m == "pause" or m in PREFETCH:
            pass
        elif m in FENCES:
            out.append("__atomic_thread_fence(__ATOMIC_SEQ_CST);")
        elif m == "cpuid":
            out.append("{ uint32_t ra, rb, rcx_, rd_; rc_cpuid((uint32_t)rax, (uint32_t)rcx, &ra, &rb, &rcx_, &rd_); "
                       "rax = ra; rbx = rb; rcx = rcx_; rdx = rd_; }")
        elif m == "ldmxcsr":
            out.append("rc_ldmxcsr(c, RC_LD(uint32_t, %s));" % self.ea(i, ops[0]))
        elif m == "stmxcsr":
            out.append("RC_ST(uint32_t, %s, rc_stmxcsr(c));" % self.ea(i, ops[0]))
        elif m in FAIL:
            out.append('rc_fail("%s", 0x%xull);' % (m, A))
        else:
            return False
        return True

    # ---------------------------------------------------------------------
    # shifts and rotates
    # ---------------------------------------------------------------------
    def shift(self, i, out):
        m = i.mnemonic
        m = "shl" if m == "sal" else m
        ops = i.operands
        n = ops[0].size
        dst = ops[0]
        bits = 8 * n
        lim = 63 if n == 8 else 31
        if len(ops) == 1:
            kc = 1
        elif ops[1].type == x86.X86_OP_IMM:
            kc = ops[1].imm & lim
        else:
            kc = None
        a = "%s & %s" % (self.rd(i, dst), MASK[n])
        zext = n == 4 and dst.type == x86.X86_OP_REG
        if kc == 0:
            if zext:
                out.append(self.wr(i, dst, self.rd(i, dst)))
            return
        k = str(kc) if kc is not None else "k"
        body = self.shift_body(m, n, k, kc) + " " + self.wr(i, dst, "t")
        if kc is not None:
            out.append("{ uint64_t a = %s; %s }" % (a, body))
        else:
            z = (" else { %s }" % self.wr(i, dst, "a")) if zext else ""
            out.append("{ unsigned k = (uint8_t)rcx & %d; uint64_t a = %s; if (k) { %s }%s }" % (lim, a, body, z))

    def shift_body(self, m, n, k, kc):
        """Statements computing t and the flags for a shift/rotate of the
        masked value `a` by k (1..63 after masking; literal when kc is not None)."""
        B, M, top = 8 * n, MASK[n], 8 * n - 1
        szp = "zf = (t & %s) == 0; sf = (t >> %d) & 1; pf = rc_parity(t);" % (M, top)
        if m == "shl":
            cfe = ("(a >> %d) & 1" % (B - kc) if kc <= B else "0") if kc is not None else \
                "(k <= %d) ? ((a >> (%d - k)) & 1) : 0" % (B, B)
            return ("uint64_t t = (a << %s) & %s, c1 = %s; cf = c1; of = ((t >> %d) & 1) ^ c1; %s"
                    % (k, M, cfe, top, szp))
        if m == "shr":
            return ("uint64_t t = a >> %s; cf = (a >> (%s - 1)) & 1; of = (a >> %d) & 1; %s" % (k, k, top, szp))
        if m == "sar":
            return ("uint64_t t = (uint64_t)((int64_t)(%s)a >> %s) & %s; cf = ((int64_t)(%s)a >> (%s - 1)) & 1; "
                    "of = 0; %s" % (STYPE[n], k, M, STYPE[n], k, szp))
        if m in ("rol", "ror"):
            e = str(kc % B) if kc is not None else "e"
            pre = "" if kc is not None else "unsigned e = k %% %d; " % B
            if kc is not None and kc % B == 0:
                t = "a"
            elif m == "rol":
                t = "((a << %s) | (a >> (%d - %s))) & %s" % (e, B, e, M)
                if kc is None:
                    t = "e ? (%s) : a" % t
            else:
                t = "((a >> %s) | (a << (%d - %s))) & %s" % (e, B, e, M)
                if kc is None:
                    t = "e ? (%s) : a" % t
            if m == "rol":
                return pre + "uint64_t t = %s; cf = t & 1; of = ((t >> %d) & 1) ^ cf;" % (t, top)
            return pre + ("uint64_t t = %s; cf = (t >> %d) & 1; of = ((t >> %d) & 1) ^ ((t >> %d) & 1);"
                          % (t, top, top, top - 1))
        # rcl / rcr: rotate the (B+1)-bit value cf:a
        e = str(kc % (B + 1)) if kc is not None else "e"
        pre = "" if kc is not None else "unsigned e = k %% %d; " % (B + 1)
        w = "unsigned __int128 w = ((unsigned __int128)cf << %d) | a; " % B
        if m == "rcl":
            r = "((w << %s) | (w >> (%d - %s)))" % (e, B + 1, e)
        else:
            r = "((w >> %s) | (w << (%d - %s)))" % (e, B + 1, e)
        if kc is None or kc % (B + 1):
            if kc is None:
                r = "(e ? %s : w)" % r
        else:
            r = "w"
        s = (pre + w + "unsigned __int128 r = %s & (((unsigned __int128)1 << %d) - 1); "
             "uint64_t t = (uint64_t)r & %s; cf = (r >> %d) & 1; " % (r, B + 1, M, B))
        if m == "rcl":
            return s + "of = ((t >> %d) & 1) ^ cf;" % top
        return s + "of = ((t >> %d) & 1) ^ ((t >> %d) & 1);" % (top, top - 1)

    def shiftd(self, i, out):
        m = i.mnemonic
        ops = i.operands
        n = ops[0].size
        B, M, top = 8 * n, MASK[n], 8 * n - 1
        lim = 63 if n == 8 else 31
        if ops[2].type == x86.X86_OP_IMM:
            kc = ops[2].imm & lim
            if kc == 0:
                if n == 4 and ops[0].type == x86.X86_OP_REG:
                    out.append(self.wr(i, ops[0], self.rd(i, ops[0])))
                return
            pre, k = "", str(kc)
        else:
            pre, k = "unsigned k = (uint8_t)rcx & %d; " % lim, "k"
        if m == "shld":
            core = ("unsigned __int128 x = ((unsigned __int128)a << %d) | s; "
                    "uint64_t t = (uint64_t)((x << %s) >> %d) & %s; cf = (uint64_t)(x >> (%d - %s)) & 1;" % (B, k, B, M, 2 * B, k))
        else:
            core = ("unsigned __int128 x = ((unsigned __int128)s << %d) | a; "
                    "uint64_t t = (uint64_t)(x >> %s) & %s; cf = (uint64_t)(x >> (%s - 1)) & 1;" % (B, k, M, k))
        core += " of = ((t ^ a) >> %d) & 1; zf = (t & %s) == 0; sf = (t >> %d) & 1; pf = rc_parity(t); %s" % (
            top, M, top, self.wr(i, ops[0], "t"))
        body = "uint64_t a = %s & %s, s = %s & %s; " % (self.rd(i, ops[0]), M, self.rd(i, ops[1]), M)
        if ops[2].type == x86.X86_OP_IMM:
            out.append("{ %s%s }" % (body, core))
        else:
            z = (" else { %s }" % self.wr(i, ops[0], self.rd(i, ops[0]))) if n == 4 and ops[0].type == x86.X86_OP_REG else ""
            out.append("{ %s%sif (k) { %s }%s }" % (pre, body, core, z))

    # ---------------------------------------------------------------------
    # multiply / divide
    # ---------------------------------------------------------------------
    def imul(self, i, out):
        ops = i.operands
        n = ops[0].size
        if len(ops) == 1:
            s = "uint64_t s = %s;" % self.rd(i, ops[0])
            if n == 1:
                out.append("{ %s int64_t p = (int64_t)(int8_t)rax * (int8_t)s; rax = (rax & ~0xffffull) | (uint16_t)p; "
                           "cf = of = p != (int8_t)p; }" % s)
            elif n == 2:
                out.append("{ %s int64_t p = (int64_t)(int16_t)rax * (int16_t)s; rax = (rax & ~0xffffull) | (uint16_t)p; "
                           "rdx = (rdx & ~0xffffull) | (uint16_t)(p >> 16); cf = of = p != (int16_t)p; }" % s)
            elif n == 4:
                out.append("{ %s int64_t p = (int64_t)(int32_t)rax * (int32_t)s; rax = (uint32_t)p; "
                           "rdx = (uint32_t)(p >> 32); cf = of = p != (int32_t)p; }" % s)
            else:
                out.append("{ %s __int128 p = (__int128)(int64_t)rax * (int64_t)s; rax = (uint64_t)p; "
                           "rdx = (uint64_t)(p >> 64); cf = of = p != (int64_t)p; }" % s)
            return
        src1 = ops[1] if len(ops) == 3 else ops[0]
        src2 = ops[2] if len(ops) == 3 else ops[1]
        a = "(int64_t)(%s)%s" % (STYPE[n], self.rd(i, src1, n))
        b = "(int64_t)(%s)%s" % (STYPE[n], self.rd(i, src2, n))
        out.append("{ __int128 p = (__int128)%s * (__int128)%s; uint64_t t = (uint64_t)p & %s; "
                   "cf = of = p != (__int128)(%s)t; %s %s }"
                   % (a, b, MASK[n], STYPE[n], self.szp(n), self.wr(i, ops[0], "t")))

    def mul(self, i, out):
        ops = i.operands
        n = ops[0].size
        s = "uint64_t s = %s;" % self.rd(i, ops[0])
        if n == 1:
            out.append("{ %s uint64_t p = (uint64_t)(uint8_t)rax * s; rax = (rax & ~0xffffull) | (uint16_t)p; "
                       "cf = of = (p >> 8) != 0; }" % s)
        elif n == 2:
            out.append("{ %s uint64_t p = (uint64_t)(uint16_t)rax * (uint16_t)s; rax = (rax & ~0xffffull) | (uint16_t)p; "
                       "rdx = (rdx & ~0xffffull) | (uint16_t)(p >> 16); cf = of = (p >> 16) != 0; }" % s)
        elif n == 4:
            out.append("{ %s uint64_t p = (uint64_t)(uint32_t)rax * (uint32_t)s; rax = (uint32_t)p; "
                       "rdx = p >> 32; cf = of = rdx != 0; }" % s)
        else:
            out.append("{ %s unsigned __int128 p = (unsigned __int128)rax * s; rax = (uint64_t)p; "
                       "rdx = (uint64_t)(p >> 64); cf = of = rdx != 0; }" % s)

    def div(self, i, out):
        m = i.mnemonic
        ops = i.operands
        n = ops[0].size
        A = i.address
        b = self.rd(i, ops[0])
        de = 'rc_fail("#DE", 0x%xull);' % A
        if m == "div":
            if n == 1:
                s = ("{ uint64_t d = (uint8_t)%s, x = (uint16_t)rax; if (!d || x / d > 0xff) %s "
                     "rax = (rax & ~0xffffull) | ((x %% d) << 8) | (x / d); }" % (b, de))
            elif n == 2:
                s = ("{ uint64_t d = (uint16_t)%s, x = ((uint64_t)(uint16_t)rdx << 16) | (uint16_t)rax; "
                     "if (!d || x / d > 0xffff) %s rax = (rax & ~0xffffull) | (x / d); "
                     "rdx = (rdx & ~0xffffull) | (x %% d); }" % (b, de))
            elif n == 4:
                s = ("{ uint32_t d = (uint32_t)%s; uint64_t x = ((uint64_t)(uint32_t)rdx << 32) | (uint32_t)rax; "
                     "if (!d || (x / d) >> 32) %s rax = (uint32_t)(x / d); rdx = (uint32_t)(x %% d); }" % (b, de))
            else:
                s = ("{ uint64_t d = %s; if (!d) %s if (!rdx) { uint64_t q = rax / d, r = rax %% d; rax = q; rdx = r; } else { "
                     "unsigned __int128 x = ((unsigned __int128)rdx << 64) | rax; if (rdx >= d) %s "
                     "rax = (uint64_t)(x / d); rdx = (uint64_t)(x %% d); } }" % (b, de, de))
        else:
            if n == 1:
                s = ("{ int32_t d = (int8_t)%s, x = (int16_t)rax; if (!d || x / d != (int8_t)(x / d)) %s "
                     "rax = (rax & ~0xffffull) | ((uint64_t)(uint8_t)(x %% d) << 8) | (uint8_t)(x / d); }" % (b, de))
            elif n == 2:
                s = ("{ int32_t d = (int16_t)%s; int64_t x = (int32_t)(((uint32_t)(uint16_t)rdx << 16) | (uint16_t)rax); "
                     "if (!d || x / d != (int16_t)(x / d)) %s rax = (rax & ~0xffffull) | (uint16_t)(x / d); "
                     "rdx = (rdx & ~0xffffull) | (uint16_t)(x %% d); }" % (b, de))
            elif n == 4:
                s = ("{ int32_t d = (int32_t)%s; int64_t x = (int64_t)(((uint64_t)(uint32_t)rdx << 32) | (uint32_t)rax); "
                     "if (!d || (d == -1 && x == INT64_MIN) || (x / d) != (int32_t)(x / d)) %s "
                     "rax = (uint32_t)(int32_t)(x / d); rdx = (uint32_t)(int32_t)(x %% d); }" % (b, de))
            else:
                s = ("{ int64_t d = (int64_t)%s; if (!d) %s "
                     "if (rdx == (uint64_t)((int64_t)rax >> 63) && !(d == -1 && rax == (1ull << 63))) { "
                     "int64_t q = (int64_t)rax / d, r = (int64_t)rax %% d; rax = (uint64_t)q; rdx = (uint64_t)r; } else { "
                     "__int128 x = (__int128)(((unsigned __int128)rdx << 64) | rax); "
                     "if (d == -1) %s __int128 q = x / d; if (q != (int64_t)q) %s "
                     "rax = (uint64_t)(int64_t)q; rdx = (uint64_t)(int64_t)(x %% d); } }" % (b, de, de, de))
        out.append(s)

    # ---------------------------------------------------------------------
    # bit instructions
    # ---------------------------------------------------------------------
    def bittest(self, i, out, base, lock):
        """bt/bts/btr/btc, including the bit-string form of a memory operand
        with a register offset (address + signed offset, in operand-size units)."""
        ops = i.operands
        n = ops[0].size
        bits = 8 * n
        ct = CTYPE[n]
        if ops[1].type == x86.X86_OP_IMM:
            off = "(uint64_t)%d" % (ops[1].imm & (bits - 1))
            pre = ""
            addr = self.ea(i, ops[0]) if ops[0].type == x86.X86_OP_MEM else None
        elif ops[0].type == x86.X86_OP_MEM:
            lg = {2: 4, 4: 5, 8: 6}[n]
            pre = "int64_t o = (int64_t)(%s)%s; " % (STYPE[n], self.rd(i, ops[1], n))
            off = "(uint64_t)(o & %d)" % (bits - 1)
            addr = "(%s + (uint64_t)((o >> %d) * %d))" % (self.ea(i, ops[0]), lg, n)
        else:
            off = "(%s & %d)" % (self.rd(i, ops[1], n), bits - 1)
            pre = ""
            addr = None
        if addr is None:     # register destination
            a = self.rd(i, ops[0])
            s = "{ %suint64_t a = %s & %s, bit = %s; cf = (a >> bit) & 1; " % (pre, a, MASK[n], off)
            if base != "bt":
                op = {"bts": "a | (1ull << bit)", "btr": "a & ~(1ull << bit)", "btc": "a ^ (1ull << bit)"}[base]
                s += self.wr(i, ops[0], op) + " "
            out.append(s + "}")
            return
        p = "(%s *)(uintptr_t)%s" % (ct, addr)
        if lock and base != "bt":
            fn = {"bts": "or", "btr": "and", "btc": "xor"}[base]
            v = "(%s)(1ull << bit)" % ct if base != "btr" else "(%s)~(1ull << bit)" % ct
            out.append("{ %suint64_t bit = %s; %s *p = %s; uint64_t o_ = __atomic_fetch_%s(p, %s, __ATOMIC_SEQ_CST); "
                       "cf = (o_ >> bit) & 1; }" % (pre, off, ct, p, fn, v))
            return
        s = "{ %suint64_t bit = %s; %s *p = %s; uint64_t a = RC_LD(%s, p); cf = (a >> bit) & 1; " % (pre, off, ct, p, ct)
        if base != "bt":
            op = {"bts": "a | (1ull << bit)", "btr": "a & ~(1ull << bit)", "btc": "a ^ (1ull << bit)"}[base]
            s += "RC_ST(%s, p, %s); " % (ct, op)
        out.append(s + "}")

    def bitscan(self, i, out):
        m = i.mnemonic
        ops = i.operands
        n = ops[0].size
        v = "(%s & %s)" % (self.rd(i, ops[1]), MASK[n])
        if m == "bsf":
            out.append("{ uint64_t v = %s; zf = v == 0; if (v) { %s } }" % (v, self.wr(i, ops[0], "__builtin_ctzll(v)")))
        elif m == "bsr":
            out.append("{ uint64_t v = %s; zf = v == 0; if (v) { %s } }" % (v, self.wr(i, ops[0], "63 - __builtin_clzll(v)")))
        elif m == "tzcnt":
            out.append("{ uint64_t v = %s; uint64_t t = v ? __builtin_ctzll(v) : %d; cf = v == 0; zf = t == 0; %s }"
                       % (v, 8 * n, self.wr(i, ops[0], "t")))
        elif m == "lzcnt":
            out.append("{ uint64_t v = %s; uint64_t t = v ? __builtin_clzll(v) - %d : %d; cf = v == 0; zf = t == 0; %s }"
                       % (v, 64 - 8 * n, 8 * n, self.wr(i, ops[0], "t")))
        else:
            out.append("{ uint64_t v = %s; cf = 0; of = 0; sf = 0; pf = 0; zf = v == 0; %s }"
                       % (v, self.wr(i, ops[0], "__builtin_popcountll(v)")))

    def xchg(self, i, out):
        ops = i.operands
        if ops[0].type == x86.X86_OP_MEM or ops[1].type == x86.X86_OP_MEM:
            mo, ro = (ops[0], ops[1]) if ops[0].type == x86.X86_OP_MEM else (ops[1], ops[0])
            ct = CTYPE[mo.size]
            out.append("{ uint64_t v = __atomic_exchange_n((%s *)(uintptr_t)%s, (%s)%s, __ATOMIC_SEQ_CST); %s }"
                       % (ct, self.ea(i, mo), ct, self.rd(i, ro), self.wr(i, ro, "v")))
        else:
            out.append("{ uint64_t a = %s, b = %s; %s %s }" % (self.rd(i, ops[0]), self.rd(i, ops[1]),
                                                             self.wr(i, ops[0], "b"), self.wr(i, ops[1], "a")))

    # ---------------------------------------------------------------------
    # lock-prefixed read-modify-write: exact flags from the fetched old value
    # ---------------------------------------------------------------------
    def emit_lock(self, i, out, base):
        ops = i.operands
        n = ops[0].size
        ct, M = CTYPE[n], MASK[n]
        p = "(%s *)(uintptr_t)%s" % (ct, self.ea(i, ops[0]))
        if base in ("bts", "btr", "btc", "bt"):
            self.bittest(i, out, base, True)
        elif base in ("inc", "dec", "add", "sub", "and", "or", "xor"):
            fn = {"inc": "add", "dec": "sub"}.get(base, base)
            b = "1" if base in ("inc", "dec") else self.rd(i, ops[1], n)
            out.append("{ uint64_t b = %s & %s; uint64_t a = __atomic_fetch_%s(%s, (%s)b, __ATOMIC_SEQ_CST); %s }"
                       % (b, M, fn, p, ct, self.alu(base, n)))
        elif base == "xadd":
            out.append("{ uint64_t b = %s & %s; uint64_t a = __atomic_fetch_add(%s, (%s)b, __ATOMIC_SEQ_CST); %s %s }"
                       % (self.rd(i, ops[1]), M, p, ct, self.alu("add", n), self.wr(i, ops[1], "a")))
        elif base == "cmpxchg":
            acc = {1: "al", 2: "ax", 4: "eax", 8: "rax"}[n]
            out.append("{ %s e = (%s)rax; uint64_t a = e; int ok = __atomic_compare_exchange_n(%s, &e, (%s)%s, 0, "
                       "__ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST); uint64_t b = e; %s if (!ok) { %s }%s }"
                       % (ct, ct, p, ct, self.rd(i, ops[1]), self.alu("cmp", n), self.wr_reg(acc, "e"),
                          " else { rax = (uint32_t)rax; }" if n == 4 else ""))
        elif base == "xchg":
            self.xchg(i, out)
        elif base in ("adc", "sbb", "neg", "not"):
            b = "b = %s & %s" % (self.rd(i, ops[1], n), M) if len(ops) > 1 else "b = 0"
            out.append("{ %s *p_ = %s; %s o = __atomic_load_n(p_, __ATOMIC_SEQ_CST); uint64_t %s; "
                       "for (;;) { uint64_t a = o; %s if (__atomic_compare_exchange_n(p_, &o, (%s)t, 0, "
                       "__ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) break; } }"
                       % (ct, p, ct, b, self.alu(base, n), ct))
        else:
            return False
        return True

    # ---------------------------------------------------------------------
    # string instructions (direction flag assumed clear)
    # ---------------------------------------------------------------------
    def string_op(self, i, out, parts):
        ops = i.operands
        base = parts[-1]
        kind, sfx = base[:-1], base[-1]
        if any(_is_xmm(i, o) for o in ops):
            return False
        if kind in ("movs", "cmps") and not all(o.type == x86.X86_OP_MEM for o in ops):
            return False
        n = {"b": 1, "w": 2, "d": 4, "q": 8}[sfx]
        ct, M = CTYPE[n], MASK[n]
        pfx = parts[0] if len(parts) > 1 else ""
        rep = bool(pfx)
        acc = {1: "al", 2: "ax", 4: "eax", 8: "rax"}[n]
        cond = ""
        if kind in ("scas", "cmps") and rep:
            cond = "if (!zf) break;" if pfx in ("rep", "repe", "repz") else "if (zf) break;"
        if kind == "movs":
            if rep:
                out.append("{ uint64_t len = rcx * %d; if ((uint64_t)(rdi - rsi) >= len) { memmove((void *)(uintptr_t)rdi, "
                           "(const void *)(uintptr_t)rsi, len); rsi += len; rdi += len; rcx = 0; } else while (rcx) { "
                           "RC_ST(%s, rdi, RC_LD(%s, rsi)); rsi += %d; rdi += %d; rcx--; } }" % (n, ct, ct, n, n))
            else:
                out.append("{ RC_ST(%s, rdi, RC_LD(%s, rsi)); rsi += %d; rdi += %d; }" % (ct, ct, n, n))
        elif kind == "stos":
            if rep:
                out.append("{ %s v = (%s)rax; if (%s) { uint64_t len = rcx * %d; memset((void *)(uintptr_t)rdi, "
                           "(int)(uint8_t)v, len); rdi += len; rcx = 0; } else while (rcx) { RC_ST(%s, rdi, v); "
                           "rdi += %d; rcx--; } }" % (ct, ct, "1" if n == 1 else "v == 0", n, ct, n))
            else:
                out.append("{ RC_ST(%s, rdi, (%s)rax); rdi += %d; }" % (ct, ct, n))
        elif kind == "lods":
            body = "%s rsi += %d;" % (self.wr_reg(acc, "RC_LD(%s, rsi)" % ct), n)
            out.append("while (rcx) { %s rcx--; }" % body if rep else "{ %s }" % body)
        elif kind == "scas":
            body = ("{ uint64_t a = (%s)rax, b = RC_LD(%s, rdi); %s rdi += %d; }" % (ct, ct, self.alu("cmp", n), n))
            out.append("while (rcx) { %s rcx--; %s }" % (body, cond) if rep else body)
        else:
            body = ("{ uint64_t a = RC_LD(%s, rsi), b = RC_LD(%s, rdi); %s rsi += %d; rdi += %d; }" % (ct, ct, self.alu("cmp", n), n, n))
            out.append("while (rcx) { %s rcx--; %s }" % (body, cond) if rep else body)
        return True
