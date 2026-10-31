#!/usr/bin/env python3
"""Hand-written differential cases for the 32-bit front end, with no game image.

Each case is a short snippet of assembly (Intel syntax) exercising one thing the plain DLLs
cover thinly or not at all: an x87 idiom, a flag corner, a calling convention, a string
operation with the direction flag, an MMX sequence. The snippets are assembled with clang,
packed into a minimal PE32 in the output directory (never committed), and run through exactly
the test.x86.difftest comparison (translated C vs Unicorn). The PE's entry and a tiny export
table mark the snippet starts so the normal discovery finds them.

Usage (from tools/ws7/recomp): python3 -m test.x86.handcases --out DIR [--cases N] [--jobs N]
Skips (exit 0) when clang cannot target i386 PE.
"""
import argparse
import os
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
RECOMP = os.path.normpath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, RECOMP)

IMAGE_BASE = 0x02000000  # inside the window's arena, clear of stack/scratch/TEB of difftest

# Each entry: name, body. A `ret N` (or `ret`) ends it. ecx/edx hold the first case's scratch
# pointers (see difftest.make_case: regs come from the seed), so thiscall/fastcall bodies can
# use them; memory operands address the scratch and stack the harness sets up.
CASES = [
    # --- x87: the _ftol2 idiom (store with round-to-zero control word), MSVC's double->int ---
    ("x87_ftol2", """
        push ebp
        mov ebp, esp
        sub esp, 8
        fld qword ptr [ecx]
        fnstcw word ptr [ebp-2]
        movzx eax, word ptr [ebp-2]
        or eax, 0xc00
        mov word ptr [ebp-4], ax
        fldcw word ptr [ebp-4]
        fistp qword ptr [ebp-8]
        fldcw word ptr [ebp-2]
        mov eax, dword ptr [ebp-8]
        mov edx, dword ptr [ebp-4]
        mov esp, ebp
        pop ebp
        ret
    """),
    # --- x87: fnstsw ax; test ah,0x41 after fcom (the compare-and-branch idiom) ---
    ("x87_fcom_sw", """
        fld qword ptr [ecx]
        fld qword ptr [ecx+8]
        fcompp
        fnstsw ax
        test ah, 0x41
        setne al
        movzx eax, al
        ret
    """),
    ("x87_fucomi", """
        fld qword ptr [ecx]
        fld qword ptr [ecx+8]
        fucomip st, st(1)
        fstp st(0)
        setb al
        setnp ah
        movzx eax, al
        ret
    """),
    ("x87_arith", """
        fld qword ptr [ecx]
        fadd qword ptr [ecx+8]
        fmul st, st(0)
        fld1
        faddp st(1), st
        fsqrt
        fstp qword ptr [edx]
        ret
    """),
    ("x87_fscale_round", """
        fld qword ptr [ecx]
        frndint
        fld qword ptr [ecx+8]
        fscale
        fstp st(1)
        fstp qword ptr [edx]
        ret
    """),
    ("x87_fprem", """
        fld qword ptr [ecx]
        fld qword ptr [ecx+8]
        fprem
        fstp qword ptr [edx]
        fstp qword ptr [edx+8]
        ret
    """),
    ("x87_int_load", """
        fild dword ptr [ecx]
        fild word ptr [ecx+4]
        faddp st(1), st
        fistp dword ptr [edx]
        ret
    """),
    # --- flags: adc/sbb chains, the rotate-through-carry and shld ---
    ("flags_adc64", """
        mov eax, dword ptr [ecx]
        mov edx, dword ptr [ecx+4]
        add eax, dword ptr [ecx+8]
        adc edx, dword ptr [ecx+12]
        setc bl
        movzx ebx, bl
        ret
    """),
    ("flags_shld", """
        mov eax, dword ptr [ecx]
        mov edx, dword ptr [ecx+4]
        shld eax, edx, 5
        shrd edx, eax, 3
        pushfd
        pop ebx
        and ebx, 0xd5
        ret
    """),
    ("flags_rcr", """
        mov eax, dword ptr [ecx]
        stc
        rcr eax, 1
        rcl eax, 3
        setc dl
        movzx edx, dl
        ret
    """),
    ("flags_mul_imul", """
        mov eax, dword ptr [ecx]
        mov edx, dword ptr [ecx+4]
        imul eax, edx
        seto bl
        mov eax, dword ptr [ecx]
        mul edx
        setc bh
        movzx ebx, bx
        ret
    """),
    ("flags_bt", """
        mov eax, dword ptr [ecx]
        mov edx, dword ptr [ecx+4]
        bt eax, edx
        setc bl
        bts dword ptr [ecx], edx
        movzx ebx, bl
        ret
    """),
    # --- conventions: callee-cleaned stdcall / thiscall / fastcall ---
    ("conv_stdcall", """
        push ebp
        mov ebp, esp
        mov eax, dword ptr [ebp+8]
        add eax, dword ptr [ebp+12]
        add eax, dword ptr [ebp+16]
        pop ebp
        ret 12
    """),
    ("conv_thiscall", """
        mov eax, dword ptr [ecx]
        add eax, dword ptr [esp+4]
        ret 4
    """),
    ("conv_fastcall", """
        lea eax, [ecx+edx]
        add eax, dword ptr [esp+4]
        ret 4
    """),
    # --- strings with the direction flag ---
    ("str_movs_fwd", """
        push esi
        push edi
        mov esi, ecx
        mov edi, edx
        mov ecx, 9
        cld
        rep movsd
        pop edi
        pop esi
        ret
    """),
    ("str_movs_bwd", """
        push esi
        push edi
        lea esi, [ecx+32]
        lea edi, [edx+32]
        mov ecx, 8
        std
        rep movsd
        cld
        pop edi
        pop esi
        ret
    """),
    ("str_scas", """
        push edi
        mov edi, ecx
        xor eax, eax
        mov ecx, 64
        cld
        repne scasb
        mov eax, ecx
        pop edi
        ret
    """),
    # --- MMX ---
    ("mmx_padd", """
        movq mm0, qword ptr [ecx]
        movq mm1, qword ptr [ecx+8]
        paddw mm0, mm1
        psubusb mm0, mm1
        punpcklbw mm0, mm1
        movq qword ptr [edx], mm0
        emms
        ret
    """),
    ("mmx_pack", """
        movq mm0, qword ptr [ecx]
        movq mm1, qword ptr [ecx+8]
        packsswb mm0, mm1
        pmaddwd mm0, mm1
        movd eax, mm0
        emms
        ret
    """),
    # --- SSE at 32-bit width ---
    ("sse_scalar", """
        movups xmm0, [ecx]
        movups xmm1, [ecx+16]
        addps xmm0, xmm1
        mulps xmm0, xmm0
        cvtps2dq xmm0, xmm0
        movups [edx], xmm0
        ret
    """),
    ("sse_cvt", """
        cvtsi2sd xmm0, dword ptr [ecx]
        cvtsd2ss xmm0, xmm0
        movss dword ptr [edx], xmm0
        cvttss2si eax, xmm0
        ret
    """),
    # --- loop / jecxz ---
    ("loop_sum", """
        xor eax, eax
        mov edx, ecx
        mov ecx, 16
    L:
        add eax, dword ptr [edx]
        add edx, 4
        loop L
        ret
    """),
]


def build_pe(bodies, out_path):
    """Assemble each body, concatenate at IMAGE_BASE+0x1000, and write a minimal PE32 whose
    export table names each start. Returns {name: va}."""
    asm = [".intel_syntax noprefix", ".text"]
    for name, body in bodies:
        asm.append("    .globl %s" % name)
        asm.append("%s:" % name)
        asm.append(body)
    src = "\n".join(asm)
    with tempfile.TemporaryDirectory() as td:
        s = os.path.join(td, "h.s")
        o = os.path.join(td, "h.o")
        open(s, "w").write(src)
        subprocess.check_call(["clang", "-target", "i386-pc-windows-msvc", "-c", s, "-o", o])
        # read the .text and the symbol offsets from the COFF object
        d = open(o, "rb").read()
        nsect, psym, nsym = struct.unpack_from("<H", d, 2)[0], struct.unpack_from("<I", d, 8)[0], struct.unpack_from("<I", d, 12)[0]
        text_off = text_size = 0
        so = 20
        for _ in range(nsect):
            nm = d[so:so + 8].rstrip(b"\0")
            size, ro = struct.unpack_from("<II", d, so + 16)
            if nm == b".text":
                text_off, text_size = ro, size
            so += 40
        strt = psym + 18 * nsym
        offs = {}
        bodyset = dict(bodies)
        k = 0
        while k < nsym:
            e = psym + 18 * k
            nmraw = d[e:e + 8]
            if nmraw[:4] == b"\0\0\0\0":
                off = struct.unpack_from("<I", nmraw, 4)[0]
                end = d.index(b"\0", strt + off)
                nm = d[strt + off:end].decode("latin-1")
            else:
                nm = nmraw.rstrip(b"\0").decode("latin-1")
            val, secn = struct.unpack_from("<Ih", d, e + 8)
            scl, naux = d[e + 16], d[e + 17]
            if nm in bodyset and scl == 2 and secn > 0:   # external, defined in a section
                offs[nm] = val
            k += 1 + naux                                  # skip the auxiliary records
        code = d[text_off:text_off + text_size]
    starts = {nm: IMAGE_BASE + 0x1000 + off for nm, off in offs.items()}
    return write_pe(code, starts, out_path), starts


def write_pe(code, starts, out_path):
    """A hand-built PE32 DLL: headers, one .text section, an export directory listing starts."""
    base = IMAGE_BASE
    sect_rva = 0x1000
    code = code + b"\xcc" * ((len(code) + 0xf & ~0xf) - len(code))
    names = sorted(starts)
    exp_rva = sect_rva + len(code) + 0x40 & ~0xf
    # export directory: 40 bytes, then EAT, name-pointer table, ordinal table, name strings
    eat = exp_rva + 40
    npt = eat + 4 * len(names)
    ordt = npt + 4 * len(names)
    strs_rva = ordt + 2 * len(names)
    blob = bytearray()
    strtab = bytearray()
    name_rvas = []
    for nm in names:
        name_rvas.append(strs_rva + len(strtab))
        strtab += nm.encode() + b"\0"
    dllname_rva = strs_rva + len(strtab)
    strtab += b"hand.dll\0"
    exp = struct.pack("<IIHHIIIIII", 0, 0, 0, 0, dllname_rva, 1, len(names), len(names), eat, npt) + \
        struct.pack("<I", ordt)
    exp += b"".join(struct.pack("<I", starts[nm] - base) for nm in names)
    exp += b"".join(struct.pack("<I", r) for r in name_rvas)
    exp += b"".join(struct.pack("<H", k) for k in range(len(names)))
    exp += bytes(strtab)
    data = bytearray(b"\0" * 0x400)                # headers (file offset of the section = 0x400)
    # section raw data: code then export table at sect_rva
    sect = bytearray(b"\0" * (exp_rva - sect_rva + len(exp) + 15 & ~15))
    sect[:len(code)] = code
    sect[exp_rva - sect_rva:exp_rva - sect_rva + len(exp)] = exp
    vsize = len(sect)
    # DOS + PE headers
    data[0:2] = b"MZ"
    struct.pack_into("<I", data, 0x3c, 0x80)
    o = 0x80
    data[o:o + 4] = b"PE\0\0"
    struct.pack_into("<HHIIIHH", data, o + 4, 0x14c, 1, 0, 0, 0, 0xe0, 0x2102)  # dll, executable
    opt = o + 24
    struct.pack_into("<HBBIIIIII", data, opt, 0x10b, 0, 0, len(code), 0, 0, starts[names[0]] - base,
                     sect_rva, sect_rva)
    struct.pack_into("<I", data, opt + 28, base)
    struct.pack_into("<II", data, opt + 32, 0x1000, 0x200)       # section / file alignment
    struct.pack_into("<I", data, opt + 56, sect_rva + vsize + 0xfff & ~0xfff)  # SizeOfImage
    struct.pack_into("<I", data, opt + 60, 0x400)                # SizeOfHeaders
    struct.pack_into("<H", data, opt + 68, 2)                    # subsystem
    struct.pack_into("<I", data, opt + 92, 16)                   # NumberOfRvaAndSizes
    struct.pack_into("<II", data, opt + 96, exp_rva, len(exp))   # export directory
    sh = opt + 96 + 16 * 8
    struct.pack_into("<8sIIIIIIHHI", data, sh, b".text", vsize, sect_rva, len(sect), 0x400, 0, 0, 0, 0, 0x60000020)
    out = bytes(data) + bytes(sect)
    open(out_path, "wb").write(out)
    return out_path


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("--cases", type=int, default=12)
    ap.add_argument("--jobs", type=int, default=os.cpu_count())
    ap.add_argument("--report")
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()
    try:
        subprocess.check_output(["clang", "-target", "i386-pc-windows-msvc", "-c", "-x", "assembler", "/dev/null",
                                 "-o", os.devnull], stderr=subprocess.STDOUT)
    except Exception as e:
        print("handcases: clang cannot target i386 PE (%s); skipped" % e)
        return 0
    os.makedirs(args.out, exist_ok=True)
    dll = os.path.join(args.out, "hand.dll")
    _, starts = build_pe(CASES, dll)
    from test.x86 import difftest as D
    D.POINTER_REGS = (1, 2)    # ecx, edx -> scratch, so the snippets dereference valid memory
    da = argparse.Namespace(out=args.out, cases=args.cases, sample=0, only=None, jobs=args.jobs,
                            limit=200000, cflags="", max_report=200, report=args.report, verbose=args.verbose,
                            debug=None)
    r = D.run_image(dll, da)
    c = r["cases"]
    print("handcases: %d snippets, %d cases: %s" % (len(starts), sum(c.values()), c))
    for m in r["mismatches"][:40]:
        print("   %s/%d [%s] %s" % (m["func"], m["case"], m["status"], "; ".join(m["diff"])))
    if args.report:
        import json
        json.dump(r, open(args.report, "w"), indent=1)
    return 1 if c.get("mismatch") else 0


if __name__ == "__main__":
    sys.exit(main())
