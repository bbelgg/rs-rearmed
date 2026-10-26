#!/usr/bin/env python3
"""Extract files from a Rocksmith 2014 .psarc archive.

    psarc_extract.py <archive.psarc> <out_dir> [--list] [--match SUBSTR]

The TOC is AES-256-CFB encrypted with the well-known Rocksmith key; decryption
is done with the `openssl` CLI so no Python crypto package is needed. Entry 0 is
the manifest (newline-separated file names for entries 1..n).
"""
import argparse
import os
import struct
import subprocess
import sys
import zlib

TOC_KEY = "C53DB23870A1A2F71CAE64061FDD0E1157309DC85204D4C5BFDF25090DF2572C"
TOC_IV = "E915AA018FEF71FC508132E4BB4CEB42"


def decrypt_toc(data):
    # CFB is a stream mode, but openssl wants whole blocks for -nopad; pad and trim.
    padded = data + b"\0" * (-len(data) % 16)
    out = subprocess.run(
        ["openssl", "enc", "-d", "-aes-256-cfb", "-nopad", "-K", TOC_KEY, "-iv", TOC_IV],
        input=padded, capture_output=True, check=True).stdout
    return out[:len(data)]


def read_archive(path):
    f = open(path, "rb")
    hdr = f.read(32)
    magic, ver, comp, toc_len, ent_size, n_ent, block_size, flags = struct.unpack(">4sI4sIIIII", hdr)
    if magic != b"PSAR":
        sys.exit("not a PSARC file")
    toc = f.read(toc_len - 32)
    if flags & 4:
        toc = decrypt_toc(toc)
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
        dst = os.path.join(a.out_dir, name.lstrip("/"))
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        with open(dst, "wb") as o:
            o.write(read_entry(f, bs, zsizes, ent))
        print(dst)


if __name__ == "__main__":
    main()
