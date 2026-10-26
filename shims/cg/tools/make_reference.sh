#!/bin/sh
# Record NVIDIA's Cg runtime's parameter tables for the corpus (x86_64, Rosetta).
#   make_reference.sh <build_dir> [analysis/cg]
# Needs the cg_reference target (built when the game's Cg.framework exists).
set -e
build=$1
out=${2:-$(cd "$(dirname "$0")/../../.." && pwd)/analysis/cg}
game_app=${RS_GAME_APP:-$HOME/Library/Application Support/Steam/steamapps/common/Rocksmith2014/Rocksmith2014.app}
# The framework's install name is @executable_path/../Frameworks/...; dyld finds
# it through DYLD_FRAMEWORK_PATH instead.
DYLD_FRAMEWORK_PATH="$game_app/Contents/Frameworks" \
    "$build/shims/cg/cg_reference" "$out/reference.jsonl" "$out"/corpus/*.?po
echo "wrote $out/reference.jsonl ($(wc -l < "$out/reference.jsonl") programs)"
