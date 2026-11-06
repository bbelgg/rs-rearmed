#!/usr/bin/env python3
"""The menu overlay (Direct Connect, Enumerate, Multiplayer's notice): a small patch archive made from the user's own cache.psarc.

    cache_overlay.py make GAME_DIR      write GAME_DIR/Update/patcharchives.psarc (or remove it: nothing to change)
    cache_overlay.py show CACHE.psarc   print what the overlay would change, write nothing

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

This makes those edits in the user's own copies of the files and packs each archive that holds them, rewritten
with only those files changed, into the patch container the engine mounts at start when it is there,
<game>/Update/patcharchives.psarc:

    patcharchives.psarc             mounted by the engine at PatchUpdate/
      cache4.7z, cache7.7z          the cache.psarc archives with the changed files (whichever cacheN.7z has them)

The engine's own patch layer takes only .new/.delta archives from the container, so the game reads the
cacheN.7z from there through a hook: FrontEnd_OpenFile opens PatchUpdate/cacheN.7z first and the cache.psarc
one when the overlay has none (Mac: tools/ws7/recomp/overrides/mods/mods.c). Without the hook the overlay
does nothing.

cache.psarc itself is never written, so a modded cache works too: files that already have the entries are left
as they are, and with nothing to change there is no overlay. Both editions use it (gamefiles.py runs it after a copy
or refresh of the game files). Nothing of the game is in this file: the archives are read and written here.

Standard library only (any Python 3.9+); the archive tables are AES-encrypted, done with /usr/bin/openssl.
"""
import hashlib
import lzma
import os
import re
import struct
import subprocess
import sys
import zlib

# --- psarc -------------------------------------------------------------------------------------------------

# The well-known Rocksmith 2014 table-of-contents key (AES-256-CFB).
TOC_KEY = "C53DB23870A1A2F71CAE64061FDD0E1157309DC85204D4C5BFDF25090DF2572C"
TOC_IV = "E915AA018FEF71FC508132E4BB4CEB42"
PSARC_VERSION = 0x00010004
BLOCK = 0x10000
FLAG_TOC_ENCRYPTED = 4
ENTRY_SIZE = 30


class Unsupported(Exception):
    """An archive this module cannot read (the overlay is then left out; the game runs without it)."""


def _toc_crypt(data, decrypt):
    # CFB is a stream mode, but openssl wants whole blocks with -nopad: pad, then trim.
    padded = data + b"\0" * (-len(data) % 16)
    try:
        out = subprocess.run(["openssl", "enc", "-d" if decrypt else "-e", "-aes-256-cfb", "-nopad",
                              "-K", TOC_KEY, "-iv", TOC_IV], input=padded, capture_output=True, check=True).stdout
    except (OSError, subprocess.CalledProcessError) as e:
        raise Unsupported("openssl failed: %s" % e)
    return out[:len(data)]


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
            toc = _toc_crypt(toc, True)
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


def write_psarc(files):
    """A psarc (bytes) with FILES, [(name, data)]: zlib blocks of 64 KiB, encrypted table, entry 0 the names."""
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
    toc = _toc_crypt(bytes(toc + ztab), False)
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


def edit_main(text):
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


EDITS = {STARTUP: edit_startup, MISSION: edit_mission, MAIN: edit_main, NOTICES: edit_notice}


# --- the overlay -------------------------------------------------------------------------------------------

PATCH_DIR = "Update"
PATCH_NAME = "patcharchives.psarc"


def plan(cache_path):
    """{archive name in cache.psarc: {file name in it: edited bytes}} of what the overlay changes."""
    out = {}
    ps = Psarc(cache_path)
    try:
        found = set()
        for name in ps.names:
            if not name.lower().endswith(".7z"):
                continue
            z = SevenZip(ps.read(name))
            for inner in z.names():
                want = inner.lower()
                if want not in EDITS or want in found:
                    continue
                found.add(want)
                # surrogateescape: the main menu's file has bytes that are not UTF-8, and they stay as they are
                text = z.read(inner).decode("utf-8", "surrogateescape")
                edited = EDITS[want](text)
                if edited is not None and edited != text:
                    out.setdefault(name, {})[inner] = edited.encode("utf-8", "surrogateescape")
            if len(found) == len(EDITS):
                break
    finally:
        ps.close()
    return out


def build(cache_path):
    """(patcharchives.psarc as bytes, [changed file names]) for a cache.psarc; (None, []) when it needs no
    change. The psarc holds the changed cacheN.7z archives under their own names."""
    changes = plan(cache_path)
    if not changes:
        return None, []
    ps = Psarc(cache_path)
    try:
        entries = [(name, rewrite_7z(ps.read(name), files)) for name, files in sorted(changes.items())]
    finally:
        ps.close()
    return write_psarc(entries), sorted(f for files in changes.values() for f in files)


def make(game_dir):
    """Write (or remove) GAME_DIR/Update/patcharchives.psarc for GAME_DIR/cache.psarc. Returns the names of the
    files the overlay changes ([] when there is none). Raises Unsupported when the cache cannot be read (the
    overlay is then removed)."""
    dst = os.path.join(game_dir, PATCH_DIR, PATCH_NAME)
    cache = os.path.join(game_dir, "cache.psarc")
    try:
        data, names = build(cache) if os.path.isfile(cache) else (None, [])
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
    if len(argv) != 2 or argv[0] not in ("make", "show"):
        sys.stderr.write(__doc__.split("\n\n")[1] + "\n")
        return 64
    try:
        if argv[0] == "show":
            for name, files in sorted(plan(argv[1]).items()):
                for f in sorted(files):
                    print("%s: %s" % (name, f))
        else:
            names = make(argv[1])
            print("overlay: %s" % (", ".join(names) if names else "nothing to change"))
    except (Unsupported, OSError) as e:
        sys.stderr.write("error: %s\n" % e)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
