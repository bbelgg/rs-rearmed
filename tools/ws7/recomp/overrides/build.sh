#!/bin/sh
# Build the override modules for arm64.
#   usage: tools/ws7/recomp/overrides/build.sh OUTDIR
# Writes OUTDIR/lib/liboverrides_<module>.a (one per module), OUTDIR/lib/liboverrides_rt.a (rc_ov.c, the
# support every adapter needs), OUTDIR/lib/liboverrides.a (all default modules and the support in one), the
# arm64 slices of the shims/game libraries the adapters call (OUTDIR/lib/librs_*.a), and OUTDIR/overrides.skip
# (gen_image.py --skip). The generated adapters are in gen/ and are checked in.
#
# Modules (default set): lua png jpeg zlib efd efd_qi crypto online. Optional: efd_ds (DataStream: malloc
# vs the efd allocator), not in liboverrides.a and not in overrides.skip.
# A link also needs: -lz, -lc++ (online), and CoreFoundation-free libc only; no network library.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../../.." && pwd)
rt=$here/../runtime
. "$here/../build/optflags.sh"
out=${1:?usage: build.sh OUTDIR}
shims=${SHIMS_BUILD:-$root/build/shims/game}
mkdir -p "$out/lib" "$out/obj"

# arm64 slices of the shims libraries
for l in game lua51 png15 jpeg8 online; do
  src=$shims/librs_$l.a
  if [ ! -f "$src" ]; then echo "missing $src: build shims/game first (cmake -S shims/game -B build/shims/game)" >&2; exit 1; fi
  if lipo -info "$src" 2>/dev/null | grep -q 'Architectures in the fat file'; then
    lipo -thin arm64 "$src" -output "$out/lib/librs_$l.a"
  else
    lipo -info "$src" | grep -q arm64 || { echo "$src has no arm64 slice" >&2; exit 1; }
    cp "$src" "$out/lib/librs_$l.a"
  fi
done

CC="clang -arch arm64 -O2 -g0 -Wall -Wno-unused-function -ffp-contract=off -fno-strict-aliasing -I$here -I$here/src -I$rt $rc_lto $rc_pgo"  # RC_LTO, RC_PGO: build/optflags.sh
G=$root/shims/game
LUA="-DLNUM_DOUBLE -DLNUM_INT64 -I$G/third_party/lua-5.1.4-lnum/src"
PNG="-include math.h -I$G/third_party/libpng-1.5.13"
JPEG="-I$G/third_party/jpeg-8d"
SH="-I$G/include"

# module | flags | sources
build_module() {
  name=$1; flags=$2; shift 2
  mkdir -p "$out/obj/$name"
  objs=
  for s in "$@"; do
    o=$out/obj/$name/$(basename "$s" .c).o
    $CC $flags -c "$s" -o "$o"
    objs="$objs $o"
  done
  rm -f "$out/lib/liboverrides_$name.a"
  ar rcs "$out/lib/liboverrides_$name.a" $objs
}
build_module rt "" "$here/src/rc_ov.c"
build_module lua "$LUA" "$here/gen/lua.c" "$here/src/lua_glue.c"
build_module png "$PNG" "$here/gen/png.c" "$here/src/png_glue.c"
build_module jpeg "$JPEG" "$here/gen/jpeg.c"
build_module zlib "" "$here/gen/zlib.c"
build_module efd "$SH" "$here/gen/efd.c" "$here/src/efd_glue.c"
build_module efd_qi "$SH" "$here/gen/efd_qi.c"
build_module efd_ds "$SH" "$here/gen/efd_ds.c"
build_module crypto "$SH" "$here/gen/crypto.c" "$here/src/crypto_glue.c"
build_module online "$SH" "$here/gen/online.c" "$here/src/online_glue.c"

# one library with the default set
rm -f "$out/lib/liboverrides.a"
libtool -static -o "$out/lib/liboverrides.a" $(for m in rt lua png jpeg zlib efd efd_qi crypto online; do echo "$out/lib/liboverrides_$m.a"; done) 2>/dev/null

# the skip file for gen_image.py: the default modules.
cat "$here/lua.skip" "$here/png.skip" "$here/jpeg.skip" "$here/zlib.skip" "$here/efd.skip" "$here/efd_qi.skip" \
    "$here/crypto.skip" "$here/online.skip" > "$out/overrides.skip"
echo "overrides: $(grep -vc '^#' "$out/overrides.skip") skip entries; libraries in $out/lib"
