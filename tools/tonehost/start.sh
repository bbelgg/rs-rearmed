#!/bin/bash
# Starts RS Tone Host with the network guard. rearmed-mods' ToneHost runs this when the game starts; it can be run by hand too.
d=$(cd "$(dirname "$0")" && pwd)
exec open -n --env DYLD_INSERT_LIBRARIES="$d/../libnetguard.dylib" --env RS_NETGUARD_LOG="$d/netguard.log" \
  --stdout "$d/host.log" --stderr "$d/host.log" "$d/RS Tone Host.app" --args --config "$d/config.json"
