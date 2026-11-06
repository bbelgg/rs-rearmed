#!/usr/bin/env python3
"""32-bit x86 (MSVC, PE) front end: the mode switch of the translator.

Translator32 is core.Translator with the 32-bit choices; every instruction emitter of
emit_int / emit_sse / jumptab is shared, only what depends on the mode is overridden here:

  * decoding: Capstone in CS_MODE_32, from a PE image (xlat/pe.py);
  * memory: a guest address is 32 bits and memory is the 4 GB window, so an operand's host
    address is RC_M(guest) = rc_mem + (uint32_t)guest (runtime/rc32_gen.h). `lea` and every
    address computation wrap at 32 bits; FS:-relative operands add the thread's TEB (c->fs);
  * the stack: 4-byte slots, return addresses are guest addresses, `ret N` removes the
    callee-cleaned arguments of stdcall/thiscall/fastcall functions (the convention needs no
    other handling inside translated code: the caller pushes, the callee pops, as on x86);
  * calls through the IAT (`call [slot]`, `jmp [slot]`) become the import's thunk I_<slot>
    (arguments at esp, no return address, runtime/rc32.h; thunk_prefix names it for an image
    that shares a process with others); any other indirect call or jump goes through
    rc32_icall;
  * import calls a packer replaced (VMProtect: `call stub` plus a junk byte, `push reg; call
    stub`, the stub computing the API address) are the original `call/jmp [slot]` again when
    a wrapper table lists them (read_wrappers, set_wrappers): decoding resumes after the
    original instruction, so the junk byte is never decoded; a stub that also pushes the call's
    arguments (VMProtect moved `push reg` / `push imm` / `lea r, [ebp-x]; push r` into it) has
    them listed in the row's pushes column, emitted before the call; a register the stub leaves
    holding the import's address (the `mov reg, [imp]; call reg` it replaced; the code calls reg
    again later) is in the sets column and loaded from the slot before the call; a stub that
    stands for `mov reg, [imp]` (kind load, reg in pushed_reg) is that instruction again;
  * the direction flag is modelled (a local `df`: MSVC's memmove copies backwards with std);
  * x87 and MMX (emit_x87.py); pushad/popad, pushfd/popfd, enter, loop/jecxz, rdtsc, AES-NI;
  * functions with resume points (addresses in their body that other code points to: SEH
    scope-table handlers, C++ catch continuations, the return of a setjmp call; the CRT gets
    there with an indirect jump) register a host frame at entry, which rc32_icall longjmps to
    when asked to jump to one of them (rc32_seh.c). The CRT's own setjmp and longjmp run: a setjmp
    call keeps its real return address (setjmp_slot), so what longjmp jumps to is that resume point.

Function discovery (PE has no function-starts table) is find_functions(); its data_code (code
found in non-executable sections) must be handed to every Translator32 that translates
(set_data_code), as build/gen_image_pe.py does.

Usage (from tools/ws7/recomp): python3 -m xlat.x86_32 IMAGE.dll OUT.c [--wrappers TSV] [ADDR ...]
"""
import bisect
import collections
import os
import re
import struct
import sys

import capstone
from capstone import x86

from . import core
from .common import COND, CTYPE, MASK, NOPS, REGS, STYPE, Unsupported  # noqa: F401
from .emit_x87 import X87Mixin
from .pe import SCN_WRITE, Image

M32 = 0xffffffff
JCC = {"j" + c for c in COND}
# imports that do not return
NORETURN32 = {"kernel32.dll!ExitProcess", "kernel32.dll!ExitThread", "kernel32.dll!FatalExit",
              "kernel32.dll!FatalAppExitA", "kernel32.dll!FatalAppExitW", "msvcrt.dll!exit",
              "msvcrt.dll!_exit", "msvcrt.dll!abort", "kernel32.dll!RaiseFailFastException",
              "msvcr100.dll!_CxxThrowException", "msvcr90.dll!_CxxThrowException",
              "msvcrt.dll!_CxxThrowException", "msvcr100.dll!_invalid_parameter_noinfo_noreturn"}
STRING = {"movs", "stos", "lods", "scas", "cmps"}
AES = {"aesenc", "aesenclast", "aesdec", "aesdeclast", "aesimc", "aeskeygenassist"}
SETJMP = {d + "!" + n for d in ("msvcr100.dll", "msvcrt.dll") for n in ("_setjmp3", "_setjmp", "setjmp")}
EH_MAGIC = (0x19930520, 0x19930521, 0x19930522)   # FuncInfo: the versions MSVC writes for 32-bit code
PADDING = {0xcc, 0x90}
DCACHE = 50000   # decoded instructions a translator keeps: 2-3 KB each with Capstone's detail
# instructions compiled 32-bit code does not contain: a gap piece that decodes to one is data
JUNK = {"daa", "das", "aaa", "aas", "aam", "aad", "lds", "les", "lfs", "lgs", "lss", "lgdt", "lidt",
        "sgdt", "sidt", "lldt", "sldt", "ltr", "str", "lmsw", "smsw", "arpl", "bound", "into", "in",
        "out", "insb", "insd", "insw", "outsb", "outsd", "outsw", "hlt", "cli", "sti", "iretd",
        "retf", "ljmp", "lcall", "salc", "int1", "icebp", "wait", "fwait", "clts", "invd", "wbinvd",
        "rsm", "ud0", "ud1", "sysexit", "sysenter", "syscall", "sysret", "xlatb", "lar", "lsl",
        "verr", "verw", "pushal", "popal", "int"}


def reads_flags(a, seen):
    """A conditional jump at `a`, or after the few instructions that leave the flags alone: the CRT's hand-written
    x87 code has `call helper; push edx; wait; fnstcw [esp]; je` as well as `call helper; je`. Compiled code
    sets the flags again before a jump, so it stops at that instruction."""
    for _ in range(4):
        i = seen.get(a)
        if i is None or i.mnemonic not in JCC and i.mnemonic not in FLAG_KEEPING:
            return False
        if i.mnemonic in JCC:
            return True
        a += i.size
    return False


def junk(i, seen):
    """An instruction compiled code never has (JUNK), or a gs: operand. `wait` next to an x87
    instruction is the CRT's hand-written x87 code (`fprem; wait; fnstsw ax; wait; sahf`), not
    data."""
    if i.mnemonic in ("wait", "fwait"):
        n = seen.get(i.address + i.size)
        return not (n is not None and n.mnemonic.startswith("f")) and not any(
            p.address + p.size == i.address and p.mnemonic.startswith("f") for p in seen.values())
    return i.mnemonic in JUNK or "ptr gs:" in i.op_str

PRELUDE = r'''// Generated by tools/ws7/recomp/xlat/x86_32.py from original 32-bit x86 machine code.
// Derived from the input image: build output only, never commit. Do not edit; regenerate.
#include "rc32_gen.h"

'''


class Cover:
    """Which bytes of an image the instructions found so far cover: a set of addresses would take a
    gigabyte for the game."""
    def __init__(self, lo, size):
        self.lo, self.b = lo, bytearray(size)

    def add(self, a, n):
        k = a - self.lo
        self.b[k:k + n] = b"\1" * n

    def __contains__(self, a):
        k = a - self.lo
        return 0 <= k < len(self.b) and self.b[k] != 0

    def __len__(self):
        return sum(self.b)


class Found(collections.namedtuple("Found", "jumps junk flag_calls")):
    """What find_functions keeps of a function: the instructions themselves would be gigabytes. jumps:
    targets of its direct `jmp`s; junk: it has an instruction compiled code never has; flag_calls:
    functions it calls whose flags are read after the call (reads_flags)."""


class _Insn:
    """A Capstone instruction with its mnemonic normalised (`repz ret`, `bnd jmp` -> `ret`, `jmp`)."""
    def __init__(self, i, m):
        self._i, self.mnemonic = i, m

    def __getattr__(self, k):
        return getattr(self._i, k)


class _Wrap(_Insn):
    """A packer's import-call site (a wrappers.tsv row) as the instruction it replaced: a
    Capstone `call/jmp dword ptr [slot]` decoded at the site, spanning the whole site (the
    push and the stub's pop cancel out; the junk byte after the call is skipped). slot is
    None when the table could not name the import."""
    def __init__(self, i, size, row, slot):
        _Insn.__init__(self, i, i.mnemonic)
        self.size, self.wrap, self.slot = size, row, slot
        self.op_str = "%s ; packer stub 0x%x%s%s" % (i.op_str if slot is not None else "<unresolved import>",
                                                    row["stub"], ", push " + row["pushed_reg"]
                                                    if row["pushed_reg"] and row["kind"] != "load" else "",
                                                    ", pushes " + ",".join(row["pushes"]) if row.get("pushes") else "")
        if row.get("sets"):
            self.op_str += ", sets " + ",".join(row["sets"])


REG32 = ("eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi")  # ModRM reg field order
WRAP_COLS = ("call_site", "stub", "kind", "resume", "pushed_reg", "import", "pushes", "sets")


def wrap_push(tok):
    """A pushes-column word as a C expression: a 32-bit register, a register plus or minus a hex
    offset ("ebp-0x33c"), or a hex constant."""
    for op in "+-":
        r, sep, k = tok.partition(op)
        if sep and r in REGS and REGS[r][1] == 4:
            return "(uint32_t)(%s %s 0x%xu)" % (REGS[r][0], op, int(k, 16))
    if tok in REGS and REGS[tok][1] == 4:
        return "(uint32_t)%s" % REGS[tok][0]
    return "0x%xu" % (int(tok, 16) & 0xffffffff)


def read_wrappers(path):
    """The rows of a wrapper table (tab-separated, columns WRAP_COLS; an optional header line,
    blank lines and `#` comments are skipped): dicts with the addresses as ints (resume None
    for jmp), pushed_reg and import as strings ("" when empty), pushes a list (the words the
    stub pushes before calling, in push order: wrap_push), sets a list (32-bit registers that
    hold the import's address after the stub)."""
    rows = []
    for n, line in enumerate(open(path), 1):
        line = line.rstrip("\r\n")
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        f = line.split("\t")
        f += [""] * (len(WRAP_COLS) - len(f))
        if f[0] == WRAP_COLS[0]:
            continue
        try:
            r = {"call_site": int(f[0], 16), "stub": int(f[1], 16), "kind": f[2].strip(),
                 "resume": int(f[3], 16) if f[3].strip() else None, "pushed_reg": f[4].strip(),
                 "import": f[5].strip(), "pushes": [t for t in f[6].strip().split(",") if t],
                 "sets": [t for t in f[7].strip().split(",") if t]}
            for t in r["pushes"]:
                wrap_push(t)
            if any(t not in REGS or REGS[t][1] != 4 or t == "esp" for t in r["sets"]):
                raise ValueError("sets: %r" % r["sets"])
        except ValueError:
            raise ValueError("%s:%d: bad wrapper row %r" % (path, n, line))
        rows.append(r)
    return rows


class Translator32(X87Mixin, core.Translator):
    M32 = True
    STOP = ("ret", "ud2", "hlt", "int3", "retf", "iretd", "ljmp")
    LOOPS = ("loop", "loope", "loopne", "jecxz", "jcxz")
    # A direct call target in a non-executable section is code too (find_functions): the
    # section table of an image rebuilt from a memory dump (the unpacked Learn & Play exe) does
    # not say where its code is.
    code_in_data = True

    def __init__(self, image_path, functions=None, wrappers=None):
        self.img = Image(image_path)
        self.md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
        self.md.detail = True
        self.code = sorted(self.img.code_ranges())
        self.text_lo = self.code[0][0]
        self.text_hi = self.code[-1][1]
        self.stubs = {}
        self.la_lo = 0
        self.imports = self.img.imports
        self.translated = set()
        self.rbp_frame = False
        self.relaxed = False
        self.cur = None
        self.ro = []
        self.resume = {}         # function -> resume points (set by find_functions)
        self.entry = None
        self.has_resume = False
        self.resume_ips = []
        self._dcache = {}
        self.data_code, self._dc_lo = [], []   # (lo, hi): code in non-executable sections (find_functions)
        self._dc_insns = set()
        self._trial = False
        self.thunk_prefix = "I_"  # import thunk names: <prefix><slot> (gen_image_pe.py --module)
        self.flag_callees = set()  # functions whose callers read the flags they leave (find_functions)
        self.set_wrappers(wrappers or ())
        if functions is None:
            functions = self.find_functions()
        self.set_functions(functions)

    def set_functions(self, fs, clear=True):
        self.fset = set(fs)
        self.fsorted = sorted(self.fset)
        if clear:
            self.__dict__.pop("_noret_cache", None)
        self.__dict__.pop("_jt_cache", None)

    def set_data_code(self, ranges):
        """Ranges of non-executable sections that hold code (find_functions' data_code)."""
        self.data_code = sorted(ranges)
        self._dc_lo = [lo for lo, _ in self.data_code]

    def set_wrappers(self, rows):
        """Packer import-call sites (read_wrappers rows); before find_functions, which must see
        them. A row's import is called through its IAT slot when the image imports it, else
        through a slot of its own: the lowest stub of that import inside the image, or, when
        all its stubs are outside, its lowest call site (the runtime stores the import pointer
        there; translated code never runs the stub or the site's original bytes). wrap_imports
        holds those extra slots (slot -> "dll!name"); import_slots() is the order of every slot."""
        self.imports = dict(self.img.imports)
        self.wrap_imports = {}
        self.wrappers = {}
        self._dcache.clear()
        byname = {}
        for s, n in sorted(self.img.imports.items()):
            byname.setdefault(n, s)
        img = self.img
        lo, hi = img.base + img.size_of_headers, img.base + img.size_of_image - 4
        own = {}
        for r in rows:
            n = r["import"]
            if n and n not in byname:
                inside = lo <= r["stub"] <= hi
                own[n] = min(own.get(n, (2, 0)), (0, r["stub"]) if inside else (1, r["call_site"]))
        self.wrap_imports = {a: n for n, (_, a) in own.items()}
        taken = sorted(self.img.imports)
        for p in sorted(self.wrap_imports):
            k = bisect.bisect_left(taken, p - 3)
            if k < len(taken) and taken[k] < p + 4:
                raise ValueError("wrapper slot 0x%x (stub of %s) overlaps slot 0x%x" % (p, self.wrap_imports[p], taken[k]))
            bisect.insort(taken, p)
        self.imports.update(self.wrap_imports)
        slot_of = dict(byname)
        slot_of.update((n, s) for s, n in self.wrap_imports.items())
        for r in rows:
            a, kind = r["call_site"], r["kind"]
            if (r.get("pushes") or r.get("sets")) and kind != "call":
                raise ValueError("wrapper at 0x%x: pushes or sets on a %s" % (a, kind))
            if kind == "load":
                reg = r["pushed_reg"]
                if reg not in REGS or REGS[reg][1] != 4 or reg == "esp" or r["resume"] is None \
                        or not 5 <= r["resume"] - a <= 16:
                    raise ValueError("wrapper at 0x%x: load %r, resume %r" % (a, reg, r["resume"]))
                slot = slot_of.get(r["import"]) if r["import"] else None
                code = bytes([0x8b, 0x05 | REG32.index(reg) << 3]) + struct.pack("<I", slot or 0)
                self.wrappers[a] = _Wrap(next(self.md.disasm(code, a, 1)), r["resume"] - a, r, slot)
                continue
            if kind == "call":
                if r["resume"] is None or not 5 <= r["resume"] - a <= 16:
                    raise ValueError("wrapper at 0x%x: resume %r" % (a, r["resume"]))
                size = r["resume"] - a
            elif kind == "jmp":
                size = 6      # the `jmp [slot]` it replaced: `call stub` and its junk byte, or `push reg; call stub`
            else:
                raise ValueError("wrapper at 0x%x: kind %r" % (a, kind))
            slot = slot_of.get(r["import"]) if r["import"] else None
            code = (b"\xff\x15" if kind == "call" else b"\xff\x25") + struct.pack("<I", slot or 0)
            self.wrappers[a] = _Wrap(next(self.md.disasm(code, a, 1)), size, r, slot)

    def import_slots(self):
        """Every import slot in thunk order (imports.c, rc32_imports): the IAT's, then the
        wrapper table's own."""
        return sorted(self.img.imports) + sorted(self.wrap_imports)

    def decodable(self, a):
        """Code may be decoded at a: an executable section; a range of data_code; while
        find_functions tries a function in a non-executable section, any file data."""
        if self.img.is_code(a):
            return True
        if self._trial:
            return self.img.foff(a) is not None and a >= self.img.base + self.img.size_of_headers
        if a in self._dc_insns:
            return True
        k = bisect.bisect_right(self._dc_lo, a) - 1 if self.data_code else -1
        return k >= 0 and a < self.data_code[k][1]

    def insn(self, a):
        i = self._dcache.get(a)
        if i is not None:
            return i
        if a in self.wrappers:
            return self.wrappers[a]
        if not self.decodable(a):
            raise Unsupported("not code at 0x%x" % a)
        cache = not self._trial or self.img.is_code(a)   # a rejected trial leaves nothing behind
        o = self.img.foff(a)
        if o is None:
            raise Unsupported("no file data at 0x%x" % a)
        for i in self.md.disasm(self.img.data[o:o + 16], a, 1):
            m = i.mnemonic
            if m.startswith(("bnd ", "repz ret", "rep ret", "notrack ")):
                i = _Insn(i, m.split()[-1])
            if len(self._dcache) > DCACHE:
                self._dcache.clear()
            if cache:
                self._dcache[a] = i
            return i
        raise Unsupported("undecodable at 0x%x" % a)

    # --- operands ----------------------------------------------------------
    def gea(self, i, op):
        """Guest (32-bit) address of a memory operand, without its segment."""
        m = op.mem
        parts = []
        if m.base:
            parts.append(self.reg(i, m.base)[0])
        if m.index:
            ix = self.reg(i, m.index)[0]
            parts.append(ix if m.scale == 1 else "%s * %d" % (ix, m.scale))
        if m.disp:
            parts.append("0x%xu" % (m.disp & M32))
        return "(uint32_t)(" + (" + ".join(parts) or "0") + ")"

    def ea(self, i, op):
        m = op.mem
        seg = i.reg_name(m.segment) if m.segment else None
        if seg == "fs":
            return "RC_M(c->fs + %s)" % self.gea(i, op)
        if seg not in (None, "ds", "es", "ss", "cs"):
            raise Unsupported("segment %s at 0x%x" % (seg, i.address))
        return "RC_M(%s)" % self.gea(i, op)

    def read_only(self, op):
        """A fixed address in a section nothing writes (a constant of .rdata, a table of them with a
        register index): no TSO fence (RC_LDC). The sections are as the original program had them: a
        write there faults on Windows."""
        m = op.mem
        if m.segment not in (0, x86.X86_REG_DS):
            return False
        a = m.disp & M32
        s = self.img.section_of(a)
        return s is not None and not s[5] & SCN_WRITE and s[1] <= a and a + max(op.size, 1) <= s[1] + s[2]

    def on_stack(self, op):
        b = op.mem.base
        if op.mem.segment == x86.X86_REG_FS:
            return False
        return b == x86.X86_REG_ESP or (b == x86.X86_REG_EBP and self.rbp_frame)

    def frame_pointer(self, entry, seen):
        i0 = seen.get(entry)
        if not i0 or i0.mnemonic != "push" or i0.op_str != "ebp":
            return False
        i1 = seen.get(entry + i0.size)
        if not i1 or i1.mnemonic != "mov" or i1.op_str != "ebp, esp":
            return False
        for a, i in seen.items():
            if a == i1.address or i.mnemonic == "leave" or (i.mnemonic == "pop" and i.op_str == "ebp"):
                continue
            try:
                w = i.regs_access()[1]
            except capstone.CsError:
                return False
            if any(r in w for r in self.RBP_REGS):
                return False
        return True

    def iat_slot(self, i, op):
        """The IAT slot of a `call/jmp dword ptr [slot]` operand, or None."""
        if op.type != x86.X86_OP_MEM or op.mem.base or op.mem.index or op.mem.segment not in (0, x86.X86_REG_DS):
            return None
        a = op.mem.disp & M32
        return a if a in self.imports else None

    def seh_scopes(self):
        """The __except blocks of MSVC's frame-based SEH, by the instruction that names the
        scope table: {address of `push scopetable`: [handler, ...]}. The frame is set up inline
        (`push -2` (SEH4) / `push -1` (SEH3); `push scopetable; push _except_handlerN`) or by
        a call (`push scopetable; call __SEH_prolog4`, whose first instructions are `push
        _except_handlerN; push dword ptr fs:[0]`). A scope record is {enclosing level, filter,
        handler}, after a 16-byte cookie header in SEH4; a record with a filter is an __except
        (the handler is reached by a jump, inside the function), one without is a __finally
        (a function the unwind calls) and is left out."""
        if "_seh" in self.__dict__:
            return self._seh
        img, out = self.img, {}

        def records(tab, seh4):
            hs, n = [], 0
            a = tab + (16 if seh4 else 0)
            while n < 64:
                lvl, flt, h = img.u32(a), img.u32(a + 4), img.u32(a + 8)
                if h is None or not (lvl == (0xfffffffe if seh4 else M32) or lvl < n) \
                        or not img.is_code(h) or not (flt == 0 or img.is_code(flt)):
                    break
                if flt:
                    hs.append(h)
                n, a = n + 1, a + 12
            return hs if n else None

        for lo, hi in self.code:
            o = img.foff(lo)
            if o is None:
                continue
            d = img.data[o:o + (hi - lo)]
            k = d.find(b"\x68")
            while 0 <= k < len(d) - 10:
                tab = struct.unpack_from("<I", d, k + 1)[0]
                seh4 = None
                if k >= 2 and d[k - 2] == 0x6a and d[k - 1] in (0xfe, 0xff) and d[k + 5] == 0x68:
                    seh4 = d[k - 1] == 0xfe
                elif d[k + 5] == 0xe8:
                    t = (lo + k + 10 + struct.unpack_from("<i", d, k + 6)[0]) & M32
                    if img.u8(t) == 0x68 and img.u32(t + 5) == 0x0035ff64 and img.u8(t + 9) == 0:
                        seh4 = records(tab, True) is not None
                if seh4 is not None and img.foff(tab) is not None:
                    hs = records(tab, seh4)
                    if hs:
                        out[lo + k] = hs
                k = d.find(b"\x68", k + 1)
        self._seh = out
        self.seh_targets = {h for hs in out.values() for h in hs}
        self.cxx_scopes()
        return out

    def catch_conts(self, h, more=()):
        """The continuations of the catch funclet at h: a funclet ends `mov eax, offset CONT; ret`
        (pops or `add esp` may stand between them), and that is where the CRT resumes the function.
        A walk of its code that follows the direct jumps near it; calls are taken to return. more:
        the continuations of try blocks inside it, the rest of its code when the try ends in a throw."""
        conts, seen, work = set(), set(), [h] + list(more)
        while work and len(seen) < 4096:
            a, prev = work.pop(), []
            while a not in seen and self.img.is_code(a):
                seen.add(a)
                try:
                    i = self.insn(a)
                except Unsupported:
                    break
                m = i.mnemonic
                if self.call_slot(i, NORETURN32) is not None:
                    break
                if m == "ret":
                    for p in reversed(prev):
                        o = p.operands
                        if p.mnemonic == "mov" and p.op_str.startswith("eax, ") and o[1].type == x86.X86_OP_IMM:
                            if self.img.is_code(o[1].imm & M32):
                                conts.add(o[1].imm & M32)
                            break
                        if p.mnemonic != "pop" and not p.op_str.startswith("esp, "):
                            break
                    break
                if m in ("jmp", "int3") or x86.X86_GRP_JUMP in i.groups:
                    t = i.operands[0] if i.operands else None
                    if t is not None and t.type == x86.X86_OP_IMM and abs((t.imm & M32) - h) < 0x10000:
                        work.append(t.imm & M32)
                    if m in ("jmp", "int3"):
                        break
                prev = prev[-3:] + [i]
                a += i.size
        return conts

    def cxx_scopes(self):
        """The catch continuations of MSVC's C++ exception handling, what seh_scopes is for
        __except: {address of the instruction that names the frame's __ehhandler thunk: [CONT, ...]}.
        The thunk is `mov eax, offset FuncInfo; jmp __CxxFrameHandler3` (with /GS, cookie checks
        first). FuncInfo's try map names the catch funclets, and each funclet returns the address
        the function goes on at (catch_conts), which nothing else may reach: no function start,
        but part of the body of the function that owns the frame, or of the catch funclet the try
        block is inside (cxx_nested: funclet -> [CONT, ...]), which is the live C frame then."""
        if "_cxx" in self.__dict__:
            return self._cxx
        img, thunks = self.img, {}
        self._cxx, self.cxx_nested, self.cont_targets = {}, {}, set()
        for lo, hi in self.code:
            o = img.foff(lo)
            if o is None:
                continue
            for m in re.finditer(rb"\xb8(?=(....)(?:\xe9|\xff\x25))", img.data[o:o + (hi - lo)], re.S):
                fi = struct.unpack("<I", m.group(1))[0]
                if img.u32(fi) in EH_MAGIC:
                    thunks[lo + m.start()] = fi
        addrs = sorted(thunks)

        def thunk_at(t):
            """The FuncInfo of the __ehhandler thunk at t: it runs straight into the `mov eax`."""
            k = bisect.bisect_left(addrs, t)
            if k == len(addrs) or addrs[k] - t > 0x40:
                return None
            a = t
            try:
                while a < addrs[k]:
                    i = self.insn(a)
                    if i.mnemonic in ("ret", "jmp", "int3"):
                        return None
                    a += i.size
            except Unsupported:
                return None
            return thunks[addrs[k]] if a == addrs[k] else None

        tries = {}
        for r in sorted(img.relocs):
            t = img.u32(r)
            if t is None or img.u8(r - 1) not in (0x68, 0xb8) or not img.is_code(r - 1) or not img.is_code(t):
                continue
            fi = thunk_at(t)
            if fi is None:
                continue
            if fi not in tries:
                tries[fi] = self.try_map(fi)
            top = {c for cs, around in tries[fi] if not around for c in cs}
            if top:
                self._cxx[r - 1] = sorted(top)
        for tm in tries.values():
            for cs, around in tm:
                for h in around:
                    self.cxx_nested.setdefault(h, set()).update(cs)
        self.cont_targets = {c for cs in self._cxx.values() for c in cs} | {
            c for cs in self.cxx_nested.values() for c in cs}
        self.seh_targets |= self.cont_targets
        return self._cxx

    def try_map(self, fi):
        """FuncInfo's try blocks: (their continuations, the funclets of the innermost catch the
        block is inside, [] for one in the function itself). A block is inside another's catch
        when its states lie between that block's tryHigh and catchHigh."""
        img = self.img
        n, tbm = img.u32(fi + 12), img.u32(fi + 16)
        if not n or n > 4096 or tbm is None or img.u32(tbm) is None:
            return []
        rows = []
        for k in range(n):
            lo, hi, chi, nh, hp = (img.u32(tbm + 20 * k + 4 * j) for j in range(5))
            if hp is None or nh is None or nh > 256:
                return []
            hs = [img.u32(hp + 16 * j + 12) for j in range(nh)]
            if not all(h and img.is_code(h) for h in hs):
                return []
            rows.append((lo, hi, chi, hs))

        def inside(a, b):
            return b[1] < a[0] and a[1] <= b[2]

        def conts(row):
            more = {c for r in rows if inside(r, row) for c in conts(r)}
            return {c for h in row[3] for c in self.catch_conts(h, more)}

        return [(conts(r), max((b for b in rows if inside(r, b)), default=(0, 0, 0, []))[3]) for r in rows]

    def call_slot(self, i, names):
        """The import slot of a call of an import in names, direct or through a `jmp [slot]` thunk
        (the game's, which VMProtect left as wrappers), or None."""
        if i.mnemonic != "call":
            return None
        op = i.operands[0]
        s = self.iat_slot(i, op)
        if s is None and op.type == x86.X86_OP_IMM and self.img.is_code(op.imm & M32):
            try:
                t = self.insn(op.imm & M32)
            except Unsupported:
                return None
            if t.mnemonic == "jmp":
                s = self.iat_slot(t, t.operands[0])
        return s if s is not None and self.imports[s] in names else None

    def setjmp_slot(self, i):
        """A setjmp call's slot, or None: it keeps its return address on the stack, for the CRT's
        setjmp to save (the import thunk would replace it, rc32_import_thunk)."""
        return self.call_slot(i, SETJMP)

    # --- discovery: the base rules, plus no-return imports -------------------
    def discover(self, entry):
        # the __except blocks of the SEH frames the function sets up and its catch continuations:
        # part of its body (no flow reaches them, the CRT's handler jumps there)
        extra, seh, cxx = [], self.seh_scopes(), self.cxx_scopes()
        nested = self.cxx_nested.get(entry, ())
        while True:
            seen, labels = core.Translator.discover(self, entry, extra)
            more = sorted(({h for a in seen if a in seh for h in seh[a]} |
                           {t for a in seen if a in cxx for t in cxx[a]} | set(nested)) - set(extra) - {entry})
            if not more:
                break
            extra += more
        if entry == self.entry:     # not a nested discovery (is_noreturn)
            ips = {a for a in self.resume.get(entry, ()) if a in seen} | (self.cont_targets & set(seen)) | {
                a + i.size for a, i in seen.items() if self.setjmp_slot(i) is not None}
            self.resume_ips = sorted(ips)
            self.has_resume = bool(self.resume_ips)
            labels |= set(self.resume_ips)
        for a, i in seen.items():
            if i.mnemonic == "call":
                s = self.iat_slot(i, i.operands[0])
                if s is not None and self.imports[s] in NORETURN32:
                    self.noret.add(a)
        return seen, labels

    # --- one instruction ---------------------------------------------------
    def emit(self, i, out):
        self.cur = i
        m = i.mnemonic
        if m in NOPS:
            return
        if (self.emit_x87(i, out) or self.emit_mmx(i, out) or self.emit_flow32(i, out)
                or self.emit_int32(i, out) or self.emit_int(i, out) or self.emit_sse(i, out)):
            return
        raise Unsupported("%s %s at 0x%x" % (m, i.op_str, i.address))

    def push4(self, v):
        return "rsp = (uint32_t)(rsp - 4); RC_STS(uint32_t, RC_M(rsp), %s);" % v

    def ret_other(self, ret):
        """After a call: the callee returned to another address than ret (rc32_gen.h)."""
        return "if (RC32_RET_OTHER(0x%xu)) { %sreturn; }" % (ret, self.leave_fn())

    def leave_fn(self):
        return "RC32_LEAVE(); " if self.has_resume else ""

    def emit_flow32(self, i, out):
        m = i.mnemonic.replace("bnd ", "").replace("repz ", "").replace("rep ", "")
        ops = i.operands
        A = i.address
        if m in ("ud2", "hlt", "int3", "into", "int1", "retf", "iretd", "sysenter", "in", "out", "insb",
                 "insd", "outsb", "outsd", "cli", "sti", "bound", "ljmp", "lcall", "arpl"):
            out.append('rc_fail("%s", 0x%xull);' % (m, A))
            return True
        if m == "int":
            out.append('rc_fail("int 0x%x", 0x%xull);' % (ops[0].imm & 0xff, A))
            return True
        if isinstance(i, _Wrap) and i.slot is None:
            out.append("rc32_wrapper_unresolved(c, 0x%xu, 0x%xu);" % (A, i.wrap["stub"]))
            return True
        if isinstance(i, _Wrap) and i.wrap.get("pushes"):   # the stub's argument pushes, then the call below
            out.append(" ".join(self.push4(wrap_push(t)) for t in i.wrap["pushes"]))
        if isinstance(i, _Wrap) and i.wrap.get("sets"):     # registers the stub leaves holding the import
            out.append(" ".join("%s = (uint32_t)RC_LDS(uint32_t, RC_M(0x%xu));" % (REGS[t][0], i.slot) for t in i.wrap["sets"]))
        if m in ("ret", "retn"):
            n = (ops[0].imm & 0xffff) if ops else 0
            if self.entry in self.flag_callees:
                out.append("RC32_FLAGS_OUT();")
            out.append("RC32_RET_SAVE(); rsp = (uint32_t)(rsp + %d); %sSPILL_RET(); return;" % (4 + n, self.leave_fn()))
            return True
        if m == "jmp" and ops[0].type != x86.X86_OP_IMM:
            slot = self.iat_slot(i, ops[0])
            if slot is not None:
                self.imports_used.add(slot)
                out.append("/* %s (tail) */ RC32_RET_SAVE(); rsp = (uint32_t)(rsp + 4); %sSPILL(); %s%x(c); FILL(); SPILL_RET(); return;"
                           % (self.imports[slot], self.leave_fn(), self.thunk_prefix, slot))
                return True
            tgt = self.rd(i, ops[0], 4)
            if A in self.tables:
                cases = " ".join("case 0x%xu: goto L_%x;" % (t, t) for t in sorted(set(self.tables[A])))
                out.append("switch ((uint32_t)%s) { %s default: rc_fail(\"jump table\", 0x%xull); }" % (tgt, cases, A))
            else:
                out.append("{ uint32_t tgt = %s; %sSPILL(); rc32_icall(c, tgt); return; }" % (tgt, self.leave_fn()))
            return True
        if m == "jmp":
            t = ops[0].imm & M32
            if t in self.fset and t != self.entry:
                out.append(self.leave_fn() + self.call_internal(t, tail=True))
            else:
                out.append("goto L_%x;" % t)
            return True
        if m in ("jecxz", "jcxz", "loop", "loope", "loopne"):
            t = ops[0].imm & M32
            go = self.call_internal(t, tail=True) if (t in self.fset and t != self.entry) else "goto L_%x;" % t
            cond = {"jecxz": "(uint32_t)rcx == 0", "jcxz": "(uint16_t)rcx == 0",
                    "loop": "(uint32_t)rcx != 0", "loope": "(uint32_t)rcx != 0 && zf",
                    "loopne": "(uint32_t)rcx != 0 && !zf"}[m]
            dec = "" if m.startswith("j") else "rcx = (uint32_t)(rcx - 1); "
            out.append("%sif (%s) { %s }" % (dec, cond, go))
            return True
        if x86.X86_GRP_JUMP in i.groups:
            t = ops[0].imm & M32
            cc = COND[m[1:]]
            if t in self.fset and t != self.entry:
                out.append("if (%s) { %s%s }" % (cc, self.leave_fn(), self.call_internal(t, tail=True)))
            else:
                out.append("if (%s) goto L_%x;" % (cc, t))
            return True
        if m == "call":
            ret = (A + i.size) & M32
            op = ops[0]
            slot = self.setjmp_slot(i)
            if slot is not None:      # an indirect call of what the slot holds: the return address stays
                out.append("/* %s */ { uint32_t tgt = RC_LDS(uint32_t, RC_M(0x%xu)); %s SPILL_CALL(); rc32_icall(c, tgt); "
                           "FILL_CALL(); %s }" % (self.imports[slot], slot, self.push4("0x%xu" % ret), self.ret_other(ret)))
                return True
            if op.type == x86.X86_OP_IMM:
                t = op.imm & M32
                if t == ret:      # call $+5; pop reg: the address of the next instruction
                    out.append(self.push4("0x%xu" % ret))
                    return True
                flags = t in self.flag_callees
                out.append("%s%s %s %s%s" % ("RC32_FLAGS_OUT(); " if flags else "", self.push4("0x%xu" % ret), self.call_internal(t),
                                             self.ret_other(ret), " RC32_FLAGS_IN();" if flags else ""))
                return True
            slot = self.iat_slot(i, op)
            if slot is not None:
                self.imports_used.add(slot)
                out.append("/* %s */ SPILL(); %s%x(c); FILL();" % (self.imports[slot], self.thunk_prefix, slot))
                return True
            out.append("{ uint32_t tgt = %s; %s SPILL_CALL(); rc32_icall(c, tgt); FILL_CALL(); %s }"
                       % (self.rd(i, op, 4), self.push4("0x%xu" % ret), self.ret_other(ret)))
            return True
        return False

    def call_internal(self, t, tail=False):
        self.callees.add(t)
        if tail:
            return "SPILL_CALL(); F_%x(c); return;" % t
        return "SPILL_CALL(); F_%x(c); FILL_CALL();" % t

    # --- integer instructions that differ in 32-bit mode -----------------------
    def emit_int32(self, i, out):
        m = i.mnemonic
        ops = i.operands
        parts = m.split()
        base = parts[-1]
        if len(base) == 5 and base[:4] in STRING and base[4] in "bwdq" and not any(
                o.type == x86.X86_OP_REG and i.reg_name(o.reg).startswith("xmm") for o in ops):
            return self.string32(i, out, parts)
        if m == "push":
            n = ops[0].size if ops[0].type != x86.X86_OP_IMM else 4
            if ops[0].type == x86.X86_OP_REG and i.reg_name(ops[0].reg) in ("cs", "ds", "es", "ss", "fs", "gs"):
                out.append(self.push4("0x%xu" % {"fs": 0x3b, "cs": 0x23}.get(i.reg_name(ops[0].reg), 0x2b)))
            elif n == 2:
                out.append("{ uint16_t v = %s; rsp = (uint32_t)(rsp - 2); RC_STS(uint16_t, RC_M(rsp), v); }"
                           % self.rd(i, ops[0], 2))
            else:
                out.append("{ uint32_t v = %s; %s }" % (self.rd(i, ops[0], 4), self.push4("v")))
            return True
        if m == "pop":
            if ops[0].type == x86.X86_OP_REG and i.reg_name(ops[0].reg) in ("ds", "es", "ss", "fs", "gs"):
                out.append("rsp = (uint32_t)(rsp + 4);")
            elif ops[0].size == 2:
                out.append("{ uint64_t v = RC_LDS(uint16_t, RC_M(rsp)); rsp = (uint32_t)(rsp + 2); %s }"
                           % self.wr(i, ops[0], "v"))
            else:
                out.append("{ uint64_t v = RC_LDS(uint32_t, RC_M(rsp)); rsp = (uint32_t)(rsp + 4); %s }"
                           % self.wr(i, ops[0], "v"))
            return True
        if m == "leave":
            out.append("rsp = rbp; rbp = RC_LDS(uint32_t, RC_M(rsp)); rsp = (uint32_t)(rsp + 4);")
            return True
        if m == "enter":
            size, lvl = ops[0].imm & 0xffff, ops[1].imm & 31
            if lvl:
                raise Unsupported("enter with nesting level at 0x%x" % i.address)
            out.append("%s rbp = rsp; rsp = (uint32_t)(rsp - %d);" % (self.push4("(uint32_t)rbp"), size))
            return True
        if m == "lea":
            n = ops[0].size
            out.append(self.wr(i, ops[0], self.gea(i, ops[1]) if n >= 4 else "(uint16_t)%s" % self.gea(i, ops[1])))
            return True
        if m in ("pushal", "pushad"):
            out.append("{ uint32_t s = (uint32_t)rsp; %s %s %s %s %s %s %s %s }" % tuple(
                self.push4(r) for r in ("(uint32_t)rax", "(uint32_t)rcx", "(uint32_t)rdx", "(uint32_t)rbx", "s",
                                        "(uint32_t)rbp", "(uint32_t)rsi", "(uint32_t)rdi")))
            return True
        if m in ("popal", "popad"):
            ld = lambda k: "RC_LDS(uint32_t, RC_M(rsp + %d))" % (4 * k)
            out.append("rdi = %s; rsi = %s; rbp = %s; rbx = %s; rdx = %s; rcx = %s; rax = %s; rsp = (uint32_t)(rsp + 32);"
                       % tuple(ld(k) for k in (0, 1, 2, 4, 5, 6, 7)))
            return True
        if m in ("pushfd", "pushf"):
            # the modelled flags, plus every other EFLAGS bit the code last wrote with popfd
            # (eflags_other): notably the ID flag (bit 21), toggled to test for cpuid.
            out.append("{ uint32_t f = (uint32_t)rc_flags_pack(cf, pf, zf, sf, of) | ((uint32_t)df << 10) | eflags_other; %s }"
                       % self.push4("f"))
            return True
        if m in ("popfd", "popf"):
            out.append("{ uint32_t f = RC_LDS(uint32_t, RC_M(rsp)); rsp = (uint32_t)(rsp + 4); "
                       "cf = f & 1; pf = (f >> 2) & 1; zf = (f >> 6) & 1; sf = (f >> 7) & 1; df = (f >> 10) & 1; "
                       "of = (f >> 11) & 1; eflags_other = f & 0x3ff138u; }")   # 0x3ff138: EFLAGS less modelled/pack bits
            # the trap flag: a single-step exception (raised here, not after the next instruction;
            # the CPU clears TF when it delivers it). Anti-debugging code sets it under __try and
            # takes a handler that never runs for a debugger that swallowed the trap.
            out.append("if (eflags_other & 0x100u) { eflags_other &= ~0x100u; SPILL(); "
                       "rc32_raise(c, 0x80000004u, 0, 0, 0); FILL(); }  // EXCEPTION_SINGLE_STEP")
            return True
        if m == "std":
            out.append("df = 1;")
            return True
        if m == "cld":
            out.append("df = 0;")
            return True
        if m == "pmaddubsw" and ops[0].type == x86.X86_OP_REG and self.xr(i, ops[0]):
            d = self.xr(i, ops[0])
            s = self.xr(i, ops[1]) if ops[1].type == x86.X86_OP_REG else \
                "%s(rc_v128, %s)" % (self.LD(ops[1]), self.ea(i, ops[1]))
            out.append("%s = rc32_pmaddubsw(%s, %s);" % (d, d, s))
            return True
        if m in AES and ops[0].type == x86.X86_OP_REG and self.xr(i, ops[0]):
            d = self.xr(i, ops[0])
            s = self.xr(i, ops[1]) if ops[1].type == x86.X86_OP_REG else \
                "%s(rc_v128, %s)" % (self.LD(ops[1]), self.ea(i, ops[1]))
            if m == "aesimc":
                out.append("%s = rc32_aesimc(%s);" % (d, s))
            elif m == "aeskeygenassist":
                out.append("%s = rc32_aeskeygenassist(%s, 0x%xu);" % (d, s, ops[2].imm & 0xff))
            else:
                out.append("%s = rc32_aes_round(%s, %s, %d, %d);" % (d, d, s, "dec" in m, m.endswith("last")))
            return True
        # SSE4.1 dword lanes (the CPUID reports no SSE4.1: compiled in next to the AES-NI paths)
        if m == "pextrd" and ops[1].type == x86.X86_OP_REG and self.xr(i, ops[1]):
            out.append(self.wr(i, ops[0], "((rc_vd4)%s)[%d]" % (self.xr(i, ops[1]), ops[2].imm & 3)))
            return True
        if m == "pinsrd" and ops[0].type == x86.X86_OP_REG and self.xr(i, ops[0]):
            d = self.xr(i, ops[0])
            out.append("{ rc_vd4 t_ = (rc_vd4)%s; t_[%d] = (uint32_t)%s; %s = (rc_v128)t_; }"
                       % (d, ops[2].imm & 3, self.rd(i, ops[1], 4), d))
            return True
        if m == "rdtsc":
            out.append("{ uint64_t t = rc32_rdtsc(); rax = (uint32_t)t; rdx = (uint32_t)(t >> 32); }")
            return True
        if m == "xgetbv":
            out.append("rax = 0; rdx = 0;")
            return True
        if m in ("xlatb", "xlat"):
            out.append("rax = (rax & ~0xffull) | RC_LD(uint8_t, RC_M(rbx + (uint8_t)rax));")
            return True
        if m == "salc":
            out.append("rax = (rax & ~0xffull) | (cf ? 0xff : 0);")
            return True
        if m in ("cmpxchg8b", "lock cmpxchg8b"):
            # lock-prefixed or not: one atomic compare-exchange of the 64-bit operand
            p = "(uint64_t *)(uintptr_t)%s" % self.ea(i, ops[0])
            out.append("{ uint64_t e = ((uint64_t)(uint32_t)rdx << 32) | (uint32_t)rax; "
                       "zf = __atomic_compare_exchange_n(%s, &e, ((uint64_t)(uint32_t)rcx << 32) | (uint32_t)rbx, 0, "
                       "__ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST); if (!zf) { rax = (uint32_t)e; rdx = (uint32_t)(e >> 32); } }" % p)
            return True
        if m == "mov" and any(o.type == x86.X86_OP_REG and i.reg_name(o.reg) in ("cs", "ds", "es", "ss", "fs", "gs")
                              for o in ops):
            if ops[0].type == x86.X86_OP_REG and i.reg_name(ops[0].reg) in ("cs", "ds", "es", "ss", "fs", "gs"):
                out.append(";  /* segment load ignored (flat model) */")
            else:
                sel = {"fs": 0x3b, "cs": 0x23}.get(i.reg_name(ops[1].reg), 0x2b)
                out.append(self.wr(i, ops[0], "0x%xu" % sel))
            return True
        return False

    def string32(self, i, out, parts):
        ops = i.operands
        base = parts[-1]
        kind, sfx = base[:-1], base[-1]
        n = {"b": 1, "w": 2, "d": 4, "q": 8}[sfx]
        if n == 8:
            return False
        ct, M = CTYPE[n], MASK[n]
        pfx = parts[0] if len(parts) > 1 else ""
        rep = bool(pfx) and pfx in ("rep", "repe", "repz", "repne", "repnz")
        acc = {1: "al", 2: "ax", 4: "eax"}[n]
        step = "(df ? -%d : %d)" % (n, n)
        adv = lambda r: "%s = (uint32_t)(%s + %s);" % (r, r, step)
        cond = ""
        if kind in ("scas", "cmps") and rep:
            cond = " if (!zf) break;" if pfx in ("rep", "repe", "repz") else " if (zf) break;"
        S, D = "RC_M(rsi)", "RC_M(rdi)"
        if kind == "movs":
            one = "RC_ST(%s, %s, RC_LD(%s, %s)); %s %s" % (ct, D, ct, S, adv("rsi"), adv("rdi"))
            if rep:
                out.append("{ uint32_t len = (uint32_t)rcx * %d; if (!df && (uint32_t)(rdi - rsi) >= len && "
                           "(uint64_t)rsi + len <= 0x100000000ull && (uint64_t)rdi + len <= 0x100000000ull) { RC_FENCE_ST(); "
                           "memmove((void *)(uintptr_t)%s, (const void *)(uintptr_t)%s, len); RC_FENCE_LD(); rsi = (uint32_t)(rsi + len); "
                           "rdi = (uint32_t)(rdi + len); rcx = 0; } else while ((uint32_t)rcx) { %s rcx = (uint32_t)(rcx - 1); } }"
                           % (n, D, S, one))
            else:
                out.append("{ %s }" % one)
        elif kind == "stos":
            one = "RC_ST(%s, %s, v); %s" % (ct, D, adv("rdi"))
            if rep:
                fast = "1" if n == 1 else "v == 0"
                out.append("{ %s v = (%s)rax; uint32_t len = (uint32_t)rcx * %d; if (!df && (%s) && "
                           "(uint64_t)rdi + len <= 0x100000000ull) { RC_FENCE_ST(); memset((void *)(uintptr_t)%s, (int)(uint8_t)v, len); "
                           "rdi = (uint32_t)(rdi + len); rcx = 0; } else while ((uint32_t)rcx) { %s rcx = (uint32_t)(rcx - 1); } }"
                           % (ct, ct, n, fast, D, one))
            else:
                out.append("{ %s v = (%s)rax; %s }" % (ct, ct, one))
        elif kind == "lods":
            one = "%s %s" % (self.wr_reg(acc, "RC_LD(%s, %s)" % (ct, S)), adv("rsi"))
            out.append("while ((uint32_t)rcx) { %s rcx = (uint32_t)(rcx - 1); }" % one if rep else "{ %s }" % one)
        elif kind == "scas":
            one = "{ uint64_t a = (%s)rax, b = RC_LD(%s, %s); %s %s }" % (ct, ct, D, self.alu("cmp", n), adv("rdi"))
            out.append("while ((uint32_t)rcx) { %s rcx = (uint32_t)(rcx - 1);%s }" % (one, cond) if rep else one)
        else:
            one = "{ uint64_t a = RC_LD(%s, %s), b = RC_LD(%s, %s); %s %s %s }" % (
                ct, S, ct, D, self.alu("cmp", n), adv("rsi"), adv("rdi"))
            out.append("while ((uint32_t)rcx) { %s rcx = (uint32_t)(rcx - 1);%s }" % (one, cond) if rep else one)
        return True

    # --- jump tables: the 32-bit absolute forms ---------------------------------
    def _jt_membase(self, fn, i, m):
        if m.base == 0 and not m.segment:
            return m.disp & M32, m.disp & M32
        r = core.Translator._jt_membase(self, fn, i, m)
        return None if r is None else (r[0] & M32, r[1] & M32)

    def _jt_source(self, fn, jmp):
        op = jmp.operands[0]
        if op.type == x86.X86_OP_MEM:
            m = op.mem
            if m.scale == 4 and m.index:
                mb = self._jt_membase(fn, jmp, m)
                if mb:
                    return dict(kind="abs4", tab=mb[1], base=0, idx=self._jt_canon(jmp, m.index),
                                load=jmp.address, esize=4)
            return None
        return self._jt_chase(fn, jmp.address, self._jt_canon(jmp, op.reg))

    def _jt_load_of(self, fn, d, reg, depth):
        ops = d.operands
        if d.mnemonic == "mov" and len(ops) == 2 and ops[0].type == x86.X86_OP_REG and ops[0].size == 4 \
                and ops[1].type == x86.X86_OP_MEM and ops[1].mem.scale == 4 and ops[1].mem.index:
            mb = self._jt_membase(fn, d, ops[1].mem)
            if mb:
                return dict(kind="abs4", tab=mb[1], base=0, idx=self._jt_canon(d, ops[1].mem.index),
                            load=d.address, esize=4)
        return core.Translator._jt_load_of(self, fn, d, reg, depth)

    def _jt_resolve(self, fn, jmp):
        """The base resolution when it found the bound; else, for an absolute table, the run
        of relocated code pointers at the table (table_run) when it is longer (the CRT's
        hand-written memcpy indexes with a value masked in another block; `push 7; pop ecx;
        cmp eax, ecx` bounds are not followed, and a resolution without a bound stops at the
        first target past the next function start, while MSVC moves cold cases elsewhere:
        msvcr100's _output_l). Targets outside the function are translated into it too (the
        descent follows them)."""
        r = core.Translator._jt_resolve(self, fn, jmp)
        if r and r["bound"] is not None:
            return r
        src = self._jt_source(fn, jmp)
        if not src or src["kind"] != "abs4" or src["tab"] is None:
            return r
        tab = src["tab"]
        targets = [self.img.u32(w) for w in self.table_run(tab)]
        if not targets or (r and len(r["targets"]) >= len(targets)):
            return r
        return dict(targets=targets, pattern="abs4", bound=None, how="relocs", tab=tab, count=len(targets),
                    level1=None)

    def table_run(self, tab):
        """Addresses of the relocated code pointers of an indexed table at tab, in address
        order: from tab on, else from its second word (memcpy's `and eax, 3; jmp [eax*4 + tab]`
        on a path where eax is not 0); and the words before it (memmove's backward tails:
        `neg ecx; jmp [ecx*4 + tab]` indexes back from the table's end, where the next table
        may start). A switch over the loaded word only gains a case for a word no index
        reaches."""
        def run(t, step):
            out = []
            while t in self.img.relocs and len(out) < 1024 and self.img.is_code(self.img.u32(t) or 0):
                out.append(t)
                t += step
            return out
        return run(tab - 4, -4)[::-1] + (run(tab, 4) or run(tab + 4, 4))

    def _jt_read(self, addr, size, signed=False):
        v = core.Translator._jt_read(self, addr, size, False)
        return v

    # --- one function ------------------------------------------------------
    def function(self, entry, name=None):
        self.imports_used = set()
        self.entry = entry
        lines = core.Translator.function(self, entry, name)
        k = next(n for n, s in enumerate(lines) if s.startswith("  (void)zf;"))
        extra = ["  uint8_t df = 0; (void)df;",
                 "  uint32_t eflags_other = 0; (void)eflags_other;  // EFLAGS bits outside cf/pf/zf/sf/of/df"]
        if entry in self.flag_callees:
            extra.append("  RC32_FLAGS_IN();")
        if self.has_resume:
            ips = self.resume_ips
            extra += ["  static const uint32_t resume_ips[] = {%s};" % ", ".join("0x%xu" % a for a in ips),
                      "  rc32_resume_rec *volatile rr_ = 0;",
                      "#define RC32_LEAVE() do { if (rr_) rc32_resume_leave(c, rr_); } while (0)",
                      "#define RC32_RESUME_ARM() do { if (!rr_) { rr_ = rc32_resume_enter(c, 0x%xu, resume_ips, %d); "
                      "if (_setjmp(rc32_resume_jb(rr_))) { FILL(); df = 0; switch (rc32_resume_ip(rr_)) { %s "
                      "default: rc_fail(\"resume\", 0x%xull); } } } } while (0)"
                      % (entry, len(ips), " ".join("case 0x%xu: goto L_%x;" % (a, a) for a in ips), entry),
                      "  RC32_RESUME_ARM();"]
            lines = [s for s in lines]
            lines.insert(len(lines) - 2, "#undef RC32_LEAVE")
            lines.insert(len(lines) - 2, "#undef RC32_RESUME_ARM")
        lines[k + 1:k + 1] = extra
        # the body is in address order: code below the entry (a loop head a backward jump
        # reaches) would otherwise run first
        first = next((n for n, s in enumerate(lines) if s.startswith("L_")), None)
        if first is not None and lines[first] != "L_%x:" % entry:
            lines.insert(first, "  goto L_%x;" % entry)
        return lines

    # --- function discovery ------------------------------------------------------
    @staticmethod
    def merge_ranges(addrs, insn):
        """[lo, hi) runs of the instructions at addrs (adjacent instructions join)."""
        out = []
        for a in sorted(addrs):
            e = a + insn(a).size
            if out and a <= out[-1][1]:
                out[-1][1] = max(out[-1][1], e)
            else:
                out.append([a, e])
        return [tuple(r) for r in out]

    def covered_by(self, seen):
        for a, i in seen.items():
            for k in range(i.size):
                yield a + k

    def find_functions(self, verbose=False):
        """Function starts: entry, exports, TLS callbacks and SafeSEH handlers; then, to a
        fixpoint, every direct call target and every relocated pointer into code found in a
        function's instructions; then relocated pointers into code from data (vtables,
        callback tables, C++ EH funclets) that do not land inside a known function, first
        those in data sections, then the words in code sections that no indexed table holds
        (`jmp/mov reg, [reg*4 + tab]`: a jump table, table_run); then what is left of the code sections, cut at padding. A pointer target in
        code (an immediate or a word in a code section) is kept only if it does not look like
        data (not_code: a relocated word, zero bytes, or the gap rule). A pointer that lands
        inside a known function's body from outside it, other than a jump table's, is a
        resume point of that function (scope-table handlers, catch continuations).

        A direct call target in a non-executable section (code_in_data) is decoded as a
        function too, and kept unless it decodes to an instruction compiled code never has (the
        gap rule); so is the target of a jump into such a section that made a function fail.
        The instructions found there become data_code, which the translation decodes as code."""
        img = self.img
        seeds = set()
        if img.entry:
            seeds.add(img.entry)
        seeds |= {a for a in img.exports if img.is_code(a)}
        if img.tls:
            seeds |= {a for a in img.tls["callbacks"] if img.is_code(a)}
        seeds |= {a for a in img.safeseh if img.is_code(a)}
        funcs = {}       # start -> Found or None (failed)
        owner = {}       # instruction address -> function
        cover = Cover(img.base, img.size_of_image + 0x1000)
        self.discovery_failed = {}
        self.set_functions(seeds)
        relocs = sorted(img.relocs)

        def in_data(t):
            """A direct call target in a non-executable section with file data (outside the
            headers): tried as a function (code_in_data)."""
            return not img.is_code(t) and img.foff(t) is not None and t >= img.base + img.size_of_headers

        strict = set()   # starts kept only if they look like code (not_code), else rejected
        rejected = set()
        reloc_set = img.relocs

        def not_code(f, seen):
            """Why a pointer target is data: a relocated word (an entry of a pointer table), zero
            bytes (`add [eax], al`: the null word that ends a table, as the _initterm bounds
            point at), or an instruction compiled code never has (the gap rule)."""
            if f in reloc_set or img.u16(f) == 0:
                return "data at 0x%x" % f
            bad = next((i for i in seen.values() if junk(i, seen)), None)
            return None if bad is None else "data decodes to %s at 0x%x" % (bad.mnemonic, bad.address)

        def run(work):
            seh = self.seh_targets
            while work:
                f = work.pop()
                if f in funcs or f in seh:   # an __except block: the body of the function it is in
                    continue
                if f in strict and f in cover:   # inside a function found since it was queued
                    continue
                if f in strict and not_code(f, {}):
                    funcs[f] = None
                    rejected.add(f)
                    continue
                self.set_functions(set(funcs) | {f} | set(work), clear=False)
                self._trial = not img.is_code(f)
                try:
                    seen, _ = self.discover(f)
                except Unsupported as e:
                    funcs[f] = None
                    if f in strict:
                        rejected.add(f)
                    else:
                        self.discovery_failed[f] = str(e)
                    continue
                finally:
                    self._trial = False
                if not img.is_code(f) or f in strict:
                    bad = not_code(f, seen) if f in strict else next(
                        (i for i in seen.values() if junk(i, seen)), None)
                    if bad is not None:
                        funcs[f] = None
                        if f in strict:
                            rejected.add(f)
                        else:
                            self.discovery_failed[f] = "data decodes to %s at 0x%x" % (bad.mnemonic, bad.address)
                        continue
                    self._dc_insns.update(a for a in seen if not img.is_code(a))
                funcs[f] = Found(
                    {i.operands[0].imm & M32 for i in seen.values()
                     if i.mnemonic == "jmp" and i.operands[0].type == x86.X86_OP_IMM},
                    any(junk(i, seen) for i in seen.values()),
                    {i.operands[0].imm & M32 for a, i in seen.items()
                     if i.mnemonic == "call" and i.operands[0].type == x86.X86_OP_IMM
                     and reads_flags(a + i.size, seen)})
                for a, i in seen.items():
                    owner.setdefault(a, f)
                    cover.add(a, i.size)
                    if i.mnemonic == "call" and i.operands and i.operands[0].type == x86.X86_OP_IMM:
                        t = i.operands[0].imm & M32
                        if (img.is_code(t) or (self.code_in_data and in_data(t))) and t not in funcs \
                                and t != a + i.size:
                            work.append(t)
                    # relocated immediates in the instruction: function pointers (push offset f,
                    # mov [x], offset f); not displacements (table bases)
                    k = bisect.bisect_left(relocs, a)
                    while k < len(relocs) and relocs[k] < a + i.size:
                        v = img.u32(relocs[k])
                        if v is not None and img.is_code(v) and v not in funcs and v not in seen and any(
                                o.type == x86.X86_OP_IMM and (o.imm & M32) == v for o in i.operands) \
                                and x86.X86_GRP_JUMP not in i.groups and i.mnemonic != "call":
                            self._ptr_cands.add(v)
                        k += 1
                    # an indexed table of words in code (`jmp [reg*4 + tab]`, `mov reg,
                    # [reg*4 + tab]`): a jump table, whose words are case labels
                    if i.mnemonic != "call":
                        for o in i.operands:
                            if o.type == x86.X86_OP_MEM and o.mem.index and o.mem.scale == 4 \
                                    and img.is_code(o.mem.disp & M32):
                                tables.add(o.mem.disp & M32)

        def table_words():
            """Relocated words of the indexed tables (table_run)."""
            return {w for t in tables for w in self.table_run(t)}

        self._ptr_cands = set()
        tables = set()
        self.seh_scopes()
        run(sorted(seeds))
        # pointers from code (immediates) and from data; then relocated words in code sections
        # that no instruction covers and no indexed table holds (Microsoft's DLLs merge
        # .rdata into .text: vtables, _initterm and callback tables), each kept under the gap
        # rule, since some of them are data too (unresolved tables, pointers into the middle of
        # a function); then table words pointing into a known function's body where that
        # function jumps with a plain `jmp`: a function the other one tail-calls (a deleting
        # destructor's `jmp` to the destructor), not a resume point
        while True:
            data_ptrs, code_ptrs = set(), set()
            jt = table_words()
            for r in relocs:
                if r in cover:
                    continue
                v = img.u32(r)
                if v is not None and img.is_code(v):
                    if not img.is_code(r):
                        data_ptrs.add(v)
                    elif r not in jt:   # else a jump table's case label, not a function
                        code_ptrs.add(v)
            new = [v for v in sorted((self._ptr_cands | data_ptrs) - set(funcs) - self.seh_targets) if v not in cover]
            strict.update(v for v in new if v in self._ptr_cands and v not in data_ptrs)
            if not new:
                new = [v for v in sorted(code_ptrs - set(funcs) - self.seh_targets) if v not in cover]
                strict.update(new)
            if not new:
                tails = set()
                for v in (data_ptrs | code_ptrs) - set(funcs):
                    f = owner.get(v)
                    if f is not None and funcs.get(f) and v in funcs[f].jumps:
                        tails.add(v)
                new = sorted(tails)
                # a jump target is code: not a strict start, which run skips once covered (a
                # tail is: it was never entered, and the loop went on forever)
                strict.difference_update(tails)
            if not new:
                break
            run(new[::-1])
        # resume points: pointers into a function's body from outside it
        self.resume = collections.defaultdict(set)
        jt = table_words()
        for r in relocs:
            v = img.u32(r)
            if v is None or v in funcs or v not in owner:
                continue
            f = owner[v]
            if owner.get(r) != f and not (img.is_code(r) and r in jt):
                self.resume[f].add(v)
        # what is left: cut the uncovered code at padding and try each piece
        gaps = gaps_rejected = 0
        # relocated words in code that no instruction covers are data (jump tables)
        datain = set()
        for r in relocs:
            if r not in cover and img.is_code(r):
                datain.update(range(r, r + 4))
        for lo, hi in self.code:
            a = lo
            while a < hi:
                if a in cover or a in datain or img.u8(a) in PADDING or img.u8(a) is None or img.u8(a) == 0:
                    a += 1
                    continue
                b = a
                while b < hi and b not in cover and b not in datain and img.u8(b) not in PADDING:
                    b += 1
                gaps += 1
                if a not in funcs:
                    run([a])
                    found = funcs.get(a)
                    if found is None or found.junk:
                        funcs.pop(a, None)
                        self.discovery_failed.pop(a, None)
                        gaps_rejected += 1
                a = max(b, a + 1)
        # a function that failed by running into a non-executable section (a jump to code there:
        # a tail call of a function nothing calls directly): try the target as a function, then
        # the function again
        tried = set()
        while self.code_in_data:
            retry = {}
            for f, why in self.discovery_failed.items():
                p = why.split()
                if p[:3] == ["not", "code", "at"] and img.is_code(f) and (f, p[3]) not in tried:
                    t = int(p[3], 16)
                    if in_data(t):
                        retry[f] = t
                        tried.add((f, p[3]))
            run(sorted({t for t in retry.values() if t not in funcs})[::-1])
            again = sorted(f for f, t in retry.items() if funcs.get(t))
            if not again:
                break
            for f in again:
                funcs.pop(f, None)
                self.discovery_failed.pop(f, None)
            run(again[::-1])
        self.set_data_code(self.merge_ranges(self._dc_insns, self.insn))
        self._dc_insns = set()
        # flags are locals of a function, but the CRT's hand-written x87 helpers answer in ZF, or
        # leave the caller's flags alone (`call helper; je`): a direct call with a conditional jump
        # reading its flags (reads_flags) is such a call, and they pass through c->ret_flags both ways (rc32_gen.h)
        self.flag_callees = {t for v in funcs.values() if v for t in v.flag_calls}
        self.discovery = {"seeds": len(seeds), "functions": sum(1 for v in funcs.values() if v),
                          "failed": len(self.discovery_failed), "gaps": gaps, "gaps_rejected": gaps_rejected,
                          "code_ptrs": len(strict), "code_ptrs_rejected": len(rejected),
                          "resume_functions": len(self.resume),
                          "data_code_functions": sum(1 for f, v in funcs.items() if v and not img.is_code(f)),
                          "data_code_bytes": sum(h - l for l, h in self.data_code),
                          "code_bytes": sum(h - l for l, h in self.code), "covered_bytes": len(cover)}
        return sorted(f for f, v in funcs.items() if v)


def translate(path, out, wanted=None, wrappers=None):
    tr = Translator32(path, wrappers=read_wrappers(wrappers) if wrappers else None)
    todo = sorted(wanted or tr.fsorted)
    lines, done, bad = [], [], {}
    used = set()
    callees = set()
    for f in todo:
        try:
            lines += tr.function(f, tr.img.exports.get(f))
            done.append(f)
            used |= tr.imports_used
            callees |= tr.callees
        except Unsupported as e:
            bad[f] = str(e)
    with open(out, "w") as fo:
        fo.write(PRELUDE)
        for f in sorted(set(done) | callees):
            fo.write("void F_%x(rc_ctx *c);\n" % f)
        for s in sorted(used):
            fo.write("void I_%x(rc_ctx *c);  // %s\n" % (s, tr.imports[s]))
        fo.write("\n" + "\n".join(lines))
    return tr, done, bad


def main():
    argv = sys.argv[1:]
    wrappers = None
    if "--wrappers" in argv:
        k = argv.index("--wrappers")
        wrappers = argv[k + 1]
        del argv[k:k + 2]
    path, out = argv[0], argv[1]
    wanted = [int(a, 16) for a in argv[2:]] or None
    tr, done, bad = translate(path, out, wanted, wrappers)
    print(tr.discovery)
    print("translated %d, unsupported %d" % (len(done), len(bad)))
    reasons = collections.Counter()
    for v in bad.values():
        for r in v.split(": ", 1)[-1].split("; "):
            reasons[" ".join(r.split()[:2])] += 1
    for r, n in reasons.most_common(30):
        print("  %5d  %s" % (n, r))


if __name__ == "__main__":
    main()
