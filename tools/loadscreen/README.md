# Title-screen logo: "RS Re-ARMed"

A red graffiti tag naming RS Re-ARMed, tilted across "REMASTERED" on the title-screen logo.
Optional: at each start the Mac launcher (`tools/play-native/launch.sh`) and the Learn & Play app
(`win-x86/app/lp_main.c`) link the game copy's `static.psarc` to `static.psarc.rearmed` when it is
there (`RS_REARMED=0` for the original logo). The install's own `static.psarc` is never changed. The two
editions have the same logo textures.

Both installs write it straight to the copy, never to Steam's folder: on the Mac `install.sh`, for Learn & Play
`lp_install.py build`. By hand:

```bash
RS_GAME_DIR="$COPY" tools/loadscreen/build.sh "$COPY/static.psarc.rearmed"   # COPY: .../RS Re-ARMed/game or .../learn-play/game
```

| File | What it does |
|---|---|
| `build.sh <out.psarc> [variant]` | Extracts `gfxassets/views/logo_elk_final.dds` and `logo_graded_elk_final.dds` (both used by `titlescreen.gfx`) from `$RS_GAME_DIR`'s `static.psarc` (or `static.psarc.orig`, or a game copy's `static.psarc.original`, if present), tags them, encodes DXT5 with etcpak after the game's original 128-byte DDS header, and repacks. Deterministic |
| `rearmed.py` | Draws the tag on a 1024x256 logo PNG. `--variant A\|B\|C` (A, the default: over "REMASTERED"; B also strikes out "2014 EDITION"; C slashes across the whole logo), `--seed N` |
| `../psarc_extract.py`, `../psarc_pack.py` | Read a psarc; rebuild one with entries replaced (other entries' blocks copied byte for byte) |

Needs a Python with Pillow, numpy and etcpak (`RS_PY=...`, or `build.sh` makes a venv in `$TMPDIR`).
Pillow's own DXT5 encoder smears thin outlines; etcpak keeps the edges clean.
