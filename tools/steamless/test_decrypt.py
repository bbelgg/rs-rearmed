#!/usr/bin/env python3
"""tools/steamless/decrypt.py on a made-up Mach-O: no game bytes. The real check runs when the clean depot is at
$RS_DEPOT_GAME (the Mac game folder), and is skipped otherwise.

    python3 tools/steamless/test_decrypt.py
"""
import hashlib
import io
import os
import random
import struct
import sys
import tempfile
import unittest
from contextlib import redirect_stdout

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import decrypt  # noqa: E402

KEY = bytes.fromhex("a1b2c3d4e5f60718")
HEADER_LOAD = 72 + 80  # one LC_SEGMENT_64 with one section


def macho(text, extra=b""):
    """An x86-64 Mach-O: header, __TEXT with a __text section, the section, what follows it."""
    start = 32 + HEADER_LOAD
    seg = struct.pack("<II16sQQQQIIII", decrypt.LC_SEGMENT_64, HEADER_LOAD, b"__TEXT", 0, 0, 0, 0, 7, 5, 1, 0)
    sect = struct.pack("<16s16sQQIIIIIIII", b"__text", b"__TEXT", 0x100001b00, len(text), start, 0, 0, 0, 0, 0, 0, 0)
    head = struct.pack("<IIIIIIII", decrypt.MH_MAGIC_64, decrypt.CPU_X86_64, 3, 2, 1, HEADER_LOAD, 0, 0)
    return head + seg + sect + text + extra


def fat(*slices):
    """A universal binary of (cpu, Mach-O) pairs."""
    out = struct.pack(">II", decrypt.FAT_MAGIC, len(slices))
    at = 4096
    body = b""
    for cpu, data in slices:
        out += struct.pack(">5I", cpu, 3, at + len(body), len(data), 12)
        body += data
    return out.ljust(4096, b"\0") + body


def clear_text():
    rng = random.Random(7)
    t = decrypt.CRT_START + bytes(rng.randrange(256) for _ in range(40))
    for _ in range(50):
        t += decrypt.PADDING + bytes(rng.randrange(256) for _ in range(24))
    return t + b"\x90\x90\xc3"  # not a multiple of 8, as the game's


def encrypted(text, key=KEY):
    return decrypt.xor(text, (key * (len(text) // 8 + 1))[:len(text)])


class Decrypt(unittest.TestCase):
    def setUp(self):
        d = tempfile.TemporaryDirectory()
        self.addCleanup(d.cleanup)
        self.tmp = d.name
        self.text = clear_text()
        self.clear = macho(self.text, b"tail of the file")
        self.game = fat((7, b"i386 code" * 10), (decrypt.CPU_X86_64, macho(encrypted(self.text), b"tail of the file")))

    def run_tool(self, data, text_pin=None, image_pin=None):
        binary = os.path.join(self.tmp, "game")
        with open(binary, "wb") as f:
            f.write(data)
        target = os.path.join(self.tmp, "target.toml")
        with open(target, "w") as f:
            f.write('text_sha256 = "%s"\nimage_sha256 = "%s"\n' % (
                text_pin or hashlib.sha256(self.text).hexdigest(), image_pin or hashlib.sha256(self.clear).hexdigest()))
        self.out = os.path.join(self.tmp, "build", "image")
        said = io.StringIO()
        with redirect_stdout(said):
            code = decrypt.main(["--game", binary, "--output", self.out, "--target", target])
        return code, said.getvalue()

    def test_universal(self):
        self.assertEqual(self.run_tool(self.game)[0], 0)
        with open(self.out, "rb") as f:
            self.assertEqual(f.read(), self.clear)
        self.assertTrue(os.access(self.out, os.X_OK))

    def test_thin(self):
        self.assertEqual(self.run_tool(macho(encrypted(self.text), b"tail of the file"))[0], 0)

    def test_any_key(self):
        data = macho(encrypted(self.text, bytes(range(1, 9))), b"tail of the file")
        self.assertEqual(self.run_tool(data)[0], 0)

    def test_not_a_program(self):
        code, said = self.run_tool(b"a song, perhaps" * 100)
        self.assertEqual(code, 1)
        self.assertIn("isn't the Mac game's program", said)
        self.assertFalse(os.path.exists(self.out))

    def test_no_intel_code(self):
        code, said = self.run_tool(fat((0x0100000c, b"arm" * 100)))
        self.assertEqual(code, 1)
        self.assertIn("no Intel", said)

    def test_no_code_section(self):
        data = bytearray(macho(encrypted(self.text)))
        data[32 + 72:32 + 72 + 6] = b"__data"
        code, said = self.run_tool(bytes(data))
        self.assertEqual(code, 1)
        self.assertIn("no code section", said)

    def test_a_cipher_it_does_not_know(self):
        rng = random.Random(1)
        code, said = self.run_tool(macho(bytes(rng.randrange(256) for _ in range(4000))))
        self.assertEqual(code, 1)
        self.assertIn("Verify integrity", said)
        self.assertFalse(os.path.exists(self.out))

    def test_another_build(self):
        code, said = self.run_tool(self.game, text_pin="0" * 64)
        self.assertEqual(code, 2)
        self.assertIn("different build", said)
        self.assertFalse(os.path.exists(self.out))

    def test_a_changed_header_with_the_right_code(self):
        code, said = self.run_tool(self.game, image_pin="0" * 64)
        self.assertEqual(code, 0)
        self.assertIn("a mod. That's fine", said)
        self.assertTrue(os.path.isfile(self.out))

    def test_the_key_is_not_said(self):
        self.assertNotIn(KEY.hex(), self.run_tool(self.game)[1])

    @unittest.skipUnless(os.environ.get("RS_DEPOT_GAME"), "RS_DEPOT_GAME: the clean Mac depot folder")
    def test_the_real_depot_matches_the_pin(self):
        out = os.path.join(self.tmp, "real")
        with redirect_stdout(io.StringIO()):
            self.assertEqual(decrypt.main(["--game", os.environ["RS_DEPOT_GAME"], "--output", out]), 0)
        h = hashlib.sha256()
        with open(out, "rb") as f:
            h.update(f.read())
        self.assertEqual(h.hexdigest(), decrypt.parse_toml(decrypt.TARGET)["image_sha256"])


if __name__ == "__main__":
    unittest.main()
