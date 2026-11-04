#!/usr/bin/env python3
"""The Learn & Play game image inside a page harvest, at its preferred base.

The harvest (`rs_harvest.bin`, the unpack notes call it master_harvest.bin) is SizeOfImage bytes
read from the running game at 0x400000 onwards. On the machine it came from the game was
relocated (ASLR) to 0xab0000, so the harvest is not the game laid out at 0x400000:

  0x400000   XINPUT1_3.dll (its preferred base; free because the game was elsewhere)
  0x860000   systemdetection.dll (its DllMain is the "OEP 0x874ded" of the earlier notes)
  0xab0000   the game: its in-memory PE header, then .text, .rdata, .data, .UBX0 ...

The game's pages were captured before VMProtect applied relocations (the harvester keeps the
first non-zero copy of a page), so the code and data are still linked for 0x400000: image RVA r
is harvest offset r + (game_base - 0x400000). Only the first SizeOfImage - shift bytes of the
game are in the harvest; the tail of .UBX0 and everything after it was never read.

The section table comes from the game's in-memory header, which VMProtect rewrites to the
original link's layout (it differs from the packed file's own table).

Usage: lp_harvest.py HARVEST     print the modules found and the game's layout
"""
import struct
import sys

HARVEST_BASE = 0x400000     # where the harvester started reading
PREFERRED = 0x400000        # the game's link base (the code's absolute addresses)


class Module:
    def __init__(self, data, off):
        self.off = off
        self.va = HARVEST_BASE + off
        pe = off + struct.unpack_from("<I", data, off + 0x3c)[0]
        self.machine, nsect = struct.unpack_from("<HH", data, pe + 4)
        optsz = struct.unpack_from("<H", data, pe + 20)[0]
        self.characteristics = struct.unpack_from("<H", data, pe + 22)[0]
        opt = pe + 24
        self.entry_rva = struct.unpack_from("<I", data, opt + 16)[0]
        self.image_base = struct.unpack_from("<I", data, opt + 28)[0]
        self.size_of_image = struct.unpack_from("<I", data, opt + 56)[0]
        self.dirs = [struct.unpack_from("<II", data, opt + 96 + 8 * k) for k in range(16)]
        self.sections = []      # (name, rva, vsize, characteristics)
        s = opt + optsz
        for _ in range(nsect):
            name = data[s:s + 8].rstrip(b"\0").decode("latin-1")
            vs, rva = struct.unpack_from("<II", data, s + 8)
            self.sections.append((name, rva, vs, struct.unpack_from("<I", data, s + 36)[0]))
            s += 40
        self.name = self._export_name(data)

    def _export_name(self, data):
        rva = self.dirs[0][0]
        if not rva or self.off + rva + 16 > len(data):
            return None
        n = struct.unpack_from("<I", data, self.off + rva + 12)[0]
        if not n or self.off + n >= len(data):
            return None
        return data[self.off + n:self.off + n + 64].split(b"\0")[0].decode("latin-1")

    def section(self, name):
        for s in self.sections:
            if s[0] == name:
                return s
        raise KeyError(name)


def modules(data):
    out = []
    for off in range(0, len(data) - 0x400, 0x10000):
        if data[off:off + 2] != b"MZ":
            continue
        pe = struct.unpack_from("<I", data, off + 0x3c)[0]
        if pe < 0x400 and data[off + pe:off + pe + 4] == b"PE\0\0":
            out.append(Module(data, off))
    return out


class Game:
    """The game image at PREFERRED, backed by the harvest. Addresses are VAs at PREFERRED."""

    def __init__(self, path):
        self.harvest = open(path, "rb").read()
        self.modules = modules(self.harvest)
        games = [m for m in self.modules if any(s[0] == ".UBX0" for s in m.sections)]
        if not games:
            raise SystemExit("%s: no module with a .UBX0 section (not a Learn & Play harvest?)" % path)
        self.mod = games[0]
        self.shift = self.mod.off               # harvest offset of RVA 0
        self.img = self.harvest[self.shift:]    # img[rva]
        self.captured = len(self.img)           # RVAs [0, captured) are in the harvest
        self.base = PREFERRED
        self.sections = {s[0]: (self.base + s[1], self.base + s[1] + s[2], s[3]) for s in self.mod.sections}
        self.text = self.sections[".text"]
        self.rdata = self.sections[".rdata"]
        self.ubx0 = self.sections[".UBX0"]
        self.end = self.base + self.captured

    def has(self, va, n=1):
        return self.base <= va and va + n <= self.end

    def read(self, va, n):
        return self.img[va - self.base:va - self.base + n]

    def u32(self, va):
        return struct.unpack_from("<I", self.img, va - self.base)[0]

    def u8(self, va):
        return self.img[va - self.base]

    def page_captured(self, va):
        return self.has(va, 0x1000) and any(self.read(va & ~0xfff, 0x1000))


def main():
    if len(sys.argv) != 2:
        print(__doc__.strip().splitlines()[-1])
        sys.exit(2)
    g = Game(sys.argv[1])
    for m in g.modules:
        print("0x%08x  %-24s base 0x%08x  size 0x%07x  entry rva 0x%x" %
              (m.va, m.name or "(no export name)", m.image_base, m.size_of_image, m.entry_rva))
    print("game at harvest 0x%x: RVA r = harvest offset r + 0x%x; RVAs < 0x%x captured" %
          (g.mod.va, g.shift, g.captured))
    for name, (lo, hi, ch) in g.sections.items():
        pages = range(lo & ~0xfff, hi, 0x1000)
        got = sum(1 for p in pages if g.page_captured(p))
        print("  %-8s 0x%08x..0x%08x  %4d/%4d pages captured" % (name, lo, hi, got, len(pages)))


if __name__ == "__main__":
    main()
