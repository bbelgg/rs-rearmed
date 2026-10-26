"""Minimal Mach-O x86_64 reader (used by xlat/core.py).

The decrypted image uses LC_DYLD_INFO_ONLY (classic rebase/bind opcodes, no
chained fixups). On disk, every rebased pointer already holds its unslid
absolute vmaddr, and every bound pointer holds 0 (or an addend). So:

  * a word is a real internal pointer  <=> its address is in the rebase table;
  * a word is an import                 <=> its address is in a bind table
    (regular or weak); the symbol name comes from the bind entry.

Rebase/bind tables are decoded with `objdump --macho --rebase/--bind/
--weak-bind` (Xcode CLT), function starts with `--function-starts`, local
symbols with `nm`. All results are plain dicts/sets; nothing is cached.
"""
import re
import struct
import subprocess


class Image:
    def __init__(self, path):
        self.path = path
        self.data = open(path, "rb").read()
        self.segs = []      # (name, vmaddr, vmsize, fileoff, filesize)
        self.sects = {}     # (seg, sect) -> (addr, size, fileoff)
        self._parse_lc()
        self.rebased = self._rebases()
        self.binds = self._binds()          # addr -> symbol (regular + weak)
        self.fstarts = self._fstarts()      # sorted list
        self.fset = set(self.fstarts)
        self.syms = self._syms()            # addr -> mangled local name
        t = self.sects[("__TEXT", "__text")]
        self.text_lo, self.text_hi = t[0], t[0] + t[1]

    # -- load commands -------------------------------------------------
    def _parse_lc(self):
        d = self.data
        ncmds = struct.unpack_from("<I", d, 16)[0]
        off = 32
        for _ in range(ncmds):
            cmd, size = struct.unpack_from("<II", d, off)
            if cmd == 0x19:  # LC_SEGMENT_64
                seg = d[off + 8:off + 24].rstrip(b"\0").decode()
                vm, vms, fo, fs = struct.unpack_from("<QQQQ", d, off + 24)
                self.segs.append((seg, vm, vms, fo, fs))
                nsects = struct.unpack_from("<I", d, off + 64)[0]
                for i in range(nsects):
                    s = off + 72 + i * 80
                    sect = d[s:s + 16].rstrip(b"\0").decode()
                    addr, sz, foff = struct.unpack_from("<QQI", d, s + 32)
                    self.sects[(seg, sect)] = (addr, sz, foff)
            off += size

    def foff(self, addr):
        for _, vm, vms, fo, fs in self.segs:
            if vm <= addr < vm + fs:
                return fo + addr - vm
        return None

    def u64(self, addr):
        o = self.foff(addr)
        return None if o is None else struct.unpack_from("<Q", self.data, o)[0]

    def u32(self, addr):
        o = self.foff(addr)
        return None if o is None else struct.unpack_from("<I", self.data, o)[0]

    def cstr(self, addr, maxlen=512):
        o = self.foff(addr)
        if o is None:
            return None
        e = self.data.find(b"\0", o, o + maxlen)
        if e < 0:
            return None
        try:
            return self.data[o:e].decode("utf-8")
        except UnicodeDecodeError:
            return None

    def section_of(self, addr):
        for (seg, sect), (a, sz, _) in self.sects.items():
            if a <= addr < a + sz:
                return seg + "," + sect
        return None

    def in_text(self, v):
        return self.text_lo <= v < self.text_hi

    # -- dyld info via objdump ----------------------------------------
    def _objdump(self, *args):
        return subprocess.run(["objdump", "--macho", *args, self.path],
                              capture_output=True, text=True, check=True).stdout

    def _rebases(self):
        out = set()
        for line in self._objdump("--rebase").splitlines():
            p = line.split()
            if len(p) >= 4 and p[0].startswith("__") and p[2].startswith("0x"):
                out.add(int(p[2], 16))
        return out

    def _binds(self):
        out = {}
        for flag in ("--bind", "--weak-bind"):
            for line in self._objdump(flag).splitlines():
                p = line.split()
                if len(p) >= 5 and p[0].startswith("__") and p[2].startswith("0x"):
                    # regular bind lines carry a dylib column before the symbol
                    out.setdefault(int(p[2], 16), p[-1])
        return out

    def _fstarts(self):
        out = self._objdump("--function-starts")
        return sorted({int(l, 16) for l in out.split() if re.fullmatch(r"[0-9a-f]{16}", l)})

    def _syms(self):
        out = subprocess.run(["nm", "-U", self.path], capture_output=True, text=True,
                             check=True).stdout
        syms = {}
        for line in out.splitlines():
            p = line.split()
            if len(p) == 3:
                syms.setdefault(int(p[0], 16), p[2])
        return syms


def demangle(names):
    names = list(names)
    if not names:
        return {}
    out = subprocess.run(["c++filt"], input="\n".join(names), capture_output=True,
                         text=True).stdout.splitlines()
    return dict(zip(names, out))
