#!/bin/bash
# Build a minimal static FFmpeg 7.1.2 with the (unmerged) Bink 2 video decoder.
#
#   build-ffmpeg.sh <prefix> <arch> [<arch>...]
#
# Produces <prefix>/<arch>/{include,lib/libavformat.a,libavcodec.a,libavutil.a}.
# Only what the Bink shim needs is enabled: the bink demuxer, the bink/bink2
# video decoders and both Bink audio decoders. No protocols: the shim feeds the
# demuxer through a custom AVIOContext.
set -euo pipefail

mkdir -p "$1"
prefix=$(cd "$1" && pwd); shift
here=$(cd "$(dirname "$0")" && pwd)
version=7.1.2
sha256=089bc60fb59d6aecc5d994ff530fd0dcb3ee39aa55867849a2bbc4e555f9c304
patch="$here/third_party/ffmpeg-bink2.patch"

mkdir -p "$prefix/src"
tarball="$prefix/src/ffmpeg-$version.tar.xz"
if [ ! -f "$tarball" ]; then
  curl -fL --retry 3 -o "$tarball.part" "https://ffmpeg.org/releases/ffmpeg-$version.tar.xz"
  mv "$tarball.part" "$tarball"
fi
echo "$sha256  $tarball" | shasum -a 256 -c -

for arch in "$@"; do
  out="$prefix/$arch"
  if [ -f "$out/lib/libavcodec.a" ] && [ "$out/.stamp" -nt "$patch" ]; then
    echo "ffmpeg ($arch): up to date"
    continue
  fi
  src="$prefix/src/ffmpeg-$version-$arch"
  rm -rf "$src" "$out"
  mkdir -p "$src"
  tar -xf "$tarball" -C "$src" --strip-components=1
  patch -d "$src" -p1 --quiet < "$patch"

  extra=()
  if [ "$arch" != "$(uname -m)" ]; then
    extra+=(--enable-cross-compile)
  fi
  if [ "$arch" = x86_64 ]; then
    extra+=(--disable-x86asm)   # avoid a nasm dependency; Bink is cheap to decode
  fi

  (cd "$src" && ./configure \
    --prefix="$out" --arch="$arch" --target-os=darwin --cc=clang \
    --extra-cflags="-arch $arch -mmacosx-version-min=11.0 -fPIC" \
    --extra-ldflags="-arch $arch -mmacosx-version-min=11.0" \
    --enable-static --disable-shared --enable-pic \
    --disable-programs --disable-doc --disable-network --disable-autodetect \
    --disable-everything --disable-avdevice --disable-avfilter \
    --disable-swscale --disable-swresample --disable-postproc \
    --enable-demuxer=bink \
    --enable-decoder=bink,bink2,binkaudio_dct,binkaudio_rdft \
    ${extra[@]+"${extra[@]}"} >/dev/null)
  make -C "$src" -j"$(sysctl -n hw.ncpu)" >/dev/null
  make -C "$src" install >/dev/null
  touch "$out/.stamp"
  echo "ffmpeg ($arch): installed to $out"
done
