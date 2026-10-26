#!/bin/bash
# Build the RS Re-ARMed Installer app bundle.
#   tools/installer/build.sh
# Output: tools/installer/RS Re-ARMed Installer.app
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../.." && pwd)
version=$(head -1 "$repo/VERSION")
out="$here/RS Re-ARMed Installer.app"
rm -rf "$out"
mkdir -p "$out/Contents/MacOS" "$out/Contents/Resources"

src="$out/Contents/Resources/_build.swift"
cp "$here/RSReARMedInstaller.swift" "$src"
sed -i '' "1s/^/let rsVersion = \"$version\"\\n/" "$src"

swiftc -O -o "$out/Contents/MacOS/RS Re-ARMed Installer" \
    "$src" \
    -framework AppKit -framework UniformTypeIdentifiers \
    -target arm64-apple-macosx12.0

rm "$src"

# App icon, drawn by make-icon.swift.
iconset=$(mktemp -d "${TMPDIR:-/tmp}/rs-installer-icon.XXXXXX")/AppIcon.iconset
mkdir -p "$iconset"
swift "$here/make-icon.swift" "$iconset/icon_512x512@2x.png"
for s in 16 32 128 256 512; do
    sips -z $s $s "$iconset/icon_512x512@2x.png" --out "$iconset/icon_${s}x${s}.png" >/dev/null
    [[ $s == 512 ]] || sips -z $((s * 2)) $((s * 2)) "$iconset/icon_512x512@2x.png" \
        --out "$iconset/icon_${s}x${s}@2x.png" >/dev/null
done
iconutil -c icns -o "$out/Contents/Resources/AppIcon.icns" "$iconset"
rm -rf "$(dirname "$iconset")"

cat > "$out/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleExecutable</key><string>RS Re-ARMed Installer</string>
  <key>CFBundleIdentifier</key><string>io.github.bbelgg.rs-rearmed.installer</string>
  <key>CFBundleName</key><string>RS Re-ARMed Installer</string>
  <key>CFBundleDisplayName</key><string>RS Re-ARMed Installer</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>$version</string>
  <key>CFBundleIconFile</key><string>AppIcon</string>
  <key>LSMinimumSystemVersion</key><string>12.0</string>
  <key>NSHighResolutionCapable</key><true/>
</dict>
</plist>
EOF

codesign --force -s - "$out" 2>/dev/null || true
echo "built: $out"
