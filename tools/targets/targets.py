#!/usr/bin/env python3
"""Per-game tables of the build: name -> address / offset, one directory per supported binary.

    tools/targets/<id>/target.toml   image identity, front end, ABI, image layout
    tools/targets/<id>/symbols.map   NAME ADDRESS CONVENTION PROTOTYPE   (functions and data the build touches)
    tools/targets/<id>/offsets.map   NAME OFFSET                         (struct fields native code reads)
    tools/targets/<id>/drops.map     MODULE ADDRESS                      (dead code once MODULE replaces a library)
    tools/targets/<id>/edits.map     GROUP ADDRESS LEN:CHECK EDIT        (instructions a mod can switch to others)
    tools/targets/<id>/mids.map      NAME ADDRESS (VALUES) [-> CONT=ADDRESS, ...]   (mid points: hooks before an
                                                                          instruction inside a function)
    tools/targets/names/             which names are overridden, hooked, called or read (shared by all targets)

A map line `NAME unsupported REASON...` says the target has no counterpart; the name then resolves to
nothing there (an override or hook point is left out, code that uses it does not compile for that target).
In mids.map, `NAME none REASON...` says the target needs no call at that moment (its code needs nothing there): a
mod's hook on the point is taken and never called (rc_mid.h).

usage: targets.py gen [--target ID] OUTDIR     write OUTDIR/rc_target_names.h, OUTDIR/rc_target.c and
                                               OUTDIR/skip/<module>.skip (gen_image.py --skip) for one target
       targets.py addr [--target ID] NAME...   print the addresses (hex) of names
       targets.py check                        see check.py
The target defaults to $RC_TARGET, else mac-x64. Standard library only (any Python 3.9+).
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
DEFAULT_TARGET = "mac-x64"
IDENT = re.compile(r"[A-Za-z_][A-Za-z0-9_]*$")
CONVENTIONS = ("sysv", "cdecl", "stdcall", "thiscall", "fastcall", "custom", "data", "code")


class TargetError(Exception):
    pass


def _strip(ln):
    return ln.split("#", 1)[0].strip()


def parse_toml(path):
    """The subset target.toml uses: `key = "string" | integer | [integer, ...]`, # comments."""
    d = {}
    with open(path) as f:
        lines = f.readlines()
    for i, ln in enumerate(lines, 1):
        s = ln.strip()
        if not s or s.startswith("#"):
            continue
        k, eq, v = s.partition("=")
        if not eq:
            raise TargetError("%s:%d: expected key = value" % (path, i))
        k, v = k.strip(), v.strip()
        if v.startswith('"'):
            d[k] = v[1:v.index('"', 1)]
        else:
            v = _strip(v)
            if v.startswith("["):
                d[k] = [int(x, 0) for x in v.strip("[]").split(",") if x.strip()]
            else:
                d[k] = int(v, 0)
    return d


class Target:
    def __init__(self, tid, base=HERE):
        self.id = tid
        self.base = base
        self.dir = os.path.join(base, tid)
        if not os.path.isfile(os.path.join(self.dir, "target.toml")):
            raise TargetError("unknown target %r (no %s/target.toml)" % (tid, self.dir))
        self.info = parse_toml(os.path.join(self.dir, "target.toml"))
        self.symbols = {}      # name -> (addr, conv, proto)
        self.offsets = {}      # name -> int
        self.unsupported = {}  # name -> reason (symbols and offsets)
        self.drops = []        # (module, addr)
        self.edits = []        # (group, addr, length, check, patches, where): parse_edit
        self.mids = {}         # name -> (addr, [(kind, name, loc, mem, off)], [(cont name, addr)])
        self.inner_unsupported = {}  # edit group or mid point -> reason
        self.inner_none = {}  # mid point -> reason: this target needs no call at that moment (`NAME none REASON`)
        self._read_symbols()
        self._read_offsets()
        self._read_drops()
        self._read_edits()
        self._read_mids()

    def _lines(self, fname):
        path = os.path.join(self.dir, fname)
        if not os.path.exists(path):
            return
        with open(path) as f:
            lines = f.readlines()
        for i, ln in enumerate(lines, 1):
            s = ln.split("#", 1)[0].rstrip() if not ln.lstrip().startswith("#") else ""
            if s.strip():
                yield "%s/%s:%d" % (self.id, fname, i), s.split(None, 3)

    def _unsupported(self, where, p):
        if p[0] in self.unsupported or p[0] in self.symbols or p[0] in self.offsets:
            raise TargetError("%s: %s defined twice" % (where, p[0]))
        self.unsupported[p[0]] = " ".join(p[2:]) or "(no reason given)"

    def _read_symbols(self):
        for where, p in self._lines("symbols.map"):
            if not IDENT.match(p[0]):
                raise TargetError("%s: bad name %r" % (where, p[0]))
            if len(p) > 1 and p[1] == "unsupported":
                self._unsupported(where, p)
                continue
            if len(p) < 3 or p[2] not in CONVENTIONS:
                raise TargetError("%s: expected NAME ADDRESS CONVENTION [PROTOTYPE] (conventions: %s)"
                                  % (where, " ".join(CONVENTIONS)))
            if p[0] in self.symbols or p[0] in self.unsupported:
                raise TargetError("%s: %s defined twice" % (where, p[0]))
            self.symbols[p[0]] = (int(p[1], 16), p[2], p[3] if len(p) > 3 else "-")

    def _read_offsets(self):
        for where, p in self._lines("offsets.map"):
            if not IDENT.match(p[0]) or len(p) < 2:
                raise TargetError("%s: expected NAME OFFSET" % where)
            if p[1] == "unsupported":
                self._unsupported(where, p)
                continue
            if p[0] in self.offsets or p[0] in self.symbols or p[0] in self.unsupported:
                raise TargetError("%s: %s defined twice" % (where, p[0]))
            self.offsets[p[0]] = int(p[1], 0)

    def _read_drops(self):
        for where, p in self._lines("drops.map"):
            self.drops.append((p[0], int(p[1], 16)))

    def _read_edits(self):
        seen = {}
        for where, p in self._lines("edits.map"):
            if not IDENT.match(p[0]):
                raise TargetError("%s: bad group name %r" % (where, p[0]))
            if len(p) > 1 and p[1] == "unsupported":
                if p[0] in self.inner_unsupported or any(e[0] == p[0] for e in self.edits):
                    raise TargetError("%s: group %s is unsupported and has edits" % (where, p[0]))
                self.inner_unsupported[p[0]] = " ".join(p[2:]) or "(no reason given)"
                continue
            if p[0] in self.inner_unsupported:
                raise TargetError("%s: group %s is unsupported and has edits" % (where, p[0]))
            if len(p) != 4:
                raise TargetError("%s: expected GROUP ADDRESS LEN:CHECK EDIT" % where)
            a, n, check, patches = int(p[1], 16), *parse_edit(p[2], p[3], where)
            for b, e in seen.items():   # one edit per range, in any group
                if b < a + n and a < e[0]:
                    raise TargetError("%s: overlaps the edit at 0x%x (%s)" % (where, b, e[1]))
            seen[a] = (a + n, where)
            self.edits.append((p[0], a, n, check, patches, where))

    def _read_mids(self):
        for where, p in self._lines("mids.map"):
            if not IDENT.match(p[0]):
                raise TargetError("%s: bad name %r" % (where, p[0]))
            if p[0] in self.mids or p[0] in self.inner_unsupported or p[0] in self.inner_none:
                raise TargetError("%s: %s defined twice" % (where, p[0]))
            if len(p) > 1 and p[1] == "unsupported":
                self.inner_unsupported[p[0]] = " ".join(p[2:]) or "(no reason given)"
                continue
            if len(p) > 1 and p[1] == "none":   # the moment needs nothing here: a hook on it is taken, never called
                if len(p) < 3:
                    raise TargetError("%s: %s none: give the reason this target needs no call there" % (where, p[0]))
                self.inner_none[p[0]] = " ".join(p[2:])
                continue
            if len(p) < 3:
                raise TargetError("%s: expected NAME ADDRESS (VALUES) [-> CONT=ADDRESS, ...]" % where)
            vals, conts = parse_mid(" ".join(p[2:]), self.info.get("abi", "sysv"), where)
            self.mids[p[0]] = (int(p[1], 16), vals, conts)

    def addr(self, name):
        if name in self.symbols:
            return self.symbols[name][0]
        if name in self.unsupported:
            return None
        raise TargetError("target %s: no symbol %r (add it to %s/symbols.map)" % (self.id, name, self.id))

    def resolves(self, name):
        return name in self.symbols or name in self.offsets or name in self.unsupported


def target_ids(base=HERE):
    return sorted(d for d in os.listdir(base) if os.path.isfile(os.path.join(base, d, "target.toml")))


# --- names/ ---------------------------------------------------------------------------------------
def read_list(path):
    """Names, one per line (the first word; the rest of the line is a comment)."""
    out = []
    with open(path) as f:
        lines = f.readlines()
    for ln in lines:
        s = _strip(ln)
        if s:
            name = s.split()[0]
            if not IDENT.match(name):
                raise TargetError("%s: bad name %r" % (path, name))
            out.append(name)
    return out


def name_lists(base=HERE):
    """{'overrides/<module>': [...], 'hooks': [...], 'seam': [...], 'refs': [...], 'offsets': [...]}"""
    d = {}
    names = os.path.join(base, "names")
    for dp, _, fs in os.walk(names):
        for f in sorted(fs):
            if f.endswith(".list"):
                rel = os.path.relpath(os.path.join(dp, f), names)[:-5]
                d[rel] = read_list(os.path.join(dp, f))
    return d


# --- generation ------------------------------------------------------------------------------------
def _hexs(v):
    return ("-0x%x" % -v) if v < 0 else ("0x%x" % v)


def gen_header(t):
    L = ["// Generated by tools/targets/targets.py for target %s. Do not edit; the source is" % t.id,
         "// tools/targets/%s/*.map (rc_abi.h has the macros that use these)." % t.id,
         "#ifndef RC_TARGET_NAMES_H", "#define RC_TARGET_NAMES_H", "",
         '#define RC_TARGET_ID "%s"' % t.id,
         "#define RC_TARGET_%s 1" % re.sub(r"\W", "_", t.id).upper(),
         "#define RC_TARGET_ABI_%s 1" % t.info["abi"].upper()]
    if "text" in t.info:
        L += ["#define RC_TARGET_TEXT_LO 0x%xull" % t.info["text"][0],
              "#define RC_TARGET_TEXT_HI 0x%xull" % t.info["text"][1]]
    L += ["", "// RC_ADDR_<name>: unslid image address; RC_FN_<name>: the F_ symbol of a function;",
          "// RC_CC_<name>: its calling convention (RC_CC_SYSV, RC_CC_CDECL, RC_CC_STDCALL, RC_CC_THISCALL, ...)"]
    for name in sorted(t.symbols):
        a, conv, _ = t.symbols[name]
        L.append("#define RC_ADDR_%s 0x%xull" % (name, a))
        if conv not in ("data", "code"):
            # a build may give an override's F_ symbol another name (-DRC_FN_<name>=...)
            L += ["#ifndef RC_FN_%s" % name, "#define RC_FN_%s F_%x" % (name, a), "#endif"]
            L.append("#define RC_CC_%s RC_CC_%s" % (name, conv.upper()))
    L += ["", "// RC_OFF_<name>: struct field offsets"]
    for name in sorted(t.offsets):
        v = t.offsets[name]
        L.append("#define RC_OFF_%s %s" % (name, _hexs(v) if v >= 0 else "(%s)" % _hexs(v)))
    if t.unsupported:
        L += ["", "// names this target has no counterpart for (code using them does not compile here)"]
        for name in sorted(t.unsupported):
            L.append("#define RC_UNSUPPORTED_%s 1" % name)
    L += ["", "#endif", ""]
    return "\n".join(L)


# Hook points of a guest with another ABI: what the runtime needs to show a hook the Mac's SysV call
# (runtime/rc_hook.h). Kinds: b h i q 8-, 16-, 32-, 64-bit integer, p pointer, f float, d double, v none,
# c an argument with one value that this target's code doesn't take (`int event=0x98`), x one the Mac's call
# doesn't have, kept from the hook (`void *manager=hidden`). Results the Mac's call has in another form:
# e the Mac's bool is a field of the first argument having a value (`bool<service_manager_state=7>`),
# r the Mac's float is 100 / this one (`float<100/x>`), z the Mac's bool is this function's answer being 0
# (`bool<zero>`: an error code, 0 when all went well).
KINDS = {"bool": "b", "char": "b", "int8_t": "b", "uint8_t": "b", "short": "h", "int16_t": "h", "uint16_t": "h",
         "int": "i", "long": "i", "unsigned": "i", "int32_t": "i", "uint32_t": "i", "size_t": "i",
         "int64_t": "q", "uint64_t": "q", "float": "f", "double": "d", "void": "v"}
REGS = {"eax": 0, "ecx": 1, "edx": 2, "ebx": 3, "esi": 6, "edi": 7}
CC_IDS = {"sysv": 0, "cdecl": 1, "stdcall": 2, "thiscall": 3, "fastcall": 4, "custom": 5}  # RC_CC_* (rc_abi.h)
MAX_HOOK_ARGS = 8  # RC_HOOK_MAXARGS


def _kind(text, where):
    """(kind, register or None) of `type name` or `type name@ecx` in a prototype; of `type name=0x98`, ("=0x98",
    None): an argument the compiler folded away, the same in every call."""
    t, eq, value = text.strip().partition("=")
    if eq:
        return "=" + value.strip(), None if value.strip() != "hidden" else _hidden_reg(t)
    t, _, reg = t.partition("@")
    if reg and reg.strip() not in REGS:
        raise TargetError("%s: no register %r" % (where, reg))
    if "*" in t or "&" in t:
        return "p", reg.strip() or None
    words = [w for w in re.findall(r"\w+", t) if w not in ("const", "volatile", "signed")]
    if words[:1] == ["unsigned"] and len(words) > 1 and words[1] in KINDS:
        words = words[1:]
    if not words or words[0] not in KINDS:
        raise TargetError("%s: type %r is not in targets.py KINDS" % (where, t))
    return ("q" if words[:2] == ["long", "long"] else KINDS[words[0]]), reg.strip() or None


def _hidden_reg(text):
    return text.partition("@")[2].strip() or None


def parse_proto(proto, where):
    """`ret(args)` -> (return kind, [(kind, register or None), ...], True when it ends in `...`). A return in
    another form than the Mac's, `bool<name=7>`, `float<100/x>` or `bool<zero>`, is the kind e, r or z, with what
    follows it."""
    i = proto.find("(")
    if i < 0 or not proto.rstrip().endswith(")"):
        raise TargetError("%s: expected a prototype, got %r" % (where, proto))
    m = re.match(r"\s*(\w+)<(.*)>\s*$", proto[:i])
    if m:
        ret = "z" if proto[:i].strip() == "bool<zero>" else {"bool": "e", "float": "r"}[m.group(1)] + m.group(2)
    else:
        ret = _kind(proto[:i], where)[0]
    body = proto[i + 1:proto.rstrip().rindex(")")].strip()
    args = [a.strip() for a in body.split(",")] if body and body != "void" else []
    variadic = bool(args) and args[-1] == "..."
    return ret, [_kind(a, where) for a in (args[:-1] if variadic else args)], variadic


def _shape(ret, args, variadic):
    """What a hook reads of a call: the return and the arguments it is shown, by what they are to a mod.
    An integer's width is not part of it (a size or an id is pointer-sized); a bool is its own."""
    def cls(k):
        return "i" if k[0] in "hiq=" and k != "=hidden" else k[0]
    return {"e": "b", "z": "b", "r": "f"}.get(ret[0], cls(ret)), [cls(k) for k, _ in args if k != "=hidden"], variadic


def hook_abi(t, name, mac=None):
    """What a hook point's call looks like: None for SysV, the runtime has nothing to do. Else
    {"cc", "ret", "ret_off", "ret_is", "pops" (-1 unknown), "args": [(kind, stack word or ~register, value), ...],
    "why"}. "args" None means hooks on it are refused at run time, for the reason in "why": without a
    prototype, or a `custom` function without `@register` in it, nothing is known of its arguments; and against the
    Mac's prototype (mac: its Target) a point of the same name has to read the same: a mod is built for one
    call. A difference is adapted in the prototype (`=value`, `=hidden`, `bool<...>`, `float<...>`) or refused."""
    _, conv, proto = t.symbols[name]
    if conv == "sysv":
        return None
    d = {"cc": CC_IDS[conv], "ret": "v", "ret_off": 0, "ret_is": 0, "pops": -1, "args": None,
         "why": "its prototype doesn't say what it takes"}
    if proto == "-":
        return d
    where = "%s/symbols.map: %s" % (t.id, name)
    ret, args, variadic = parse_proto(proto, where)
    if len(args) > MAX_HOOK_ARGS:
        raise TargetError("%s: more than %d arguments" % (where, MAX_HOOK_ARGS))
    d["ret"] = ret[0] if ret[0] in "erz" else ret
    if ret[0] == "e":
        field, _, value = ret[1:].partition("=")
        if field not in t.offsets:
            raise TargetError("%s: no offset %s" % (where, field))
        d["ret_off"], d["ret_is"] = t.offsets[field], int(value, 0)
    if conv == "custom" and not any(r for k, r in args):
        d["why"] = "its prototype doesn't say which registers it takes"
        return d
    if mac is not None and name in mac.symbols and mac.symbols[name][2] != "-":
        macproto = mac.symbols[name][2]
        if _shape(ret, args, variadic) != _shape(*parse_proto(macproto, "mac-x64/symbols.map: " + name)):
            d["why"] = "here it is %s, on the Mac %s" % (proto, macproto)
            return d
    regs = {"thiscall": ["ecx"], "fastcall": ["ecx", "edx"]}.get(conv, [])
    out, word = [], 0
    for kind, reg in args:
        if kind == "=hidden":
            kind = "x"
        elif kind[0] == "=":
            out.append(("c", 0, int(kind[1:], 0)))
            continue
        if reg and conv != "custom":
            raise TargetError("%s: @%s on a %s function" % (where, reg, conv))
        if conv != "custom" and regs and kind not in "qfd":
            reg = regs.pop(0)
        if reg:
            out.append((kind, ~REGS[reg], 0))
        else:
            out.append((kind, word, 0))
            word += 2 if kind in "qd" else 1
    d["args"], d["why"] = out, None
    # what the callee removes; with `...`, or in a function of its own making, only its code says
    d["pops"] = 0 if conv == "cdecl" else -1 if variadic or conv == "custom" else 4 * word
    return d


# Mid points (runtime/rc_mid.h): where each value of a point is. A location is a register (an x86 encoding index:
# 0-15 rax..r15 / eax..edi, 16-31 xmm0..15, 32-39 st0..st7, an x87 register counted from the top), or memory at a
# register plus an offset (`[esp+0x18]`), which RC_MID_MEM marks.
GPR64 = ["rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi"] + ["r%d" % k for k in range(8, 16)]
GPR32 = ["eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi"]
MID_MEM = 0x80      # RC_MID_MEM
MAX_MID_VALUES = 8  # RC_MID_MAXVALUES
MAX_MID_CONTS = 8   # RC_MID_MAXCONTS
MAX_EDIT_GROUPS = 64    # RC_EDIT_GROUPS: the switches the generated code indexes
MAX_MID_POINTS = 1024   # RC_MID_POINTS


def _mid_reg(r, abi, where, mem=False):
    gpr = GPR32 if abi == "win32" else GPR64
    if r in gpr:
        return gpr.index(r)
    if mem:
        raise TargetError("%s: memory at %r: a general register only" % (where, r))
    m = re.match(r"(xmm|st)(\d+)$", r)
    if m and int(m.group(2)) < (8 if m.group(1) == "st" or abi == "win32" else 16):
        if m.group(1) == "st" and abi != "win32":
            raise TargetError("%s: %s: x87 values are a 32-bit guest's" % (where, r))
        return (16 if m.group(1) == "xmm" else 32) + int(m.group(2))
    raise TargetError("%s: no register %r on %s" % (where, r, abi))


def parse_mid(text, abi, where):
    """`(TYPE name@LOC, ...) [-> CONT=ADDRESS, ...]` -> ([(kind, name, loc, mem, off)], [(cont, addr)])."""
    m = re.match(r"\s*\((.*)\)\s*(?:->\s*(.*))?$", text)
    if not m:
        raise TargetError("%s: expected (VALUES) [-> CONT=ADDRESS, ...], got %r" % (where, text))
    vals, names = [], set()
    for v in [x.strip() for x in m.group(1).split(",") if x.strip() and x.strip() != "void"]:
        decl, at, loc = v.rpartition("@")
        if not at:
            raise TargetError("%s: value %r: TYPE NAME@LOCATION" % (where, v))
        kind, _ = _kind(decl, where)
        vname = re.search(r"(\w+)\s*$", decl).group(1)
        if vname in names:
            raise TargetError("%s: value %s twice" % (where, vname))
        names.add(vname)
        loc = loc.strip().lower()
        mm = re.match(r"\[\s*(\w+)\s*(?:([+-])\s*(0x[0-9a-f]+|\d+))?\s*\]$", loc)
        if mm:
            off = int(mm.group(3), 0) * (-1 if mm.group(2) == "-" else 1) if mm.group(3) else 0
            vals.append((kind, vname, _mid_reg(mm.group(1), abi, where, mem=True), 1, off))
        else:
            r = _mid_reg(loc, abi, where)
            if (r >= 16) != (kind in "fd"):
                raise TargetError("%s: value %s: a float in a float register, an integer in another" % (where, vname))
            vals.append((kind, vname, r, 0, 0))
    conts = []
    for c in [x.strip() for x in (m.group(2) or "").split(",") if x.strip()]:
        n, eq, a = c.partition("=")
        if not eq or not IDENT.match(n.strip()) or n.strip() in [x for x, _ in conts]:
            raise TargetError("%s: continuation %r: NAME=ADDRESS, each name once" % (where, c))
        conts.append((n.strip(), int(a, 16)))
    if len(vals) > MAX_MID_VALUES or len(conts) > MAX_MID_CONTS:
        raise TargetError("%s: at most %d values and %d continuations" % (where, MAX_MID_VALUES, MAX_MID_CONTS))
    return vals, conts


def parse_edit(span, edit, where):
    """`LEN:CHECK` and `+OFF=HEX[,+OFF=HEX...]` or `=HEX` -> (length, check, patches). The table holds no bytes of
    the game: CHECK is the first 8 hex digits of the SHA-256 of the LEN bytes at the address, which the translator
    reads from the image. Patches are (offset, bytes) inside the range; (None, bytes) replaces the whole range by
    instructions of any length (flow goes on at the range's end)."""
    m = re.match(r"(\d+):([0-9a-f]{8})$", span)
    if not m or not 0 < int(m.group(1)) <= 64:
        raise TargetError("%s: expected LEN:CHECK (a length up to 64, the first 8 hex digits of the bytes' SHA-256)" % where)
    n, patches = int(m.group(1)), []
    try:
        if edit.startswith("="):
            patches.append((None, bytes.fromhex(edit[1:])))
            if not patches[0][1]:
                raise ValueError
        else:
            for part in edit.split(","):
                off, eq, hx = part.partition("=")
                if not eq or not off.startswith("+"):
                    raise ValueError
                patches.append((int(off[1:], 0), bytes.fromhex(hx)))
    except ValueError:
        raise TargetError("%s: expected the edit as =HEX or +OFF=HEX[,+OFF=HEX...]" % where)
    taken = set()
    for off, b in patches:
        if off is None:
            continue
        r = set(range(off, off + len(b)))
        if not b or off < 0 or off + len(b) > n or r & taken:
            raise TargetError("%s: a patch outside the range, empty, or over another" % where)
        taken |= r
    return n, m.group(2), patches


def edit_check(old):
    """The CHECK of an edit's bytes (parse_edit)."""
    import hashlib
    return hashlib.sha256(old).hexdigest()[:8]


def edit_bytes(old, patches):
    """The bytes an edit puts in place of old."""
    if patches and patches[0][0] is None:
        return patches[0][1]
    new = bytearray(old)
    for off, b in patches:
        new[off:off + len(b)] = b
    return bytes(new)


def inner_points(t, lists):
    """What the generators put inside functions: ({addr: (group index, length, check, patches, where)} of
    names/edits.list's groups, {addr: (point index, [continuation addrs], name)} of names/mids.list's points). The
    index is the name's place in its list, the same on every target."""
    groups, points = lists.get("edits", []), lists.get("mids", [])
    if len(groups) > MAX_EDIT_GROUPS or len(points) > MAX_MID_POINTS:
        raise TargetError("names/edits.list and mids.list: at most %d groups and %d points (rc_mid.h: RC_EDIT_GROUPS, "
                          "RC_MID_POINTS)" % (MAX_EDIT_GROUPS, MAX_MID_POINTS))
    edits = {}
    for g, a, n, check, patches, where in t.edits:
        if g not in groups:
            raise TargetError("%s: group %s is not in names/edits.list" % (where, g))
        edits[a] = (groups.index(g), n, check, patches, where)
    mids = {}
    for name, (a, _, conts) in t.mids.items():
        if name not in points:
            raise TargetError("%s/mids.map: %s is not in names/mids.list" % (t.id, name))
        if a in mids:
            raise TargetError("%s/mids.map: %s and %s at the same address" % (t.id, name, mids[a][2]))
        mids[a] = (points.index(name), [c for _, c in conts], name)
    return edits, mids


def _c_string(s):
    return '"%s"' % s.replace("\\", "\\\\").replace('"', '\\"')


def gen_runtime_c(t, lists):
    """The target's name tables for the runtime (rc_hook_add_name, rc_off, rc_addr, rc_target)."""
    mac = t if t.id == DEFAULT_TARGET else Target(DEFAULT_TARGET, t.base)
    L = ["// Generated by tools/targets/targets.py for target %s. Do not edit." % t.id,
         '#include "rc_hook.h"', '#include "rc_mid.h"', "",
         'const char rc_target_id[] = "%s";' % t.id, "",
         "// names/hooks.list: this target's address of each hook point and the mac-x64 address of the same",
         "// point (rc_hook_add with a mac-x64 address keeps working on every target); a target without the",
         "// point has address 0 and the reason; a guest with another ABI has how its call looks",
         "const rc_hook_name rc_target_hooks[] = {"]
    for name in lists.get("hooks", []):
        a = t.addr(name)
        m = mac.addr(name) if mac.resolves(name) else None
        if a is None:
            L.append('  {"%s", 0, 0x%xull, %s},' % (name, m or 0, _c_string(t.unsupported[name])))
            continue
        abi = hook_abi(t, name, mac)
        if abi is None:
            L.append('  {"%s", 0x%xull, 0x%xull},' % (name, a, m or 0))
            continue
        args = abi["args"]
        L.append("  {\"%s\", 0x%xull, 0x%xull, %s, %d, '%s', %d, %d, {%s}, %d, %d}," % (
            name, a, m or 0, _c_string(abi["why"]) if abi["why"] else "0", abi["cc"], abi["ret"], abi["pops"],
            -1 if args is None else len(args), ", ".join("{'%s', %d, %d}" % x for x in args or []),
            abi["ret_off"], abi["ret_is"]))
    L += ["  {0, 0, 0},", "};", "",
          "// offsets.map and symbols.map by name, for a mod built once for every target (rc_off, rc_addr):",
          "// sorted, the runtime searches them; a symbol has its convention, data and code -1",
          "const rc_target_value rc_target_offsets[] = {"]
    L += ['  {"%s", %d},' % (n, t.offsets[n]) for n in sorted(t.offsets)]
    L += ["  {0, 0},", "};", "const rc_target_value rc_target_symbols[] = {"]
    L += ['  {"%s", 0x%x, %d},' % (n, t.symbols[n][0], CC_IDS.get(t.symbols[n][1], -1)) for n in sorted(t.symbols)]
    L += ["  {0, 0},", "};", ""]
    # edit groups and mid points, in their lists' order (the generated code's indexes)
    L += ["// names/edits.list: how many edits each group has here, or why it has none (rc_edit_enable)",
          "const rc_edit_name rc_target_edits[] = {"]
    for g in lists.get("edits", []):
        n = sum(1 for e in t.edits if e[0] == g)
        why = t.inner_unsupported.get(g) or (None if n else "no edits on this target")
        L.append('  {"%s", %d, %s},' % (g, n, _c_string(why) if why else "0"))
    L += ["  {0, 0, 0},", "};", "",
          "// names/mids.list: each point's address, values and continuations, or why this target has none (rc_mid.h)",
          "const rc_mid_name rc_target_mids[] = {"]
    for name in lists.get("mids", []):
        if name in t.inner_none:   # needed nowhere here: no address, the reason, and none = 1
            L.append('  {"%s", 0, %s, 0, {{0}}, 0, {0}, 1},' % (name, _c_string(t.inner_none[name])))
            continue
        if name not in t.mids:
            why = t.inner_unsupported.get(name, "not in %s/mids.map" % t.id)
            L.append('  {"%s", 0, %s},' % (name, _c_string(why)))
            continue
        a, vals, conts = t.mids[name]
        L.append("  {\"%s\", 0x%xull, 0, %d, {%s}, %d, {%s}}," % (
            name, a, len(vals), ", ".join("{\"%s\", '%s', %d, %d}" % (vn, k, loc | (MID_MEM if mem else 0), off)
                                          for k, vn, loc, mem, off in vals),
            len(conts), ", ".join('"%s"' % c for c, _ in conts)))
    L += ["  {0, 0, 0},", "};", ""]
    return "\n".join(L)


def gen_skip(t, lists):
    """{module: text} of gen_image.py --skip files: the override modules, `drop` lines, hooks (`wrap`)."""
    out = {}
    for key, names in sorted(lists.items()):
        if key.startswith("overrides/"):
            mod = key.split("/", 1)[1]
            lines = ["%x" % a for a in (t.addr(n) for n in names) if a is not None]
            lines += ["%x drop" % a for m, a in t.drops if m == mod]
            out[mod] = lines
    out["hooks"] = ["%x wrap" % a for a in (t.addr(n) for n in lists.get("hooks", [])) if a is not None]
    for mod, names in out.items():
        out[mod] = "# generated by tools/targets/targets.py (target %s): module %s\n" % (t.id, mod) + \
            "".join(x + "\n" for x in names)
    return out


def write_if_changed(path, text):
    try:
        if open(path).read() == text:
            return
    except OSError:
        pass
    with open(path, "w") as f:
        f.write(text)


def gen(tid, outdir, base=HERE):
    t = Target(tid, base)
    lists = name_lists(base)
    os.makedirs(os.path.join(outdir, "skip"), exist_ok=True)
    write_if_changed(os.path.join(outdir, "rc_target_names.h"), gen_header(t))
    write_if_changed(os.path.join(outdir, "rc_target.c"), gen_runtime_c(t, lists))
    for mod, text in gen_skip(t, lists).items():
        write_if_changed(os.path.join(outdir, "skip", mod + ".skip"), text)
    write_if_changed(os.path.join(outdir, "target"), t.id + "\n")
    return t


def main(argv):
    args = list(argv)
    tid = os.environ.get("RC_TARGET") or DEFAULT_TARGET
    if "--target" in args:
        i = args.index("--target")
        tid = args[i + 1]
        del args[i:i + 2]
    if not args:
        print(__doc__, file=sys.stderr)
        return 64
    try:
        if args[0] == "gen" and len(args) == 2:
            t = gen(tid, args[1])
            print("target %s: %d symbols, %d offsets, %d unsupported -> %s"
                  % (t.id, len(t.symbols), len(t.offsets), len(t.unsupported), args[1]))
        elif args[0] == "addr":
            t = Target(tid)
            for n in args[1:]:
                a = t.addr(n)
                print("%x" % a if a is not None else "unsupported")
        elif args[0] == "check":
            sys.path.insert(0, HERE)
            import check
            return check.main(args[1:])
        else:
            print(__doc__, file=sys.stderr)
            return 64
    except TargetError as e:
        print("targets: %s" % e, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
