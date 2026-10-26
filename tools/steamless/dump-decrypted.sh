#!/usr/bin/env bash
set -euo pipefail

# Dump the decrypted __text section from Rocksmith 2014.
#
# The game's DRM stub calls ptrace(PT_DENY_ATTACH), blocking lldb.
# Instead we inject dump_decrypted.dylib via an LC_LOAD_WEAK_DYLIB load
# command — the same method librsmacfixes uses. The dylib's constructor
# polls __text until decryption finishes, then writes the dump to disk.

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
GAME_DIR="$HOME/Library/Application Support/Steam/steamapps/common/Rocksmith2014"
APP_DIR="$GAME_DIR/Rocksmith2014.app/Contents/MacOS"
GAME_BIN="$APP_DIR/Rocksmith2014"
OUTDIR="${1:-build/steamless}"
DUMP="$OUTDIR/text_decrypted.bin"
DYLIB_NAME="dump_decrypted.dylib"
DYLIB_SRC="$SCRIPT_DIR/$DYLIB_NAME"
DYLIB_DST="$APP_DIR/$DYLIB_NAME"

# Build the dylib if needed
if [ ! -f "$DYLIB_SRC" ]; then
    echo "Building $DYLIB_NAME..."
    clang -arch x86_64 -shared -o "$DYLIB_SRC" "$SCRIPT_DIR/dump_decrypted.c" -Wall
    install_name_tool -id "@executable_path/$DYLIB_NAME" "$DYLIB_SRC"
fi

echo "=== Rocksmith 2014 Decrypted Binary Dumper ==="
echo ""

# Check if the load command is already present
if otool -l "$GAME_BIN" -arch x86_64 2>/dev/null | grep -q "dump_decrypted"; then
    echo "Load command already present in game binary."
else
    echo "Adding LC_LOAD_WEAK_DYLIB to game binary..."
    cp "$GAME_BIN" "$GAME_BIN.pre-dump"
    python3 "$SCRIPT_DIR/add_weak_dylib.py" "$GAME_BIN" "@executable_path/$DYLIB_NAME"
    echo "Load command added."
fi

# Copy dylib into the game directory
if [ ! -f "$DYLIB_DST" ] || ! cmp -s "$DYLIB_SRC" "$DYLIB_DST"; then
    echo "Installing $DYLIB_NAME into game directory..."
    cp "$DYLIB_SRC" "$DYLIB_DST"
fi

# Remove any old dump
rm -f "$DUMP"

echo ""
echo "Setup complete. Now launch Rocksmith through Steam."
echo "The dylib will dump decrypted __text to: $DUMP"
echo ""
echo "Waiting for dump to appear..."
echo "(Press Ctrl-C to cancel)"

while true; do
    if [ -f "$DUMP" ]; then
        ACTUAL=$(wc -c < "$DUMP" | tr -d ' ')
        if [ "$ACTUAL" -gt 1000000 ]; then
            echo ""
            echo "Dump found: $DUMP ($ACTUAL bytes)"
            FIRST=$(xxd -l 4 -p "$DUMP")
            echo "First bytes: $FIRST"
            if [ "$FIRST" = "11e1cf98" ]; then
                echo "ERROR: Still encrypted!"
                exit 1
            fi
            echo ""
            echo "SUCCESS! Now run: python3 tools/steamless/patch-binary.py"
            exit 0
        fi
    fi
    sleep 1
done
