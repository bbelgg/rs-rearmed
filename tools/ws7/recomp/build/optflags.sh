# Optimisation switches shared by the build scripts (sourced, POSIX sh). Unset = the default build.
#   RC_LTO=thin          ThinLTO: the image TUs (except --o1 ones), runtime, app, thunks and overrides are
#                        compiled to bitcode and optimised across TUs when the exe is linked
#   RC_LTO_JOBS=N        ThinLTO backend threads at the exe link (default 6)
#   RC_LTO_CACHE=DIR     ThinLTO cache, so a relink only recompiles changed modules (callers set a default)
#   RC_PGO=gen           instrument (-fprofile-instr-generate); the app writes RC_PGO_DIR/rs-<pid>.profraw
#                        in continuous mode (%c: counters are mmapped to the file, so the game's _exit and
#                        crashes keep the profile); LLVM_PROFILE_FILE overrides the path (keep %c in it)
#   RC_PGO=use:FILE      optimise with a merged profile (build/pgo-merge.sh): -fprofile-instr-use=FILE
#   RC_PGO_DIR=DIR       where a gen build's app writes its profiles (callers set a default)
# Sets rc_lto (compile flags for C/ObjC), rc_pgo (compile flags), rc_pgo_dep (the profile file or empty).
# rc_lto_link CACHE prints the link flags for ThinLTO, rc_pgo_main DIR the flags for the one TU that
# names the profile file.
rc_lto= rc_pgo= rc_pgo_dep=
case ${RC_LTO:-} in
  "") ;;
  thin) rc_lto=-flto=thin ;;
  *) echo "RC_LTO=$RC_LTO: only 'thin' is supported" >&2; exit 64 ;;
esac
case ${RC_PGO:-} in
  "") ;;
  gen) rc_pgo=-fprofile-instr-generate ;;
  use:?*)
    rc_pgo_dep=${RC_PGO#use:}
    [ -f "$rc_pgo_dep" ] || { echo "RC_PGO: no profile $rc_pgo_dep" >&2; exit 1; }
    rc_pgo_dep=$(cd "$(dirname "$rc_pgo_dep")" && pwd)/$(basename "$rc_pgo_dep")
    case $rc_pgo_dep in *[[:space:]]*) echo "RC_PGO: the profile path must not contain spaces" >&2; exit 1 ;; esac
    rc_pgo="-fprofile-instr-use=$rc_pgo_dep -Wno-profile-instr-out-of-date -Wno-profile-instr-unprofiled" ;;
  *) echo "RC_PGO=$RC_PGO: use gen or use:FILE.profdata" >&2; exit 64 ;;
esac
rc_lto_link() {  # cache-dir
  [ -n "$rc_lto" ] || return 0
  mkdir -p "${RC_LTO_CACHE:-$1}"
  echo "${RC_OPT:--O2} -Wl,-cache_path_lto,${RC_LTO_CACHE:-$1} -Wl,-mllvm,-threads=${RC_LTO_JOBS:-6}"
}
rc_pgo_main() {  # profile-dir
  [ "${RC_PGO:-}" = gen ] || return 0
  echo "-fprofile-instr-generate=${RC_PGO_DIR:-$1}/rs-%p%c.profraw"
}
