# Per-game tables

Everything the build knows about one game binary — which address a function is at, how its arguments are
passed, where a field sits in a struct — lives here, one directory per supported binary. The rest of the
tree uses names only, so the same source builds for every target
([docs/recompiler](../../docs/recompiler/README.md#per-game-tables) has the design).

```
tools/targets/
  mac-x64/           the macOS game (Steam, x86-64 Mach-O)
    target.toml      identity of the image (SHA-256), front end, ABI, image layout
    symbols.map      NAME  ADDRESS  CONVENTION  PROTOTYPE   functions and globals the build touches
    offsets.map      NAME  VALUE                            struct fields, sizes, vtable slots (NAME_slot)
    drops.map        MODULE  ADDRESS                        dead code once MODULE replaces a library wholesale
  win-x86/           Learn & Play (Steam, 32-bit PE): the same files (no drops.map); 966 of 1,156 symbols read, the rest `unsupported`
                     with the reason; target.toml also has the install pins
  names/             shared by every target
    overrides/*.list the functions each override module replaces
    hooks.list       hook points (the names mods use with rc_hook_add_name)
    seam.list        what the platform seam (app/seam_platform.c) calls and reads
    refs.list        other functions and globals native code calls or reads (also the shims/game tests)
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
- `rc_target.c`: the names for the runtime: the hook points (`rc_hook_add_name`, `rc_target`), with how each
  one's call looks on a target that isn't SysV, and `offsets.map` and `symbols.map` for a mod (`rc_off`, `rc_addr`);
- `skip/<module>.skip`: gen_image.py's skip lists, from `names/` and `drops.map`.

The override, seam and app builds use that directory (`RC_TARGET_DIR`), or make their own when run alone.
The CMake build makes one per target it needs (`cmake/RSTargetNames.cmake`): the differential tests of
`shims/game` and `shims/platform` map the Mac image, so they use mac-x64's, and their `RS_ORIG_*`, `RS_KEY_*`
and `RS_RANGE_*` defines are `RC_ADDR(name)` (`shims/game/include/rs_names.h`). `check.py` reads those
sources too.

## Hook points on a target with another ABI

The runtime shows a hook the Mac's SysV call whatever the game's convention
([docs/recompiler](../../docs/recompiler/README.md#overrides-and-hook-points)), and goes by the prototype in
`symbols.map`, so a hook point's prototype has to be right (`targets.py hook_abi`; `check.py` fails on one it
can't read). A hook point of a name is the Mac's call on every target. `hook_abi` compares the two prototypes:
the same arguments in the same order, the same kind of result, an integer's width aside. What differs is adapted
in the prototype, or hooks on that point are refused at run time, with both prototypes in the log line. The forms
only hook points need:

```
frontend_open_file  0x0101ba80  custom    void **(void **out, const char *path@ecx)     # a register argument
ui_event            0x01163d50  stdcall   void(void *menu, int event=0x98, int arg=0)   # folded away: its one value
game_frame          0x01479df0  thiscall  bool<service_manager_state=7>(void *m=hidden) # not in the Mac's call; a bool
                                                                                        # that is a field having a value
rr_speed_ratio      0x00f89360  cdecl     float<100/x>(float slider_percent)            # the Mac has 100 / this result
si_open             0x017c50a0  custom    bool<zero>(void *self@edi, int slot, ...)     # the Mac's bool is this result being 0
```

A map line `unsupported` on a point can still run: the platform layer takes it with `rc_hook_add_seam` (Learn & Play's
`input_sink` and `set_app_focus`, `win-x86/shims/win32_input.c`).

A `custom` function without an `@register` is refused too, and so is a stdcall, thiscall or fastcall one whose stack
arguments don't match its own `ret N`. A cdecl or `custom` function's `ret` says nothing of its arguments, so no check
stands behind those prototypes: 7 of the 13 points on `win-x86`.

## Adding a game function, global or field

1. Add the name to the list in `names/` for the code that uses it.
2. Add its address (or offset) to every target's map, or mark it `unsupported` there with a reason.
3. Use it by name: `OV_GUEST(path_intern, ...)`, `RC_GA(game_config)`, `RC_OFF(si_state)`.

`python3 tools/targets/check.py` (and the test) fails until every target has it, and also when native code
uses a name no list has. Comments may cite addresses of the Mac binary as evidence; code may not use them.
