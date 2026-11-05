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


    def test_gen_win(self):
        d = tempfile.mkdtemp()
        try:
            targets.gen("win-x86", d)
            c = open(os.path.join(d, "rc_target.c")).read()
            self.assertIn("""{"steam_dlc_owned", 0x1448b10ull, 0x100a07800ull, 0, 3, 'b', 4, 2, {{'p', -2, 0}, {'i', 0, 0}}, 0, 0},""", c)
            self.assertIn("""{"frontend_open_file", 0x101ba80ull, 0x1003c7810ull, 0, 5, 'p', -1, 2, {{'p', 0, 0}, {'p', -2, 0}}, 0, 0},""", c)
            self.assertIn('{"boot_phase", 72},', c)
            # a name in common with the Mac is the Mac's call, adapted or refused
            self.assertIn("""{"game_frame", 0x1479df0ull, 0x10017a020ull, 0, 3, 'e', 0, 1, {{'x', -2, 0}}, 32, 7},""", c)
            self.assertIn("""{"rr_speed_ratio", 0xf89360ull, 0x1002ddd70ull, 0, 1, 'r', 0, 1, {{'f', 0, 0}}, 0, 0},""", c)
            self.assertRegex(c, r'\{"si_open", 0x17c50a0ull, 0x10133bee0ull, "[^"]+", 5, ')
        finally:
            shutil.rmtree(d)


class HookAbi(unittest.TestCase):
    def abi(self, conv, proto, mac=None):
        t = targets.Target.__new__(targets.Target)
        t.id, t.symbols, t.offsets = "t", {"f": (0x1000, conv, proto)}, {"fld": 0x20}
        m = None
        if mac:
            m = targets.Target.__new__(targets.Target)
            m.id, m.symbols = "mac-x64", {"f": (0x2000, "sysv", mac)}
        return targets.hook_abi(t, "f", m)

    def test_same_name_same_call(self):
        self.assertIsNotNone(self.abi("thiscall", "bool(void *a, int b)", "bool(void *a, int b)")["args"])
        # an id or a size is pointer-sized: a 64-bit one on the Mac is the same to a mod
        self.assertIsNotNone(self.abi("cdecl", "int(uint32_t a, ...)", "int(uint64_t a, ...)")["args"])

    def test_other_call_is_refused(self):
        for proto, mac in (("void(void *a)", "void(void *a, int b)"), ("int(void *a)", "bool(void *a)"),
                           ("void(float a)", "void(int a)"), ("void(void *a)", "bool(void)"),
                           ("void(void *a, ...)", "void(void *a, int b)")):
            d = self.abi("cdecl", proto, mac)
            self.assertIsNone(d["args"], proto)
            self.assertIn(mac, d["why"])

    def test_adapted_is_the_mac_call(self):
        d = self.abi("thiscall", "bool<fld=7>(void *m=hidden)", "bool(void)")
        self.assertEqual(d["args"], [("x", ~1, 0)])
        self.assertEqual((d["ret"], d["ret_off"], d["ret_is"], d["pops"]), ("e", 0x20, 7, 0))
        d = self.abi("cdecl", "float<100/x>(float p)", "float(float p)")
        self.assertEqual((d["ret"], d["args"]), ("r", [("f", 0, 0)]))
        self.assertIsNotNone(self.abi("stdcall", "void(void *m, int e=0x98)", "void(void *m, int e)")["args"])

    def test_sysv_needs_nothing(self):
        self.assertIsNone(self.abi("sysv", "int(int)"))

    def test_stack(self):
        d = self.abi("stdcall", "uint64_t(const char *s, uint64_t q, double v, bool b)")
        self.assertEqual(d["args"], [("p", 0, 0), ("q", 1, 0), ("d", 3, 0), ("b", 5, 0)])
        self.assertEqual((d["ret"], d["pops"]), ("q", 24))

    def test_this_and_fast(self):
        self.assertEqual(self.abi("thiscall", "void(void *me, int a)")["args"], [("p", ~1, 0), ("i", 0, 0)])
        d = self.abi("fastcall", "float(int a, double d, int b, int c)")
        self.assertEqual(d["args"], [("i", ~1, 0), ("d", 0, 0), ("i", ~2, 0), ("i", 2, 0)])
        self.assertEqual((d["ret"], d["pops"]), ("f", 12))

    def test_custom_says_where(self):
        d = self.abi("custom", "void **(void **out, const char *path@ecx, int n@edi)")
        self.assertEqual(d["args"], [("p", 0, 0), ("p", ~1, 0), ("i", ~7, 0)])
        self.assertEqual(d["pops"], -1)  # its code says

    def test_folded_argument(self):
        d = self.abi("stdcall", "void(void *menu, int event=0x98, int arg=0)")
        self.assertEqual(d["args"], [("p", 0, 0), ("c", 0, 0x98), ("c", 0, 0)])
        self.assertEqual(d["pops"], 4)

    def test_custom_silent_is_refused(self):
        self.assertIsNone(self.abi("custom", "int(void *a, int b)")["args"])
        self.assertIsNone(self.abi("custom", "-")["args"])

    def test_variadic_stdcall_has_no_size(self):
        self.assertEqual(self.abi("stdcall", "int(int a, ...)")["pops"], -1)
        self.assertEqual(self.abi("cdecl", "int(int a, ...)")["pops"], 0)

    def test_unknown_type_fails(self):
        with self.assertRaises(targets.TargetError):
            self.abi("cdecl", "NiPoint3(int)")
        with self.assertRaises(targets.TargetError):
            self.abi("cdecl", "int(int a@ecx)")
        with self.assertRaises(targets.TargetError):
            self.abi("custom", "int(int a@xmm0)")


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
