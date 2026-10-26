#!/bin/bash
# Usage: check-shim.sh <dylib> <expected_install_name> <arch[,arch...]>
# Verifies a built shim is a dylib with the expected LC_ID_DYLIB and slices.
set -euo pipefail

[[ $# -eq 3 ]] || { echo "usage: $0 <dylib> <install_name> <archs>" >&2; exit 64; }
lib=$1 want_id=$2 want_archs=$3
fail=0

if ! otool -hv "$lib" | grep -q DYLIB; then
    echo "FAIL: $lib is not a Mach-O dylib"; exit 1
fi

id=$(otool -D "$lib" | tail -n +2 | head -1)
if [[ "$id" == "$want_id" ]]; then
    echo "ok: install name $id"
else
    echo "FAIL: install name is '$id', expected '$want_id'"; fail=1
fi

have=$(lipo -archs "$lib")
for a in ${want_archs//,/ }; do
    if [[ " $have " == *" $a "* ]]; then
        echo "ok: has $a slice"
    else
        echo "FAIL: missing $a slice (has: $have)"; fail=1
    fi
done
exit $fail
