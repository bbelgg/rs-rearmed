#!/bin/sh
# Full build: the arm64 app from the recompiled image plus the native overrides.
#
#   tools/ws7/recomp/app/build-m4.sh [OUTDIR]     (default $RS_BUILD_DIR/m4)
#
# 1. builds the override libraries (tools/ws7/recomp/overrides/build.sh);
# 2. makes the skip list: every override module including `online` (offline
#    stub) and `mods` (the rsmacfixes and RSBypass patches), and un-drops, to
#    a fixpoint, every dropped address that generated code still references;
# 3. builds the image with that list (tools/ws7/recomp/build/build.sh) and links
#    OUTDIR/app/Rocksmith2014.app (tools/ws7/recomp/app/build.sh).
# Memory order: the whole image is compiled with -DRC_MO_TSO (x86 total store
# order): with only the threaded components fenced (Wwise, Scaleform, efd)
# song loading hangs or reaches a ud2, i.e. game code relies on TSO elsewhere too.
# Optimisation (unset = the default -O2 build; see tools/ws7/recomp/build/optflags.sh):
#   RC_OPT=-O3   the generated C;  RC_O1=FILE  functions compiled alone at -O1 (gen_image.py --o1)
#   RC_LTO=thin  ThinLTO, cache OUTDIR/lto-cache, RC_LTO_JOBS link threads (default 6)
#   RC_PGO=gen   instrumented app, profiles in OUTDIR/pgo (RC_PGO_DIR);  RC_PGO=use:FILE.profdata
#   RC_JOBS=N    compile jobs (default: all cores)
# Then: tools/play-native/install.sh OUTDIR/app/Rocksmith2014.app
set -eu
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../../.." && pwd)
build=${RS_BUILD_DIR:-$root/build}
out=${1:-$build/m4}
# The build scripts pass lists of paths as space-separated strings, so their own paths can't have spaces.
case "$root|$out" in *[[:space:]]*)
  echo "error: the repository and build paths must not contain spaces: $root, $out" >&2; exit 1 ;;
esac
mkdir -p "$out"
ov=$root/tools/ws7/recomp/overrides
export RC_LTO_CACHE=${RC_LTO_CACHE:-$out/lto-cache} RC_PGO_DIR=${RC_PGO_DIR:-$out/pgo}
echo "build: ${RC_OPT:--O2}${RC_LTO:+ lto=$RC_LTO}${RC_PGO:+ pgo=$RC_PGO}${RC_O1:+ o1=$RC_O1}"

# Progress for the installer: "progress F LABEL" lines on stdout, F the fraction of the whole build
# (the compilers' own output goes to the logs in OUTDIR).
progress() { printf 'progress %s %s\n' "$1" "$2"; }
progress 0.00 "Building the native libraries"
sh "$ov/build.sh" "$out/ov" > "$out/ov.log" 2>&1 || { tail -20 "$out/ov.log"; exit 1; }
sh "$ov/mods/build.sh" "$out/ov" > "$out/ov-mods.log" 2>&1 || { tail -20 "$out/ov-mods.log"; exit 1; }
lib=$out/ov/lib

# RC_OV_MODULES selects the override modules (default: all of them); handy for
# bisecting a regression to one module.
modules=${RC_OV_MODULES:-zlib lua png jpeg efd efd_qi crypto online dsp mods}
: > "$out/skip.txt"
for m in $modules; do
  if [ "$m" = mods ]; then cat "$out/ov/mods.skip"; else cat "$ov/$m.skip"; fi
done | grep -v '^#' >> "$out/skip.txt"

export RC_GEN_DIR=$out/gen RC_OBJ_DIR=$out/obj
echo 100000000-110000000 > "$out/tso-all.txt"
export RC_TSO=$out/tso-all.txt
for it in 1 2 3 4 5 6 7 8; do
  # Translation (gen_image.py: "  N/M functions") is 5-25% of the build, compiling (ninja: "[N/M]")
  # 25-85%; only the first pass reports, later ones recompile a few TUs.
  sh "$root/tools/ws7/recomp/build/build.sh" ${RC_JOBS:+--jobs $RC_JOBS} --skip "$out/skip.txt" ${RC_O1:+--o1 "$RC_O1"} 2>&1 |
    awk -v logf="$out/build.$it.log" -v show=$((it == 1)) '
      function report(f, what) {
        p = int(f * 100)
        if (show && p != last) { last = p; printf "progress %.2f %s\n", f, what; fflush() }
      }
      { print > logf; fflush(logf) }
      /^  [0-9]+\/[0-9]+ functions/ { split($1, a, "/"); report(0.05 + 0.20 * a[1] / a[2], "Translating the game to C") }
      /^\[[0-9]+\/[0-9]+\]/ { split(substr($1, 2), a, "[/\\]]"); report(0.25 + 0.60 * a[1] / a[2], "Compiling the game code") }' || true
  nm -u -A "$RC_OBJ_DIR/libgame_rc.a" 2>/dev/null | awk '{print $NF}' | grep '^_F_' | sed 's/^_F_//' | sort -u > "$out/refs.txt"
  awk '$2 == "drop" { print $1 }' "$out/skip.txt" | sort -u | comm -12 "$out/refs.txt" - > "$out/undrop.txt"
  n=$(wc -l < "$out/undrop.txt" | tr -d ' ')
  echo "image pass $it: $n dropped addresses still referenced"
  [ "$n" = 0 ] && break
  awk 'NR==FNR { u[$1] = 1; next } !($2 == "drop" && ($1 in u))' "$out/undrop.txt" "$out/skip.txt" > "$out/skip.new"
  mv "$out/skip.new" "$out/skip.txt"
done
[ "$n" = 0 ] || { echo "skip list did not converge" >&2; exit 1; }

o=""
# crypto_mods: the crypto module without the DSA verifier, which mods wraps
o=""
for m in $modules rt; do
  case " $modules " in *" mods "*) [ "$m" = crypto ] && m=crypto_mods ;; esac
  o="$o $lib/liboverrides_$m.a"
done
progress 0.85 "Linking the app"
RC_OVERRIDES="$o $lib/librs_game.a $lib/librs_lua51.a $lib/librs_png15.a $lib/librs_jpeg8.a $lib/librs_online.a" \
  sh "$here/build.sh" "$out/app" ${RC_JOBS:+--jobs $RC_JOBS}
if [ -s "$out/app/work/undefined.txt" ]; then
  echo "link: undefined symbols, see $out/app/work/undefined.txt" >&2
  exit 1
fi
progress 0.97 "Checking the app"
# (a gen build's self-test profile goes to work/, not into OUTDIR/pgo)
LLVM_PROFILE_FILE=$out/app/work/init-only-%p%c.profraw \
  "$out/app/Rocksmith2014.app/Contents/MacOS/Rocksmith2014" --init-only 2>&1 | grep 'init-only:'
echo "app: $out/app/Rocksmith2014.app"

# Build the "Re-ARMed" title logo unless RS_REARMED=0.
if [ "${RS_REARMED:-1}" != 0 ]; then
  game_dir="${RS_GAME_DIR:-$HOME/Library/Application Support/Steam/steamapps/common/Rocksmith2014}"
  rearmed_psarc="$game_dir/static.psarc.rearmed"
  if [ ! -f "$rearmed_psarc" ]; then
    echo "loadscreen: building Re-ARMed title logo..."
    sh "$root/tools/loadscreen/build.sh" "$rearmed_psarc" || echo "loadscreen: failed (non-fatal), skipping"
  fi
fi
