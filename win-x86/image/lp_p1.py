#!/usr/bin/env python3
"""P1 facts of a Learn & Play game image (lp_image.py output): what is there to translate.

  * per section: pages with data (captured) of all pages;
  * a Capstone linear sweep of .text (jump tables skipped via the image's relocations):
    direct call/jmp targets by where they land: captured .text, a .UBX0 import wrapper
    (a site the image's wrapper table, or iat_map.tsv, lists), other .UBX0 code (VMProtect virtualisation candidates, split
    by what precedes the transfer), or nowhere captured;
  * `call [slot]` / `jmp [slot]` through the IAT, and how many slots are named;
  * with a translation manifest (gen_image_pe.py's manifest.json): functions, translated and
    stubbed, and how many direct call targets in .text are function starts.

Usage: lp_p1.py IMAGE [--wrappers W.tsv] [--iat-map iat_map.tsv] [--manifest manifest.json]
  --wrappers defaults to IMAGE without .exe + .wrappers.tsv (lp_image.py writes it, at the image
  base); without one, iat_map.tsv's wrapper rows are used, moved from the link base.
"""
import argparse
import bisect
import collections
import json
import os
import struct
import sys

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(here, "..", "..", "tools", "ws7", "recomp"))
from xlat.pe import Image  # noqa: E402

import capstone  # noqa: E402
from capstone import x86  # noqa: E402

PAGE = 0x1000


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("image")
    ap.add_argument("--wrappers", default=None)
    ap.add_argument("--iat-map", default=os.path.join(here, "..", "imports", "iat_map.tsv"))
    ap.add_argument("--manifest")
    args = ap.parse_args()
    img = Image(args.image)
    sec = {s[0]: s for s in img.sections}

    def data_at(a, n):
        o = img.foff(a)
        return None if o is None else img.data[o:o + n]

    print("sections (pages with data / pages):")
    for name, va, vs, ro, rs, ch in img.sections:
        pages = range(va, va + vs, PAGE)
        got = sum(1 for p in pages if (data_at(p, PAGE) or b"").strip(b"\0"))
        print("  %-9s 0x%08x..0x%08x  %5d / %5d" % (name, va, va + vs, got, len(pages)))

    wrap_sites = set()
    wpath = args.wrappers or args.image[:-4] + ".wrappers.tsv"
    if os.path.exists(wpath):
        for line in open(wpath):
            f = line.rstrip("\n").split("\t")
            if not line.startswith("#") and f[0] != "call_site" and line.strip():
                wrap_sites.add(int(f[0], 16))
                if len(f) > 4 and f[4]:
                    wrap_sites.add(int(f[0], 16) + 1)     # push reg; call stub: the call is 1 byte on
    else:
        shift = img.base - 0x400000
        for line in open(args.iat_map):
            f = line.rstrip("\n").split("\t")
            if f[0] == "wrap":
                wrap_sites.add(int(f[1], 16) + shift)
    named = {s for s, n in img.imports.items() if "lp_unresolved_" not in n}

    t0, t1 = sec[".text"][1], sec[".text"][1] + sec[".text"][2]
    u0, u1 = sec[".UBX0"][1], sec[".UBX0"][1] + sec[".UBX0"][2]
    code = img.data[img.foff(t0):img.foff(t0) + (t1 - t0)]
    # words in .text the image relocates and no instruction decodes over: jump tables
    relocs = sorted(r for r in img.relocs if t0 <= r < t1)
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    md.skipdata = True
    cnt = collections.Counter()
    targets = collections.Counter()
    vm = collections.Counter()
    vm_examples = []
    prev = None
    a = t0
    while a < t1:
        nxt = a + 1
        for i in md.disasm(code[a - t0:min(t1, a + 0x10000) - t0], a):
            nxt = i.address + i.size
            if i.id == 0:
                prev = None
                continue
            m = i.mnemonic
            if m in ("call", "jmp") and i.operands:
                op = i.operands[0]
                if op.type == x86.X86_OP_IMM:
                    t = op.imm & 0xffffffff
                    if t0 <= t < t1:
                        k = "text"
                        targets[(m, t)] += 1
                    elif u0 <= t < u1:
                        if i.address in wrap_sites:
                            k = "ubx0_wrapper"
                        elif data_at(t, 1) is None or not data_at(t, 16).strip(b"\0"):
                            k = "ubx0_uncaptured"
                        else:
                            k = "ubx0_other"
                        if k != "ubx0_wrapper":
                            pre = "push_imm" if prev is not None and prev.mnemonic == "push" and \
                                prev.operands and prev.operands[0].type == x86.X86_OP_IMM else "plain"
                            vm[(m, k, pre)] += 1
                            if len(vm_examples) < 12 and pre == "push_imm":
                                vm_examples.append("%x: %s %s ; %s 0x%x" % (prev.address, prev.mnemonic, prev.op_str, m, t))
                    else:
                        s = img.section_of(t)
                        k = "in_" + s[0] if s else "outside_image"
                    cnt[(m, k)] += 1
                elif op.type == x86.X86_OP_MEM and op.mem.base == 0 and op.mem.index == 0:
                    s = op.mem.disp & 0xffffffff
                    if s in img.imports:
                        cnt[(m, "iat_named" if s in named else "iat_unresolved")] += 1
            prev = i
        a = max(nxt, a + 1)
        k = bisect.bisect_left(relocs, a)
        while k < len(relocs) and relocs[k] == a and a < t1:      # jump table words
            a += 4
            k += 1

    print("\nlinear sweep of .text, direct transfers by destination:")
    for (m, k), n in sorted(cnt.items()):
        print("  %-5s %-16s %7d" % (m, k, n))
    print("\ntransfers into .UBX0 that are not listed import wrappers (virtualisation candidates):")
    for (m, k, pre), n in sorted(vm.items()):
        print("  %-5s %-16s %-9s %6d" % (m, k, pre, n))
    for e in vm_examples:
        print("    " + e)
    print("\nexports: %d; entry 0x%x; relocations %d; IAT slots %d (%d named)"
          % (len(img.exports), img.entry, len(img.relocs), len(img.imports), len(named)))

    if args.manifest:
        man = json.load(open(args.manifest))
        pf = man["per_function"]
        starts = {int(k, 16) for k in pf}
        ok = {int(k, 16) for k, v in pf.items() if v[0] == "ok"}
        calls = {t for (m, t) in targets if m == "call"}
        print("\nmanifest: %d functions %s; discovery %s" % (man["functions"], man["counts"], man["discovery"]))
        print("direct call targets in .text: %d distinct, %d function starts, %d translated"
              % (len(calls), len(calls & starts), len(calls & ok)))
        print("stub reasons (top):")
        for r, n in man["stub_reasons"][:12]:
            print("  %6d  %s" % (n, r))


if __name__ == "__main__":
    main()
