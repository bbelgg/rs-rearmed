#!/bin/sh
# usage: test/build.sh OUTDIR   -- builds and runs the seam unit test (arm64, no game needed)
set -eu
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../../../.." && pwd)
rt=$root/tools/ws7/recomp/runtime
plat=$root/shims/platform
out=$1
mkdir -p "$out"
tgt=${RC_TARGET_DIR:-$out/target}
[ -n "${RC_TARGET_DIR:-}" ] || "${RC_PY:-python3}" -I "$root/tools/targets/targets.py" gen "$tgt" > /dev/null
clang -arch arm64 -O1 -g -Wall -Wno-unused-parameter -I"$rt" -I"$plat/include" -I"$here/.." -I"$tgt" \
  "$here/seam_test.c" "$here/../seam_platform.c" "$plat/src/rsp_engine.c" "$plat/src/rsp_paths.c" \
  "$plat/src/rsp_config.c" "$plat/src/rsp_logic.c" "$plat/src/rsp_renderer.c" "$rt/rc_native.c" "$rt/rc_native_arm64.S" \
  -o "$out/seam_test"
RSP_LOG=0 "$out/seam_test"
