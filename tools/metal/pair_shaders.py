#!/usr/bin/env python3
"""Pairs dumped GLSL shaders into the programs msl_verify / the shader corpus check.

    pair_shaders.py [--scaleform DIR] [--cg DIR] > pairs.tsv

Output lines: <vertex file> TAB <fragment file> TAB <attribute bindings>.

Scaleform (RS_GL_DUMP_SHADERS output, <seq>_<id>_<vs|fs>.glsl): the HAL builds each program
from a vertex and a fragment shader generated for the same shader descriptor, so a program's
two stages declare exactly the same varyings. Every vertex/fragment pair with identical varying
declarations is listed (a superset of what the game links). No bindings: the translator assigns
locations in declaration order, like GL's linker.

Cg shim (cg_corpus_load --dump-glsl output, <key>.<varying bits>.vert / <key>.frag): the game
draws any vertex program with any fragment program and the shim emits the vertex shader variant
whose varyings are the fragment shader's inputs, so every vertex shader is paired with every
fragment shader that reads exactly its varyings. Bindings rs_attrN -> N, as the shim binds them.
"""
import argparse
import os
import re
import sys

VARYING = re.compile(r'^\s*varying\s+(\w+)\s+(\w+)\s*;', re.M)


def varyings(path):
    with open(path, encoding='utf-8', errors='replace') as f:
        return frozenset(VARYING.findall(f.read()))


def pairs(vs_files, fs_files, bindings):
    vs = {f: varyings(f) for f in vs_files}
    fs = {f: varyings(f) for f in fs_files}
    by_set = {}
    for f, v in vs.items():
        by_set.setdefault(v, []).append(f)
    out = []
    for f in sorted(fs):
        for v in sorted(by_set.get(fs[f], [])):
            out.append((v, f, bindings))
    unpaired = [f for f in fs if fs[f] not in by_set]
    return out, unpaired


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--scaleform')
    ap.add_argument('--cg')
    a = ap.parse_args()
    out = []
    for d, vsuf, fsuf, bind in ((a.scaleform, '_vs.glsl', '_fs.glsl', ''),
                                (a.cg, '.vert', '.frag', ','.join('rs_attr%d=%d' % (i, i) for i in range(16)))):
        if not d or not os.path.isdir(d):
            continue
        names = sorted(n for n in os.listdir(d) if not n.startswith('.'))  # no ._ AppleDouble files (exFAT)
        v = [os.path.join(d, n) for n in names if n.endswith(vsuf)]
        f = [os.path.join(d, n) for n in names if n.endswith(fsuf)]
        p, unpaired = pairs(v, f, bind)
        print('%s: %d vertex, %d fragment shaders -> %d pairs%s' % (d, len(v), len(f), len(p),
              ', %d fragment shaders without a vertex shader' % len(unpaired) if unpaired else ''), file=sys.stderr)
        out += p
    for v, f, b in out:
        print('%s\t%s\t%s' % (v, f, b))


if __name__ == '__main__':
    main()
