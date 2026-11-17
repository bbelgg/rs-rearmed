#!/bin/bash
# The release's files: the installer app with the seed, zipped for the Releases page, and the zip's signature.
#   RS_LP_SEED=/path/to/seed.bin RS_UPDATE_KEY=/path/to/update.key tools/release/package.sh
# Output, in OUT ($RS_RELEASE_OUT, else build/release under $RS_BUILD_DIR or the repository):
#   RS.Re-ARMed.Installer.app.zip       what goes on the Releases page
#   RS.Re-ARMed.Installer.app.zip.sig   beside it there: what an installed installer checks before it replaces
#                                       itself with this one (tools/release/sign.swift)
#   package.txt                         the zip's version, commit, size and SHA-256, which publish.sh checks the upload against
# A release always carries the seed (win-x86/unpack/README.md, Shipping the seed), so unlike
# tools/installer/build.sh this refuses to go on without RS_LP_SEED. It is always signed too, with the
# maintainer's private key, which like the seed is never in git: without RS_UPDATE_KEY, or with a key other than
# the one the installer's source names (updatePublicKey), it refuses. Run it at the release commit: the app has
# VERSION built in, and downloads the source of that version's tag.
set -euo pipefail
source "$(dirname "$0")/lib.sh"

[[ -n ${RS_LP_SEED:-} ]] || rel_die "RS_LP_SEED is not set: a release's installer carries the seed"
[[ -n ${RS_UPDATE_KEY:-} ]] || rel_die "RS_UPDATE_KEY is not set: a release's installer is signed (tools/release/sign.swift)"
[[ -f $RS_UPDATE_KEY ]] || rel_die "no key at $RS_UPDATE_KEY"
public=$(rel_update_public)
[[ -n $public ]] || rel_die "no updatePublicKey line in the installer's source"
# Before the build: with another key every installed installer would refuse this release.
[[ $(swift "$rel_here/sign.swift" public "$RS_UPDATE_KEY") == "$public" ]] \
    || rel_die "RS_UPDATE_KEY is not the key the installer trusts (updatePublicKey in its source)"
rel_no_game_run
version=$(rel_version)
app="$rel_root/tools/installer/RS Re-ARMed Installer.app"
mkdir -p "$rel_out"
log="$rel_out/installer-build.log"

RS_LP_SEED=$RS_LP_SEED "$rel_root/tools/installer/build.sh" > "$log" 2>&1 || { cat "$log" >&2; rel_die "the installer did not build"; }
grep -q '^with the seed: ' "$log" || rel_die "the installer was built without the seed ($log)"
grep -q '^source digest: ' "$log" || rel_die "the installer was built without its source's digest ($log)"
cmp -s "$RS_LP_SEED" "$app/Contents/Resources/seed.bin" || rel_die "the app's seed.bin is not $RS_LP_SEED"
built=$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$app/Contents/Info.plist")
[[ $built == "$version" ]] || rel_die "the app says $built, VERSION says $version"
codesign --verify "$app" || rel_die "the app's signature does not verify"

# No resource forks or extended attributes: a zip made with them unpacks with a __MACOSX folder beside the app.
zip="$rel_out/$rel_zip_name"
rm -f "$zip"
ditto -c -k --norsrc --noextattr --keepParent "$app" "$zip"
if unzip -Z1 "$zip" | grep -E '(^|/)(__MACOSX|\._)' >&2; then rel_die "the zip has Finder's files in it"; fi
unzip -Z1 "$zip" | grep -qx 'RS Re-ARMed Installer.app/Contents/Resources/seed.bin' || rel_die "the zip has no seed.bin"

sig="$rel_out/$rel_sig_name"
rm -f "$sig"
swift "$rel_here/sign.swift" sign "$RS_UPDATE_KEY" "$zip" > "$sig" || rel_die "the zip could not be signed"
rel_signed "$zip" "$sig" || rel_die "the signature just made does not verify"

sha=$(shasum -a 256 "$zip" | cut -d' ' -f1)
size=$(stat -f %z "$zip")
commit=$(git -C "$rel_root" rev-parse HEAD)
[[ -z $(git -C "$rel_root" status --porcelain) ]] || commit="$commit+uncommitted"
printf 'version %s\ncommit %s\nsize %s\nsha256 %s\n' "$version" "$commit" "$size" "$sha" > "$rel_out/package.txt"
echo "$zip"
echo "$sig"
cat "$rel_out/package.txt"
