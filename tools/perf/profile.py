#!/usr/bin/env python3
"""Where the CPU time of a traced run went (tools/perf/run.py --trace): by thread, and in each thread by function.

  tools/perf/profile.py OUT/<edition>-trace/game.trace [--top 25] [--threads 4] [--under NAME]

It exports the trace's time-profile table with xctrace (kept beside the trace as time-profile.xml) and adds up the
samples: a thread's share of a core, the functions the samples were in ("self"), and the functions they were in
or under ("with callees"). --under NAME gives the self times of what runs under the function NAME alone.

Shares within a thread are what to read. A thread's total is not, for a thread that wakes for less than the
sampling interval at a time: the audio threads come out at about half of what the measured run's CPU says
(docs/mac/PERFORMANCE.md, Method). A sampled game is slower than a measured one, so its frame times say nothing.
"""
import argparse
import collections
import os
import subprocess
import sys
import xml.etree.ElementTree as ET

XPATH = '/trace-toc/run[@number="1"]/data/table[@schema="time-profile"]'


def export(trace):
    out = os.path.join(os.path.dirname(os.path.abspath(trace)), "time-profile.xml")
    if not os.path.exists(out):
        subprocess.run(["xcrun", "xctrace", "export", "--input", trace, "--xpath", XPATH, "--output", out],
                       check=True, stdout=subprocess.DEVNULL)
    return out


def samples(xml):
    """(time ns, thread id, weight ns, [(function, binary)] from the sample's own function outwards) for each row.
    The export writes an element once with an id and refers to it by ref after that."""
    seen = {}

    def resolve(e):
        if e is None:
            return None
        if e.get("ref") is not None:
            return seen[e.get("ref")]
        if e.get("id") is not None:
            seen[e.get("id")] = e
        return e

    def register(e):  # every element with an id under e, before it is cleared
        for x in e.iter():
            if x.get("id") is not None:
                seen[x.get("id")] = x

    frames = {}

    def frame(e):
        e = resolve(e)
        key = id(e)
        if key not in frames:
            b = resolve(e.find("binary"))
            frames[key] = (e.get("name") or e.get("addr") or "?", b.get("name") if b is not None else "?")
        return frames[key]

    for _, row in ET.iterparse(xml, events=("end",)):
        if row.tag != "row":
            continue
        register(row)
        t = resolve(row.find("sample-time"))
        th = resolve(row.find("thread"))
        w = resolve(row.find("weight"))
        bt = resolve(row.find("tagged-backtrace"))
        if t is None or th is None or bt is None:
            continue
        tid = th.find("tid")
        yield (int(t.text), tid.get("fmt") if tid is not None else th.get("fmt"), int(w.text) if w is not None else 0,
               [frame(f) for f in bt.findall("frame")])
        # the elements stay alive in `seen`; only the row's own list of children goes


def summarise(rows):
    """-> (seconds, {thread: {"total": ns, "self": Counter, "incl": Counter, "roots": Counter}})"""
    threads, t0, t1 = {}, None, None
    for t, tid, w, stack in rows:
        t0 = t if t0 is None else min(t0, t)
        t1 = t if t1 is None else max(t1, t)
        th = threads.setdefault(tid, {"total": 0, "self": collections.Counter(), "incl": collections.Counter(),
                                      "roots": collections.Counter(), "stacks": []})
        th["total"] += w
        if not stack:
            continue
        th["self"][stack[0]] += w
        for f in set(stack):
            th["incl"][f] += w
        # what the thread is: the outermost function that is not the system's thread start
        for f in reversed(stack):
            if f[1] not in ("libsystem_pthread.dylib", "dyld", "libdispatch.dylib"):
                th["roots"][f] += w
                break
        th["stacks"].append((w, stack))
    return ((t1 - t0) / 1e9 if t0 is not None and t1 > t0 else 0.0), threads


def under(th, name):
    """Self times of the samples that have the function `name` in their stack, and their sum."""
    c, total = collections.Counter(), 0
    for w, stack in th["stacks"]:
        if any(f[0] == name for f in stack):
            c[stack[0]] += w
            total += w
    return c, total


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("trace", help="a .trace, or an exported time-profile .xml")
    ap.add_argument("--top", type=int, default=25, help="functions a thread")
    ap.add_argument("--threads", type=int, default=4, help="threads to list, busiest first")
    ap.add_argument("--under", help="only what runs under this function, in the threads that have it")
    a = ap.parse_args()
    xml = a.trace if a.trace.endswith(".xml") else export(a.trace)
    seconds, threads = summarise(samples(xml))
    if not seconds:
        print("no samples in %s" % xml, file=sys.stderr)
        return 1
    order = sorted(threads, key=lambda k: -threads[k]["total"])
    whole = sum(t["total"] for t in threads.values())
    print("%.1f s, %d threads, %.1f%% of a core in all" % (seconds, len(threads), whole / seconds / 1e7))

    def line(f, w, total):
        return "  %5.1f%%  %5.2f  %s  [%s]" % (w / total * 100, w / seconds / 1e7, f[0], f[1])

    for tid in order[:a.threads]:
        th = threads[tid]
        root = th["roots"].most_common(1)[0][0][0] if th["roots"] else "?"
        if a.under:
            c, total = under(th, a.under)
            if not total:
                continue
            print("\nthread %s, under %s: %.2f%% of a core" % (tid, a.under, total / seconds / 1e7))
            for f, w in c.most_common(a.top):
                print(line(f, w, total))
            continue
        print("\nthread %s (%s): %.1f%% of a core" % (tid, root, th["total"] / seconds / 1e7))
        print("  self: of the thread, of a core")
        for f, w in th["self"].most_common(a.top):
            print(line(f, w, th["total"]))
        print("  with callees:")
        for f, w in th["incl"].most_common(a.top):
            print(line(f, w, th["total"]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
