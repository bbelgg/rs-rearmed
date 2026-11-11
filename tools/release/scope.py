#!/usr/bin/env python3
"""What a release has to check: the files changed since the last release, mapped to the editions they reach,
the builds they make stale, the test suites that cover them and the game runs they call for.

  tools/release/scope.py                 since the newest v* tag, to the working tree
  tools/release/scope.py --since v1.12.0
  tools/release/scope.py --all           everything, as for a change no rule knows
  tools/release/scope.py --json          for check.py and other tools

A release that changed docs and the installer needs no game build; one that changed win-x86/ needs no Mac run.
The rules below say so, one path prefix at a time; the longest prefix that matches a file decides for it. A file
no rule matches counts as everything, and is listed, so a new directory is never left unchecked by mistake:
give it a rule in the same change.

The suites are the ones of docs/mac/TESTING.md and docs/windows/TESTING.md ("Tests in the repository"). Keep
the three in step.
"""
import argparse
import json
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

MAC, LP = "mac", "lp"
BOTH = (MAC, LP)

# Suite: (what it checks, kind, command or CTest pattern). Kinds:
#   cmd    a shell command run from the repository's root. {py} is the Python to use, {out} a scratch folder of
#          the suite's own, {cmake} the CMake build folder.
#   ctest  a CTest pattern (-R) of the root CMake build, which check.py brings up to date first.
SUITES = {
    "release":       ("the release tools", "cmd", "{py} tools/release/test_release.py"),
    "perf":          ("the measuring tools", "cmd", "{py} tools/perf/test_perf.py"),
    "gamefiles":     ("the game-file copy", "cmd", "{py} tools/play-native/test_gamefiles.py"),
    "cache_overlay": ("the menu overlay", "cmd", "{py} tools/play-native/test_cache_overlay.py"),
    "settings":      ("settings.env", "cmd", "{py} tools/play-native/test_settings.py"),
    "decrypt":       ("the Mac image's decryption", "cmd", "{py} tools/steamless/test_decrypt.py"),
    "targets":       ("the per-game tables", "cmd",
                      "{py} tools/targets/check.py && {py} tools/targets/test_targets.py"),
    "hooks":         ("hook points and mods, both runtimes", "cmd",
                      "make -C tools/ws7/recomp/test/hooks test OUT={out}"),
    "inner64":       ("edits and mid points, 64-bit", "cmd",
                      "cd tools/ws7/recomp && {py} -m test.x64.innercases --out {out}"),
    "inner32":       ("edits and mid points, 32-bit", "cmd",
                      "cd tools/ws7/recomp && UC_IGNORE_REG_BREAK=1 {py} -m test.x86.innercases --out {out}"),
    "hand32":        ("the 32-bit front end's hand cases", "cmd",
                      "cd tools/ws7/recomp && UC_IGNORE_REG_BREAK=1 {py} -m test.x86.handcases --out {out}"),
    "netguard":      ("the network guard", "cmd", "tools/ws8/netguard/build.sh --test"),
    "steam":         ("the Mac Steam layer", "cmd", "make -C shims/steam check && make -C shims/steam test"),
    "lp.install":    ("Learn & Play's installer", "cmd",
                      "{py} -m unittest discover -s win-x86/install -p 'test_*.py'"),
    "lp.unpack":     ("Learn & Play's offline unpack and seed capture", "cmd",
                      "{py} -m unittest discover -s win-x86/unpack -p 'test_*.py'"),
    "lp.shaders":    ("Learn & Play's shader corpus sources", "cmd",
                      "{py} -m unittest discover -s win-x86/shaders -p 'test_*.py'"),
    "lp.shims":      ("Learn & Play's kernel32 waits and input hooks", "cmd",
                      "make -C win-x86/shims/tests test OUT={out}"),
    "lp.audio":      ("Learn & Play's audio layer", "cmd", "make -C win-x86/shims/audio test OUT={out}"),
    "lp.steam":      ("Learn & Play's Steam face", "cmd", "make -C win-x86/shims/steam test OUT={out}"),
    "lp.online":     ("Learn & Play's online layer", "cmd", "make -C win-x86/online test OUT={out}"),
    "lp.screen":     ("Learn & Play's window rules", "cmd",
                      "cc -I win-x86/app win-x86/test/lp_screen_test.c win-x86/app/lp_screen.c"
                      " -o {out}/lp_screen_test && {out}/lp_screen_test"),
    "lp.d3d9":       ("Learn & Play's D3D9 layer on both backends", "cmd",
                      "cmake --build {cmake} --target opengl && win-x86/test/d3d9_test.sh {cmake} {out}"),
    "ct.audio":      ("guitar input", "ctest", r"^audio\."),
    "ct.platform":   ("the platform shell", "ctest", r"^platform\."),
    "ct.opengl":     ("the renderer", "ctest", r"^opengl\."),
    "ct.msl":        ("the shader translator", "ctest", r"^msl\."),
    "ct.cg":         ("Cg", "ctest", r"^cg\."),
    "ct.bink":       ("Bink", "ctest", r"^bink\."),
    "ct.game":       ("the game libraries", "ctest", r"^game\."),
    "ct.shims":      ("the shims' architectures and names", "ctest", r"^(shim|libstdcxx)\."),
}

RECOMP = ("hooks", "inner64", "inner32", "hand32")


def rule(editions=(), build=(), run=(), suites=(), installer=False):
    return {"editions": tuple(editions), "build": tuple(build), "run": tuple(run), "suites": tuple(suites),
            "installer": installer}


NOTHING = rule()

# Path prefix (a directory ends in "/") -> what a change under it reaches.
#   editions   whose players can see the change
#   build      whose game has to be built again before it is run
#   run        which game is run once to the note highway and out
#   suites     the tests that cover it
#   installer  the installer app is built and looked at (it is built for the package either way)
RULES = {
    # Words only.
    "docs/": NOTHING, "README.md": NOTHING, "AGENTS.md": NOTHING, "CLAUDE.md": NOTHING,
    "CONTRIBUTING.md": NOTHING, "LICENSE": NOTHING, "THIRD_PARTY_NOTICES.md": NOTHING, "VERSION": NOTHING,
    ".github/": NOTHING, ".claude/": NOTHING, ".gitignore": NOTHING, "requirements.txt": NOTHING,
    "tools/release/": rule(suites=["release"]),
    "tools/perf/": rule(suites=["perf"]),
    "tools/ws8/keyfifo/": NOTHING,
    "tools/loadscreen/": NOTHING,

    # The installer and the install scripts.
    "tools/installer/": rule(BOTH, installer=True),
    "tools/play-native/": rule(BOTH, suites=["gamefiles", "cache_overlay", "settings"], installer=True),
    # (Learn & Play's installer gets its game from Steam with gamefiles.py's code)
    "tools/play-native/gamefiles.py": rule(BOTH, suites=["gamefiles", "lp.install"], installer=True),
    "tools/play-native/install.sh": rule([MAC], run=[MAC], installer=True),
    "tools/play-native/launch.sh": rule([MAC], run=[MAC], suites=["settings"]),
    "tools/play-native/launch-stub.c": rule([MAC], run=[MAC]),
    "tools/play-native/warm-shaders.sh": rule([MAC], run=[MAC]),
    "tools/play-native/learn-play.sh": rule([LP], run=[LP], installer=True),
    "tools/play-native/cache_overlay.py": rule(BOTH, run=BOTH, suites=["cache_overlay"]),
    "tools/psarc_extract.py": rule(BOTH, suites=["cache_overlay", "gamefiles"]),
    "tools/psarc_pack.py": rule(BOTH, suites=["cache_overlay", "gamefiles"]),
    "tools/steamless/": rule([MAC], suites=["decrypt"], installer=True),
    "tools/rs-env.sh": rule([MAC], suites=["netguard"]),
    "tools/check-shim.sh": rule([MAC], suites=["ct.shims"]),

    # Shared: the recompiler, the tables, the renderer, audio, the guard.
    "tools/ws7/": rule(BOTH, build=BOTH, run=BOTH, suites=RECOMP + ("targets",)),
    ".recomp.json": rule(BOTH, build=BOTH, run=BOTH, suites=RECOMP),
    "tools/ws7/recomp/app/": rule([MAC], build=[MAC], run=[MAC]),
    "tools/ws7/recomp/test/": rule(suites=RECOMP),
    "tools/targets/": rule(BOTH, build=BOTH, run=BOTH, suites=["targets", "hooks"]),
    "tools/targets/mac-x64/": rule([MAC], build=[MAC], run=[MAC], suites=["targets", "hooks", "inner64"]),
    "tools/targets/win-x86/": rule([LP], build=[LP], run=[LP], suites=["targets", "hooks", "inner32"]),
    "tools/metal/": rule(BOTH, run=BOTH, suites=["ct.msl", "lp.shaders"]),
    "shims/opengl/": rule(BOTH, build=BOTH, run=BOTH, suites=["ct.opengl", "ct.msl", "lp.d3d9"]),
    "shims/coreaudio/": rule(BOTH, build=BOTH, run=BOTH, suites=["ct.audio", "lp.audio"]),
    "shims/platform/": rule(BOTH, build=BOTH, run=BOTH, suites=["ct.platform", "lp.screen"]),
    "shims/include/": rule(BOTH, build=BOTH, run=BOTH, suites=["ct.shims", "ct.opengl", "ct.platform"]),
    "tools/ws8/netguard/": rule(BOTH, build=[LP], run=[MAC], suites=["netguard"]),

    # The Mac edition alone.
    "shims/": rule([MAC], build=[MAC], run=[MAC], suites=["ct.shims"]),
    "shims/steam/": rule([MAC], build=[MAC], run=[MAC], suites=["steam"]),
    "shims/cg/": rule([MAC], build=[MAC], run=[MAC], suites=["ct.cg"]),
    "shims/bink/": rule([MAC], build=[MAC], run=[MAC], suites=["ct.bink"]),
    "shims/game/": rule([MAC], build=[MAC], run=[MAC], suites=["ct.game"]),
    "shims/libstdcxx/": rule([MAC], build=[MAC], run=[MAC], suites=["ct.shims"]),
    "CMakeLists.txt": rule([MAC], build=[MAC], suites=["ct.shims"]),
    "cmake/": rule([MAC], build=[MAC], suites=["ct.shims"]),

    # Learn & Play alone.
    "win-x86/": rule([LP], build=[LP], run=[LP], suites=["lp.shims"]),
    "win-x86/install/": rule([LP], suites=["lp.install"], installer=True),
    "win-x86/unpack/": rule([LP], suites=["lp.install", "lp.unpack"], installer=True),
    "win-x86/harvest/": NOTHING,
    "win-x86/shaders/": rule([LP], run=[LP], suites=["lp.shaders"]),
    "win-x86/shims/audio/": rule([LP], build=[LP], run=[LP], suites=["lp.audio"]),
    "win-x86/shims/steam/": rule([LP], build=[LP], run=[LP], suites=["lp.steam"]),
    "win-x86/shims/d3d9/": rule([LP], build=[LP], run=[LP], suites=["lp.d3d9"]),
    "win-x86/shims/win32_d3d9.c": rule([LP], build=[LP], run=[LP], suites=["lp.d3d9"]),
    "win-x86/shims/tests/": rule(suites=["lp.shims"]),
    "win-x86/online/": rule([LP], build=[LP], run=[LP], suites=["lp.online"]),
    "win-x86/app/": rule([LP], build=[LP], run=[LP], suites=["lp.screen"]),
    "win-x86/test/": rule(suites=["lp.screen", "lp.d3d9"]),
    "win-x86/README.md": NOTHING,
}

EVERYTHING = rule(BOTH, build=BOTH, run=BOTH, suites=tuple(SUITES), installer=True)


def rule_for(path):
    """The rule of the longest prefix matching PATH, or None."""
    if path.endswith(".md"):
        return NOTHING  # words, wherever they are: a README beside code too
    best = None
    for prefix in RULES:
        hit = path.startswith(prefix) if prefix.endswith("/") else path == prefix
        if hit and (best is None or len(prefix) > len(best)):
            best = prefix
    return RULES[best] if best is not None else None


def scope(paths):
    """The union of the rules of PATHS: {editions, build, run, suites, installer, unknown, files}."""
    got = {"editions": set(), "build": set(), "run": set(), "suites": set()}
    installer, unknown = False, []
    for p in paths:
        r = rule_for(p)
        if r is None:
            unknown.append(p)
            r = EVERYTHING
        for k in got:
            got[k].update(r[k])
        installer = installer or r["installer"]
    order = [MAC, LP]
    return {
        "editions": [e for e in order if e in got["editions"]],
        "build": [e for e in order if e in got["build"]],
        "run": [e for e in order if e in got["run"]],
        "suites": [s for s in SUITES if s in got["suites"]],
        "installer": installer,
        "unknown": unknown,
        "files": len(paths),
    }


def git(*args):
    return subprocess.run(("git", "-C", ROOT) + args, check=True, capture_output=True, text=True).stdout


def last_tag():
    tags = git("tag", "--list", "v*", "--sort=-v:refname").split()
    return tags[0] if tags else None


def changed(since):
    """Files that differ between SINCE and the working tree, with the untracked ones."""
    paths = set(git("diff", "--name-only", since).split("\n"))
    paths.update(git("ls-files", "--others", "--exclude-standard").split("\n"))
    return sorted(p for p in paths if p)


NAMES = {MAC: "Mac", LP: "Learn & Play"}


def words(editions, none="neither"):
    return " and ".join(NAMES[e] for e in editions) if editions else none


def report(s, since):
    out = ["Since %s: %d files changed." % (since, s["files"])]
    if s["unknown"]:
        out.append("No rule for (counted as everything; add one to tools/release/scope.py):")
        out += ["  " + p for p in s["unknown"]]
    out.append("Editions reached:  " + words(s["editions"]))
    out.append("Game builds:       " + words(s["build"], "none"))
    out.append("Game runs:         " + words(s["run"], "none"))
    out.append("Installer:         " + ("build it and look at it" if s["installer"] else "build for the package only"))
    if s["suites"]:
        out.append("Suites (tools/release/check.py runs them):")
        out += ["  %-14s %s" % (n, SUITES[n][0]) for n in s["suites"]]
    else:
        out.append("Suites:            none")
    return "\n".join(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--since", help="a tag or commit (default: the newest v* tag)")
    ap.add_argument("--all", action="store_true", help="everything, whatever changed")
    ap.add_argument("--json", action="store_true")
    a = ap.parse_args()
    since = a.since or last_tag()
    if since is None:
        sys.exit("error: no v* tag here; say --since")
    paths = changed(since)
    s = scope(paths)
    if a.all:
        s = dict(EVERYTHING, editions=list(BOTH), build=list(BOTH), run=list(BOTH), suites=list(SUITES),
                 unknown=[], files=len(paths))
    s["since"] = since
    print(json.dumps(s, indent=2) if a.json else report(s, since))


if __name__ == "__main__":
    main()
