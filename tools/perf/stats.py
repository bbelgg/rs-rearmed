"""The numbers of a measured run, from what the run wrote down: frame times (RS_GL_FRAMETIMES, either edition),
CPU and memory samples, and the settings file the run pinned. No game and no Mac needed: tools/perf/test_perf.py.

A frame-time file (shims/opengl/src/nsgl_hook.m) has a line per event, times in ns since the library loaded:
  F t0 dur x tid flags   a present (flags bit 0: a snapshot was taken inside it)
  G t0 dur x tid flags   the Mac edition's game frame, from the lock to the game's return
  P t0 dur x tid flags   Metal: the time a frame went on the screen (flags bit 0: it never did)
  U t0 dur x tid flags   Metal: a command buffer on the GPU, from its start to its end
  W now unix_ns          the wall clock, at every write-out
  X lost N               events the writer could not keep
"""


def parse_frametimes(lines):
    """-> {"F": [(t0, dur, flags)], "G": [...], "P": [...], "U": [...], "wall": unix_ns - t0 of the same moment
    or None, "lost": N}"""
    out = {"F": [], "G": [], "P": [], "U": [], "wall": None, "lost": 0}
    for line in lines:
        p = line.split()
        if not p:
            continue
        try:
            if p[0] in ("F", "G", "P", "U") and len(p) >= 3:
                out[p[0]].append((int(p[1]), int(p[2]), int(p[5]) if len(p) > 5 else 0))
            elif p[0] == "W" and len(p) >= 3 and out["wall"] is None:
                out["wall"] = int(p[2]) - int(p[1])
            elif p[0] == "X" and len(p) >= 3:
                out["lost"] += int(p[2])
        except ValueError:
            continue  # a line cut short by the game's end
    return out


def percentile(sorted_values, q):
    """The value at fraction q (0-1) of a sorted list, between neighbours."""
    if not sorted_values:
        return None
    k = (len(sorted_values) - 1) * q
    lo = int(k)
    hi = min(lo + 1, len(sorted_values) - 1)
    return sorted_values[lo] + (sorted_values[hi] - sorted_values[lo]) * (k - lo)


def frame_stats(ft, start_unix, end_unix):
    """Frame intervals between presents whose time is in [start_unix, end_unix) seconds, in ms."""
    if ft["wall"] is None:
        return {"error": "no wall clock line in the frame times"}
    a = start_unix * 1e9 - ft["wall"]
    b = end_unix * 1e9 - ft["wall"]
    t = sorted(e[0] for e in ft["F"] if a <= e[0] < b)
    if len(t) < 3:
        return {"error": "%d presents in the window" % len(t)}
    iv = sorted((y - x) / 1e6 for x, y in zip(t, t[1:]))
    game = sorted(e[1] / 1e6 for e in ft["G"] if a <= e[0] < b)
    span = (t[-1] - t[0]) / 1e9
    return dict(shown_stats(ft, a, b), **{
        "frames": len(iv),
        "seconds": round(span, 3),
        "fps": round(len(iv) / span, 2),
        "median_ms": round(percentile(iv, 0.5), 2),
        "p95_ms": round(percentile(iv, 0.95), 2),
        "p99_ms": round(percentile(iv, 0.99), 2),
        "worst_ms": round(iv[-1], 2),
        "over_20": sum(1 for v in iv if v > 20),
        "over_33": sum(1 for v in iv if v > 33),
        "over_50": sum(1 for v in iv if v > 50),
        "snapshots": sum(1 for e in ft["F"] if a <= e[0] < b and e[2] & 1),
        "game_frame_ms": round(percentile(game, 0.5), 2) if game else None,
        "lost": ft["lost"],
    })


def shown_stats(ft, a, b):
    """What the screen got, from Metal's own times: the intervals between frames going on the screen, the
    refreshes that passed without a new one, and the GPU's time a frame. {} without those events."""
    shown = sorted(e[0] for e in ft["P"] if a <= e[0] < b and not e[2] & 1)
    out = {}
    if len(shown) >= 3:
        iv = sorted((y - x) / 1e6 for x, y in zip(shown, shown[1:]))
        period = percentile(iv, 0.5)
        out.update(shown_median_ms=round(period, 2), shown_p99_ms=round(percentile(iv, 0.99), 2),
                   shown_worst_ms=round(iv[-1], 2),
                   # a frame that stayed for more than one refresh, and how many refreshes went without one
                   shown_late=sum(1 for v in iv if v > period * 1.5),
                   shown_missed=sum(int(round(v / period)) - 1 for v in iv if v > period * 1.5),
                   never_shown=sum(1 for e in ft["P"] if a <= e[0] < b and e[2] & 1))
    gpu = [e for e in ft["U"] if a <= e[0] < b]
    if gpu and len(shown) >= 3:
        busy = sum(e[1] for e in gpu) / 1e6
        out.update(gpu_ms=round(busy / len(shown), 2),
                   gpu_last_ms=round(percentile(sorted(e[1] / 1e6 for e in gpu if e[2] & 1), 0.5) or 0, 2),
                   gpu_buffers=round(len(gpu) / len(shown), 2))
    return out


def spread(values):
    """min, max and mean of the samples that are numbers."""
    v = [x for x in values if x is not None]
    if not v:
        return None
    return {"min": min(v), "max": max(v), "mean": round(sum(v) / len(v), 3)}


def slope(points):
    """Least-squares slope of (x, y) points, y per x: memory per second over a window."""
    n = len(points)
    if n < 2:
        return None
    mx = sum(p[0] for p in points) / n
    my = sum(p[1] for p in points) / n
    d = sum((p[0] - mx) ** 2 for p in points)
    return sum((p[0] - mx) * (p[1] - my) for p in points) / d if d else None


def pin_ini(text, pins):
    """Rocksmith.ini with the keys of pins set: {"Renderer": {key: value}, "Audio": {...}}. A name matches the
    section it starts ("Renderer" is [Renderer.MacOS] and [Renderer.Win32]); a key the section lacks is added, a
    section the file lacks is not (its name is the edition's). Line ends stay as they were.
    -> (text, [names that matched no section])"""
    nl = "\r\n" if "\r\n" in text else "\n"
    lines = text.replace("\r\n", "\n").split("\n")
    while lines and lines[-1] == "":
        lines.pop()
    out, todo, matched = [], {}, set()

    def flush():
        for k, v in todo.items():
            out.append("%s=%s" % (k, v))
        todo.clear()

    for line in lines:
        s = line.strip()
        if s.startswith("[") and s.endswith("]"):
            flush()
            for name, keys in pins.items():
                if s[1:-1].startswith(name):
                    todo.update((k, v) for k, v in keys.items())
                    matched.add(name)
            out.append(line)
            continue
        k = s.split("=", 1)[0]
        if "=" in s and k in todo:
            out.append("%s=%s" % (k, todo.pop(k)))
        else:
            out.append(line)
    flush()
    return nl.join(out) + nl, sorted(set(pins) - matched)


def ini_values(text, names=("Renderer", "Audio")):
    """{"Renderer": {key: value}, ...} of the sections that start with one of names: what a run's settings were."""
    out, cur = {}, None
    for line in text.replace("\r\n", "\n").split("\n"):
        s = line.strip()
        if s.startswith("[") and s.endswith("]"):
            cur = next((n for n in names if s[1:-1].startswith(n)), None)
            if cur:
                out.setdefault(cur, {})
        elif cur and "=" in s:
            k, v = s.split("=", 1)
            out[cur][k] = v
    return out


def cpu_per_frame_ms(cpu_pct, fps):
    """CPU time a frame, from % of a core and the frame rate."""
    return round(cpu_pct * 10.0 / fps, 2) if cpu_pct is not None and fps else None
