#!/usr/bin/env python3
"""What a tools/perf/soak.py run shows.

  tools/perf/soak_report.py OUT/EDITION

Memory over time (the footprint by five minutes, its slope, and at each song's end), by kind (vmmap's regions and
malloc zones, the footprint's categories) with what each gained from the first minute to the end, the leaks
`leaks` counts, CPU by five minutes and by thread, open files, frames by five minutes with the long ones told
apart (a tool stopping the game, a screen change, or the middle of a song), the songs played, and the log's lines
by kind. A leak shows as a slope that stays, a malloc zone whose blocks only grow, or a `leaks` count that grows
between the two reports."""
import json
import os
import re
import sys
from collections import Counter

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import stats  # noqa: E402


def main(argv):
    if len(argv) != 2:
        print(__doc__, file=sys.stderr)
        return 64
    d = argv[1]
    with open(os.path.join(d, "samples.json")) as fh:
        J = json.load(fh)
    S, E, t0 = J["samples"], J["events"], J["t0_unix"]
    MB = 2.0 ** 20
    print("== %s, %s, %g minutes asked; summary %s" % (J["edition"], J["path"], J["minutes"],
                                                    {k: v for k, v in J["summary"].items() if k != "threads_cpu"}))
    if not S:
        print("no samples")
        return 1


    def size(text):
        m = re.match(r"([\d.]+)([KMGT]?)", text)
        return float(m.group(1)) * {"": 1 / 1024 / 1024, "K": 1 / 1024, "M": 1, "G": 1024, "T": 1024 * 1024}[m.group(2)]


    # --- memory over time
    print("\n-- memory, MB (5-minute means): footprint, RSS, helper footprint")
    for a in range(0, int(S[-1]["t"] // 300) + 1):
        rows = [r for r in S if a * 300 <= r["t"] < (a + 1) * 300]
        if rows:
            print("  %2d-%2d min  footprint %7.0f (min %7.0f max %7.0f)  rss %7.0f  helper %5.0f  threads %d-%d" % (
                a * 5, a * 5 + 5, sum(r["footprint"] for r in rows) / len(rows) / MB, min(r["footprint"] for r in rows) / MB,
                max(r["footprint"] for r in rows) / MB, sum(r["rss"] for r in rows) / len(rows) / MB,
                sum(r["helper_footprint"] for r in rows) / len(rows) / MB, min(r["threads"] for r in rows),
                max(r["threads"] for r in rows)))
    for lo in (0, 300, 600):
        pts = [(r["t"], r["footprint"]) for r in S if r["t"] >= lo]
        if len(pts) > 10:
            print("  slope from minute %d: %+.2f MB/min" % (lo // 60, (stats.slope(pts) or 0) * 60 / MB))
    # the footprint while a song plays, at each song's end (the last sample on the highway before the screen changes)
    ends, prev = [], None
    for r in S:
        if prev and prev["screen"] == "NonStopPlay_Game" and r["screen"] != "NonStopPlay_Game":
            ends.append((prev["t"], prev["footprint"] / MB))
        prev = r
    print("  footprint at each song's end:", " ".join("%.0f@%dm" % (f, t // 60) for t, f in ends))
    if len(ends) > 3:
        print("  slope over the songs' ends: %+.2f MB/min" % ((stats.slope(ends) or 0) * 60))

    # --- memory by kind (vmmap) and malloc
    maps = sorted(f for f in os.listdir(d) if f.startswith("vmmap-"))


    def read_vmmap(path):
        regions, zones, mode = {}, {}, None
        with open(path, errors="replace") as fh:
            text = fh.read()
        for ln in text.split("\n"):
            if ln.startswith("REGION TYPE"):
                mode = "r"
                continue
            if ln.startswith("MALLOC ZONE"):
                mode = "z"
                continue
            if ln.startswith("===") or not ln.strip():
                continue
            if mode == "r":
                m = re.match(r"(.+?)\s{2,}([\d.]+[KMGT]?)\s+([\d.]+[KMGT]?)\s+([\d.]+[KMGT]?)\s+([\d.]+[KMGT]?)\s+([\d.]+[KMGT]?)", ln)
                if m:  # virtual, resident, dirty, swapped
                    regions[m.group(1).strip()] = (size(m.group(4)) + size(m.group(5)), size(m.group(2)))
            elif mode == "z":
                m = re.match(r"(\S.*?)\s+([\d.]+[KMGT]?)\s+([\d.]+[KMGT]?)\s+([\d.]+[KMGT]?)\s+([\d.]+[KMGT]?)\s+(\d+)\s+([\d.]+[KMGT]?)", ln)
                if m:
                    zones[re.sub(r"_0x[0-9a-f]+$", "", m.group(1))] = (size(m.group(7)), int(m.group(6)), size(m.group(4)) + size(m.group(5)))
        return regions, zones


    if len(maps) >= 2:
        data = [(f, read_vmmap(os.path.join(d, f))) for f in maps]
        names = sorted(set(n for _, (r, _) in data for n in r), key=lambda n: -data[-1][1][0].get(n, (0, 0))[0])
        print("\n-- dirty + swapped by region kind, MB (%s)" % ", ".join(f[6:-4] for f in maps))
        for n in names:
            v = [m[0].get(n, (0, 0))[0] for _, m in data]
            if max(v) >= 2 or abs(v[-1] - v[0]) >= 1:
                print("  %-36s %s   %+.1f" % (n[:36], " ".join("%8.1f" % x for x in v), v[-1] - v[0]))
        print("-- malloc zones: bytes allocated MB / blocks")
        for z in sorted(set(n for _, (_, zz) in data for n in zz), key=lambda n: -data[-1][1][1].get(n, (0, 0, 0))[0]):
            v = [m[1].get(z, (0, 0, 0)) for _, m in data]
            if max(x[0] for x in v) >= 1:
                print("  %-36s %s   %+.1f MB, %+d blocks" % (z[:36], " ".join("%7.1f/%-7d" % (x[0], x[1]) for x in v),
                                                             v[-1][0] - v[0][0], v[-1][1] - v[0][1]))

    foots = sorted(f for f in os.listdir(d) if f.startswith("footprint-"))
    if len(foots) >= 2:
        def read_fp(path):
            out = {}
            with open(path, errors="replace") as fh:
                text = fh.read()
            for ln in text.split("\n"):
                m = re.match(r"\s*([\d.]+ [KMG]?B)\s+([\d.]+ [KMG]?B)\s+([\d.]+ [KMG]?B)\s+(\d+)\s+(.+)", ln)
                if m:
                    out[m.group(5).strip()] = size(m.group(1).replace(" ", "").replace("B", ""))
            return out
        fps = [(f, read_fp(os.path.join(d, f))) for f in foots]
        print("\n-- footprint by category, dirty MB (%s)" % ", ".join(f[10:-4] for f in foots))
        for n in sorted(set(k for _, v in fps for k in v), key=lambda n: -fps[-1][1].get(n, 0)):
            v = [x.get(n, 0) for _, x in fps]
            if max(v) >= 2:
                print("  %-48s %s   %+.1f" % (n[:48], " ".join("%8.1f" % x for x in v), v[-1] - v[0]))

    # --- leaks
    print("\n-- leaks")
    for f in sorted(x for x in os.listdir(d) if x.startswith("leaks")):
        with open(os.path.join(d, f), errors="replace") as fh:
            text = fh.read()
        m = re.search(r"(\d+) leaks? for (\d+) total leaked bytes", text)
        n = re.search(r"(\d+) nodes malloced for (\d+) KB", text)
        print("  %-14s %s; %s" % (f, "%s leaks, %.2f MB" % (m.group(1), int(m.group(2)) / MB) if m else "no count",
                                  "%s blocks, %.0f MB malloced" % (n.group(1), int(n.group(2)) / 1024) if n else ""))
        roots = Counter()
        for r in re.finditer(r"^\s+(\d+) \(([\d.]+[KMG]?)\) ROOT (LEAK|CYCLE): (?:<(.+?)>|0x[0-9a-f]+ \[(\d+)\])", text, re.M):
            roots[r.group(4) or "block of %s" % r.group(5)] += size(r.group(2))
        for k, v in roots.most_common(8):
            print("      %8.2f MB  %s" % (v, k[:90]))

    # --- CPU
    print("\n-- CPU, % of a core by 5 minutes; helper; GPU %; load; front")
    for a in range(0, int(S[-1]["t"] // 300) + 1):
        rows = [r for r in S if a * 300 <= r["t"] < (a + 1) * 300]
        if len(rows) > 2:
            dt = rows[-1]["unix"] - rows[0]["unix"]
            gpu = [r["gpu"] for r in rows if r["gpu"] is not None]
            print("  %2d-%2d min  game %5.1f  helper %5.1f  gpu %s  load %.1f  not in front %d of %d" % (
                a * 5, a * 5 + 5, (rows[-1]["cpu"] - rows[0]["cpu"]) / dt * 100,
                (rows[-1]["helper_cpu"] - rows[0]["helper_cpu"]) / dt * 100,
                "%d-%d" % (min(gpu), max(gpu)) if gpu else "?", sum(r["load"] for r in rows) / len(rows),
                sum(1 for r in rows if not r["front"]), len(rows)))
    tc = J["summary"].get("threads_cpu")
    if tc:
        print("  threads over the run: " + ", ".join("%s %.1f" % (n, p) for n, p in tc[:10]))
    mins = [r for r in S if "fds" in r]
    print("  open files %s; song archives open %s; thermal limit %s; swap MB %s" % (
        "%d..%d (first %d, last %d)" % (min(r["fds"] for r in mins), max(r["fds"] for r in mins), mins[0]["fds"], mins[-1]["fds"]),
        "%d..%d" % (min(r["fds_psarc"] for r in mins), max(r["fds_psarc"] for r in mins)),
        sorted(set(str(r["cpu_speed_limit"]) for r in mins)), "%s..%s" % (mins[0]["swap_used_mb"], mins[-1]["swap_used_mb"])))

    # --- frames
    with open(os.path.join(d, "frames.txt")) as fh:
        ft = stats.parse_frametimes(fh)
    print("\n-- frames by 5 minutes (intervals between presents, ms)")
    for a in range(0, int(S[-1]["t"] // 300) + 1):
        fs = stats.frame_stats(ft, t0 + a * 300, t0 + min((a + 1) * 300, S[-1]["t"]))
        if "error" not in fs:
            print("  %2d-%2d min  fps %6.2f  median %5.2f  p99 %6.2f  worst %7.1f  >20: %4d  >33: %3d  >50: %3d  gpu %s ms  missed %s" % (
                a * 5, a * 5 + 5, fs["fps"], fs["median_ms"], fs["p99_ms"], fs["worst_ms"], fs["over_20"], fs["over_33"],
                fs["over_50"], fs.get("gpu_ms"), fs.get("shown_missed")))
    # long frames, and what was going on: a screen change, a tool that stops the process, or the middle of a song
    if ft["wall"] is not None:
        pres = sorted(e[0] for e in ft["F"])
        a, b = t0 * 1e9 - ft["wall"], (t0 + S[-1]["t"]) * 1e9 - ft["wall"]
        longs = [((x + ft["wall"]) / 1e9 - t0, (y - x) / 1e6) for x, y in zip(pres, pres[1:]) if a <= x < b and y - x > 50e6]
        marks = [(e["t"], e["what"]) for e in E if "menu: " in e["what"] or " took " in e["what"]]
        change = [t for t, w in marks if "menu: " in w]
        tools = [(t, w) for t, w in marks if " took " in w]

        def why(t):
            for tt, w in tools:
                took = float(re.search(r"took ([\d.]+)", w).group(1))
                if tt - took - 2 <= t <= tt + 2:
                    return "tool"
            return "screen change" if any(abs(t - c) < 12 for c in change) else "in a song"
        kinds = Counter(why(t) for t, _ in longs)
        print("  frames over 50 ms: %d (%s)" % (len(longs), ", ".join("%s %d" % kv for kv in kinds.items())))
        insong = [(t, ms) for t, ms in longs if why(t) == "in a song"]
        print("  in a song: " + (" ".join("%.0fms@%d:%02d" % (ms, t // 60, t % 60) for t, ms in insong[:40]) or "none"))
        print("  the ten longest: " + " ".join("%.0fms@%d:%02d(%s)" % (ms, t // 60, t % 60, why(t)[:6])
                                             for t, ms in sorted(longs, key=lambda x: -x[1])[:10]))

    # --- songs and screens
    songs = [(e["t"], e["what"].split("song: ")[1]) for e in E if "song: " in e["what"] and e["t"] > -30]
    scr = [(e["t"], e["what"].split("menu: ")[1]) for e in E if "menu: " in e["what"] and e["t"] >= 0]
    print("\n-- %d songs: %s" % (len(songs), ", ".join("%s@%d:%02d" % (s, max(t, 0) // 60, max(t, 0) % 60) for t, s in songs)))
    on = sum(5 for r in S if r["screen"] == "NonStopPlay_Game")
    print("  on the highway %.0f%% of the samples; screens seen: %s" % (100.0 * on / (5 * len(S)), dict(Counter(s for _, s in scr))))
    odd = [e for e in E if e["t"] >= 0 and not any(k in e["what"] for k in ("menu: ", "song: ", " took ", "first note"))]
    print("  other events: %d" % len(odd))
    for e in odd[:25]:
        print("    %6.0f  %s" % (e["t"], e["what"][:150]))

    # --- the log
    with open(os.path.join(d, "game.log"), errors="replace") as fh:
        log = fh.read().split("\n")
    pre = Counter(re.match(r"(\[[^\]]+\]|\w+:)?", ln).group(0) or "(none)" for ln in log if ln.strip())
    print("\n-- log: %d lines; by start: %s" % (len(log), ", ".join("%s %d" % kv for kv in pre.most_common(12))))
    bad = Counter(re.sub(r"0x[0-9a-f]+|\d+", "N", ln)[:110] for ln in log
                  if re.search(r"error|fail|abort|assert|warning|not implemented|unhandled|exception", ln, re.I))
    for k, v in bad.most_common(15):
        print("  %5d  %s" % (v, k))
    ng = os.path.join(d, "netguard.log")
    if os.path.exists(ng):
        with open(ng, errors="replace") as fh:
            lines = fh.read().split("\n")
        print("-- netguard: %d lines; refused or blocked: %d" % (len(lines), sum(1 for ln in lines if re.search(r"refus|block(ed)? |deny|denied", ln) and "mode=block" not in ln)))

    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
