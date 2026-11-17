#!/bin/bash
# The installer replacing itself with a newer release, end to end and without its window:
#   tools/installer/test_update.sh
# It builds the installer in a scratch folder with a key made for the test in place of the maintainer's public
# key, makes releases of it there (the app at another version, zipped and signed as tools/release/package.sh does,
# and the answer GitHub would give, with file:// addresses), and runs the copy's --update-now against each with
# RS_UPDATE_URL. No network, no game, nothing outside the scratch folder; under a minute.
#   RS_TEST_KEEP=1    keep the scratch folder (its path is printed)
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../.." && pwd)
sign() { swift "$repo/tools/release/sign.swift" "$@"; }
name="RS Re-ARMed Installer"
zip_name="RS.Re-ARMed.Installer.app.zip"

work=$(mktemp -d "${TMPDIR:-/tmp}/rs-installer-update.XXXXXX")
if [[ -n ${RS_TEST_KEEP:-} ]]; then echo "scratch folder: $work"; else trap 'chmod -R u+w "$work" 2>/dev/null; rm -rf "$work"' EXIT; fi
failed=0
ok() { echo "ok    $*"; }
bad() { echo "FAIL  $*"; failed=1; }

# --- the installer, with the test's key -------------------------------------------------------------------------
tree="$work/tree"
mkdir -p "$tree/tools/installer"
cp "$repo/VERSION" "$tree/"
cp "$here/build.sh" "$here/RSReARMedInstaller.swift" "$here/make-icon.swift" "$tree/tools/installer/"
public=$(sign keygen "$work/test.key")
sign keygen "$work/other.key" > /dev/null
sed -i '' "s|^let updatePublicKey = \".*\"\$|let updatePublicKey = \"$public\"|" "$tree/tools/installer/RSReARMedInstaller.swift"
grep -qF "let updatePublicKey = \"$public\"" "$tree/tools/installer/RSReARMedInstaller.swift" \
    || { echo "error: no updatePublicKey line in RSReARMedInstaller.swift" >&2; exit 1; }
RS_LP_SEED= "$tree/tools/installer/build.sh" > "$work/build.log" 2>&1 || { cat "$work/build.log" >&2; exit 1; }
built="$tree/tools/installer/$name.app"
version=$(head -1 "$repo/VERSION")

# release DIR VERSION KEY [TAG]: DIR/feed.json names the installer at VERSION, signed with KEY, as release TAG.
release() {
    local dir=$1 ver=$2 key=$3 tag=${4:-v$2}
    mkdir -p "$dir/pack"
    cp -R "$built" "$dir/pack/"
    /usr/libexec/PlistBuddy -c "Set :CFBundleShortVersionString $ver" "$dir/pack/$name.app/Contents/Info.plist"
    codesign --force -s - "$dir/pack/$name.app" 2> /dev/null
    ditto -c -k --norsrc --noextattr --keepParent "$dir/pack/$name.app" "$dir/$zip_name"
    sign sign "$key" "$dir/$zip_name" > "$dir/$zip_name.sig"
    python3 -I - "$dir" "$tag" "$zip_name" <<'EOF'
import json, pathlib, sys
d, tag, zip_name = pathlib.Path(sys.argv[1]), sys.argv[2], sys.argv[3]
assets = [{"name": n, "browser_download_url": (d / n).as_uri()} for n in (zip_name, zip_name + ".sig")]
feed = {"tag_name": tag, "html_url": d.as_uri(), "body": "## What's new\n\n- A test.\n\n## Install\n\nAs ever.", "assets": assets}
(d / "feed.json").write_text(json.dumps(feed))
EOF
}

# fresh CASE: a fresh copy of the installer, $copy, in $work/CASE.
fresh() {
    copy="$work/$1/$name.app"
    mkdir -p "$work/$1"
    cp -R "$built" "$copy"
}
# run FEEDDIR: $copy updated from FEEDDIR. Sets $status and $said.
run() {
    # (from the scratch folder: in a checkout the installer follows the checkout and does not update itself)
    status=0
    said=$(cd "$work" && RS_UPDATE_URL="file://$1/feed.json" "$copy/Contents/MacOS/$name" --update-now 2>&1) || status=$?
}
update() { fresh "$1"; run "$2"; }
now() { /usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$copy/Contents/Info.plist"; }
# refused CASE WHAT WORDS: status 1, WORDS in what it said, and the copy still this version's and whole.
refused() {
    if [[ $status == 1 && $said == *"$3"* && $(now) == "$version" ]] && codesign --verify --strict "$copy" 2> /dev/null; then
        ok "$2 is refused, and the installer is as it was"
    else
        bad "$2: status $status, version $(now), said: $said"
    fi
}

# --- a good release ---------------------------------------------------------------------------------------------
release "$work/r-good" 99.0.0 "$work/test.key"
update good "$work/r-good"
if [[ $status == 0 && $(now) == 99.0.0 ]] && codesign --verify --strict "$copy" 2> /dev/null; then
    ok "a signed newer release replaces the installer ($version to 99.0.0), and the new one verifies"
else
    bad "a good update: status $status, version $(now), said: $said"
fi
[[ $(ls "$work/good") == "$name.app" ]] && ok "nothing is left beside the app" || bad "left beside the app: $(ls "$work/good")"

# --- what is refused --------------------------------------------------------------------------------------------
release "$work/r-changed" 99.0.0 "$work/test.key"
printf 'x' >> "$work/r-changed/$zip_name"
update changed "$work/r-changed"
refused changed "a zip changed after it was signed" "signature"

release "$work/r-other" 99.0.0 "$work/other.key"
update other "$work/r-other"
refused other "a zip signed with another key" "signature"

release "$work/r-nosig" 99.0.0 "$work/test.key"
: > "$work/r-nosig/$zip_name.sig"
update nosig "$work/r-nosig"
refused nosig "an empty signature" "signature"

# (the maintainer's own installer of an earlier release, offered as a newer one)
release "$work/r-replay" 98.0.0 "$work/test.key" v99.0.0
update replay "$work/r-replay"
refused replay "a signed installer of another version than the release's" "is not version 99.0.0"

release "$work/r-unsigned" 99.0.0 "$work/test.key"
python3 -I - "$work/r-unsigned/feed.json" <<'EOF'
import json, sys
feed = json.load(open(sys.argv[1]))
feed["assets"] = [a for a in feed["assets"] if not a["name"].endswith(".sig")]
json.dump(feed, open(sys.argv[1], "w"))
EOF
update unsigned "$work/r-unsigned"
refused unsigned "a release without a signature file" "no installer"

fresh folder
chmod a-w "$work/folder"
if [[ -w "$work/folder" ]]; then
    echo "skip  a folder that cannot be written to (this volume keeps no permissions)"
else
    run "$work/r-good"
    refused folder "an installer in a folder that cannot be written to" "cannot be written"
fi
chmod u+w "$work/folder"

# --- nothing newer ----------------------------------------------------------------------------------------------
release "$work/r-same" "$version" "$work/test.key"
update same "$work/r-same"
[[ $status == 3 && $(now) == "$version" ]] && ok "a release of this installer's own version is no update" \
    || bad "the same version: status $status, said: $said"
release "$work/r-older" 0.9.0 "$work/test.key"
update older "$work/r-older"
[[ $status == 3 ]] && ok "an older release is no update" || bad "an older release: status $status, said: $said"

[[ $failed == 0 ]] && echo "PASS" || { echo "FAILED"; exit 1; }
