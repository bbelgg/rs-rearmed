#!/usr/bin/env python3
"""Build the Learn & Play game image: a PE32 of the game alone, from a harvest or the unpacker.

Three sources:

  unpacked  a flat file from win-x86/unpack/lp_unpack.py: the game by RVA, unpacked offline and
      relocated to the base in its header, its .data as at load, with the exact relocation sites
      beside it (FILE.relocs). It is taken like a v2 harvest with a reference.

and two kinds of harvest (win-x86/imports/ reads both):

  v2  a directory from harvester v2 (lp_harvest2.py): the game by RVA, read where Windows
      loaded it, complete, its code relocated to that base. The image is built at that base
      (the recompiler loads an image at any base).
  v1  the first harvester's rs_harvest.bin (lp_harvest.py): SizeOfImage bytes read from
      0x400000 while the game ran elsewhere, so the game is partly there, captured before
      VMProtect relocated it. The image is built at the link base 0x400000.

The image base is the base the code is linked for, by vote: `call/jmp [IAT]` operands in .text
at each candidate base (the link base 0x400000 and the base the game ran at).

What the image holds:

  * the game's true section table (its in-memory header), the captured bytes, everything not
    captured zero; the export and resource directories when they were captured;
  * .data as the game had it at load, as far as the harvest allows. Its tail (.bss) was zero at
    load and holds what the running game wrote: it is zeroed. From a v1 harvest the boundary is
    found by vote (pages first captured after relocation hold pointers for the run base; the
    first page from which on every voting page does is where .bss starts at the latest). For a
    v2 harvest, `--reference V1` gives that boundary, and the reference's initialised .data,
    captured before the game ran, replaces the v2 copy (moved to the image base where the v2
    copy shows the relocation, or where the word points into the image); without a reference
    .data is the v2 first copy, runtime state included;
  * an import directory over the game's real IAT (the 18 groups at the start of .rdata): a slot
    named in slots.tsv (from the v2 capture's exports) or iat_map.tsv imports that name, an
    ordinal slot its ordinal, any other slot the placeholder `lp_unresolved_<link address>`;
  * a relocation table, rebuilt (the game's own is inside VMProtect's packed data): with a
    reference, every word the reference and the v2 copy differ in by exactly the relocation
    delta; and, always, every absolute operand of a linear sweep of .text that points into
    .text/.rdata/.data (into .text only at a padded function start or a call target), every jump
    table, and every aligned word of .rdata/.data that points there. The recompiler needs it to
    find functions reached only through pointers (vtables, callbacks, SEH scope tables);
  * the entry point WinMainCRTStartup (`call __security_init_cookie; jmp __tmainCRTStartup`).

The VMProtect sections (.UBX0/1/2) are written without the execute flag, so the translator does
not take VMProtect's own code for game functions; `--ubx-exec` keeps their real flags.

The tables in win-x86/imports/ hold link addresses (0x400000 + RVA). This script moves them to
the image base: next to OUT it writes OUT-without-.exe + `.wrappers.tsv`, wrappers.tsv at the
image base, for gen_image_pe.py --wrappers (translate.sh picks it up).

The image is derived from the game: build output, never commit it.

Usage: lp_image.py HARVEST OUT.exe [--reference V1_HARVEST] [--report FILE] [options]
  HARVEST may be $LP_HARVEST.
"""
import argparse
import bisect
import collections
import hashlib
import os
import re
import struct
import sys

# (build time stamp, rva, length, sha256 of the bytes, what): data checked after the build
KEY_CHECKS = [
    (0x67497d00, 0xeeea08, 0x4e7, "e79d85222319fe065fcc635534a767c3208b7cffc53da30afd7745ca8cf412aa",
     "SNG signature key blob"),
    (0x67497d00, 0xeee9c0, 0x44, "698585b061dbf16ac414d33d4ef1521b2aff72c4420687a7e6518e1029f2fee5",
     "SNG AES key blob"),
]

here = os.path.dirname(os.path.abspath(__file__))
imports_dir = os.path.join(here, "..", "imports")
sys.path.insert(0, imports_dir)
from lp_harvest import Game, PREFERRED as LINK  # noqa: E402

import capstone  # noqa: E402
from capstone import x86  # noqa: E402

PAGE = 0x1000
FILE_ALIGN = 0x200
SCN_EXEC = 0x20000000
SCN_CODE = 0x20
ENTRY = 0xf5b8f4            # WinMainCRTStartup in the harvested build (link address)
PADDING = (0xcc, 0x90, 0xc3)
M32 = 0xffffffff


def align(v, a):
    return (v + a - 1) & ~(a - 1)


def u32(b, o):
    return struct.unpack_from("<I", b, o)[0]


class Source:
    """A harvest as the game by RVA: img (SizeOfImage bytes), the section table and data
    directories of its in-memory header, the base it ran at, and for v2 the first copy."""

    def __init__(self, path):
        if os.path.isdir(path):
            import lp_harvest2
            im = lp_harvest2.Image(path)
            self.kind, self.run_base, size = "v2", im.base, im.size
            self.img = bytearray(im.img)
            self.first = im.first
            self.captured = size
        elif self.is_unpacked(path):
            with open(path, "rb") as f:
                self.img = bytearray(f.read())
            pe = u32(self.img, 0x3c)
            self.kind, self.run_base = "unpacked", u32(self.img, pe + 24 + 28)
            self.first = bytes(self.img)
            self.captured = len(self.img)
            with open(path + ".relocs", "rb") as f:
                raw = f.read()
            self.sites = struct.unpack("<%dI" % (len(raw) // 4), raw)
        else:
            g = Game(path)
            self.kind, self.run_base = "v1", g.mod.image_base
            size = g.mod.size_of_image
            self.img = bytearray(size)
            n = min(g.captured, size)
            self.img[:n] = g.img[:n]
            self.first = None
            self.captured = n
        h = self.img
        pe = u32(h, 0x3c)
        nsect, optsz = struct.unpack_from("<H", h, pe + 6)[0], struct.unpack_from("<H", h, pe + 20)[0]
        self.stamp = u32(h, pe + 8)
        opt = pe + 24
        self.size = u32(h, opt + 56)
        self.dirs = [struct.unpack_from("<II", h, opt + 96 + 8 * k) for k in range(16)]
        self.secs = []
        s = opt + optsz
        for _ in range(nsect):
            self.secs.append((h[s:s + 8].rstrip(b"\0").decode("latin-1"), u32(h, s + 12), u32(h, s + 8), u32(h, s + 36)))
            s += 40

    @staticmethod
    def is_unpacked(path):
        """A flat image starts with the game's own header and is exactly SizeOfImage long."""
        with open(path, "rb") as f:
            head = f.read(0x1000)
        if head[:2] != b"MZ":
            return False
        return os.path.getsize(path) == u32(head, u32(head, 0x3c) + 24 + 56)

    def has(self, rva, n):
        return rva + n <= self.captured and any(self.img[rva:rva + n])


class Layout:
    """The game's sections at an image base, and the address ranges pointers are judged by.
    delta is the other candidate base minus this one: a word linked for the other base, moved
    by -delta, is a pointer at this one."""

    def __init__(self, src, base, other):
        self.base, self.size = base, src.size
        self.delta = (other - base) & M32
        self.secs = [(n, base + rva, vs, ch) for n, rva, vs, ch in src.secs]
        s = {n: (va, va + vs) for n, va, vs, _ in self.secs}
        self.text, self.rdata, self.data = s[".text"], s[".rdata"], s[".data"]
        self.lo, self.hi = self.text[0], self.data[1]        # what game pointers point into

    def target(self, v):
        return self.lo <= v < self.hi


def known_targets(img, L):
    """Base-independent evidence: call rel32 targets in .text (relative, so they do not move);
    and, at L.base, vtables stored by `mov [r], imm`, literals pushed and globals named."""
    t0, t1 = L.text
    code = img[t0 - L.base:t1 - L.base]
    calls, linked = set(), set()
    for m in re.finditer(rb"\xe8", code):
        o = m.start()
        if o + 5 > len(code):
            break
        t = (t0 + o + 5 + struct.unpack_from("<i", code, o + 1)[0]) & M32
        if t0 < t < t1 and img[t - L.base - 1] in PADDING:
            calls.add(t)
    for m in re.finditer(rb"\xc7[\x00-\x07](....)|\x68(....)", code, re.S):
        v = struct.unpack("<I", m.group(1) or m.group(2))[0]
        if L.rdata[0] <= v < L.data[1]:
            linked.add(v)
    for m in re.finditer(rb"[\xa1\xa3](....)|[\x8b\x89\xc7\x8d][\x05\x0d\x15\x1d\x25\x2d\x35\x3d](....)", code, re.S):
        v = struct.unpack("<I", m.group(1) or m.group(2))[0]
        if L.data[0] <= v < L.data[1]:
            linked.add(v)
    return calls, linked


def padded_start(img, L, a):
    """a is in .text where a function starts: right after int3 padding, or a frame setup
    (`push ebp; mov ebp, esp`) right after a ret."""
    if not L.text[0] < a < L.text[1] - 3:
        return False
    o = a - L.base
    if img[o - 1] == 0xcc:
        return img[o] not in (0xcc, 0)
    return img[o - 1] == 0xc3 and img[o:o + 3] == b"\x55\x8b\xec"


def page_votes(img, L, lo, hi, linked, calls):
    """page -> [this-base votes, other-base votes]: distinct aligned words that are known targets
    (`linked`: vtables, literals, globals the code names) at one base and not the other. A
    this-base vote does not count when the word moved by -delta is a function start: a function
    pointer the running game stored can look like a literal at this base. (Function starts are
    not votes themselves: they are dense enough in .text that 16-byte aligned data pointers
    moved by the delta land on them by chance.)"""
    out = {}
    for p in range(lo & ~(PAGE - 1), hi, PAGE):
        o = p - L.base
        pg = img[o:o + PAGE]
        if len(pg) < PAGE or not any(pg):
            continue
        a, b = set(), set()
        for k in range(0, PAGE, 4):
            v = u32(pg, k)
            w = (v - L.delta) & M32
            ia, ib = v in linked and not (w in calls or padded_start(img, L, w)), w in linked
            if ia and not ib:
                a.add(v)
            elif ib and not ia:
                b.add(v)
        out[p] = [len(a), len(b)]
    return out


def text_votes(img, L, iat):
    """[this-base, other-base] counts of `call [slot]` / `jmp [slot]` in .text: the IAT is the
    one table every part of the code points at."""
    t0, t1 = L.text
    code = img[t0 - L.base:t1 - L.base]
    lo, hi = iat
    out = [0, 0]
    for m in re.finditer(rb"\xff[\x15\x25](....)", code, re.S):
        v = struct.unpack("<I", m.group(1))[0]
        if lo <= v < hi:
            out[0] += 1
        elif lo <= (v - L.delta) & M32 < hi:
            out[1] += 1
    return out


def bss_boundary(img, L, say):
    """The address .data's runtime tail starts at (pages linked for the other base after the
    last page linked for this one), or None. L.base must be the link base the initialised data
    was captured at."""
    calls, linked = known_targets(img, L)
    lo, hi = L.data
    pv = page_votes(img, L, lo, hi, linked, calls)
    pa = [p for p, v in pv.items() if v[0] >= 2 and v[0] > v[1]]
    pb = [p for p, v in pv.items() if v[1] >= 3 and v[1] > 2 * v[0]]
    say("  .data pages linked for 0x%x: %d, for 0x%x: %d (votes %d / %d)"
        % (L.base, len(pa), (L.base + L.delta) & M32, len(pb),
           sum(v[0] for v in pv.values()), sum(v[1] for v in pv.values())))
    last_a = max(pa) if pa else lo
    tail = [p for p in pb if p > last_a]
    return (min(tail), len(tail)) if tail else (None, 0)


def sweep(img, L, calls):
    """Relocation sites in .text: absolute operands of a linear sweep, and jump tables (`jmp
    [r*4 + table]`: the table's words; the byte index table next to it is skipped)."""
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    md.skipdata = True
    t0, t1 = L.text
    code = bytes(img[t0 - L.base:t1 - L.base])
    tl, th = L.text

    def word(a):
        return u32(code, a - t0)

    def pass_(skip):
        sites, tables = set(), {}
        spans = sorted(skip.items())
        starts = [s for s, _ in spans]
        a = t0
        while a < t1:
            k = bisect.bisect_right(starts, a) - 1
            if k >= 0 and a < spans[k][1]:
                a = spans[k][1]
                continue
            nk = bisect.bisect_right(starts, a)
            stop = min(t1, starts[nk] if nk < len(starts) else t1, a + 0x10000)
            nxt = a + 1
            for i in md.disasm(code[a - t0:stop - t0], a):
                nxt = i.address + i.size
                if i.id == 0:
                    continue
                for op in i.operands:
                    if op.type == x86.X86_OP_MEM and op.mem.base == 0 and op.mem.segment == 0 and i.disp_size == 4:
                        v = op.mem.disp & M32
                        if L.target(v):
                            sites.add(i.address + i.disp_offset)
                            if i.mnemonic == "jmp" and op.mem.index and op.mem.scale == 4 and tl <= v < th:
                                tables[v] = "jump"
                            elif i.mnemonic.startswith("movzx") and op.mem.index == 0 and op.size == 1 and tl <= v < th:
                                tables.setdefault(v, "index")
                    elif op.type == x86.X86_OP_MEM and op.mem.base and op.mem.segment == 0 and i.disp_size == 4:
                        v = op.mem.disp & M32                       # movzx r, byte [r + table]
                        if tl <= v < th and i.mnemonic.startswith("movzx") and op.size == 1:
                            tables.setdefault(v, "index")
                            sites.add(i.address + i.disp_offset)
                        elif L.target(v) and v >= L.rdata[0]:
                            sites.add(i.address + i.disp_offset)
                        elif tl <= v < th and op.mem.index and op.mem.scale == 4 and i.mnemonic == "jmp":
                            tables[v] = "jump"
                            sites.add(i.address + i.disp_offset)
                    elif op.type == x86.X86_OP_IMM and i.imm_size == 4 and not (
                            i.mnemonic in ("call", "jmp") or x86.X86_GRP_JUMP in i.groups):
                        v = op.imm & M32
                        if L.rdata[0] <= v < L.hi or (tl <= v < th and (v in calls or code[v - t0 - 1] in PADDING)):
                            sites.add(i.address + i.imm_offset)
            a = max(nxt, a + 1)
        return sites, tables

    _, tables = pass_({})
    spans, jt = {}, set()
    for t, kind in sorted(tables.items()):
        a = t
        if kind == "jump":
            while a + 4 <= t1 and tl <= word(a) < th and a - t < 4 * 1024:
                jt.add(a)
                a += 4
        else:
            while a < t1 and code[a - t0] != 0xcc and (a == t or a not in tables) and a - t < 4096:
                a += 1
        if a > t:
            spans[t] = max(spans.get(t, t), a)
    sites, _ = pass_(spans)
    sites = {s for s in sites if not any(lo <= s < hi for lo, hi in spans.items())}
    return sites | jt, len([1 for k in tables.values() if k == "jump"]), len(jt)


def data_sites(img, L, lo, hi, skip):
    out = set()
    for a in range(lo, hi - 3, 4):
        if not skip(a) and L.target(u32(img, a - L.base)):
            out.add(a)
    return out


def delta_sites(ref, img, lo, hi, d):
    """RVAs in [lo, hi) where img's dword is ref's plus d (ref captured before relocation, img
    after), on pages ref has."""
    out = []
    for p in range(lo & ~(PAGE - 1), hi, PAGE):
        pa, pb = ref[p:p + PAGE], img[p:p + PAGE]
        if pa == pb or not any(pa):
            continue
        i, e = max(p, lo), min(p + PAGE, hi)
        while i < e:
            if ref[i] == img[i]:
                i += 1
                continue
            for k in range(i - 3, i + 1):
                if k >= lo and k + 4 <= len(img) and (u32(img, k) - u32(ref, k)) & M32 == d:
                    out.append(k)
                    i = k + 4
                    break
            else:
                i += 1
    return out


def reloc_table(sites, base):
    out = bytearray()
    pages = collections.defaultdict(list)
    for s in sorted(sites):
        pages[(s - base) & ~(PAGE - 1)].append((s - base) & (PAGE - 1))
    for p, offs in sorted(pages.items()):
        ent = [0x3000 | o for o in offs]
        if len(ent) % 2:
            ent.append(0)
        out += struct.pack("<II", p, 8 + 2 * len(ent)) + struct.pack("<%dH" % len(ent), *ent)
    return bytes(out)


def read_slots(iat_map, slots_tsv):
    """[(link address, dll, name, kind)]: the IAT slots of iat_map.tsv, named from slots.tsv
    (exports the v2 capture read) where it has a name, else from iat_map."""
    out = {}
    for line in open(iat_map):
        f = line.rstrip("\n").split("\t")
        if f[0] == "slot":
            k = re.search(r"slot=(\w+)", f[5]).group(1)
            out[int(f[1], 16)] = [f[2], f[3], k]
    if slots_tsv and os.path.exists(slots_tsv):
        for line in open(slots_tsv):
            if line.startswith("#") or not line.strip():
                continue
            f = line.rstrip("\n").split("\t")
            a, src = int(f[0], 16), f[3] if len(f) > 3 else ""
            if a in out and src in ("export", "forwarded", "samename", "alias", "ordinal") and f[2]:
                out[a][1] = f[2]
                if f[2].startswith("#"):
                    out[a][2] = "ordinal"
    return [(a, d, n, k) for a, (d, n, k) in sorted(out.items())]


def build_imports(slots, sec_rva, base):
    """The import directory for the IAT groups (contiguous slots of one DLL, zero-ended), slot
    addresses at the image base. Returns (section bytes, {slot: thunk value}, (dir rva, size),
    (iat rva, size))."""
    groups = []
    for s in sorted(slots):
        if groups and groups[-1][-1][0] + 4 == s[0] and groups[-1][-1][1] == s[1]:
            groups[-1].append(s)
        else:
            groups.append([s])
    ndesc = len(groups) + 1
    blob = bytearray(20 * ndesc)
    ilt_off = len(blob)
    str_off = ilt_off + sum(4 * (len(g) + 1) for g in groups)
    strings, ilt, descs, thunks = bytearray(), bytearray(), [], {}

    def add_str(b):
        o = str_off + len(strings)
        strings.extend(b)
        if len(strings) % 2:
            strings.append(0)
        return o
    for g in groups:
        name_rva = sec_rva + add_str(g[0][1].encode() + b"\0")
        first_ilt = ilt_off + len(ilt)
        for addr, _, name, kind in g:
            if kind == "ordinal" and name.startswith("#"):
                v = 0x80000000 | int(name[1:])
            else:
                nm = name or "lp_unresolved_%08x" % (addr - base + LINK)
                v = sec_rva + add_str(b"\0\0" + nm.encode() + b"\0")
            ilt += struct.pack("<I", v)
            thunks[addr] = v
        ilt += b"\0\0\0\0"
        descs.append((sec_rva + first_ilt, name_rva, g[0][0] - base))
    for k, (first_ilt, name_rva, iat_rva) in enumerate(descs):
        struct.pack_into("<IIIII", blob, 20 * k, first_ilt, 0, 0, name_rva, iat_rva)
    blob += ilt + strings
    lo = min(s[0] for s in slots) - base
    hi = max(s[0] for s in slots) + 8 - base
    return bytes(blob), thunks, (sec_rva, 20 * ndesc), (lo, hi - lo)


def write_wrappers(src_path, out_path, shift, size):
    """wrappers.tsv with its addresses (call_site, stub, resume, and the constants of the pushes
    column that point into the image, [0x400000, + size)) moved by shift."""
    n = 0
    with open(out_path, "w") as o:
        o.write("# wrappers.tsv moved to the image base (+0x%x) by win-x86/image/lp_image.py\n" % shift)
        for line in open(src_path):
            f = line.rstrip("\n").split("\t")
            if line.startswith("#") or f[0] == "call_site" or not line.strip():
                o.write(line)
                continue
            for k in (0, 1, 3):
                if k < len(f) and f[k].strip():
                    f[k] = "0x%08x" % ((int(f[k], 16) + shift) & M32)
            if len(f) > 6 and f[6].strip():
                f[6] = ",".join("0x%x" % ((int(t, 16) + shift) & M32) if t.startswith("0x") and 0 <= int(t, 16) - 0x400000 < size
                                else t for t in f[6].split(","))
            o.write("\t".join(f) + "\n")
            n += 1
    return n


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("harvest", nargs="?", default=os.environ.get("LP_HARVEST"))
    ap.add_argument("out")
    ap.add_argument("--reference", help="a v1 harvest of the same build (for a v2 HARVEST)")
    ap.add_argument("--iat-map", default=os.path.join(imports_dir, "iat_map.tsv"))
    ap.add_argument("--slots", default=os.path.join(imports_dir, "slots.tsv"))
    ap.add_argument("--wrappers", default=os.path.join(imports_dir, "wrappers.tsv"))
    ap.add_argument("--entry", type=lambda s: int(s, 16), default=ENTRY, help="link address")
    ap.add_argument("--base", type=lambda s: int(s, 16), default=None, help="override the voted image base")
    ap.add_argument("--bss-from", type=lambda s: int(s, 16), default=None, help="link address .bss starts at")
    ap.add_argument("--keep-runtime-data", action="store_true")
    ap.add_argument("--ubx-exec", action="store_true")
    ap.add_argument("--report", default=None, help="write the evidence summary here too")
    args = ap.parse_args()
    if not args.harvest:
        ap.error("need HARVEST (or $LP_HARVEST)")
    rep = []

    def say(s):
        print(s)
        rep.append(s)

    src = Source(args.harvest)
    if src.stamp not in {k[0] for k in KEY_CHECKS}:   # skip.txt, the overrides and the tables are for one build
        raise SystemExit("the game in %s is build 0x%x, which this tree has no tables for (known: %s)"
                         % (args.harvest, src.stamp, ", ".join(sorted({"0x%x" % k[0] for k in KEY_CHECKS}))))
    img = src.img
    say("%s harvest: ran at 0x%x, SizeOfImage 0x%x, captured RVAs < 0x%x"
        % (src.kind, src.run_base, src.size, src.captured))

    slots = read_slots(args.iat_map, args.slots)
    iat_rva = (min(s[0] for s in slots) - LINK, max(s[0] for s in slots) + 4 - LINK)

    # -- the image base: what .text is linked for ------------------------------------------
    other = src.run_base if src.run_base != LINK else LINK
    Lk = Layout(src, LINK, src.run_base)
    tv = text_votes(img, Lk, (LINK + iat_rva[0], LINK + iat_rva[1]))
    say(".text call/jmp [IAT]: %d at 0x%x, %d at 0x%x" % (tv[0], LINK, tv[1], src.run_base))
    if tv[0] and tv[1]:
        raise SystemExit(".text is linked for both bases (mixed): not handled")
    base = args.base or (LINK if tv[0] >= tv[1] else src.run_base)
    other = src.run_base if base == LINK else LINK
    L = Layout(src, base, other)
    shift = (base - LINK) & M32                     # link address -> image address
    say("image base 0x%x (link addresses + 0x%x)" % (base, shift))

    ref = None
    if args.reference:
        ref = Source(args.reference)
        if ref.stamp != src.stamp:
            raise SystemExit("reference build 0x%x differs from the harvest's 0x%x" % (ref.stamp, src.stamp))

    # -- .data: initial state and the runtime tail -------------------------------------------
    bss = None if args.bss_from is None else args.bss_from - LINK
    lo, hi = L.data[0] - base, L.data[1] - base
    exact = set()
    if base == LINK:
        if L.delta and bss is None:
            b, n = bss_boundary(img, L, say)
            bss = None if b is None else b - base
    else:
        if ref is not None:
            Lr = Layout(ref, LINK, ref.run_base)
            if bss is None:
                b, n = bss_boundary(ref.img, Lr, say)
                bss = None if b is None else b - LINK
            # relocation sites: where the v2 copy is the pre-relocation reference plus the delta
            d = shift
            for name in (".text", ".rdata"):
                s0, s1 = [(r, r + vs) for nm, r, vs, _ in src.secs if nm == name][0]
                exact.update(base + k for k in delta_sites(ref.img, img, s0, s1, d))
            # .data below .bss: the reference (before the game ran), moved where needed
            top = bss if bss is not None else hi
            first = bytearray(src.first)
            kept = moved_ptr = unmoved = 0
            for p in range(lo, top, PAGE):
                rp = ref.img[p:p + PAGE]
                if not any(rp) or not ref.has(p, PAGE):
                    continue
                if not any(first[p:p + PAGE]):
                    first[p:p + PAGE] = bytes(PAGE)
                seen = any(first[p:p + PAGE])  # the v2 copy has this page
                diff = set(delta_sites(ref.img, first, p, min(p + PAGE, top), d))
                new = bytearray(rp)
                for k in range(p, min(p + PAGE, top) - 3, 4):
                    if k in diff:
                        continue
                    v = u32(rp, k - p)
                    if Lr.target(v) and k not in diff:
                        # A word in pointer range that the v2 copy holds unmoved was not relocated
                        # when the game loaded: data that looks like a pointer (key material, packed
                        # values), kept as it is. Moving it corrupted the SNG signature key.
                        if seen and u32(first, k) == v:
                            unmoved += 1
                            continue
                        diff.add(k)
                        moved_ptr += 1
                for k in diff:
                    if k + 4 <= p + PAGE:
                        struct.pack_into("<I", new, k - p, (u32(rp, k - p) + d) & M32)
                img[p:p + PAGE] = new
                exact.update(base + k for k in diff if k + 4 <= p + PAGE)
                kept += 1
            say("  .data below .bss from the reference: %d pages, %d words moved to the image base "
                "(%d by pointer range alone; %d in pointer range kept: the v2 copy has them unmoved)"
                % (kept, len([1 for e in exact if L.data[0] <= e < L.data[1]]), moved_ptr, unmoved))
            say("  exact relocation sites (reference vs harvest): %d" % len(exact))
        elif src.kind == "unpacked":
            exact.update(base + r for r in src.sites
                         if any(a <= base + r < b for a, b in (L.text, L.rdata, L.data)))
            say("  .data as at load; exact relocation sites from the unpacker: %d" % len(exact))
        else:
            for p in range(lo, hi, PAGE):            # the first copy: closest to the load state
                if any(src.first[p:p + PAGE]):
                    img[p:p + PAGE] = src.first[p:p + PAGE]
            say("  no --reference: .data is the harvest's first copy, runtime state included")
    if bss is not None and not args.keep_runtime_data:
        img[bss:hi] = bytes(hi - bss)
        say("  .bss from 0x%x (link 0x%x) zeroed: runtime state" % (base + bss, LINK + bss))
    elif bss is None:
        say("  .bss boundary unknown: .data kept whole")

    # -- relocations ------------------------------------------------------------------------
    calls, _ = known_targets(img, L)
    tsites, njt, njtw = sweep(img, L, calls)
    slotset = {a + shift for a, _, _, _ in slots}
    iat = (base + iat_rva[0], base + iat_rva[1])

    def in_iat(a):
        return iat[0] <= a < iat[1] + 4 or a in slotset
    dsites = data_sites(img, L, L.rdata[0], L.rdata[1], in_iat) | \
        data_sites(img, L, L.data[0], L.data[1], lambda a: False)
    sites = tsites | dsites | {e for e in exact if not in_iat(e)}
    say("relocations: %d (sweep of .text %d incl. %d jump tables / %d words; .rdata/.data words %d; exact %d)"
        % (len(sites), len(tsites), njt, njtw, len(dsites), len(exact)))

    # -- integrity of data the game checks songs with --------------------------------------
    # The SNG signature key (byte-pair-encoded DSA public key, read by the song loader): one
    # wrong byte and every song fails its signature check, and the game waits on the stage
    # forever. The hash is of the bytes, not the key.
    for stamp, rva, n, digest, what in KEY_CHECKS:
        got = hashlib.sha256(bytes(img[rva:rva + n])).hexdigest()
        if got != digest:
            raise SystemExit("%s at 0x%x (rva 0x%x, 0x%x bytes) does not match the game's: "
                             "the songs would fail their signature check" % (what, base + rva, rva, n))
        say("%s at 0x%x: as in the game" % (what, base + rva))

    # -- sections ---------------------------------------------------------------------------
    n = src.captured
    secs = []
    for name, va, vs, ch in L.secs:
        rva = va - base
        if name.startswith(".UBX") and not args.ubx_exec:
            ch = (ch & ~(SCN_EXEC | SCN_CODE)) | 0x40
        secs.append([name, rva, vs, ch])
    end = align(max(s[1] + s[2] for s in secs), PAGE)
    imp_rva = end
    slots_img = [(a + shift, d, nm, k) for a, d, nm, k in slots]
    imp_blob, thunks, imp_dir, iat_dir = build_imports(slots_img, imp_rva, base)
    for a, v in thunks.items():                 # the IAT holds what the ILT does (unbound)
        struct.pack_into("<I", img, a - base, v)
    say("IAT: %d slots (%d named, %d ordinal, %d placeholders)"
        % (len(slots), sum(1 for s in slots if s[2] and s[3] != "ordinal"),
           sum(1 for s in slots if s[3] == "ordinal"), sum(1 for s in slots if not s[2])))
    raws = []
    for s in secs:
        have = max(0, min(s[2], n - s[1]))
        raws.append(bytes(img[s[1]:s[1] + have]).rstrip(b"\0") if have else b"")
    rel_rva = align(imp_rva + len(imp_blob), PAGE)
    rel_blob = reloc_table(sites, base)
    secs += [[".lpimp", imp_rva, len(imp_blob), 0x40000040], [".lpreloc", rel_rva, len(rel_blob), 0x42000040]]
    raws += [imp_blob, rel_blob]
    size_of_image = align(rel_rva + len(rel_blob), PAGE)

    e = args.entry + shift
    eb = img[e - base:e - base + 10]
    if not (eb[0] == 0xe8 and eb[5] == 0xe9):
        raise SystemExit("entry 0x%x is not `call; jmp` (WinMainCRTStartup): %s" % (e, eb.hex()))
    say("entry 0x%x (link 0x%x): call 0x%x; jmp 0x%x"
        % (e, args.entry, (e + 5 + struct.unpack_from("<i", eb, 1)[0]) & M32,
           (e + 10 + struct.unpack_from("<i", eb, 6)[0]) & M32))

    # -- headers ----------------------------------------------------------------------------
    dirs = {1: imp_dir, 5: (rel_rva, len(rel_blob)), 12: iat_dir}
    for k, what in ((0, "export"), (2, "resource")):
        r, s = src.dirs[k]
        if r and src.has(r, min(s, 16)):
            dirs[k] = (r, s)
            say("%s directory kept (rva 0x%x, 0x%x bytes)" % (what, r, s))
    nsec = len(secs)
    hdr_size = align(0x40 + 4 + 20 + 0xe0 + 40 * nsec, FILE_ALIGN)
    out = bytearray(hdr_size)
    struct.pack_into("<H", out, 0, 0x5a4d)
    struct.pack_into("<I", out, 0x3c, 0x40)
    out[0x40:0x44] = b"PE\0\0"
    struct.pack_into("<HHIIIHH", out, 0x44, 0x14c, nsec, src.stamp, 0, 0, 0xe0, 0x0102)
    opt = 0x58
    code_size = sum(align(s[2], FILE_ALIGN) for s in secs if s[3] & SCN_CODE)
    idata_size = sum(align(s[2], FILE_ALIGN) for s in secs if s[3] & 0x40)
    struct.pack_into("<HBBIIIIIII", out, opt, 0x10b, 10, 0, code_size, idata_size, 0,
                     e - base, L.text[0] - base, L.rdata[0] - base, base)
    struct.pack_into("<IIHHHHHHIIIIHHIIIIII", out, opt + 32, PAGE, FILE_ALIGN, 5, 1, 0, 0, 5, 1, 0,
                     size_of_image, hdr_size, 0, 2, 0x8140, 0x100000, 0x1000, 0x100000, 0x1000, 0, 16)
    for k, (r, s) in dirs.items():
        struct.pack_into("<II", out, opt + 96 + 8 * k, r, s)
    st = opt + 0xe0
    fo = hdr_size
    body = bytearray()
    for k, ((name, rva, vs, ch), raw) in enumerate(zip(secs, raws)):
        rs = align(len(raw), FILE_ALIGN) if raw else 0
        ent = bytearray(40)
        ent[:len(name)] = name.encode()
        struct.pack_into("<IIII", ent, 8, vs, rva, rs, fo if rs else 0)
        struct.pack_into("<I", ent, 36, ch)
        out[st + 40 * k:st + 40 * k + 40] = ent
        if rs:
            body += raw + bytes(rs - len(raw))
            fo += rs
    # the wrapper table first, the image last and whole: an image on disk always has its table (the
    # installer takes an image that is there as done, and translating without the table breaks the game)
    if args.wrappers and os.path.exists(args.wrappers):
        wout = re.sub(r"\.exe$", "", args.out) + ".wrappers.tsv"
        say("wrote %s: %d wrapper sites at the image base" % (wout, write_wrappers(args.wrappers, wout, shift, size_of_image)))
    with open(args.out + ".tmp", "wb") as f:
        f.write(out + body)
    os.replace(args.out + ".tmp", args.out)
    say("wrote %s: %d bytes, %d sections, SizeOfImage 0x%x" % (args.out, len(out) + len(body), nsec, size_of_image))
    if args.report:
        with open(args.report, "w") as f:
            f.write("\n".join(rep) + "\n")


if __name__ == "__main__":
    main()
