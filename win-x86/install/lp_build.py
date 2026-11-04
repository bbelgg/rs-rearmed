"""Build and install steps for the Windows game (lp_install.py build, app-install, build-clean).

    build WORK --image EXE --dlls DIR --game DIR [--py PYTHON] [--jobs N]
        translate the game image and the DLLs it runs with (gen_image_pe.py), compile them, build
        libRSOpenGL (build_gl.sh), the Win32 seam, the runtime and the app, and link
        WORK/out/rs2014_lp (win-x86/build/build.sh). Each translation is keyed by the SHA-256 of its
        input and of the translator; a step whose key matches what is there is skipped, so a run
        that was stopped continues where it was, and a rerun after an update redoes only what the
        update changed.
    app-install WORK --game DIR [--app APP]
        the app bundle (default ~/Applications/RS Re-ARMed Learn & Play.app; $LP_NATIVE_APP) from
        WORK/out, ad-hoc signed, and its folder ~/Library/Application Support/RS Re-ARMed/learn-play
        ($LP_HOME): game-dir (the game folder the app reads: its own copy, game/), gamefiles.py and
        cache_overlay.py, userdata/, steam/, logs/ (win-x86/app/lp_main.c).
    build-clean WORK [--all]
        delete the build's intermediates; keep each translated library and its key (a cache that
        makes a rerun skip the translation), or nothing with --all.

The tools are the Xcode command line tools' (clang, make, libtool, xcrun) and the system's (codesign,
python3): no CMake, no Ninja, no Homebrew, no Rosetta. The translator needs a Python with capstone (--py).
Build output is derived from the user's own game and DLLs: it stays on this Mac."""
import contextlib
import hashlib
import json
import os
import plistlib
import re
import shlex
import shutil
import struct
import subprocess
import sys
import threading
import time
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
RECOMP = os.path.join(ROOT, "tools", "ws7", "recomp")
WINBUILD = os.path.join(ROOT, "win-x86", "build")
APP_NAME = "RS Re-ARMed Learn & Play"
BUNDLE_ID = "io.github.bbelgg.rs-rearmed.learn-play"
EXE = "rs2014_lp"


class BuildError(Exception):
    pass


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for b in iter(lambda: f.read(1 << 20), b""):
            h.update(b)
    return h.hexdigest()


def tree_hash(paths):
    """SHA-256 over the names and contents of files (sorted), for keys."""
    h = hashlib.sha256()
    for p in sorted(paths):
        h.update(os.path.relpath(p, ROOT).encode() + b"\0")
        with open(p, "rb") as f:
            h.update(hashlib.sha256(f.read()).digest())
    return h.hexdigest()


def files_in(d, exts):
    return [os.path.join(d, f) for f in os.listdir(d) if f.endswith(exts) and not f.startswith("._")]


def translator_hash():
    """The translator's sources: a change to any of them is a new translation."""
    return tree_hash(files_in(os.path.join(RECOMP, "xlat"), (".py",)) +
                     [os.path.join(RECOMP, "build", f) for f in ("gen_image_pe.py", "gen_image.py")])


_clang = []


def compiler_hash():
    """What compiling the generated C depends on besides the C: the runtime headers and the compiler."""
    if not _clang:
        r = subprocess.run(["clang", "--version"], capture_output=True, text=True)
        _clang.append(r.stdout)
    return tree_hash(files_in(os.path.join(RECOMP, "runtime"), (".h",))) + hashlib.sha256(
        _clang[0].encode()).hexdigest()


def read_key(path):
    try:
        with open(path) as f:
            return f.read().strip()
    except OSError:
        return ""


def remove(path):
    try:
        os.remove(path)
    except FileNotFoundError:
        pass


def write_key(path, key):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path + ".tmp", "w") as f:
        f.write(key + "\n")
    os.replace(path + ".tmp", path)


# --- build.ninja, without Ninja --------------------------------------------------------------------------

class Ninja:
    """The subset of build.ninja that gen_image.py's write_ninja writes: top-level variables, rules with a
    command (and a depfile), `build OUT: RULE IN... [| IMPLICIT...]` with indented bindings, `default`.
    An output is rebuilt when it is missing, its command changed (OUT.cmd holds the last one), or an
    input, an implicit input or a header in its depfile is newer."""

    def __init__(self, path):
        self.dir = os.path.dirname(os.path.abspath(path))
        self.vars, self.rules, self.edges = {}, {}, []
        cur = None
        with open(path) as f:
            for raw in f:
                line = raw.rstrip("\n")
                if not line.strip() or line.lstrip().startswith("#"):
                    continue
                if line.startswith("  ") and cur is not None:
                    k, _, v = line.strip().partition("=")
                    cur[k.strip()] = v.strip()
                    continue
                cur = None
                if line.startswith("rule "):
                    cur = self.rules.setdefault(line[5:].strip(), {})
                elif line.startswith("build "):
                    outs, _, rest = line[6:].partition(":")
                    parts = rest.split()
                    rule, deps = parts[0], parts[1:]
                    implicit = []
                    if "|" in deps:
                        k = deps.index("|")
                        deps, implicit = deps[:k], deps[k + 1:]
                    cur = {}
                    self.edges.append({"out": outs.strip(), "rule": rule, "in": deps, "implicit": implicit,
                                       "vars": cur})
                elif line.startswith("default ") or line.startswith("ninja_required_version"):
                    pass
                else:
                    k, _, v = line.partition("=")
                    self.vars[k.strip()] = v.strip()

    def expand(self, s, scope):
        def sub(m):
            name = m.group(1) or m.group(2)
            if name is None:
                return "$"
            v = scope.get(name)
            if v is None:
                v = self.vars.get(name, "")
            return self.expand(v, scope) if "$" in v else v
        return re.sub(r"\$(?:\{(\w+)\}|(\w+)|\$)", sub, s)

    def command(self, e):
        scope = dict(e["vars"])
        scope["in"] = " ".join(e["in"])
        scope["out"] = e["out"]
        rule = self.rules[e["rule"]]
        cmd = self.expand(rule["command"], scope)
        return cmd, (self.expand(rule["depfile"], scope) if "depfile" in rule else None)

    @staticmethod
    def depfile_inputs(path):
        try:
            with open(path) as f:
                text = f.read().replace("\\\n", " ")
        except OSError:
            return None
        _, _, deps = text.partition(":")
        return [d.replace("\\ ", " ") for d in re.findall(r"(?:\\ |[^\s])+", deps)]

    def dirty(self, e, cmd, depfile):
        out = e["out"]
        try:
            t = os.stat(out).st_mtime_ns
        except OSError:
            return True
        if read_key(out + ".cmd") != hashlib.sha256(cmd.encode()).hexdigest():
            return True
        ins = e["in"] + e["implicit"]
        if depfile:
            more = self.depfile_inputs(depfile)
            if more is None:
                return True
            ins = ins + more
        for p in ins:
            try:
                if os.stat(p).st_mtime_ns > t:
                    return True
            except OSError:
                return True
        return False

    def run(self, jobs, progress=None, log=None):
        """Build every edge (the compiles in parallel, then what depends on them). Returns how many ran."""
        produced = {e["out"] for e in self.edges}
        first = [e for e in self.edges if not any(i in produced for i in e["in"])]
        later = [e for e in self.edges if e not in first]
        ran = 0
        for group in (first, later):
            todo = []
            for e in group:
                cmd, dep = self.command(e)
                if group is later and ran:
                    todo.append((e, cmd))  # an input was rebuilt
                elif self.dirty(e, cmd, dep):
                    todo.append((e, cmd))
            if not todo:
                continue
            done = [0]
            lock = threading.Lock()
            failed = []

            def one(item):
                e, cmd = item
                if failed:
                    return
                os.makedirs(os.path.dirname(e["out"]) or ".", exist_ok=True)
                r = subprocess.run(["/bin/sh", "-c", cmd], cwd=self.dir, capture_output=True, text=True,
                                   errors="replace")
                with lock:
                    done[0] += 1
                    if log:
                        log.write("[%d/%d] %s\n%s%s" % (done[0], len(todo), cmd, r.stdout, r.stderr))
                        log.flush()
                    if r.returncode:
                        failed.append((e["out"], cmd, (r.stdout + r.stderr)[-4000:]))
                        return
                    write_key(e["out"] + ".cmd", hashlib.sha256(cmd.encode()).hexdigest())
                    if progress:
                        progress(done[0], len(todo))

            with ThreadPoolExecutor(max(1, jobs)) as pool:
                list(pool.map(one, todo))
            if failed:
                out, cmd, text = failed[0]
                raise BuildError("compiling %s failed:\n%s\n%s" % (out, cmd, text))
            ran += len(todo)
        return ran


# --- the build -------------------------------------------------------------------------------------------

# name, the file in --dlls (None: from the game folder), the base to rebase it to (None: as it is)
MODULES = [
    ("msvcr100", "msvcr100.dll", None),
    ("msvcp100", "msvcp100.dll", None),
    ("d3dx9_42", "d3dx9_42.dll", None),
    ("d3dcompiler_42", "D3DCompiler_42.dll", 0x11000000),  # lp_install.py redist writes it rebased already
    ("bink2w32", None, 0x12000000),
]


def find_case(folder, name):
    try:
        for f in os.listdir(folder):
            if f.lower() == name.lower():
                return os.path.join(folder, f)
    except OSError:
        pass
    return None


# what translate() passes gen_image_pe.py besides the paths (part of each translation's key)
TRANSLATE_OPTS = ["--cflags=-w", "--opt=-O2"]


_py_ids = {}


def python_id(py):
    """The translator's Python and capstone versions (capstone decodes the code: part of the key). A
    Python without capstone fails in the translation itself, with its log."""
    if py not in _py_ids:
        code = ("import sys\ntry:\n import capstone; v = capstone.__version__\nexcept Exception:\n v = 'none'\n"
                "print(sys.version.split()[0], v)")
        r = subprocess.run([py, "-I", "-c", code], capture_output=True, text=True)
        _py_ids[py] = r.stdout.strip() if r.returncode == 0 else ""
    return _py_ids[py]


JOB_MEMORY = 600 << 20   # a translation worker peaks at 0.37 GB, a compile at 0.8 GB and about 0.35 on average


def default_jobs(cores=None, memory=None):
    """One job per core, as many as fit in half the memory: an 8 GB Mac gets 6, with the system and whatever else
    is open. Measured on the game: docs/windows/PERFORMANCE.md."""
    if memory is None:
        r = subprocess.run(["sysctl", "-n", "hw.memsize"], capture_output=True, text=True)
        memory = int(r.stdout) if r.stdout.strip().isdigit() else 8 << 30
    return max(1, min(cores or os.cpu_count() or 4, memory // 2 // JOB_MEMORY))


class Unit:
    """One image to translate: the game (module None) or a DLL."""

    def __init__(self, name, src, module, gen, obj, wrappers=None, skip=None):
        self.name, self.src, self.module, self.gen, self.obj = name, src, module, gen, obj
        self.wrappers, self.skip = wrappers, skip
        self.lib = os.path.join(obj, "libgame_rc.a")

    def translate_key(self, py_id=""):
        k = {"src": sha256_file(self.src), "module": self.module, "translator": translator_hash(), "py": py_id,
             "opts": TRANSLATE_OPTS,
             "wrappers": sha256_file(self.wrappers) if self.wrappers else None,
             "skip": sha256_file(self.skip) if self.skip else None, "obj": self.obj}
        return hashlib.sha256(json.dumps(k, sort_keys=True).encode()).hexdigest()

    def lib_key(self, tkey):
        return hashlib.sha256((tkey + compiler_hash()).encode()).hexdigest()


class Builder:
    """lp_install.py build: see the module's doc. progress: lp_install.Progress."""

    # shares of the whole build, for the progress bar (measured: the game's translation and its compile
    # are almost all of it)
    WEIGHTS = {"translate:game": 0.50, "compile:game": 0.36, "modules": 0.08, "app": 0.06}
    DISCOVERY, DISCOVERY_SECS = 0.8, 640.0  # the game's translation: share and time of finding its functions
    ALIVE_SECS = 60.0

    def __init__(self, work, image, dlls, game, py, jobs=None, progress=None, say=None, run=subprocess.run,
                 translator=None):
        self.work, self.image, self.dlls, self.game = work, image, dlls, game
        self.py = py
        self.jobs = jobs or default_jobs()
        self.progress = progress or (lambda f, label: None)
        self.say = say or (lambda msg: None)
        self.runner = run
        self.translator = translator or os.path.join(RECOMP, "build", "gen_image_pe.py")
        self.logs = os.path.join(work, "logs")
        self.base = 0.0

    def frac(self, part, f):
        f = min(max(f, 0.0), 1.0)
        self.progress(self.base + self.WEIGHTS[part] * f, "%s… %d%%" % (self.label, int(f * 100)))

    def units(self):
        mods = os.path.join(self.work, "modules")
        out = []
        for name, file, base in MODULES:
            src = os.path.join(mods, "in", name + ".dll")
            out.append(Unit(name, src, name, os.path.join(mods, name, "gen"), os.path.join(mods, name, "obj")))
        wrappers = os.path.splitext(self.image)[0] + ".wrappers.tsv"
        skip = os.path.join(WINBUILD, "skip.txt")
        out.append(Unit("game", self.image, None, os.path.join(self.work, "game", "gen"),
                        os.path.join(self.work, "game", "obj"),
                        wrappers if os.path.isfile(wrappers) else None, skip if os.path.isfile(skip) else None))
        return out

    def check_inputs(self):
        if not os.path.isfile(self.image):
            raise BuildError("no game image at %s (the code image step makes it)" % self.image)
        for name, file, base in MODULES:
            if file and not find_case(self.dlls, file):
                raise BuildError("no %s in %s (lp_install.py redist)" % (file, self.dlls))
        if not find_case(self.game, "bink2w32.dll"):
            raise BuildError("no bink2w32.dll in the game folder %s" % self.game)

    def stage_dlls(self):
        """WORK/modules/in/<name>.dll: the DLL images the translation reads and the game maps at run time."""
        sys.path.insert(0, os.path.join(ROOT, "win-x86", "d3dx"))
        from rebase_pe import rebase
        d = os.path.join(self.work, "modules", "in")
        os.makedirs(d, exist_ok=True)
        for name, file, base in MODULES:
            src = find_case(self.dlls, file) if file else find_case(self.game, "bink2w32.dll")
            with open(src, "rb") as f:
                data = f.read()
            if base and struct.unpack_from("<I", data, struct.unpack_from("<I", data, 0x3c)[0] + 24 + 28)[0] != base:
                data = bytes(rebase(data, base)[0])
            dst = os.path.join(d, name + ".dll")
            try:
                with open(dst, "rb") as f:
                    if f.read() == data:
                        continue
            except OSError:
                pass
            with open(dst + ".tmp", "wb") as f:
                f.write(data)
            os.replace(dst + ".tmp", dst)

    def translate(self, u, part):
        """gen_image_pe.py into u.gen, with its "  N/M functions" lines as progress."""
        os.makedirs(self.logs, exist_ok=True)
        cmd = [self.py, "-I", self.translator, u.src, u.gen,
               "--jobs", str(self.jobs), "--obj-dir", u.obj] + TRANSLATE_OPTS
        if u.module:
            cmd += ["--module", u.module]
        if u.wrappers:
            cmd += ["--wrappers", u.wrappers]
        if u.skip:
            cmd += ["--skip", u.skip]
        logp = os.path.join(self.logs, "translate-%s.log" % u.name)
        with open(logp, "w") as log:
            log.write(" ".join(shlex.quote(c) for c in cmd) + "\n")
            log.flush()
            # Finding the functions (one process, no progress lines) is most of a translation: about 11 of
            # the game's 13-14 minutes on an M2. Until the first "0/N functions" the bar moves with the time.
            counting = threading.Event()
            stop = threading.Event()
            t0 = time.time()

            def tick():
                while not stop.wait(2.0) and not counting.is_set():
                    self.frac(part, self.DISCOVERY * min((time.time() - t0) / self.DISCOVERY_SECS, 0.98))

            ticker = threading.Thread(target=tick, daemon=True) if part else None
            if ticker:
                ticker.start()
            try:
                with subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                                      errors="replace") as p:
                    for line in p.stdout:
                        log.write(line)
                        log.flush()
                        m = re.match(r"^\s+(\d+)/(\d+) functions", line)
                        if m and part:
                            counting.set()
                            self.frac(part, self.DISCOVERY + (1 - self.DISCOVERY) * int(m.group(1)) /
                                      max(int(m.group(2)), 1))
            finally:
                stop.set()
                if ticker:
                    ticker.join()
        if p.returncode:
            raise BuildError("translating %s failed (exit %d); the log is %s:\n%s"
                             % (u.name, p.returncode, logp, tail(logp)))

    def compile(self, u, part):
        os.makedirs(self.logs, exist_ok=True)
        with open(os.path.join(self.logs, "compile-%s.log" % u.name), "w") as log:
            Ninja(os.path.join(u.gen, "build.ninja")).run(
                self.jobs, (lambda n, total: self.frac(part, n / total)) if part else None, log)

    @contextlib.contextmanager
    def alive(self, what):
        """A line a minute while a step without progress of its own runs: d3dcompiler_42 takes three minutes, and the
        installer app takes two minutes of silence for a system window it can't see."""
        done = threading.Event()

        def tick():
            while not done.wait(self.ALIVE_SECS):
                self.say("  %s: still working" % what)

        ticker = threading.Thread(target=tick, daemon=True)
        ticker.start()
        try:
            yield
        finally:
            done.set()
            ticker.join()

    def unit(self, u, part_t=None, part_c=None, what=None):
        """Translate and compile one image unless its library is up to date. True if anything ran."""
        what = what or u.name
        tkey = u.translate_key(python_id(self.py))
        lkey = u.lib_key(tkey)
        if read_key(u.lib + ".key") == lkey and os.path.isfile(u.lib):
            self.say("  %s: up to date" % what)
            return False
        t0 = time.time()
        # A key is removed before what it vouches for is redone, so a run stopped halfway never leaves an
        # old key next to half-new files (a later run with the old inputs would take them as done).
        remove(u.lib + ".key")
        if read_key(os.path.join(u.gen, ".lp-key")) != tkey or not os.path.isfile(os.path.join(u.gen, "build.ninja")):
            remove(os.path.join(u.gen, ".lp-key"))
            self.say("  %s: translating%s" % (what, " (the long part)" if u.module is None else ""))
            self.label = "Translating %s to C" % what
            self.translate(u, part_t)
            write_key(os.path.join(u.gen, ".lp-key"), tkey)
        else:
            self.say("  %s: translated already" % what)
        if part_t:
            self.base += self.WEIGHTS[part_t]
        self.say("  %s: compiling" % what)
        self.label = "Compiling %s" % what
        self.compile(u, part_c)
        write_key(u.lib + ".key", lkey)
        self.say("  %s: done (%d min)" % (what, round((time.time() - t0) / 60)))
        return True

    def run(self):
        # The generated build.ninja and the shell build scripts take these paths as single words.
        for what, path in (("the build folder", self.work), ("the source folder", ROOT)):
            if any(ch.isspace() for ch in path):
                raise BuildError("%s has a space in its path (%s); choose one without spaces (RS_BUILD_DIR, or the "
                                 "installer's build folder)" % (what, path))
        self.check_inputs()
        os.makedirs(self.work, exist_ok=True)
        self.stage_dlls()
        units = self.units()
        t0 = time.time()
        # the DLLs first: a minute or two, and a broken setup shows before the long part
        self.base = 0.0
        mods = [u for u in units if u.module]
        for k, u in enumerate(mods):
            with self.alive(u.name):
                self.unit(u)
            self.label = "Translating the Microsoft libraries"
            self.frac("modules", (k + 1) / len(mods))
        self.base = self.WEIGHTS["modules"]
        self.unit(units[-1], "translate:game", "compile:game", "the game")
        self.base = 1.0 - self.WEIGHTS["app"]
        self.label = "Building the app"
        self.frac("app", 0.0)
        self.say("  libRSOpenGL, the Win32 seam, the runtime, the link")
        self.app()
        self.progress(1.0, "Built")
        return time.time() - t0

    def app(self):
        env = dict(os.environ, LP_BUILD=self.work, LP_OUT=os.path.join(self.work, "out"), LP_IMAGE=self.image,
                   LP_GL_DIR=os.path.join(self.work, "gl"), OUT_DIR=os.path.join(self.work, "gl"),
                   LP_JOBS=str(self.jobs), LP_TRANSLATE="0", LP_MODULES_BUILD="0")
        os.makedirs(self.logs, exist_ok=True)
        for script, logname in (("build_gl.sh", "gl"), ("build.sh", "link")):
            logp = os.path.join(self.logs, logname + ".log")
            with open(logp, "w") as log:
                r = self.runner(["/bin/sh", os.path.join(WINBUILD, script)], env=env, stdout=log,
                                stderr=subprocess.STDOUT)
            if r.returncode:
                raise BuildError("%s failed (exit %d); the log is %s:\n%s" % (script, r.returncode, logp, tail(logp)))
            self.frac("app", 0.5)


def tail(path, n=30):
    try:
        with open(path, errors="replace") as f:
            return "".join(f.readlines()[-n:])
    except OSError:
        return ""


# --- clean -----------------------------------------------------------------------------------------------

def clean(work, everything=False):
    """Delete WORK's intermediates. Kept unless everything: each libgame_rc.a with its key (a rerun with the
    same inputs and translator skips the translation and the compile), modules/in (small). Returns bytes freed."""
    if not os.path.isdir(work):
        return 0
    real, home = os.path.realpath(work), os.path.realpath(default_home())
    if real in ("/", os.path.realpath(os.path.expanduser("~"))) or home == real or home.startswith(real + os.sep) \
            or not any(os.path.exists(os.path.join(work, d)) for d in ("game", "modules", "out", "gl", "obj", "logs")):
        raise BuildError("%s does not look like a build folder; nothing deleted" % work)
    before = du(work)
    if everything:
        shutil.rmtree(work)
        return before
    for d, subdirs, files in os.walk(work, topdown=True):
        if os.path.basename(d) == "gen":
            shutil.rmtree(d)
            subdirs[:] = []
            continue
        for f in files:
            if f.startswith("libgame_rc.a") or os.path.basename(d) == "in":
                continue
            os.remove(os.path.join(d, f))
    for sub in ("out", "obj", "gl", "logs"):
        shutil.rmtree(os.path.join(work, sub), ignore_errors=True)
    for d, subdirs, files in os.walk(work, topdown=False):
        if d != work and not os.listdir(d):
            os.rmdir(d)
    return before - du(work)


def du(top):
    n = 0
    for d, _, files in os.walk(top):
        for f in files:
            try:
                n += os.lstat(os.path.join(d, f)).st_size
            except OSError:
                pass
    return n


# --- the app ---------------------------------------------------------------------------------------------

def default_home():
    return os.environ.get("LP_HOME") or os.path.expanduser("~/Library/Application Support/RS Re-ARMed/learn-play")


def default_app():
    return os.environ.get("LP_NATIVE_APP") or os.path.expanduser("~/Applications/%s.app" % APP_NAME)


def pe_icon(data):
    """The first icon group of a PE's resources as an .ico file, or None."""
    try:
        pe = struct.unpack_from("<I", data, 0x3c)[0]
        nsect, optsz = struct.unpack_from("<H", data, pe + 6)[0], struct.unpack_from("<H", data, pe + 20)[0]
        opt = pe + 24
        dd = opt + (96 if struct.unpack_from("<H", data, opt)[0] == 0x10b else 112)
        rva = struct.unpack_from("<I", data, dd + 16)[0]
        sects = [struct.unpack_from("<IIII", data, opt + optsz + 40 * k + 8) for k in range(nsect)]

        def off(r):
            for vs, va, rs, ro in sects:
                if va <= r < va + max(vs, rs) and r - va < rs:
                    return ro + r - va
            raise ValueError("rva 0x%x" % r)

        root = off(rva)

        def entries(o):
            nn, ni = struct.unpack_from("<HH", data, o + 12)
            return [struct.unpack_from("<II", data, o + 16 + 8 * k) for k in range(nn + ni)]

        def leaf(t):
            while t & 0x80000000:
                t = entries(root + (t & 0x7fffffff))[0][1]
            drva, dsz = struct.unpack_from("<II", data, root + t)
            return data[off(drva):off(drva) + dsz]

        def resources(rtype):
            for name, t in entries(root):
                if name == rtype and t & 0x80000000:
                    return {n: leaf(tt) for n, tt in entries(root + (t & 0x7fffffff))}
            return {}

        groups = resources(14)  # RT_GROUP_ICON
        if not groups:
            return None
        icons = resources(3)  # RT_ICON
        g = groups[sorted(groups)[0]]
        ents = []
        for k in range(struct.unpack_from("<H", g, 4)[0]):
            w, h, cc, res, planes, bc, _, ident = struct.unpack_from("<BBBBHHIH", g, 6 + 14 * k)
            if ident in icons:
                ents.append(((w, h, cc, res, planes, bc), icons[ident]))
        if not ents:
            return None
        o = 6 + 16 * len(ents)
        head, body = struct.pack("<HHH", 0, 1, len(ents)), b""
        for e, b in ents:
            head += struct.pack("<BBBBHHII", *e, len(b), o + len(body))
            body += b
        return head + body
    except (struct.error, ValueError, IndexError):
        return None


def make_icns(exe, dest, run=subprocess.run):
    """dest (.icns) from the game exe's icon, with sips. False if there is none or sips cannot read it."""
    try:
        with open(exe, "rb") as f:
            ico = pe_icon(f.read())
    except OSError:
        return False
    if not ico:
        return False
    tmp = dest + ".ico"
    with open(tmp, "wb") as f:
        f.write(ico)
    try:
        r = run(["/usr/bin/sips", "-s", "format", "icns", tmp, "--out", dest], capture_output=True)
        return r.returncode == 0 and os.path.isfile(dest)
    finally:
        os.remove(tmp)


def info_plist(version, icon):
    p = {
        "CFBundleExecutable": EXE,
        "CFBundleIdentifier": BUNDLE_ID,
        "CFBundleName": "RS Re-ARMed LP",
        "CFBundleDisplayName": APP_NAME,
        "CFBundlePackageType": "APPL",
        "CFBundleShortVersionString": version,
        "CFBundleVersion": version,
        "LSMinimumSystemVersion": "11.0",
        "LSApplicationCategoryType": "public.app-category.music-games",
        "NSHighResolutionCapable": True,
        "NSMicrophoneUsageDescription": "Rocksmith listens to your guitar through the Real Tone cable or another input.",
    }
    if icon:
        p["CFBundleIconFile"] = "RS2014.icns"
    return p


def running(app):
    """Is the installed game running (a process whose executable is in app)?"""
    r = subprocess.run(["/usr/bin/pgrep", "-f", os.path.join(app, "Contents", "MacOS", EXE)],
                       capture_output=True, text=True)
    return r.returncode == 0 and bool(r.stdout.strip())


def install_app(work, game, app=None, home=None, run=subprocess.run, say=None):
    """The bundle from WORK/out and the user folder; returns (app, home)."""
    say = say or (lambda m: None)
    app = os.path.abspath(app or default_app())
    home = os.path.abspath(home or default_home())
    out = os.path.join(work, "out")
    for f in (EXE, "libRSOpenGL.dylib", "lp_game.exe"):
        if not os.path.isfile(os.path.join(out, f)):
            raise BuildError("no %s in %s (lp_install.py build)" % (f, out))
    if running(app):
        raise BuildError("quit the game first (%s is running)" % app)
    parent = os.path.dirname(app)
    while parent and not os.path.exists(parent):
        parent = os.path.dirname(parent)
    st = os.statvfs(parent or "/")
    need = du(out) + (64 << 20)
    if st.f_bavail * st.f_frsize < need:
        raise BuildError("not enough free space for the app in %s: about %d MB needed" % (parent, need >> 20))
    new = app + ".new"
    shutil.rmtree(new, ignore_errors=True)
    macos, res = os.path.join(new, "Contents", "MacOS"), os.path.join(new, "Contents", "Resources")
    os.makedirs(macos)
    os.makedirs(os.path.join(res, "dlls"))
    shutil.copy2(os.path.join(out, EXE), macos)
    shutil.copy2(os.path.join(out, "libRSOpenGL.dylib"), macos)
    shutil.copy2(os.path.join(out, "lp_game.exe"), res)
    for f in sorted(os.listdir(os.path.join(out, "dlls"))):
        if f.lower().endswith(".dll") and not f.startswith("._"):
            shutil.copy2(os.path.join(out, "dlls", f), os.path.join(res, "dlls", f))
    icon = make_icns(os.path.join(out, "lp_game.exe"), os.path.join(res, "RS2014.icns"), run)
    with open(os.path.join(ROOT, "VERSION")) as f:
        version = f.readline().strip()
    with open(os.path.join(new, "Contents", "Info.plist"), "wb") as f:
        plistlib.dump(info_plist(version, icon), f)
    # From a drive without extended attributes, cp leaves "._" files that break the seal.
    for d, _, files in os.walk(new):
        for f in files:
            if f.startswith("._"):
                os.remove(os.path.join(d, f))
    for target in (os.path.join(macos, "libRSOpenGL.dylib"), new):
        r = run(["/usr/bin/codesign", "--force", "--sign", "-", target], capture_output=True, text=True)
        if r.returncode:
            raise BuildError("codesign %s failed: %s" % (target, (r.stderr or "").strip()))
    old = app + ".old"
    shutil.rmtree(old, ignore_errors=True)
    os.makedirs(os.path.dirname(app), exist_ok=True)
    if os.path.exists(app):
        os.rename(app, old)
    os.rename(new, app)
    shutil.rmtree(old, ignore_errors=True)
    # the user's folder: never replaced, only made and pointed at the game (the copy lp_install.py game-copy
    # made), with the copy's tool for a refresh without the repository
    for sub in ("userdata", "steam", "logs"):
        os.makedirs(os.path.join(home, sub), exist_ok=True)
    with open(os.path.join(home, "game-dir"), "w") as f:
        f.write(os.path.abspath(game) + "\n")
    for tool in ("gamefiles.py", "cache_overlay.py"):
        shutil.copy2(os.path.join(ROOT, "tools", "play-native", tool), os.path.join(home, tool))
    lsregister = ("/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/"
                  "Support/lsregister")
    if os.path.exists(lsregister):
        run([lsregister, "-f", app], capture_output=True)
    return app, home
