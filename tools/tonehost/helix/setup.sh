#!/bin/bash
# Sets up rs-tonehost for Helix Native, from nothing or again after a Helix update: builds it, makes one preset per Helix
# amp, has you import them once, saves their states and puts your Helix library back as it was. Quit the game first.
#   tools/tonehost/helix/setup.sh [--input DEVICE --channel N] [--output DEVICE]
# The first time it needs --input and --channel; a later run keeps your settings. RS_NATIVE_HOME: the install, by default
# ~/Library/Application Support/RS Re-ARMed. RS_HELIX_SETLIST: which setlist the import goes to, counted as MIDI does
# (6 is USER 5). Then turn on ToneHost in RSMods: the game starts the host from then on.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
home=${RS_NATIVE_HOME:-"$HOME/Library/Application Support/RS Re-ARMed"}
out=${RS_BUILD_DIR:-$root/build}/tonehost
dest=$home/tonehost
native="$HOME/Library/Application Support/Line 6/Helix Native"
setlist=${RS_HELIX_SETLIST:-6}
guard=$home/libnetguard.dylib

pgrep -xq Rocksmith2014 && { echo "Quit the game first." >&2; exit 1; }
[ -d "$native/#PRESET_LIBRARY" ] || { echo "Helix Native isn't set up here: open it in a host once." >&2; exit 1; }
[ -f "$guard" ] || { echo "No network guard at $guard: install RS Re-ARMed first." >&2; exit 1; }

"$here/../build.sh"
work=$(mktemp -d)
python3 "$here/make_presets.py" "$work"

# The import overwrites a setlist, so the library is put back afterwards
backup=$work/library
mkdir "$backup"
cp -R "$native/#PRESET_LIBRARY" "$backup/"
cp "$native/Helix Native.prefs" "$backup/"
restore() {
  rm -rf "$native/#PRESET_LIBRARY"
  cp -R "$backup/#PRESET_LIBRARY" "$native/"
  cp "$backup/Helix Native.prefs" "$native/Helix Native.prefs"
  echo "Your Helix library is back as it was."
}
trap restore EXIT

cat <<EOT

One step for you, in the window that opens:
  1. Choose the setlist USER $((setlist - 1)) at the top left. It is put back afterwards.
  2. Click IMPORT next to SETLISTS and open
     $work/setlist.hls
  3. Answer Yes, then close the window.

EOT
export RS_NETGUARD_LOG=$work/netguard.log
DYLD_INSERT_LIBRARIES=$guard "$out/rs-tone-editor" aumf 6HPA LSIX

mkdir -p "$work/states" "$dest"
names=$(python3 -c "
import json, sys
t = json.load(open(sys.argv[1]))['templates']
print(' '.join(v['state'] for k, v in sorted(t.items(), key=lambda kv: kv[1]['slot'])))" "$work/tones.json")
# shellcheck disable=SC2086
DYLD_INSERT_LIBRARIES=$guard "$out/capture_states" "$setlist" "$work/states" $names | tail -1

[ "$(md5 -q "$work"/states/* | sort -u | wc -l)" -gt 1 ] || { echo "The presets all came out the same: was the import in the right setlist?" >&2; exit 1; }

[ -p "$dest/commands" ] && echo quit > "$dest/commands"
rm -rf "$dest/states" "$dest/RS Tone Host.app"
cp -R "$work/states" "$dest/states"
cp -R "$out/RS Tone Host.app" "$dest/"
cp "$out/start.sh" "$out/stop.sh" "$dest/"
python3 "$here/make_config.py" "$work/tones.json" "$dest/config.json" "$@"
echo "Done. Turn on ToneHost in RSMods; macOS asks for the microphone the first time the host runs."
