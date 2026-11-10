#!/bin/bash
# One game run for a release: to the note highway, a look that the game is still up, and out.
#   tools/release/smoke.sh --lp     Learn & Play, with the input script of docs/windows/TESTING.md (Unattended runs)
#   tools/release/smoke.sh --mac    the Mac edition, with the route command you give it
# It prints PASS or FAIL and where the log and the snapshots are; look at the last snapshot, which is what shows the
# highway. A test build and a test user folder, never the installed game or your progress.
#
# Learn & Play needs:
#   RS_SMOKE_LP_EXE     a build's out/rs2014_lp
#   LP_GAME_DIR, LP_USER_DIR, RS_STEAM_DATA_DIR    as for any test run (docs/windows/TESTING.md, Game runs); the
#                       user folder holds a profile that goes straight from the title to the main menu
#   RS_SMOKE_LP_SCRIPT  another LP_INPUT_SCRIPT than the documented one (optional)
#   RS_SMOKE_HOLD       seconds to stay on the highway before the look (default 30)
# The Mac edition has no input script of its own: its unattended routes use an injector that is not in this
# repository (docs/mac/TESTING.md, Game runs). So:
#   RS_SMOKE_MAC_CMD    a command that runs the route and exits 0 when the game reached the highway. It gets
#                       RS_SMOKE_OUT, the folder for its log and snapshots.
set -euo pipefail
source "$(dirname "$0")/lib.sh"

edition=${1:-}
out="$rel_out/smoke-${edition#--}"
case $edition in
--mac)
    [[ -n ${RS_SMOKE_MAC_CMD:-} ]] || rel_die "RS_SMOKE_MAC_CMD is not set: the Mac route is not in the repository (see the top of this file)"
    rel_no_game_run
    rm -rf "$out"; mkdir -p "$out"
    if RS_SMOKE_OUT=$out /bin/bash -c "$RS_SMOKE_MAC_CMD" > "$out/route.log" 2>&1; then
        echo "PASS  Mac: the route command exited 0 ($out)"
    else
        echo "FAIL  Mac: the route command failed ($out/route.log)"; exit 1
    fi
    ;;
--lp)
    exe=${RS_SMOKE_LP_EXE:-}
    [[ -x $exe ]] || rel_die "RS_SMOKE_LP_EXE is not a program: a build's out/rs2014_lp"
    for v in LP_GAME_DIR LP_USER_DIR RS_STEAM_DATA_DIR; do
        [[ -d ${!v:-} ]] || rel_die "$v is not a folder (docs/windows/TESTING.md, Game runs)"
    done
    rel_no_game_run
    rm -rf "$out"; mkdir -p "$out/snap"
    script=${RS_SMOKE_LP_SCRIPT:-@callback 1101,3:24,10:24,20:24,32:24,44:24,62:75,68:24}
    steps=$(tr ',' '\n' <<< "$script" | grep -vc '^@')
    log="$out/log.txt"
    RS_STEAM_LOG=1 LP_INPUT_SCRIPT=$script RS_GL_SNAPSHOT_DIR="$out/snap" RS_GL_SNAPSHOT_SECS=10 \
        "$exe" 2> "$log" > /dev/null &
    pid=$!
    trap 'kill "$pid" 2>/dev/null || true' EXIT
    fail() { echo "FAIL  Learn & Play: $* ($log)"; exit 1; }
    # Until the script's last key is in the log, at most the waits (120 s each) and the steps' own time.
    for _ in $(seq 360); do
        kill -0 "$pid" 2>/dev/null || fail "the game ended before the script did"
        grep -q 'input script: .* gave up waiting' "$log" && fail "the script gave up waiting for a log line"
        [[ $(grep -c 'input script: .* key 0x' "$log") -ge $steps ]] && break
        sleep 1
    done
    [[ $(grep -c 'input script: .* key 0x' "$log") -ge $steps ]] || fail "the script did not finish in 6 minutes"
    sleep "${RS_SMOKE_HOLD:-30}"
    kill -0 "$pid" 2>/dev/null || fail "the game ended on the way to the highway"
    kill "$pid"; wait "$pid" 2>/dev/null || true
    trap - EXIT
    last=$(ls "$out/snap" 2>/dev/null | tail -1)
    echo "PASS  Learn & Play: every key sent, and the game up ${RS_SMOKE_HOLD:-30} s later"
    echo "      look at $out/snap/${last:-(no snapshot)}: it should show the note highway"
    ;;
*)
    rel_die "usage: smoke.sh --mac | --lp"
    ;;
esac
