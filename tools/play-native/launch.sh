#!/bin/bash
# Everyday launcher of the native arm64 Rocksmith 2014 (installed by tools/play-native/install.sh in
# ~/Applications/RS Re-ARMed.app, whose executable, launch-stub.c, runs it). Offline, and self-contained:
#   - the game reads only $HOME_DIR/game, the install's own copy of the game files (install.sh, with
#     tools/play-native/gamefiles.py; complete when .rearmed/manifest.json is there, else this asks for a
#     refresh; for a game on another drive game/ is a link to the copy on that drive, and when the drive is
#     not connected this says so), with this build's Rocksmith2014.app (the shell takes the folder that holds the .app as the
#     game root), an offline Rocksmith.ini made from the copy of the install's ([Net] AutoLogin=0,
#     [Renderer.MacOS] DisableBrowser=1, [Audio] LatencyBuffer from $RS_LATENCY_BUFFER or the game's own
#     saved choice) and static.psarc linked to the "Re-ARMed 2026" title logo (static.psarc.rearmed) when
#     it is there, else to the original. A Steam update, uninstall or library move changes none of it;
#   - the Steam shim runs in stub mode with its cloud in $HOME_DIR/steam: on the first launch it is
#     seeded with a copy of the real Steam/userdata/<account>/221680/remote (profiles,
#     LocalProfiles.json). The real Steam folder is never written, and `crd` (stored Ubisoft
#     credentials) is never copied;
#   - the DLC the account owns (the ones Steam installed, recorded in the copy's .rearmed/steam.env with
#     the language), the free 248750 that custom songs carry and every AppID the copy's dlc/ archives
#     carry (recorded there too: custom songs show whatever AppID they carry) count as owned, and so does
#     the first Rocksmith's DLC the account owns (RS_STEAM_OWNED, recorded there at a refresh); set
#     RS_STEAM_DLC=all with launchctl setenv to own every AppID. The Steam account (its saves and profiles) is the one the copy
#     recorded there too (RS_STEAM_ID: the account whose license has the Mac game), so logging in to Steam
#     with another account (e.g. one for Learn & Play) changes nothing here; RS_STEAM_ID with launchctl
#     setenv picks another. Without Steam's loginusers.vdf (Steam uninstalled) the account
#     is the one whose saves are in $HOME_DIR/steam;
#   - libnetguard refuses every non-local connection and logs to $HOME_DIR/logs/netguard.log;
#   - the renderer: $HOME_DIR/renderer (metal or opengl; see below).
# Steam's game folder (and the Rosetta game) is never read or changed here.
set -u
HOME_DIR=${RS_NATIVE_HOME:-"$HOME/Library/Application Support/RS Re-ARMed"}
stage=$HOME_DIR/game
logs=$HOME_DIR/logs

fail() {
    /usr/bin/osascript -e "display alert \"RS Re-ARMed\" message \"$1\" as critical" >/dev/null 2>&1
    echo "launch: $1" >&2
    exit 1
}

# A game on another drive: the copy is there too and game/ is a link to it (gamefiles.py). That drive not
# connected, or the copy on it gone, rather than "not installed".
if [[ -L $stage && ! -e $stage ]]; then
    target=$(readlink "$stage")
    [[ $target == /* ]] || target=$HOME_DIR/$target
    drive=
    [[ $target == /Volumes/?* ]] && { drive=${target#/Volumes/}; drive=${drive%%/*}; }
    # mounted: a folder in /Volumes on a volume of its own (not one left there)
    if [[ -n $drive ]] && ! [[ -d /Volumes/$drive && $(stat -f %d "/Volumes/$drive") != "$(stat -f %d /Volumes)" ]]; then
        fail "The drive with the game files ($drive) isn't connected. Connect it and start RS Re-ARMed again."
    fi
    fail "The game files on the other drive are gone ($target is not there). Run the RS Re-ARMed Installer again for a new copy."
fi
# macOS's privacy answer for a folder or a drive: "not permitted", not a missing file.
ls "$stage/.rearmed" 2>&1 | grep -q "not permitted" &&
    fail "macOS is not letting RS Re-ARMed read the game files. In System Settings, open Privacy & Security, then Files & Folders, turn on Removable Volumes (or the folder) for RS Re-ARMed, and start it again."
[[ -x "$stage/Rocksmith2014.app/Contents/MacOS/Rocksmith2014" ]] ||
    fail "The native build is not installed. Run tools/play-native/install.sh in the RS Re-ARMed repository."
[[ -f "$HOME_DIR/libnetguard.dylib" ]] || fail "libnetguard.dylib is missing; refusing to start without it."
if [[ ! -f "$stage/.rearmed/manifest.json" ]]; then
    [[ -d "$stage/.rearmed" ]] &&
        fail "The copy of the game files is incomplete (a copy or refresh was stopped). Open the RS Re-ARMed Installer and click Refresh Game Files (or run tools/play-native/install.sh --refresh-game)."
    fail "RS Re-ARMed has no copy of the game files yet. Run the RS Re-ARMed Installer (or tools/play-native/install.sh) again."
fi
if pgrep -x Rocksmith2014 >/dev/null || pgrep -x Rocksmith2014_x64_plain >/dev/null; then
    fail "Rocksmith 2014 is already running."
fi
mkdir -p "$logs" "$HOME_DIR/steam"

# settings.env (docs/mac/INSTALL.md "Settings and switches"): the built-in mods switched off in the installer
# (Built-in Mods...). Lines KEY=0 or KEY=1 of the keys below, whitespace ignored, # a comment; each is passed
# to the game unless the environment has another value (launchctl setenv wins), and what was used goes at the
# top of last-run.log. A value that wins is passed too: open starts the game with launchd's environment, so one
# this launcher was given some other way (open --env, a Terminal) would not reach it, though the log says it won.
# Learn & Play reads its own the same way (win-x86/app/lp_settings.c); the installer writes both, and
# tools/play-native/test_settings.py checks the three lists agree.
settings_keys=(RS_MODS_BYPASS RS_MODS_FIXES RS_MODS_INTRO RS_MODS_AUTOSAVE RS_MODS_OVERLAY RS_MODS_MULTIPLAYER RS_REARMED)
settings_env() {  # FILE -> settings_args (--env K=V for open, the value used) and settings_log; K set here
    settings_args=() settings_log=()
    local line k v seen=" "
    [[ -f $1 ]] || return 0
    while IFS= read -r line || [[ -n $line ]]; do
        line=${line//[[:space:]]/}
        [[ -z $line || $line == \#* ]] && continue
        k=${line%%=*} v=${line#*=}
        if [[ $line != *=* || " ${settings_keys[*]} " != *" $k "* || ! $v =~ ^[01]$ ]]; then
            settings_log+=("[settings] ignored: $line")
        elif [[ $seen == *" $k "* ]]; then
            settings_log+=("[settings] ignored: $line (repeated)")
        elif [[ -n ${!k:-} && ${!k} != "$v" ]]; then
            seen+="$k "
            settings_args+=(--env "$k=${!k}")
            settings_log+=("[settings] $k=$v not used: the environment has $k=${!k}")
        else
            seen+="$k "
            printf -v "$k" %s "$v"
            settings_args+=(--env "$k=$v")
            settings_log+=("[settings] $k=$v")
        fi
    done < "$1"
}
settings_env "$HOME_DIR/settings.env"

# The title logo: static.psarc is a link to one of the two copies.
if [[ -f "$stage/static.psarc.rearmed" && "${RS_REARMED:-1}" != 0 ]]; then
    ln -sfn static.psarc.rearmed "$stage/static.psarc"
else
    ln -sfn static.psarc.original "$stage/static.psarc"
fi
# [Audio] LatencyBuffer, the game's output buffer in steps of 256 frames (see README, FAQ): RS_LATENCY_BUFFER=N
# (1-16) when set, else the value in the copy, which the game's Audio settings save there, else the install's.
lb=${RS_LATENCY_BUFFER:-}
[[ $lb =~ ^([1-9]|1[0-6])$ ]] ||
    lb=$(awk '{ sub(/\r$/, "") } /^\[/ { sec = $0 }
              sec == "[Audio]" && sub(/^LatencyBuffer=/, "") { print; exit }' "$stage/Rocksmith.ini" 2>/dev/null)
[[ $lb =~ ^([1-9]|1[0-6])$ ]] || lb=
# What the game saved stays (a run showed the game writes all of its settings to this file): the copy's ini from
# Steam's folder is only the start. The keys below are forced either way.
ini=$stage/Rocksmith.ini
[[ -f $ini ]] || ini=$stage/.rearmed/Rocksmith.ini
if [[ -f $ini ]]; then
    awk -v lb="$lb" '{ sub(/\r$/, "") }
         /^\[/ { sec = $0 }
         sec == "[Net]" && /^AutoLogin=/ { next }
         sec == "[Renderer.MacOS]" && /^DisableBrowser=/ { print "DisableBrowser=1"; next }
         sec == "[Audio]" && lb != "" && /^LatencyBuffer=/ { next }
         { print }
         $0 == "[Net]" { print "AutoLogin=0" }
         $0 == "[Audio]" && lb != "" { print "LatencyBuffer=" lb }' "$ini" > "$stage/Rocksmith.ini.new" &&
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

# Renderer (docs/mac/INSTALL.md "Renderer"): $HOME_DIR/renderer says metal (the default) or opengl. The
# game reads it itself (direct starts too) and falls back to OpenGL when Metal cannot start or ended
# abnormally twice in a row, so for metal nothing is passed (an RS_GL_BACKEND=metal would force Metal
# past that fallback). For opengl the launcher sets RS_GL_BACKEND=gl, which spares the game a
# restart. An RS_GL_BACKEND in the launcher's own environment wins.
renderer_env=()
if [[ -n ${RS_GL_BACKEND:-} ]]; then
    renderer_env=(--env RS_GL_BACKEND="$RS_GL_BACKEND")
else
    case $(tr -d '[:space:]' < "$HOME_DIR/renderer" 2>/dev/null | tr '[:upper:]' '[:lower:]') in
        opengl|gl) renderer_env=(--env RS_GL_BACKEND=gl) ;;
    esac
fi

# The Steam facts the copy recorded (the installed DLC, the language, the account), unless set already
# (launchctl setenv).
steam_env=()
while IFS='=' read -r k v; do
    [[ $k =~ ^RS_STEAM_[A-Z_]+$ && -n $v && -z ${!k:-} ]] && steam_env+=(--env "$k=$v")
done < <(grep -v '^#' "$stage/.rearmed/steam.env" 2>/dev/null)
# The account: RS_STEAM_ID set, else the one the copy recorded (the account whose license has the Mac game,
# whoever is logged in to Steam now), else without Steam's list of accounts (Steam uninstalled) the account
# whose saves are here; else the shim takes the account Steam last logged in.
if [[ -z ${RS_STEAM_ID:-} && " ${steam_env[*]-} " != *" RS_STEAM_ID="* &&
      ! -f "$HOME/Library/Application Support/Steam/config/loginusers.vdf" ]]; then
    accounts=()
    for d in "$HOME_DIR/steam"/*/221680; do
        a=$(basename "$(dirname "$d")")
        [[ -d $d && $a =~ ^[0-9]+$ ]] && accounts+=("$a")
    done
    [[ ${#accounts[@]} == 1 ]] && steam_env+=(--env "RS_STEAM_ID=$((76561197960265728 + accounts[0]))")
fi

[[ -f "$logs/last-run.log" ]] && mv -f "$logs/last-run.log" "$logs/previous-run.log"
# The game's output is added after these lines (open --stdout appends).
[[ ${#settings_log[@]} == 0 ]] || printf '%s\n' "${settings_log[@]}" > "$logs/last-run.log"
# Started through LaunchServices as an app of its own: exec'ing the game from this (LSUIElement)
# launcher leaves it without a window when the launcher itself was opened from the Finder. The
# environment comes from launchd plus --env, so nothing else of this shell's leaks in.
exec /usr/bin/open -n -a "$stage/Rocksmith2014.app" \
    --env RS_STEAM_MODE=stub --env RS_STEAM_DATA_DIR="$HOME_DIR/steam" \
    --env RS_NETGUARD_LOG="$logs/netguard.log" \
    --env DYLD_INSERT_LIBRARIES="$HOME_DIR/libnetguard.dylib" \
    --env RS_NATIVE_HOME="$HOME_DIR" ${renderer_env[@]+"${renderer_env[@]}"} \
    ${steam_env[@]+"${steam_env[@]}"} ${settings_args[@]+"${settings_args[@]}"} \
    --stdout "$logs/last-run.log" --stderr "$logs/last-run.log"
