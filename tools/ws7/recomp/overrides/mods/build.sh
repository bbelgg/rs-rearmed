#!/bin/sh
# Build the community-mod override module for arm64.
#   usage: tools/ws7/recomp/overrides/mods/build.sh OUTDIR
# Writes OUTDIR/lib/liboverrides_mods.a (the overrides), OUTDIR/lib/liboverrides_crypto_mods.a (the crypto
# module with the DSA verify adapter renamed, to be linked INSTEAD of liboverrides_crypto.a: the mods module
# overrides the same function, sng_verify, and wraps the renamed one) and OUTDIR/mods.skip (gen_image.py --skip).
# It does not need the shims/game libraries or the game image to build. Game names: RC_TARGET_DIR, as in
# ../build.sh.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
ov=$here/..
root=$(cd "$ov/../../../.." && pwd)
rt=$ov/../runtime
. "$ov/../build/optflags.sh"
out=${1:?usage: build.sh OUTDIR}
mkdir -p "$out/lib" "$out/obj/mods" "$out/obj/crypto_mods"
tgt=${RC_TARGET_DIR:-$out/target}
[ -n "${RC_TARGET_DIR:-}" ] || "${RC_PY:-python3}" -I "$root/tools/targets/targets.py" gen "$tgt"

CC="clang -arch arm64 -O2 -g0 -Wall -Wno-unused-function -ffp-contract=off -fno-strict-aliasing -I$ov -I$ov/src -I$here -I$rt -I$tgt $rc_lto $rc_pgo"  # RC_LTO, RC_PGO: build/optflags.sh
SH="-I$root/shims/game/include"

$CC -c "$here/mods.c" -o "$out/obj/mods/mods.o"
# what is one source for both editions (Learn & Play: win-x86/build/build_shims.sh): the switches, the log and the
# start, and the frame rate fixes, with the game's names looked up at run time as a mod's are
$CC -c "$here/mods_shared.c" -o "$out/obj/mods/mods_shared.o"
$CC -c "$here/framerate.c" -o "$out/obj/mods/framerate.o"
# this edition's own, apart from the game's tables: the FFT setups the game's import thunks ask for
$CC -c "$here/fftsetup.c" -o "$out/obj/mods/fftsetup.o"
# RS Re-ARMed's options in the Adv Display Settings menu, the methods in front of the game's
$CC -c "$here/menu_options.c" -o "$out/obj/mods/menu_options.o"
$CC -c "$here/menu_options_common.c" -o "$out/obj/mods/menu_options_common.o"
rm -f "$out/lib/liboverrides_mods.a"
ar rcs "$out/lib/liboverrides_mods.a" "$out/obj/mods/mods.o" "$out/obj/mods/mods_shared.o" "$out/obj/mods/framerate.o" \
    "$out/obj/mods/fftsetup.o" "$out/obj/mods/menu_options.o" "$out/obj/mods/menu_options_common.o"

# the crypto module with the DSA verifier renamed (the wrapper in mods.c calls it)
$CC $SH -DRC_FN_rsg_cryptopp_dsa_sha1_verify=rs_crypto_dsa_verify -c "$ov/gen/crypto.c" -o "$out/obj/crypto_mods/crypto.o"
$CC $SH -c "$ov/src/crypto_glue.c" -o "$out/obj/crypto_mods/crypto_glue.o"
rm -f "$out/lib/liboverrides_crypto_mods.a"
ar rcs "$out/lib/liboverrides_crypto_mods.a" "$out/obj/crypto_mods/crypto.o" "$out/obj/crypto_mods/crypto_glue.o"

cp "$tgt/skip/mods.skip" "$out/mods.skip"
echo "overrides/mods: $(grep -vc '^#' "$out/mods.skip") skip entries; libraries in $out/lib"
