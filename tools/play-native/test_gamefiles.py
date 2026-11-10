#!/usr/bin/env python3
"""Tests of gamefiles.py (the install's own copy of the game files) with small made-up game folders: what each
edition copies, clones and real copies, resuming, refreshing, removals only within the copied set, links in
the copy, the space check and the completeness check; a game on another drive (made-up volumes, and real
APFS and HFS+ disk images where hdiutil works): where the copy goes, the link to it, a drive not connected.

    python3 tools/play-native/test_gamefiles.py
"""
import io
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import gamefiles  # noqa: E402

# The archives here are made up, and so are their key and IV: the game's own are read from a game (psarc_key.py).
TEST_KEY = ("00112233445566778899AABBCCDDEEFF" * 2, "0F1E2D3C4B5A69788796A5B4C3D2E1F0")
gamefiles.cache_overlay.psarc_key.use(*TEST_KEY)
from gamefiles import Copier, Failure  # noqa: E402

MANIFEST = """"AppState"
{
	"appid"		"221680"
	"installdir"		"Rocksmith2014"
	"InstalledDepots"
	{
		"221682" { "manifest" "1" "size" "2" }
		"258341" { "manifest" "3" "size" "4" "dlcappid" "258341" }
		"248750" { "manifest" "5" "size" "6" "dlcappid" "248750" }
	}
	"UserConfig" { "language" "german" }
}
"""


def write(path, data=b"x", mtime=None):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(data)
    if mtime is not None:
        os.utime(path, (mtime, mtime))
    return path


def quiet():
    return gamefiles.Progress(stream=io.StringIO())


def no_clone(src, dst):
    return False


class Base(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="gamefiles-test-")
        self.addCleanup(shutil.rmtree, self.tmp, True)
        self.steam = os.path.join(self.tmp, "Steam")
        self.lib = os.path.join(self.steam, "steamapps")
        self.src = os.path.join(self.lib, "common", "Rocksmith2014")
        self.dest = os.path.join(self.tmp, "home", "game")
        env = mock.patch.dict(os.environ)
        env.start()
        self.addCleanup(env.stop)
        os.environ.pop("RS_STEAM_ID", None)
        os.environ.pop("RS_GAME_COPY_DIR", None)

    def mac_game(self):
        s = self.src
        for n in ("static.psarc", "songs.psarc", "audio.psarc", "session.psarc", "metadata",
                  "Rocksmith.ini", "cache.psarc.backup", "D396655.txt", ".DS_Store"):
            write(os.path.join(s, n), n.encode())
        write(os.path.join(s, "Rocksmith2014.app", "Contents", "MacOS", "Rocksmith2014"), b"steam app")
        write(os.path.join(s, "base", "game_flow.psarc"), b"flow")
        write(os.path.join(s, "Tmp", "previews", "steam1_m.psarc"), b"preview")
        write(os.path.join(s, "dlc", "song_m.psarc"), b"mac song")
        write(os.path.join(s, "dlc", "song_p.psarc"), b"pc song")
        write(os.path.join(s, "dlc", "cdlc", "Custom_m.psarc"), b"custom mac")
        write(os.path.join(s, "dlc", "cdlc", "readme.txt"), b"readme")
        write(os.path.join(s, "dlc", ".DS_Store"), b"finder")
        write(os.path.join(self.lib, "appmanifest_221680.acf"), MANIFEST.encode())

    def lp_game(self, top=None):
        top = top or os.path.join(self.tmp, "depot")
        for n in ("Rocksmith2014.exe", "static.psarc", "songs.psarc", "bink2w32.dll", "steam_appid.txt"):
            write(os.path.join(top, n), n.encode())
        write(os.path.join(top, "UPlay", "x.dll"), b"uplay")
        write(os.path.join(top, "dlc", "own_p.psarc"), b"own pc")
        write(os.path.join(top, "dlc", "own_m.psarc"), b"own mac")
        return top

    def copier(self, edition="mac", source=None, **kw):
        kw.setdefault("progress", quiet())
        kw.setdefault("is_running", lambda e: False)
        kw.setdefault("steam", self.steam)
        return Copier(edition, source or self.src, self.dest, **kw)

    def _clones_here(self):
        a = write(os.path.join(self.tmp, "probe-a"), b"a")
        b = os.path.join(self.tmp, "probe-b")
        try:
            return gamefiles.clonefile(a, b)
        finally:
            for p in (a, b):
                if os.path.lexists(p):
                    os.unlink(p)

    def manifest(self):
        with open(os.path.join(self.dest, ".rearmed", "manifest.json")) as f:
            return json.load(f)

    def read(self, rel):
        with open(os.path.join(self.dest, rel), "rb") as f:
            return f.read()


class Plan(Base):
    def test_mac(self):
        self.mac_game()
        got = sorted(gamefiles.plan("mac", self.src))
        self.assertEqual(got, sorted([
            "static.psarc.original", "songs.psarc", "audio.psarc", "session.psarc", "metadata",
            ".rearmed/Rocksmith.ini", "base/game_flow.psarc", "Tmp/previews/steam1_m.psarc",
            "dlc/song_m.psarc", "dlc/cdlc/Custom_m.psarc"]))

    def test_learn_play(self):
        depot = self.lp_game()
        self.mac_game()
        got = sorted(gamefiles.plan("learn-play", depot, [os.path.join(self.src, "dlc")]))
        self.assertEqual(got, sorted([
            "Rocksmith2014.exe", "static.psarc.original", "songs.psarc", "bink2w32.dll", "steam_appid.txt",
            "UPlay/x.dll", "dlc/own_p.psarc", "dlc/song_p.psarc"]))

    def test_first_source_wins_case_insensitive(self):
        depot = self.lp_game()
        write(os.path.join(self.tmp, "macdlc", "OWN_P.psarc"), b"other")
        files = gamefiles.plan("learn-play", depot, [os.path.join(self.tmp, "macdlc")])
        self.assertEqual(files["dlc/own_p.psarc"], os.path.join(depot, "dlc", "own_p.psarc"))
        self.assertNotIn("dlc/OWN_P.psarc", files)

    def test_linked_dlc_folder_is_followed(self):
        self.mac_game()
        other = os.path.join(self.tmp, "external", "dlc")
        write(os.path.join(other, "far_m.psarc"), b"far")
        os.symlink(other, os.path.join(self.src, "dlc", "external"))
        self.assertIn("dlc/external/far_m.psarc", gamefiles.plan("mac", self.src))

    def test_steam_env(self):
        self.mac_game()
        env = gamefiles.steam_env(gamefiles.find_appmanifest(self.src, self.steam))
        self.assertEqual(env, ["RS_STEAM_DLC=248750,258341", "RS_STEAM_LANGUAGE=german"])
        self.assertEqual(gamefiles.steam_env(None), [])
        # the AppIDs of the copy's archives are added; the base game's is not a DLC
        env = gamefiles.steam_env(gamefiles.find_appmanifest(self.src, self.steam), archive_apps={999999, 221680})
        self.assertEqual(env, ["RS_STEAM_DLC=248750,258341,999999", "RS_STEAM_LANGUAGE=german"])
        self.assertEqual(gamefiles.steam_env(None, archive_apps={999999}), ["RS_STEAM_DLC=248750,999999"])


class Rs1Owned(Base):
    """The songs of the RS1 compatibility pack need the first Rocksmith's DLC: the Steam client is asked for it when
    the edition's account is logged in, and the answer is kept for a refresh without it."""

    ACCOUNT = 12345

    def setUp(self):
        super().setUp()
        self.dest = os.path.join(self.tmp, "copy")
        os.makedirs(os.path.join(self.dest, gamefiles.META))
        index = {"Entries": {"a": {"Attributes": {"DLCRS1Key": [{"360": "x", "WIN32": 206166}]}},
                             "b": {"Attributes": {"DLCRS1Key": [{"WIN32": "222041"}]}},
                             "c": {"Attributes": {"DLCRS1Key": None}}, "d": {"Attributes": {}}}}
        write(os.path.join(self.dest, "dlc", "rs1compatibilitydlc_m.psarc"), gamefiles.cache_overlay.write_psarc(
            [("appid.appid", b"221680"), ("manifests/songs_rs1dlc/songs_rs1dlc.hsan", json.dumps(index).encode())]))
        self.asked = []
        self.answer = {"account": self.ACCOUNT, "apps": [206166]}
        self.state = gamefiles.SteamState(True, True, True, self.ACCOUNT)
        patches = [mock.patch.object(gamefiles, "steam_state", lambda steam: self.state),
                   mock.patch.object(gamefiles, "ask_steam", lambda steam, apps: self.asked.append(apps) or self.answer)]
        for p in patches:
            p.start()
            self.addCleanup(p.stop)
        self.said = []

    def owned(self, account=ACCOUNT):
        return gamefiles.rs1_owned(self.steam, self.dest, account, self.said.append)

    def test_the_pack_names_the_dlc(self):
        self.assertEqual(gamefiles.rs1_dlc_apps(self.dest), {206166, 222041})

    def test_steam_says_what_the_account_owns(self):
        self.assertEqual(self.owned(), [206166])
        self.assertEqual(self.asked, [{206166, 222041}])
        self.assertIn("owns 1 of the 2", self.said[0])

    def test_steam_env_keeps_them_apart_from_the_dlc(self):
        env = gamefiles.steam_env(None, archive_apps={999999}, owned=[222041, 206166])
        self.assertEqual(env, ["RS_STEAM_DLC=248750,999999", "RS_STEAM_OWNED=206166,222041"])

    def test_another_account_is_not_asked_and_the_answer_is_kept(self):
        self.owned()
        self.state = gamefiles.SteamState(True, True, True, 777)
        self.assertEqual(self.owned(), [206166])
        self.assertEqual(len(self.asked), 1)

    def test_steam_closed_keeps_the_answer(self):
        self.owned()
        self.state = gamefiles.SteamState(True, False, False)
        self.assertEqual(self.owned(), [206166])

    def test_an_answer_for_another_account_is_not_used(self):
        self.answer = {"account": 777, "apps": [206166, 222041]}
        self.assertEqual(self.owned(), [])
        self.assertIn("not checked", self.said[0])

    def test_steam_with_no_answer_keeps_what_was_known(self):
        self.owned()
        self.answer = None
        self.assertEqual(self.owned(), [206166])

    def test_what_one_account_owns_is_not_the_others(self):
        self.owned()
        self.state = gamefiles.SteamState(True, False, False)
        self.assertEqual(self.owned(account=777), [])

    def test_nothing_is_asked_without_the_pack(self):
        os.unlink(os.path.join(self.dest, "dlc", "rs1compatibilitydlc_m.psarc"))
        self.assertEqual(self.owned(), [])
        self.assertEqual(self.asked, [])

    def test_the_owned_apps_reach_steam_env(self):
        self.mac_game()
        with mock.patch.object(gamefiles, "steam_account", lambda *a: (self.ACCOUNT, gamefiles.HOW_LICENSE)):
            write(os.path.join(self.src, "dlc", "rs1compatibilitydlc_m.psarc"),
                  open(os.path.join(self.dest, "dlc", "rs1compatibilitydlc_m.psarc"), "rb").read())
            copy = os.path.join(self.tmp, "fresh")
            Copier("mac", self.src, copy, (), self.steam, quiet(), is_running=lambda e: False).run()
        self.assertIn(b"RS_STEAM_OWNED=206166\n", open(os.path.join(copy, ".rearmed", "steam.env"), "rb").read())


class ArchiveKey(Base):
    """The archives' key is read from the game (psarc_key.py), never from RS Re-ARMed: a game image shows it, a
    copy keeps it, and a copy made before the image is there waits for it. A made-up key stands in for the
    game's, known by its hash as the game's is."""

    KEY = b"89ABCDEF0123456776543210FEDCBA98" * 2

    def setUp(self):
        super().setUp()
        self.pk = pk = gamefiles.cache_overlay.psarc_key
        pk.use(None, None)
        self.addCleanup(pk.use, *TEST_KEY)
        p = mock.patch.multiple(pk, KEY_SHA256=__import__("hashlib").sha256(self.KEY).hexdigest(), _checked=False)
        p.start()
        self.addCleanup(p.stop)
        os.environ.pop(pk.IMAGE_ENV, None)
        self.image = write(os.path.join(self.tmp, "image.bin"), b"\x90" * 99 + b"0123456789ABCDEF" * 4 + b"\0"
                           + b"G" + self.KEY + b"\0code" + b"A" * 70)

    def archive(self, path, appid):
        """A song archive made as the game's are, wherever the key comes from afterwards."""
        self.pk.use(self.KEY.decode(), TEST_KEY[1])
        data = song(appid)
        self.pk.use(None, None)
        self.pk._checked = False
        return write(path, data)

    def test_image(self):
        self.assertEqual(self.pk.key_in_image(self.image), self.KEY.decode())
        self.assertIsNone(self.pk.key_in_image(write(os.path.join(self.tmp, "packed.exe"), b"MZ" + b"\x11" * 500)))
        self.assertIsNone(self.pk.key_in_image(os.path.join(self.tmp, "missing")))

    def test_found_near_the_archive(self):
        far = self.archive(os.path.join(self.tmp, "alone", "a_p.psarc"), 5)
        self.assertIsNone(self.pk.find_key(far))
        self.assertIsNone(gamefiles.archive_appid(far))
        game = os.path.join(self.tmp, "game")
        deep = self.archive(os.path.join(game, "dlc", "cdlc", "pack", "b_m.psarc"), 7)
        shutil.copy(self.image, write(os.path.join(game, "Rocksmith2014.app", "Contents", "MacOS", "Rocksmith2014"), b""))
        self.assertEqual(gamefiles.archive_appid(deep), 7)
        self.pk.use(None, None)
        self.pk._checked = False
        copy = os.path.join(self.tmp, "copy")
        self.assertTrue(self.pk.note(copy, image=self.image))
        self.assertEqual(self.pk.find_key(os.path.join(copy, "dlc", "x_p.psarc")), self.KEY.decode())
        write(os.path.join(copy, ".rearmed", "toc-key"), b"F" * 64)  # not the key: not believed
        self.assertIsNone(self.pk.find_key(os.path.join(copy, "dlc", "x_p.psarc")))
        with mock.patch.dict(os.environ, {self.pk.IMAGE_ENV: self.image}):
            self.assertEqual(gamefiles.archive_appid(far), 5)

    def test_a_copy_waits_for_the_image(self):
        self.mac_game()
        lp = self.lp_game()
        self.archive(os.path.join(self.src, "dlc", "cdlc", "Custom_p.psarc"), 999999)
        said = []
        progress = quiet()
        progress.say = said.append
        self.copier("learn-play", lp, dlc_from=[os.path.join(self.src, "dlc")], progress=progress).run()
        meta = os.path.join(self.dest, ".rearmed")
        self.assertTrue(os.path.exists(os.path.join(meta, "key-pending")))
        self.assertTrue(any("code image" in line for line in said))
        self.assertNotIn("dlc/cdlc/Custom_p.psarc", gamefiles.read_json(os.path.join(meta, "dlc-appids.json"))["files"])
        self.assertTrue(os.path.isfile(os.path.join(self.dest, "dlc", "cdlc", "Custom_p.psarc")))  # copied all the same
        self.assertFalse(gamefiles.key_from_image(self.dest, os.path.join(self.tmp, "missing")))
        self.assertTrue(gamefiles.key_from_image(self.dest, self.image, quiet(), is_running=lambda e: False))
        self.assertFalse(os.path.exists(os.path.join(meta, "key-pending")))
        self.assertEqual(gamefiles.read_json(os.path.join(meta, "dlc-appids.json"))["files"]["dlc/cdlc/Custom_p.psarc"][2],
                         999999)
        with open(os.path.join(meta, "steam.env")) as f:
            self.assertIn("999999", f.read())
        # and a later refresh needs neither the image nor the game
        self.pk.use(None, None)
        self.pk._checked = False
        self.archive(os.path.join(self.src, "dlc", "cdlc", "Later_p.psarc"), 123456)
        gamefiles.refresh(self.dest, progress=quiet(), is_running=lambda e: False)
        with open(os.path.join(meta, "steam.env")) as f:
            self.assertIn("123456", f.read())


def song(appid=None, name="songs/bin/generic/x_lead.sng"):
    """A small song archive (a real psarc) carrying APPID in appid.appid (none: no such entry)."""
    files = [(name, b"song data")]
    if appid is not None:
        files.insert(0, ("appid.appid", str(appid).encode()))
    return gamefiles.cache_overlay.write_psarc(files)


class AppIds(Base):
    """Custom songs are listed whatever AppID they carry: the AppIDs of the copy's dlc/ archives are in
    steam.env's RS_STEAM_DLC, so the offline Steam layer reports them owned and installed."""

    def test_archive_appid(self):
        self.assertEqual(gamefiles.archive_appid(write(os.path.join(self.tmp, "a_p.psarc"), song(999999))), 999999)
        self.assertEqual(gamefiles.archive_appid(write(os.path.join(self.tmp, "b_p.psarc"), song(" 248750\n"))),
                         248750)
        self.assertIsNone(gamefiles.archive_appid(write(os.path.join(self.tmp, "c_p.psarc"), song())))
        self.assertIsNone(gamefiles.archive_appid(write(os.path.join(self.tmp, "d_p.psarc"), b"not a psarc")))
        self.assertIsNone(gamefiles.archive_appid(write(os.path.join(self.tmp, "e_p.psarc"), song(0))))
        self.assertIsNone(gamefiles.archive_appid(os.path.join(self.tmp, "missing_p.psarc")))

    def env(self):
        with open(os.path.join(self.dest, ".rearmed", "steam.env")) as f:
            return [line for line in f.read().splitlines() if line.startswith("RS_STEAM_DLC=")]

    def test_both_editions(self):
        for edition in ("mac", "learn-play"):
            with self.subTest(edition=edition):
                shutil.rmtree(self.tmp)
                os.makedirs(self.tmp)
                self.mac_game()
                k = "m" if edition == "mac" else "p"
                write(os.path.join(self.src, "dlc", "official_%s.psarc" % k), song(258341))
                write(os.path.join(self.src, "dlc", "cdlc", "Custom_%s.psarc" % k), song(999999))
                write(os.path.join(self.src, "dlc", "cdlc", "Free_%s.psarc" % k), song(248750))
                write(os.path.join(self.src, "dlc", "rs1compatibilitydlc_%s.psarc" % k), song(221680))
                write(os.path.join(self.src, "dlc", "other_%s.psarc" % ("p" if k == "m" else "m")), song(777777))
                if edition == "mac":
                    self.copier("mac").run()
                else:
                    self.copier("learn-play", self.lp_game(), dlc_from=[os.path.join(self.src, "dlc")]).run()
                # the other platform's archive is not in the copy, so its AppID is not added
                self.assertEqual(self.env(), ["RS_STEAM_DLC=248750,258341,999999"])

    def test_refresh_reads_only_new_and_changed_archives(self):
        self.mac_game()
        write(os.path.join(self.src, "dlc", "cdlc", "Custom_m.psarc"), song(999999), mtime=1000)
        self.copier().run()
        self.assertEqual(self.env(), ["RS_STEAM_DLC=248750,258341,999999"])
        write(os.path.join(self.src, "dlc", "cdlc", "New_m.psarc"), song(123456))
        write(os.path.join(self.src, "dlc", "cdlc", "Custom_m.psarc"), song(654321), mtime=2000)
        real = gamefiles.archive_appid
        read = []
        with mock.patch.object(gamefiles, "archive_appid", lambda p: read.append(os.path.basename(p)) or real(p)):
            gamefiles.refresh(self.dest, progress=quiet(), is_running=lambda e: False)
        self.assertEqual(sorted(read), ["Custom_m.psarc", "New_m.psarc"])
        self.assertEqual(self.env(), ["RS_STEAM_DLC=123456,248750,258341,654321"])
        # a removed archive's AppID goes with it
        os.remove(os.path.join(self.src, "dlc", "cdlc", "New_m.psarc"))
        gamefiles.refresh(self.dest, progress=quiet(), is_running=lambda e: False)
        self.assertEqual(self.env(), ["RS_STEAM_DLC=248750,258341,654321"])


class Sync(Base):
    def test_first_copy_clones_and_is_complete(self):
        self.mac_game()
        self.assertFalse(gamefiles.check(self.dest)[0])
        c = self.copier()
        c.run()
        self.assertTrue(gamefiles.check(self.dest)[0])
        self.assertEqual(self.read("static.psarc.original"), b"static.psarc")
        self.assertEqual(self.read("dlc/cdlc/Custom_m.psarc"), b"custom mac")
        self.assertFalse(os.path.exists(os.path.join(self.dest, "dlc", "song_p.psarc")))
        self.assertFalse(os.path.exists(os.path.join(self.dest, "Rocksmith2014.app")))
        m = self.manifest()
        self.assertEqual(m["edition"], "mac")
        self.assertEqual(len(m["files"]), 10)
        st = os.stat(os.path.join(self.src, "songs.psarc"))
        self.assertEqual(m["files"]["songs.psarc"]["mtime_ns"], st.st_mtime_ns)
        self.assertEqual(os.stat(os.path.join(self.dest, "songs.psarc")).st_mtime_ns, st.st_mtime_ns)
        with open(os.path.join(self.dest, ".rearmed", "steam.env")) as f:
            self.assertIn("RS_STEAM_DLC=248750,258341\n", f.read())
        # the scratch folder is on APFS: clones (and nothing copied)
        if self._clones_here():
            self.assertEqual((c.stats["cloned"], c.stats["copied"]), (10, 0))
        self.assertFalse(os.path.exists(os.path.join(self.dest, ".rearmed", "manifest.previous.json")))

    def test_real_copy_when_clones_do_not_work(self):
        self.mac_game()
        c = self.copier(clone=no_clone)
        c.run()
        self.assertEqual(c.stats["cloned"], 0)
        self.assertEqual(c.stats["copied"], 10)
        self.assertEqual(self.read("dlc/song_m.psarc"), b"mac song")

    def test_progress_lines(self):
        self.mac_game()
        out = io.StringIO()
        self.copier(clone=no_clone, progress=gamefiles.Progress(lines=True, stream=out)).run()
        lines = [l for l in out.getvalue().splitlines() if l.startswith("progress ")]
        self.assertTrue(lines)
        self.assertTrue(lines[-1].startswith("progress 1.0000 Copying the game files"))
        fractions = [float(l.split()[1]) for l in lines]
        self.assertEqual(fractions, sorted(fractions))

    def test_second_run_copies_nothing(self):
        self.mac_game()
        self.copier().run()
        c = self.copier(clone=no_clone)
        c.run()
        self.assertEqual(c.stats["copied"] + c.stats["cloned"], 0)
        self.assertEqual(c.stats["same"], 10)

    def test_space_check(self):
        self.mac_game()
        c = self.copier(clone=no_clone, free=lambda p: 1000)
        with self.assertRaises(Failure) as e:
            c.run()
        self.assertIn("not enough free space", str(e.exception))
        self.assertIn("needed", str(e.exception))
        self.assertFalse(gamefiles.check(self.dest)[0])
        # with clones nothing but the margin is needed
        if self._clones_here():
            self.copier(free=lambda p: gamefiles.MARGIN + 1).run()
            self.assertTrue(gamefiles.check(self.dest)[0])

    def test_refuses_overlap_and_non_game(self):
        self.mac_game()
        with self.assertRaises(Failure):
            Copier("mac", self.src, os.path.join(self.src, "copy"), progress=quiet(), is_running=lambda e: False).run()
        with self.assertRaises(Failure):
            Copier("mac", self.src, self.src, progress=quiet(), is_running=lambda e: False).run()
        empty = os.path.join(self.tmp, "empty")
        os.makedirs(empty)
        with self.assertRaises(Failure) as e:
            Copier("mac", empty, self.dest, progress=quiet(), is_running=lambda e: False).run()
        self.assertIn("not the game's folder", str(e.exception))

    def test_refuses_while_running(self):
        self.mac_game()
        with self.assertRaises(Failure) as e:
            self.copier(is_running=lambda ed: ed == "mac").run()
        self.assertIn("quit the game", str(e.exception))


class Resume(Base):
    def test_interrupted_copy_continues(self):
        self.mac_game()
        real_copy = gamefiles.copy_data
        calls = []

        def flaky(src, dst, on_bytes):
            calls.append(src)
            if len(calls) == 5:
                raise KeyboardInterrupt  # stopped half way
            real_copy(src, dst, on_bytes)
        with mock.patch.object(gamefiles, "copy_data", flaky), self.assertRaises(KeyboardInterrupt):
            self.copier(clone=no_clone).run()
        self.assertFalse(gamefiles.check(self.dest)[0])
        self.assertIn("incomplete", gamefiles.check(self.dest)[1])
        # a leftover part file from the stopped run
        write(os.path.join(self.dest, "songs.psarc" + gamefiles.TMP_SUFFIX), b"half")
        c = gamefiles.refresh(self.dest, progress=quiet(), is_running=lambda e: False, clone=no_clone)
        self.assertTrue(gamefiles.check(self.dest)[0])
        self.assertEqual(len(c), 10)
        self.assertFalse(os.path.exists(os.path.join(self.dest, "songs.psarc" + gamefiles.TMP_SUFFIX)))

    def test_stopped_refresh_still_removes(self):
        self.mac_game()
        self.copier().run()
        os.unlink(os.path.join(self.src, "dlc", "song_m.psarc"))

        calls = []

        def stop(src, dst):  # the first call is the check that clones work; the second is the copy
            calls.append(src)
            if len(calls) > 1:
                raise KeyboardInterrupt
            return gamefiles.clonefile(src, dst)
        write(os.path.join(self.src, "new.psarc"), b"new")  # something to copy, so the copy loop runs
        with self.assertRaises(KeyboardInterrupt):
            self.copier(clone=stop).run()
        self.assertFalse(gamefiles.check(self.dest)[0])
        self.assertTrue(os.path.exists(os.path.join(self.dest, "dlc", "song_m.psarc")))
        gamefiles.refresh(self.dest, progress=quiet(), is_running=lambda e: False)
        self.assertFalse(os.path.exists(os.path.join(self.dest, "dlc", "song_m.psarc")))


class Refresh(Base):
    def test_songs_the_enumerate_entry_copied(self):
        self.mac_game()
        self.copier().run()
        # what shims/steam/src/enumerate.h leaves: the archive with its size and time, and its name in the list
        for name in ("kept_m.psarc", "gone_m.psarc"):
            src = write(os.path.join(self.src, "dlc", name), name.encode())
            shutil.copy2(src, os.path.join(self.dest, "dlc", name))
        listed = os.path.join(self.dest, ".rearmed", gamefiles.ENUMERATED)
        write(listed, b"dlc/kept_m.psarc\ndlc/gone_m.psarc\n")
        write(os.path.join(self.dest, "dlc", "mine_m.psarc"), b"put in the copy by hand")
        os.unlink(os.path.join(self.src, "dlc", "gone_m.psarc"))
        self.assertTrue(gamefiles.check(self.dest)[0])  # complete before a refresh too
        c = gamefiles.refresh(self.dest, progress=quiet(), is_running=lambda e: False)
        self.assertIn("dlc/kept_m.psarc", c)  # now in the manifest, as any other file
        self.assertEqual(self.read("dlc/kept_m.psarc"), b"kept_m.psarc")
        self.assertFalse(os.path.exists(os.path.join(self.dest, "dlc", "gone_m.psarc")))
        self.assertEqual(self.read("dlc/mine_m.psarc"), b"put in the copy by hand")  # never one of the copy's
        self.assertFalse(os.path.exists(listed))

    def test_adds_changes_removes_within_the_set_only(self):
        self.mac_game()
        self.copier().run()
        # the game's own writes in the copy, and the install's own files: never touched
        write(os.path.join(self.dest, "Rocksmith.ini"), b"game's settings")
        write(os.path.join(self.dest, "Rocksmith2014.app", "Contents", "MacOS", "Rocksmith2014"), b"our build")
        write(os.path.join(self.dest, "Tmp", "previews", "steam9_m.psarc"), b"game download")
        # Steam: a new DLC, a changed archive, a DLC gone, a whole folder gone
        write(os.path.join(self.src, "dlc", "new_m.psarc"), b"new song")
        write(os.path.join(self.src, "songs.psarc"), b"songs v2", mtime=2000000000)
        os.unlink(os.path.join(self.src, "dlc", "cdlc", "Custom_m.psarc"))
        shutil.rmtree(os.path.join(self.src, "base"))
        c = gamefiles.refresh(self.dest, progress=quiet(), is_running=lambda e: False)
        self.assertIn("dlc/new_m.psarc", c)
        self.assertEqual(self.read("songs.psarc"), b"songs v2")
        self.assertEqual(self.read("dlc/new_m.psarc"), b"new song")
        self.assertFalse(os.path.exists(os.path.join(self.dest, "dlc", "cdlc")))  # emptied folder pruned
        self.assertFalse(os.path.exists(os.path.join(self.dest, "base")))
        self.assertEqual(self.read("Rocksmith.ini"), b"game's settings")
        self.assertEqual(self.read("Rocksmith2014.app/Contents/MacOS/Rocksmith2014"), b"our build")
        self.assertEqual(self.read("Tmp/previews/steam9_m.psarc"), b"game download")
        self.assertTrue(gamefiles.check(self.dest)[0])

    def test_same_size_other_time_is_hashed(self):
        self.mac_game()
        self.copier().run()
        p = os.path.join(self.src, "audio.psarc")
        os.utime(p, (1900000000, 1900000000))  # touched, same contents
        c = self.copier(clone=no_clone)
        c.run()
        self.assertEqual(c.stats["touched"], 1)
        self.assertEqual(c.stats["copied"], 0)
        self.assertEqual(os.stat(os.path.join(self.dest, "audio.psarc")).st_mtime_ns, os.stat(p).st_mtime_ns)
        write(p, b"AUDIO.PSARC"[:len(b"audio.psarc")], mtime=1950000000)  # same size, other contents
        c = self.copier(clone=no_clone)
        c.run()
        self.assertEqual(c.stats["copied"], 1)
        self.assertEqual(self.read("audio.psarc"), b"AUDIO.PSARC")

    def test_coarse_file_times_are_hashed_once(self):
        """A volume that keeps whole seconds: the copy's time is never its source's, and the files are read
        once more, not on every refresh."""
        self.mac_game()
        real_utime = os.utime

        def coarse(path, times=None, *, ns=None, **kw):
            if ns is not None and os.path.abspath(path).startswith(os.path.abspath(self.dest) + os.sep):
                ns = tuple(t - t % 10 ** 9 for t in ns)
            return real_utime(path, times, ns=ns, **kw) if ns is not None else real_utime(path, times, **kw)

        p = os.path.join(self.src, "audio.psarc")
        real_utime(p, ns=(1900000000123456789, 1900000000123456789))
        with mock.patch.object(gamefiles.os, "utime", coarse):
            self.copier(clone=no_clone).run()
            self.assertNotEqual(os.stat(os.path.join(self.dest, "audio.psarc")).st_mtime_ns, os.stat(p).st_mtime_ns)
            with mock.patch.object(gamefiles, "sha256", side_effect=AssertionError("read again")):
                c = self.copier(clone=no_clone)
                c.run()
            self.assertEqual(c.stats["copied"], 0)
            self.assertEqual(c.stats["touched"], 0)
            write(p, b"AUDIO.PSARC"[:len(b"audio.psarc")], mtime=1950000000)  # a changed source is still seen
            c = self.copier(clone=no_clone)
            c.run()
            self.assertEqual(c.stats["copied"], 1)

    def test_source_gone_keeps_the_copy(self):
        self.mac_game()
        self.copier().run()
        shutil.rmtree(self.src)
        with self.assertRaises(Failure) as e:
            gamefiles.refresh(self.dest, progress=quiet(), is_running=lambda e: False)
        self.assertIn("not there", str(e.exception))
        self.assertTrue(gamefiles.check(self.dest)[0])
        self.assertEqual(self.read("songs.psarc"), b"songs.psarc")

    def test_refresh_without_a_copy(self):
        os.makedirs(self.dest)
        with self.assertRaises(Failure):
            gamefiles.refresh(self.dest, progress=quiet())

    def test_clone_is_not_written_through(self):
        """A replaced file is a new file renamed over the old one: the source of a clone never changes."""
        self.mac_game()
        self.copier().run()
        write(os.path.join(self.src, "songs.psarc"), b"songs v2!", mtime=2000000000)
        self.copier().run()
        with open(os.path.join(self.src, "songs.psarc"), "rb") as f:
            self.assertEqual(f.read(), b"songs v2!")


class Links(Base):
    def test_old_mirror_links_are_removed_not_followed(self):
        self.mac_game()
        os.makedirs(self.dest)
        # the launcher's mirror before: links to the Steam folder's entries, dlc among them
        for n in ("dlc", "base", "songs.psarc", "Rocksmith2014.app"):
            os.symlink(os.path.join(self.src, n), os.path.join(self.dest, n))
        os.symlink("static.psarc.original", os.path.join(self.dest, "static.psarc"))  # a link inside: kept
        before = {d: sorted(os.listdir(os.path.join(self.src, d))) for d in ("dlc", "base")}
        self.copier().run()
        for d in ("dlc", "base"):
            self.assertFalse(os.path.islink(os.path.join(self.dest, d)))
            self.assertEqual(sorted(os.listdir(os.path.join(self.src, d))), before[d])
        self.assertTrue(os.path.islink(os.path.join(self.dest, "static.psarc")))
        self.assertFalse(os.path.lexists(os.path.join(self.dest, "Rocksmith2014.app")))
        self.assertTrue(os.path.isfile(os.path.join(self.src, "songs.psarc")))
        self.assertEqual(self.read("dlc/song_m.psarc"), b"mac song")
        self.assertFalse(os.path.exists(os.path.join(self.src, "dlc", "song_m.psarc" + gamefiles.TMP_SUFFIX)))

    def test_nested_link_in_the_copy_is_replaced(self):
        self.mac_game()
        self.copier().run()
        outside = os.path.join(self.tmp, "outside")
        os.makedirs(outside)
        shutil.rmtree(os.path.join(self.dest, "dlc", "cdlc"))
        os.symlink(outside, os.path.join(self.dest, "dlc", "cdlc"))
        os.unlink(os.path.join(self.dest, ".rearmed", "manifest.json"))
        os.utime(os.path.join(self.src, "dlc", "cdlc", "Custom_m.psarc"), (1800000000, 1800000000))
        self.copier().run()
        self.assertEqual(os.listdir(outside), [])
        self.assertFalse(os.path.islink(os.path.join(self.dest, "dlc", "cdlc")))
        self.assertEqual(self.read("dlc/cdlc/Custom_m.psarc"), b"custom mac")


class LearnPlay(Base):
    def test_learn_play_copy(self):
        depot = self.lp_game()
        self.mac_game()
        self.copier("learn-play", depot, dlc_from=[os.path.join(self.src, "dlc")]).run()
        m = self.manifest()
        self.assertEqual(m["edition"], "learn-play")
        self.assertEqual(self.read("dlc/song_p.psarc"), b"pc song")
        self.assertEqual(self.read("Rocksmith2014.exe"), b"Rocksmith2014.exe")
        with open(os.path.join(self.dest, ".rearmed", "steam.env")) as f:
            self.assertIn("RS_STEAM_LANGUAGE=german", f.read())  # Steam's own library's appmanifest
        # refresh: the Mac game's DLC folder gains a PC archive
        write(os.path.join(self.src, "dlc", "more_p.psarc"), b"more")
        gamefiles.refresh(self.dest, progress=quiet(), is_running=lambda e: False)
        self.assertEqual(self.read("dlc/more_p.psarc"), b"more")


def ticket(account, licenses, app=221680):
    """An app ownership ticket as localconfig.vdf keeps it (hex)."""
    body = struct.pack("<IQIIIIII", 4, 76561197960265728 + account, app, 0, 0, 0, 0, 0)
    body += struct.pack("<H", len(licenses)) + b"".join(struct.pack("<I", p) for p in licenses) + b"\0\0"
    return (struct.pack("<I", len(body) + 4) + body).hex()


def packageinfo(packages):
    """A binary appcache/packageinfo.vdf (v40) with {package id: [depot ids]}."""
    out = struct.pack("<II", 0x06565528, 1)
    for pid, depots in packages.items():
        kv = b"\0" + str(pid).encode() + b"\0" + b"\2packageid\0" + struct.pack("<i", pid)
        kv += b"\0appids\0\x02" + b"0\0" + struct.pack("<i", 221680) + b"\x08"
        kv += b"\0depotids\0" + b"".join(b"\2%d\0" % i + struct.pack("<i", d) for i, d in enumerate(depots))
        kv += b"\x08\x08\x08"
        out += struct.pack("<I", pid) + b"\0" * 20 + struct.pack("<I", 1) + b"\0" * 8 + kv
    return out + struct.pack("<I", 0xFFFFFFFF)


class TitleLogo(Base):
    """static.psarc in the copy: a link to the original, or to the title logo (the launcher, the Learn & Play app)."""

    def link(self):
        return os.readlink(os.path.join(self.dest, "static.psarc"))

    def logo(self):
        """A title logo made in the copy, in use, as a Learn & Play build and start leave it."""
        write(os.path.join(self.dest, "static.psarc.rearmed"), b"tagged")
        os.unlink(os.path.join(self.dest, "static.psarc"))
        os.symlink("static.psarc.rearmed", os.path.join(self.dest, "static.psarc"))

    def test_both_editions_link_the_original(self):
        self.mac_game()
        self.copier().run()
        self.assertEqual(self.link(), "static.psarc.original")
        self.assertEqual(self.read("static.psarc"), b"static.psarc")
        shutil.rmtree(self.dest)
        self.copier("learn-play", self.lp_game()).run()
        self.assertEqual(self.link(), "static.psarc.original")
        self.assertEqual(self.read("static.psarc"), b"static.psarc")
        self.assertEqual(gamefiles.check(self.dest)[0], True)

    def test_copy_from_before_the_link(self):
        depot = self.lp_game()
        self.copier("learn-play", depot).run()
        m = self.manifest()  # as an earlier version left it: the file itself, under its own name
        m["files"]["static.psarc"] = m["files"].pop("static.psarc.original")
        with open(os.path.join(self.dest, ".rearmed", "manifest.json"), "w") as f:
            json.dump(m, f)
        os.unlink(os.path.join(self.dest, "static.psarc"))
        os.rename(os.path.join(self.dest, "static.psarc.original"), os.path.join(self.dest, "static.psarc"))
        self.copier("learn-play", depot).run()
        self.assertEqual(self.link(), "static.psarc.original")
        self.assertEqual(self.read("static.psarc.original"), b"static.psarc")
        self.assertNotIn("static.psarc", self.manifest()["files"])

    def test_refresh_keeps_the_logo_and_the_choice(self):
        depot = self.lp_game()
        self.copier("learn-play", depot).run()
        self.logo()
        self.copier("learn-play", depot).run()
        self.assertEqual(self.link(), "static.psarc.rearmed")
        self.assertEqual(self.read("static.psarc"), b"tagged")

    def test_new_static_psarc_drops_the_logo_made_from_the_old_one(self):
        depot = self.lp_game()
        self.copier("learn-play", depot).run()
        self.logo()
        write(os.path.join(depot, "static.psarc"), b"updated")
        self.copier("learn-play", depot).run()
        self.assertFalse(os.path.lexists(os.path.join(self.dest, "static.psarc.rearmed")))
        self.assertEqual(self.link(), "static.psarc.original")
        self.assertEqual(self.read("static.psarc"), b"updated")

    def test_a_logo_in_the_game_folder_is_left_there(self):
        # Earlier Mac builds wrote one to Steam's folder: it is not the copy's business now.
        self.mac_game()
        write(os.path.join(self.src, "static.psarc.rearmed"), b"old logo")
        self.copier().run()
        self.assertFalse(os.path.exists(os.path.join(self.dest, "static.psarc.rearmed")))
        self.assertEqual(self.link(), "static.psarc.original")

    def test_a_logo_an_earlier_version_copied_stays_until_the_game_changes(self):
        self.mac_game()
        self.copier().run()
        write(os.path.join(self.dest, "static.psarc.rearmed"), b"tagged")
        path = os.path.join(self.dest, ".rearmed", "manifest.json")
        m = gamefiles.read_json(path)
        m["files"]["static.psarc.rearmed"] = {"size": 6, "mtime_ns": 1, "src": "x"}
        gamefiles.write_json(path, m)
        self.copier().run()
        self.assertEqual(self.read("static.psarc.rearmed"), b"tagged")
        write(os.path.join(self.src, "static.psarc"), b"updated")
        self.copier().run()
        self.assertFalse(os.path.exists(os.path.join(self.dest, "static.psarc.rearmed")))


class Account(Base):
    MAC, LP = 11111111, 22222222

    def steam_setup(self, last_owner=None, lp_owner=True):
        self.mac_game()
        text = MANIFEST if not last_owner else MANIFEST.replace(
            '"installdir"', '"LastOwner"\t\t"%d"\n\t"installdir"' % (76561197960265728 + last_owner))
        write(os.path.join(self.lib, "appmanifest_221680.acf"), text.encode())
        write(os.path.join(self.steam, "appcache", "packageinfo.vdf"),
              packageinfo({29700: [221681, 221682], 1182935: [221683], 555: [1]}))
        for acct, lic, play in ((self.MAC, [29700], 106524), (self.LP, [1182935] if lp_owner else [555], 29)):
            write(os.path.join(self.steam, "userdata", str(acct), "config", "localconfig.vdf"), (
                '"UserLocalConfigStore"\n{\n\t"apptickets"\n\t{\n\t\t"221680"\t\t"%s"\n\t}\n'
                '\t"Software" { "Valve" { "Steam" { "apps" { "221680" {\n\t\t"cloud" { "x" "1" }\n'
                '\t\t"Playtime"\t\t"%d"\n\t} } } } }\n}\n' % (ticket(acct, lic), play)).encode())

    def env_id(self):
        with open(os.path.join(self.dest, ".rearmed", "steam.env")) as f:
            m = [l for l in f.read().splitlines() if l.startswith("RS_STEAM_ID=")]
        return int(m[0].split("=")[1]) - 76561197960265728 if m else None

    def test_parsers(self):
        self.steam_setup()
        self.assertEqual(gamefiles.ticket_licenses(ticket(self.MAC, [29700, 7])), {29700, 7})
        self.assertEqual(gamefiles.ticket_licenses(ticket(self.MAC, [1], app=1)), set())
        self.assertEqual(gamefiles.ticket_licenses("zz"), set())
        pk = gamefiles.package_depots(os.path.join(self.steam, "appcache", "packageinfo.vdf"))
        self.assertEqual(pk[29700], {221681, 221682})
        self.assertEqual(pk[1182935], {221683})
        self.assertEqual(gamefiles.steam_accounts(self.steam),
                         {self.MAC: ({29700}, 106524), self.LP: ({1182935}, 29)})
        self.assertEqual(gamefiles.edition_depots("mac", gamefiles.find_appmanifest(self.src, self.steam)),
                         {221682})
        self.assertEqual(gamefiles.account_of("76561198000000000"), 39734272)
        self.assertEqual(gamefiles.account_of(42), 42)
        self.assertIsNone(gamefiles.account_of("x"))

    def test_each_edition_its_license_owner(self):
        # Steam last updated the game while the Learn & Play account was logged in: LastOwner is that one
        self.steam_setup(last_owner=self.LP)
        am = gamefiles.find_appmanifest(self.src, self.steam)
        self.assertEqual(gamefiles.steam_account("mac", self.steam, am)[0], self.MAC)
        self.assertEqual(gamefiles.steam_account("learn-play", self.steam, am)[0], self.LP)
        self.copier().run()
        self.assertEqual(self.env_id(), self.MAC)
        self.dest = os.path.join(self.tmp, "lp", "game")
        self.copier("learn-play", self.lp_game(), dlc_from=[os.path.join(self.src, "dlc")]).run()
        self.assertEqual(self.env_id(), self.LP)

    def test_override_and_refresh(self):
        self.steam_setup()
        os.environ["RS_STEAM_ID"] = str(76561197960265728 + self.LP)
        self.copier().run()
        self.assertEqual(self.env_id(), self.LP)
        del os.environ["RS_STEAM_ID"]
        gamefiles.refresh(self.dest, progress=quiet(), is_running=lambda e: False)
        self.assertEqual(self.env_id(), self.MAC)  # the license decides again

    def test_no_clear_owner(self):
        # no account has the Learn & Play license: LastOwner, else the longest player; an earlier choice stays
        self.steam_setup(last_owner=self.LP, lp_owner=False)
        am = gamefiles.find_appmanifest(self.src, self.steam)
        self.assertEqual(gamefiles.steam_account("learn-play", self.steam, am),
                         (self.LP, "the appmanifest's LastOwner"))
        self.steam_setup(lp_owner=False)
        self.assertEqual(gamefiles.steam_account("learn-play", self.steam, am)[0], self.MAC)
        os.environ["RS_STEAM_ID"] = str(self.LP)
        self.copier().run()
        del os.environ["RS_STEAM_ID"]
        # without Steam's package list no license decides: the guess (the longest player) keeps the earlier choice
        os.unlink(os.path.join(self.steam, "appcache", "packageinfo.vdf"))
        self.assertEqual(gamefiles.steam_account("mac", self.steam, am)[0], self.MAC)
        gamefiles.refresh(self.dest, progress=quiet(), is_running=lambda e: False)
        self.assertEqual(self.env_id(), self.LP)

    def test_no_steam_accounts(self):
        self.mac_game()
        self.copier().run()
        self.assertIsNone(self.env_id())


FRENCH = MANIFEST.replace('"german"', '"french"').replace('"258341" {', '"999999" { "dlcappid" "999999" } "258341" {')


class ExternalDrive(Base):
    """A Steam library on another drive: the copy goes onto that drive (clones), the edition's path is a link to
    it. The volumes are made up: <tmp>/Volumes/Ext counts as a volume of its own (an injected device id), so
    gamefiles takes it for an external drive; the files are still on the scratch folder's APFS volume, so the
    clones are real."""

    def setUp(self):
        super().setUp()
        self.volumes = os.path.join(os.path.realpath(self.tmp), "Volumes")
        self.ext = os.path.join(self.volumes, "Ext")
        self.ext_lib = os.path.join(self.ext, "SteamLibrary")
        self.src = os.path.join(self.ext_lib, "steamapps", "common", "Rocksmith2014")
        self.mac_game()  # the game, its dlc/ and an appmanifest next to it (german)
        # Steam's own library (on the startup disk) has another appmanifest: the library's must win
        write(os.path.join(self.lib, "appmanifest_221680.acf"), MANIFEST.encode())
        write(os.path.join(self.ext_lib, "steamapps", "appmanifest_221680.acf"), FRENCH.encode())
        p = mock.patch.object(gamefiles, "VOLUMES", self.volumes)
        p.start()
        self.addCleanup(p.stop)

    def device(self, path):
        rp = os.path.realpath(path)
        if rp == self.ext or rp.startswith(self.ext + os.sep):
            return -1  # "another volume"
        return os.stat(path).st_dev

    def placement(self, *a, **kw):
        return gamefiles.place(*a, device=self.device, **kw)

    def copier(self, edition="mac", source=None, **kw):
        kw.setdefault("placement", self.placement)
        return super().copier(edition, source, **kw)

    def mounted(self, connected):
        p = mock.patch.object(gamefiles, "mounted", lambda name: connected)
        p.start()
        self.addCleanup(p.stop)

    def test_external_library_end_to_end(self):
        os.makedirs(self.dest)  # install.sh made the empty folder before: still a new copy
        self.copier().run()
        target = os.path.join(self.ext, "RS Re-ARMed", "game")
        self.assertTrue(os.path.islink(self.dest))
        self.assertEqual(os.readlink(self.dest), target)
        self.assertTrue(gamefiles.check(self.dest)[0])
        self.assertEqual(self.read("dlc/cdlc/Custom_m.psarc"), b"custom mac")
        self.assertTrue(os.path.isfile(os.path.join(target, ".rearmed", "manifest.json")))
        self.assertEqual(gamefiles.read_json(os.path.join(target, ".rearmed", "link.json"))["link"], self.dest)
        # PR #2: the appmanifest next to the source library, not Steam's own
        with open(os.path.join(self.dest, ".rearmed", "steam.env")) as f:
            env = f.read()
        self.assertIn("RS_STEAM_DLC=248750,258341,999999\n", env)
        self.assertIn("RS_STEAM_LANGUAGE=french\n", env)
        self.assertEqual(gamefiles.find_appmanifest(self.src, self.steam),
                         os.path.join(self.ext_lib, "steamapps", "appmanifest_221680.acf"))
        # a refresh keeps the place
        write(os.path.join(self.src, "dlc", "new_m.psarc"), b"new")
        gamefiles.refresh(self.dest, progress=quiet(), is_running=lambda e: False)
        self.assertEqual(os.readlink(self.dest), target)
        self.assertEqual(self.read("dlc/new_m.psarc"), b"new")
        # the Learn & Play copy: next to it, with the PC DLC from the external library's dlc folder
        depot = self.lp_game(os.path.join(self.ext, "depot"))
        lp_dest = os.path.join(self.tmp, "home", "learn-play", "game")
        Copier("learn-play", depot, lp_dest, [os.path.join(self.src, "dlc")], self.steam, quiet(),
               is_running=lambda e: False, placement=self.placement).run()
        self.assertEqual(os.readlink(lp_dest), os.path.join(self.ext, "RS Re-ARMed", "learn-play", "game"))
        self.assertTrue(gamefiles.check(lp_dest)[0])
        with open(os.path.join(lp_dest, "dlc", "song_p.psarc"), "rb") as f:
            self.assertEqual(f.read(), b"pc song")

    def test_no_clones_there_real_copy_at_home(self):
        self.copier(clone=no_clone).run()
        self.assertFalse(os.path.islink(self.dest))
        self.assertTrue(gamefiles.check(self.dest)[0])
        self.assertFalse(os.path.exists(os.path.join(self.ext, "RS Re-ARMed")))

    def test_top_of_the_volume_not_writable(self):
        os.chmod(self.ext, 0o555)
        self.addCleanup(os.chmod, self.ext, 0o755)
        self.copier().run()
        self.assertEqual(os.readlink(self.dest), os.path.join(self.ext_lib, "RS Re-ARMed", "game"))

    def test_existing_copy_at_home_stays(self):
        self.copier(placement=None).run()  # an install from before
        self.copier().run()
        self.assertFalse(os.path.islink(self.dest))
        self.assertTrue(gamefiles.check(self.dest)[0])
        self.assertFalse(os.path.exists(os.path.join(self.ext, "RS Re-ARMed")))

    def test_drive_not_connected(self):
        self.copier().run()
        away = self.ext + "-unmounted"
        os.rename(self.ext, away)
        self.mounted(False)
        ok, msg = gamefiles.check(self.dest)
        self.assertFalse(ok)
        self.assertIn("The drive with the game files (Ext) isn't connected", msg)
        for run in (lambda: gamefiles.refresh(self.dest, quiet(), is_running=lambda e: False),
                    lambda: self.copier().run(), lambda: self.copier(placement=None).run()):
            with self.assertRaises(Failure) as e:
                run()
            self.assertIn("isn't connected", str(e.exception))
        self.assertTrue(os.path.islink(self.dest))  # left as it is
        os.rename(away, self.ext)
        self.assertTrue(gamefiles.check(self.dest)[0])

    def test_copy_on_the_drive_gone(self):
        self.copier().run()
        shutil.rmtree(os.path.join(self.ext, "RS Re-ARMed"))
        self.mounted(True)
        ok, msg = gamefiles.check(self.dest)
        self.assertFalse(ok)
        self.assertIn("are gone", msg)
        self.copier().run()  # an install again: a new copy there
        self.assertTrue(gamefiles.check(self.dest)[0])
        self.assertEqual(os.readlink(self.dest), os.path.join(self.ext, "RS Re-ARMed", "game"))

    def test_second_install_gets_its_own(self):
        self.copier().run()
        other = os.path.join(self.tmp, "home2", "game")
        Copier("mac", self.src, other, steam=self.steam, progress=quiet(), is_running=lambda e: False,
               placement=self.placement).run()
        self.assertEqual(os.readlink(other), os.path.join(self.ext, "RS Re-ARMed", "game 2"))
        self.assertEqual(os.readlink(self.dest), os.path.join(self.ext, "RS Re-ARMed", "game"))
        # the first install's folder deleted, the copy left: a new install takes it over
        os.unlink(self.dest)
        third = os.path.join(self.tmp, "home3", "game")
        Copier("mac", self.src, third, steam=self.steam, progress=quiet(), is_running=lambda e: False,
               placement=self.placement).run()
        self.assertEqual(os.readlink(third), os.path.join(self.ext, "RS Re-ARMed", "game"))

    def test_override(self):
        os.environ["RS_GAME_COPY_DIR"] = os.path.join(self.tmp, "chosen")
        self.copier().run()
        self.assertEqual(os.readlink(self.dest), os.path.join(self.tmp, "chosen", "game"))
        self.assertTrue(gamefiles.check(self.dest)[0])

    def test_override_home(self):
        os.environ["RS_GAME_COPY_DIR"] = "home"
        self.copier().run()
        self.assertFalse(os.path.islink(self.dest))

    def test_same_volume_no_link(self):
        src = os.path.join(self.tmp, "Steam", "steamapps", "common", "Rocksmith2014")
        shutil.copytree(self.src, src)
        self.copier(source=src).run()
        self.assertFalse(os.path.islink(self.dest))


class DiskImage(Base):
    """The same with a real APFS volume: a small sparse disk image attached under the scratch folder (no
    privileges needed; skipped where hdiutil cannot). HFS+ (no clones): a real copy at home."""
    FS = "APFS"

    def setUp(self):
        super().setUp()
        if sys.platform != "darwin" or not os.path.exists("/usr/bin/hdiutil"):
            self.skipTest("no hdiutil")
        img = os.path.join(self.tmp, "ext.sparseimage")
        self.mnt = os.path.join(self.tmp, "mnt")
        os.makedirs(self.mnt)
        r = subprocess.run(["/usr/bin/hdiutil", "create", "-quiet", "-size", "64m", "-fs", self.FS, "-type",
                            "SPARSE", "-volname", "RSReARMedTest", img], capture_output=True)
        if r.returncode:
            self.skipTest("hdiutil create: %s" % r.stderr.decode(errors="replace").strip())
        r = subprocess.run(["/usr/bin/hdiutil", "attach", "-quiet", "-nobrowse", "-noautoopen", "-owners", "on",
                            "-mountpoint", self.mnt, img], capture_output=True)
        if r.returncode:
            self.skipTest("hdiutil attach: %s" % r.stderr.decode(errors="replace").strip())
        self.addCleanup(subprocess.run, ["/usr/bin/hdiutil", "detach", "-quiet", "-force", self.mnt],
                        capture_output=True)
        self.lib = os.path.join(self.mnt, "SteamLibrary", "steamapps")
        self.src = os.path.join(self.lib, "common", "Rocksmith2014")

    def test_copy_on_the_image(self):
        self.mac_game()
        before = gamefiles.free_bytes(self.mnt)
        write(os.path.join(self.src, "big.psarc"), b"\0" * (8 << 20))
        self.copier().run()  # the real place(): real devices and clonefile
        target = os.path.join(os.path.realpath(self.mnt), "RS Re-ARMed", "game")
        self.assertEqual(os.readlink(self.dest), target)
        self.assertTrue(gamefiles.check(self.dest)[0])
        # clones: the 8 MB file takes no second 8 MB
        self.assertGreater(gamefiles.free_bytes(self.mnt), before - (12 << 20))
        self.assertEqual(os.path.getsize(os.path.join(self.dest, "big.psarc")), 8 << 20)


class DiskImageHFS(DiskImage):
    FS = "HFS+"

    def test_copy_on_the_image(self):
        self.mac_game()
        self.copier().run()
        self.assertFalse(os.path.islink(self.dest))
        self.assertTrue(gamefiles.check(self.dest)[0])
        self.assertFalse(os.path.exists(os.path.join(self.mnt, "RS Re-ARMed")))
        self.assertEqual(self.read("dlc/cdlc/Custom_m.psarc"), b"custom mac")


class Review(Base):
    """Things a copy must survive."""

    def source(self):
        src = os.path.join(self.tmp, "Rocksmith2014")
        for n in ("static.psarc", "songs.psarc", "metadata"):
            write(os.path.join(src, n), n.encode())
        write(os.path.join(src, "dlc", "song_m.psarc"), b"song")
        return src

    def test_a_cache_the_overlay_cannot_read_does_not_stop_the_copy(self):
        src = self.source()
        write(os.path.join(src, "cache.psarc"), b"PSAR" + b"\xff" * 40)
        dest = os.path.join(self.tmp, "copy")
        with mock.patch.object(gamefiles.cache_overlay, "make", side_effect=IndexError("bad table")):
            Copier("mac", src, dest, progress=quiet(), is_running=lambda e: False).run()
        self.assertEqual(gamefiles.check(dest)[0], True)

    def test_no_room_leaves_the_copy_working(self):
        src = self.source()
        dest = os.path.join(self.tmp, "copy")
        Copier("mac", src, dest, progress=quiet(), is_running=lambda e: False).run()
        write(os.path.join(src, "dlc", "new_m.psarc"), b"x" * 100)
        c = Copier("mac", src, dest, progress=quiet(), is_running=lambda e: False, free=lambda p: 0, clone=no_clone)
        with self.assertRaises(Failure):
            c.run()
        self.assertEqual(gamefiles.check(dest)[0], True)

    def test_songs_of_a_folder_that_is_away_stay(self):
        src = self.source()
        extra = os.path.join(self.tmp, "Mac game dlc")
        write(os.path.join(extra, "other_p.psarc"), b"other")
        write(os.path.join(src, "Rocksmith2014.exe"), b"exe")
        dest = os.path.join(self.tmp, "copy")
        Copier("learn-play", src, dest, [extra], progress=quiet(), is_running=lambda e: False).run()
        kept = os.path.join(dest, "dlc", "other_p.psarc")
        self.assertTrue(os.path.isfile(kept))
        shutil.rmtree(extra)  # a drive taken away, the Mac game uninstalled
        gamefiles.refresh(dest, quiet(), is_running=lambda e: False)
        self.assertTrue(os.path.isfile(kept))
        self.assertEqual(gamefiles.check(dest)[0], True)
        # A new full install, which no longer finds the folder, too; and later refreshes.
        Copier("learn-play", src, dest, [], progress=quiet(), is_running=lambda e: False).run()
        gamefiles.refresh(dest, quiet(), is_running=lambda e: False)
        self.assertTrue(os.path.isfile(kept))

    def test_a_rename_that_only_changes_the_case_keeps_the_file(self):
        src = self.source()
        dest = os.path.join(self.tmp, "copy")
        Copier("mac", src, dest, progress=quiet(), is_running=lambda e: False).run()
        os.rename(os.path.join(src, "dlc", "song_m.psarc"), os.path.join(src, "dlc", "x"))
        os.rename(os.path.join(src, "dlc", "x"), os.path.join(src, "dlc", "Song_m.psarc"))
        gamefiles.refresh(dest, quiet(), is_running=lambda e: False)
        self.assertTrue(os.path.isfile(os.path.join(dest, "dlc", "Song_m.psarc")))

    def test_not_while_steam_changes_the_game(self):
        lib = os.path.join(self.tmp, "lib", "steamapps")
        src = os.path.join(lib, "common", "Rocksmith2014")
        for n in ("static.psarc", "songs.psarc", "metadata"):
            write(os.path.join(src, n), n.encode())
        write(os.path.join(lib, "appmanifest_221680.acf"), b'"AppState" { "StateFlags" "1026" }')
        with self.assertRaises(Failure) as e:
            Copier("mac", src, os.path.join(self.tmp, "copy"), progress=quiet(), is_running=lambda e: False).run()
        self.assertIn("updating", str(e.exception))

    def test_a_refresh_finds_dlc_that_came_after_the_first_copy(self):
        src = self.source()
        write(os.path.join(src, "Rocksmith2014.exe"), b"exe")
        steam = os.path.join(self.tmp, "Steam")
        dest = os.path.join(self.tmp, "copy")
        Copier("learn-play", src, dest, [], steam, progress=quiet(), is_running=lambda e: False).run()
        write(os.path.join(steam, "steamapps", "common", "Rocksmith2014", "dlc", "new_p.psarc"), b"new")
        gamefiles.refresh(dest, quiet(), is_running=lambda e: False)
        self.assertTrue(os.path.isfile(os.path.join(dest, "dlc", "new_p.psarc")))

    def test_new_dlc_reaches_the_copy_after_steam_cleared_its_folder(self):
        src = self.source()
        write(os.path.join(src, "Rocksmith2014.exe"), b"exe")
        extra = os.path.join(self.tmp, "dlc2")
        write(os.path.join(extra, "old_p.psarc"), b"old")
        dest = os.path.join(self.tmp, "copy")
        Copier("learn-play", src, dest, [extra], progress=quiet(), is_running=lambda e: False).run()
        shutil.rmtree(src)
        write(os.path.join(extra, "new_p.psarc"), b"new")
        gamefiles.refresh(dest, quiet(), is_running=lambda e: False)
        for name in ("songs.psarc", "Rocksmith2014.exe", os.path.join("dlc", "new_p.psarc")):
            self.assertTrue(os.path.isfile(os.path.join(dest, name)), name)
        self.assertEqual(gamefiles.check(dest)[0], True)

    def test_a_song_deleted_from_a_folder_that_is_there_goes(self):
        src = self.source()
        dest = os.path.join(self.tmp, "copy")
        Copier("mac", src, dest, progress=quiet(), is_running=lambda e: False).run()
        os.remove(os.path.join(src, "dlc", "song_m.psarc"))
        gamefiles.refresh(dest, quiet(), is_running=lambda e: False)
        self.assertFalse(os.path.exists(os.path.join(dest, "dlc", "song_m.psarc")))

    def test_a_dlc_folder_gone_from_the_game_keeps_its_songs(self):
        src = self.source()
        dest = os.path.join(self.tmp, "copy")
        Copier("mac", src, dest, progress=quiet(), is_running=lambda e: False).run()
        shutil.rmtree(os.path.join(src, "dlc"))
        os.symlink(os.path.join(self.tmp, "gone"), os.path.join(src, "dlc"))  # a disk that is not mounted
        gamefiles.refresh(dest, quiet(), is_running=lambda e: False)
        self.assertTrue(os.path.isfile(os.path.join(dest, "dlc", "song_m.psarc")))


class FakeSteamState:
    """Steam as a test says it is."""

    def __init__(self, st):
        self.st = st
        self.urls = []

    def __call__(self, steam):
        return self.st

    def open(self, url=None):
        self.urls.append(url)


class Clock:
    def __init__(self):
        self.t = 0.0
        self.hooks = []

    def __call__(self):
        return self.t

    def sleep(self, s):
        self.t += s
        while self.hooks and self.hooks[0][0] <= self.t:
            self.hooks.pop(0)[1]()


class SteamSteps(Base):
    """What the installers read from Steam's folder, and the walk through Steam for the Mac game."""

    def setUp(self):
        Base.setUp(self)
        self.steam = os.path.join(self.tmp, "Steam")
        self.stream = io.StringIO()
        self.guide = gamefiles.Guide(True, self.stream)

    def codes(self):
        return [line.split("\t")[1] for line in self.stream.getvalue().splitlines()]

    def conn(self, *lines):
        write(os.path.join(self.steam, "logs", "connection_log.txt"), "".join(l + "\n" for l in lines).encode())

    def test_connection_log(self):
        self.assertEqual(gamefiles.steam_connection(self.steam), (None, 0))
        self.conn("[2026-11-04 02:04:21] [Connected, 4, 7] [U:1:42] Logging on [U:1:42]",
                  "[2026-11-04 02:04:21] [Logged On, 4, 7] [U:1:42] RecvMsgClientLogOnResponse() : processing complete",
                  "[2026-11-04 02:04:21] CClientJobGetClientUpdateHosts: cached version not expired")
        self.assertEqual(gamefiles.steam_connection(self.steam), ("Logged On", 42))
        self.conn("[2026-11-03 22:16:49] [Logged On, 4, 7] [U:1:42] LogOff()",
                  "[2026-11-03 22:16:50] [Logged Off, 0, 0] [U:1:42] Log session ended")
        self.assertEqual(gamefiles.steam_connection(self.steam)[0], "Logged Off")

    def test_account_name(self):
        write(os.path.join(self.steam, "config", "loginusers.vdf"),
              b'"users"\n{\n\t"%d"\n\t{\n\t\t"AccountName"\t\t"somebody"\n\t}\n}\n'
              % (gamefiles.STEAMID64_BASE + 42))
        self.assertEqual(gamefiles.steam_account_name(self.steam, 42), "somebody")
        self.assertEqual(gamefiles.steam_account_name(self.steam, 43), "this account")

    def test_steam_home(self):
        self.assertEqual(gamefiles.steam_home("/Users/x/Library/Application Support/Steam"), "/Users/x")

    def gate(self, st, clock):
        self.fake = FakeSteamState(st)
        return gamefiles.SteamGate(self.steam, gamefiles.GAME_MAC, self.guide, state=self.fake,
                                   opener=self.fake.open, clock=clock)

    def installed(self, flags=b"4"):
        write(os.path.join(self.steam, "steamapps", "common", "Rocksmith2014", gamefiles.MAC_GAME_BIN), b"x")
        write(os.path.join(self.steam, "steamapps", "appmanifest_221680.acf"),
              b'"AppState" { "StateFlags" "%s" "BytesToDownload" "2097152" "BytesDownloaded" "1048576" }' % flags)

    def test_the_game_found_in_another_library(self):
        lib = os.path.join(self.tmp, "Library2")
        write(os.path.join(self.steam, "steamapps", "libraryfolders.vdf"), b'"path" "%s"' % lib.encode())
        write(os.path.join(lib, "steamapps", "common", "Rocksmith2014", gamefiles.MAC_GAME_BIN), b"x")
        write(os.path.join(lib, "steamapps", "appmanifest_221680.acf"), b'"AppState" { "StateFlags" "4" }')
        self.assertEqual(gamefiles.find_mac_game(self.steam), os.path.join(lib, "steamapps", "common", "Rocksmith2014"))

    def test_a_folder_the_user_chose_needs_no_steam(self):
        folder = os.path.join(self.tmp, "depot")
        write(os.path.join(folder, gamefiles.MAC_GAME_BIN), b"x")
        clock = Clock()
        got = gamefiles.mac_game_wait(self.steam, folder, quiet(), self.guide, clock=clock, sleep=clock.sleep,
                                      gate=self.gate(gamefiles.SteamState(False, False, False), clock))
        self.assertEqual(got, folder)
        self.assertEqual(self.codes(), [])

    def test_the_walk_through_steam(self):
        clock = Clock()
        gate = self.gate(gamefiles.SteamState(False, False, False), clock)

        def state(*a):
            def set_it():
                self.fake.st = gamefiles.SteamState(*a)
            return set_it
        clock.hooks += [(10, state(True, False, False)), (20, state(True, True, False)),
                        (100, state(True, True, True, 42, "somebody")),
                        (120, lambda: self.installed(b"1026")), (130, lambda: self.installed())]
        got = gamefiles.mac_game_wait(self.steam, None, quiet(), self.guide, interval=1, gate=gate, clock=clock,
                                      sleep=clock.sleep, opener=self.fake.open)
        self.assertTrue(got.endswith("Rocksmith2014"))
        self.assertEqual(self.codes(), ["steam-missing", "steam-starting", "sign-in", "install-game",
                                        "installing-game", "-"])
        self.assertEqual(self.fake.urls, [None, "steam://install/221680"])

    def test_guide_in_a_terminal_says_each_thing_once(self):
        out = io.StringIO()
        g = gamefiles.Guide(False, out)
        g("sign-in", game="the game")
        g("sign-in", game="the game")
        self.assertEqual(out.getvalue().count("Sign in to Steam"), 1)
        self.assertIn("the game", out.getvalue())

    def test_every_guide_message_fills_in(self):
        for code in gamefiles.GUIDE:
            gamefiles.Guide(True, io.StringIO())(code, game="g", name="n", why="w", command="c")


def lp_ticket(dlc, app=221680):
    b = struct.pack("<IIQIIII", 40, 4, 76561197960265770, app, 0, 0, 0) + struct.pack("<II", 0, 0)
    b = b[:40] + struct.pack("<HI", 1, 1182935) + struct.pack("<H", len(dlc))
    for d in dlc:
        b += struct.pack("<IHI", d, 1, 1000 + d)
    return b.hex()


class Dlc(SteamSteps):
    """Learn & Play's DLC: a depot each, fetched from the Steam the test plays."""

    ACCOUNT = 43

    def setUp(self):
        SteamSteps.setUp(self)
        self.state = os.path.join(self.tmp, "fetched.json")
        self.clock = Clock()
        self.st = gamefiles.SteamState(True, True, True, self.ACCOUNT, "x")
        self.sent = []
        self.answers = {}
        self.console = os.path.join(self.steam, "logs", "console_log.txt")
        write(self.console, b"")

    def own(self, dlc):
        write(os.path.join(self.steam, "userdata", str(self.ACCOUNT), "config", "localconfig.vdf"),
              ('"UserLocalConfigStore" { "apptickets" { "221680" "%s" } }' % lp_ticket(dlc)).encode())

    def log(self, line):
        with open(self.console, "a") as f:
            f.write("[2026-11-04 07:31:23] %s\n" % line)

    def send(self, app, depot):
        self.sent.append((app, depot))
        answer = self.answers.get((app, depot), self.answers.get(depot, "complete"))
        if answer == "complete":
            write(os.path.join(self.steam, "steamapps", "content", "app_%d" % app, "depot_%d" % depot, "dlc",
                               "s%d_p.psarc" % depot), b"song")
            self.log('Depot download complete : "x\\content\\app_%d\\depot_%d" (manifest 1) ' % (app, depot))
        elif answer:
            self.log("Depot download failed : %s " % answer)

    def fetch(self, wait=120.0):
        gate = gamefiles.SteamGate(self.steam, gamefiles.GAME_LP, self.guide, state=lambda s: self.st,
                                   opener=lambda u=None: None, clock=self.clock)
        self.out = quiet()
        f = gamefiles.DlcFetch(self.steam, self.state, self.out, self.guide, wait, gate, self.send, self.clock,
                               self.clock.sleep)
        return f.run()

    def test_ticket(self):
        self.assertEqual(gamefiles.ticket_dlc(lp_ticket([294990, 899893])), [294990, 899893])
        self.assertEqual(gamefiles.ticket_dlc(lp_ticket([])), [])
        self.assertIsNone(gamefiles.ticket_dlc(lp_ticket([1], app=7)))
        self.assertIsNone(gamefiles.ticket_dlc("zz"))
        self.own([5, 6])
        self.assertEqual(gamefiles.account_dlc(self.steam, self.ACCOUNT), [5, 6])
        self.assertIsNone(gamefiles.account_dlc(self.steam, 44))

    def test_nothing_owned_says_nothing(self):
        self.own([])
        self.assertEqual(self.fetch(), [])
        self.assertEqual(self.sent, [])
        self.assertEqual(self.out.stream.getvalue(), "")
        self.assertEqual(self.codes(), [])

    def test_one_after_the_other_each_form(self):
        self.own([294990, 899893, 260460])
        self.answers = {(221680, 899893): "Invalid default manifest (Missing configuration)",
                        260460: "Invalid default manifest (Missing configuration)"}
        self.assertEqual(self.fetch(), [])
        self.assertEqual(self.sent, [(221680, 260460), (260460, 260460), (221680, 294990),
                                     (221680, 899893), (899893, 899893)])
        self.assertEqual(sorted(os.path.basename(os.path.dirname(d)) for d in gamefiles.dlc_depot_folders(self.steam)),
                         ["depot_294990", "depot_899893"])

    def test_what_is_done_is_not_fetched_again(self):
        self.own([1, 2])
        self.fetch()
        self.own([1, 2, 3])
        self.sent = []
        self.fetch()
        self.assertEqual(self.sent, [(221680, 3)])

    def test_a_failure_is_tried_again_and_then_named_and_the_rest_goes_on(self):
        self.own([1, 2, 3])
        self.answers = {2: "Failed downloading 1 manifests (No connection)"}
        failed = self.fetch()
        self.assertEqual([d for d, _ in failed], [2])
        self.assertEqual([s for s in self.sent if s == (221680, 2)], [(221680, 2)] * 2)
        self.assertIn((221680, 3), self.sent)
        self.assertIn("note: 1 of your 3 DLC", self.out.stream.getvalue())
        self.answers = {}
        self.sent = []
        self.fetch()
        self.assertEqual(self.sent, [(221680, 2)])

    def test_a_dlc_this_account_lacks_is_skipped_quietly(self):
        self.own([1])
        self.answers = {1: "missing license for depot (No subscription)"}
        self.assertEqual(self.fetch(), [])
        self.assertNotIn("note:", self.out.stream.getvalue())

    def test_steam_not_ready_is_only_a_note(self):
        self.st = gamefiles.SteamState(True, True, False)
        self.assertEqual(self.fetch(wait=30), [])
        self.assertIn("note: Steam isn't signed in", self.out.stream.getvalue())

    def test_no_ticket_on_disk_is_only_a_note(self):
        self.assertEqual(self.fetch(), [])
        self.assertIn("note: Steam hasn't saved", self.out.stream.getvalue())
        self.assertEqual(self.sent, [])

    def test_only_pc_archives_reach_the_copy_and_ownership_counts_them(self):
        self.own([294990])
        self.fetch()
        depot = os.path.join(self.steam, "steamapps", "content", "app_221680", "depot_294990", "dlc")
        write(os.path.join(depot, "s_m.psarc"), b"mac")
        src = os.path.join(self.tmp, "game")
        for n in ("static.psarc", "songs.psarc", "Rocksmith2014.exe"):
            write(os.path.join(src, n), n.encode())
        dest = os.path.join(self.tmp, "copy")
        with mock.patch.object(gamefiles, "steam_account", lambda *a: (self.ACCOUNT, gamefiles.HOW_LICENSE)):
            Copier("learn-play", src, dest, gamefiles.lp_dlc_folders(self.steam), self.steam, quiet(),
                   is_running=lambda e: False).run()
        self.assertEqual(os.listdir(os.path.join(dest, "dlc")), ["s294990_p.psarc"])
        with open(os.path.join(dest, ".rearmed", "steam.env")) as f:
            self.assertIn("294990", f.read())


class CommandLine(Base):
    def test_cli(self):
        self.mac_game()
        py = [sys.executable, "-I", os.path.join(HERE, "gamefiles.py")]
        env = dict(os.environ, RS_STEAM_DIR=self.steam, RS_GAME_PROCESS="no-such-process")  # whatever runs here
        r = subprocess.run(py + ["check", self.dest], capture_output=True, text=True, env=env)
        self.assertEqual(r.returncode, 1)
        r = subprocess.run(py + ["--progress-lines", "sync", self.src, self.dest, "--edition", "mac"],
                           capture_output=True, text=True, env=env)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn("progress 1.0000", r.stdout)
        self.assertIn("Game files: 10 files", r.stdout)
        r = subprocess.run(py + ["check", self.dest], capture_output=True, text=True, env=env)
        self.assertEqual(r.returncode, 0)
        r = subprocess.run(py + ["refresh", self.dest, "--progress-lines"], capture_output=True, text=True, env=env)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn("10 unchanged", r.stdout)
        r = subprocess.run(py + ["sync", os.path.join(self.tmp, "nope"), self.dest, "--edition", "mac"],
                           capture_output=True, text=True, env=env)
        self.assertEqual(r.returncode, 1)
        self.assertIn("error:", r.stdout)


if __name__ == "__main__":
    unittest.main()
