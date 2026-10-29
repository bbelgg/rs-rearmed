#!/bin/bash
# Usage: tools/play-native/warm-shaders.sh [APP]
#
# The install's shader warm-up (docs/METAL-DESIGN.md "Safe load"): compiles the Metal shader libraries
# the game will need (the disk cache's manifest and the app's bundled shader corpus) once, now, so
# the first Metal start does not spend minutes on "Preparing shaders" (or, with a fast-load
# cache.psarc and no warm-up at all, stall on the loading screen). Runs APP's own executable with
# --warm-shaders (default: the installed app, $RS_NATIVE_HOME/game/Rocksmith2014.app): no window, no
# game. Metal's shader cache belongs to an app (its bundle identifier and bundle path), so only the
# installed app itself can fill the one the game reads; it is run by its resolved path, as
# LaunchServices starts it. A few minutes the first time (about 7 on an M2), a second or so when
# everything is compiled already. RS_WARM_SHADERS=0 skips it.
#
# stdout: "progress F Preparing shaders… N of M" lines (F: 0..1; the installer's progress bar), on a
# terminal one updating line instead. Never fails the install: without the warm-up the game warms
# up on its first Metal start ("Preparing shaders" on the black window).
set -uo pipefail
home=${RS_NATIVE_HOME:-"$HOME/Library/Application Support/RS Re-ARMed"}
app=${1:-"$home/game/Rocksmith2014.app"}

if [[ ${RS_WARM_SHADERS:-1} == 0 ]]; then
    echo "shader warm-up skipped (RS_WARM_SHADERS=0)"
    exit 0
fi
dir=$(cd -P "$app/Contents/MacOS" 2>/dev/null && pwd) || { echo "warm-shaders: no app at $app" >&2; exit 0; }
exe=$dir/Rocksmith2014
[[ -x $exe ]] || { echo "warm-shaders: no executable in $app" >&2; exit 0; }
# A build from before the warm-up would start the game instead.
if ! grep -qa -- '--warm-shaders' "$exe"; then
    echo "warm-shaders: this build has no shader warm-up; skipped"
    exit 0
fi
[[ -f "$app/Contents/Resources/rsmsl-corpus.bin" ]] ||
    echo "warm-shaders: the app has no shader corpus; only shaders known from earlier runs are compiled"

echo "Preparing shaders (once per install or macOS update; the first time takes a few minutes)..."
if [[ -t 1 ]]; then
    "$exe" --warm-shaders 2>/dev/null | awk '
        $1 == "progress" { sub(/^progress [^ ]* /, ""); printf "\r%s   ", $0; fflush(); next }
        { printf "\n%s\n", $0 }'
else
    "$exe" --warm-shaders 2>&1
fi
rc=${PIPESTATUS[0]}
case $rc in
    0) ;;
    1) echo "warm-shaders: timed out; the game compiles the rest on its first Metal start" ;;
    *) echo "warm-shaders: failed (status $rc); the game warms up on its first Metal start instead" ;;
esac
exit 0
