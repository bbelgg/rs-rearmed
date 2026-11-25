#!/bin/sh
# Compile the Win32 seam (win-x86/shims: win32_*.c and the D3D9 layer) and the offline online layer
# (win-x86/online) into win32_shims.a, and
# the offline steam_api face (shims/steam) into libsteam32.a.
#
# env:
#   OBJ_DIR        object directory (required)
#   LP_GL_INC      libRSOpenGL's headers (default: shims/opengl/src in this tree)
#   RC_TARGET_DIR  the game's names (tools/targets/targets.py gen --target win-x86), as build.sh makes them;
#                  unset, this script makes its own in OBJ_DIR/target
set -eu
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
runtime=$root/tools/ws7/recomp/runtime
shims=$root/win-x86/shims
obj=${OBJ_DIR:?set OBJ_DIR}
gl_inc=${LP_GL_INC:-$root/shims/opengl/src}
mkdir -p "$obj/shims"
obj=$(cd "$obj" && pwd)  # the Steam face is made by make -C: a relative folder would be another one there
# the game's names for the built-in fixes (win32_overrides.c, through runtime/rc_abi.h): rc_target_names.h
tgt=${RC_TARGET_DIR:-$obj/target}
[ -n "${RC_TARGET_DIR:-}" ] || "${LP_PY:-python3}" -I "$root/tools/targets/targets.py" gen --target win-x86 "$tgt" >/dev/null

mods=$root/tools/ws7/recomp/overrides/mods
CC="clang -arch arm64 -O2 -g -Wall -Wno-unused-parameter -DRC_GUEST32 -I$runtime -I$shims -I$gl_inc -I$tgt -I$mods"
echo "=== Win32 shims ==="
rm -f "$obj/shims"/*.o
for f in "$shims"/win32_*.c "$shims"/d3d9/*.c "$shims"/audio/wa_*.c "$root/shims/coreaudio"/rs_audio_*.c \
         "$root"/win-x86/online/online32.c; do
  $CC -I"$root/shims/coreaudio" -I"$root/shims/platform/include" -c "$f" -o "$obj/shims/$(basename "$f" .c).o"
done
# the built-in fixes' part that is the Mac's source (overrides/mods: mods_shared.c, the switches, the log and the start;
# framerate.c, the frame rate fixes): built as a mod is, by the game's names at run time
for f in mods_shared framerate; do
  clang -arch arm64 -O2 -g -Wall -I"$runtime" -I"$root/tools/ws7/recomp/overrides" -c "$mods/$f.c" -o "$obj/shims/$f.o"
done
# the overlay API for mods, the Mac's (shims/include/rs_overlay.h): linked as an object, as only mods call it
$CC -I"$root/shims/platform/include" -c "$root/shims/platform/src/rsp_overlay.c" -o "$obj/rsp_overlay.o"
# the trackpad accumulator the scroll handler shares with the Mac shell (rsp_scroll_steps):
# in the archive, pulled in by lp_window.o's reference to it
$CC -I"$root/shims/platform/include" -c "$root/shims/platform/src/rsp_logic.c" -o "$obj/shims/rsp_logic.o"
rm -f "$obj/win32_shims.a"
ar rcs "$obj/win32_shims.a" "$obj/shims"/*.o
echo "=== steam_api (offline) ==="
make -s -C "$shims/steam" OUT="$obj/steam" "$obj/steam/libsteam32.a"
echo "=== Done: $obj/win32_shims.a, $obj/steam/libsteam32.a ==="
