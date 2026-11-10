# Shared by tools/release/*.sh. Source it.
rel_here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
rel_root=$(cd "$rel_here/../.." && pwd)
rel_out=${RS_RELEASE_OUT:-${RS_BUILD_DIR:-$rel_root/build}/release}
rel_zip_name="RS.Re-ARMed.Installer.app.zip"

rel_die() { echo "error: $*" >&2; exit 1; }

# No builds during a game run (AGENTS.md).
rel_no_game_run() {
    local pids
    pids=$(pgrep -x 'Rocksmith2014|rs2014_lp' | tr '\n' ' ' || true)
    [[ -z $pids ]] || rel_die "a game is running (pid ${pids% }): wait for it"
}

rel_version() { head -1 "$rel_root/VERSION"; }
