"""Tests for the offline unpacker.

Fast tests (always run): seed container round-trips; the module exposes its API.
Integration test (opt-in): set RS_UNPACK_EXE, RS_UNPACK_SEED and, optionally,
RS_UNPACK_SHA256 to run a full unpack and verify the output. Skipped when the heavy
deps (unicorn/pefile) or the artifacts are absent, so CI stays stdlib-only.
"""
import os, sys, gzip, pickle, hashlib, unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

try:
    import unicorn, pefile  # noqa: F401
    _HAVE_DEPS = True
except Exception:
    _HAVE_DEPS = False


class SeedContainer(unittest.TestCase):
    def test_roundtrip(self):
        seed = {
            'exe_delta': [(0x1467abc, 0x12), (0x1467abd, 0x34)],
            'gdt_addr': 0x30000, 'gdt': b'\x00' * 0x60,
            'pages': [(0xfffdd000, b'\xaa' * 0x1000)],
            'kern_targets': [0x10016b30, 0x1001c7f0],
            'regs': {'eip': 0x2366562, 'esp': 0xfffdf000},
        }
        import io
        buf = io.BytesIO()
        with gzip.GzipFile(fileobj=buf, mode="wb") as f:
            f.write(b"LPSEED01"); pickle.dump(seed, f, protocol=4)
        buf.seek(0)
        with gzip.GzipFile(fileobj=buf, mode="rb") as f:
            self.assertEqual(f.read(8), b"LPSEED01")
            got = pickle.load(f)
        self.assertEqual(got['exe_delta'], seed['exe_delta'])
        self.assertEqual(got['regs']['eip'], 0x2366562)
        self.assertEqual(got['kern_targets'], seed['kern_targets'])


@unittest.skipUnless(_HAVE_DEPS, "unicorn/pefile not installed")
class Api(unittest.TestCase):
    def test_exports(self):
        import lp_unpack
        self.assertTrue(hasattr(lp_unpack, "unpack"))
        self.assertTrue(hasattr(lp_unpack, "layout_exe_at_vas"))
        self.assertTrue(hasattr(lp_unpack, "pe_layout"))
        # the anti-hook prologue must begin with 0x8b (what VMProtect checks)
        self.assertEqual(lp_unpack.PROLOGUE[0], 0x8b)


@unittest.skipUnless(_HAVE_DEPS, "unicorn/pefile not installed")
class Relocation(unittest.TestCase):
    def _image(self):
        import struct
        img = bytearray(0x2000)
        struct.pack_into("<I", img, 0x3c, 0x80)
        struct.pack_into("<I", img, 0x80 + 24 + 28, 0x400000)
        struct.pack_into("<I", img, 0x1004, 0x00401234)
        return bytes(img)

    def test_sites_and_base_move(self):
        import lp_unpack, struct
        seed = {"relocs": [0x1004], "link_base": 0x400000, "run_base": 0xf80000}
        out = lp_unpack.relocate(self._image(), seed)
        self.assertEqual(struct.unpack_from("<I", out, 0x1004)[0], 0x00f81234)
        self.assertEqual(struct.unpack_from("<I", out, 0x80 + 24 + 28)[0], 0xf80000)

    def test_a_seed_without_sites_leaves_the_image(self):
        import lp_unpack
        self.assertEqual(lp_unpack.relocate(self._image(), {}), self._image())


@unittest.skipUnless(_HAVE_DEPS, "unicorn/pefile not installed")
class SeedChecks(unittest.TestCase):
    def _write(self, seed):
        import tempfile
        fd, path = tempfile.mkstemp(suffix=".bin")
        with os.fdopen(fd, "wb") as raw, gzip.GzipFile(fileobj=raw, mode="wb") as f:
            f.write(b"LPSEED01"); pickle.dump(seed, f, protocol=4)
        self.addCleanup(os.remove, path)
        return path

    def test_objects_refused(self):
        import lp_unpack
        path = self._write({"regs": {"eip": 1}, "evil": unittest.TestCase})
        with self.assertRaises(pickle.UnpicklingError):
            lp_unpack.load_seed(path)

    def test_plain_data_loads(self):
        import lp_unpack
        path = self._write({"regs": {"eip": 1}, "pages": [(0x1000, b"\x00")]})
        self.assertEqual(lp_unpack.load_seed(path)["regs"]["eip"], 1)

    def test_wrong_build_refused(self):
        import lp_unpack, tempfile
        fd, exe = tempfile.mkstemp(); os.write(fd, b"MZ not the game"); os.close(fd)
        self.addCleanup(os.remove, exe)
        with self.assertRaises(ValueError):
            lp_unpack.check_seed({"exe_sha256": "0" * 64}, exe)
        lp_unpack.check_seed({"exe_sha256": lp_unpack.file_sha256(exe)}, exe)
        lp_unpack.check_seed({}, exe)   # old seeds carry no key: a warning, not an error


class GuestState(unittest.TestCase):
    def test_segments_move_to_ring_0(self):
        import make_seed
        gdt = bytearray(0x40)
        gdt[0x18 + 5], gdt[0x20 + 5], gdt[0x38 + 5] = 0xfb, 0xf3, 0xf3
        regs = {"cs": 0x1b, "ss": 0x23, "ds": 0x23, "es": 0x23, "fs": 0x3b, "gs": 0, "eip": 7}
        new_gdt, new_regs = make_seed._ring0_segments(bytes(gdt), regs)
        self.assertEqual((new_regs["cs"], new_regs["ss"], new_regs["fs"]), (0x18, 0x20, 0x38))
        self.assertEqual((new_gdt[0x18 + 5], new_gdt[0x20 + 5], new_gdt[0x38 + 5]),
                         (0x9b, 0x93, 0x93))
        self.assertEqual(new_regs["eip"], 7)

    def test_text_is_blanked_in_place(self):
        import make_seed
        text = "C:\\Users\\someone".encode("utf-16-le")
        data = b"\x01\x02" + text + b"\x03"
        self.assertEqual(make_seed._blank_text(data), b"\x01\x02" + bytes(len(text)) + b"\x03")

    RVAS = {".text": (0x1000, 0x1000), ".rdata": (0x2000, 0x1000), ".data": (0x3000, 0x1000)}

    def sites(self, image, loaded):
        import make_seed
        gaps = make_seed.relocation_sites(bytes(image), bytes(loaded), 0xb80000, 0x400000, self.RVAS)
        return [sum(gaps[:k + 1]) for k in range(len(gaps))]

    def test_relocation_sites_are_the_words_moved_by_the_delta(self):
        import struct
        image = bytearray(0x4000)
        struct.pack_into("<I", image, 0x1004, 0x00401234)
        struct.pack_into("<I", image, 0x1011, 0x00e00000)   # unaligned, low bytes unchanged
        loaded = bytearray(image)
        struct.pack_into("<I", loaded, 0x1004, 0x00f81234)
        struct.pack_into("<I", loaded, 0x1011, 0x01980000)
        struct.pack_into("<I", loaded, 0x1020, 0x12345678)  # runtime state: not a relocation
        self.assertEqual(self.sites(image, loaded), [0x1004, 0x1011])

    def test_a_pointer_the_game_overwrote_is_still_a_site(self):
        import struct
        image = bytearray(0x4000)
        struct.pack_into("<I", image, 0x3010, 0x00401000)   # the game changed this one
        struct.pack_into("<I", image, 0x3014, 0x00401000)   # unchanged: data, not a pointer
        struct.pack_into("<I", image, 0x3018, 0x00000123)   # changed, but no pointer
        loaded = bytearray(image)
        struct.pack_into("<I", loaded, 0x3010, 0x01001234)
        struct.pack_into("<I", loaded, 0x3018, 0x00000456)
        self.assertEqual(self.sites(image, loaded), [0x3010])

    def test_modules_are_found_by_their_headers(self):
        import make_seed, struct
        page = bytearray(0x1000)
        page[:2] = b"MZ"
        struct.pack_into("<I", page, 0x3c, 0x80)
        page[0x80:0x84] = b"PE\0\0"
        struct.pack_into("<I", page, 0x80 + 24 + 56, 0x3000)
        pages = [(0x70000000, bytes(page)), (0x70001000, b"\x90" * 0x1000),
                 (0x80001000, bytes(page))]   # not on a 64 KB boundary: not a module
        self.assertEqual(make_seed._module_ranges(pages), [(0x70000000, 0x70003000)])


@unittest.skipUnless(_HAVE_DEPS and os.environ.get("RS_UNPACK_EXE") and os.environ.get("RS_UNPACK_SEED"),
                     "set RS_UNPACK_EXE and RS_UNPACK_SEED for the full integration test")
class Integration(unittest.TestCase):
    def test_unpack_matches(self):
        import lp_unpack
        img = lp_unpack.unpack(os.environ["RS_UNPACK_EXE"], os.environ["RS_UNPACK_SEED"])
        self.assertEqual(len(img), lp_unpack.pe_layout(os.environ["RS_UNPACK_EXE"]).size)
        want = os.environ.get("RS_UNPACK_SHA256")
        if want:
            self.assertEqual(hashlib.sha256(img).hexdigest(), want)


if __name__ == "__main__":
    unittest.main()
