#!/bin/sh
# Builds the offline MSL shader corpus from shader dumps and checks every program against GL
# (docs/METAL-DESIGN.md "Shader translation"). Runs on the user's machine at build time: the dumps
# come from their game and are never committed.
#
#   build-msl-corpus.sh --verify <msl_verify> --out <dir> [--scaleform <dir>] [--cg <dir>] [--jobs <n>]
#                       [--translate-only] [--progress]
#
#   --scaleform  RS_GL_DUMP_SHADERS output (<seq>_<id>_<vs|fs>.glsl), or tools/metal/extract_scaleform.py's
#   --cg         cg_corpus_load --dump-glsl output (<key>.<bits>.vert, <key>.frag)
#   --translate-only  no check against GL (msl_verify --translate-only): seconds instead of minutes;
#                the corpus is the same, its entries are not flagged verified (a user's build:
#                tools/metal/make-corpus.sh)
#   --progress   "progress F LABEL" lines on stdout while the programs are checked (F: 0..1)
#
# Writes to <dir>: rsmsl-corpus.bin (copy it next to libRSOpenGL.dylib or into the app's
# Contents/Resources, or point RS_MSL_CORPUS at it), report.tsv (one line per program, see
# msl_verify), failures.tsv (everything but PASS / NEAR / TRANSLATED), summary.txt. Exit status: 0 all programs match GL, 1 some don't,
# 77 no dumps found.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
verify= out= sf= cg= jobs= xonly= progress=0
while [ $# -gt 0 ]; do
  case $1 in
    --verify) verify=$2; shift 2 ;;
    --out) out=$2; shift 2 ;;
    --scaleform) sf=$2; shift 2 ;;
    --cg) cg=$2; shift 2 ;;
    --jobs) jobs=$2; shift 2 ;;
    --translate-only) xonly=--translate-only; shift ;;
    --progress) progress=1; shift ;;
    *) echo "unknown argument $1" >&2; exit 2 ;;
  esac
done
[ -n "$verify" ] && [ -n "$out" ] || { echo "usage: $0 --verify <msl_verify> --out <dir> [--scaleform <dir>] [--cg <dir>]" >&2; exit 2; }
[ -n "$jobs" ] || jobs=$(sysctl -n hw.perflevel0.physicalcpu 2>/dev/null || sysctl -n hw.ncpu)
[ -z "$xonly" ] || jobs=1  # translation alone takes seconds
mkdir -p "$out"
work="$out/work"
rm -rf "$work"
mkdir -p "$work"

set --
[ -n "$sf" ] && [ -d "$sf" ] && set -- "$@" --scaleform "$sf"
[ -n "$cg" ] && [ -d "$cg" ] && set -- "$@" --cg "$cg"
if [ $# -eq 0 ]; then echo "no shader dumps found (scaleform: ${sf:-unset}, cg: ${cg:-unset}); skipping"; exit 77; fi
python3 "$here/pair_shaders.py" "$@" > "$work/pairs.tsv"
total=$(wc -l < "$work/pairs.tsv" | tr -d ' ')
if [ "$total" -eq 0 ]; then echo "no shader pairs found; skipping"; exit 77; fi

# Shards in parallel (the Metal compiler service and GL run fine side by side).
split -n "r/$jobs" "$work/pairs.tsv" "$work/shard." 2>/dev/null || {
  # BSD split without -n: round-robin by hand
  awk -v n="$jobs" -v d="$work" '{ print > (d "/shard." sprintf("%03d", NR % n)) }' "$work/pairs.tsv"
}
if [ -n "$xonly" ]; then what="Translating shaders"; echo "translating $total programs (no check against GL)..."
else what="Checking shaders against GL"; echo "checking $total programs against GL in $jobs processes..."; fi
pids=
for s in "$work"/shard.*; do
  [ -s "$s" ] || continue
  "$verify" "$s" $xonly --corpus-out "$s.bin" --report "$s.report" --dump "$work/failed" > "$s.log" 2>&1 &
  pids="$pids $!"
done
if [ "$progress" = 1 ]; then
  # msl_verify writes a report line per program as it goes.
  while :; do
    alive=0
    for p in $pids; do kill -0 "$p" 2>/dev/null && alive=1; done
    n=$(cat "$work"/shard.*.report 2>/dev/null | wc -l | tr -d ' ')
    awk -v n="$n" -v t="$total" -v w="$what" 'BEGIN { printf "progress %.3f %s\n", (n > t ? t : n) / t, w }'
    [ "$alive" = 1 ] || break
    sleep 2
  done
fi
for p in $pids; do wait "$p" || true; done

python3 "$here/merge_corpus.py" "$out/rsmsl-corpus.bin" "$work"/shard.*.bin
# The library must find every program through rsmsl_translate.
"$verify" "$work/pairs.tsv" --check-corpus "$out/rsmsl-corpus.bin" --report "$work/corpus-check.tsv" | head -1
cat "$work"/shard.*.report | sort -t "$(printf '\t')" -k7 > "$out/report.tsv"
grep -v '^PASS\|^NEAR\|^TRANSLATED' "$out/report.tsv" > "$out/failures.tsv" || true
python3 - "$out/report.tsv" > "$out/summary.txt" <<'PY'
import sys, collections
st = collections.Counter(); worst = 0; probe = 0.0; hist = collections.Counter()
for l in open(sys.argv[1]):
    f = l.rstrip('\n').split('\t')
    st[f[0]] += 1
    if f[1] != '-':
        e = int(f[1]); worst = max(worst, e)
        hist['0' if e == 0 else '1' if e == 1 else '2' if e == 2 else '3-8' if e <= 8 else '9-32' if e <= 32 else '>32'] += 1
        probe = max(probe, float(f[5]))
n = sum(st.values())
print('programs: %d' % n)
if st['TRANSLATED']:
    print('translated (not checked against GL): %d' % st['TRANSLATED'])
else:
    print('match GL within %s: %d (%.1f%%)' % ('2/255', st['PASS'], 100.0 * st['PASS'] / max(n, 1)))
    print('near (a few isolated pixels of precision differences): %d' % st['NEAR'])
for k in sorted(st):
    if k not in ('PASS', 'NEAR', 'TRANSLATED'): print('%s: %d' % (k, st[k]))
if not st['TRANSLATED']:
    print('largest colour error: %d/255; histogram: %s' % (worst, ', '.join('%s: %d' % (k, hist[k]) for k in ['0', '1', '2', '3-8', '9-32', '>32'])))
    print('largest vertex output difference (relative): %.3g' % probe)
PY
cat "$out/summary.txt"
echo "corpus: $out/rsmsl-corpus.bin; failures: $out/failures.tsv"
rm -f "$work"/shard.*.bin
[ ! -s "$out/failures.tsv" ]
