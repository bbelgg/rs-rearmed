#!/usr/bin/env python3
"""Map the Learn & Play imports: every IAT slot and every VMProtect import wrapper call site.

VMProtect leaves the game's IAT (the start of .rdata) in place but unresolved, and rewrites most
import calls. What the harvest shows (lp_harvest.py explains the layout):

  * The IAT is 18 groups, one per DLL, each ended by a zero word. The DLLs are the 18 the packed
    file still imports (VMProtect keeps one import per DLL, in the file's import directory); the
    groups are in ASCII order of the DLL names, as the linker emits them (KERNEL32 < MSVCP100 <
    MSVCR100 < ... < WS2_32 < bink2w32 < d3dx9_42 < ole32 < steam_api).
  * A slot VMProtect did not protect still holds its hint/name RVA (the names themselves are
    encrypted); `call [slot]` in .text reaches it directly. A protected slot holds noise, and
    each of its call sites became `call <stub in .UBX0>`, a stub generated for that one site.
  * The hint/name entries keep their size: hint (2) + name + NUL, padded to even with a zero
    byte that is not encrypted, so the first odd-offset zero after an entry's start bounds its
    name's length (`padlen` in the map: the length, or an odd length below it).
  * Wrapper forms, from the stub's first instruction:
      pop r   the site is `push r; call stub` (6 bytes, the push is filler): stub_pop
      push r  the site is `call stub; <1 junk byte>`: stub_push
      xchg [esp], r   (rare; another form, not rewritten here): stub_xchg
    The stub's code chains into .UBX0 pages the harvest does not have, so its target cannot be
    computed from this harvest; wrapper rows stay unresolved unless names.tsv names them.

Names come from names.tsv (curated: call-site evidence checked against the name length); a slot
or wrapper without a row there is written with an empty name.

Usage: lp_iat.py HARVEST PACKED_EXE [-o iat_map.tsv]
  HARVEST     the page harvest           (or $LP_HARVEST)
  PACKED_EXE  the packed Rocksmith2014.exe, for its DLL list   (or $LP_PACKED)
"""
import argparse
import collections
import os
import re
import struct
import sys

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, here)
sys.path.insert(0, os.path.join(here, "..", "..", "tools", "ws7", "recomp"))
from lp_harvest import Game  # noqa: E402

try:
    import capstone
except ImportError:
    capstone = None


def dll_list(packed):
    """The DLL names the packed file imports, as spelled there, minus duplicates (VMProtect adds
    descriptors of its own for KERNEL32)."""
    from xlat.pe import Image
    img = Image(packed)
    rva, _ = img.dir(1)
    names, a = [], img.base + rva
    while img.u32(a + 12) or img.u32(a + 16):
        d = img.cstr(img.base + img.u32(a + 12))
        if d.lower() not in [n.lower() for n in names]:
            names.append(d)
        a += 20
    return names


def iat_groups(g):
    """[(dll_index, [slot, ...]), ...] walking .rdata's start until two zero words."""
    groups, cur, a = [], [], g.rdata[0]
    while True:
        if g.u32(a) == 0:
            groups.append(cur)
            cur = []
            if g.u32(a + 4) == 0:
                return groups
        else:
            cur.append(a)
        a += 4


def assign_dlls(groups, dlls):
    """Groups are the DLLs in ASCII order of their names as the import libraries spell them."""
    if len(groups) != len(dlls):
        raise SystemExit("IAT has %d groups but the packed file imports %d DLLs" % (len(groups), len(dlls)))
    order = sorted(dlls)
    return {s: order[k].lower() for k, grp in enumerate(groups) for s in grp}


def name_lengths(g, kind):
    """slot -> L for slots that still hold their RVA: the name is L characters long, or an odd
    length below L. (The first zero byte at an odd offset is the pad of this entry when its name
    has an even length, else of a later entry; an odd-length name has no pad.)"""
    hi = g.rdata[1] - g.base
    out = {}
    for s, (k, r) in kind.items():
        if k != "name":
            continue
        blob = g.read(g.base + r, min(0x100, hi - r))
        if not any(blob[:8]):
            continue                        # an uncaptured page
        z = next((k for k in range(3, len(blob), 2) if blob[k] == 0), None)
        if z is not None:
            out[s] = z - 3
    return out


def direct_refs(g):
    """slot -> {'call': n, 'jmp': n, 'load': n} from the instruction bytes in .text."""
    t = g.read(g.text[0], g.text[1] - g.text[0])
    lo, hi = g.rdata[0], g.rdata[0] + 0x1000
    out = collections.defaultdict(collections.Counter)
    for m in re.finditer(rb"\xff[\x15\x25]", t, re.S):
        s = struct.unpack_from("<I", t, m.start() + 2)[0]
        if lo <= s < hi:
            out[s]["call" if t[m.start() + 1] == 0x15 else "jmp"] += 1
    for m in re.finditer(rb"\x8b[\x05\x0d\x15\x1d\x25\x2d\x35\x3d]|\xa1", t, re.S):
        s = struct.unpack_from("<I", t, m.end())[0]
        if lo <= s < hi:
            out[s]["load"] += 1
    return out


def wrapper_sites(g):
    """Linear sweep of .text: [(site, stub, form)] for every `call rel32` into .UBX0."""
    if capstone is None:
        raise SystemExit("capstone is needed for the wrapper sweep (pip install capstone)")
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.skipdata = True
    lo, hi = g.text[0], g.text[1]
    code = g.read(lo, hi - lo)
    out, a = [], lo
    while a < hi:
        nxt = a + 1
        for i in md.disasm(code[a - lo:a - lo + 0x10000], a):
            nxt = i.address + i.size
            if i.size == 5 and i.bytes[0] == 0xe8:
                d = (i.address + 5 + struct.unpack_from("<i", i.bytes, 1)[0]) & 0xffffffff
                if g.ubx0[0] <= d < g.ubx0[1]:
                    form = stub_form(g, d)
                    out.append((i.address, d, form))
                    if form == "stub_push":         # a junk byte follows the call
                        nxt += 1
                        break
        a = nxt
    return out


def stub_form(g, stub):
    if not g.has(stub, 3):
        return "uncaptured"
    b = g.u8(stub)
    if 0x50 <= b <= 0x57:
        return "stub_push"
    if 0x58 <= b <= 0x5f:
        return "stub_pop"
    if b == 0x87 and g.read(stub + 1, 2)[1:] == b"\x24" and g.u8(stub + 1) & 0xc7 == 0x04:
        return "stub_xchg"
    return "other"


def site_shape(g, site, form):
    """The bytes VMProtect rewrote, and the instruction they stood for (best guess for
    uncaptured stubs: a preceding push byte means the push form; it is right for 2/3 of the
    captured ones)."""
    pre = g.u8(site - 1)
    if form == "uncaptured":
        form = "guess_pop" if 0x50 <= pre <= 0x57 else "guess_push"
    start = site - 1 if form in ("stub_pop", "guess_pop") else site
    if form not in ("stub_pop", "guess_pop", "stub_push", "guess_push"):
        return form, None, None
    after = g.read(start + 6, 4)
    op = "jmp" if after[:1] in (b"\xcc", b"\x90") else "call"   # padding after: a tail jump
    return form, start, op


def load_names(path):
    """names.tsv: KEY  NAME  CONFIDENCE  EVIDENCE   (KEY = slot or wrapper site, hex)."""
    out = {}
    if not os.path.exists(path):
        return out
    for line in open(path):
        line = line.rstrip("\n")
        if not line or line.startswith("#"):
            continue
        f = line.split("\t")
        out[int(f[0], 16)] = (f[1], f[2], f[3] if len(f) > 3 else "")
    return out


def build(harvest, packed):
    g = Game(harvest)
    groups = iat_groups(g)
    slot_dll = assign_dlls(groups, dll_list(packed))
    kind = {}
    for grp in groups:
        for s in grp:
            v = g.u32(s)
            if g.rdata[0] - g.base <= v < g.rdata[1] - g.base:
                kind[s] = ("name", v)
            elif v & 0x80000000 and not v & 0x7fff0000:
                kind[s] = ("ordinal", v & 0xffff)
            else:
                kind[s] = ("protected", v)
    return g, groups, slot_dll, kind, name_lengths(g, kind), direct_refs(g), wrapper_sites(g)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("harvest", nargs="?", default=os.environ.get("LP_HARVEST"))
    ap.add_argument("packed", nargs="?", default=os.environ.get("LP_PACKED"))
    ap.add_argument("-o", "--out", default=os.path.join(here, "iat_map.tsv"))
    ap.add_argument("--names", default=os.path.join(here, "names.tsv"))
    args = ap.parse_args()
    if not args.harvest or not args.packed:
        ap.error("need HARVEST and PACKED_EXE (or $LP_HARVEST / $LP_PACKED)")
    g, groups, slot_dll, kind, lens, refs, wraps = build(args.harvest, args.packed)
    names = load_names(args.names)

    rows = ["# Generated by win-x86/imports/lp_iat.py; names from names.tsv. Do not edit; regenerate.",
            "# kind  addr  dll  name  confidence  detail"]
    stat = collections.Counter()
    for s in sorted(kind):
        k, v = kind[s]
        dll = slot_dll[s]
        nm, conf, ev = names.get(s, ("", "unknown", ""))
        if k == "ordinal" and not nm:
            nm, conf = "#%d" % v, "high"
        r = refs.get(s, {})
        ln = lens.get(s)
        detail = "slot=%s call=%d jmp=%d load=%d" % (k, r.get("call", 0), r.get("jmp", 0), r.get("load", 0))
        if ln is not None:
            detail += " padlen=%d" % ln
        if ev:
            detail += " | " + ev
        rows.append("slot\t0x%08x\t%s\t%s\t%s\t%s" % (s, dll, nm, conf, detail))
        stat[("slot", k, conf)] += 1
    for site, stub, form in wraps:
        form, start, op = site_shape(g, site, form)
        nm, conf, ev = names.get(site, ("", "unknown", ""))
        dll = nm.split("!")[0] if "!" in nm else ""
        nm = nm.split("!")[1] if "!" in nm else nm
        detail = "stub=0x%08x form=%s" % (stub, form)
        if start is not None:
            detail += " patch=0x%08x op=%s" % (start, op)
        if ev:
            detail += " | " + ev
        rows.append("wrap\t0x%08x\t%s\t%s\t%s\t%s" % (site, dll, nm, conf, detail))
        stat[("wrap", form, conf)] += 1
    with open(args.out, "w") as f:
        f.write("\n".join(rows) + "\n")
    print("%s: %d IAT slots in %d DLL groups, %d wrapper sites" % (args.out, len(kind), len(groups), len(wraps)))
    for k, n in sorted(stat.items()):
        print("  %-5s %-12s %-8s %d" % (k[0], k[1], k[2], n))


if __name__ == "__main__":
    main()
