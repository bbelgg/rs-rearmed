# Shared by tools/release/*.sh. Source it.
rel_here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
rel_root=$(cd "$rel_here/../.." && pwd)
rel_out=${RS_RELEASE_OUT:-${RS_BUILD_DIR:-$rel_root/build}/release}
rel_zip_name="RS.Re-ARMed.Installer.app.zip"
rel_sig_name="$rel_zip_name.sig"

rel_die() { echo "error: $*" >&2; exit 1; }

# No builds during a game run (AGENTS.md).
rel_no_game_run() {
    local pids
    pids=$(pgrep -x 'Rocksmith2014|rs2014_lp' | tr '\n' ' ' || true)
    [[ -z $pids ]] || rel_die "a game is running (pid ${pids% }): wait for it"
}

rel_version() { head -1 "$rel_root/VERSION"; }

# The key an installed installer checks a newer one against before it replaces itself with it: the public half
# of the maintainer's key, in the installer's source.
rel_update_public() {
    sed -n 's/^let updatePublicKey = "\(.*\)"$/\1/p' "$rel_root/tools/installer/RSReARMedInstaller.swift"
}
# rel_signed ZIP SIG: is SIG that key's signature of ZIP?
rel_signed() { swift "$rel_here/sign.swift" verify "$(rel_update_public)" "$1" "$2" 2> /dev/null; }
