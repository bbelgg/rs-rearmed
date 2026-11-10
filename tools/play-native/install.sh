#!/bin/bash
# Usage: tools/play-native/install.sh [APP]
#        tools/play-native/install.sh --refresh-game
#
# Installs the native arm64 build for everyday play:
#   ~/Library/Application Support/RS Re-ARMed/   game/: the install's own copy of the game files (from
#       RS_GAME_DIR, default Steam's library; tools/play-native/gamefiles.py: APFS clones when the game is on
#       the same volume, which take no extra space, else a real copy; a game on another APFS drive: the copy is
#       on that drive, <drive>/RS Re-ARMed/game, and game/ a link to it) with a copy of APP (default: the
#       build-m4.sh output, $RS_BUILD_DIR/m4/app/Rocksmith2014.app, RS_BUILD_DIR default <repo>/build),
#       libnetguard.dylib, gamefiles.py, cache_overlay.py and psarc_key.py (for a refresh without the repository),
#       in game/ also the "Re-ARMed 2026" title logo, static.psarc.rearmed (tools/loadscreen, once, unless
#       RS_REARMED=0; RS_PY or the build's venv has its packages, else it makes its own with pip),
#       steam/ (the shim's offline cloud, made on first launch), logs/
#   ~/Applications/RS Re-ARMed.app               the launcher (tools/play-native/launch.sh)
# The game then reads only that copy: a Steam update, uninstall or library move does not change it.
# --refresh-game syncs the copy with the game folder again (new DLC and custom songs, a Steam update) and
# changes nothing else; a full install does that too. game-source holds the folder it is copied from.
# The renderer: RS_RENDERER=metal|opengl, written to <home>/renderer (the launcher and the game read
# it; docs/mac/INSTALL.md "Renderer"); without RS_RENDERER an existing setting is kept, else metal.
# With Metal it then compiles the game's Metal shaders (tools/play-native/warm-shaders.sh: a few
# minutes the first time; RS_WARM_SHADERS=0 skips it). With OpenGL there is nothing to prepare (a
# later switch to Metal compiles them as the game uses them, or run warm-shaders.sh then).
# Re-run it after a new build to update the game. Saves in steam/ are kept. Never touches the
# install or the real Steam folder. RS_NATIVE_HOME / RS_NATIVE_LAUNCHER override the two paths.
#
#        tools/play-native/install.sh --learn-play [HARVEST]
# The Windows game (Learn & Play) instead, for a Mac without the Mac game: tools/play-native/learn-play.sh
# (from the game files to ~/Applications/RS Re-ARMed Learn & Play.app). Offered when neither a
# build nor the Mac game is there.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
source "$root/tools/rs-env.sh"
if [[ ${1:-} == --learn-play ]]; then
    shift
    exec "$here/learn-play.sh" "$@"
fi
home=${RS_NATIVE_HOME:-"$HOME/Library/Application Support/RS Re-ARMed"}
command -v python3 >/dev/null || rs_die "python3 is missing: install the Xcode command line tools (xcode-select --install)"
progress=()
[[ -t 1 ]] || progress=(--progress-lines)  # the installer app's progress format
if [[ ${1:-} == --refresh-game ]]; then
    # (a link to a copy on a drive that is not connected: gamefiles.py says so)
    [[ -d "$home/game/.rearmed" || -L "$home/game" ]] || rs_die "no copy of the game files in $home/game: run install.sh first"
    exec python3 -I "$here/gamefiles.py" ${progress[@]+"${progress[@]}"} refresh "$home/game"
fi
app=${1:-${RS_BUILD_DIR:-$root/build}/m4/app/Rocksmith2014.app}
if [[ ! -x "$app/Contents/MacOS/Rocksmith2014" && ! -d $RS_GAME_APP && -t 0 ]]; then
    echo "Rocksmith 2014 for Mac is not installed ($RS_GAME_APP)."
    read -r -p "Set up the Windows game (Learn & Play) instead? [y/N] " answer
    [[ $answer == [yY]* ]] && exec "$here/learn-play.sh"
fi
launcher=${RS_NATIVE_LAUNCHER:-"$HOME/Applications/RS Re-ARMed.app"}
netguard=$root/build/ws8/libnetguard.dylib

[[ -x "$app/Contents/MacOS/Rocksmith2014" ]] || rs_die "no native build at $app (tools/ws7/recomp/app/build-m4.sh)"
[[ "$(lipo -archs "$app/Contents/MacOS/Rocksmith2014")" == arm64 ]] || rs_die "$app is not arm64"
[[ -f $netguard ]] || rs_die "build libnetguard first (tools/ws8/netguard/build.sh)"
pgrep -x Rocksmith2014 >/dev/null && rs_die "quit Rocksmith 2014 first"
renderer=$(echo "${RS_RENDERER:-$(cat "$home/renderer" 2>/dev/null)}" | tr -d '[:space:]' | tr '[:upper:]' '[:lower:]')
case $renderer in
    metal|opengl) ;;
    gl) renderer=opengl ;;
    '') renderer=metal ;;
    *) [[ -n ${RS_RENDERER:-} ]] && rs_die "RS_RENDERER=$RS_RENDERER: use metal or opengl"
       renderer=metal ;;  # an unreadable setting: the default
esac

mkdir -p "$home/logs" "$home/steam"
# The game files: the install's own copy (only what the game reads; DLC and custom songs as dlc/*_m.psarc).
# Clones on the same APFS volume; a real copy otherwise, after a check that there is room. A new copy of a
# game on another APFS drive goes onto that drive (<drive>/RS Re-ARMed/game, clones) and game/ is a link to it;
# RS_GAME_COPY_DIR=DIR puts it in DIR/game (home: here). A stopped copy continues on the next run.
echo "game files: $RS_GAME_DIR -> $home/game"
python3 -I "$here/gamefiles.py" ${progress[@]+"${progress[@]}"} sync "$RS_GAME_DIR" "$home/game" --edition mac ||
    rs_die "the game files could not be copied (see above); nothing else was changed"
cp "$here/gamefiles.py" "$here/cache_overlay.py" "$here/psarc_key.py" "$home/"
# The title logo goes in the copy, as Learn & Play's does, and not in Steam's folder. Without it the game shows
# its own.
if [[ ${RS_REARMED:-1} != 0 && ! -f "$home/game/static.psarc.rearmed" ]]; then
    echo "title logo: building static.psarc.rearmed in the copy"
    RS_GAME_DIR="$home/game" RS_PY=${RS_PY:-$(ls "${RS_BUILD_DIR:-$root/build}/venv/bin/python" 2>/dev/null)} \
        "$root/tools/loadscreen/build.sh" "$home/game/static.psarc.rearmed" ||
        echo "title logo: not made, the game shows its own"
fi
rm -rf "$home/game/Rocksmith2014.app.new"
cp -R "$app" "$home/game/Rocksmith2014.app.new"
for r in RS2014.icns en.lproj; do  # the original app's icon and strings
    [[ -e "$RS_GAME_APP/Contents/Resources/$r" ]] && cp -R "$RS_GAME_APP/Contents/Resources/$r" "$home/game/Rocksmith2014.app.new/Contents/Resources/"
done
# From a drive without extended attributes (exFAT, FAT), cp copies the "._" file macOS keeps next to each file.
find "$home/game/Rocksmith2014.app.new" -name '._*' -delete
# Builds from before CFBundleIconFile was in app/Info.plist: add it (the Dock icon).
plist="$home/game/Rocksmith2014.app.new/Contents/Info.plist"
if ! /usr/libexec/PlistBuddy -c 'Print :CFBundleIconFile' "$plist" >/dev/null 2>&1; then
    plutil -insert CFBundleIconFile -string RS2014.icns "$plist"
fi
# Re-sign: the files added above are outside the build's signature, which broke the bundle's seal.
codesign --force -s - "$home/game/Rocksmith2014.app.new/Contents/MacOS/Rocksmith2014" 2>/dev/null || true
find "$home/game/Rocksmith2014.app.new" -name '._*' -delete
rm -rf "$home/game/Rocksmith2014.app"
mv "$home/game/Rocksmith2014.app.new" "$home/game/Rocksmith2014.app"
# A game folder without the game's program left the copy's archives unread (gamefiles.py): the app is here now.
if [[ -e "$home/game/.rearmed/key-pending" ]]; then
    python3 -I "$here/gamefiles.py" ${progress[@]+"${progress[@]}"} refresh "$home/game" || true
fi
cp "$netguard" "$home/libnetguard.dylib"
printf '%s\n' "$home/game" > "$home/game-dir"  # the folder the game reads (marks an install, rsp_renderer_home)
printf '%s\n' "$RS_GAME_DIR" > "$home/game-source"  # where the copy comes from (a refresh reads .rearmed/source.json)
printf '%s\n' "$renderer" > "$home/renderer"
rm -f "$home/metal-session"  # a new build: Metal's crash count starts again

# The launcher app: launch.sh, run by a small program of its own (launch-stub.c says why).
# Compiled before the old launcher goes: a compiler that fails leaves the app the player starts in place.
clang -O2 -o "$home/launch.new" "$here/launch-stub.c"
rm -rf "$launcher"
mkdir -p "$launcher/Contents/MacOS" "$launcher/Contents/Resources"
mv -f "$home/launch.new" "$launcher/Contents/MacOS/launch"
cp "$here/launch.sh" "$launcher/Contents/Resources/launch.sh"
chmod +x "$launcher/Contents/Resources/launch.sh"
[[ -f "$RS_GAME_APP/Contents/Resources/RS2014.icns" ]] && cp "$RS_GAME_APP/Contents/Resources/RS2014.icns" "$launcher/Contents/Resources/"
version=$(head -1 "$root/VERSION")
cat > "$launcher/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleExecutable</key><string>launch</string>
  <key>CFBundleIdentifier</key><string>io.github.bbelgg.rs-rearmed.launcher</string>
  <key>CFBundleName</key><string>RS Re-ARMed</string>
  <key>CFBundleDisplayName</key><string>RS Re-ARMed</string>
  <key>CFBundleIconFile</key><string>RS2014.icns</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>$version</string>
  <key>LSUIElement</key><true/>
</dict>
</plist>
EOF
codesign --force -s - "$launcher" 2>/dev/null || true
# Let the Finder and the Dock pick up the new bundles and their icons.
lsregister=/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister
[[ -x $lsregister ]] && { "$lsregister" -f "$launcher" "$home/game/Rocksmith2014.app" 2>/dev/null || true; }
touch "$launcher" "$home/game/Rocksmith2014.app"
echo "installed: $home/game/Rocksmith2014.app"
echo "launcher:  $launcher"
echo "renderer:  $renderer (switch: echo metal or opengl > \"$home/renderer\")"
# Compile the Metal shaders now rather than on the first Metal start (tools/play-native/warm-shaders.sh;
# RS_WARM_SHADERS=0 skips it, a second or so when everything is compiled already). Not for OpenGL.
if [[ $renderer == metal ]]; then
    "$here/warm-shaders.sh" "$home/game/Rocksmith2014.app"
else
    echo "shader warm-up skipped (renderer opengl; tools/play-native/warm-shaders.sh prepares them for a switch to Metal)"
fi
