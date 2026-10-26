#!/bin/bash
# check-exports.sh <shim.dylib> <game executable>
# Every _Bink* symbol the game imports must be exported by every slice of the
# shim, and nothing from the bundled FFmpeg may leak into the export table.
set -euo pipefail
shim=$1 game=$2

if [ -f "$game" ]; then
  wanted=$(nm -arch x86_64 -u "$game" | grep '^_Bink' | sort -u)
else
  echo "game not found, using the documented import list"
  wanted=$(printf '%s\n' _BinkClose _BinkCloseTrack _BinkDoFrame _BinkGetFrameBuffersInfo \
    _BinkGetSummary _BinkGetTrackData _BinkGetTrackID _BinkNextFrame _BinkOpen _BinkOpenTrack \
    _BinkRegisterFrameBuffers _BinkSetIO _BinkSetIOSize _BinkSetSoundTrack _BinkShouldSkip _BinkWait)
fi

status=0
for arch in $(lipo -archs "$shim"); do
  exported=$(nm -arch "$arch" -gU "$shim" | awk '{print $3}' | sort -u)
  missing=$(comm -23 <(echo "$wanted") <(echo "$exported"))
  leaked=$(echo "$exported" | grep -E '^_(av|ff|avio|avcodec|avformat|swr)_' || true)
  if [ -n "$missing" ]; then echo "$arch: missing: $missing"; status=1; fi
  if [ -n "$leaked" ]; then echo "$arch: leaks FFmpeg symbols: $(echo $leaked | head -c 200)"; status=1; fi
  echo "$arch: $(echo "$wanted" | wc -l | tr -d ' ') imports covered, $(echo "$exported" | wc -l | tr -d ' ') exports"
done
id=$(otool -D "$shim" | tail -1)
[ "$id" = "@executable_path/libBink2Macx64.dylib" ] || { echo "bad install name: $id"; status=1; }
exit $status
