#!/bin/sh
# Build and run the D3D9 layer's host test (d3d9_test.c): no game, no guest code. It needs a
# built libRSOpenGL (cmake --build <dir> --target opengl).
#
#   win-x86/test/d3d9_test.sh <cmake build dir> [out dir] [-- test args]
#
# The test runs on both libRSOpenGL backends (RS_GL_BACKEND=metal, then gl). Extra arguments
# after -- go to the test (e.g. --corpus DIR: translate and link every shader blob in DIR).
set -eu
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
cm=${1:?usage: d3d9_test.sh <cmake build dir> [out dir] [-- args]}
shift
out=${1:-$cm/d3d9-test}
[ $# -gt 0 ] && shift
[ "${1:-}" = "--" ] && shift
gl=$cm/shims/opengl
mkdir -p "$out"
CC="clang -arch arm64 -O1 -g -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -DRC_GUEST32"
INC="-I$root/tools/ws7/recomp/runtime -I$root/win-x86/shims -I$root/win-x86/shims/d3d9 -I$root/shims/opengl/src"
$CC $INC -o "$out/d3d9_test" \
    "$here/d3d9_test.c" \
    "$root"/win-x86/shims/d3d9/*.c \
    "$root/win-x86/shims/win32_d3d9.c" \
    "$root/win-x86/shims/win32_frame.c" \
    "$root/win-x86/shims/win32_guestobj.c" \
    "$root/tools/ws7/recomp/runtime/rc32_mem.c" \
    -L"$gl" -lRSOpenGL
# libRSOpenGL's install name is @executable_path/libRSOpenGL.dylib
cp "$gl/libRSOpenGL.dylib" "$out/"
for be in metal gl; do
    echo "--- RS_GL_BACKEND=$be"
    RS_GL_BACKEND=$be "$out/d3d9_test" "$@"
done
