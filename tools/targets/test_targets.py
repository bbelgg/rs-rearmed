#!/usr/bin/env python3
"""Tests of tools/targets: the committed tables pass check.py, and check.py catches what it should.

usage: python3 tools/targets/test_targets.py   (also run by ctest: targets_check)
"""
import os
import shutil
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import check  # noqa: E402
import targets  # noqa: E402


class Repo(unittest.TestCase):
    def test_committed_tables_pass(self):
        self.assertEqual(check.check(), [])

    def test_gen_mac(self):
        d = tempfile.mkdtemp()
        try:
            targets.gen("mac-x64", d)
            h = open(os.path.join(d, "rc_target_names.h")).read()
            self.assertIn('#define RC_TARGET_ID "mac-x64"', h)
            self.assertIn("#define RC_ADDR_game_frame 0x10017a020ull", h)
            self.assertIn("#define RC_FN_game_frame F_10017a020", h)
            hooks = open(os.path.join(d, "skip", "hooks.skip")).read().split("\n")
            self.assertIn("10017a020 wrap", hooks)
            self.assertEqual(len([x for x in hooks if x.endswith(" wrap")]), len(targets.name_lists()["hooks"]))
            c = open(os.path.join(d, "rc_target.c")).read()
            self.assertIn('{"game_frame", 0x10017a020ull, 0x10017a020ull},', c)
        finally:
            shutil.rmtree(d)


class Synthetic(unittest.TestCase):
    """A scratch tree: names/ plus a second target that lacks one name."""

    def setUp(self):
        self.base = tempfile.mkdtemp()
        os.makedirs(os.path.join(self.base, "names", "overrides"))
        self.write("names/overrides/m.list", "f_one  # a function\nf_two\n")
        self.write("names/hooks.list", "frame\n")
        self.write("names/refs.list", "glob\n")
        self.write("names/offsets.list", "fld\n")
        for t in ("mac-x64", "win-x86"):
            self.write(t + "/target.toml", 'id = "%s"\nfront_end = "x"\nabi = "x"\n' % t)
            self.write(t + "/offsets.map", "fld 0x10\n")
        self.write("mac-x64/symbols.map", "f_one 0x1000 sysv void(void)\nf_two 0x1010 sysv -\nframe 0x1020 sysv -\n"
                                          "glob 0x2000 data int\n")

    def tearDown(self):
        shutil.rmtree(self.base)

    def write(self, rel, text):
        os.makedirs(os.path.dirname(os.path.join(self.base, rel)), exist_ok=True)
        with open(os.path.join(self.base, rel), "w") as f:
            f.write(text)

    def errs(self):
        return check.check(self.base, scan_sources=False)

    def test_missing_name_fails(self):
        self.write("win-x86/symbols.map", "f_one 0x401000 cdecl -\nframe 0x401020 cdecl -\nglob 0x500000 data int\n")
        e = self.errs()
        self.assertEqual(len(e), 1, e)
        self.assertIn("target win-x86: no symbol f_two", e[0])

    def test_unsupported_passes(self):
        self.write("win-x86/symbols.map", "f_one 0x401000 cdecl -\nf_two unsupported no such function\n"
                                          "frame 0x401020 thiscall -\nglob 0x500000 data int\n")
        self.assertEqual(self.errs(), [])
        t = targets.Target("win-x86", self.base)
        self.assertEqual(t.unsupported["f_two"], "no such function")
        skip = targets.gen_skip(t, targets.name_lists(self.base))
        self.assertEqual(skip["m"].split("\n")[1:], ["401000", ""])  # f_two is left out
        self.assertIn("401020 wrap", skip["hooks"])

    def test_missing_offset_and_stale_and_data_override(self):
        self.write("win-x86/offsets.map", "")
        self.write("win-x86/symbols.map", "f_one 0x401000 data -\nf_two 0x401010 cdecl -\nframe 0x401020 cdecl -\n"
                                          "glob 0x500000 data int\nextra 0x600000 data int\n")
        e = "\n".join(self.errs())
        self.assertIn("no offset fld", e)
        self.assertIn("symbols.map has extra", e)
        self.assertIn("f_one is overridden or hooked but is not a function", e)

    def test_duplicate_and_bad_map(self):
        self.write("names/refs.list", "glob\nglob\n")
        self.write("win-x86/symbols.map", "f_one 0x401000 cdecl -\nf_two 0x401010 cdecl -\nframe 0x401020 cdecl -\n"
                                          "glob 0x500000 data int\n")
        self.assertIn("names/refs.list: glob listed twice", "\n".join(self.errs()))
        self.write("win-x86/symbols.map", "f_one 0x401000 pascal -\n")
        self.assertIn("expected NAME ADDRESS CONVENTION", "\n".join(self.errs()))


class Sources(unittest.TestCase):
    def test_scanner_finds_uses(self):
        syms, offs, hooks = check.source_uses()
        self.assertIn("game_bootstrap", syms)          # the seam's G()
        self.assertIn("path_intern", syms)             # OV_GUEST
        self.assertIn("lua_gettop", syms)              # RC_FN
        self.assertIn("efd_inc_ref_slot", offs)        # RC_SLOT
        self.assertIn("service_manager_quit", offs)    # RC_OFF
        self.assertNotIn("name", syms)                 # macro parameters are not uses


if __name__ == "__main__":
    unittest.main()
