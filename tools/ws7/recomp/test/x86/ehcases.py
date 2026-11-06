#!/usr/bin/env python3
"""A small PE of hand-written exception code, the frames and tables MSVC writes for 32-bit x86:
C++ try/catch (a __ehhandler thunk, FuncInfo, its try and unwind maps, catch funclets that return
the continuation), __except blocks, and setjmp/longjmp through msvcr100.

    python3 -m test.x86.ehcases OUT.dll     (from tools/ws7/recomp): write the PE
    check(OUT.dll)                          what the translator must find in it (handcases runs this)

The cases themselves run against the real CRT in win-x86/crt/tests (crt_probe, probe_eh): every
function takes `int *out`, leaves its marks there and returns a value, which probe_eh checks.
Skips (exit 0) when clang cannot target i386 PE.
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.normpath(os.path.join(HERE, "..", "..")))

from test.x86.handcases import build_pe  # noqa: E402

IMPORTS = ("msvcr100.dll", ["__CxxFrameHandler3", "_CxxThrowException", "_global_unwind2", "_setjmp3", "longjmp"])


def cxx_frame(thunk):
    """The C++ frame: state at ebp-4, the handler at ebp-8, the link at ebp-12, the saved esp at
    ebp-16; the thrown object goes at ebp-20, the catch object at ebp-24."""
    return """
        push ebp
        mov ebp, esp
        push -1
        .byte 0x68  # push offset
        .long %s
        mov eax, dword ptr fs:[0]
        push eax
        sub esp, 32
        lea eax, [ebp-12]
        mov dword ptr fs:[0], eax
        mov dword ptr [ebp-16], esp
""" % thunk


CXX_LEAVE = """
        mov ecx, dword ptr [ebp-12]
        mov dword ptr fs:[0], ecx
        mov esp, ebp
        pop ebp
        ret
"""


def mark(k, v):
    return """
        mov eax, dword ptr [ebp+8]
        mov dword ptr [eax+%d], %s
""" % (4 * k, v)


def throw(v):
    return """
        mov dword ptr [ebp-20], %d
        .byte 0x68  # push offset
        .long ti_int
        lea eax, [ebp-20]
        push eax
        call dword ptr [iat__CxxThrowException]
        int3
""" % v


def state(n):
    return "        mov dword ptr [ebp-4], %d\n" % n


def thunk(name, info):
    return (name, """
        mov eax, offset %s
        jmp dword ptr [iat___CxxFrameHandler3]
    """ % info)


def func_info(name, states, unwind, tries, ntry):
    return (name, """
        .long 0x19930522, %d, %s, %d, %s, 0, 0, 0, 0
    """ % (states, unwind, ntry, tries))


CASES = [
    # the image's entry: the DllMain the loader calls
    ("a_dllmain", """
        mov eax, 1
        ret 12
    """),
    # a throw caught in the same function. Only the catch continuation goes on: no path from the
    # entry reaches eh_basic_cont, which is what makes it look like a function
    ("eh_basic", cxx_frame("ehh_basic") + state(0) + mark(0, 1) + throw(42)),
    ("eh_basic_catch", """
        mov ecx, dword ptr [ebp-24]
    """ + mark(1, "ecx") + """
        mov eax, offset eh_basic_cont
        ret
    eh_basic_cont:
    """ + state(-1) + mark(2, 3) + """
        mov eax, 0x1234
    """ + CXX_LEAVE),
    thunk("ehh_basic", "fi_basic"),
    # a throw from below an inner C++ frame with an object to destroy: the unwind runs the frame's
    # handler (RtlUnwind -> the handler -> back in the CRT), then the catch
    ("eh_nested", cxx_frame("ehh_nested") + state(0) + """
        push dword ptr [ebp+8]
        call eh_mid
        add esp, 4
    """ + mark(5, 99) + """
        mov dword ptr [ebp-4], -1
        xor eax, eax
    """ + CXX_LEAVE),
    ("eh_nested_catch", """
        mov ecx, dword ptr [ebp-24]
    """ + mark(1, "ecx") + """
        mov eax, offset eh_nested_cont
        ret
    eh_nested_cont:
    """ + state(-1) + mark(2, 5) + """
        mov eax, 0x2345
    """ + CXX_LEAVE),
    thunk("ehh_nested", "fi_nested"),
    ("eh_mid", cxx_frame("ehh_mid") + state(0) + mark(0, 1) + throw(7)),
    ("eh_mid_dtor", """
        mov eax, dword ptr [ebp+8]
        add dword ptr [eax+12], 1
        ret
    """),
    thunk("ehh_mid", "fi_mid"),
    # a try inside a catch: its continuation runs in the funclet, the frame live then
    ("eh_incatch", cxx_frame("ehh_incatch") + state(0) + mark(0, 1) + throw(1)),
    ("eh_incatch_outer", """
        mov dword ptr [ebp-16], esp
    """ + state(2) + mark(1, 2) + throw(2) + """
    eh_incatch_inner_cont:
    """ + state(1) + mark(3, 4) + """
        mov eax, offset eh_incatch_cont
        ret
    eh_incatch_cont:
    """ + state(-1) + mark(4, 5) + """
        mov eax, 0x3456
    """ + CXX_LEAVE),
    ("eh_incatch_inner", mark(2, 3) + """
        mov eax, offset eh_incatch_inner_cont
        ret
    """),
    thunk("ehh_incatch", "fi_incatch"),
    # __except around a frame with a handler of its own: the search goes up, the outer handler
    # unwinds through _global_unwind2 and jumps to the block its scope table names, as the
    # CRT's handlers do
    ("seh_outer", """
        push ebp
        mov ebp, esp
        push -1
        .byte 0x68  # push offset
        .long seh_tab
        .byte 0x68  # push offset
        .long seh_handler
        mov eax, dword ptr fs:[0]
        push eax
        mov dword ptr fs:[0], esp
        sub esp, 32
        mov dword ptr [ebp-24], esp
        mov dword ptr [ebp-4], 0
        push dword ptr [ebp+8]
        call seh_mid
        add esp, 4
    """ + mark(5, 99) + """
        mov ecx, dword ptr [ebp-16]
        mov dword ptr fs:[0], ecx
        xor eax, eax
        mov esp, ebp
        pop ebp
        ret
    seh_block:
        mov esp, dword ptr [ebp-24]
        mov dword ptr [ebp-4], -1
    """ + mark(2, 6) + """
        mov ecx, dword ptr [ebp-16]
        mov dword ptr fs:[0], ecx
        mov eax, 0x4567
        mov esp, ebp
        pop ebp
        ret
    """),
    ("seh_filter", """
        mov eax, 1
        ret
    """),
    ("seh_handler", """
        mov eax, dword ptr [esp+4]
        test dword ptr [eax+4], 6
        jne 1f
        mov ebx, dword ptr [esp+8]
        push ebx
        call dword ptr [iat__global_unwind2]
        add esp, 4
        lea ebp, [ebx+16]
        mov eax, dword ptr [ebp-8]
        mov eax, dword ptr [eax+8]
        jmp eax
    1:
        mov eax, 1
        ret
    """),
    ("seh_mid", """
        push ebp
        mov ebp, esp
        push dword ptr [ebp+8]
        .byte 0x68  # push offset
        .long seh_mid_handler
        mov eax, dword ptr fs:[0]
        push eax
        mov dword ptr fs:[0], esp
        sub esp, 32
    """ + mark(0, 1) + """
        mov dword ptr [ebp-20], 9
        .byte 0x68  # push offset
        .long ti_int
        lea eax, [ebp-20]
        push eax
        call dword ptr [iat__CxxThrowException]
        int3
    """),
    ("seh_mid_handler", """
        mov eax, dword ptr [esp+4]
        test dword ptr [eax+4], 6
        je 1f
        mov ecx, dword ptr [esp+8]
        mov ecx, dword ptr [ecx+8]
        add dword ptr [ecx+12], 1
    1:
        mov eax, 1
        ret
    """),
    # setjmp through the IAT: three longjmps back, each from below a frame with something to
    # unwind, then the count; the marks are what setjmp returned
    ("sj_basic", """
        push ebp
        mov ebp, esp
        sub esp, 80
        mov dword ptr [ebp-72], 0
        push 0
        lea eax, [ebp-68]
        push eax
        call dword ptr [iat__setjmp3]
        add esp, 8
        mov ecx, dword ptr [ebp+8]
        mov edx, dword ptr [ebp-72]
        mov dword ptr [ecx+edx*4], eax
        inc dword ptr [ebp-72]
        cmp eax, 3
        je 1f
        add eax, 1
        push dword ptr [ebp+8]
        push eax
        lea eax, [ebp-68]
        push eax
        call sj_deep
        int3
    1:
        mov eax, dword ptr [ebp-72]
        mov esp, ebp
        pop ebp
        ret
    """),
    ("sj_deep", """
        push ebp
        mov ebp, esp
        push -1
        .byte 0x68  # push offset
        .long ehh_deep
        mov eax, dword ptr fs:[0]
        push eax
        sub esp, 32
        lea eax, [ebp-12]
        mov dword ptr fs:[0], eax
        mov dword ptr [ebp-16], esp
        mov dword ptr [ebp-4], 0
        push dword ptr [ebp+12]
        push dword ptr [ebp+8]
        call dword ptr [iat_longjmp]
        int3
    """),
    ("sj_deep_dtor", """
        mov eax, dword ptr [ebp+16]
        add dword ptr [eax+28], 1
        ret
    """),
    thunk("ehh_deep", "fi_deep"),
    # setjmp through a `jmp [slot]` thunk (the game's form), and a longjmp from an inner function
    # with a setjmp of its own back to the outer one
    ("sj_thunked", """
        push ebp
        mov ebp, esp
        sub esp, 80
        push 0
        lea eax, [ebp-68]
        push eax
        call sj_thunk
        add esp, 8
        test eax, eax
        jne 1f
        lea eax, [ebp-68]
        push dword ptr [ebp+8]
        push eax
        call sj_inner
        int3
    1:
        mov edx, eax
    """ + mark(2, "edx") + """
        mov eax, edx
        mov esp, ebp
        pop ebp
        ret
    """),
    ("sj_inner", """
        push ebp
        mov ebp, esp
        sub esp, 80
        push 0
        lea eax, [ebp-68]
        push eax
        call sj_thunk
        add esp, 8
        test eax, eax
        jne 1f
    """ + mark(1, 1).replace("[ebp+8]", "[ebp+12]") + """
        push 8
        lea eax, [ebp-68]
        push eax
        call dword ptr [iat_longjmp]
        int3
    1:
        mov edx, eax
    """ + mark(0, "edx").replace("[ebp+8]", "[ebp+12]") + """
        push 11
        push dword ptr [ebp+8]
        call dword ptr [iat_longjmp]
        int3
    """),
    ("sj_thunk", """
        jmp dword ptr [iat__setjmp3]
    """),
    # not a funclet: a function pointer returned the same way stays a function
    ("not_funclet", """
        mov eax, offset not_funclet_target
        ret
    """),
    ("not_funclet_target", """
        xor eax, eax
        ret
    """),
]

DATA_CASES = [
    ("iat___CxxFrameHandler3", ".long 0"),
    ("iat__CxxThrowException", ".long 0"),
    ("iat__global_unwind2", ".long 0"),
    ("iat__setjmp3", ".long 0"),
    ("iat_longjmp", ".long 0"),
    # throw int
    ("ti_int", ".long 0, 0, 0, cta_int"),
    ("cta_int", ".long 1, ct_int"),
    ("ct_int", ".long 1, td_int, 0, -1, 0, 4, 0"),
    ("td_int", """
        .long 0, 0
        .asciz ".H"
        .p2align 2
    """),
    func_info("fi_basic", 2, "uw_basic", "try_basic", 1),
    ("uw_basic", ".long -1, 0, -1, 0"),
    ("try_basic", ".long 0, 0, 1, 1, hs_basic"),
    ("hs_basic", ".long 0, td_int, -24, eh_basic_catch"),
    func_info("fi_nested", 2, "uw_basic", "try_nested", 1),
    ("try_nested", ".long 0, 0, 1, 1, hs_nested"),
    ("hs_nested", ".long 0, td_int, -24, eh_nested_catch"),
    func_info("fi_mid", 1, "uw_mid", "0", 0),
    ("uw_mid", ".long -1, eh_mid_dtor"),
    func_info("fi_incatch", 4, "uw_incatch", "try_incatch", 2),
    ("uw_incatch", ".long -1, 0, -1, 0, 1, 0, 1, 0"),
    ("try_incatch", """
        .long 2, 2, 3, 1, hs_incatch_inner
        .long 0, 0, 3, 1, hs_incatch_outer
    """),
    ("hs_incatch_inner", ".long 0, 0, 0, eh_incatch_inner"),
    ("hs_incatch_outer", ".long 0, 0, 0, eh_incatch_outer"),
    func_info("fi_deep", 1, "uw_deep", "0", 0),
    ("uw_deep", ".long -1, sj_deep_dtor"),
    ("seh_tab", ".long -1, seh_filter, seh_block"),
]

def build(path):
    labels = {}
    build_pe(CASES, path, DATA_CASES, IMPORTS, labels)
    return labels


def check(path, labels):
    """What the discovery must find: each continuation in the body and the resume points of the
    function that is live when it runs, none a function; the return of each setjmp a resume point."""
    from xlat.x86_32 import Translator32
    tr = Translator32(path)
    L = labels

    def resume_of(f):
        tr.entry = f
        tr.discover(f)
        return set(tr.resume_ips)

    conts = {L[n]: L[f] for n, f in (("eh_basic_cont", "eh_basic"), ("eh_nested_cont", "eh_nested"),
                                     ("eh_incatch_cont", "eh_incatch"), ("eh_incatch_inner_cont", "eh_incatch_outer"))}
    for c, f in conts.items():
        assert c not in tr.fset, "continuation 0x%x is a function start" % c
        assert c in resume_of(f), "continuation 0x%x is not a resume point of 0x%x" % (c, f)
    assert L["seh_block"] in resume_of(L["seh_outer"])
    assert L["not_funclet_target"] in tr.fset, "a pointer returned by a plain function was taken for a continuation"
    for f, n in (("sj_basic", 1), ("sj_thunked", 1), ("sj_inner", 1)):
        assert len(resume_of(L[f])) == n, "the setjmp of %s is not a resume point" % f
    text = "\n".join(tr.function(L["sj_thunked"]))
    assert "RC32_ICALL(c, tgt)" in text and "F_%x(c)" % L["sj_thunk"] not in text, "a setjmp call through the import thunk"
    return len(conts)


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    try:
        subprocess.check_output(["clang", "-target", "i386-pc-windows-msvc", "-c", "-x", "assembler", "/dev/null",
                                 "-o", os.devnull], stderr=subprocess.STDOUT)
    except Exception as e:
        print("ehcases: clang cannot target i386 PE (%s); skipped" % e)
        return 0
    build(sys.argv[1])
    return 0


if __name__ == "__main__":
    sys.exit(main())
