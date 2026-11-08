#!/bin/sh
# Translate the Learn & Play PE: run gen_image_pe.py on the game image (win-x86/image/lp_image.py).
# Output: $GEN_DIR/part_*.c, table.c, imports.c, decls.h, manifest.json, build.ninja
#
# env:
#   LP_IMAGE     the game image, built by win-x86/image/lp_image.py (required)
#   GEN_DIR      output directory         (default $LP_BUILD/game/gen)
#   OBJ_DIR      object directory         (default $LP_BUILD/game/obj)
#   LP_JOBS      parallelism              (default: hw.ncpu)
#   LP_PY        python with capstone     (default: python3 from venv or system)
#   RS_NINJA     ninja                    (default: the one next to the python, else ninja on PATH)
#   LP_WRAPPERS  wrapper table at the image base (default: IMAGE without .exe + .wrappers.tsv)
#   LP_IAT       optional: an iat_resolved.json to replace imports.c with (gen_iat_imports.py);
#                unset, imports.c comes from the image's own import directory
set -eu
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
recomp=$root/tools/ws7/recomp

img=${LP_IMAGE:?set LP_IMAGE to the game image built by win-x86/image/lp_image.py}
gen=${GEN_DIR:-${LP_BUILD:?set LP_BUILD or GEN_DIR}/game/gen}
obj=${OBJ_DIR:-$LP_BUILD/game/obj}
jobs=${LP_JOBS:-$(sysctl -n hw.ncpu)}

# Find python with capstone
if [ -n "${LP_PY:-}" ]; then
  py=$LP_PY
elif [ -x "${RS_BUILD_DIR:-$root/build}/venv/bin/python" ]; then
  py=${RS_BUILD_DIR:-$root/build}/venv/bin/python
else
  py=python3
fi

# Verify capstone is available
"$py" -c "import capstone" 2>/dev/null || {
  echo "error: capstone not found for $py"
  echo "  pip install capstone"
  exit 1
}

mkdir -p "$gen" "$obj"

# Skip file: addresses the override/shim modules define
skip=
[ -f "$here/skip.txt" ] && skip="--skip $here/skip.txt"

# VMProtect import-call sites at the image's base: lp_image.py writes IMAGE.wrappers.tsv
wrap=${LP_WRAPPERS:-${img%.exe}.wrappers.tsv}
wrappers=
[ -f "$wrap" ] && wrappers="--wrappers $wrap"

echo "=== Translating $img → $gen (jobs=$jobs) ==="
# The options are the installer's (win-x86/install/lp_build.py, TRANSLATE_OPTS): -mno-outline keeps the
# machine outliner from splitting the generated code into fragments (docs/windows/PERFORMANCE.md)
"$py" -I "$recomp/build/gen_image_pe.py" "$img" "$gen" \
  --jobs "$jobs" \
  --obj-dir "$obj" \
  --cflags="-w" \
  --opt="-O2 -mno-outline" \
  --target win-x86 \
  $skip $wrappers

# The image carries its import directory, so imports.c is already the game's IAT. An
# iat_resolved.json (the earlier, wrong-module image's) replaces it only when asked for.
if [ -n "${LP_IAT:-}" ]; then
  echo "=== Generating IAT imports from $LP_IAT ==="
  "$py" -I "$here/gen_iat_imports.py" "$LP_IAT" "$gen"
fi

echo "=== Compiling libgame_rc.a ==="
ninja=${RS_NINJA:-$(dirname "$py")/ninja}
[ -n "${RS_NINJA:-}" ] || [ -x "$ninja" ] || ninja=ninja
"$ninja" -C "$gen" -j "$jobs"

echo "=== Done: $(ls "$obj"/libgame_rc.a 2>/dev/null && echo "$obj/libgame_rc.a" || echo "check build output") ==="
