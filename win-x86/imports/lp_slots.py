#!/usr/bin/env python3
"""Name the Learn & Play import slots from a harvester v2 capture.

Each slot in iat_map.tsv is read from the _last image (the import table as the game filled it in)
and the address it holds is looked up among the exports the harvester read from every loaded
module. One address can carry several names: a DLL's own export, and other DLLs' exports that
forward to it (kernel32!EnterCriticalSection -> NTDLL.RtlEnterCriticalSection, kernel32 ->
api-ms-win-* -> kernelbase). The name is taken in the DLL the slot's group belongs to when it has
one, so slots read as the game imported them.

source column:
  export     the slot holds an export of its group's DLL
  forwarded  the slot holds another DLL's export, and the group's DLL forwards that name to it
  samename   the slot holds another DLL's export whose name the group's DLL also exports
  alias      the slot holds an export of another DLL only (name kept, DLL as found)
  ordinal    as export, but the export has no name
  game       the slot points into the game image (a VMProtect stub): not named here
  unknown    the address is in no listed module
  empty      the slot is zero in the capture

Usage: lp_slots.py HARVEST_DIR [IAT_MAP] [OUT]
  IAT_MAP defaults to iat_map.tsv next to this script; OUT to stdout.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import lp_harvest2


def base_name(dll):
    """'KERNEL32.DLL' / 'kernel32' / 'api-ms-win-core-synch-l1-2-0.dll' -> lower name without .dll"""
    d = dll.lower()
    return d[:-4] if d.endswith(".dll") else d


class Exports:
    """address -> [(module, name, ordinal)], plus forwarders (module, name) -> 'TARGET.name'."""

    def __init__(self, rows):
        self.at = {}
        self.fwd = {}
        self.names = {}                 # module -> set of names it exports (forwarded or not)
        for mod, ordinal, name, addr, fwd in rows:
            m = base_name(mod)
            if name:
                self.names.setdefault(m, set()).add(name)
            if fwd:
                self.fwd[(m, name)] = fwd
            else:
                self.at.setdefault(addr, []).append((m, name, ordinal))

    def forwards_to(self, mod, name, target_mod, target_name):
        """Does mod!name forward (directly, or through an api-ms-win set) to target_mod!target_name?"""
        f = self.fwd.get((mod, name))
        for _ in range(4):
            if not f or "." not in f:
                return False
            tm, tn = f.split(".", 1)
            tm = base_name(tm)
            if tn == target_name and (tm == target_mod or tm.startswith("api-ms-win-") or tm.startswith("ext-ms-")):
                return True
            f = self.fwd.get((tm, tn))
        return False


def read_slots(path):
    """[(slot_link_va, dll)] in table order."""
    out = []
    for line in open(path):
        if line.startswith("#"):
            continue
        r = line.rstrip("\n").split("\t")
        if r[0] == "slot":
            out.append((int(r[1], 16), r[2]))
    return out


def resolve(im, ex, slot, dll):
    """(name, dll, source) for one slot."""
    group = base_name(dll)
    va = slot + im.delta
    if not im.has(va, 4):
        return "", dll, "empty"
    v = im.u32(va)
    if v == 0:
        return "", dll, "empty"
    if im.base <= v < im.end:
        return "0x%08x" % im.link(v), dll, "game"
    cands = ex.at.get(v)
    if not cands:
        hit = im.module_of(v)
        return ("%s+0x%x" % hit) if hit else "0x%08x" % v, dll, "unknown"
    for m, name, ordinal in cands:
        if m == group:
            return (name, dll, "export") if name else ("#%d" % ordinal, dll, "ordinal")
    for m, name, ordinal in cands:
        if name and ex.forwards_to(group, name, m, name):
            return name, dll, "forwarded"
    # The group's DLL may export the API under its own name while forwarding to a differently
    # named one (kernel32!EnterCriticalSection -> NTDLL.RtlEnterCriticalSection).
    for m, name, ordinal in cands:
        for gname in ex.names.get(group, ()):
            f = ex.fwd.get((group, gname))
            if f and f.split(".", 1)[-1] == name:
                return gname, dll, "forwarded"
    # Same name in the group's DLL as its own export (kernel32!GetTickCount next to
    # kernelbase!GetTickCount): the slot was bound through a different path to the same API.
    for m, name, ordinal in cands:
        if name and name in ex.names.get(group, ()):
            return name, dll, "samename"
    m, name, ordinal = cands[0]
    return name or "#%d" % ordinal, m + ".dll", "alias"


def build(harvest, iat_map):
    im = lp_harvest2.Image(harvest)
    ex = Exports(lp_harvest2.read_exports(im.exports_path))
    rows = []
    for slot, dll in read_slots(iat_map):
        name, d, src = resolve(im, ex, slot, dll)
        rows.append((slot, d, name, src))
    return rows


def main():
    if len(sys.argv) < 2:
        print(__doc__.strip().splitlines()[-2])
        sys.exit(2)
    harvest = sys.argv[1]
    iat_map = sys.argv[2] if len(sys.argv) > 2 else os.path.join(HERE, "iat_map.tsv")
    rows = build(harvest, lp_harvest2.need(iat_map, "IAT map"))
    out = open(sys.argv[3], "w") if len(sys.argv) > 3 else sys.stdout
    out.write("# Generated by win-x86/imports/lp_slots.py from a harvester v2 capture. Do not edit; regenerate.\n")
    out.write("# slot_va\tdll\tname\tsource\n")
    for slot, d, name, src in rows:
        out.write("0x%08x\t%s\t%s\t%s\n" % (slot, d, name, src))
    counts = {}
    for r in rows:
        counts[r[3]] = counts.get(r[3], 0) + 1
    sys.stderr.write("%d slots: %s\n" % (len(rows), ", ".join("%s %d" % kv for kv in sorted(counts.items()))))


if __name__ == "__main__":
    main()
