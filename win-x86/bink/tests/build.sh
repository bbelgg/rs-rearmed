#!/bin/sh
# Translate bink2w32.dll with the P2 front end as a module of its own (gen_image_pe.py --module
# bink2w32), after rebasing it to 0x12000000 (its own base, 0x10000000, is d3dx9_42's), and build
# bink_probe against it, the game's Win32 seam (win-x86/shims, with the D3D9 layer and the audio
# layer it pulls in) and the rc32 runtime.
#
#   build.sh <bink2w32.dll> <out dir> <cmake build dir with libRSOpenGL>
#   then: <out dir>/bink_probe <out dir>/in/bink2w32.dll <file.bk2> [-png frame.png] [-v]
#         (LP_LOG=1: each Win32 import's first call)
#
# env: PY    a Python with capstone (default: python3)
#      JOBS  parallelism (default: 2)
#
# The DLL and everything generated from it stay in <out dir>: build inputs, never committed.
# A .bk2 to play: the game's videos are in video.psarc and etudes.psarc
# (tools/psarc_extract.py <psarc> <dir> --match .bk2).
set -eu
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
recomp=$root/tools/ws7/recomp
rt=$recomp/runtime
dll=$1
out=$2
cm=$3
gl=$cm/shims/opengl
py=${PY:-python3}
jobs=${JOBS:-2}
mkdir -p "$out/in" "$out/obj"

"$py" -I "$root/win-x86/d3dx/rebase_pe.py" "$dll" "$out/in/bink2w32.dll" 12000000
mkdir -p "$out/bink2w32"
"$py" -I "$recomp/build/gen_image_pe.py" "$out/in/bink2w32.dll" "$out/bink2w32" --module bink2w32 \
  --jobs "$jobs" --cflags=-w --opt=-O2
ninja -C "$out/bink2w32" -j "$jobs"

OBJ_DIR=$out/obj/seam "$root/win-x86/build/build_shims.sh"

CC="clang -arch arm64 -O1 -g -DRC_GUEST32 -I$rt -I$root/win-x86/shims"
mkdir -p "$out/obj/rt"
for f in rc32_mem rc32_image rc32_teb rc32_seh rc32_wrappers; do $CC -w -c "$rt/$f.c" -o "$out/obj/rt/$f.o"; done
$CC -Wall -c "$here/bink_probe.c" -o "$out/obj/bink_probe.o"
# -all_load for the tree: nothing names it but the probe's bink2w32_rc32_mod
clang -arch arm64 -g "$out/obj/bink_probe.o" "$out"/obj/rt/*.o \
  -Wl,-force_load,"$out/bink2w32/obj/libgame_rc.a" \
  "$out/obj/seam/win32_shims.a" "$out/obj/seam/steam/libsteam32.a" \
  -L"$gl" -lRSOpenGL -lc++ -lz -framework Metal -framework QuartzCore -framework Cocoa \
  -framework AudioToolbox -framework CoreAudio -framework CoreFoundation \
  -Wl,-rpath,@executable_path -o "$out/bink_probe"
cp "$gl/libRSOpenGL.dylib" "$out/"
echo "built $out/bink_probe"
