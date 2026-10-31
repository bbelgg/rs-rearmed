# Per-game tables

Everything the build knows about one game binary — which address a function is at, how its arguments are
passed, where a field sits in a struct — lives here, one directory per supported binary. The rest of the
tree uses names only, so the same source builds for every target
([docs/PORT-FROM-WINDOWS.md](../../docs/PORT-FROM-WINDOWS.md) has the design).

```
tools/targets/
  mac-x64/           the macOS game (Steam, x86-64 Mach-O)
    target.toml      identity of the image (SHA-256), front end, ABI, image layout
    symbols.map      NAME  ADDRESS  CONVENTION  PROTOTYPE   functions and globals the build touches
    offsets.map      NAME  VALUE                            struct fields, sizes, vtable slots (NAME_slot)
    drops.map        MODULE  ADDRESS                        dead code once MODULE replaces a library wholesale
  names/             shared by every target
    overrides/*.list the functions each override module replaces
    hooks.list       hook points (the names mods use with rc_hook_add_name)
    seam.list        what the platform seam (app/seam_platform.c) calls and reads
    refs.list        other functions and globals native code calls or reads
    offsets.list     the struct fields, sizes and vtable slots native code uses
    relaxed.list     functions the translator emits without per-access fences (xlat/relaxed.py)
  targets.py         reads the tables; `gen` writes what one build needs
  check.py           every name resolves in every target (ctest: targets.check)
```

A map line `NAME unsupported REASON` says a target has no counterpart: an override or hook point is then
left out of that target's build, and native code that uses the name does not compile for it (code can test
`#ifndef RC_UNSUPPORTED_<name>`).

## In a build

`RC_TARGET` (default `mac-x64`) picks the target. `tools/ws7/recomp/app/build-m4.sh` runs
`targets.py gen OUTDIR/target`, which writes

- `rc_target_names.h`: `RC_ADDR_<name>`, `RC_FN_<name>`, `RC_CC_<name>`, `RC_OFF_<name>`, used through the
  macros of `tools/ws7/recomp/runtime/rc_abi.h` (`RC_ADDR(name)`, `RC_FN(name)`, `RC_GA(name)`, `RC_OFF(name)`,
  `RC_SLOT(name)`) and `overrides/rc_ov.h` (`OV_GUEST(name, ...)`, `OV_ARGS_OF(name, a, c)`, `OV_ALIAS`);
- `rc_target.c`: the hook-point names for the runtime (`rc_hook_add_name`, `rc_target`);
- `skip/<module>.skip`: gen_image.py's skip lists, from `names/` and `drops.map`.

The override, seam and app builds use that directory (`RC_TARGET_DIR`), or make their own when run alone.

## Adding a game function, global or field

1. Add the name to the list in `names/` for the code that uses it.
2. Add its address (or offset) to every target's map, or mark it `unsupported` there with a reason.
3. Use it by name: `OV_GUEST(path_intern, ...)`, `RC_GA(game_config)`, `RC_OFF(si_state)`.

`python3 tools/targets/check.py` (and the test) fails until every target has it, and also when native code
uses a name no list has. Comments may cite addresses of the Mac binary as evidence; code may not use them.
