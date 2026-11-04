#!/bin/sh
# Translate d3dx9_42.dll, D3DCompiler_42.dll and msvcr100.dll with the P2 front end and build
# d3dx_probe against all three.
#
#   build.sh <dir with the three DLLs> <out dir> [<cmake build dir with libRSOpenGL>]
#   then: <out dir>/d3dx_probe <out dir> [-v]
#
# env: PY  a Python with capstone (default: python3)
#
# The DLLs and everything generated from them stay in <out dir>: build inputs, never committed.
#
# Three images in one process, on the runtime's registry of images (each tree generated with --module,
# as in the game). D3DCompiler_42 shares d3dx9_42's preferred base (0x10000000) and is rebased to
# 0x11000000 first. msvcr100 serves d3dx9's and D3DCompiler's msvcrt.dll imports (every one of them is a
# msvcr100 export of the same name).
#
# With a cmake build dir (libRSOpenGL built: cmake --build <dir> --target opengl) the probe also
# links the D3D9 layer (win-x86/shims/d3d9) and runs D3DXCreateEffect against its device.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
recomp=$root/tools/ws7/recomp
rt=$recomp/runtime
dlls=$1
out=$2
cm=${3:-}
py=${PY:-python3}
mkdir -p "$out/in" "$out/obj"

find_dll() {  # case-insensitive name in $dlls
    f=$(ls "$dlls" | grep -i "^$1\$" | head -1)
    [ -n "$f" ] || { echo "no $1 in $dlls" >&2; exit 2; }
    echo "$dlls/$f"
}
cp "$(find_dll d3dx9_42.dll)" "$out/in/d3dx9_42.dll"
cp "$(find_dll msvcr100.dll)" "$out/in/msvcr100.dll"
"$py" -I "$here/../rebase_pe.py" "$(find_dll d3dcompiler_42.dll)" "$out/in/d3dcompiler_42.dll" 11000000

gen() {  # gen <dll> <module>
    "$py" -I "$recomp/build/gen_image_pe.py" "$out/in/$1" "$out/$2" --module "$2" --cflags=-w
    ninja -C "$out/$2"
}
gen msvcr100.dll msvcr100
gen d3dx9_42.dll d3dx9_42
gen d3dcompiler_42.dll d3dcompiler_42

CC="clang -arch arm64 -O1 -g -DRC_GUEST32 -I$rt -I$root/win-x86/shims -I$root/win-x86/shims/d3d9"
for f in rc32_mem rc32_image rc32_teb rc32_seh rc32_wrappers; do $CC -w -c "$rt/$f.c" -o "$out/obj/$f.o"; done
$CC -Wall -c "$root/win-x86/shims/win32_guestobj.c" -o "$out/obj/win32_guestobj.o"
$CC -Wall -c "$here/probe_images.c" -o "$out/obj/probe_images.o"
$CC -Wall -c "$here/probe_win32.c" -o "$out/obj/probe_win32.o"
# the D3D9 layer's bytecode -> GLSL translator (no GL needed) checks what D3DX produces
$CC -w -c "$root/win-x86/shims/d3d9/d3d9_shader.c" -o "$out/obj/d3d9_d3d9_shader.o"
# -all_load: the trees reach each other only through the registry, not by symbol
libs="-Wl,-all_load $out/msvcr100/obj/libgame_rc.a $out/d3dx9_42/obj/libgame_rc.a $out/d3dcompiler_42/obj/libgame_rc.a"
if [ -n "$cm" ]; then
    gl=$cm/shims/opengl
    INC="-I$root/shims/opengl/src"
    mkdir -p "$out/obj-d3d9"
    for f in "$root"/win-x86/shims/d3d9/*.c; do
        [ "$(basename "$f")" = d3d9_shader.c ] && continue
        $CC -w $INC -c "$f" -o "$out/obj-d3d9/$(basename "$f" .c).o"
    done
    $CC -Wall $INC -DPROBE_D3D9 -c "$here/d3dx_probe.c" -o "$out/obj/d3dx_probe.o"
    clang -arch arm64 "$out"/obj/*.o "$out"/obj-d3d9/*.o $libs -L"$gl" -lRSOpenGL -o "$out/d3dx_probe"
    cp "$gl/libRSOpenGL.dylib" "$out/"
else
    $CC -Wall -c "$here/d3dx_probe.c" -o "$out/obj/d3dx_probe.o"
    clang -arch arm64 "$out"/obj/*.o $libs -o "$out/d3dx_probe"
fi
echo "built $out/d3dx_probe"
