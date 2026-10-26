#!/usr/bin/env python3
"""Diff the shim's parameter tables (tests/param_dump) against NVIDIA's
(tools/cg_reference.c, stored as tests/data/reference.jsonl).

    compare_params.py <reference.jsonl> <shim.jsonl>

Compares every field the game can observe: order, namespace, name, type,
semantic, direction, variability, resource, resource index, used, the
cgGetNamedParameter round trip and cgGLGetTextureEnum. Exits 1 on any mismatch.
"""
import json
import sys

FIELDS = ["ns", "name", "type", "semantic", "dir", "var", "res", "index", "used", "named_ok", "tex_enum"]


def load(path):
    return {json.loads(l)["file"]: json.loads(l) for l in open(path)}


def main():
    ref, shim = load(sys.argv[1]), load(sys.argv[2])
    bad = 0
    for name, r in sorted(ref.items()):
        s = shim.get(name)
        if s is None:
            print(f"{name}: missing from shim output")
            bad += 1
            continue
        problems = []
        if r["ok"] != s["ok"]:
            problems.append(f"ok {r['ok']} != {s['ok']}")
        rp, sp = r.get("params", []), s.get("params", [])
        if len(rp) != len(sp):
            problems.append(f"{len(rp)} params, shim has {len(sp)}")
        for i, (a, b) in enumerate(zip(rp, sp)):
            for f in FIELDS:
                if f in a and a.get(f) != b.get(f):
                    problems.append(f"param {i} ({a['name']}): {f} {a.get(f)!r} != {b.get(f)!r}")
        if problems:
            bad += 1
            print(f"{name}:")
            for p in problems[:8]:
                print("   ", p)
            if len(problems) > 8:
                print(f"    ... {len(problems) - 8} more")
    total = len(ref)
    print(f"{total - bad}/{total} programs match NVIDIA's parameter tables")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
