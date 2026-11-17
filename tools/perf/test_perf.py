#!/usr/bin/env python3
"""Tests of the measuring tools: frame times, the settings file, the report, the key channel. No game."""
import contextlib
import io
import json
import os
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import profile as hot  # noqa: E402
import report  # noqa: E402
import run     # noqa: E402
import stats   # noqa: E402


def frames(n, step_ns, start=0, wall=1000 * 10 ** 9):
    lines = ["W 0 %d" % wall]
    lines += ["F %d 1000000 0 7 0" % (start + i * step_ns) for i in range(n)]
    lines += ["G %d 4000000 0 7 0" % (start + i * step_ns) for i in range(n)]
    return lines


class FrameTimes(unittest.TestCase):
    def test_window_and_rate(self):
        ft = stats.parse_frametimes(frames(601, 16666667))
        s = stats.frame_stats(ft, 1000 + 2, 1000 + 8)
        self.assertEqual(s["fps"], 60.0)
        self.assertEqual(s["median_ms"], 16.67)
        self.assertEqual((s["over_20"], s["over_33"], s["over_50"]), (0, 0, 0))
        self.assertEqual(s["game_frame_ms"], 4.0)
        self.assertTrue(355 <= s["frames"] <= 361)

    def test_a_stall_is_counted(self):
        lines = frames(100, 10 ** 7) + ["F %d 1000000 0 7 1" % (99 * 10 ** 7 + 6 * 10 ** 7)]
        s = stats.frame_stats(stats.parse_frametimes(lines), 1000, 1002)
        self.assertEqual((s["worst_ms"], s["over_50"], s["over_33"], s["snapshots"]), (60.0, 1, 1, 1))

    def test_cut_lines_and_losses(self):
        ft = stats.parse_frametimes(["W 0 5", "F 1 2 0 7 0", "F 12", "F x y", "X lost 3", "", "W 9 99"])
        self.assertEqual((len(ft["F"]), ft["lost"], ft["wall"]), (1, 3, 5))

    def test_what_the_screen_got(self):
        lines = frames(100, 10 ** 7)
        shown = [i * 10 ** 7 for i in range(100) if i != 50]          # one refresh without a new frame
        lines += ["P %d 0 0 9 0" % t for t in shown] + ["P 500000000 0 0 9 1"]
        lines += ["U %d 3000000 0 9 1" % t for t in shown] + ["U %d 1000000 0 9 0" % t for t in shown]
        s = stats.frame_stats(stats.parse_frametimes(lines), 1000, 1001)
        self.assertEqual((s["shown_median_ms"], s["shown_late"], s["shown_missed"], s["never_shown"]), (10.0, 1, 1, 1))
        self.assertEqual((s["gpu_ms"], s["gpu_last_ms"], s["gpu_buffers"]), (4.0, 3.0, 2.0))
        self.assertNotIn("gpu_ms", stats.frame_stats(stats.parse_frametimes(frames(100, 10 ** 7)), 1000, 1001))

    def test_nothing_to_measure(self):
        self.assertIn("error", stats.frame_stats(stats.parse_frametimes(["F 1 2 0 7 0"]), 0, 1))
        self.assertIn("error", stats.frame_stats(stats.parse_frametimes(["W 0 0", "F 1 2 0 7 0"]), 0, 1))

    def test_percentile_and_slope(self):
        self.assertEqual(stats.percentile([1, 2, 3, 4, 5], 0.5), 3)
        self.assertEqual(stats.percentile([1, 2], 0.5), 1.5)
        self.assertIsNone(stats.percentile([], 0.5))
        self.assertAlmostEqual(stats.slope([(0, 1), (1, 3), (2, 5)]), 2.0)
        self.assertIsNone(stats.slope([(0, 1)]))
        self.assertEqual(stats.cpu_per_frame_ms(45.0, 75.0), 6.0)


class Ini(unittest.TestCase):
    MAC = "[Audio]\r\nLatencyBuffer=2\r\n[Renderer.MacOS]\r\nFullscreen=0\r\nMsaaSamples=4\r\n[Net]\r\n"

    def test_pins_keep_the_rest(self):
        text, missing = stats.pin_ini(self.MAC, {"Renderer": {"Fullscreen": "1", "ScreenWidth": "1470"},
                                                 "Audio": {"LatencyBuffer": "4"}})
        self.assertEqual(missing, [])
        self.assertEqual(text, "[Audio]\r\nLatencyBuffer=4\r\n[Renderer.MacOS]\r\nFullscreen=1\r\nMsaaSamples=4\r\n"
                               "ScreenWidth=1470\r\n[Net]\r\n")
        self.assertEqual(stats.ini_values(text), {"Audio": {"LatencyBuffer": "4"},
                                                  "Renderer": {"Fullscreen": "1", "MsaaSamples": "4",
                                                               "ScreenWidth": "1470"}})

    def test_both_editions_sections(self):
        text, _ = stats.pin_ini("[Renderer.Win32]\nFullscreen=0\n", {"Renderer": {"Fullscreen": "1"}})
        self.assertEqual(text, "[Renderer.Win32]\nFullscreen=1\n")

    def test_a_missing_section_is_reported(self):
        text, missing = stats.pin_ini("[Net]\n", {"Renderer": {"Fullscreen": "1"}})
        self.assertEqual((text, missing), ("[Net]\n", ["Renderer"]))

    def test_twice_is_once(self):
        pins = {"Renderer": {"Fullscreen": "1", "New": "2"}}
        once, _ = stats.pin_ini(self.MAC, pins)
        self.assertEqual(stats.pin_ini(once, pins)[0], once)


TRACE = """<?xml version="1.0"?><trace-query-result><node>
<row><sample-time id="1">1000000000</sample-time><thread id="2" fmt="game"><tid id="3" fmt="0x1">1</tid></thread>
<weight id="4">1000000</weight><tagged-backtrace id="5"><frame id="6" name="leaf" addr="0x1"><binary id="7" name="game"/></frame>
<frame id="8" name="frame" addr="0x2"><binary ref="7"/></frame><frame id="9" name="thread_start" addr="0x3">
<binary id="10" name="libsystem_pthread.dylib"/></frame></tagged-backtrace></row>
<row><sample-time id="11">2000000000</sample-time><thread ref="2"/><weight ref="4"/><tagged-backtrace ref="5"/></row>
<row><sample-time id="12">3000000000</sample-time><thread ref="2"/><weight ref="4"/><tagged-backtrace id="13">
<frame id="14" name="other" addr="0x4"><binary ref="7"/></frame><frame ref="8"/><frame ref="9"/></tagged-backtrace></row>
</node></trace-query-result>"""


class Profile(unittest.TestCase):
    def test_refs_are_followed_and_times_added(self):
        import io
        seconds, threads = hot.summarise(hot.samples(io.StringIO(TRACE)))
        t = threads["0x1"]
        self.assertEqual((seconds, t["total"]), (2.0, 3000000))
        self.assertEqual(t["self"][("leaf", "game")], 2000000)
        self.assertEqual(t["incl"][("frame", "game")], 3000000)
        self.assertEqual(t["roots"].most_common(1)[0][0], ("frame", "game"))  # not the system's thread start
        self.assertEqual(hot.under(t, "frame")[1], 3000000)
        self.assertEqual(hot.under(t, "leaf")[0][("leaf", "game")], 2000000)


class FakeGame:
    """A process for run.Game: running until told to stop, and what it was sent."""
    def __init__(self, heeds_term=True):
        self.signals, self.killed, self.heeds_term, self.ended = [], False, heeds_term, False

    def poll(self):
        return 0 if self.ended else None

    def wait(self, timeout=None):
        if not self.ended:
            raise run.subprocess.TimeoutExpired("game", timeout)
        return 0

    def send_signal(self, sig):
        self.signals.append(sig)
        self.ended = self.heeds_term

    def kill(self):
        self.killed = self.ended = True


class Keys(unittest.TestCase):
    """The key channel: what can't be sent stops the run, and the game is ended all the same."""
    def game(self, fifo, p):
        g = run.Game.__new__(run.Game)
        g.fifo, g.p, g.t_start = fifo, p, 0.0
        return g

    def test_keys_reach_a_reader(self):
        import tempfile
        with tempfile.TemporaryDirectory() as d:
            fifo = os.path.join(d, "keys")
            os.mkfifo(fifo)
            rd = os.open(fifo, os.O_RDONLY | os.O_NONBLOCK)
            try:
                with contextlib.redirect_stdout(io.StringIO()):
                    self.game(fifo, FakeGame()).say("key return")
                self.assertEqual(os.read(rd, 100), b"front\nkey return\n")
            finally:
                os.close(rd)

    def test_no_reader_and_no_folder_stop_the_run(self):
        import tempfile
        with tempfile.TemporaryDirectory() as d:
            fifo = os.path.join(d, "keys")
            os.mkfifo(fifo)
            with self.assertRaisesRegex(run.Stop, "keyfifo is not reading"):
                self.game(fifo, FakeGame()).say("key return")
            gone = os.path.join(d, "unmounted", "keys")  # the run's folder went away under the game
            with self.assertRaisesRegex(run.Stop, "can't be sent"):
                self.game(gone, FakeGame()).say("key return")
        ended = FakeGame()
        ended.ended = True
        with self.assertRaisesRegex(run.Stop, "the game ended"):
            self.game(gone, ended).say("key return")

    def test_quit_ends_a_game_it_can_not_tell(self):
        import signal
        p = FakeGame()
        self.game("/nonexistent/keys", p).quit()  # no key channel: the signal, not an error
        self.assertEqual((p.signals, p.killed), ([signal.SIGTERM], False))
        p = FakeGame(heeds_term=False)
        self.game("/nonexistent/keys", p).quit()
        self.assertEqual((p.signals, p.killed), ([signal.SIGTERM], True))


class StayedUp(unittest.TestCase):
    """A run that reached the highway counts only if the game is still up and on it (--route-only is a release's
    game run, and a game that ended keeps the highway as its log's last screen)."""
    def game(self, d, p, screens):
        g = run.Game.__new__(run.Game)
        g.p, g.log_path = p, os.path.join(d, "game.log")
        with open(g.log_path, "w") as f:
            f.write("".join("menu: %s\n" % s for s in screens))
        return g

    def test_up_and_on_the_highway(self):
        import tempfile
        with tempfile.TemporaryDirectory() as d:
            self.game(d, FakeGame(), ["MainMenu", run.HIGHWAY]).on_highway("it played")
            ended = FakeGame()
            ended.ended = True
            with self.assertRaisesRegex(run.Stop, "the game ended while it played"):
                self.game(d, ended, ["MainMenu", run.HIGHWAY]).on_highway("it played")
            with self.assertRaisesRegex(run.Stop, "left the note highway while it was traced \\(MainMenu\\)"):
                self.game(d, FakeGame(), [run.HIGHWAY, "MainMenu"]).on_highway("it was traced")


VMMAP = """REGION TYPE                        SIZE    SIZE    SIZE    SIZE    SIZE    SIZE    SIZE    COUNT
===========                     ======= ========   =====  ======= ======== =========  =======  =======
IOAccelerator (graphics)         %(gfx)s  %(gfx)s  %(gfx)s      0K      0K      0K      0K       10
Malloc Small                     %(small)s  %(small)s  %(small)s      0K      0K      0K      0K       20
===========                     ======= ========   =====  ======= ======== =========  =======  =======
TOTAL                              1.0G    1.0G    1.0G      0K      0K      0K      0K       30

                                 VIRTUAL RESIDENT    DIRTY  SWAPPED ALLOCATION      BYTES DIRTY+SWAP          REGION
MALLOC ZONE                         SIZE     SIZE     SIZE     SIZE      COUNT  ALLOCATED  FRAG SIZE  %% FRAG   COUNT
===========                      =======  =======  =======  =======  =========  =========  =========  ======  ======
DefaultMallocZone_0x100000000     200.0M   200.0M   200.0M       0K   %(blocks)s    %(small)s      10.0M     10%%      20
"""


class SoakReport(unittest.TestCase):
    """soak_report.py on a run made up here: a heap that grows by blocks and a footprint that climbs."""
    def test_a_growing_run_reads_as_one(self):
        import tempfile
        import soak_report
        with tempfile.TemporaryDirectory() as d:
            t0 = 1000.0
            samples = [{"t": 5.0 * i, "unix": t0 + 5.0 * i, "cpu": 2.5 * i, "footprint": (1400 + i) * 2 ** 20,
                        "rss": 1100 * 2 ** 20, "instructions": 0, "gpu": 88, "threads": 42, "front": True,
                        "screen": "NonStopPlay_Game" if i % 30 else "NonStopPlay_Hub", "helper_cpu": 0.0,
                        "helper_footprint": 0, "load": 1.5,
                        **({"fds": 70 + i // 12, "fds_psarc": 19, "cpu_speed_limit": None, "swap_used_mb": 0.0}
                           if i % 12 == 0 else {})} for i in range(121)]
            events = [{"t": 0.0, "unix": t0, "what": "log: [rsmods] song: One"},
                      {"t": 150.0, "unix": t0 + 150, "what": "log: [rsmods] menu: NonStopPlay_Hub"},
                      {"t": 61.0, "unix": t0 + 61, "what": "vmmap-01.txt took 1.5 s"}]
            with open(os.path.join(d, "samples.json"), "w") as f:
                json.dump({"summary": {"cpu_pct": 50.0}, "samples": samples, "events": events, "t0_unix": t0,
                           "edition": "lp", "path": "bass", "minutes": 10}, f)
            for name, v in (("01", {"gfx": "430.0M", "small": "100.0M", "blocks": "150000"}),
                            ("end", {"gfx": "490.0M", "small": "170.0M", "blocks": "380000"})):
                with open(os.path.join(d, "vmmap-%s.txt" % name), "w") as f:
                    f.write(VMMAP % v)
            with open(os.path.join(d, "leaks.txt"), "w") as f:
                f.write("Process 1: 380000 nodes malloced for 170000 KB\nProcess 1: 1 leak for 32 total leaked bytes.\n")
            ns = lambda s: int((t0 + s) * 1e9)  # noqa: E731
            lines = ["W 0 0"]
            at, k = 0.0, 0
            while at < 600:
                # a frame every 1/60 s; one of 300 ms in a song, one of 800 ms at the screen change, one in vmmap
                step = 0.3 if k == 6000 else 0.8 if abs(at - 150) < 0.01 else 1.4 if abs(at - 60) < 0.01 else 1 / 60
                lines.append("F %d 1000000 0 0 0" % ns(at))
                at, k = at + step, k + 1
            with open(os.path.join(d, "frames.txt"), "w") as f:
                f.write("\n".join(lines) + "\n")
            with open(os.path.join(d, "game.log"), "w") as f:
                f.write("[rsmods] menu: NonStopPlay_Game\nw32: SetWarningMessageHook not implemented (returns 0x0)\n")
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                self.assertEqual(soak_report.main(["soak_report.py", d]), 0)
            text = out.getvalue()
        self.assertIn("slope from minute 0: +12.00 MB/min", text)        # a MB a sample, twelve samples a minute
        self.assertRegex(text, r"IOAccelerator \(graphics\) +430\.0 +490\.0 +\+60\.0")
        self.assertRegex(text, r"DefaultMallocZone +100\.0/150000 +170\.0/380000 +\+70\.0 MB, \+230000 blocks")
        self.assertIn("1 leaks, 0.00 MB", text)
        self.assertRegex(text, r"frames over 50 ms: 3 \(.*in a song 1")   # the others: a tool's, a screen change's
        self.assertIn("open files 70..80 (first 70, last 80)", text)
        self.assertIn("SetWarningMessageHook not implemented (returns N)", text)  # numbers apart, lines are counted


def result(edition, cpu, fps=60.0, **manifest):
    m = {"edition": edition, "ini": {"Renderer": {"Fullscreen": "1", "VisualQuality": "2"}}, "env": {"RS_METAL_MSAA": "4", "RS_STEMS_CACHE": "/mac"},
         "mods": {"librsmods.dylib": "aa"}, "displays": [{"name": "Built-in", "main": True, "resolution": "1470 x 956"}],
         "song": "CherubRock", "settle": 45, "window": 150, "macos": "27.0", "commit": "abc", "exe": "ee", "cpu": "M2"}
    m.update(manifest)
    return {"kind": "measured", "front": True, "manifest": m, "cpu_pct": cpu,
            "frames": {"fps": fps, "median_ms": 16.67}, "footprint_gb": {"max": 1.0}}


class Report(unittest.TestCase):
    def test_cells(self):
        self.assertEqual(report.cell([44.12, 44.14], 1), "44.1")
        self.assertEqual(report.cell([44.1, 44.4, 44.3], 1), "44.1-44.4")
        self.assertEqual(report.cell([], 1), "")

    def test_change_needs_more_than_noise(self):
        self.assertEqual(report.change([45.0, 45.5], [44.0, 44.4], 1, 2.0), " (within noise)")
        self.assertEqual(report.change([50.0, 50.5], [44.0, 44.4], 1, 2.0), " (+6.0)")
        self.assertEqual(report.change([50.0], [44.0, 52.0], 1, 2.0), " (within noise)")  # the base's own spread
        self.assertEqual(report.change([50.0], [], 1, 2.0), "")

    def test_table_has_a_column_an_edition(self):
        runs = {"lp": [result("lp", 53.0)], "mac": [result("mac", 44.0), result("mac", 45.0)]}
        t = report.table(runs)
        self.assertEqual(t[0], "| Song | Mac (2 runs) | Learn & Play (1 run) |")
        self.assertIn("| CPU (% of a core) | 44.0-45.0 | 53.0 |", t)

    def test_a_busy_run_is_left_out(self):
        import json, tempfile
        with tempfile.TemporaryDirectory() as d:
            for name, load in (("mac-1", 0.4), ("mac-2", 4.6)):
                os.mkdir(os.path.join(d, name))
                with open(os.path.join(d, name, "result.json"), "w") as f:
                    json.dump(dict(result("mac", 44.0), other_load=load), f)
            runs, left = report.load(d)
        self.assertEqual(len(runs["mac"]), 1)
        self.assertIn("mac-2: the Mac was busy", left[0])

    def test_same_setup_has_no_differences(self):
        self.assertEqual(report.differences({"mac": [result("mac", 44.0)], "lp": [result("lp", 53.0)]}), [])

    def test_differences_are_named(self):
        runs = {"mac": [result("mac", 44.0)],
                "lp": [result("lp", 53.0, ini={"Renderer": {"Fullscreen": "0", "VisualQuality": "1", "ShowGamepadUI": "0"}},
                              env={"RS_METAL_MSAA": "1", "LP_GAME_DIR": "/x", "RS_STEMS_CACHE": "/lp"}, mods={"other.dylib": "bb"})]}
        d = report.differences(runs)
        self.assertEqual(d, ["Rocksmith.ini [Renderer] VisualQuality: 2, 1", "RS_METAL_MSAA: 4, 1",
                             "mods: librsmods.dylib | other.dylib"])

    def test_two_builds_of_one_edition(self):
        d = report.differences({"mac": [result("mac", 44.0), result("mac", 44.0, exe="ff")]})
        self.assertEqual(d, ["Mac, executable: ee, ff"])


if __name__ == "__main__":
    unittest.main()
