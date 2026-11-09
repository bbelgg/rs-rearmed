#!/usr/bin/env python3
"""Edits and mid points (xlat/inner.py, runtime/rc_mid.h) on hand-written code, with no game image.

The snippets are assembled into a minimal PE32 (test.x86.handcases.build_pe), translated with edits and mid points
given in place of a target's (gen_image_pe.py --inner), and checked:

  * edits, against Unicorn (test.x86.difftest): the translation with the switches off against the image as built,
    with them on (the runner's RC32_RUNNER_EDITS) against a copy with the new bytes written in. One instruction
    whose numbers change (a stride, a displacement, an x87 operand, a count) is the merged form, a branch made
    unconditional or taken out and a range of other instructions the two-branch form, a patch over two
    instructions with a jump to the second one edit per instruction;
  * mid points, by test/x86/midrun.c: each snippet's answer without hooks and with the hooks it has there (a value
    in a register, in memory and in st0 changed, a byte register, a pointer value, a continuation taken, a flag
    changed), against the answers written here;
  * the table errors: old bytes the image hasn't, a range that ends inside an instruction, a jump into a range.

Usage (from tools/ws7/recomp): python3 -m test.x86.innercases --out DIR [--cases N] [--jobs N]
Skips (exit 0) when clang cannot target i386 PE.
"""
import argparse
import collections
import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
RECOMP = os.path.normpath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, RECOMP)
sys.path.insert(0, os.path.join(RECOMP, "..", "..", "targets"))

EDIT_CASES = [
    ("ed_imm", """
        mov eax, ecx
    ed_imm_1:
        imul eax, eax, 0x1cc
        add eax, 7
        ret
    """),
    ("ed_disp", """
    ed_disp_1:
        mov eax, dword ptr [ecx + 0x10]
    ed_disp_2:
        lea edx, [ecx + 0x20]
        ret
    """),
    ("ed_x87", """
    ed_x87_1:
        fld dword ptr [ecx + 0x8]
        fadd dword ptr [ecx + 0xc]
        fstp dword ptr [ecx + 0x10]
        ret
    """),
    ("ed_count", """
        xor eax, eax
        xor edx, edx
    ed_count_loop:
        add eax, edx
        inc edx
    ed_count_1:
        cmp edx, 6
        jl ed_count_loop
        ret
    """),
    ("ed_branch", """
        cmp ecx, edx
    ed_branch_1:
        je ed_branch_eq
        mov eax, 1
        ret
    ed_branch_eq:
        mov eax, 2
        ret
    """),
    ("ed_nop", """
        test ecx, ecx
    ed_nop_1:
        jne ed_nop_t
        mov eax, 5
        ret
    ed_nop_t:
        mov eax, 6
        ret
    """),
    ("ed_split", """
        xor eax, eax
        mov edx, 3
    ed_split_1:
        add eax, 0x10
    ed_split_loop:
        add eax, 1
        dec edx
        jnz ed_split_loop
        ret
    """),
    ("ed_seq", """
        mov edx, ecx
    ed_seq_1:
        mov eax, 1
        add eax, edx
        ret
    """),
]
# label, the range's length, the edit as edits.map has it
EDITS = [
    ("ed_imm_1", 6, "+2=6402"),          # imul eax, eax, 0x264
    ("ed_disp_1", 3, "+2=14"),           # mov eax, [ecx + 0x14]
    ("ed_disp_2", 3, "+2=28"),           # lea edx, [ecx + 0x28]
    ("ed_x87_1", 3, "+2=04"),            # fld dword ptr [ecx + 4]
    ("ed_count_1", 3, "+2=08"),          # cmp edx, 8
    ("ed_branch_1", 2, "+0=eb"),         # je -> jmp, the same target
    ("ed_nop_1", 2, "+0=9090"),          # jne -> nops
    ("ed_seq_1", 5, "=31c0409090"),      # mov eax, 1 -> xor eax, eax; inc eax; nop; nop
    ("ed_split_1", 6, "+2=20,+5=02"),    # two adds, a loop jumping to the second: one edit per instruction
]

# Edits that change the length: the patched image can't show them to Unicorn, so midrun.c runs them (edits on with
# its hooks), against the answers here (ecx = 5): (name, label, length, edit, eax off, eax on)
VAR_CASES = [
    ("var_short", """
        mov eax, 100
    var_short_1:
        add eax, 0x1000
        ret
    """),
    ("var_long", """
        mov eax, ecx
    var_long_1:
        inc eax
        ret
    """),
    ("var_jump", """
        mov eax, ecx
    var_jump_1:
        nop
        nop
        mov eax, 1
        ret
    var_jump_t:
        mov eax, 2
        ret
    """),
]
VAR_EDITS = [
    ("var_short", "var_short_1", 5, lambda a, lab: "=40", 0x1064, 0x65),                 # add eax, 0x1000 -> inc eax
    ("var_long", "var_long_1", 1, lambda a, lab: "=83c020" + "0500030000", 6, 0x325),    # inc eax -> add 0x20, add 0x300
    ("var_jump", "var_jump_1", 2,                                                        # nops -> cmp eax, 5; je var_jump_t
     lambda a, lab: "=83f805" + "74%02x" % ((lab["var_jump_t"] - (a + 5)) & 0xff), 1, 2),
]

MID_CASES = [
    ("mid_add", """
        mov eax, ecx
    mid_add_1:
        add eax, 1
        ret
    """),
    ("mid_skip", """
        mov eax, 7
    mid_skip_1:
        mov eax, 9
    mid_skip_2:
        add eax, 1
        ret
    """),
    ("mid_flags", """
        cmp ecx, ecx
    mid_flags_1:
        je mid_flags_eq
        mov eax, 1
        ret
    mid_flags_eq:
        mov eax, 2
        ret
    """),
    ("mid_mem", """
        sub esp, 8
        mov dword ptr [esp + 4], ecx
    mid_mem_1:
        mov eax, dword ptr [esp + 4]
        add esp, 8
        ret
    """),
    ("mid_x87", """
        fld1
    mid_x87_1:
        fadd st(0), st(0)
        mov eax, ecx
        ret
    """),
    ("mid_ptr", """
    mid_ptr_1:
        mov eax, dword ptr [edx]
        ret
    """),
    ("mid_byte", """
        mov eax, 0x12345678
    mid_byte_1:
        ret
    """),
]
# name: (label, values, continuations (label), answer without hooks, with them: eax, st0)
MIDS = {
    "mid_add": ("mid_add_1", "(int x@eax)", [], (0x6, 0), (0x106, 0)),
    "mid_skip": ("mid_skip_1", "()", [("past", "mid_skip_2")], (10, 0), (8, 0)),
    "mid_flags": ("mid_flags_1", "()", [], (2, 0), (1, 0)),
    "mid_mem": ("mid_mem_1", "(int v@[esp+0x4])", [], (5, 0), (10, 0)),
    "mid_x87": ("mid_x87_1", "(double top@st0)", [], (5, 2.0), (5, 10.0)),
    "mid_ptr": ("mid_ptr_1", "(uint32_t *p@edx)", [], (41, 0), (42, 0)),
    "mid_byte": ("mid_byte_1", "(uint8_t lo@eax)", [], (0x12345678, 0), (0x1234567f, 0)),
}


def image_bytes(dll, a, n):
    from xlat.pe import Image
    img = Image(dll)
    o = img.foff(a)
    return bytes(img.data[o:o + n])


def mid_table(labels):
    """rc_target_mids for midrun.c, as targets.py writes one (its parse_mid, the win32 ABI)."""
    import targets
    L = ['#include "rc_mid.h"', "const rc_mid_name rc_target_mids[] = {"]
    for name, (lab, vals, conts, _, _) in MIDS.items():
        v, _ = targets.parse_mid(vals, "win32", name)
        L.append("  {\"%s\", 0x%xull, 0, %d, {%s}, %d, {%s}}," % (
            name, labels[lab], len(v), ", ".join("{\"%s\", '%s', %d, %d}" % (vn, k, loc | (targets.MID_MEM if mem else 0), off)
                                                 for k, vn, loc, mem, off in v),
            len(conts), ", ".join('"%s"' % c for c, _ in conts)))
    return "\n".join(L + ["  {0},", "};", ""])


def check_errors(dll, labels, out):
    """Tables the build must refuse: each a TableError from the translator."""
    from xlat.common import TableError
    from xlat.x86_32 import Translator32
    import targets

    def edit(a, n, new, check=None):
        return {a: (0, n, check or targets.edit_check(image_bytes(dll, a, n)), [(None, new)], "t")}
    a, c = labels["ed_imm_1"], labels["ed_count"] + 2
    bad = [("bytes the check doesn't match", edit(a, 6, b"\x90" * 6, "00000000"), "ed_imm"),
           ("a range ending inside an instruction", edit(a, 7, b"\x90" * 7), "ed_imm"),
           ("a jump into the range", edit(c, 4, b"\x90" * 4), "ed_count"),
           ("a branch of the edit into its own range", edit(a, 6, b"\xeb\x02\x90\x90\x90\x90"), "ed_imm")]
    n = 0
    for what, edits, fn in bad:
        tr = Translator32(dll)
        tr.set_inner(edits, {})
        try:
            tr.function(labels[fn])
        except TableError:
            n += 1
            continue
        print("innercases: not refused: %s" % what)
    return n == len(bad)


def run_edits(dll, patched, exe, labels, cases, out):
    from test.x86 import difftest as D
    funcs = sorted(labels[n] for n, _ in EDIT_CASES + MID_CASES + VAR_CASES)
    res = {}
    for side, image, env in (("off", dll, None), ("on", patched, dict(os.environ, RC32_RUNNER_EDITS="1"))):
        tr = D.translator(image, argparse.Namespace(wrappers=None))
        pts, mmx, trans, ccset, x87set = D.hook_points(tr, funcs)
        runner = D.Runner(exe, dll, env)    # the runner's image is the one built: only its code is edited
        ld = D.Loaded(tr.img, runner.imp_area, tr.import_slots())
        uc = D.Unicorn(ld, runner.imp_area, len(ld.slots), pts, tr.insn)
        cnt = collections.Counter()
        for f in funcs if side == "off" else sorted(labels[n] for n, _ in EDIT_CASES):
            for k in range(cases):
                case = D.make_case(f, (f << 8) | k, ld, f in mmx)
                case.update(mmx_used=f in mmx, cc=f in ccset, x87=f in x87set, stubs=set())
                c, u = runner.run(case), uc.run(case, 200000)
                d = D.compare(case, c, u, 1e-12 if f in trans or u["trans"] else 0)
                if d is None or d == "untranslated":
                    cnt["other"] += 1
                elif d:
                    cnt["mismatch"] += 1
                    if cnt["mismatch"] <= 10:
                        name = next(n for n, _ in EDIT_CASES + MID_CASES + VAR_CASES if labels[n] == f)
                        print("   edits %s: %s/%d: %s" % (side, name, k, "; ".join(d[:4])))
                else:
                    cnt["match"] += 1
        runner.p.kill()
        res[side] = dict(cnt)
    return res


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("--cases", type=int, default=12)
    ap.add_argument("--jobs", type=int, default=os.cpu_count())
    args = ap.parse_args()
    try:
        subprocess.check_output(["clang", "-target", "i386-pc-windows-msvc", "-c", "-x", "assembler", "/dev/null",
                                 "-o", os.devnull], stderr=subprocess.STDOUT)
    except Exception as e:
        print("innercases: clang cannot target i386 PE (%s); skipped" % e)
        return 0
    from test.x86 import difftest as D
    from test.x86.handcases import build_pe
    os.makedirs(args.out, exist_ok=True)
    dll = os.path.join(args.out, "inner.dll")
    labels = {}
    build_pe(EDIT_CASES + MID_CASES + VAR_CASES, dll, labels=labels)
    edits, patched_bytes = [], bytearray(open(dll, "rb").read())
    import targets
    from xlat.pe import Image
    img = Image(dll)
    for lab, n, edit in EDITS:
        a = labels[lab]
        old = image_bytes(dll, a, n)
        nb = targets.edit_bytes(old, targets.parse_edit("%d:%s" % (n, targets.edit_check(old)), edit, lab)[2])
        edits.append([a, 0, "%d:%s" % (n, targets.edit_check(old)), edit])
        o = img.foff(a)
        patched_bytes[o:o + len(nb)] = nb
    patched = os.path.join(args.out, "inner-edited.dll")
    open(patched, "wb").write(bytes(patched_bytes))
    for name, lab, n, edit, _, _ in VAR_EDITS:
        a = labels[lab]
        edits.append([a, 0, "%d:%s" % (n, targets.edit_check(image_bytes(dll, a, n))), edit(a, labels)])
    mids = [[labels[lab], k, [labels[c] for _, c in conts], name]
            for k, (name, (lab, _, conts, _, _)) in enumerate(MIDS.items())]
    inner = os.path.join(args.out, "inner.json")
    json.dump({"edits": edits, "mids": mids}, open(inner, "w"))
    ok = True

    # the table errors
    if not check_errors(dll, labels, args.out):
        ok = False

    # edits against Unicorn
    exe, man = D.build(dll, os.path.join(args.out, "inner"), args.jobs, "", extra=["--inner", inner])
    stubs = [a for a, v in man["per_function"].items() if v[0] != "ok"]
    if stubs:
        print("innercases: not translated: %s" % stubs)
        ok = False
    res = run_edits(dll, patched, exe, labels, args.cases, args.out)
    print("innercases: edits: %d snippets, %d edits: off %s, on %s" % (len(EDIT_CASES), len(EDITS), res["off"], res["on"]))
    if any(r.get("mismatch") or not r.get("match") for r in res.values()):
        ok = False

    # mid points: midrun.c with the table
    gen = os.path.join(args.out, "inner", "gen")
    rt = os.path.join(RECOMP, "runtime")
    tab = os.path.join(args.out, "mid_table.c")
    open(tab, "w").write(mid_table(labels))
    mr = os.path.join(args.out, "midrun")
    subprocess.check_call(["clang", "-arch", "arm64", "-O1", "-w", "-DRC_GUEST32", "-I", rt, os.path.join(HERE, "midrun.c"), tab]
                          + [os.path.join(rt, f) for f in D.RUNTIME] + [os.path.join(gen, "obj", "libgame_rc.a"), "-o", mr])
    fns = ["%s=%x" % (n, labels[n]) for n in list(MIDS) + [v[0] for v in VAR_EDITS]]
    got = {}
    for side in ("off", "on"):
        for ln in subprocess.check_output([mr, dll, side] + fns, text=True).splitlines():
            n, eax, st0 = ln.split()
            got[(n, side)] = (int(eax, 16), float(st0))
    bad = []
    want_all = dict((n, (off, on)) for n, (_, _, _, off, on) in MIDS.items())
    want_all.update((n, ((off, 0), (on, 0))) for n, _, _, _, off, on in VAR_EDITS)
    for n, (off, on) in want_all.items():
        for side, want in (("off", off), ("on", on)):
            if got.get((n, side)) != (want[0], float(want[1])):
                bad.append("%s %s: %s, expected %s" % (n, side, got.get((n, side)), want))
    print("innercases: mid points and length-changing edits: %d, %d answers right%s" % (len(want_all), 2 * len(want_all) - len(bad),
                                                               "".join("\n   " + b for b in bad)))
    ok = ok and not bad
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
