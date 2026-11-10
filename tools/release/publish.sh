#!/bin/bash
# The remote half of a release: the tag, the push, the release with its notes and the installer, checked.
#   tools/release/publish.sh 1.13.0          look only: every check, and what would be sent
#   tools/release/publish.sh 1.13.0 --yes    do it
# Ask the maintainer before --yes (AGENTS.md: any push, tag or release). It needs, from the steps before it:
#   the release commit at HEAD of main ("Release 1.13.0: ..."), a clean tree, VERSION saying 1.13.0
#   OUT/notes-1.13.0.md (bump.py), OUT/RS.Re-ARMed.Installer.app.zip and OUT/package.txt (package.sh)
# With --yes, in this order, stopping at the first thing that fails:
#   1. the annotated tag v1.13.0, in UTC
#   2. push main, then the tag
#   3. a draft release with the notes and the zip
#   4. the zip downloaded again and compared with package.txt's SHA-256
#   5. the draft published as the latest release
# A stop after 2 leaves the tag pushed and no release: run it again with --resume, which skips what is done.
set -euo pipefail
source "$(dirname "$0")/lib.sh"

version=${1:-}
[[ $version =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || rel_die "usage: publish.sh VERSION [--yes] [--resume]"
shift
yes=0 resume=0
for a in "$@"; do
    case $a in
        --yes) yes=1 ;;
        --resume) resume=1 ;;
        *) rel_die "unknown option $a" ;;
    esac
done
tag="v$version"
notes="$rel_out/notes-$version.md"
zip="$rel_out/$rel_zip_name"
g() { git -C "$rel_root" "$@"; }
bad=0
no() { echo "NO   $*"; bad=1; }
ok() { echo "ok   $*"; }

# --- checks ------------------------------------------------------------------------------------------------------
[[ $(g rev-parse --abbrev-ref HEAD) == main ]] && ok "on main" || no "not on main"
[[ -z $(g status --porcelain) ]] && ok "the tree is clean" || no "the tree has uncommitted changes"
[[ $(rel_version) == "$version" ]] && ok "VERSION is $version" || no "VERSION is $(rel_version)"
subject=$(g log -1 --format=%s)
[[ $subject == "Release $version: "* ]] && ok "HEAD is the release commit" || no "HEAD is not \"Release $version: ...\": $subject"
[[ $subject != *"("*"in a line)"* ]] || no "the release commit's subject is still the draft's"
[[ -s $notes ]] && ok "notes: $notes" || no "no notes at $notes (bump.py)"
if [[ -s $zip && -s $rel_out/package.txt ]]; then
    sha=$(awk '$1 == "sha256" { print $2 }' "$rel_out/package.txt")
    [[ $(shasum -a 256 "$zip" | cut -d' ' -f1) == "$sha" ]] && ok "the zip is package.txt's" || no "the zip is not the one package.txt describes"
    [[ $(awk '$1 == "version" { print $2 }' "$rel_out/package.txt") == "$version" ]] || no "the zip was made for another version"
    [[ $(awk '$1 == "commit" { print $2 }' "$rel_out/package.txt") == "$(g rev-parse HEAD)" ]] && ok "the zip was made at HEAD" \
        || no "the zip was not made at HEAD with a clean tree: run package.sh again"
else
    no "no zip or package.txt in $rel_out (package.sh)"
fi
have_tag=0
if g rev-parse -q --verify "refs/tags/$tag" >/dev/null; then
    have_tag=1
    [[ $resume == 1 && $(g rev-parse "$tag^{commit}") == "$(g rev-parse HEAD)" ]] && ok "the tag is there, at HEAD (--resume)" \
        || no "the tag $tag exists already"
fi

# What would be pushed reads as main's history should, and gives nothing personal away (AGENTS.md).
g fetch -q origin main
range="origin/main..HEAD"
me="$(g config user.name) <$(g config user.email)>"
while IFS=$'\t' read -r h an ad cd s; do
    [[ -n $h ]] || continue
    [[ $an == "$me" ]] || no "$h is not by the repository's identity"
    [[ $ad == *+0000 && $cd == *+0000 ]] || no "$h has a date outside UTC"
done < <(g log --format='%h%x09%an <%ae>%x09%ad%x09%cd%x09%s' --date=iso "$range")
if g log -p --format='%B' "$range" | grep -v '^-' | grep -E '/Users/[a-z]{2,}|/Volumes/[A-Z][a-z]+/|@(gmail|icloud|outlook)\.' | head -5 | grep . ; then
    no "the unpushed commits name a home path, a volume or an address (above)"
else
    ok "no home paths, volumes or addresses in the unpushed commits"
fi
echo
echo "To be pushed ($(g rev-list --count "$range") commits; read that each is one piece of work):"
g log --format='  %h %s' "$range"
echo
echo "Tag $tag, release \"RS Re-ARMed $version\", $rel_zip_name ($(awk '$1 == "size" { print $2 }' "$rel_out/package.txt" 2>/dev/null) bytes)"

[[ $bad == 0 ]] || rel_die "not ready (the NO lines above)"
if [[ $yes == 0 ]]; then
    echo "Ready. Nothing was sent: --yes does it, after the maintainer's go-ahead."
    exit 0
fi

# --- the release ---------------------------------------------------------------------------------------------------
if [[ $have_tag == 0 ]]; then
    TZ=UTC g tag -a "$tag" -m "RS Re-ARMed $version"
fi
g push origin main
g push origin "$tag"
if gh release view "$tag" >/dev/null 2>&1; then
    [[ $resume == 1 ]] || rel_die "a release for $tag exists already"
    gh release upload "$tag" "$zip" --clobber
else
    gh release create "$tag" --draft --verify-tag --title "RS Re-ARMed $version" --notes-file "$notes" "$zip"
fi
back=$(mktemp -d "${TMPDIR:-/tmp}/rs-release.XXXXXX")
gh release download "$tag" --pattern "$rel_zip_name" --dir "$back"
got=$(shasum -a 256 "$back/$rel_zip_name" | cut -d' ' -f1)
rm -rf "$back"
[[ $got == "$sha" ]] || rel_die "the uploaded zip is $got, not $sha: the release stays a draft"
gh release edit "$tag" --draft=false --latest
echo "published: $(gh release view "$tag" --json url --jq .url)"
echo "sha256 $sha (confirmed by download)"
