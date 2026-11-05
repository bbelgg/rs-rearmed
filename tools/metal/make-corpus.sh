#!/bin/sh
# The Metal backend's offline shader corpus for a user's build, made from the game files alone
# (docs/mac/METAL.md "Shader translation"): no game run, no shader dumps. tools/ws7/recomp/app/
# build-m4.sh runs it and bundles the result; the installer's warm-up compiles it.
#
#   tools/metal/make-corpus.sh OUTDIR
#   tools/metal/make-corpus.sh --learn-play OUTDIR
#
# 1. Scaleform: tools/metal/extract_scaleform.py reads the GL HAL's shader sources from the game
#    image (RC_IMAGE) as the HAL compiles them (the same 213 shaders a game run dumps);
# 2. Cg: shims/cg/tools/extract_corpus.py takes the game's Cg programs from static.psarc (RS_GAME_DIR),
#    cg_corpus_load --dump-glsl turns them into the GLSL the Cg shim generates;
# 3. tools/metal/build-msl-corpus.sh pairs vertex and fragment shaders into programs and translates
#    them: OUTDIR/msl/rsmsl-corpus.bin.
# With --learn-play (win-x86/install/lp_build.py runs it, docs/windows/PERFORMANCE.md "Shader preparation"), the Windows
# game: its shaders are Direct3D bytecode, in the game image and the shaders of static.psarc, and any vertex
# shader can go with any pixel shader, so steps 1 and 2 are win-x86/shaders/lp_shaders.c, which makes the GLSL of the
# programs listed in win-x86/shaders/programs.tsv by the D3D9 layer's own code. Step 3 is the same.
# What it cannot know: shaders made at run time from anything but these sources (none seen in game
# runs so far); a program missing from the corpus is translated when the game links it, as before.
#
# env: RC_IMAGE        the game image (default build/steamless/Rocksmith2014_x64_decrypted; required
#                      with --learn-play)
#      RS_GAME_DIR     the game folder (default: the Steam library's)
#      MSL_VERIFY      msl_verify       (default build/shims/opengl/msl_verify)
#      CG_CORPUS_LOAD  cg_corpus_load   (default build/shims/cg/cg_corpus_load)
#      LP_SHADERS      lp_shaders       (--learn-play; default build/win-x86/lp/obj/lp_shaders)
#      RS_MSL_VERIFY=1 also check every program against GL (minutes; the developer's check)
# stdout: "progress F LABEL" lines (F: 0..1 of this script) and a summary; details in OUTDIR/*.log.
# Everything in OUTDIR comes from the game: keep it on this Mac, never commit it.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
learn_play=0
if [ "${1:-}" = --learn-play ]; then learn_play=1; shift; fi
out=${1:?usage: make-corpus.sh [--learn-play] OUTDIR}
if [ "$learn_play" = 1 ]; then
  img=${RC_IMAGE:?set RC_IMAGE to the Windows game image}
else
  img=${RC_IMAGE:-$root/build/steamless/Rocksmith2014_x64_decrypted}
fi
game=${RS_GAME_DIR:-$HOME/Library/Application Support/Steam/steamapps/common/Rocksmith2014}
verify=${MSL_VERIFY:-$root/build/shims/opengl/msl_verify}
cgload=${CG_CORPUS_LOAD:-$root/build/shims/cg/cg_corpus_load}
lpshaders=${LP_SHADERS:-$root/build/win-x86/lp/obj/lp_shaders}
progress() { printf 'progress %s %s\n' "$1" "$2"; }

[ -f "$img" ] || { echo "make-corpus: no game image at $img (RC_IMAGE)" >&2; exit 1; }
[ -f "$game/static.psarc" ] || { echo "make-corpus: no static.psarc in $game (RS_GAME_DIR)" >&2; exit 1; }
[ "$learn_play" = 1 ] && tool=$lpshaders || tool=$cgload
for t in "$verify" "$tool"; do
  [ -x "$t" ] || { echo "make-corpus: $t is missing: build the shims first (cmake --build build)" >&2; exit 1; }
done
mkdir -p "$out"

if [ "$learn_play" = 1 ]; then
  progress 0.00 "Reading the shaders"
  rm -rf "$out/static" "$out/glsl"
  python3 -I "$root/tools/psarc_extract.py" "$game/static.psarc" "$out/static" --match shaders/ > /dev/null
  "$lpshaders" "$root/win-x86/shaders/programs.tsv" "$out/glsl" "$img" "$out/static/shaders/dx9/"* \
    "$out/static/shaders/generated/"*
  rm -rf "$out/static"
  set -- --pairs "$out/glsl/pairs.tsv"
else
  progress 0.00 "Reading the Scaleform shaders"
  python3 -I "$here/extract_scaleform.py" "$img" "$out/scaleform"

  progress 0.10 "Reading the Cg shaders"
  python3 -I "$root/shims/cg/tools/extract_corpus.py" --game "$game" --out "$out/cg" > "$out/cg-extract.log" 2>&1 ||
    { tail -5 "$out/cg-extract.log" >&2; exit 1; }
  tail -1 "$out/cg-extract.log"
  # Exits 1 when a program NVIDIA's runtime loads fails in the shim: that one is missing from the corpus,
  # the rest is fine.
  "$cgload" --dump-glsl "$out/cg-glsl" "$out/cg/corpus/"*.?po > "$out/cg-glsl.log" 2>&1 ||
    echo "make-corpus: some Cg programs failed in the shim (see $out/cg-glsl.log); the corpus goes without them"
  rm -rf "$out/cg/static"  # the extracted static.psarc entries, only needed for the step above
  set -- --scaleform "$out/scaleform" --cg "$out/cg-glsl"
fi

progress 0.20 "Translating the shaders"
xonly=--translate-only
[ "${RS_MSL_VERIFY:-0}" = 1 ] && xonly=
rm -f "$out/msl.rc" "$out/msl/rsmsl-corpus.bin"  # a failed run must not leave the last corpus to bundle
# The checker's own progress lines, mapped onto 0.20..0.99 as they come; everything to the log.
{ sh "$here/build-msl-corpus.sh" --verify "$verify" --out "$out/msl" "$@" $xonly --progress 2>&1
  echo $? > "$out/msl.rc"; } |
  awk -v logf="$out/msl.log" '
    { print > logf; fflush(logf) }
    $1 == "progress" { f = $2; sub(/^progress [^ ]* /, ""); printf "progress %.3f %s\n", 0.20 + 0.79 * f, $0; fflush() }'
rc=$(cat "$out/msl.rc" 2>/dev/null || echo 1)
[ -f "$out/msl/rsmsl-corpus.bin" ] || { tail -20 "$out/msl.log" >&2; exit 1; }
[ "$rc" = 0 ] || echo "make-corpus: some programs failed (see $out/msl/failures.tsv); the corpus goes without them"
grep -v '^progress ' "$out/msl.log" | tail -4
progress 1.00 "Shader corpus ready"
echo "corpus: $out/msl/rsmsl-corpus.bin"
