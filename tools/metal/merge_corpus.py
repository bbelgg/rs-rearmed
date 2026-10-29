#!/usr/bin/env python3
"""Merges shader corpus files (format: shims/opengl/src/msl/msl_corpus.cpp) into one.

    merge_corpus.py <out> <in>...
"""
import os
import struct
import sys

MAGIC = b'RSMSLC3\0'
ENTRY = '<QQIIIIII'  # key, key2, vs_len, fs_len, offset, size, flags, check
ENTRY_SIZE = struct.calcsize(ENTRY)
HEADER = 24


def read(path):
    with open(path, 'rb') as f:
        d = f.read()
    if d[:8] != MAGIC:
        sys.exit('%s: not a shader corpus (or another format version)' % path)
    ver, count, pool_count, pool_off = struct.unpack_from('<IIII', d, 8)
    pool, o = [], pool_off
    for _ in range(pool_count):
        n, = struct.unpack_from('<I', d, o)
        pool.append(d[o + 4:o + 4 + n])
        o += 4 + n
    entries = {}
    for i in range(count):
        key, key2, vs_len, fs_len, off, size, flags, check = struct.unpack_from(ENTRY, d, HEADER + ENTRY_SIZE * i)
        blob = d[off:off + size]
        nseg, = struct.unpack_from('<I', blob, 0)
        segs = [pool[k] for k in struct.unpack_from('<%dI' % nseg, blob, 4)]
        entries[key] = (key2, vs_len, fs_len, flags, check, segs, blob[4 + 4 * nseg:])
    return ver, entries


def main():
    out, ins = sys.argv[1], sys.argv[2:]
    version, merged = None, {}
    for p in ins:
        ver, e = read(p)
        if version is not None and ver != version:
            sys.exit('%s: translator version %d, expected %d' % (p, ver, version))
        version = ver
        merged.update(e)
    if version is None:
        sys.exit('no input')
    keys = sorted(merged)
    pool_idx, pool = {}, []
    off = HEADER + ENTRY_SIZE * len(keys)
    index, blobs = [], []
    for k in keys:
        key2, vs_len, fs_len, flags, check, segs, rest = merged[k]
        ids = []
        for sg in segs:
            if sg not in pool_idx:
                pool_idx[sg] = len(pool)
                pool.append(sg)
            ids.append(pool_idx[sg])
        b = struct.pack('<I%dI' % len(ids), len(ids), *ids) + rest
        # `check` covers `rest` and the MSL, not the pool indices: it stays valid
        index.append(struct.pack(ENTRY, k, key2, vs_len, fs_len, off, len(b), flags, check))
        blobs.append(b)
        off += len(b)
    head = MAGIC + struct.pack('<IIII', version, len(keys), len(pool), off)
    poolb = b''.join(struct.pack('<I', len(sg)) + sg for sg in pool)
    with open(out + '.tmp', 'wb') as f:
        f.write(head + b''.join(index) + b''.join(blobs) + poolb)
    os.replace(out + '.tmp', out)
    print('%s: %d programs, %d shared MSL pieces, %.1f MB' % (out, len(keys), len(pool), os.path.getsize(out) / 1e6))


if __name__ == '__main__':
    main()
