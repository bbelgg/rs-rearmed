#!/usr/bin/env python3
"""Per-game tables of the build: name -> address / offset, one directory per supported binary.

    tools/targets/<id>/target.toml   image identity, front end, ABI, image layout
    tools/targets/<id>/symbols.map   NAME ADDRESS CONVENTION PROTOTYPE   (functions and data the build touches)
    tools/targets/<id>/offsets.map   NAME OFFSET                         (struct fields native code reads)
    tools/targets/<id>/drops.map     MODULE ADDRESS                      (dead code once MODULE replaces a library)
    tools/targets/names/             which names are overridden, hooked, called or read (shared by all targets)

A map line `NAME unsupported REASON...` says the target has no counterpart; the name then resolves to
nothing there (an override or hook point is left out, code that uses it does not compile for that target).

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
CONVENTIONS = ("sysv", "cdecl", "stdcall", "thiscall", "fastcall", "data", "code")


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
        self._read_symbols()
        self._read_offsets()
        self._read_drops()

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


def gen_runtime_c(t, lists):
    """The target's name tables for the runtime (rc_hook_add_name, rc_target)."""
    mac = t if t.id == DEFAULT_TARGET else Target(DEFAULT_TARGET, t.base)
    L = ["// Generated by tools/targets/targets.py for target %s. Do not edit." % t.id,
         '#include "rc_hook.h"', "",
         'const char rc_target_id[] = "%s";' % t.id, "",
         "// names/hooks.list: this target's address of each hook point and the mac-x64 address of the same",
         "// point (rc_hook_add with a mac-x64 address keeps working on every target)",
         "const rc_hook_name rc_target_hooks[] = {"]
    for name in lists.get("hooks", []):
        a = t.addr(name)
        if a is None:
            continue
        m = mac.addr(name) if mac.resolves(name) else None
        L.append('  {"%s", 0x%xull, 0x%xull},' % (name, a, m or 0))
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
