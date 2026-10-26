#!/bin/sh
# Whole-image build: regenerate (incrementally), compile libgame_rc.a, run the smoke test.
#
# usage: tools/ws7/recomp/build/build.sh [--jobs N] [gen_image.py options...]
#   e.g. build.sh --skip overrides.txt --o1 slow.txt
#   --skip files: `ADDR` = override module provides F_ADDR (kept in rc_table);
#   `ADDR drop` = not generated and not in rc_table
# env:  RC_GEN_DIR   generated C + build.ninja (default analysis/ws7/recomp/gen)
#       RC_OBJ_DIR   objects + libgame_rc.a    (default analysis/ws7/recomp/obj)
#       RC_IMAGE     plain x86_64 image        (default analysis/cg/Rocksmith2014_x64_plain)
#       RC_CFLAGS    extra flags for every TU  (default "-w -DRC_ABI_TRUST"; set to "-w" for the
#                    exact, slower-to-compile register traffic)
#       RC_OPT       optimisation level        (default -O2; --o1 TUs stay at -O1)
#       RC_LTO, RC_PGO, RC_PGO_DIR   see optflags.sh (thin LTO; PGO gen / use:FILE.profdata)
#       RC_SMOKE     1 = link and run the smoke test (default 1, 0 with RC_LTO: it would be a
#                    second whole-image ThinLTO link)
#       RC_TSO       tso-ranges file, lo-hi    (default: the whole image, 100000000-110000000;
#                    TUs in a range are compiled with -DRC_MO_TSO)
#       RC_PY        python with capstone      (default analysis/venv/bin/python)
# Output: $RC_OBJ_DIR/libgame_rc.a, $RC_OBJ_DIR/smoke (run result printed).
set -eu
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../../.." && pwd)
gen=${RC_GEN_DIR:-$root/analysis/ws7/recomp/gen}
obj=${RC_OBJ_DIR:-$root/analysis/ws7/recomp/obj}
img=${RC_IMAGE:-$root/analysis/cg/Rocksmith2014_x64_plain}
py=${RC_PY:-$root/analysis/venv/bin/python}
cflags=${RC_CFLAGS--w -DRC_ABI_TRUST}
opt=${RC_OPT:--O2}
jobs=$(sysctl -n hw.ncpu)
if [ "${1:-}" = "--jobs" ]; then jobs=$2; shift 2; fi
. "$here/optflags.sh"
mkdir -p "$gen" "$obj"
tso=${RC_TSO:-$obj/tso-ranges.txt}
[ -n "${RC_TSO:-}" ] || echo 100000000-110000000 > "$tso"

"$py" -I "$here/gen_image.py" "$img" "$gen" --jobs "$jobs" --tso-ranges "$tso" \
  --obj-dir "$obj" "--cflags=$cflags" "--opt=$opt" "--lto=$rc_lto" "--prof=$rc_pgo" ${rc_pgo_dep:+--prof-dep=$rc_pgo_dep} "$@"
ninja -C "$gen" -j "$jobs"
smoke=${RC_SMOKE:-1}
[ -z "$rc_lto" ] || smoke=${RC_SMOKE:-0}
[ "$smoke" != 0 ] || { echo "smoke test skipped (RC_SMOKE=0; the default with RC_LTO)"; exit 0; }

rt=$root/tools/ws7/recomp/runtime
pgo=; [ "${RC_PGO:-}" != gen ] || pgo="-fprofile-instr-generate=$obj/smoke-%p%c.profraw"  # not into RC_PGO_DIR
cc="clang -arch arm64 -O1 -w -I$rt $pgo $(rc_lto_link "$obj/lto-cache")"
# import thunks and the whole runtime
thunks=
if [ -f "$root/tools/ws7/recomp/thunks/gen_thunks.py" ]; then
  "$py" -I "$root/tools/ws7/recomp/thunks/gen_thunks.py" "$img" "$obj/thunks.c" > "$obj/thunks.log" 2>&1 && thunks=$obj/thunks.c ||
    echo "thunk generation failed, see $obj/thunks.log"
fi
rtsrc=$(ls "$rt"/rc_*.c "$rt"/rc_*.S 2>/dev/null)
link() {  # extra sources...
  $cc "$here/smoke/smoke_main.c" $rtsrc $thunks "$@" "$obj/libgame_rc.a" -lz -lobjc -framework Foundation \
    -Wl,-undefined,error -o "$obj/smoke" 2> "$obj/smoke.link.log"
}
if ! link; then
  grep -o '"_[A-Za-z0-9_]*"' "$obj/smoke.link.log" | tr -d '"' | sed 's/^_//' | sort -u > "$obj/undefined.txt"
  echo "smoke link: $(wc -l < "$obj/undefined.txt") undefined symbols (list: $obj/undefined.txt); linking abort stubs for them"
  head -3 "$obj/smoke.link.log" | cut -c1-200
  { echo '#include "rc.h"'; while read s; do echo "void $s(rc_ctx *c) { rc_fail(\"undefined at link: $s\", 0); }"; done < "$obj/undefined.txt"; } > "$obj/undef_stubs.c"
  link "$obj/undef_stubs.c" || { echo "smoke link failed"; tail -5 "$obj/smoke.link.log"; exit 1; }
fi
"$obj/smoke" "$img"
