#!/bin/sh
# Compile the Win32 seam (win-x86/shims: win32_*.c and the D3D9 layer) and the offline online layer
# (win-x86/online) into win32_shims.a, and
# the offline steam_api face (shims/steam) into libsteam32.a.
#
# env:
#   OBJ_DIR      object directory (required)
#   LP_GL_INC    libRSOpenGL's headers (default: shims/opengl/src in this tree)
set -eu
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
runtime=$root/tools/ws7/recomp/runtime
shims=$root/win-x86/shims
obj=${OBJ_DIR:?set OBJ_DIR}
gl_inc=${LP_GL_INC:-$root/shims/opengl/src}
mkdir -p "$obj/shims"

CC="clang -arch arm64 -O2 -g -Wall -Wno-unused-parameter -DRC_GUEST32 -I$runtime -I$shims -I$gl_inc"
echo "=== Win32 shims ==="
rm -f "$obj/shims"/*.o
for f in "$shims"/win32_*.c "$shims"/d3d9/*.c "$shims"/audio/wa_*.c "$root/shims/coreaudio/rs_audio_host.c" \
         "$root"/win-x86/online/online32.c; do
  $CC -I"$root/shims/coreaudio" -c "$f" -o "$obj/shims/$(basename "$f" .c).o"
done
# the overlay API for mods, the Mac's (shims/include/rs_overlay.h): linked as an object, as only mods call it
$CC -I"$root/shims/platform/include" -c "$root/shims/platform/src/rsp_overlay.c" -o "$obj/rsp_overlay.o"
rm -f "$obj/win32_shims.a"
ar rcs "$obj/win32_shims.a" "$obj/shims"/*.o
echo "=== steam_api (offline) ==="
make -s -C "$shims/steam" OUT="$obj/steam" "$obj/steam/libsteam32.a"
echo "=== Done: $obj/win32_shims.a, $obj/steam/libsteam32.a ==="
