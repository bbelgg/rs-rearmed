"""Jump-table resolution.

The resolver works on the whole function, not on the linear path that
discovery happens to be on: it runs its own recursive descent from the
function entry (same rules as Translator.discover) to build a CFG with
per-instruction predecessors, then resolves each indirect `jmp` by walking
backwards over the predecessors:

  * the jump target is chased through register copies to its table load:
      movsxd X, [B + I*4]; add X, B; jmp X      (PIC, 32-bit offsets)
      mov X, [T + I*8]; jmp X  /  jmp [T + I*8]  (absolute 8-byte entries)
    where B / T come from `lea B,[rip+T]`, `mov B, imm` or an absolute
    displacement, found on any path back (so hoisted bases work);
  * the index may come through a byte/word table
      movzx I2, byte|word [T1 + I]; movsxd X, [B + I2*4]
    (two levels: the second table is as long as max(T1[0..bound]) + 1);
  * the bound comes from the guarding conditional branch: a `cmp R, imm` (or
    `cmp R, R2` with R2 a constant) feeding ja/jae/jbe/jb (or the signed
    forms) on the edge that leads to the jump, with R tracked back to the
    table index through mov/movzx/movsxd/add/sub/lea/inc/dec, or an `and`
    mask;
  * resolved jumps add their case bodies to the CFG, which can expose the
    guards of further (nested) jumps, so resolution iterates to a fixpoint.

`jump_table(path, jmp, entry)` keeps core.py's interface; `self.jt_info[addr]`
holds a description of every resolved jump (pattern, bound, table).
"""
import bisect
import struct

from capstone import x86

from .common import CTYPE, MASK, REGS, STYPE, COND, Unsupported, NORETURN  # noqa: F401

M64 = 0xffffffffffffffff
CALLER_SAVED = ("rax", "rcx", "rdx", "rsi", "rdi", "r8", "r9", "r10", "r11")
# jcc -> (edge on which the compared value is <= limit, bound adjust)
_IN_TAKEN = {"jbe": 0, "jb": -1, "jle": 0, "jl": -1}     # taken edge is in range
_IN_FALL = {"ja": 0, "jae": -1, "jg": 0, "jge": -1}      # fall-through edge is in range
_NSTEPS = 4000


class _Fn:
    def __init__(self, entry, fend):
        self.entry, self.fend = entry, fend
        self.insns = {}
        self.preds = {}          # addr -> [(src addr, 'fall'|'taken'|'tab')]
        self.pending = []        # unresolved indirect jmp addresses
        self.done = {}           # jmp addr -> targets
        self.info = {}
        self.wcache = {}

    def edge(self, src, dst, kind):
        p = self.preds.setdefault(dst, [])
        if (src, kind) not in p:
            p.append((src, kind))


class JumpTableMixin:
    M32 = False    # xlat/x86_32.py: also the 32-bit forms
    LOOPS = ()
    # --- public entry -------------------------------------------------------
    def jump_table(self, path, jmp, entry):
        """Targets of the switch-style indirect `jmp` at jmp.address, or None."""
        c = getattr(self, "_jt_cache", None)
        if c is None or c.entry != entry:
            c = self._jt_cache = self._jt_analyse(entry)
            self.jt_info = c.info
        r = c.done.get(jmp.address)
        return list(r) if r else None

    # --- CFG construction ---------------------------------------------------
    def _jt_analyse(self, entry):
        k = bisect.bisect_right(self.fsorted, entry)
        fend = self.fsorted[k] if k < len(self.fsorted) else self.text_hi
        fn = _Fn(entry, fend)
        self._jt_descend(fn, [entry])
        while fn.pending:
            progress = False
            for j in list(fn.pending):
                r = self._jt_resolve(fn, fn.insns[j])
                if r is None:
                    continue
                fn.pending.remove(j)
                fn.done[j] = r["targets"]
                fn.info[j] = r
                for t in r["targets"]:
                    fn.edge(j, t, "tab")
                self._jt_descend(fn, list(r["targets"]))
                progress = True
            if not progress:
                break
        return fn

    def _jt_descend(self, fn, work):
        entry = fn.entry
        while work:
            a = work.pop()
            while a not in fn.insns:
                try:
                    i = self.insn(a)
                except Unsupported:
                    break
                fn.insns[a] = i
                m = i.mnemonic
                nxt = a + i.size
                if m in self.STOP:
                    break
                if x86.X86_GRP_JUMP in i.groups or m in self.LOOPS:
                    op = i.operands[0]
                    if op.type != x86.X86_OP_IMM:
                        if m == "jmp":
                            fn.pending.append(a)
                        break
                    t = op.imm
                    if t != entry and t in self.fset:        # tail call
                        if m == "jmp":
                            break
                        fn.edge(a, nxt, "fall")
                        a = nxt
                        continue
                    fn.edge(a, t, "taken")
                    work.append(t)
                    if m == "jmp":
                        break
                    fn.edge(a, nxt, "fall")
                    a = nxt
                    continue
                if x86.X86_GRP_CALL in i.groups and i.operands[0].type == x86.X86_OP_IMM:
                    t = i.operands[0].imm
                    if t in self.stubs and self.stubs[t][1] in NORETURN:
                        break
                fn.edge(a, nxt, "fall")
                a = nxt

    # --- register helpers ---------------------------------------------------
    def _jt_canon(self, i, r):
        n = REGS.get(i.reg_name(r))
        return n[0] if n else None

    def _jt_writes(self, fn, i):
        """Canonical GPRs written by i (calls clobber the caller-saved set)."""
        w = fn.wcache.get(i.address)
        if w is None:
            try:
                _, wr = i.regs_access()
            except Exception:
                wr = ()
            w = set()
            for r in wr:
                c = self._jt_canon(i, r)
                if c:
                    w.add(c)
            if x86.X86_GRP_CALL in i.groups:
                w.update(CALLER_SAVED)
            w = fn.wcache[i.address] = (frozenset(w), any(i.reg_name(r) in ("eflags", "rflags") for r in wr))
        return w

    def _jt_defs(self, fn, at, reg):
        """Instructions that may define canonical `reg` as seen just before the
        instruction at `at` (None = live-in at the function entry)."""
        out, seen, stack, steps = [], set(), [at], 0
        while stack and steps < _NSTEPS:
            cur = stack.pop()
            ps = fn.preds.get(cur, ())
            if not ps and cur == fn.entry:
                out.append(None)
            for p, _ in ps:
                if p in seen:
                    continue
                seen.add(p)
                steps += 1
                i = fn.insns[p]
                if reg in self._jt_writes(fn, i)[0]:
                    out.append(i)
                else:
                    stack.append(p)
        if steps >= _NSTEPS:
            return None
        return out

    def _jt_const(self, fn, at, reg, depth=0):
        if depth > 4:
            return None
        """Constant value of canonical `reg` before `at` (lea rip / mov imm)."""
        ds = self._jt_defs(fn, at, reg)
        if not ds:
            return None
        v = None
        for d in ds:
            if d is None:
                return None
            c = None
            if d.mnemonic == "lea" and d.operands[1].type == x86.X86_OP_MEM:
                m = d.operands[1].mem
                if m.base == x86.X86_REG_RIP and not m.index:
                    c = (d.address + d.size + m.disp) & M64
            elif d.mnemonic in ("mov", "movabs") and d.operands[1].type == x86.X86_OP_IMM and \
                    d.operands[0].type == x86.X86_OP_REG:
                c = d.operands[1].imm & (M64 if d.operands[0].size == 8 else 0xffffffff)
            elif d.mnemonic == "xor" and d.operands[0].type == d.operands[1].type == x86.X86_OP_REG \
                    and d.operands[0].reg == d.operands[1].reg:
                c = 0
            elif d.mnemonic == "mov" and d.operands[1].type == x86.X86_OP_REG and d.operands[0].type == x86.X86_OP_REG:
                c = self._jt_const(fn, d.address, self._jt_canon(d, d.operands[1].reg), depth + 1)
            if c is None or (v is not None and v != c):
                return None
            v = c
        return v

    def _jt_membase(self, fn, i, m):
        """(constant base, address) of a [base + index*s + disp] operand's table."""
        if m.segment:
            return None
        if m.base == 0:
            return m.disp & M64, m.disp & M64
        if m.base == x86.X86_REG_RIP:
            a = (i.address + i.size + m.disp) & M64
            return a, a
        c = self._jt_canon(i, m.base)
        if c is None:
            return None
        b = self._jt_const(fn, i.address, c)
        if b is None:
            return None
        return b, (b + m.disp) & M64

    def _jt_read(self, addr, size, signed=False):
        o = self.img.foff(addr)
        if o is None:
            return None
        return struct.unpack_from({1: "<B", 2: "<H", 4: "<i" if signed else "<I", 8: "<Q"}[size], self.img.data, o)[0]

    # --- table-load recognition --------------------------------------------
    def _jt_chase(self, fn, at, reg, depth=0):
        """Table load feeding canonical `reg` before `at`: dict(kind, tab, base, idx, load)."""
        if depth > 6:
            return None
        ds = self._jt_defs(fn, at, reg)
        if not ds:
            return None
        res = None
        for d in ds:
            if d is None:
                return None
            r = self._jt_load_of(fn, d, reg, depth)
            if r is None or (res is not None and (res["tab"], res["kind"]) != (r["tab"], r["kind"])):
                return None
            res = r
        return res

    def _jt_load_of(self, fn, d, reg, depth):
        m, ops = d.mnemonic, d.operands
        if len(ops) < 2:
            return None
        if m == "mov" and ops[1].type == x86.X86_OP_REG and ops[0].type == x86.X86_OP_REG:
            return self._jt_chase(fn, d.address, self._jt_canon(d, ops[1].reg), depth + 1)
        if m == "add" and ops[0].type == x86.X86_OP_REG and ops[1].type == x86.X86_OP_REG:
            a, b = self._jt_canon(d, ops[0].reg), self._jt_canon(d, ops[1].reg)
            for x, y in ((a, b), (b, a)):
                r = self._jt_chase(fn, d.address, x, depth + 1)
                if r and r["kind"] == "rel4":
                    bv = self._jt_const(fn, d.address, y)
                    if bv is not None:
                        r = dict(r, base=bv)
                        return r
            return None
        if m == "movsxd" and ops[1].type == x86.X86_OP_MEM and ops[1].mem.scale == 4 and ops[1].mem.index:
            mb = self._jt_membase(fn, d, ops[1].mem)
            if mb:
                return dict(kind="rel4", tab=mb[1], base=mb[0], idx=self._jt_canon(d, ops[1].mem.index),
                            load=d.address, esize=4)
        if m == "mov" and ops[0].type == x86.X86_OP_REG and ops[0].size == 8 and ops[1].type == x86.X86_OP_MEM \
                and ops[1].mem.scale == 8 and ops[1].mem.index:
            mb = self._jt_membase(fn, d, ops[1].mem)
            if mb:
                return dict(kind="abs8", tab=mb[1], base=0, idx=self._jt_canon(d, ops[1].mem.index),
                            load=d.address, esize=8)
        return None

    def _jt_source(self, fn, jmp):
        op = jmp.operands[0]
        if op.type == x86.X86_OP_MEM:
            m = op.mem
            if m.scale == 8 and m.index:
                mb = self._jt_membase(fn, jmp, m)
                if mb:
                    return dict(kind="abs8", tab=mb[1], base=0, idx=self._jt_canon(jmp, m.index),
                                load=jmp.address, esize=8)
            return None
        return self._jt_chase(fn, jmp.address, self._jt_canon(jmp, op.reg))

    def _jt_slot(self, i, op):
        """Key of a [rbp/rsp + disp] stack slot operand, else None."""
        if op.type != x86.X86_OP_MEM or op.mem.index or op.mem.segment or not op.mem.base:
            return None
        b = self._jt_canon(i, op.mem.base)
        return ("m", b, op.mem.disp) if b in ("rbp", "rsp") else None

    # --- bound ---------------------------------------------------------------
    def _jt_bound(self, fn, ld):
        """Walk back from the table load along every path, tracking the index
        value; return (max_index, level1 or None, how) or None.  level1 is
        (tab1, esize) when the index went through a byte/word lookup table.
        Aliases are (reg, off, mask): index == reg + off (after `& mask`);
        facts are (reg, limit, mask): reg (& mask) <= limit, taken from a
        guarding compare whose register is not (yet) known to be the index."""
        start = (ld["load"], frozenset([(ld["idx"], 0, None)]), None, None, frozenset())
        stack, seen, found, steps = [start], set(), [], 0
        while stack and steps < _NSTEPS:
            cur, al, cond, lvl, facts = stack.pop()
            for p, kind in fn.preds.get(cur, ()):
                steps += 1
                i = fn.insns[p]
                st = self._jt_step(fn, i, kind, al, cond, lvl, facts, found)
                if st is None:
                    continue
                key = (p,) + st
                if key in seen:
                    continue
                seen.add(key)
                stack.append(key)
        strong = [f for f in found if f[2] == "cmp"]
        found = strong or found
        if not found:
            return None
        return max(found, key=lambda f: f[0])

    def _jt_step(self, fn, i, kind, al, cond, lvl, facts, found):
        """Transfer one instruction backwards. Returns the new (aliases, cond, lvl,
        facts) or None when the path ends (bound found, or index untrackable)."""
        m, ops = i.mnemonic, i.operands
        # conditional branch edge: remember the condition until its flag setter
        if x86.X86_GRP_JUMP in i.groups and m != "jmp" and ops[0].type == x86.X86_OP_IMM:
            if cond is None:
                if (kind == "taken" and m in _IN_TAKEN) or (kind == "fall" and m in _IN_FALL):
                    cond = _IN_TAKEN.get(m, _IN_FALL.get(m))
            return al, cond, lvl, facts
        wr, sets_flags = self._jt_writes(fn, i)
        dslot = self._jt_slot(i, ops[0]) if ops and m not in ("cmp", "test", "push") else None
        if dslot:
            wr = wr | {dslot}                       # store to a stack slot
        if cond is not None and sets_flags:
            if m == "cmp" and ops[0].type == x86.X86_OP_REG:
                r = self._jt_canon(i, ops[0].reg)
                lim = None
                if ops[1].type == x86.X86_OP_IMM:
                    lim = ops[1].imm & ((1 << (8 * ops[0].size)) - 1)
                elif ops[1].type == x86.X86_OP_REG:
                    lim = self._jt_const(fn, i.address, self._jt_canon(i, ops[1].reg))
                if lim is not None and 0 <= lim < 70000:
                    for reg, off, mask in al:
                        if reg == r and mask is None:
                            found.append((lim + cond + off, lvl, "cmp"))
                            return None
                    # compared value is not (yet) the index: it may be its source
                    facts = facts | {(r, lim + cond, None)}
            cond = None
        facts_in = facts
        facts = frozenset(f for f in facts if f[0] not in wr)
        if not (wr & {a[0] for a in al}) and facts == facts_in:
            return al, cond, lvl, facts
        # facts follow the value through copies and `and`
        if ops and len(ops) >= 2 and (dslot or ops[0].type == x86.X86_OP_REG) and ops[0].size >= 4:
            d = dslot or self._jt_canon(i, ops[0].reg)
            mv = [f for f in facts_in if f[0] == d]
            if m in ("mov", "movsxd", "movzx") and ops[1].type == x86.X86_OP_REG:
                facts = facts | {(self._jt_canon(i, ops[1].reg), f[1], f[2]) for f in mv}
            elif m == "mov" and ops[1].type == x86.X86_OP_MEM and self._jt_slot(i, ops[1]):
                facts = facts | {(self._jt_slot(i, ops[1]), f[1], f[2]) for f in mv}
            elif m == "and" and ops[1].type == x86.X86_OP_IMM and ops[1].imm >= 0:
                facts = facts | {(d, f[1], ops[1].imm) for f in mv if f[2] is None}
        new = set()
        for reg, off, mask in al:
            if reg not in wr:
                new.add((reg, off, mask))
                continue
            if x86.X86_GRP_CALL in i.groups:
                continue
            dst = ops[0] if ops else None
            if isinstance(reg, tuple):                  # stack slot alias
                if dslot == reg and m == "mov" and ops[1].type == x86.X86_OP_REG and dst.size >= 4:
                    new.add((self._jt_canon(i, ops[1].reg), off, mask))
                continue
            isdst = dst is not None and dst.type == x86.X86_OP_REG and self._jt_canon(i, dst.reg) == reg
            if m == "cdqe":
                new.add((reg, off, mask))
            elif not isdst or dst.size < 4:
                continue                                # untrackable write
            elif m in ("inc", "dec"):
                if mask is None:
                    new.add((reg, off + (1 if m == "inc" else -1), mask))
            elif len(ops) < 2:
                continue
            elif m in ("mov", "movsxd", "movzx") and ops[1].type == x86.X86_OP_REG:
                if m != "mov" or ops[1].size >= dst.size or dst.size == 4:
                    new.add((self._jt_canon(i, ops[1].reg), off, mask))
            elif m == "mov" and ops[1].type == x86.X86_OP_MEM and self._jt_slot(i, ops[1]) and dst.size >= 4:
                new.add((self._jt_slot(i, ops[1]), off, mask))
            elif mask is not None:
                continue
            elif m == "add" and ops[1].type == x86.X86_OP_IMM:
                new.add((reg, off + ops[1].imm, mask))
            elif m == "sub" and ops[1].type == x86.X86_OP_IMM:
                new.add((reg, off - ops[1].imm, mask))
            elif m == "lea":
                mm = ops[1].mem
                if mm.base and mm.base != x86.X86_REG_RIP and not mm.index and not mm.segment:
                    new.add((self._jt_canon(i, mm.base), off + mm.disp, mask))
            elif m == "and" and ops[1].type == x86.X86_OP_IMM and off == 0 and 0 <= ops[1].imm < 70000:
                found.append((ops[1].imm, lvl, "and"))
                new.add((reg, 0, ops[1].imm))           # keep going: a tighter compare may exist
            elif m == "movzx" and ops[1].type == x86.X86_OP_MEM and ops[1].mem.scale == 1 and \
                    ops[1].mem.index and off == 0 and lvl is None and ops[1].size in (1, 2):
                mb = self._jt_membase(fn, i, ops[1].mem)
                if mb:
                    new.add((self._jt_canon(i, ops[1].mem.index), 0, None))
                    lvl = (mb[1], ops[1].size)
            elif self.M32 and m == "movzx" and ops[1].type == x86.X86_OP_MEM and ops[1].mem.base and \
                    not ops[1].mem.index and not ops[1].mem.segment and off == 0 and lvl is None and \
                    ops[1].size in (1, 2):    # movzx I2, byte [I + T1]: the absolute byte table of MSVC
                new.add((self._jt_canon(i, ops[1].mem.base), 0, None))
                lvl = (ops[1].mem.disp & 0xffffffff, ops[1].size)
        if not new:
            return None
        fd = [(f[0], f[1], f[2]) for f in facts]
        for reg, off, mask in new:
            for fr, fl, fm in fd:
                if fr == reg and fm == mask:
                    found.append((fl + off, lvl, "cmp"))
                    return None
        return frozenset(new), cond, lvl, facts

    # --- full resolution -----------------------------------------------------
    def _jt_resolve(self, fn, jmp):
        src = self._jt_source(fn, jmp)
        if src is None or not src["idx"]:
            return None
        b = self._jt_bound(fn, src)
        lo, hi = fn.entry, fn.fend
        tab, esz = src["tab"], src["esize"]
        pattern = src["kind"]
        lvl = None
        count = None
        if b is not None:
            mx, lvl, how = b
            if mx < 0:
                return None
            if lvl is not None:
                t1, e1 = lvl
                vals = [self._jt_read(t1 + e1 * n, e1) for n in range(mx + 1)]
                if None in vals:
                    return None
                count = max(vals) + 1
                pattern += "+idx%d" % (8 * e1)
            else:
                count = mx + 1
            if count > 70000:
                return None
        targets = []
        n = 0
        while count is None and n < 4096 or count is not None and n < count:
            v = self._jt_read(tab + esz * n, esz, signed=True) if esz == 4 else self._jt_read(tab + esz * n, 8)
            if v is None:
                if count is not None:
                    return None
                break
            t = (src["base"] + v) & M64
            if not (lo <= t < hi):
                if count is not None:
                    return None
                break
            targets.append(t)
            n += 1
        if not targets:
            return None
        return dict(targets=targets, pattern=pattern, bound=None if count is None else count - 1,
                    how=None if b is None else b[2], tab=tab, count=len(targets), level1=lvl)
