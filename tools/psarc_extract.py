#!/usr/bin/env python3
"""Extract files from a Rocksmith 2014 .psarc archive.

    psarc_extract.py <archive.psarc> <out_dir> [--list] [--match SUBSTR]

The TOC is AES-256-CFB encrypted with the game's key, which is read from the
game the archive belongs to (tools/play-native/psarc_key.py: found by itself for an
archive in a Mac game folder or in a copy of the game files, else from the image
RS_PSARC_KEY_IMAGE names); decryption is done with the `openssl` CLI so no Python
crypto package is needed. Entry 0 is the manifest (newline-separated file names
for entries 1..n).
"""
import argparse
import os
import struct
import sys
import zlib

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "play-native"))
import psarc_key  # noqa: E402
del sys.path[0]


def decrypt_toc(data, path):
    """The table of the archive at PATH; reading it also tells the IV a writer needs (psarc_pack.py)."""
    try:
        key = psarc_key.toc_key(path)
    except psarc_key.NoKey as e:
        sys.exit(str(e))
    psarc_key.table_iv(data[:16], key)
    return psarc_key.crypt_toc(data, True, key)


def read_archive(path):
    f = open(path, "rb")
    hdr = f.read(32)
    magic, ver, comp, toc_len, ent_size, n_ent, block_size, flags = struct.unpack(">4sI4sIIIII", hdr)
    if magic != b"PSAR":
        sys.exit("not a PSARC file")
    toc = f.read(toc_len - 32)
    if flags & 4:
        toc = decrypt_toc(toc, path)
    entries = []
    for i in range(n_ent):
        e = toc[i * ent_size:(i + 1) * ent_size]
        zindex = struct.unpack(">I", e[16:20])[0]
        length = int.from_bytes(e[20:25], "big")
        offset = int.from_bytes(e[25:30], "big")
        entries.append((zindex, length, offset))
    zbytes = 2 if block_size <= 0x10000 else 3 if block_size <= 0x1000000 else 4
    ztab = toc[n_ent * ent_size:]
    zsizes = [int.from_bytes(ztab[i:i + zbytes], "big") for i in range(0, len(ztab) - zbytes + 1, zbytes)]
    return f, block_size, entries, zsizes


def read_entry(f, block_size, zsizes, entry):
    zindex, length, offset = entry
    f.seek(offset)
    out = bytearray()
    zi = zindex
    while len(out) < length:
        z = zsizes[zi]
        zi += 1
        if z == 0:
            z = block_size
        chunk = f.read(z)
        want = min(block_size, length - len(out))
        if z != want:
            try:
                chunk = zlib.decompress(chunk)
            except zlib.error:
                pass
        out += chunk
    return bytes(out[:length])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("archive")
    ap.add_argument("out_dir", nargs="?")
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--match", default="")
    a = ap.parse_args()

    f, bs, entries, zsizes = read_archive(a.archive)
    names = read_entry(f, bs, zsizes, entries[0]).decode().split("\n")
    for name, ent in zip(names, entries[1:]):
        if a.match not in name:
            continue
        if a.list or not a.out_dir:
            print(f"{ent[1]:12d}  {name}")
            continue
        # an archive's names are its maker's: one that leads out of OUT_DIR (`..`) is not written
        root = os.path.realpath(a.out_dir)
        dst = os.path.realpath(os.path.join(root, name.lstrip("/")))
        if os.path.commonpath([root, dst]) != root or dst == root:
            print(f"skipped (outside {a.out_dir}): {name}", file=sys.stderr)
            continue
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        with open(dst, "wb") as o:
            o.write(read_entry(f, bs, zsizes, ent))
        print(dst)


if __name__ == "__main__":
    main()
