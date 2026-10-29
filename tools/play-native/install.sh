#!/bin/bash
# Usage: tools/play-native/install.sh [APP]
#
# Installs the native arm64 build for everyday play:
#   ~/Library/Application Support/RS Re-ARMed/   game mirror with a copy of APP (default: the
#       build-m4.sh output, $RS_BUILD_DIR/m4/app/Rocksmith2014.app, RS_BUILD_DIR default <repo>/build),
#       libnetguard.dylib,
#       steam/ (the shim's offline cloud, made on first launch), logs/
#   ~/Applications/RS Re-ARMed.app               the launcher (tools/play-native/launch.sh)
# The renderer: RS_RENDERER=metal|opengl, written to <home>/renderer (the launcher and the game read
# it; docs/BUILDING.md "Renderer"); without RS_RENDERER an existing setting is kept, else metal.
# With Metal it then compiles the game's Metal shaders (tools/play-native/warm-shaders.sh: a few
# minutes the first time; RS_WARM_SHADERS=0 skips it). With OpenGL there is nothing to prepare (a
# later switch to Metal prepares them on its first Metal start, or run warm-shaders.sh then).
# Re-run it after a new build to update the game. Saves in steam/ are kept. Never touches the
# install or the real Steam folder. RS_NATIVE_HOME / RS_NATIVE_LAUNCHER override the two paths.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
source "$root/tools/rs-env.sh"
app=${1:-${RS_BUILD_DIR:-$root/build}/m4/app/Rocksmith2014.app}
home=${RS_NATIVE_HOME:-"$HOME/Library/Application Support/RS Re-ARMed"}
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

mkdir -p "$home/game" "$home/logs" "$home/steam"
rm -rf "$home/game/Rocksmith2014.app.new"
cp -R "$app" "$home/game/Rocksmith2014.app.new"
for r in RS2014.icns en.lproj; do  # the original app's icon and strings
    [[ -e "$RS_GAME_APP/Contents/Resources/$r" ]] && cp -R "$RS_GAME_APP/Contents/Resources/$r" "$home/game/Rocksmith2014.app.new/Contents/Resources/"
done
# Builds from before CFBundleIconFile was in app/Info.plist: add it (the Dock icon), re-sign the executable.
plist="$home/game/Rocksmith2014.app.new/Contents/Info.plist"
if ! /usr/libexec/PlistBuddy -c 'Print :CFBundleIconFile' "$plist" >/dev/null 2>&1; then
    plutil -insert CFBundleIconFile -string RS2014.icns "$plist"
    codesign --force -s - "$home/game/Rocksmith2014.app.new/Contents/MacOS/Rocksmith2014" 2>/dev/null || true
fi
rm -rf "$home/game/Rocksmith2014.app"
mv "$home/game/Rocksmith2014.app.new" "$home/game/Rocksmith2014.app"
cp "$netguard" "$home/libnetguard.dylib"
printf '%s\n' "$RS_GAME_DIR" > "$home/game-dir"  # the launcher's game folder (RS_GAME_DIR overrides it)
printf '%s\n' "$renderer" > "$home/renderer"
rm -f "$home/metal-session"  # a new build: Metal's crash count starts again

# The launcher app: a bundle whose executable is launch.sh.
rm -rf "$launcher"
mkdir -p "$launcher/Contents/MacOS" "$launcher/Contents/Resources"
cp "$here/launch.sh" "$launcher/Contents/MacOS/launch"
chmod +x "$launcher/Contents/MacOS/launch"
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
