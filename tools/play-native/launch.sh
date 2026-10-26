#!/bin/bash
# Everyday launcher of the native arm64 Rocksmith 2014 (installed by tools/play-native/install.sh as
# the executable of ~/Applications/RS Re-ARMed.app). Offline:
#   - the game folder is mirrored with symlinks into $HOME_DIR/game (the shell takes the folder that
#     holds the .app as the game root), with this build's Rocksmith2014.app, an offline copy of the
#     install's Rocksmith.ini ([Net] AutoLogin=0, [Renderer.MacOS] DisableBrowser=1) and the
#     "Re-ARMed 2026" title logo (static.psarc.rearmed) when it is there;
#   - the Steam shim runs in stub mode with its cloud in $HOME_DIR/steam: on the first launch it is
#     seeded with a copy of the real Steam/userdata/<account>/221680/remote (profiles,
#     LocalProfiles.json). The real Steam folder is never written, and `crd` (stored Ubisoft
#     credentials) is never copied;
#   - the DLC the account owns (the ones Steam installed) and the free 248750 that custom songs
#     carry count as owned; set RS_STEAM_DLC=all with launchctl setenv to own every AppID;
#   - libnetguard refuses every non-local connection and logs to $HOME_DIR/logs/netguard.log.
# The install's own files (and the Rosetta game) are not changed.
set -u
HOME_DIR=${RS_NATIVE_HOME:-"$HOME/Library/Application Support/RS Re-ARMed"}
GAME_DIR=${RS_GAME_DIR:-"$HOME/Library/Application Support/Steam/steamapps/common/Rocksmith2014"}
stage=$HOME_DIR/game
logs=$HOME_DIR/logs

fail() {
    /usr/bin/osascript -e "display alert \"RS Re-ARMed\" message \"$1\" as critical" >/dev/null 2>&1
    echo "launch: $1" >&2
    exit 1
}

[[ -x "$stage/Rocksmith2014.app/Contents/MacOS/Rocksmith2014" ]] ||
    fail "The native build is not installed. Run tools/play-native/install.sh in the RS Re-ARMed repository."
[[ -f "$HOME_DIR/libnetguard.dylib" ]] || fail "libnetguard.dylib is missing; refusing to start without it."
[[ -d "$GAME_DIR" ]] || fail "Rocksmith 2014 is not installed at $GAME_DIR."
if pgrep -x Rocksmith2014 >/dev/null || pgrep -x Rocksmith2014_x64_plain >/dev/null; then
    fail "Rocksmith 2014 is already running."
fi
mkdir -p "$logs" "$HOME_DIR/steam"

# Mirror: one symlink per install entry (new DLC folders appear here too), except what is ours.
for e in "$GAME_DIR"/*; do
    n=$(basename "$e")
    case $n in Rocksmith2014.app|Rocksmith.ini) continue ;; esac
    [[ -e "$stage/$n" || -L "$stage/$n" ]] || ln -s "$e" "$stage/$n"
done
for l in "$stage"/*; do  # entries that left the install
    [[ -L $l && ! -e $l ]] && rm -f "$l"
done
if [[ -f "$GAME_DIR/static.psarc.rearmed" && "${RS_REARMED:-1}" != 0 ]]; then
    ln -sfn "$GAME_DIR/static.psarc.rearmed" "$stage/static.psarc"
else
    ln -sfn "$GAME_DIR/static.psarc" "$stage/static.psarc"
fi
if [[ -f "$GAME_DIR/Rocksmith.ini" ]]; then
    awk '{ sub(/\r$/, "") }
         /^\[/ { sec = $0 }
         sec == "[Net]" && /^AutoLogin=/ { next }
         sec == "[Renderer.MacOS]" && /^DisableBrowser=/ { print "DisableBrowser=1"; next }
         { print }
         $0 == "[Net]" { print "AutoLogin=0" }' "$GAME_DIR/Rocksmith.ini" > "$stage/Rocksmith.ini.new" &&
        mv -f "$stage/Rocksmith.ini.new" "$stage/Rocksmith.ini"
fi

# First launch: seed the offline cloud with a copy of the real Steam saves (every account that has
# Rocksmith saves), leaving out `crd`, so the shim's own seeding (which copies, then deletes crd)
# never runs. Later launches use the copy only; the real folder is never written.
# RS_NATIVE_SEED=0: no seeding (tests with a prepared steam/ folder).
for src in "$HOME/Library/Application Support/Steam/userdata"/*/221680/remote; do
    [[ -d $src && "${RS_NATIVE_SEED:-1}" != 0 ]] || continue
    acct=$(basename "$(dirname "$(dirname "$src")")")
    dst=$HOME_DIR/steam/$acct/221680/remote
    [[ -d $dst ]] && continue
    mkdir -p "$dst.tmp" && rsync -a --exclude crd "$src/" "$dst.tmp/" && mv "$dst.tmp" "$dst" &&
        echo "seeded $dst from $src (without crd)" >> "$logs/seed.log"
done

# Stored Ubisoft credentials never reach the game (the shim leaves them out too; this is a second guard).
find "$HOME_DIR/steam" -name crd -type f -delete 2>/dev/null

[[ -f "$logs/last-run.log" ]] && mv -f "$logs/last-run.log" "$logs/previous-run.log"
# Started through LaunchServices as an app of its own: exec'ing the game from this (LSUIElement)
# launcher leaves it without a window when the launcher itself was opened from the Finder. The
# environment comes from launchd plus --env, so nothing else of this shell's leaks in.
exec /usr/bin/open -n -a "$stage/Rocksmith2014.app" \
    --env RS_STEAM_MODE=stub --env RS_STEAM_DATA_DIR="$HOME_DIR/steam" \
    --env RS_NETGUARD_LOG="$logs/netguard.log" \
    --env DYLD_INSERT_LIBRARIES="$HOME_DIR/libnetguard.dylib" \
    --stdout "$logs/last-run.log" --stderr "$logs/last-run.log"
