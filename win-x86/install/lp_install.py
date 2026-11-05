#!/usr/bin/env python3
"""Install steps for the Windows game (Rocksmith 2014 Learn & Play): get the game and the DLLs it needs.

    lp_install.py redist OUTDIR [--from DIR]... [--no-download]
        msvcr100.dll, msvcp100.dll, d3dx9_42.dll and D3DCompiler_42.dll (rebased) into OUTDIR, from Steam's
        Common Redistributables on this Mac, else from Microsoft's redistributables (downloaded, SHA-256
        pinned), extracted with /usr/bin/tar; every DLL's SHA-256 is checked.
    lp_install.py game-locate [DIR] [--path-only]
        find Rocksmith2014.exe (in DIR, default Steam's depot download folders) and check it is the build the
        win-x86 tables are made for.
    lp_install.py game-command
        the line for the Steam console that downloads the game.
    lp_install.py game-space [--with-build WORK]
        is there room for the download where Steam puts it, and for the build in WORK when that is on the same
        disk (exit 1 if not)? A folder chosen earlier with game-redirect is linked again first, if a Steam
        update dropped the link.
    lp_install.py startup-space WORK
        is there room on the startup disk for the build: swap, which is there whatever folder the build uses, and the
        build itself when WORK is on it (exit 1 if not)?
    lp_install.py game-redirect DIR
        make Steam's download folder a link to DIR (another disk); what Steam already keeps there moves along.
        The folder is remembered (<LP_HOME>/download-dir), since a Steam update may drop the link.
    lp_install.py game-download [--timeout S]
        get the game from Steam, with nothing to paste: Steam is opened, and once someone is signed in the
        running Steam is given the download command on its command line. Waits with progress. Says what the
        user has to do, if anything, in "guide" lines (gamefiles.py: Steam missing, not signed in, the wrong
        account, stalled), and goes on by itself when that is done. If Steam ignores the command, its console
        opens with the command on the clipboard. SIGUSR1 is the installer app's Try Again.
    lp_install.py game-wait [DIR] [--timeout S]
        wait until Steam has downloaded the game (a command pasted by hand), with progress from Steam's log,
        then check it as game-locate does.
    lp_install.py seed-check
        is the unpacker's seed here (exit 1 if not)? $LP_SEED, else seed.bin next to win-x86/unpack/lp_unpack.py.
    lp_install.py game-image GAME OUT --py PYTHON [--seed FILE] [--report FILE]
        the code image, with nothing from the user: GAME's exe unpacked offline with the seed
        (win-x86/unpack/lp_unpack.py), then built into OUT with win-x86/image/lp_image.py. PYTHON has the
        packages of win-x86/install/requirements.txt.
    lp_install.py game-unpack EXE OUT [--seed FILE]
        only the unpacking, in this Python. Needs unicorn + pefile (win-x86/install/requirements.txt).
    lp_install.py harvest-export DIR [--rebuild]
        write the harvester (win-x86/harvest: the prebuilt rsharvest2.exe, the one-click wrapper, the source)
        to DIR, to copy to a Windows PC for the one-time capture of the game's code. Needs no .NET; --rebuild
        (or LP_HARVEST_REBUILD=1) rebuilds the exe from the source with the .NET SDK.
    lp_install.py game-copy GAME [--dest DIR] [--dlc-from DIR]...
        the app's own copy of the game files (tools/play-native/gamefiles.py): GAME (the Windows game folder,
        e.g. Steam's depot download) and the PC song archives (*_p.psarc) of the Mac game's dlc folder in each
        Steam library (--dlc-from: other folders) into DEST (default <LP_HOME>/game,
        ~/Library/Application Support/RS Re-ARMed/learn-play/game). APFS clones on the same volume (no extra
        space), else a real copy after a free-space check; again later, only what changed. A new copy of a
        game on another APFS drive (a download redirected there) goes onto that drive as clones,
        <drive>/RS Re-ARMed/learn-play/game, and DEST is a link to it (RS_GAME_COPY_DIR chooses the place).
        The app reads only this copy.
    lp_install.py game-refresh [--dest DIR]
        the same from the folders the copy was made from (new DLC, a new download): the installer app's
        Refresh Game Files.
    lp_install.py build-estimate
        how long the build takes and the space it needs (target.toml's measurements).
    lp_install.py build WORK --image EXE --dlls DIR --game DIR [--py PYTHON] [--jobs N]
        translate, compile and link the game into WORK/out/rs2014_lp (lp_build.py); resumable, each
        translation keyed by the SHA-256 of its input and of the translator. --py: a Python with capstone.
    lp_install.py app-install WORK --game DIR [--app APP]
        install ~/Applications/RS Re-ARMed Learn & Play.app (ad-hoc signed) and its folder for settings
        and saves, ~/Library/Application Support/RS Re-ARMed/learn-play; DIR (the copy game-copy made) is the
        game folder the app reads.
    lp_install.py build-clean WORK [--all]
        delete the build's intermediates (keeping the translated libraries as a cache, unless --all).

Steam's depot download folder: the native Steam client writes download_depot output inside its own bundle,
<Steam>/Steam.AppBundle/Steam/Contents/MacOS/steamapps/content/app_221680/depot_<id>; older clients wrote to
<Steam>/steamapps/content/. Both are looked at.

Common options: --steam DIR (default $RS_STEAM_DIR, else ~/Library/Application Support/Steam),
--config FILE (default $LP_CONFIG, else tools/targets/win-x86/target.toml), --progress-lines ("progress F LABEL" lines
and "guide" lines on stdout, the installer app's format, instead of a status line in a terminal).

Every command can run again: what is already in place is checked and kept, a partial download continues.
Exit status: 0 done, 1 not there yet or failed, 2 a different build of the game, 3 timed out, 4 no room for
the download, 64 usage.

Standard library only (any Python 3.9+), and only tools macOS ships (/usr/bin/tar, /usr/bin/curl): no
Rosetta, no Homebrew, no steamcmd. The only network access is Microsoft's two redistributables, only when
Steam's copy is missing, and the Python packages of the build (pip); nothing about the user is sent anywhere.
"""
import argparse
import hashlib
import os
import re
import shutil
import signal
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "tools", "targets"))
sys.path.insert(0, os.path.join(ROOT, "win-x86", "d3dx"))
sys.path.insert(0, os.path.join(ROOT, "tools", "play-native"))
sys.path.insert(0, HERE)  # lp_build (python3 -I leaves the script's folder out of the path)
from targets import parse_toml  # noqa: E402
from rebase_pe import rebase  # noqa: E402
import gamefiles  # noqa: E402
import lp_build  # noqa: E402

CONFIG = os.environ.get("LP_CONFIG") or os.path.join(ROOT, "tools", "targets", "win-x86", "target.toml")
LOADSCREEN = os.path.join(ROOT, "tools", "loadscreen", "build.sh")
TAR = "/usr/bin/tar"
CURL = "/usr/bin/curl"
COMMON_REDIST = os.path.join("steamapps", "common", "Steamworks Shared", "_CommonRedist")
VC_EXE = "vcredist_x86.exe"
VC_CAB = "vc_red.cab"
DX_EXE = "directx_Jun2010_redist.exe"

# out name, target.toml key, package, file name in the package
DLLS = [
    ("msvcr100.dll", "msvcr100", "vc", "F_CENTRAL_msvcr100_x86"),
    ("msvcp100.dll", "msvcp100", "vc", "F_CENTRAL_msvcp100_x86"),
    ("d3dx9_42.dll", "d3dx9_42", "dx", "d3dx9_42.dll"),
    ("D3DCompiler_42.dll", "d3dcompiler_42", "dx", "D3DCompiler_42.dll"),
]


class Failure(Exception):
    def __init__(self, msg, code=1):
        Exception.__init__(self, msg)
        self.code = code


def load_config(path=None):
    return parse_toml(path or CONFIG)


def steam_dir(arg=None):
    return arg or os.environ.get("RS_STEAM_DIR") or os.path.expanduser("~/Library/Application Support/Steam")


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for b in iter(lambda: f.read(1 << 20), b""):
            h.update(b)
    return h.hexdigest()


def has_sha(path, sha):
    return bool(sha) and os.path.isfile(path) and sha256(path) == sha


def human(n):
    for unit, size in (("GB", 1 << 30), ("MB", 1 << 20), ("KB", 1 << 10)):
        if n >= size:
            return "%.1f %s" % (n / size, unit)
    return "%d bytes" % n


class Progress:
    """Progress of a long step: "progress F LABEL" lines (the installer app reads them), or one status line
    rewritten in place in a terminal, or nothing. F only grows."""

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
            self.stream.write("progress %.4f %s\n" % (self.fraction, label))
        elif self.tty:
            self.stream.write("\r\033[K" + label)
            self.pending = True
        self.stream.flush()

    def say(self, msg):
        if self.pending:
            self.stream.write("\n")
            self.pending = False
        self.stream.write(msg + "\n")
        self.stream.flush()


# --- redist ------------------------------------------------------------------------------------------------

library_folders = gamefiles.library_folders


def find_files(top, names, depth=6):
    """Files under top whose name is one of names (case-insensitive): {lower name: [paths]}."""
    want = {n.lower() for n in names}
    found = {}
    if not top or not os.path.isdir(top):
        return found
    base = top.rstrip(os.sep).count(os.sep)
    for d, subdirs, files in os.walk(top):
        if d.count(os.sep) - base >= depth:
            subdirs[:] = []
        for f in files:
            if f.lower() in want:
                found.setdefault(f.lower(), []).append(os.path.join(d, f))
    return found


def untar(archive, dest, names):
    """Extract the members of archive whose base names are names (case-insensitive) with /usr/bin/tar,
    which reads cabinets and self-extracting cabinet exes. {lower name: extracted path} of those found."""
    r = subprocess.run([TAR, "-tf", archive], capture_output=True, text=True, errors="replace")
    want = {n.lower() for n in names}
    members = [m for m in r.stdout.splitlines() if os.path.basename(m).lower() in want]
    if not members:
        return {}
    os.makedirs(dest, exist_ok=True)
    subprocess.run([TAR, "-xf", archive, "-C", dest] + members, capture_output=True)
    out = {}
    for d, _, files in os.walk(dest):
        for f in files:
            if f.lower() in want:
                out.setdefault(f.lower(), os.path.join(d, f))
    return out


def content_length(url):
    r = subprocess.run([CURL, "-sSIL", "--retry", "2", url], capture_output=True, text=True, errors="replace")
    lengths = re.findall(r"(?im)^content-length:\s*(\d+)", r.stdout)
    return int(lengths[-1]) if lengths else 0


def download(url, sha, dest, progress, label):
    """url to dest with /usr/bin/curl, continuing a partial download, checked against sha."""
    if has_sha(dest, sha):
        return dest
    part = dest + ".part"
    name = os.path.basename(dest)
    progress.say("Downloading %s from Microsoft (%s)" % (name, url))
    total = content_length(url)
    p = subprocess.Popen([CURL, "-fL", "-sS", "--retry", "3", "--retry-all-errors", "-C", "-", "-o", part, url])
    while p.poll() is None:
        have = os.path.getsize(part) if os.path.exists(part) else 0
        if total:
            progress(have / total, "%s… %s %s of %s" % (label, name, human(have), human(total)))
        time.sleep(0.5)
    # 33: the server cannot continue from where the file ends, which is what a complete file gets
    if p.returncode not in (0, 33) or not os.path.exists(part):
        raise Failure("could not download %s (curl exit %d); check the network and run again" % (url, p.returncode))
    if sha256(part) != sha:
        os.remove(part)  # a broken or different file: start again next time
        raise Failure("%s does not have the expected SHA-256; deleted, run again to download it again" % name)
    os.replace(part, dest)
    return dest


class Redist:
    def __init__(self, cfg, out, steam, extra=(), cache=None, allow_download=True, progress=None):
        self.cfg, self.out, self.steam = cfg, out, steam
        self.extra = list(extra)
        self.cache = cache or os.path.join(out, ".cache")
        self.allow_download = allow_download
        self.progress = progress or Progress()
        self.label = "Getting Microsoft libraries"

    def final_sha(self, key):
        if key == "d3dcompiler_42" and self.cfg.get("d3dcompiler_42_base"):
            return self.cfg["d3dcompiler_42_rebased_sha256"]
        return self.cfg[key + "_sha256"]

    def missing(self):
        return [d for d in DLLS if not has_sha(os.path.join(self.out, d[0]), self.final_sha(d[1]))]

    def install(self, dll, path, source):
        """Check an extracted DLL and write it to OUTDIR (rebased if it must be). False if it is not the one."""
        name, key = dll[0], dll[1]
        if sha256(path) != self.cfg[key + "_sha256"]:
            self.progress.say("  %s from %s is not the expected version; skipped" % (name, source))
            return False
        with open(path, "rb") as f:
            data = f.read()
        base = self.cfg.get("d3dcompiler_42_base") if key == "d3dcompiler_42" else 0
        if base:
            data = rebase(data, base)[0]
        tmp = os.path.join(self.out, name + ".tmp")
        with open(tmp, "wb") as f:
            f.write(data)
        os.replace(tmp, os.path.join(self.out, name))
        self.progress.say("  %s from %s%s" % (name, source, " (rebased to 0x%x)" % base if base else ""))
        return True

    def take(self, files, source):
        """Install whichever still-missing DLLs files ({lower package name: path}) hold."""
        for dll in self.missing():
            p = files.get(dll[3].lower())
            if p:
                self.install(dll, p, source)

    def scratch(self):
        os.makedirs(self.cache, exist_ok=True)
        return tempfile.mkdtemp(prefix="x-", dir=self.cache)

    def from_vc_exe(self, exe, source):
        tmp = self.scratch()
        cab = untar(exe, tmp, [VC_CAB]).get(VC_CAB)
        if cab:
            self.take(untar(cab, os.path.join(tmp, "vc"), [d[3] for d in DLLS if d[2] == "vc"]), source)
        shutil.rmtree(tmp, ignore_errors=True)

    def from_cabs(self, cabs, source):
        tmp = self.scratch()
        for i, cab in enumerate(cabs):
            self.take(untar(cab, os.path.join(tmp, str(i)), [d[3] for d in DLLS if d[2] == "dx"]), source)
        shutil.rmtree(tmp, ignore_errors=True)

    def from_dx_exe(self, exe, source):
        tmp = self.scratch()
        cabs = untar(exe, tmp, [os.path.basename(self.cfg["steam_d3dx9_cab"]),
                                os.path.basename(self.cfg["steam_d3dcompiler_cab"])])
        self.from_cabs(sorted(cabs.values()), source)
        shutil.rmtree(tmp, ignore_errors=True)

    def need(self, package):
        return any(d[2] == package for d in self.missing())

    def local_sources(self):
        """(kind, path, description) of every local candidate, in order of preference."""
        cfg = self.cfg
        dx_cabs = [os.path.basename(cfg["steam_d3dx9_cab"]), os.path.basename(cfg["steam_d3dcompiler_cab"])]
        # 1. Steam's Common Redistributables, in every library
        for lib in library_folders(self.steam):
            redist = os.path.join(lib, COMMON_REDIST)
            yield "vc", os.path.join(redist, cfg["steam_vcredist"]), "Steam's Common Redistributables"
            cabs = [os.path.join(redist, cfg[k]) for k in ("steam_d3dx9_cab", "steam_d3dcompiler_cab")]
            yield "cabs", cabs, "Steam's Common Redistributables"
        # 3. the redistributables' Steam depots, downloaded with the console
        content = os.path.join(self.steam, "steamapps", "content", "app_%d" % cfg["steam_redist_app"])
        for p in find_files(os.path.join(content, "depot_%d" % cfg["steam_vcredist_depot"]), [VC_EXE]).get(VC_EXE.lower(), []):
            yield "vc", p, "Steam depot %d" % cfg["steam_vcredist_depot"]
        found = find_files(os.path.join(content, "depot_%d" % cfg["steam_dxredist_depot"]), dx_cabs + [DX_EXE])
        yield "cabs", sum((found.get(c.lower(), []) for c in dx_cabs), []), "Steam depot %d" % cfg["steam_dxredist_depot"]
        for p in found.get(DX_EXE.lower(), []):
            yield "dx", p, "Steam depot %d" % cfg["steam_dxredist_depot"]
        # folders the user names: the DLLs themselves, or any of the packages
        for top in self.extra:
            found = find_files(top, [d[0] for d in DLLS] + [d[3] for d in DLLS] + dx_cabs + [VC_EXE, VC_CAB, DX_EXE])
            yield "files", {k: v[0] for k, v in found.items()}, top
            for p in found.get(VC_CAB.lower(), []):
                yield "vccab", p, top
            for p in found.get(VC_EXE.lower(), []):
                yield "vc", p, top
            yield "cabs", sum((found.get(c.lower(), []) for c in dx_cabs), []), top
            for p in found.get(DX_EXE.lower(), []):
                yield "dx", p, top
        # 2. an earlier download (checked against its pinned hash)
        for kind, exe, key in (("vc", VC_EXE, "vcredist_sha256"), ("dx", DX_EXE, "dxredist_sha256")):
            p = os.path.join(self.cache, exe)
            if has_sha(p, cfg[key]):
                yield kind, p, "an earlier download"

    def run(self):
        os.makedirs(self.out, exist_ok=True)
        missing = self.missing()
        have = [d[0] for d in DLLS if d not in missing]
        if have:
            self.progress.say("Already in place: " + ", ".join(have))
        for kind, path, source in self.local_sources():
            if not self.missing():
                break
            if kind == "files":
                files = {}
                for dll in DLLS:  # the DLLs by their own names, else by their name in the package
                    p = path.get(dll[0].lower()) or path.get(dll[3].lower())
                    if p:
                        files[dll[3].lower()] = p
                self.take(files, source)
            elif kind == "vc" and self.need("vc") and os.path.isfile(path):
                self.from_vc_exe(path, source)
            elif kind == "vccab" and self.need("vc"):
                tmp = self.scratch()
                self.take(untar(path, os.path.join(tmp, "vc"), [d[3] for d in DLLS if d[2] == "vc"]), source)
                shutil.rmtree(tmp, ignore_errors=True)
            elif kind == "cabs" and self.need("dx") and path and all(os.path.isfile(p) for p in path):
                self.from_cabs(path, source)
            elif kind == "dx" and self.need("dx") and os.path.isfile(path):
                self.from_dx_exe(path, source)
        for package, exe, key in (("vc", VC_EXE, "vcredist"), ("dx", DX_EXE, "dxredist")):
            if not self.need(package):
                continue
            if not self.allow_download:
                break
            os.makedirs(self.cache, exist_ok=True)
            p = download(self.cfg[key + "_url"], self.cfg[key + "_sha256"], os.path.join(self.cache, exe),
                         self.progress, self.label)
            (self.from_vc_exe if package == "vc" else self.from_dx_exe)(p, "Microsoft's %s" % exe)
        left = [d[0] for d in self.missing()]
        if left:
            raise Failure("not found: %s. Install the Steamworks Common Redistributables in Steam, or run again "
                          "with the network on, or point --from at a folder with %s or %s"
                          % (", ".join(left), VC_EXE, DX_EXE))
        shutil.rmtree(self.cache, ignore_errors=True)
        self.progress(1.0, "%s… done" % self.label)
        self.progress.say("Microsoft libraries ready in %s" % self.out)


# --- the game ----------------------------------------------------------------------------------------------

# The native (arm64) Steam client keeps its own steamapps inside its bundle, and download_depot writes there;
# older clients wrote to <Steam>/steamapps/content.
APP_BUNDLE = os.path.join("Steam.AppBundle", "Steam", "Contents", "MacOS")


def content_dirs(steam):
    return [os.path.join(steam, APP_BUNDLE, "steamapps", "content"), os.path.join(steam, "steamapps", "content")]


def download_content_dir(steam):
    """The content folder the next download_depot writes to."""
    return content_dirs(steam)[0 if os.path.isdir(os.path.join(steam, APP_BUNDLE)) else 1]


def depot_dirs(cfg, steam):
    sub = os.path.join("app_%d" % cfg["steam_app"], "depot_%d" % cfg["steam_depot"])
    return [os.path.join(c, sub) for c in content_dirs(steam)]


def depot_dir(cfg, steam):
    """The depot folder: the one that exists, else where the next download goes."""
    for d in depot_dirs(cfg, steam):
        if os.path.isdir(d):
            return d
    return os.path.join(download_content_dir(steam), "app_%d" % cfg["steam_app"], "depot_%d" % cfg["steam_depot"])


def locate_default(cfg, steam):
    """locate() over every depot folder Steam may have used: the supported build if one has it. An exe of either
    kind while Steam's log shows a download of the depot that has not finished is "unfinished": Steam makes
    every file at its full size first, so an unfinished exe has the right size and the wrong contents, and a
    finished exe says nothing about the files still being filled."""
    found = [locate(cfg, d) for d in depot_dirs(cfg, steam)]
    rank = {"ok": 0, "different": 1, "incomplete": 2, "missing": 3}
    best = min(found, key=lambda loc: rank[loc.status])
    if best.status == "missing":
        return locate(cfg, depot_dir(cfg, steam))
    if best.status in ("ok", "different") and (download_unfinished(steam, cfg["steam_depot"])
                                               or (best.status == "ok" and depot_short(cfg, best.folder))):
        best.status = "unfinished"
    return best


def depot_short(cfg, folder):
    """A depot folder holds less than the whole depot: a finished one has every byte of depot_disk_bytes on
    disk (checked on both clean downloads), and Steam makes the files at full size long before they are filled,
    so a good exe says nothing about the rest. Half a percent of room for other file systems."""
    return folder_bytes(folder)[0] < cfg.get("depot_disk_bytes", 0) * 0.995


def download_unfinished(steam, depot):
    """Steam's logs show a download of depot that started after the last one finished."""
    log = SteamLog(steam, depot, from_start=True).poll()
    return log.started and not log.complete


def free_bytes(path):
    while path and not os.path.exists(path):
        path = os.path.dirname(path)
    st = os.statvfs(path or "/")
    return st.f_bavail * st.f_frsize


def space(cfg, steam, build_dir=None):
    """(free, need, folder): free space where the download goes, and what it needs (the depot's size on disk
    and a margin, less what is already there; with build_dir, also the build, when that is on the same disk). A
    link to a drive that is not connected has no room."""
    content = download_content_dir(steam)
    target = os.path.realpath(content)
    depot = os.path.join(content, "app_%d" % cfg["steam_app"], "depot_%d" % cfg["steam_depot"])
    have = folder_bytes(depot)[0] if os.path.isdir(depot) else 0
    need = max(cfg["depot_disk_bytes"] - have, 0) + cfg["disk_margin_bytes"]
    need += dlc_room(cfg, steam)
    if os.path.islink(content) and not os.path.isdir(content):
        return 0, need, target
    if build_dir and same_disk(target, build_dir):
        need += build_left(cfg, build_dir)
    # On the startup disk everything else has to fit next to it: the build's swap, and room for macOS. A depot
    # that only just fits there fills the disk.
    if on_startup_disk(target):
        need += swap_room(cfg) + max(cfg.get("startup_margin_bytes", 0) - cfg["disk_margin_bytes"], 0)
    return free_bytes(target), need, target


def build_left(cfg, build_dir):
    return max(cfg["build_disk_bytes"] - (lp_build.du(build_dir) if os.path.isdir(build_dir) else 0), 0)


def memory_bytes():
    try:
        return int(subprocess.run(["/usr/sbin/sysctl", "-n", "hw.memsize"], capture_output=True, text=True).stdout)
    except (OSError, ValueError):
        return 0


def swap_room(cfg):
    """What the translation's peak doesn't find in memory goes to swap, in the startup disk's free space."""
    return max(cfg.get("build_peak_memory_bytes", 0) - memory_bytes(), 0)


def startup_folder():
    return "/System/Volumes/Data" if os.path.isdir("/System/Volumes/Data") else "/"


def on_startup_disk(path):
    """On the startup disk: macOS's system volume and the data volume are two devices of it."""
    dev = os.stat(existing(path)).st_dev
    return dev in (os.stat("/").st_dev, os.stat(startup_folder()).st_dev)


def startup_space(cfg, build_dir):
    """(free, need, words): the room the build wants on the startup disk, the download apart."""
    swap = swap_room(cfg)
    need = swap + cfg.get("startup_margin_bytes", 0)
    if on_startup_disk(build_dir):
        need += build_left(cfg, build_dir)
    free = free_bytes(startup_folder())
    ram = memory_bytes()
    why = (" The build's translation uses about %s of memory, and this Mac has %s: macOS keeps the rest on the "
           "startup disk while it runs." % (human(cfg["build_peak_memory_bytes"]), human(ram))) if swap else ""
    return free, need, "The startup disk has %s free, and the build wants about %s there.%s" % (human(free), human(need), why)


def dlc_room(cfg, steam):
    """Room for the DLC still to come: what is owned at the average size, when the signed-in account's ticket is on
    disk, else all of it as a Mac library holds it."""
    account = gamefiles.steam_connection(steam)[1]
    owned = gamefiles.account_dlc(steam, account) if account else None
    if owned is None:
        return cfg.get("dlc_disk_bytes", 0)
    return min(len(owned) * cfg.get("dlc_average_bytes", 0), cfg.get("dlc_disk_bytes", 0))


def existing(path):
    while path and not os.path.exists(path):
        path = os.path.dirname(path)
    return path or "/"


def same_disk(a, b):
    return os.stat(existing(a)).st_dev == os.stat(existing(b)).st_dev


def describe_space(free, need, where):
    if not free and not os.path.isdir(where):
        return "Steam's download folder is %s, on a drive that isn't connected." % where
    return "%s free in %s; about %s is needed%s" % (human(free), where, human(need), "" if free >= need else ": not enough")


def download_note():
    return os.path.join(lp_build.default_home(), "download-dir")


def redirect(steam, folder):
    """Make Steam's content folder a link to folder (Steam follows it), so the download goes to another disk.
    What is in a real content folder moves to folder (a stopped download continues there)."""
    content = download_content_dir(steam)
    folder = os.path.abspath(os.path.expanduser(folder))
    if os.path.ismount(folder):  # a whole disk was chosen: not all over its top folder
        folder = os.path.join(folder, gamefiles.FOLDER, "steam-content")
    os.makedirs(folder, exist_ok=True)
    if os.path.islink(content):
        if os.path.realpath(content) == os.path.realpath(folder):
            remember_download(folder)
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
    remember_download(folder)
    return content


def remember_download(folder):
    os.makedirs(os.path.dirname(download_note()), exist_ok=True)
    with open(download_note(), "w") as f:
        f.write(folder + "\n")


def restore_redirect(steam):
    """A Steam update can drop the link game-redirect made, with the download still on the other disk: link it
    again, when that folder is there and Steam has not started a new content folder of its own."""
    try:
        with open(download_note()) as f:
            folder = f.read().strip()
    except OSError:
        return
    content = download_content_dir(steam)
    if not folder or not os.path.isdir(folder) or os.path.isdir(content):
        return
    if os.path.islink(content):  # a link to nowhere: the folder it led to is not the one noted
        os.remove(content)
    redirect(steam, folder)


def find_exe(cfg, folder):
    """The game's exe in folder (or folder itself, or one folder down), case-insensitive; None if absent."""
    name = cfg["exe_name"].lower()
    if os.path.isfile(folder):
        return folder if os.path.basename(folder).lower() == name else None
    if not os.path.isdir(folder):
        return None
    try:
        entries = sorted(os.listdir(folder))
    except OSError:
        return None
    for f in entries:
        if f.lower() == name and os.path.isfile(os.path.join(folder, f)):
            return os.path.join(folder, f)
    for d in entries:
        sub = os.path.join(folder, d)
        if os.path.isdir(sub) and not d.startswith("."):
            for f in sorted(os.listdir(sub)):
                if f.lower() == name and os.path.isfile(os.path.join(sub, f)):
                    return os.path.join(sub, f)
    return None


class Located:
    """status: "ok" (the pinned build), "missing" (no exe), "incomplete" (smaller than the pinned exe, as
    while Steam writes it) or "different" (another build)."""

    def __init__(self, status, folder, exe=None, size=0, sha=""):
        self.status, self.folder, self.exe, self.size, self.sha = status, folder, exe, size, sha


def locate(cfg, folder, _cache={}):
    exe = find_exe(cfg, folder)
    if not exe:
        return Located("missing", folder)
    st = os.stat(exe)
    game = os.path.dirname(exe)
    if st.st_size < cfg["exe_size"]:
        return Located("incomplete", game, exe, st.st_size)
    key = (exe, st.st_size, st.st_mtime_ns)
    if key not in _cache:
        _cache[key] = sha256(exe)
    sha = _cache[key]
    return Located("ok" if sha == cfg["exe_sha256"] else "different", game, exe, st.st_size, sha)


def describe(cfg, loc):
    if loc.status == "ok":
        return "Found the Windows game in %s (the supported build)." % loc.folder
    if loc.status == "missing":
        return "No %s in %s." % (cfg["exe_name"], loc.folder)
    if loc.status == "unfinished":
        return ("Steam's download of the game into %s has not finished (Steam may have quit); start the download "
                "again to continue it." % loc.folder)
    if loc.status == "incomplete":
        return "%s in %s is incomplete (%s of %s)." % (cfg["exe_name"], loc.folder, human(loc.size), human(cfg["exe_size"]))
    return ("%s in %s is a different build of the game from the one RS Re-ARMed supports, so it cannot be used "
            "(found: SHA-256 %s, %d bytes; supported: %s, %d bytes)."
            % (cfg["exe_name"], loc.folder, loc.sha, loc.size, cfg["exe_sha256"], cfg["exe_size"]))


def game_command(cfg):
    cmd = "download_depot %d %d" % (cfg["steam_app"], cfg["steam_depot"])
    return cmd + (" " + cfg["depot_manifest"] if cfg.get("depot_manifest") else "")


HARVEST_SRC = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "harvest")
UNPACK_DIR = os.environ.get("LP_UNPACK_DIR") or os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "unpack")
LP_IMAGE = os.path.join(ROOT, "win-x86", "image", "lp_image.py")


HARVEST_FILES = ["rsharvest2.exe", "rsharvest2.exe.config", "harvest-game.bat", "harvest-game.ps1", "README.md"]
HARVEST_SOURCE = ["rsharvest2.cs", "rsharvest2.csproj", "build.sh"]


def harvest_sha256():
    """The SHA-256 of the committed rsharvest2.exe, as win-x86/harvest/README.md records it."""
    with open(os.path.join(HARVEST_SRC, "README.md"), encoding="utf-8") as f:
        m = re.search(r"^SHA-256 of rsharvest2\.exe: `([0-9a-f]{64})`", f.read(), re.M)
    return m.group(1) if m else None


def game_unpack(exe_path, out_path, seed_path=None, progress=None):
    """Decompress the VMProtect-packed exe into a flat image, using the shipped seed.

    The seed is a ~190 KB file of VMProtect VM scratch and relocation offsets — no game code, no third-party code.
    The 33 MB of game content comes from the user's own exe. Needs unicorn + pefile.
    """
    if seed_path is None:
        seed_path = os.path.join(UNPACK_DIR, "seed.bin")
    if not os.path.isfile(seed_path):
        raise Failure("no seed.bin found at %s: the offline unpacker needs a seed for this "
                      "build of the game (see win-x86/unpack/README.md)" % seed_path)
    if not os.path.isfile(exe_path):
        raise Failure("exe not found: %s" % exe_path)
    try:
        import importlib.util
        spec = importlib.util.spec_from_file_location("lp_unpack", os.path.join(UNPACK_DIR, "lp_unpack.py"))
        lp_unpack = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(lp_unpack)
    except ImportError as e:
        raise Failure("the offline unpacker needs unicorn and pefile: pip install unicorn pefile (%s)" % e)
    except FileNotFoundError:
        raise Failure("win-x86/unpack/lp_unpack.py not found at %s" % UNPACK_DIR)
    if progress:
        progress.say("Decompressing %s..." % os.path.basename(exe_path))
    os.makedirs(os.path.dirname(os.path.abspath(out_path)), exist_ok=True)
    img = lp_unpack.write_image(exe_path, seed_path, out_path)
    import hashlib
    sha = hashlib.sha256(img).hexdigest()
    return out_path, len(img), sha


def seed_file(arg=None):
    """The unpacker's seed: ARG, $LP_SEED, else seed.bin next to lp_unpack.py (the release ships one); None
    without one, and then the code image comes from a capture."""
    for p in (arg, os.environ.get("LP_SEED"), os.path.join(UNPACK_DIR, "seed.bin")):
        if p and os.path.isfile(p):
            return p
    return None


def tool(py, script, *args):
    """A build tool in the build's Python; its output goes to the log only when it fails."""
    r = subprocess.run([py, script] + list(args), stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                       errors="replace")
    if r.returncode:
        raise Failure("%s failed: %s" % (os.path.basename(script), r.stdout.strip()[-600:]))


def game_image(cfg, game, out, py, seed=None, report=None, progress=None):
    """The code image with nothing from the user: the exe of the game folder GAME unpacked with the seed, and
    the image built from that. Written last, so a stopped run leaves no half image; a run takes a few minutes."""
    progress = progress or Progress()
    label = "Code image"
    seed = seed_file(seed)
    if not seed:
        raise Failure("no seed.bin: this copy of RS Re-ARMed has no offline unpacker for the game (the released "
                      "installer has one); make a capture instead (win-x86/harvest)")
    loc = locate(cfg, game)
    if loc.status != "ok":
        raise Failure(describe(cfg, loc), 2 if loc.status == "different" else 1)
    out = os.path.abspath(out)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    flat = out + ".unpacked"
    try:
        progress(0.05, "%s… unpacking the game's code" % label)
        tool(py, os.path.join(UNPACK_DIR, "lp_unpack.py"), loc.exe, seed, flat)
        progress(0.6, "%s… building the image" % label)
        tool(py, LP_IMAGE, flat, out, *(["--report", os.path.abspath(report)] if report else []))
    finally:
        for f in (flat, flat + ".relocs"):
            if os.path.exists(f):
                os.remove(f)
    progress(1.0, "%s… done" % label)
    return out


def harvest_export(dest, progress=None, rebuild=False):
    """Assemble, in `dest`, the folder a user copies to a Windows PC to capture their own game.

    Copies the prebuilt harvester (rsharvest2.exe and its .config, committed in win-x86/harvest), the
    one-click wrapper (harvest-game.bat/.ps1), the README and the source with its build script. Nothing
    is built and no .NET is needed: the PC has .NET Framework 4.8 built in. With `rebuild` (or
    LP_HARVEST_REBUILD=1) the exe is rebuilt from the source with the .NET SDK instead. We ship no game
    code; the capture this tool makes is the user's own game's data. See win-x86/harvest/README.md.
    Returns (dest, rebuilt)."""
    if not os.path.isdir(HARVEST_SRC):
        raise Failure("the harvester is missing (%s)" % HARVEST_SRC)
    os.makedirs(dest, exist_ok=True)
    for name in HARVEST_FILES + HARVEST_SOURCE:
        s = os.path.join(HARVEST_SRC, name)
        if not os.path.isfile(s):
            raise Failure("the harvester is incomplete: %s is missing" % s)
        shutil.copy2(s, os.path.join(dest, name))
    rebuilt = False
    if rebuild or os.environ.get("LP_HARVEST_REBUILD") == "1":
        if not shutil.which("dotnet"):
            raise Failure("rebuilding the harvester needs the .NET SDK (https://dotnet.microsoft.com/download); "
                          "without LP_HARVEST_REBUILD=1 the prebuilt rsharvest2.exe is used")
        if progress:
            progress.say("Rebuilding rsharvest2.exe from its source...")
        with tempfile.TemporaryDirectory() as tmp:
            r = subprocess.run(["dotnet", "publish", os.path.join(HARVEST_SRC, "rsharvest2.csproj"),
                                "-c", "Release", "-o", tmp], stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
            if r.returncode != 0 or not os.path.isfile(os.path.join(tmp, "rsharvest2.exe")):
                raise Failure("could not rebuild rsharvest2.exe: %s" % r.stderr.decode(errors="replace")[-500:])
            for name in ("rsharvest2.exe", "rsharvest2.exe.config"):
                shutil.copy2(os.path.join(tmp, name), os.path.join(dest, name))
        rebuilt = True
    return dest, rebuilt


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


UNITS = {"b": 1, "bytes": 1, "kb": 1 << 10, "mb": 1 << 20, "gb": 1 << 30}
RATE_UNITS = {"bps": 1 / 8, "kbps": 1e3 / 8, "mbps": 1e6 / 8, "gbps": 1e9 / 8,
              "b/s": 1, "kb/s": 1 << 10, "mb/s": 1 << 20, "gb/s": 1 << 30}
STAMP = re.compile(r"^\[(\d{4})-(\d\d)-(\d\d) (\d\d):(\d\d):(\d\d)\]")


class SteamLog:
    """What the Steam client logs about one depot download from now on (content_log.txt, console_log.txt):
    the download size, the bytes downloaded (from the download rate it logs), the end and the manifest, or
    a failure."""

    FILES = ("content_log.txt", "console_log.txt")

    def __init__(self, steam, depot, from_start=False):
        # from_start: the whole log, the one before Steam last started first
        files = [f.replace(".txt", ".previous.txt") for f in self.FILES] + list(self.FILES) if from_start else self.FILES
        self.paths = [os.path.join(d, "logs", f) for d in (steam, os.path.join(steam, APP_BUNDLE)) for f in files]
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
    for d in (steam, os.path.join(steam, APP_BUNDLE)):
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
    do. wait() calls tick() on every round; failed() takes Steam's failure line. The running Steam takes the
    console command on its command line, so the only steps left to the user are the ones only they can do:
    installing Steam and signing in. A second command for a download that runs makes Steam fetch every chunk
    twice (it ends well, but with thousands of assertions in its log), so one goes out only when none runs."""

    PASTE_AFTER = 30.0   # Steam hasn't reacted to the command by then: the console and the clipboard
    ADOPT = 60.0         # a download Steam says is running, from an earlier run: how long it may be silent
    STALLED = 180.0      # nothing written for this long: say so
    RESEND = 600.0       # and for this long: send the command again (Steam may have dropped it)
    RETRY_AFTER = 30.0   # after a failure of Steam's
    RETRIES = 3

    def __init__(self, cfg, steam, guide, gate=None, send=None, paste=None, clock=time.monotonic,
                 running=download_running):
        self.cfg, self.steam, self.guide, self.clock = cfg, steam, guide, clock
        self.gate = gate or gamefiles.SteamGate(steam, gamefiles.GAME_LP, guide, clock=clock)
        self.command = game_command(cfg)
        self.send = send or (lambda: gamefiles.steam_command(steam, *self.command.split()))
        self.paste = paste or self.open_console
        self.running = running
        self.sent = self.pasting = self.adopted = self.retry = self.looked = False
        self.sent_at = 0.0
        self.bad_account = None
        self.fails = 0
        self.retry_at = None

    def open_console(self):
        subprocess.run(["/usr/bin/pbcopy"], input=self.command.encode(), check=False)
        gamefiles.steam_open("steam://open/console")

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
                self.guide("wrong-account", name=st.name, game=gamefiles.GAME_LP)
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
        if not self.looked and self.running(self.steam, self.cfg["steam_depot"]):
            self.looked = self.adopted = log.started = True
            self.guide("downloading")
            return
        self.looked = True
        free, need, where = space(self.cfg, self.steam)
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


def wait(cfg, folder, steam, progress, timeout=0, interval=2.0, settle=90.0, clock=time.monotonic, sleep=time.sleep,
         driver=None):
    """Wait until the depot download has finished and the exe checks out (folder: the depot folder, default
    whichever of Steam's depot folders appears). driver (a DepotDownload) starts the download and looks after it;
    without one the user has started it. Returns Located."""
    log = SteamLog(steam, cfg["steam_depot"])
    label = "Downloading the Windows game"
    start = clock()
    last_change, last_seen = start, None
    last_have, last_at, speed = 0, start, 0.0
    said_waiting = False
    while True:
        log.poll()
        here = folder or depot_dir(cfg, steam)
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
            if cfg.get("depot_disk_bytes"):
                part = min(max(part, have / cfg["depot_disk_bytes"]), 1.0)
            done = part * log.expected
            rate = " (%.1f MB/s)" % (log.rate / (1 << 20)) if log.rate else ""
            if speed > 1 << 16:
                rate = " (%.1f MB/s, about %d min left)" % (speed / (1 << 20), (1 - part) * cfg["depot_disk_bytes"]
                                                            / speed / 60 + 1)
            progress(part * 0.99, "%s… %s of %s%s" % (label, human(done), human(log.expected), rate))
        elif have and cfg.get("depot_disk_bytes"):
            progress(have / cfg["depot_disk_bytes"] * 0.99,
                     "%s… about %s of %s" % (label, human(have), human(cfg["depot_disk_bytes"])))
        elif have:
            progress(0, "%s… %s so far" % (label, human(have)))
        elif not said_waiting:
            progress(0, "%s… waiting for Steam to start" % label)
            said_waiting = True
        if log.failed:
            if not driver or not driver.failed(log.failed):
                raise Failure("Steam says the download failed: %s" % log.failed)
            log.failed = None
        loc = locate(cfg, here)
        if log.complete:
            files, manifest = log.complete
            what = ", ".join(x for x in ("%d files" % files if files else "",
                                         "manifest %s" % manifest if manifest else "") if x)
            progress.say("Steam finished the download%s." % (" (%s)" % what if what else ""))
            if loc.status == "ok" and manifest and manifest != cfg.get("depot_manifest"):
                progress.say("Manifest %s holds the supported build (depot_manifest in tools/targets/win-x86/target.toml)." % manifest)
            if loc.status != "ok":
                raise Failure(describe(cfg, loc), 2 if loc.status == "different" else 1)
            break
        # Without Steam's word (its log elsewhere, or in a format this does not know): the exe checks out and
        # Steam has written nothing for `settle` seconds. File counts and sizes do not tell: Steam makes every
        # file at its full size first.
        settled = clock() - last_change >= settle
        if loc.status == "ok" and settled and not driver and not download_running(steam, cfg["steam_depot"]) \
                and not depot_short(cfg, here):
            progress.say("Steam's log does not say the download finished; nothing has changed for %d s, so it "
                         "is taken as finished." % settle)
            break
        # Steam makes the exe at its full size first, so a stalled or waiting download looks like this too.
        if loc.status == "different" and settled and not driver and not download_running(steam, cfg["steam_depot"]):
            raise Failure(describe(cfg, loc), 2)
        if timeout and clock() - start >= timeout:
            raise Failure("timed out waiting for the download (%s in %s so far)" % (human(have), here), 3)
        sleep(interval)
    progress(1.0, "%s… done" % label)
    return loc


# --- the app's own copy of the game files (gamefiles.py) --------------------------------------------------

mac_dlc_folders = gamefiles.mac_dlc_folders


def game_dest(dest=None):
    return os.path.abspath(dest or os.path.join(lp_build.default_home(), "game"))


def game_copy(cfg, steam, game, dest=None, dlc_from=None, progress=None, **kw):
    """Copy the supported build in game (checked as game-locate does) and the PC DLC to dest."""
    loc = locate(cfg, game)
    if loc.status != "ok":
        raise Failure(describe(cfg, loc), 2 if loc.status == "different" else 1)
    dlc = gamefiles.lp_dlc_folders(steam) if dlc_from is None else dlc_from
    try:
        gamefiles.Copier("learn-play", loc.folder, game_dest(dest), dlc, steam, progress, **kw).run()
    except gamefiles.Failure as e:
        raise Failure(str(e))
    return game_dest(dest)


# --- the title logo (tools/loadscreen) ---------------------------------------------------------------------

def title_logo(game, say=print):
    """The "Re-ARMed 2026" title logo, static.psarc.rearmed in the copy GAME, as the Mac build makes it: from the
    copy's static.psarc.original, once, unless RS_REARMED=0. Written to the copy and not to the game folder, which
    Steam may clear. Not made (its Python packages need the network once): the game shows its own logo."""
    out = os.path.join(game, gamefiles.STATIC_REARMED)
    if (os.environ.get("RS_REARMED") == "0" or os.path.isfile(out)
            or not os.path.isfile(os.path.join(game, gamefiles.STATIC_ORIGINAL))):
        return
    say("Building the Re-ARMed title logo")
    r = subprocess.run(["bash", LOADSCREEN, out], env=dict(os.environ, RS_GAME_DIR=game), stdout=subprocess.PIPE,
                       stderr=subprocess.STDOUT, text=True)
    if r.returncode:
        say("The title logo was not made, the game shows its own: %s" % r.stdout.strip()[-300:])


# --- command line ------------------------------------------------------------------------------------------

def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0],
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--config", default=None)
    ap.add_argument("--steam", default=None)
    ap.add_argument("--progress-lines", action="store_true")
    sub = ap.add_subparsers(dest="cmd")
    p = sub.add_parser("redist")
    p.add_argument("out")
    p.add_argument("--from", dest="extra", action="append", default=[], metavar="DIR")
    p.add_argument("--no-download", action="store_true")
    p = sub.add_parser("game-locate")
    p.add_argument("folder", nargs="?")
    p.add_argument("--path-only", action="store_true")
    sub.add_parser("game-command")
    p = sub.add_parser("game-space")
    p.add_argument("--with-build", default=None, metavar="WORK")
    p = sub.add_parser("startup-space")
    p.add_argument("work")
    p = sub.add_parser("game-redirect")
    p.add_argument("folder")
    p = sub.add_parser("game-wait")
    p.add_argument("folder", nargs="?")
    p.add_argument("--timeout", type=float, default=0)
    p.add_argument("--interval", type=float, default=2.0)
    p.add_argument("--settle", type=float, default=90.0)
    p = sub.add_parser("game-download")
    p.add_argument("--timeout", type=float, default=0)
    p.add_argument("--interval", type=float, default=2.0)
    sub.add_parser("seed-check")
    p = sub.add_parser("game-image")
    p.add_argument("game")
    p.add_argument("out")
    p.add_argument("--py", default=sys.executable)
    p.add_argument("--seed", default=None)
    p.add_argument("--report", default=None)
    p = sub.add_parser("game-unpack")
    p.add_argument("exe")
    p.add_argument("out")
    p.add_argument("--seed", default=None)
    p = sub.add_parser("harvest-export")
    p.add_argument("dir")
    p.add_argument("--rebuild", action="store_true")
    p = sub.add_parser("build")
    p.add_argument("work")
    p.add_argument("--image", required=True)
    p.add_argument("--dlls", required=True)
    p.add_argument("--game", required=True)
    p.add_argument("--py", default=sys.executable)
    p.add_argument("--jobs", type=int, default=0)
    p = sub.add_parser("app-install")
    p.add_argument("work")
    p.add_argument("--game", required=True)
    p.add_argument("--app", default=None)
    p = sub.add_parser("game-copy")
    p.add_argument("game")
    p.add_argument("--dest", default=None)
    p.add_argument("--dlc-from", action="append", default=None, metavar="DIR")
    p = sub.add_parser("game-refresh")
    p.add_argument("--dest", default=None)
    p = sub.add_parser("build-clean")
    p.add_argument("work")
    p.add_argument("--all", action="store_true")
    sub.add_parser("build-estimate")
    a = ap.parse_args(argv)
    if not a.cmd:
        ap.print_usage(sys.stderr)
        return 64
    cfg = load_config(a.config)
    steam = steam_dir(a.steam)
    progress = Progress(a.progress_lines)
    try:
        if a.cmd == "redist":
            Redist(cfg, os.path.abspath(a.out), steam, a.extra, allow_download=not a.no_download,
                   progress=progress).run()
        elif a.cmd == "game-command":
            print(game_command(cfg))
        elif a.cmd == "game-space":
            restore_redirect(steam)
            free, need, where = space(cfg, steam, a.with_build and os.path.abspath(a.with_build))
            print(describe_space(free, need, where))
            return 0 if free >= need else 1
        elif a.cmd == "startup-space":
            free, need, words = startup_space(cfg, os.path.abspath(a.work))
            print(words)
            return 0 if free >= need else 1
        elif a.cmd == "game-redirect":
            content = redirect(steam, a.folder)
            print("Steam's downloads now go to %s (%s links there)" % (os.path.realpath(content), content))
        elif a.cmd == "game-locate":
            if not a.folder:
                restore_redirect(steam)
            loc = locate(cfg, a.folder) if a.folder else locate_default(cfg, steam)
            if a.path_only:
                if loc.status == "ok":
                    print(loc.folder)
                else:
                    print(describe(cfg, loc), file=sys.stderr)
            else:
                print(describe(cfg, loc))
            return {"ok": 0, "different": 2}.get(loc.status, 1)
        elif a.cmd == "game-wait":
            loc = wait(cfg, a.folder, steam, progress, a.timeout, a.interval, a.settle)
            progress.say(describe(cfg, loc))
        elif a.cmd == "game-download":
            restore_redirect(steam)
            guide = gamefiles.Guide(a.progress_lines)
            driver = DepotDownload(cfg, steam, guide)
            signal.signal(signal.SIGUSR1, driver.try_again)
            loc = wait(cfg, None, steam, progress, a.timeout, a.interval, driver=driver)
            guide.clear()
            progress.say(describe(cfg, loc))
        elif a.cmd == "seed-check":
            seed = seed_file()
            print(seed or "no seed.bin")
            return 0 if seed else 1
        elif a.cmd == "game-image":
            out = game_image(cfg, a.game, a.out, a.py, a.seed, a.report, progress)
            progress.say("Code image ready: %s" % out)
        elif a.cmd == "game-unpack":
            out, size, sha = game_unpack(os.path.abspath(a.exe), os.path.abspath(a.out), a.seed, progress)
            progress.say("Wrote %s (%d bytes, sha256=%s)" % (out, size, sha))
        elif a.cmd == "harvest-export":
            dest, rebuilt = harvest_export(os.path.abspath(a.dir), progress, a.rebuild)
            progress.say("Harvester%s written to %s" % (" (rebuilt from source)" if rebuilt else "", dest))
        elif a.cmd == "build-estimate":
            print(build_estimate(cfg))
        elif a.cmd == "build":
            build(cfg, a, progress)
        elif a.cmd == "app-install":
            app, home = lp_build.install_app(os.path.abspath(a.work), os.path.abspath(a.game), a.app,
                                             say=progress.say)
            progress.say("Installed: %s" % app)
            progress.say("Your settings, saves and logs: %s" % home)
        elif a.cmd == "game-copy":
            game_copy(cfg, steam, a.game, a.dest, a.dlc_from, progress)
        elif a.cmd == "game-refresh":
            try:
                gamefiles.refresh(game_dest(a.dest), progress)
            except gamefiles.Failure as e:
                raise Failure(str(e))
        elif a.cmd == "build-clean":
            freed = lp_build.clean(os.path.abspath(a.work), a.all)
            progress.say("Build files deleted: %s freed%s" % (
                human(freed), "" if a.all else " (the translated libraries are kept, to make a rerun quick)"))
    except lp_build.BuildError as e:
        progress.say("error: %s" % e)
        return 1
    except Failure as e:
        progress.say("error: %s" % e)
        return e.code
    return 0


def build_estimate(cfg):
    """What the build takes, for the start of the install: time and space."""
    text = ("Building the game takes about %d minutes the first time on an Apple Silicon Mac (measured on an "
            "M2 with 8 cores; more cores are quicker), most of it translating the game's code, and needs about "
            "%s free for its files (about %s is kept: the app and a cache that makes a rerun quick)."
            % (cfg["build_minutes"], human(cfg["build_disk_bytes"]), human(cfg["app_disk_bytes"] + cfg["cache_disk_bytes"])))
    if swap_room(cfg):
        text += (" Translating uses about %s of memory at its peak, and this Mac has %s: it gets slower, and macOS "
                 "needs about %s of the startup disk for the rest while it runs."
                 % (human(cfg["build_peak_memory_bytes"]), human(memory_bytes()), human(swap_room(cfg))))
    return text


def build(cfg, a, progress):
    work = os.path.abspath(a.work)
    need = cfg["build_disk_bytes"] - lp_build.du(work) if os.path.isdir(work) else cfg["build_disk_bytes"]
    free = free_bytes(work)
    if free < need:
        raise Failure("not enough free space for the build in %s: %s free, about %s needed. Free some space, or "
                      "set RS_BUILD_DIR to a folder on another disk, and run this again." % (work, human(free), human(need)))
    b = lp_build.Builder(work, os.path.abspath(a.image), os.path.abspath(a.dlls), os.path.abspath(a.game), a.py,
                         a.jobs or None, progress, progress.say)
    secs = b.run()
    progress.say("Built %s in %d min" % (os.path.join(work, "out", lp_build.EXE), round(secs / 60)))
    title_logo(os.path.abspath(a.game), progress.say)


if __name__ == "__main__":
    sys.exit(main())
