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
import re
import shutil
import struct
import sys
import tempfile
import unittest
from unittest import mock
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import cache_overlay as co  # noqa: E402
import gamefiles  # noqa: E402

# The archives here are made up, and so are their key and IV: the game's own are read from a game (psarc_key.py).
TEST_KEY = ("00112233445566778899AABBCCDDEEFF" * 2, "0F1E2D3C4B5A69788796A5B4C3D2E1F0")
co.psarc_key.use(*TEST_KEY)

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
\t\t\t\t\t"Label" : "$[22953]UPLAY",
\t\t\t\t\t"OnSelect" : { "id" : "Uplay" }
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


# The shape of the options menu's definition: Adv Display Settings' buttons among the other menus'.
OPTIONS = """{
	"DisplaySettings" :
	{
		"View" : { "Definition" : { "Buttons": { "subtitles": { "ID": "subtitles" } } } }
	},
	"AdvancedDisplaySettings" :
	{
		"View" :
		{
			"Definition" :
			{
				"Title": "$[36632]VISUAL QUALITY SETTINGS",
				"Buttons":
				{
					"visualQuality":
					{
						"ID": "visualQuality",
						"SortOrder" : 0,
						"Component": "RSButton"
					}
				}
			}
		}
	}
}
""".replace("\n", "\r\n")


def _num(v):
    return co._wnum(v)


def make_7z(files, coder="lzma1"):
    """A 7z archive like the game's own (one file per folder, plain header) of FILES [(name, bytes)]. coder:
    lzma1, lzma2, or deflate (stored as it is: a coder cache_overlay doesn't read)."""
    packs, folders = [], []
    for _, data in files:
        if coder == "lzma1":
            p, props = co._lzma1(data)
            folders.append((b"\x03\x01\x01", props, len(data)))
        elif coder == "deflate":
            p = data
            folders.append((b"\x04\x01\x08", b"", len(data)))
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


# A made-up song list movie with the shape of the game's: the list's script gives the scroll bar's thumb a clickable
# area, its bounds read from the thumb and its depth the thumb's own, and has an UpdateScrollBar that draws the bar
# (here: sets drawn) for a list that asks for it.
SONGS_POOL = ["ListScene", "ListSceneThumb", "xMin", "yMin", "xMax", "yMax", "mScrollBarThumb", "mScrollBarTrack",
              "depth", "getDepth", "_x", "_y", "_width", "_height", "id", "UpdateScrollBar"] + list(co.BAR_NAMES)
GET, SET, ADD = bytes([co.A_GET_MEMBER]), bytes([co.A_SET_MEMBER]), b"\x47"
# What the interpreter below runs and the overlay's own actions don't have
A_GET_VARIABLE, A_SUBTRACT, A_MULTIPLY, A_DIVIDE, A_OR = 0x1C, 0x0B, 0x0C, 0x0D, 0x11
METHOD_FLAGS = 0x29  # preload this, suppress arguments and super: as the game's methods


def push(*values):
    """A push of registers (ints), pool strings (str) and strings (bytes)."""
    body = b""
    for v in values:
        if isinstance(v, int):
            body += bytes([co.P_REGISTER, v])
        elif isinstance(v, str):
            body += bytes([co.P_POOL, SONGS_POOL.index(v)])
        else:
            body += b"\x00" + v + b"\x00"
    return bytes([co.A_PUSH]) + struct.pack("<H", len(body)) + body


# A made-up Adv Display Settings movie with the shape of the game's: a row's label colored white, or blue with the
# focus, in a function with branches around it, after a branch over the function.
DS_POOL = ["Draw", "fontColor", "States", "Focus", "rs", "common", "Prefs", "WHITE_CLR", "BLUE_CLR_HILITE", "fontSize"]


def ds_push(*values):
    """A push of registers (ints), pool strings (str) and booleans."""
    body = b""
    for v in values:
        if isinstance(v, bool):
            body += bytes([co.P_BOOL, int(v)])
        elif isinstance(v, int):
            body += bytes([co.P_REGISTER, v])
        else:
            body += bytes([co.P_POOL, DS_POOL.index(v)])
    return bytes([co.A_PUSH]) + struct.pack("<H", len(body)) + body


def ds_color(name):
    prefs = ds_push("rs") + bytes([co.A_GET_VARIABLE]) + ds_push("common") + GET + ds_push("Prefs") + GET
    return prefs + ds_push(name) + GET


def make_display_settings(into=False):
    """The movie (into: a branch into the color's choice), and where the function's body starts in its script."""
    white, blue = ds_color("WHITE_CLR"), ds_color("BLUE_CLR_HILITE")
    choice = co._as_branch(co.A_IF, len(white) + 5) + white + co._as_branch(co.A_JUMP, len(blue)) + blue
    focus = ds_push("Focus") + GET + ds_push(True) + bytes([co.A_EQUALS2])
    row = ds_push("fontColor", 6, "States") + GET + focus + choice
    row += ds_push("fontSize", 1) + bytes([co.A_POP, co.A_POP, co.A_POP])
    after = len(row) + 5  # the forward branch's offset: past the row and the backward branch, to the body's end
    top = ds_push(1) + co._as_branch(co.A_IF, after - (len(blue) + 5 + len(ds_push("fontSize", 1)) + 3 if into else 0))
    body = top + row
    body += co._as_branch(co.A_JUMP, -(len(body) + 5))  # back to the body's start
    func = (bytes([co.A_DEFINE_FUNCTION2]) + struct.pack("<H", 12) + b"Draw\x00"
            + struct.pack("<HBHH", 0, 8, 0, len(body)))  # (no parameters, 8 registers)
    names = b"".join(n.encode() + b"\x00" for n in DS_POOL)
    head = bytes([co.A_CONSTANT_POOL]) + struct.pack("<HH", len(names) + 2, len(DS_POOL)) + names
    script = head + ds_push(1) + co._as_branch(co.A_IF, len(func) + len(body)) + func + body + b"\x00"
    tags = b""
    for code, data in ((co.DO_INIT_ACTION, b"\x01\x00\x00"), (co.DO_INIT_ACTION, b"\x02\x00" + script), (0, b"")):
        tags += struct.pack("<HI", (code << 6) | 0x3F, len(data)) + data if len(data) >= 0x3F else \
            struct.pack("<H", (code << 6) | len(data)) + data
    frame = b"\x00" + struct.pack("<HH", 24 << 8, 1)
    return b"GFX\x08" + struct.pack("<I", 8 + len(frame) + len(tags)) + frame + tags


class AS2:
    """Enough of ActionScript 2 to run the overlay's own actions: objects are dicts (a missing member undefined, None),
    a native method a Python function of (this, *args), a script's method its body run with this in r1."""

    def __init__(self, pool, variables):
        self.pool, self.variables = pool, variables

    def call(self, fn, this, args):
        if callable(fn):
            return fn(this, *args)
        code, start, end, registers = fn
        return self.run(code, start, [None, this] + [None] * (registers - 2), end)

    def member(self, obj, name):
        if isinstance(obj, list):
            return len(obj) if name == "length" else obj[int(name)]
        while obj is not None:
            if name in obj:
                return obj[name]
            obj = obj.get("__proto__")
        return None

    def run(self, code, start=0, regs=None, end=None):
        stack, regs, at, end = [], regs or [None] * 4, start, len(code) if end is None else end
        while at < end:
            op = code[at]
            size = struct.unpack_from("<H", code, at + 1)[0] if op >= 0x80 else 0
            body = at + (3 if op >= 0x80 else 1)
            at = body + size
            if op == co.A_PUSH:
                i = body
                while i < body + size:
                    kind = code[i]
                    if kind == co.P_STRING:
                        j = code.index(b"\x00", i + 1)
                        stack.append(code[i + 1:j].decode())
                        i = j + 1
                        continue
                    value = {co.P_POOL: lambda: self.pool[code[i + 1]], co.P_REGISTER: lambda: regs[code[i + 1]],
                             co.P_BOOL: lambda: bool(code[i + 1]), co.P_UNDEFINED: lambda: None,
                             co.P_INT: lambda: struct.unpack_from("<i", code, i + 1)[0]}[kind]()
                    stack.append(value)
                    i += 1 + co.P_SIZES[kind]
            elif op == co.A_DEFINE_FUNCTION2:
                registers = code[code.index(b"\x00", body) + 3]  # (after the name and the parameters' count)
                length = struct.unpack_from("<H", code, at - 2)[0]
                stack.append((code, at, at + length, registers))
                at += length
            elif op in (co.A_IF, co.A_JUMP):
                if op == co.A_JUMP or stack.pop():
                    at += struct.unpack_from("<h", code, body)[0]
            elif op == co.A_STORE_REGISTER:
                regs[code[body]] = stack[-1]
            elif op == 0:
                break
            elif op == A_GET_VARIABLE:
                stack.append(self.variables[stack.pop()])
            elif op == co.A_GET_MEMBER:
                name = stack.pop()
                stack.append(self.member(stack.pop(), name))
            elif op == co.A_SET_MEMBER:
                value, name = stack.pop(), stack.pop()
                stack.pop()[name] = value
            elif op == co.A_CALL_METHOD:
                name, obj, n = stack.pop(), stack.pop(), stack.pop()
                args = [stack.pop() for _ in range(n)]
                stack.append(self.call(self.member(obj, name), obj, args))
            elif op == co.A_POP:
                stack.pop()
            elif op == co.A_NOT:
                stack.append(not stack.pop())
            else:
                b, a = stack.pop(), stack.pop()
                stack.append({co.A_ADD2: lambda: a + b, A_SUBTRACT: lambda: a - b, A_MULTIPLY: lambda: a * b,
                              A_DIVIDE: lambda: a / b, co.A_LESS2: lambda: a < b, co.A_EQUALS2: lambda: a == b,
                              A_OR: lambda: bool(a or b)}[op]())
        return stack[-1] if stack else None


def native(name):
    """A whole number menu_options.h names (an enum's NAME = N), which drags the bar beside SettingsScene's panel."""
    with open(os.path.join(os.path.dirname(os.path.abspath(co.__file__)), "..", "ws7", "recomp", "overrides", "mods",
                           "menu_options.h")) as f:
        return int(re.search(r"\b%s = (\d+)" % name, f.read()).group(1))


def as_constants(script):
    """{NAME: n} of a script's pushes of a register, a pool name and a whole number, as a class sets its constants
    (r.NAME = n), the first push of each NAME."""
    pool, out = co._as_pool(script) or [], {}
    for _, op, body, stop in co._as_actions(script):
        if op != co.A_PUSH or script[body] != co.P_REGISTER or script[body + 2] != co.P_POOL:
            continue
        name, kind = pool[script[body + 3]], script[body + 4] if body + 4 < stop else None
        if kind == co.P_INT:
            out.setdefault(name, struct.unpack_from("<i", script, body + 5)[0])
        elif kind == co.P_DOUBLE:  # (the high half first)
            out.setdefault(name, struct.unpack("<d", script[body + 9:body + 13] + script[body + 5:body + 9])[0])
    return out


def settings_scene(rows, focus, entry_y=100, height=50, description=150):
    """A SettingsScene as the game's lays out ROWS (the one at FOCUS with its description), and what it was asked."""
    calls, drawn = [], []

    def populate(this):  # the game's PopulateButtons: rows from mEntryInitialPosY, the description's height after one
        calls.append(this["mEntryInitialPosY"])
        this["mEntryHeight"] = height
        this["mEntryCurrentPosY"] = this["mEntryInitialPosY"] + (description if focus is not None else 0)
    proto = {"PopulateButtons": populate}
    cls = {"prototype": proto, "BACKGROUND_X": 70, "BACKGROUND_Y": 40, "BACKGROUND_WIDTH": 600,
           "BACKGROUND_HEIGHT": 640}
    buttons = {"b%d" % i: {"States": {"Focus": i == focus}} for i in range(rows)}
    clips = {k: {} for k in buttons}

    def attach(this, linkage, name, depth):  # (what was attached, in DRAWN)
        drawn.append((linkage, name, depth))
        return {"linkage": linkage}
    scene = {"__proto__": proto, "mEntryInitialPosY": entry_y, "mSortedData": [{"objId": k} for k in buttons],
             "mDataObj": {"Buttons": buttons}, "mButtons": clips, "getNextHighestDepth": lambda this: 7 + len(drawn),
             "attachMovie": attach}
    variables = {"rs": {"views": {"optionsmenu": {"controls": {"SettingsScene": cls}}}}}
    return scene, proto, variables, calls, drawn


def method(body, registers, params=()):
    """A method of a class's script with BODY, and PARAMS: (its register, its name) each."""
    head = (b"\x00" + struct.pack("<HBH", len(params), registers, METHOD_FLAGS)
            + b"".join(bytes([r]) + n + b"\x00" for r, n in params) + struct.pack("<H", len(body)))
    return bytes([co.A_DEFINE_FUNCTION2]) + struct.pack("<H", len(head)) + head + body


def make_songs_list(edited=False, pool=SONGS_POOL, bar=True, registers=10, more=b"", params=(), barred=None):
    """The movie (edited: as the overlay has it), with the script of a sprite before the list's; bar: the list's script
    has UpdateScrollBar, with REGISTERS and PARAMS, and MORE actions after it draws the bar. barred: whether the edited
    one has the bar's actions too (None: when nothing takes r9 from them)."""
    names = b"".join(n.encode() + b"\x00" for n in pool)
    part = "mScrollBarTrack" if edited else "mScrollBarThumb"
    thumb = push(1, part) + GET
    script = (bytes([co.A_CONSTANT_POOL]) + struct.pack("<HH", len(names) + 2, len(pool)) + names
              + push(b"a string", 4, "xMin", 1, part) + GET + push("_x") + GET + SET
              + push(4, "yMin") + thumb + push("_y") + GET + SET
              + push(4, "xMax") + thumb + push("_x") + GET + thumb + push("_width") + GET + ADD + SET
              + push(4, "yMax") + thumb + push("_y") + GET + thumb + push("_height") + GET + ADD + SET
              + push(4, "id", "ListSceneThumb") + SET
              + co._thumb_depth(SONGS_POOL, edited))
    if bar:
        draw = co._as_asm(pool, [("push", ("r", 1), "mShowScrollBar"), co.A_GET_MEMBER, co.A_NOT, ("if", "end"),
                                 ("push", ("r", 1), "drawn", True), co.A_SET_MEMBER, ("label", "end")])
        if barred is None:
            barred = registers > co.BAR_REGISTER and not more and not params
        shown = co._bar_shown(pool) if edited and barred else b""
        script += push(2, "UpdateScrollBar") + method(shown + draw + more, registers, params) + SET
    tags = b""
    for code, body in ((co.DO_INIT_ACTION, b"\x01\x00\x00"), (co.DO_INIT_ACTION, b"\x02\x00" + script), (1, b""),
                       (0, b"")):
        tags += struct.pack("<HI", (code << 6) | 0x3F, len(body)) + body if len(body) >= 0x3F else \
            struct.pack("<H", (code << 6) | len(body)) + body
    head = b"\x00" + struct.pack("<HH", 24 << 8, 1)  # an empty frame size, the rate and the frame count
    return b"GFX\x08" + struct.pack("<I", 8 + len(head) + len(tags)) + head + tags


def make_cache(path, startup=STARTUP, mission=MISSION, coder="lzma1", main=MAIN, notices=NOTICES, songs_list=None,
               songs_7z=None, options=OPTIONS):
    a = make_7z([("jsonloadlist.txt", b"list"), ("manifests/other.json", b"{}")], coder)
    n = make_7z([("localization/maingame.csv", notices.encode()), ("sparkactionmap.actionmap", b"m")], coder)
    b = make_7z([("manifests/ui_menu_pillar_main.database.json", main.encode("latin-1")),  # not UTF-8, as the game's
                 ("manifests/ui_menu_pillar_mission.database.json", mission.encode()),
                 ("manifests/ui_menu_pillar_startup.database.json", startup.encode()),
                 ("manifests/ui_menu_pillar_options.database.json", options.encode()),
                 ("manifests/zz.json", b"z" * 70000)], coder)
    archives = [("cache0.7z", a), ("cache4.7z", n), ("cache7.7z", b)]
    if songs_list is not None:
        songs_7z = make_7z([("gfxassets/views/las_songslist.gfx", songs_list), ("gfxassets/views/other.gfx", b"GFX")],
                           coder)
    if songs_7z is not None:  # (the song list's archive as it is)
        archives.append(("cache8.7z", songs_7z))
    with open(path, "wb") as f:
        f.write(co.write_psarc(archives))


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
        self.assertEqual(len(co.edit_enumerate(MAIN)), len(MAIN) + len("ENUMERATE") - len("$[22952]SHOP"))  # the label and orders only
        self.assertIsNone(co.edit_main(out))

    def test_exit_game(self):
        out = co.edit_main(MAIN)
        self.assertRegex(out, r'"ID": "uplay",[^}]*"Label" : "\$\[19651\]EXIT GAME",[^}]*"id" : "ExitGame"')
        self.assertNotIn("UPLAY", out)
        self.assertNotIn('"Uplay"', out)
        self.assertEqual(len(co.edit_exit(MAIN)),
                         len(MAIN) + len(co.EXIT_LABEL) - len("$[22953]UPLAY") + len("ExitGame") - len("Uplay"))

    def test_exit_game_modded(self):
        modded = MAIN.replace('"$[22953]UPLAY"', '"$[19651]EXIT GAME"').replace('"id" : "Uplay"', '"id" : "ExitGame"')
        self.assertIsNone(co.edit_exit(modded))  # the RSMods edit counts as there
        self.assertIsNotNone(co.edit_enumerate(modded))  # and the rest of the file is still done
        self.assertIsNotNone(co.edit_main(modded))

    def test_main_modded(self):
        modded = MAIN.replace('"$[22952]SHOP"', '"ENUMERATE"')
        self.assertIsNone(co.edit_enumerate(modded))  # the RSMods edit counts as there

    def test_multiplayer_notice(self):
        table = NOTICES + "37359,Player 2 must use the cable.,Le joueur 2,,Der Spieler,,,,\r\n"
        rows = co.edit_notice(table).split("\r\n")
        self.assertEqual(rows[-2].split(","), ["37359", co.MULTIPLAYER, co.MULTIPLAYER, "", co.MULTIPLAYER, "", "", "", ""])
        self.assertNotIn(",", co.MULTIPLAYER)
        self.assertIsNone(co.edit_notice(co.edit_notice(table)))

    def test_options(self):
        out = co.edit_options(OPTIONS)
        i = out.index('"AdvancedDisplaySettings"')
        for bid, lrow, label, drow, desc, order in co.MENU_OPTIONS:  # in its Buttons, before the game's own, its fields
            j = out.index('"%s":' % bid)
            self.assertTrue(i < j < out.index('"visualQuality":', i))
            self.assertIn('"ID": "%s"' % bid, out)
            self.assertIn('"Label": "$[%s]%s"' % (lrow, label), out)
            self.assertIn('"Description" : "$[%s]%s"' % (drow, desc.replace("{C}", ",")), out)
            self.assertIn('"SortOrder" : %d' % order, out)
        self.assertNotIn("\r\r", out)
        self.assertEqual(out.count("\n"), out.count("\r\n"))  # the file's own line ends
        self.assertEqual(out.index('"subtitles"'), OPTIONS.index('"subtitles"'))  # another menu's left as it is
        self.assertIsNone(co.edit_options(out))
        self.assertIsNone(co.edit_options('{ "PlaySettings" : {} }'))

    def test_bar_matches_the_native_drag(self):
        """menu_options.h drags the bar the overlay puts beside SettingsScene's panel: the same numbers."""
        self.assertEqual((native("BAR_GAP"), native("THUMB_LEAST")), (co.SETTINGS_BAR_GAP, co.THUMB_LEAST))
        # SettingsScene's own BACKGROUND_X, _Y, _WIDTH and 720 less twice _Y, as the test's scene has them
        scene, proto, variables, _, _ = settings_scene(1, 0)
        cls = variables["rs"]["views"]["optionsmenu"]["controls"]["SettingsScene"]
        self.assertEqual((native("PANEL_X"), native("PANEL_Y"), native("PANEL_W"), native("PANEL_H")),
                         (cls["BACKGROUND_X"], cls["BACKGROUND_Y"], cls["BACKGROUND_WIDTH"], cls["BACKGROUND_HEIGHT"]))

    def test_options_match_the_native_table(self):
        """The buttons are the ones menu_options_common.c gives a control to, in its order."""
        with open(os.path.join(os.path.dirname(os.path.abspath(co.__file__)), "..", "ws7", "recomp", "overrides",
                               "mods", "menu_options_common.c")) as f:
            src = f.read()
        table = src[src.index("rs_menu_option rs_menu_opts[] = {"):]
        ids = re.findall(r'^\s*\{"(rs\w+)",', table[:table.index("};")], re.M)
        self.assertEqual(ids, [o[0] for o in co.MENU_OPTIONS])
        mac_only = re.findall(r'^\s*\{"(rs\w+)",.*, 1\},$', table[:table.index("};")], re.M)
        self.assertEqual(tuple(mac_only), co.MAC_ONLY_OPTIONS)  # Learn & Play's list leaves the same out
        self.assertEqual(len({o[1] for o in co.MENU_OPTIONS} | {o[3] for o in co.MENU_OPTIONS}),
                         2 * len(co.MENU_OPTIONS))  # every row its own

    def test_text_rows(self):
        out = co.edit_maingame(NOTICES)
        rows = out.split("\r\n")
        for _, lrow, label, drow, desc, _ in co.MENU_OPTIONS:
            for row, text in ((lrow, label), (drow, desc)):
                r = [x for x in rows if x.startswith(row + ",")]
                self.assertEqual(len(r), 1)
                self.assertEqual(r[0].split(","), [row] + [text] * 7 + [""])
            self.assertGreater(int(lrow), 37382)  # after the game's own rows
        self.assertTrue(out.endswith("\r\n"))
        self.assertIsNone(co.edit_maingame(out))
        self.assertEqual(co.edit_text_rows(co.edit_text_rows(NOTICES)), None)

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
        self.assertIsNone(co.edit_exit('{"x": 1}'))
        self.assertIsNone(co.edit_notice('1,x,y\r\n'))


class SongsList(unittest.TestCase):
    def test_edit(self):
        out = co.edit_songs_list(make_songs_list())
        self.assertEqual(out, make_songs_list(edited=True))  # the thumb's area the track's, on top; a bar for each list
        self.assertIsNone(co.edit_songs_list(out))
        self.assertIsNone(co.edit_file(co.SONGS_LIST, out))

    def test_edit_without_the_bar(self):  # no UpdateScrollBar, or no register it may take: the thumb's edit alone
        r9 = co.BAR_REGISTER
        store = bytes([co.A_STORE_REGISTER]) + struct.pack("<HB", 1, r9) + bytes([co.A_POP])
        for kw in ({"bar": False}, {"registers": r9}, {"more": push(r9) + bytes([co.A_POP])},
                   {"more": push(b"x") + store}, {"params": [(r9, b"a")]}):
            self.assertEqual(co.edit_songs_list(make_songs_list(**kw)), make_songs_list(edited=True, **kw), kw)
        # registers other than r9 used: the bar too
        kw = {"more": push(3, 8) + bytes([co.A_POP, co.A_POP]), "params": [(2, b"a")]}
        self.assertEqual(co.edit_songs_list(make_songs_list(**kw)), make_songs_list(edited=True, barred=True, **kw))

    def test_bar_edit_failing(self):  # however the bar's edit fails, the thumb's stays
        for error in (co.Unsupported, struct.error, ValueError, IndexError):
            with mock.patch.object(co, "_edit_bar_shown", side_effect=error("x")):
                self.assertEqual(co.edit_songs_list(make_songs_list()), make_songs_list(edited=True, barred=False))

    def bar(self, **this):
        """THIS (a list's own members) after the edited UpdateScrollBar ran on it."""
        movie = co.edit_songs_list(make_songs_list())
        script = [movie[body + 2:end] for code, body, end in co._swf_tags(movie) if code == co.DO_INIT_ACTION][1]
        stop = [stop for _, op, _, stop in co._as_actions(script) if op == co.A_DEFINE_FUNCTION2][0]
        fn = (script, stop, stop + struct.unpack_from("<H", script, stop - 2)[0], 10)
        list_ = dict({"mShowScrollBar": False, "mScrollBarXPos": co.BAR_RIGHTMOST, "mDataObj": {"ListEntriesCount": 9},
                      "mSortedData": [{}] * 9}, **this)
        AS2(SONGS_POOL, {}).call(fn, list_, [])
        return list_

    def test_bar_for_each_list(self):
        # one that doesn't ask: its bar right of its panel, as the game's lists have theirs (a Tone Designer list)
        tone = self.bar(mTopListPanelXPos=400, mTopListPanelWidth=770)
        self.assertEqual((tone["mShowScrollBar"], tone["mScrollBarXPos"], tone["drawn"]), (True, 1172, True))
        edge = self.bar(mTopListPanelXPos=498, mTopListPanelWidth=770)  # right at the screen's edge: still one
        self.assertEqual((edge["mShowScrollBar"], edge["mScrollBarXPos"]), (True, co.BAR_RIGHTMOST))
        # past it (Load Tone's list, which keeps the class's width): none, as before
        load = self.bar(mTopListPanelXPos=381, mTopListPanelWidth=1075)
        self.assertEqual((load["mShowScrollBar"], load["mScrollBarXPos"], load.get("drawn")),
                         (False, co.BAR_RIGHTMOST, None))
        # one that asks: as it put it
        asked = self.bar(mShowScrollBar=True, mScrollBarXPos=980, mTopListPanelXPos=400, mTopListPanelWidth=770)
        self.assertEqual((asked["mScrollBarXPos"], asked["drawn"]), (980, True))

    def test_bar_counts_the_entries(self):
        self.assertEqual(self.bar(mTopListPanelXPos=208, mTopListPanelWidth=770)["mDataObj"]["ListEntriesCount"], 9)
        none = self.bar(mTopListPanelXPos=208, mTopListPanelWidth=770, mDataObj={}, mSortedData=[{}] * 3)
        self.assertEqual(none["mDataObj"]["ListEntriesCount"], 3)  # the game gave no count: the list's own

    def test_not_the_expected_file(self):
        movie = make_songs_list()
        self.assertIsNone(co.edit_songs_list(b"CFX" + movie[3:]))  # a compressed movie
        self.assertIsNone(co.edit_songs_list(movie[:40]))
        self.assertIsNone(co.edit_songs_list(b""))
        other = [n.replace("depth", "deep") for n in SONGS_POOL]  # a script with no depth for the thumb
        self.assertIsNone(co.edit_songs_list(make_songs_list(pool=other)))


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
                                 "manifests/ui_menu_pillar_options.database.json",
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
                   co.edit_main(MAIN), co.edit_maingame(NOTICES), options=co.edit_options(OPTIONS))
        self.assertEqual(co.make(game), [])
        self.assertFalse(os.path.exists(os.path.join(game, "Update")))

    def test_partly_modded_cache(self):
        game = os.path.join(self.tmp, "game")
        os.makedirs(game)
        make_cache(os.path.join(game, "cache.psarc"), mission=co.edit_mission(MISSION), main=co.edit_main(MAIN),
                   notices=co.edit_maingame(NOTICES), options=co.edit_options(OPTIONS))
        self.assertEqual(co.make(game), ["manifests/ui_menu_pillar_startup.database.json"])

    def test_songs_list(self):
        game = os.path.join(self.tmp, "game")
        os.makedirs(game)
        make_cache(os.path.join(game, "cache.psarc"), songs_list=make_songs_list())
        self.assertIn(co.SONGS_LIST, co.make(game))
        ps = co.Psarc(os.path.join(game, "Update", "patcharchives.psarc"))
        self.assertEqual(ps.names, ["cache4.7z", "cache7.7z", "cache8.7z"])
        z = co.SevenZip(ps.read("cache8.7z"))
        self.assertEqual(z.read(co.SONGS_LIST), make_songs_list(edited=True))
        self.assertEqual(z.read("gfxassets/views/other.gfx"), b"GFX")
        ps.close()

    def test_songs_list_left_out(self):
        """An archive with the song list that can't be read or rewritten costs the scroll bar's edit alone."""
        game = os.path.join(self.tmp, "game")
        os.makedirs(game)
        cache = os.path.join(game, "cache.psarc")
        menus = sorted(co.EDITS)

        def overlay():
            ps = co.Psarc(os.path.join(game, "Update", "patcharchives.psarc"))
            names = ps.names
            ps.close()
            return names

        make_cache(cache, songs_7z=b"not a 7z")  # an archive that can't be read, after the menu's
        self.assertEqual(co.make(game), menus)
        self.assertEqual(overlay(), ["cache4.7z", "cache7.7z"])
        make_cache(cache, songs_7z=make_7z([(co.SONGS_LIST, make_songs_list())], "deflate"))  # a coder it can't read
        self.assertEqual(co.make(game), menus)
        self.assertEqual(overlay(), ["cache4.7z", "cache7.7z"])

        make_cache(cache, songs_list=make_songs_list())  # one that can't be rewritten (a solid one)
        rewrite = co.rewrite_7z

        def no_songs_list(data, files):
            if co.SONGS_LIST in files:
                raise co.Unsupported("solid 7z archive")
            return rewrite(data, files)

        with mock.patch.object(co, "rewrite_7z", no_songs_list):
            self.assertEqual(co.make(game), menus)
        self.assertEqual(overlay(), ["cache4.7z", "cache7.7z"])

        def none(data, files):
            raise co.Unsupported("solid 7z archive")

        with mock.patch.object(co, "rewrite_7z", none):  # a menu archive: no overlay, as before
            with self.assertRaises(co.Unsupported):
                co.make(game)
        self.assertFalse(os.path.exists(os.path.join(game, "Update")))

    def test_display_settings(self):
        """A locked row's color: the choice made one of four, and the branches, the function and the lengths around it
        kept right; once only."""
        movie = make_display_settings()
        out = co.edit_display_settings(movie)
        self.assertIsNotNone(out)
        self.assertIsNone(co.edit_display_settings(out))
        self.assertEqual(struct.unpack_from("<I", out, 4)[0], len(out))
        (_, _, old_end), (_, body, end) = [t for t in co._swf_tags(out) if t[0] == co.DO_INIT_ACTION]
        script = out[body + 2:end]
        for text in ("Locked", "GREY_CLR", "GREY_CLR_HILITE"):
            self.assertIn(co._as_text(text), script)
        acts = co._as_actions(script)
        starts = {a for a, _, _, _ in acts} | {len(script)}
        func = [(stop, struct.unpack_from("<H", script, stop - 2)[0]) for _, op, _, stop in acts
                if op == co.A_DEFINE_FUNCTION2]
        self.assertEqual(len(func), 1)
        body_start, size = func[0]
        branches = [(stop + struct.unpack_from("<h", script, b)[0], a) for a, op, b, stop in acts
                    if op in (co.A_IF, co.A_JUMP)]
        for target, _ in branches:
            self.assertIn(target, starts)
        ends = [t for t, a in branches if t == body_start + size]
        self.assertEqual(len(ends), 2)  # the one over the function, and the body's own forward one
        self.assertIn((body_start, max(a for _, a in branches)), branches)  # the backward one, to the body's start
        self.assertEqual(script[body_start + size], 0)  # (the script's end follows the body)
        self.assertIsNone(co.edit_display_settings(make_display_settings(into=True)))  # a branch into the choice
        self.assertIsNone(co.edit_display_settings(make_songs_list()))  # not the menu's movie

    def test_list_scroll(self):
        """SettingsScene's PopulateButtons wrapped: the focused row and its description inside the panel, the rows
        outside hidden, a bar for a list longer than the panel; a list that fits as it was."""
        pool = ["rs"]

        def run(rows, focus):
            scene, proto, variables, calls, drawn = settings_scene(rows, focus)
            vm = AS2(pool, variables)
            vm.run(co._scroll_install(pool))
            vm.run(co._scroll_install(pool))  # (once: a second run leaves it)
            vm.call(vm.member(proto, "PopulateButtons"), scene, [])
            visible = [scene["mButtons"]["b%d" % i].get("_visible") for i in range(rows)]
            return scene, calls, drawn, visible

        scene, calls, drawn, visible = run(4, 1)  # 4 rows and a description fit the panel: 40 + 640 - 24 - 100 = 556
        self.assertEqual((scene["rsScroll"], calls, scene["mEntryInitialPosY"]), (0, [100], 100))
        self.assertEqual(visible, [True] * 4)
        self.assertEqual(drawn, [])  # no bar

        scene, calls, drawn, visible = run(14, 0)  # 14 rows: 850 high, at most 294 scrolled
        self.assertEqual((scene["rsScroll"], calls), (0, [100]))
        self.assertEqual(visible, [True] * 8 + [False] * 6)  # row 7 at 150 + 350, its bottom 550
        self.assertEqual(drawn, [("scrollBar_trackUp", "rsTrack", 7), ("scrollBar_thumbDown", "rsThumb", 8)])
        track, thumb = scene["rsTrack"], scene["rsThumb"]
        self.assertEqual((track["_x"], track["_y"], track["_height"], track["_visible"]), (678, 40, 640, True))
        self.assertEqual((thumb["_x"], thumb["_y"], thumb["_height"], thumb["_visible"]), (678, 40, 100, True))

        scene, calls, drawn, visible = run(14, 13)  # the last row: its description's bottom at the panel's
        self.assertEqual((scene["rsScroll"], calls, scene["mEntryInitialPosY"]), (294, [100, 100 - 294], 100))
        self.assertEqual(visible, [False] * 6 + [True] * 8)  # row 6 at 300 - 294
        self.assertEqual(scene["rsThumb"]["_y"], 40 + 640 - 100)  # the thumb at the bottom

        scene, proto, variables, calls, drawn = settings_scene(14, 9)  # the focus down, then up: the least scroll
        vm = AS2(pool, variables)
        vm.run(co._scroll_install(pool))
        vm.call(vm.member(proto, "PopulateButtons"), scene, [])
        self.assertEqual(scene["rsScroll"], 9 * 50 + 50 + 150 - 556)  # its description's bottom at the panel's: 94
        for i, button in enumerate(scene["mDataObj"]["Buttons"].values()):
            button["States"]["Focus"] = i == 1
        vm.call(vm.member(proto, "PopulateButtons"), scene, [])
        self.assertEqual(scene["rsScroll"], 50)  # its top at the list's
        self.assertAlmostEqual(scene["rsThumb"]["_y"], 40 + 540 / 13)  # at row 1 of 0-13
        scene["mSortedData"][4:] = []  # a list that fits again: the bar hidden
        vm.call(vm.member(proto, "PopulateButtons"), scene, [])
        self.assertEqual((scene["rsScroll"], scene["rsTrack"]["_visible"], scene["rsThumb"]["_visible"]),
                         (0, False, False))

    def test_display_settings_overlay(self):
        """The menu's movie of static.psarc, edited, in the overlay as it is."""
        game = os.path.join(self.tmp, "game")
        os.makedirs(game)
        make_cache(os.path.join(game, "cache.psarc"))
        with open(os.path.join(game, "static.psarc"), "wb") as f:
            f.write(co.write_psarc([(co.DISPLAY_SETTINGS, make_display_settings())]))
        self.assertIn(co.DISPLAY_SETTINGS, co.make(game))
        ps = co.Psarc(os.path.join(game, "Update", "patcharchives.psarc"))
        self.assertEqual(ps.read(co.DISPLAY_SETTINGS), co.edit_display_settings(make_display_settings()))
        ps.close()

    def test_learn_play_options(self):
        """Learn & Play's overlay: the same edits, with its own list of RS Re-ARMed's options in Adv Display Settings
        (no renderer or OpenGL engine: it has Metal only)."""
        game = os.path.join(self.tmp, "game")
        os.makedirs(game)
        make_cache(os.path.join(game, "cache.psarc"))
        with open(os.path.join(game, "static.psarc"), "wb") as f:
            f.write(co.write_psarc([(co.DISPLAY_SETTINGS, make_display_settings())]))
        mac = co.make(game)
        self.assertTrue({co.OPTIONS, co.DISPLAY_SETTINGS} <= set(mac))
        names = co.make(game, "learn-play")
        self.assertEqual(names, mac)
        ps = co.Psarc(os.path.join(game, "Update", "patcharchives.psarc"))
        self.assertEqual(ps.read(co.DISPLAY_SETTINGS), co.edit_display_settings(make_display_settings()))
        inner = {x.lower(): co.SevenZip(ps.read(n)).read(x) for n in ps.names if n.endswith(".7z")
                 for x in co.SevenZip(ps.read(n)).names()}
        ps.close()
        self.assertEqual(inner[co.NOTICES], co.edit_maingame(NOTICES, co.LP_MENU_OPTIONS).encode())
        options = inner[co.OPTIONS].decode()
        for bid in co.MAC_ONLY_OPTIONS:
            self.assertNotIn('"%s":' % bid, options)
        for o in co.LP_MENU_OPTIONS:
            self.assertIn('"%s":' % o[0], options)
        self.assertEqual(len(co.LP_MENU_OPTIONS), len(co.MENU_OPTIONS) - 2)
        # (the renderer's four take effect at once, as on the Mac; its window's two at the next start)
        self.assertEqual([o[0] for o in co.LP_MENU_OPTIONS if "at once" in o[4]],
                         ["rsSharpening", "rsAntiAliasing", "rsTextureFiltering", "rsColor"])
        self.assertEqual([o[0] for o in co.LP_MENU_OPTIONS if o[0] in co.LP_AT_START and o[4].endswith(co.AT_START)],
                         list(co.LP_AT_START))
        self.assertTrue(any(co.AT_ONCE in o[4] for o in co.MENU_OPTIONS))
        hud = next(o[4] for o in co.MENU_OPTIONS if o[0] == "rsPerformanceOverlay")  # (at once only if it started on)
        self.assertTrue(hud.endswith(co.HUD_AT_ONCE))
        self.assertRaises(ValueError, co.make, game, "windows")

    def test_static_movies(self):
        """The lists of static.psarc: each menu movie with the list's script, edited, in the overlay as it is."""
        game = os.path.join(self.tmp, "game")
        os.makedirs(game)
        make_cache(os.path.join(game, "cache.psarc"), songs_list=make_songs_list())
        movie, edited = make_songs_list(), make_songs_list(edited=True)
        original, static = os.path.join(game, "static.psarc.original"), os.path.join(game, "static.psarc")
        with open(original, "wb") as f:
            f.write(co.write_psarc([("gfxassets/views/td_songlist.gfx", movie), ("gfxassets/views/other.gfx", b"GFX"),
                                    ("gfxassets/views/las_songslist.gfx", movie),  # cache.psarc's: not twice
                                    ("gfxassets/libs/commonlib.gfx", movie), ("gfxassets/views/sub/x.gfx", movie),
                                    ("gfxassets/views/td_amplist.gfx", edited)]))  # (edited already)
        with open(static, "wb") as f:  # the title logo's copy: the original is read
            f.write(co.write_psarc([("gfxassets/views/td_racklist.gfx", movie)]))
        menus = sorted(co.EDITS)
        self.assertEqual(co.make(game), sorted(menus + [co.SONGS_LIST, "gfxassets/views/td_songlist.gfx"]))
        ps = co.Psarc(os.path.join(game, "Update", "patcharchives.psarc"))
        self.assertEqual(ps.names, ["cache4.7z", "cache7.7z", "cache8.7z", "gfxassets/views/td_songlist.gfx"])
        self.assertEqual(ps.read("gfxassets/views/td_songlist.gfx"), edited)
        ps.close()

        os.unlink(original)  # static.psarc alone
        self.assertIn("gfxassets/views/td_racklist.gfx", co.make(game))
        with open(static, "wb") as f:  # one that can't be read: the rest as before
            f.write(b"not a psarc")
        self.assertEqual(co.make(game), sorted(menus + [co.SONGS_LIST]))

        # a cache with every edit made already: the movies alone
        make_cache(os.path.join(game, "cache.psarc"), co.edit_startup(STARTUP), co.edit_mission(MISSION), "lzma2",
                   co.edit_main(MAIN), co.edit_maingame(NOTICES), options=co.edit_options(OPTIONS))
        with open(static, "wb") as f:
            f.write(co.write_psarc([("gfxassets/views/td_songlist.gfx", movie)]))
        self.assertEqual(co.make(game), ["gfxassets/views/td_songlist.gfx"])
        os.unlink(static)
        self.assertEqual(co.make(game), [])
        self.assertFalse(os.path.exists(os.path.join(game, "Update")))

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
        co.psarc_key.use(None, None)  # the game's own archive: its key, from the game next to it
        self.addCleanup(co.psarc_key.use, *TEST_KEY)
        if not co.psarc_key.have_key(*caches):
            self.skipTest("no game next to cache.psarc to read it with (set RS_PSARC_KEY_IMAGE)")
        for cache in caches:
            data, names = co.build(cache)
            if data is None:
                continue  # a cache with the entries already
            self.assertIn(co.OPTIONS, [x.lower() for x in names])  # Adv Display Settings' buttons, and their texts
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
                    self.assertIsNone(co.edit_file(x.lower(), z.read(x)))
            ov.close()
            orig.close()
            static = co.static_of(os.path.dirname(cache))
            if static:  # its lists: Tone Designer's among them, each edited once
                movies = co.plan_movies(static)
                self.assertIn("gfxassets/views/td_songlist.gfx", movies)
                for d in movies.values():
                    self.assertIsNone(co.edit_songs_list(d))
                shown = movies[co.DISPLAY_SETTINGS]  # Adv Display Settings: a locked row greyed, the list scrolled
                self.assertIsNone(co.edit_display_settings(shown))
                for text in ("GREY_CLR_HILITE", "rsPopulateButtons"):
                    self.assertIn(co._as_text(text), shown)
                # SettingsScene's panel, where menu_options.c drags its bar: its height 720 less twice its top
                panel = [k for c, b, e in co._swf_tags(shown) if c == co.DO_INIT_ACTION
                         for k in [as_constants(shown[b + 2:e])] if "BACKGROUND_X" in k]
                self.assertEqual(len(panel), 1)
                k = panel[0]
                self.assertEqual((k["BACKGROUND_X"], k["BACKGROUND_Y"], k["BACKGROUND_WIDTH"],
                                  k["BACKGROUND_HEIGHT"] - 2 * k["BACKGROUND_Y"]),
                                 tuple(native(n) for n in ("PANEL_X", "PANEL_Y", "PANEL_W", "PANEL_H")))
                nsp = movies["gfxassets/views/nsp_songlist.gfx"]  # Nonstop Play's songs, which had no bar
                pools = [(co._as_pool(nsp[b + 2:e]) or [], nsp[b + 2:e]) for c, b, e in co._swf_tags(nsp)
                         if c == co.DO_INIT_ACTION]
                self.assertTrue(any("mShowScrollBar" in p and co._bar_shown(p) in s for p, s in pools))


if __name__ == "__main__":
    unittest.main()
