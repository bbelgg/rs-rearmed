#!/usr/bin/env python3
"""Tests of cache_overlay.py (the menu overlay) with small made-up archives: the menu and notice edits, the
7z reader and rewriter, the psarc reader and writer, the overlay made from a cache and left out for a cache
that has the entries already, and gamefiles.py making it after a copy. With the game's own cache.psarc
(RS_TEST_CACHE, or the copies in the usual places) also the real files; those tests skip without it.

    python3 tools/play-native/test_cache_overlay.py
"""
import io
import lzma
import os
import shutil
import struct
import sys
import tempfile
import unittest
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import cache_overlay as co  # noqa: E402
import gamefiles  # noqa: E402

# Made-up menu files with the shape of the game's (tabs, CRLF, the game's loose JSON).
STARTUP = """{ "Static" : { "UI" : { "Menus" : { "Entries" :
{
\t"TitleScreen" :
\t{
\t\t"View" : { "Definition" : { "Buttons" : { "pressstart": { "ID" : "pressstart", "SortOrder" : 0 }, } } }
\t},
\t"InputSelect" :
\t{
\t\t"View" :
\t\t{
\t\t\t"Definition" :
\t\t\t{
\t\t\t\t"Images":
\t\t\t\t{
\t\t\t\t\t"realtone":
\t\t\t\t\t{
\t\t\t\t\t\t"ID": "realtone",
\t\t\t\t\t\t"File": "InputMode_RealToneCable.png"
\t\t\t\t\t},
\t\t\t\t\t"microphone":
\t\t\t\t\t{
\t\t\t\t\t\t"ID": "microphone",
\t\t\t\t\t\t"File": "InputMode_Microphone.png"
\t\t\t\t\t}
\t\t\t\t},
\t\t\t\t"Buttons" :
\t\t\t\t{
\t\t\t\t\t"realtone":
\t\t\t\t\t{
\t\t\t\t\t\t"ID" : "realtone",
\t\t\t\t\t\t"Label" : "$[37286]REAL TONE CABLE",
\t\t\t\t\t\t"States" : { "Default" : true },
\t\t\t\t\t\t"SortOrder" : 0
\t\t\t\t\t},

\t\t\t\t\t"microphone":
\t\t\t\t\t{
\t\t\t\t\t\t"ID" : "microphone",
\t\t\t\t\t\t"SortOrder" : 1
\t\t\t\t\t},
\t\t\t\t\t"disconnected":
\t\t\t\t\t{
\t\t\t\t\t\t"ID" : "disconnected",
\t\t\t\t\t\t"SortOrder" : 2
\t\t\t\t\t}
\t\t\t\t}
\t\t\t}
\t\t}
\t}
}
}}}}
""".replace("\n", "\r\n")

MISSION = """{
\t"inputMode":
\t{
\t\t"ID": "inputMode",
\t\t"AcceptedValues":
\t\t{
\t\t\t"0": "$[1]NORMAL",
\t\t\t"1": "$[2]MICROPHONE",
\t\t\t"2": "$[3]DISCONNECTED"
\t\t}
\t},
\t"other": { "AcceptedValues": { "0": "x" } }
}
""".replace("\n", "\r\n")


MAIN = """{ "Static" : { "UI" : { "Menus" : { "Entries" :
{
\t"MainMenu" :
\t{
\t\t"View" : { "ItemTemplate": { "SortOrder" : 1, "Label" : "#" }, "Definition" :
\t\t{
\t\t\t"Buttons" :
\t\t\t{
\t\t\t\t"learnasong":
\t\t\t\t{
\t\t\t\t\t"ID" : "learnasong",
\t\t\t\t\t"Label" : "$[22956]LEARN A SONG",
\t\t\t\t\t"SortOrder" : 0,
\t\t\t\t\t"OnSelect" : { "id" : "ChangeMenu", "StageId" :"LearnASong_Songs" }
\t\t\t\t},
\t\t\t\t"sessionmode":
\t\t\t\t{
\t\t\t\t\t"ID": "sessionmode",
\t\t\t\t\t"SortOrder" : 1,
\t\t\t\t\t"Label" : "$[22957]SESSION MODE \xe9"
\t\t\t\t},
\t\t\t\t"tonedesigner":
\t\t\t\t{
\t\t\t\t\t"ID": "tonedesigner",
\t\t\t\t\t"SortOrder" : 2,
\t\t\t\t\t"Label" : "$[22949]TONE DESIGNER"
\t\t\t\t},
\t\t\t\t"shop":
\t\t\t\t{
\t\t\t\t\t"ID": "shop",
\t\t\t\t\t"Component" : "RSButton",
\t\t\t\t\t"SortOrder" : 3,
\t\t\t\t\t"Label" : "$[22952]SHOP",
\t\t\t\t\t"OnSelect" :
\t\t\t\t\t{
\t\t\t\t\t\t"id" : "ChangeMenu",
\t\t\t\t\t\t"StageId" :"Shop"
\t\t\t\t\t}
\t\t\t\t},
\t\t\t\t"uplay":
\t\t\t\t{
\t\t\t\t\t"ID": "uplay",
\t\t\t\t\t"SortOrder" : 4,
\t\t\t\t\t"Label" : "$[22953]UPLAY"
\t\t\t\t}
\t\t\t},
\t\t\t"Help" : { "helpshop": { "ID": "helpshop", "SortOrder" : 2, "Label" : "$[-1]helpSHOP" } }
\t\t} }
\t}
}
""".replace("\n", "\r\n")

# The shape of the game's table: id, then the languages; a comma in a text is {C}.
NOTICES = ("5857,Invite a Friend,,,,,\u30d5,,\r\n"
           "36675,You must enable Steam Community In-Game to access the Shop.,Pour acc\u00e9der{C} oui,Debes,Devi,Du,"
           "\u30b7,You must enable Steam Community In-Game to access the Shop.,\r\n"
           "36676,Your audio input device is not properly configured.,Ton,Tu,La,Dein,\u30aa,,\r\n")


def _num(v):
    return co._wnum(v)


def make_7z(files, coder="lzma1"):
    """A 7z archive like the game's own (one file per folder, plain header) of FILES [(name, bytes)]."""
    packs, folders = [], []
    for _, data in files:
        if coder == "lzma1":
            p, props = co._lzma1(data)
            folders.append((b"\x03\x01\x01", props, len(data)))
        else:
            p = lzma.compress(data, lzma.FORMAT_RAW, filters=[{"id": lzma.FILTER_LZMA2, "dict_size": 1 << 20}])
            folders.append((b"\x21", b"\x10", len(data)))
        packs.append(p)
    h = bytearray(b"\x01\x04\x06" + _num(0) + _num(len(packs)) + b"\x09")
    for p in packs:
        h += _num(len(p))
    h += b"\x00\x07\x0b" + _num(len(folders)) + b"\x00"
    for cid, props, _ in folders:
        h += b"\x01" + bytes([len(cid) | 0x20]) + cid + _num(len(props)) + props
    h += b"\x0c" + b"".join(_num(n) for _, _, n in folders) + b"\x00\x00"
    names = b"\x00" + b"".join(n.encode("utf-16-le") + b"\x00\x00" for n, _ in files)
    h += b"\x05" + _num(len(files)) + b"\x11" + _num(len(names)) + names
    attrs = b"\x01\x00" + b"\x20\x00\x00\x00" * len(files)
    h += b"\x15" + _num(len(attrs)) + attrs + b"\x00\x00"
    body = b"".join(packs)
    start = struct.pack("<QQI", len(body), len(h), zlib.crc32(bytes(h)))
    return co.SIG + b"\x00\x04" + struct.pack("<I", zlib.crc32(start)) + start + body + bytes(h)


def make_cache(path, startup=STARTUP, mission=MISSION, coder="lzma1", main=MAIN, notices=NOTICES):
    a = make_7z([("jsonloadlist.txt", b"list"), ("manifests/other.json", b"{}")], coder)
    n = make_7z([("localization/maingame.csv", notices.encode()), ("sparkactionmap.actionmap", b"m")], coder)
    b = make_7z([("manifests/ui_menu_pillar_main.database.json", main.encode("latin-1")),  # not UTF-8, as the game's
                 ("manifests/ui_menu_pillar_mission.database.json", mission.encode()),
                 ("manifests/ui_menu_pillar_startup.database.json", startup.encode()),
                 ("manifests/zz.json", b"z" * 70000)], coder)
    with open(path, "wb") as f:
        f.write(co.write_psarc([("cache0.7z", a), ("cache4.7z", n), ("cache7.7z", b)]))


class Edits(unittest.TestCase):
    def test_startup(self):
        out = co.edit_startup(STARTUP)
        self.assertIn('"othercable"', out)
        self.assertIn('"Label": "DIRECT CONNECT"', out)
        self.assertIn('"File": "InputMode_DirectConnect.png"', out)
        # the new button sorts right after the real tone cable, the others move down one
        orders = {k: int(v) for k, v in __import__("re").findall(
            r'"ID" : "(\w+)",[^{}]*?"SortOrder" : (\d+)', out)}
        self.assertEqual(orders, {"pressstart": 0, "microphone": 2, "disconnected": 3})
        self.assertIn('"ID": "othercable"', out)
        self.assertRegex(out, r'"ID": "othercable",\r\n[^}]*"SortOrder": 1')
        self.assertNotIn("\n\t", out.replace("\r\n", ""))  # CRLF kept
        self.assertIsNone(co.edit_startup(out))  # already there: unchanged

    def test_mission(self):
        out = co.edit_mission(MISSION)
        self.assertIn('"2": "$[3]DISCONNECTED",\r\n\t\t\t"3": "DIRECT CONNECT"\r\n', out)
        self.assertEqual(out.count('"3"'), 1)
        self.assertIsNone(co.edit_mission(out))

    def test_mission_trailing_comma_and_modded(self):
        t = MISSION.replace('"$[3]DISCONNECTED"', '"$[3]DISCONNECTED",')
        self.assertNotIn(",,", co.edit_mission(t))
        modded = MISSION.replace('"$[3]DISCONNECTED"', '"$[3]DISCONNECTED"\r\n\t\t\t"3": "DIRECT CONNECT"')
        self.assertIsNone(co.edit_mission(modded))  # the RSMods edit (no comma) counts as there

    def test_main(self):
        out = co.edit_main(MAIN)
        orders = {k: int(v) for k, v in __import__("re").findall(
            r'"ID"\s*: "(\w+)",(?:[^{}]*?)"SortOrder" : (\d+)', out)}
        self.assertEqual(orders, {"learnasong": 0, "shop": 1, "sessionmode": 2, "tonedesigner": 3, "uplay": 4,
                                  "helpshop": 2})  # the help list is left alone
        self.assertIn('"Label" : "ENUMERATE"', out)
        self.assertNotIn('SHOP"', out.replace('helpSHOP"', ""))
        self.assertRegex(out, r'"ID": "shop",[^}]*"id" : "ChangeMenu",\r\n\t+"StageId" :"Shop"')  # OnSelect kept
        self.assertIn('"ItemTemplate": { "SortOrder" : 1,', out)  # outside the Buttons
        self.assertEqual(len(out), len(MAIN) + len("ENUMERATE") - len("$[22952]SHOP"))  # the label and orders only
        self.assertIsNone(co.edit_main(out))

    def test_main_modded(self):
        modded = MAIN.replace('"$[22952]SHOP"', '"ENUMERATE"')
        self.assertIsNone(co.edit_main(modded))  # the RSMods edit counts as there

    def test_multiplayer_notice(self):
        table = NOTICES + "37359,Player 2 must use the cable.,Le joueur 2,,Der Spieler,,,,\r\n"
        rows = co.edit_notice(table).split("\r\n")
        self.assertEqual(rows[-2].split(","), ["37359", co.MULTIPLAYER, co.MULTIPLAYER, "", co.MULTIPLAYER, "", "", "", ""])
        self.assertNotIn(",", co.MULTIPLAYER)
        self.assertIsNone(co.edit_notice(co.edit_notice(table)))

    def test_notice(self):
        out = co.edit_notice(NOTICES)
        rows = out.split("\r\n")
        self.assertEqual(rows[1].split(","), ["36675"] + [co.NOTICE] * 7 + [""])
        self.assertEqual([rows[0], rows[2]], [NOTICES.split("\r\n")[0], NOTICES.split("\r\n")[2]])
        self.assertNotIn(",", co.NOTICE)
        self.assertIsNone(co.edit_notice(out))

    def test_not_the_expected_files(self):
        self.assertIsNone(co.edit_startup('{"x": 1}'))
        self.assertIsNone(co.edit_mission('{"x": 1}'))
        self.assertIsNone(co.edit_main('{"x": 1}'))
        self.assertIsNone(co.edit_notice('1,x,y\r\n'))


class TmpDir(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="overlay-test-")
        self.addCleanup(shutil.rmtree, self.tmp, True)


def read(path):
    with open(path, "rb") as f:
        return f.read()


class Archives(TmpDir):

    def test_psarc_round_trip(self):
        files = [("a/b.txt", b"hello"), ("big.bin", os.urandom(200000)), ("empty", b"")]
        p = os.path.join(self.tmp, "x.psarc")
        with open(p, "wb") as f:
            f.write(co.write_psarc(files))
        ps = co.Psarc(p)
        self.assertEqual(ps.names, [n for n, _ in files])
        for n, d in files:
            self.assertEqual(ps.read(n), d)
        ps.close()

    def test_7z_read_and_rewrite(self):
        for coder in ("lzma1", "lzma2"):
            files = [("m/a.json", b"a" * 1000), ("m/b.json", b"bb"), ("m/c.json", b"c" * 100000)]
            z = co.SevenZip(make_7z(files, coder))
            self.assertEqual(z.names(), [n for n, _ in files])
            for n, d in files:
                self.assertEqual(z.read(n), d)
            out = co.rewrite_7z(make_7z(files, coder), {"m/b.json": b"new"})
            self.assertEqual(out[32 + struct.unpack("<Q", out[12:20])[0]], 0x17)  # packed header
            z2 = co.SevenZip(out)
            self.assertEqual(z2.order, z.order)
            self.assertEqual(z2.props[0x15], z.props[0x15])
            self.assertEqual([z2.read(n) for n, _ in files], [b"a" * 1000, b"new", b"c" * 100000])
            # the files not replaced keep their stored bytes
            self.assertEqual(z2.packed(0), z.packed(0))

    def test_not_a_7z(self):
        with self.assertRaises(co.Unsupported):
            co.SevenZip(b"PK\x03\x04")


class Overlay(TmpDir):
    def test_make_and_remove(self):
        game = os.path.join(self.tmp, "game")
        os.makedirs(game)
        make_cache(os.path.join(game, "cache.psarc"))
        before = read(os.path.join(game, "cache.psarc"))
        names = co.make(game)
        self.assertEqual(names, ["localization/maingame.csv", "manifests/ui_menu_pillar_main.database.json",
                                 "manifests/ui_menu_pillar_mission.database.json",
                                 "manifests/ui_menu_pillar_startup.database.json"])
        self.assertEqual(read(os.path.join(game, "cache.psarc")), before)  # never written
        ps = co.Psarc(os.path.join(game, "Update", "patcharchives.psarc"))
        self.assertEqual(ps.names, ["cache4.7z", "cache7.7z"])  # only the archives with the changed files
        z = co.SevenZip(ps.read("cache7.7z"))
        self.assertIn("othercable", z.read("manifests/ui_menu_pillar_startup.database.json").decode())
        self.assertIn('"3": "DIRECT CONNECT"', z.read("manifests/ui_menu_pillar_mission.database.json").decode())
        main = z.read("manifests/ui_menu_pillar_main.database.json")
        self.assertIn(b'"Label" : "ENUMERATE"', main)
        self.assertIn(b"SESSION MODE \xe9", main)  # a byte that is not UTF-8 stays as it is
        self.assertEqual(z.read("manifests/zz.json"), b"z" * 70000)
        self.assertIn(co.NOTICE, co.SevenZip(ps.read("cache4.7z")).read("localization/maingame.csv").decode())
        ps.close()

        # a cache that has the entries already (a modded one): no overlay, an earlier one removed
        make_cache(os.path.join(game, "cache.psarc"), co.edit_startup(STARTUP), co.edit_mission(MISSION), "lzma2",
                   co.edit_main(MAIN), co.edit_notice(NOTICES))
        self.assertEqual(co.make(game), [])
        self.assertFalse(os.path.exists(os.path.join(game, "Update")))

    def test_partly_modded_cache(self):
        game = os.path.join(self.tmp, "game")
        os.makedirs(game)
        make_cache(os.path.join(game, "cache.psarc"), mission=co.edit_mission(MISSION), main=co.edit_main(MAIN),
                   notices=co.edit_notice(NOTICES))
        self.assertEqual(co.make(game), ["manifests/ui_menu_pillar_startup.database.json"])

    def test_unreadable_cache(self):
        game = os.path.join(self.tmp, "game")
        os.makedirs(os.path.join(game, "Update"))
        with open(os.path.join(game, "Update", "patcharchives.psarc"), "wb") as f:
            f.write(b"old")
        with open(os.path.join(game, "cache.psarc"), "wb") as f:
            f.write(b"not a psarc")
        with self.assertRaises(co.Unsupported):
            co.make(game)
        self.assertFalse(os.path.exists(os.path.join(game, "Update")))

    def test_no_cache(self):
        game = os.path.join(self.tmp, "game")
        os.makedirs(game)
        self.assertEqual(co.make(game), [])

    def test_cli(self):
        game = os.path.join(self.tmp, "game")
        os.makedirs(game)
        make_cache(os.path.join(game, "cache.psarc"))
        out = io.StringIO()
        sys.stdout, sys.stderr, old = out, io.StringIO(), (sys.stdout, sys.stderr)
        try:
            self.assertEqual(co.main(["show", os.path.join(game, "cache.psarc")]), 0)
            self.assertEqual(co.main(["make", game]), 0)
            self.assertEqual(co.main(["bogus"]), 64)
        finally:
            sys.stdout, sys.stderr = old
        self.assertIn("cache7.7z: manifests/ui_menu_pillar_startup.database.json", out.getvalue())
        self.assertTrue(os.path.isfile(os.path.join(game, "Update", "patcharchives.psarc")))


class GameFiles(TmpDir):
    def test_copy_makes_the_overlay(self):
        src = os.path.join(self.tmp, "Rocksmith2014")
        os.makedirs(src)
        for n in ("static.psarc", "songs.psarc"):
            with open(os.path.join(src, n), "wb") as f:
                f.write(b"x")
        make_cache(os.path.join(src, "cache.psarc"))
        dest = os.path.join(self.tmp, "copy")
        progress = gamefiles.Progress(stream=io.StringIO())
        gamefiles.Copier("mac", src, dest, steam=os.path.join(self.tmp, "steam"), progress=progress,
                         is_running=lambda e: False, clone=lambda s, d: False, placement=None).run()
        self.assertTrue(os.path.isfile(os.path.join(dest, "Update", "patcharchives.psarc")))
        self.assertIn("Direct Connect", progress.stream.getvalue())
        self.assertTrue(gamefiles.check(dest)[0])
        # a refresh keeps it (it is not one of the copied files, so never removed as one)
        gamefiles.refresh(dest, progress, is_running=lambda e: False, clone=lambda s, d: False, placement=None)
        self.assertTrue(os.path.isfile(os.path.join(dest, "Update", "patcharchives.psarc")))


def real_caches():
    cands = [os.environ.get("RS_TEST_CACHE", ""),
             os.path.expanduser("~/Library/Application Support/RS Re-ARMed/game/cache.psarc"),
             os.path.expanduser("~/Library/Application Support/RS Re-ARMed/learn-play/game/cache.psarc")]
    return [c for c in cands if c and os.path.isfile(c)]


class RealCache(unittest.TestCase):
    """With the game's own cache.psarc: the overlay is made and reads back."""

    def test_real(self):
        caches = real_caches()
        if not caches:
            self.skipTest("no cache.psarc (set RS_TEST_CACHE)")
        for cache in caches:
            data, names = co.build(cache)
            if data is None:
                continue  # a cache with the entries already
            tmp = tempfile.mkdtemp(prefix="overlay-real-")
            self.addCleanup(shutil.rmtree, tmp, True)
            p = os.path.join(tmp, "o.psarc")
            with open(p, "wb") as f:
                f.write(data)
            ov, orig = co.Psarc(p), co.Psarc(cache)
            for n in ov.names:
                z, z0 = co.SevenZip(ov.read(n)), co.SevenZip(orig.read(n))
                self.assertEqual(z.order, z0.order)
                changed = sorted(x for x in z0.names() if z.read(x) != z0.read(x))
                self.assertEqual(changed, sorted(x for x in names if x in z0.files))
                for x in changed:
                    self.assertIsNone(co.EDITS[x.lower()](z.read(x).decode("utf-8", "surrogateescape")))
            ov.close()
            orig.close()


if __name__ == "__main__":
    unittest.main()
