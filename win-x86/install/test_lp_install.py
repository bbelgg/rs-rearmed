#!/usr/bin/env python3
"""Tests of lp_install.py without Steam, the game or the network: small cabinets, self-extracting exes and a
PE32 DLL made here stand in for Microsoft's files (the files have the real names, made-up contents), a
folder for Steam, file:// URLs for Microsoft's downloads. The build steps (lp_build.py) run with a fake
translator, compiler and build scripts.

    python3 win-x86/install/test_lp_install.py
    LP_REDIST_REAL=1 python3 win-x86/install/test_lp_install.py     also the real DLLs from this Mac's Steam
"""
import argparse
import contextlib
import functools
import hashlib
import io
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import threading
import time
import unittest
import zlib
from unittest import mock

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import lp_build  # noqa: E402
import lp_install  # noqa: E402
from lp_install import Failure, Progress, Redist, gamefiles  # noqa: E402


def cab(files, mszip=True):
    """A one-folder cabinet (MS-CAB) of files [(name, bytes)], MSZIP or stored."""
    blob = b"".join(d for _, d in files)
    blocks = []
    for i in range(0, max(len(blob), 1), 32768):
        chunk = blob[i:i + 32768]
        if mszip:
            c = zlib.compressobj(9, zlib.DEFLATED, -15)
            data = b"CK" + c.compress(chunk) + c.flush()
        else:
            data = chunk
        blocks.append(struct.pack("<IHH", 0, len(data), len(chunk)) + data)
    entries, off = b"", 0
    for name, d in files:
        entries += struct.pack("<IIHHHH", len(d), off, 0, 0x5021, 0, 0x20) + name.encode() + b"\0"
        off += len(d)
    coff_files = 36 + 8
    data_start = coff_files + len(entries)
    body = b"".join(blocks)
    header = b"MSCF" + struct.pack("<IIIIIBBHHHHH", 0, data_start + len(body), 0, coff_files, 0, 3, 1, 1,
                                   len(files), 0, 1, 0)
    return header + struct.pack("<IHH", data_start, len(blocks), 1 if mszip else 0) + entries + body


def sfx(files):
    """A self-extracting exe as far as tar cares: an MZ stub with a cabinet after it."""
    return b"MZ" + b"\0" * 0x3e + b"\x90" * 3000 + cab(files)


def pe32(base=0x10000000, tag=b""):
    """A PE32 DLL at base with one relocated pointer (base + 0x1000) at the start of .text."""
    d = bytearray(0x600)
    d[0:2] = b"MZ"
    struct.pack_into("<I", d, 0x3c, 0x40)
    d[0x40:0x44] = b"PE\0\0"
    struct.pack_into("<HHIIIHH", d, 0x44, 0x14c, 2, 0, 0, 0, 224, 0x2102)
    opt = 0x58
    struct.pack_into("<H", d, opt, 0x10b)
    struct.pack_into("<I", d, opt + 28, base)
    struct.pack_into("<II", d, opt + 32, 0x1000, 0x200)
    struct.pack_into("<I", d, opt + 92, 16)
    struct.pack_into("<II", d, opt + 96 + 8 * 5, 0x2000, 10)
    sec = opt + 224
    d[sec:sec + 8] = b".text\0\0\0"
    struct.pack_into("<IIII", d, sec + 8, 0x200, 0x1000, 0x200, 0x200)
    d[sec + 40:sec + 48] = b".reloc\0\0"
    struct.pack_into("<IIII", d, sec + 48, 0x200, 0x2000, 0x200, 0x400)
    struct.pack_into("<I", d, 0x200, base + 0x1000)
    d[0x210:0x210 + len(tag)] = tag
    struct.pack_into("<IIH", d, 0x400, 0x1000, 10, 3 << 12)
    return bytes(d)


def sha(b):
    return hashlib.sha256(b).hexdigest()


class Fixture:
    """Made-up DLLs, the packages holding them, and a target.toml-like config with their hashes."""

    def __init__(self, top):
        self.top = top
        self.dlls = {
            "msvcr100": os.urandom(5000) + b"msvcr100",
            "msvcp100": os.urandom(3000) + b"msvcp100",
            "d3dx9_42": os.urandom(70000) + b"d3dx9",
            "d3dcompiler_42": pe32(tag=b"dcomp"),
        }
        self.rebased = lp_install.rebase(self.dlls["d3dcompiler_42"], 0x11000000)[0]
        d = self.dlls
        self.vc_red = cab([("F_CENTRAL_msvcr100_x86", d["msvcr100"]), ("F_CENTRAL_msvcp100_x86", d["msvcp100"]),
                           ("F_CENTRAL_atl100_x86", b"atl")])
        self.vc_exe = sfx([("_sfx_manifest_", b"m"), ("vc_red.msi", b"msi"), ("vc_red.cab", self.vc_red)])
        self.d3dx_cab = cab([("d3dx9_42.dll", d["d3dx9_42"]), ("d3dx9_42_x86.inf", b"inf")])
        self.dcomp_cab = cab([("D3DCompiler_42.dll", d["d3dcompiler_42"]), ("D3DCompiler_42_x86.inf", b"inf")], False)
        self.dx_exe = sfx([("DXSETUP.exe", b"setup"), ("Aug2009_d3dx9_42_x86.cab", self.d3dx_cab),
                           ("Aug2009_D3DCompiler_42_x86.cab", self.dcomp_cab), ("Jun2010_xinput_x86.cab", b"x")])
        self.web = os.path.join(top, "web")
        os.makedirs(self.web)
        for name, data in (("vcredist_x86.exe", self.vc_exe), ("directx_Jun2010_redist.exe", self.dx_exe)):
            with open(os.path.join(self.web, name), "wb") as f:
                f.write(data)
        self.steam = os.path.join(top, "Steam")
        os.makedirs(os.path.join(self.steam, "logs"))
        self.cfg = lp_install.load_config()
        self.cfg.update({
            "vcredist_url": "file://" + os.path.join(self.web, "vcredist_x86.exe"),
            "vcredist_sha256": sha(self.vc_exe),
            "dxredist_url": "file://" + os.path.join(self.web, "directx_Jun2010_redist.exe"),
            "dxredist_sha256": sha(self.dx_exe),
            "d3dcompiler_42_rebased_sha256": sha(self.rebased),
            "exe_size": 4096,
            "depot_disk_bytes": 4096,
        })
        for k, v in d.items():
            self.cfg[k + "_sha256"] = sha(v)
        self.game = os.urandom(4096)
        self.cfg["exe_sha256"] = sha(self.game)

    def install_steam_redist(self, vc=True, dx=True):
        redist = os.path.join(self.steam, lp_install.COMMON_REDIST)
        if vc:
            write(os.path.join(redist, self.cfg["steam_vcredist"]), self.vc_exe)
        if dx:
            write(os.path.join(redist, self.cfg["steam_d3dx9_cab"]), self.d3dx_cab)
            write(os.path.join(redist, self.cfg["steam_d3dcompiler_cab"]), self.dcomp_cab)

    def no_web(self):
        shutil.rmtree(self.web)

    def expect_out(self, test, out):
        for name, key, _, _ in lp_install.DLLS:
            data = read(os.path.join(out, name))
            test.assertEqual(data, self.rebased if key == "d3dcompiler_42" else self.dlls[key], name)


def write(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(data)


def read(path):
    with open(path, "rb") as f:
        return f.read()


def quiet():
    return Progress(stream=io.StringIO())


class Base(unittest.TestCase):
    def setUp(self):
        self.top = tempfile.mkdtemp(prefix="lp-install-test-")
        self.addCleanup(shutil.rmtree, self.top, True)
        env = mock.patch.dict(os.environ, LP_HOME=os.path.join(self.top, "lp-home"))
        env.start()
        self.addCleanup(env.stop)
        # The real disk and memory are not the test's business.
        for name, value in (("free_bytes", 1 << 50), ("memory_bytes", 1 << 40), ("on_startup_disk", False)):
            p = mock.patch.object(lp_install, name, lambda *a, v=value: v)
            p.start()
            self.addCleanup(p.stop)
        self.fx = Fixture(self.top)
        self.out = os.path.join(self.top, "build", "dlls")

    def redist(self, **kw):
        p = quiet()
        Redist(self.fx.cfg, self.out, self.fx.steam, progress=p, **kw).run()
        return p.stream.getvalue()


class TestRedist(Base):
    def test_fixture_cabs_read_by_tar(self):
        tmp = os.path.join(self.top, "x")
        got = lp_install.untar(os.path.join(self.fx.web, "vcredist_x86.exe"), tmp, ["VC_RED.CAB"])
        self.assertEqual(read(got["vc_red.cab"]), self.fx.vc_red)

    def test_from_steam(self):
        self.fx.install_steam_redist()
        self.fx.no_web()  # source 1 must not touch the network
        log = self.redist()
        self.fx.expect_out(self, self.out)
        self.assertIn("Steam's Common Redistributables", log)
        self.assertIn("rebased to 0x11000000", log)
        self.assertFalse(os.path.exists(os.path.join(self.out, ".cache")))

    def test_again_keeps_what_is_there(self):
        self.fx.install_steam_redist()
        self.redist()
        stamps = {f: os.stat(os.path.join(self.out, f)).st_mtime_ns for f in os.listdir(self.out)}
        shutil.rmtree(self.fx.steam)
        self.fx.no_web()
        log = self.redist()
        self.assertIn("Already in place: msvcr100.dll, msvcp100.dll, d3dx9_42.dll, D3DCompiler_42.dll", log)
        self.assertEqual(stamps, {f: os.stat(os.path.join(self.out, f)).st_mtime_ns for f in os.listdir(self.out)})

    def test_download_when_steam_has_none(self):
        log = self.redist()
        self.fx.expect_out(self, self.out)
        self.assertIn("Microsoft's vcredist_x86.exe", log)
        self.assertIn("Microsoft's directx_Jun2010_redist.exe", log)

    def test_only_the_missing_package_is_downloaded(self):
        self.fx.install_steam_redist(vc=True, dx=False)
        os.remove(os.path.join(self.fx.web, "vcredist_x86.exe"))
        self.redist()
        self.fx.expect_out(self, self.out)

    def test_wrong_dll_in_steam_falls_back_to_download(self):
        self.fx.install_steam_redist()
        bad = cab([("d3dx9_42.dll", b"another version")])
        write(os.path.join(self.fx.steam, lp_install.COMMON_REDIST, self.fx.cfg["steam_d3dx9_cab"]), bad)
        log = self.redist()
        self.fx.expect_out(self, self.out)
        self.assertIn("d3dx9_42.dll from Steam's Common Redistributables is not the expected version", log)

    def test_partial_download_continues(self):
        cache = os.path.join(self.out, ".cache")
        write(os.path.join(cache, "directx_Jun2010_redist.exe.part"), self.fx.dx_exe[:5000])
        write(os.path.join(cache, "vcredist_x86.exe.part"), self.fx.vc_exe)  # complete, not yet renamed
        self.redist()
        self.fx.expect_out(self, self.out)

    def test_bad_download_is_refused(self):
        self.fx.cfg["dxredist_sha256"] = "0" * 64
        with self.assertRaises(Failure):
            self.redist()
        self.assertFalse(os.path.exists(os.path.join(self.out, "d3dx9_42.dll")))
        self.assertFalse(os.path.exists(os.path.join(self.out, ".cache", "directx_Jun2010_redist.exe.part")))
        self.assertTrue(os.path.exists(os.path.join(self.out, "msvcr100.dll")))  # the other package is kept

    def test_no_download(self):
        with self.assertRaises(Failure) as e:
            self.redist(allow_download=False)
        self.assertIn("msvcr100.dll", str(e.exception))

    def test_earlier_download_is_used(self):
        write(os.path.join(self.out, ".cache", "vcredist_x86.exe"), self.fx.vc_exe)
        write(os.path.join(self.out, ".cache", "directx_Jun2010_redist.exe"), self.fx.dx_exe)
        self.fx.no_web()
        self.assertIn("an earlier download", self.redist())
        self.fx.expect_out(self, self.out)

    def test_from_folder_of_dlls(self):
        src = os.path.join(self.top, "pc", "SysWOW64")
        write(os.path.join(src, "MSVCR100.dll"), self.fx.dlls["msvcr100"])
        write(os.path.join(src, "msvcp100.dll"), self.fx.dlls["msvcp100"])
        write(os.path.join(src, "d3dx9_42.dll"), self.fx.dlls["d3dx9_42"])
        write(os.path.join(src, "D3DCompiler_42.dll"), self.fx.dlls["d3dcompiler_42"])
        self.fx.no_web()
        self.redist(extra=[os.path.join(self.top, "pc")])
        self.fx.expect_out(self, self.out)

    def test_from_steam_depots(self):
        content = os.path.join(self.fx.steam, "steamapps", "content", "app_228980")
        write(os.path.join(content, "depot_228983", "vcredist", "2010", "vcredist_x86.exe"), self.fx.vc_exe)
        write(os.path.join(content, "depot_228990", "DirectX", "Jun2010", "Aug2009_d3dx9_42_x86.cab"), self.fx.d3dx_cab)
        write(os.path.join(content, "depot_228990", "DirectX", "Jun2010", "Aug2009_D3DCompiler_42_x86.cab"), self.fx.dcomp_cab)
        self.fx.no_web()
        self.assertIn("Steam depot 228990", self.redist())
        self.fx.expect_out(self, self.out)

    def test_other_steam_library(self):
        lib = os.path.join(self.top, "External", "SteamLibrary")
        write(os.path.join(self.fx.steam, "steamapps", "libraryfolders.vdf"),
              ('"libraryfolders"\n{\n\t"0"\n\t{\n\t\t"path"\t\t"%s"\n\t}\n\t"1"\n\t{\n\t\t"path"\t\t"%s"\n\t}\n}\n'
               % (self.fx.steam, lib)).encode())
        redist = os.path.join(lib, lp_install.COMMON_REDIST)
        write(os.path.join(redist, self.fx.cfg["steam_vcredist"]), self.fx.vc_exe)
        write(os.path.join(redist, self.fx.cfg["steam_d3dx9_cab"]), self.fx.d3dx_cab)
        write(os.path.join(redist, self.fx.cfg["steam_d3dcompiler_cab"]), self.fx.dcomp_cab)
        self.fx.no_web()
        self.redist()
        self.fx.expect_out(self, self.out)

    def test_rebase_moves_the_pointer(self):
        self.assertEqual(struct.unpack_from("<I", self.fx.rebased, 0x200)[0], 0x11001000)
        self.assertEqual(struct.unpack_from("<I", self.fx.rebased, 0x58 + 28)[0], 0x11000000)


class TestGame(Base):
    def depot(self):
        return lp_install.depot_dir(self.fx.cfg, self.fx.steam)

    def test_command(self):
        self.fx.cfg["depot_manifest"] = ""
        self.assertEqual(lp_install.game_command(self.fx.cfg), "download_depot 221680 221683")
        self.fx.cfg["depot_manifest"] = "1234567890"
        self.assertEqual(lp_install.game_command(self.fx.cfg), "download_depot 221680 221683 1234567890")

    def test_depot_folders(self):
        cfg, steam = self.fx.cfg, self.fx.steam
        legacy = os.path.join(steam, "steamapps", "content", "app_221680", "depot_221683")
        bundle = os.path.join(steam, lp_install.APP_BUNDLE, "steamapps", "content", "app_221680", "depot_221683")
        self.assertEqual(lp_install.depot_dir(cfg, steam), legacy)  # no native client bundle
        os.makedirs(os.path.join(steam, lp_install.APP_BUNDLE))
        self.assertEqual(lp_install.depot_dir(cfg, steam), bundle)  # where the next download goes
        write(os.path.join(legacy, "Rocksmith2014.exe"), self.fx.game)  # an old download is still found
        self.assertEqual(lp_install.depot_dir(cfg, steam), legacy)
        self.assertEqual(lp_install.locate_default(cfg, steam).status, "ok")
        write(os.path.join(bundle, "Rocksmith2014.exe"), os.urandom(4096))
        loc = lp_install.locate_default(cfg, steam)
        self.assertEqual((loc.status, loc.folder), ("ok", legacy))

    def test_unfinished_download(self):
        cfg, steam = self.fx.cfg, self.fx.steam
        write(os.path.join(lp_install.depot_dir(cfg, steam), "Rocksmith2014.exe"), os.urandom(4096))
        self.assertEqual(lp_install.locate_default(cfg, steam).status, "different")
        write(os.path.join(steam, "logs", "console_log.previous.txt"),
              b"[2026-11-01 10:47:14] Downloading depot 221683 (103 files, 5363 MB) ...\n")
        write(os.path.join(steam, "logs", "content_log.txt"),
              b"[2026-11-01 10:48:24] Current download rate: 187.221 Mbps\n")
        loc = lp_install.locate_default(cfg, steam)
        self.assertEqual(loc.status, "unfinished")
        self.assertIn("has not finished", lp_install.describe(cfg, loc))
        with open(os.path.join(steam, "logs", "console_log.txt"), "a") as f:
            f.write("[2026-11-01 11:00:00] Depot download complete : depot 221683 (103 files, manifest 1)\n")
        self.assertEqual(lp_install.locate_default(cfg, steam).status, "different")

    def test_a_finished_exe_in_an_unfinished_download_does_not_count(self):
        cfg, steam = self.fx.cfg, self.fx.steam
        write(os.path.join(self.depot(), "Rocksmith2014.exe"), self.fx.game)
        self.assertEqual(lp_install.locate_default(cfg, steam).status, "ok")
        write(os.path.join(steam, "logs", "console_log.txt"), b"Downloading depot 221683 (103 files, 5363 MB) ...\n")
        self.assertEqual(lp_install.locate_default(cfg, steam).status, "unfinished")
        write(os.path.join(steam, "logs", "console_log.txt"), b"")
        cfg["depot_disk_bytes"] = 1 << 20  # Steam's log says nothing, but the folder is short of bytes
        self.assertEqual(lp_install.locate_default(cfg, steam).status, "unfinished")

    def test_space_and_redirect(self):
        cfg, steam = self.fx.cfg, self.fx.steam
        os.makedirs(os.path.join(steam, lp_install.APP_BUNDLE))
        free, need, where = lp_install.space(cfg, steam)
        self.assertEqual(need, cfg["depot_disk_bytes"] + cfg["disk_margin_bytes"] + cfg["dlc_disk_bytes"])
        # With the account's list of DLC on disk, room for what it owns.
        with mock.patch.object(lp_install.gamefiles, "steam_connection", lambda s: ("Logged On", 43)), \
                mock.patch.object(lp_install.gamefiles, "account_dlc", lambda s, a: [1, 2]):
            self.assertEqual(lp_install.dlc_room(cfg, steam), 2 * cfg["dlc_average_bytes"])
        self.assertGreater(free, 0)
        content = os.path.join(steam, lp_install.APP_BUNDLE, "steamapps", "content")
        other = os.path.join(self.top, "External", "steam-content")
        self.assertEqual(lp_install.redirect(steam, other), content)
        self.assertEqual(os.path.realpath(content), os.path.realpath(other))
        self.assertEqual(lp_install.space(cfg, steam)[2], os.path.realpath(other))
        self.assertEqual(lp_install.redirect(steam, other), content)  # again: kept
        write(os.path.join(lp_install.depot_dir(cfg, steam), "Rocksmith2014.exe"), self.fx.game)
        self.assertTrue(os.path.isfile(os.path.join(other, "app_221680", "depot_221683", "Rocksmith2014.exe")))
        os.remove(content)  # a Steam update put a real folder back, with something in it: it moves along
        write(os.path.join(content, "app_1", "x"), b"x")
        lp_install.redirect(steam, other)
        self.assertEqual(read(os.path.join(content, "app_1", "x")), b"x")
        self.assertEqual(read(os.path.join(other, "app_1", "x")), b"x")
        os.remove(content)  # the same thing on both sides: not moved, and the message says what to do
        write(os.path.join(content, "app_1", "x"), b"y")
        with self.assertRaises(Failure):
            lp_install.redirect(steam, other)

    def test_the_startup_disk_keeps_room_for_everything_else(self):
        # The case that filled a disk: no link, 6.5 GB free, a 5.5 GB depot, the build on another disk.
        cfg, steam = self.fx.cfg, self.fx.steam
        cfg.update(depot_disk_bytes=5857456194, build_peak_memory_bytes=23 << 30, startup_margin_bytes=2 << 30,
                   disk_margin_bytes=512 << 20, dlc_disk_bytes=0)
        gb = 1 << 30
        with mock.patch.object(lp_install, "on_startup_disk", lambda p: True), \
                mock.patch.object(lp_install, "memory_bytes", lambda: 16 * gb), \
                mock.patch.object(lp_install, "free_bytes", lambda p: int(6.5 * gb)):
            free, need, _ = lp_install.space(cfg, steam)
            self.assertLess(free, need)
            self.assertAlmostEqual(need / gb, 5.46 + 2 + 7, delta=0.1)
        with mock.patch.object(lp_install, "on_startup_disk", lambda p: False), \
                mock.patch.object(lp_install, "free_bytes", lambda p: int(6.5 * gb)):
            free, need, _ = lp_install.space(cfg, steam)
            self.assertGreater(free, need)  # another disk needs only the download

    def test_startup_space(self):
        cfg = self.fx.cfg
        cfg.update(build_peak_memory_bytes=23 << 30, startup_margin_bytes=2 << 30, build_disk_bytes=2 << 30)
        gb = 1 << 30
        work = os.path.join(self.top, "build")
        with mock.patch.object(lp_install, "on_startup_disk", lambda p: False), \
                mock.patch.object(lp_install, "memory_bytes", lambda: 8 * gb), \
                mock.patch.object(lp_install, "free_bytes", lambda p: 10 * gb):
            free, need, words = lp_install.startup_space(cfg, work)
            self.assertEqual(need, 17 * gb)
            self.assertIn("memory", words)
            self.assertIn("memory", lp_install.build_estimate(cfg))
        with mock.patch.object(lp_install, "on_startup_disk", lambda p: True), \
                mock.patch.object(lp_install, "memory_bytes", lambda: 64 * gb), \
                mock.patch.object(lp_install, "free_bytes", lambda p: 10 * gb):
            free, need, words = lp_install.startup_space(cfg, work)
            self.assertEqual(need, 4 * gb)
            self.assertNotIn("memory", words)

    def test_a_whole_disk_gets_a_folder_of_its_own(self):
        cfg, steam = self.fx.cfg, self.fx.steam
        os.makedirs(os.path.join(steam, lp_install.APP_BUNDLE))
        disk = os.path.join(self.top, "Disk")
        with mock.patch.object(os.path, "ismount", lambda p: p == disk):
            content = lp_install.redirect(steam, disk)
        self.assertEqual(os.path.realpath(content),
                         os.path.realpath(os.path.join(disk, "RS Re-ARMed", "steam-content")))

    def test_a_dropped_link_comes_back(self):
        cfg, steam = self.fx.cfg, self.fx.steam
        os.makedirs(os.path.join(steam, lp_install.APP_BUNDLE))
        content = os.path.join(steam, lp_install.APP_BUNDLE, "steamapps", "content")
        other = os.path.join(self.top, "External", "steam-content")
        lp_install.redirect(steam, other)
        write(os.path.join(lp_install.depot_dir(cfg, steam), "Rocksmith2014.exe"), self.fx.game)
        os.remove(content)  # a Steam update
        self.assertEqual(lp_install.locate_default(cfg, steam).status, "missing")
        lp_install.restore_redirect(steam)
        self.assertEqual(lp_install.locate_default(cfg, steam).status, "ok")
        # A new real folder of Steam's stays as it is.
        os.remove(content)
        os.makedirs(content)
        lp_install.restore_redirect(steam)
        self.assertFalse(os.path.islink(content))

    def test_the_mac_editions_folder_is_this_ones_too(self):
        # Steam has one download folder for both editions: a folder the Mac install chose comes back here as well.
        cfg, steam = self.fx.cfg, self.fx.steam
        os.makedirs(os.path.join(steam, lp_install.APP_BUNDLE))
        content = os.path.join(steam, lp_install.APP_BUNDLE, "steamapps", "content")
        other = os.path.join(self.top, "External", "steam-content")
        mine, macs = lp_install.download_notes()
        self.assertEqual(os.path.dirname(macs), os.path.dirname(os.path.dirname(mine)))
        gamefiles.redirect(steam, other, macs)
        os.remove(content)
        lp_install.restore_redirect(steam)
        self.assertEqual(os.path.realpath(content), os.path.realpath(other))

    def test_a_link_to_a_drive_that_is_gone_has_no_room(self):
        cfg, steam = self.fx.cfg, self.fx.steam
        os.makedirs(os.path.join(steam, lp_install.APP_BUNDLE, "steamapps"))
        content = os.path.join(steam, lp_install.APP_BUNDLE, "steamapps", "content")
        os.symlink(os.path.join(self.top, "Gone", "steam-content"), content)
        free, need, where = lp_install.space(cfg, steam)
        self.assertEqual(free, 0)
        self.assertIn("isn't connected", lp_install.describe_space(free, need, where))

    def test_the_build_counts_on_the_same_disk(self):
        cfg, steam = self.fx.cfg, self.fx.steam
        alone = lp_install.space(cfg, steam)[1]
        both = lp_install.space(cfg, steam, os.path.join(self.top, "build", "lp"))[1]
        self.assertEqual(both, alone + cfg["build_disk_bytes"])

    def test_locate(self):
        cfg, depot = self.fx.cfg, self.depot()
        self.assertEqual(lp_install.locate(cfg, depot).status, "missing")
        write(os.path.join(depot, "Rocksmith2014.exe"), self.fx.game[:100])
        self.assertEqual(lp_install.locate(cfg, depot).status, "incomplete")
        write(os.path.join(depot, "Rocksmith2014.exe"), os.urandom(4096))
        loc = lp_install.locate(cfg, depot)
        self.assertEqual(loc.status, "different")
        self.assertIn("different build", lp_install.describe(cfg, loc))
        write(os.path.join(depot, "Rocksmith2014.exe"), self.fx.game)
        self.assertEqual(lp_install.locate(cfg, depot).status, "ok")

    def test_locate_copied_folder(self):
        pc = os.path.join(self.top, "from-pc")
        write(os.path.join(pc, "Rocksmith2014", "ROCKSMITH2014.EXE"), self.fx.game)
        loc = lp_install.locate(self.fx.cfg, pc)
        self.assertEqual(loc.status, "ok")
        self.assertEqual(loc.folder, os.path.join(pc, "Rocksmith2014"))

    def test_cli(self):
        cfgfile = os.path.join(self.top, "target.toml")
        with open(cfgfile, "w") as f:
            for k, v in self.fx.cfg.items():
                f.write('%s = %s\n' % (k, '"%s"' % v if isinstance(v, str) else v))
        write(os.path.join(self.depot(), "Rocksmith2014.exe"), self.fx.game)
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            rc = lp_install.main(["--config", cfgfile, "--steam", self.fx.steam, "game-locate", "--path-only"])
        self.assertEqual((rc, out.getvalue().strip()), (0, self.depot()))
        write(os.path.join(self.depot(), "Rocksmith2014.exe"), os.urandom(4096))
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(lp_install.main(["--config", cfgfile, "--steam", self.fx.steam, "game-locate"]), 2)


class TestGameCopy(Base):
    """lp_install.py game-copy / game-refresh: the app's own copy of the game files (gamefiles.py)."""

    def setUp(self):
        Base.setUp(self)
        self.depot = os.path.join(self.top, "depot")
        write(os.path.join(self.depot, "Rocksmith2014.exe"), self.fx.game)
        for n in ("static.psarc", "songs.psarc", "bink2w32.dll"):
            write(os.path.join(self.depot, n), n.encode())
        # the Mac game in Steam's library, with both kinds of DLC archives, and its appmanifest
        self.mac = os.path.join(self.fx.steam, "steamapps", "common", "Rocksmith2014")
        write(os.path.join(self.mac, "dlc", "song_p.psarc"), b"pc")
        write(os.path.join(self.mac, "dlc", "song_m.psarc"), b"mac")
        write(os.path.join(self.fx.steam, "steamapps", "appmanifest_221680.acf"),
              b'"AppState" { "InstalledDepots" { "258341" { "dlcappid" "258341" } } "language" "english" }')
        self.dest = os.path.join(self.top, "home", "learn-play", "game")
        os.environ.pop("RS_GAME_DIR", None)

    def copy(self, game=None, **kw):
        return lp_install.game_copy(self.fx.cfg, self.fx.steam, game or self.depot, self.dest, progress=quiet(),
                                    is_running=lambda e: False, **kw)

    def test_copy_and_refresh(self):
        self.assertEqual(lp_install.mac_dlc_folders(self.fx.steam), [os.path.join(self.mac, "dlc")])
        self.assertEqual(self.copy(), self.dest)
        self.assertEqual(read(os.path.join(self.dest, "Rocksmith2014.exe")), self.fx.game)
        self.assertEqual(read(os.path.join(self.dest, "dlc", "song_p.psarc")), b"pc")
        self.assertFalse(os.path.exists(os.path.join(self.dest, "dlc", "song_m.psarc")))
        self.assertTrue(lp_install.gamefiles.check(self.dest)[0])
        self.assertEqual(read(os.path.join(self.dest, ".rearmed", "steam.env")).splitlines()[1:],
                         [b"RS_STEAM_DLC=248750,258341", b"RS_STEAM_LANGUAGE=english"])
        # Steam clears its download folder, a DLC arrives: the copy keeps the game and the DLC still reaches it
        write(os.path.join(self.mac, "dlc", "new_p.psarc"), b"new")
        shutil.rmtree(self.depot)
        lp_install.gamefiles.refresh(self.dest, quiet(), is_running=lambda e: False)
        self.assertEqual(read(os.path.join(self.dest, "dlc", "new_p.psarc")), b"new")
        self.assertEqual(read(os.path.join(self.dest, "Rocksmith2014.exe")), self.fx.game)
        self.assertTrue(lp_install.gamefiles.check(self.dest)[0])

    def test_refresh_command(self):
        self.copy()
        write(os.path.join(self.mac, "dlc", "new_p.psarc"), b"new")
        os.remove(os.path.join(self.mac, "dlc", "song_p.psarc"))
        # as if no game ran: a Learn & Play game running on this Mac would make the refresh refuse
        refresh = functools.partial(lp_install.gamefiles.refresh, is_running=lambda e: False)
        with contextlib.redirect_stdout(io.StringIO()), mock.patch.object(lp_install.gamefiles, "refresh", refresh):
            self.assertEqual(lp_install.main(["--steam", self.fx.steam, "game-refresh", "--dest", self.dest]), 0)
        self.assertEqual(read(os.path.join(self.dest, "dlc", "new_p.psarc")), b"new")
        self.assertFalse(os.path.exists(os.path.join(self.dest, "dlc", "song_p.psarc")))

    def test_only_the_supported_build(self):
        write(os.path.join(self.depot, "Rocksmith2014.exe"), os.urandom(4096))
        with self.assertRaises(Failure) as e:
            self.copy()
        self.assertEqual(e.exception.code, 2)
        self.assertFalse(os.path.exists(self.dest))

    def test_app_install_points_at_the_copy(self):
        self.copy()
        home = os.path.dirname(self.dest)
        work = os.path.join(self.top, "work")
        for f in (lp_build.EXE, "libRSOpenGL.dylib", "lp_game.exe"):
            write(os.path.join(work, "out", f), b"x")
        os.makedirs(os.path.join(work, "out", "dlls"))
        lp_build.install_app(work, self.dest, os.path.join(self.top, "A.app"), home, run=FakeRun())
        self.assertEqual(read(os.path.join(home, "game-dir")), (self.dest + "\n").encode())
        self.assertTrue(os.path.isfile(os.path.join(home, "gamefiles.py")))
        self.assertTrue(os.path.isfile(os.path.join(home, "cache_overlay.py")))
        self.assertTrue(os.path.isfile(os.path.join(home, "psarc_key.py")))


class TestTitleLogo(unittest.TestCase):
    """lp_install.title_logo: static.psarc.rearmed in the copy, from tools/loadscreen."""

    def setUp(self):
        self.game = tempfile.mkdtemp(prefix="lp-logo-test-")
        self.addCleanup(shutil.rmtree, self.game, True)
        write(os.path.join(self.game, "static.psarc.original"), b"static")
        self.out = os.path.join(self.game, "static.psarc.rearmed")
        self.said = []
        env = mock.patch.dict(os.environ)
        env.start()
        self.addCleanup(env.stop)
        os.environ.pop("RS_REARMED", None)

    def run_with(self, code=0):
        r = subprocess.CompletedProcess([], code, "pip: no network\n")
        with mock.patch.object(lp_install.subprocess, "run", return_value=r) as run:
            lp_install.title_logo(self.game, self.said.append)
        return run

    def test_made_from_the_copy(self):
        run = self.run_with()
        args, kw = run.call_args
        self.assertEqual(args[0], ["bash", lp_install.LOADSCREEN, self.out])
        self.assertTrue(os.path.isfile(lp_install.LOADSCREEN))
        self.assertEqual(kw["env"]["RS_GAME_DIR"], self.game)

    def test_not_made(self):
        os.environ["RS_REARMED"] = "0"
        self.run_with().assert_not_called()
        del os.environ["RS_REARMED"]
        write(self.out, b"tagged")
        self.run_with().assert_not_called()
        os.unlink(self.out)
        os.unlink(os.path.join(self.game, "static.psarc.original"))  # not a copy: a folder of Steam's
        self.run_with().assert_not_called()

    def test_failure_does_not_stop_the_install(self):
        self.run_with(1)
        self.assertIn("pip: no network", self.said[-1])


class FakeClock:
    def __init__(self):
        self.t = 0.0
        self.hooks = []

    def __call__(self):
        return self.t

    def sleep(self, s):
        self.t += s
        while self.hooks and self.hooks[0][0] <= self.t:
            self.hooks.pop(0)[1]()


class WaitBase(Base):
    def setUp(self):
        Base.setUp(self)
        self.depot = lp_install.depot_dir(self.fx.cfg, self.fx.steam)
        self.console = os.path.join(self.fx.steam, "logs", "console_log.txt")
        write(self.console, b"[2026-01-01 10:00:00] old line\n"
              b'[2026-01-01 10:00:01] Depot download complete : "%s" (3 files, manifest 111)\n' % self.depot.encode())
        self.clock = FakeClock()
        self.progress = Progress(lines=True, stream=io.StringIO())

    def log(self, line):
        with open(self.console, "a") as f:
            f.write("[2026-11-01 10:00:00] %s\n" % line)

    def wait(self, **kw):
        return lp_install.wait(self.fx.cfg, self.depot, self.fx.steam, self.progress, interval=1,
                               clock=self.clock, sleep=self.clock.sleep, **kw)

    def at(self, t, fn):
        self.clock.hooks.append((t, fn))


class TestWait(WaitBase):
    def test_steam_log(self):
        self.fx.cfg["depot_disk_bytes"] = 1 << 33
        content = os.path.join(self.fx.steam, "logs", "content_log.txt")

        def clog(line):
            with open(content, "a") as f:
                f.write(line + "\n")
        self.at(2, lambda: clog("[2026-11-01 10:00:00] Downloading depot 221683 (103 files, 10 MB)"))
        self.at(3, lambda: write(os.path.join(self.depot, "data", "a.psarc"), b"x" * (512 << 10)))
        self.at(3, lambda: clog("[2026-11-01 10:00:01] Current download rate: 8.389 Mbps"))
        self.at(4, lambda: clog("[2026-11-01 10:00:06] Current download rate: 8.389 Mbps"))
        self.at(4, lambda: write(os.path.join(self.depot, "Rocksmith2014.exe"), self.fx.game))
        self.at(6, lambda: self.log('Depot download complete : "%s" (2 files, manifest 4242)' % self.depot))
        loc = self.wait(timeout=100)
        self.assertEqual(loc.status, "ok")
        out = self.progress.stream.getvalue()
        self.assertIn("progress 0.0000 Downloading the Windows game… waiting for Steam to start", out)
        self.assertIn("Downloading the Windows game… 5.0 MB of 10.0 MB (1.0 MB/s)", out)
        self.assertIn("manifest 4242", out)
        self.assertTrue(out.rstrip().endswith("progress 1.0000 Downloading the Windows game… done"))
        fractions = [float(ln.split()[1]) for ln in out.splitlines() if ln.startswith("progress ")]
        self.assertEqual(fractions, sorted(fractions))
        self.assertLess(self.clock.t, 10)

    def test_old_log_line_is_ignored(self):
        with self.assertRaises(Failure) as e:
            self.wait(timeout=5)
        self.assertEqual(e.exception.code, 3)

    def test_native_client_folder(self):
        os.makedirs(os.path.join(self.fx.steam, lp_install.APP_BUNDLE))
        bundle = lp_install.depot_dir(self.fx.cfg, self.fx.steam)
        self.assertIn("Steam.AppBundle", bundle)
        self.at(2, lambda: write(os.path.join(bundle, "Rocksmith2014.exe"), self.fx.game))
        self.at(3, lambda: self.log("Depot download complete : depot 221683 done"))
        loc = lp_install.wait(self.fx.cfg, None, self.fx.steam, self.progress, timeout=100, interval=1,
                              clock=self.clock, sleep=self.clock.sleep)
        self.assertEqual((loc.status, loc.folder), ("ok", bundle))

    def test_real_console_lines(self):
        # As the native client writes them: mixed slashes, no file count.
        os.makedirs(os.path.join(self.fx.steam, lp_install.APP_BUNDLE))
        bundle = lp_install.depot_dir(self.fx.cfg, self.fx.steam)
        self.at(1, lambda: self.log("Downloading depot 221683 (103 files, 5363 MB) ..."))
        self.at(2, lambda: write(os.path.join(bundle, "Rocksmith2014.exe"), self.fx.game))
        self.at(3, lambda: self.log('Depot download complete : "%s\\\\steamapps\\\\content\\\\app_221680\\\\depot_221683" '
                                    '(manifest 8918188356915973501)'
                                    % os.path.join(self.fx.steam, lp_install.APP_BUNDLE)))
        loc = lp_install.wait(self.fx.cfg, None, self.fx.steam, self.progress, timeout=100, interval=1,
                              clock=self.clock, sleep=self.clock.sleep)
        self.assertEqual(loc.status, "ok")
        self.assertIn("Steam finished the download (manifest 8918188356915973501).", self.progress.stream.getvalue())

    def test_no_subscription(self):
        self.at(2, lambda: self.log("Depot download failed : missing license for depot 221683 (No subscription)"))
        with self.assertRaises(Failure) as e:
            self.wait(timeout=100)
        self.assertIn("No subscription", str(e.exception))

    def test_settles_without_log_or_pins(self):
        self.at(2, lambda: write(os.path.join(self.depot, "Rocksmith2014.exe"), self.fx.game))
        self.assertEqual(self.wait(timeout=1000, settle=30).status, "ok")
        self.assertGreaterEqual(self.clock.t, 32)

    def test_different_build(self):
        self.at(2, lambda: write(os.path.join(self.depot, "Rocksmith2014.exe"), os.urandom(4096)))
        self.at(3, lambda: self.log('Depot download complete : "%s" (1 files, manifest 5)' % self.depot))
        with self.assertRaises(Failure) as e:
            self.wait(timeout=100)
        self.assertEqual(e.exception.code, 2)

    def test_failure(self):
        self.at(2, lambda: self.log("Depot download failed : Access Denied for depot 221683"))
        with self.assertRaises(Failure) as e:
            self.wait(timeout=100)
        self.assertIn("Access Denied", str(e.exception))

    def test_steams_real_failure_line(self):
        # Steam's line names no depot.
        self.at(2, lambda: self.log("Depot download failed : missing license for depot (No subscription) "))
        with self.assertRaises(Failure) as e:
            self.wait(timeout=100)
        self.assertIn("missing license", str(e.exception))

    def test_a_stalled_download_is_not_a_different_build(self):
        # Steam makes the exe at its full size first: with Steam's download unfinished, it only waits.
        self.at(1, lambda: self.log("Console Log Start"))
        self.at(1, lambda: self.log("Downloading depot 221683 (103 files, 10 MB) ..."))
        self.at(2, lambda: write(os.path.join(self.depot, "Rocksmith2014.exe"), os.urandom(4096)))
        with self.assertRaises(Failure) as e:
            self.wait(timeout=200, settle=30)
        self.assertEqual(e.exception.code, 3)

    def test_steam_restart_new_log(self):
        def restart():
            write(self.console, b"")
            self.log('Depot download complete : "%s" (1 files, manifest 9)' % self.depot)
        self.at(1, lambda: write(os.path.join(self.depot, "Rocksmith2014.exe"), self.fx.game))
        self.at(2, restart)
        self.assertEqual(self.wait(timeout=100).status, "ok")


class FakeSteam:
    """A Steam that is what the test says it is, and notes what it is asked."""

    def __init__(self, **kw):
        self.st = gamefiles.SteamState(True, True, True, 1234, "first-account")
        self.sends = self.opened = self.pastes = 0

    def state(self, steam):
        return self.st

    def open(self, url=None):
        self.opened += 1

    def send(self):
        self.sends += 1

    def paste(self):
        self.pastes += 1


class TestDownload(WaitBase):
    """game-download: the command goes to the running Steam, and only what only the user can do is left."""

    def setUp(self):
        WaitBase.setUp(self)
        self.steam = FakeSteam()
        self.stream = io.StringIO()
        self.guide = gamefiles.Guide(True, self.stream)
        self.driver = self.make()

    def make(self):
        gate = gamefiles.SteamGate(self.fx.steam, gamefiles.GAME_LP, self.guide, state=self.steam.state,
                                   opener=self.steam.open, clock=self.clock)
        return lp_install.DepotDownload(self.fx.cfg, self.fx.steam, self.guide, gate=gate, send=self.steam.send,
                                        paste=self.steam.paste, clock=self.clock, running=lambda *a: False)

    def download(self, **kw):
        kw.setdefault("timeout", 2000)
        return lp_install.wait(self.fx.cfg, self.depot, self.fx.steam, self.progress, interval=1, clock=self.clock,
                               sleep=self.clock.sleep, driver=self.driver, **kw)

    def codes(self):
        return [line.split("\t")[1] for line in self.stream.getvalue().splitlines()]

    def done(self, after=0):
        write(os.path.join(self.depot, "Rocksmith2014.exe"), self.fx.game)
        self.log('Depot download complete : "%s" (manifest 1)' % self.depot)

    def test_nothing_to_do_for_the_user(self):
        self.at(1, lambda: self.log("Downloading depot 221683 (103 files, 10 MB) ..."))
        self.at(5, self.done)
        self.assertEqual(self.download().status, "ok")
        self.assertEqual(self.steam.sends, 1)
        self.assertEqual(self.steam.pastes, 0)
        self.assertEqual(self.codes(), ["downloading"])

    def test_steam_is_not_installed_then_it_is(self):
        self.steam.st = gamefiles.SteamState(False, False, False)

        def installed():
            self.steam.st = gamefiles.SteamState(True, True, True, 1234, "x")
        self.at(5, installed)
        self.at(7, lambda: self.log("Downloading depot 221683 (103 files, 10 MB) ..."))
        self.at(9, self.done)
        self.download()
        self.assertEqual(self.codes(), ["steam-missing", "downloading"])

    def test_steam_is_opened_and_signed_in_later(self):
        self.steam.st = gamefiles.SteamState(True, False, False)

        def opened():
            self.steam.st = gamefiles.SteamState(True, True, False)

        def signed_in():
            self.steam.st = gamefiles.SteamState(True, True, True, 1234, "x")
        self.at(5, opened)
        self.at(100, signed_in)
        self.at(102, lambda: self.log("Downloading depot 221683 (103 files, 10 MB) ..."))
        self.at(104, self.done)
        self.download()
        self.assertEqual(self.steam.opened, 1)
        self.assertEqual(self.steam.sends, 1)
        self.assertEqual(self.codes(), ["steam-starting", "sign-in", "downloading"])

    def test_wrong_account_then_the_right_one(self):
        self.at(2, lambda: self.log("Depot download failed : missing license for depot (No subscription)"))

        def switch():
            self.steam.st = gamefiles.SteamState(True, True, True, 5678, "second-account")
        self.at(20, switch)
        self.at(22, lambda: self.log("Downloading depot 221683 (103 files, 10 MB) ..."))
        self.at(25, self.done)
        self.download()
        self.assertEqual(self.steam.sends, 2)
        self.assertEqual(self.codes(), ["downloading", "wrong-account", "downloading"])
        self.assertIn("first-account", self.stream.getvalue())

    def test_the_wrong_account_again_waits(self):
        self.at(2, lambda: self.log("Depot download failed : missing license for depot (No subscription)"))
        with self.assertRaises(Failure) as e:
            self.download(timeout=100)
        self.assertEqual(e.exception.code, 3)
        self.assertEqual(self.steam.sends, 1)

    def test_steam_quits_during_the_download(self):
        self.at(2, lambda: self.log("Downloading depot 221683 (103 files, 10 MB) ..."))

        def quit():
            self.steam.st = gamefiles.SteamState(True, False, False)

        def back():
            self.steam.st = gamefiles.SteamState(True, True, True, 1234, "x")
        self.at(5, quit)
        self.at(30, back)
        self.at(32, lambda: self.log("Downloading depot 221683 (103 files, 10 MB) ..."))
        self.at(40, self.done)
        self.download()
        self.assertEqual(self.steam.opened, 1)
        self.assertEqual(self.steam.sends, 2)

    def test_steam_ignores_the_command(self):
        self.at(60, lambda: self.log("Downloading depot 221683 (103 files, 10 MB) ..."))
        self.at(65, self.done)
        self.download()
        self.assertEqual(self.steam.pastes, 1)
        self.assertEqual(self.steam.sends, 1)
        self.assertIn("paste", self.codes())
        self.assertIn("download_depot 221680 221683", self.stream.getvalue())

    def test_try_again_at_the_console(self):
        self.at(40, self.driver.try_again)
        self.at(60, lambda: self.log("Downloading depot 221683 (103 files, 10 MB) ..."))
        self.at(65, self.done)
        self.download()
        self.assertEqual(self.steam.pastes, 2)

    def test_a_running_download_gets_no_second_command(self):
        self.driver.running = lambda *a: True
        write(os.path.join(self.depot, "part"), b"x")
        for t in range(5, 50, 5):
            self.at(t, lambda t=t: write(os.path.join(self.depot, "part"), b"x" * (t * 1000)))
        self.at(50, self.done)
        self.download()
        self.assertEqual(self.steam.sends, 0)

    def test_a_dead_download_gets_a_new_command(self):
        self.driver.running = lambda *a: self.steam.sends == 0
        self.at(70, lambda: self.log("Downloading depot 221683 (103 files, 10 MB) ..."))
        self.at(75, self.done)
        self.download()
        self.assertEqual(self.steam.sends, 1)

    def test_a_stalled_download_says_so_and_asks_again(self):
        self.at(2, lambda: self.log("Downloading depot 221683 (103 files, 10 MB) ..."))
        self.at(700, lambda: self.log("Downloading depot 221683 (103 files, 10 MB) ..."))
        self.at(702, self.done)
        self.download()
        self.assertIn("download-stalled", self.codes())
        self.assertEqual(self.steam.sends, 2)

    def test_steams_failures_are_retried_then_reported(self):
        for t in (2, 40, 80, 120):
            self.at(t, lambda: self.log("Depot download failed : Failed to get manifest (Timeout)"))
        with self.assertRaises(Failure) as e:
            self.download()
        self.assertIn("Failed to get manifest", str(e.exception))
        self.assertEqual(self.steam.sends, 4)
        self.assertIn("download-failed", self.codes())

    def test_no_room_goes_back_to_the_question(self):
        real = lp_install.space
        with mock.patch.object(lp_install, "space", lambda *a: (1, 2, "/x")):
            with self.assertRaises(Failure) as e:
                self.download()
        self.assertEqual(e.exception.code, 4)
        self.assertEqual(self.steam.sends, 0)
        self.assertIs(lp_install.space, real)

    def test_download_running_reads_this_session(self):
        lines = ["Downloading depot 221683 (103 files, 5363 MB) ...", "Console Log Start"]
        for line in lines:
            self.log(line)
        self.assertFalse(lp_install.download_running(self.fx.steam, 221683))
        self.log("Downloading depot 221683 (103 files, 5363 MB) ...")
        self.assertTrue(lp_install.download_running(self.fx.steam, 221683))
        self.log('Depot download complete : "x" (manifest 1)')
        self.assertFalse(lp_install.download_running(self.fx.steam, 221683))


class TestImagePin(Base):
    def setUp(self):
        Base.setUp(self)
        self.image = os.path.join(self.top, "image.exe")
        write(self.image, b"the image")
        self.cfg = dict(self.fx.cfg, image_sha256=hashlib.sha256(b"the image").hexdigest())

    def build(self, cfg, unpinned=False):
        a = argparse.Namespace(work=os.path.join(self.top, "work"), image=self.image, dlls=self.top, game=self.top,
                               py=sys.executable, jobs=0, unpinned=unpinned)
        with mock.patch.object(lp_install.lp_build, "Builder") as builder, \
                mock.patch.object(lp_install, "title_logo"):
            builder.return_value.run.return_value = 60
            try:
                lp_install.build(cfg, a, quiet())
            except Failure as e:
                return builder, str(e)
        return builder, None

    def test_the_pinned_image_builds(self):
        self.assertIsNone(lp_install.image_problem(self.cfg, self.image))
        builder, failure = self.build(self.cfg)
        self.assertIsNone(failure)
        builder.assert_called_once()

    def test_another_image_stops_before_the_build(self):
        write(self.image, b"a capture with pages missing")
        self.assertIn("not the image", lp_install.image_problem(self.cfg, self.image))
        builder, failure = self.build(self.cfg)
        self.assertIn("--unpinned", failure)
        builder.assert_not_called()

    def test_a_capture_says_so(self):
        write(self.image, b"a capture")
        builder, failure = self.build(self.cfg, unpinned=True)
        self.assertIsNone(failure)
        builder.assert_called_once()

    def test_no_image_is_a_problem(self):
        self.assertIn("no game image", lp_install.image_problem(self.cfg, self.image + ".gone"))


class TestSeed(Base):
    def setUp(self):
        Base.setUp(self)
        self.game = os.path.join(self.top, "game")
        write(os.path.join(self.game, "Rocksmith2014.exe"), self.fx.game)
        self.seed = os.path.join(self.top, "seed.bin")
        write(self.seed, b"seed")
        self.out = os.path.join(self.top, "image", "Rocksmith2014.exe")

    def test_found_in_order(self):
        with mock.patch.dict(os.environ, {"LP_SEED": ""}), mock.patch.object(lp_install, "UNPACK_DIR", self.top):
            self.assertEqual(lp_install.seed_file(), self.seed)
            self.assertIsNone(lp_install.seed_file(os.path.join(self.top, "gone")) and None)
        other = os.path.join(self.top, "other.bin")
        write(other, b"s")
        with mock.patch.dict(os.environ, {"LP_SEED": other}):
            self.assertEqual(lp_install.seed_file(), other)

    def test_absent(self):
        with mock.patch.dict(os.environ, {"LP_SEED": ""}), \
                mock.patch.object(lp_install, "UNPACK_DIR", os.path.join(self.top, "nowhere")):
            self.assertIsNone(lp_install.seed_file())
            with self.assertRaises(Failure) as e:
                lp_install.game_image(self.fx.cfg, self.game, self.out, sys.executable, progress=quiet())
            self.assertIn("capture", str(e.exception))

    def test_tools_run_in_order_and_leave_only_the_image(self):
        calls = []

        def tool(py, script, *args):
            calls.append(os.path.basename(script))
            if script.endswith("lp_unpack.py"):
                write(args[-1], b"flat")
                write(args[-1] + ".relocs", b"r")
            else:
                self.assertEqual(read(args[0]), b"flat")
                write(args[1], b"image")
        with mock.patch.object(lp_install, "tool", tool):
            out = lp_install.game_image(self.fx.cfg, self.game, self.out, sys.executable, self.seed, progress=quiet())
        self.assertEqual(calls, ["lp_unpack.py", "lp_image.py"])
        self.assertEqual(os.listdir(os.path.dirname(out)), ["Rocksmith2014.exe"])

    def test_a_failed_tool_leaves_nothing_half_done(self):
        def tool(py, script, *args):
            write(args[-1], b"half")
            raise Failure("no")
        with mock.patch.object(lp_install, "tool", tool), self.assertRaises(Failure):
            lp_install.game_image(self.fx.cfg, self.game, self.out, sys.executable, self.seed, progress=quiet())
        self.assertEqual(os.listdir(os.path.dirname(self.out)), [])

    def test_the_wrong_game_is_refused(self):
        write(os.path.join(self.game, "Rocksmith2014.exe"), os.urandom(4096))
        with self.assertRaises(Failure) as e:
            lp_install.game_image(self.fx.cfg, self.game, self.out, sys.executable, self.seed, progress=quiet())
        self.assertEqual(e.exception.code, 2)


class TestConfig(unittest.TestCase):
    def test_target_toml(self):
        cfg = lp_install.load_config()
        for k in ("steam_app", "steam_depot", "depot_manifest", "exe_size", "exe_sha256", "vcredist_url",
                  "dxredist_url", "d3dcompiler_42_base", "d3dcompiler_42_rebased_sha256"):
            self.assertIn(k, cfg)
        for name, key, _, _ in lp_install.DLLS:
            self.assertEqual(len(cfg[key + "_sha256"]), 64)
        self.assertTrue(cfg["vcredist_url"].startswith("https://download.microsoft.com/"))
        self.assertTrue(cfg["dxredist_url"].startswith("https://download.microsoft.com/"))


# --- build, app-install, build-clean (lp_build.py) ----------------------------------------------------------
# A fake translator (writes two TUs and a build.ninja like gen_image.py's), a fake compiler (copies the
# source, writes a depfile) and a fake runner for build_gl.sh / build.sh stand in for the slow parts.

FAKE_TRANSLATOR = r'''
import os, sys
src, gen = sys.argv[1], sys.argv[2]
args = sys.argv[3:]
obj = args[args.index("--obj-dir") + 1]
with open(os.environ["FAKE_LOG"], "a") as f:
    f.write(os.path.basename(src) + " " + " ".join(a for a in args if a.startswith("--module") or a == "--wrappers") + "\n")
if os.environ.get("FAKE_FAIL") == os.path.basename(src):
    sys.exit(3)
os.makedirs(gen, exist_ok=True)
with open(src, "rb") as f:
    n = len(f.read())
print("  0/2 functions, 0s", file=sys.stderr, flush=True)
for k in range(2):
    with open(os.path.join(gen, "part_%d.c" % k), "w") as f:
        f.write("// %s %d %d\n" % (os.path.basename(src), n, k))
print("  2/2 functions, 0s", file=sys.stderr, flush=True)
L = ["# generated", "ninja_required_version = 1.8", "cc = " + os.environ["FAKE_CC"], "base = -arch arm64 '-I$$x'",
     "rule cc", "  command = $cc $base $in $out $out.d", "  depfile = $out.d", "  deps = gcc",
     "rule ar", "  command = rm -f $out && cat $in > $out", ""]
objs = []
for k in range(2):
    o = "%s/part_%d.o" % (obj, k)
    objs.append(o)
    L.append("build %s: cc %s/part_%d.c" % (o, gen, k))
    if k:
        L.append("  base = -O1")
L.append("build %s/libgame_rc.a: ar %s" % (obj, " ".join(objs)))
L.append("default %s/libgame_rc.a" % obj)
with open(os.path.join(gen, "build.ninja"), "w") as f:
    f.write("\n".join(L) + "\n")
'''

FAKE_CC = r'''
import os, sys
args = sys.argv[1:]
src, out, dep = args[-3:]
with open(os.environ["FAKE_CC_LOG"], "a") as f:
    f.write(src + " " + " ".join(args[:-3]) + "\n")
if os.environ.get("FAKE_CC_FAIL") and os.environ["FAKE_CC_FAIL"] in src:
    print("error: no", file=sys.stderr)
    sys.exit(1)
with open(src) as f:
    text = f.read()
with open(out, "w") as f:
    f.write(text)
with open(dep, "w") as f:
    f.write("%s: %s \\\n  %s\n" % (out, src, os.environ["FAKE_HEADER"]))
'''


class FakeRun:
    """subprocess.run for build_gl.sh, build.sh, make-corpus.sh, codesign, sips, lsregister: records the calls and makes
    the files the real ones would."""

    def __init__(self):
        self.calls = []
        self.corpus_rc = 0

    def __call__(self, cmd, env=None, **kw):
        self.calls.append(list(cmd))
        script = os.path.basename(cmd[1]) if len(cmd) > 1 else ""
        if script == "make-corpus.sh":
            self.corpus_env = env
            if not self.corpus_rc:
                write(os.path.join(cmd[-1], "msl", "rsmsl-corpus.bin"), b"corpus")
            return subprocess.CompletedProcess(cmd, self.corpus_rc, "", "")
        if script == "build.sh":
            out = env["LP_OUT"]
            os.makedirs(os.path.join(out, "dlls"), exist_ok=True)
            write(os.path.join(out, "rs2014_lp"), b"exe")
            write(os.path.join(out, "libnetguard.dylib"), b"guard")
            shutil.copy(env["LP_IMAGE"], os.path.join(out, "lp_game.exe"))
            shutil.copy(os.path.join(env["LP_GL_DIR"], "libRSOpenGL.dylib"), out)
            for d in os.listdir(os.path.join(env["LP_BUILD"], "modules", "in")):
                shutil.copy(os.path.join(env["LP_BUILD"], "modules", "in", d), os.path.join(out, "dlls", d))
        elif script == "build_gl.sh":
            write(os.path.join(env["OUT_DIR"], "libRSOpenGL.dylib"), b"gl")
        elif cmd[0].endswith("sips"):
            write(cmd[-1], b"icns")
        return subprocess.CompletedProcess(cmd, 0, "", "")


class Jobs(unittest.TestCase):
    def test_jobs_follow_cores_and_memory(self):
        self.assertEqual(lp_build.default_jobs(8, 8 << 30), 6)
        self.assertEqual(lp_build.default_jobs(8, 16 << 30), 8)
        self.assertEqual(lp_build.default_jobs(10, 24 << 30), 10)
        self.assertEqual(lp_build.default_jobs(8, 1 << 30), 1)
        self.assertGreaterEqual(lp_build.default_jobs(), 1)


class BuildBase(unittest.TestCase):
    def setUp(self):
        self.top = tempfile.mkdtemp(prefix="lp-build-test-")
        self.addCleanup(shutil.rmtree, self.top, True)
        t = self.top
        write(os.path.join(t, "translator.py"), FAKE_TRANSLATOR.encode())
        write(os.path.join(t, "cc.py"), FAKE_CC.encode())
        self.header = os.path.join(t, "rc32_gen.h")
        write(self.header, b"// header")
        self.tlog, self.cclog = os.path.join(t, "translate.log"), os.path.join(t, "cc.log")
        env = {"FAKE_LOG": self.tlog, "FAKE_CC_LOG": self.cclog, "FAKE_HEADER": self.header,
               "FAKE_CC": "%s %s" % (sys.executable, os.path.join(t, "cc.py"))}
        for k in ("FAKE_FAIL", "FAKE_CC_FAIL"):
            os.environ.pop(k, None)
        for k, v in env.items():
            os.environ[k] = v
        self.addCleanup(lambda: [os.environ.pop(k, None) for k in list(env) + ["FAKE_FAIL", "FAKE_CC_FAIL"]])
        self.dlls = os.path.join(t, "dlls")
        for name in ("msvcr100.dll", "msvcp100.dll", "d3dx9_42.dll"):
            write(os.path.join(self.dlls, name), os.urandom(64) + name.encode())
        write(os.path.join(self.dlls, "D3DCompiler_42.dll"), lp_install.rebase(pe32(tag=b"dcomp"), 0x11000000)[0])
        self.game = os.path.join(t, "game")
        write(os.path.join(self.game, "bink2w32.dll"), pe32(tag=b"bink"))
        self.image = os.path.join(t, "image", "Rocksmith2014.exe")
        write(self.image, os.urandom(256))
        write(os.path.join(t, "image", "Rocksmith2014.wrappers.tsv"), b"0x1000\tkernel32.dll!Sleep\n")
        self.work = os.path.join(t, "work")
        self.run_ = FakeRun()

    def builder(self, **kw):
        self.said = []
        self.fractions = []
        return lp_build.Builder(self.work, self.image, self.dlls, self.game, sys.executable, jobs=4,
                                progress=lambda f, label: self.fractions.append(f), say=self.said.append,
                                run=self.run_, translator=os.path.join(self.top, "translator.py"), **kw)

    def lines(self, path):
        try:
            with open(path) as f:
                return f.read().splitlines()
        except OSError:
            return []


class TestNinja(BuildBase):
    def ninja(self):
        gen = os.path.join(self.top, "gen")
        obj = os.path.join(self.top, "obj")
        write(os.path.join(self.top, "src.exe"), b"x")
        subprocess.run([sys.executable, os.path.join(self.top, "translator.py"), os.path.join(self.top, "src.exe"),
                        gen, "--obj-dir", obj], check=True, capture_output=True)
        return lp_build.Ninja(os.path.join(gen, "build.ninja")), obj

    def test_builds_then_nothing_to_do(self):
        n, obj = self.ninja()
        self.assertEqual(n.run(4), 3)  # two compiles, the archive
        self.assertEqual(len(self.lines(self.cclog)), 2)
        with open(os.path.join(obj, "libgame_rc.a")) as f:
            self.assertEqual(f.read().count("src.exe"), 2)
        self.assertEqual(n.run(4), 0)

    def test_bindings_and_escapes(self):
        n, obj = self.ninja()
        n.run(4)
        cmds = self.lines(self.cclog)
        self.assertIn("-arch arm64 -I$x", [c for c in cmds if "part_0" in c][0])
        self.assertIn("-O1", [c for c in cmds if "part_1" in c][0])

    def test_header_in_depfile_rebuilds(self):
        n, obj = self.ninja()
        n.run(4)
        t = time.time() + 5
        os.utime(self.header, (t, t))
        self.assertEqual(n.run(4), 3)

    def test_changed_command_rebuilds_one(self):
        n, obj = self.ninja()
        n.run(4)
        n.edges[1]["vars"]["base"] = "-O0"
        self.assertEqual(n.run(4), 2)  # part_1 and the archive

    def test_failure_keeps_what_compiled(self):
        n, obj = self.ninja()
        os.environ["FAKE_CC_FAIL"] = "part_1"
        with self.assertRaises(lp_build.BuildError) as e:
            n.run(1)
        self.assertIn("part_1", str(e.exception))
        self.assertIn("error: no", str(e.exception))
        del os.environ["FAKE_CC_FAIL"]
        self.assertEqual(n.run(4), 2)  # part_1 and the archive, not part_0 again


class TestBuild(BuildBase):
    def test_a_library_says_it_is_alive(self):
        b = self.builder()
        b.ALIVE_SECS = 0.001
        b.run()
        self.assertTrue([s for s in self.said if s.endswith(": still working")])

    def test_full_build(self):
        self.builder().run()
        t = self.lines(self.tlog)
        self.assertEqual([x.split()[0] for x in t], ["msvcr100.dll", "msvcp100.dll", "d3dx9_42.dll",
                                                     "d3dcompiler_42.dll", "bink2w32.dll", "Rocksmith2014.exe"])
        self.assertIn("--module", t[0])
        self.assertIn("--wrappers", t[-1])  # IMAGE.wrappers.tsv beside the image
        self.assertNotIn("--module", t[-1])
        for d in ("game", "modules/msvcr100", "modules/bink2w32"):
            self.assertTrue(os.path.isfile(os.path.join(self.work, d, "obj", "libgame_rc.a")), d)
        out = os.path.join(self.work, "out")
        self.assertEqual(sorted(os.listdir(os.path.join(out, "dlls"))),
                         ["bink2w32.dll", "d3dcompiler_42.dll", "d3dx9_42.dll", "msvcp100.dll", "msvcr100.dll"])
        self.assertEqual([os.path.basename(c[1]) for c in self.run_.calls], ["build_gl.sh", "build.sh", "make-corpus.sh"])
        self.assertEqual(self.fractions, sorted(self.fractions))
        self.assertEqual(self.fractions[-1], 1.0)

    def test_the_corpus_switches_are_the_macs(self):
        given = os.path.join(self.work, "given.bin")
        write(given, b"given")
        with mock.patch.dict(os.environ, {"RS_MSL_CORPUS_FILE": given}):
            self.builder().run()
        dest = os.path.join(self.work, "out", "rsmsl-corpus.bin")
        self.assertEqual(read(dest), b"given")
        with mock.patch.dict(os.environ, {"RS_SHADER_CORPUS": "0"}):
            self.builder().run()
        self.assertFalse(os.path.exists(dest))
        self.assertNotIn("make-corpus.sh", [os.path.basename(c[1]) for c in self.run_.calls])

    def test_the_shader_corpus_is_made_from_the_game(self):
        self.builder().run()
        self.assertEqual(self.run_.calls[-1][2], "--learn-play")
        self.assertEqual(self.run_.corpus_env["RC_IMAGE"], self.image)
        self.assertEqual(self.run_.corpus_env["RS_GAME_DIR"], self.game)
        self.assertEqual(read(os.path.join(self.work, "out", "rsmsl-corpus.bin")), b"corpus")

    def test_a_build_goes_on_without_a_corpus(self):
        write(os.path.join(self.work, "out", "rsmsl-corpus.bin"), b"old")
        self.run_.corpus_rc = 1
        self.builder().run()
        self.assertFalse(os.path.exists(os.path.join(self.work, "out", "rsmsl-corpus.bin")))
        self.assertTrue([s for s in self.said if "no shader corpus" in s])
        self.assertEqual(self.fractions[-1], 1.0)

    def test_dlls_staged_and_rebased(self):
        self.builder().run()
        d = os.path.join(self.work, "modules", "in")
        bink = read(os.path.join(d, "bink2w32.dll"))
        self.assertEqual(bink, lp_install.rebase(pe32(tag=b"bink"), 0x12000000)[0])
        # already rebased by redist: as it is
        self.assertEqual(read(os.path.join(d, "d3dcompiler_42.dll")), read(os.path.join(self.dlls, "D3DCompiler_42.dll")))

    def test_rerun_skips_everything(self):
        self.builder().run()
        n_t, n_cc = len(self.lines(self.tlog)), len(self.lines(self.cclog))
        self.builder().run()
        self.assertEqual(len(self.lines(self.tlog)), n_t)
        self.assertEqual(len(self.lines(self.cclog)), n_cc)
        self.assertIn("  the game: up to date", self.said)

    def test_changed_input_redoes_only_it(self):
        self.builder().run()
        write(self.image, os.urandom(300))
        n_t = len(self.lines(self.tlog))
        self.builder().run()
        self.assertEqual([x.split()[0] for x in self.lines(self.tlog)[n_t:]], ["Rocksmith2014.exe"])

    def test_resume_after_failed_compile(self):
        os.environ["FAKE_CC_FAIL"] = os.path.join("game", "gen", "part_1")
        with self.assertRaises(lp_build.BuildError):
            self.builder().run()
        del os.environ["FAKE_CC_FAIL"]
        n_t, n_cc = len(self.lines(self.tlog)), len(self.lines(self.cclog))
        self.builder().run()
        self.assertEqual(len(self.lines(self.tlog)), n_t)  # no translation again
        self.assertEqual([os.path.basename(x.split()[0]) for x in self.lines(self.cclog)[n_cc:]], ["part_1.c"])
        self.assertIn("  the game: translated already", self.said)

    def test_resume_after_failed_translation(self):
        os.environ["FAKE_FAIL"] = "Rocksmith2014.exe"
        with self.assertRaises(lp_build.BuildError) as e:
            self.builder().run()
        self.assertIn("translate-game.log", str(e.exception))
        del os.environ["FAKE_FAIL"]
        n_t = len(self.lines(self.tlog))
        self.builder().run()
        self.assertEqual([x.split()[0] for x in self.lines(self.tlog)[n_t:]], ["Rocksmith2014.exe"])

    def test_stopped_retranslation_is_not_taken_for_the_old_one(self):
        # a translation of new inputs that stops halfway leaves no key: going back to the old inputs
        # translates again instead of taking the half-new files for the old translation
        self.builder().run()
        old = open(self.image, "rb").read()
        write(self.image, os.urandom(300))
        os.environ["FAKE_FAIL"] = "Rocksmith2014.exe"
        with self.assertRaises(lp_build.BuildError):
            self.builder().run()
        del os.environ["FAKE_FAIL"]
        write(self.image, old)
        n_t = len(self.lines(self.tlog))
        self.builder().run()
        self.assertEqual([x.split()[0] for x in self.lines(self.tlog)[n_t:]], ["Rocksmith2014.exe"])

    def test_spaces_refused(self):
        self.work = os.path.join(self.top, "my build")
        with self.assertRaises(lp_build.BuildError) as e:
            self.builder().run()
        self.assertIn("space", str(e.exception))

    def test_clean_refuses_other_folders(self):
        home = os.path.join(self.top, "home")
        write(os.path.join(home, "steam", "save"), b"x")
        os.environ["LP_HOME"] = home
        self.addCleanup(os.environ.pop, "LP_HOME", None)
        for d in (home, self.top, os.path.join(self.top, "dlls")):
            with self.assertRaises(lp_build.BuildError):
                lp_build.clean(d, everything=True)
        self.assertTrue(os.path.isfile(os.path.join(home, "steam", "save")))

    def test_missing_inputs(self):
        os.remove(os.path.join(self.dlls, "d3dx9_42.dll"))
        with self.assertRaises(lp_build.BuildError) as e:
            self.builder().run()
        self.assertIn("d3dx9_42.dll", str(e.exception))
        os.remove(self.image)
        with self.assertRaises(lp_build.BuildError) as e:
            self.builder().run()
        self.assertIn("code image", str(e.exception))

    def test_clean_keeps_the_libraries(self):
        self.builder().run()
        freed = lp_build.clean(self.work)
        self.assertGreater(freed, 0)
        left = sorted(os.path.relpath(os.path.join(d, f), self.work) for d, _, fs in os.walk(self.work) for f in fs)
        self.assertNotIn(os.path.join("game", "gen", "build.ninja"), left)
        self.assertNotIn(os.path.join("game", "obj", "part_0.o"), left)
        self.assertIn(os.path.join("game", "obj", "libgame_rc.a"), left)
        self.assertIn(os.path.join("game", "obj", "libgame_rc.a.key"), left)
        self.assertFalse(os.path.exists(os.path.join(self.work, "out")))
        n_t, n_cc = len(self.lines(self.tlog)), len(self.lines(self.cclog))
        self.builder().run()  # the cache: nothing translated or compiled again
        self.assertEqual((len(self.lines(self.tlog)), len(self.lines(self.cclog))), (n_t, n_cc))
        self.assertTrue(os.path.isfile(os.path.join(self.work, "out", "rs2014_lp")))

    def test_clean_all(self):
        self.builder().run()
        lp_build.clean(self.work, everything=True)
        self.assertFalse(os.path.exists(self.work))
        self.assertEqual(lp_build.clean(self.work), 0)

    def test_not_enough_space(self):
        cfg = lp_install.load_config()
        cfg["build_disk_bytes"] = 1 << 60
        a = argparse.Namespace(work=self.work, image=self.image, unpinned=True, dlls=self.dlls, game=self.game,
                               py=sys.executable, jobs=1)
        with self.assertRaises(Failure) as e:
            lp_install.build(cfg, a, quiet())
        self.assertIn("not enough free space", str(e.exception))

    def test_estimate(self):
        text = lp_install.build_estimate(lp_install.load_config())
        self.assertRegex(text, r"about \d+ minutes")
        self.assertIn("GB free", text)


def pe_with_icon(icon=b"\x89PNG-icon-bytes", group_id=101, icon_id=1):
    """A PE32 with a .rsrc section: one RT_GROUP_ICON (one 256x256 entry) and its RT_ICON."""
    R = 0x3000  # the section's RVA

    def dirhdr(n):
        return struct.pack("<IIHHHH", 0, 0, 0, 0, 0, n)

    grp = struct.pack("<HHH", 0, 1, 1) + struct.pack("<BBBBHHIH", 0, 0, 0, 0, 1, 32, len(icon), icon_id)
    # layout: root, types[3,14] -> name dirs -> lang dirs -> data entries -> blobs
    root, d_icon, d_grp, l_icon, l_grp = 0, 32, 56, 80, 104
    e_icon, e_grp, blob_icon = 128, 144, 160
    blob_grp = blob_icon + len(icon) + (-len(icon) % 4)
    s = bytearray(blob_grp + len(grp))
    s[root:root + 32] = dirhdr(2) + struct.pack("<II", 3, 0x80000000 | d_icon) + struct.pack("<II", 14, 0x80000000 | d_grp)
    s[d_icon:d_icon + 24] = dirhdr(1) + struct.pack("<II", icon_id, 0x80000000 | l_icon)
    s[d_grp:d_grp + 24] = dirhdr(1) + struct.pack("<II", group_id, 0x80000000 | l_grp)
    s[l_icon:l_icon + 24] = dirhdr(1) + struct.pack("<II", 0x409, e_icon)
    s[l_grp:l_grp + 24] = dirhdr(1) + struct.pack("<II", 0x409, e_grp)
    s[e_icon:e_icon + 16] = struct.pack("<IIII", R + blob_icon, len(icon), 0, 0)
    s[e_grp:e_grp + 16] = struct.pack("<IIII", R + blob_grp, len(grp), 0, 0)
    s[blob_icon:blob_icon + len(icon)] = icon
    s[blob_grp:] = grp
    size = (len(s) + 0x1ff) & ~0x1ff
    d = bytearray(pe32())
    pe = 0x40
    struct.pack_into("<H", d, pe + 6, 3)  # a third section
    opt = pe + 24
    struct.pack_into("<II", d, opt + 96 + 8 * 2, R, len(s))
    sec = opt + 224 + 80
    d[sec:sec + 8] = b".rsrc\0\0\0"
    struct.pack_into("<IIII", d, sec + 8, len(s), R, size, len(d))
    return bytes(d) + bytes(s) + b"\0" * (size - len(s))


class TestApp(BuildBase):
    def built(self):
        self.builder().run()
        self.app = os.path.join(self.top, "Applications", lp_build.APP_NAME + ".app")
        self.home = os.path.join(self.top, "home")

    def install(self):
        return lp_build.install_app(self.work, self.game, self.app, self.home, run=self.run_)

    def test_bundle(self):
        self.built()
        app, home = self.install()
        self.assertEqual(app, self.app)
        c = os.path.join(app, "Contents")
        self.assertEqual(read(os.path.join(c, "MacOS", "rs2014_lp")), b"exe")
        self.assertTrue(os.path.isfile(os.path.join(c, "MacOS", "libRSOpenGL.dylib")))
        self.assertTrue(os.path.isfile(os.path.join(c, "MacOS", "libnetguard.dylib")))  # mods load only with it
        self.assertEqual(read(os.path.join(c, "Resources", "lp_game.exe")), read(self.image))
        self.assertEqual(len(os.listdir(os.path.join(c, "Resources", "dlls"))), 5)
        self.assertEqual(read(os.path.join(c, "Resources", "rsmsl-corpus.bin")), b"corpus")
        import plistlib
        with open(os.path.join(c, "Info.plist"), "rb") as f:
            p = plistlib.load(f)
        self.assertEqual(p["CFBundleExecutable"], "rs2014_lp")
        self.assertIn("NSMicrophoneUsageDescription", p)
        self.assertNotIn("LSEnvironment", p)  # the game finds its folders by itself (lp_main.c)
        signed = [c[-1] for c in self.run_.calls if c[0].endswith("codesign")]
        self.assertEqual(signed, [os.path.join(app + ".new", "Contents", "MacOS", f)
                                  for f in ("libRSOpenGL.dylib", "libnetguard.dylib")] + [app + ".new"])
        with open(os.path.join(home, "game-dir")) as f:
            self.assertEqual(f.read(), os.path.abspath(self.game) + "\n")
        for sub in ("userdata", "steam", "logs"):
            self.assertTrue(os.path.isdir(os.path.join(home, sub)))

    def test_reinstall_keeps_the_users_folder(self):
        self.built()
        self.install()
        write(os.path.join(self.home, "userdata", "game", "Rocksmith.ini"), b"[Audio]\nLatencyBuffer=2\n")
        write(os.path.join(self.app, "Contents", "stale"), b"x")
        self.install()
        self.assertEqual(read(os.path.join(self.home, "userdata", "game", "Rocksmith.ini")), b"[Audio]\nLatencyBuffer=2\n")
        self.assertFalse(os.path.exists(os.path.join(self.app, "Contents", "stale")))
        self.assertFalse(os.path.exists(self.app + ".new") or os.path.exists(self.app + ".old"))

    def test_needs_a_build(self):
        with self.assertRaises(lp_build.BuildError):
            lp_build.install_app(self.work, self.game, os.path.join(self.top, "x.app"), os.path.join(self.top, "h"),
                                 run=self.run_)

    def test_icon(self):
        exe = pe_with_icon(b"ICONDATA")
        ico = lp_build.pe_icon(exe)
        self.assertEqual(struct.unpack_from("<HHH", ico, 0), (0, 1, 1))
        size, off = struct.unpack_from("<II", ico, 6 + 8)
        self.assertEqual((size, ico[off:off + size]), (8, b"ICONDATA"))
        self.assertIsNone(lp_build.pe_icon(pe32()))
        self.assertIsNone(lp_build.pe_icon(b"MZ" + b"\0" * 100))

    def test_icon_in_the_bundle(self):
        write(self.image, pe_with_icon())
        self.built()
        self.install()
        import plistlib
        with open(os.path.join(self.app, "Contents", "Info.plist"), "rb") as f:
            self.assertEqual(plistlib.load(f)["CFBundleIconFile"], "RS2014.icns")
        self.assertTrue(os.path.isfile(os.path.join(self.app, "Contents", "Resources", "RS2014.icns")))
        self.assertFalse(any(f.endswith(".ico") for f in os.listdir(os.path.join(self.app, "Contents", "Resources"))))


class TestHarvest(unittest.TestCase):
    """The prebuilt harvester: the committed exe is the one README.md names, and harvest-export copies it
    without .NET."""

    def test_committed_exe_matches_readme(self):
        want = lp_install.harvest_sha256()
        self.assertRegex(want or "", r"^[0-9a-f]{64}$", "README.md has no SHA-256 line for rsharvest2.exe")
        with open(os.path.join(lp_install.HARVEST_SRC, "rsharvest2.exe"), "rb") as f:
            self.assertEqual(hashlib.sha256(f.read()).hexdigest(), want,
                             "rsharvest2.exe differs from the SHA-256 in win-x86/harvest/README.md")

    def test_export_copies_prebuilt_without_dotnet(self):
        calls = []
        old_which, old_run = shutil.which, subprocess.run
        shutil.which = lambda *a, **k: calls.append(("which", a)) or "/usr/local/bin/dotnet"
        subprocess.run = lambda *a, **k: calls.append(("run", a)) or (_ for _ in ()).throw(AssertionError("ran"))
        old_env = os.environ.pop("LP_HARVEST_REBUILD", None)
        try:
            with tempfile.TemporaryDirectory() as tmp:
                dest, rebuilt = lp_install.harvest_export(os.path.join(tmp, "h"))
                self.assertFalse(rebuilt)
                for name in lp_install.HARVEST_FILES + lp_install.HARVEST_SOURCE:
                    self.assertTrue(os.path.isfile(os.path.join(dest, name)), name)
                with open(os.path.join(dest, "rsharvest2.exe"), "rb") as f:
                    self.assertEqual(hashlib.sha256(f.read()).hexdigest(), lp_install.harvest_sha256())
        finally:
            shutil.which, subprocess.run = old_which, old_run
            if old_env is not None:
                os.environ["LP_HARVEST_REBUILD"] = old_env
        self.assertEqual(calls, [])

    def test_rebuild_needs_dotnet(self):
        old_which = shutil.which
        shutil.which = lambda *a, **k: None
        try:
            with tempfile.TemporaryDirectory() as tmp:
                with self.assertRaises(Failure):
                    lp_install.harvest_export(os.path.join(tmp, "h"), rebuild=True)
        finally:
            shutil.which = old_which


@unittest.skipUnless(os.environ.get("LP_REDIST_REAL"), "set LP_REDIST_REAL=1 to check the real DLLs from Steam")
class TestReal(unittest.TestCase):
    def test_real_steam_redist(self):
        out = tempfile.mkdtemp(prefix="lp-redist-real-")
        self.addCleanup(shutil.rmtree, out, True)
        cfg = lp_install.load_config()
        Redist(cfg, out, lp_install.steam_dir(), allow_download=False, progress=quiet()).run()
        self.assertEqual(lp_install.sha256(os.path.join(out, "D3DCompiler_42.dll")), cfg["d3dcompiler_42_rebased_sha256"])


@unittest.skipUnless(os.environ.get("LP_GAME_REAL"), "set LP_GAME_REAL=<depot folder> to check a real download")
class TestRealGame(unittest.TestCase):
    def test_real_depot(self):
        cfg = lp_install.load_config()
        folder = os.environ["LP_GAME_REAL"]
        self.assertEqual(lp_install.locate(cfg, folder).status, "ok")
        have, files, _ = lp_install.folder_bytes(folder)
        self.assertEqual((have, files), (cfg["depot_disk_bytes"], cfg["depot_files"]))


if __name__ == "__main__":
    unittest.main()
