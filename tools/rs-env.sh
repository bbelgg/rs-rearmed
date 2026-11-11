# Shared paths for tools/*.sh. Source it; every variable can be overridden from
# the environment.
RS_ROOT="${RS_ROOT:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
if [[ -z ${RS_GAME_DIR:-} ]]; then
    RS_GAME_DIR="$HOME/Library/Application Support/Steam/steamapps/common/Rocksmith2014"
    # Not installed in Steam: Steam's download of the Mac depot, when that is there
    # (tools/play-native/gamefiles.py mac-game gets it).
    if [[ ! -d $RS_GAME_DIR/Rocksmith2014.app ]]; then
        for _rs_content in "$HOME/Library/Application Support/Steam/Steam.AppBundle/Steam/Contents/MacOS/steamapps/content" \
                           "$HOME/Library/Application Support/Steam/steamapps/content"; do
            if [[ -d $_rs_content/app_221680/depot_221682/Rocksmith2014.app ]]; then
                RS_GAME_DIR=$_rs_content/app_221680/depot_221682
                break
            fi
        done
        unset _rs_content
    fi
fi
RS_GAME_APP="${RS_GAME_APP:-$RS_GAME_DIR/Rocksmith2014.app}"
RS_GAME_BIN="${RS_GAME_BIN:-$RS_GAME_APP/Contents/MacOS/Rocksmith2014}"

rs_die() { echo "error: $*" >&2; exit 64; }
