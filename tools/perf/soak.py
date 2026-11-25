#!/usr/bin/env python3
"""A long run in Nonstop Play, sampled: for leaks, and for what changes over time.

  tools/perf/soak.py CONFIG OUT EDITION MINUTES guitar|bass

CONFIG is run.py's. To the main menu by run.py's route, the path (bass: Path/Input, Bass Guitar), Nonstop Play
with Tuning at ANY TUNING and Arrangements at its last value (LEAD + RHYTHM + ALT, or BASS + ALT BASS), Song List
ALL SONGS and Play Order SHUFFLE as a new profile has them, then START. Every tuner is skipped and a mission's
dialog accepted. From the first note highway on, for MINUTES: a sample every 5 s (CPU, footprint, RSS, GPU,
threads, the screen), every minute the open files (the song archives by name), the thermal limit and the
system's swap; a vmmap summary and a footprint breakdown at 1 minute and every 10; `leaks` at 1 minute and at the
end. OUT/EDITION has game.log, frames.txt, samples.json, vmmap-*.txt, footprint-*.txt, leaks*.txt and a snapshot
every 30 s; tools/perf/soak_report.py reads it.

  SOAK_ENV="MallocStackLogging=1"   more for the game's environment: leaks then has the allocations' stacks
  SOAK_TOOLS=heap,mh                also `heap` (the blocks by class) and, with the stacks, `malloc_history
                                    -allBySize` (the live blocks by stack) at the same moments

The tools stop the game for a second or two each (`leaks` for five): the report leaves those frames out. The run
takes the screen, and the game pauses when another app takes the focus."""
import json
import os
import re
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import proc  # noqa: E402
import run  # noqa: E402


def main(argv):
    if len(argv) != 6 or argv[5] not in ("guitar", "bass"):
        print(__doc__, file=sys.stderr)
        return 64
    cfg = run.load_config(argv[1])
    out, name, minutes, path = os.path.abspath(argv[2]), argv[3], float(argv[4]), argv[5]
    os.makedirs(out, exist_ok=True)
    cfg.setdefault("keyfifo", run.build_keyfifo(out))
    bad, ds = run.preflight(cfg)
    print("preflight:", bad, flush=True)
    d = os.path.join(out, name)
    if os.path.exists(d):  # (a run's samples.json is the one file soak_report.py needs: never written over)
        print("soak: %s exists already" % d, file=sys.stderr)
        return 1
    g = run.Game(cfg, name, d, 30)
    CTRL = "lpctrl" if name == "lp" else "ctrl"
    GAME, TUNER, MAIN, PAUSE = "NonStopPlay_Game", "NonStopPlay_PreSongTuner", "NonStopPlay_Main", "NonStopPlay_Pause"
    HUB = "NonStopPlay_Hub"
    events, samples = [], []
    T0 = [None]


    def now():
        return round(time.time() - (T0[0] or g.t_start), 1)


    def note(what):
        events.append({"t": now(), "unix": time.time(), "what": what})
        print("  %7.1f  %s" % (now(), what), flush=True)


    def keys(seq, wait=2.0):
        for k in seq.split():
            k, _, w = k.partition(":")
            g.say("key " + k)
            time.sleep(float(w or wait))


    def screen():
        s = g.screens()
        return s[-1] if s else None


    def tool(args, file, timeout=600):
        t = time.time()
        try:
            r = subprocess.run(args, capture_output=True, text=True, timeout=timeout)
            text = r.stdout + r.stderr
        except subprocess.TimeoutExpired:
            text = "(timed out after %d s)\n" % timeout
        with open(os.path.join(d, file), "w") as f:
            f.write(text)
        note("%s took %.1f s" % (file, time.time() - t))
        return text


    def extra_tools(tag):
        """With SOAK_TOOLS=heap,mh: the heap's blocks by class, and (MallocStackLogging) the live blocks by stack."""
        want = os.environ.get("SOAK_TOOLS", "").split(",")
        if "heap" in want:
            tool(["heap", str(g.p.pid)], "heap-%s.txt" % tag, 300)
        if "mh" in want:
            tool(["malloc_history", str(g.p.pid), "-allBySize"], "mh-%s.txt" % tag, 600)


    def start_nonstop():
        """From the main menu to the first tuner or highway."""
        keys("down down down", 2)
        g.say("key return")
        g.wait_screen(MAIN, key=None, seconds=40)
        time.sleep(8)  # the set list is built
        keys("down down down down", 2)      # TUNING
        keys("left:10 left:10", 2)          # ANY TUNING (the set list is built again)
        keys("down down", 2)                # ARRANGEMENTS
        keys("right:10 right:10 right:10 right:10 right:10", 2)
        time.sleep(10)
        keys("up up up up up up", 2)        # START NONSTOP PLAY
        time.sleep(3)
        note("settings done; starting")
        g.say("key return")


    summary = {}
    try:
        g.prepare(ds)
        for kv in os.environ.get("SOAK_ENV", "").split():  # more for the game's environment: MallocStackLogging=1
            g.env[kv.partition("=")[0]] = kv.partition("=")[2]
        g.start()
        note("started pid %d" % g.p.pid)
        g.to_highway(until="MainMenu")
        t = time.time()
        while "archives mounted" not in g.log() and time.time() - t < 120:
            time.sleep(0.5)
        m = re.findall(r"\[rs-mods\] DLC: .*", g.log())
        note("; ".join(m) or "no DLC line in 120 s")
        time.sleep(8)
        if path == "bass":
            g.say("key " + CTRL)
            time.sleep(6)
            keys("right right right", 3)  # Lead, Rhythm, Bass Guitar: the last
            time.sleep(2)
            g.wait_screen("MainMenu", key="esc", every=5)
            time.sleep(5)
        start_nonstop()

        pid = g.p.pid
        last_screen, since, tuner_tries, lines_seen = None, time.time(), 0, 0
        next_sample = next_minute = next_front = next_map = 0.0
        maps_done = 0
        u_start = th_start = None
        while True:
            if not g.alive():
                note("THE GAME ENDED")
                break
            text = g.log()
            lines = text.split("\n")
            for ln in lines[lines_seen:-1]:
                low = ln.lower()
                if ("menu: " in ln or "song: " in ln or "[rs-mods] DLC" in ln or "mod loaded" in ln or "not loaded" in ln
                        or "error" in low or "fail" in low or "abort" in low or "assert" in low or "warning" in low):
                    note("log: " + ln[:160])
            lines_seen = max(len(lines) - 1, 0)
            cur = screen()
            if cur != last_screen:
                last_screen, since, tuner_tries = cur, time.time(), 0
            if cur == GAME and T0[0] is None:
                T0[0] = time.time()
                u_start, th_start = proc.usage(pid), proc.threads(pid)
                for e in events:
                    e["t"] = round(e["unix"] - T0[0], 1)
                note("first note highway: the %g minutes start" % minutes)
                next_map = 60.0
            idle = time.time() - since
            if cur == TUNER and idle > 6 + 12 * tuner_tries and tuner_tries < 6:
                g.say("key fwddelete")
                time.sleep(3)
                g.say("key return")
                tuner_tries += 1
            elif cur == PAUSE and idle > 5:
                note("paused: Esc to resume")
                g.say("key esc")
                since = time.time()
            elif cur == MAIN and T0[0] is not None and idle > 20:
                note("back on the Nonstop Play page: starting again")
                g.say("key return")
                since = time.time()
            elif cur == MAIN and T0[0] is None and idle > 60:
                note("START did nothing in 60 s: Return again")
                g.say("key return")
                since = time.time()
            elif cur == HUB and idle > 25:
                note("on the set list for %d s: Return" % idle)
                g.say("key return")
                since = time.time()
            elif cur == "MissionInformationDialog" and idle > 8:  # "MISSION COMPLETE!", between two songs: Accept
                note("a mission's dialog: Return")
                g.say("key return")
                since = time.time()
            elif cur not in (GAME, TUNER, PAUSE, MAIN, HUB) and idle > 45:
                note("on %s for %d s" % (cur, idle))
                since = time.time()
            if T0[0] is None:
                if time.time() - g.t_start > 600:
                    raise run.Stop("no note highway in 10 minutes (on %s)" % cur)
                time.sleep(0.5)
                continue
            t = time.time() - T0[0]
            if t >= minutes * 60:
                break
            if t >= next_front:
                g.say()  # in front
                next_front = t + 30
            if t >= next_sample:
                u = proc.usage(pid)
                if u is None:
                    continue
                helpers = [proc.usage(p) for h in cfg["helpers"] for p in proc.pids_named(h)]
                helpers = [h for h in helpers if h]
                row = {"t": round(t, 1), "unix": time.time(), "cpu": round(u["cpu"], 3), "footprint": u["footprint"],
                       "rss": u["rss"], "instructions": u["instructions"], "gpu": proc.gpu_percent(),
                       "threads": len(proc.threads(pid)), "front": proc.frontmost_pid() == pid, "screen": cur,
                       "helper_cpu": round(sum(h["cpu"] for h in helpers), 3),
                       "helper_footprint": sum(h["footprint"] for h in helpers), "load": os.getloadavg()[0]}
                if t >= next_minute:
                    r = subprocess.run(["lsof", "-p", str(pid)], capture_output=True, text=True)
                    row["fds"] = max(len(r.stdout.splitlines()) - 1, 0)
                    row["fds_psarc"] = sum(1 for ln in r.stdout.splitlines() if ".psarc" in ln)
                    row["psarc_names"] = sorted(os.path.basename(ln.split(None, 8)[-1]) for ln in r.stdout.splitlines()
                                                if ".psarc" in ln)
                    row["fd_kinds"] = {}
                    for ln in r.stdout.splitlines()[1:]:
                        p = ln.split(None, 8)
                        if len(p) > 8:
                            k = p[4] + (" " + os.path.splitext(p[8])[1] if p[4] == "REG" else "")
                            row["fd_kinds"][k] = row["fd_kinds"].get(k, 0) + 1
                    th = subprocess.run(["pmset", "-g", "therm"], capture_output=True, text=True).stdout
                    lim = re.search(r"CPU_Speed_Limit\s*=\s*(\d+)", th)
                    row["cpu_speed_limit"] = int(lim.group(1)) if lim else None
                    sw = subprocess.run(["sysctl", "-n", "vm.swapusage"], capture_output=True, text=True).stdout
                    used = re.search(r"used = ([\d.]+)M", sw)
                    row["swap_used_mb"] = float(used.group(1)) if used else None
                    next_minute = t + 60
                samples.append(row)
                next_sample = t + 5
            if t >= next_map:
                tag = "%02d" % round(t / 60)
                tool(["vmmap", "--summary", str(pid)], "vmmap-%s.txt" % tag, 180)
                tool(["footprint", str(pid)], "footprint-%s.txt" % tag, 180)
                if maps_done == 0:
                    tool(["leaks", str(pid)], "leaks-01.txt", 900)
                extra_tools(tag)
                maps_done += 1
                next_map = 600.0 * maps_done if maps_done * 600.0 < minutes * 60 - 30 else 1e18
            time.sleep(0.5)

        if g.alive() and T0[0] is not None:
            u_end, th_end = proc.usage(pid), proc.threads(pid)
            dt = time.time() - T0[0]
            summary = {"seconds": round(dt, 1), "cpu_pct": round((u_end["cpu"] - u_start["cpu"]) / dt * 100, 2),
                       "threads_cpu": proc.thread_cpu(th_start, th_end, dt)[:16],
                       "threads_start": len(th_start), "threads_end": len(th_end)}
            tool(["vmmap", "--summary", str(pid)], "vmmap-end.txt", 180)
            tool(["footprint", str(pid)], "footprint-end.txt", 180)
            tool(["leaks", str(pid)], "leaks.txt", 900)
            extra_tools("end")
        else:
            summary = {"ended_early": True}
    except run.Stop as err:
        summary = {"error": str(err)}
        note("STOPPED: %s" % err)
    finally:
        if os.path.isdir(d):  # (a refused start makes no folder: its message stands)
            with open(os.path.join(d, "samples.json"), "w") as f:
                json.dump({"summary": summary, "samples": samples, "events": events,
                           "t0_unix": T0[0], "edition": name, "path": path, "minutes": minutes}, f, indent=1)
        g.quit()
        print("done:", d, flush=True)
    return 1 if "error" in summary or summary.get("ended_early") else 0



if __name__ == "__main__":
    sys.exit(main(sys.argv))
