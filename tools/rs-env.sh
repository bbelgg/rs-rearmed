# Shared paths for tools/*.sh. Source it; every variable can be overridden from
# the environment.
RS_ROOT="${RS_ROOT:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
RS_GAME_DIR="${RS_GAME_DIR:-$HOME/Library/Application Support/Steam/steamapps/common/Rocksmith2014}"
RS_GAME_APP="${RS_GAME_APP:-$RS_GAME_DIR/Rocksmith2014.app}"
RS_GAME_BIN="${RS_GAME_BIN:-$RS_GAME_APP/Contents/MacOS/Rocksmith2014}"

rs_die() { echo "error: $*" >&2; exit 64; }
