#!/usr/bin/env python3
"""Check the per-game tables against the shared names (tools/targets/targets.py).

usage: check.py [--quiet]

Fails (exit 1) when
  - a name in names/ has no entry in a target (symbols.map for functions and globals, offsets.map for
    names/offsets.list), unless that target marks it `unsupported` with a reason;
  - an overridden or hooked name is not a function in a target (convention data / code);
  - native code uses a name (RC_ADDR, RC_FN, RC_WFN, RC_NFN, RC_GA, RC_OFF, RC_SLOT, OV_GUEST, OV_ALIAS,
    OV_ARGS_OF, the seam's G, rc_hook_add_name) that no list in names/ has, or uses a symbol as an offset or the
    reverse;
  - code built for one target alone (TARGET_SOURCES: Learn & Play's Win32 seam and built-in fixes) uses a name
    that target marks `unsupported`: it would not compile there;
  - a target's map has a name that no list has (stale), or a list has a name twice;
  - a hook point of a target with another ABI has a prototype the runtime can't show a hook (targets.py hook_abi).
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
SOURCES = ["tools/ws7/recomp/overrides/**/*.[ch]", "tools/ws7/recomp/app/*.c", "tools/ws7/recomp/app/*.h",
           "tools/ws7/recomp/test/hooks/probe_mod.c", "shims/game/include/*.h", "shims/game/harness/*.[ch]", "shims/game/test/*.[ch]"]
# Sources built for one target alone. What they name has to be in that target's maps: a name it marks
# `unsupported` has no value there. (The sources above are written for every target, or are a mod's, which looks
# a name up when it runs and can ask whether the game has it.)
TARGET_SOURCES = {"win-x86": ["win-x86/shims/*.[ch]", "win-x86/shims/*/*.[ch]", "win-x86/app/*.[chm]",
                              "win-x86/online/*.[ch]"]}
SYM_USE = re.compile(r"\b(?:RC_ADDR|RC_FN|RC_WFN|RC_NFN|RC_GA|RC_CC|OV_GUEST|OV_GUESTV|OV_ALIAS|OV_ARGS_OF)\(\s*([A-Za-z_]\w*)")
SEAM_USE = re.compile(r"\bG0?\(\s*([a-z_]\w*)")
OFF_USE = re.compile(r"\bRC_OFF\(\s*([A-Za-z_]\w*)")
SLOT_USE = re.compile(r"\bRC_SLOT\(\s*([A-Za-z_]\w*)")
HOOK_USE = re.compile(r"\brc_hook_add_name\(\s*\"(\w+)\"")
MACRO_DEF = re.compile(r"^\s*#\s*define\s+\w+\(")


def source_uses(root=ROOT, patterns=None):
    """{name: [file:line, ...]} for symbols, offsets and hook names the native sources use: every source, or the
    ones `patterns` match."""
    syms, offs, hooks = {}, {}, {}
    files = set()
    for pat in patterns if patterns is not None else SOURCES + [p for v in TARGET_SOURCES.values() for p in v]:
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


MID_ADD = re.compile(r"(?:\b(\w+)\s*=[^;=]*?)?\brc_mid_add_name\(\s*\"(\w+)\"")
MID_READ = re.compile(r"\brc_mid_(value|cont)\(\s*(\w+)\s*,\s*\"(\w+)\"")
MID_ROW = re.compile(r"\{\s*\"(\w+)\"\s*((?:,\s*(?:NULL|\"\w+\")\s*)+)\}")
EDIT_USE = re.compile(r"\brc_edit_enable\(\s*\"(\w+)\"")


def inner_uses(root=ROOT, patterns=None):
    """What the native sources name of the edits and mid points (rc_mid.h): (points, reads, groups).
    points {name: [file:line]} from rc_mid_add_name("NAME", ...) and from a table's rows {"NAME", "value" or NULL,
    ...}; reads [(where, point, kind, name)] with kind "value", "cont" or "either" (a table's row), from
    rc_mid_value(P, "NAME") and rc_mid_cont(P, "NAME") where P is the variable a rc_mid_add_name("POINT") in the
    same file was assigned to, and from the rows; groups {name: [file:line]} from rc_edit_enable("NAME")."""
    points, reads, groups = {}, [], {}
    files = set()
    for pat in patterns if patterns is not None else SOURCES + [p for v in TARGET_SOURCES.values() for p in v]:
        files.update(glob.glob(os.path.join(root, pat), recursive=True))
    for path in sorted(files):
        rel = os.path.relpath(path, root)
        with open(path, errors="replace") as f:
            lines = [ln.split("//", 1)[0] for ln in f.readlines()]
        if not any("rc_mid_" in ln or "rc_edit_enable" in ln for ln in lines):
            continue
        var = {}
        for i, code in enumerate(lines, 1):
            where = "%s:%d" % (rel, i)
            for m in MID_ADD.finditer(code):
                points.setdefault(m.group(2), []).append(where)
                if m.group(1):
                    var[m.group(1)] = m.group(2)
            for m in EDIT_USE.finditer(code):
                groups.setdefault(m.group(1), []).append(where)
        for i, code in enumerate(lines, 1):
            where = "%s:%d" % (rel, i)
            for m in MID_READ.finditer(code):
                if m.group(2) in var:
                    reads.append((where, var[m.group(2)], m.group(1), m.group(3)))
            for m in MID_ROW.finditer(code):
                points.setdefault(m.group(1), []).append(where)
                reads += [(where, m.group(1), "either", n) for n in re.findall(r'"(\w+)"', m.group(2))]
    return points, reads, groups


def check(base=targets.HERE, root=ROOT, scan_sources=True):
    """Returns a list of error strings."""
    errs = []
    try:
        lists = targets.name_lists(base)
        tids = targets.target_ids(base)
        tgts = [targets.Target(t, base) for t in tids]
    except (targets.TargetError, ValueError) as e:
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
    inner = ("edits", "mids")   # edits.map and mids.map, not symbols (targets.py)
    sym_names = set(n for k, v in lists.items() if k != "offsets" and k not in inner for n in v)
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
        for n in lists.get("hooks", []):
            try:
                if n in t.symbols:
                    targets.hook_abi(t, n, next((m for m in tgts if m.id == targets.DEFAULT_TARGET), None))
            except targets.TargetError as e:
                errs.append(str(e))
        for n in sorted(set(t.symbols) - sym_names):
            errs.append("target %s: symbols.map has %s, which no list in names/ has" % (t.id, n))
        for n in sorted(set(t.offsets) - off_names):
            errs.append("target %s: offsets.map has %s, which names/offsets.list does not have" % (t.id, n))
        for n in sorted(set(t.unsupported) - sym_names - off_names):
            errs.append("target %s: %s is marked unsupported but no list in names/ has it" % (t.id, n))
        # edit groups and mid points: each name has entries here or a reason it has none, and each entry a name
        groups = set(e[0] for e in t.edits)
        for g in lists.get("edits", []):
            if g not in groups and g not in t.inner_unsupported:
                errs.append("target %s: edit group %s has no edits (add them to %s/edits.map, or `%s unsupported REASON`)"
                            % (t.id, g, t.id, g))
        for n in lists.get("mids", []):
            if n not in t.mids and n not in t.inner_unsupported and n not in t.inner_none:
                errs.append("target %s: no mid point %s (add it to %s/mids.map, `%s none REASON` or `%s unsupported "
                            "REASON`)" % (t.id, n, t.id, n, n))
        for n in sorted((groups | set(t.mids) | set(t.inner_unsupported) | set(t.inner_none))
                        - set(lists.get("edits", [])) - set(lists.get("mids", []))):
            errs.append("target %s: %s is in edits.map or mids.map but not in names/edits.list or mids.list" % (t.id, n))
        try:
            targets.inner_points(t, lists)
        except targets.TargetError as e:
            errs.append(str(e))
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
        # the mid points, their values and continuations, and the edit groups the built-in code names: a name the
        # tables lack is a fix that is off when the game runs, in the one edition whose table lacks it
        points, reads, groups = inner_uses(root)
        for n, where in sorted(points.items()):
            if n not in lists.get("mids", []):
                errs.append("%s: mid point %s is not in names/mids.list" % (where[0], n))
        for n, where in sorted(groups.items()):
            if n not in lists.get("edits", []):
                errs.append("%s: edit group %s is not in names/edits.list" % (where[0], n))
        for t in tgts:
            for where, point, kind, n in reads:
                if point not in t.mids:
                    continue  # not in this game (none, unsupported), or not a point at all (said above)
                vals, conts = set(v[1] for v in t.mids[point][1]), set(c[0] for c in t.mids[point][2])
                if n not in (vals if kind == "value" else conts if kind == "cont" else vals | conts):
                    errs.append("%s: target %s: mid point %s has no %s %s (%s/mids.map)" % (
                        where, t.id, point, "value or continuation" if kind == "either" else
                        "value" if kind == "value" else "continuation", n, t.id))
        for t in tgts:
            syms, offs, _ = source_uses(root, TARGET_SOURCES.get(t.id, []))
            for n, where in sorted(list(syms.items()) + list(offs.items())):
                if n in t.unsupported:
                    errs.append("%s: %s is used in code built for %s alone, where it is unsupported (%s)"
                                % (where[0], n, t.id, t.unsupported[n]))
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
