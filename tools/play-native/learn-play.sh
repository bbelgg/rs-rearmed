#!/bin/bash
# Usage: tools/play-native/learn-play.sh [HARVEST]
#        tools/play-native/install.sh --learn-play [HARVEST]
#        tools/play-native/learn-play.sh --refresh
#
# The Windows game (Rocksmith 2014 Learn & Play, a purchased edition that Steam offers for Windows only) on a Mac,
# EXPERIMENTAL. One run goes from your game files to an installed app:
#   1. The game: found in Steam's depot download folder (or $LP_GAME_DIR, e.g. a folder copied from a
#      Windows PC) and checked against tools/targets/win-x86/target.toml. If it is not there, Steam is opened if it isn't
#      and told to download it (lp_install.py game-download): nothing to paste. This says what is left for you
#      to do, if anything (install Steam, sign in, the right account), and goes on by itself. Only if Steam
#      ignores the command does its console open with the command on the clipboard.
#      Steam downloads to the internal disk (inside its own app bundle) and needs the whole depot's size
#      free up front; when there is not enough room this asks for a folder on another disk (or takes
#      $LP_DOWNLOAD_DIR) and links Steam's download folder there.
#   1a. The DLC the signed-in account owns (gamefiles.py dlc-fetch): each DLC is a Steam depot of its own, and
#      Steam downloads them one after another the same way; a stop continues, one that fails is named at the end.
#   1b. The app's own copy of the game files, ~/Library/Application Support/RS Re-ARMed/learn-play/game
#      (lp_install.py game-copy: the game folder and the PC song archives, *_p.psarc, of the Mac game's
#      dlc folder; APFS clones on the same volume, which take no extra space, else a real copy after a
#      free-space check; a game on another APFS drive: the copy is on that drive, <drive>/RS Re-ARMed/
#      learn-play/game, and game/ is a link to it). The app reads only this copy, so Steam clearing its
#      download folder or an update changes nothing; --refresh syncs it again (new DLC) and changes nothing
#      else.
#   2. The Microsoft DLLs it loads (msvcr100, msvcp100, d3dx9_42, D3DCompiler_42), from Steam's Common
#      Redistributables, else downloaded from Microsoft, into $RS_BUILD_DIR/win-x86/dlls.
#   3. The code image, $RS_BUILD_DIR/win-x86/image/Rocksmith2014.exe, by itself when the unpacker's seed is
#      here ($LP_SEED, else win-x86/unpack/seed.bin; the released installer has one, git does not): your exe
#      unpacked offline (lp_install.py game-image), a few minutes. HARVEST (or $LP_HARVEST), a folder from
#      harvester v2 on a Windows PC, is used instead when it is given, and without a seed, built with
#      win-x86/image/lp_image.py ($LP_HARVEST_V1: a v1 harvest of the same build, its --reference). With neither,
#      and no image from an earlier run, it stops before this step and says how to make a capture.
#   4. The build (win-x86/install/lp_build.py): the game and the DLLs translated to C and compiled,
#      libRSOpenGL, the Win32 seam, the runtime and the app linked into one arm64 binary, in
#      $RS_BUILD_DIR/win-x86/lp. About 26 minutes the first time on an M2 (most of it translating the game)
#      and about 2 GB free; a stopped run continues where it was, and each translation is redone only
#      when its input or the translator changed. It also makes the "Re-ARMed 2026" title logo in the copy,
#      game/static.psarc.rearmed (tools/loadscreen, RS_REARMED=0 not; its Python packages need the network once).
#   5. The install: ~/Applications/RS Re-ARMed Learn & Play.app ($LP_NATIVE_APP), ad-hoc signed, and its
#      folder ~/Library/Application Support/RS Re-ARMed/learn-play (game/: the copy; game-dir: the folder
#      the app reads, game/; gamefiles.py, cache_overlay.py: the copy's tools, for a refresh without the
#      repository; userdata/: Rocksmith.ini; steam/: profiles and progress; logs/). Then the build's
#      intermediates are deleted, except the translated libraries, a cache of about 140 MB
#      (LP_KEEP_BUILD=1 keeps everything, LP_KEEP_BUILD=0 nothing).
#   6. The Metal shaders (tools/play-native/warm-shaders.sh): the installed app compiles the build's shader corpus
#      once, under a minute the first time, so the first play does not stall on them. RS_WARM_SHADERS=0 skips it.
# Needs the Xcode command line tools (clang, make) and their python3; the build's Python packages
# (win-x86/install/requirements.txt) go into $RS_BUILD_DIR/venv with pip. Everything runs natively (no Rosetta, no Homebrew, no
# CMake or Ninja, no steamcmd); the Mac game install and its saves are never touched. The game runs offline.
# RS_STEAM_DIR: the Steam folder (default ~/Library/Application Support/Steam). LP_JOBS: parallel jobs.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
source "$root/tools/rs-env.sh"
build=${RS_BUILD_DIR:-$root/build}/win-x86
lp="$root/win-x86/install/lp_install.py"
harvest=${1:-${LP_HARVEST:-}}
[[ "$(uname -m)" == arm64 ]] || rs_die "an Apple Silicon Mac is needed"
xcode-select -p >/dev/null 2>&1 && command -v clang >/dev/null \
    || rs_die "the Xcode command line tools are missing: install them with  xcode-select --install  and run this again"
command -v python3 >/dev/null || rs_die "python3 is missing: install the Xcode command line tools (xcode-select --install)"
# With Xcode itself selected, every clang and python3 goes through it: one that isn't set up stops them with a macOS
# window asking for an administrator's password. Better said here than behind the build.
if [[ $(xcode-select -p) == *.app/* ]] && ! { xcodebuild -license check && xcodebuild -checkFirstLaunchStatus; } >/dev/null 2>&1; then
    rs_die "Xcode is not set up yet: open Xcode once, click Agree and Install (it asks for your Mac's password), then run this again"
fi
lpi() { python3 -I "$lp" "$@"; }
if [[ ${1:-} == --refresh ]]; then
    echo "== Refreshing the game files of RS Re-ARMed Learn & Play"
    python3 -I "$here/gamefiles.py" dlc-fetch "${LP_HOME:-$HOME/Library/Application Support/RS Re-ARMed/learn-play}/dlc-fetched.json"
    lpi game-refresh
    exit
fi
# The build's Python: win-x86/install/requirements.txt in $RS_BUILD_DIR/venv, made here the first time (pip; a
# few MB).
venv=${RS_BUILD_DIR:-$root/build}/venv
py=${RC_PY:-$venv/bin/python}
ensure_venv() {
    "$py" -c 'import capstone, unicorn, pefile' 2>/dev/null && return
    [[ -n ${RC_PY:-} ]] && rs_die "RC_PY=$RC_PY lacks a package (pip install -r win-x86/install/requirements.txt)"
    echo "== Python environment for the translator ($venv)"
    [[ -x $venv/bin/python ]] || python3 -m venv "$venv"
    # from a drive without extended attributes, ._ files next to the .pth files stop Python starting
    find "$venv" -name '._*' -delete 2>/dev/null || true
    "$venv/bin/python" -m pip install -q --disable-pip-version-check -r "$root/win-x86/install/requirements.txt" \
        || rs_die "could not install the Python packages into $venv (pip needs the network once)"
}

echo "RS Re-ARMed for the Windows game (Learn & Play), experimental."
lpi build-estimate | fold -s -w 100
mkdir -p "$build"
# The startup disk holds swap wherever the build folder is. An estimate: said, and the build goes on.
lpi startup-space "$build/lp" >/dev/null || { echo "Warning:"; lpi startup-space "$build/lp" | fold -s -w 100; } || true

echo "== The Windows game (Learn & Play)"
if [[ -n ${LP_GAME_DIR:-} ]]; then
    game=$(lpi game-locate --path-only "$LP_GAME_DIR") || exit $?
elif ! game=$(lpi game-locate --path-only 2>/dev/null); then
    # Not downloaded yet, or not completely; a different build cannot be used (game-locate says why).
    status=0
    lpi game-locate >/dev/null || status=$?
    [[ $status == 2 ]] && { lpi game-locate; exit 2; }
    # Room for the download, checked before every download: a Steam update can put back the folder a
    # link replaced.
    if [[ -n ${LP_DOWNLOAD_DIR:-} ]]; then
        lpi game-redirect "$LP_DOWNLOAD_DIR"
    fi
    echo "(Steam downloads it into a folder of its own; your Mac game and saves are not touched.)"
    while :; do
        while ! lpi game-space --with-build "$build/lp"; do
            [[ -t 0 ]] || rs_die "not enough free space for the download: set LP_DOWNLOAD_DIR to a folder on a disk with room"
            read -r -p "Folder on another disk for Steam's download (empty to stop): " dir
            [[ -n $dir ]] || exit 1
            lpi game-redirect "${dir/#\~/$HOME}" || true
        done
        status=0
        lpi game-download || status=$?
        [[ $status == 4 ]] || break  # no room after all: a Steam update can drop the link
    done
    [[ $status == 0 ]] || exit $status
    game=$(lpi game-locate --path-only)
fi
echo "game: $game"

# The DLC this Steam account owns: each a depot of its own, fetched the same way (nothing if it owns none).
if [[ -z ${LP_GAME_DIR:-} ]]; then
    echo "== Your DLC"
    python3 -I "$here/gamefiles.py" dlc-fetch "${LP_HOME:-$HOME/Library/Application Support/RS Re-ARMed/learn-play}/dlc-fetched.json"
fi

echo "== The app's own copy of the game files"
lpi game-copy "$game"
game=${LP_HOME:-$HOME/Library/Application Support/RS Re-ARMed/learn-play}/game

echo "== Microsoft libraries"
lpi redist "$build/dlls"

ensure_venv
echo "== Code image"
mkdir -p "$build/image"
img="$build/image/Rocksmith2014.exe"
unpinned=()
if [[ -n $harvest ]]; then
    # A capture of YOUR OWN game (rs-harvest), made once on a Windows PC with the one-step harvester shipped
    # in win-x86/harvest (harvest-game.bat). Re-ARMed ships no game code -- this is your own game's data,
    # captured by you. (A folder, i.e. unzipped rs-harvest.)
    [[ -d $harvest ]] || rs_die "no harvest folder at $harvest (unzip rs-harvest.zip and pass the folder)"
    "$py" -c 'import capstone' 2>/dev/null \
        || rs_die "lp_image.py needs the build's Python environment: pip install -r win-x86/install/requirements.txt"
    ref=()
    [[ -n ${LP_HARVEST_V1:-} ]] && ref=(--reference "$LP_HARVEST_V1")
    "$py" "$root/win-x86/image/lp_image.py" "$harvest" "$img" ${ref[@]+"${ref[@]}"} \
        --report "$build/image/report.txt"
    unpinned=(--unpinned)
elif [[ -f $img ]] && lpi image-check "$img" >/dev/null; then
    echo "(made by an earlier run; delete it to make it again)"
elif lpi seed-check >/dev/null; then
    # Nothing needed from the user: their own exe, unpacked offline with the seed, over an image that isn't the pinned one.
    lpi game-image "$game" "$img" --py "$py" --report "$build/image/report.txt"
elif [[ -f $img ]]; then
    echo "(from an earlier capture: not the image this version is made for, which a capture's can be; delete it to make it again)"
    unpinned=(--unpinned)
else
    # No seed here (a clone of the repository; the released installer has one) and no capture yet: build the
    # harvester folder here and open it, so the user can copy it to a Windows PC. The capture is a one-time
    # Windows step on the user's own game; we ship no game code.
    hx="$build/harvest"
    lpi harvest-export "$hx"
    [[ -d $hx ]] && open "$hx" 2>/dev/null || true
    cat <<EOF
This copy of RS Re-ARMed has no seed.bin (the released installer has one and needs no capture), so the
code image is built from a one-time capture of your own game, done on a Windows PC.
A harvester folder is ready at:
  $hx
(It has the prebuilt rsharvest2.exe: nothing to install, on this Mac or on the PC. Its source and
README.md are there too.)
  1. Copy that whole folder to a Windows PC with Rocksmith 2014 (Learn & Play) installed.
  2. Double-click harvest-game.bat there; reach the menu, play ~30s, press Enter. It writes
     Desktop\\rs-harvest.zip.
  3. Copy rs-harvest.zip back to this Mac, unzip it (e.g. to ~/Downloads/rs-harvest), and re-run:
       tools/play-native/learn-play.sh ~/Downloads/rs-harvest
We ship no game code; the capture is your own game's data and stays with you.
EOF
    exit 0
fi
echo "code image: $img"

echo "== Building the game (translating it the first time takes most of the time; a stopped run continues)"
lpi build "$build/lp" --image "$img" ${unpinned[@]+"${unpinned[@]}"} --dlls "$build/dlls" --game "$game" --py "$py" \
    ${LP_JOBS:+--jobs "$LP_JOBS"}

echo "== Installing"
lpi app-install "$build/lp" --game "$game" ${LP_NATIVE_APP:+--app "$LP_NATIVE_APP"}
case ${LP_KEEP_BUILD:-} in
    1) echo "(build files kept in $build/lp)" ;;
    0) lpi build-clean --all "$build/lp" ;;
    *) lpi build-clean "$build/lp" ;;
esac
app=${LP_NATIVE_APP:-$HOME/Applications/RS Re-ARMed Learn & Play.app}
"$here/warm-shaders.sh" "$app"
cat <<EOF

Done. Start the game with:
  open "$app"
or double-click "$(basename "$app" .app)" in $(dirname "$app") (Launchpad and Spotlight find it too).
macOS asks for microphone access the first time the game listens for your guitar. The game runs
offline; no Steam client is needed while you play. Logs: ~/Library/Application Support/RS Re-ARMed/learn-play/logs
EOF
