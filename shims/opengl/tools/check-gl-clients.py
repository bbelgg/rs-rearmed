#!/usr/bin/env python3
"""Check that libRSOpenGL wraps every GL/CGL function its clients call.

    check-gl-clients.py <gl_api.inc> <build-dir>

A client is a Mach-O under <build-dir> that links libRSOpenGL.dylib, or a static
library there (they all end up in something that does). A gl*/CGL* name one of them
imports that isn't in the table still links, through the framework libRSOpenGL
re-exports, and on RS_GL_BACKEND=metal calls Apple's GL with no context. Prints
"clients ok" or each missing name (add it to extra-gl-imports.txt).
"""
import os
import re
import subprocess
import sys

ENTRY = re.compile(r"^RS_GL_(?:FN\([^,]+,|VOID\()\s*(\w+)")
NAME = re.compile(r"^_((?:gl|CGL)[A-Z]\w*)$")


def is_macho(path):
    try:
        with open(path, "rb") as f:
            magic = f.read(4)
    except OSError:
        return False
    return magic in (b"\xcf\xfa\xed\xfe", b"\xca\xfe\xba\xbe", b"\xbe\xba\xfe\xca")


def main():
    inc, root = sys.argv[1:3]
    table = {m.group(1) for m in map(ENTRY.match, open(inc)) if m}
    checked, missing = 0, {}
    for d, dirs, files in os.walk(root):
        dirs[:] = [x for x in dirs if x != "CMakeFiles"]
        for f in files:
            p = os.path.join(d, f)
            if os.path.islink(p):
                continue
            if f.endswith(".a"):
                pass
            elif not is_macho(p) or "libRSOpenGL" in f:
                continue
            else:
                libs = subprocess.run(["otool", "-L", p], capture_output=True, text=True).stdout
                if "libRSOpenGL.dylib" not in libs:
                    continue
            out = subprocess.run(["nm", "-u", p], capture_output=True, text=True).stdout
            names = {m.group(1) for m in map(NAME.match, out.split()) if m}
            checked += 1
            for n in sorted(names - table):
                missing.setdefault(n, []).append(os.path.relpath(p, root))
    for n, users in sorted(missing.items()):
        print(f"missing {n}: {' '.join(sorted(set(users)))}")
    print(f"{checked} clients checked, {len(table)} functions in the table")
    if missing:
        return 1
    print("clients ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
