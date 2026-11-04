#!/bin/sh
# libRSOpenGL.dylib for the Learn & Play build without CMake: the same sources, flags and link as
# shims/opengl/CMakeLists.txt (+ msl.cmake, metal.cmake) give the `opengl` target, arm64 only.
# The installer uses it (no Homebrew, so no CMake or Ninja); a CMake build dir works as well
# (build.sh: LP_CMAKE_DIR).
#
#   OUT_DIR=DIR build_gl.sh      -> DIR/libRSOpenGL.dylib (install name @executable_path/libRSOpenGL.dylib)
#
# env: OUT_DIR (required), LP_JOBS (parallel compiles, default hw.ncpu)
set -eu
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
src=$root/shims/opengl
out=${OUT_DIR:?set OUT_DIR}
jobs=${LP_JOBS:-$(sysctl -n hw.ncpu)}
o=$out/obj
mkdir -p "$o"
sdk=$(xcrun --show-sdk-path)
python3 -I "$src/tools/gen-gl-api.py" "$src/game-gl-imports.txt" "$src/extra-gl-imports.txt" \
  "$sdk/System/Library/Frameworks/OpenGL.framework/Headers" "$o/gl_api.inc" "$o/rsmtl_stubs.c"
python3 -I "$src/tools/gen-gl-enums.py" "$sdk/System/Library/Frameworks/OpenGL.framework/Headers" "$o/gl_enums.inc"

common="-O2 -g -arch arm64 -mmacosx-version-min=11.0 -fPIC -fvisibility=hidden"
inc="-I$o -I$src/src -I$src/src/msl"
# one line per object: output, then the compile command's language flags and source
{
  for f in rsgl.c gltrace.c rs_gfx.c; do echo "$o/${f%.c}.o clang -std=gnu11 $inc $src/src/$f"; done
  echo "$o/rsmtl_stubs.o clang -std=gnu11 $inc $o/rsmtl_stubs.c"
  echo "$o/nsgl_hook.o clang -std=gnu11 $inc $src/src/nsgl_hook.m"
  for f in mtl_context mtl_state mtl_objects mtl_program mtl_draw mtl_extras mtl_cache; do
    echo "$o/$f.o clang -std=gnu11 -fobjc-arc $inc $src/src/metal/$f.m"
  done
  for f in msl_xlat msl_corpus; do echo "$o/$f.o clang++ -std=gnu++17 -I$src/src/msl $src/src/msl/$f.cpp"; done
} > "$o/jobs.txt"
# shellcheck disable=SC2016
xargs -P "$jobs" -L 1 sh -c 'out=$1; shift; exec "$@" '"$common"' -w -c -o "$out"' sh < "$o/jobs.txt"

clang++ -O2 -g -arch arm64 -mmacosx-version-min=11.0 -dynamiclib -Wl,-headerpad_max_install_names \
  -Xlinker -reexport_framework -Xlinker OpenGL -install_name @executable_path/libRSOpenGL.dylib \
  -o "$out/libRSOpenGL.dylib" "$o"/rsgl.o "$o"/nsgl_hook.o "$o"/gltrace.o "$o"/rs_gfx.o "$o"/rsmtl_stubs.o \
  "$o"/mtl_*.o "$o"/msl_*.o \
  -framework OpenGL -framework AppKit -framework ImageIO -framework Metal -framework QuartzCore -framework Foundation
echo "=== Done: $out/libRSOpenGL.dylib ==="
