#!/usr/bin/env python3
"""Tests of the measuring tools' arithmetic: frame times, the settings file, the report. No game."""
import os
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import report  # noqa: E402
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
