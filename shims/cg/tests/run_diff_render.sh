#!/bin/sh
# Render the corpus with both backends and compare (see diff_render.c).
#   run_diff_render.sh <cg_diff_render> <corpus_dir> <work_dir>
set -e
bin=$1 corpus=$2 work=$3
here=$(cd "$(dirname "$0")" && pwd)
rm -rf "${work:?}/arb" "${work:?}/glsl"
mkdir -p "$work/arb" "$work/glsl"
RS_CG_BACKEND=arb "$bin" "$work/arb" "$corpus"/*.?po > "$work/arb.log"
RS_CG_BACKEND=glsl "$bin" "$work/glsl" "$corpus"/*.?po > "$work/glsl.log"
tail -1 "$work/glsl.log"
[ -n "$(ls "$work/arb")" ] || { echo "no renders produced"; exit 1; }
exec python3 -I "$here/compare_renders.py" "$work/arb" "$work/glsl" --known "$here/data/known_render_diffs.txt"
