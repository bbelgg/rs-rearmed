#!/usr/bin/env python3
"""Run the test suites a release needs (scope.py says which), several at a time, and write what happened.

  tools/release/check.py                      the suites of the changes since the newest v* tag
  tools/release/check.py --since v1.12.0
  tools/release/check.py --all                every suite
  tools/release/check.py hooks lp.audio       these suites
  tools/release/check.py --list               the suites and their commands

One line per suite: pass, skip (a suite that says it skipped: a game file or an emulator that is not here) or
FAIL. Each suite's output goes to OUT/<suite>.log, and OUT/checks.json has the results, which bump.py puts
into the release commit's "Checked for the release" paragraph. OUT is $RS_RELEASE_OUT, else
build/release under $RS_BUILD_DIR or the repository.

The CTest suites need the root CMake build ($RS_CMAKE_DIR, else $RS_BUILD_DIR, else build/), which is brought up
to date first; they are left out, and said so, when it has not been configured. CTest runs one test at a time:
opengl.metal.pbo_bounds can fail beside others. The Python is $RS_PY, else the build's venv, else python3; the
recompiler's cases skip without Unicorn.

Nothing here needs the game, and nothing here runs it: don't start this during a game run, as it builds.
"""
import argparse
import concurrent.futures
import json
import os
import re
import shlex
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import scope  # noqa: E402

ROOT = scope.ROOT


def build_dir():
    return os.environ.get("RS_BUILD_DIR") or os.path.join(ROOT, "build")


def out_dir():
    return os.environ.get("RS_RELEASE_OUT") or os.path.join(build_dir(), "release")


def cmake_dir():
    return os.environ.get("RS_CMAKE_DIR") or build_dir()


def python():
    if os.environ.get("RS_PY"):
        return os.environ["RS_PY"]
    venv = os.path.join(build_dir(), "venv", "bin", "python")
    return venv if os.access(venv, os.X_OK) else sys.executable


def tool(name):
    """cmake and ctest from the build's venv when it has them (docs/mac/INSTALL.md, no Homebrew)."""
    venv = os.path.join(build_dir(), "venv", "bin", name)
    return venv if os.access(venv, os.X_OK) else name


SKIPPED = re.compile(r"^(?:OK \(skipped=\d+\)|.*\bSKIP(?:PED)?\b.*|.*\bskipped:.*|.*\*\*\*Skipped.*)$", re.M)


def run_suite(name, out):
    what, kind, arg = scope.SUITES[name]
    scratch = os.path.join(out, "suites", name)
    os.makedirs(scratch, exist_ok=True)
    q = shlex.quote
    if kind == "ctest":
        cmd = "%s --test-dir %s --output-on-failure -R %s" % (q(tool("ctest")), q(cmake_dir()), q(arg))
    else:
        cmd = arg.format(py=q(python()), out=q(scratch), cmake=q(cmake_dir()))
    log = os.path.join(out, name + ".log")
    t0 = time.time()
    with open(log, "w") as f:
        f.write("$ %s\n\n" % cmd)
        f.flush()
        rc = subprocess.run(["/bin/bash", "-c", cmd], cwd=ROOT, stdout=f, stderr=subprocess.STDOUT).returncode
    text = open(log, errors="replace").read()
    result = {"suite": name, "what": what, "seconds": round(time.time() - t0, 1), "log": log}
    if rc != 0:
        result["status"] = "fail"
    elif kind == "ctest" and "No tests were found" in text:
        result.update(status="skip", note="no such tests in the CMake build")
    else:
        result["status"] = "pass"
        skips = SKIPPED.findall(text)
        if skips:
            result["note"] = "%d skipped inside" % len(skips)
        m = re.search(r"\d+% tests passed(?:, (\d+) tests failed)? out of (\d+)", text)
        if m:
            result["note"] = "%d of %s tests" % (int(m.group(2)) - int(m.group(1) or 0), m.group(2)) + (
                ", " + result["note"] if "note" in result else "")
    return result


def line(r):
    mark = {"pass": "pass", "skip": "skip", "fail": "FAIL"}[r["status"]]
    note = "  (%s)" % r["note"] if r.get("note") else ""
    return "%-4s  %-14s %6.1fs  %s%s" % (mark, r["suite"], r["seconds"], r["what"], note)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("suites", nargs="*")
    ap.add_argument("--since")
    ap.add_argument("--all", action="store_true")
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--jobs", type=int, default=4, help="suites at a time (default 4)")
    ap.add_argument("--during-run", action="store_true", help="run though a game is running")
    a = ap.parse_args()

    if a.list:
        for n, (what, kind, arg) in scope.SUITES.items():
            print("%-14s %s\n%14s   %s %s" % (n, what, "", "ctest -R" if kind == "ctest" else "$", arg))
        return 0
    since = None
    if a.suites:
        bad = [s for s in a.suites if s not in scope.SUITES]
        if bad:
            sys.exit("error: no suite named %s (--list shows them)" % ", ".join(bad))
        names = [s for s in scope.SUITES if s in a.suites]
    elif a.all:
        names = list(scope.SUITES)
    else:
        since = a.since or scope.last_tag()
        if since is None:
            sys.exit("error: no v* tag here; say --since, --all or the suites")
        s = scope.scope(scope.changed(since))
        if s["unknown"]:
            print("no rule for %d files, so every suite runs: %s" % (len(s["unknown"]), ", ".join(s["unknown"][:5])))
        names = s["suites"]

    # No builds during a game run (AGENTS.md): the suites compile, and a busy Mac changes what the run shows.
    games = subprocess.run(["pgrep", "-x", "Rocksmith2014|rs2014_lp"], capture_output=True, text=True).stdout.split()
    if games and not a.during_run:
        sys.exit("error: a game is running (pid %s): wait for it, or say --during-run" % ", ".join(games))

    out = out_dir()
    os.makedirs(out, exist_ok=True)
    results = []

    # The CMake build once, before anything that reads it.
    need_cmake = [n for n in names if scope.SUITES[n][1] == "ctest" or "{cmake}" in scope.SUITES[n][2]]
    if need_cmake:
        if not os.path.exists(os.path.join(cmake_dir(), "build.ninja")):
            print("no CMake build at %s (docs/mac/INSTALL.md, build by hand): its suites are left out" % cmake_dir())
            for n in need_cmake:
                results.append({"suite": n, "what": scope.SUITES[n][0], "status": "skip", "seconds": 0.0,
                                "note": "no CMake build"})
            names = [n for n in names if n not in need_cmake]
        else:
            log = os.path.join(out, "cmake-build.log")
            print("building %s ..." % cmake_dir(), flush=True)
            with open(log, "w") as f:
                rc = subprocess.run([tool("cmake"), "--build", cmake_dir()], cwd=ROOT, stdout=f,
                                    stderr=subprocess.STDOUT).returncode
            if rc != 0:
                print("FAIL  the CMake build: %s" % log)
                for n in need_cmake:
                    results.append({"suite": n, "what": scope.SUITES[n][0], "status": "fail", "seconds": 0.0,
                                    "note": "the CMake build failed", "log": log})
                names = [n for n in names if n not in need_cmake]

    for r in results:
        print(line(r))
    # CTest suites share one build folder and its Testing/ files: one after another, while the rest run beside.
    ctests = [n for n in names if scope.SUITES[n][1] == "ctest"]
    others = [n for n in names if n not in ctests]

    def chain():
        return [run_suite(n, out) for n in ctests]

    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, a.jobs)) as pool:
        futures = [pool.submit(run_suite, n, out) for n in others]
        ct = pool.submit(chain) if ctests else None
        for f in concurrent.futures.as_completed(futures):
            results.append(f.result())
            print(line(f.result()), flush=True)
        for r in (ct.result() if ct else []):
            results.append(r)
            print(line(r), flush=True)

    order = list(scope.SUITES)
    results.sort(key=lambda r: order.index(r["suite"]))
    head = subprocess.run(["git", "-C", ROOT, "rev-parse", "HEAD"], capture_output=True, text=True).stdout.strip()
    dirty = bool(subprocess.run(["git", "-C", ROOT, "status", "--porcelain"], capture_output=True,
                                text=True).stdout.strip())
    with open(os.path.join(out, "checks.json"), "w") as f:
        json.dump({"since": since, "commit": head, "uncommitted": dirty,
                   "when": time.strftime("%Y-%m-%d %H:%M UTC", time.gmtime()), "results": results}, f, indent=2)
    n = {k: sum(r["status"] == k for r in results) for k in ("pass", "skip", "fail")}
    print("\n%d passed, %d skipped, %d failed. Logs and checks.json: %s" % (n["pass"], n["skip"], n["fail"], out))
    return 1 if n["fail"] else 0


if __name__ == "__main__":
    sys.exit(main())
