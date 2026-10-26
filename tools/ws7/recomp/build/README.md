# Whole-image build

`build.sh [--jobs N] [gen_image.py options]` regenerates C for every function start in `__text`
(incrementally: a TU file is rewritten only when its content changes, so ninja recompiles only
those), builds `libgame_rc.a` with ninja, links `smoke/smoke_main.c` against the runtime and the
import thunks, and checks zlib `adler32`/`crc32` against the system libz. `app/build-m4.sh` runs it
with the override skip lists.

Defaults: generated C in `analysis/ws7/recomp/gen` (large; may be a symlink to another disk), objects and
`libgame_rc.a` in `analysis/ws7/recomp/obj`. Override with `RC_GEN_DIR`, `RC_OBJ_DIR`.
Other env vars: `RC_CFLAGS` (default `-w -DRC_ABI_TRUST`), `RC_OPT` (`-O2`), `RC_TSO`, `RC_IMAGE`, `RC_PY`.

- `gen_image.py` -- parallel translation, TU chunking by instruction count (`--chunk-insns`),
  stubs for rejected functions, `decls.h`, `table.c`, `manifest.json`, `build.ninja`.
  `--skip FILE` (repeatable): `ADDR` = override provides `F_ADDR` (stays in `rc_table`);
  `ADDR drop` = not generated, not in the table. `--o1 FILE`: functions compiled alone at -O1.
  `--tso-ranges FILE`: TUs in those ranges get `-DRC_MO_TSO` (`RC_TSO`; default: the whole image).
- Undefined symbols at smoke link time are listed in `obj/undefined.txt` and stubbed with aborting
  functions so the test can still run.
