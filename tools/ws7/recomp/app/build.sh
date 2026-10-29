#!/bin/sh
# Build the arm64 Rocksmith2014.app (native platform shell + translated engine).
#
# usage: tools/ws7/recomp/app/build.sh OUTDIR [--stub] [--jobs N]
#   default  link libgame_rc.a ($RC_OBJ_DIR, build/build.sh) + generated thunks + runtime + shell + seam.
#            Symbols the library does not define are listed in OUTDIR/work/undefined.txt and
#            linked as abort stubs (rc_fail), exactly as tools/ws7/recomp/build/build.sh does.
#   --stub   no translated library: the shell runs with the platform stub hooks (a layout/link check).
# env:  RC_OBJ_DIR     libgame_rc.a directory     (default analysis/ws7/recomp/obj)
#       RC_IMAGE       plain x86_64 image         (default analysis/cg/Rocksmith2014_x64_plain)
#       RC_THUNKS      pre-generated thunks.c     (default: generated into OUTDIR/work)
#       RC_OVERRIDES   extra .c/.o/.a linked before libgame_rc.a (native overrides; optional;
#                      default: tools/ws7/recomp/overrides/build/liboverrides.a if it exists)
#       RC_PY          python with capstone       (default analysis/venv/bin/python)
#       SHIM_STEAM / SHIM_BINK / SHIM_CG   arm64 dylibs to bundle (defaults: first arm64 build found)
#       SHIM_OPENGL    libRSOpenGL.dylib (shims/opengl; default build/shims/opengl/libRSOpenGL.dylib):
#                      the app links it instead of OpenGL.framework (it re-exports the framework) and
#                      bundles it as Contents/MacOS/libRSOpenGL.dylib
#       RS_MSL_CORPUS_FILE  the Metal backend's offline shader corpus (rsmsl-corpus.bin, tools/metal/
#                      build-msl-corpus.sh; default build/metal/msl-corpus/ or build/shims/opengl/msl-corpus/
#                      if one exists): bundled as Contents/Resources/rsmsl-corpus.bin, where libRSOpenGL
#                      looks; optional (without it shaders are translated at run time)
#       RC_UNIT_TEST=1 also build and run test/seam_test (no game needed)
#       RC_LTO, RC_LTO_JOBS, RC_LTO_CACHE (default OUTDIR/lto-cache), RC_PGO, RC_PGO_DIR (default
#                      OUTDIR/pgo): see tools/ws7/recomp/build/optflags.sh (libgame_rc.a must be built with the same)
# Output: OUTDIR/Rocksmith2014.app (arm64). It must sit in the game folder (the shell uses the
#         folder holding the .app as the game root); the image is cloned into Contents/Resources.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../../.." && pwd)
out=${1:?usage: build.sh OUTDIR [--stub] [--jobs N]}
shift
stub=0
jobs=$(sysctl -n hw.ncpu)
while [ $# -gt 0 ]; do
  case $1 in
    --stub) stub=1 ;;
    --jobs) jobs=$2; shift ;;
    *) echo "unknown option $1" >&2; exit 64 ;;
  esac
  shift
done
rt=$root/tools/ws7/recomp/runtime
plat=$root/shims/platform
obj=${RC_OBJ_DIR:-$root/analysis/ws7/recomp/obj}
img=${RC_IMAGE:-$root/analysis/cg/Rocksmith2014_x64_plain}
py=${RC_PY:-$root/analysis/venv/bin/python}
work=$out/work
. "$root/tools/ws7/recomp/build/optflags.sh"
app=$out/Rocksmith2014.app
mkdir -p "$work" "$app/Contents/MacOS" "$app/Contents/Resources" "$app/Contents/Frameworks/Cg.framework"

# first existing arm64 slice among the candidates
arm64_of() {
  for f in "$@"; do
    [ -f "$f" ] && lipo -archs "$f" 2>/dev/null | grep -qw arm64 && { echo "$f"; return; }
  done
}
shim_steam=${SHIM_STEAM:-$(arm64_of "$root/shims/steam/build/libsteam_api.dylib" "$root/build-rosetta/shims/steam/libsteam_api.dylib")}
shim_bink=${SHIM_BINK:-$(arm64_of "$root/build/shims/bink/libBink2Macx64.dylib" "$root/build-rosetta/shims/bink/libBink2Macx64.dylib")}
shim_cg=${SHIM_CG:-$(arm64_of "$root/build/shims/cg/Cg" "$root/build-rosetta/shims/cg/Cg")}
shim_opengl=${SHIM_OPENGL:-$(arm64_of "$root/build/shims/opengl/libRSOpenGL.dylib" "$root/build-rosetta/shims/opengl/libRSOpenGL.dylib")}
# GL (game, shell, Cg) goes through libRSOpenGL: linked, not only bundled, so it is required.
[ -n "$shim_opengl" ] && [ -f "$shim_opengl" ] ||
  { echo "no arm64 libRSOpenGL.dylib (build the opengl target, or set SHIM_OPENGL)" >&2; exit 1; }

CF="-arch arm64 -O2 -g -mmacosx-version-min=11.0 -fno-strict-aliasing -ffp-contract=off -Wall -Wno-unused-parameter -Wno-unused-function -I$rt -I$plat/include -I$here $rc_pgo"
mkdir -p "$work/o"
cc_obj() {  # src [extra flags]  -> work/o/<base>.o
  src=$1; shift
  b=$(basename "$src"); b=${b%.*}
  case $src in *.S) lto= ;; *) lto=$rc_lto ;; esac
  clang $CF $lto "$@" -c "$src" -o "$work/o/$b.o"
  echo "$work/o/$b.o"
}

objs=""
for s in rsp_engine.c rsp_paths.c rsp_config.c rsp_logic.c rsp_overlay.c; do objs="$objs $(cc_obj "$plat/src/$s")"; done
objs="$objs $(cc_obj "$plat/src/rsp_shell.m" -fno-objc-arc -Wno-deprecated-declarations)"
objs="$objs $(cc_obj "$plat/src/rsp_present_metal.m" -fno-objc-arc)"
objs="$objs $(cc_obj "$here/seam_platform.c")"
for s in "$rt"/rc_*.c; do objs="$objs $(cc_obj "$s" -w)"; done
for s in "$rt"/rc_*.S; do objs="$objs $(cc_obj "$s")"; done
FW="-framework AppKit $shim_opengl -framework CoreVideo -framework IOKit -framework Metal -framework QuartzCore -framework Foundation -framework CoreFoundation -lobjc -lz"
exe=$app/Contents/MacOS/Rocksmith2014
# rc_main.o names the profile file of a gen build (the image TUs carry the counters only)
pgo_main=$(rc_pgo_main "$out/pgo")
LF="$(rc_lto_link "$out/lto-cache") ${pgo_main:+-fprofile-instr-generate}"

if [ $stub = 1 ]; then
  objs="$objs $(cc_obj "$here/rc_main.c" -DRCAPP_STUB $pgo_main) $(cc_obj "$here/stub_main.c")"
  clang -arch arm64 $LF $objs $FW -Wl,-undefined,error -o "$exe"
else
  [ -f "$obj/libgame_rc.a" ] || { echo "no $obj/libgame_rc.a (run tools/ws7/recomp/build/build.sh, or use --stub)" >&2; exit 1; }
  thunks=${RC_THUNKS:-$work/thunks.c}
  if [ -z "${RC_THUNKS:-}" ]; then
    "$py" -I "$root/tools/ws7/recomp/thunks/gen_thunks.py" "$img" "$thunks" --report "$work/thunks.json" > "$work/thunks.log" 2>&1 ||
      { echo "thunk generation failed, see $work/thunks.log" >&2; exit 1; }
  fi
  objs="$objs $(cc_obj "$here/rc_main.c" $pgo_main) $(cc_obj "$thunks" -w -DRC_ABI_TRUST)"
  ovr=${RC_OVERRIDES:-}
  [ -n "$ovr" ] || { [ -f "$root/tools/ws7/recomp/overrides/build/liboverrides.a" ] && ovr=$root/tools/ws7/recomp/overrides/build/liboverrides.a || true; }
  link() {  # extra objects...
    # -export_dynamic: the runtime stays exported to the mods (rc_hook.h) also when RC_LTO would internalize it
    clang -arch arm64 $LF $objs "$@" $ovr "$obj/libgame_rc.a" $FW -lc++ -Wl,-stack_size,0x10000000 -Wl,-export_dynamic -Wl,-undefined,error -o "$exe" 2> "$work/link.log"
  }
  if ! link; then
    grep -o '"_[A-Za-z0-9_$.]*"' "$work/link.log" | tr -d '"' | sed 's/^_//' | sort -u > "$work/undefined.txt"
    echo "link: $(wc -l < "$work/undefined.txt") undefined symbols (list: $work/undefined.txt); linking abort stubs"
    head -3 "$work/link.log" | cut -c1-200
    { echo '#include "rc.h"'
      while read s; do echo "void $s(rc_ctx *c) { rc_fail(\"undefined at link: $s\", 0); }"; done < "$work/undefined.txt"
    } > "$work/undef_stubs.c"
    stubo=$(cc_obj "$work/undef_stubs.c" -w)
    link "$stubo" || { echo "link failed"; tail -8 "$work/link.log"; exit 1; }
  else
    : > "$work/undefined.txt"
  fi
  # the image goes into the bundle (APFS clone: no extra space)
  cp -c "$img" "$app/Contents/Resources/Rocksmith2014.x86_64" 2>/dev/null || cp "$img" "$app/Contents/Resources/Rocksmith2014.x86_64"
fi

cp "$here/Info.plist" "$app/Contents/Info.plist"
# the original app's icon (CFBundleIconFile RS2014.icns), so the Dock shows it
game_res="${RS_GAME_DIR:-$HOME/Library/Application Support/Steam/steamapps/common/Rocksmith2014}/Rocksmith2014.app/Contents/Resources"
[ -f "$game_res/RS2014.icns" ] && cp "$game_res/RS2014.icns" "$app/Contents/Resources/" || echo "warning: no RS2014.icns in $game_res" >&2
printf 'APPL????' > "$app/Contents/PkgInfo"
[ -n "$shim_steam" ] && cp "$shim_steam" "$app/Contents/MacOS/libsteam_api.dylib" || echo "warning: no arm64 libsteam_api.dylib" >&2
[ -n "$shim_bink" ] && cp "$shim_bink" "$app/Contents/MacOS/libBink2Macx64.dylib" || echo "warning: no arm64 libBink2Macx64.dylib" >&2
[ -n "$shim_cg" ] && cp "$shim_cg" "$app/Contents/Frameworks/Cg.framework/Cg" || echo "warning: no arm64 Cg" >&2
cp "$shim_opengl" "$app/Contents/MacOS/libRSOpenGL.dylib"
# the shader corpus is data: Contents/Resources (a non-code file in Contents/MacOS breaks the bundle's
# signature seal); libRSOpenGL finds it there as ../Resources/rsmsl-corpus.bin
rm -f "$app/Contents/MacOS/rsmsl-corpus.bin" "$app/Contents/Resources/rsmsl-corpus.bin"
if [ -n "${RS_MSL_CORPUS_FILE:-}" ]; then
  [ -f "$RS_MSL_CORPUS_FILE" ] || { echo "RS_MSL_CORPUS_FILE: no such file: $RS_MSL_CORPUS_FILE" >&2; exit 1; }
  corpus=$RS_MSL_CORPUS_FILE
else
  corpus=$(for f in "$root/build/metal/msl-corpus/rsmsl-corpus.bin" "$root/build/shims/opengl/msl-corpus/rsmsl-corpus.bin"; do
    [ -f "$f" ] && { echo "$f"; break; }; done || true)
fi
if [ -n "$corpus" ]; then cp "$corpus" "$app/Contents/Resources/rsmsl-corpus.bin"
else echo "note: no rsmsl-corpus.bin (Metal translates shaders at run time); set RS_MSL_CORPUS_FILE" >&2; fi
shims="$app/Contents/MacOS/libsteam_api.dylib $app/Contents/MacOS/libBink2Macx64.dylib $app/Contents/MacOS/libRSOpenGL.dylib $app/Contents/Frameworks/Cg.framework/Cg"
# lipo -thin when a fat shim was found, so every bundled Mach-O is arm64
for f in $shims; do
  [ -f "$f" ] && [ "$(lipo -archs "$f")" != arm64 ] && lipo -thin arm64 "$f" -output "$f.thin" && mv "$f.thin" "$f"
done
for f in $shims; do [ -f "$f" ] && codesign --force -s - "$f" 2>/dev/null || true; done
codesign --force -s - "$exe" 2>/dev/null
echo "built $app"
lipo -archs "$exe"

if [ "${RC_UNIT_TEST:-0}" = 1 ]; then sh "$here/test/build.sh" "$out/test"; fi
