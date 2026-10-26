"""Import table of an x86_64 Mach-O: for every __stubs entry, where its lazy pointer comes from.

kinds: "lazy"/"bind" (dylib + symbol, bound natively by rc_image.c), "weak" (a
weak-bind symbol; dyld coalesces it to a definition, here normally the image's
own copy, so the slot is rebased to an address inside the image).
"""
import subprocess


def _table(path, flag):
    out = subprocess.run(["xcrun", "objdump", "--macho", flag, path], capture_output=True, text=True, check=True).stdout
    for line in out.splitlines():
        p = line.split()
        if len(p) >= 5 and p[0].startswith("__") and p[2].lower().startswith("0x"):
            yield p


def slot_info(path):
    """slot vmaddr -> dict(kind, dylib, sym)"""
    info = {}
    for p in _table(path, "--weak-bind"):
        info[int(p[2], 16)] = dict(kind="weak", dylib="", sym=p[-1])
    for flag, kind in (("--bind", "bind"), ("--lazy-bind", "lazy")):
        for p in _table(path, flag):
            if len(p) >= 7 or (kind == "lazy" and len(p) == 5):
                info[int(p[2], 16)] = dict(kind=kind, dylib=p[-2], sym=p[-1])
    return info
