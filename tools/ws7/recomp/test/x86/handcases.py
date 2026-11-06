#!/usr/bin/env python3
"""Hand-written differential cases for the 32-bit front end, with no game image.

Each case is a short snippet of assembly (Intel syntax) exercising one thing the plain DLLs
cover thinly or not at all: an x87 idiom, a flag corner, a calling convention, a string
operation with the direction flag, an MMX sequence. The snippets are assembled with clang,
packed into a minimal PE32 in the output directory (never committed), and run through exactly
the test.x86.difftest comparison (translated C vs Unicorn). The PE's entry and a tiny export
table mark the snippet starts so the normal discovery finds them.

The wr_* cases are import calls the way VMProtect leaves them (`call stub` and a junk byte,
`push reg; call stub`, the jmp forms), with stubs Unicorn runs: each fixes the stack the way
the original `call/jmp [slot]` would have left it and jumps through the IAT (a small import
table in the PE). The translation uses the wrapper table test/x86/wrappers.tsv, whose
addresses are labels of the snippets or hex (resolved into DIR/wrappers.tsv).

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
DATA_START = "__data_code"   # the label that starts DATA_CASES
WRAPPERS = os.path.join(HERE, "wrappers.tsv")
# the PE's import table: dll, then the names, whose IAT is the label iat_<name> (DATA_CASES)
IMPORTS = ("kernel32.dll", ["GetTickCount", "GetVersion", "Sleep"])

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
    # flags left by the callee, as msvcr100's x87 helpers answer in ZF
    ("flags_callee", """
        mov eax, 7
        call flags_callee_zero
        je 1f
        mov eax, 8
    1:
        mov ecx, 7
        call flags_callee_nonzero
        jne 2f
        add eax, 16
    2:
        cmp ecx, 7
        call flags_callee_keeps
        jne 3f
        add eax, 32
    3:
        ret
    """),
    # the same with what leaves the flags alone before the jump: steam_api's CRT, `call; push edx; wait; fnstcw; je`
    ("flags_callee_later", """
        mov eax, 0
        call flags_callee_zero
        push edx
        wait
        fnstcw word ptr [esp]
        je 1f
        mov eax, 1
    1:
        pop edx
        ret
    """),
    ("flags_callee_keeps", """
        mov edx, 5
        ret
    """),
    ("flags_callee_zero", """
        test eax, 0
        mov edx, 3
        ret
    """),
    ("flags_callee_nonzero", """
        cmp ecx, 0
        mov edx, 4
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
    # --- MMX <-> SSE conversions (cvtpi2ps/pd, cvt(t)ps2pi, cvt(t)pd2pi) ---
    ("mmx_cvt_ps", """
        movups xmm0, [ecx]
        movups xmm1, [ecx+16]
        cvtps2pi mm0, xmm0
        cvttps2pi mm1, xmm1
        movq qword ptr [edx], mm0
        movq qword ptr [edx+8], mm1
        cvtpi2ps xmm1, mm0
        movups [edx+16], xmm1
        movd eax, mm1
        emms
        ret
    """),
    ("mmx_cvt_pd", """
        cvtdq2pd xmm0, qword ptr [ecx]
        cvtdq2pd xmm1, qword ptr [ecx+8]
        mulpd xmm0, xmm1
        cvtpd2pi mm0, xmm0
        cvttpd2pi mm1, xmm1
        movq qword ptr [edx], mm0
        paddd mm0, mm1
        cvtpi2pd xmm2, mm0
        movupd [edx+8], xmm2
        movd eax, mm0
        emms
        ret
    """),
    # --- AES-NI ---
    ("aes_enc_dec", """
        movdqu xmm0, [ecx]
        movdqu xmm1, [ecx+16]
        aesenc xmm0, xmm1
        aesenc xmm0, [ecx+32]
        aesenclast xmm0, xmm1
        movdqu [edx], xmm0
        aesdec xmm0, xmm1
        aesdeclast xmm0, [ecx+16]
        movdqu [edx+16], xmm0
        ret
    """),
    ("aes_key", """
        movdqu xmm1, [ecx]
        aeskeygenassist xmm0, xmm1, 0x1b
        aeskeygenassist xmm2, [ecx+16], 0x80
        aesimc xmm3, xmm1
        aesimc xmm1, [ecx+32]
        movdqu [edx], xmm0
        movdqu [edx+16], xmm2
        movdqu [edx+32], xmm3
        ret
    """),
    # --- code in a non-executable section (an image rebuilt from a memory dump): called, and
    # jumped to (a tail call) ---
    ("data_code_call", """
        push esi
        mov esi, ecx
        push dword ptr [esi]
        push dword ptr [esi+4]
        call dc_mix
        add eax, dword ptr [esi+8]
        pop esi
        ret
    """),
    ("data_code_tail", """
        mov eax, dword ptr [ecx+4]
        test eax, eax
        js dc_neg
        add eax, 7
        ret
    """),
    # --- discovery from relocated words in a code section (Microsoft's DLLs merge .rdata into
    # .text): a function only a table in code points to is a start of its own, not swallowed by
    # a gap piece starting at the non-pointer word before it (0x01010101: add [ecx], eax twice);
    # the case labels of a switch table in code are not function starts (check_discovery) ---
    ("disc_ptr_user", """
        mov eax, dword ptr [disc_tab]
        ret
    disc_tab:
        .long disc_vfunc
        .long 0x01010101
    disc_vfunc:
        lea eax, [ecx+1]
        ret
    """),
    ("disc_switch", """
        mov eax, dword ptr [esp+4]
        and eax, 3
        cmp eax, 2
        ja disc_c3
        jmp dword ptr [disc_jt + eax*4]
    disc_c0:
        mov eax, 10
        ret
    disc_c1:
        mov eax, 11
        ret
    disc_c2:
        mov eax, 12
        ret
    disc_c3:
        xor eax, eax
        ret
        .p2align 2, 0xcc
    disc_jt:
        .long disc_c0, disc_c1, disc_c2
    """),
    # --- jump tables with no bound the resolver can use, read from their relocated words (the
    # CRT's hand-written memcpy / memmove): one whose index 0 never jumps (its first word is not
    # a pointer), one indexed back from its end (`neg ecx`) ---
    ("jt_skip0", """
        mov eax, dword ptr [esp+4]
        and eax, 3
        jz jt_s0
        jmp dword ptr [jt_s_tab + eax*4]
    jt_s0:
        xor eax, eax
        ret
    jt_s1:
        mov eax, 31
        ret
    jt_s2:
        mov eax, 32
        ret
    jt_s3:
        mov eax, 33
        ret
        .p2align 2, 0xcc
    jt_s_tab:
        .long 0xcccccccc
        .long jt_s1, jt_s2, jt_s3
    """),
    ("jt_back", """
        mov ecx, dword ptr [esp+4]
        and ecx, 3
        jz jt_b_end
        neg ecx
        jmp dword ptr [jt_b_end + ecx*4]
    jt_b1:
        mov eax, 21
        ret
    jt_b2:
        mov eax, 22
        ret
    jt_b3:
        mov eax, 23
        ret
        .p2align 2, 0xcc
        .long jt_b3, jt_b2, jt_b1
    jt_b_end:
        xor eax, eax
        ret
    """),
    # --- import calls through a packer's stubs (wrappers.tsv; the stubs are in DATA_CASES).
    # A case ends at its import call, so what follows a site only has to translate; the junk
    # byte would make it decode differently (0x62: bound, 0xc4: les) ---
    ("wr_call_junk", """
        push 0x11
        mov eax, 0x1234
    wr_call_junk_site:
        call vm_stub_skip1
        .byte 0x62
    wr_call_junk_resume:
        add esp, 4
        ret
    """),
    ("wr_push_call", """
        mov ecx, 0x5555
        push 0x22
    wr_push_call_site:
        push ecx
        call vm_stub_push_ecx
    wr_push_call_resume:
        add esp, 4
        ret
    """),
    ("wr_xchg", """
        push 0x33
        mov eax, 0x4321
    wr_xchg_site:
        call vm_stub_xchg
        .byte 0xc4
    wr_xchg_resume:
        add eax, 2
        add esp, 4
        ret
    """),
    # a stub that pushes the call's arguments itself (pushes column): the site pops a word the
    # stub puts back, then the stub pushes eax + 1 and a constant
    ("wr_args", """
        mov eax, 0x2468
        mov ebx, 0x1357
        push 0x44
        pop ebx
    wr_args_site:
        call vm_stub_args
        .byte 0x62
    wr_args_resume:
        add esp, 8
        ret
    """),
    # a stub standing for `mov esi, [imp]` (kind load): the code calls esi itself
    ("wr_load", """
        push 0x66
    wr_load_site:
        call vm_stub_load
        .byte 0x62
    wr_load_resume:
        call esi
        ret
    """),
    ("wr_jmp", """
        mov eax, dword ptr [ecx]
    wr_jmp_site:
        call vm_stub_tail
        .byte 0x62
        int3
    """),
    ("wr_push_jmp", """
        mov edx, 0x7777
    wr_push_jmp_site:
        push edx
        call vm_stub_push_tail
        int3
    """),
    ("wr_own_slot", """
        push 0x44
    wr_own_slot_site:
        call vm_stub_qpc
        .byte 0xc4
    wr_own_slot_resume:
        add esp, 4
        ret
    """),
    ("wr_unresolved", """
        push 0x55
    wr_unresolved_site:
        call vm_stub_unknown
        .byte 0x62
    wr_unresolved_resume:
        add esp, 4
        ret
    """),
]

# Bodies placed in the image's second section, which is not executable (the PE says data); not
# exported, so only the calls and jumps above lead to them.
DATA_CASES = [
    ("dc_mix", """
        mov eax, dword ptr [esp+4]
        mov edx, dword ptr [esp+8]
        imul eax, edx
        xor eax, 0x5a5a
        rol eax, 3
        ret 8
    """),
    ("dc_neg", """
        neg eax
        shl eax, 1
        ret
    """),
    # the IAT of IMPORTS
    ("iat_GetTickCount", ".long 0"),
    ("iat_GetVersion", ".long 0"),
    ("iat_Sleep", ".long 0"),
    # packer stubs (wr_* cases): each leaves the stack as the original instruction would have
    # and jumps to the import. call + junk byte: step the return address over the junk
    ("vm_stub_skip1", """
        add dword ptr [esp], 1
        jmp dword ptr [iat_GetTickCount]
    """),
    # push ecx; call: the return address into ecx, ecx back, the return address on top
    ("vm_stub_push_ecx", """
        pop ecx
        xchg dword ptr [esp], ecx
        jmp dword ptr [iat_Sleep]
    """),
    # call + junk byte, the return address stepped through xchg [esp], reg
    ("vm_stub_xchg", """
        xchg dword ptr [esp], eax
        lea eax, [eax+1]
        xchg dword ptr [esp], eax
        jmp dword ptr [iat_GetVersion]
    """),
    # pushes ebx (back where the return address was), eax + 1 and 0x55, then the return address
    # stepped over the junk byte; leaves the import's address in esi (sets column)
    ("vm_stub_args", """
        sub esp, 12
        mov dword ptr [esp+4], 0x55
        mov dword ptr [esp+8], eax
        add dword ptr [esp+8], 1
        push dword ptr [esp+12]
        pop dword ptr [esp]
        add dword ptr [esp], 1
        mov dword ptr [esp+12], ebx
        mov esi, dword ptr [iat_Sleep]
        jmp dword ptr [iat_Sleep]
    """),
    # loads esi with the import and returns over the junk byte
    ("vm_stub_load", """
        mov esi, dword ptr [iat_Sleep]
        add dword ptr [esp], 1
        ret
    """),
    # jmp forms: drop the return address (and the pushed register)
    ("vm_stub_tail", """
        lea esp, [esp+4]
        jmp dword ptr [iat_Sleep]
    """),
    ("vm_stub_push_tail", """
        lea esp, [esp+4]
        pop edx
        jmp dword ptr [iat_GetTickCount]
    """),
    # an import the IAT lacks: its slot is its lowest stub, vm_stub_qpc0 (never run; the
    # loader stores the import pointer there), which vm_stub_qpc jumps through
    ("vm_stub_qpc0", ".long 0xcccccccc"),
    ("vm_stub_qpc", """
        add dword ptr [esp], 1
        jmp dword ptr [vm_stub_qpc0]
    """),
    # a site of vm_stub_qpc0 nothing runs
    ("wr_qpc0_site", """
        call vm_stub_qpc0
        .byte 0x62
    wr_qpc0_resume:
        ret
    """),
    # an import whose only stub is outside the image (as in a dump): its slot is its site,
    # which nothing runs either
    ("wr_far_site", """
        .byte 0xe8
        .long 0
        .byte 0x62
    wr_far_resume:
        ret
    """),
    # a stub the table could not resolve: the reference faults, the translation fails by name
    ("vm_stub_unknown", "ud2"),
]

# Discovery of code that only a pointer table in the code section leads to (Microsoft's DLLs
# merge .rdata into .text): its own PE; check_code_ptrs. The exports are
# the seeds; cp_init, cp_virt1 and cp_virt2 are reached only through the tables after cp_ctor.
CP_CASES = [
    # a switch whose jump table sits in the code section: its words are not functions
    ("cp_switch", """
        mov eax, dword ptr [esp+4]
        cmp eax, 2
        ja cp_sw_def
        jmp dword ptr [cp_sw_tab + eax*4]
    cp_sw_0:
        mov eax, 10
        ret
    cp_sw_1:
        mov eax, 11
        ret
    cp_sw_2:
        mov eax, 12
        ret
    cp_sw_def:
        xor eax, eax
        ret
        .p2align 2, 0xcc
    cp_sw_tab:
        .long cp_sw_0, cp_sw_1, cp_sw_2
    """),
    # _initterm over [cp_xi_a, cp_xi_z): both bounds are zero words, not functions
    ("cp_initterm", """
        push esi
        mov esi, offset cp_xi_a
    cp_it_loop:
        cmp esi, offset cp_xi_z
        jae cp_it_done
        mov eax, dword ptr [esi]
        test eax, eax
        je cp_it_next
        call eax
    cp_it_next:
        add esi, 4
        jmp cp_it_loop
    cp_it_done:
        pop esi
        ret
    """),
    # a deleting destructor: a tail jump to the destructor, which the vtable points to too (a
    # function, not a resume point of this one)
    ("cp_deldtor", """
        mov eax, ecx
        jmp cp_dtor
    """),
    # a constructor storing a vtable pointer (a relocated word: data), then the tables and the
    # code only they point to; a pointer to a string in code (data: `insb`)
    ("cp_ctor", """
        mov eax, ecx
        mov dword ptr [eax], offset cp_vtbl
        ret
        .p2align 2, 0xcc
    cp_vtbl:
        .long cp_virt1, cp_virt2, cp_dtor
    cp_strp:
        .long cp_str
    cp_xi_a:
        .long 0
        .long cp_init
    cp_xi_z:
        .long 0
    cp_init:
        mov eax, 1
        ret
        .p2align 4, 0xcc
    cp_virt1:
        mov eax, ecx
        ret
        .p2align 4, 0xcc
    cp_virt2:
        lea eax, [ecx+4]
        ret 4
        .p2align 4, 0xcc
    cp_dtor:
        mov dword ptr [ecx], 0
        ret
        .p2align 4, 0xcc
    cp_str:
        .asciz "Hello, world"
    """),
]


def check_code_ptrs(out):
    """Relocated words in a code section that no resolved jump table claims are function
    candidates, kept unless they decode as data: the initializer an _initterm table points to
    and the virtual functions of a vtable are found, a destructor that another function
    tail-jumps to too; the table bounds, the vtable itself, the case labels and a string are
    not functions."""
    from xlat.x86_32 import Translator32
    labels = {}
    dll = os.path.join(out, "code_ptrs.dll")
    build_pe(CP_CASES, dll, labels=labels)
    tr = Translator32(dll)
    want = {"cp_switch", "cp_initterm", "cp_deldtor", "cp_ctor", "cp_init", "cp_virt1", "cp_virt2", "cp_dtor"}
    data = {"cp_sw_tab", "cp_sw_0", "cp_sw_1", "cp_sw_2", "cp_vtbl", "cp_strp", "cp_xi_a", "cp_xi_z", "cp_str"}
    missing = sorted(n for n in want if labels[n] not in tr.fset)
    wrong = sorted(n for n in data if labels[n] in tr.fset)
    assert not missing and not wrong, "code pointers: missing %s, data taken for code %s" % (missing, wrong)
    seen, _ = tr.discover(labels["cp_switch"])
    assert tr.tables and labels["cp_sw_2"] in seen, "cp_switch: the jump table is not resolved"
    assert not tr.resume, "resume points %s" % dict(tr.resume)
    return len(want)


def build_pe(bodies, out_path, data_bodies=(), imports=None, labels=None):
    """Assemble each body, concatenate at IMAGE_BASE+0x1000, and write a minimal PE32 whose
    export table names each start. data_bodies follow at the next page, which the PE gives a
    second, non-executable section; imports (dll, names) adds an import table whose IAT is the
    label iat_<name> of each. Absolute references to labels are relocated to the image base, and
    the PE lists them as base relocations, as a linker does. Returns {name: va}; with labels (a
    dict), it also gets the va of every label."""
    asm = [".intel_syntax noprefix", ".text"]
    for name, body in bodies:
        asm.append("    .globl %s" % name)
        asm.append("%s:" % name)
        asm.append(body)
    if data_bodies:
        asm += ["    .p2align 12, 0xcc", "    .globl %s" % DATA_START, "%s:" % DATA_START]
        for name, body in data_bodies:
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
        text_off = text_size = text_sec = 0
        rel_off = nrel = 0
        split = None
        so = 20
        for n in range(nsect):
            nm = d[so:so + 8].rstrip(b"\0")
            size, ro, rp = struct.unpack_from("<III", d, so + 16)
            if nm == b".text":
                text_off, text_size, text_sec = ro, size, n + 1
                rel_off, nrel = rp, struct.unpack_from("<H", d, so + 32)[0]
            so += 40
        strt = psym + 18 * nsym
        offs = {}
        syms = {}      # every symbol of .text: index -> value, name -> value
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
            if secn == text_sec:
                syms[k] = syms[nm] = val
            if nm in bodyset and scl == 2 and secn > 0:   # external, defined in a section
                offs[nm] = val
            elif nm == DATA_START and scl == 2 and secn > 0:
                split = val
            k += 1 + naux                                  # skip the auxiliary records
        code = bytearray(d[text_off:text_off + text_size])
        relocs = []
        for k in range(nrel):
            va, si, ty = struct.unpack_from("<IIH", d, rel_off + 10 * k)
            if ty == 6:            # IMAGE_REL_I386_DIR32: an absolute address
                struct.pack_into("<I", code, va, struct.unpack_from("<I", code, va)[0] + IMAGE_BASE + 0x1000 + syms[si])
                relocs.append(va)
            elif ty != 0x14:       # REL32 across sections: there is one
                raise ValueError("relocation type %d" % ty)
    starts = {nm: IMAGE_BASE + 0x1000 + off for nm, off in offs.items()}
    if labels is not None:
        labels.update((nm, IMAGE_BASE + 0x1000 + v) for nm, v in syms.items() if isinstance(nm, str))
    imp = None
    if imports:
        imp = (imports[0], [(nm, syms["iat_" + nm]) for nm in imports[1]])
    return write_pe(bytes(code), starts, out_path, split, imp, relocs), starts


def write_pe(code, starts, out_path, split=None, imports=None, relocs=()):
    """A hand-built PE32 DLL: headers, one .text section, an export directory listing starts.
    With split (a page multiple), the code from split on and the exports are a second section,
    readable data only. imports: (dll, [(name, offset of its IAT word in code)]), an import
    table after the exports. relocs: offsets in code of absolute addresses, a base relocation
    directory (IMAGE_REL_BASED_HIGHLOW) after the rest."""
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
    code = bytearray(code)
    imp, imp_rva = b"", exp_rva + len(exp) + 15 & ~15
    if imports:
        # descriptor, null descriptor, lookup table, hint/name entries, dll name; the IAT
        # words in code get the hint/name RVAs too
        dll, inames = imports
        ilt = imp_rva + 40
        hn = ilt + 4 * (len(inames) + 1)
        hns = bytearray()
        rvas = []
        for nm, off in inames:
            rvas.append(hn + len(hns))
            struct.pack_into("<I", code, off, rvas[-1])
            hns += b"\0\0" + nm.encode() + b"\0"
            hns += b"\0" * (len(hns) & 1)
        dll_rva = hn + len(hns)
        iat = min(off for _, off in inames)
        assert [off for _, off in inames] == [iat + 4 * k for k in range(len(inames))], "IAT words not consecutive"
        imp = struct.pack("<IIIII", ilt, 0, 0, dll_rva, sect_rva + iat) + b"\0" * 20 + \
            b"".join(struct.pack("<I", r) for r in rvas) + b"\0" * 4 + bytes(hns) + dll.encode() + b"\0"
    # base relocations: one block per page, entries (3 << 12 | offset), padded to 4 bytes
    rel, rel_rva = bytearray(), (imp_rva + len(imp) if imp else exp_rva + len(exp)) + 15 & ~15
    pages = {}
    for off in sorted(relocs):
        pages.setdefault(sect_rva + off & ~0xfff, []).append(sect_rva + off & 0xfff)
    for page, offs in sorted(pages.items()):
        ents = [3 << 12 | o for o in offs] + [0] * (len(offs) & 1)   # ABSOLUTE (0) pads the block
        rel += struct.pack("<II", page, 8 + 2 * len(ents)) + b"".join(struct.pack("<H", e) for e in ents)
    data = bytearray(b"\0" * 0x400)                # headers (file offset of the section = 0x400)
    # section raw data: code, export table at exp_rva, import table at imp_rva, relocations
    end = rel_rva + len(rel) if rel else imp_rva + len(imp) if imp else exp_rva + len(exp)
    sect = bytearray(b"\0" * (end - sect_rva + 15 & ~15))
    sect[:len(code)] = code
    sect[exp_rva - sect_rva:exp_rva - sect_rva + len(exp)] = exp
    sect[imp_rva - sect_rva:imp_rva - sect_rva + len(imp)] = imp
    sect[rel_rva - sect_rva:rel_rva - sect_rva + len(rel)] = rel
    vsize = len(sect)
    # DOS + PE headers
    data[0:2] = b"MZ"
    struct.pack_into("<I", data, 0x3c, 0x80)
    o = 0x80
    data[o:o + 4] = b"PE\0\0"
    nsect = 2 if split else 1
    struct.pack_into("<HHIIIHH", data, o + 4, 0x14c, nsect, 0, 0, 0, 0xe0, 0x2102)  # dll, executable
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
    if imp:
        struct.pack_into("<II", data, opt + 104, imp_rva, len(imp))   # import directory
    if rel:
        struct.pack_into("<II", data, opt + 136, rel_rva, len(rel))   # base relocation directory
    sh = opt + 96 + 16 * 8
    if split:
        struct.pack_into("<8sIIIIIIHHI", data, sh, b".text", split, sect_rva, split, 0x400, 0, 0, 0, 0, 0x60000020)
        struct.pack_into("<8sIIIIIIHHI", data, sh + 40, b".rdata", vsize - split, sect_rva + split, len(sect) - split,
                         0x400 + split, 0, 0, 0, 0, 0x40000040)
    else:
        struct.pack_into("<8sIIIIIIHHI", data, sh, b".text", vsize, sect_rva, len(sect), 0x400, 0, 0, 0, 0, 0x60000020)
    out = bytes(data) + bytes(sect)
    open(out_path, "wb").write(out)
    return out_path


def resolve_wrappers(src, labels, dst):
    """The sample wrapper table with its labels replaced by their addresses, written to dst."""
    out = []
    for line in open(src):
        f = line.rstrip("\n").split("\t")
        if line.startswith("#") or not line.strip() or f[0] == "call_site":
            out.append(line.rstrip("\n"))
            continue
        for k in (0, 1, 3):
            if f[k]:
                f[k] = "%x" % labels[f[k]] if f[k] in labels else f[k]
        out.append("\t".join(f))
    open(dst, "w").write("\n".join(out) + "\n")
    return dst


def check_wrappers(dll, path):
    """What the difftest cannot see: without the table a site is still the `call stub` it is
    today; with it, the junk byte is never decoded, the import the IAT lacks has a slot of its
    own, and the unresolved site calls the failure helper."""
    from xlat.x86_32 import Translator32, read_wrappers
    rows = read_wrappers(path)
    plain = Translator32(dll)
    tr = Translator32(dll, wrappers=rows)
    for r in rows:
        if not plain.img.is_code(r["call_site"]):
            continue          # wr_qpc0_site
        i = plain.insn(r["call_site"] + (1 if r["pushed_reg"] and r["kind"] != "load" else 0))
        assert i.mnemonic == "call" and i.operands[0].imm == r["stub"], "0x%x without the table" % r["call_site"]
        w = tr.insn(r["call_site"])
        assert w.mnemonic == {"load": "mov"}.get(r["kind"], r["kind"]) and w.address + w.size == (r["resume"] or r["call_site"] + 6)
    junk = {r["call_site"] + 5 for r in rows if not r["pushed_reg"] or r["kind"] == "load"}
    for f in tr.fsorted:
        seen, _ = tr.discover(f)
        assert not junk & set(seen), "a junk byte decoded in 0x%x" % f
    own = {n: s for s, n in tr.wrap_imports.items()}
    stub = {r["import"]: r["stub"] for r in rows if r["import"]}
    qpc, far = "kernel32.dll!QueryPerformanceCounter", "kernel32.dll!GetSystemTimeAsFileTime"
    assert set(own) == {qpc, far} and own[qpc] < stub[qpc], own
    assert own[far] == next(r["call_site"] for r in rows if r["import"] == far), own
    bad = next(r for r in rows if not r["import"])
    f = max(a for a in tr.fsorted if a <= bad["call_site"])
    assert any("rc32_wrapper_unresolved(c, 0x%xu, 0x%xu)" % (bad["call_site"], bad["stub"]) in s
               for s in tr.function(f)), "no failure helper at 0x%x" % bad["call_site"]


def check_discovery(dll, labels):
    """Starts found from relocated words in code: the function only a table in code points to,
    and none of the case labels of a switch table in code."""
    from xlat.x86_32 import Translator32
    tr = Translator32(dll)
    assert labels["disc_vfunc"] in tr.fset, "disc_vfunc is not a function start"
    jt = {labels[n] for n in ("disc_c0", "disc_c1", "disc_c2")}
    assert all(r in tr.img.relocs for r in range(labels["disc_jt"], labels["disc_jt"] + 12, 4))
    assert not jt & tr.fset, "a case label became a function start"


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
    labels = {}
    _, starts = build_pe(CASES, dll, DATA_CASES, IMPORTS, labels)
    wrappers = resolve_wrappers(WRAPPERS, labels, os.path.join(args.out, "wrappers.tsv"))
    check_wrappers(dll, wrappers)
    check_discovery(dll, labels)
    ncp = check_code_ptrs(args.out)
    print("handcases: code pointers: %d functions found, the tables left as data" % ncp)
    from test.x86 import ehcases
    eh = os.path.join(args.out, "eh.dll")
    print("handcases: exceptions: %d catch continuations in their functions" % ehcases.check(eh, ehcases.build(eh)))
    from test.x86 import difftest as D
    D.POINTER_REGS = (1, 2)    # ecx, edx -> scratch, so the snippets dereference valid memory
    da = argparse.Namespace(out=args.out, cases=args.cases, sample=0, only=None, jobs=args.jobs,
                            limit=200000, cflags="", max_report=200, report=args.report, verbose=args.verbose,
                            debug=None, wrappers=wrappers)
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
