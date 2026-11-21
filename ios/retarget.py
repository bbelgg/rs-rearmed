"""Mark arm64 objects compiled for the iOS simulator as the device's, in place: their LC_BUILD_VERSION's platform.

    retarget.py ARCHIVE_OR_OBJECT... [--to ios|simulator]

The generated C of the game and the DLLs is compiled the same for both (arm64, the same ABI; nothing in it depends on
the platform), and compiling it takes half an hour, so ios/build_lp.sh's from-simulator step copies the simulator's
libraries and changes the one field the linker checks. The load command has the same size for both platforms, so the
objects need no room they don't have (vtool asks for header padding objects lack). An archive is unpacked, each object
marked, and packed again with libtool."""
import os
import shutil
import struct
import subprocess
import sys
import tempfile

LC_BUILD_VERSION = 0x32
PLATFORMS = {"ios": 2, "simulator": 7}
MH_MAGIC_64 = 0xFEEDFACF


def mark_object(path, to):
    with open(path, "r+b") as f:
        head = f.read(32)
        magic, _cpu, _sub, _ftype, ncmds, _size, _flags, _res = struct.unpack("<8I", head)
        if magic != MH_MAGIC_64:
            raise SystemExit("%s: not a 64-bit Mach-O object" % path)
        off = 32
        done = 0
        for _ in range(ncmds):
            f.seek(off)
            cmd, size = struct.unpack("<2I", f.read(8))
            if cmd == LC_BUILD_VERSION:
                f.seek(off + 8)
                (plat,) = struct.unpack("<I", f.read(4))
                if plat not in PLATFORMS.values():
                    raise SystemExit("%s: platform %d is neither iOS nor its simulator" % (path, plat))
                f.seek(off + 8)
                f.write(struct.pack("<I", to))
                done += 1
            off += size
        if not done:
            raise SystemExit("%s: no LC_BUILD_VERSION" % path)


def mark_archive(path, to):
    with tempfile.TemporaryDirectory() as d:
        subprocess.run(["ar", "-x", os.path.abspath(path)], cwd=d, check=True)
        objs = sorted(os.path.join(d, n) for n in os.listdir(d) if n.endswith(".o"))
        for o in objs:
            mark_object(o, to)
        tmp = path + ".new"
        subprocess.run(["libtool", "-static", "-no_warning_for_no_symbols", "-o", tmp] + objs, check=True)
        shutil.move(tmp, path)
    return len(objs)


def main(argv):
    to = PLATFORMS["ios"]
    if "--to" in argv:
        i = argv.index("--to")
        to = PLATFORMS[argv[i + 1]]
        argv = argv[:i] + argv[i + 2:]
    for p in argv:
        n = mark_archive(p, to) if p.endswith(".a") else (mark_object(p, to) or 1)
        print("%s: %d object(s)" % (p, n))


if __name__ == "__main__":
    main(sys.argv[1:])
