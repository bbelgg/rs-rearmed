#!/bin/sh
# Translate msvcr100.dll (and msvcp100.dll) with the P2 front end, each as a module of its own
# (gen_image_pe.py --module), and build crt_probe against them and the rc32 runtime.
#
#   build.sh <msvcr100.dll> [<msvcp100.dll>] <out dir>
#   then: <out dir>/crt_probe <msvcr100.dll> [<msvcp100.dll>] [-v] [-bench] [-eh <out dir>/eh.dll]
#
# env: PY  a Python with capstone (default: python3)
#
# The DLLs and everything generated from them stay in <out dir>: build inputs, never committed.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
recomp=$root/tools/ws7/recomp
rt=$recomp/runtime
crt=$1
if [ $# -ge 3 ]; then cpp=$2; out=$3; else cpp=; out=$2; fi
py=${PY:-python3}
mkdir -p "$out/obj"

libs=
defs=
for m in msvcr100 msvcp100; do
  if [ $m = msvcr100 ]; then dll=$crt; else dll=$cpp; fi
  [ -n "$dll" ] || continue
  mkdir -p "$out/$m"
  "$py" -I "$recomp/build/gen_image_pe.py" "$dll" "$out/$m" --module $m --cflags=-w
  ninja -C "$out/$m"
  libs="$libs $out/$m/obj/libgame_rc.a"
done
[ -n "$cpp" ] && defs=-DWITH_MSVCP100
# the exception cases (tools/ws7/recomp/test/x86/ehcases.py): needs clang's i386 PE target
(cd "$recomp" && "$py" -m test.x86.ehcases "$out/eh.dll")
if [ -f "$out/eh.dll" ]; then
  mkdir -p "$out/eh"
  "$py" -I "$recomp/build/gen_image_pe.py" "$out/eh.dll" "$out/eh" --module eh --cflags=-w
  ninja -C "$out/eh"
  libs="$libs $out/eh/obj/libgame_rc.a"
  defs="$defs -DWITH_EH"
fi

CC="clang -arch arm64 -O1 -g -DRC_GUEST32 -I$rt -I$root/win-x86/shims"
# the registry runtime without rc32_main.c: no main image in this process
for f in rc32_mem rc32_image rc32_teb rc32_seh rc32_wrappers; do $CC -w -c "$rt/$f.c" -o "$out/obj/$f.o"; done
$CC -Wall -c "$root/win-x86/shims/win32_guestobj.c" -o "$out/obj/win32_guestobj.o"
$CC -Wall -O2 -c "$root/win-x86/shims/win32_crtmath.c" -o "$out/obj/win32_crtmath.o"
$CC -Wall $defs -c "$here/crt_probe.c" -o "$out/obj/crt_probe.o"
$CC -Wall -c "$here/math_probe.c" -o "$out/obj/math_probe.o"
# -all_load: the trees reach each other only through the registry, not by symbol
clang -arch arm64 "$out"/obj/*.o -Wl,-all_load $libs -o "$out/crt_probe"
echo "built $out/crt_probe"
