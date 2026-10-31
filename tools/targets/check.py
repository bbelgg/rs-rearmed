#!/usr/bin/env python3
"""Check the per-game tables against the shared names (tools/targets/targets.py).

usage: check.py [--quiet]

Fails (exit 1) when
  - a name in names/ has no entry in a target (symbols.map for functions and globals, offsets.map for
    names/offsets.list), unless that target marks it `unsupported` with a reason;
  - an overridden or hooked name is not a function in a target (convention data / code);
  - native code uses a name (RC_ADDR, RC_FN, RC_GA, RC_OFF, RC_SLOT, OV_GUEST, OV_ALIAS, OV_ARGS_OF, the
    seam's G, rc_hook_add_name) that no list in names/ has, or uses a symbol as an offset or the reverse;
  - a target's map has a name that no list has (stale), or a list has a name twice.
So adding a game function to the build means: its name in names/, its address in every target.
"""
import glob
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import targets  # noqa: E402

ROOT = targets.ROOT
SOURCES = ["tools/ws7/recomp/overrides/**/*.[ch]", "tools/ws7/recomp/app/*.c", "tools/ws7/recomp/app/*.h"]
SYM_USE = re.compile(r"\b(?:RC_ADDR|RC_FN|RC_GA|RC_CC|OV_GUEST|OV_GUESTV|OV_ALIAS|OV_ARGS_OF)\(\s*([A-Za-z_]\w*)")
SEAM_USE = re.compile(r"\bG0?\(\s*([a-z_]\w*)")
OFF_USE = re.compile(r"\bRC_OFF\(\s*([A-Za-z_]\w*)")
SLOT_USE = re.compile(r"\bRC_SLOT\(\s*([A-Za-z_]\w*)")
HOOK_USE = re.compile(r"\brc_hook_add_name\(\s*\"(\w+)\"")
MACRO_DEF = re.compile(r"^\s*#\s*define\s+\w+\(")


def source_uses(root=ROOT):
    """{name: [file:line, ...]} for symbols, offsets and hook names the native sources use."""
    syms, offs, hooks = {}, {}, {}
    files = set()
    for pat in SOURCES:
        files.update(glob.glob(os.path.join(root, pat), recursive=True))
    for path in sorted(files):
        rel = os.path.relpath(path, root)
        with open(path, errors="replace") as f:
            lines = f.readlines()
        for i, ln in enumerate(lines, 1):
            code = ln.split("//", 1)[0]
            if MACRO_DEF.match(code) and re.search(r"\((?:name|n)\b", code):
                continue  # a macro taking a name parameter
            where = "%s:%d" % (rel, i)
            for m in SYM_USE.finditer(code):
                syms.setdefault(m.group(1), []).append(where)
            if rel.endswith("seam_platform.c"):
                for m in SEAM_USE.finditer(code):
                    syms.setdefault(m.group(1), []).append(where)
            for m in OFF_USE.finditer(code):
                offs.setdefault(m.group(1), []).append(where)
            for m in SLOT_USE.finditer(code):
                offs.setdefault(m.group(1) + "_slot", []).append(where)
            for m in HOOK_USE.finditer(code):
                hooks.setdefault(m.group(1), []).append(where)
    for n in ("name", "n"):
        syms.pop(n, None)
        offs.pop(n, None)
    return syms, offs, hooks


def check(base=targets.HERE, root=ROOT, scan_sources=True):
    """Returns a list of error strings."""
    errs = []
    try:
        lists = targets.name_lists(base)
        tids = targets.target_ids(base)
        tgts = [targets.Target(t, base) for t in tids]
    except targets.TargetError as e:
        return [str(e)]
    if not tids:
        errs.append("no targets in %s" % base)
    for key, names in lists.items():
        seen = set()
        for n in names:
            if n in seen:
                errs.append("names/%s.list: %s listed twice" % (key, n))
            seen.add(n)
    off_names = set(lists.get("offsets", []))
    sym_names = set(n for k, v in lists.items() if k != "offsets" for n in v)
    func_names = set(n for k, v in lists.items() if k.startswith("overrides/") or k == "hooks" for n in v)
    for t in tgts:
        for n in sorted(sym_names):
            if n in t.unsupported:
                continue
            if n not in t.symbols:
                errs.append("target %s: no symbol %s (add it to %s/symbols.map, or `%s unsupported REASON`)"
                            % (t.id, n, t.id, n))
            elif n in func_names and t.symbols[n][1] in ("data", "code"):
                errs.append("target %s: %s is overridden or hooked but is not a function (%s)" % (t.id, n, t.symbols[n][1]))
        for n in sorted(off_names):
            if n not in t.offsets and n not in t.unsupported:
                errs.append("target %s: no offset %s (add it to %s/offsets.map, or `%s unsupported REASON`)"
                            % (t.id, n, t.id, n))
        for n in sorted(set(t.symbols) - sym_names):
            errs.append("target %s: symbols.map has %s, which no list in names/ has" % (t.id, n))
        for n in sorted(set(t.offsets) - off_names):
            errs.append("target %s: offsets.map has %s, which names/offsets.list does not have" % (t.id, n))
        for n in sorted(set(t.unsupported) - sym_names - off_names):
            errs.append("target %s: %s is marked unsupported but no list in names/ has it" % (t.id, n))
    if scan_sources:
        syms, offs, hooks = source_uses(root)
        for n, where in sorted(syms.items()):
            if n not in sym_names:
                errs.append("%s: %s is used as a game symbol but no list in tools/targets/names has it%s"
                            % (where[0], n, " (it is an offset)" if n in off_names else ""))
        for n, where in sorted(offs.items()):
            if n not in off_names:
                errs.append("%s: %s is used as an offset but names/offsets.list does not have it" % (where[0], n))
        for n, where in sorted(hooks.items()):
            if n not in lists.get("hooks", []):
                errs.append("%s: hook point %s is not in names/hooks.list" % (where[0], n))
    return errs


def main(argv=()):
    errs = check()
    for e in errs:
        print("check: %s" % e, file=sys.stderr)
    if errs:
        print("check: %d problem(s)" % len(errs), file=sys.stderr)
        return 1
    if "--quiet" not in argv:
        lists = targets.name_lists()
        print("check: %d names in %d lists resolve in %s" % (sum(len(v) for v in lists.values()), len(lists),
                                                            ", ".join(targets.target_ids())))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
