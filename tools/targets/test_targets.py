#!/usr/bin/env python3
"""Tests of tools/targets: the committed tables pass check.py, and check.py catches what it should.

usage: python3 tools/targets/test_targets.py   (also run by ctest: targets.check)
"""
import os
import re
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
            self.assertIn("#define RC_ADDR_autosave_warning 0x1005e7e10ull", h)
            self.assertIn("#define RC_FN_autosave_warning F_1005e7e10", h)
            self.assertIn("#define RC_WFN_game_frame W_10017a020", h)   # a hook point's translated function
            self.assertFalse(os.path.exists(os.path.join(d, "skip", "win32.skip")))   # Learn & Play's module
            self.assertNotIn(" wrap", open(os.path.join(d, "skip", "mods.skip")).read())
            self.assertNotIn("RC_NFN_", h)                              # gen_image_pe.py's alone
            hooks = open(os.path.join(d, "skip", "hooks.skip")).read().split("\n")
            self.assertIn("10017a020 wrap", hooks)
            self.assertIn("1005e7e10 wrap", hooks)
            self.assertEqual(len([x for x in hooks if x.endswith(" wrap")]), len(targets.name_lists()["hooks"]))
            c = open(os.path.join(d, "rc_target.c")).read()
            self.assertIn('{"game_frame", 0x10017a020ull, 0x10017a020ull},', c)
        finally:
            shutil.rmtree(d)


    def test_gen_win(self):
        d = tempfile.mkdtemp()
        try:
            targets.gen("win-x86", d)
            h = open(os.path.join(d, "rc_target_names.h")).read()
            # what Learn & Play's built-in fixes are compiled against (win-x86/shims/win32_overrides.c)
            self.assertIn("#define RC_FN_dlc_update F_1449310", h)
            self.assertIn("#define RC_WFN_path_intern W_141c130", h)
            self.assertIn("#define RC_NFN_autosave_warning N_120adf0", h)
            self.assertIn("#define RC_ADDR_input_binding 0x1ed87b0ull", h)
            self.assertIn("#define RC_OFF_dlc_installing 0x6", h)
            # and the skip file its translation takes, of names/overrides/win32.list
            skip = open(os.path.join(d, "skip", "win32.skip")).read().split("\n")
            self.assertIn("1449310", skip)        # dlc_update: replaced
            self.assertIn("141c130 wrap", skip)   # path_intern: kept as W_ beside the native one
            self.assertIn("120adf0 wrap", skip)   # autosave_warning: a hook point too, so the native one is N_
            # boot_intro_update and input_bind: the shared pre hooks, nothing native, kept or not
            self.assertFalse([x for x in skip if x.split()[:1] in (["1126950"], ["1344640"])])
            self.assertEqual(len([x for x in skip if x and not x.startswith("#")]),
                             len(targets.name_lists()["overrides/win32"]))   # every name is a function here
            c = open(os.path.join(d, "rc_target.c")).read()
            self.assertIn("""{"steam_dlc_owned", 0x1448b10ull, 0x100a07800ull, 0, 3, 'b', 4, 2, {{'p', -2, 0}, {'i', 0, 0}}, 0, 0},""", c)
            self.assertIn("""{"frontend_open_file", 0x101ba80ull, 0x1003c7810ull, 0, 5, 'p', -1, 2, {{'p', 0, 0}, {'p', -2, 0}}, 0, 0},""", c)
            # the input in a register, and the Mac's second argument folded away: its one value
            self.assertIn("""{"input_bind", 0x1344640ull, 0x10044aaf0ull, 0, 5, 'v', -1, 2, {{'i', -1, 0}, {'c', 0, 0}}, 0, 0},""", c)
            self.assertIn('{"input_key_direct", 0x1e69558, -1},', c)   # a pointer to the key's text, as the Mac's
            self.assertIn('{"boot_phase", 72},', c)
            # a name in common with the Mac is the Mac's call, adapted or refused
            self.assertIn("""{"game_frame", 0x1479df0ull, 0x10017a020ull, 0, 3, 'e', 0, 1, {{'x', -2, 0}}, 32, 7},""", c)
            self.assertIn("""{"rr_speed_ratio", 0xf89360ull, 0x1002ddd70ull, 0, 1, 'r', 0, 1, {{'f', 0, 0}}, 0, 0},""", c)
            self.assertRegex(c, r"\{\"si_open\", 0x17c50a0ull, 0x10133bee0ull, 0, 5, 'z', ")
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
        d = self.abi("custom", "bool<zero>(void *m@edi, int s, int a=hidden)", "bool(void *m, int s)")
        self.assertEqual((d["ret"], d["args"]), ("z", [("p", ~7, 0), ("i", 0, 0), ("x", 1, 0)]))

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


class Tree:
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



class Synthetic(Tree, unittest.TestCase):
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

    def test_wrap_mark(self):
        # `NAME wrap` in an override module's list: the function stays translated beside the native one
        self.write("names/overrides/m.list", "f_one wrap  # the original for the rest\nf_two\n")
        self.write("win-x86/symbols.map", "f_one 0x401000 cdecl -\nf_two 0x401010 cdecl -\nframe 0x401020 thiscall -\n"
                                          "glob 0x500000 data int\n")
        self.write("mac-x64/target.toml", 'id = "mac-x64"\nfront_end = "x"\nabi = "sysv64"\n')
        self.write("win-x86/target.toml", 'id = "win-x86"\nfront_end = "x"\nabi = "win32"\n')
        self.assertEqual(self.errs(), [])
        wraps = {}
        lists = targets.name_lists(self.base, wraps)
        self.assertEqual((lists["overrides/m"], wraps), (["f_one", "f_two"], {"overrides/m": {"f_one"}}))
        skip = targets.gen_skip(targets.Target("win-x86", self.base), lists, wraps)
        self.assertEqual(skip["m"].split("\n")[1:], ["401000 wrap", "401010", ""])
        # the 64-bit generator reads `ADDR wrap` as a hook point: no skip file of such a module there
        mac = targets.gen_skip(targets.Target("mac-x64", self.base), lists, wraps)
        self.assertNotIn("m", mac)
        self.assertIn("hooks", mac)
        # any other second word, and the mark outside an override list, are mistakes
        self.write("names/overrides/m.list", "f_one wrapped\nf_two\n")
        self.assertIn("f_one: one name a line, or NAME wrap", "\n".join(self.errs()))
        self.write("names/overrides/m.list", "f_one\nf_two\n")
        self.write("names/refs.list", "glob wrap\n")
        self.assertIn("glob: one name a line", "\n".join(self.errs()))

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


class Inner(Tree, unittest.TestCase):
    """Edit groups and mid points: edits.map and mids.map against names/edits.list and mids.list."""

    def setUp(self):
        Tree.setUp(self)
        self.write("win-x86/symbols.map", "f_one 0x401000 cdecl -\nf_two 0x401010 cdecl -\nframe 0x401020 cdecl -\n"
                                          "glob 0x500000 data int\n")
        self.write("names/edits.list", "wide  # a group\n")
        self.write("names/mids.list", "pick\n")
        self.write("mac-x64/target.toml", 'id = "mac-x64"\nfront_end = "x"\nabi = "sysv64"\n')
        self.write("win-x86/target.toml", 'id = "win-x86"\nfront_end = "x"\nabi = "win32"\n')
        self.write("mac-x64/edits.map", "wide 0x1004 6:0123abcd +2=6402  # a stride\nwide 0x1010 2:89abcdef =ebfe9090\n")
        self.write("mac-x64/mids.map", "pick 0x1008 (int n@rsi, void *o@[rsp+0x10], float x@xmm1) -> skip=0x100c, out=0x1010\n")
        self.write("win-x86/edits.map", "wide unsupported not found here\n")
        self.write("win-x86/mids.map", "pick 0x401008 (int n@esi, void *o@[esp+0x10], double x@st0) -> skip=0x40100c, out=0x401010\n")

    def test_tables_pass_and_generate(self):
        self.assertEqual(self.errs(), [])
        lists = targets.name_lists(self.base)
        mac, win = targets.Target("mac-x64", self.base), targets.Target("win-x86", self.base)
        edits, mids = targets.inner_points(mac, lists)
        self.assertEqual(edits[0x1004][:4], (0, 6, "0123abcd", [(2, b"\x64\x02")]))
        self.assertEqual(edits[0x1010][3], [(None, bytes.fromhex("ebfe9090"))])
        old = bytes.fromhex("69c0cc010000")
        self.assertEqual(targets.edit_bytes(old, edits[0x1004][3]), bytes.fromhex("69c064020000"))
        self.assertEqual(targets.edit_check(old), __import__("hashlib").sha256(old).hexdigest()[:8])
        self.assertEqual(mids[0x1008], (0, [0x100c, 0x1010], "pick"))
        self.assertEqual(mac.mids["pick"][1], [("i", "n", 6, 0, 0), ("p", "o", 4, 1, 0x10), ("f", "x", 17, 0, 0)])
        self.assertEqual(win.mids["pick"][1][2], ("d", "x", 32, 0, 0))
        c = targets.gen_runtime_c(win, lists)
        self.assertIn('{"wide", 0, "not found here"},', c)
        self.assertIn("{\"pick\", 0x401008ull, 0, 3, {{\"n\", 'i', 6, 0}, {\"o\", 'p', 132, 16}, {\"x\", 'd', 32, 0}}, 2, "
                      "{\"skip\", \"out\"}},", c)
        self.assertIn('{"wide", 2, 0},', targets.gen_runtime_c(mac, lists))

    def test_none_point(self):
        # `NAME none REASON`: the target needs no call there; it is neither a point nor unsupported
        self.write("names/mids.list", "pick\nskip\n")
        self.write("mac-x64/mids.map", "pick 0x1008 (int n@rsi)\nskip none read through the widened function\n")
        self.write("win-x86/mids.map", "pick 0x401008 (int n@esi)\nskip unsupported not here\n")
        self.assertEqual(self.errs(), [])
        lists = targets.name_lists(self.base)
        mac = targets.Target("mac-x64", self.base)
        self.assertEqual(mac.inner_none, {"skip": "read through the widened function"})
        self.assertNotIn("skip", mac.inner_unsupported)
        self.assertEqual(sorted(m[2] for m in targets.inner_points(mac, lists)[1].values()), ["pick"])
        self.assertIn('{"skip", 0, "read through the widened function", 0, {{0}}, 0, {0}, 1},',
                      targets.gen_runtime_c(mac, lists))
        self.assertIn('{"skip", 0, "not here"},', targets.gen_runtime_c(targets.Target("win-x86", self.base), lists))
        for text, want in (("skip none\n", "give the reason"), ("skip none x\nskip none y\n", "defined twice"),
                           ("skip none x\nskip unsupported y\n", "defined twice")):
            self.write("mac-x64/mids.map", "pick 0x1008 (int n@rsi)\n" + text)
            self.assertIn(want, "\n".join(self.errs()))

    def test_missing_and_stale(self):
        self.write("win-x86/edits.map", "")
        self.write("win-x86/mids.map", "other 0x401008 ()\n")
        e = "\n".join(self.errs())
        self.assertIn("target win-x86: edit group wide has no edits", e)
        self.assertIn("target win-x86: no mid point pick", e)
        self.assertIn("target win-x86: other is in edits.map or mids.map but not in names/edits.list or mids.list", e)

    def test_bad_lines(self):
        for text, want in (("wide 0x1004 6:0123abcd +5=6402\n", "a patch outside the range"),
                           ("wide 0x1004 6:0123abcd +2=6402,+3=02\n", "over another"),
                           ("wide 0x1004 6:0123abcd\n", "expected GROUP ADDRESS LEN:CHECK EDIT"),
                           ("wide 0x1004 6:0123abcd +2=zz\n", "=HEX or +OFF=HEX"),
                           ("wide 0x1004 69c0cc010000 +2=64\n", "expected LEN:CHECK"),
                           ("wide 0x1004 6:0123abcd +2=6402\nwide 0x1006 2:00000000 =9090\n", "overlaps the edit at 0x1004")):
            self.write("mac-x64/edits.map", text)
            self.assertIn(want, "\n".join(self.errs()))
        self.write("mac-x64/edits.map", "wide 0x1004 6:0123abcd +2=6402\n")
        for text, want in (("pick 0x1008 (int n@st0)\n", "x87 values are a 32-bit guest's"),
                           ("pick 0x1008 (float x@rax)\n", "a float in a float register"),
                           ("pick 0x1008 (int n@[xmm0+4])\n", "a general register only"),
                           ("pick 0x1008 (int n@rax) -> skip\n", "NAME=ADDRESS"),
                           ("pick 0x1008 int n@rax\n", "expected (VALUES)")):
            self.write("mac-x64/mids.map", text)
            self.assertIn(want, "\n".join(self.errs()))


class InnerRules(unittest.TestCase):
    """Rules of the committed edits and mid points that check.py does not hold every table to, and the mods that
    use them do: rearmed-mods' Seven-String Guitar reads both games with one source."""
    # The points whose values differ between the games, which that mod reads by target (README.md, "Edits and mid
    # points")
    DIFFER = {"seven_psn2", "seven_psn2_after", "seven_s3", "seven_md1"}
    # Learn & Play's F1 fret bars: an injected Entity_SetScale in seven_string itself (released so, in 1.20.0), which
    # is on only with the mod that always takes seven_f1_scale; later ones have a group of their own
    INJECTED_IN_SEVEN_STRING = {"seven_f1_scale"}

    @classmethod
    def setUpClass(cls):
        cls.t = {tid: targets.Target(tid) for tid in targets.target_ids()}
        with open(os.path.join(HERE, "names", "mids.list")) as f:
            lines = f.read().splitlines()
        section, cls.seven = False, []
        for ln in lines:
            if ln.startswith("# --- Seven-String Guitar"):
                section = ln.rstrip().endswith("begin")
                continue
            name = ln.split("#", 1)[0].strip()
            if section and name:
                cls.seven.append(name)

    @staticmethod
    def kind(t, name):
        return "address" if name in t.mids else "none" if name in t.inner_none else \
            "unsupported" if name in t.inner_unsupported else "missing"

    def test_section_read(self):
        self.assertIn("seven_gkc", self.seven)
        self.assertIn("seven_gkr", self.seven)
        self.assertNotIn("fps_calib_window", self.seven)

    def test_every_point_runs_somewhere(self):
        # a point no game calls is a name for nothing: `none` in one target says the other does the work
        for name in targets.read_list(os.path.join(HERE, "names", "mids.list")):
            kinds = {self.kind(t, name) for t in self.t.values()}
            self.assertIn("address", kinds, name)

    def test_seven_string_points_are_there(self):
        # every point of the section is a place or `none` in each game, never `unsupported`: the mod takes each
        # (the native charts, eight strings, the extended bass and co-op among them) on both editions
        for name in self.seven:
            for tid, t in self.t.items():
                self.assertIn(self.kind(t, name), ("address", "none"), "%s in %s" % (name, tid))

    def test_same_values_in_every_target(self):
        # one mod source reads both games: a point placed in two games has the same values by name and kind (an integer
        # of any width, a pointer, a float or double) and the same continuations
        cls = {"b": "int", "h": "int", "i": "int", "q": "int", "p": "ptr", "f": "float", "d": "float"}
        placed = [t for t in self.t.values()]
        for name in sorted(set().union(*(set(t.mids) for t in placed)) - self.DIFFER):
            have = [t for t in placed if name in t.mids]
            seen = {t.id: (sorted((v[1], cls[v[0]]) for v in t.mids[name][1]), sorted(c for c, _ in t.mids[name][2]))
                    for t in have}
            self.assertEqual(len(set(map(repr, seen.values()))), 1, "%s: %s" % (name, seen))

    def test_injected_calls_have_groups_of_their_own(self):
        # Learn & Play's edits that call Entity_SetScale with what a point put in a register start at that point
        # (NAME_scale) and are a group the mod switches on only with the point taken; the other game calls it from the
        # point NAME_setscale, its NAME_scale `none` and the group `unsupported`
        for tid, t in self.t.items():
            at = {a: n for n, (a, _, _) in t.mids.items()}
            groups = {}
            for g, a, *_ in t.edits:
                n = at.get(a, "")
                if n.endswith("_scale"):
                    if n in self.INJECTED_IN_SEVEN_STRING:
                        self.assertEqual(g, "seven_string", n)
                        continue
                    self.assertNotEqual(g, "seven_string", "%s %s: an injected call in seven_string" % (tid, n))
                    groups.setdefault(g, []).append(n)
            for g, points in groups.items():
                for g2, a, *_ in t.edits:
                    if g2 == g:
                        self.assertTrue(at.get(a, "").endswith("_scale"), "%s %s 0x%x: not at a scale point" % (tid, g, a))
                for n in points:
                    self.assertEqual(self.kind(t, n[:-len("_scale")] + "_setscale"), "none", "%s %s" % (tid, n))
                for oid, o in self.t.items():
                    if oid == tid or g not in o.inner_unsupported:
                        continue
                    for n in points:
                        self.assertEqual(self.kind(o, n), "none", "%s %s" % (oid, n))
                        self.assertEqual(self.kind(o, n[:-len("_scale")] + "_setscale"), "address", "%s %s" % (oid, n))
        self.assertIn("seven_glow", {e[0] for e in self.t["win-x86"].edits})
        self.assertIn("seven_barre", {e[0] for e in self.t["win-x86"].edits})

    def test_no_register_read_after_a_call_changed_it(self):
        # An `=HEX` that calls a game function reads no register the call may change (the caller-saved ones but the
        # result: Learn & Play's ecx and edx, the Mac's rcx, rdx, rsi, rdi, r8-r11) before it sets it again. The barre
        # edit pushed ecx, the scale, once more for its second Entity_SetScale, which had left its own ecx there.
        try:
            import capstone
        except ImportError:
            self.skipTest("no capstone (tools/requirements.txt)")

        def family(n):
            m = re.match(r"^[re]?([abcd])[xlh]$", n) or re.match(r"^[re]?(si|di|sp|bp)l?$", n) or \
                re.match(r"^(r\d+)[dwb]?$", n)
            return (m.group(1) + ("x" if len(m.group(1)) == 1 else "")) if m else n
        changed = {"win32": {"cx", "dx"}, "sysv": {"cx", "dx", "si", "di", "r8", "r9", "r10", "r11"}}
        for tid, t in self.t.items():
            abi = "win32" if t.info.get("abi") == "win32" else "sysv"
            md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32 if abi == "win32" else capstone.CS_MODE_64)
            md.detail = True
            for g, a, n, check_, patches, where in t.edits:
                for off, code in patches:
                    if off is not None:
                        continue
                    stale = set()
                    for i in md.disasm(code, a):
                        read, written = (({family(i.reg_name(r)) for r in regs}) for regs in i.regs_access())
                        ops = [o.strip() for o in i.op_str.split(",")]
                        if i.mnemonic in ("xor", "sub") and len(ops) == 2 and ops[0] == ops[1]:
                            read = set()  # a register cleared, not read
                        if i.mnemonic != "call":
                            self.assertFalse(read & stale, "%s (%s) 0x%x %s %s: %s after a call" % (
                                where, g, i.address, i.mnemonic, i.op_str, " ".join(sorted(read & stale))))
                        stale -= written
                        if i.mnemonic == "call":
                            stale |= changed[abi]

    def test_eight_strings(self):
        # with seven_e8 (its presence tells the mod that the chord loops run over eight strings) no edit of the
        # Seven-String groups stops at seven; the peg pool's PSQ1 asks for seven, and seven_gkp makes it eight
        seven_bound = re.compile(r"\b6 -> 7\b|strings 0-6\b|0-5 -> 0-6\b|< 7\b|over 7 strings")
        for tid, t in self.t.items():
            self.assertEqual(self.kind(t, "seven_e8"), "address", tid)
            self.assertIn(self.kind(t, "seven_gkp"), ("address", "none"), tid)
            with open(os.path.join(HERE, tid, "edits.map")) as f:
                lines = f.readlines()
            for ln in lines:
                code, _, note = ln.partition("#")
                if code.strip().startswith("seven_") and seven_bound.search(note) and "PSQ1" not in note:
                    self.fail("%s: %s" % (tid, ln.strip()))


class Sources(unittest.TestCase):
    def test_scanner_finds_uses(self):
        syms, offs, hooks = check.source_uses()
        self.assertIn("game_bootstrap", syms)          # the seam's G()
        self.assertIn("path_intern", syms)             # OV_GUEST
        self.assertIn("lua_gettop", syms)              # RC_FN
        self.assertIn("efd_inc_ref_slot", offs)        # RC_SLOT
        self.assertIn("service_manager_quit", offs)    # RC_OFF
        self.assertNotIn("name", syms)                 # macro parameters are not uses
        # Learn & Play's built-in fixes (win-x86/shims/win32_overrides.c)
        self.assertIn("song_key_known", syms)          # RC_FN and RC_WFN
        self.assertIn("frontend_open_file", syms)      # RC_NFN
        self.assertIn("dlc_install_step", syms)        # RC_ADDR
        self.assertIn("dlc_installing", offs)          # RC_OFF
        self.assertIn("dlc_job_fail_slot", offs)       # RC_SLOT
        own, own_offs, _ = check.source_uses(patterns=check.TARGET_SOURCES["win-x86"])
        self.assertIn("num_hash", own)
        self.assertNotIn("lua_gettop", own)            # the Mac's overrides are not Learn & Play's
        self.assertIn("dlc_map_size", own_offs)        # a field handed to a macro of the file's own is still read
        self.assertNotIn("path_table_lock", own)       # the Mac's lock: Learn & Play's is path_table_cs


class InnerSources(Tree, unittest.TestCase):
    """The mid points, their values and continuations, and the edit groups the built-in code names."""

    def setUp(self):
        Tree.setUp(self)
        self.write("win-x86/symbols.map", "f_one 0x401000 cdecl -\nf_two 0x401010 cdecl -\n"
                                          "frame 0x401020 thiscall -\nglob 0x500000 data int\n")
        self.write("mac-x64/target.toml", 'id = "mac-x64"\nfront_end = "x"\nabi = "sysv64"\n')
        self.write("win-x86/target.toml", 'id = "win-x86"\nfront_end = "x"\nabi = "win32"\n')
        self.write("names/mids.list", "pick\ngate\n")
        self.write("names/edits.list", "wide\n")
        self.write("mac-x64/mids.map", "pick 0x1008 (int n@rsi) -> done=0x100c\ngate 0x1004 (int who@rdi) -> skip=0x100e\n")
        self.write("win-x86/mids.map", "pick 0x401008 (int n@esi) -> done=0x40100c\ngate unsupported not here\n")
        self.write("mac-x64/edits.map", "wide 0x1002 2:00000000 +1=07\n")
        self.write("win-x86/edits.map", "wide unsupported not here\n")
        self.src = "tools/ws7/recomp/overrides/mods/x.c"

    def errs(self):
        return [e for e in check.check(self.base, root=self.base) if self.src in e]

    def test_names_the_tables_have_pass(self):
        self.write(self.src, 'void f(void) {\n  int p = rc_mid_add_name("pick", on, 0);\n'
                             '  v = rc_mid_value(p, "n"), c = rc_mid_cont(p, "done");\n  rc_edit_enable("wide", 1);\n}\n'
                             'static const struct { const char *name, *value, *cont; } t[] = {\n'
                             '    {"gate", "who", "skip"},\n    {"pick", NULL, "done"},\n};\n')
        self.assertEqual(self.errs(), [])

    def test_a_point_or_group_no_list_has(self):
        self.write(self.src, 'void f(void) { rc_mid_add_name("pik", on, 0); rc_edit_enable("wyde", 1); }\n')
        e = "\n".join(self.errs())
        self.assertIn("x.c:1: mid point pik is not in names/mids.list", e)
        self.assertIn("x.c:1: edit group wyde is not in names/edits.list", e)

    def test_a_value_or_continuation_one_target_lacks(self):
        self.write(self.src, 'void f(void) {\n  int p = cond ? -1 : rc_mid_add_name("pick", on, 0);\n'
                             '  v = rc_mid_value(p, "n");\n  c = rc_mid_cont(p, "done");\n}\n')
        self.assertEqual(self.errs(), [])
        self.write("win-x86/mids.map", "pick 0x401008 (int count@esi) -> end=0x40100c\ngate unsupported not here\n")
        e = "\n".join(self.errs())
        self.assertIn("x.c:3: target win-x86: mid point pick has no value n", e)
        self.assertIn("x.c:4: target win-x86: mid point pick has no continuation done", e)
        self.assertNotIn("mac-x64", e)
        # a value is no continuation, and a table's row may name either
        self.write("win-x86/mids.map", "pick 0x401008 (int n@esi) -> done=0x40100c\ngate unsupported not here\n")
        self.write(self.src, 'void f(void) {\n  int p = rc_mid_add_name("pick", on, 0);\n  c = rc_mid_cont(p, "n");\n}\n'
                             'static const struct { const char *name, *value; } t[] = {{"gate", "whom"}};\n')
        e = "\n".join(self.errs())
        self.assertIn("x.c:3: target mac-x64: mid point pick has no continuation n", e)
        self.assertIn("x.c:5: target mac-x64: mid point gate has no value or continuation whom", e)
        self.assertNotIn("target win-x86: mid point gate", e)  # not a point of that game

    def test_the_built_in_fixes_are_read(self):
        points, reads, groups = check.inner_uses()
        for n in ("fps_calib_window", "fps_tuner_miss", "tuner_skip_offer", "manifest_response", "fs_mount", "fs_exists"):
            self.assertIn(n, points)
        self.assertIn(("fps_tuner_miss", "either", "grace"), [r[1:] for r in reads])
        self.assertIn(("fs_exists", "cont", "answered"), [r[1:] for r in reads])
        self.assertIn(("tuner_skip_offer", "value", "offer"), [r[1:] for r in reads])


class BadNumbers(Tree, unittest.TestCase):
    def errs(self):
        return check.check(self.base, root=self.base)

    def test_an_address_or_offset_that_is_no_number_is_said_with_its_line(self):
        self.write("win-x86/symbols.map", "f_one 0x40100g cdecl -\n")
        self.assertIn("win-x86/symbols.map:1: f_one: '0x40100g' is not an address", "\n".join(self.errs()))
        self.write("win-x86/symbols.map", "f_one 0x401000 cdecl -\nf_two 0x401010 cdecl -\n"
                                          "frame 0x401020 thiscall -\nglob 0x500000 data int\n")
        self.write("win-x86/offsets.map", "fld 010\n")
        self.assertIn("win-x86/offsets.map:1: fld: '010' is not an offset", "\n".join(self.errs()))
        self.write("win-x86/offsets.map", "fld 0x10 0x14  a comment that lost its mark\n")
        self.assertIn("win-x86/offsets.map:1: fld: expected NAME OFFSET", "\n".join(self.errs()))

    def test_a_skip_file_no_longer_made_goes(self):
        self.write("win-x86/symbols.map", "f_one 0x401000 cdecl -\nf_two 0x401010 cdecl -\n"
                                          "frame 0x401020 thiscall -\nglob 0x500000 data int\n")
        out = os.path.join(self.base, "out")
        targets.gen("win-x86", out, self.base)
        self.assertTrue(os.path.isfile(os.path.join(out, "skip", "m.skip")))
        os.unlink(os.path.join(self.base, "names", "overrides", "m.list"))
        targets.gen("win-x86", out, self.base)
        self.assertFalse(os.path.exists(os.path.join(out, "skip", "m.skip")))
        self.assertTrue(os.path.isfile(os.path.join(out, "skip", "hooks.skip")))


class TargetSources(Tree, unittest.TestCase):
    """Code built for one target alone (check.TARGET_SOURCES): a name it uses has to be in that target."""

    def setUp(self):
        Tree.setUp(self)
        self.write("win-x86/symbols.map", "f_one 0x401000 cdecl -\nf_two unsupported inlined here\n"
                                          "frame 0x401020 thiscall -\nglob 0x500000 data int\n")

    def errs(self):
        return check.check(self.base, root=self.base)

    def test_names_of_the_target_pass(self):
        self.write("win-x86/shims/win32_x.c", "void RC_FN(f_one)(rc_ctx *c) { RC_WFN(f_one)(c); }\n"
                                               "void RC_NFN(frame)(rc_ctx *c) { st(RC_ADDR(glob) + RC_OFF(fld)); }\n")
        self.assertEqual(self.errs(), [])

    def test_unknown_name_fails(self):
        self.write("win-x86/shims/win32_x.c", "void f(void) { call(RC_ADDR(f_three)); read(RC_OFF(no_field)); }\n")
        e = "\n".join(self.errs())
        self.assertIn("win-x86/shims/win32_x.c:1: f_three is used as a game symbol but no list", e)
        self.assertIn("win-x86/shims/win32_x.c:1: no_field is used as an offset", e)

    def test_unsupported_name_fails(self):
        # the Mac has f_two, Learn & Play doesn't: code of every target may name it, Learn & Play's own may not
        self.write("tools/ws7/recomp/overrides/src/x.c", "void RC_FN(f_two)(rc_ctx *c) {}\n")
        self.assertEqual(self.errs(), [])
        self.write("win-x86/app/lp_x.c", "void g(void) { call(RC_ADDR(f_two)); }\n")
        e = self.errs()
        self.assertEqual(len(e), 1, e)
        self.assertIn("win-x86/app/lp_x.c:1: f_two is used in code built for win-x86 alone, where it is unsupported "
                      "(inlined here)", e[0])

    def test_offset_as_symbol_fails(self):
        self.write("win-x86/shims/d3d9/x.c", "void g(void) { call(RC_ADDR(fld)); }\n")
        self.assertIn("fld is used as a game symbol but no list in tools/targets/names has it (it is an offset)",
                      "\n".join(self.errs()))


if __name__ == "__main__":
    unittest.main()
