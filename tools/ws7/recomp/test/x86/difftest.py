#!/usr/bin/env python3
"""Differential test of the 32-bit front end: translated C against Unicorn.

For every function of a PE image (or a sample), N cases are built from a seed: registers,
a stack with a return sentinel and arguments, a scratch region of words that are partly
pointers into itself, a TEB, x87 / MMX / SSE state. runner.c runs the translated function,
Unicorn the original bytes, from the same state; the end states are compared: status (return,
first import call and its index, or abnormal: a fault or rc_fail), the eight registers,
xmm0..7, the x87 stack (TOP, which registers are empty, the values of the others, C0 C2 C3,
the control word), the MMX registers when the function uses MMX, and the bytes of the stack,
the scratch region, the TEB and the image's writable sections.

Usage (from tools/ws7/recomp, with a Python that has capstone and unicorn):
  python3 -m test.x86.difftest IMAGE.dll --out DIR [--cases N] [--sample N] [--only ADDR,...]
                               [--jobs N] [--report FILE.json]
  IMAGE may also be several, or come from $RC32_TEST_DLLS (os.pathsep-separated); without an
  image the test is skipped (exit 0). Images are read, never copied or committed; everything
  built goes to --out.

Exit status 1 when any case mismatches.
"""
import argparse
import collections
import json
import math
import os
import random
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
RECOMP = os.path.normpath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, RECOMP)

from capstone import x86  # noqa: E402

STACK_LO, STACK_HI = 0x00100000, 0x00120000
SCRATCH_LO, SCRATCH_HI = 0x00200000, 0x00210000
TEB = 0x00300000
PEB = 0x00301000
SENTINEL = 0xfffffff0
GDT = 0x00380000
FS_SEL = 0x08
WIRE_FPU = struct.Struct("<8d8QHHBB2x")
RUNTIME = ["rc32_mem.c", "rc32_image.c", "rc32_teb.c", "rc32_seh.c"]
TRANSCENDENTAL = {(0xd9, 0xf0), (0xd9, 0xf1), (0xd9, 0xf2), (0xd9, 0xf3), (0xd9, 0xf9), (0xd9, 0xfb),
                  (0xd9, 0xfe), (0xd9, 0xff), (0xd9, 0xfd), (0xd9, 0xf5), (0xd9, 0xf8)}


# --- x87 values: double <-> 80-bit (mantissa, exponent) as Unicorn wants them ----------------
def to80(v):
    if v != v:
        return (0xc000000000000000, 0x7fff)
    s = 0x8000 if math.copysign(1.0, v) < 0 else 0
    if math.isinf(v):
        return (0x8000000000000000, 0x7fff | s)
    if v == 0:
        return (0, s)
    m, e = math.frexp(abs(v))
    return (int(m * (1 << 64)), (e - 1 + 16383) | s)


def from80(t):
    m, se = t
    e = se & 0x7fff
    s = -1.0 if se & 0x8000 else 1.0
    if e == 0x7fff:
        return float("nan") if (m << 1) & 0xffffffffffffffff else s * float("inf")
    if m == 0:
        return s * 0.0
    try:
        return s * math.ldexp(float(m), (e or 1) - 16383 - 63)
    except OverflowError:
        return s * float("inf")


def rc_cpuid(leaf):
    """runtime/rc_int.h rc_cpuid."""
    if leaf == 0:
        return 7, 0x756e6547, 0x6c65746e, 0x49656e69
    if leaf == 1:
        d = sum(1 << b for b in (0, 4, 8, 15, 19, 23, 24, 25, 26))
        return 0x000306a9, 0x00010800, (1 << 0) | (1 << 9), d
    if leaf == 0x80000000:
        return 0x80000001, 0, 0, 0
    if leaf == 0x80000001:
        return 0, 0, 1, (1 << 11) | (1 << 20) | (1 << 29)
    return 0, 0, 0, 0


# --- the image, as both sides load it -------------------------------------------------------
class Loaded:
    def __init__(self, img, imp_area):
        self.img = img
        self.base, self.size = img.base, img.size_of_image
        b = bytearray(self.size)
        b[:img.size_of_headers] = img.data[:img.size_of_headers]
        for _, va, vs, ro, rs, _ in img.sections:
            n = min(rs, vs) if vs else rs
            o = va - img.base
            b[o:o + n] = img.data[ro:ro + n]
        self.slots = sorted(img.imports)
        for k, s in enumerate(self.slots):
            struct.pack_into("<I", b, s - img.base, imp_area + 4 * k)
        self.bytes = bytes(b)
        self.writable = [(va, vs) for _, va, vs, _, _, ch in img.sections if ch & 0x80000000 and vs]


class Unicorn:
    def __init__(self, ld, imp_area, nimp, hooks, insn=None):
        self.insn = insn
        from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE, UC_HOOK_INTR
        from unicorn import x86_const as X
        self.X = X
        self.ld = ld
        u = self.u = Uc(UC_ARCH_X86, UC_MODE_32)
        # what the runner has committed: the image and the runtime's arena (the import area),
        # in host pages (16 KB on arm64 macOS) and 1 MB arena steps (runtime/rc32_mem.c)
        hp = os.sysconf("SC_PAGE_SIZE")
        u.mem_map(ld.base, (ld.size + hp - 1) & ~(hp - 1))
        self.imp_area, self.nimp = imp_area, nimp
        u.mem_map(imp_area & ~0xfffff, 0x100000)
        u.mem_write(imp_area, b"\xc3" * (4 * max(nimp, 1)))
        u.mem_map(STACK_LO, STACK_HI - STACK_LO)
        u.mem_map(SCRATCH_LO, SCRATCH_HI - SCRATCH_LO)
        u.mem_map(TEB, 0x2000)
        u.mem_map(GDT, 0x1000)

        def desc(base, limit, access, flags):
            return struct.pack("<HHBBBB", limit & 0xffff, base & 0xffff, (base >> 16) & 0xff, access,
                               ((limit >> 16) & 0xf) | (flags << 4), (base >> 24) & 0xff)
        gdt = desc(0, 0, 0, 0) + desc(TEB, 0xfff, 0x93, 0x4) + desc(0, 0xfffff, 0x93, 0xc)
        u.mem_write(GDT, gdt)
        u.reg_write(X.UC_X86_REG_GDTR, (0, GDT, len(gdt) - 1, 0))
        u.reg_write(X.UC_X86_REG_FS, FS_SEL)
        for r in (X.UC_X86_REG_SS, X.UC_X86_REG_DS, X.UC_X86_REG_ES):
            u.reg_write(r, 0x10)
        self.stop = None
        self.dbg = False

        def on_imp(uc, addr, size, ud):
            self.stop = ("import", (addr - imp_area) // 4)
            uc.emu_stop()
        u.hook_add(UC_HOOK_CODE, on_imp, begin=imp_area, end=imp_area + 4 * max(nimp, 1) - 1)

        def on_intr(uc, intno, ud):
            self.stop = ("abnormal", intno)
            uc.emu_stop()
        u.hook_add(UC_HOOK_INTR, on_intr)

        def on_fetch(uc, acc, addr, size, val, ud):
            if self.dbg:
                print("fetch unmapped at %x" % addr, file=sys.stderr)
            if addr == SENTINEL and self.stop is None:
                self.stop = ("return", 0)
            return False
        from unicorn import UC_HOOK_MEM_FETCH_UNMAPPED
        u.hook_add(UC_HOOK_MEM_FETCH_UNMAPPED, on_fetch)
        self._env = {}
        for a, kind, nxt in hooks:
            if kind in ("fsw", "fenv"):
                u.hook_add(UC_HOOK_CODE, self._fsw, begin=a, end=a)
            if kind == "fenv":    # fnstenv / fnsave: the pointer fields are not modelled
                u.hook_add(UC_HOOK_CODE, self._env_pre, begin=a, end=a)
                u.hook_add(UC_HOOK_CODE, self._env_post, begin=nxt, end=nxt)
            elif kind == "mxcsr":
                u.hook_add(UC_HOOK_CODE, self._mxcsr, begin=a, end=a)
            elif kind == "cpuid":
                u.hook_add(UC_HOOK_CODE, self._cpuid_pre, begin=a, end=a)
                u.hook_add(UC_HOOK_CODE, self._cpuid_post, begin=nxt, end=nxt)
            elif kind == "sreg":     # selector values: Windows' (the translation's), not Unicorn's
                u.hook_add(UC_HOOK_CODE, self._env_pre, begin=a, end=a)
                u.hook_add(UC_HOOK_CODE, self._sreg_post, begin=nxt, end=nxt, user_data=a)
            elif kind == "pushf":    # AF is not modelled (xlat/emit_int.py)
                u.hook_add(UC_HOOK_CODE, self._pushf_post, begin=nxt, end=nxt)
            elif kind == "ref_x87":    # an operand QEMU 5 handles unlike hardware
                u.hook_add(UC_HOOK_CODE, self._ref_x87, begin=a, end=a)
            elif kind == "rdtsc":
                u.hook_add(UC_HOOK_CODE, self._rdtsc_post, begin=nxt, end=nxt)
        self._leaf = {}

    def _fsw(self, uc, addr, size, ud):
        X = self.X
        uc.reg_write(X.UC_X86_REG_FPSW, uc.reg_read(X.UC_X86_REG_FPSW) & ~0x80ff)

    def _env_pre(self, uc, addr, size, ud):
        X = self.X
        i = self.insn(addr)
        m = next((o.mem for o in i.operands if o.type == x86.X86_OP_MEM), None)
        if m is None:
            self._env[addr + size] = None
            return
        rv = lambda r: uc.reg_read({x86.X86_REG_EAX: X.UC_X86_REG_EAX, x86.X86_REG_ECX: X.UC_X86_REG_ECX,
                                    x86.X86_REG_EDX: X.UC_X86_REG_EDX, x86.X86_REG_EBX: X.UC_X86_REG_EBX,
                                    x86.X86_REG_ESP: X.UC_X86_REG_ESP, x86.X86_REG_EBP: X.UC_X86_REG_EBP,
                                    x86.X86_REG_ESI: X.UC_X86_REG_ESI, x86.X86_REG_EDI: X.UC_X86_REG_EDI}[r]) if r else 0
        self._env[addr + size] = (rv(m.base) + rv(m.index) * m.scale + m.disp) & 0xffffffff

    def _pushf_post(self, uc, addr, size, ud):
        X = self.X
        esp = uc.reg_read(X.UC_X86_REG_ESP)
        f = struct.unpack("<I", uc.mem_read(esp, 4))[0]
        uc.mem_write(esp, struct.pack("<I", f & ~0x10))

    def _sreg_post(self, uc, addr, size, ud):
        X = self.X
        i = self.insn(ud)
        sel = {"fs": 0x3b, "cs": 0x23}.get(i.reg_name(i.operands[-1].reg) if i.mnemonic == "mov" else
                                          i.reg_name(i.operands[0].reg), 0x2b)
        a = self._env.pop(addr, None)
        if i.mnemonic == "push":
            uc.mem_write(uc.reg_read(X.UC_X86_REG_ESP), struct.pack("<I", sel))
        elif a is not None and i.operands[0].type == x86.X86_OP_MEM:
            uc.mem_write(a, struct.pack("<H", sel))
        else:
            r = {"eax": X.UC_X86_REG_EAX, "ecx": X.UC_X86_REG_ECX, "edx": X.UC_X86_REG_EDX, "ebx": X.UC_X86_REG_EBX,
                 "esi": X.UC_X86_REG_ESI, "edi": X.UC_X86_REG_EDI, "ebp": X.UC_X86_REG_EBP}
            n = i.reg_name(i.operands[0].reg)
            full = {"ax": "eax", "cx": "ecx", "dx": "edx", "bx": "ebx", "si": "esi", "di": "edi", "bp": "ebp"}
            if n in r:
                uc.reg_write(r[n], sel)
            elif n in full:
                uc.reg_write(r[full[n]], (uc.reg_read(r[full[n]]) & ~0xffff) | sel)

    def _env_post(self, uc, addr, size, ud):
        a = self._env.pop(addr, None)
        if a is not None:
            uc.mem_write(a + 12, b"\0" * 16)

    def _ref_x87(self, uc, addr, size, ud):
        """fyl2x / fyl2xp1 with ST0 <= 0 (QEMU 5 sets C2 and does not pop), fsqrt of a negative
        number (QEMU sets C2): the reference is not hardware there."""
        X = self.X
        top = (uc.reg_read(X.UC_X86_REG_FPSW) >> 11) & 7
        v = from80(uc.reg_read(X.UC_X86_REG_FP0 + top))
        op = bytes(uc.mem_read(addr, 2))
        if (op == b"\xd9\xf1" and not v > 0) or (op == b"\xd9\xf9" and not v + 1.0 > 0) or (op == b"\xd9\xfa" and v < 0):
            self.ref_limit = True

    def _mxcsr(self, uc, addr, size, ud):
        X = self.X
        uc.reg_write(X.UC_X86_REG_MXCSR, uc.reg_read(X.UC_X86_REG_MXCSR) & ~0x3f)

    def _cpuid_pre(self, uc, addr, size, ud):
        self._leaf[addr + size] = uc.reg_read(self.X.UC_X86_REG_EAX)

    def _cpuid_post(self, uc, addr, size, ud):
        X = self.X
        if addr in self._leaf:
            a, b, c, d = rc_cpuid(self._leaf.pop(addr))
            for r, v in ((X.UC_X86_REG_EAX, a), (X.UC_X86_REG_EBX, b), (X.UC_X86_REG_ECX, c), (X.UC_X86_REG_EDX, d)):
                uc.reg_write(r, v)

    def _rdtsc_post(self, uc, addr, size, ud):
        X = self.X
        uc.reg_write(X.UC_X86_REG_EAX, 0x12345678)
        uc.reg_write(X.UC_X86_REG_EDX, 0)

    def run(self, case, limit):
        from unicorn import UcError
        X, u = self.X, self.u
        u.mem_write(self.ld.base, self.ld.bytes)
        for a, data in case["write"]:
            u.mem_write(a, bytes(data))
        regs = [X.UC_X86_REG_EAX, X.UC_X86_REG_ECX, X.UC_X86_REG_EDX, X.UC_X86_REG_EBX,
                X.UC_X86_REG_ESP, X.UC_X86_REG_EBP, X.UC_X86_REG_ESI, X.UC_X86_REG_EDI]
        for r, v in zip(regs, case["regs"]):
            u.reg_write(r, v)
        u.reg_write(X.UC_X86_REG_EFLAGS, 0x202)   # IF: user mode
        f = case["fpu"]
        u.reg_write(X.UC_X86_REG_FPCW, f["cw"])
        u.reg_write(X.UC_X86_REG_FPSW, (f["sw"] & ~0x3800) | (f["top"] << 11))
        tw = 0
        for k in range(8):
            if f["tag"] >> k & 1:
                tw |= 3 << (2 * k)
        u.reg_write(X.UC_X86_REG_FPTAG, tw)
        for k in range(8):
            if case["mmx"]:
                u.reg_write(X.UC_X86_REG_FP0 + k, (f["mm"][k], 0xffff))
            else:
                u.reg_write(X.UC_X86_REG_FP0 + k, to80(f["st"][k]))
        for k in range(8):
            u.reg_write(X.UC_X86_REG_XMM0 + k, case["xmm"][k])
        u.reg_write(X.UC_X86_REG_MXCSR, case["mxcsr"])
        self.stop = None
        self.ref_limit = False
        try:
            u.emu_start(case["func"], SENTINEL, count=limit)
            pc = u.reg_read(X.UC_X86_REG_EIP)
            if self.stop is None:
                self.stop = ("return", 0) if pc == SENTINEL else ("timeout", pc)
        except UcError as e:
            # the return sentinel is not mapped: fetching it is the return
            if self.stop is None:
                self.stop = ("abnormal", str(e))
        st = {"status": self.stop[0], "info": self.stop[1], "ref_limit": self.ref_limit,
              "regs": [u.reg_read(r) for r in regs], "mxcsr": u.reg_read(X.UC_X86_REG_MXCSR),
              "xmm": [u.reg_read(X.UC_X86_REG_XMM0 + k) for k in range(8)]}
        sw = u.reg_read(X.UC_X86_REG_FPSW)
        tw = u.reg_read(X.UC_X86_REG_FPTAG)
        st["fpu"] = {"cw": u.reg_read(X.UC_X86_REG_FPCW), "sw": sw & ~0x3800, "top": (sw >> 11) & 7,
                     "tag": sum(1 << k for k in range(8) if (tw >> (2 * k)) & 3 == 3),
                     "st": [from80(u.reg_read(X.UC_X86_REG_FP0 + k)) for k in range(8)],
                     "mm": [u.reg_read(X.UC_X86_REG_FP0 + k)[0] for k in range(8)]}
        st["dump"] = [bytes(u.mem_read(a, n)) for a, n in case["dump"]]
        return st


class Runner:
    def __init__(self, exe, image):
        self.exe, self.image = exe, image
        self.start()

    def start(self):
        self.p = subprocess.Popen([self.exe, self.image], stdin=subprocess.PIPE, stdout=subprocess.PIPE)
        h = self.p.stdout.read(16)
        magic, self.imp_area, self.base, self.size = struct.unpack("<4I", h)
        assert magic == 0x314e5552, "runner did not start"

    def run(self, case):
        b = [struct.pack("<4sI", b"CASE", case["func"]), struct.pack("<8I", *case["regs"]),
             struct.pack("<II", TEB, case["mxcsr"])]
        f = case["fpu"]
        b.append(WIRE_FPU.pack(*f["st"], *f["mm"], f["cw"], f["sw"], f["top"], f["tag"]))
        b.append(b"".join(v.to_bytes(16, "little") for v in case["xmm"]))
        b.append(struct.pack("<I", len(case["write"])))
        for a, data in case["write"]:
            b.append(struct.pack("<II", a, len(data)) + bytes(data))
        b.append(struct.pack("<I", len(case["dump"])))
        for a, n in case["dump"]:
            b.append(struct.pack("<II", a, n))
        try:
            self.p.stdin.write(b"".join(b))
            self.p.stdin.flush()
            hdr = self.p.stdout.read(8 + 32 + WIRE_FPU.size + 128 + 4)
            if len(hdr) < 8 + 32 + WIRE_FPU.size + 132:
                raise BrokenPipeError
        except (BrokenPipeError, OSError):
            self.p.kill()
            self.start()
            return {"status": "abnormal", "info": "runner crashed"}
        status, info = struct.unpack_from("<II", hdr)
        regs = list(struct.unpack_from("<8I", hdr, 8))
        fv = WIRE_FPU.unpack_from(hdr, 40)
        o = 40 + WIRE_FPU.size
        xmm = [int.from_bytes(hdr[o + 16 * k:o + 16 * k + 16], "little") for k in range(8)]
        mxcsr = struct.unpack_from("<I", hdr, o + 128)[0]
        dump = [self.p.stdout.read(n) for _, n in case["dump"]]
        return {"status": ["return", "import", "abnormal", "timeout"][status], "info": info, "regs": regs,
                "fpu": {"st": list(fv[0:8]), "mm": list(fv[8:16]), "cw": fv[16], "sw": fv[17], "top": fv[18],
                        "tag": fv[19]}, "xmm": xmm, "mxcsr": mxcsr, "dump": dump}


# --- cases ------------------------------------------------------------------------------
def make_case(func, seed, ld, mmx):
    R = random.Random(seed)
    img = ld.img
    data_ptrs = [va + R.randrange(0, max(vs - 64, 4)) & ~3 for va, vs in ld.writable] or [SCRATCH_LO]

    def word():
        r = R.random()
        if r < 0.40:
            return SCRATCH_LO + R.randrange(0, SCRATCH_HI - SCRATCH_LO - 256) & ~3
        if r < 0.65:
            return R.randrange(0, 256)
        if r < 0.80:
            return 0
        if r < 0.85:
            return R.choice(data_ptrs)
        if r < 0.90:
            return 0xffffffff
        return R.getrandbits(32)

    scratch = bytearray(SCRATCH_HI - SCRATCH_LO)
    for k in range(0, len(scratch), 4):
        struct.pack_into("<I", scratch, k, word())
    for k in range(R.randrange(0, 8)):     # some strings
        o = R.randrange(0, len(scratch) - 64)
        n = R.randrange(1, 40)
        scratch[o:o + n + 1] = bytes(R.randrange(0x20, 0x7f) for _ in range(n)) + b"\0"
    stack = bytearray(R.getrandbits(8) for _ in range(256)) * ((STACK_HI - STACK_LO) // 256)
    esp = STACK_HI - 0x1000
    struct.pack_into("<I", stack, esp - STACK_LO, SENTINEL)
    for k in range(1, 24):
        struct.pack_into("<I", stack, esp - STACK_LO + 4 * k, word())
    teb = bytearray(0x2000)
    struct.pack_into("<I", teb, 0x00, 0xffffffff)
    struct.pack_into("<I", teb, 0x04, STACK_HI)
    struct.pack_into("<I", teb, 0x08, STACK_LO)
    struct.pack_into("<I", teb, 0x18, TEB)
    struct.pack_into("<I", teb, 0x2c, SCRATCH_LO + 0x100)
    struct.pack_into("<I", teb, 0x30, PEB)
    regs = [word() for _ in range(8)]
    regs[4] = esp
    regs[5] = R.choice([esp + 0x40, word()])
    top = R.randrange(8)
    nvalid = R.randrange(0, 5)
    tag = 0xff
    st = [0.0] * 8
    for k in range(nvalid):
        p = (top + k) & 7
        tag &= ~(1 << p)
        st[p] = R.choice([R.uniform(-1e3, 1e3), float(R.randrange(-100, 100)), R.uniform(0, 1), 0.0,
                          R.uniform(-1e9, 1e9), 0.5, 2.0 ** R.randrange(-30, 30)])
    mm = [R.getrandbits(64) for _ in range(8)]
    if mmx:
        top, tag = 0, 0

    def lane():
        return struct.unpack("<I", struct.pack("<f", R.uniform(-100, 100)))[0] if R.random() < 0.5 else R.getrandbits(32)
    xmm = [sum(lane() << (32 * j) for j in range(4)) for _ in range(8)]
    case = {"func": func, "regs": regs, "mxcsr": 0x1f80, "mmx": mmx,
            "fpu": {"st": st, "mm": mm, "cw": 0x27f, "sw": 0, "top": top, "tag": tag},
            "xmm": xmm,
            "write": [(STACK_LO, stack), (SCRATCH_LO, scratch), (TEB, teb)],
            "dump": [(STACK_LO, STACK_HI - STACK_LO), (SCRATCH_LO, SCRATCH_HI - SCRATCH_LO), (TEB, 0x2000)] +
                    [(va, vs) for va, vs in ld.writable]}
    return case


def feq(a, b, tol):
    if a != a and b != b:
        return True
    if a == b:
        return True
    return tol and abs(a - b) <= tol * max(abs(a), abs(b), 1e-300)


def compare(case, c, u, tol):
    """List of differences (empty: equal), or None when the case is not comparable."""
    if c["status"] == "timeout" or u["status"] == "timeout":
        return None
    if c["status"] == "abnormal" and u["status"] == "abnormal":
        return []
    if c["status"] == "abnormal" and c.get("info") in case["stubs"]:
        return "untranslated"
    d = []
    if c["status"] != u["status"]:
        return ["status %s/%s (%s / %s)" % (c["status"], u["status"], c.get("info"), u.get("info"))]
    if c["status"] == "import" and c["info"] != u["info"]:
        d.append("import %d/%d" % (c["info"], u["info"]))
    adj = 4 if c["status"] == "import" else 0
    names = ["eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi"]
    for k in range(8):
        cv, uv = c["regs"][k], (u["regs"][k] + (adj if k == 4 else 0)) & 0xffffffff
        if cv != uv:
            d.append("%s %08x/%08x" % (names[k], cv, uv))
    for k in range(8):
        if c["xmm"][k] != u["xmm"][k]:
            d.append("xmm%d %032x/%032x" % (k, c["xmm"][k], u["xmm"][k]))
    cf, uf = c["fpu"], u["fpu"]
    if case["mmx_used"]:
        for k in range(8):
            if cf["mm"][k] != uf["mm"][k]:
                d.append("mm%d %016x/%016x" % (k, cf["mm"][k], uf["mm"][k]))
        if cf["top"] != uf["top"]:
            d.append("fpu top %d/%d" % (cf["top"], uf["top"]))
    else:
        if cf["top"] != uf["top"] or cf["tag"] != uf["tag"]:
            d.append("fpu top/tag %d %02x / %d %02x" % (cf["top"], cf["tag"], uf["top"], uf["tag"]))
        else:
            for k in range(8):
                if not (cf["tag"] >> k & 1) and not feq(cf["st"][k], uf["st"][k], tol):
                    d.append("st phys%d %r/%r" % (k, cf["st"][k], uf["st"][k]))
    if (cf["sw"] ^ uf["sw"]) & 0x4500:
        d.append("fpu C3C2C0 %04x/%04x" % (cf["sw"] & 0x4500, uf["sw"] & 0x4500))
    if cf["cw"] != uf["cw"]:
        d.append("fpu cw %04x/%04x" % (cf["cw"], uf["cw"]))
    skip = (u["regs"][4], u["regs"][4] + 4) if adj else (0, 0)
    for (a, n), cb, ub in zip(case["dump"], c["dump"], u["dump"]):
        if cb == ub:
            continue
        diffs = [a + k for k in range(n) if cb[k] != ub[k] and not skip[0] <= a + k < skip[1]]
        if diffs:
            d.append("mem %d bytes from %08x (%02x/%02x)" % (len(diffs), diffs[0], cb[diffs[0] - a], ub[diffs[0] - a]))
    return d


# --- build ------------------------------------------------------------------------------
def build(image, out, jobs, cflags, trace=False):
    cflags = "-DRC32_KEEP_LOADS " + cflags
    gen = os.path.join(out, "gen-trace" if trace else "gen")
    py = sys.executable
    env = dict(os.environ, RC_EMIT_TRACE="1" if trace else "0")
    subprocess.check_call([py, "-I", os.path.join(RECOMP, "build", "gen_image_pe.py"), image, gen,
                           "--jobs", str(jobs), "--opt=-O1", "--cflags=-w " + cflags + (" -DRC_TRACE" if trace else "")],
                          stdout=subprocess.DEVNULL, env=env)
    subprocess.check_call(["ninja", "-C", gen, "-j", str(jobs)], stdout=subprocess.DEVNULL)
    rt = os.path.join(RECOMP, "runtime")
    exe = os.path.join(out, "runner-trace" if trace else "runner")
    subprocess.check_call(["clang", "-arch", "arm64", "-O1", "-w", "-DRC_GUEST32", "-DRC32_NO_FAIL", "-I", rt,
                           os.path.join(HERE, "runner.c")] + [os.path.join(rt, f) for f in RUNTIME] +
                          [os.path.join(gen, "obj", "libgame_rc.a"), "-o", exe])
    return exe, json.load(open(os.path.join(gen, "manifest.json")))


def hook_points(tr, funcs):
    """Addresses where Unicorn's state is aligned with the runtime's model before or after the
    instruction: status-word stores (exception bits), stmxcsr, cpuid, rdtsc. Also which
    functions use MMX and which use transcendental x87 instructions."""
    pts, mmx, trans = set(), set(), set()
    for f in funcs:
        try:
            tr.entry = f
            seen, _ = tr.discover(f)
        except Exception:
            continue
        for a, i in seen.items():
            m = i.mnemonic
            oc = tr.x87_opcode(i)
            nxt = a + i.size
            if oc:
                op, modrm = oc
                reg = (modrm >> 3) & 7
                if (op == 0xdf and modrm == 0xe0) or (modrm < 0xc0 and op == 0xdd and reg == 7):
                    pts.add((a, "fsw", nxt))
                elif modrm < 0xc0 and ((op == 0xdd and reg == 6) or (op == 0xd9 and reg == 6)):
                    pts.add((a, "fenv", nxt))
                if oc in TRANSCENDENTAL:
                    trans.add(f)
                if oc in ((0xd9, 0xf1), (0xd9, 0xf9), (0xd9, 0xfa)):
                    pts.add((a, "ref_x87", nxt))
            if m in ("mov", "push") and any(o.type == x86.X86_OP_REG and i.reg_name(o.reg) in
                                            ("cs", "ds", "es", "ss", "fs", "gs") for o in i.operands[-1:]):
                pts.add((a, "sreg", nxt))
            if m in ("pushfd", "pushf"):
                pts.add((a, "pushf", nxt))
            if m == "stmxcsr":
                pts.add((a, "mxcsr", nxt))
            elif m == "cpuid":
                pts.add((a, "cpuid", nxt))
            elif m == "rdtsc":
                pts.add((a, "rdtsc", nxt))
            if any(o.type == x86.X86_OP_REG and i.reg_name(o.reg).startswith("mm") for o in i.operands):
                mmx.add(f)
    return sorted(pts), mmx, trans


def debug_case(image, args):
    """--debug FUNC:CASE: run one case with function-entry traces on both sides and print where
    the two call sequences part, then both end states."""
    from xlat.x86_32 import Translator32
    from unicorn import UC_HOOK_CODE
    name = os.path.splitext(os.path.basename(image))[0]
    out = os.path.join(args.out, name)
    os.makedirs(out, exist_ok=True)
    exe, man = build(image, out, args.jobs, args.cflags, trace=True)
    f, k = args.debug.split(":")
    f, k = int(f, 16), int(k)
    tr = Translator32(image)
    ok = sorted(int(a, 16) for a, v in man["per_function"].items() if v[0] == "ok")
    pts, mmx, trans = hook_points(tr, ok)
    os.environ["RC32_RUNNER_VERBOSE"] = "1"
    errpath = os.path.join(out, "debug.stderr")
    with open(errpath, "w") as err:
        p = subprocess.Popen([exe, image], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=err)
        runner = Runner.__new__(Runner)
        runner.exe, runner.image, runner.p = exe, image, p
        magic, runner.imp_area, runner.base, runner.size = struct.unpack("<4I", p.stdout.read(16))
        ld = Loaded(tr.img, runner.imp_area)
        uc = Unicorn(ld, runner.imp_area, len(ld.slots), pts, tr.insn)
        utrace = []
        uc.dbg = True
        fset = set(ok)
        uc.u.hook_add(UC_HOOK_CODE, lambda u_, a, s_, d: utrace.append(a) if a in fset else None)
        case = make_case(f, (f << 8) | k, ld, f in mmx)
        case["mmx_used"] = f in mmx
        case["stubs"] = {int(a, 16) for a, v in man["per_function"].items() if v[0] == "stub"}
        c = runner.run(case)
        u = uc.run(case, args.limit)
        p.kill()
    ctrace = [int(ln.split()[1], 16) for ln in open(errpath) if ln.startswith("enter ")]
    other = [ln.strip() for ln in open(errpath) if not ln.startswith("enter ")]
    n = 0
    while n < min(len(ctrace), len(utrace)) and ctrace[n] == utrace[n]:
        n += 1
    print("calls: C %d, Unicorn %d, equal for the first %d" % (len(ctrace), len(utrace), n))
    print("  C       next: %s" % " ".join("%x" % a for a in ctrace[max(0, n - 3):n + 5]))
    print("  Unicorn next: %s" % " ".join("%x" % a for a in utrace[max(0, n - 3):n + 5]))
    print("runner: %s" % "; ".join(other[-5:]))
    print("C:", c["status"], c.get("info"), " ".join("%08x" % v for v in c.get("regs", [])))
    print("U:", u["status"], u.get("info"), " ".join("%08x" % v for v in u.get("regs", [])))
    print("diff:", compare(case, c, u, 1e-12))
    return 0


def run_image(image, args):
    from xlat.x86_32 import Translator32
    name = os.path.splitext(os.path.basename(image))[0]
    out = os.path.join(args.out, name)
    os.makedirs(out, exist_ok=True)
    exe, man = build(image, out, args.jobs, args.cflags)
    tr = Translator32(image)
    ok = sorted(int(a, 16) for a, v in man["per_function"].items() if v[0] == "ok")
    funcs = ok
    if args.only:
        funcs = [int(a, 16) for a in args.only.split(",")]
    elif args.sample and args.sample < len(ok):
        funcs = sorted(random.Random(1).sample(ok, args.sample))
    pts, mmx, trans = hook_points(tr, ok)
    stubs = {int(a, 16) for a, v in man["per_function"].items() if v[0] == "stub"}
    runner = Runner(exe, image)
    ld = Loaded(tr.img, runner.imp_area)
    uc = Unicorn(ld, runner.imp_area, len(ld.slots), pts, tr.insn)
    res = collections.Counter()
    per = {}
    fails = []
    for n, f in enumerate(funcs):
        fr = collections.Counter()
        for k in range(args.cases):
            case = make_case(f, (f << 8) | k, ld, f in mmx)
            case["mmx_used"] = f in mmx
            c = runner.run(case)
            u = uc.run(case, args.limit)
            case["stubs"] = stubs
            d = compare(case, c, u, 1e-12 if f in trans else 0)
            if d is None:
                kind = "timeout"
            elif d == "untranslated":
                kind = "untranslated_reached"
            elif d and u.get("ref_limit"):
                kind = "reference_limit"
            elif d:
                kind = "mismatch"
                if len(fails) < args.max_report:
                    fails.append({"func": "%x" % f, "case": k, "diff": d[:6], "status": u["status"]})
            else:
                kind = {"return": "match_return", "import": "match_import", "abnormal": "both_abnormal"}[c["status"]]
            fr[kind] += 1
        res.update(fr)
        per["%x" % f] = dict(fr)
        if args.verbose and fr["mismatch"]:
            print("  %x: %s" % (f, dict(fr)), file=sys.stderr)
        if (n + 1) % 200 == 0:
            print("  %s: %d/%d functions, %s" % (name, n + 1, len(funcs), dict(res)), file=sys.stderr)
    runner.p.kill()
    mism_funcs = sum(1 for v in per.values() if v.get("mismatch"))
    return {"image": name, "functions_total": man["functions"], "translated": man["counts"].get("ok", 0),
            "stubs": man["counts"].get("stub", 0), "stub_reasons": man["stub_reasons"][:12],
            "instructions_translated": man["instructions"], "discovery": man["discovery"],
            "conventions": man["conventions"], "tested_functions": len(funcs), "cases": dict(res),
            "functions_with_mismatch": mism_funcs, "mismatches": fails, "per_function": per}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("images", nargs="*")
    ap.add_argument("--out", required=True)
    ap.add_argument("--cases", type=int, default=6)
    ap.add_argument("--sample", type=int, default=0)
    ap.add_argument("--only")
    ap.add_argument("--jobs", type=int, default=os.cpu_count())
    ap.add_argument("--limit", type=int, default=200000, help="Unicorn instruction limit per case")
    ap.add_argument("--cflags", default="")
    ap.add_argument("--max-report", type=int, default=200)
    ap.add_argument("--report")
    ap.add_argument("-v", "--verbose", action="store_true")
    ap.add_argument("--debug", help="FUNC:CASE: trace one case (see debug_case)")
    args = ap.parse_args()
    images = args.images or [p for p in os.environ.get("RC32_TEST_DLLS", "").split(os.pathsep) if p]
    images = [p for p in images if os.path.exists(p)]
    if not images:
        print("difftest: no image (pass one or set RC32_TEST_DLLS); skipped")
        return 0
    if args.debug:
        return debug_case(images[0], args)
    reports = [run_image(p, args) for p in images]
    bad = 0
    for r in reports:
        bad += r["cases"].get("mismatch", 0)
        print("%s: %d functions, %d translated, %d stubs; tested %d functions: %s; %d functions with a mismatch"
              % (r["image"], r["functions_total"], r["translated"], r["stubs"], r["tested_functions"],
                 r["cases"], r["functions_with_mismatch"]))
        for m in r["mismatches"][:20]:
            print("   %s/%d [%s] %s" % (m["func"], m["case"], m["status"], "; ".join(m["diff"])))
    if args.report:
        json.dump(reports, open(args.report, "w"), indent=1)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
