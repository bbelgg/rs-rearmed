#!/usr/bin/env python3
"""Merges shader corpus files (format: shims/opengl/src/msl/msl_corpus.cpp) into one.

    merge_corpus.py <out> <in>...
"""
import struct
import sys

MAGIC = b'RSMSLC1\0'


def read(path):
    with open(path, 'rb') as f:
        d = f.read()
    if d[:8] != MAGIC:
        sys.exit('%s: not a shader corpus' % path)
    ver, count = struct.unpack_from('<II', d, 8)
    entries = {}
    for i in range(count):
        key, vs_len, fs_len, off, size, flags, _ = struct.unpack_from('<QIIIIII', d, 16 + 32 * i)
        entries[key] = (vs_len, fs_len, flags, d[off:off + size])
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
    head = MAGIC + struct.pack('<II', version, len(keys))
    off = 16 + 32 * len(keys)
    index, blobs = b'', []
    for k in keys:
        vs_len, fs_len, flags, blob = merged[k]
        index += struct.pack('<QIIIIII', k, vs_len, fs_len, off, len(blob), flags, 0)
        blobs.append(blob)
        off += len(blob)
    with open(out + '.tmp', 'wb') as f:
        f.write(head + index + b''.join(blobs))
    import os
    os.replace(out + '.tmp', out)
    print('%s: %d programs' % (out, len(keys)))


if __name__ == '__main__':
    main()
