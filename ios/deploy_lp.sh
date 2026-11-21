#!/bin/sh
# Put Learn & Play on an iPhone or iPad (or the iOS simulator): the app ios/build_lp.sh built, then the game files,
# the saves and the settings from the Mac app's folder into the app's Documents folder, the folder lp_main.c reads
# on iOS (game/, steam/, userdata/, settings.env, options.env; the logs are written to logs/ there).
#
#   ios/deploy_lp.sh [device|simulator] [STEP...]
#
# Steps (default: install files):
#   install   the app (the device: by USB or the network, unlocked and in Developer Mode)
#   files     the game files and the user's folder; on a device only what changed is copied again (about 6 GB the
#             first time); the simulator gets a link to the Mac's copy of the game files
#   launch    start the app, its output here (Control-C stops watching, not the app)
#   log       the last run's log from the device (logs/last-run.log), to build/ios/last-run-<platform>.log
#
# env:
#   LP_HOME       the Mac app's folder (default ~/Library/Application Support/RS Re-ARMed/learn-play)
#   LP_IOS_DEVICE the device (a name, UDID or ECID; default the one devicectl lists first)
#   LP_IOS_DLC=0  no dlc/ folder (the songs bought for the game), to save space on the device
#   LP_IOS_BUILD, LP_IOS_TEAM   as ios/build_lp.sh's
set -eu
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/.." && pwd)
bdir=${RS_BUILD_DIR:-$root/build}
plat=device
case ${1:-} in device|simulator) plat=$1; shift ;; esac
steps=${*:-install files}
b=${LP_IOS_BUILD:-$bdir/ios/lp-$plat}
home=${LP_HOME:-$HOME/Library/Application Support/RS Re-ARMed/learn-play}
sdkname=iphoneos
[ $plat = simulator ] && sdkname=iphonesimulator
app=$(ls -d "$b"/xcode/Build/Products/Release-$sdkname/*.app 2>/dev/null | head -1 || true)
team=$("$here/team.sh")
bundle=io.github.rsrearmed.learnplay.$team
[ -n "$app" ] && bundle=$(/usr/libexec/PlistBuddy -c "Print :CFBundleIdentifier" "$app/Info.plist")

device() {
  if [ -n "${LP_IOS_DEVICE:-}" ]; then echo "$LP_IOS_DEVICE"; return; fi
  j=$(mktemp)
  xcrun devicectl list devices --json-output "$j" >/dev/null 2>&1 || true
  python3 -I -c 'import json,sys
d=json.load(open(sys.argv[1]))["result"]["devices"]
d=[x for x in d if x.get("hardwareProperties",{}).get("platform")=="iOS"]
print(d[0]["hardwareProperties"]["udid"] if d else "")' "$j"
  rm -f "$j"
}

# The game files the app reads: the copy without what only the Windows game, its launcher or the Mac's mods use.
# static.psarc is the title logo's link, which the app makes itself (lp_main.c: title_logo).
stage_game() {
  src=$(cd "$home/game" && pwd -P)
  st=$bdir/ios/deploy/game
  mkdir -p "$st"
  set -- --exclude /Rocksmith2014.exe --exclude /RocksmithConfiguration.pdf --exclude /UPlay --exclude /Tmp \
    --exclude /Profile_AutoBackups --exclude /uplay_r1_loader.dll --exclude /systemdetection.dll \
    --exclude '/RSMods.ini*' --exclude /rearmed-mods --exclude /static.psarc
  [ "${LP_IOS_DLC:-1}" = 0 ] && set -- "$@" --exclude /dlc
  # hard links: the staging costs no space (the same volume), else copies
  if [ "$(stat -f %d "$src")" = "$(stat -f %d "$st")" ]; then
    rsync -a --delete --delete-excluded --link-dest="$src" "$@" "$src"/ "$st"/
  else
    rsync -a --delete --delete-excluded "$@" "$src"/ "$st"/
  fi
  echo "$st"
}

step_install() {
  [ -n "$app" ] || { echo "no app: run ios/build_lp.sh $plat first" >&2; exit 1; }
  if [ $plat = simulator ]; then
    xcrun simctl list devices booted | grep -q Booted || { echo "no simulator booted (open Simulator.app)" >&2; exit 1; }
    xcrun simctl install booted "$app"
  else
    xcrun devicectl device install app --device "$(device)" "$app"
  fi
  echo "installed $bundle"
}

step_files() {
  if [ $plat = simulator ]; then
    docs=$(xcrun simctl get_app_container booted "$bundle" data)/Documents
    mkdir -p "$docs"
    rm -rf "$docs/game"
    ln -s "$(cd "$home/game" && pwd -P)" "$docs/game"  # the simulator runs on this Mac's disk
    for d in steam userdata; do [ -d "$home/$d" ] && rsync -a "$home/$d"/ "$docs/$d"/; done
    for f in settings.env options.env; do [ -f "$home/$f" ] && cp "$home/$f" "$docs/"; done
    echo "the simulator's Documents: $docs"
    return
  fi
  dev=$(device)
  st=$(stage_game)
  echo "copying the game files to the device (only what changed; the first time takes a while: keep it unlocked)"
  # Never devicectl's --remove-existing-content: given Documents/game/dlc it emptied the whole of Documents. To take a
  # folder off the device, delete it in the Files app (On My iPhone > Learn & Play).
  # one item at a time, each tried again: a long transfer can lose its connection (the device locking, say), and
  # what arrived already is not sent again
  for item in "$st"/* "$st"/.rearmed; do
    [ -e "$item" ] || continue
    n=$(basename "$item")
    for try in 1 2 3; do
      if xcrun devicectl device copy to --device "$dev" --domain-type appDataContainer --domain-identifier "$bundle" \
        --source "$item" --destination "Documents/game/$n" >/dev/null 2>"$bdir/ios/deploy-copy.err"; then
        echo "  $n"
        break
      fi
      [ $try = 3 ] && { echo "  $n: not copied: $(grep -m1 ERROR "$bdir/ios/deploy-copy.err")" >&2; exit 1; }
      echo "  $n: again ($try)"
      sleep 3
    done
  done
  for d in steam userdata; do
    [ -d "$home/$d" ] && xcrun devicectl device copy to --device "$dev" --domain-type appDataContainer \
      --domain-identifier "$bundle" --source "$home/$d" --destination "Documents/$d"
  done
  for f in settings.env options.env; do
    [ -f "$home/$f" ] && xcrun devicectl device copy to --device "$dev" --domain-type appDataContainer \
      --domain-identifier "$bundle" --source "$home/$f" --destination "Documents/$f"
  done
  echo "copied to $bundle's Documents"
}

step_launch() {
  if [ $plat = simulator ]; then
    xcrun simctl launch --console-pty --terminate-running-process booted "$bundle"
  else
    xcrun devicectl device process launch --device "$(device)" --terminate-existing --console "$bundle"
  fi
}

step_log() {
  out=$bdir/ios/last-run-$plat.log
  if [ $plat = simulator ]; then
    cp "$(xcrun simctl get_app_container booted "$bundle" data)/Documents/logs/last-run.log" "$out"
  else
    xcrun devicectl device copy from --device "$(device)" --domain-type appDataContainer --domain-identifier "$bundle" \
      --source Documents/logs/last-run.log --destination "$out" >/dev/null
  fi
  echo "$out"
}

for s in $steps; do
  case $s in
    install|files|launch|log) "step_$s" ;;
    *) echo "unknown step $s" >&2; exit 2 ;;
  esac
done
