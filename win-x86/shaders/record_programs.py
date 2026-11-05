#!/usr/bin/env python3
"""Adds what game runs linked to the list of Learn & Play's programs.

    record_programs.py [--list win-x86/shaders/programs.tsv] LOG...

LOG is a run's LP_SHADER_LOG=<dir>/log.tsv (docs/windows/TESTING.md "Shaders of a run"). The list holds one
line per program: "pair VS PS" or "ffp VS PS KEY". VS and PS are 64-bit hashes of the shaders' bytecode (0: that
stage is fixed function), KEY the fixed-function state key in hex, without the run's shader numbers. That is all a
line says: no shader text or bytecode, so the list can be in the repository. Sorted, so runs merge.
"""
import argparse
import os

HERE = os.path.dirname(os.path.abspath(__file__))
HEADER = ("# The programs Learn & Play was seen linking: win-x86/shaders/lp_shaders.c makes their shaders from the\n"
          "# user's game files. Hashes and state keys only. Add to it with record_programs.py.\n")
ID_HEX = 16  # ffp_key starts with the two shader numbers of the run (uint32 each)


def line_of(fields):
    """A log.tsv line's list line, or None for one that is not a program of the game."""
    if len(fields) < 8 or fields[1] != "program" or fields[2] == "blit":
        return None
    kind, vs, ps, key = fields[2], fields[3], fields[4], fields[7]
    short = lambda h: h.lstrip("0") or "0"
    if kind == "pair":
        return "pair %s %s" % (short(vs), short(ps))
    return "ffp %s %s %s" % (short(vs), short(ps), "0" * ID_HEX + key[ID_HEX:])


def read_list(path):
    try:
        with open(path) as f:
            return {l.strip() for l in f if l.strip() and not l.startswith("#")}
    except FileNotFoundError:
        return set()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--list", default=os.path.join(HERE, "programs.tsv"))
    ap.add_argument("logs", nargs="+")
    a = ap.parse_args()
    have = read_list(a.list)
    before = len(have)
    for log in a.logs:
        with open(log) as f:
            for l in f:
                p = line_of(l.rstrip("\n").split("\t"))
                if p:
                    have.add(p)
    with open(a.list, "w") as f:
        f.write(HEADER)
        for l in sorted(have):
            f.write(l + "\n")
    print("%d programs, %d new" % (len(have), len(have) - before))


if __name__ == "__main__":
    main()
