#!/usr/bin/env bash
set -euo pipefail

# Dump the decrypted __text section from Rocksmith 2014.
#
# The game's DRM stub calls ptrace(PT_DENY_ATTACH), blocking lldb.
# Instead we inject dump_decrypted.dylib via an LC_LOAD_WEAK_DYLIB load
# command — the same method librsmacfixes uses. The dylib's constructor
# polls __text until decryption finishes, then writes the dump next to
# itself; this script moves it to the output folder.
#
#   dump-decrypted.sh [output_dir]           setup, wait, restore
#   dump-decrypted.sh setup [output_dir]     patch the game (it must not be running)
#   dump-decrypted.sh wait [output_dir]      wait for the dump, then restore
#   dump-decrypted.sh restore                undo the patch
#
# The Steam game binary is changed only between setup and restore: the
# original is kept as Rocksmith2014.pre-dump and moved back afterwards.
# RS_GAME_DIR sets the game folder; RS_DUMP_TIMEOUT the wait in seconds (900).

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
GAME_DIR="${RS_GAME_DIR:-$HOME/Library/Application Support/Steam/steamapps/common/Rocksmith2014}"
APP_DIR="$GAME_DIR/Rocksmith2014.app/Contents/MacOS"
GAME_BIN="$APP_DIR/Rocksmith2014"
BACKUP="$GAME_BIN.pre-dump"
DYLIB_NAME="dump_decrypted.dylib"
DYLIB_DST="$APP_DIR/$DYLIB_NAME"
GAME_DUMP="$APP_DIR/text_decrypted.bin"  # written by the dylib

mode=all
case "${1:-}" in setup|wait|restore) mode=$1; shift ;; esac
OUTDIR="${1:-build/steamless}"
DUMP="$OUTDIR/text_decrypted.bin"
DYLIB_SRC="$OUTDIR/$DYLIB_NAME"

game_running() { pgrep -f "^$GAME_BIN( |\$)" >/dev/null; }

restore() {
    if [ -f "$BACKUP" ]; then
        # A rename, not a copy over the file: safe while the game still runs from it.
        mv -f "$BACKUP" "$GAME_BIN"
        echo "Restored the original game binary."
    elif otool -l -arch x86_64 "$GAME_BIN" 2>/dev/null | grep -q "$DYLIB_NAME"; then
        echo "WARNING: the game binary still loads $DYLIB_NAME and there is no $BACKUP to restore." >&2
        echo "         Use Steam's \"Verify integrity of game files\" to get the original back." >&2
    fi
    rm -f "$DYLIB_DST" "$GAME_DUMP" "$GAME_DUMP.part"
}

setup() {
    [ -f "$GAME_BIN" ] || { echo "ERROR: game binary not found: $GAME_BIN" >&2; exit 1; }
    if game_running; then
        echo "ERROR: Rocksmith 2014 is running. Quit it first: it has to start after this setup." >&2
        exit 3
    fi
    mkdir -p "$OUTDIR"

    # Build the dylib if needed
    if [ ! -f "$DYLIB_SRC" ] || [ "$SCRIPT_DIR/dump_decrypted.c" -nt "$DYLIB_SRC" ]; then
        echo "Building $DYLIB_NAME..."
        clang -arch x86_64 -shared -o "$DYLIB_SRC" "$SCRIPT_DIR/dump_decrypted.c" -Wall
        install_name_tool -id "@executable_path/$DYLIB_NAME" "$DYLIB_SRC"
    fi

    # Check if the load command is already present
    if otool -l -arch x86_64 "$GAME_BIN" 2>/dev/null | grep -q "$DYLIB_NAME"; then
        echo "Load command already present in game binary."
        [ -f "$BACKUP" ] || echo "WARNING: no $BACKUP; the original can't be restored afterwards." >&2
    else
        echo "Adding LC_LOAD_WEAK_DYLIB to game binary (original kept as $(basename "$BACKUP"))..."
        cp -p "$GAME_BIN" "$BACKUP"
        python3 "$SCRIPT_DIR/add_weak_dylib.py" "$GAME_BIN" "@executable_path/$DYLIB_NAME"
    fi

    cp "$DYLIB_SRC" "$DYLIB_DST"
    rm -f "$DUMP" "$GAME_DUMP" "$GAME_DUMP.part"
    echo "Setup complete."
}

wait_for_dump() {
    trap restore EXIT
    trap 'exit 130' INT TERM  # so the EXIT trap restores on Ctrl-C or kill too
    local timeout="${RS_DUMP_TIMEOUT:-900}" start=$SECONDS
    echo "Waiting for the decrypted dump (start Rocksmith 2014 through Steam; up to $((timeout / 60)) min)..."
    while [ ! -f "$GAME_DUMP" ]; do
        if [ $((SECONDS - start)) -ge "$timeout" ]; then
            echo "ERROR: no dump after $((timeout / 60)) min. Was the game started through Steam after setup?" >&2
            exit 1
        fi
        sleep 1
    done

    mkdir -p "$OUTDIR"
    mv -f "$GAME_DUMP" "$DUMP"
    local actual first
    actual=$(wc -c < "$DUMP" | tr -d ' ')
    first=$(xxd -l 4 -p "$DUMP")
    echo "Dump found: $DUMP ($actual bytes, first bytes $first)"
    if [ "$actual" -le 1000000 ] || [ "$first" = "11e1cf98" ]; then
        echo "ERROR: the dump is incomplete or still encrypted." >&2
        rm -f "$DUMP"
        exit 1
    fi
    echo "SUCCESS. You can quit the game now. Next: python3 tools/steamless/patch-binary.py"
}

case $mode in
    setup)   setup ;;
    wait)    wait_for_dump ;;
    restore) restore ;;
    all)
        echo "=== Rocksmith 2014 Decrypted Binary Dumper ==="
        setup
        echo "Now launch Rocksmith 2014 through Steam."
        wait_for_dump ;;
esac
