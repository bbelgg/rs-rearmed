#!/usr/bin/env python3
"""RS Re-ARMed's own copy of the game files, so a Steam update, uninstall or library move can't break an install.

    gamefiles.py sync SOURCE DEST --edition mac|learn-play [--dlc-from DIR]... [--steam DIR]
        copy what the game reads from SOURCE (Steam's game folder) into DEST: new and changed files are
        copied, files of an earlier copy that SOURCE no longer has are removed (only those: nothing else in
        DEST is touched). On an APFS volume that also holds SOURCE every file is a clone (clonefile(2)):
        instant, and no extra space until one side changes. Otherwise a real copy, after a check that there
        is room for it, with progress. An interrupted run continues where it was when run again.
        A new copy of a game on another drive (an external Steam library) goes onto that drive as clones,
        <volume>/RS Re-ARMed/game or .../learn-play/game, and DEST becomes a link to it (see "where the copy
        is" below; RS_GAME_COPY_DIR chooses the place); a copy already there stays where it is, unless
        RS_GAME_COPY_DIR=home (to DEST, a real copy) or =drive (to the game's drive, clones) asks for the other.
    gamefiles.py refresh DEST
        the same again, from the SOURCE (and options) the copy was made from.
    gamefiles.py check DEST
        is the copy complete (exit 0), or does it need a refresh (exit 1)? A DEST that is a link to a copy
        on a drive that is not connected says so (exit 1).
    gamefiles.py dlc-fetch STATE [--steam DIR] [--wait S] [--edition mac|learn-play] [--copy DEST]
        a game from Steam's download (Learn & Play, and the Mac game when it is not installed in Steam): have
        Steam download every DLC the signed-in account owns, one after another, into its depot folders, where the
        copy finds the archives of its kind. STATE (a JSON file) remembers what has been fetched, so a stopped
        run and a later refresh fetch only the rest. Never fails an install: what could not be fetched is named
        in a "note:" line, and the next run tries again. No DLC owned: nothing happens. --copy DEST: for the copy
        DEST, and only when its game is from Steam's download (a Mac game installed in Steam: nothing happens).
    gamefiles.py mac-game [--folder DIR] [--steam DIR] [--timeout S] [--locate]
        find the Mac game: DIR; else the game installed in a Steam library; else Steam's download of the Mac
        depot, which is started when it is not there yet (--locate: only looked for, exit 1), as the Learn & Play
        installer gets the Windows game: Steam is opened, and once someone is signed in the running Steam is
        given the download command. Waits with progress, with "guide" lines saying what, if anything, is left for
        the user to do, and checks that what arrived is the supported build. SIGUSR1 is the installer app's Try
        Again. The last lines are "game-from folder|library|download" and "game-folder DIR". Exit status as
        lp_install.py's: 2 a different build of the game, 3 timed out, 4 no room for the download.
    gamefiles.py mac-game-command | mac-game-space | mac-game-redirect DIR
        the Mac depot's line for the Steam console; whether there is room for it where Steam downloads (exit 1 if
        not); and Steam's download folder made a link to DIR (another disk), which is remembered in the
        edition's folder (download-dir). The same as lp_install.py's game-command, game-space and game-redirect.

What is copied (DEST/.rearmed/manifest.json lists it; it is written last, so a copy without it is incomplete):
  mac         the game folder (the game installed in Steam, or Steam's download of the Mac depot: then with the
              Mac archives of the DLC depots, --dlc-from by default) without Rocksmith2014.app (the install has its own build) and Steam's files: its
              *.psarc (static.psarc as static.psarc.original), metadata, the folders (base, guitarcade, venues, Tmp, ...),
              and from dlc/ (official DLC and custom songs, in their subfolders) only the Mac archives,
              *_m.psarc. Rocksmith.ini goes to .rearmed/Rocksmith.ini: the launcher makes the game's offline
              copy from it.
  learn-play  the Windows game folder (Steam's depot download; static.psarc as static.psarc.original), dlc/ with
              only the PC archives, *_p.psarc, and the *_p.psarc in each --dlc-from folder (the Mac game's dlc/:
              Steam downloads both kinds).
In both, the title logo, static.psarc.rearmed, is made in the copy by the install (install.sh; lp_install.py
title_logo), never in the game folder.
In both, DEST/static.psarc is a link: to static.psarc.original here, and the launcher and the Learn & Play app
point it at static.psarc.rearmed at each start when that is there (RS_REARMED=0: the original). A
static.psarc.rearmed that was made in the copy is removed when static.psarc changes: it was made from the old one.
DEST/.rearmed/ also holds source.json (what the copy is made from) and steam.env (KEY=VALUE lines the launcher
and the Learn & Play app put in the game's environment unless already set: the DLC the account has installed
and the language, from Steam's appmanifest (the offline Steam layer reads Steam's folder for them otherwise),
with the AppIDs the copy's dlc/ archives carry, so custom songs show whatever AppID they carry;
and, for Learn & Play, the DLC the account's ownership ticket lists; RS_STEAM_OWNED, the first Rocksmith's DLC the
account owns, asked of the Steam client when that account is logged in (rs1-owned.json keeps the answer); and
RS_STEAM_ID, the Steam account the edition
plays as: the account whose Steam license has the edition's
depot, so another account logged in to Steam later doesn't change the saves, profiles and songs. RS_STEAM_ID
set when the copy is made chooses it; a refresh keeps an earlier choice unless the license says otherwise).
After each copy or refresh, DEST/Update/patcharchives.psarc is made from the copy's cache.psarc: the Direct
Connect menu entries (cache_overlay.py, next to this file); cache.psarc itself is not changed.

Files are compared by size and modification time; when only the time differs, by SHA-256. A file being
replaced is written next to it first and then renamed over it, so a clone is never written through. SOURCE is
only read. Paths in DEST that are symbolic links (the launcher's mirror in earlier versions) are removed, never followed.

Options: --progress-lines ("progress F LABEL" and "guide" lines, the installer app's format, instead of a status
line in a terminal); --steam DIR (default $RS_STEAM_DIR, else ~/Library/Application Support/Steam), where the appmanifest
is looked for when it is not next to SOURCE.

Standard library only (any Python 3.9+). Exit status: 0 done, 1 failed or incomplete, 64 usage (mac-game: more,
see there).
"""
import argparse
import ctypes
import errno
import fnmatch
import hashlib
import json
import os
import re
import shutil
import signal
import stat
import struct
import subprocess
import sys
import time

# The Direct Connect overlay (cache_overlay.py): next to this file in the repository and in the installs.
# Run with python3 -I, so the folder is not on the path by itself.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
try:
    import cache_overlay
except ImportError:
    cache_overlay = None
del sys.path[0]

FORMAT = 1
META = ".rearmed"
MANIFEST = "manifest.json"
PREVIOUS = "manifest.previous.json"  # the set of an earlier copy while a refresh runs
SOURCE_INFO = "source.json"
ENUMERATED = "enumerated.txt"  # songs the game's ENUMERATE entry copied in (shims/steam/src/enumerate.h)
STEAM_ENV = "steam.env"
KEY_PENDING = "key-pending"  # the copy's archives could not be read yet (Copier._archive_key)
TMP_SUFFIX = ".rearmed-part"
SKIP_NAMES = {".DS_Store", "Thumbs.db", "desktop.ini"}
FREE_CDLC_APP = 248750  # the free DLC most custom songs carry (shims/steam: owned in "installed" mode)
APP_ID = 221680
MARGIN = 256 << 20  # free space kept on top of what a real copy needs
EDITIONS = ("mac", "learn-play")
# What the source must have to be the game: a refresh from a folder that lost it (an unmounted library, a
# half-uninstalled game) would otherwise remove the copy's files.
STATIC, STATIC_ORIGINAL, STATIC_REARMED = "static.psarc", "static.psarc.original", "static.psarc.rearmed"
MARKERS = {"mac": ("static.psarc", "songs.psarc"), "learn-play": ("Rocksmith2014.exe", "static.psarc")}
# The process of each edition: a refresh waits until it is not running.
PROCESS = {"mac": "Rocksmith2014", "learn-play": "rs2014_lp"}


class Failure(Exception):
    """What went wrong, in words for the user; code is the exit status (lp_install.py's are in its header)."""

    def __init__(self, msg, code=1):
        Exception.__init__(self, msg)
        self.code = code


def human(n):
    for unit, size in (("GB", 1 << 30), ("MB", 1 << 20), ("KB", 1 << 10)):
        if n >= size:
            return "%.1f %s" % (n / size, unit)
    return "%d bytes" % n


class Progress:
    """"progress F LABEL" lines (the installer app reads them), or one status line rewritten in place in a
    terminal, or nothing. F only grows."""

    def __init__(self, lines=False, stream=None):
        self.stream = stream or sys.stdout
        self.lines = lines
        self.tty = not lines and hasattr(self.stream, "isatty") and self.stream.isatty()
        self.fraction = 0.0
        self.last = None
        self.pending = False

    def __call__(self, fraction, label):
        fraction = max(self.fraction, min(max(fraction, 0.0), 1.0))
        if (fraction, label) == (self.fraction, self.last):
            return
        self.fraction, self.last = fraction, label
        if self.lines:
            self.stream.write("progress %.4f %s\n" % (fraction, label))
        elif self.tty:
            self.stream.write("\r\033[K" + label)
            self.pending = True
        else:
            return
        self.stream.flush()

    def say(self, msg):
        if self.pending:
            self.stream.write("\n")
            self.pending = False
        self.stream.write(msg + "\n")
        self.stream.flush()


# --- clones and copies -------------------------------------------------------------------------------------

CLONE_NOOWNERCOPY = 0x0002
_libc = None


def clonefile(src, dst):
    """clonefile(2): True when dst is now a clone of src, False when the volume cannot clone it (another
    volume, not APFS). Other errors raise OSError."""
    global _libc
    if _libc is None:
        try:
            _libc = ctypes.CDLL(None, use_errno=True)
            _libc.clonefile.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint32]
        except (OSError, AttributeError):
            _libc = False
    if not _libc:
        return False
    if _libc.clonefile(os.fsencode(src), os.fsencode(dst), CLONE_NOOWNERCOPY) == 0:
        return True
    e = ctypes.get_errno()
    if e in (errno.EXDEV, errno.ENOTSUP, errno.EOPNOTSUPP, errno.ENOSYS, errno.EINVAL):
        return False
    raise OSError(e, os.strerror(e), dst)


def copy_data(src, dst, on_bytes, chunk=8 << 20):
    with open(src, "rb") as fi, open(dst, "wb") as fo:
        while True:
            b = fi.read(chunk)
            if not b:
                break
            fo.write(b)
            on_bytes(len(b))


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for b in iter(lambda: f.read(8 << 20), b""):
            h.update(b)
    return h.hexdigest()


def free_bytes(path):
    while path and not os.path.exists(path):
        path = os.path.dirname(path)
    st = os.statvfs(path or "/")
    return st.f_bavail * st.f_frsize


def running(edition):
    """The edition's game is running (pgrep), so its files must not change under it."""
    try:
        return subprocess.run(["/usr/bin/pgrep", "-x", os.environ.get("RS_GAME_PROCESS") or PROCESS[edition]],
                              stdout=subprocess.DEVNULL,
                              stderr=subprocess.DEVNULL).returncode == 0
    except OSError:
        return False


# --- what is copied ----------------------------------------------------------------------------------------

def walk_files(top):
    """(path relative to top, absolute path) of every file under top, following symbolic links (a dlc folder
    on another disk is a link), once per real folder; hidden and Finder files left out, sorted."""
    out = []
    seen = set()
    for d, subdirs, files in os.walk(top, followlinks=True):
        real = os.path.realpath(d)
        if real in seen:
            subdirs[:] = []
            continue
        seen.add(real)
        subdirs[:] = sorted(s for s in subdirs if not s.startswith("."))
        for f in sorted(files):
            if f.startswith(".") or f in SKIP_NAMES:
                continue
            p = os.path.join(d, f)
            if os.path.isfile(p):  # a broken link is not
                out.append((os.path.relpath(p, top), p))
    return out


def plan(edition, source, dlc_from=()):
    """{destination path relative to DEST: source file} for the edition (see the top of this file)."""
    if edition not in EDITIONS:
        raise Failure("unknown edition %r (%s)" % (edition, " or ".join(EDITIONS)))
    want = "*_m.psarc" if edition == "mac" else "*_p.psarc"
    files = {}
    keys = set()  # case-insensitive, as the volumes usually are: the first source of a name wins

    def add(dst, src):
        if dst.lower() not in keys:
            keys.add(dst.lower())
            files[dst] = src

    for name in sorted(os.listdir(source)) if os.path.isdir(source) else []:
        p = os.path.join(source, name)
        if name.startswith(".") or name in SKIP_NAMES:
            continue
        if os.path.isdir(p):
            if name.lower() == "dlc":
                for rel, f in walk_files(p):
                    if fnmatch.fnmatch(os.path.basename(rel).lower(), want):
                        add(os.path.join("dlc", rel), f)
            elif edition == "mac" and name.endswith(".app"):
                continue  # the game's own app: the install has its build
            else:
                for rel, f in walk_files(p):
                    add(os.path.join(name, rel), f)
        elif os.path.isfile(p):
            if name == STATIC:
                add(STATIC_ORIGINAL, p)
            elif edition == "learn-play":
                add(name, p)
            elif name == "Rocksmith.ini":
                add(os.path.join(META, "Rocksmith.ini"), p)
            elif name != STATIC_REARMED and (name.lower().endswith(".psarc") or name == "metadata"):
                add(name, p)
    for extra in dlc_from:
        if os.path.isdir(extra):
            for rel, f in walk_files(extra):
                if fnmatch.fnmatch(os.path.basename(rel).lower(), want):
                    add(os.path.join("dlc", rel), f)
    return files


def find_appmanifest(source, steam):
    """Steam's appmanifest for the game: next to SOURCE's library (steamapps/common/<game>), else in Steam's
    own library."""
    name = "appmanifest_%d.acf" % APP_ID
    for d in (os.path.dirname(os.path.dirname(os.path.abspath(source))), os.path.join(steam, "steamapps")):
        p = os.path.join(d, name)
        if os.path.isfile(p):
            return p
    return None


def steam_is_updating(appmanifest):
    """Steam is changing the game's files (the appmanifest's StateFlags: 4 is fully installed)."""
    if not appmanifest:
        return False
    with open(appmanifest, errors="replace") as f:
        m = re.search(r'"StateFlags"\s+"(\d+)"', f.read())
    return bool(m) and int(m.group(1)) != 4


def steam_env(appmanifest, account=None, archive_apps=(), owned=()):
    """The steam.env lines from an appmanifest: the installed DLC (dlcappid entries; Steam installs only DLC
    the account owns) plus the free 248750 and ARCHIVE_APPS (the AppIDs the copy's dlc/ archives carry, see
    dlc_appids), and the language; with OWNED, RS_STEAM_OWNED: the first Rocksmith's DLC the account owns (see
    rs1_owned); with ACCOUNT (a Steam account id, see steam_account), RS_STEAM_ID: the account whose saves and
    profiles the game uses."""
    lines = []
    text = ""
    if appmanifest:
        with open(appmanifest, errors="replace") as f:
            text = f.read()
    if appmanifest or archive_apps:
        dlc = {int(m) for m in re.findall(r'"dlcappid"\s+"(\d+)"', text)}
        dlc.add(FREE_CDLC_APP)
        dlc |= set(archive_apps)
        dlc.discard(APP_ID)  # the base game is not a DLC (an RS1 compatibility archive carries it)
        lines.append("RS_STEAM_DLC=" + ",".join(str(d) for d in sorted(dlc)))
        m = re.search(r'"language"\s+"([A-Za-z_]+)"', text)
        if m:
            lines.append("RS_STEAM_LANGUAGE=" + m.group(1))
    if owned:
        lines.append("RS_STEAM_OWNED=" + ",".join(str(a) for a in sorted(owned)))
    if account:
        lines.append("RS_STEAM_ID=%d" % (STEAMID64_BASE + account))
    return lines


# --- the AppIDs of the song archives ------------------------------------------------------------------------
#
# The game mounts a dlc/ archive only when the AppID in its appid.appid entry is a DLC that Steam lists and
# reports installed. Steam installs only the official DLC the account owns, so the copy's dlc/ archives are
# that DLC and custom songs, which carry any AppID (mostly 248750, a free DLC that Learn & Play accounts
# usually don't have). So every AppID the copy's archives carry goes into steam.env's RS_STEAM_DLC: songs are
# listed whatever AppID they carry, in both editions, and official DLC is unchanged. Read at each copy and
# refresh (custom songs reach the copy only through one); .rearmed/dlc-appids.json keeps each archive's AppID
# by size and time, so a refresh reads only new and changed archives.

DLC_APPIDS = "dlc-appids.json"


def archive_appid(path):
    """The AppID in a song archive's appid.appid entry, or None (no such entry, or not a readable psarc)."""
    if cache_overlay is None:
        return None
    try:
        p = cache_overlay.Psarc(path)
    except Exception:  # not a psarc, damaged, or no openssl: the archive just adds no AppID
        return None
    try:
        name = next((n for n in p.names if n.rsplit("/", 1)[-1].lower() == "appid.appid"), None)
        m = re.match(rb"\s*(\d+)", p.read(name)) if name else None
        return (int(m.group(1)) or None) if m else None
    except Exception:
        return None
    finally:
        p.close()


def dlc_appids(dest, entries, readable=True):
    """The AppIDs the archives under DEST/dlc carry, ENTRIES being the copy's files (relative paths). Not
    READABLE (the archives' key is not at hand yet): what an earlier copy found, and the rest is left for the
    run that can read them."""
    cache_path = os.path.join(dest, META, DLC_APPIDS)
    old = read_json(cache_path)
    old = old.get("files") if isinstance(old, dict) and isinstance(old.get("files"), dict) else {}
    new, apps = {}, set()
    for rel in sorted(entries):
        if rel.split(os.sep)[0] != "dlc" or not rel.lower().endswith(".psarc"):
            continue
        try:
            st = os.stat(os.path.join(dest, rel))
        except OSError:
            continue
        key = [st.st_size, st.st_mtime_ns]
        known = old.get(rel)
        if isinstance(known, list) and len(known) == 3 and known[:2] == key:
            app = known[2]
        elif readable:
            app = archive_appid(os.path.join(dest, rel))
        else:
            continue
        new[rel] = key + [app]
        if isinstance(app, int) and app > 0:
            apps.add(app)
    write_json(cache_path, {"format": FORMAT, "files": new})
    return apps


# --- the Steam account of an edition -----------------------------------------------------------------------
#
# The offline Steam layer plays as the account Steam last logged in (loginusers.vdf), and the game's saves and
# profiles are per account. With a second Steam account on the Mac (e.g. one that owns Learn & Play), the
# edition then runs as the wrong account: its profiles, and the songs that account's profile shows. So each
# copy records its account in steam.env (RS_STEAM_ID), and the launchers keep using it whoever is logged in.
#
# The account of an edition is the one whose Steam license has the edition's depot: Steam keeps each account's
# app ownership ticket in userdata/<account>/config/localconfig.vdf (apptickets, the licenses = package ids)
# and each package's depots in appcache/packageinfo.vdf. Several or no such accounts: the appmanifest's
# LastOwner when it is one of them (or of the accounts with a ticket), else the one that played the game
# longest through Steam. RS_STEAM_ID set when the copy is made (a SteamID64 or an account id) chooses it.

STEAMID64_BASE = 76561197960265728
LP_DEPOT = 221683  # the Learn & Play (Windows) depot
MAC_DEPOT = 221682  # the Mac game's depot
HOW_OVERRIDE = "RS_STEAM_ID when the copy was made"
HOW_LICENSE = "the account whose Steam license has this edition's depot"


def account_of(steam_id):
    """An account id from a SteamID64 or an account id (string or int); None if neither."""
    try:
        v = int(str(steam_id).strip())
    except ValueError:
        return None
    if v > STEAMID64_BASE:
        v -= STEAMID64_BASE
    return v if 0 < v < 1 << 32 else None


def _vdf_blocks(text, key):
    """The texts of the `"key" { ... }` blocks of a text VDF (braces balanced)."""
    for m in re.finditer(r'"%s"\s*\{' % re.escape(key), text, re.IGNORECASE):
        depth, i = 1, m.end()
        while i < len(text) and depth:
            depth += {"{": 1, "}": -1}.get(text[i], 0)
            i += 1
        yield text[m.end():i - 1]


def _vdf_block(text, key):
    """The text of the first `"key" { ... }` block of a text VDF, or ''."""
    return next(_vdf_blocks(text, key), "")


def ticket_dlc(hexticket, app=APP_ID):
    """The DLC AppIDs of an app ownership ticket, signed by Steam: after the licenses a uint16 count and, for
    each DLC, its uint32 AppID, a uint16 count and that many uint32 licenses. None when it is not a ticket for app."""
    try:
        b = bytes.fromhex(hexticket)
        if len(b) < 42 or struct.unpack_from("<I", b, 16)[0] != app:
            return None
        at = 42 + 4 * struct.unpack_from("<H", b, 40)[0]
        count = struct.unpack_from("<H", b, at)[0]
        at += 2
        out = []
        for _ in range(count):
            appid, licenses = struct.unpack_from("<IH", b, at)
            at += 6 + 4 * licenses
            out.append(appid)
        return out
    except (ValueError, struct.error):
        return None


def account_dlc(steam, account, app=APP_ID):
    """The DLC account owns, from its ownership ticket in localconfig.vdf (Steam's own file; the signed part of
    it, and nothing to do with signing in); None when Steam hasn't written a ticket for app there."""
    try:
        with open(os.path.join(steam, "userdata", str(account), "config", "localconfig.vdf"), errors="replace") as f:
            m = re.search(r'"%d"\s+"([0-9a-fA-F]+)"' % app, _vdf_block(f.read(), "apptickets"))
    except OSError:
        return None
    return ticket_dlc(m.group(1), app) if m else None


def ticket_licenses(hexticket, app=APP_ID):
    """The package ids of an app ownership ticket (hex, as localconfig.vdf keeps it): length, version,
    SteamID64, app id, two IPs, flags, issued, expires, then a uint16 count of uint32 licenses."""
    try:
        b = bytes.fromhex(hexticket)
        if len(b) < 42 or struct.unpack_from("<I", b, 16)[0] != app:
            return set()
        n = struct.unpack_from("<H", b, 40)[0]
        return set(struct.unpack_from("<%dI" % n, b, 42)) if len(b) >= 42 + 4 * n else set()
    except (ValueError, struct.error):
        return set()


def package_depots(packageinfo):
    """{package id: set of depot ids} from Steam's binary appcache/packageinfo.vdf (v28+: per package its id,
    SHA-1, change number, access token, then binary key-values)."""
    try:
        with open(packageinfo, "rb") as f:
            d = f.read()
    except OSError:
        return {}
    if len(d) < 8 or d[1:4] != b"UV\x06":
        return {}
    token = d[0] >= 0x28

    def cstr(i):
        j = d.index(b"\0", i)
        return d[i:j].decode("utf-8", "replace"), j + 1

    def kv(i):  # (dict, next offset)
        out = {}
        while True:
            t = d[i]
            i += 1
            if t == 8:
                return out, i
            name, i = cstr(i)
            if t == 0:
                out[name], i = kv(i)
            elif t == 1:
                out[name], i = cstr(i)
            elif t in (2, 3, 4):
                out[name] = struct.unpack_from("<i", d, i)[0]
                i += 4
            elif t in (7, 10):
                out[name] = struct.unpack_from("<Q", d, i)[0]
                i += 8
            else:
                raise ValueError("binary VDF type %d" % t)

    out, i = {}, 8
    try:
        while i + 4 <= len(d):
            pid = struct.unpack_from("<I", d, i)[0]
            if pid == 0xFFFFFFFF:
                break
            i += 4 + 20 + 4 + (8 if token else 0)
            body, i = kv(i)
            pkg = next(iter(body.values()), {}) if body else {}
            depots = pkg.get("depotids", {}) if isinstance(pkg, dict) else {}
            out[pid] = {v for v in depots.values() if isinstance(v, int)}
    except (ValueError, IndexError, struct.error):
        pass
    return out


def steam_accounts(steam, app=APP_ID):
    """{account id: (licenses, playtime in minutes)} of every account with an ownership ticket for APP."""
    out = {}
    userdata = os.path.join(steam, "userdata")
    try:
        names = os.listdir(userdata)
    except OSError:
        return out
    for name in names:
        if not name.isdigit():
            continue
        try:
            with open(os.path.join(userdata, name, "config", "localconfig.vdf"), errors="replace") as f:
                text = f.read()
        except OSError:
            continue
        m = re.search(r'"%d"\s+"([0-9a-fA-F]+)"' % app, _vdf_block(text, "apptickets"))
        if not m:
            continue
        play = 0
        for b in _vdf_blocks(text, str(app)):
            p = re.search(r'^\s*"Playtime"\s+"(\d+)"', b, re.MULTILINE)
            play = max(play, int(p.group(1)) if p else 0)
        out[int(name)] = (ticket_licenses(m.group(1), app), play)
    return out


def recorded_account(env_path):
    """The account an earlier copy recorded in steam.env, or None."""
    try:
        with open(env_path) as f:
            m = re.search(r"^RS_STEAM_ID=(\d+)\s*$", f.read(), re.MULTILINE)
    except OSError:
        return None
    return account_of(m.group(1)) if m else None


def edition_depots(edition, appmanifest):
    """The depots that make an edition: Learn & Play's Windows depot; for the Mac game the appmanifest's
    installed depots that are not DLC, and without an appmanifest (a game from Steam's download, not installed
    in Steam) the Mac depot."""
    if edition == "learn-play":
        return {LP_DEPOT}
    if not appmanifest:
        return {MAC_DEPOT}
    with open(appmanifest, errors="replace") as f:
        block = _vdf_block(f.read(), "InstalledDepots")
    return {int(m.group(1)) for m in re.finditer(r'"(\d+)"\s*\{([^{}]*)\}', block)
            if "dlcappid" not in m.group(2)} or {MAC_DEPOT}


def steam_account(edition, steam, appmanifest, override=None):
    """(account id or None, how it was chosen) of the Steam account an edition plays as."""
    if override:
        a = account_of(override)
        if a:
            return a, HOW_OVERRIDE
    last = None
    if appmanifest:
        with open(appmanifest, errors="replace") as f:
            m = re.search(r'"LastOwner"\s+"(\d+)"', f.read())
        last = account_of(m.group(1)) if m else None
    accounts = steam_accounts(steam)
    depots = edition_depots(edition, appmanifest)
    packages = package_depots(os.path.join(steam, "appcache", "packageinfo.vdf")) if accounts and depots else {}
    owners = sorted(a for a, (lic, _) in accounts.items()
                    if any(packages.get(p, set()) & depots for p in lic))
    if len(owners) == 1:
        return owners[0], HOW_LICENSE
    pool = owners or sorted(accounts)
    if last and (last in pool or not pool):
        return last, "the appmanifest's LastOwner"
    if pool:
        return max(pool, key=lambda a: accounts[a][1]), "the account that played the game longest"
    return None, "no Steam account found"


# --- where the copy is ------------------------------------------------------------------------------------
#
# A new copy of a game on another drive than the edition's folder (e.g. a Steam library on an external disk)
# goes onto that drive, where its files are clones, instead of a real copy on the startup disk:
# <volume>/RS Re-ARMed/game (Mac) or <volume>/RS Re-ARMed/learn-play/game (Learn & Play), and DEST (the usual
# path in the edition's folder) is a symbolic link to it, so the launchers and a refresh keep using DEST. When
# the top of the volume is not writable, the first writable folder on the way down to the game folder (not in
# Steam's steamapps) holds RS Re-ARMed/ instead. Only when a clone from the game folder into that place works
# (APFS); otherwise (exFAT, HFS+, a network volume) a real copy at DEST, as before. RS_GAME_COPY_DIR=DIR puts
# a new copy in DIR/game or DIR/learn-play/game whatever the volumes (RS_GAME_COPY_DIR=home: at DEST;
# RS_GAME_COPY_DIR=drive: as without it). A copy that is already there (a real folder at DEST, or a link to one)
# stays where it is, unless home or drive asks for the other place (see _move). The copy records the link
# that leads to it in .rearmed/link.json: a copy another link leads to (a second install) is not taken over.

COPY_DIR_ENV = "RS_GAME_COPY_DIR"
CHOICE_HOME, CHOICE_DRIVE = "home", "drive"  # its two words; anything else is a folder
MOVING, MOVED = ".moving", ".moved"  # a copy on its way to a new place, and the old one at DEST on its way out
FOLDER = "RS Re-ARMed"
LINK_INFO = "link.json"
VOLUMES = "/Volumes"


def edition_path(edition):
    """The copy's path below an RS Re-ARMed folder."""
    return "game" if edition == "mac" else os.path.join("learn-play", "game")


def _device(path):
    return os.stat(path).st_dev


def existing(path):
    """PATH, or the nearest folder above it that exists."""
    path = os.path.abspath(path)
    while not os.path.exists(path) and path != os.path.dirname(path):
        path = os.path.dirname(path)
    return path


def mount_point(path, device=_device):
    """The top folder of the volume PATH is on."""
    path = os.path.realpath(path)
    dev = device(path)
    while path != os.path.dirname(path):
        parent = os.path.dirname(path)
        if device(parent) != dev:
            return path
        path = parent
    return path


def drive_of(target):
    """The volume name of a path in /Volumes, else None."""
    root = VOLUMES.rstrip(os.sep) + os.sep
    t = os.path.abspath(target)
    return (t[len(root):].split(os.sep)[0] or None) if t.startswith(root) else None


def mounted(name):
    """Whether /Volumes/NAME is a mounted volume (not only a folder left there)."""
    p = os.path.join(VOLUMES, name)
    return os.path.isdir(p) and os.path.ismount(p)


def link_target(link):
    return os.path.join(os.path.dirname(link), os.readlink(link))


def missing_copy(dest, is_mounted=None):
    """None, or (connected, message) when DEST is a link to a copy on another drive that leads nowhere:
    the drive is not connected (False), or it is but the copy there is gone (True). The launchers say the same
    (launch.sh, lp_main.c)."""
    if not os.path.islink(dest) or os.path.exists(dest):
        return None
    target = link_target(dest)
    name = drive_of(target)
    if name and not (is_mounted or mounted)(name):
        return False, ("The drive with the game files (%s) isn't connected: the copy is in %s, where %s leads. "
                       "Connect the drive and try again." % (name, target, dest))
    return True, ("The game files on the other drive are gone: %s, where %s leads, is not there. Run the "
                  "installer again for a new copy." % (target, dest))


def _usable(base, probe_src, clone, need_clone):
    """Whether a copy can go below BASE (made if needed): writable, and (NEED_CLONE) a clone of PROBE_SRC works
    there. Folders made for the test are removed again when it fails."""
    made = []
    try:
        d = base
        while not os.path.isdir(d):
            made.append(d)
            d = os.path.dirname(d)
        for m in reversed(made):
            os.mkdir(m)
        probe = os.path.join(base, ".probe" + TMP_SUFFIX)
        if os.path.lexists(probe):
            os.unlink(probe)
        if need_clone:
            try:
                ok = clone(os.path.realpath(probe_src), probe)
            except OSError:
                ok = False
        else:
            with open(probe, "w"):
                pass
            ok = True
        if os.path.lexists(probe):
            os.unlink(probe)
    except OSError:
        ok = False
    if not ok:
        for m in made:
            try:
                os.rmdir(m)
            except OSError:
                break
    return ok


def _free_name(target, dest):
    """TARGET, or "TARGET 2", ...: the first that is empty, or a copy whose link is DEST or leads elsewhere now."""
    for i in range(1, 100):
        t = target if i == 1 else "%s %d" % (target, i)
        if not os.path.lexists(t):
            return t
        if os.path.islink(t) or not os.path.isdir(t):
            continue
        if not os.listdir(t):
            return t
        link = (read_json(os.path.join(t, META, LINK_INFO)) or {}).get("link")
        if link and (os.path.abspath(link) == dest or not os.path.islink(link)
                     or os.path.realpath(link) != os.path.realpath(t)):
            return t  # an earlier copy of this install, or one whose install is gone: refreshed into place
    raise Failure("no free folder name for the copy next to %s" % target)


def _drive_target(edition, src, dest, clone, device, say):
    """The folder for the copy on the drive of the game folder SRC, or None: the game is on DEST's volume (or the
    startup disk's), or no place there takes clones (said)."""
    probe_src = os.path.join(src, MARKERS[edition][0])
    src_dev = device(src)
    here = {device(existing(os.path.dirname(dest)))}
    for p in (os.path.expanduser("~"), "/"):
        try:
            here.add(device(p))
        except OSError:
            pass
    if src_dev in here:
        return None  # the game is on DEST's volume (or the startup disk's): the copy is at DEST
    top = mount_point(src, device)
    rel = os.path.relpath(os.path.dirname(src), top)
    parts = [] if rel == os.curdir else rel.split(os.sep)
    candidates = [top]
    for part in parts:
        if part.lower() == "steamapps":
            break
        candidates.append(os.path.join(candidates[-1], part))
    for c in candidates:
        base = os.path.join(c, FOLDER)
        t = os.path.join(base, edition_path(edition))
        if os.access(c, os.W_OK) and _usable(os.path.dirname(t), probe_src, clone, True):
            return t
    say("The game is on another drive (%s), but RS Re-ARMed's copy cannot be clones there (not APFS, or "
        "no folder there is writable): a real copy in %s." % (os.path.basename(top) or top, dest))
    return None


# A copy that is there moves only when asked: RS_GAME_COPY_DIR=home brings a copy on another drive to DEST as a
# real copy (the game then starts without that drive), RS_GAME_COPY_DIR=drive takes a copy at DEST to the game's
# drive as clones (the installer's "Game files" choice sets one or the other). Everything of the copy moves,
# also what is not from the game folder (its notes, the title logo, the Mac edition's program): made
# whole in "<new place>.moving" first, then swapped in, and the old copy is removed last. A stopped move leaves
# the old copy in use.

def _move(old, new, clone, device, free, progress):
    """Everything of the copy OLD, made in NEW + MOVING. A file the game folder still has as the copy recorded it
    is a clone of that one where the volume clones, else it is copied from OLD. Returns the bytes copied."""
    part = new + MOVING
    if os.path.lexists(part):
        shutil.rmtree(part)  # a stopped move's
    files = (read_json(os.path.join(old, META, MANIFEST)) or {}).get("files")
    files = files if isinstance(files, dict) else {}
    there = device(existing(os.path.dirname(part)))
    dirs, todo, need = [], [], 0
    for d, subdirs, names in os.walk(old):
        rel_d = os.path.relpath(d, old)
        for n in list(subdirs):
            if os.path.islink(os.path.join(d, n)):
                subdirs.remove(n)
                names.append(n)
            else:
                dirs.append(os.path.normpath(os.path.join(rel_d, n)))
        for n in names:
            rel, p = os.path.normpath(os.path.join(rel_d, n)), os.path.join(d, n)
            st = os.lstat(p)
            origin = None
            if stat.S_ISREG(st.st_mode):
                e = files.get(rel)
                src = e.get("src") if isinstance(e, dict) else None
                try:
                    s = os.stat(src) if src else None
                    if s and s.st_size == e.get("size") == st.st_size and s.st_mtime_ns == e.get("mtime_ns"):
                        origin = os.path.realpath(src)
                except OSError:
                    pass
                if device(origin or p) != there:
                    need += st.st_size
            elif not stat.S_ISLNK(st.st_mode):
                continue
            todo.append((rel, p, st, origin))
    if need and free(part) < need + MARGIN:
        raise Failure("not enough free space to move the game files: %s are copied to %s, which has %s free "
                      "(about %s needed). The copy stays where it is." % (
                          human(need), os.path.dirname(part), human(free(part)), human(need + MARGIN)))
    label = "Moving the game files"
    done, shown, copied = [0], [0.0], [0]

    def on_bytes(n):
        done[0] += n
        copied[0] += n
        now = time.monotonic()
        if now - shown[0] >= 0.2:
            shown[0] = now
            progress((done[0] / need if need else 1.0) * 0.99, "%s… %s of %s" % (label, human(done[0]), human(need)))

    os.makedirs(part)
    for rel in dirs:
        os.makedirs(os.path.join(part, rel), exist_ok=True)
    for rel, p, st, origin in todo:
        dst = os.path.join(part, rel)
        if stat.S_ISLNK(st.st_mode):
            t = os.readlink(p)
            if t == old or t.startswith(old + os.sep):
                t = new + t[len(old):]
            os.symlink(t, dst)
            continue
        cloned = False
        for s in ([origin] if origin else []) + [p]:
            try:
                cloned = clone(s, dst)
            except OSError:
                cloned = False
            if cloned:
                break
            if os.path.lexists(dst):
                os.unlink(dst)
        if not cloned:
            copy_data(p, dst, on_bytes)
            shutil.copymode(p, dst)
        os.utime(dst, ns=(st.st_atime_ns, st.st_mtime_ns))
    return copied[0]


def _discard(old, dest, say):
    """The copy OLD that DEST led to, removed with the RS Re-ARMed folders it leaves empty; left when another
    install's link is recorded in it."""
    link = (read_json(os.path.join(old, META, LINK_INFO)) or {}).get("link")
    if link and os.path.abspath(link) != dest:
        say("The old copy in %s is left there (another install's)." % old)
        return
    shutil.rmtree(old)
    d = os.path.dirname(old)
    while os.path.basename(d) in (FOLDER, os.path.dirname(edition_path("learn-play"))):
        try:
            os.rmdir(d)
        except OSError:
            break
        d = os.path.dirname(d)


def _move_home(dest, clone, device, free, progress):
    """The copy DEST links to, as a real folder at DEST."""
    old = os.path.realpath(dest)
    n = _move(old, dest, clone, device, free, progress)
    os.unlink(dest)
    os.rename(dest + MOVING, dest)
    info = os.path.join(dest, META, LINK_INFO)
    if os.path.exists(info):
        os.unlink(info)
    _discard(old, dest, progress.say)
    progress.say("The game files are now in %s (%s copied from %s): the game starts without that drive." % (
        dest, human(n), old))


def _move_to_drive(dest, target, clone, device, free, progress):
    """The copy at DEST, in TARGET on the game's drive, with DEST a link to it."""
    if os.path.isdir(target):
        shutil.rmtree(target)  # empty, or an earlier copy of this install that nothing leads to (_free_name)
    n = _move(dest, target, clone, device, free, progress)
    os.rename(target + MOVING, target)
    write_json(os.path.join(target, META, LINK_INFO), {"format": FORMAT, "link": dest})
    os.rename(dest, dest + MOVED)
    os.symlink(target, dest)
    shutil.rmtree(dest + MOVED)
    progress.say("The game files are now in %s, on the drive with the game%s, and %s leads there: the game needs "
                 "that drive connected." % (target, ", %s copied" % human(n) if n else " as clones", dest))


def place(edition, source, dest, clone=None, device=_device, say=None, env=None, progress=None, free=free_bytes):
    """Where the copy of SOURCE for DEST goes (see above): makes DEST a link to it and returns it, or returns
    None (the copy is at DEST: one is there already, or the game is on DEST's volume, or no clones there)."""
    clone = clone or clonefile
    env = os.environ if env is None else env
    say = say or (lambda m: None)
    if progress is None:
        progress = Progress()
        progress.say = say
    dest = os.path.abspath(dest)
    override = env.get(COPY_DIR_ENV, "").strip()
    choice = override.lower() if override.lower() in (CHOICE_HOME, CHOICE_DRIVE) else ""
    src = os.path.realpath(source)
    if os.path.isdir(dest + MOVED):  # a move to the drive stopped at its last step
        if os.path.lexists(dest):
            shutil.rmtree(dest + MOVED)
        else:
            os.rename(dest + MOVED, dest)
    if os.path.islink(dest):
        if os.path.exists(dest):
            if choice == CHOICE_HOME:
                _move_home(dest, clone, device, free, progress)
            return None  # a copy on another drive already: it stays there, unless asked home
        connected, why = missing_copy(dest)
        if not connected:
            raise Failure(why)
        os.unlink(dest)  # the copy it led to is gone: a new one
    elif os.path.lexists(dest) and not (os.path.isdir(dest) and not os.listdir(dest)):
        # a copy at DEST already (or something the copy's checks refuse): it stays there, unless asked away
        if choice != CHOICE_DRIVE or not os.path.isfile(os.path.join(dest, META, SOURCE_INFO)):
            return None
        target = _drive_target(edition, src, dest, clone, device, say)
        if not target:
            return None
        target = _free_name(target, dest)
        _move_to_drive(dest, target, clone, device, free, progress)
        return target
    if choice == CHOICE_HOME:
        return None
    if override and not choice:
        base = os.path.abspath(os.path.expanduser(override))
        target = os.path.join(base, edition_path(edition))
        if os.path.realpath(target) == os.path.realpath(dest):
            return None
        if not _usable(os.path.dirname(target), os.path.join(src, MARKERS[edition][0]), clone, False):
            raise Failure("%s=%s: cannot make a folder there" % (COPY_DIR_ENV, override))
    else:
        target = _drive_target(edition, src, dest, clone, device, say)
        if not target:
            return None
    target = _free_name(target, dest)
    os.makedirs(os.path.join(target, META), exist_ok=True)
    write_json(os.path.join(target, META, LINK_INFO), {"format": FORMAT, "link": dest})
    if os.path.isdir(dest) and not os.path.islink(dest):
        os.rmdir(dest)
    os.makedirs(os.path.dirname(dest), exist_ok=True)
    os.symlink(target, dest)
    say("The game files go to %s (%s), and %s leads there." % (
        target, "%s=%s" % (COPY_DIR_ENV, override) if override and not choice
        else "on the drive with the game, %s, as clones" % (drive_of(target) or mount_point(target, device)), dest))
    return target


# --- the copy ----------------------------------------------------------------------------------------------

def read_json(path):
    try:
        with open(path) as f:
            return json.load(f)
    except (OSError, ValueError):
        return None


def write_json(path, data):
    tmp = path + TMP_SUFFIX
    with open(tmp, "w") as f:
        json.dump(data, f, indent=1, sort_keys=True)
        f.write("\n")
    os.replace(tmp, path)


def check(dest):
    """(complete, message)."""
    missing = missing_copy(dest)
    if missing:
        return False, missing[1]
    m = read_json(os.path.join(dest, META, MANIFEST))
    if m and m.get("format") == FORMAT and isinstance(m.get("files"), dict):
        return True, "The game files in %s are complete (%d files, %s, copied %s)." % (
            dest, len(m["files"]), human(m.get("bytes", 0)), m.get("made", "?"))
    if os.path.isfile(os.path.join(dest, META, SOURCE_INFO)):
        return False, ("The game files in %s are incomplete (a copy or refresh was stopped): refresh them "
                       "(the installer's Refresh Game Files)." % dest)
    return False, "There is no copy of the game files in %s: run the installer." % dest


class Copier:
    def __init__(self, edition, source, dest, dlc_from=(), steam=None, progress=None, is_running=running,
                 clone=clonefile, free=free_bytes, placement=place, source_away=False):
        self.edition = edition
        self.source_away = source_away  # the game folder is gone: the copy keeps its files, only the DLC moves
        self.source = os.path.abspath(source)
        self.dest = os.path.abspath(dest)
        self.dlc_from = [os.path.abspath(d) for d in dlc_from]
        self.steam = steam or default_steam()
        self.progress = progress or Progress()
        self.is_running = is_running
        self.clone = clone
        self.free = free
        self.stats = {"same": 0, "cloned": 0, "copied": 0, "copied_bytes": 0, "removed": 0, "touched": 0}
        self._clone_ok = {}  # source volume -> clones work into DEST
        self.placement = placement  # where a new copy goes (place), or None: at DEST

    # Every path written or removed is in DEST, below a real (not linked) folder of DEST.
    def _inside(self, path):
        parent = os.path.realpath(os.path.dirname(path))
        root = os.path.realpath(self.dest)
        return parent == root or parent.startswith(root + os.sep)

    def _guard(self, path):
        if not self._inside(path):
            raise Failure("refusing to write outside %s: %s" % (self.dest, path))

    def _make_dirs(self, rel_dir):
        """DEST/rel_dir as real folders: a link or a file in the way is removed (never followed)."""
        cur = self.dest
        for part in [p for p in rel_dir.split(os.sep) if p]:
            cur = os.path.join(cur, part)
            if os.path.islink(cur):
                self._guard(cur)
                os.unlink(cur)
            elif os.path.exists(cur) and not os.path.isdir(cur):
                self._guard(cur)
                os.unlink(cur)
            if not os.path.isdir(cur):
                os.mkdir(cur)

    def _check_paths(self):
        if self.source_away:
            return
        if not os.path.isdir(self.source):
            raise Failure("the game folder %s is not there" % self.source)
        missing = [m for m in MARKERS[self.edition] if not os.path.isfile(os.path.join(self.source, m))]
        if missing:
            raise Failure("%s is not the game's folder (no %s); the copy in %s is left as it is"
                          % (self.source, ", ".join(missing), self.dest))
        s, d = os.path.realpath(self.source), os.path.realpath(self.dest)
        for a, b in ((s, d), (d, s)):
            if a == b or b.startswith(a + os.sep):
                raise Failure("the copy (%s) and the game folder (%s) overlap" % (self.dest, self.source))

    def _old_links(self):
        """Top-level links of DEST that lead out of it: the launcher's mirror of the game folder in earlier versions."""
        n = 0
        root = os.path.realpath(self.dest)
        for name in os.listdir(self.dest):
            p = os.path.join(self.dest, name)
            if os.path.islink(p):
                t = os.path.realpath(p)
                if not (t == root or t.startswith(root + os.sep)):
                    os.unlink(p)
                    n += 1
        return n

    def _leftovers(self):
        for d, subdirs, files in os.walk(self.dest):
            subdirs[:] = [s for s in subdirs if not os.path.islink(os.path.join(d, s))]
            for f in files:
                if f.endswith(TMP_SUFFIX):
                    os.unlink(os.path.join(d, f))

    def _clone_works(self, src_st, src):
        """Whether files of src's volume clone into DEST (tried once per volume, with a real file)."""
        key = src_st.st_dev
        if key not in self._clone_ok:
            ok = False
            if key == os.stat(self.dest).st_dev:
                probe = os.path.join(self.dest, META, "probe" + TMP_SUFFIX)
                try:
                    if os.path.lexists(probe):
                        os.unlink(probe)
                    ok = self.clone(os.path.realpath(src), probe)
                except OSError:
                    ok = False
                finally:
                    if os.path.lexists(probe):
                        os.unlink(probe)
            self._clone_ok[key] = ok
        return self._clone_ok[key]

    def _same(self, src_st, dst):
        """Whether DEST's file is the source's: size and time, else (only the time differs) the contents."""
        try:
            st = os.lstat(dst)
        except OSError:
            return False
        if not os.path.isfile(dst) or os.path.islink(dst) or st.st_size != src_st.st_size:
            return False
        if st.st_mtime_ns == src_st.st_mtime_ns:
            return True
        return None  # same size, another time: compare the contents

    @staticmethod
    def _as_recorded(entry, src_st, dst):
        """Whether the source and DEST's file are both as the last copy's manifest ENTRY has them: the source's
        size and time, and the time the copy read back as then (copy_mtime_ns)."""
        if not isinstance(entry, dict) or entry.get("copy_mtime_ns") is None:
            return False
        return (entry.get("size") == src_st.st_size and entry.get("mtime_ns") == src_st.st_mtime_ns
                and entry["copy_mtime_ns"] == os.lstat(dst).st_mtime_ns)

    def _stale_logo(self, files, pending):
        """A title logo made in the copy goes before a new static.psarc comes: it was made from the old one. Before
        the copying, so a stopped run does not leave it behind."""
        p = os.path.join(self.dest, STATIC_REARMED)
        if any(rel == STATIC_ORIGINAL for rel, _, _ in pending) and STATIC_REARMED not in files and os.path.isfile(p):
            os.unlink(p)
            self.progress.say("The title logo was made from the old static.psarc: removed, the next build makes it "
                              "again.")

    def _static_link(self):
        """static.psarc as the game opens it: a link to the original, unless it leads to a copy already. The launcher
        and the Learn & Play app choose at each start; this keeps the copy whole until then."""
        link = os.path.join(self.dest, STATIC)
        if not os.path.exists(link):
            if os.path.lexists(link):
                os.unlink(link)
            os.symlink(STATIC_ORIGINAL, link)

    def _dlc_away(self, meta, previous, files):
        """A DLC folder that is not there now (a drive not connected, the Mac game uninstalled, a link to
        nowhere) is not a folder without songs: the copy keeps what came from it, and remembers the folder.
        The manifest entries of those files; a copy without a manifest (a stopped run) keeps all of dlc/."""
        info = read_json(os.path.join(meta, SOURCE_INFO)) or {}
        recorded = [d for d in info.get("dlc_from") or [] if d not in self.dlc_from]
        dlc = os.path.join(self.source, "dlc")
        away = [d for d in recorded + list(self.dlc_from) + [dlc] if not os.path.isdir(d)]
        if self.source_away:
            away.append(self.source)
        if not away:
            return {}
        self.dlc_from = list(self.dlc_from) + [d for d in recorded if d in away]
        old = (read_json(os.path.join(meta, MANIFEST)) or {}).get("files") or {}
        kept = {}
        for rel in previous:
            p = os.path.join(self.dest, rel)
            top = rel.split(os.sep)[0]
            if top == META or (top != "dlc" and not self.source_away) or rel in files or not os.path.isfile(p):
                continue
            src = old.get(rel, {}).get("src")
            if src is None or any(src.startswith(d + os.sep) for d in away):
                st = os.stat(p)
                kept[rel] = {"size": st.st_size, "mtime_ns": st.st_mtime_ns, "src": src or p}
        if kept:
            self.progress.say("%s isn't there now: its %d files stay in the copy." % (", ".join(away), len(kept)))
        return kept

    def _archive_key(self, meta):
        """Whether the copy's archives can be read: their tables are encrypted with a key that is in the game's
        own code (psarc_key.py), not in RS Re-ARMed. The Mac game's executable shows it; the copy then keeps it
        in .rearmed, so a refresh needs no game folder. Learn & Play's exe shows it only unpacked, and the code
        image is made after the first copy: until then the copy is marked, the parts that read archives (the
        DLC's AppIDs, the Rocksmith 1 songs, the menu overlay) wait, and key_from_image finishes them. A Mac
        game folder without the game's program waits the same way, for a refresh once the app is installed."""
        if cache_overlay is None:
            return True
        pk = cache_overlay.psarc_key
        mark = os.path.join(meta, KEY_PENDING)
        if pk.have_key(self.dest, self.source):
            pk.note(self.dest, pk.toc_key())
            if os.path.exists(mark):
                os.unlink(mark)
            return True
        with open(mark, "w") as f:
            f.write("the archives are read once the game's code is at hand (gamefiles.py)\n")
        self.progress.say("Your DLC's list and the menu entries are made when the game's code image is there."
                          if self.edition == "learn-play" else
                          "Rocksmith2014.app is not in %s: your DLC's list and the menu entries wait for it."
                          % self.source)
        return False

    def _overlay(self):
        """The Direct Connect overlay, Update/patcharchives.psarc, made from the copy's cache.psarc
        (cache_overlay.py, next to this file). Not made (a cache it cannot read): the game runs without it."""
        if cache_overlay is None:
            return
        try:
            names = cache_overlay.make(self.dest)
        except Exception as e:  # a damaged or hand-made cache.psarc can fail in many ways: never the copy's end
            cache_overlay.remove(self.dest)
            self.progress.say("No Direct Connect menu entries: %s." % e)
            return
        if names:
            self.progress.say("Direct Connect: %d menu files in %s." % (
                len(names), os.path.join(cache_overlay.PATCH_DIR, cache_overlay.PATCH_NAME)))

    def run(self):
        missing = missing_copy(self.dest)
        if missing and not (missing[0] and self.placement):  # gone from a connected drive: a new copy (place)
            raise Failure(missing[1])
        self._check_paths()
        if self.is_running(self.edition):
            raise Failure("quit the game first (%s is running)" % PROCESS[self.edition])
        if self.placement and self.placement(self.edition, self.source, self.dest, clone=self.clone,
                                             say=self.progress.say, progress=self.progress, free=self.free):
            self._check_paths()  # the place chosen must not be in the game folder either
        missing = missing_copy(self.dest)
        if missing:
            raise Failure(missing[1])
        # (a game folder that Steam's console downloaded is not the one an appmanifest is about)
        if self.edition == "mac" and not depot_source(self.source) \
                and steam_is_updating(find_appmanifest(self.source, self.steam)):
            raise Failure("Steam is updating or downloading the game: let it finish, then run this again")
        os.makedirs(self.dest, exist_ok=True)
        meta = os.path.join(self.dest, META)
        if os.path.islink(meta):
            os.unlink(meta)
        os.makedirs(meta, exist_ok=True)
        readable = self._archive_key(meta)
        unlinked = self._old_links()
        if unlinked:
            self.progress.say("Removed %d links to the game folder (the earlier install's mirror)." % unlinked)
        self._leftovers()

        # The earlier copy's set: what may be removed. A refresh keeps it as manifest.previous.json until it
        # is done, so a stopped refresh still knows it.
        previous = set()
        for name in (MANIFEST, PREVIOUS):
            m = read_json(os.path.join(meta, name))
            if m and isinstance(m.get("files"), dict):
                previous |= set(m["files"])
        try:  # the same as the rest of the copy, though no manifest has them yet
            with open(os.path.join(meta, ENUMERATED)) as f:
                previous |= set(f.read().split("\n")) - {""}
        except OSError:
            pass
        write_json(os.path.join(meta, PREVIOUS), {"format": FORMAT, "files": {k: {} for k in sorted(previous)}})

        self.progress(0, "Checking the game files")
        files = plan(self.edition, self.source, self.dlc_from)
        # What the last copy wrote down: a volume that keeps coarser file times than the source's gives each
        # copy another time than its source, and without this every refresh would read every file again.
        recorded = (read_json(os.path.join(meta, MANIFEST)) or {}).get("files")
        recorded = recorded if isinstance(recorded, dict) else {}
        entries, pending = self._dlc_away(meta, previous, files), []
        for rel, src in sorted(files.items()):
            st = os.stat(src)
            dst = os.path.join(self.dest, rel)
            same = self._same(st, dst)
            if same is None and self._as_recorded(recorded.get(rel), st, dst):
                same = True
            if same is None:
                if sha256(src) == sha256(dst):
                    os.utime(dst, ns=(st.st_atime_ns, st.st_mtime_ns))
                    self.stats["touched"] += 1
                    same = True
            if same:
                self.stats["same"] += 1
            else:
                pending.append((rel, src, st))
            entries[rel] = {"size": st.st_size, "mtime_ns": st.st_mtime_ns, "src": src}

        self._stale_logo(files, pending)

        # Room: nothing for clones, the bytes for a real copy (both copies exist until the rename).
        total = sum(st.st_size for _, _, st in pending)
        need_copy = sum(st.st_size for _, src, st in pending if not self._clone_works(st, src))
        if need_copy:
            free = self.free(self.dest)
            if free < need_copy + MARGIN:
                raise Failure("not enough free space for the game files: %s are copied to %s, which has %s free "
                              "(about %s needed). Free some space and run this again: what is copied is kept."
                              % (human(need_copy), self.dest, human(free), human(need_copy + MARGIN)))
        # Nothing has changed up to here: a failed check leaves a copy that still works.
        if os.path.exists(os.path.join(meta, MANIFEST)):
            os.unlink(os.path.join(meta, MANIFEST))  # incomplete from here until the new one is written
        write_json(os.path.join(meta, SOURCE_INFO), {"format": FORMAT, "edition": self.edition,
                                                     "source": self.source, "dlc_from": self.dlc_from,
                                                     "steam": self.steam})
        label = "Copying the game files"
        done = [0]
        shown = [0.0]

        def show():  # at most five times a second
            now = time.monotonic()
            if now - shown[0] >= 0.2:
                shown[0] = now
                frac = done[0] / total if total else 1.0
                self.progress(frac * 0.99, "%s… %s of %s" % (label, human(done[0]), human(total)))

        if pending:
            self.progress.say("%d files to %s (%s)%s." % (
                len(pending), "copy" if need_copy else "clone", human(total),
                ", %s as real copies" % human(need_copy) if need_copy and need_copy != total else ""))
        for rel, src, st in pending:
            dst = os.path.join(self.dest, rel)
            self._make_dirs(os.path.dirname(rel))
            self._guard(dst)
            tmp = dst + TMP_SUFFIX
            if os.path.lexists(tmp):
                os.unlink(tmp)
            real = os.path.realpath(src)
            cloned = False
            if self._clone_works(st, src):
                try:
                    cloned = self.clone(real, tmp)
                except OSError:
                    cloned = False
                if not cloned and os.path.lexists(tmp):
                    os.unlink(tmp)
            if cloned:
                done[0] += st.st_size
                self.stats["cloned"] += 1
            else:
                if not need_copy:
                    # the volume took clones in the check but not this one: there must be room for it
                    if self.free(self.dest) < st.st_size + MARGIN:
                        raise Failure("not enough free space for %s (%s) in %s" % (rel, human(st.st_size), self.dest))

                def on_bytes(n):
                    done[0] += n
                    show()
                copy_data(real, tmp, on_bytes)
                shutil.copymode(real, tmp)
                self.stats["copied"] += 1
                self.stats["copied_bytes"] += st.st_size
            os.utime(tmp, ns=(st.st_atime_ns, st.st_mtime_ns))
            if os.path.isdir(dst) and not os.path.islink(dst):
                raise Failure("%s is a folder in the copy, where the game has a file" % dst)
            os.replace(tmp, dst)
            show()

        # Files of the earlier copy that the game folder no longer has; empty folders they leave.
        keep = {e.lower() for e in entries}  # Foo_m.psarc renamed foo_m.psarc is the same file here
        for rel in sorted(previous - set(entries), reverse=True):
            if rel.lower() in keep:
                continue
            if rel.split(os.sep)[0] == META and rel != os.path.join(META, "Rocksmith.ini"):
                continue
            if rel == STATIC_REARMED:
                continue  # copied from the game folder by earlier versions; it stays until _stale_logo says
            p = os.path.join(self.dest, rel)
            if not self._inside(p) or not os.path.lexists(p) or (os.path.isdir(p) and not os.path.islink(p)):
                continue
            os.unlink(p)
            self.stats["removed"] += 1
            d = os.path.dirname(p)
            while d != self.dest and self._inside(d) and os.path.isdir(d) and not os.listdir(d):
                os.rmdir(d)
                d = os.path.dirname(d)

        appmanifest = find_appmanifest(self.source, self.steam)
        env_path = os.path.join(meta, STEAM_ENV)
        account, how = steam_account(self.edition, self.steam, appmanifest, os.environ.get("RS_STEAM_ID"))
        recorded = recorded_account(env_path)
        if recorded and how not in (HOW_OVERRIDE, HOW_LICENSE):
            account, how = recorded, "kept from the earlier copy"  # a guess never replaces an earlier choice
        apps = dlc_appids(self.dest, entries, readable)
        # What the ticket lists: a Learn & Play buyer has no appmanifest, nor has a Mac game that is not installed
        # in Steam.
        if (self.edition == "learn-play" or not appmanifest) and account:
            apps = set(apps) | set(account_dlc(self.steam, account) or [])
        env = steam_env(appmanifest, account, apps, rs1_owned(self.steam, self.dest, account, self.progress.say))
        if not readable and os.path.exists(env_path):
            pass  # an earlier copy's facts stay until the archives can be read again
        elif env:
            with open(env_path + TMP_SUFFIX, "w") as f:
                f.write("# the game's Steam facts, from Steam's appmanifest when the copy was made (gamefiles.py)\n")
                if account:
                    f.write("# RS_STEAM_ID: %s\n" % how)
                f.write("\n".join(env) + "\n")
            os.replace(env_path + TMP_SUFFIX, env_path)
        elif os.path.exists(env_path):
            os.unlink(env_path)

        self._static_link()
        if readable:
            self._overlay()
        nbytes = sum(e["size"] for e in entries.values())
        for rel, e in entries.items():  # the time each copy has on this volume, where it is not its source's
            try:
                t = os.lstat(os.path.join(self.dest, rel)).st_mtime_ns
            except OSError:
                continue
            if t != e.get("mtime_ns"):
                e["copy_mtime_ns"] = t
        write_json(os.path.join(meta, MANIFEST), {
            "format": FORMAT, "edition": self.edition, "source": self.source, "dlc_from": self.dlc_from,
            "made": time.strftime("%Y-%m-%d %H:%M:%S UTC", time.gmtime()), "bytes": nbytes, "files": entries})
        os.unlink(os.path.join(meta, PREVIOUS))
        if os.path.exists(os.path.join(meta, ENUMERATED)):
            os.unlink(os.path.join(meta, ENUMERATED))
        self.progress(1.0, "%s… %s of %s" % (label, human(total), human(total)))
        s = self.stats
        parts = ["%d unchanged" % s["same"]] if s["same"] else []
        if s["cloned"]:
            parts.append("%d cloned (no extra space)" % s["cloned"])
        if s["copied"]:
            parts.append("%d copied (%s)" % (s["copied"], human(s["copied_bytes"])))
        if s["removed"]:
            parts.append("%d removed" % s["removed"])
        self.progress.say("Game files: %d files, %s, in %s%s." % (
            len(entries), human(nbytes), self.dest, ": " + ", ".join(parts) if parts else ""))
        return entries


# --- Steam, as the installers see it ---------------------------------------------------------------------
#
# Both installers lead the user through Steam with the same steps and the same words, from what Steam shows on
# disk: whether Steam.app is there, whether steam_osx runs, and who is signed in (its connection log). No
# Accessibility, nothing that needs a macOS permission. What the user has to do comes out as Guide messages.

STEAM_BUNDLE_ID = "com.valvesoftware.steam"
STEAM_PROCESS = os.environ.get("LP_STEAM_PROCESS") or "steam_osx"  # (another: a test with a stand-in for Steam)
STEAM_SUFFIX = os.path.join("Library", "Application Support", "Steam")
# "[Logged On, 4, 7] [U:1:123] ...": the connection state and the account of the line
CONNECTION_LINE = re.compile(r"^\[[-\d: ]+\] \[(Logged On|Logged Off|Logging On|Logging Off|Connecting|Connected),"
                             r" \d+, \d+\] \[U:1:(\d+)\]", re.MULTILINE)
GAME_MAC = "Rocksmith 2014 for Mac"
GAME_LP = "Rocksmith 2014 Learn & Play"
# Steam's own words (Menu_ChangeAccount, Menu_GoOnline).
CHANGE_ACCOUNT = "open the Steam menu, choose Change Account…, and sign in"

# code: (title, text). What the user sees in the installer app's window and in a terminal, the same for both
# editions; %(game)s and the like are filled in. The app adds the buttons by code.
GUIDE = {
    "steam-missing": ("Install Steam",
                      "Steam isn't on this Mac yet. Click Get Steam, download it from Steam's website, open the "
                      "download and drag Steam to Applications. Then open it. The installer goes on by itself."),
    "steam-starting": ("Steam is starting",
                       "Steam is opening, and it may update itself first, which can take a few minutes. If it asks "
                       "you to sign in, use the account that owns %(game)s. The installer goes on by itself."),
    "sign-in": ("Sign in to Steam",
                "Steam is open but nobody is signed in. In Steam's window, sign in with the account that owns "
                "%(game)s. If Steam is in Offline Mode, open the Steam menu and choose Go Online…. The installer goes "
                "on by itself when you are signed in."),
    "steam-offline": ("Steam lost its connection",
                      "Steam isn't connected right now. Check that this Mac is on the internet. Steam reconnects by "
                      "itself, and the installer goes on when it has."),
    "wrong-account": ("This Steam account doesn't have the game",
                      "Steam is signed in as %(name)s, and Steam says that account doesn't own %(game)s. In Steam, "
                      + CHANGE_ACCOUNT + " with the account that owns it. Steam restarts, and the installer goes on "
                      "by itself."),
    "wrong-account-mac": ("This Steam account doesn't have the Mac game",
                          "Steam is signed in as %(name)s, and Steam says that account doesn't own %(game)s. If it "
                          "has the Windows edition, click Stop and choose Learn & Play under Game. Otherwise, in "
                          "Steam, " + CHANGE_ACCOUNT + " with the account that owns it. Steam restarts, and the "
                          "installer goes on by itself."),
    "downloading": ("Steam is downloading the game",
                    "Nothing to do here: leave Steam open. Steam's own window doesn't show this download, only the "
                    "progress here does. If it is stopped, the next try continues from where it was."),
    "download-stalled": ("The download has stopped moving",
                         "Nothing has arrived for a while. Check that this Mac is on the internet, and that Steam "
                         "is open and signed in. It goes on by itself when it can, or click Try Again."),
    "download-failed": ("Steam couldn't download the game",
                        "Steam says: %(why)s. The installer tries again by itself; Try Again does it now."),
    "paste": ("One step in Steam, please",
              "Steam didn't start the download by itself, so it needs your help once. Steam's console is open, and "
              "the command is copied: click in the box at the bottom of the console window, press ⌘V, then press "
              "Return. No console? Click Try Again. The command is: %(command)s"),
    "downloading-dlc": ("Steam is downloading your DLC",
                        "Nothing to do here: leave Steam open. Steam's own window doesn't show this download, only "
                        "the progress here does."),
    "installing-game": ("Steam is installing the game",
                        "Steam is installing or updating %(game)s in its library. Nothing to do here: leave Steam "
                        "open. The installer goes on by itself when it is done."),
}


class Guide:
    """What the user has to do now: "guide<TAB>CODE<TAB>TITLE<TAB>TEXT" lines for the installer app (a line
    with the code "-" clears it), the title and the text in a terminal. Says each message once."""

    def __init__(self, lines=False, stream=None):
        self.stream = stream or sys.stdout
        self.lines = lines
        self.now = None

    def __call__(self, code, **values):
        title, text = GUIDE[code]
        text = text % values if "%(" in text else text
        if (code, text) == self.now:
            return
        self.now = (code, text)
        if self.lines:
            self.stream.write("guide\t%s\t%s\t%s\n" % (code, title, text))
        else:
            self.stream.write("\n== %s\n%s\n" % (title, text))
        self.stream.flush()

    def clear(self):
        if self.now is None:
            return
        self.now = None
        if self.lines:
            self.stream.write("guide\t-\n")
            self.stream.flush()


def steam_home(steam):
    """The home folder Steam runs in. A test with a fake HOME still talks to the real Steam."""
    if steam.rstrip(os.sep).endswith(os.sep + STEAM_SUFFIX):
        return steam.rstrip(os.sep)[:-len(STEAM_SUFFIX) - 1]
    return os.path.expanduser("~")


def steam_exe(steam):
    """Steam's launcher, or None when Steam isn't on this Mac. $LP_STEAM_EXE: another one."""
    if os.environ.get("LP_STEAM_EXE"):
        return os.environ["LP_STEAM_EXE"] if os.access(os.environ["LP_STEAM_EXE"], os.X_OK) else None
    tail = os.path.join("Contents", "MacOS", "steam_osx")
    for app in ("/Applications/Steam.app", os.path.join(steam_home(steam), "Applications", "Steam.app")):
        if os.access(os.path.join(app, tail), os.X_OK):
            return os.path.join(app, tail)
    try:
        r = subprocess.run(["/usr/bin/mdfind", "kMDItemCFBundleIdentifier == '%s'" % STEAM_BUNDLE_ID],
                           capture_output=True, text=True, timeout=10)
    except (OSError, subprocess.SubprocessError):
        return None
    for app in r.stdout.splitlines():
        if os.access(os.path.join(app, tail), os.X_OK):
            return os.path.join(app, tail)
    return None


def steam_running():
    try:
        return subprocess.run(["/usr/bin/pgrep", "-x", STEAM_PROCESS], stdout=subprocess.DEVNULL,
                              stderr=subprocess.DEVNULL).returncode == 0
    except OSError:
        return False


def steam_connection(steam):
    """(state, account id) of the last line of Steam's connection log that has them; (None, 0) without one."""
    newest, when = None, -1
    for d in (steam, os.path.join(steam, "Steam.AppBundle", "Steam", "Contents", "MacOS")):
        p = os.path.join(d, "logs", "connection_log.txt")
        try:
            t = os.path.getmtime(p)
        except OSError:
            continue
        if t > when:
            newest, when = p, t
    if not newest:
        return None, 0
    with open(newest, "rb") as f:
        f.seek(0, os.SEEK_END)
        f.seek(max(f.tell() - (256 << 10), 0))
        text = f.read().decode("utf-8", "replace")
    found = None
    for found in CONNECTION_LINE.finditer(text):
        pass
    return (found.group(1), int(found.group(2))) if found else (None, 0)


def steam_account_name(steam, account):
    """The name of a signed-in account as Steam's sign-in shows it (loginusers.vdf), or a plain description."""
    try:
        with open(os.path.join(steam, "config", "loginusers.vdf"), errors="replace") as f:
            block = _vdf_block(f.read(), str(STEAMID64_BASE + account))
    except OSError:
        block = ""
    m = re.search(r'"AccountName"\s+"([^"]*)"', block)
    return m.group(1) if m else "this account"


# --- the first Rocksmith's DLC the account owns --------------------------------------------------------------
#
# The RS1 compatibility pack comes with the game and carries the base game's AppID, but the game lists one of its
# songs only when Steam says the account owns the DLC of the first Rocksmith the song came from: DLCRS1Key's WIN32
# in the song's manifest (the game asks for each with BIsSubscribedApp). Steam installs nothing for those, and its
# files keep no ticket for an app that never ran, so only the Steam client knows. So a copy or refresh asks it, once,
# when the edition's account is the one logged in: the answers go into steam.env's RS_STEAM_OWNED, for the offline
# Steam layer, and .rearmed/rs1-owned.json keeps them for a refresh with Steam closed or on another account. The
# asking is a process of its own, gone before the game starts: the game itself never talks to Steam.

RS1_PACK = "rs1compatibilitydlc_"
RS1_OWNED = "rs1-owned.json"
ASK_TIMEOUT = 30
ASK_MARK = "owned-apps:"

# argv: Valve's library; the AppIDs, as JSON, on stdin. Prints ASK_MARK and {"account": ..., "apps": [...]} as JSON.
ASK_STEAM = r'''
import ctypes, json, sys
ids = json.load(sys.stdin)
L = ctypes.CDLL(sys.argv[1])
def newest(prefix, versions):
    for v in versions:
        if hasattr(L, prefix + v):
            f = getattr(L, prefix + v)
            f.restype = ctypes.c_void_p
            return f()
    sys.exit("no " + prefix)
L.SteamAPI_InitSafe.restype = ctypes.c_bool
if not L.SteamAPI_InitSafe():
    sys.exit("SteamAPI_InitSafe failed")
apps = newest("SteamAPI_SteamApps_v", ("009", "008"))
user = newest("SteamAPI_SteamUser_v", ("023", "021"))
L.SteamAPI_ISteamUser_GetSteamID.argtypes = [ctypes.c_void_p]
L.SteamAPI_ISteamUser_GetSteamID.restype = ctypes.c_uint64
L.SteamAPI_ISteamApps_BIsSubscribedApp.argtypes = [ctypes.c_void_p, ctypes.c_uint32]
L.SteamAPI_ISteamApps_BIsSubscribedApp.restype = ctypes.c_bool
out = {"account": L.SteamAPI_ISteamUser_GetSteamID(user) & 0xFFFFFFFF,
       "apps": [i for i in ids if L.SteamAPI_ISteamApps_BIsSubscribedApp(apps, i)]}
L.SteamAPI_Shutdown()
print("\n%s%s" % (sys.argv[2], json.dumps(out)), flush=True)
'''


def rs1_dlc_apps(dest):
    """The AppIDs of the first Rocksmith's DLC that the songs of the copy's RS1 compatibility pack name, from the
    pack's song index; none without a readable pack."""
    if cache_overlay is None:
        return set()
    apps = set()
    try:
        packs = [n for n in os.listdir(os.path.join(dest, "dlc")) if n.lower().startswith(RS1_PACK)]
    except OSError:
        return apps
    for pack in packs:
        try:
            p = cache_overlay.Psarc(os.path.join(dest, "dlc", pack))
        except Exception:  # not a psarc, damaged, or no openssl
            continue
        try:
            for name in (n for n in p.names if n.lower().endswith(".hsan")):
                for entry in json.loads(p.read(name)).get("Entries", {}).values():
                    for key in entry.get("Attributes", {}).get("DLCRS1Key") or []:
                        if str(key.get("WIN32", "")).isdigit():
                            apps.add(int(key["WIN32"]))
        except Exception:
            pass
        finally:
            p.close()
    return apps


def steam_api_library(steam):
    """Valve's libsteam_api, which the Steam client ships, or None."""
    p = os.environ.get("RS_STEAM_REAL_LIB") or os.path.join(
        steam, "Steam.AppBundle", "Steam", "Contents", "MacOS", "Frameworks", "Steam Helper.app", "Contents", "MacOS",
        "libsteam_api.dylib")
    return p if os.path.isfile(p) else None


def ask_steam(steam, apps):
    """{"account": id, "apps": [...]}: which of APPS the account logged in to the running Steam owns; None when
    Steam can't say."""
    lib = steam_api_library(steam)
    if not lib:
        return None
    try:
        r = subprocess.run([sys.executable, "-I", "-c", ASK_STEAM, lib, ASK_MARK], input=json.dumps(sorted(apps)),
                           capture_output=True, text=True, timeout=ASK_TIMEOUT, cwd=steam,
                           env=dict(os.environ, SteamAppId=str(APP_ID), SteamGameId=str(APP_ID)))
    except (OSError, subprocess.SubprocessError):
        return None
    for line in r.stdout.splitlines():
        if line.startswith(ASK_MARK):
            try:
                out = json.loads(line[len(ASK_MARK):])
                return {"account": int(out["account"]), "apps": [int(a) for a in out["apps"]]}
            except (ValueError, KeyError, TypeError):
                return None
    return None


def rs1_owned(steam, dest, account, say=lambda text: None):
    """The first Rocksmith's DLC ACCOUNT owns, of the DLC the copy's RS1 compatibility pack names: asked of Steam
    when that account is logged in, else what an earlier copy found. Sorted; empty when nothing is known."""
    apps = rs1_dlc_apps(dest)
    if not apps or not account:
        return []
    path = os.path.join(dest, META, RS1_OWNED)
    known = read_json(path)
    known = set(known.get("apps", [])) if isinstance(known, dict) and known.get("account") == account else None
    state = steam_state(steam)
    if state.online and state.account == account:
        answer = ask_steam(steam, apps)
        if answer and answer["account"] == account:
            write_json(path, {"format": FORMAT, "account": account, "apps": sorted(answer["apps"])})
            say("Rocksmith 1 DLC: this account owns %d of the %d the game knows." % (len(answer["apps"]), len(apps)))
            return sorted(answer["apps"])
    if known is None:
        say("Rocksmith 1 DLC: not checked. To play the ones %s owns, have that account logged in to Steam and "
            "refresh the game files." % steam_account_name(steam, account))
        return []
    return sorted(known & apps)


class SteamState:
    def __init__(self, installed, running, online, account=0, name=""):
        self.installed, self.running, self.online, self.account, self.name = installed, running, online, account, name


def steam_state(steam):
    state, account = steam_connection(steam)
    running = steam_running()
    online = running and state == "Logged On" and account != 0
    return SteamState(running or steam_exe(steam) is not None, running, online, account if online else 0,
                      steam_account_name(steam, account) if online else "")


def steam_open(url=None):
    """Steam, opened or brought to the front, at url if one is given."""
    subprocess.run(["/usr/bin/open"] + ([url] if url else ["-b", STEAM_BUNDLE_ID]),
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def steam_command(steam, *words):
    """A console command for the Steam that is running: its launcher hands on a +command, and Steam runs it as
    its console would (the line "ExecCommandLine" in console_log.txt). The launcher ends at once."""
    exe = steam_exe(steam)
    if not exe:
        return False
    subprocess.Popen([exe, "+" + words[0]] + list(words[1:]), env=dict(os.environ, HOME=steam_home(steam)),
                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, stdin=subprocess.DEVNULL)
    return True




class SteamGate:
    """Steam ready for a download: installed, open and signed in. ready() is called in a loop; until it is true,
    guide says what is missing, and Steam is opened when it isn't."""

    REOPEN = 60.0    # seconds before opening a Steam that doesn't come up again
    STARTING = 45.0  # seconds a just-opened Steam counts as starting, before "sign in"

    def __init__(self, steam, game, guide, state=steam_state, opener=steam_open, clock=time.monotonic):
        self.steam, self.game, self.guide = steam, game, guide
        self.state, self.opener, self.clock = state, opener, clock
        self.opened_at = None
        self.was_online = False
        self.st = None

    def ready(self):
        st = self.st = self.state(self.steam)
        now = self.clock()
        if not st.installed:
            self.guide("steam-missing")
            return False
        if not st.running:
            if self.opened_at is None or now - self.opened_at >= self.REOPEN:
                self.opener()
                self.opened_at = now
            self.guide("steam-starting", game=self.game)
            return False
        if st.online:
            self.was_online = True
            return True
        if self.opened_at is not None and now - self.opened_at < self.STARTING:
            self.guide("steam-starting", game=self.game)
        elif self.was_online:
            self.guide("steam-offline")
        else:
            self.guide("sign-in", game=self.game)
        return False


# --- the DLC of a game from Steam's download ---------------------------------------------------------------
#
# Learn & Play's, and the Mac game's when it is not installed in Steam (Steam installs the DLC of a game in its
# library by itself). Each DLC is a Steam depot of its own whose id is the DLC's AppID, with both kinds of archives
# (_m and _p), and the games' depots have none (the Mac depot has the Rocksmith 1 compatibility pack). download_depot takes it under the game's app, or under its own AppID for the DLC whose
# depot is defined there ("hasdepotsindlc"); the other form answers "Invalid default manifest". Some DLC have
# no depot at all (260460, the game key): both forms answer that. It lands in content/app_<app>/depot_<id>/dlc.

BUNDLE_MACOS = os.path.join("Steam.AppBundle", "Steam", "Contents", "MacOS")
DLC_LABEL = "Downloading your DLC"
DLC_NO_DEPOT = re.compile(r"Invalid default manifest|Missing configuration|missing app info", re.I)
DLC_NOT_OWNED = re.compile(r"missing license|no subscription", re.I)


def steam_content_dirs(steam):
    return [os.path.join(steam, BUNDLE_MACOS, "steamapps", "content"), os.path.join(steam, "steamapps", "content")]


def dlc_depot_folders(steam):
    """The dlc folders of the depots Steam downloaded, DLC or not (the Windows game's is empty)."""
    out = []
    for content in steam_content_dirs(steam):
        for app in sorted(glob_dirs(content, "app_")):
            for depot in sorted(glob_dirs(app, "depot_")):
                if os.path.isdir(os.path.join(depot, "dlc")) and os.listdir(os.path.join(depot, "dlc")):
                    out.append(os.path.join(depot, "dlc"))
    return out


def glob_dirs(top, prefix):
    try:
        return [os.path.join(top, n) for n in os.listdir(top) if n.startswith(prefix)]
    except OSError:
        return []


class ConsoleTail:
    """New lines of Steam's console_log.txt, from now on."""

    def __init__(self, steam):
        self.paths = [os.path.join(d, "logs", "console_log.txt") for d in (steam, os.path.join(steam, BUNDLE_MACOS))]
        self.pos = {p: self._size(p) for p in self.paths}

    @staticmethod
    def _size(p):
        try:
            return os.path.getsize(p)
        except OSError:
            return 0

    def poll(self):
        lines = []
        for p in self.paths:
            size = self._size(p)
            if size < self.pos[p]:  # Steam started again: a new log
                self.pos[p] = 0
            if size == self.pos[p]:
                continue
            with open(p, "rb") as f:
                f.seek(self.pos[p])
                data = f.read(size - self.pos[p])
            end = data.rfind(b"\n") + 1
            self.pos[p] += end
            lines += data[:end].decode("utf-8", "replace").splitlines()
        return lines


class DlcFetch:
    """Each owned DLC in turn: one command at a time, the next when Steam has answered the last (two downloads of
    the same depot fetch everything twice)."""

    ANSWER = 600.0  # seconds Steam may take over one DLC before it counts as failed

    def __init__(self, steam, state, progress, guide, wait=120.0, gate=None, send=None, clock=time.monotonic,
                 sleep=time.sleep, game=None):
        self.steam, self.state, self.progress, self.guide = steam, state, progress, guide
        self.wait, self.clock, self.sleep = wait, clock, sleep
        self.gate = gate or SteamGate(steam, game or GAME_LP, guide, clock=clock)
        self.send = send or (lambda app, depot: steam_command(steam, "download_depot", str(app), str(depot)))

    def ready(self):
        start = self.clock()
        while not self.gate.ready():
            if self.clock() - start >= self.wait:
                return False
            self.sleep(2.0)
        return True

    def one(self, dlc):
        """Steam's answer about one DLC: "done", "no-depot", "not-owned", or why it failed."""
        result = "no-depot"
        for app in (APP_ID, dlc):
            tail = ConsoleTail(self.steam)
            self.send(app, dlc)
            why = self.answer(tail, dlc)
            if why == "done":
                return why
            if DLC_NOT_OWNED.search(why):
                return "not-owned"
            if not DLC_NO_DEPOT.search(why):
                result = why
        return result

    def answer(self, tail, dlc):
        start = self.clock()
        while self.clock() - start < self.ANSWER:
            for line in tail.poll():
                if "Depot download complete" in line and "depot_%d" % dlc in line:
                    return "done"
                m = re.search(r"Depot download failed\s*:\s*(.*)", line)
                if m:
                    return m.group(1).strip()
            self.sleep(0.5)
        return "Steam didn't answer"

    def fetch(self, dlc, memo):
        """Why dlc could not be fetched, or "". What is settled is noted at once, for a stop."""
        why = self.one(dlc)
        if why == "done":
            memo.setdefault("done", []).append(dlc)
        elif why in ("no-depot", "not-owned"):
            memo.setdefault("none", []).append(dlc)
        else:
            return why
        write_json(self.state, memo)
        return ""

    def run(self):
        """The DLC that could not be fetched, as a list of (AppID, why)."""
        if not self.ready():
            self.progress.say("note: Steam isn't signed in, so no DLC was downloaded. Refresh Game Files gets it later.")
            self.guide.clear()
            return []
        owned = account_dlc(self.steam, self.gate.st.account)
        if owned is None:  # the only full list there is: appinfo.vdf and app_status miss most DLC of the later years
            self.progress.say("note: Steam hasn't saved this account's list of DLC yet. Refresh Game Files gets the "
                              "DLC later.")
            self.guide.clear()
            return []
        memo = read_json(self.state) or {}
        have = {int(k) for k in memo.get("done", [])} | {int(k) for k in memo.get("none", [])}
        todo = [d for d in sorted(owned) if d not in have]
        failed = []
        if not todo:
            self.guide.clear()
            return failed
        self.guide("downloading-dlc")
        for i, dlc in enumerate(todo):
            self.progress(i / len(todo), "%s… %d of %d" % (DLC_LABEL, i, len(todo)))
            if not self.ready():
                failed += [(d, "Steam isn't signed in") for d in todo[i:]]
                break
            self.guide("downloading-dlc")
            why = self.fetch(dlc, memo)
            if why:
                failed.append((dlc, why))
        # Steam's content servers fail now and then ("No connection"): once more, before saying so.
        failed = [(d, w) for d, w in ((d, self.fetch(d, memo) if self.gate.ready() else w) for d, w in failed) if w]
        self.progress(1.0, "%s… %d of %d" % (DLC_LABEL, len(todo), len(todo)))
        self.guide.clear()
        if failed:
            self.progress.say("note: %d of your %d DLC could not be downloaded (%s: %s). Refresh Game Files tries them "
                              "again." % (len(failed), len(todo), failed[0][0], failed[0][1]))
        return failed


# --- a game from Steam's console ---------------------------------------------------------------------------
#
# Neither edition needs its game installed in Steam: `download_depot APP DEPOT [MANIFEST]`, a console command of
# the Steam client, downloads a depot the signed-in account owns into Steam's content folder, as plain files. The
# native (arm64) client keeps that folder inside its bundle, <Steam>/Steam.AppBundle/Steam/Contents/MacOS/
# steamapps/content/app_<app>/depot_<depot>; older clients wrote to <Steam>/steamapps/content. The running Steam
# takes the command on its launcher's command line (steam_command), so nothing is pasted. What a download needs
# is the same for both: Steam ready (SteamGate), room where Steam puts it or a link to another disk (redirect),
# progress from Steam's logs and the folder (SteamLog, depot_wait), and a check that what arrived is the
# supported build (Depot.locate). The Learn & Play installer (win-x86/install/lp_install.py) and the Mac game
# (mac_game below) both use this; each edition's figures are in its target.toml.

UNITS = {"b": 1, "bytes": 1, "kb": 1 << 10, "mb": 1 << 20, "gb": 1 << 30}
RATE_UNITS = {"bps": 1 / 8, "kbps": 1e3 / 8, "mbps": 1e6 / 8, "gbps": 1e9 / 8,
              "b/s": 1, "kb/s": 1 << 10, "mb/s": 1 << 20, "gb/s": 1 << 30}
STAMP = re.compile(r"^\[(\d{4})-(\d\d)-(\d\d) (\d\d):(\d\d):(\d\d)\]")
DOWNLOAD_NOTE = "download-dir"  # in an edition's folder: where game-redirect sent Steam's downloads


def download_content_dir(steam):
    """The content folder the next download_depot writes to."""
    return steam_content_dirs(steam)[0 if os.path.isdir(os.path.join(steam, BUNDLE_MACOS)) else 1]


def depot_source(path):
    """PATH is a depot folder of Steam's content folder (.../app_<app>/depot_<depot>), wherever that folder is."""
    path = os.path.abspath(path)
    return bool(re.match(r"depot_\d+$", os.path.basename(path))
                and re.match(r"app_\d+$", os.path.basename(os.path.dirname(path))))


def folder_bytes(folder):
    """Bytes written under folder (a file's allocated blocks when smaller than its size: Steam makes files at
    their full size before it fills them), the file count, and a stamp that changes whenever Steam writes."""
    total = n = stamp = 0
    for d, _, files in os.walk(folder):
        for f in files:
            try:
                st = os.lstat(os.path.join(d, f))
            except OSError:
                continue
            blocks = getattr(st, "st_blocks", None)
            total += min(st.st_size, blocks * 512) if blocks is not None else st.st_size
            stamp = max(stamp, st.st_mtime_ns)
            n += 1
    return total, n, (total, n, stamp)


def startup_folder():
    return "/System/Volumes/Data" if os.path.isdir("/System/Volumes/Data") else "/"


def on_startup_disk(path):
    """On the startup disk: macOS's system volume and the data volume are two devices of it."""
    dev = os.stat(existing(path)).st_dev
    return dev in (os.stat("/").st_dev, os.stat(startup_folder()).st_dev)


def dlc_room(steam, total, average):
    """Room for the DLC still to come: what is owned at the AVERAGE size, when the signed-in account's ticket is
    on disk, else all of it (TOTAL) as a Mac library holds it."""
    account = steam_connection(steam)[1]
    owned = account_dlc(steam, account) if account else None
    if owned is None:
        return total
    return min(len(owned) * average, total)


def describe_space(free, need, where):
    if not free and not os.path.isdir(where):
        return "Steam's download folder is %s, on a drive that isn't connected." % where
    return "%s free in %s; about %s is needed%s" % (human(free), where, human(need), "" if free >= need else ": not enough")


def redirect(steam, folder, note=None):
    """Make Steam's content folder a link to folder (Steam follows it), so the download goes to another disk.
    What is in a real content folder moves to folder (a stopped download continues there). NOTE: the file that
    remembers the folder, since a Steam update may drop the link (restore_redirect)."""
    content = download_content_dir(steam)
    folder = os.path.abspath(os.path.expanduser(folder))
    if os.path.ismount(folder):  # a whole disk was chosen: not all over its top folder
        folder = os.path.join(folder, FOLDER, "steam-content")
    os.makedirs(folder, exist_ok=True)
    if os.path.islink(content):
        if os.path.realpath(content) == os.path.realpath(folder):
            remember_download(folder, note)
            return content
        os.remove(content)
    elif os.path.isdir(content):
        left = [f for f in os.listdir(content) if f != ".DS_Store"]
        clash = [f for f in left if os.path.exists(os.path.join(folder, f))]
        if clash:
            raise Failure("%s and %s both hold %s: move or delete one of them, then run this again"
                          % (content, folder, ", ".join(sorted(clash))))
        for f in left:
            shutil.move(os.path.join(content, f), os.path.join(folder, f))
        shutil.rmtree(content)
    os.makedirs(os.path.dirname(content), exist_ok=True)
    os.symlink(folder, content)
    remember_download(folder, note)
    return content


def remember_download(folder, note):
    if note:
        os.makedirs(os.path.dirname(note), exist_ok=True)
        with open(note, "w") as f:
            f.write(folder + "\n")


def restore_redirect(steam, notes):
    """A Steam update can drop the link redirect made, with the download still on the other disk: link it again,
    when that folder is there and Steam has not started a new content folder of its own. NOTES: this edition's
    note, then the other's (Steam has one content folder for both)."""
    content = download_content_dir(steam)
    for note in notes:
        try:
            with open(note) as f:
                folder = f.read().strip()
        except OSError:
            continue
        if not folder or not os.path.isdir(folder) or os.path.isdir(content):
            continue
        if os.path.islink(content):  # a link to nowhere: the folder it led to is not the one noted
            os.remove(content)
        redirect(steam, folder, notes[0])
        return


class Located:
    """status: "ok" (the pinned build), "missing" (no program), "incomplete" (smaller than the pinned one, as
    while Steam writes it), "different" (another build) or "unfinished" (Steam's download has not ended)."""

    def __init__(self, status, folder, exe=None, size=0, sha=""):
        self.status, self.folder, self.exe, self.size, self.sha = status, folder, exe, size, sha


class Depot:
    """One edition's game as Steam's console downloads it. CFG has the edition's figures, as its target.toml
    names them: steam_app, steam_depot, depot_manifest (empty: the latest), depot_disk_bytes (the depot on disk),
    disk_margin_bytes, and the game's program, the build RS Re-ARMed supports: exe_name, exe_size, exe_sha256.
    FIND(folder) is the program's path in a game folder, or None; WHAT and LABEL are the words for it ("the
    Windows game", "Downloading the Windows game"), GAME the name the Steam steps use, TARGET the file the
    figures are in."""

    def __init__(self, cfg, find, what, label, game, target, wrong_account="wrong-account"):
        self.cfg, self.find, self.what, self.label, self.game, self.target = cfg, find, what, label, game, target
        self.wrong_account = wrong_account  # the guide message for an account without the depot

    app = property(lambda self: self.cfg["steam_app"])
    depot = property(lambda self: self.cfg["steam_depot"])
    disk_bytes = property(lambda self: self.cfg.get("depot_disk_bytes", 0))

    def command(self):
        cmd = "download_depot %d %d" % (self.app, self.depot)
        return cmd + (" " + self.cfg["depot_manifest"] if self.cfg.get("depot_manifest") else "")

    def sub(self):
        return os.path.join("app_%d" % self.app, "depot_%d" % self.depot)

    def dirs(self, steam):
        return [os.path.join(c, self.sub()) for c in steam_content_dirs(steam)]

    def dir(self, steam):
        """The depot folder: the one that exists, else where the next download goes."""
        for d in self.dirs(steam):
            if os.path.isdir(d):
                return d
        return os.path.join(download_content_dir(steam), self.sub())

    def locate(self, folder, _cache={}):
        exe = self.find(folder)
        if not exe:
            return Located("missing", folder)
        st = os.stat(exe)
        game = self.folder_of(exe)
        if st.st_size < self.cfg["exe_size"]:
            return Located("incomplete", game, exe, st.st_size)
        key = (exe, st.st_size, st.st_mtime_ns)
        if key not in _cache:
            _cache[key] = sha256(exe)
        sha = _cache[key]
        return Located("ok" if sha == self.cfg["exe_sha256"] else "different", game, exe, st.st_size, sha)

    def folder_of(self, exe):
        """The game folder of a program FIND found: the program's path less the name's own folders."""
        for _ in self.cfg["exe_name"].split("/"):
            exe = os.path.dirname(exe)
        return exe

    def short(self, folder):
        """A depot folder holds less than the whole depot: a finished one has every byte of depot_disk_bytes on
        disk (checked on clean downloads of both games), and Steam makes the files at full size long before they
        are filled, so a good program says nothing about the rest. Half a percent of room for other file
        systems."""
        return folder_bytes(folder)[0] < self.disk_bytes * 0.995

    def unfinished(self, steam):
        """Steam's logs show a download of the depot that started after the last one finished."""
        log = SteamLog(steam, self.depot, from_start=True).poll()
        return log.started and not log.complete

    def locate_default(self, steam):
        """locate() over every depot folder Steam may have used: the supported build if one has it. A program of
        either kind while Steam's log shows a download of the depot that has not finished is "unfinished": Steam
        makes every file at its full size first, so an unfinished program has the right size and the wrong
        contents, and a finished one says nothing about the files still being filled."""
        found = [self.locate(d) for d in self.dirs(steam)]
        rank = {"ok": 0, "different": 1, "incomplete": 2, "missing": 3}
        best = min(found, key=lambda loc: rank[loc.status])
        if best.status == "missing":
            return self.locate(self.dir(steam))
        if best.status in ("ok", "different") and (self.unfinished(steam)
                                                   or (best.status == "ok" and self.short(best.folder))):
            best.status = "unfinished"
        return best

    def describe(self, loc):
        name = self.cfg["exe_name"].split("/")[0]  # (the Mac game: Rocksmith2014.app)
        if loc.status == "ok":
            return "Found %s in %s (the supported build)." % (self.what, loc.folder)
        if loc.status == "missing":
            return "No %s in %s." % (name, loc.folder)
        if loc.status == "unfinished":
            return ("Steam's download of the game into %s has not finished (Steam may have quit); start the download "
                    "again to continue it." % loc.folder)
        if loc.status == "incomplete":
            return "%s in %s is incomplete (%s of %s)." % (name, loc.folder, human(loc.size),
                                                           human(self.cfg["exe_size"]))
        return ("%s in %s is a different build of the game from the one RS Re-ARMed supports, so it cannot be used "
                "(found: SHA-256 %s, %d bytes; supported: %s, %d bytes)."
                % (name, loc.folder, loc.sha, loc.size, self.cfg["exe_sha256"], self.cfg["exe_size"]))

    def room(self, steam):
        """(need, folder, there): what the download needs where Steam puts it (the depot's size on disk and a
        margin, less what is already there, and the DLC still to come), that folder, and whether it is there (a
        link to a drive that is not connected is not)."""
        content = download_content_dir(steam)
        depot = os.path.join(content, self.sub())
        have = folder_bytes(depot)[0] if os.path.isdir(depot) else 0
        need = max(self.cfg["depot_disk_bytes"] - have, 0) + self.cfg["disk_margin_bytes"]
        need += dlc_room(steam, self.cfg.get("dlc_disk_bytes", 0), self.cfg.get("dlc_average_bytes", 0))
        return need, os.path.realpath(content), not (os.path.islink(content) and not os.path.isdir(content))

    def space(self, steam):
        """(free, need, folder): free space where the download goes, and what it needs. On the startup disk
        room for macOS has to stay next to it: a depot that only just fits there fills the disk."""
        need, target, there = self.room(steam)
        if not there:
            return 0, need, target
        if on_startup_disk(target):
            need += max(self.cfg.get("startup_margin_bytes", 0) - self.cfg["disk_margin_bytes"], 0)
        return free_bytes(target), need, target


class SteamLog:
    """What the Steam client logs about one depot download from now on (content_log.txt, console_log.txt):
    the download size, the bytes downloaded (from the download rate it logs), the end and the manifest, or
    a failure."""

    FILES = ("content_log.txt", "console_log.txt")

    def __init__(self, steam, depot, from_start=False):
        # from_start: the whole log, the one before Steam last started first
        files = [f.replace(".txt", ".previous.txt") for f in self.FILES] + list(self.FILES) if from_start else self.FILES
        self.paths = [os.path.join(d, "logs", f) for d in (steam, os.path.join(steam, BUNDLE_MACOS)) for f in files]
        self.depot = str(depot)
        self.pos = {p: 0 if from_start else self._size(p) for p in self.paths}
        self.started = False
        self.expected = 0      # bytes to download
        self.files = 0
        self.downloaded = 0.0  # estimated from the logged rate
        self.rate = 0.0        # bytes/s
        self.rate_at = None
        self.complete = None   # (files or 0, manifest or "")
        self.failed = None

    @staticmethod
    def _size(p):
        try:
            return os.path.getsize(p)
        except OSError:
            return 0

    def poll(self):
        for p in self.paths:
            size = self._size(p)
            if size < self.pos[p]:  # Steam started again: a new log
                self.pos[p] = 0
            if size == self.pos[p]:
                continue
            with open(p, "rb") as fh:
                fh.seek(self.pos[p])
                data = fh.read(size - self.pos[p])
            end = data.rfind(b"\n") + 1  # complete lines only
            self.pos[p] += end
            for line in data[:end].decode("utf-8", "replace").splitlines():
                self.line(line)
        return self

    def line(self, line):
        d = self.depot
        ours = re.search(r"(depot[ _]?%s\b|\b%s\b)" % (d, d), line, re.I)
        m = re.search(r"Downloading depot %s\b[^(]*\((?:\s*(\d+)\s+files?,)?\s*([\d.,]+)\s*(bytes|b|kb|mb|gb)\s*\)"
                      % d, line, re.I)
        if m:
            self.started, self.complete = True, None
            self.files = int(m.group(1) or 0)
            self.expected = int(float(m.group(2).replace(",", "")) * UNITS[m.group(3).lower()])
            self.downloaded, self.rate_at = 0.0, None
        m = re.search(r"Current download rate\D*?([\d.]+)\s*([kmg]?b(?:ps|/s))", line, re.I)
        if m and self.expected and not self.complete:
            t = STAMP.match(line)
            t = time.mktime(tuple(int(x) for x in t.groups()) + (0, 0, -1)) if t else time.time()
            if self.rate_at is not None and t > self.rate_at:
                self.downloaded = min(self.downloaded + self.rate * (t - self.rate_at), float(self.expected))
            self.rate = float(m.group(1)) * RATE_UNITS[m.group(2).lower()]
            self.rate_at = t
        if re.search(r"depot download complete", line, re.I) and ours:
            files = re.search(r"(\d+)\s+files?", line)
            manifest = re.search(r"manifest\s+(\d+)", line, re.I)
            self.complete = (int(files.group(1)) if files else 0, manifest.group(1) if manifest else "")
        # "Depot download failed : missing license for depot (No subscription)": Steam's line names no depot
        if re.search(r"depot download failed", line, re.I) or \
                (re.search(r"no subscription|missing license|access denied", line, re.I)
                 and (ours or "depot" not in line.lower())):
            self.failed = STAMP.sub("", line).strip()


def download_running(steam, depot):
    """Steam, since it last started, began a download of depot that has neither finished nor failed."""
    for d in (steam, os.path.join(steam, BUNDLE_MACOS)):
        try:
            with open(os.path.join(d, "logs", "console_log.txt"), "rb") as f:
                f.seek(0, os.SEEK_END)
                f.seek(max(f.tell() - (2 << 20), 0))
                text = f.read().decode("utf-8", "replace")
        except OSError:
            continue
        text = text[text.rfind("Console Log Start"):] if "Console Log Start" in text else text
        last = None
        for last in re.finditer(r"Downloading depot (\d+)|Depot download (?:complete|failed)", text):
            pass
        if last and last.group(1) == str(depot):
            return True
    return False


class DepotDownload:
    """Gets the download going with nothing for the user to paste, and says what, if anything, they have to
    do. depot_wait() calls tick() on every round; failed() takes Steam's failure line. The running Steam takes
    the console command on its command line, so the only steps left to the user are the ones only they can do:
    installing Steam and signing in. A second command for a download that runs makes Steam fetch every chunk
    twice (it ends well, but with thousands of assertions in its log), so one goes out only when none runs.
    SPACE() is (free, need, folder) where the download goes (default: the depot's own count)."""

    PASTE_AFTER = 30.0   # Steam hasn't reacted to the command by then: the console and the clipboard
    ADOPT = 60.0         # a download Steam says is running, from an earlier run: how long it may be silent
    STALLED = 180.0      # nothing written for this long: say so
    RESEND = 600.0       # and for this long: send the command again (Steam may have dropped it)
    RETRY_AFTER = 30.0   # after a failure of Steam's
    RETRIES = 3

    def __init__(self, depot, steam, guide, gate=None, send=None, paste=None, clock=time.monotonic,
                 running=download_running, space=None):
        self.depot, self.steam, self.guide, self.clock = depot, steam, guide, clock
        self.gate = gate or SteamGate(steam, depot.game, guide, clock=clock)
        self.command = depot.command()
        self.send = send or (lambda: steam_command(steam, *self.command.split()))
        self.paste = paste or self.open_console
        self.running = running
        self.space = space or (lambda: depot.space(steam))
        self.sent = self.pasting = self.adopted = self.retry = self.looked = False
        self.sent_at = 0.0
        self.bad_account = None
        self.fails = 0
        self.retry_at = None

    def open_console(self):
        subprocess.run(["/usr/bin/pbcopy"], input=self.command.encode(), check=False)
        steam_open("steam://open/console")

    def try_again(self, *_):
        self.retry = True

    def reset(self, log):
        self.sent = self.pasting = self.adopted = False
        log.started, log.complete, log.failed, log.expected = False, None, None, 0

    def tick(self, log, idle):
        now = self.clock()
        if not self.gate.ready():
            if self.sent and not self.gate.st.running:
                self.reset(log)  # Steam quit, and its download with it
            return
        st = self.gate.st
        if self.bad_account is not None:
            if st.account == self.bad_account:
                self.guide(self.depot.wrong_account, name=st.name, game=self.depot.game)
                return
            self.bad_account = None  # another account is signed in now: it may have the game
            self.reset(log)
        if self.retry:
            self.retry = False
            if self.pasting:
                self.paste()
            else:
                self.retry_at = None
                self.fails = 0
                self.reset(log)
        if self.retry_at is not None and now < self.retry_at:
            return
        self.retry_at = None
        if not self.sent:
            self.start(log, now)
        elif self.adopted and idle >= self.ADOPT:
            self.reset(log)  # nothing is happening after all
        elif log.started:
            quiet = min(idle, now - self.sent_at)
            if quiet >= self.RESEND:
                self.reset(log)
            else:
                self.guide("download-stalled" if quiet >= self.STALLED else "downloading")
        elif not self.pasting and now - self.sent_at >= self.PASTE_AFTER:
            self.pasting = True
            self.paste()
            self.guide("paste", command=self.command)

    def start(self, log, now):
        self.sent, self.sent_at = True, now
        if not self.looked and self.running(self.steam, self.depot.depot):
            self.looked = self.adopted = log.started = True
            self.guide("downloading")
            return
        self.looked = True
        free, need, where = self.space()
        if free < need:
            raise Failure(describe_space(free, need, where), 4)
        self.send()
        self.guide("downloading")

    def failed(self, line):
        """Steam said the download failed: True when that is handled here, False to give up."""
        log_line = re.sub(r"^.*?failed\s*:\s*", "", line, flags=re.I)
        if re.search(r"missing license|no subscription|missing app info", line, re.I):
            self.bad_account = self.gate.st.account if self.gate.st else 0
            self.sent = False
            return True
        self.fails += 1
        if self.fails > self.RETRIES:
            return False
        self.guide("download-failed", why=log_line)
        self.retry_at = self.clock() + self.RETRY_AFTER
        self.sent = False
        return True


def depot_wait(depot, folder, steam, progress, timeout=0, interval=2.0, settle=90.0, clock=time.monotonic,
               sleep=time.sleep, driver=None):
    """Wait until the depot download has finished and the game's program checks out (folder: the depot folder,
    default whichever of Steam's depot folders appears). driver (a DepotDownload) starts the download and looks
    after it; without one the user has started it. Returns Located."""
    log = SteamLog(steam, depot.depot)
    label = depot.label
    start = clock()
    last_change, last_seen = start, None
    last_have, last_at, speed = 0, start, 0.0
    said_waiting = False
    while True:
        log.poll()
        here = folder or depot.dir(steam)
        have, nfiles, stamp = folder_bytes(here) if os.path.isdir(here) else (0, 0, None)
        if stamp != last_seen:
            last_seen, last_change = stamp, clock()
        if driver:
            driver.tick(log, clock() - last_change)
        if clock() - last_at >= 5 * interval:
            speed = 0.6 * speed + 0.4 * max(have - last_have, 0) / (clock() - last_at)
            last_have, last_at = have, clock()
        if log.expected:
            # How far it is: the bytes on disk, or the rate Steam logs once a minute, whichever says more
            part = log.downloaded / log.expected
            if depot.disk_bytes:
                part = min(max(part, have / depot.disk_bytes), 1.0)
            done = part * log.expected
            rate = " (%.1f MB/s)" % (log.rate / (1 << 20)) if log.rate else ""
            if speed > 1 << 16:
                rate = " (%.1f MB/s, about %d min left)" % (speed / (1 << 20), (1 - part) * depot.disk_bytes
                                                            / speed / 60 + 1)
            progress(part * 0.99, "%s… %s of %s%s" % (label, human(done), human(log.expected), rate))
        elif have and depot.disk_bytes:
            progress(have / depot.disk_bytes * 0.99,
                     "%s… about %s of %s" % (label, human(have), human(depot.disk_bytes)))
        elif have:
            progress(0, "%s… %s so far" % (label, human(have)))
        elif not said_waiting:
            progress(0, "%s… waiting for Steam to start" % label)
            said_waiting = True
        if log.failed:
            if not driver or not driver.failed(log.failed):
                raise Failure("Steam says the download failed: %s" % log.failed)
            log.failed = None
        loc = depot.locate(here)
        if log.complete:
            files, manifest = log.complete
            what = ", ".join(x for x in ("%d files" % files if files else "",
                                         "manifest %s" % manifest if manifest else "") if x)
            progress.say("Steam finished the download%s." % (" (%s)" % what if what else ""))
            if loc.status == "ok" and manifest and manifest != depot.cfg.get("depot_manifest"):
                progress.say("Manifest %s holds the supported build (depot_manifest in %s)." % (manifest, depot.target))
            if loc.status != "ok":
                raise Failure(depot.describe(loc), 2 if loc.status == "different" else 1)
            break
        # Without Steam's word (its log elsewhere, or in a format this does not know): the program checks out and
        # Steam has written nothing for `settle` seconds. File counts and sizes do not tell: Steam makes every
        # file at its full size first.
        settled = clock() - last_change >= settle
        if loc.status == "ok" and settled and not driver and not download_running(steam, depot.depot) \
                and not depot.short(here):
            progress.say("Steam's log does not say the download finished; nothing has changed for %d s, so it "
                         "is taken as finished." % settle)
            break
        # Steam makes the program at its full size first, so a stalled or waiting download looks like this too.
        if loc.status == "different" and settled and not driver and not download_running(steam, depot.depot):
            raise Failure(depot.describe(loc), 2)
        if timeout and clock() - start >= timeout:
            raise Failure("timed out waiting for the download (%s in %s so far)" % (human(have), here), 3)
        sleep(interval)
    progress(1.0, "%s… done" % label)
    return loc


# --- the Mac game ------------------------------------------------------------------------------------------
#
# The Mac game's folder is, in this order: the folder the user names; the game installed in a Steam library (the
# appmanifest says so); Steam's download of the Mac depot (its content folder, see above), which mac_game starts
# when the game is nowhere, as the Learn & Play installer does for the Windows depot. A game that Steam itself
# is installing or updating right now is waited for. A game from Steam's download has no appmanifest: its DLC
# comes from the DLC depots (dlc-fetch, as Learn & Play's), the DLC the account owns from its ownership ticket,
# and the account from the license that has the Mac depot (steam_account).

MAC_GAME_FOLDER = "Rocksmith2014"
MAC_GAME_BIN = os.path.join("Rocksmith2014.app", "Contents", "MacOS", "Rocksmith2014")
MAC_TARGET = os.path.join("tools", "targets", "mac-x64", "target.toml")
MAC_LABEL = "Downloading the Mac game"


def mac_home():
    """The Mac edition's folder (install.sh's RS_NATIVE_HOME)."""
    return os.environ.get("RS_NATIVE_HOME") or os.path.expanduser("~/Library/Application Support/RS Re-ARMed")


def download_notes(edition):
    """The files that remember where Steam's downloads were sent: this edition's first."""
    notes = [os.path.join(mac_home(), DOWNLOAD_NOTE),
             os.path.join(os.environ.get("LP_HOME") or os.path.join(mac_home(), "learn-play"), DOWNLOAD_NOTE)]
    return notes if edition == "mac" else notes[::-1]


def mac_depot(target=None):
    """The Mac game's depot, with the figures of tools/targets/mac-x64/target.toml (TARGET: another file); None
    where that file is not at hand (the copy of this tool next to an installed game: it downloads no game)."""
    path = target or os.environ.get("RS_MAC_TARGET") or os.path.join(
        os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))), MAC_TARGET)
    try:
        cfg = _flat_toml(path)
    except (OSError, ValueError):
        return None
    if not all(k in cfg for k in ("steam_app", "steam_depot", "depot_disk_bytes", "disk_margin_bytes", "exe_name",
                                  "exe_size", "exe_sha256")):
        return None

    def find(folder):
        p = os.path.join(folder, cfg["exe_name"])
        return p if os.path.isfile(p) else None
    return Depot(cfg, find, "the Mac game", MAC_LABEL, GAME_MAC, MAC_TARGET, wrong_account="wrong-account-mac")


def _flat_toml(path):
    """target.toml's subset (tools/targets/targets.py parse_toml, which the installed copy of this file does
    not have next to it): `key = "string" | integer`, # comments; lists are left out."""
    d = {}
    with open(path) as f:
        for ln in f:
            k, eq, v = ln.strip().partition("=")
            k, v = k.strip(), v.strip()
            if not eq or k.startswith("#"):
                continue
            if v.startswith('"'):
                d[k] = v[1:v.index('"', 1)]
            elif not v.startswith("["):
                d[k] = int(v.split("#")[0].strip(), 0)
    return d


def library_folders(steam):
    """Steam's library folders: the Steam folder and the `"path"` entries of libraryfolders.vdf."""
    dirs = [steam]
    try:
        with open(os.path.join(steam, "steamapps", "libraryfolders.vdf"), errors="replace") as f:
            for m in re.finditer(r'"path"\s+"([^"]*)"', f.read()):
                p = m.group(1).replace("\\\\", "\\")
                if p not in dirs:
                    dirs.append(p)
    except OSError:
        pass
    return dirs


def mac_dlc_folders(steam):
    """The Mac game's dlc folder in each Steam library (Steam downloads the PC archives there too), and
    $RS_GAME_DIR's."""
    dirs = []
    if os.environ.get("RS_GAME_DIR"):
        dirs.append(os.path.join(os.environ["RS_GAME_DIR"], "dlc"))
    for lib in library_folders(steam):
        dirs.append(os.path.join(lib, "steamapps", "common", MAC_GAME_FOLDER, "dlc"))
    out = []
    for d in dirs:
        if os.path.isdir(d) and os.path.realpath(d) not in [os.path.realpath(o) for o in out]:
            out.append(d)
    return out


def steam_dlc_folders(steam):
    """Where a game from Steam's download finds its song archives, Learn & Play's PC ones and the Mac game's
    alike (each DLC depot has both kinds): the DLC depots Steam downloaded and the Mac game's dlc folders, which
    is also where custom songs are."""
    return dlc_depot_folders(steam) + mac_dlc_folders(steam)


lp_dlc_folders = steam_dlc_folders


def mac_game_install(lib):
    """(installed, bytes done, bytes in all) from the appmanifest in library lib; (False, 0, 0) without one.
    Steam's StateFlags: 4 is fully installed, other bits mean it is being downloaded or updated."""
    try:
        with open(os.path.join(lib, "steamapps", "appmanifest_%d.acf" % APP_ID), errors="replace") as f:
            text = f.read()
    except OSError:
        return False, 0, 0

    def number(key):
        m = re.search(r'"%s"\s+"(\d+)"' % key, text)
        return int(m.group(1)) if m else 0
    return number("StateFlags") == 4, number("BytesDownloaded"), number("BytesToDownload")


def find_mac_game(steam, folder=None):
    """The Mac game's folder: FOLDER when it has the game, else the first Steam library with it installed."""
    if folder and os.path.isfile(os.path.join(folder, MAC_GAME_BIN)):
        return folder
    for lib in library_folders(steam):
        game = os.path.join(lib, "steamapps", "common", MAC_GAME_FOLDER)
        if os.path.isfile(os.path.join(game, MAC_GAME_BIN)) and mac_game_install(lib)[0]:
            return game
    return None


def mac_game_pending(steam):
    """Steam is installing or updating the Mac game in one of its libraries (an appmanifest that does not say
    "installed" yet): that install is waited for, not downloaded a second time."""
    return any(os.path.isfile(os.path.join(lib, "steamapps", "appmanifest_%d.acf" % APP_ID))
               and not mac_game_install(lib)[0] for lib in library_folders(steam))


def mac_game_wait(steam, folder, progress, guide, timeout=0, interval=2.0, gate=None, clock=time.monotonic,
                  sleep=time.sleep):
    """The Mac game's folder, once Steam has finished the install or update it is at; None when Steam gave that
    up (the appmanifest is gone)."""
    gate = gate or SteamGate(steam, GAME_MAC, guide)
    start = clock()
    while True:
        game = find_mac_game(steam, folder)
        if game or not mac_game_pending(steam):
            guide.clear()
            return game
        if gate.ready():
            done = [mac_game_install(lib) for lib in library_folders(steam)]
            total = max((d[2] for d in done), default=0)
            guide("installing-game", game=GAME_MAC)
            if total:
                got = max(d[1] for d in done)
                progress(got / total * 0.99, "Installing the game in Steam… %d of %d MB" % (got >> 20, total >> 20))
        if timeout and clock() - start >= timeout:
            raise Failure("the game did not appear in Steam in time", 3)
        sleep(interval)


def mac_game(steam, folder, progress, guide, timeout=0, interval=2.0, depot=None, download=True, **kw):
    """(the Mac game's folder, where it is from: "folder", "library" or "download"). A FOLDER the user names has
    to hold the game. Otherwise the game installed in a Steam library; else Steam's download of the Mac depot,
    started here when it is not there yet (DOWNLOAD false: only looked for), with the Steam steps, progress and
    checks of depot_wait. kw: for the tests (gate, clock, sleep, send, paste, running)."""
    if folder:
        if not os.path.isfile(os.path.join(folder, MAC_GAME_BIN)):
            raise Failure("Rocksmith2014.app is not in %s: that is not the Mac game's folder" % folder)
        return folder, "folder"
    game = find_mac_game(steam)
    if game:
        return game, "library"
    depot = depot or mac_depot()
    if depot is None:
        raise Failure("the Mac game is not installed in Steam, and %s is not here to download it with" % MAC_TARGET)
    restore_redirect(steam, download_notes("mac"))
    loc = depot.locate_default(steam)
    if loc.status == "ok":
        return loc.folder, "download"
    wait = {k: kw[k] for k in ("gate", "clock", "sleep") if k in kw}
    if mac_game_pending(steam) and loc.status == "missing" and download:
        game = mac_game_wait(steam, None, progress, guide, timeout, interval, **wait)
        if game:
            return game, "library"
    if not download:
        raise Failure(depot.describe(loc), 2 if loc.status == "different" else 1)
    driver = DepotDownload(depot, steam, guide, **{k: kw[k] for k in ("gate", "clock", "send", "paste", "running")
                                                   if k in kw})
    try:
        signal.signal(signal.SIGUSR1, driver.try_again)  # the installer app's Try Again
    except ValueError:
        pass  # not the main thread (a test)
    wait.pop("gate", None)
    loc = depot_wait(depot, None, steam, progress, timeout, interval, driver=driver, **wait)
    guide.clear()
    progress.say(depot.describe(loc))
    return loc.folder, "download"


def default_steam():
    return os.environ.get("RS_STEAM_DIR") or os.path.expanduser("~/Library/Application Support/Steam")


def refresh(dest, progress=None, **kw):
    missing = missing_copy(dest)
    if missing:
        raise Failure(missing[1])
    info = read_json(os.path.join(dest, META, SOURCE_INFO))
    if not info or info.get("edition") not in EDITIONS or not info.get("source"):
        raise Failure("%s is not a copy of the game files (no %s/%s): run the installer" % (dest, META, SOURCE_INFO))
    dlc = info.get("dlc_from") or []
    # A game from Steam's download, which Learn & Play's always is and the Mac game's is when it is not installed
    # in Steam.
    if info["edition"] == "learn-play" or depot_source(info["source"]):
        if info.get("steam"):  # the Mac game, a library or DLC depots added since the copy
            dlc = dlc + [d for d in steam_dlc_folders(info["steam"]) if d not in dlc]
        # Steam may clear its download folder: the copy is what counts, and new DLC can still reach it.
        kw.setdefault("source_away", not os.path.isdir(info["source"]))
        if kw["source_away"]:
            kw["placement"] = None
    return Copier(info["edition"], info["source"], dest, dlc, info.get("steam"), progress, **kw).run()


def key_from_image(dest, image, progress=None, **kw):
    """The archives' key from the game's code IMAGE, kept in the copy DEST; a copy that waited for it
    (Copier._archive_key) is finished with a refresh. True when the copy has the key."""
    if cache_overlay is None:
        return False
    pk = cache_overlay.psarc_key
    if not os.path.isfile(os.path.join(dest, META, SOURCE_INFO)):
        return False  # not a copy (a game folder of Steam's): nothing is written there
    if not pk.note(dest, image=image):
        return False
    if os.path.exists(os.path.join(dest, META, KEY_PENDING)):
        refresh(dest, progress, **kw)
    return True


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0],
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--progress-lines", action="store_true")
    sub = ap.add_subparsers(dest="cmd")
    p = sub.add_parser("sync")
    p.add_argument("source")
    p.add_argument("dest")
    p.add_argument("--edition", required=True, choices=EDITIONS)
    p.add_argument("--dlc-from", action="append", default=[], metavar="DIR")
    p.add_argument("--steam", default=None)
    p.add_argument("--progress-lines", action="store_true", dest="progress_lines2")
    p = sub.add_parser("refresh")
    p.add_argument("dest")
    p.add_argument("--progress-lines", action="store_true", dest="progress_lines2")
    p = sub.add_parser("check")
    p.add_argument("dest")
    p.add_argument("--quiet", action="store_true")
    p = sub.add_parser("dlc-fetch")
    p.add_argument("state")
    p.add_argument("--steam", default=None)
    p.add_argument("--wait", type=float, default=120.0)
    p.add_argument("--edition", default="learn-play", choices=EDITIONS)
    p.add_argument("--copy", default=None, metavar="DEST")
    p = sub.add_parser("mac-game")
    p.add_argument("--folder", default=None)
    p.add_argument("--steam", default=None)
    p.add_argument("--timeout", type=float, default=0)
    p.add_argument("--interval", type=float, default=2.0)
    p.add_argument("--locate", action="store_true")
    p = sub.add_parser("mac-game-command")
    p = sub.add_parser("mac-game-space")
    p.add_argument("--steam", default=None)
    p = sub.add_parser("mac-game-redirect")
    p.add_argument("folder")
    p.add_argument("--steam", default=None)
    a = ap.parse_args(argv)
    if not a.cmd:
        ap.print_usage(sys.stderr)
        return 64
    progress = Progress(a.progress_lines or getattr(a, "progress_lines2", False))
    try:
        if a.cmd == "sync":
            dlc = a.dlc_from
            if not dlc and a.edition == "mac" and depot_source(a.source):
                dlc = steam_dlc_folders(a.steam or default_steam())  # no Steam install: the DLC is in its depots
            Copier(a.edition, a.source, a.dest, dlc, a.steam, progress).run()
        elif a.cmd == "refresh":
            refresh(os.path.abspath(a.dest), progress)
        elif a.cmd == "check":
            ok, msg = check(os.path.abspath(a.dest))
            if not a.quiet or not ok:
                print(msg, file=sys.stdout if ok else sys.stderr)
            return 0 if ok else 1
        elif a.cmd == "dlc-fetch":
            edition = a.edition
            if a.copy:  # a refresh: the copy says which game, and whether Steam's downloads are where its DLC is
                info = read_json(os.path.join(a.copy, META, SOURCE_INFO)) or {}
                edition = info.get("edition")
                if edition != "learn-play" and not (edition == "mac" and depot_source(info.get("source") or "")):
                    return 0
            os.makedirs(os.path.dirname(os.path.abspath(a.state)), exist_ok=True)
            DlcFetch(a.steam or default_steam(), a.state, progress, Guide(a.progress_lines), a.wait,
                     game=GAME_MAC if edition == "mac" else GAME_LP).run()
        elif a.cmd == "mac-game":
            game, where = mac_game(a.steam or default_steam(), a.folder, progress, Guide(a.progress_lines),
                                   a.timeout, a.interval, download=not a.locate)
            print("game-from %s\ngame-folder %s" % (where, game), flush=True)
        elif a.cmd in ("mac-game-command", "mac-game-space", "mac-game-redirect"):
            depot, steam = mac_depot(), getattr(a, "steam", None) or default_steam()
            if depot is None:
                raise Failure("%s is not here" % MAC_TARGET)
            if a.cmd == "mac-game-command":
                print(depot.command())
            elif a.cmd == "mac-game-space":
                restore_redirect(steam, download_notes("mac"))
                free, need, where = depot.space(steam)
                print(describe_space(free, need, where))
                return 0 if free >= need else 1
            else:
                content = redirect(steam, a.folder, download_notes("mac")[0])
                print("Steam's downloads now go to %s (%s links there)" % (os.path.realpath(content), content))
    except Failure as e:
        progress.say("error: %s" % e)
        return e.code
    except OSError as e:
        progress.say("error: %s" % e)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
