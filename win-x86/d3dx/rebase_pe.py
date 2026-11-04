#!/usr/bin/env python3
"""Rebase a 32-bit PE DLL: apply its base relocations for a new ImageBase and write the result.

    rebase_pe.py <in.dll> <out.dll> <new base, hex>

The translation maps every guest image at its preferred base, and Microsoft gives many DLLs the
same one (d3dx9_42.dll and D3DCompiler_42.dll are both at 0x10000000), so one of them is rebased
before it is translated, as the Windows loader would at run time. Only IMAGE_REL_BASED_HIGHLOW
(3) and ABSOLUTE (0, padding) entries are expected in a 32-bit image; anything else is refused.
The relocation directory is kept (still valid: it holds RVAs), the checksum is cleared."""
import struct
import sys


def rebase(d, new):
    d = bytearray(d)
    pe = struct.unpack_from("<I", d, 0x3c)[0]
    if d[pe:pe + 4] != b"PE\0\0" or struct.unpack_from("<H", d, pe + 4)[0] != 0x14c:
        raise SystemExit("not a PE32 x86 image")
    nsect, optsz = struct.unpack_from("<H", d, pe + 6)[0], struct.unpack_from("<H", d, pe + 20)[0]
    opt = pe + 24
    old = struct.unpack_from("<I", d, opt + 28)[0]
    if new & 0xffff:
        raise SystemExit("the base must be 64 KB aligned")
    sects = []
    for k in range(nsect):
        vs, va, rs, ro = struct.unpack_from("<IIII", d, opt + optsz + 40 * k + 8)
        sects.append((va, max(vs, rs), ro, rs))

    def foff(rva):
        for va, n, ro, rs in sects:
            if va <= rva < va + n:
                return ro + rva - va if rva - va < rs else None
        return None

    rva, size = struct.unpack_from("<II", d, opt + 96 + 8 * 5)
    if not rva or foff(rva) is None:
        raise SystemExit("no relocations: the image cannot move")
    o, n = foff(rva), 0
    end = o + size
    delta = (new - old) & 0xffffffff
    while o < end:
        page, blk = struct.unpack_from("<II", d, o)
        if blk < 8:
            break
        for k in range((blk - 8) // 2):
            e = struct.unpack_from("<H", d, o + 8 + 2 * k)[0]
            t, off = e >> 12, e & 0xfff
            if t == 0:
                continue
            if t != 3:
                raise SystemExit("relocation type %d at 0x%x" % (t, page + off))
            f = foff(page + off)
            if f is not None:  # a site in zero fill (no file data) holds no pointer yet
                v = struct.unpack_from("<I", d, f)[0]
                struct.pack_into("<I", d, f, (v + delta) & 0xffffffff)
                n += 1
        o += blk
    struct.pack_into("<I", d, opt + 28, new)
    struct.pack_into("<I", d, opt + 64, 0)  # CheckSum
    return bytes(d), old, n


def main():
    if len(sys.argv) != 4:
        raise SystemExit(__doc__.split("\n\n")[1])
    new = int(sys.argv[3], 16)
    out, old, n = rebase(open(sys.argv[1], "rb").read(), new)
    open(sys.argv[2], "wb").write(out)
    print("rebased 0x%x -> 0x%x: %d relocations" % (old, new, n))


if __name__ == "__main__":
    main()
