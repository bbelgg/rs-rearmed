#!/bin/sh
# Build the community-mod override module for arm64.
#   usage: tools/ws7/recomp/overrides/mods/build.sh OUTDIR
# Writes OUTDIR/lib/liboverrides_mods.a (the overrides), OUTDIR/lib/liboverrides_crypto_mods.a (the crypto
# module with the DSA verify adapter renamed, to be linked INSTEAD of liboverrides_crypto.a: the mods module
# defines F_10137683d itself and wraps the renamed one) and OUTDIR/mods.skip (gen_image.py --skip).
# It does not need the shims/game libraries or the game image to build.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
ov=$here/..
root=$(cd "$ov/../../../.." && pwd)
rt=$ov/../runtime
. "$ov/../build/optflags.sh"
out=${1:?usage: build.sh OUTDIR}
mkdir -p "$out/lib" "$out/obj/mods" "$out/obj/crypto_mods"

CC="clang -arch arm64 -O2 -g0 -Wall -Wno-unused-function -ffp-contract=off -fno-strict-aliasing -I$ov -I$ov/src -I$here -I$rt $rc_lto $rc_pgo"  # RC_LTO, RC_PGO: build/optflags.sh
SH="-I$root/shims/game/include"

$CC -c "$here/mods.c" -o "$out/obj/mods/mods.o"
rm -f "$out/lib/liboverrides_mods.a"
ar rcs "$out/lib/liboverrides_mods.a" "$out/obj/mods/mods.o"

# the crypto module with F_10137683d renamed (the wrapper in mods.c calls it)
$CC $SH -DF_10137683d=rs_crypto_F_10137683d -c "$ov/gen/crypto.c" -o "$out/obj/crypto_mods/crypto.o"
$CC $SH -c "$ov/src/crypto_glue.c" -o "$out/obj/crypto_mods/crypto_glue.o"
rm -f "$out/lib/liboverrides_crypto_mods.a"
ar rcs "$out/lib/liboverrides_crypto_mods.a" "$out/obj/crypto_mods/crypto.o" "$out/obj/crypto_mods/crypto_glue.o"

cp "$here/mods.skip" "$out/mods.skip"
echo "overrides/mods: $(grep -vc '^#' "$out/mods.skip") skip entries; libraries in $out/lib"
