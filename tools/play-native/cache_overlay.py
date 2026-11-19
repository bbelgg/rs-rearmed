#!/usr/bin/env python3
"""The menu overlay (Direct Connect, Enumerate, Exit Game, Multiplayer's notice, the lists' scroll bars,
RS Re-ARMed's options in Adv Display Settings): a small patch archive made from the user's own cache.psarc and
static.psarc.

    cache_overlay.py make GAME_DIR [EDITION]     write GAME_DIR/Update/patcharchives.psarc (or remove it: nothing
                                                 to change)
    cache_overlay.py show CACHE.psarc [EDITION]  print what the overlay would change, write nothing (with the
                                                 static.psarc beside it: static_of)
    EDITION                                      the game's: mac (when not given) or learn-play

The game's cable screen ("HOW WOULD YOU LIKE TO CONNECT?") offers a real tone cable, a microphone or no input.
The game code also handles a fourth button, "othercable" (Direct Connect: any audio interface), and a fourth
value of the INPUT slider, but the menu data leaves both out. They are menu entries in two JSON files inside
cache.psarc (manifests/ui_menu_pillar_startup.database.json and manifests/ui_menu_pillar_mission.database.json,
in one of its cacheN.7z archives).

The main menu's SHOP can't work offline: the game checks the Steam overlay before it opens the Shop and, with
the overlay off, shows a notice instead. So the entry (manifests/ui_menu_pillar_main.database.json) becomes
ENUMERATE, in second place, its OnSelect left as it is, and the notice (row 36675 of localization/maingame.csv,
all languages) says what now happens: the offline Steam layer takes the overlay check as the press, brings new
songs into the copy and has the game scan dlc/ again (shims/steam/src/enumerate.h). Row 37359, the notice MULTIPLAYER
INPUT SUPPORT, says what two players can use now.

The main menu's UPLAY can't work either: there is no Uplay here. So the same file's entry becomes EXIT GAME, the
game's own label (row 19651, in every language) and its own action, which the file's help list has too.

Learn a Song's scroll bar can be dragged only near the top of the list: the rows of songs below the one in focus
take the clicks on it. Its menu (gfxassets/views/las_songslist.gfx, a Scaleform movie) gives the game the bar's
clickable area; the edit makes that area the bar's whole track, above the rows, by changing a few bytes of its
ActionScript (edit_songs_list). It is the one optional edit: an archive with the movie that can't be read or
rewritten leaves the movie out, not the menu's edits with it. The game's other lists with a scroll bar, the Tone
Designer's among them, are movies of static.psarc with the same script (gfxassets/views/<name>.gfx): they get the
same edit, and the overlay holds them as they are, beside the archives (plan_movies). They are optional too. Most of
the game's lists have no bar at all, Nonstop Play's songs among them: the edit also gives each of those one, where
the lists with a bar have theirs (_edit_bar_shown).

In the Mac edition, Adv Display Settings also lists RS Re-ARMed's picture switches: the overlay adds their buttons to
the menu (manifests/ui_menu_pillar_options.database.json) and their texts to the game's (edit_maingame), and its movie
(gfxassets/views/displaysettings.gfx of static.psarc) greys a row that can't be used and scrolls its list, which no
longer fits (edit_display_settings). The rest is native (tools/ws7/recomp/overrides/mods/menu_options.c), on the Mac
game's own menu code, so Learn & Play's overlay leaves these out (LP_EDITS).

This makes those edits in the user's own copies of the files and packs each archive that holds them, rewritten
with only those files changed, into the patch container the engine mounts at start when it is there,
<game>/Update/patcharchives.psarc:

    patcharchives.psarc             mounted by the engine at PatchUpdate/
      cache4.7z, cache7.7z, ...     the cache.psarc archives with the changed files (whichever cacheN.7z has them)
      gfxassets/views/<name>.gfx    the static.psarc menu movies with the changed lists

The engine's own patch layer takes only .new/.delta archives from the container, so the game reads them from
there through a hook: FrontEnd_OpenFile opens PatchUpdate/cacheN.7z or PatchUpdate/gfxassets/views/<name>.gfx
first and the game's own when the overlay has none (rs_mods_overlay_name in
tools/ws7/recomp/overrides/mods/mods_shared.c, both editions). Without the hook the overlay does nothing.

cache.psarc itself is never written, so a modded cache works too: files that already have the entries are left
as they are, and with nothing to change there is no overlay. Both editions use it (gamefiles.py runs it after a copy
or refresh of the game files). Nothing of the game is in this file: the archives are read and written here.

Standard library only (any Python 3.9+); the archive tables are AES-encrypted, done with /usr/bin/openssl, with
the key read from the player's own game (psarc_key.py, next to this file).
"""
import hashlib
import lzma
import os
import re
import struct
import subprocess
import sys
import zlib

# Run with python3 -I, so the folder is not on the path by itself.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import psarc_key  # noqa: E402
del sys.path[0]

# --- psarc -------------------------------------------------------------------------------------------------

PSARC_VERSION = 0x00010004
BLOCK = 0x10000
FLAG_TOC_ENCRYPTED = 4
ENTRY_SIZE = 30


class Unsupported(Exception):
    """An archive this module cannot read (the overlay is then left out; the game runs without it)."""


def _toc_crypt(data, decrypt, near=()):
    """A table of contents de- or encrypted with the game's key (psarc_key.py: found near the archive NEAR, in
    the player's game). Reading one of the game's archives also tells the IV that writing needs."""
    try:
        key = psarc_key.toc_key(*near)
        if decrypt:
            psarc_key.table_iv(data[:16], key)
            return psarc_key.crypt_toc(data, True, key)
        _learn_iv(near, key)
        return psarc_key.crypt_toc(data, False, key, psarc_key.toc_iv())
    except psarc_key.NoKey as e:
        raise Unsupported(str(e))
    except (OSError, subprocess.CalledProcessError) as e:
        raise Unsupported("openssl failed: %s" % e)


def _learn_iv(near, key):
    """The IV comes from a game archive read earlier; when none was (a cache.psarc some tool wrote without
    encryption), from another archive in the folders of NEAR."""
    for start in near:
        folder = start if os.path.isdir(start) else os.path.dirname(os.path.abspath(start))
        try:
            names = sorted(n for n in os.listdir(folder) if n.lower().endswith(".psarc"))
        except OSError:
            continue
        for name in names:
            try:
                with open(os.path.join(folder, name), "rb") as f:
                    hdr = f.read(48)
            except OSError:
                continue
            if (len(hdr) == 48 and hdr[:4] == b"PSAR" and struct.unpack(">I", hdr[28:32])[0] & FLAG_TOC_ENCRYPTED
                    and psarc_key.table_iv(hdr[32:], key)):
                return


class Psarc:
    """A psarc archive: names (entry 0 is the list of names) and the data of an entry by name."""

    def __init__(self, path):
        self.f = open(path, "rb")
        try:
            self._open(path)
        except BaseException:
            self.f.close()
            raise

    def _open(self, path):
        hdr = self.f.read(32)
        if len(hdr) < 32 or hdr[:4] != b"PSAR":
            raise Unsupported("%s is not a psarc archive" % path)
        _, _, comp, toc_len, ent_size, n, self.block, flags = struct.unpack(">4sI4sIIIII", hdr)
        if comp != b"zlib" or ent_size != ENTRY_SIZE:
            raise Unsupported("%s: psarc kind %r/%d" % (path, comp, ent_size))
        toc = self.f.read(toc_len - 32)
        if flags & FLAG_TOC_ENCRYPTED:
            toc = _toc_crypt(toc, True, (path,))
        self.entries = []
        for i in range(n):
            e = toc[i * ent_size:(i + 1) * ent_size]
            self.entries.append((struct.unpack(">I", e[16:20])[0], int.from_bytes(e[20:25], "big"),
                                 int.from_bytes(e[25:30], "big")))
        zb = 2 if self.block <= 0x10000 else 3 if self.block <= 0x1000000 else 4
        zt = toc[n * ent_size:]
        self.zsizes = [int.from_bytes(zt[i:i + zb], "big") for i in range(0, len(zt) - zb + 1, zb)]
        self.names = self._read(self.entries[0]).decode("utf-8", "replace").split("\n") if n else []
        self.index = {name: i + 1 for i, name in enumerate(self.names)}

    def close(self):
        self.f.close()

    def _read(self, entry):
        zi, length, offset = entry
        self.f.seek(offset)
        out = bytearray()
        while len(out) < length:
            z = self.zsizes[zi] or self.block
            zi += 1
            chunk = self.f.read(z)
            want = min(self.block, length - len(out))
            if len(chunk) != want:
                try:
                    chunk = zlib.decompress(chunk)
                except zlib.error:
                    pass  # a stored block
            out += chunk
        return bytes(out[:length])

    def read(self, name):
        return self._read(self.entries[self.index[name]])


def write_psarc(files, near=()):
    """A psarc (bytes) with FILES, [(name, data)]: zlib blocks of 64 KiB, encrypted table, entry 0 the names.
    NEAR: the game's archives it is made for (the key and the IV come from the game: _toc_crypt)."""
    names = [n for n, _ in files]
    datas = [b"\n".join(n.encode() for n in names)] + [d for _, d in files]
    hashes = [bytes(16)] + [hashlib.md5(n.encode()).digest() for n in names]
    blocks = []
    for d in datas:
        bl = []
        for i in range(0, len(d), BLOCK) if d else [0]:
            raw = d[i:i + BLOCK]
            z = zlib.compress(raw, 9)
            if len(z) < len(raw):
                bl.append((len(z), z))
            else:  # stored; a whole raw block is written with size 0
                bl.append((0 if len(raw) == BLOCK else len(raw), raw))
        blocks.append(bl)
    n_blocks = sum(len(b) for b in blocks)
    toc_len = 32 + len(datas) * ENTRY_SIZE + n_blocks * 2
    toc, ztab, zi, pos = bytearray(), bytearray(), 0, toc_len
    for h, d, bl in zip(hashes, datas, blocks):
        toc += h + struct.pack(">I", zi) + len(d).to_bytes(5, "big") + pos.to_bytes(5, "big")
        for z, b in bl:
            ztab += z.to_bytes(2, "big")
            pos += len(b)
        zi += len(bl)
    toc = _toc_crypt(bytes(toc + ztab), False, near)
    out = bytearray(struct.pack(">4sI4sIIIII", b"PSAR", PSARC_VERSION, b"zlib", toc_len, ENTRY_SIZE, len(datas),
                                BLOCK, FLAG_TOC_ENCRYPTED))
    out += toc
    for bl in blocks:
        for _, b in bl:
            out += b
    return bytes(out)


# --- 7z (reading only: LZMA, LZMA2 and stored streams, one coder per folder) --------------------------------

SIG = b"7z\xbc\xaf\x27\x1c"


def _num(b, i):
    """7z's variable-length number at b[i]: (value, next offset)."""
    first = b[i]
    i += 1
    mask, val = 0x80, 0
    for n in range(8):
        if not first & mask:
            return val | ((first & (mask - 1)) << (8 * n)), i
        val |= b[i] << (8 * n)
        i += 1
        mask >>= 1
    return val, i


def _bits(b, i, n):
    """A 7z bit vector of N items (with its all-defined byte): (list of bools, next offset)."""
    if b[i]:
        return [True] * n, i + 1
    i += 1
    out = [bool(b[i + k // 8] & (0x80 >> (k % 8))) for k in range(n)]
    return out, i + (n + 7) // 8


def _decode(coder, props, data, size):
    if coder == b"\x00":
        return data[:size]
    if coder == b"\x03\x01\x01" and len(props) == 5:
        d = props[0]
        f = {"id": lzma.FILTER_LZMA1, "lc": d % 9, "lp": (d // 9) % 5, "pb": d // 45,
             "dict_size": struct.unpack("<I", props[1:5])[0]}
    elif coder == b"\x21" and len(props) == 1:
        p = props[0]
        dict_size = 0xFFFFFFFF if p == 40 else (2 | (p & 1)) << (p // 2 + 11)
        f = {"id": lzma.FILTER_LZMA2, "dict_size": min(dict_size, 1 << 30)}
    else:
        raise Unsupported("7z coder %s" % coder.hex())
    try:
        out = lzma.LZMADecompressor(lzma.FORMAT_RAW, filters=[f]).decompress(data, size)
    except lzma.LZMAError as e:
        raise Unsupported("7z data: %s" % e)
    if len(out) < size:
        raise Unsupported("7z data: short stream")
    return out


class SevenZip:
    """The files of a 7z archive (bytes): names, and the data of a file by name."""

    def __init__(self, data):
        if data[:6] != SIG:
            raise Unsupported("not a 7z archive")
        off, size = struct.unpack("<QQ", data[12:28])
        self.d = data
        self.files = {}  # name -> (folder, offset in the folder's output, size)
        self._out = {}
        try:
            self._header(data[32 + off:32 + off + size])
        except (IndexError, struct.error) as e:
            raise Unsupported("7z header: %s" % e)

    def _header(self, h):
        while h[0] == 0x17:  # an encoded (packed) header
            pack, folders, _, _ = self._streams(h, 1)
            h = self._unpack(pack, folders, 0)
        if h[0] != 0x01:
            raise Unsupported("7z header kind %d" % h[0])
        i = 1
        if h[i] == 0x02:  # archive properties
            raise Unsupported("7z archive properties")
        if h[i] == 0x03:  # additional streams
            raise Unsupported("7z additional streams")
        pack = folders = sub = None
        if h[i] == 0x04:
            pack, folders, sub, i = self._streams(h, i + 1)
        self.pack, self.folders = pack, folders or []
        names, empty = [], []
        self.props = {}  # the file properties as stored (times, attributes, ...), for a rewrite
        if h[i] == 0x05:
            n, i = _num(h, i + 1)
            empty = [False] * n
            while h[i] != 0x00:
                t = h[i]
                size, i = _num(h, i + 1)
                body = h[i:i + size]
                i += size
                self.props[t] = bytes(body)
                if t == 0x0E:  # empty streams: a plain bit vector
                    empty = [bool(body[k // 8] & (0x80 >> (k % 8))) for k in range(n)]
                elif t == 0x11:
                    if body[0] != 0:
                        raise Unsupported("7z external names")
                    names = body[1:].decode("utf-16-le").split("\0")[:n]
        # Non-empty files take the folders' substreams in order.
        nums = sub["num"] if sub else [1] * len(self.folders)
        sizes = sub["sizes"] if sub else [f["unpack"] for f in self.folders]
        streams = []
        k = 0
        for fi, cnt in enumerate(nums):
            o = 0
            for _ in range(cnt):
                streams.append((fi, o, sizes[k]))
                o += sizes[k]
                k += 1
        self.order = names  # every file as stored, empty ones too
        self.solid = any(c != 1 for c in nums)
        s = 0
        for name, is_empty in zip(names, empty or [False] * len(names)):
            key = name.replace("\\", "/")
            if is_empty:
                continue
            self.files[key] = streams[s]
            s += 1

    def _streams(self, h, i):
        pack = folders = sub = None
        while True:
            t = h[i]
            i += 1
            if t == 0x00:
                return pack, folders, sub, i
            if t == 0x06:  # pack info
                pos, i = _num(h, i)
                n, i = _num(h, i)
                sizes = []
                while h[i] != 0x00:
                    if h[i] == 0x09:
                        i += 1
                        for _ in range(n):
                            v, i = _num(h, i)
                            sizes.append(v)
                    elif h[i] == 0x0A:
                        defined, i = _bits(h, i + 1, n)
                        i += 4 * sum(defined)
                    else:
                        raise Unsupported("7z pack info %d" % h[i])
                i += 1
                pack = (pos, sizes)
            elif t == 0x07:  # unpack info: folders
                if h[i] != 0x0B:
                    raise Unsupported("7z folders")
                n, i = _num(h, i + 1)
                if h[i] != 0x00:
                    raise Unsupported("7z external folders")
                i += 1
                folders = []
                for _ in range(n):
                    nc, i = _num(h, i)
                    if nc != 1:
                        raise Unsupported("7z folder with %d coders" % nc)
                    flag = h[i]
                    i += 1
                    cid = h[i:i + (flag & 0x0F)]
                    i += flag & 0x0F
                    if flag & 0x10:
                        raise Unsupported("7z complex coder")
                    props = b""
                    if flag & 0x20:
                        pl, i = _num(h, i)
                        props = h[i:i + pl]
                        i += pl
                    folders.append({"coder": bytes(cid), "props": bytes(props)})
                if h[i] != 0x0C:
                    raise Unsupported("7z folder sizes")
                i += 1
                for f in folders:
                    f["unpack"], i = _num(h, i)
                if h[i] == 0x0A:
                    defined, i = _bits(h, i + 1, len(folders))
                    i += 4 * sum(defined)
                    for f, d in zip(folders, defined):
                        f["crc"] = d
                if h[i] != 0x00:
                    raise Unsupported("7z unpack info %d" % h[i])
                i += 1
            elif t == 0x08:  # substreams
                sub = {"num": [1] * len(folders), "sizes": []}
                have_sizes = False
                while True:
                    s = h[i]
                    i += 1
                    if s == 0x00:
                        break
                    if s == 0x0D:
                        sub["num"] = []
                        for _ in folders:
                            v, i = _num(h, i)
                            sub["num"].append(v)
                    elif s == 0x09:
                        have_sizes = True
                        for fi, f in enumerate(folders):
                            if not sub["num"][fi]:
                                continue
                            tot = 0
                            for _ in range(sub["num"][fi] - 1):
                                v, i = _num(h, i)
                                sub["sizes"].append(v)
                                tot += v
                            sub["sizes"].append(f["unpack"] - tot)
                    elif s == 0x0A:
                        n = sum(c for fi, c in enumerate(sub["num"]) if not (c == 1 and folders[fi].get("crc")))
                        defined, i = _bits(h, i, n)
                        i += 4 * sum(defined)
                    else:
                        raise Unsupported("7z substreams %d" % s)
                if not have_sizes:
                    for fi, f in enumerate(folders):
                        if sub["num"][fi] == 1:
                            sub["sizes"].append(f["unpack"])
            else:
                raise Unsupported("7z streams %d" % t)

    def _unpack(self, pack, folders, fi):
        pos, sizes = pack
        start = 32 + pos + sum(sizes[:fi])
        f = folders[fi]
        return _decode(f["coder"], f["props"], self.d[start:start + sizes[fi]], f["unpack"])

    def names(self):
        return list(self.files)

    def read(self, name):
        fi, o, size = self.files[name]
        if fi not in self._out:
            self._out = {fi: self._unpack(self.pack, self.folders, fi)}
        return self._out[fi][o:o + size]

    def packed(self, fi):
        """The stored (packed) bytes of folder FI."""
        pos, sizes = self.pack
        start = 32 + pos + sum(sizes[:fi])
        return self.d[start:start + sizes[fi]]


def _wnum(v):
    """7z's variable-length number."""
    for n in range(8):
        if v < 1 << (7 * (n + 1)):
            return bytes([((0xFF00 >> n) & 0xFF) | (v >> (8 * n))]) + v.to_bytes(8, "little")[:n]
    return b"\xff" + v.to_bytes(8, "little")


def _lzma1(data):
    f = {"id": lzma.FILTER_LZMA1, "lc": 3, "lp": 0, "pb": 2, "dict_size": 1 << 20}
    return lzma.compress(data, lzma.FORMAT_RAW, filters=[f]), b"\x5d\x00\x00\x10\x00"


def rewrite_7z(data, replace):
    """A 7z archive (bytes) like DATA with the files in REPLACE ({name: bytes}) changed: one LZMA2 stream
    each; every other file keeps its stored stream byte for byte, the files their order, times and
    attributes. The layout is the one the game's own archives have and its 7z reader takes: one file per
    folder, the CRCs in the substreams, the header packed (LZMA). Solid archives are not rewritten."""
    z = SevenZip(data)
    if z.solid:
        raise Unsupported("solid 7z archive")
    by_folder = {fi: name for name, (fi, _, _) in z.files.items()}
    coders, packs, crcs = [], [], []
    for fi, f in enumerate(z.folders):
        name = by_folder[fi]
        if name in replace:
            new = replace[name]
            dict_props = 16  # 1 MiB
            packs.append(lzma.compress(new, lzma.FORMAT_RAW, filters=[{"id": lzma.FILTER_LZMA2,
                                                                        "dict_size": 1 << 20}]))
            coders.append((b"\x21", bytes([dict_props]), len(new)))
            crcs.append(zlib.crc32(new))
        else:
            packs.append(z.packed(fi))
            coders.append((f["coder"], f["props"], f["unpack"]))
            crcs.append(zlib.crc32(z.read(name)))
    h = bytearray(b"\x01\x04\x06" + _wnum(0) + _wnum(len(packs)) + b"\x09")
    for p in packs:
        h += _wnum(len(p))
    h += b"\x00\x07\x0b" + _wnum(len(coders)) + b"\x00"
    for cid, props, _ in coders:
        h += b"\x01" + bytes([len(cid) | (0x20 if props else 0)]) + cid
        if props:
            h += _wnum(len(props)) + props
    h += b"\x0c"
    for _, _, size in coders:
        h += _wnum(size)
    h += b"\x00\x08\x0a\x01" + b"".join(struct.pack("<I", c) for c in crcs) + b"\x00\x00"
    names = b"\x00" + b"".join(n.encode("utf-16-le") + b"\x00\x00" for n in z.order)
    h += b"\x05" + _wnum(len(z.order)) + b"\x11" + _wnum(len(names)) + names
    for t in sorted(z.props):
        if t not in (0x11, 0x19):  # names (above), padding
            h += bytes([t]) + _wnum(len(z.props[t])) + z.props[t]
    h += b"\x00\x00"
    body = b"".join(packs)
    hp, hprops = _lzma1(bytes(h))
    eh = (b"\x17\x06" + _wnum(len(body)) + _wnum(1) + b"\x09" + _wnum(len(hp)) + b"\x00"
          + b"\x07\x0b" + _wnum(1) + b"\x00\x01\x23\x03\x01\x01" + _wnum(len(hprops)) + hprops
          + b"\x0c" + _wnum(len(h)) + b"\x0a\x01" + struct.pack("<I", zlib.crc32(bytes(h))) + b"\x00\x00")
    start = struct.pack("<QQI", len(body) + len(hp), len(eh), zlib.crc32(eh))
    return SIG + b"\x00\x04" + struct.pack("<I", zlib.crc32(start)) + start + body + hp + eh


# --- the menu edits ----------------------------------------------------------------------------------------

STARTUP = "manifests/ui_menu_pillar_startup.database.json"
MISSION = "manifests/ui_menu_pillar_mission.database.json"
LABEL = "DIRECT CONNECT"
# 37291 is the game's own (localised) description of the button: "Use a cable other than the Real Tone Cable
# to plug in your electric guitar or bass."
DESCRIPTION = "$[37291]Use a cable other than the Real Tone Cable to plug in your electric guitar or bass."
ICON = "InputMode_DirectConnect.png"  # static.psarc has it (gfxassets/views/inputmode_directconnect.png.dds)
MAIN = "manifests/ui_menu_pillar_main.database.json"
NOTICES = "localization/maingame.csv"
ENUMERATE = "ENUMERATE"
ENUMERATE_ORDER = 1  # after LEARN A SONG
EXIT_LABEL = "$[19651]EXIT GAME"
# 36675 is "You must enable Steam Community In-Game to access the Shop."
NOTICE_ID = "36675"
NOTICE = "Looking for new songs. They will be in the song list in a moment."
# 37359 is the notice MULTIPLAYER INPUT SUPPORT, which says Player 2 must use the Real Tone cable.
MULTIPLAYER_ID = "37359"
MULTIPLAYER = ("Each player can use any valid input device. To choose one load that profile on its own. "
               "Multiplayer is not available in Disconnected Mode.")


def _close(text, i):
    """The index just past the brace that closes the one at text[i] (strings skipped)."""
    depth, j, n = 0, i, len(text)
    while j < n:
        c = text[j]
        if c == '"':
            j += 1
            while j < n and text[j] != '"':
                j += 2 if text[j] == "\\" else 1
        elif c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                return j + 1
        j += 1
    raise ValueError("unbalanced braces")


def _open_of(text, i):
    """The index of the brace that opens the object containing text[i]."""
    depth = 0
    for j in range(i - 1, -1, -1):  # strings in these files hold no braces before an entry's key
        c = text[j]
        if c == "}":
            depth += 1
        elif c == "{":
            if depth == 0:
                return j
            depth -= 1
    raise ValueError("no enclosing object")


def _entries(text, key):
    """(start of the key, start of its object, end of its object) of each `"key": { ... }` in text."""
    for m in re.finditer(r'"%s"\s*:\s*\{' % re.escape(key), text):
        o = m.end() - 1
        yield m.start(), o, _close(text, o)


def _layout(text, key_start):
    nl = "\r\n" if "\r\n" in text else "\n"
    line = text.rfind("\n", 0, key_start) + 1
    return nl, text[line:key_start]


def _obj(nl, ind, key, fields):
    inner = ind + "\t"
    body = ("," + nl).join('%s"%s": %s' % (inner, k, v) for k, v in fields)
    return '"%s":%s%s{%s%s%s%s}' % (key, nl, ind, nl, body, nl, ind)


def edit_startup(text):
    """The cable screen with the "othercable" button (and its image) after the real tone cable, the buttons
    after it one place down; None when it has the button already or is not the expected file."""
    view = None
    for k, o, e in _entries(text, "realtone"):
        if '"$[37286]' in text[o:e] or "REAL TONE CABLE" in text[o:e]:
            view = (k, o, e)  # the button
    if not view:
        return None
    k, o, e = view
    bo = _open_of(text, k)  # the Buttons object
    be = _close(text, bo)
    if re.search(r'"othercable"\s*:', text[bo:be]):
        return None
    m = re.search(r'"SortOrder"\s*:\s*(\d+)', text[o:e])
    order = int(m.group(1)) if m else 0
    nl, ind = _layout(text, k)

    def bump(mm):
        v = int(mm.group(2))
        return mm.group(1) + str(v + 1) if v > order else mm.group(0)

    buttons = text[bo:o] + text[o:e] + "," + nl + ind + _obj(nl, ind, "othercable", [
        ("ID", '"othercable"'), ("Label", '"%s"' % LABEL), ("Description", '"%s"' % DESCRIPTION),
        ("State", '"up"'), ("SortOrder", str(order + 1))]) + re.sub(r'("SortOrder"\s*:\s*)(\d+)', bump, text[e:be])
    out = text[:bo] + buttons + text[be:]
    # The image the screen shows for the choice, next to the real tone cable's.
    for ik, io_, ie in _entries(out, "realtone"):
        if "InputMode_RealToneCable.png" in out[io_:ie]:
            ibo = _open_of(out, ik)
            if not re.search(r'"othercable"\s*:', out[ibo:_close(out, ibo)]):
                nl2, ind2 = _layout(out, ik)
                out = out[:ie] + "," + nl2 + ind2 + _obj(nl2, ind2, "othercable", [
                    ("ID", '"othercable"'), ("File", '"%s"' % ICON),
                    ("InputModeIconFileText", '"Directcon_"')]) + out[ie:]
            break
    return out


def edit_mission(text):
    """The INPUT slider with a fourth value, Direct Connect; None when it has one or is not the expected file."""
    for k, o, e in _entries(text, "inputMode"):
        for ak, ao, ae in _entries(text[o:e], "AcceptedValues"):
            ao, ae = o + ao, o + ae
            vals = text[ao:ae]
            if re.search(r'"3"\s*:', vals) or not re.search(r'"2"\s*:', vals):
                return None
            m = re.search(r'"2"\s*:\s*"[^"]*"', vals)
            nl, ind = _layout(text, ao + m.start())
            end = ao + m.end()
            rest = text[end:ae - 1]
            sep = "" if rest.lstrip().startswith(",") else ","
            if sep == "":
                end = end + rest.index(",") + 1
            return text[:end] + sep + nl + ind + '"3": "%s"' % LABEL + text[end:]
    return None


def edit_exit(text):
    """The main menu's UPLAY entry as EXIT GAME: its label and action. None when it does something else already
    (RSMods' edit counts as there) or is not the expected file."""
    for k, o, e in _entries(text, "uplay"):
        entry, actions = re.subn(r'("id"\s*:\s*")Uplay"', r'\1ExitGame"', text[o:e], count=1)
        entry, labels = re.subn(r'("Label"\s*:\s*")[^"]*', lambda m: m.group(1) + EXIT_LABEL, entry, count=1)
        return text[:o] + entry + text[e:] if actions and labels else None
    return None


def edit_main(text):
    """The main menu's two entries that can't work here; None when neither needs a change."""
    out = text
    for edit in (edit_enumerate, edit_exit):
        edited = edit(out)
        if edited is not None:
            out = edited
    return None if out == text else out


def edit_enumerate(text):
    """The main menu's SHOP entry as ENUMERATE, second in the list, the entries it passes one place down: only
    the label and the sort orders change, its OnSelect stays. None when it has that label or is not the expected
    file."""
    for k, o, e in _entries(text, "shop"):
        if not re.search(r'"StageId"\s*:\s*"Shop"', text[o:e]):
            continue
        label = re.search(r'"Label"\s*:\s*"([^"]*)"', text[o:e])
        order = re.search(r'"SortOrder"\s*:\s*(\d+)', text[o:e])
        if not label or not order or ENUMERATE in label.group(1):
            return None
        old = int(order.group(1))
        bo = _open_of(text, k)  # the Buttons object
        be = _close(text, bo)

        def bump(mm):
            v = int(mm.group(2))
            return mm.group(1) + str(v + 1) if ENUMERATE_ORDER <= v < old else mm.group(0)

        sort = r'("SortOrder"\s*:\s*)(\d+)'
        entry = text[o:o + label.start(1)] + ENUMERATE + text[o + label.end(1):e]
        entry = re.sub(sort, lambda mm: mm.group(1) + str(ENUMERATE_ORDER), entry)
        return text[:bo] + re.sub(sort, bump, text[bo:o]) + entry + re.sub(sort, bump, text[e:be]) + text[be:]
    return None


# --- RS Re-ARMed's options in Adv Display Settings -------------------------------------------------------

OPTIONS = "manifests/ui_menu_pillar_options.database.json"
OPTIONS_MENU = '"AdvancedDisplaySettings"'
# The buttons tools/ws7/recomp/overrides/mods/menu_options.c gives a value and a control to: (id, label row, label,
# description row, description, sort order). The rows are new rows of the game's text table, after its own last
# (37382), in every language for now; a comma in a text is {C}, as in the game's.
MENU_OPTIONS = [
    ("rsFrameRate", "38911", "FRAME RATE", "38912",
     "DISPLAY follows the screen's refresh rate. A fixed rate turns vertical sync off. Takes effect the next time the "
     "game starts.", 10),
    ("rsAnyWindowShape", "38913", "ANY WINDOW SHAPE", "38914",
     "Lets the game's window take any shape{C} the picture drawn to fit it. Disabling this keeps 16:9 with bars. "
     "Takes effect the next time the game starts.", 11),
    ("rsRenderer", "38915", "RENDERER", "38916",
     "METAL is the default. OPENGL is the original renderer{C} should Metal have a problem. Takes effect the next "
     "time the game starts.", 12),
    ("rsMultithreadedGL", "38917", "MULTITHREADED GL ENGINE", "38918",
     "Apple's multithreaded OpenGL engine{C} used only with the OPENGL renderer. Takes effect the next time the game "
     "starts.", 13),
    ("rsLowLatency", "38921", "LOW LATENCY", "38922",
     "Queues two frames for the screen instead of three: each one shows a refresh sooner{C} and a late one as a "
     "repeat. Takes effect the next time the game starts.", 14),
    ("rsMetalFXUpscaling", "38901", "METALFX UPSCALING", "38902",
     "Scales the picture up to a Retina screen's pixels with MetalFX. Disabling this uses a plain scale{C} improving "
     "performance. Takes effect the next time the game starts.", 15),
    ("rsFullRetina", "38903", "FULL RETINA RESOLUTION", "38904",
     "Draws every pixel of a Retina screen{C} for the sharpest picture. Enabling this takes about four times the "
     "graphics work. Takes effect the next time the game starts.", 16),
    ("rsRenderScale", "38919", "RENDER SCALE", "38920",
     "Draws the game at a lower resolution and scales it up to the screen{C} for less graphics work and a softer "
     "picture. Takes effect the next time the game starts.", 17),
    ("rsAntiAliasing", "38905", "ANTI-ALIASING", "38906",
     "Smooths the edges of the 3D scene with MSAA. Lower settings improve performance. Takes effect the next time "
     "the game starts.", 18),
    ("rsTextureFiltering", "38907", "TEXTURE FILTERING", "38908",
     "Keeps textures sharp when seen at an angle. 1X is the game's own. Takes effect the next time the game starts.",
     19),
    ("rsColor", "38909", "COLOR", "38910",
     "SRGB shows the game's colors as it made them. VIVID leaves them unmatched{C} more saturated on a wide-color "
     "screen. Takes effect the next time the game starts.", 20),
    ("rsPerformanceOverlay", "38923", "PERFORMANCE OVERLAY", "38924",
     "Shows Apple's Metal performance HUD over the game (macOS 13 and later): its frame rate{C} frame times and "
     "memory. Takes effect the next time the game starts.", 21),
]
TEXT_COLUMNS = 8  # the table's languages after a row's id


def edit_options(text):
    """Adv Display Settings' definition with RS Re-ARMed's buttons (MENU_OPTIONS) first among its own, laid out as
    the game's; None when it has them already or has no such menu."""
    if '"%s":' % MENU_OPTIONS[0][0] in text:
        return None
    at = text.find(OPTIONS_MENU)
    at = text.find('"Buttons"', at) if at >= 0 else -1
    at = text.find("{", at) if at >= 0 else -1
    if at < 0:
        return None
    nl = "\r\n" if "\r\n" in text else "\n"
    m = re.compile(r"[\r\n]+(\t*)\"", re.S).search(text, at)  # the first button's indentation
    ind = m.group(1) if m else "\t" * 5
    entries = "".join(
        '%s%s"%s":%s%s{%s%s\t"ID": "%s",%s%s\t"Label": "$[%s]%s",%s%s\t"Description" : "$[%s]%s",%s'
        '%s\t"State": "up",%s%s\t"SortOrder" : %d,%s%s\t"Component": "RSButton"%s%s},'
        % (nl, ind, bid, nl, ind, nl, ind, bid, nl, ind, lrow, label, nl, ind, drow, desc.replace("{C}", ","), nl,
           ind, nl, ind, order, nl, ind, nl, ind)
        for bid, lrow, label, drow, desc, order in MENU_OPTIONS)
    return text[:at + 1] + entries + text[at + 1:]


def edit_text_rows(text):
    """The text table with MENU_OPTIONS' rows added where missing; None when it has them all."""
    nl = "\r\n" if "\r\n" in text else "\n"
    rows = []
    for _, lrow, label, drow, desc, _ in MENU_OPTIONS:
        for row, t in ((lrow, label), (drow, desc)):
            if not re.search(r"^%s," % row, text, re.M):
                rows.append(row + "," + ",".join([t] * (TEXT_COLUMNS - 1) + [""]))
    if not rows:
        return None
    body = text if text.endswith(nl) or not text else text + nl
    return body + nl.join(rows) + nl


def edit_maingame(text):
    """The text table's edits: the notices (edit_notice) and RS Re-ARMed's rows (edit_text_rows)."""
    out = edit_notice(text) or text
    out = edit_text_rows(out) or out
    return None if out == text else out


def edit_notice(text):
    """The notices of the Shop's overlay check and of Multiplayer's inputs, in every language; None when they say
    that already or the table has no such rows. A row is `id,text,text,...`, a comma in a text written {C}."""
    out = text
    for row, notice in ((NOTICE_ID, NOTICE), (MULTIPLAYER_ID, MULTIPLAYER)):
        m = re.search(r'^%s,([^\r\n]*)' % row, out, re.M)
        if m and not m.group(1).startswith(notice + ","):
            fields = [notice if f else f for f in m.group(1).split(",")]
            out = out[:m.start(1)] + ",".join(fields) + out[m.end(1):]
    return None if out == text else out


# --- the song list's scroll bar ----------------------------------------------------------------------------

SONGS_LIST = "gfxassets/views/las_songslist.gfx"
LIST_SCENE, THUMB_ID = "ListScene", "ListSceneThumb"
DO_INIT_ACTION = 59  # Flash tag: a sprite's script
A_CONSTANT_POOL, A_PUSH, A_GET_MEMBER, A_SET_MEMBER, A_CALL_METHOD, A_JUMP, A_POP = (
    0x88, 0x96, 0x4E, 0x4F, 0x52, 0x99, 0x17)
P_STRING, P_REGISTER, P_DOUBLE, P_INT, P_POOL = 0, 4, 6, 7, 8  # a push's value kinds
P_SIZES = {1: 4, 2: 0, 3: 0, 4: 1, 5: 1, 6: 8, 7: 4, 8: 1, 9: 2}  # (a string's ends at its zero byte)
THUMB_READS = 6  # the area's bounds: the thumb's _x, _y, _x and _width, _y and _height
THUMB_TOP = 1000000  # the area's depth: the list's own are small numbers


def _swf_tag_list(movie):
    """Each tag of an uncompressed Flash movie: (code, its start, the start of its body, its end)."""
    bits = movie[8] >> 3
    at = 8 + (5 + 4 * bits + 7) // 8 + 4  # past the frame's size, the rate and the frame count
    tags = []
    while at + 2 <= len(movie):
        word = struct.unpack_from("<H", movie, at)[0]
        code, length, body = word >> 6, word & 0x3F, at + 2
        if length == 0x3F:
            length, body = struct.unpack_from("<I", movie, at + 2)[0], at + 6
        tags.append((code, at, body, body + length))
        if code == 0:
            break
        at = body + length
    return tags


def _swf_tags(movie):
    """Each tag of an uncompressed Flash movie: (code, start of its body, its end)."""
    return [(code, body, end) for code, _, body, end in _swf_tag_list(movie)]


def _as_pool(code):
    """An ActionScript's constant pool, its first action; None when it has none."""
    if not code or code[0] != A_CONSTANT_POOL:
        return None
    size, count = struct.unpack_from("<HH", code, 1)
    return [s.decode("latin-1") for s in code[5:3 + size].split(b"\x00")[:count]]


def _as_pushes(code, pool):
    """The pool strings each push of a script pushes, in order: [(string, where its index byte is)]. A function's
    body follows its header as more actions, so this walks them all. None for a push it doesn't know."""
    out, at = [], 0
    while at < len(code):
        op = code[at]
        length = struct.unpack_from("<H", code, at + 1)[0] if op >= 0x80 else 0
        body = at + (3 if op >= 0x80 else 1)
        if op == A_PUSH:
            names, i = [], body
            while i < body + length:
                kind = code[i]
                if kind == P_STRING:
                    i = code.index(b"\x00", i + 1) + 1
                    continue
                if kind not in P_SIZES:
                    return None
                if kind == P_POOL and code[i + 1] < len(pool):
                    names.append((pool[code[i + 1]], i + 1))
                i += 1 + P_SIZES[kind]
            out.append(names)
        at = body + length
    return out


def _thumb_reads(pushes, part):
    """Where the list reads the bounds of the thumb's clickable area from PART (mScrollBarThumb, or mScrollBarTrack
    once edited): the index bytes of PART's name in the pushes from the bounds' xMin to the area's id."""
    ids = [k for k, push in enumerate(pushes) if any(name == THUMB_ID for name, _ in push)]
    if len(ids) != 1:
        return []
    starts = [k for k, push in enumerate(pushes[:ids[0]])
              if [name for name, _ in push][:2] in (["xMin", "mScrollBarThumb"], ["xMin", "mScrollBarTrack"])]
    if not starts:
        return []
    return [at for push in pushes[starts[-1]:ids[0]] for name, at in push if name == part]


def _thumb_depth(pool, edited):
    """The area's depth as the list gives it: push r4, "depth", 0, r1, "mScrollBarThumb"; getMember;
    push "getDepth"; callMethod; setMember. Edited, as many bytes: push r4, "depth", THUMB_TOP; a jump over pops
    that never run; setMember."""
    depth = pool.index("depth")
    old = (bytes([A_PUSH]) + struct.pack("<H", 17) + bytes([P_REGISTER, 4, P_POOL, depth, P_DOUBLE]) + bytes(8)
           + bytes([P_REGISTER, 1, P_POOL, pool.index("mScrollBarThumb"), A_GET_MEMBER, A_PUSH])
           + struct.pack("<H", 2) + bytes([P_POOL, pool.index("getDepth"), A_CALL_METHOD, A_SET_MEMBER]))
    if not edited:
        return old
    head = (bytes([A_PUSH]) + struct.pack("<H", 9) + bytes([P_REGISTER, 4, P_POOL, depth, P_INT])
            + struct.pack("<i", THUMB_TOP))
    pops = len(old) - len(head) - 5 - 1  # (the jump is 5 bytes, setMember 1)
    return head + bytes([A_JUMP]) + struct.pack("<Hh", 2, pops) + bytes([A_POP]) * pops + bytes([A_SET_MEMBER])


def _edit_list_script(script):
    """The list's script with the thumb's clickable area the whole track's, on top; None when it has that already
    or is not the script this knows. Each change is as long as what it replaces, so nothing in the script moves."""
    pool = _as_pool(script)
    names = ("xMin", "depth", "getDepth", "mScrollBarThumb", "mScrollBarTrack", THUMB_ID, LIST_SCENE)
    if pool is None or any(n not in pool or pool.index(n) > 0xFF for n in names):
        return None
    pushes = _as_pushes(script, pool)
    if pushes is None:
        return None
    code = bytearray(script)
    reads = _thumb_reads(pushes, "mScrollBarThumb")
    if reads:
        if len(reads) != THUMB_READS:
            return None
        for at in reads:
            code[at] = pool.index("mScrollBarTrack")
    elif len(_thumb_reads(pushes, "mScrollBarTrack")) != THUMB_READS:
        return None
    old, new = _thumb_depth(pool, False), _thumb_depth(pool, True)
    if new not in script:
        if script.count(old) != 1:
            return None
        at = script.index(old)
        code[at:at + len(old)] = new
    return None if code == script else bytes(code)


# --- a scroll bar for each list ---------------------------------------------------------------------------
# The lists' class (ListScene, one script in every list's movie) draws the bar only for a list that asks for it,
# with ShowScrollBar(true) and SetScrollBarXPos: Learn a Song, the inlays, the rewards, the shop's and the Tone
# Designer's songs do. The others scroll with none: Nonstop Play's songs, the Tone Designer's amps, pedals, cabinets
# and the rest, Session Mode's bands and instruments, the profiles, the settings. The edit gives a list that doesn't
# ask a bar where the lists that do have it, BAR_GAP right of the list's panel, unless that is past the class's own
# place for a bar at the screen's right edge (Load Tone's short list and the skins have no room there). The bar also
# needs the game's count of the list's entries; for a list it doesn't give one, the edit takes the list's own (the
# inlays ask for a bar, but the game gives them no count, so they had none).
# In ActionScript, where UpdateScrollBar starts:
#
#   if (!this.mShowScrollBar) {                     // r9: a register the method has and doesn't use
#     r9 = this.mTopListPanelXPos + this.mTopListPanelWidth + BAR_GAP;
#     if (!(BAR_RIGHTMOST < r9)) { this.mShowScrollBar = true; this.mScrollBarXPos = r9; }
#   }
#   if (this.mDataObj.ListEntriesCount == undefined) this.mDataObj.ListEntriesCount = this.mSortedData.length;
A_EQUALS2, A_TRY, A_WITH, A_IF, A_DEFINE_FUNCTION, A_DEFINE_FUNCTION2 = 0x49, 0x8F, 0x94, 0x9D, 0x9B, 0x8E
P_BOOL, P_POOL16 = 5, 9
BAR_GAP, BAR_RIGHTMOST, BAR_REGISTER = 2, 1270, 9  # (BAR_RIGHTMOST: mScrollBarXPos as the class has it)
BAR_NAMES = ("mShowScrollBar", "mTopListPanelXPos", "mTopListPanelWidth", "mScrollBarXPos", "mDataObj",
             "ListEntriesCount", "mSortedData")


def _as_actions(code):
    """Each action of a script in order, a function's body as more actions: (its start, op, its body, its end)."""
    out, at = [], 0
    while at < len(code):
        op = code[at]
        length = struct.unpack_from("<H", code, at + 1)[0] if op >= 0x80 else 0
        body = at + (3 if op >= 0x80 else 1)
        out.append((at, op, body, body + length))
        at = body + length
    if at != len(code):
        raise Unsupported("a script's last action past its end")
    return out


def _as_replace(code, at, end, new):
    """CODE with its actions [AT, END) replaced by NEW (which has its own branches right): each branch around them and
    each function that holds them kept right. Unsupported for a branch from outside into them, or a try or with block
    (they have sizes too)."""
    delta = len(new) - (end - at)
    out = bytearray(code[:at] + new + code[end:])

    def moved(x):
        return x + delta if x >= end else x
    for start, op, body, stop in _as_actions(code):
        if op in (A_TRY, A_WITH):
            raise Unsupported("a try or with block in the script")
        if at <= start < end:
            continue
        if op in (A_IF, A_JUMP):
            target = stop + struct.unpack_from("<h", code, body)[0]
            if at < target < end:
                raise Unsupported("a branch into the edit")
            struct.pack_into("<h", out, moved(body), moved(target) - moved(stop))
        elif op in (A_DEFINE_FUNCTION, A_DEFINE_FUNCTION2):  # (its body's size is the header's last word)
            size = struct.unpack_from("<H", code, stop - 2)[0]
            if stop <= at and end <= stop + size:
                struct.pack_into("<H", out, moved(stop - 2), size + delta)
    return bytes(out)


def _as_push(*items):
    data = b"".join(items)
    return bytes([A_PUSH]) + struct.pack("<H", len(data)) + data


def _as_name(pool, name):  # a push's item: a name of the constant pool
    i = pool.index(name)
    return bytes([P_POOL, i]) if i <= 0xFF else bytes([P_POOL16]) + struct.pack("<H", i)


def _as_text(text):  # a push's item: a string of its own
    return bytes([P_STRING]) + text.encode("latin-1") + b"\0"


def _as_branch(op, offset):
    return bytes([op]) + struct.pack("<Hh", 2, offset)


# A few actions put together (_as_asm): ("label", name) marks a place, ("if" | "jump", name) branches to it,
# ("push", value, ...) pushes strings (the pool's, else their own), whole numbers, booleans, None (undefined) and
# ("r", n) registers, ("store", n) stores the top in register n, ("function", registers, items) is a method (this in
# r1, as the game's: no arguments or super), and an int is an action without data.
A_ADD2, A_SUBTRACT, A_MULTIPLY, A_DIVIDE, A_LESS2, A_OR, A_NOT, A_STORE_REGISTER = (
    0x47, 0x0B, 0x0C, 0x0D, 0x48, 0x11, 0x12, 0x87)
A_GET_VARIABLE = 0x1C
P_UNDEFINED = 3
AS_METHOD_FLAGS = 0x29  # preload this, suppress arguments and super


def _as_value(pool, v):
    if v is None:
        return bytes([P_UNDEFINED])
    if isinstance(v, bool):
        return bytes([P_BOOL, int(v)])
    if isinstance(v, int):
        return bytes([P_INT]) + struct.pack("<i", v)
    if isinstance(v, tuple):  # ("r", n)
        return bytes([P_REGISTER, v[1]])
    return _as_name(pool, v) if v in pool else _as_text(v)


def _as_asm(pool, items):
    def size(item):
        if isinstance(item, int):
            return 1
        kind = item[0]
        return {"label": 0, "if": 5, "jump": 5, "store": 4}.get(kind) or len(piece(item, {}, 0))

    def piece(item, labels, end):
        if isinstance(item, int):
            return bytes([item])
        kind = item[0]
        if kind == "label":
            return b""
        if kind in ("if", "jump"):
            return _as_branch(A_IF if kind == "if" else A_JUMP, labels[item[1]] - end if labels else 0)
        if kind == "store":
            return bytes([A_STORE_REGISTER]) + struct.pack("<HB", 1, item[1])
        if kind == "push":
            return _as_push(*(_as_value(pool, v) for v in item[1:]))
        if kind == "function":
            body = _as_asm(pool, item[2])
            head = b"\x00" + struct.pack("<HBH", 0, item[1], AS_METHOD_FLAGS) + struct.pack("<H", len(body))
            return bytes([A_DEFINE_FUNCTION2]) + struct.pack("<H", len(head)) + head + body
        raise ValueError(kind)
    labels, at = {}, 0
    for item in items:
        if not isinstance(item, int) and item[0] == "label":
            labels[item[1]] = at
        at += size(item)
    out, at = b"", 0
    for item in items:
        part = piece(item, labels, at + size(item))
        out += part
        at += len(part)
    return out


def _bar_shown(pool):
    """The actions put where UpdateScrollBar starts (a scroll bar for each list, above)."""
    this, x = ("r", 1), ("r", BAR_REGISTER)
    get, put = A_GET_MEMBER, A_SET_MEMBER
    return _as_asm(pool, [
        ("push", this, "mShowScrollBar"), get, ("if", "placed"),
        ("push", this, "mTopListPanelXPos"), get, ("push", this, "mTopListPanelWidth"), get, A_ADD2,
        ("push", BAR_GAP), A_ADD2, ("store", BAR_REGISTER), A_POP,
        ("push", BAR_RIGHTMOST, x), A_LESS2, ("if", "placed"),
        ("push", this, "mShowScrollBar", True), put, ("push", this, "mScrollBarXPos", x), put,
        ("label", "placed"),
        ("push", this, "mDataObj"), get, ("push", "ListEntriesCount"), get, ("push", None), A_EQUALS2, A_NOT,
        ("if", "counted"),
        ("push", this, "mDataObj"), get, ("push", "ListEntriesCount", this, "mSortedData"), get, ("push", "length"),
        get, put,
        ("label", "counted")])


def _as_registers(code):
    """The registers CODE's actions use: those its pushes read, its stores write and its functions' parameters are
    given (a function's body follows its header as more actions, so its too); None for a push it doesn't know."""
    used = set()
    for _, op, body, stop in _as_actions(code):
        if op == A_STORE_REGISTER:
            used.add(code[body])
        elif op == A_DEFINE_FUNCTION2:  # name, parameters' count, registers, flags, then each parameter's register
            at = code.index(b"\x00", body) + 1
            count, at = struct.unpack_from("<H", code, at)[0], at + 5
            for _ in range(count):
                used.add(code[at])
                at = code.index(b"\x00", at + 1) + 1
        elif op == A_PUSH:
            at = body
            while at < stop:
                kind = code[at]
                if kind == P_STRING:
                    at = code.index(b"\x00", at + 1) + 1
                    continue
                if kind not in P_SIZES:
                    return None
                if kind == P_REGISTER:
                    used.add(code[at + 1])
                at += 1 + P_SIZES[kind]
    return used


def _edit_bar_shown(script):
    """The lists' script with a bar for each list (_bar_shown, where UpdateScrollBar starts: the one method that
    begins by reading mShowScrollBar, with a register BAR_REGISTER that nothing in it uses); None when it has that
    already or is not the script this knows."""
    pool = _as_pool(script)
    if pool is None or any(n not in pool for n in BAR_NAMES):
        return None
    shown = _bar_shown(pool)
    if shown in script:
        return None
    first = _as_asm(pool, [("push", ("r", 1), "mShowScrollBar"), A_GET_MEMBER, A_NOT])  # (as UpdateScrollBar starts)
    starts = []
    for start, op, body, stop in _as_actions(script):
        if op == A_DEFINE_FUNCTION2 and script[stop:stop + len(first)] == first:
            registers = script[script.index(b"\x00", body) + 3]  # (after its name and its parameters' count)
            used = _as_registers(script[start:stop + struct.unpack_from("<H", script, stop - 2)[0]])
            free = registers > BAR_REGISTER and used is not None and BAR_REGISTER not in used
            starts.append(stop if free else None)
    if len(starts) != 1 or starts[0] is None:
        return None
    return _as_replace(script, starts[0], starts[0], shown)


def _swf_with_tag(movie, start, end, code, content):
    """MOVIE with CONTENT in place of its tag at [START, END), in a long tag, and the movie's length made right."""
    out = bytearray(movie[:start] + struct.pack("<HI", (code << 6) | 0x3F, len(content)) + content + movie[end:])
    struct.pack_into("<I", out, 4, len(out))
    return bytes(out)


def edit_songs_list(data):
    """A list's movie (Learn a Song's, or another with the same script) with its scroll bar's thumb grabbed anywhere
    on the track, and a bar for a list that doesn't ask for one; None when it has that already or is not the
    expected file. The bar's edit is left out of a script it can't be made in. The script gets longer, so its tag
    and the movie say their new lengths."""
    if len(data) < 9 or data[:3] not in (b"GFX", b"FWS"):  # (an uncompressed movie, as the game's is)
        return None
    try:
        for code, start, body, end in _swf_tag_list(data):
            if code != DO_INIT_ACTION:
                continue
            script = _edit_list_script(data[body + 2:end])  # (after the sprite's id)
            if script is None:
                continue
            try:
                script = _edit_bar_shown(script) or script
            except (Unsupported, struct.error, ValueError, IndexError):
                pass
            return _swf_with_tag(data, start, end, code, data[body:body + 2] + script)
    except (struct.error, ValueError, IndexError):
        return None
    return None


# --- Adv Display Settings' rows -------------------------------------------------------------------------
# The menu's movie (static.psarc's gfxassets/views/displaysettings.gfx, Display Settings' too) colors a row's label
# WHITE_CLR, or BLUE_CLR_HILITE while it has the focus: it has no look for a row that can't be used. The edit gives it
# one in the movie's own greys, GREY_CLR and GREY_CLR_HILITE, for a row whose States.Locked is set (a field of the
# game's own StatesTemplate; menu_options.c sets it for the options of the renderer not chosen). Its rows' scripts get
# longer, so what holds them is made to say so (_as_replace, edit_display_settings).

DISPLAY_SETTINGS = "gfxassets/views/displaysettings.gfx"


def _edit_row_color(script):
    """The rows' script with a locked row's color (GREY_CLR, GREY_CLR_HILITE with the focus); None when it has that
    already or is not the script this knows. Its color is the pushes of

        "fontColor", rows[i].States.Focus == true ? rs.common.Prefs.BLUE_CLR_HILITE : rs.common.Prefs.WHITE_CLR

    and the edit makes the choice one of four, by States.Locked as well."""
    pool = _as_pool(script)
    names = ("fontColor", "States", "Focus", "rs", "common", "Prefs", "WHITE_CLR", "BLUE_CLR_HILITE")
    if pool is None or any(n not in pool for n in names) or _as_text("GREY_CLR_HILITE") in script:
        return None
    prefs = (_as_push(_as_name(pool, "rs")) + bytes([A_GET_VARIABLE]) + _as_push(_as_name(pool, "common"))
             + bytes([A_GET_MEMBER]) + _as_push(_as_name(pool, "Prefs")) + bytes([A_GET_MEMBER]))

    def color(item):
        return prefs + _as_push(item) + bytes([A_GET_MEMBER])
    white, blue = color(_as_name(pool, "WHITE_CLR")), color(_as_name(pool, "BLUE_CLR_HILITE"))
    focus = (bytes([A_GET_MEMBER]) + _as_push(_as_name(pool, "Focus")) + bytes([A_GET_MEMBER])
             + _as_push(bytes([P_BOOL, 1])) + bytes([A_EQUALS2]))
    choice = _as_branch(A_IF, len(white) + 5) + white + _as_branch(A_JUMP, len(blue)) + blue
    font, states = _as_name(pool, "fontColor"), _as_name(pool, "States")
    head = bytes([A_PUSH]) + struct.pack("<H", len(font) + 2 + len(states)) + font + bytes([P_REGISTER])
    found = [(m.start(), m.group(1)[0]) for m in re.finditer(
        re.escape(head) + b"(.)" + re.escape(states + focus + choice), script, re.S)]  # (the row's register)
    if len(found) != 1:
        return None
    at, row = found[0]
    at += len(head) + 1 + len(states) + len(focus)  # the choice's start (its if)
    if at not in {start for start, _, _, _ in _as_actions(script)}:
        return None
    locked = (_as_push(bytes([P_REGISTER, row]), _as_name(pool, "States")) + bytes([A_GET_MEMBER])
              + _as_push(_as_text("Locked")) + bytes([A_GET_MEMBER]))
    grey, grey_hilite = color(_as_text("GREY_CLR")), color(_as_text("GREY_CLR_HILITE"))
    focused = locked + _as_branch(A_IF, len(blue) + 5) + blue + _as_branch(A_JUMP, len(grey_hilite)) + grey_hilite
    unfocused = (locked + _as_branch(A_IF, len(white) + 5) + white + _as_branch(A_JUMP, len(grey) + 5 + len(focused))
                 + grey + _as_branch(A_JUMP, len(focused)))
    return _as_replace(script, at, at + len(choice), _as_branch(A_IF, len(unfocused)) + unfocused + focused)


# --- Adv Display Settings' list, scrolled -----------------------------------------------------------------
# SettingsScene (the list of the settings menus: Display, Visual Quality, Play, ...) lays its rows out from
# mEntryInitialPosY down, the focused row's CURRENT SETTING line and description under it, and has no scroll: rows past
# its panel run off the screen. The edit wraps its PopulateButtons on the class's prototype: the rows laid out from a
# scroll's distance higher (rsScroll, the least change that shows the focused row with its description), those outside
# the panel hidden, and the game's own scroll bar beside the panel (rsTrack, rsThumb: CommonLib's clips, attached as
# its lists do in ListScene.UpdateScrollBar) while the list is longer than the panel.
# A list that fits is as it was. In ActionScript:
#
#   if (proto.rsPopulateButtons == undefined) {
#     proto.rsPopulateButtons = proto.PopulateButtons;
#     proto.PopulateButtons = function () {      // r2 top, r3 the panel's height for rows, r4 rows, r5 a row's
#       if (this.rsScroll == undefined) this.rsScroll = 0;  // height, r6 the description's, r7 the most scroll,
#       r2 = this.mEntryInitialPosY;             // r8 the focused row (-1 none), r9 the scroll, r10 a row, r11 a y,
#       this.mEntryInitialPosY = r2 - this.rsScroll; this.rsPopulateButtons(); this.mEntryInitialPosY = r2;
#       ... r9 from the focused row, kept within 0 and r7; laid out again when it changed;
#       ... each row's _visible: inside the panel; the bar shown, sized and placed, or hidden
#     };
#   }
LIST_MARGIN = 24  # between the last row and the panel's bottom
SETTINGS_BAR_GAP, THUMB_LEAST = 8, 100  # the bar right of the panel; the thumb's least height, the game's lists' own


def _scroll_method():
    R = lambda n: ("r", n)  # noqa: E731
    G, S, ADD, SUB, MUL, DIV, LT, NOT, POP = (A_GET_MEMBER, A_SET_MEMBER, A_ADD2, A_SUBTRACT, A_MULTIPLY, A_DIVIDE,
                                              A_LESS2, A_NOT, A_POP)

    def member(*path):  # this.a.b... pushed
        return [("push", R(1), path[0]), G] + [x for name in path[1:] for x in (("push", name), G)]

    def cls():  # rs.views.optionsmenu.controls.SettingsScene
        return ([("push", "rs"), A_GET_VARIABLE]
                + [x for name in ("views", "optionsmenu", "controls", "SettingsScene") for x in (("push", name), G)])

    def call(obj, name, *args):  # obj.name(args) with its result popped; args: lists of items, each one value
        return [x for a in reversed(args) for x in a] + [("push", len(args))] + obj + [("push", name),
                                                                                         A_CALL_METHOD, POP]

    def layout(scroll):  # the game's layout, its rows from r2 - scroll
        return ([("push", R(1), "mEntryInitialPosY", R(2))] + scroll + [SUB, S]
                + call([("push", R(1))], "rsPopulateButtons")
                + [("push", R(1), "mEntryInitialPosY", R(2)), S])

    def row_id(reg):  # this.mSortedData[reg].objId
        return member("mSortedData") + [("push", reg), G, ("push", "objId"), G]

    def attach(linkage, name):  # this[name] = this.attachMovie(linkage, name, this.getNextHighestDepth())
        return [("push", R(1), name, 0, R(1), "getNextHighestDepth"), A_CALL_METHOD,
                ("push", name, linkage, 3, R(1), "attachMovie"), A_CALL_METHOD, S]

    def put(clip, name, value):  # this[clip][name] = value
        return member(clip) + [("push", name)] + value + [S]

    body = (
        member("rsScroll") + [("push", None), A_EQUALS2, NOT, ("if", "scrolled"), ("push", R(1), "rsScroll", 0), S,
                              ("label", "scrolled")]
        + member("mEntryInitialPosY") + [("store", 2), POP]
        + layout(member("rsScroll"))
        # r3: the panel's bottom less the margin, from r2
        + cls() + [("store", 13), POP, ("push", R(13), "BACKGROUND_Y"), G, ("push", R(13), "BACKGROUND_HEIGHT"), G, ADD,
                   ("push", LIST_MARGIN), SUB, ("push", R(2)), SUB, ("store", 3), POP]
        + member("mSortedData", "length") + [("store", 4), POP]
        + [("push", R(4), 0), A_EQUALS2, ("if", "bar")]  # (no rows: no scroll, no bar)
        + member("mEntryHeight") + [("store", 5), POP]
        + member("mEntryCurrentPosY") + [("push", R(2))] + member("rsScroll") + [SUB, SUB, ("store", 6), POP]
        + [("push", R(4), R(5)), MUL, ("push", R(6)), ADD, ("push", R(3)), SUB, ("store", 7), POP,
           ("push", R(7), 0), LT, NOT, ("if", "most"), ("push", 0), ("store", 7), POP, ("label", "most")]
        # r8: the focused row
        + [("push", -1), ("store", 8), POP, ("push", 0), ("store", 10), POP, ("label", "find"),
           ("push", R(10), R(4)), LT, NOT, ("if", "found"),
           ("push", R(1), "mDataObj"), G, ("push", "Buttons"), G] + row_id(R(10)) + [G, ("push", "States"), G,
                                                                                    ("push", "Focus"), G,
                                                                                    ("push", True), A_EQUALS2, NOT,
                                                                                    ("if", "next")]
        + [("push", R(10)), ("store", 8), POP, ("label", "next"), ("push", R(10), 1), ADD, ("store", 10), POP,
           ("jump", "find"), ("label", "found")]
        # r9: the least change that shows it and its description, within 0 and r7
        + member("rsScroll") + [("store", 9), POP, ("push", R(8), 0), LT, ("if", "kept"),
                                ("push", R(8), R(5)), MUL, ("push", R(5)), ADD, ("push", R(6)), ADD, ("push", R(3)),
                                SUB, ("store", 11), POP, ("push", R(9), R(11)), LT, NOT, ("if", "low"),
                                ("push", R(11)), ("store", 9), POP, ("label", "low"),
                                ("push", R(8), R(5)), MUL, ("store", 11), POP, ("push", R(11), R(9)), LT, NOT,
                                ("if", "kept"), ("push", R(11)), ("store", 9), POP, ("label", "kept")]
        + [("push", R(7), R(9)), LT, NOT, ("if", "under"), ("push", R(7)), ("store", 9), POP, ("label", "under"),
           ("push", R(9), 0), LT, NOT, ("if", "over"), ("push", 0), ("store", 9), POP, ("label", "over")]
        + [("push", R(9))] + member("rsScroll") + [A_EQUALS2, ("if", "shown"), ("push", R(1), "rsScroll", R(9)), S]
        + layout([("push", R(9))]) + [("label", "shown")]
        # each row inside the panel shown, the others hidden
        + [("push", 0), ("store", 10), POP, ("label", "row"), ("push", R(10), R(4)), LT, NOT, ("if", "bar"),
           ("push", R(10), R(5)), MUL, ("push", R(9)), SUB, ("store", 11), POP,
           ("push", R(8), 0), LT, ("if", "placed"), ("push", R(8), R(10)), LT, NOT, ("if", "placed"),
           ("push", R(11), R(6)), ADD, ("store", 11), POP, ("label", "placed")]
        + member("mButtons") + row_id(R(10)) + [G, ("push", "_visible"),  # = !(y < -1 || r3 + 1 < y + r5)
                                                ("push", R(11), -1), LT, ("push", R(3), 1), ADD, ("push", R(11), R(5)),
                                                ADD, LT, A_OR, NOT, S]
        + [("push", R(10), 1), ADD, ("store", 10), POP, ("jump", "row")]
        # the game's scroll bar (CommonLib's clips, as its lists attach them): made when first needed, shown while the
        # list is longer than the panel, its thumb at the focused row's place (at the scroll's without one)
        + [("label", "bar")] + member("rsTrack") + [("push", None), A_EQUALS2, NOT, ("if", "made"),
                               ("push", 0, R(7)), LT, NOT, ("if", "end")]
        + attach("scrollBar_trackUp", "rsTrack") + attach("scrollBar_thumbDown", "rsThumb") + [("label", "made")]
        + [("push", R(4), 0), A_EQUALS2, ("push", 0, R(7)), LT, NOT, A_OR, NOT, ("store", 11), POP]
        + put("rsTrack", "_visible", [("push", R(11))]) + put("rsThumb", "_visible", [("push", R(11))])
        + [("push", R(11)), NOT, ("if", "end")]
        + cls() + [("store", 13), POP, ("push", R(13), "BACKGROUND_X"), G, ("push", R(13), "BACKGROUND_WIDTH"), G, ADD,
                   ("push", SETTINGS_BAR_GAP), ADD, ("store", 15), POP]
        + put("rsTrack", "_x", [("push", R(15))]) + put("rsThumb", "_x", [("push", R(15))])
        + put("rsTrack", "_y", [("push", R(13), "BACKGROUND_Y"), G])
        + put("rsTrack", "_height", [("push", R(13), "BACKGROUND_HEIGHT"), G])
        + [("push", R(13), "BACKGROUND_HEIGHT"), G, ("push", R(4)), DIV, ("store", 14), POP,
           ("push", R(14), THUMB_LEAST), LT, NOT, ("if", "thumb"), ("push", THUMB_LEAST), ("store", 14), POP,
           ("label", "thumb")]
        + put("rsThumb", "_height", [("push", R(14))])
        + [("push", R(9), R(7)), DIV, ("store", 11), POP, ("push", R(8), 0), LT, ("if", "at"), ("push", R(4), 1), SUB,
           ("push", 0), A_EQUALS2, ("if", "at"), ("push", R(8), R(4), 1), SUB, DIV, ("store", 11), POP, ("label", "at")]
        + put("rsThumb", "_y", [("push", R(13), "BACKGROUND_Y"), G, ("push", R(13), "BACKGROUND_HEIGHT"), G,
                                ("push", R(14)), SUB, ("push", R(11)), MUL, ADD])
        + [("label", "end")])
    return body


SCROLL_REGISTERS = 16  # the method's: r1 this, r2-r15 its own


def _scroll_install(pool):
    """The actions that wrap SettingsScene.prototype.PopulateButtons, once (register 2 as the class's script has it)."""
    path = [x for name in ("views", "optionsmenu", "controls", "SettingsScene", "prototype")
            for x in (("push", name), A_GET_MEMBER)]
    proto = ("r", 2)
    return _as_asm(pool, [("push", "rs"), A_GET_VARIABLE] + path + [
        ("store", 2), A_POP,
        ("push", proto, "rsPopulateButtons"), A_GET_MEMBER, ("push", None), A_EQUALS2, A_NOT, ("if", "done"),
        ("push", proto, "rsPopulateButtons", proto, "PopulateButtons"), A_GET_MEMBER, A_SET_MEMBER,
        ("push", proto, "PopulateButtons"), ("function", SCROLL_REGISTERS, _scroll_method()), A_SET_MEMBER,
        ("label", "done")])


def _edit_list_scroll(script):
    """SettingsScene's script with its list scrolled (_scroll_method), the wrapping added where it ends (so it runs
    also when the class was made already); None when it has it or is not that script."""
    pool = _as_pool(script)
    names = ("SettingsScene", "PopulateButtons", "mEntryInitialPosY", "mEntryCurrentPosY", "mEntryHeight",
             "mSortedData", "objId", "mButtons", "mDataObj", "BACKGROUND_Y", "BACKGROUND_HEIGHT", "CLASS_NAME")
    if pool is None or any(n not in pool for n in names) or _as_text("rsPopulateButtons") in script:
        return None
    if _as_push(_as_name(pool, "CLASS_NAME"))[3:] not in script or b"SettingsScene\0" not in script:
        return None
    acts = _as_actions(script)
    if not acts or acts[-1][1] != 0:  # (it ends with End)
        return None
    at = acts[-1][0]
    return _as_replace(script, at, at + 1, _scroll_install(pool) + b"\x00")


def edit_display_settings(data):
    """Adv Display Settings' movie with a locked row greyed and its list scrolled; None when it has that already
    or is not the expected file. Each script it edits gets longer: its tag and the movie say their new lengths."""
    if len(data) < 9 or data[:3] not in (b"GFX", b"FWS"):  # (an uncompressed movie, as the game's is)
        return None
    try:
        out, last = [], 0
        for code, start, body, end in _swf_tag_list(data):
            if code != DO_INIT_ACTION:
                continue
            old = data[body + 2:end]  # (after the sprite's id)
            script = _edit_row_color(old) or old
            script = _edit_list_scroll(script) or script
            if script == old:
                continue
            content = data[body:body + 2] + script
            out += [data[last:start], struct.pack("<HI", (code << 6) | 0x3F, len(content)), content]
            last = end
        if not out:
            return None
        movie = bytearray(b"".join(out) + data[last:])
        struct.pack_into("<I", movie, 4, len(movie))
        return bytes(movie)
    except (struct.error, ValueError, IndexError, Unsupported):
        return None


EDITS = {STARTUP: edit_startup, MISSION: edit_mission, MAIN: edit_main, NOTICES: edit_maingame,
         OPTIONS: edit_options}
BYTE_EDITS = {SONGS_LIST: edit_songs_list}  # (a movie, not text)
MOVIE_EDITS = {DISPLAY_SETTINGS: edit_display_settings}  # a static.psarc movie's own edit, after the lists'
# Learn & Play's: without RS Re-ARMed's options in Adv Display Settings, whose native half is the Mac game's only
LP_EDITS = dict({k: f for k, f in EDITS.items() if k != OPTIONS}, **{NOTICES: edit_notice})
EDITIONS = ("mac", "learn-play")


def edits_of(edition):
    """(text edits, static.psarc movies' own edits) of EDITION's overlay."""
    if edition not in EDITIONS:
        raise ValueError("no such edition: %s" % edition)
    return (EDITS, MOVIE_EDITS) if edition == "mac" else (LP_EDITS, {})


def edit_file(name, data, edits=EDITS):
    """A file's bytes with its edit made (NAME in lower case, in EDITS or BYTE_EDITS); None when it needs none."""
    if name in BYTE_EDITS:
        edited = BYTE_EDITS[name](data)
        return None if edited is None or edited == data else edited
    # surrogateescape: the main menu's file has bytes that are not UTF-8, and they stay as they are
    text = data.decode("utf-8", "surrogateescape")
    edited = edits[name](text)
    return None if edited is None or edited == text else edited.encode("utf-8", "surrogateescape")


# --- the lists of static.psarc ----------------------------------------------------------------------------

STATIC, STATIC_ORIGINAL = "static.psarc", "static.psarc.original"
VIEWS = "gfxassets/views/"


def static_of(game_dir):
    """The game's own static.psarc in GAME_DIR: static.psarc.original when the copy has it (static.psarc is a link to
    it or to the title logo's static.psarc.rearmed), else static.psarc; None without one."""
    for name in (STATIC_ORIGINAL, STATIC):
        p = os.path.join(game_dir, name)
        if os.path.isfile(p):
            return p
    return None


def plan_movies(static_path, edition="mac"):
    """{movie name: edited bytes} of the menu movies in static.psarc (gfxassets/views/<name>.gfx) whose list
    edit_songs_list changes, or that have an edit of their own (MOVIE_EDITS); the song list's own is cache.psarc's
    (BYTE_EDITS). {} when the archive can't be read, and a movie that can't be is passed by: these are optional, as
    the song list's."""
    movie_edits = edits_of(edition)[1]
    out = {}
    try:
        ps = Psarc(static_path) if static_path else None
    except (Unsupported, OSError):
        return out
    if ps is None:
        return out
    try:
        for name in ps.names:
            low = name.lower()
            if not low.startswith(VIEWS) or not low.endswith(".gfx") or "/" in low[len(VIEWS):] or low in BYTE_EDITS:
                continue
            try:
                data = ps.read(name)
                edited = edit_songs_list(data)
                if low in movie_edits:
                    edited = movie_edits[low](edited or data) or edited
            except Unsupported:
                continue
            if edited is not None:
                out[low] = edited
    finally:
        ps.close()
    return out


# --- the overlay -------------------------------------------------------------------------------------------

PATCH_DIR = "Update"
PATCH_NAME = "patcharchives.psarc"


def plan(cache_path, edition="mac"):
    """{archive name in cache.psarc: {file name in it: edited bytes}} of what EDITION's overlay changes. An archive
    that can't be read once the menu's files are found, or a BYTE_EDITS file that can't be, is passed by."""
    edits = edits_of(edition)[0]
    out = {}
    ps = Psarc(cache_path)
    try:
        found = set()
        for name in ps.names:
            if not name.lower().endswith(".7z"):
                continue
            try:
                z = SevenZip(ps.read(name))
            except Unsupported:
                if found.issuperset(edits):
                    continue  # (only the song list's movie can be in it)
                raise
            for inner in z.names():
                want = inner.lower()
                if want in found or (want not in edits and want not in BYTE_EDITS):
                    continue
                found.add(want)
                try:
                    edited = edit_file(want, z.read(inner), edits)
                except Unsupported:
                    if want in edits:
                        raise
                    continue
                if edited is not None:
                    out.setdefault(name, {})[inner] = edited
            if len(found) == len(edits) + len(BYTE_EDITS):
                break
    finally:
        ps.close()
    return out


def build(cache_path, static_path=None, edition="mac"):
    """(patcharchives.psarc as bytes, [changed file names]) for EDITION's cache.psarc and the static.psarc beside it;
    (None, []) when they need no change. The psarc holds the changed cacheN.7z archives under their own names and
    the changed menu movies under theirs. An archive with a menu file that can't be rewritten (Unsupported: a solid
    one) costs the whole overlay, one with only BYTE_EDITS files only those."""
    edits = edits_of(edition)[0]
    changes = plan(cache_path, edition)
    movies = plan_movies(static_path, edition)
    entries, names = [], []
    if changes:
        ps = Psarc(cache_path)
        try:
            for name, files in sorted(changes.items()):
                try:
                    entries.append((name, rewrite_7z(ps.read(name), files)))
                except Unsupported:
                    if any(f.lower() in edits for f in files):
                        raise
                    continue
                names += files
        finally:
            ps.close()
    entries += sorted(movies.items())
    names += movies
    if not entries:
        return None, []
    return write_psarc(entries, (cache_path,)), sorted(names)


def make(game_dir, edition="mac"):
    """Write (or remove) GAME_DIR/Update/patcharchives.psarc for GAME_DIR/cache.psarc and its static.psarc, for
    EDITION's game (gamefiles.py's).
    Returns the names of the files the overlay changes ([] when there is none). Raises Unsupported when the cache
    cannot be read (the overlay is then removed)."""
    dst = os.path.join(game_dir, PATCH_DIR, PATCH_NAME)
    cache = os.path.join(game_dir, "cache.psarc")
    try:
        data, names = build(cache, static_of(game_dir), edition) if os.path.isfile(cache) else (None, [])
    except Unsupported:
        remove(game_dir)
        raise
    if data is None:
        remove(game_dir)
        return []
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    tmp = dst + ".part"
    with open(tmp, "wb") as f:
        f.write(data)
    os.replace(tmp, dst)
    return names


def remove(game_dir):
    """Remove the overlay (and Update/ when that leaves it empty)."""
    d = os.path.join(game_dir, PATCH_DIR)
    p = os.path.join(d, PATCH_NAME)
    if os.path.isfile(p):
        os.unlink(p)
    try:
        os.rmdir(d)
    except OSError:
        pass


def main(argv=None):
    argv = sys.argv[1:] if argv is None else argv
    if not 2 <= len(argv) <= 3 or argv[0] not in ("make", "show") or argv[2:] and argv[2] not in EDITIONS:
        sys.stderr.write(__doc__.split("\n\n")[1] + "\n")
        return 64
    try:
        if argv[0] == "show":
            for name, files in sorted(plan(argv[1], *argv[2:]).items()):
                for f in sorted(files):
                    print("%s: %s" % (name, f))
            for f in sorted(plan_movies(static_of(os.path.dirname(os.path.abspath(argv[1]))), *argv[2:])):
                print("%s: %s" % (STATIC, f))
        else:
            names = make(*argv[1:])
            print("overlay: %s" % (", ".join(names) if names else "nothing to change"))
    except (Unsupported, OSError) as e:
        sys.stderr.write("error: %s\n" % e)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
