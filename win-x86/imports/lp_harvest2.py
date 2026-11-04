#!/usr/bin/env python3
"""Load a harvester v2 capture of the Learn & Play game and check it.

Harvester v2 reads the game from wherever Windows loaded it and writes, into one directory:

  rs_harvest2.bin       the game image by RVA (SizeOfImage bytes), first non-zero copy of each page
  rs_harvest2_last.bin  the same layout, latest non-zero copy of each page (import slots filled in)
  rs_modules.tsv        name, base, size, path of every module seen in the process
  rs_exports.tsv        module, ordinal, name, address, forwarder: every export, read from memory
  rs_harvest2.txt       key<TAB>value: game_base, game_size, exe_sha256, pages_captured, ...

The image used for analysis is the _last copy, at the base the game ran at (game_base), with the
first copy filling any page the _last copy lacks. Its relocations, if the game was moved, are
whatever VMProtect had applied by then; `reloc_state` compares the two copies page by page to tell
(a dword that differs by exactly game_base - 0x400000 is a relocation).

All addresses the tools write are link addresses: RVA + 0x400000 (LINK_BASE), the base the code
was linked for and the one iat_map.tsv and names.tsv use. `Image.link(va)` converts.

The section table comes from the header in the image: VMProtect rewrites it at run time to the
original link's layout (.text, .rdata, .data, .UBX0 ...); a capture taken before that has the
packed file's own table instead (`Image.header_kind` says which). The wrapper stubs live above
.data, so the stub region is taken as [end of .data, end of image) in both cases.

Usage: lp_harvest2.py DIR      print the consistency and coverage report   (or $LP_HARVEST2)
"""
import os
import struct
import sys

LINK_BASE = 0x400000
PAGE = 0x1000

FILES = {
    "first": "rs_harvest2.bin",
    "last": "rs_harvest2_last.bin",
    "modules": "rs_modules.tsv",
    "exports": "rs_exports.tsv",
    "info": "rs_harvest2.txt",
}


def need(path, what):
    if not os.path.exists(path):
        raise SystemExit("missing %s: %s (harvester v2 writes it next to rsharvest2.exe)" % (what, path))
    return path


def read_info(path):
    out = {}
    for line in open(need(path, "harvest info"), encoding="utf-8-sig"):
        f = line.rstrip("\r\n").split("\t", 1)
        if len(f) == 2:
            out[f[0]] = f[1]
    for k in ("game_base", "game_size"):
        if k not in out:
            raise SystemExit("%s: no %s line (not a harvester v2 info file?)" % (path, k))
    return out


def read_modules(path):
    """[(name, base, size, path)], names as the loader reported them."""
    out = []
    with open(need(path, "module list"), encoding="utf-8-sig") as f:
        head = f.readline().rstrip("\r\n").split("\t")
        if head[:3] != ["name", "base", "size"]:
            raise SystemExit("%s: unexpected header %r" % (path, head))
        for line in f:
            r = line.rstrip("\r\n").split("\t")
            if len(r) >= 3 and r[0]:
                out.append((r[0], int(r[1], 16), int(r[2], 16), r[3] if len(r) > 3 else ""))
    return out


def read_exports(path):
    """[(module, ordinal, name, address, forwarder)]."""
    out = []
    with open(need(path, "export list"), encoding="utf-8-sig") as f:
        head = f.readline().rstrip("\r\n").split("\t")
        if head[:4] != ["module", "ordinal", "name", "address"]:
            raise SystemExit("%s: unexpected header %r" % (path, head))
        for line in f:
            r = line.rstrip("\r\n").split("\t")
            if len(r) >= 4 and r[0]:
                out.append((r[0], int(r[1]), r[2], int(r[3], 16), r[4] if len(r) > 4 else ""))
    return out


def pe_sections(img):
    """(image_base, size_of_image, [(name, rva, vsize, characteristics)]) from the header at img[0]."""
    if img[:2] != b"MZ":
        raise SystemExit("the image has no MZ header at RVA 0 (page 0 not captured?)")
    pe = struct.unpack_from("<I", img, 0x3c)[0]
    if img[pe:pe + 4] != b"PE\0\0":
        raise SystemExit("the image has no PE header")
    nsect = struct.unpack_from("<H", img, pe + 6)[0]
    optsz = struct.unpack_from("<H", img, pe + 20)[0]
    opt = pe + 24
    base = struct.unpack_from("<I", img, opt + 28)[0]
    soi = struct.unpack_from("<I", img, opt + 56)[0]
    secs, s = [], opt + optsz
    for _ in range(nsect):
        name = img[s:s + 8].rstrip(b"\0").decode("latin-1")
        vs, rva = struct.unpack_from("<II", img, s + 8)
        secs.append((name, rva, vs, struct.unpack_from("<I", img, s + 36)[0]))
        s += 40
    return base, soi, secs


def zero_page(buf, off):
    return buf.count(0, off, off + PAGE) == PAGE


class Image:
    """The game image at the base it ran at. VAs below are runtime VAs (base + RVA); link(va)
    gives the link address. The interface matches lp_harvest.Game where lp_iat uses it."""

    def __init__(self, d):
        self.dir = d
        p = {k: os.path.join(d, v) for k, v in FILES.items()}
        self.info = read_info(p["info"])
        self.base = int(self.info["game_base"], 16)
        self.size = int(self.info["game_size"], 16)
        first = open(need(p["first"], "first-copy image"), "rb").read()
        last = open(need(p["last"], "latest-copy image"), "rb").read()
        for nm, b in (("rs_harvest2.bin", first), ("rs_harvest2_last.bin", last)):
            if len(b) != self.size:
                raise SystemExit("%s is 0x%x bytes, game_size says 0x%x" % (nm, len(b), self.size))
        self.first, self.last = first, last
        img = bytearray(last)
        self.from_first = []            # pages only the first copy has
        for off in range(0, self.size, PAGE):
            if zero_page(last, off) and not zero_page(first, off):
                img[off:off + PAGE] = first[off:off + PAGE]
                self.from_first.append(off)
        self.img = bytes(img)
        self.zero = {off for off in range(0, self.size, PAGE) if zero_page(self.img, off)}
        self.modules = read_modules(p["modules"])
        self.exports_path = p["exports"]
        need(p["exports"], "export list")
        self.header_base, self.header_size, secs = pe_sections(self.img)
        self.header_kind = "original" if any(s[0] == ".UBX0" for s in secs) and \
            not any(s[0] == ".tls" for s in secs) else "packed"
        self.sec_list = secs
        self.sections = {s[0]: (self.base + s[1], self.base + s[1] + s[2], s[3]) for s in secs}
        for k in (".text", ".rdata", ".data"):
            if k not in self.sections:
                raise SystemExit("the image's header has no %s section" % k)
        self.text = self.sections[".text"]
        self.rdata = self.sections[".rdata"]
        self.data = self.sections[".data"]
        self.end = self.base + self.size
        self.ubx0 = (self.data[1], self.end)      # where the wrapper stubs are
        self.captured = self.size
        self.delta = self.base - LINK_BASE

    # lp_harvest.Game interface ------------------------------------------------
    def has(self, va, n=1):
        if not (self.base <= va and va + n <= self.end):
            return False
        return all((p - self.base) not in self.zero for p in range(va & ~0xfff, va + n, PAGE))

    def read(self, va, n):
        return self.img[va - self.base:va - self.base + n]

    def u32(self, va):
        return struct.unpack_from("<I", self.img, va - self.base)[0]

    def u8(self, va):
        return self.img[va - self.base]

    def page_captured(self, va):
        return self.has(va & ~0xfff, PAGE)

    # -------------------------------------------------------------------------
    def link(self, va):
        return va - self.delta

    def module_of(self, va):
        for name, base, size, _ in self.modules:
            if base <= va < base + size:
                return name, va - base
        return None


def reloc_state(im, off):
    """Compare a page's two copies: 'same', 'reloc' (every difference is a dword that differs by
    the load delta), 'changed' (other differences: data written, or code re-encrypted),
    'first_only', 'last_only' or 'none'."""
    a, b = im.first, im.last
    za, zb = zero_page(a, off), zero_page(b, off)
    if za and zb:
        return "none"
    if za:
        return "last_only"
    if zb:
        return "first_only"
    pa, pb = a[off:off + PAGE], b[off:off + PAGE]
    if pa == pb:
        return "same"
    if not im.delta:
        return "changed"
    d = im.delta & 0xffffffff
    i = 0
    while i < PAGE:
        if pa[i] == pb[i]:
            i += 1
            continue
        for k in range(max(0, i - 3), i + 1):
            if k + 4 <= PAGE and (struct.unpack_from("<I", pb, k)[0] - struct.unpack_from("<I", pa, k)[0]) & 0xffffffff == d:
                i = k + 4
                break
        else:
            return "changed"
    return "reloc"


def report(im):
    lines = []
    w = lines.append
    w("harvest      %s" % im.dir)
    w("game_base    0x%08x   game_size 0x%x   link base 0x%x   delta 0x%x" %
      (im.base, im.size, LINK_BASE, im.delta & 0xffffffff))
    w("header       %s section table, ImageBase 0x%08x, SizeOfImage 0x%x" %
      (im.header_kind, im.header_base, im.header_size))
    if im.header_base not in (im.base, LINK_BASE):
        w("  WARNING: header ImageBase is neither game_base nor the link base")
    elif im.header_base != im.base:
        w("  note: header ImageBase is the link base, not game_base (header not updated by the loader)")
    if im.header_kind == "packed":
        w("  note: the packed file's section table: VMProtect had not unpacked when page 0 was read")
    w("exe_sha256   %s" % im.info.get("exe_sha256", "?"))
    w("pages        %s/%s captured (harvester count); %d page(s) taken from the first copy" %
      (im.info.get("pages_captured", "?"), im.info.get("pages_total", "?"), len(im.from_first)))
    w("modules      %d" % len(im.modules))
    w("")
    w("%-8s %-21s %6s %6s  %5s %5s %7s %6s" % ("section", "link range", "pages", "have", "same", "reloc", "changed", "1only"))
    tot = {}
    for name, rva, vs, _ in im.sec_list:
        st = {}
        pages = range(rva & ~0xfff, min(rva + vs, im.size), PAGE)
        for off in pages:
            s = reloc_state(im, off)
            st[s] = st.get(s, 0) + 1
            tot[s] = tot.get(s, 0) + 1
        have = len(pages) - st.get("none", 0)
        w("%-8s 0x%08x..0x%08x %6d %6d  %5d %5d %7d %6d" %
          (name, LINK_BASE + rva, LINK_BASE + rva + vs, len(pages), have, st.get("same", 0),
           st.get("reloc", 0), st.get("changed", 0), st.get("first_only", 0)))
    if im.delta:
        if tot.get("reloc", 0) and not tot.get("changed", 0):
            w("relocation   the first copy is linked for 0x%x, the _last copy relocated to game_base" % LINK_BASE)
        elif tot.get("reloc", 0):
            w("relocation   mixed: %d page(s) differ by relocations only, %d otherwise" %
              (tot.get("reloc", 0), tot.get("changed", 0)))
        else:
            w("relocation   no page differs by the load delta: the copies agree on relocation state")
    else:
        w("relocation   none needed (loaded at the link base)")
    return "\n".join(lines)


def main():
    d = sys.argv[1] if len(sys.argv) > 1 else os.environ.get("LP_HARVEST2")
    if not d:
        print(__doc__.strip().splitlines()[-1])
        sys.exit(2)
    if not os.path.isdir(d):
        raise SystemExit("not a directory: %s" % d)
    print(report(Image(d)))


if __name__ == "__main__":
    main()
