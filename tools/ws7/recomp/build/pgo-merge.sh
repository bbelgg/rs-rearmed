#!/bin/sh
# Merge the raw profiles of an RC_PGO=gen build into one .profdata for RC_PGO=use:FILE.
#
#   tools/ws7/recomp/build/pgo-merge.sh OUT.profdata [DIR|FILE.profraw...]   (default DIR: $RC_PGO_DIR)
set -eu
out=${1:?usage: pgo-merge.sh OUT.profdata [DIR|FILE.profraw...]}
shift
[ $# -gt 0 ] || set -- "${RC_PGO_DIR:?pass the profile folder (OUTDIR/pgo of the gen build)}"
list=$(mktemp -t pgo-merge)
trap 'rm -f "$list"' EXIT
for a in "$@"; do
  if [ -d "$a" ]; then find "$a" -name '*.profraw' -size +0; else echo "$a"; fi
done > "$list"
[ -s "$list" ] || { echo "no .profraw files in $*" >&2; exit 1; }
echo "merging $(wc -l < "$list" | tr -d ' ') profiles"
xcrun llvm-profdata merge -o "$out" -f "$list"
xcrun llvm-profdata show "$out" | grep -E 'Total functions|Maximum function count'
