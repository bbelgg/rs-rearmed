#!/usr/bin/env python3
"""The corpus's sources from stand-in shaders: lp_shaders.c finds bytecode at any offset and makes the listed programs,
record_programs.py makes the list. Hand-assembled shaders, nothing of the game.

    python3 -m unittest discover -s win-x86/shaders -p 'test_*.py'
"""
import importlib.util
import os
import re
import shutil
import struct
import subprocess
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
D3D9 = os.path.join(HERE, "..", "shims", "d3d9")
spec = importlib.util.spec_from_file_location("record_programs", os.path.join(HERE, "record_programs.py"))
record_programs = importlib.util.module_from_spec(spec)
spec.loader.exec_module(record_programs)


def fnv64(data):
    h = 0xcbf29ce484222325
    for b in data:
        h = ((h ^ b) * 0x100000001b3) & 0xffffffffffffffff
    return h


def words(*w):
    return struct.pack("<%dI" % len(w), *w)


def dcl(usage, index, reg):
    return (0x0200001f, 0x80000000 | usage | index << 16, reg)


# vs_3_0: dcl_position v0; dcl_texcoord0 v1; dcl_position o0; dcl_texcoord0 o1; dcl_texcoord1 o2;
#         mov o0, v0; mov o1, v1; mov o2, v1
VS = words(0xfffe0300, *dcl(0, 0, 0x900f0000), *dcl(5, 0, 0x900f0001), *dcl(0, 0, 0xe00f0000), *dcl(5, 0, 0xe00f0001),
           *dcl(5, 1, 0xe00f0002), 0x02000001, 0xe00f0000, 0x90e40000, 0x02000001, 0xe00f0001, 0x90e40001,
           0x02000001, 0xe00f0002, 0x90e40001, 0x0000ffff)
# ps_3_0: dcl_texcoord0 v0; mov oC0, v0
PS = words(0xffff0300, *dcl(5, 0, 0x900f0000), 0x02000001, 0x800f0800, 0x90e40000, 0x0000ffff)
# ps_3_0: dcl_color0 v0; mov oC0, v0
PS_COLOR = words(0xffff0300, *dcl(10, 0, 0x900f0000), 0x02000001, 0x800f0800, 0x90e40000, 0x0000ffff)


class TestRecord(unittest.TestCase):
    def test_a_log_becomes_lines_without_the_runs_numbers(self):
        key = "07000000" "09000000" + "ab" * 8
        self.assertEqual(record_programs.line_of(["1.0", "program", "ffp", "0" * 16, "00000000000000ff", "1", "2", key]),
                         "ffp 0 ff " + "0" * 16 + "ab" * 8)
        self.assertEqual(record_programs.line_of(["1.0", "program", "pair", "000000000000000a", "000000000000000b", "1", "2",
                                                  ""]), "pair a b")
        self.assertIsNone(record_programs.line_of(["1.0", "program", "blit", "0", "0", "1", "2", ""]))
        self.assertIsNone(record_programs.line_of(["1.0", "shader", "vs_3_0", "000000000000000a"]))

    def test_the_list_merges_and_stays_sorted(self):
        d = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, d)
        log, lst = os.path.join(d, "log.tsv"), os.path.join(d, "list.tsv")
        for vs in ("b", "a"):
            with open(log, "w") as f:
                f.write("1.0\tprogram\tpair\t%s\t1\t2\t3\t\n" % vs.rjust(16, "0"))
            subprocess.run(["python3", "-I", os.path.join(HERE, "record_programs.py"), "--list", lst, log], check=True,
                           capture_output=True)
        self.assertEqual(sorted(record_programs.read_list(lst)), ["pair a 1", "pair b 1"])
        with open(lst) as f:
            self.assertEqual([l for l in f if not l.startswith("#")], ["pair a 1\n", "pair b 1\n"])


@unittest.skipUnless(shutil.which("clang"), "no clang")
class TestTool(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.d = tempfile.mkdtemp()
        cls.tool = os.path.join(cls.d, "lp_shaders")
        subprocess.run(["clang", "-O1", "-I" + D3D9, os.path.join(HERE, "lp_shaders.c"), os.path.join(D3D9, "d3d9_shader.c"),
                        os.path.join(D3D9, "d3d9_ffp_gen.c"), "-o", cls.tool], check=True)
        # an image: the shaders at offsets that are not multiples of 4, between bytes that look like nothing
        cls.blob = os.path.join(cls.d, "image.bin")
        with open(cls.blob, "wb") as f:
            f.write(b"\xff" * 5 + VS + b"\x00\x00\xfe\xff\x03" + PS + b"\x01")

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.d)

    def make(self, lines):
        out = tempfile.mkdtemp(dir=self.d)
        lst = os.path.join(out, "list.tsv")
        with open(lst, "w") as f:
            f.write("# a comment\n" + "".join(l + "\n" for l in lines))
        r = subprocess.run([self.tool, lst, os.path.join(out, "o"), self.blob], capture_output=True, text=True, check=True)
        with open(os.path.join(out, "o", "pairs.tsv")) as f:
            return r.stdout, [l.rstrip("\n").split("\t") for l in f]

    def test_a_pair_is_made_for_its_pixel_shader(self):
        vs, ps = "%x" % fnv64(VS), "%x" % fnv64(PS)
        out, pairs = self.make(["pair %s %s" % (vs, ps)])
        self.assertIn("2 shaders", out)
        self.assertIn("1 of 1 listed", out)
        self.assertEqual(len(pairs), 2)  # the layer's blit and the program
        v, f, bind = pairs[1]
        with open(v) as t:
            text = t.read()
        # generated for this pixel shader: its one varying, not the vertex shader's second output
        self.assertEqual(re.findall(r"varying vec4 (\w+);", text), ["rs_texcoord0"])
        with open(f) as t:
            self.assertIn("rs_texcoord0", t.read())
        self.assertTrue(bind.startswith("rs_v0=0,rs_v1=1,"))

    def effect(self, name_vs, name_ps, ps):
        d = tempfile.mkdtemp(dir=self.d)
        for name, data in ((name_vs, VS), (name_ps, ps)):
            with open(os.path.join(d, name), "wb") as f:
                f.write(data)
        lst = os.path.join(d, "list.tsv")
        open(lst, "w").close()
        r = subprocess.run([self.tool, lst, os.path.join(d, "o"), os.path.join(d, name_vs), os.path.join(d, name_ps)],
                           capture_output=True, text=True, check=True)
        with open(os.path.join(d, "o", "pairs.tsv")) as f:
            return len(f.readlines()) - 1  # without the blit

    def test_the_shaders_of_one_effect_pair_up(self):
        self.assertEqual(self.effect("glow.vso", "glow.pso", PS), 1)
        self.assertEqual(self.effect("glow.vso", "bloom.pso", PS), 0)  # another name
        self.assertEqual(self.effect("glow.bin", "glow.dat", PS), 0)  # not effect files
        self.assertEqual(self.effect("glow.vso", "glow.pso", PS_COLOR), 0)  # the vertex shader doesn't write its input

    def test_a_shader_not_in_the_files_is_only_counted(self):
        out, pairs = self.make(["pair 123 %x" % fnv64(PS), "something else", "ffp 0 0 00"])
        self.assertIn("0 of 3 listed", out)
        self.assertEqual(len(pairs), 1)


if __name__ == "__main__":
    unittest.main()
