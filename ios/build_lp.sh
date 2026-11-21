#!/bin/sh
# Learn & Play for iPhone and iPad (experimental): the same translation, Win32 seam, D3D9 layer,
# libRSOpenGL and runtime as the Mac app (win-x86/build/build.sh), compiled for iOS, and an iOS app
# (ios/LearnPlay.xcodeproj, ios/app) that links them, carries the game image and the DLLs, and is
# signed by Xcode with the developer's own team. The game files are not in the app: ios/deploy_lp.sh
# copies them, with the saves and settings, into the app's Documents folder on the device.
#
#   ios/build_lp.sh [device|simulator] [STEP...]
#
# Steps (default: all of them, in order; a translation that is there is kept unless asked for):
#   translate   the game image to C, compiled for the platform (win-x86/build/translate.sh, 15-25 minutes)
#   modules     the DLLs, the same way (win-x86/build/build_modules.sh)
#   from-simulator  (device only, in place of translate and modules) the simulator build's game and DLL libraries,
#               marked as the device's (ios/retarget.py): the same arm64 code, without compiling it again
#   gl          libRSOpenGL, static, without its macOS-only parts (nsgl_hook.m)
#   shims       the Win32 seam, the D3D9 layer, the audio faces on the iOS audio host, the offline Steam face
#   runtime     the 32-bit runtime, the target's tables, lp_main.c, lp_screen.c, lp_settings.c
#   bundle      what the app carries: lp_game.exe, dlls/, rsmsl-corpus.bin
#   app         xcodebuild of ios/LearnPlay.xcodeproj, signed for the team (device) or not (simulator)
#
# env:
#   LP_IOS_BUILD  build root (default ${RS_BUILD_DIR:-build}/ios/lp-<platform>)
#   LP_IMAGE      the game image (lp_image.py)            default ${RS_BUILD_DIR:-build}/win-x86/image/Rocksmith2014.exe
#   LP_DLLS       a folder with msvcr100.dll, msvcp100.dll, d3dx9_42.dll, D3DCompiler_42.dll
#                                                          default ${RS_BUILD_DIR:-build}/win-x86/dlls
#   LP_GAME_DIR   the Windows game folder (for bink2w32.dll) default: the installed Mac app's copy
#   LP_CORPUS     a Metal shader corpus to carry (rsmsl-corpus.bin, from a Mac build of Learn & Play); optional
#   LP_PY         a Python with capstone                   default ${RS_BUILD_DIR:-build}/venv/bin/python
#   LP_IOS_TEAM   the signing team (default: the account signed in to Xcode's, ios/team.sh)
#   LP_IOS_MIN    the lowest iOS version (default 17.0)
#   LP_IOS_BASIC=1  without the entitlements a free Apple account can't have (more memory, extended addressing)
#   LP_JOBS       parallel compiles (default hw.ncpu)
# Nothing made here is committed: the build is derived from the user's own game and DLLs.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/.." && pwd)
runtime=$root/tools/ws7/recomp/runtime
bdir=${RS_BUILD_DIR:-$root/build}

plat=device
case ${1:-} in device|simulator) plat=$1; shift ;; esac
steps=${*:-translate modules gl shims runtime bundle app}

min=${LP_IOS_MIN:-17.0}
if [ $plat = device ]; then
  sdkname=iphoneos
  triple=arm64-apple-ios$min
else
  sdkname=iphonesimulator
  triple=arm64-apple-ios$min-simulator
fi
sdk=$(xcrun --sdk $sdkname --show-sdk-path)
b=${LP_IOS_BUILD:-$bdir/ios/lp-$plat}
mkdir -p "$b"
b=$(cd "$b" && pwd)
obj=$b/obj
jobs=${LP_JOBS:-$(sysctl -n hw.ncpu)}
py=${LP_PY:-$bdir/venv/bin/python}
[ -x "$py" ] || py=python3
export LP_PY=$py LP_JOBS=$jobs
# the platform for every compile, the generated C's too (translate.sh, build_modules.sh: LP_CFLAGS)
export LP_CFLAGS="-target $triple -isysroot $sdk"
CC="xcrun --sdk $sdkname clang -arch arm64 -target $triple -isysroot $sdk"
CXX="xcrun --sdk $sdkname clang++ -arch arm64 -target $triple -isysroot $sdk"
lp_home="$HOME/Library/Application Support/RS Re-ARMed/learn-play"
game_dir=${LP_GAME_DIR:-$lp_home/game}

step_translate() {
  echo "===== Translate the game for $plat ====="
  LP_IMAGE=${LP_IMAGE:-$bdir/win-x86/image/Rocksmith2014.exe} GEN_DIR=$b/game/gen OBJ_DIR=$b/game/obj \
    "$root/win-x86/build/translate.sh"
}

step_modules() {
  echo "===== Translate the DLLs for $plat ====="
  d=${LP_DLLS:-$bdir/win-x86/dlls}
  LP_BUILD=$b LP_MSVCR100=$d/msvcr100.dll LP_MSVCP100=$d/msvcp100.dll LP_D3DX9=$d/d3dx9_42.dll \
    LP_D3DCOMPILER=$d/D3DCompiler_42.dll LP_BINK2W32=${LP_BINK2W32:-$game_dir/bink2w32.dll} \
    "$root/win-x86/build/build_modules.sh"
}

step_from_simulator() {
  [ $plat = device ] || { echo "from-simulator is a device build's step" >&2; exit 2; }
  sim=${LP_IOS_SIM_BUILD:-$bdir/ios/lp-simulator}
  echo "===== The game and the DLLs from $sim ====="
  for lib in game/obj/libgame_rc.a modules/*/obj/libgame_rc.a; do
    for src in "$sim"/$lib; do
      [ -f "$src" ] || continue
      rel=${src#"$sim"/}
      mkdir -p "$b/$(dirname "$rel")"
      cp "$src" "$b/$rel"
      python3 -I "$here/retarget.py" "$b/$rel" --to ios
    done
  done
  mkdir -p "$b/modules/in" && cp "$sim"/modules/in/*.dll "$b/modules/in/"
}

# libRSOpenGL as a static library. The GL names and enums come from the macOS SDK's OpenGL headers (iOS has
# none): the game's D3D9 layer calls libRSOpenGL by the GL API, and nothing of OpenGL.framework is linked.
step_gl() {
  echo "===== libRSOpenGL for $plat ====="
  src=$root/shims/opengl
  o=$obj/gl
  mkdir -p "$o/inc"
  macsdk=$(xcrun --sdk macosx --show-sdk-path)
  glh=$macsdk/System/Library/Frameworks/OpenGL.framework/Headers
  rm -rf "$o/inc/OpenGL" && mkdir -p "$o/inc/OpenGL" && cp "$glh"/*.h "$o/inc/OpenGL/"
  python3 -I "$src/tools/gen-gl-api.py" "$src/game-gl-imports.txt" "$src/extra-gl-imports.txt" "$glh" \
    "$o/gl_api.inc" "$o/rsmtl_stubs.c"
  python3 -I "$src/tools/gen-gl-enums.py" "$glh" "$o/gl_enums.inc"
  common="-O2 -g -fvisibility=hidden -I$o/inc -I$o -I$src/src -I$src/src/msl -I$root/ios/compat -w"
  {
    for f in rsgl.c gltrace.c rs_gfx.c; do echo "$o/${f%.c}.o $CC -std=gnu11 $src/src/$f"; done
    echo "$o/rsmtl_stubs.o $CC -std=gnu11 $o/rsmtl_stubs.c"
    for f in mtl_context mtl_state mtl_objects mtl_program mtl_draw mtl_extras mtl_texlog mtl_render mtl_cache; do
      echo "$o/$f.o $CC -std=gnu11 -fobjc-arc $src/src/metal/$f.m"
    done
    for f in msl_xlat msl_corpus; do echo "$o/$f.o $CXX -std=gnu++17 $src/src/msl/$f.cpp"; done
  } > "$o/jobs.txt"
  # shellcheck disable=SC2016
  xargs -P "$jobs" -L 1 sh -c 'out=$1; shift; exec "$@" '"$common"' -c -o "$out"' sh < "$o/jobs.txt"
  rm -f "$o/libRSOpenGL.a"
  libtool -static -o "$o/libRSOpenGL.a" "$o"/rsgl.o "$o"/gltrace.o "$o"/rs_gfx.o "$o"/rsmtl_stubs.o "$o"/mtl_*.o \
    "$o"/msl_*.o
}

step_shims() {
  echo "===== Win32 seam for $plat ====="
  tgt=$obj/target
  "$py" -I "$root/tools/targets/targets.py" gen --target win-x86 "$tgt" >/dev/null
  shims=$root/win-x86/shims
  mods=$root/tools/ws7/recomp/overrides/mods
  o=$obj/shims
  mkdir -p "$o" && rm -f "$o"/*.o
  inc="-DRC_GUEST32 -I$root/ios/compat -I$runtime -I$shims -I$root/shims/opengl/src -I$obj/gl/inc -I$tgt -I$mods \
       -I$root/shims/coreaudio -I$root/shims/platform/include"
  for f in "$shims"/win32_*.c "$shims"/d3d9/*.c "$shims"/audio/wa_*.c "$root"/shims/coreaudio/rs_audio_*.c \
           "$root"/win-x86/online/online32.c; do
    echo "$o/$(basename "$f" .c).o $CC -O2 -g -Wall -Wno-unused-parameter $inc $f"
  done > "$o/jobs.txt"
  echo "$o/rs_hal_ios.o $CC -O2 -g -Wall -fobjc-arc $inc $root/ios/compat/rs_hal_ios.m" >> "$o/jobs.txt"
  for f in mods_shared framerate; do
    echo "$o/$f.o $CC -O2 -g -Wall -I$runtime -I$root/tools/ws7/recomp/overrides $mods/$f.c"
  done >> "$o/jobs.txt"
  echo "$o/rsp_overlay.o $CC -O2 -g -Wall $inc $root/shims/platform/src/rsp_overlay.c" >> "$o/jobs.txt"
  # shellcheck disable=SC2016
  xargs -P "$jobs" -L 1 sh -c 'out=$1; shift; exec "$@" -c -o "$out"' sh < "$o/jobs.txt"
  rm -f "$obj/win32_shims.a"
  libtool -static -o "$obj/win32_shims.a" "$o"/*.o
  echo "=== steam_api (offline) ==="
  make -s -C "$shims/steam" OUT="$obj/steam" CC="$CC" CXX="$CXX" "$obj/steam/libsteam32.a"
}

step_runtime() {
  echo "===== Runtime and app for $plat ====="
  tgt=$obj/target
  mkdir -p "$obj/rt"
  RT="$CC -O2 -g -DRC_GUEST32 -I$runtime"
  for m in rc32_mem rc32_image rc32_main rc32_teb rc32_seh rc32_wrappers rc32_hook rc_hook rc_mods rc_mid rc_file; do
    $RT -w -c "$runtime/$m.c" -o "$obj/rt/$m.o"
  done
  $RT -w -c "$tgt/rc_target.c" -o "$obj/rt/rc_target.o"
  $RT -Wall -I"$root/win-x86/shims" -c "$root/win-x86/app/lp_main.c" -o "$obj/rt/lp_main.o"
  $RT -Wall -c "$root/win-x86/app/lp_screen.c" -o "$obj/rt/lp_screen.o"
  $RT -Wall -c "$root/win-x86/app/lp_settings.c" -o "$obj/rt/lp_settings.o"
  rm -f "$obj/librt.a"
  libtool -static -o "$obj/librt.a" "$obj"/rt/*.o
}

# What the app carries beside its executable: the game image, the DLL images, the shader corpus.
step_bundle() {
  echo "===== The app's resources ====="
  r=$b/bundle
  rm -rf "$r" && mkdir -p "$r/dlls"
  cp "${LP_IMAGE:-$bdir/win-x86/image/Rocksmith2014.exe}" "$r/lp_game.exe"
  for d in msvcr100 msvcp100 d3dx9_42 d3dcompiler_42 bink2w32; do
    [ -f "$b/modules/in/$d.dll" ] && cp "$b/modules/in/$d.dll" "$r/dlls/"
  done
  corpus=${LP_CORPUS:-}
  if [ -z "$corpus" ]; then  # a Mac build's: the corpus is MSL source, the same on iOS
    for c in "$bdir/win-x86/lp/out/rsmsl-corpus.bin" "$HOME/Applications/RS Re-ARMed Learn & Play.app/Contents/Resources/rsmsl-corpus.bin"; do
      [ -f "$c" ] && corpus=$c && break
    done
  fi
  if [ -n "$corpus" ] && [ -f "$corpus" ]; then cp "$corpus" "$r/"; else echo "note: no shader corpus: the game compiles its shaders as it plays"; fi
  ls -la "$r" "$r/dlls"
}

# The libraries the app links, for the Xcode project (ios/LearnPlay.xcodeproj reads LP_IOS_BUILD's lp.xcconfig).
write_xcconfig() {
  dcomp=$b/modules/d3dcompiler_42/obj/libgame_rc.a
  bink=$b/modules/bink2w32/obj/libgame_rc.a
  # librt.a, win32_shims.a and libRSOpenGL.a whole: their weak defaults (libRSOpenGL's "unimplemented" Metal stubs,
  # the seam's no-op hooks) are in members the linker would take first, and a strong definition in a member nothing
  # else needs would never be pulled in; the Mac links libRSOpenGL as one dylib, which has them all
  libs="-Wl,-force_load,$obj/librt.a $b/game/obj/libgame_rc.a $b/modules/msvcr100/obj/libgame_rc.a $b/modules/msvcp100/obj/libgame_rc.a"
  libs="$libs $b/modules/d3dx9_42/obj/libgame_rc.a"
  [ -f "$dcomp" ] && libs="$libs $dcomp" || libs="$libs -Wl,-U,_d3dcompiler_42_rc32_mod"
  [ -f "$bink" ] && libs="$libs $bink" || libs="$libs -Wl,-U,_bink2w32_rc32_mod"
  libs="$libs -Wl,-force_load,$obj/win32_shims.a $obj/steam/libsteam32.a -Wl,-force_load,$obj/gl/libRSOpenGL.a"
  team=$("$here/team.sh")
  {
    echo "// written by ios/build_lp.sh: not in the repository"
    echo "LP_IOS_LIBS = $libs"
    echo "LP_IOS_BUILD = $b"
    echo "LP_IOS_BUNDLE = $b/bundle"
    echo "LP_VERSION = $(cat "$root/VERSION")"
    echo "IPHONEOS_DEPLOYMENT_TARGET = $min"
    [ "${LP_IOS_BASIC:-0}" = 1 ] && echo "LP_IOS_ENTITLEMENTS = app/LearnPlay-basic.entitlements"
    [ -n "$team" ] && echo "DEVELOPMENT_TEAM = $team"
  } > "$b/lp.xcconfig"
}

step_app() {
  echo "===== The app ($plat) ====="
  write_xcconfig
  if [ $plat = device ]; then
    dest="generic/platform=iOS"
    sign="-allowProvisioningUpdates"
  else
    dest="generic/platform=iOS Simulator"
    sign="CODE_SIGNING_ALLOWED=NO"
  fi
  log=$b/xcodebuild.log
  if ! xcodebuild -project "$here/LearnPlay.xcodeproj" -scheme LearnPlay -configuration Release \
    -destination "$dest" -derivedDataPath "$b/xcode" -xcconfig "$b/lp.xcconfig" ARCHS=arm64 $sign build > "$log" 2>&1; then
    grep -E ": (fatal )?error:|^error:|Undefined symbols|^ *\"_" "$log" | sort -u | head -40 >&2
    echo "the app did not build: $log" >&2
    exit 1
  fi
  app=$(ls -d "$b"/xcode/Build/Products/Release-$sdkname/*.app 2>/dev/null | head -1)
  echo "===== Done: $app ====="
}

for s in $steps; do
  case $s in
    translate|modules|gl|shims|runtime|bundle|app) "step_$s" ;;
    from-simulator) step_from_simulator ;;
    *) echo "unknown step $s" >&2; exit 2 ;;
  esac
done
