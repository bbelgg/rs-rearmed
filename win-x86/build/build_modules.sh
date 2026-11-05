#!/bin/sh
# Translate the DLLs the game runs with (docs/windows/DESIGN.md: Libraries, D3DX), each as a module of
# its own (gen_image_pe.py --module NAME), and compile each tree to NAME/obj/libgame_rc.a.
#
#   msvcr100        the VC++ 2010 C runtime (also answers d3dx's msvcrt.dll imports)
#   msvcp100        the VC++ 2010 C++ library
#   d3dx9_42        D3DX (August 2009)
#   d3dcompiler_42  the HLSL compiler d3dx loads with LoadLibraryA; rebased to 0x11000000 first,
#                   since it shares d3dx9_42's preferred base
#   bink2w32        Bink 2 (docs/windows/DESIGN.md "Video (Bink)"), from the game folder; rebased to 0x12000000 (it
#                   also shares d3dx9_42's base)
#
# env (the DLLs are the user's copies: build inputs, never committed, nor anything made from them):
#   LP_MSVCR100, LP_MSVCP100, LP_D3DX9, LP_D3DCOMPILER   the four Microsoft DLLs (required)
#   LP_BINK2W32  bink2w32.dll (default: LP_GAME_DIR/bink2w32.dll)
#   LP_BUILD     build root (required); output in $LP_BUILD/modules:
#                  in/<name>.dll       the images the game loads at run time (D3DCompiler rebased)
#                  <name>/             generated C, <name>/obj/libgame_rc.a
#   LP_PY        a Python with capstone (default python3)
#   RS_NINJA     ninja (default: the one next to the Python, else ninja on PATH)
#   LP_JOBS      parallelism (default: hw.ncpu)
#   LP_MODULES   the modules to build (default: all five)
set -eu
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
recomp=$root/tools/ws7/recomp
out=${LP_BUILD:?set LP_BUILD to the build root}/modules
py=${LP_PY:-python3}
jobs=${LP_JOBS:-$(sysctl -n hw.ncpu)}
ninja=${RS_NINJA:-$(dirname "$py")/ninja}
[ -n "${RS_NINJA:-}" ] || [ -x "$ninja" ] || ninja=ninja
mods=${LP_MODULES:-msvcr100 msvcp100 d3dx9_42 d3dcompiler_42 bink2w32}
mkdir -p "$out/in"

for m in $mods; do
  case $m in
    msvcr100) src=${LP_MSVCR100:?set LP_MSVCR100 to msvcr100.dll} ;;
    msvcp100) src=${LP_MSVCP100:?set LP_MSVCP100 to msvcp100.dll} ;;
    d3dx9_42) src=${LP_D3DX9:?set LP_D3DX9 to d3dx9_42.dll} ;;
    d3dcompiler_42) src=${LP_D3DCOMPILER:?set LP_D3DCOMPILER to D3DCompiler_42.dll} ;;
    bink2w32) src=${LP_BINK2W32:-${LP_GAME_DIR:?set LP_BINK2W32 or LP_GAME_DIR}/bink2w32.dll} ;;
    *) echo "unknown module $m" >&2; exit 2 ;;
  esac
  if [ $m = d3dcompiler_42 ]; then
    "$py" -I "$root/win-x86/d3dx/rebase_pe.py" "$src" "$out/in/$m.dll" 11000000
  elif [ $m = bink2w32 ]; then
    "$py" -I "$root/win-x86/d3dx/rebase_pe.py" "$src" "$out/in/$m.dll" 12000000
  else
    cp "$src" "$out/in/$m.dll"
  fi
  echo "=== $m ==="
  mkdir -p "$out/$m"
  "$py" -I "$recomp/build/gen_image_pe.py" "$out/in/$m.dll" "$out/$m" --module $m \
    --jobs "$jobs" --cflags=-w --opt=-O2
  "$ninja" -C "$out/$m" -j "$jobs"
done
echo "=== modules in $out ==="
