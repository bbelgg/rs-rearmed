#!/usr/bin/env python3
"""Extract every Cg program object Rocksmith 2014 ships, for the shim's tests.

    extract_corpus.py [--game DIR] [--out analysis/cg]

Writes <out>/corpus/*.vpo|*.fpo: the shaders/macos/*.vpo/.fpo files from
static.psarc, plus the programs embedded in the Gamebryo shader caches
(shaders/generated/*.cache, "NiGPUProgram Cache File": each program is a
little-endian uint32 length followed by the "!!ARB..." text), named
<cache>_<nnn>.vpo/.fpo. These are exactly the texts the game hands to
cgCreateProgram(CG_OBJECT, ...). Then run tools/make_reference.sh to record
NVIDIA's runtime's view of them.
"""
import argparse
import glob
import os
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
DEFAULT_GAME = os.path.expanduser(
    "~/Library/Application Support/Steam/steamapps/common/Rocksmith2014")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--game", default=DEFAULT_GAME)
    ap.add_argument("--out", default=os.path.join(ROOT, "analysis", "cg"))
    args = ap.parse_args()

    static = os.path.join(args.out, "static")
    subprocess.run([sys.executable, "-I", os.path.join(ROOT, "tools", "psarc_extract.py"),
                    os.path.join(args.game, "static.psarc"), static, "--match", "shaders/"],
                   check=True, stdout=subprocess.DEVNULL)
    corpus = os.path.join(args.out, "corpus")
    os.makedirs(corpus, exist_ok=True)
    n = 0
    for f in sorted(glob.glob(os.path.join(static, "shaders", "macos", "*.[vf]po"))):
        with open(f, "rb") as src, open(os.path.join(corpus, os.path.basename(f)), "wb") as dst:
            dst.write(src.read())
        n += 1
    for f in sorted(glob.glob(os.path.join(static, "shaders", "generated", "*.cache"))):
        data = open(f, "rb").read()
        base = os.path.basename(f)[:-len(".cache")]
        i = k = 0
        while True:
            i = data.find(b"!!ARB", i)
            if i < 0:
                break
            length = struct.unpack("<I", data[i - 4:i])[0]
            prog = data[i:i + length].rstrip(b"\0")
            ext = "vpo" if prog.startswith(b"!!ARBvp") else "fpo"
            with open(os.path.join(corpus, f"{base}_{k:03d}.{ext}"), "wb") as dst:
                dst.write(prog)
            i += length
            k += 1
            n += 1
    print(f"{n} programs in {corpus}")


if __name__ == "__main__":
    main()
