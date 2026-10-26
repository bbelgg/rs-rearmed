#!/usr/bin/env python3
"""Generate the import thunks of an x86_64 Mach-O image.

    gen_thunks.py IMAGE OUT.c [--report OUT.json] [--check]

For every __stubs slot that is a real import (not a weak instance defined by
the image itself, which xlat/calls.py turns into a direct call) emit
`void I_<slot hex>(rc_ctx *c)`: it reads the x86-64 SysV arguments from the
guest registers and stack, calls the natively bound function of that slot with
the arm64 calling convention and writes rax/rdx/xmm0/xmm1 back.

Signatures come from the macOS SDK headers (hdr_system.m, hdr_gl3.m), the
project's shim headers and protos_extra.c, via clang's AST dump (sigs.py).
Imports without a prototype get the universal thunk (rc_native_generic: six
integer registers + two stack words, eight FP registers) and are listed in the
report. Report categories: typed, callback (function-pointer parameters wrapped with rc_tramp_get),
variadic, sret, special, cxx (libstdc++ members through the untyped bridge), universal, local.

Run with the capstone venv python (xlat.core.Translator provides the stub map).
"""
import argparse
import json
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
from thunks import sigs  # noqa: E402
from thunks.imports import slot_info  # noqa: E402

ROOT = os.path.abspath(os.path.join(HERE, "..", "..", "..", ".."))

# --- printf/scanf family: guest variadic call -> native v* function -----------------
FMT_VARIADIC = {  # name -> (native v-function, kind)
    "printf": ("vprintf", "RC_VA_PRINTF"), "fprintf": ("vfprintf", "RC_VA_PRINTF"),
    "sprintf": ("vsprintf", "RC_VA_PRINTF"), "snprintf": ("vsnprintf", "RC_VA_PRINTF"),
    "asprintf": ("vasprintf", "RC_VA_PRINTF"), "dprintf": ("vdprintf", "RC_VA_PRINTF"),
    "__sprintf_chk": ("__vsprintf_chk", "RC_VA_PRINTF"), "__snprintf_chk": ("__vsnprintf_chk", "RC_VA_PRINTF"),
    "sscanf": ("vsscanf", "RC_VA_SCANF"), "fscanf": ("vfscanf", "RC_VA_SCANF"), "scanf": ("vscanf", "RC_VA_SCANF"),
    "NSLog": ("NSLogv", "RC_VA_NSLOG"),
}
# variadic functions whose extra arguments are a fixed number of integers/pointers:
# name -> C types of the extras (passed after the `...` so arm64 puts them on the stack)
EXTRA_VARIADIC = {"open": ["int"], "openat": ["int"], "fcntl": ["long"], "ioctl": ["long"],
                  "sem_open": ["int", "unsigned"]}

# --- hooks that rewrite callback-carrying structs ------------------------------------
HOOKS = {
    # struct sigaction {handler; mask; flags}: wrap the handler in the copy we pass down
    "sigaction": lambda p: [
        "uint64_t sa_tmp[2];",
        "if (%s) { memcpy(sa_tmp, %s, 16); sa_tmp[0] = (uint64_t)rc_tramp_get(sa_tmp[0]); %s = sa_tmp; }" % (p[1], p[1], p[1]),
        # the old action's handler is handed back unwrapped (it may be one of our trampolines)
        "extern void rc_unwrap_ptr_end(uint64_t **);",
        "uint64_t *ua_ __attribute__((cleanup(rc_unwrap_ptr_end))) = (uint64_t *)%s;" % p[2]],
    # const AURenderCallbackStruct *: {inputProc, inputProcRefCon}
    "AUGraphSetNodeInputCallback": lambda p: [
        "uint64_t cb_tmp[2];",
        "if (%s) { memcpy(cb_tmp, %s, 16); cb_tmp[0] = (uint64_t)rc_tramp_get(cb_tmp[0]); %s = cb_tmp; }" % (p[3], p[3], p[3])],
    # kAudioUnitProperty_SetRenderCallback (23) / kAudioOutputUnitProperty_SetInputCallback (2005): same struct
    "AudioUnitSetProperty": lambda p: [
        "uint64_t cb_tmp[2];",
        "if ((%s == 23 || %s == 2005) && %s && %s == 16) { memcpy(cb_tmp, %s, 16); "
        "cb_tmp[0] = (uint64_t)rc_tramp_get(cb_tmp[0]); %s = cb_tmp; }" % (p[1], p[1], p[4], p[5], p[4], p[4])],
}
# Steam (the stub shim calls back into these natively):
#   SteamInternal_ContextInit(data): data->pFn(ctx) is a guest function
#   SteamAPI_RegisterCallback/RegisterCallResult(CCallbackBase *): its 3 virtuals
#   (Run, Run(io_failure, call), GetCallbackSizeBytes) are called natively
HOOKS["SteamInternal_ContextInit"] = lambda p: [
    "if (%s) ((uint64_t *)%s)[0] = (uint64_t)rc_tramp_get(((uint64_t *)%s)[0]);" % (p[0], p[0], p[0])]
HOOKS["SteamAPI_RegisterCallback"] = lambda p: [
    "{ extern void rc_shadow_vtable(uint64_t, int); rc_shadow_vtable((uint64_t)%s, 3); }" % p[0]]
HOOKS["SteamAPI_RegisterCallResult"] = HOOKS["SteamAPI_RegisterCallback"]
# Callbacks the native side hands back or stores in structures the game fills in:
#   BinkSetIO(open): the game's open callback fills a BINKIO (shims/bink/bink2_abi.h) with its own
#   ReadHeader, ReadFrame, GetBufferSize, SetInfo, Idle, Close, BGControl (the first 7 fields), which
#   the shim calls: rc_tramp_get_fixfn wraps those after the callback returns
HOOKS["BinkSetIO"] = lambda p: [
    "extern void *rc_tramp_get_fixfn(uint64_t, int);",
    "%s = rc_tramp_get_fixfn((uint64_t)%s, 7);" % (p[0], p[0])]
#   signal(): the old handler comes back to guest code as the guest function it was, not a trampoline
HOOKS["signal"] = lambda p: [
    "extern void rc_unwrap_ret_end(rc_ctx **);",
    "rc_ctx *ur_ __attribute__((cleanup(rc_unwrap_ret_end))) = c;"]
HOOKS["pthread_create"] = lambda p: [
    # translated frames are larger than x86 ones and nest as C frames: give every
    # thread the game creates a big (lazily committed) native stack
    "extern size_t rc_thread_min_stack(void); pthread_attr_t at_tmp; size_t at_ss = 0;",
    "if (%s) memcpy(&at_tmp, %s, sizeof at_tmp); else pthread_attr_init(&at_tmp);" % (p[1], p[1]),
    "pthread_attr_getstacksize(&at_tmp, &at_ss); "
    "if (at_ss < rc_thread_min_stack()) pthread_attr_setstacksize(&at_tmp, rc_thread_min_stack()); "
    "%s = &at_tmp;" % p[1]]
# Every thunk that calls a native function directly reconciles the guest context when the function
# returns (rc_unwind_end, see rc_rt.h): the native function may have unwound over its own callbacks
# with longjmp. The untyped bridge (rc_native_generic) does the same itself. The cost is one
# load and compare, inline (rc_call.h: rc_unwind_guard).
UNWIND_GUARD = ["rc_ctx *ug_ __attribute__((cleanup(rc_unwind_guard))) = c;"]
# Trampoline flags (rc_rt.h RC_TR_*) for a function-pointer parameter, from its type
TR_RET = {("int", 8, False): 1, ("int", 8, True): 2, ("int", 16, False): 3, ("int", 16, True): 4, "bool": 5}
TR_SRET, TR_VA = 16, 32
# Imports that return a native function pointer whose function returns a struct (>16 bytes, not
# a float aggregate) by hidden pointer. The prototype database types such a result as `void *`, so
# the names are listed: the returned address is registered with rc_native_register_sret and guest
# callers of it (rc_icall_native) use x8. Add the Steam/online-object accessors here as they are found.
RETURNS_SRET_FN = set()
# whole-thunk handlers (name -> C body using the guest context `c`)
SPECIAL_BODY = {
    "objc_msgSend_stret": "rc_objc_msgsend_stret(c);",
    "objc_msgSendSuper2_stret": "rc_objc_msgsend_stret(c);",
    # simd_float4x4 __invert_f4(simd_float4x4): 64-byte vector struct, in memory
    # on x86 (argument on the stack, result through rdi), in v0-v3 on arm64
    "__invert_f4": "rc_simd_invert_f4(c);",
}
# objc_msgSend / objc_msgSendSuper2: the callee's real signature is unknown at the call site, and
# arm64's objc_msgSend preserves every argument register: the universal bridge is exact.
UNIVERSAL_OK = {"objc_msgSend", "objc_msgSendSuper2", "objc_msgSend_fpret", "objc_msgSendSuper"}

# libstdc++ functions that return a class by value (hidden result pointer: rdi on x86-64, x8 on arm64)
CXX_SRET = {
    "_ZNKSt15basic_stringbufIcSt11char_traitsIcESaIcEE3strEv": "std::stringbuf::str() const -> std::string",
}
CXX_NAMESPACES = ("std", "__cxxabiv1", "__gnu_cxx", "__gnu_cxx_ldbl128")
CXX_SCALARS = {"bool": ("int", 8, False), "char": ("int", 8, True), "signed char": ("int", 8, True),
               "unsigned char": ("int", 8, False), "short": ("int", 16, True), "unsigned short": ("int", 16, False),
               "int": ("int", 32, True), "unsigned int": ("int", 32, False), "long": ("int", 64, True),
               "unsigned long": ("int", 64, False), "long long": ("int", 64, True),
               "unsigned long long": ("int", 64, False), "wchar_t": ("int", 32, True),
               "float": ("f32", 0, 0), "double": ("f64", 0, 0),
               "std::_Ios_Iostate": ("int", 32, False), "std::_Ios_Openmode": ("int", 32, False),
               "std::_Ios_Fmtflags": ("int", 32, False)}


def ctype(k):
    """C type used in the arm64 function-pointer cast for a classified type."""
    kind = k["kind"]
    if kind == "void":
        return "void"
    if kind == "int":
        return "%sint%d_t" % ("" if k["signed"] else "u", k["bits"])
    if kind == "bool":
        return "_Bool"
    if kind in ("ptr", "fnptr"):
        return "void *"
    if kind == "f32":
        return "float"
    if kind == "f64":
        return "double"
    if kind == "i128":
        return "__int128" if k["signed"] else "unsigned __int128"
    if kind == "vec128":
        return "rc_vec4f"
    if kind == "agg":
        return "struct rc_" + k["name"]
    raise ValueError(kind)


class Gen:
    def __init__(self, image, root=ROOT):
        sys.path.insert(0, os.path.join(HERE, ".."))
        from xlat.core import Translator
        self.tr = Translator(image)
        self.info = slot_info(image)
        self.root = root
        self.P = sigs.Protos()
        self.aggs = {}       # definition text (insertion order = dependency order)
        self.agg_sizes = {}  # record name -> (size, align, 'struct'|'union')
        self.direct = {}     # link-time symbol -> C identifier
        self.thunks = []     # (slot, name, text)
        self.entries = []    # report rows

    # --- prototype database -----------------------------------------------------
    def load_protos(self, headers=()):
        shims = os.path.join(self.root, "shims")
        errs = []
        errs.append(self.P.add_source(os.path.join(HERE, "hdr_system.m"), ["-DGL_GLEXT_PROTOTYPES"], "sdk"))
        errs.append(self.P.add_source(os.path.join(HERE, "protos_extra.c"),
                                      ["-I", os.path.join(shims, "bink"), "-I", os.path.join(shims, "cg", "src")],
                                      "project", lang="c"))
        errs.append(self.P.add_source(os.path.join(HERE, "hdr_gl3.m"), [], "sdk-gl3"))
        for h in headers:
            errs.append(self.P.add_source(os.path.abspath(h), ["-I", os.path.dirname(os.path.abspath(h))], "header", lang="c"))
        return errs

    # --- C++ ------------------------------------------------------------------
    @staticmethod
    def demangle(names):
        out = subprocess.run(["c++filt", "-n"], input="\n".join(names), capture_output=True, text=True).stdout
        return out.splitlines()

    @staticmethod
    def split_top(s):
        parts, depth, cur = [], 0, ""
        for ch in s:
            if ch in "<(":
                depth += 1
            elif ch in ">)":
                depth -= 1
            if ch == "," and depth == 0:
                parts.append(cur.strip())
                cur = ""
            else:
                cur += ch
        if cur.strip():
            parts.append(cur.strip())
        return parts

    def cxx_signature(self, mangled, demangled):
        """(is_member, [param kinds]) or None when a parameter type is not understood."""
        d = demangled
        d = re.sub(r" const$", "", d)
        if not d.endswith(")"):
            return None
        depth = 0
        for k in range(len(d) - 1, -1, -1):
            depth += (d[k] == ")") - (d[k] == "(")
            if depth == 0:
                break
        name, plist = d[:k], d[k + 1:-1]
        base = name
        while True:  # drop template arguments to find the qualifier
            n2 = re.sub(r"<[^<>]*>", "", base)
            if n2 == base:
                break
            base = n2
        qual = base.rsplit("::", 1)[0] if "::" in base else ""
        comps = [c for c in qual.split("::") if c and c not in CXX_NAMESPACES]
        member = bool(comps) and mangled.startswith("_ZN")
        params = []
        for ptxt in ([] if plist.strip() in ("", "void") else self.split_top(plist)):
            t = ptxt.replace("const ", "").replace(" const", "").strip()
            if "(*)" in t or "(*&)" in t:
                params.append(dict(kind="fnptr"))
            elif "*" in t or "&" in t:
                params.append(dict(kind="ptr"))
            elif t in CXX_SCALARS:
                kind, bits, signed = CXX_SCALARS[t]
                params.append(dict(kind=kind) if kind != "int" else dict(kind="int", bits=bits, signed=signed))
            else:
                return None
        return member, params

    # --- emission helpers ---------------------------------------------------------
    def direct_ref(self, sym):
        ident = "rc_ref_" + re.sub(r"\W", "_", sym)
        self.direct[sym] = ident
        return "(uint64_t)&" + ident

    def read_param(self, k, v, ar="ar"):
        kind = k["kind"]
        if kind == "int":
            return "%s %s = (%s)rc_arg_i(&%s);" % (ctype(k), v, ctype(k), ar)
        if kind == "bool":
            return "_Bool %s = (_Bool)(rc_arg_i(&%s) & 0xff);" % (v, ar)
        if kind == "ptr":
            return "void *%s = (void *)(uintptr_t)rc_arg_i(&%s);" % (v, ar)
        if kind == "fnptr":
            return "void *%s = rc_tramp_get(rc_arg_i(&%s));" % (v, ar)
        if kind == "f32":
            return "float %s = rc_f32((uint32_t)rc_arg_f(&%s));" % (v, ar)
        if kind == "f64":
            return "double %s = rc_f64(rc_arg_f(&%s));" % (v, ar)
        if kind == "i128":
            return ("unsigned __int128 %s; { uint64_t lo, hi; if (%s.gi <= 4) { lo = rc_arg_i(&%s); hi = rc_arg_i(&%s); } "
                    "else { %s.gi = 6; %s.sp = (%s.sp + 15) & ~15ull; lo = RC_LD(uint64_t, %s.sp); "
                    "hi = RC_LD(uint64_t, %s.sp + 8); %s.sp += 16; } %s = ((unsigned __int128)hi << 64) | lo; }"
                    % (v, ar, ar, ar, ar, ar, ar, ar, ar, ar, v))
        if kind == "vec128":
            return "rc_vec4f %s; { rc_v128 t = rc_arg_v128(&%s); memcpy(&%s, &t, 16); }" % (v, ar, v)
        if kind == "agg":
            n, s0, s1 = k["cls"] if k["cls"] else (0, 0, 0)
            cls = ("RC_AGG(%d, %d, %d)" % (n, s0, s1)) if k["cls"] else "0"
            return "%s %s; rc_arg_agg(&%s, &%s, %d, %d, %s);" % (ctype(k), v, ar, v, k["size"], k["align"], cls)
        raise ValueError(kind)

    def write_ret(self, k, r="r"):
        kind = k["kind"]
        if kind == "void":
            return []
        if kind == "int":
            return ["c->r[0] = (uint64_t)(uint%d_t)%s;" % (k["bits"], r)]
        if kind == "bool":
            return ["c->r[0] = (uint64_t)(uint8_t)%s;" % r]
        if kind in ("ptr", "fnptr"):
            return ["c->r[0] = (uint64_t)(uintptr_t)%s;" % r]
        if kind == "f32":
            return ["rc_ret_f32(c, %s);" % r]
        if kind == "f64":
            return ["rc_ret_f64(c, %s);" % r]
        if kind == "i128":
            return ["c->r[0] = (uint64_t)%s; c->r[2] = (uint64_t)(%s >> 64);" % (r, r)]
        if kind == "vec128":
            return ["{ rc_v128 t; memcpy(&t, &%s, 16); rc_ret_v128(c, t); }" % r]
        if kind == "agg":
            if not k["cls"]:
                return ["memcpy((void *)(uintptr_t)sret, &%s, %d); c->r[0] = sret;" % (r, k["size"])]
            n, s0, s1 = k["cls"]
            return ["rc_ret_agg(c, &%s, %d, RC_AGG(%d, %d, %d));" % (r, k["size"], n, s0, s1)]
        raise ValueError(kind)

    def note_agg(self, k):
        if k["kind"] == "agg":
            for d in k["cdefs"]:
                self.aggs[d] = d
            self.agg_sizes[k["name"]] = (k["size"], k["align"], k["cdefs"][-1].split()[0])

    def cb_flags(self, text):
        """RC_TR_* flags for a native caller of a function pointer of type `text`, or 0.

        A struct returned through a hidden pointer (x8 on arm64, rdi on x86), a result narrower than
        32 bits (x86 leaves the upper bits of eax undefined, arm64 callers assume them extended) and
        a variadic callee (`...` arguments in stack slots on Darwin arm64) each need a trampoline
        variant. Named arguments of a variadic callee are assumed to be integers or pointers."""
        m = re.match(r"^(.*?)\s*\(\*\)\s*\((.*)\)\s*$", re.sub(r"\s+", " ", text))
        if not m:
            return 0
        try:
            ret = self.P.classify(m.group(1))
        except Exception:
            return 0
        fl = 0
        if ret["kind"] == "agg" and not ret["cls"]:
            fl |= TR_SRET
        elif ret["kind"] == "bool":
            fl |= TR_RET["bool"]
        elif ret["kind"] == "int" and ret["bits"] < 32:
            fl |= TR_RET[("int", ret["bits"], ret["signed"])]
        args = self.split_top(m.group(2))
        if args and args[-1] == "...":
            named = args[:-1]
            idx = max([i for i, a in enumerate(named) if "*" in a and "char" in a] or
                      [i for i, a in enumerate(named) if "*" in a] or [0])
            fl |= TR_VA | (idx << 8)
        return fl

    # --- thunk builders -------------------------------------------------------------
    def thunk_universal(self, slot, name):
        return ["rc_native_generic(c, rc_imp(0x%xull, \"%s\"), 0);" % (slot, name)]

    def fn_expr(self, slot, name, sym):
        if "$" in sym:  # legacy decoration (stat$INODE64, select$1050): the plain symbol is the arm64 one
            return self.direct_ref(name.split("$")[0])
        return "rc_imp(0x%xull, \"%s\")" % (slot, name)

    def thunk_typed(self, slot, name, sym, proto, kinds):
        ret, params = kinds
        for k in [ret] + params:
            self.note_agg(k)
        body = ["rc_args ar; rc_args_init(&ar, c);"]
        sret = ret["kind"] == "agg" and not ret["cls"]
        if sret:
            body.append("uint64_t sret = rc_arg_i(&ar);")
        names = []
        for i, k in enumerate(params):
            v = "a%d" % i
            names.append(v)
            body.append(self.read_param(k, v))
            if k["kind"] == "fnptr":
                fl = self.cb_flags(proto["params"][i])
                if fl:
                    body.append("%s = rc_tramp_get_ex((uint64_t)%s, 0x%x);" % (v, v, fl))
        if any(k["kind"] == "fnptr" for k in params) and any(b.startswith("a") and "rc_tramp_get_ex" in b for b in body):
            body.insert(0, "extern void *rc_tramp_get_ex(uint64_t, unsigned);")
        return body, names, sret

    def build_call(self, fnexpr, ret, params, names, extra_sig="", extra_args=()):
        sig = ", ".join([ctype(k) for k in params] + ([extra_sig] if extra_sig else [])) or "void"
        call = "((%s (*)(%s))%s)(%s)" % (ctype(ret), sig, fnexpr, ", ".join(list(names) + list(extra_args)))
        if ret["kind"] == "void":
            return [call + ";"]
        return ["%s r = %s;" % (ctype(ret), call)] + self.write_ret(ret)

    # --- planning -------------------------------------------------------------------
    def plan(self, slot, sym, dylib):
        """Returns (category, reason, body lines) for one import."""
        n = sym[1:] if sym.startswith("_") else sym
        base = n.split("$")[0]
        if n in SPECIAL_BODY:
            return "special", "", [SPECIAL_BODY[n]]
        if base in UNIVERSAL_OK:
            return "universal", "objc_msgSend family: untyped by design", self.thunk_universal(slot, n)
        if n.startswith("_Z"):
            return self.plan_cxx(slot, sym, n)
        p = self.P.proto(base)
        if not p:
            return "universal", "no prototype found", self.thunk_universal(slot, n)
        # sigs.py looks for "...)" anywhere in the type, which also matches a variadic function-pointer
        # parameter: the function itself is variadic only if its own parameter list ends in "..."
        if p["variadic"] and not self.P.funcs[base]["type"].rstrip().endswith("...)"):
            p["variadic"] = False
        ret = self.P.classify(p["ret"])
        params = [self.P.classify(t) for t in p["params"]]
        sugar = p["sugar"]
        for i, k in enumerate(params):
            if k["kind"] == "ptr" and "va_list" in sugar[i]:
                k["va"] = True
        bad = [k for k in [ret] + params if k["kind"] in ("unknown", "f80")]
        if bad:
            return "universal", "unsupported type: " + ", ".join(k.get("text", k["kind"]) for k in bad), \
                self.thunk_universal(slot, n)
        if ret["kind"] == "vec128" or any(k["kind"] == "vec128" for k in params):
            pass
        body, names, sret = self.thunk_typed(slot, n, sym, p, (ret, params))
        fn = self.fn_expr(slot, n, sym)
        cat = "typed"
        if p["variadic"]:
            if base in FMT_VARIADIC:
                return self.plan_fmt(slot, n, base, ret, params, body, names, sret)
            if base in EXTRA_VARIADIC:
                extras = EXTRA_VARIADIC[base]
                eargs = []
                for j, ct in enumerate(extras):
                    body.append("long e%d = (long)rc_arg_i(&ar);" % j)
                    eargs.append("(%s)e%d" % (ct, j))
                body += self.build_call(fn, ret, params, names, extra_sig="...", extra_args=eargs)
                return "variadic", "fixed extra arguments: " + base, body
            return "universal", "variadic without a known format: " + p["ret"], self.thunk_universal(slot, n)
        if any(k.get("va") for k in params):
            return self.plan_valist(slot, n, base, ret, params, body, names, sret, fn)
        if base in HOOKS:
            body += HOOKS[base](names)
            cat = "callback"
        elif any(k["kind"] == "fnptr" for k in params):
            cat = "callback"
        body += self.build_call(fn, ret, params, names)
        if base in RETURNS_SRET_FN:
            # a native function pointer that returns a struct: guest callers must use x8
            body += ["extern void rc_native_register_sret(const void *);", "rc_native_register_sret(r);"]
        if sret:
            cat = "sret"
        return cat, "", body

    def fmt_setup(self, fmt_var, kind, with_ns):
        if with_ns:
            return ["char nsbuf[1024];", "const char *fmt = rc_va_nsfmt((uint64_t)(uintptr_t)%s, nsbuf, sizeof nsbuf);" % fmt_var]
        return ["const char *fmt = (const char *)%s;" % fmt_var]

    def plan_fmt(self, slot, n, base, ret, params, body, names, sret):
        vfn, kind = FMT_VARIADIC[base]
        body += self.fmt_setup(names[-1], kind, kind == "RC_VA_NSLOG")
        body += ["uint64_t slots[rc_va_max(fmt)];",
                 "void *va = rc_va_from_regs(&ar, fmt, %s, slots);" % ("RC_VA_PRINTF" if kind == "RC_VA_NSLOG" else kind)]
        body += self.build_call(self.direct_ref(vfn), ret, params, names, extra_sig="void *", extra_args=["va"])
        return "variadic", "format string (%s -> %s)" % (base, vfn), body

    def plan_valist(self, slot, n, base, ret, params, body, names, sret, fn):
        vi = [i for i, k in enumerate(params) if k.get("va")][0]
        fi = max(i for i in range(vi) if params[i]["kind"] == "ptr")  # the format string
        kind = "RC_VA_SCANF" if "scanf" in base else ("RC_VA_NSLOG" if base == "NSLogv" else "RC_VA_PRINTF")
        body += self.fmt_setup(names[fi], kind, kind == "RC_VA_NSLOG")
        body += ["uint64_t slots[rc_va_max(fmt)];",
                 "%s = rc_va_from_x86(%s, fmt, %s, slots);" % (names[vi], names[vi],
                                                                  "RC_VA_PRINTF" if kind == "RC_VA_NSLOG" else kind)]
        body += self.build_call(fn, ret, params, names)
        return "variadic", "x86 va_list converted (%s)" % base, body

    def plan_cxx(self, slot, sym, n):
        dem = self.demangled.get(n)
        sig = self.cxx_signature(n, dem) if dem else None
        if sig is None:
            return "universal", "C++ signature not understood: %s" % dem, self.thunk_universal(slot, n)
        member, params = sig
        if member:
            params = [dict(kind="ptr")] + params
        sret = n in CXX_SRET
        fnptr = [i for i, k in enumerate(params) if k["kind"] == "fnptr"]
        if not sret and not fnptr:
            return "cxx", dem, self.thunk_universal(slot, n)
        # register marshalling: params -> x/d slots in order (ints and pointers to x0.., floats to d0..)
        body = ["rc_args ar; rc_args_init(&ar, c);", "uint64_t ix[8] = {0}, dx[8] = {0}, ret[6]; unsigned xi = 0, di = 0;"]
        if sret:
            body.append("uint64_t sret = rc_arg_i(&ar);")
        for k in params:
            if k["kind"] in ("f32", "f64"):
                body.append("dx[di++] = rc_arg_f(&ar);")
            elif k["kind"] == "fnptr":
                body.append("ix[xi++] = (uint64_t)(uintptr_t)rc_tramp_get(rc_arg_i(&ar));")
            else:
                body.append("ix[xi++] = rc_arg_i(&ar);")
        body.append("if (xi > 8 || di > 8) rc_fail(\"C++ import with too many arguments\", 0x%xull);" % slot)
        fn = "rc_imp(0x%xull, \"%s\")" % (slot, n)
        if sret:
            body.append("rc_native_call_x8(%s, ix, dx, ret, sret); c->r[0] = sret;" % fn)
        else:
            body += ["rc_native_call(%s, ix, dx, ret);" % fn,
                     "c->r[0] = ret[0]; c->r[2] = ret[1]; c->x[0].lo = ret[2]; c->x[0].hi = 0; "
                     "c->x[1].lo = ret[3]; c->x[1].hi = 0;"]
        return ("sret" if sret else "callback"), dem + (" [%s]" % CXX_SRET[n] if sret else ""), body

    # --- driver -----------------------------------------------------------------------
    def run(self):
        stubs = sorted(self.tr.stubs.items())
        cxx = []
        for st, (slot, sym) in stubs:
            if self.tr.stub_local_target(slot) is None and sym.startswith("__Z"):
                cxx.append(sym[1:])
        self.demangled = dict(zip(cxx, self.demangle(cxx)))
        for st, (slot, sym) in stubs:
            info = self.info.get(slot)
            local = self.tr.stub_local_target(slot)
            row = dict(slot="0x%x" % slot, stub="0x%x" % st, sym=sym, dylib=(info or {}).get("dylib", ""))
            if local is not None:
                row.update(category="local", reason="defined in the image (direct call to 0x%x)" % local)
                self.entries.append(row)
                continue
            cat, reason, body = self.plan(slot, sym, row["dylib"])
            if cat not in ("universal", "cxx", "local"):
                body = UNWIND_GUARD + body
            row.update(category=cat, reason=reason)
            self.entries.append(row)
            self.thunks.append((slot, sym, body))

    def emit(self, path):
        o = ["// Generated by tools/ws7/recomp/thunks/gen_thunks.py: one thunk per import of the image.",
             "// Game-derived (the import list): build output only, never commit. Do not edit; regenerate.",
             "#include <stdint.h>", "#include <stdio.h>", "#include <stdlib.h>", "#include <string.h>",
             "#include <pthread.h>", '#include "rc_call.h"', "",
             "typedef float rc_vec4f __attribute__((vector_size(16)));"]
        o += list(self.aggs)
        for name, (size, align, tag) in sorted(self.agg_sizes.items()):
            o.append('_Static_assert(sizeof(%s rc_%s) == %d && _Alignof(%s rc_%s) == %d, "layout of %s");'
                     % (tag, name, size, tag, name, align, name))
        for sym, ident in sorted(self.direct.items()):
            o.append('extern char %s __asm__("_%s");' % (ident, sym))
        o.append("")
        for slot, sym, body in self.thunks:
            o.append("// %s" % sym)
            o.append("void I_%x(rc_ctx *c) {" % slot)
            o += ["  " + b for b in body]
            o.append("}")
        o += ["", "static const struct { uint64_t slot; rc_fn fn; } rc_imp_tab[] = {"]
        o += ["  {0x%xull, I_%x}," % (s, s) for s, _, _ in self.thunks]
        # rc_import_thunk (rc_icall_native, for every native call target): an open-addressing hash
        # over the bound addresses, built on first use (after rc_image_load bound the slots)
        hbits = max(4, (2 * len(self.thunks)).bit_length())
        o += ["};", "#define RC_IMP_N (sizeof rc_imp_tab / sizeof *rc_imp_tab)",
              "#define RC_IMP_H %du  /* power of two > 2 * RC_IMP_N */" % (1 << hbits),
              "static struct { uint64_t host; rc_fn fn; } rc_imp_hash[RC_IMP_H];",
              "static int rc_imp_ready;", "static pthread_once_t rc_imp_once = PTHREAD_ONCE_INIT;",
              "static unsigned rc_imp_h(uint64_t h) { return (unsigned)((h * 0x9E3779B97F4A7C15ull) >> 40) & (RC_IMP_H - 1); }",
              "static void rc_imp_build(void) {",
              "  for (size_t i = 0; i < RC_IMP_N; i++) {",
              "    uint64_t h = RC_LD(uint64_t, RC_A(rc_imp_tab[i].slot));",
              "    if (!h) continue;",
              "    unsigned k = rc_imp_h(h);",
              "    while (rc_imp_hash[k].host && rc_imp_hash[k].host != h) k = (k + 1) & (RC_IMP_H - 1);",
              "    if (!rc_imp_hash[k].host) { rc_imp_hash[k].host = h; rc_imp_hash[k].fn = rc_imp_tab[i].fn; }",
              "  }",
              "  __atomic_store_n(&rc_imp_ready, 1, __ATOMIC_RELEASE);",
              "}",
              "rc_fn rc_import_thunk(uint64_t host) {",
              "  if (__builtin_expect(!__atomic_load_n(&rc_imp_ready, __ATOMIC_ACQUIRE), 0)) pthread_once(&rc_imp_once, rc_imp_build);",
              "  if (!host) return 0;",
              "  for (unsigned k = rc_imp_h(host);; k = (k + 1) & (RC_IMP_H - 1)) {",
              "    if (rc_imp_hash[k].host == host) return rc_imp_hash[k].fn;",
              "    if (!rc_imp_hash[k].host) return 0;",
              "  }", "}", ""]
        with open(path, "w") as f:
            f.write("\n".join(o))

    def report(self):
        cats = {}
        for r in self.entries:
            cats.setdefault(r["category"], []).append(r)
        summary = {c: len(v) for c, v in sorted(cats.items())}
        summary["slots"] = len(self.entries)
        summary["distinct_symbols"] = len({r["sym"] for r in self.entries})
        uni = [r for r in cats.get("universal", [])]
        return dict(summary=summary, universal=uni, entries=self.entries,
                    cxx_sret=CXX_SRET, notes=[
                        "universal: six integer registers + two stack words and eight FP registers are passed through; "
                        "exact only for scalar/pointer arguments that fit",
                        "callback: function-pointer parameters are wrapped with rc_tramp_get",
                        "local: weak instance defined by the image (xlat/calls.py calls it directly)"])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("image")
    ap.add_argument("out")
    ap.add_argument("--report")
    ap.add_argument("--variadic", action="append", default=[], metavar="NAME=int,long",
                    help="a variadic function whose extra arguments are these integer types (repeatable)")
    ap.add_argument("--header", action="append", default=[], help="extra C header with prototypes (repeatable)")
    a = ap.parse_args()
    for v in a.variadic:
        name, _, types = v.partition("=")
        EXTRA_VARIADIC[name] = [t for t in types.split(",") if t]
    g = Gen(a.image)
    errs = g.load_protos(a.header)
    for e in errs:
        if "error:" in e:
            sys.stderr.write("[gen_thunks] header errors (non-fatal):\n" + "\n".join(
                l for l in e.splitlines() if "error:" in l)[:1500] + "\n")
    g.run()
    g.emit(a.out)
    rep = g.report()
    if a.report:
        with open(a.report, "w") as f:
            json.dump(rep, f, indent=1)
    print(json.dumps(rep["summary"]))
    for r in rep["universal"]:
        print("  universal: %-40s %s" % (r["sym"], r["reason"]))


if __name__ == "__main__":
    main()
