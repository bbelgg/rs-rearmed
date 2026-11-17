# Shared paths for tools/*.sh. Source it; every variable can be overridden from
# the environment.
RS_ROOT="${RS_ROOT:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
if [[ -z ${RS_GAME_DIR:-} ]]; then
    # Steam's folder: RS_STEAM_DIR, as tools/play-native/gamefiles.py and Learn & Play's scripts take it.
    _rs_steam=${RS_STEAM_DIR:-$HOME/Library/Application Support/Steam}
    RS_GAME_DIR="$_rs_steam/steamapps/common/Rocksmith2014"
    # Not installed in Steam: Steam's download of the Mac depot, when that is there
    # (tools/play-native/gamefiles.py mac-game gets it, and says whether it is whole: this only looks).
    if [[ ! -d $RS_GAME_DIR/Rocksmith2014.app ]]; then
        for _rs_content in "$_rs_steam/Steam.AppBundle/Steam/Contents/MacOS/steamapps/content" \
                           "$_rs_steam/steamapps/content"; do
            if [[ -d $_rs_content/app_221680/depot_221682/Rocksmith2014.app ]]; then
                RS_GAME_DIR=$_rs_content/app_221680/depot_221682
                break
            fi
        done
        unset _rs_content
    fi
    unset _rs_steam
fi
RS_GAME_APP="${RS_GAME_APP:-$RS_GAME_DIR/Rocksmith2014.app}"
RS_GAME_BIN="${RS_GAME_BIN:-$RS_GAME_APP/Contents/MacOS/Rocksmith2014}"

rs_die() { echo "error: $*" >&2; exit 64; }
