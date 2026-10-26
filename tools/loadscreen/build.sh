#!/usr/bin/env bash
# Build static.psarc with the "Re-ARMed 2026" tag sprayed on the title-screen logo.
#
#   tools/loadscreen/build.sh <out.psarc> [variant]
#
# Reads the install's static.psarc (or static.psarc.orig if present), rewrites
# gfxassets/views/logo_elk_final.dds and logo_graded_elk_final.dds (both are
# used by titlescreen.gfx), and writes the result to <out.psarc>. The install
# is not touched. Needs a python with Pillow, numpy and etcpak (DXT5 encoder;
# Pillow's own smears the tag's outline): set RS_PY, or a venv is made in $TMPDIR.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
tools=$(dirname "$here")
source "$tools/rs-env.sh"
out=${1:?usage: build.sh <out.psarc> [variant]}
variant=${2:-A}

src="$RS_GAME_DIR/static.psarc"
[[ -f "$RS_GAME_DIR/static.psarc.orig" ]] && src="$RS_GAME_DIR/static.psarc.orig"

if [[ -z "${RS_PY:-}" ]]; then
    venv="${TMPDIR:-/tmp}/rs-loadscreen-venv"
    [[ -x "$venv/bin/python" ]] || python3 -m venv "$venv"
    "$venv/bin/python" -c "import PIL, numpy, etcpak" 2>/dev/null || "$venv/bin/pip" -q install pillow numpy etcpak
    RS_PY="$venv/bin/python"
fi

work=$(mktemp -d "${TMPDIR:-/tmp}/rs-loadscreen.XXXXXX")
trap 'rm -rf "$work"' EXIT
python3 "$tools/psarc_extract.py" "$src" "$work/x" --match _elk_final.dds >/dev/null

args=()
for l in logo_elk_final logo_graded_elk_final; do
    dds="$work/x/gfxassets/views/$l.dds"
    "$RS_PY" - "$dds" "$work/$l.png" <<'EOF'
import sys
from PIL import Image
Image.open(sys.argv[1]).save(sys.argv[2])
EOF
    "$RS_PY" "$here/rearmed.py" "$work/$l.png" "$work/$l.tag.png" --variant "$variant"
    # DXT5 data after the game's own 128-byte header (1024x256, no mipmaps).
    "$RS_PY" - "$work/$l.tag.png" "$dds" "$work/$l.game.dds" <<'EOF'
import sys
import etcpak
import numpy as np
from PIL import Image
im = Image.open(sys.argv[1]).convert("RGBA")
hdr = open(sys.argv[2], "rb").read(128)
open(sys.argv[3], "wb").write(hdr + etcpak.compress_bc3(np.asarray(im).tobytes(), *im.size))
EOF
    [[ $(stat -f %z "$work/$l.game.dds") == $(stat -f %z "$dds") ]] || rs_die "$l: size changed"
    args+=("gfxassets/views/$l.dds=$work/$l.game.dds")
done

python3 "$tools/psarc_pack.py" "$src" "$out" "${args[@]}"
echo "wrote $out"
