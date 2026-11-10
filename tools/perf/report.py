#!/usr/bin/env python3
"""The table of a set of measured runs (tools/perf/run.py), and how it differs from another set.

  tools/perf/report.py OUT                   a Markdown table, a column an edition, each cell the runs' range
  tools/perf/report.py OUT --against BASE    the same with the change from BASE's runs beside each cell

Warm-up runs, runs that stopped, runs whose window was not in front and runs that had other work beside them are
left out and listed. What the two
editions' runs did not have in common (settings, mods, switches, display) is listed under the table: a table of
runs that differ there is not a comparison. A change smaller than the noise between runs of one build is marked
"within noise": about 2 points of a core, and the larger of 2% and the runs' own spread for the rest.

Two rows need care. "Handed over" is when the game gave a frame to Metal: it moves with the game's own work in each
frame, and says nothing of what was seen; "On the screen" does. And at a fixed frame rate neither the GPU's
utilisation nor its start-to-end time is its work: the GPU slows its clock to fill the refresh. What it can do
shows only with the frame rate free (RS_FPS above the display's), on a Mac that is cool.
"""
import argparse
import glob
import json
import os
import sys

# (label, how to read it from a result, digits, noise floor in the row's unit)
ROWS = [
    ("Frame rate", lambda r: r["frames"].get("fps"), 1, 0.3),
    ("On the screen: interval, p99 (ms)", lambda r: r["frames"].get("shown_p99_ms"), 2, 0.2),
    ("Frames shown late", lambda r: r["frames"].get("shown_late"), 0, None),
    ("Refreshes without a new frame", lambda r: r["frames"].get("shown_missed"), 0, None),
    ("Handed over: interval, median (ms)", lambda r: r["frames"].get("median_ms"), 2, 0.1),
    ("p95 (ms)", lambda r: r["frames"].get("p95_ms"), 1, 0.5),
    ("p99 (ms)", lambda r: r["frames"].get("p99_ms"), 1, 1.0),
    ("Worst (ms)", lambda r: r["frames"].get("worst_ms"), 1, None),
    ("Over 33 ms", lambda r: r["frames"].get("over_33"), 0, None),
    ("Over 50 ms", lambda r: r["frames"].get("over_50"), 0, None),
    ("On the GPU, start to end (ms)", lambda r: r["frames"].get("gpu_ms"), 2, 0.3),
    ("Game frame, median (ms)", lambda r: r["frames"].get("game_frame_ms"), 2, 0.1),
    ("CPU (% of a core)", lambda r: r.get("cpu_pct"), 1, 2.0),
    ("CPU per frame (ms)", lambda r: r.get("cpu_per_frame_ms"), 2, 0.25),
    ("Helpers' CPU (% of a core)", lambda r: sum(h["cpu_pct"] for h in r.get("helpers", {}).values())
        if r.get("helpers") else None, 1, 2.0),
    ("GPU (%)", lambda r: (r.get("gpu_pct") or {}).get("mean"), 0, 3.0),
    ("Footprint, largest (GB)", lambda r: (r.get("footprint_gb") or {}).get("max"), 2, 0.03),
    ("Footprint's growth (MB a minute)", lambda r: r.get("footprint_mb_per_min"), 1, None),
]
NAMES = {"mac": "Mac", "lp": "Learn & Play"}
BUSY = 3.0  # the load beside the game's own over which a run is not a result (run.py's other_load)


def load(out):
    """-> ({edition: [result]}, [why a run was left out])"""
    runs, left = {}, []
    for path in sorted(glob.glob(os.path.join(out, "*", "result.json"))):
        with open(path) as f:
            r = json.load(f)
        name = os.path.basename(os.path.dirname(path))
        if r.get("kind") != "measured":
            continue
        if "error" in r:
            left.append("%s: %s" % (name, r["error"]))
        elif "error" in r.get("frames", {}):
            left.append("%s: %s" % (name, r["frames"]["error"]))
        elif not r.get("front"):
            left.append("%s: the game's window was not in front" % name)
        elif r.get("other_load", 0) > BUSY:
            left.append("%s: the Mac was busy with something else (a load of %.1f beside the game)" % (name, r["other_load"]))
        else:
            runs.setdefault(r["manifest"]["edition"], []).append(r)
            if r["manifest"].get("preflight_failed"):
                left.append("%s: kept, but run with %s" % (name, "; ".join(r["manifest"]["preflight_failed"])))
    return runs, left


def values(results, get):
    return [v for v in (get(r) for r in results) if v is not None]


def cell(vals, digits):
    if not vals:
        return ""
    lo, hi = min(vals), max(vals)
    fmt = "%%.%df" % digits
    return fmt % lo if fmt % lo == fmt % hi else "%s-%s" % (fmt % lo, fmt % hi)


def change(vals, base, digits, floor):
    """" (+1.2)" or " (within noise)": the means' difference against the floor and the two sets' own spread."""
    if not vals or not base or floor is None:
        return ""
    d = sum(vals) / len(vals) - sum(base) / len(base)
    noise = max(floor, max(vals) - min(vals), max(base) - min(base), abs(sum(base) / len(base)) * 0.02)
    return " (within noise)" if abs(d) <= noise else " (%+.*f)" % (digits, d)


def differences(runs):
    """What the runs did not share: every run should have had the same settings, switches and mods' names."""
    out = []
    all_runs = [r for rs in runs.values() for r in rs]
    def distinct(get):
        seen = []
        for r in all_runs:
            v = get(r["manifest"])
            if v not in seen:
                seen.append(v)
        return seen
    keys = None  # the keys every run has: [Audio] has Windows-only ones
    for r in all_runs:
        k = {(s, key) for s, v in r["manifest"]["ini"].items() for key in v}
        keys = k if keys is None else keys & k
    for s, key in sorted(keys or ()):
        if key == "Fullscreen":
            continue  # full screen is 2 in the Mac edition's file and 1 in Learn & Play's
        vs = distinct(lambda m: m["ini"][s][key])
        if len(vs) > 1:
            out.append("Rocksmith.ini [%s] %s: %s" % (s, key, ", ".join(vs)))
    env_keys = sorted({k for r in all_runs for k in r["manifest"]["env"]})
    for k in env_keys:
        if k.startswith("LP_") or k in ("RS_STEAM_MODE", "RS_NATIVE_HOME", "RS_STEAM_ID"):
            continue  # an edition's own way to its files
        vs = distinct(lambda m: m["env"].get(k, "(not set)"))
        if all(v.startswith("/") for v in vs):
            continue  # each edition's own folder
        if len(vs) > 1:
            out.append("%s: %s" % (k, ", ".join(vs)))
    mods = distinct(lambda m: sorted(n for n in m["mods"] if n.endswith(".dylib")))
    if len(mods) > 1:
        out.append("mods: " + " | ".join(", ".join(m) or "none" for m in mods))
    for label, get in (("the mods' settings", lambda m: m.get("mods_settings")), ("display", lambda m: [(d["name"], d["resolution"]) for d in m["displays"] if d["main"]]),
                       ("song", lambda m: m["song"]), ("window", lambda m: (m["settle"], m["window"])),
                       ("macOS", lambda m: m["macos"])):
        vs = distinct(get)
        if len(vs) > 1:
            out.append("%s: %s" % (label, " | ".join(str(v) for v in vs)))
    for name, rs in sorted(runs.items()):
        for label, get in (("commit", lambda m: m["commit"]), ("executable", lambda m: m["exe"])):
            vs = []
            for r in rs:
                if get(r["manifest"]) not in vs:
                    vs.append(get(r["manifest"]))
            if len(vs) > 1:
                out.append("%s, %s: %s" % (NAMES.get(name, name), label, ", ".join(vs)))
    return out


def table(runs, base=None):
    eds = sorted(runs, key=lambda n: n != "mac")
    lines = ["| Song | " + " | ".join("%s (%d run%s)" % (NAMES.get(e, e), len(runs[e]), "s" * (len(runs[e]) != 1)) for e in eds) + " |",
             "|---|" + "---|" * len(eds)]
    for label, get, digits, floor in ROWS:
        cells = []
        for e in eds:
            v = values(runs[e], get)
            cells.append(cell(v, digits) + (change(v, values(base.get(e, []), get), digits, floor) if base else ""))
        if any(cells):
            lines.append("| %s | %s |" % (label, " | ".join(cells)))
    return lines


def threads(runs):
    lines = []
    for e in sorted(runs, key=lambda n: n != "mac"):
        r = runs[e][-1]
        top = ["%s %.1f" % (n, p) for n, p in r.get("threads", []) if p >= 1.0]
        lines.append("%s, threads over 1%% of a core (last run): %s" % (NAMES.get(e, e), ", ".join(top)))
    return lines


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("out")
    ap.add_argument("--against", help="another OUT: the change from its runs")
    a = ap.parse_args()
    runs, left = load(a.out)
    if not runs:
        print("no measured run in %s" % a.out, file=sys.stderr)
        for line in left:
            print("  " + line, file=sys.stderr)
        return 1
    base = load(a.against)[0] if a.against else None
    m = next(iter(runs.values()))[0]["manifest"]
    print("%s, %d s from %d s after the note highway came up; %s, macOS %s, %s" % (
        m["song"], m["window"], m["settle"], m["cpu"], m["macos"],
        "; ".join("%s %s" % (d["name"], d["resolution"]) for d in m["displays"] if d["main"])))
    print()
    print("\n".join(table(runs, base)))
    print()
    print("\n".join(threads(runs)))
    diff = differences(runs)
    if diff:
        print("\nThe runs differ in:")
        print("\n".join("- " + d for d in diff))
    if left:
        print("\nLeft out or to note:")
        print("\n".join("- " + line for line in left))
    return 0


if __name__ == "__main__":
    sys.exit(main())
