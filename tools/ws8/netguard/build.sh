#!/bin/bash
# Builds the network guard into build/ws8/ (universal: x86_64 for the Rosetta
# game, arm64 for a native one):
#   libnetguard.dylib  refuses non-local connects/sends, name lookups and
#                      URL-opening shell-outs, and logs each (netguard.c)
#   netguard_test      self-test, run with --test on both architectures
# Usage: build.sh [--test]
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
source "$here/../../rs-env.sh"
out="$RS_ROOT/build/ws8"
mkdir -p "$out"
clang -arch x86_64 -arch arm64 -dynamiclib -O1 -Wall -Wextra -mmacosx-version-min=11.0 \
    -o "$out/libnetguard.dylib" "$here/netguard.c"
codesign -f -s - "$out/libnetguard.dylib" 2>/dev/null
echo "built: $out/libnetguard.dylib ($(lipo -archs "$out/libnetguard.dylib"))"
if [[ ${1:-} == --test ]]; then
    clang -arch x86_64 -arch arm64 -O1 -Wall -mmacosx-version-min=11.0 \
        -o "$out/netguard_test" "$here/netguard_test.c"
    codesign -f -s - "$out/netguard_test" 2>/dev/null
    failed=0
    for arch in arm64 x86_64; do
        log=$(mktemp -t netguard)
        echo "== $arch"
        arch "-$arch" -e RS_NETGUARD_LOG="$log" -e DYLD_INSERT_LIBRARIES="$out/libnetguard.dylib" \
            "$out/netguard_test" || failed=1
        [[ $(grep -c '^ *[0-9]* B ' "$log") -eq 5 ]] || { echo "FAIL: expected 5 blocked entries in the log"; cat "$log"; failed=1; }
        rm -f "$log"
    done
    [[ $failed -eq 0 ]] && echo "PASS: netguard blocks and logs on arm64 and x86_64" || { echo FAIL; exit 1; }
fi
