#!/bin/sh
# usage: tools/ws8/keyfifo/build.sh OUTDIR: builds OUTDIR/libkeyfifo.dylib (see keyfifo.m). A test tool: it never
# goes into an install.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
out=${1:?usage: build.sh OUTDIR}
mkdir -p "$out"
clang -arch arm64 -O1 -g -Wall -fobjc-arc -dynamiclib -framework Cocoa "$here/keyfifo.m" -o "$out/libkeyfifo.dylib"
codesign --force -s - "$out/libkeyfifo.dylib"
echo "$out/libkeyfifo.dylib"
