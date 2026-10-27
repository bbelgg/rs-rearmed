# Whole-image build

`build.sh [--jobs N] [gen_image.py options]` regenerates C for every function start in `__text`
(incrementally: a TU file is rewritten only when its content changes, so ninja recompiles only
those), builds `libgame_rc.a` with ninja, links `smoke/smoke_main.c` against the runtime and the
import thunks, and checks zlib `adler32`/`crc32` against the system libz. `app/build-m4.sh` runs it
with the override skip lists.

Defaults: generated C in `analysis/ws7/recomp/gen` (large; may be a symlink to another disk), objects and
`libgame_rc.a` in `analysis/ws7/recomp/obj`. Override with `RC_GEN_DIR`, `RC_OBJ_DIR`.
Other env vars: `RC_CFLAGS` (default `-w -DRC_ABI_TRUST`), `RC_OPT` (`-O2`), `RC_TSO`, `RC_IMAGE`, `RC_PY`,
`RC_SMOKE` (`0` skips the smoke test).

- `optflags.sh` -- the optimisation switches every build script sources: `RC_LTO=thin` (TUs, runtime,
  thunks and overrides as bitcode; ThinLTO at the app link with `-Wl,-cache_path_lto`, `RC_LTO_JOBS`
  threads, at `RC_OPT`), `RC_PGO=gen` (`-fprofile-instr-generate`; the app names the profile
  `RC_PGO_DIR/rs-%p%c.profraw`: continuous mode, because the game ends with `_exit`, which skips the
  profile writer), `RC_PGO=use:FILE` (`-fprofile-instr-use`; the profile is an implicit ninja input).
  `--o1` TUs are never bitcode, so the link-time optimiser cannot take them past -O1. With `RC_LTO`
  the smoke test is skipped unless `RC_SMOKE=1` (it would be a second whole-image LTO link).
- `pgo-merge.sh OUT.profdata DIR|FILES` -- `llvm-profdata merge` of a gen build's `.profraw` files.

- `gen_image.py` -- parallel translation, TU chunking by instruction count (`--chunk-insns`),
  stubs for rejected functions, `decls.h`, `table.c`, `manifest.json`, `build.ninja`.
  `--skip FILE` (repeatable): `ADDR` = override provides `F_ADDR` (stays in `rc_table`);
  `ADDR drop` = not generated, not in the table. `--o1 FILE`: functions compiled alone at -O1.
  `--tso-ranges FILE`: TUs in those ranges get `-DRC_MO_TSO` (`RC_TSO`; default: the whole image).
  Two kinds of access stay unfenced inside them: RIP-relative loads from sections nothing writes after
  the image is loaded (`RC_LDC`), and the functions listed in `xlat/relaxed.py`, which fence only at
  entry and exit (`RC_RELAXED=0` turns the list off when the C is generated; runtime/rc_int.h).
- Undefined symbols at smoke link time are listed in `obj/undefined.txt` and stubbed with aborting
  functions so the test can still run.
