#!/bin/sh
# Build the Learn & Play binary: the translated game and DLLs, the Win32 seam, the D3D9 layer on
# libRSOpenGL, the offline Steam face and the 32-bit runtime, linked into one arm64 executable.
#
#   build.sh
#
# env:
#   LP_BUILD      build root (required). Layout:
#                   game/gen, game/obj/libgame_rc.a   the game's translation (translate.sh)
#                   modules/<name>/obj/libgame_rc.a   the DLLs' translations (build_modules.sh)
#                   modules/in/<name>.dll              the DLL images loaded at run time
#                   obj/                               the seam, the runtime, the app
#                   out/                               the result (LP_OUT)
#   LP_OUT        output directory (default $LP_BUILD/out): rs2014_lp, lp_game.exe, dlls/,
#                 libRSOpenGL.dylib, libnetguard.dylib
#   LP_IMAGE      the game image (lp_image.py), copied to the output
#   LP_TRANSLATE  1: run translate.sh first (about 50 minutes), else use game/obj as it is
#   LP_MODULES_BUILD  1: run build_modules.sh first (needs its LP_* DLL paths)
#   LP_GL_DIR     a directory with libRSOpenGL.dylib (build_gl.sh, no CMake: the installer's), or
#   LP_CMAKE_DIR  a cmake build dir with libRSOpenGL built (cmake --build DIR --target opengl);
#                 libRSOpenGL.dylib is DIR/shims/opengl/libRSOpenGL.dylib
set -eu
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
runtime=$root/tools/ws7/recomp/runtime
b=${LP_BUILD:?set LP_BUILD to the build root}
obj=$b/obj
out=${LP_OUT:-$b/out}
if [ -n "${LP_GL_DIR:-}" ]; then
  gl=$LP_GL_DIR
else
  gl=${LP_CMAKE_DIR:?set LP_GL_DIR to build_gl.sh output or LP_CMAKE_DIR to a cmake build dir with libRSOpenGL}/shims/opengl
fi
mkdir -p "$obj/rt" "$out/dlls"

if [ "${LP_TRANSLATE:-0}" = 1 ]; then
  GEN_DIR=$b/game/gen OBJ_DIR=$b/game/obj "$here/translate.sh"
fi
if [ "${LP_MODULES_BUILD:-0}" = 1 ]; then
  "$here/build_modules.sh"
fi
for f in "$b/game/obj/libgame_rc.a" "$b"/modules/msvcr100/obj/libgame_rc.a "$b"/modules/msvcp100/obj/libgame_rc.a \
         "$b"/modules/d3dx9_42/obj/libgame_rc.a "$gl/libRSOpenGL.dylib"; do
  [ -f "$f" ] || { echo "missing $f" >&2; exit 2; }
done
# D3DCompiler_42 is optional: without it D3DX cannot compile HLSL (LoadLibrary of it fails)
dcomp=$b/modules/d3dcompiler_42/obj/libgame_rc.a
[ -f "$dcomp" ] || { echo "note: no $dcomp: linking without D3DCompiler_42"; dcomp=-Wl,-U,_d3dcompiler_42_rc32_mod; }
# so is Bink 2: without it the game's Bink imports are the seam's stubs (no videos)
bink=$b/modules/bink2w32/obj/libgame_rc.a
[ -f "$bink" ] || { echo "note: no $bink: linking without Bink 2"; bink=-Wl,-U,_bink2w32_rc32_mod; }

echo "===== Shims ====="
OBJ_DIR=$obj "$here/build_shims.sh"

echo "===== Runtime ====="
# the target's names and hook points for the runtime (rc_hook_add_name, rc_off): tools/targets
tgt=$obj/target
"${LP_PY:-python3}" -I "$root/tools/targets/targets.py" gen --target win-x86 "$tgt"
CC="clang -arch arm64 -O2 -g -DRC_GUEST32 -I$runtime"
for m in rc32_mem rc32_image rc32_main rc32_teb rc32_seh rc32_wrappers rc32_hook rc_hook rc_mods; do
  $CC -w -c "$runtime/$m.c" -o "$obj/rt/$m.o"
done
$CC -w -c "$tgt/rc_target.c" -o "$obj/rt/rc_target.o"

echo "===== App ====="
$CC -Wall -I"$root/win-x86/shims" -c "$root/win-x86/app/lp_main.c" -o "$obj/lp_main.o"
# The network guard: Learn & Play has no socket of its own to guard, but a mod is native code. The app loads
# it before any mod (lp_main.c), and without it loads no mods; the same library as the Mac edition's.
clang -arch arm64 -dynamiclib -O1 -mmacosx-version-min=11.0 -o "$out/libnetguard.dylib" "$root/tools/ws8/netguard/netguard.c"
codesign -f -s - "$out/libnetguard.dylib" 2>/dev/null
$CC -Wall -c "$root/win-x86/app/lp_screen.c" -o "$obj/lp_screen.o"
clang -arch arm64 -O2 -g -Wall -fobjc-arc -DLP_VERSION="\"$(cat "$root/VERSION")\"" -I"$root/shims/opengl/src" -c "$root/win-x86/app/lp_window.m" -o "$obj/lp_window.o"

clang -arch arm64 -O2 -g -Wall -I"$root/win-x86/shims/d3d9" "$root/win-x86/shaders/lp_shaders.c" \
  "$root/win-x86/shims/d3d9/d3d9_shader.c" "$root/win-x86/shims/d3d9/d3d9_ffp_gen.c" -o "$obj/lp_shaders" ||
  echo "no lp_shaders: this build has no shader corpus"

echo "===== Link ====="
clang -arch arm64 -g "$obj/lp_main.o" "$obj/lp_window.o" "$obj/lp_screen.o" "$obj"/rt/*.o \
  "$b/game/obj/libgame_rc.a" \
  "$b/modules/msvcr100/obj/libgame_rc.a" "$b/modules/msvcp100/obj/libgame_rc.a" \
  "$b/modules/d3dx9_42/obj/libgame_rc.a" $dcomp $bink \
  "$obj/win32_shims.a" "$obj/steam/libsteam32.a" \
  -L"$gl" -lRSOpenGL -lc++ -lz -framework Metal -framework QuartzCore -framework Cocoa \
  -framework AudioToolbox -framework CoreAudio -framework CoreFoundation -framework IOKit \
  -Wl,-rpath,@executable_path -o "$out/rs2014_lp"

# libRSOpenGL's install name is @executable_path/libRSOpenGL.dylib
cp "$gl/libRSOpenGL.dylib" "$out/"
for d in msvcr100 msvcp100 d3dx9_42 d3dcompiler_42 bink2w32; do
  if [ -f "$b/modules/in/$d.dll" ]; then cp "$b/modules/in/$d.dll" "$out/dlls/"; fi
done
if [ -n "${LP_IMAGE:-}" ]; then cp "$LP_IMAGE" "$out/lp_game.exe"; fi
echo "===== Done: $out/rs2014_lp ====="
