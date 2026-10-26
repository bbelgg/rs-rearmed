"""Register tables and constants shared by the translator modules."""

REG64 = ["rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi",
         "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15"]


def _regtab():
    t = {}
    legacy = {"rax": ("eax", "ax", "al", "ah"), "rcx": ("ecx", "cx", "cl", "ch"),
              "rdx": ("edx", "dx", "dl", "dh"), "rbx": ("ebx", "bx", "bl", "bh"),
              "rsp": ("esp", "sp", "spl", None), "rbp": ("ebp", "bp", "bpl", None),
              "rsi": ("esi", "si", "sil", None), "rdi": ("edi", "di", "dil", None)}
    for r in REG64:
        t[r] = (r, 8, 0)
        if r in legacy:
            d, w, b, h = legacy[r]
        else:
            d, w, b, h = r + "d", r + "w", r + "b", None
        t[d] = (r, 4, 0)
        t[w] = (r, 2, 0)
        t[b] = (r, 1, 0)
        if h:
            t[h] = (r, 1, 8)
    return t


REGS = _regtab()
CTYPE = {1: "uint8_t", 2: "uint16_t", 4: "uint32_t", 8: "uint64_t"}
STYPE = {1: "int8_t", 2: "int16_t", 4: "int32_t", 8: "int64_t"}
MASK = {1: "0xffull", 2: "0xffffull", 4: "0xffffffffull", 8: "0xffffffffffffffffull"}
COND = {
    "o": "of", "no": "!of", "b": "cf", "ae": "!cf", "e": "zf", "ne": "!zf",
    "be": "(cf | zf)", "a": "!(cf | zf)", "s": "sf", "ns": "!sf", "p": "pf", "np": "!pf",
    "l": "(sf != of)", "ge": "(sf == of)", "le": "(zf | (sf != of))", "g": "!(zf | (sf != of))",
}
NOPS = {"nop", "endbr64"}
NORETURN = {"_exit", "_abort", "___cxa_throw", "___cxa_rethrow", "__Unwind_Resume",
            "___stack_chk_fail", "___assert_rtn", "_longjmp", "_siglongjmp", "__exit",
            "__ZSt9terminatev", "___cxa_bad_cast", "___cxa_bad_typeid", "_pthread_exit",
            "__ZSt17__throw_bad_allocv", "__ZSt20__throw_length_errorPKc",
            "__ZSt20__throw_out_of_rangePKc", "__ZSt19__throw_logic_errorPKc",
            "___cxa_call_unexpected", "__ZSt24__throw_out_of_range_fmtPKcz"}


class Unsupported(Exception):
    pass
