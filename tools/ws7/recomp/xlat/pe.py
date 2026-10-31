"""Minimal PE32 (i386) reader for the 32-bit front end (xlat/x86_32.py).

Everything comes from the file's own headers and directories; nothing is
loaded or run:

  * sections (name, vaddr, vsize, file offset, raw size, characteristics);
  * base relocations (IMAGE_REL_BASED_HIGHLOW sites): a word at a site is a
    pointer into the image;
  * imports: IAT slot address -> "dll!name" (or "dll!#ordinal");
  * exports: address -> name (forwarders are skipped);
  * the TLS directory (template range, index slot, callbacks) and the SafeSEH
    handler table of the load configuration;
  * the entry point.

All addresses are virtual addresses at the preferred image base.
"""
import struct

SCN_CODE = 0x20
SCN_EXEC = 0x20000000
SCN_WRITE = 0x80000000


class PEError(Exception):
    pass


class Image:
    def __init__(self, path):
        self.path = path
        self.data = d = open(path, "rb").read()
        if d[:2] != b"MZ":
            raise PEError("not an MZ file")
        o = struct.unpack_from("<I", d, 0x3c)[0]
        if d[o:o + 4] != b"PE\0\0":
            raise PEError("no PE signature")
        machine, nsect = struct.unpack_from("<HH", d, o + 4)
        if machine != 0x14c:
            raise PEError("not i386 (machine 0x%x)" % machine)
        optsz = struct.unpack_from("<H", d, o + 20)[0]
        self.characteristics = struct.unpack_from("<H", d, o + 22)[0]
        opt = o + 24
        if struct.unpack_from("<H", d, opt)[0] != 0x10b:
            raise PEError("not PE32")
        self.entry_rva = struct.unpack_from("<I", d, opt + 16)[0]
        self.base = struct.unpack_from("<I", d, opt + 28)[0]
        self.size_of_image = struct.unpack_from("<I", d, opt + 56)[0]
        self.size_of_headers = struct.unpack_from("<I", d, opt + 60)[0]
        ndirs = struct.unpack_from("<I", d, opt + 92)[0]
        self.dirs = [struct.unpack_from("<II", d, opt + 96 + 8 * k) for k in range(min(ndirs, 16))]
        self.sections = []   # (name, va, vsize, fileoff, rawsize, chars)
        s = opt + optsz
        for _ in range(nsect):
            name = d[s:s + 8].rstrip(b"\0").decode("latin-1")
            vs, va, rs, ro = struct.unpack_from("<IIII", d, s + 8)
            ch = struct.unpack_from("<I", d, s + 36)[0]
            self.sections.append((name, self.base + va, vs or rs, ro, rs, ch))
            s += 40
        self.entry = self.base + self.entry_rva if self.entry_rva else None
        self.relocs = self._relocs()
        self.imports = self._imports()
        self.exports = self._exports()
        self.tls = self._tls()
        self.safeseh = self._safeseh()

    # -- address helpers -------------------------------------------------
    def section_of(self, a):
        for sec in self.sections:
            if sec[1] <= a < sec[1] + max(sec[2], sec[4]):
                return sec
        return None

    def foff(self, a):
        """File offset of address a, or None (headers count; bss does not)."""
        if self.base <= a < self.base + self.size_of_headers:
            return a - self.base
        for _, va, vs, ro, rs, _ in self.sections:
            if va <= a < va + min(vs, rs):
                return ro + a - va
        return None

    def u8(self, a):
        o = self.foff(a)
        return None if o is None else self.data[o]

    def u32(self, a):
        o = self.foff(a)
        return None if o is None or o + 4 > len(self.data) else struct.unpack_from("<I", self.data, o)[0]

    def u16(self, a):
        o = self.foff(a)
        return None if o is None else struct.unpack_from("<H", self.data, o)[0]

    def cstr(self, a, maxlen=512):
        o = self.foff(a)
        if o is None:
            return None
        e = self.data.find(b"\0", o, o + maxlen)
        return self.data[o:e if e >= 0 else o + maxlen].decode("latin-1")

    def code_ranges(self):
        return [(va, va + vs) for _, va, vs, _, _, ch in self.sections if ch & (SCN_EXEC | SCN_CODE)]

    def is_code(self, a):
        s = self.section_of(a)
        return bool(s and s[5] & (SCN_EXEC | SCN_CODE))

    def writable(self, a):
        s = self.section_of(a)
        return bool(s and s[5] & SCN_WRITE)

    def dir(self, k):
        return self.dirs[k] if k < len(self.dirs) else (0, 0)

    # -- directories -------------------------------------------------------
    def _relocs(self):
        rva, size = self.dir(5)
        out = set()
        if not rva:
            return out
        a, end = self.base + rva, self.base + rva + size
        while a < end:
            page, blk = self.u32(a), self.u32(a + 4)
            if not blk:
                break
            for k in range((blk - 8) // 2):
                e = self.u16(a + 8 + 2 * k)
                if e >> 12 == 3:                      # IMAGE_REL_BASED_HIGHLOW
                    out.add(self.base + page + (e & 0xfff))
                elif e >> 12 not in (0,):
                    raise PEError("relocation type %d" % (e >> 12))
            a += blk
        return out

    def _imports(self):
        rva, _ = self.dir(1)
        out = {}
        if not rva:
            return out
        a = self.base + rva
        while True:
            ilt, _, _, name, iat = (self.u32(a + 4 * k) for k in range(5))
            if not name and not iat:
                break
            dll = self.cstr(self.base + name).lower()
            look = self.base + (ilt or iat)
            slot = self.base + iat
            while True:
                e = self.u32(look)
                if not e:
                    break
                if e & 0x80000000:
                    out[slot] = "%s!#%d" % (dll, e & 0xffff)
                else:
                    out[slot] = "%s!%s" % (dll, self.cstr(self.base + e + 2))
                look += 4
                slot += 4
            a += 20
        return out

    def _exports(self):
        rva, size = self.dir(0)
        out = {}
        if not rva:
            return out
        a = self.base + rva
        obase, nfun, nname, afun, aname, aord = (self.u32(a + 16 + 4 * k) for k in range(6))
        names = {}
        for k in range(nname):
            names[self.u16(self.base + aord + 2 * k)] = self.cstr(self.base + self.u32(self.base + aname + 4 * k))
        for k in range(nfun):
            f = self.u32(self.base + afun + 4 * k)
            if not f or rva <= f < rva + size:        # empty or forwarder
                continue
            out.setdefault(self.base + f, names.get(k, "#%d" % (obase + k)))
        return out

    def _tls(self):
        rva, _ = self.dir(9)
        if not rva:
            return None
        a = self.base + rva
        start, end, index, cbs, zfill, _ = (self.u32(a + 4 * k) for k in range(6))
        callbacks = []
        while cbs:
            f = self.u32(cbs)
            if not f:
                break
            callbacks.append(f)
            cbs += 4
        return {"start": start, "end": end, "index": index, "zerofill": zfill, "callbacks": callbacks}

    def _safeseh(self):
        rva, _ = self.dir(10)
        if not rva:
            return []
        a = self.base + rva
        size = self.u32(a)
        if size < 72:
            return []
        table, count = self.u32(a + 64), self.u32(a + 68)
        if not table:
            return []
        return [self.base + self.u32(table + 4 * k) for k in range(count)]
