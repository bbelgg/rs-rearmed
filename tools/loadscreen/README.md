# Title-screen logo: "RS Re-ARMed"

A red graffiti tag naming RS Re-ARMed, tilted across "REMASTERED" on the title-screen logo.
Optional: the launcher (`tools/play-native/launch.sh`) stages `static.psarc.rearmed` as
`static.psarc` when it is in the game folder (`RS_REARMED=0` for the original logo). The install's
own `static.psarc` is never changed.

```bash
tools/loadscreen/build.sh "$GAME_DIR/static.psarc.rearmed"   # GAME_DIR: the Rocksmith2014 folder
```

| File | What it does |
|---|---|
| `build.sh <out.psarc> [variant]` | Extracts `gfxassets/views/logo_elk_final.dds` and `logo_graded_elk_final.dds` (both used by `titlescreen.gfx`) from the install's `static.psarc` (or `static.psarc.orig` if present), tags them, encodes DXT5 with etcpak after the game's original 128-byte DDS header, and repacks. Deterministic |
| `rearmed.py` | Draws the tag on a 1024x256 logo PNG. `--variant A\|B\|C` (A, the default: over "REMASTERED"; B also strikes out "2014 EDITION"; C slashes across the whole logo), `--seed N` |
| `../psarc_extract.py`, `../psarc_pack.py` | Read a psarc; rebuild one with entries replaced (other entries' blocks copied byte for byte) |

Needs a Python with Pillow, numpy and etcpak (`RS_PY=...`, or `build.sh` makes a venv in `$TMPDIR`).
Pillow's own DXT5 encoder smears thin outlines; etcpak keeps the edges clean.
