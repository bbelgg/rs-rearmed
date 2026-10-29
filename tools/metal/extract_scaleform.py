#!/usr/bin/env python3
"""Writes the Scaleform GLSL shaders the game compiles, from the game image, without running it.

    extract_scaleform.py <x86_64 game image> <out dir>

Scaleform's GL HAL (GFx 4.2) keeps the source of every shader it can use as a whole string in the
image: one set per GLSL version (no #version line, "#version 120", "#version 150"). On macOS's
legacy 2.1 context it uses the "#version 120" set, and before compiling it sizes the uniform arrays
of the batched shaders (vfuniforms, vfmuniforms: one slot group per batched draw) for half the
batches the strings are written for (12 instead of 24: what the HAL derives from the GL limits
both of our backends report). Nothing else changes: the result is exactly what RS_GL_DUMP_SHADERS
writes during a game run (all 213 shaders, checked byte for byte against dumps of menu and song
runs).

Output: <out>/<seq>_<seq>_<vs|fs>.glsl, the RS_GL_DUMP_SHADERS naming tools/metal/pair_shaders.py
reads (a vertex and a fragment shader with the same varyings make a program). Game-derived: keep it
in the build directory, never commit it. Exit status 1 when the image does not hold the expected set
(another game version or a wrong file).
"""
import os
import re
import sys

VERSION = b'#version 120\n'
BATCH_ARRAY = re.compile(rb'^(uniform \w+ (?:vfuniforms|vfmuniforms))\[(\d+)\];', re.M)
# The shipped image has 66 vertex and 147 fragment shaders in the set; accept a little drift.
MIN_VS, MIN_FS = 40, 100


def shaders(image):
    """Every NUL-terminated string of the image that starts with "#version 120" and has a main()."""
    out, seen = [], set()
    for m in re.finditer(re.escape(VERSION), image):
        s = m.start()
        if s and image[s - 1] != 0:
            continue
        e = image.find(b'\0', s)
        text = image[s:e]
        if b'void main()' not in text or text in seen:
            continue
        seen.add(text)
        out.append(text)
    return out


def as_compiled(text):
    def half(m):
        n = int(m.group(2))
        return b'%s[%d];' % (m.group(1), n // 2 if n % 2 == 0 else n)
    return BATCH_ARRAY.sub(half, text)


def main():
    if len(sys.argv) != 3:
        sys.exit('usage: extract_scaleform.py <game image> <out dir>')
    with open(sys.argv[1], 'rb') as f:
        image = f.read()
    out = sys.argv[2]
    texts = shaders(image)
    vs = [t for t in texts if b'gl_Position' in t and b'gl_FragColor' not in t]
    fs = [t for t in texts if b'gl_FragColor' in t and b'gl_Position' not in t]
    if len(vs) < MIN_VS or len(fs) < MIN_FS or len(vs) + len(fs) != len(texts):
        print('extract_scaleform: %s: %d vertex and %d fragment shaders (%d strings) is not the expected '
              'Scaleform set' % (sys.argv[1], len(vs), len(fs), len(texts)), file=sys.stderr)
        return 1
    os.makedirs(out, exist_ok=True)
    for n in os.listdir(out):
        if n.endswith(('_vs.glsl', '_fs.glsl')):
            os.unlink(os.path.join(out, n))
    batched = 0
    for i, t in enumerate(texts):
        c = as_compiled(t)
        batched += c != t
        kind = 'vs' if t in vs else 'fs'
        with open(os.path.join(out, '%05d_%d_%s.glsl' % (i, i, kind)), 'wb') as f:
            f.write(c)
    print('scaleform: %d vertex, %d fragment shaders (%d batched) -> %s' % (len(vs), len(fs), batched, out))
    return 0


if __name__ == '__main__':
    sys.exit(main())
