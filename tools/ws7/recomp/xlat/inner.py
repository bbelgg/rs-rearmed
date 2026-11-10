"""Edits and mid points (runtime/rc_mid.h): what the target's tables put inside a function.

  * An edit (tools/targets/<id>/edits.map: GROUP ADDRESS LEN:CHECK EDIT) is a range of whole instructions the
    function has twice: as the game has them, and as the edit makes them, decoded from the range's address. The
    table has no bytes of the game: the old ones are read from the image here and must match its check (the
    SHA-256's first 8 hex digits); the edit is patches inside the range (the same length) or instructions that
    replace it (any length; flow goes on at the range's end; a branch in them may not land inside the range, and a
    call in them pushes the address after it in the new code). Which runs is the group's switch, rc_edit_on[group]
    (a mod's rc_edit_enable, fixed before the game runs). When the range is one instruction both ways and the two
    translate to the same C but for numbers (a displacement, an immediate: most edits), it is that one statement
    with each number that differs chosen by the switch; else the two are branches of their own, both going on at
    the end of the range.
  * A mid point (mids.map: NAME ADDRESS (VALUES) [-> CONT=ADDRESS, ...]) is a call before the instruction at its
    address, when a mod has hooked it (rc_mid_on[point]): the registers go to rc_ctx, the flags are packed, and
    rc_mid_call's answer picks a continuation (a label of this function) or none.

Both are checked as the function is translated: the old bytes must match the check, a range must start and end on
an instruction with no jump into it, the new bytes must decode to whole instructions, a continuation must be an
instruction of the function. Anything else is a TableError, which stops the build (build/gen_image*.py), and so is
an entry that no translated function reached (the generators compare inner_done with the tables).
"""
import hashlib
import re

from capstone import x86

from .common import TableError, Unsupported

LIT = re.compile(r"(?<![\w.])(0x[0-9a-f]+(?:ull|ll|u)?|\d+(?:ull|ll|u)?)(?![\w.])")


def merge_literals(old, new, group):
    """One statement list for both variants when they differ in number literals only, else None."""
    if len(old) != len(new):
        return None
    out = []
    for so, sn in zip(old, new):
        po, pn = LIT.split(so), LIT.split(sn)
        if len(po) != len(pn):
            return None
        s = []
        for k, (a, b) in enumerate(zip(po, pn)):
            if k % 2 == 0:
                if a != b:
                    return None
                s.append(a)
            else:
                s.append(a if a == b else "(rc_edit_on[%d] ? %s : %s)" % (group, b, a))
        out.append("".join(s))
    return out


class InnerMixin:
    edits = {}   # addr -> (group index, length, check, patches, where): tools/targets/targets.py parse_edit
    mids = {}    # addr -> (point index, [continuation addrs], name)
    edit_parent = {}   # a split edit's parts -> the table's address (set_inner)

    def set_inner(self, edits, mids):
        """The tables' entries. A patch over several instructions that keeps every instruction where it was (a
        byte here and there in each) becomes one edit per changed instruction: a jump to any of them, from inside
        the range or outside it, is then a jump to an instruction, as in the game."""
        self.edits, self.mids, self.edit_parent = {}, dict(mids or {}), {}
        for a, e in sorted((edits or {}).items()):
            self.edits[a] = e
            if e[3] and e[3][0][0] is None:
                continue
            try:
                orig, eds, _ = self.edit_insns(a)
            except (TableError, Unsupported):
                continue      # reported where a function reaches it
            if len(orig) < 2 or [(i.address, i.size) for i in orig] != [(i.address, i.size) for i in eds]:
                continue
            del self.edits[a]
            for i, j in zip(orig, eds):
                if bytes(i.bytes) != bytes(j.bytes):
                    self.edits[i.address] = (e[0], i.size, hashlib.sha256(bytes(i.bytes)).hexdigest()[:8],
                                             [(None, bytes(j.bytes))], "%s (+%d)" % (e[4], i.address - a))
                    self.edit_parent[i.address] = a

    # --- what a front end provides -------------------------------------------------------------
    def image_bytes(self, a, n):
        o = self.text_fo + (a - self.text_lo)
        if not (self.text_lo <= a and a + n <= self.text_hi):
            raise TableError("0x%x: not in __text" % a)
        return bytes(self.img.data[o:o + n])

    def decode_at(self, code, a):
        return list(self.md.disasm(code, a))

    # --- edits --------------------------------------------------------------------------------
    def edit_insns(self, a):
        """(original instructions, edited instructions, end) of the edit at a, checked."""
        g, n, check, patches, where = self.edits[a]
        old = self.image_bytes(a, n)
        if hashlib.sha256(old).hexdigest()[:8] != check:
            raise TableError("%s: the %d bytes at 0x%x are not the ones the table was made for (check %s, these %s)"
                             % (where, n, a, check, hashlib.sha256(old).hexdigest()[:8]))
        orig, p = [], a
        while p < a + n:
            try:
                i = self.insn(p)
            except Unsupported as e:
                raise TableError("%s: %s" % (where, e))
            orig.append(i)
            p += i.size
        if p != a + n:
            raise TableError("%s: 0x%x..0x%x doesn't end on an instruction" % (where, a, a + n))
        if patches and patches[0][0] is None:
            new = patches[0][1]
        else:
            new = bytearray(old)
            for off, b in patches:
                new[off:off + len(b)] = b
            new = bytes(new)
        eds = self.decode_at(new, a)
        if sum(i.size for i in eds) != len(new):
            raise TableError("%s: the new bytes don't decode to whole instructions" % where)
        return orig, eds, a + n

    def ends_flow(self, i):
        from .common import NORETURN
        return i.mnemonic in ("jmp",) + self.STOP or (
            i.mnemonic == "call" and i.operands and i.operands[0].type == x86.X86_OP_IMM
            and getattr(self, "stubs", {}).get(i.operands[0].imm, (0, ""))[1] in NORETURN)

    def inner_discover(self, a, entry, labels, work):
        """Discovery at an instruction that has a mid point or starts an edit: their targets are the function's too."""
        if a in self.mids:
            for t in self.mids[a][1]:
                if t != entry and t in self.fset:
                    raise TableError("mid point %s: continuation 0x%x is another function" % (self.mids[a][2], t))
                labels.add(t)
                work.append(t)
        if a in self.edits:
            _, eds, end = self.edit_insns(a)
            for i in eds:
                if (x86.X86_GRP_JUMP in i.groups or i.mnemonic in self.LOOPS) and i.operands \
                        and i.operands[0].type == x86.X86_OP_IMM:
                    t = i.operands[0].imm & ((1 << 64) - 1)
                    if a < t < end:
                        raise TableError("%s: a branch to 0x%x, inside the range" % (self.edits[a][4], t))
                    if t == entry or t not in self.fset:
                        labels.add(t)
                        work.append(t)
            labels.add(end)
            if not self.ends_flow(eds[-1]):
                work.append(end)

    def emit_all(self, insns, what):
        out = []
        for i in insns:
            stmts = []
            try:
                self.emit(i, stmts)
            except Unsupported as e:
                raise TableError("%s: %s" % (what, e))
            for s in stmts:
                out.append("  %s  // %x: %s %s" % (s, i.address, i.mnemonic, i.op_str) if s is stmts[0] else "  " + s)
            if not stmts:
                out.append("  ;  // %x: %s %s" % (i.address, i.mnemonic, i.op_str))
        return out

    def edit_block(self, a, labels):
        """The C of the edit at a, and the address after its range."""
        g, where = self.edits[a][0], self.edits[a][4]
        orig, eds, end = self.edit_insns(a)
        inside = sorted(t for t in labels if a < t < end)
        if inside:
            raise TableError("%s: a jump goes into the range 0x%x..0x%x (to 0x%x)" % (where, a, end, inside[0]))
        if any(a < t < end for t in self.mids):
            raise TableError("%s: a mid point is inside the range" % where)
        if len(orig) == 1 and len(eds) == 1:
            so, sn = [], []
            for i, out in ((orig[0], so), (eds[0], sn)):
                try:
                    self.emit(i, out)
                except Unsupported as e:
                    raise TableError("%s: %s" % (where, e))
            merged = merge_literals(so, sn, g)
            if merged is not None:
                note = "  // %x: %s %s, edit %d: %s %s" % (a, orig[0].mnemonic, orig[0].op_str, g, eds[0].mnemonic,
                                                           eds[0].op_str)
                lines = ["  %s%s" % (s, note if k == 0 else "") for k, s in enumerate(merged)] or ["  ;" + note]
                if not self.ends_flow(orig[0]) and not self.ends_flow(eds[0]):
                    lines.append("  goto L_%x;" % end)
                return lines, end
        lines = ["  if (__builtin_expect(rc_edit_on[%d], 0)) goto L_e%x;  // %x: edit %d (%s)" % (g, a, a, g, where)]
        lines += self.emit_all(orig, where)
        if not self.ends_flow(orig[-1]):
            lines.append("  goto L_%x;" % end)
        lines.append("L_e%x:" % a)
        lines += self.emit_all(eds, where)
        if not self.ends_flow(eds[-1]):
            lines.append("  goto L_%x;" % end)
        return lines, end

    # --- mid points ---------------------------------------------------------------------------
    def mid_block(self, a, seen):
        k, conts, name = self.mids[a]
        for t in conts:
            # discovery decodes from a continuation as from any target: one inside another instruction is decoded
            # too, and is no instruction of the function
            if t not in seen or any(b < t < b + i.size for b, i in seen.items() if t - 16 < b < t):
                raise TableError("mid point %s: continuation 0x%x is not an instruction of the function" % (name, t))
        go = "switch (go_) { %s default: break; }" % " ".join(
            "case %d: goto L_%x;" % (n + 1, t) for n, t in enumerate(conts)) if conts else "(void)go_;"
        return ("  if (__builtin_expect(rc_mid_on[%d], 0)) { uint32_t fl_ = (uint32_t)rc_flags_pack(cf, pf, zf, sf, of); "
                "SPILL(); int go_ = rc_mid_call(%d, c, &fl_); FILL(); cf = fl_ & 1; pf = (fl_ >> 2) & 1; "
                "zf = (fl_ >> 6) & 1; sf = (fl_ >> 7) & 1; of = (fl_ >> 11) & 1; %s }  // %x: mid point %s"
                % (k, k, go, a, name))
