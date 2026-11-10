#!/usr/bin/env python3
"""Measured runs of either edition, the same way every time: the same settings, profile, mods and song, the same
keys, and the numbers over the same part of the song.

  tools/perf/run.py CONFIG OUT                    one warm-up and three measured runs of each edition, in turn
  tools/perf/run.py CONFIG OUT --editions mac --rounds 1 --no-warmup
  tools/perf/run.py CONFIG OUT --route-only       to the note highway and out, with snapshots: to check a route
  tools/perf/run.py CONFIG OUT --profile-keys bench   once: a profile folder with the settings of PROFILE_KEYS
  tools/perf/report.py OUT                        the table

CONFIG is a JSON file of this Mac's test installs (tools/perf/example.json says what goes in it); OUT gets a
folder a run (mac-1, lp-1, ...) with result.json, the game's log and the frame times. Test installs only: a run
writes Rocksmith.ini and plays on a copy of the profile, and refuses the installed game's folder.

What a run does (docs/mac/TESTING.md, Measured runs):
  - checks the Mac is in a state worth measuring: mains power, no Low Power Mode, no thermal limit, a low load,
    no game or build running, the built-in screen as the main display;
  - pins [Renderer] and [Audio] of the edition's Rocksmith.ini, copies the profile folder, and starts the game's
    executable with a clean environment, the mods, the network guard and tools/ws8/keyfifo;
  - goes from the title to the song by the mods' `menu:` lines (rearmed-mods logs them with RS_MODS_LOG=1), and
    stops on a screen it doesn't expect;
  - waits `settle` seconds on the note highway, then measures for `window` seconds: when frames were handed over
    and when they went on the screen, CPU of the game and of each thread, CPU of the helper processes, memory, GPU;
  - quits the game and writes result.json.
"""
import argparse
import errno
import hashlib
import json
import os
import re
import shutil
import signal
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
import proc   # noqa: E402
import stats  # noqa: E402

GAMES = ("Rocksmith2014", "rs2014_lp")
INSTALLED = os.path.expanduser("~/Library/Application Support/RS Re-ARMed")

# What every run pins unless CONFIG says otherwise. The screen's size comes from the display.
INI_PINS = {"Renderer": {"Fullscreen": "1", "RenderingWidth": "0", "RenderingHeight": "0", "EnablePostEffects": "1",
                         "EnableShadows": "1", "EnableHighResScope": "1", "EnableDepthOfField": "1",
                         "EnablePerPixelLighting": "1", "MsaaSamples": "4", "VisualQuality": "2"},
            "Audio": {"LatencyBuffer": "1", "EnableMicrophone": "0"}}
# Full screen is 2 in the Mac edition's file (it reads 1 as 2) and 1 in Learn & Play's; VisualQuality 2 is High.
EDITION_INI = {"mac": {"Renderer": {"Fullscreen": "2"}}, "lp": {"Renderer": {"Fullscreen": "1"}}}
ENV_PINS = {"RS_METAL_MSAA": "4", "RS_METAL_ANISO": "16", "RS_METAL_UPSCALE": "1", "RS_MODS_LOG": "1"}
DEFAULTS = {"song": "CherubRock", "search": "cher", "settle": 45, "window": 150, "sample": 2, "max_load": 3.0,
            "helpers": [], "env": {}, "ini": {}}

# The keys of each screen on the way, by the mods' name for it; seconds to wait first (menus drop keys while
# they move in). The song list has its own step.
ROUTE = {"TitleScreen": (10, ["return"]), "ProfileSelect": (4, ["return"]), "SelectionListDialog": (4, ["return"]),
         "MainMenu": (6, ["return"]), "MissionMenu": (4, ["return"]), "LearnASong_SongOptions": (4, ["return"]),
         "LearnASong_PreSongTuner": (6, ["fwddelete", "return"])}
PASSING = ("MainOverlay",)  # logged on the way, nothing to press
# --profile-keys bench, from a profile on the game's defaults: INPUT to DIRECT CONNECT (its last value; Right past
# it does nothing), and Tools, Options, Adv Display Settings, Venue Mode on (Return opens the setting, Return ticks
# it: from Off only). Tools, Difficulty Settings has Override to Max, and Play Settings has Master Mode, the same
# way; look at the run's snapshots for what a profile has.
PROFILE_KEYS = ("ctrl down right right right right, "
                "space down down down return:5 down down down return:6 down down down return:5 return:5")
SONGS, HIGHWAY = "LearnASong_Songs", "LearnASong_Game"
SEARCH_KEY = {"mac": "/", "lp": "lpshift"}


class Stop(Exception):
    pass


def sh(*cmd):
    return subprocess.run(cmd, capture_output=True, text=True).stdout


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def displays():
    """[{"name", "builtin", "main", "resolution"}] from system_profiler."""
    out = []
    try:
        data = json.loads(sh("system_profiler", "SPDisplaysDataType", "-json"))
    except ValueError:
        return out
    for gpu in data.get("SPDisplaysDataType", []):
        for d in gpu.get("spdisplays_ndrvs", []):
            out.append({"name": d.get("_name", ""),
                        "builtin": d.get("spdisplays_connection_type") == "spdisplays_internal",
                        "main": d.get("spdisplays_main") == "spdisplays_yes",
                        "resolution": d.get("_spdisplays_resolution", ""),
                        "pixels": d.get("_spdisplays_pixels", "")})
    return out


def preflight(cfg):
    """-> (what is wrong, the displays). Nothing wrong: an empty list."""
    bad = []
    if sh("pgrep", "-x", "|".join(GAMES)).strip() or any(proc.pids_named(g) for g in GAMES):
        bad.append("a game is running")
    if sh("pgrep", "-x", "ninja|clang|cmake|xcodebuild").strip():
        bad.append("a build is running")
    if "AC Power" not in sh("pmset", "-g", "batt"):
        bad.append("on battery")
    if re.search(r"lowpowermode\s+1", sh("pmset", "-g")):
        bad.append("Low Power Mode is on")
    m = re.search(r"CPU_Speed_Limit\s*=\s*(\d+)", sh("pmset", "-g", "therm"))
    if m and int(m.group(1)) < 100:
        bad.append("the CPU is limited to %s%% (heat)" % m.group(1))
    if os.getloadavg()[0] > cfg["max_load"]:
        bad.append("load %.1f, over %.1f" % (os.getloadavg()[0], cfg["max_load"]))
    ds = displays()
    main = next((d for d in ds if d["main"]), None)
    if not main or not main["builtin"]:
        bad.append("the main display is not the built-in screen")
    return bad, ds


def wait_calm(cfg, seconds=300):
    """Between runs: until the load is low and no heat limit, at most `seconds`."""
    end = time.time() + seconds
    while time.time() < end:
        bad, _ = preflight(cfg)
        if not [b for b in bad if b.startswith("load") or "heat" in b]:
            return
        time.sleep(10)


def screen_points(ds):
    """The built-in screen's size in points, (w, h), or None."""
    for d in ds:
        m = re.match(r"(\d+) x (\d+)", d["resolution"])
        if d["builtin"] and m:
            return int(m.group(1)), int(m.group(2))
    return None


def manifest(cfg, ed, name, ds, bad):
    e = cfg["editions"][name]
    mods = {}  # a folder, or one library
    if os.path.isfile(e.get("mods", "")):
        mods[os.path.basename(e["mods"])] = sha256(e["mods"])[:16]
    for base, _, files in os.walk(e.get("mods", "")):
        for f in sorted(files):
            p = os.path.join(base, f)
            mods[os.path.relpath(p, e["mods"])] = sha256(p)[:16]
    git = sh("git", "-C", ROOT, "rev-parse", "--short", "HEAD").strip()
    if sh("git", "-C", ROOT, "status", "--porcelain", "--untracked-files=no").strip():
        git += "+changes"
    return {"edition": name, "version": open(os.path.join(ROOT, "VERSION")).readline().strip(), "commit": git,
            "exe": sha256(e["exe"])[:16], "mods": mods, "mods_settings": ed["mods_settings"], "env": ed["env_given"], "ini": ed["ini_values"],
            "macos": sh("sw_vers", "-productVersion").strip(), "cpu": sh("sysctl", "-n", "machdep.cpu.brand_string").strip(),
            "memory_gb": round(int(sh("sysctl", "-n", "hw.memsize") or 0) / 2 ** 30), "displays": ds,
            "preflight_failed": bad, "song": cfg["song"], "settle": cfg["settle"], "window": cfg["window"]}


class Game:
    def __init__(self, cfg, name, out, snapshots):
        self.cfg, self.name, self.out, self.e = cfg, name, out, cfg["editions"][name]
        self.log_path = os.path.join(out, "game.log")
        self.fifo = os.path.join(out, "keys")
        self.p = None
        self.snapshots = snapshots

    def prepare(self, ds):
        e, out = self.e, self.out
        for k in ("exe", "ini"):
            if os.path.realpath(e[k]).startswith(os.path.realpath(INSTALLED) + os.sep):
                raise Stop("%s is the installed game's: runs are for test installs" % e[k])
        os.makedirs(out)
        os.mkfifo(self.fifo)
        shutil.copytree(self.cfg["profile"], os.path.join(out, "steam"), symlinks=True)
        for base, _, files in os.walk(os.path.join(out, "steam")):  # stored credentials never reach the game
            for f in files:
                if f == "crd":
                    os.remove(os.path.join(base, f))
        pins = {s: dict(v) for s, v in INI_PINS.items()}
        for s, v in EDITION_INI.get(self.name, {}).items():
            pins[s].update(v)
        size = screen_points(ds)
        if size:
            pins["Renderer"].update(ScreenWidth=str(size[0]), ScreenHeight=str(size[1]))
        for ini in (self.cfg["ini"], e.get("ini_pins", {})):
            for s, v in ini.items():
                pins.setdefault(s, {}).update({k: str(x) for k, x in v.items()})
        with open(e["ini"], newline="") as f:
            text, missing = stats.pin_ini(f.read(), pins)
        if missing:
            raise Stop("%s has no [%s] section: start the game once in this test install" % (e["ini"], missing[0]))
        with open(e["ini"], "w", newline="") as f:
            f.write(text)
        if self.cfg.get("mods_settings") and e.get("mods_settings"):  # the same switches of the mods for both
            if os.path.realpath(self.cfg["mods_settings"]) != os.path.realpath(e["mods_settings"]):
                shutil.copyfile(self.cfg["mods_settings"], e["mods_settings"])
        given = dict(ENV_PINS)
        given.update(self.cfg["env"])
        given.update(e.get("env", {}))
        env = {k: os.environ[k] for k in ("HOME", "USER", "LOGNAME", "PATH", "TMPDIR", "LANG") if k in os.environ}
        env.update(given)
        insert = list(e.get("insert", [])) + [self.cfg["keyfifo"]]
        env.update(RS_STEAM_DATA_DIR=os.path.join(out, "steam"), RS_KEY_FIFO=self.fifo,
                   RS_GL_FRAMETIMES=os.path.join(out, "frames.txt"), RS_NETGUARD_LOG=os.path.join(out, "netguard.log"),
                   DYLD_INSERT_LIBRARIES=":".join(insert))
        if e.get("mods"):
            env["RS_MODS_PATH"] = e["mods"]
        if self.cfg.get("guitar_input"):
            env["RS_AUDIO_INPUT_FILE"] = self.cfg["guitar_input"]
        if self.snapshots:
            os.makedirs(os.path.join(out, "snap"))
            env.update(RS_GL_SNAPSHOT_DIR=os.path.join(out, "snap"), RS_GL_SNAPSHOT_SECS=str(self.snapshots))
        for k in ("RS_NETGUARD", "RS_STEAM_KEEP_CREDENTIALS"):  # never weakened here (AGENTS.md, Offline)
            if k in env:
                raise Stop("%s is set: not in a run of this tool" % k)
        self.env = env
        settings = sha256(e["mods_settings"])[:16] if e.get("mods_settings") else None
        return {"env_given": given, "ini_values": stats.ini_values(text), "mods_settings": settings}

    def start(self):
        log = open(self.log_path, "wb")
        self.t_start = time.time()
        self.p = subprocess.Popen([self.e["exe"]] + self.e.get("args", []), cwd=self.e.get("cwd") or None,
                                  env=self.env, stdout=log, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL)
        log.close()

    def alive(self):
        return self.p.poll() is None

    def log(self):
        try:
            with open(self.log_path, errors="replace") as f:
                return f.read()
        except OSError:
            return ""

    def say(self, *cmds):
        """Lines for keyfifo; `front` first, since the game takes keys only in front."""
        if not self.alive():
            raise Stop("the game ended")
        try:
            fd = os.open(self.fifo, os.O_WRONLY | os.O_NONBLOCK)
        except OSError as err:
            if err.errno == errno.ENXIO:
                raise Stop("keyfifo is not reading: is it in the game? (no `keyfifo:` line in the log)")
            raise
        with os.fdopen(fd, "w") as f:
            f.write("".join(c + "\n" for c in ("front",) + cmds))
        print("  %6.1f s  %s" % (time.time() - self.t_start, ", ".join(cmds)), flush=True)

    def screens(self):
        return re.findall(r"menu: (\w+)", self.log())

    def to_highway(self, timeout=420, until=HIGHWAY):
        """From the start to the note highway (or the screen `until`). -> {screen: seconds after the start it
        was first seen}"""
        seen, last, sent, tries, searched = {}, None, 0.0, 0, False
        guard = self.e.get("netguard", self.name == "mac")
        while time.time() - self.t_start < timeout:
            if not self.alive():
                raise Stop("the game ended (%s)" % self.log_path)
            if guard:  # no key before the guard says it blocks
                try:
                    with open(self.env["RS_NETGUARD_LOG"]) as f:
                        guard = "mode=block" not in f.read()
                except OSError:
                    pass
                if guard and time.time() - self.t_start > 30:
                    raise Stop("the network guard did not say mode=block in 30 s")
                time.sleep(0.5)
                continue
            if "mods not loaded" in self.log():
                raise Stop("the mods were not loaded (%s)" % self.log_path)
            s = self.screens()
            cur = s[-1] if s else None
            if cur and cur not in seen:
                seen[cur] = round(time.time() - self.t_start, 1)
            if cur == until:
                return seen
            if cur and cur not in ROUTE and cur not in PASSING and cur != SONGS:
                raise Stop("a screen the route doesn't know: %s" % cur)
            if cur == SONGS and not searched:
                time.sleep(6)
                self.pick_song()
                searched, last, sent = True, cur, time.time()
            elif cur in ROUTE and (cur != last or time.time() - sent > 12):
                tries = tries + 1 if cur == last else 0
                if tries > 4:
                    raise Stop("stuck on %s" % cur)
                wait, keys = ROUTE[cur]
                time.sleep(wait)
                if (self.screens() or [None])[-1] != cur:
                    continue  # it moved on by itself
                for k in keys:
                    self.say("key " + k)
                    time.sleep(3)
                last, sent = cur, time.time()
            elif cur == SONGS and time.time() - sent > 40:
                raise Stop("the song list did not go on to the song")
            time.sleep(0.5)
        raise Stop("no note highway in %d s" % timeout)

    def pick_song(self):
        want = self.cfg["song"]
        self.say("key " + SEARCH_KEY[self.name])
        time.sleep(2)
        self.say("text " + self.cfg["search"])
        time.sleep(1.6 * len(self.cfg["search"]) + 6)
        for _ in range(15):
            songs = re.findall(r"song: (\w+)", self.log())
            if songs and songs[-1] == want:
                self.say("key return")
                return
            self.say("key down")
            time.sleep(2)
        raise Stop("%s is not among the first rows of the search for \"%s\"" % (want, self.cfg["search"]))

    def wait_screen(self, name, key=None, seconds=90, every=8):
        """Until the last screen is `name`, pressing `key` every few seconds if given (the main menu takes no key
        while the song archives are still being listed)."""
        end, sent = time.time() + seconds, 0.0
        while (self.screens() or [None])[-1] != name:
            if time.time() > end:
                raise Stop("waited for %s, still on %s" % (name, (self.screens() or [None])[-1]))
            if key and time.time() - sent > every:
                self.say("key " + key)
                sent = time.time()
            time.sleep(0.5)

    def edit_profile(self, sequences):
        """From the main menu: each sequence of keys ("key" or "key:seconds to wait after it"), back to the main
        menu after each, then out to the title, which saves the profile. The first key of a sequence is pressed
        until the screen changes."""
        for seq in sequences:
            keys = [k.partition(":") for k in seq.split()]
            lp = {"ctrl": "lpctrl", "shift": "lpshift"} if self.name == "lp" else {}
            time.sleep(6)
            n, end, first = len(self.screens()), time.time() + 90, lp.get(keys[0][0], keys[0][0])
            while len(self.screens()) == n:
                if time.time() > end:
                    raise Stop("\"%s\" did nothing on the main menu" % first)
                self.say("key " + first)
                time.sleep(8)
            for key, _, wait in keys[1:]:
                self.say("key " + lp.get(key, key))
                time.sleep(float(wait or 3))
            time.sleep(4)  # a snapshot of the result
            self.wait_screen("MainMenu", key="esc", every=5)
        time.sleep(4)
        self.wait_screen("SelectionListDialog", key="esc")
        time.sleep(4)
        self.wait_screen("TitleScreen", key="return")
        time.sleep(5)

    def measure(self):
        """Samples over the window. -> the numbers that don't come from the frame times"""
        cfg, pid = self.cfg, self.p.pid
        self.say()  # in front
        time.sleep(1)
        front0 = proc.frontmost_pid() == pid
        t0, u0, th0 = time.time(), proc.usage(pid), proc.threads(pid)
        helpers0 = {h: [(p, proc.usage(p)) for p in proc.pids_named(h)] for h in cfg["helpers"]}
        rows = []
        while time.time() - t0 < cfg["window"]:
            time.sleep(max(0.0, min(cfg["sample"], t0 + cfg["window"] - time.time())))
            u = proc.usage(pid)
            if u is None:
                raise Stop("the game ended in the window (%s)" % self.log_path)
            rows.append({"t": round(time.time() - t0, 2), "cpu": round(u["cpu"], 4), "footprint": u["footprint"],
                         "rss": u["rss"], "gpu": proc.gpu_percent()})
        t1, u1, th1 = time.time(), proc.usage(pid), proc.threads(pid)
        if self.screens()[-1] != HIGHWAY:  # the game pauses when another app takes the focus
            raise Stop("the game left the note highway in the window (%s)" % self.screens()[-1])
        dt = t1 - t0
        helpers = {}
        for h, before in helpers0.items():
            cpu = sum((proc.usage(p) or u)["cpu"] - u["cpu"] for p, u in before if u)
            helpers[h] = {"processes": len(before), "cpu_pct": round(cpu / dt * 100, 2)}
        gb = 2.0 ** 30
        return {"start_unix": t0, "end_unix": t1,
                "front": front0 and proc.frontmost_pid() == pid,
                "cpu_pct": round((u1["cpu"] - u0["cpu"]) / dt * 100, 2),
                "threads": proc.thread_cpu(th0, th1, dt)[:12], "helpers": helpers,
                "footprint_gb": stats.spread([round(r["footprint"] / gb, 3) for r in rows]),
                "rss_gb": stats.spread([round(r["rss"] / gb, 3) for r in rows]),
                "footprint_mb_per_min": round((stats.slope([(r["t"], r["footprint"]) for r in rows]) or 0) * 60 / 2 ** 20, 2),
                "gpu_pct": stats.spread([r["gpu"] for r in rows]),
                "instructions_per_s": round((u1["instructions"] - u0["instructions"]) / dt),
                "samples": rows}

    def quit(self):
        if self.p is None or not self.alive():
            return
        try:
            self.say("quit")
            self.p.wait(15)
        except (Stop, subprocess.TimeoutExpired):
            self.p.send_signal(signal.SIGTERM)
            try:
                self.p.wait(10)
            except subprocess.TimeoutExpired:
                self.p.kill()


def one_run(cfg, name, out, kind, force, snapshots):
    """kind: "measured", "warmup" (the same run, not in the report), "route" (to the highway and out) or
    a list of key sequences (the profile's settings; the profile folder it leaves is OUT/steam)."""
    print("%s: %s (%s)" % (os.path.basename(out), name, "profile" if isinstance(kind, list) else kind), flush=True)
    bad, ds = preflight(cfg)
    if bad and not force:
        raise Stop("not a state to measure in: %s (--force runs anyway and says so in the result)" % "; ".join(bad))
    run_cfg = dict(cfg)  # a warm-up plays the whole window: what it doesn't reach is first drawn in a measured run
    g = Game(run_cfg, name, out, snapshots)
    result = {"kind": kind}
    try:
        result["manifest"] = manifest(run_cfg, g.prepare(ds), name, ds, bad)
        g.start()
        result["screens"] = g.to_highway(until="MainMenu" if isinstance(kind, list) else HIGHWAY)
        if isinstance(kind, list):
            g.edit_profile(kind)
        elif kind != "route":
            time.sleep(run_cfg["settle"])
            m = g.measure()
            result.update(m)
        else:
            time.sleep(snapshots * 2 + 2)
    except Stop as err:
        result["error"] = str(err)
    finally:
        g.quit()
    if "error" not in result and kind in ("measured", "warmup"):
        with open(os.path.join(out, "frames.txt"), errors="replace") as f:
            result["frames"] = stats.frame_stats(stats.parse_frametimes(f), result["start_unix"], result["end_unix"])
        result["cpu_per_frame_ms"] = stats.cpu_per_frame_ms(result["cpu_pct"], result["frames"].get("fps"))
    if os.path.isdir(out):
        with open(os.path.join(out, "result.json"), "w") as f:
            json.dump(result, f, indent=1)
    if "error" in result:
        raise Stop("%s: %s" % (os.path.basename(out), result["error"]))
    if kind in ("measured", "warmup"):
        fr = result["frames"]
        print("  %s fps, median %s ms, p99 %s ms, CPU %s%% (%s ms a frame), GPU %s%%, footprint %s GB%s" % (
            fr.get("fps"), fr.get("median_ms"), fr.get("p99_ms"), result["cpu_pct"], result["cpu_per_frame_ms"],
            (result["gpu_pct"] or {}).get("mean"), (result["footprint_gb"] or {}).get("max"),
            "" if result["front"] else "  NOT IN FRONT: not a result"), flush=True)
    return result


def load_config(path):
    with open(path) as f:
        cfg = json.load(f)
    for k, v in DEFAULTS.items():
        cfg.setdefault(k, v)
    for k in ("profile", "editions"):
        if k not in cfg:
            raise Stop("%s: no \"%s\"" % (path, k))
    return cfg


def build_keyfifo(out):
    lib = os.path.join(out, "libkeyfifo.dylib")
    if not os.path.exists(lib):
        subprocess.run([os.path.join(ROOT, "tools/ws8/keyfifo/build.sh"), out], check=True, stdout=subprocess.DEVNULL)
    return lib


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("config")
    ap.add_argument("out")
    ap.add_argument("--editions", nargs="+", default=None, help="mac, lp (default: all in CONFIG)")
    ap.add_argument("--rounds", type=int, default=3)
    ap.add_argument("--no-warmup", action="store_true", help="no unmeasured first play of each edition")
    ap.add_argument("--route-only", action="store_true", help="to the note highway and out, with snapshots")
    ap.add_argument("--profile-keys", metavar="KEYS",
                    help="keys from the main menu, sequences apart by a comma, or \"bench\" for PROFILE_KEYS; "
                         "OUT/<edition>-profile/steam is then a \"profile\"")
    ap.add_argument("--force", action="store_true", help="run in a state the checks refuse; the result says so")
    a = ap.parse_args()
    try:
        cfg = load_config(a.config)
        editions = a.editions or sorted(cfg["editions"], key=lambda n: n != "mac")
        for n in editions:
            if n not in cfg["editions"]:
                raise Stop("no edition \"%s\" in %s" % (n, a.config))
        os.makedirs(a.out, exist_ok=True)
        cfg.setdefault("keyfifo", build_keyfifo(os.path.abspath(a.out)))

        def run(name, label, kind, snapshots=0):
            d = os.path.join(os.path.abspath(a.out), "%s-%s" % (name, label))
            if os.path.exists(d):
                raise Stop("%s is there already: another OUT, or remove it" % d)
            wait_calm(cfg)
            return one_run(cfg, name, d, kind, a.force, snapshots)

        if a.profile_keys:
            keys = PROFILE_KEYS if a.profile_keys == "bench" else a.profile_keys
            run(editions[0], "profile", [k.strip() for k in keys.split(",")], snapshots=2)
            return 0
        if a.route_only:
            for n in editions:
                run(n, "route", "route", snapshots=5)
            return 0
        if not a.no_warmup:
            for n in editions:
                run(n, "warmup", "warmup")
        for i in range(1, a.rounds + 1):
            for n in editions:
                run(n, str(i), "measured")
    except Stop as err:
        print("stopped: %s" % err, file=sys.stderr)
        return 1
    print("report: tools/perf/report.py %s" % a.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
