#!/usr/bin/env python3
"""Edits and mid points of the 64-bit front end (xlat/core.py with xlat/inner.py, runtime/rc_mid.h) on hand-written
code, with no game image.

The snippets are assembled and linked into a small x86-64 Mach-O (at an address of its own, so the test's runner can
map it where it was linked), translated with edits and mid points given in place of a target's, and checked:

  * edits, against Unicorn: each function's translation run from states made from seeds (test/x64/run64.c) with
    the switches off against Unicorn on the image as linked, and with them on against Unicorn on a copy with the
    new bytes written in (edits of the same length) or on a twin function written with the edited instructions
    (edits that change the length: a disp8 grown to disp32, a RIP-relative operand in a replaced range, a call and
    a branch in replaced code). The edits: a stride (imul), disp8 and disp32 displacements, a register r12-r15
    base and index, a RIP-relative displacement, a loop count, a branch made unconditional, a patch over two
    instructions with a loop jumping to the second;
  * mid points, with hooks against the answers here: values in rax..r15, in xmm registers (float and double),
    memory at a register plus an offset, a pointer value, a byte and a 32-bit value of a 64-bit register, flags, a
    continuation, and a hook that calls guest code, after which the registers it did not set are put back;
  * the table errors (TableError): old bytes the image hasn't, a range that ends inside an instruction, a jump into
    a range, a branch of the edit into its own range, a mid point inside a range, a continuation that is not an
    instruction of the function.

The runner is built twice, with every register through rc_ctx at calls (all sixteen compared) and with
RC_ABI_TRUST as the Mac build compiles (rax and rdx compared, with xmm0, xmm1 and the scratch buffer in both).

Usage (from tools/ws7/recomp): python3 -m test.x64.innercases --out DIR [--cases N]
Skips (exit 0) when clang cannot link x86-64 Mach-O or Unicorn is missing.
"""
import argparse
import hashlib
import os
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
RECOMP = os.path.normpath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, RECOMP)
sys.path.insert(0, os.path.join(RECOMP, "..", "..", "targets"))

BASE = 0x10000000000      # where the image is linked (run64.c maps it there)
BUF, BUF_LEN = 0x10100000000, 512
STACK, STACK_LEN = 0x10200000000, 0x10000
SENTINEL = 0x10300000000

# name, body (Intel syntax). `@x:` marks an address the tables use (a local label, written to a table in __DATA so
# the test can read where it is); functions are the names. _tab is data in __DATA.
CASES = [
    # --- edits of the same length (Unicorn on the patched image)
    ("e_imm", """
        mov rax, rsi
    @e_imm_1:
        imul rax, rsi, 0x1cc
        add rax, 7
        ret"""),
    ("e_disp", """
    @e_disp_1:
        mov eax, dword ptr [rdi + 0x10]
    @e_disp_2:
        lea rdx, [rdi + 0x20]
    @e_disp_3:
        mov rcx, qword ptr [rdi + 0x80]
        add rax, rcx
        ret"""),
    ("e_rex", """
        mov r12, rdi
        mov r13, rsi
        and r13, 7
    @e_rex_1:
        mov eax, dword ptr [r12 + r13*4 + 8]
    @e_rex_2:
        mov r15d, dword ptr [r12 + 0x40]
        add rax, r15
        mov qword ptr [r12 + 0x48], rax
        ret"""),
    ("e_rip", """
    @e_rip_1:
        mov rax, qword ptr [rip + _tab]
        add rax, rsi
        ret"""),
    ("e_count", """
        xor eax, eax
        xor edx, edx
    @e_count_loop:
        add eax, edx
        inc edx
    @e_count_1:
        cmp edx, 6
        jl @e_count_loop
        ret"""),
    ("e_branch", """
        cmp rsi, rdx
    @e_branch_1:
        je @e_branch_eq
        mov eax, 1
        ret
    @e_branch_eq:
        mov eax, 2
        ret"""),
    ("e_split", """
        xor eax, eax
        mov edx, 3
    @e_split_1:
        add eax, 0x10
    @e_split_loop:
        add eax, 1
        dec edx
        jnz @e_split_loop
        ret"""),
    ("e_xmm", """
        movss xmm0, dword ptr [rdi + 0x10]
    @e_xmm_1:
        addss xmm0, dword ptr [rdi + 0x14]
        movss dword ptr [rdi + 0x18], xmm0
        ret"""),
    # --- edits that change the length (Unicorn on the twin, NAME_ref)
    ("v_grow", """
    @v_grow_1:
        mov eax, dword ptr [rdi + 0x10]
        ret"""),
    ("v_grow_ref", """
        mov eax, dword ptr [rdi + 0x90]
        ret"""),
    ("v_grow12", """
        mov r12, rdi
    @v_grow12_1:
        mov rax, qword ptr [r12 + 0x18]
        mov qword ptr [r12 + 0x20], rax
        ret"""),
    ("v_grow12_ref", """
        mov r12, rdi
        mov rax, qword ptr [r12 + 0x118]
        mov qword ptr [r12 + 0x20], rax
        ret"""),
    ("v_rip", """
    @v_rip_1:
        mov eax, 1
        add rax, rsi
        ret"""),
    ("v_rip_ref", """
        mov rax, qword ptr [rip + _tab + 16]
        add rax, rsi
        ret"""),
    ("v_call", """
        mov rax, rsi
    @v_call_1:
        nop
        nop
        nop
        nop
        nop
        add rax, rsi
        ret"""),
    ("v_call_ref", """
        mov rax, rsi
        call _helper
        add rax, 1
        add rax, rsi
        ret"""),
    ("v_jump", """
        mov eax, esi
    @v_jump_1:
        nop
        nop
        mov eax, 1
        ret
    @v_jump_t:
        mov eax, 2
        ret"""),
    ("v_jump_ref", """
        mov eax, esi
        cmp eax, 5
        jb 1f
        mov eax, 1
        ret
    1:
        mov eax, 2
        ret"""),
    # --- mid points (answers in MIDS)
    ("m_reg", """
        mov r13, 5
    @m_reg_1:
        lea rax, [r13 + 1]
        ret"""),
    ("m_xmm", """
        mov eax, 3
        cvtsi2ss xmm1, eax
        mov eax, 4
        cvtsi2sd xmm2, eax
    @m_xmm_1:
        cvttss2si eax, xmm1
        cvttsd2si ecx, xmm2
        shl eax, 8
        add eax, ecx
        ret"""),
    ("m_mem", """
        mov dword ptr [rdi + 0x20], 21
    @m_mem_1:
        mov eax, dword ptr [rdi + 0x20]
        ret"""),
    ("m_ptr", """
        mov dword ptr [rdi + 0x30], 41
        lea r14, [rdi + 0x30]
    @m_ptr_1:
        mov eax, dword ptr [r14]
        ret"""),
    ("m_flags", """
        xor ecx, ecx
        cmp ecx, ecx
    @m_flags_1:
        je @m_flags_eq
        mov eax, 1
        ret
    @m_flags_eq:
        mov eax, 2
        ret"""),
    ("m_cont", """
        mov eax, 7
    @m_cont_1:
        mov eax, 9
    @m_cont_2:
        add eax, 1
        ret"""),
    ("m_byte", """
        mov rax, 0x1122334455667788
    @m_byte_1:
        ret"""),
    ("m_wide", """
        mov r9, -1
    @m_wide_1:
        mov rax, r9
        ret"""),
    ("m_call", """
        mov ecx, 11
        mov r8d, 12
        mov r11d, 13
        mov esi, 4
    @m_call_1:
        lea rax, [rcx + r8]
        add rax, r11
        ret"""),
    ("helper", """
        lea rax, [rsi + rsi*2]
        mov ecx, 0x99
        mov r8d, 0x98
        mov r11d, 0x97
        xorps xmm1, xmm1
        ret"""),
]

# same-length edits: label, length, the edit as edits.map has it (a function of the labels, for the RIP one)
EDITS = [
    ("e_imm_1", 7, lambda lab: "+3=6402"),          # imul rax, rsi, 0x264
    ("e_disp_1", 3, lambda lab: "+2=14"),           # mov eax, [rdi + 0x14]
    ("e_disp_2", 4, lambda lab: "+3=28"),           # lea rdx, [rdi + 0x28]
    ("e_disp_3", 7, lambda lab: "+3=90"),           # mov rcx, [rdi + 0x90] (disp32)
    ("e_rex_1", 5, lambda lab: "+4=18"),            # mov eax, [r12 + r13*4 + 0x18]
    ("e_rex_2", 5, lambda lab: "+4=50"),            # mov r15d, [r12 + 0x50]
    ("e_rip_1", 7, lambda lab: "+3=" + struct.pack("<i", lab["_tab"] + 8 - (lab["e_rip_1"] + 7)).hex()),  # _tab + 8
    ("e_count_1", 3, lambda lab: "+2=08"),          # cmp edx, 8
    ("e_branch_1", 2, lambda lab: "+0=eb"),         # je -> jmp, the same target
    ("e_split_1", 6, lambda lab: "+2=20,+5=02"),    # two adds, a loop jumping to the second: one edit per instruction
    ("e_xmm_1", 5, lambda lab: "+4=1c"),            # addss xmm0, [rdi + 0x1c]
]
# length-changing edits: label, length, edit, the twin Unicorn runs for the switches on
VAR_EDITS = [
    ("v_grow_1", 3, lambda lab: "=8b8790000000", "v_grow_ref"),                   # disp8 0x10 -> disp32 0x90
    ("v_grow12_1", 5, lambda lab: "=498b842418010000", "v_grow12_ref"),           # [r12 + 0x18] -> [r12 + 0x118]
    ("v_rip_1", 5, lambda lab: "=488b05" + struct.pack("<i", lab["_tab"] + 16 - (lab["v_rip_1"] + 7)).hex(),
     "v_rip_ref"),                                                                # mov eax, 1 -> mov rax, [rip + _tab + 16]
    ("v_call_1", 5, lambda lab: "=e8" + struct.pack("<i", lab["helper"] - (lab["v_call_1"] + 5)).hex() + "4883c001",
     "v_call_ref"),                                                               # nops -> call helper; add rax, 1
    ("v_jump_1", 2, lambda lab: "=83f805" + "72%02x" % ((lab["v_jump_t"] - (lab["v_jump_1"] + 5)) & 0xff),
     "v_jump_ref"),                                                               # nops -> cmp eax, 5; jb v_jump_t
]
# name: (label, values, continuations, rax without hooks, rax with them)
MIDS = {
    "m_reg": ("m_reg_1", "(int64_t v@r13)", [], 6, 0x106),
    "m_xmm": ("m_xmm_1", "(float s@xmm1, double d@xmm2)", [], 3 * 256 + 4, 6 * 256 + 14),
    "m_mem": ("m_mem_1", "(int v@[rdi+0x20])", [], 21, 42),
    "m_ptr": ("m_ptr_1", "(uint32_t *p@r14)", [], 41, 42),
    "m_flags": ("m_flags_1", "()", [], 2, 1),
    "m_cont": ("m_cont_1", "()", [("past", "m_cont_2")], 10, 8),
    "m_byte": ("m_byte_1", "(uint8_t lo@rax)", [], 0x1122334455667788, 0x112233445566777f),
    "m_wide": ("m_wide_1", "(int w@r9)", [], 0xffffffffffffffff, 0xffffffff),
    "m_call": ("m_call_1", "(int64_t a@rcx, int n@rsi)", [], 36, 11 + 12 + 12 + 13),
}


def assemble(out):
    """The image and {label: address} (functions, marks, _tab)."""
    marks, src = [], [".intel_syntax noprefix", ".text"]
    for name, body in CASES:
        src += [".globl _%s" % name, ".p2align 4", "_%s:" % name]
        for ln in body.strip("\n").splitlines():
            s = ln.strip()
            if s.startswith("@") and s.endswith(":"):
                marks.append(s[1:-1])
                src.append("L%s:" % s[1:-1])
            else:
                src.append("  " + s.replace("@", "L"))
    src += [".globl _uses_import", "_uses_import:", "  call _abort",   # an import, so the image has __stubs
            ".data", ".p2align 4", "_tab: .quad 0x1111, 0x2222, 0x3333, 0x4444",
            ".globl _marks", "_marks:"] + ["  .quad L%s" % m for m in marks] + ["  .quad _%s" % n for n, _ in CASES]
    s = os.path.join(out, "inner64.s")
    o = os.path.join(out, "inner64.o")
    img = os.path.join(out, "inner64.dylib")
    open(s, "w").write("\n".join(src) + "\n")
    subprocess.check_call(["clang", "-arch", "x86_64", "-c", s, "-o", o])
    subprocess.check_call(["clang", "-arch", "x86_64", "-dynamiclib", "-nostdlib", "-lSystem",
                           "-Wl,-image_base,0x%x" % BASE, "-Wl,-no_fixup_chains", o, "-o", img])
    import xlat.core  # noqa: F401  (puts tools/ws7/vtables, macho.py, on the path)
    import macho
    m = macho.Image(img)
    syms = subprocess.check_output(["nm", "--defined-only", img], text=True).split("\n")
    sym = {p[2]: int(p[0], 16) for p in (x.split() for x in syms) if len(p) == 3}
    names = marks + [n for n, _ in CASES]
    lab = {n: m.u64(sym["_marks"] + 8 * k) for k, n in enumerate(names)}
    lab["_tab"] = sym["_tab"]
    return img, lab, m


def segments(img):
    """[(vmaddr, bytes)] of the image's segments, as run64.c maps them."""
    d = open(img, "rb").read()
    ncmds = struct.unpack_from("<I", d, 16)[0]
    o, out = 32, []
    for _ in range(ncmds):
        cmd, size = struct.unpack_from("<II", d, o)
        if cmd == 0x19:
            name = d[o + 8:o + 24].rstrip(b"\0")
            vm, vs, fo, fs = struct.unpack_from("<QQQQ", d, o + 24)
            if vs and name != b"__PAGEZERO":
                out.append((vm, vs, d[fo:fo + fs]))
        o += size
    return out


def mix(s):
    s = (s + 0x9e3779b97f4a7c15) & (2 ** 64 - 1)
    z = s
    z = ((z ^ (z >> 30)) * 0xbf58476d1ce4e5b9) & (2 ** 64 - 1)
    z = ((z ^ (z >> 27)) * 0x94d049bb133111eb) & (2 ** 64 - 1)
    return s, z ^ (z >> 31)


class Uni:
    def __init__(self, segs, patches=()):
        from unicorn import Uc, UC_ARCH_X86, UC_MODE_64
        from unicorn import x86_const as X
        self.X = X
        u = self.u = Uc(UC_ARCH_X86, UC_MODE_64)
        lo = min(vm for vm, _, _ in segs) & ~0x3fff
        hi = (max(vm + vs for vm, vs, _ in segs) + 0x3fff) & ~0x3fff
        u.mem_map(lo, hi - lo)
        for vm, vs, data in segs:
            u.mem_write(vm, data)
        for a, b in patches:
            u.mem_write(a, b)
        u.mem_map(BUF, 0x4000)
        u.mem_map(STACK, STACK_LEN)
        u.mem_map(SENTINEL, 0x1000)
        self.regs = [getattr(X, "UC_X86_REG_" + r) for r in
                     "RAX RCX RDX RBX RSP RBP RSI RDI R8 R9 R10 R11 R12 R13 R14 R15".split()]

    def run(self, addr, seed):
        X, u = self.X, self.u
        s, r = seed, []
        for _ in range(16):
            s, v = mix(s)
            r.append(v)
        r[4] = STACK + STACK_LEN - 0x100 - 8
        r[7] = BUF
        s, v = mix(s)
        r[6] = v % 9 + 1
        s, v = mix(s)
        r[2] = v % 9 + 1
        buf = b""
        for _ in range(0, BUF_LEN, 8):
            s, v = mix(s)
            buf += struct.pack("<Q", v)
        u.mem_write(BUF, buf)
        u.mem_write(r[4], struct.pack("<Q", SENTINEL))
        for k, v in enumerate(r):
            u.reg_write(self.regs[k], v)
        for k in range(16):
            u.reg_write(getattr(X, "UC_X86_REG_XMM%d" % k), 0)
        u.reg_write(X.UC_X86_REG_EFLAGS, 0x202)
        u.emu_start(addr, SENTINEL, count=100000)
        h = 0xcbf29ce484222325
        for b in bytes(u.mem_read(BUF, BUF_LEN)):
            h = ((h ^ b) * 0x100000001b3) & (2 ** 64 - 1)
        x0 = u.reg_read(X.UC_X86_REG_XMM0) & (2 ** 64 - 1)
        x1 = u.reg_read(X.UC_X86_REG_XMM1) & (2 ** 64 - 1)
        return [u.reg_read(g) for g in self.regs] + [x0, x1, h]


def translate(img, lab, edits, mids, out):
    """gen.c: every function translated with the tables, and run64_lookup."""
    from xlat.core import Translator, PRELUDE
    tr = Translator(img)
    tr.set_inner(edits, mids)
    funcs = [lab[n] for n, _ in CASES]
    body, placed = [], set()
    for f in funcs:
        body += tr.function(f)
        placed |= tr.inner_done
    missing = (set(edits) | set(mids)) - placed
    if missing:
        raise SystemExit("innercases64: not placed: %s" % sorted(hex(a) for a in missing))
    decl = "".join("void F_%x(rc_ctx *c);\n" % f for f in funcs)
    look = ["rc_fn run64_lookup(uint64_t a) {"] + ["  if (a == 0x%xull) return F_%x;" % (f, f) for f in funcs] + \
        ["  return 0;", "}"]
    path = os.path.join(out, "gen.c")
    open(path, "w").write(PRELUDE + decl + "\n".join(body) + "\n" + "\n".join(look) + "\n")
    return path


def mid_table(lab):
    import targets
    L = ['#include "rc_mid.h"', "const rc_mid_name rc_target_mids[] = {"]
    for name, (l_, vals, conts, _, _) in MIDS.items():
        v, _ = targets.parse_mid(vals, "sysv64", name)
        L.append("  {\"%s\", 0x%xull, 0, %d, {%s}, %d, {%s}}," % (
            name, lab[l_], len(v), ", ".join("{\"%s\", '%s', %d, %d}" % (vn, k, loc | (targets.MID_MEM if mem else 0),
                                                                          off) for k, vn, loc, mem, off in v),
            len(conts), ", ".join('"%s"' % c for c, _ in conts)))
    return "\n".join(L + ["  {0},", "};", 'const rc_edit_name rc_target_edits[] = {{"all", 1, 0}, {0}};', ""])


def check_errors(img, lab):
    """Tables the build must refuse."""
    from xlat.common import TableError
    from xlat.core import Translator
    import targets

    def edit(a, n, new, check=None):
        tr = Translator(img)
        return {a: (0, n, check or targets.edit_check(tr.image_bytes(a, n)), [(None, new)], "t")}
    a, loop = lab["e_imm_1"], lab["e_count_loop"]
    bad = [("bytes the check doesn't match", edit(a, 7, b"\x90" * 7, "00000000"), {}, "e_imm"),
           ("a range ending inside an instruction", edit(a, 8, b"\x90" * 8), {}, "e_imm"),
           ("a jump into the range", edit(loop - 2, 4, b"\x90" * 4), {}, "e_count"),
           ("a branch of the edit into its own range", edit(a, 7, b"\xeb\x02" + b"\x90" * 5), {}, "e_imm"),
           ("a mid point inside the range", edit(a, 11, b"\x90" * 11), {a + 7: (0, [], "in")}, "e_imm"),
           ("a continuation that is not an instruction", {}, {a: (0, [a + 1], "cont")}, "e_imm")]
    n = 0
    for what, edits, mids, fn in bad:
        tr = Translator(img)
        tr.set_inner(edits, mids)
        try:
            tr.function(lab[fn])
        except TableError:
            n += 1
            continue
        except Exception as e:  # noqa: BLE001
            print("innercases64: %s: %s: %s, not a TableError" % (what, type(e).__name__, e))
            continue
        print("innercases64: not refused: %s" % what)
    return n == len(bad)


def run(argv):
    p = subprocess.run(argv, text=True, capture_output=True)
    if p.returncode:
        raise SystemExit("innercases64: %s %s: exit %d: %s" % (os.path.basename(argv[0]), argv[2], p.returncode,
                                                               p.stderr.strip()[-500:]))
    return p.stdout.splitlines()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("--cases", type=int, default=16)
    args = ap.parse_args()
    try:
        import unicorn  # noqa: F401
        import capstone  # noqa: F401
    except ImportError as e:
        print("innercases64: %s; skipped" % e)
        return 77  # ctest's skip (SKIP_RETURN_CODE)
    os.makedirs(args.out, exist_ok=True)
    os.environ.setdefault("RC_TARGET", "mac-x64")
    try:
        img, lab, m = assemble(args.out)
    except (subprocess.CalledProcessError, OSError) as e:
        print("innercases64: cannot build an x86-64 Mach-O (%s); skipped" % e)
        return 77
    import targets
    from xlat.core import Translator
    tr = Translator(img)
    edits, patches = {}, []
    for l_, n, ed in EDITS:
        a = lab[l_]
        old = tr.image_bytes(a, n)
        e = ed(lab)
        nn, chk, p = targets.parse_edit("%d:%s" % (n, targets.edit_check(old)), e, l_)
        edits[a] = (0, nn, chk, p, l_)
        patches.append((a, targets.edit_bytes(old, p)))
    for l_, n, ed, _ in VAR_EDITS:
        a = lab[l_]
        nn, chk, p = targets.parse_edit("%d:%s" % (n, targets.edit_check(tr.image_bytes(a, n))), ed(lab), l_)
        edits[a] = (0, nn, chk, p, l_)
    mids = {lab[l_]: (k, [lab[c] for _, c in conts], name)
            for k, (name, (l_, _, conts, _, _)) in enumerate(MIDS.items())}
    ok = check_errors(img, lab)

    gen = translate(img, lab, edits, mids, args.out)
    tab = os.path.join(args.out, "mid_table.c")
    open(tab, "w").write(mid_table(lab))
    rt = os.path.join(RECOMP, "runtime")
    runners = {}
    for kind, flags in (("exact", []), ("trust", ["-DRC_ABI_TRUST"])):
        exe = os.path.join(args.out, "run64-" + kind)
        subprocess.check_call(["clang", "-arch", "arm64", "-O2", "-w", "-I", rt] + flags +
                              [os.path.join(HERE, "run64.c"), gen, tab, os.path.join(rt, "rc_mid.c"), "-o", exe])
        runners[kind] = exe

    segs = segments(img)
    uni_off, uni_on = Uni(segs), Uni(segs, patches)
    twin = {lab[l_]: lab[t] for l_, _, _, t in VAR_EDITS}
    fn_of = {}
    for name, _ in CASES:
        fn_of[name] = lab[name]
    var_fn = {n: next(t for l_, _, _, t in VAR_EDITS if l_.startswith(n + "_")) for n, _ in CASES
              if any(l_.startswith(n + "_") for l_, _, _, _ in VAR_EDITS) and not n.endswith("_ref")}
    names = [n for n, _ in CASES if not n.startswith("m_")]
    counts, changed = {}, set()
    for kind, exe in runners.items():
        for side in ("off", "on"):
            argv = [exe, img, side] + ["%s=%x:%x" % (n, lab[n], (lab[n] << 8) | k)
                                       for n in names for k in range(args.cases)]
            got = run(argv)
            match = bad = 0
            for ln, (n, k) in zip(got, [(n, k) for n in names for k in range(args.cases)]):
                vals = [int(x, 16) for x in ln.split()[1:]]
                seed = (lab[n] << 8) | k
                if side == "on" and n in var_fn:
                    want = uni_off.run(lab[var_fn[n]], seed)
                else:
                    want = (uni_on if side == "on" else uni_off).run(lab[n], seed)
                idx = (list(range(16)) if kind == "exact" else [0, 2]) + [16, 17, 18]
                if side == "on" and any(vals[i] != w for i, w in zip(idx, [uni_off.run(lab[n], seed)[i] for i in idx])):
                    changed.add(n)
                diff = ["%s 0x%x, Unicorn 0x%x" % (("r%d" % i) if i < 16 else ("x0", "x1", "buffer")[i - 16],
                                                   vals[i], want[i]) for i in idx if vals[i] != want[i]]
                if diff:
                    bad += 1
                    if bad <= 8:
                        print("   %s %s: %s/%d: %s" % (kind, side, n, k, "; ".join(diff[:4])))
                else:
                    match += 1
            counts[(kind, side)] = (match, bad)
            ok = ok and not bad and match
    edited = {n for n, _ in CASES if any(l_.startswith(n + "_") for l_, *_ in EDITS + VAR_EDITS)}
    if edited - changed:   # an edit that changes nothing the test sees tests nothing
        print("innercases64: edits on change nothing in %s" % sorted(edited - changed))
        ok = False
    print("innercases64: edits: %d same-length, %d length-changing, %d functions x %d cases: %s" % (
        len(EDITS), len(VAR_EDITS), len(names), args.cases,
        ", ".join("%s %s %d right %d wrong" % (k[0], k[1], v[0], v[1]) for k, v in counts.items())))

    # mid points against the answers
    bad = []
    for kind, exe in runners.items():
        for side in ("off", "hooks"):
            argv = [exe, img, side] + ["%s=%x:1" % (n, lab[n]) for n in MIDS] + ["helper=%x:1" % lab["helper"]]
            # the helper first, so h_call knows it
            argv = argv[:3] + [argv[-1]] + argv[3:-1]
            for ln in run(argv):
                p = ln.split()
                if p[0] == "helper":
                    continue
                want = MIDS[p[0]][3 if side == "off" else 4]
                if int(p[1], 16) != want:
                    bad.append("%s %s %s: rax 0x%x, expected 0x%x" % (kind, side, p[0], int(p[1], 16), want))
                # the hook's call into the guest clobbered r8 and r11; put back (rcx was set: kept)
                if p[0] == "m_call" and kind == "exact" and (int(p[9], 16), int(p[12], 16)) != (12, 13):
                    bad.append("%s %s m_call: r8 0x%s r11 0x%s, expected put back" % (kind, side, p[9], p[12]))
    print("innercases64: mid points: %d, %d answers right%s" % (len(MIDS), 4 * len(MIDS) - len(bad),
                                                                 "".join("\n   " + b for b in bad)))
    ok = ok and not bad
    print("innercases64: %s" % ("ok" if ok else "FAILED"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
