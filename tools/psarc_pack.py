#!/usr/bin/env python3
"""Rebuild a Rocksmith 2014 .psarc with some entries replaced.

    psarc_pack.py <in.psarc> <out.psarc> <name>=<file> [<name>=<file> ...]

<name> is the path as psarc_extract.py --list prints it. Entries that are not
replaced keep their compressed blocks byte for byte; only the TOC (offsets,
lengths, block-size table) is rewritten and re-encrypted. Entry names, hashes
and order are unchanged, so nothing can be added or removed. Entry data is
packed back to back, so archives whose entries are padded to alignment
(cache.psarc) do not round-trip byte for byte; static.psarc does.
"""
import struct
import subprocess
import sys
import zlib

from psarc_extract import TOC_IV, TOC_KEY, read_archive, read_entry


def encrypt_toc(data):
    padded = data + b"\0" * (-len(data) % 16)
    out = subprocess.run(
        ["openssl", "enc", "-e", "-aes-256-cfb", "-nopad", "-K", TOC_KEY, "-iv", TOC_IV],
        input=padded, capture_output=True, check=True).stdout
    return out[:len(data)]


def raw_blocks(f, block_size, zsizes, entry):
    """The stored blocks of an entry, as (zsize, bytes) pairs."""
    zindex, length, offset = entry
    if length == 0:
        # An empty entry still owns one slot in the block table.
        return [(zsizes[zindex], b"")]
    f.seek(offset)
    out, got, zi = [], 0, zindex
    while got < length:
        z = zsizes[zi]
        zi += 1
        out.append((z, f.read(z or block_size)))
        got += block_size
    return out


def compress_blocks(data, block_size):
    if not data:
        return [(0, b"")]
    out = []
    for i in range(0, len(data), block_size):
        raw = data[i:i + block_size]
        z = zlib.compress(raw, 9)
        if len(z) < len(raw):
            out.append((len(z), z))
        else:
            # Stored raw; a full-size raw block is written as zsize 0.
            out.append((0 if len(raw) == block_size else len(raw), raw))
    return out


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    src, dst = sys.argv[1], sys.argv[2]
    repl = dict(a.split("=", 1) for a in sys.argv[3:])

    f, block_size, entries, zsizes = read_archive(src)
    f.seek(0)
    hdr = f.read(32)
    magic, ver, comp, toc_len, ent_size, n_ent, _, flags = struct.unpack(">4sI4sIIIII", hdr)
    f.seek(32)
    toc = f.read(toc_len - 32)
    if flags & 4:
        from psarc_extract import decrypt_toc
        toc = decrypt_toc(toc)
    names = read_entry(f, block_size, zsizes, entries[0]).decode().split("\n")
    by_name = {n: i + 1 for i, n in enumerate(names)}
    for name in repl:
        if name not in by_name:
            sys.exit(f"no entry {name}")
    replaced = {by_name[n]: open(p, "rb").read() for n, p in repl.items()}

    zbytes = 2 if block_size <= 0x10000 else 3 if block_size <= 0x1000000 else 4
    blocks, lengths = [], []
    for i, ent in enumerate(entries):
        if i in replaced:
            blocks.append(compress_blocks(replaced[i], block_size))
            lengths.append(len(replaced[i]))
        else:
            blocks.append(raw_blocks(f, block_size, zsizes, ent))
            lengths.append(ent[1])

    n_blocks = sum(len(b) for b in blocks)
    new_toc_len = 32 + n_ent * ent_size + n_blocks * zbytes
    # Keep the original data order on disk: entries sorted by old offset.
    order = sorted(range(n_ent), key=lambda i: entries[i][2])
    offsets, pos = {}, new_toc_len
    for i in order:
        offsets[i] = pos
        pos += sum(len(d) for _, d in blocks[i])
    zindex, ztab, zi = {}, bytearray(), 0
    for i in range(n_ent):
        zindex[i] = zi
        for z, _ in blocks[i]:
            ztab += z.to_bytes(zbytes, "big")
            zi += 1

    new_toc = bytearray()
    for i in range(n_ent):
        e = toc[i * ent_size:(i + 1) * ent_size]
        new_toc += e[:16] + struct.pack(">I", zindex[i]) + lengths[i].to_bytes(5, "big") + \
            offsets[i].to_bytes(5, "big") + e[30:]
    new_toc += ztab
    if flags & 4:
        new_toc = encrypt_toc(bytes(new_toc))

    with open(dst, "wb") as o:
        o.write(struct.pack(">4sI4sIIIII", magic, ver, comp, new_toc_len, ent_size, n_ent,
                            block_size, flags))
        o.write(new_toc)
        for i in order:
            for _, d in blocks[i]:
                o.write(d)


if __name__ == "__main__":
    main()
