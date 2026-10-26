#!/usr/bin/env python3
"""Compare tests/diff_render output of the arb backend (reference: Apple's GL
running the ARB object) with the glsl backend (the shim's translation).

    compare_renders.py <arb_dir> <glsl_dir> [--tol 2e-3] [--known FILE] [-v]

A pixel channel matches when |a-b| <= tol * max(1, |a|), or both are NaN, or
both are the same infinity. Exits 1 if any image differs in more than 0.1% of
channels (a few edge pixels may round differently).
"""
import argparse
import array
import math
import os
import sys


def load(path):
    a = array.array("f")
    with open(path, "rb") as f:
        a.frombytes(f.read())
    return a


def close(a, b, tol):
    if math.isnan(a) or math.isnan(b):
        return math.isnan(a) and math.isnan(b)
    if math.isinf(a) or math.isinf(b):
        return a == b
    return abs(a - b) <= tol * max(1.0, abs(a))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("arb")
    ap.add_argument("glsl")
    ap.add_argument("--tol", type=float, default=2e-3)
    ap.add_argument("-v", action="store_true")
    ap.add_argument("--known", help="file listing renders whose arb reference is not valid")
    args = ap.parse_args()
    known = {}
    if args.known:
        for line in open(args.known):
            if line.strip() and not line.startswith("#"):
                name, _, why = line.strip().partition(" ")
                known[name] = why.strip()
    names = sorted(os.listdir(args.arb))
    bad = 0
    for n in names:
        gp = os.path.join(args.glsl, n)
        if not os.path.exists(gp):
            print(f"{n}: missing from glsl run")
            bad += 1
            continue
        a, b = load(os.path.join(args.arb, n)), load(gp)
        diff = [i for i in range(len(a)) if not close(a[i], b[i], args.tol)]
        if len(diff) > len(a) // 1000:
            if n in known:
                print(f"{n}: differs as expected ({known[n][:70]}...)")
                continue
            bad += 1
            i = diff[0]
            print(f"{n}: {len(diff)}/{len(a)} channels differ, first at pixel {i // 4} "
                  f"ch {i % 4}: arb {a[i]!r} glsl {b[i]!r}")
        elif args.v:
            print(f"{n}: ok ({len(diff)} stray channels)")
    print(f"{len(names) - bad}/{len(names)} renders match")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
