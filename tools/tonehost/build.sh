#!/bin/sh
# Builds rs-tonehost into $RS_BUILD_DIR/tonehost (default build/tonehost). Needs only the Xcode tools.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
out=${RS_BUILD_DIR:-$root/build}/tonehost
app="$out/RS Tone Host.app"
mkdir -p "$app/Contents/MacOS"
clang -arch arm64 -O2 -Wall -fobjc-arc -mmacosx-version-min=11.0 "$here/tonehost.m" -framework Foundation -framework AudioToolbox \
  -framework CoreAudio -o "$app/Contents/MacOS/rs-tonehost"
cp "$here/Info.plist" "$app/Contents/Info.plist"
codesign --force -s - "$app"
clang -arch arm64 -O2 -Wall -fobjc-arc -mmacosx-version-min=11.0 "$here/editor.m" -framework Cocoa -framework AudioToolbox \
  -framework AudioUnit -o "$out/rs-tone-editor"
clang -arch arm64 -O2 -Wall -fobjc-arc -mmacosx-version-min=11.0 "$here/helix/capture_states.m" -framework Foundation \
  -framework AudioToolbox -o "$out/capture_states"
cp "$here/start.sh" "$here/stop.sh" "$out/"
echo "built: $app"
