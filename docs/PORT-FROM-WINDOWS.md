# Plan: one installer for the Mac and the Windows ("Learn & Play") game

*Design note. It records the goal, the decisions that shape the work and the order to do it in. P0 (the
Mac build made name-keyed) is done, see [P0 status](#p0-status); nothing of the Windows target is built
yet.*

## Goal

1. **One installer.** `RS Re-ARMed Installer.app` takes either game a user owns: the macOS game (the
   x86-64 build that runs under Rosetta) or the Windows game (the free **Learn & Play** edition, or any
   Windows copy). The user picks a game folder; the installer recognises which one it is.
2. **One result.** Either way the user gets the same native arm64 `~/Applications/RS Re-ARMed.app`: the
   same launcher, the same settings and saves folder, the same renderer choice, the same built-in fixes
   and the same `mods/` folder with the same hook points, so a mod built once runs on both.
3. **One code base.** The two builds stay as close as possible, and a change is written once. Only data
   that is pinned to one binary (addresses, struct offsets, calling conventions) is kept per game.
4. **No extra tools.** Building from the Windows game needs nothing beyond what building from the Mac game
   needs today (the command line tools, CMake, Ninja, Python packages the installer installs itself). No
   Windows, no Wine, no virtual machine, no Windows DLLs, no emulator framework the user installs.

## Why the Windows game is needed

The free Learn & Play package grants only the Windows depot (221681) of Rocksmith 2014 (app 221680), not
the macOS depot (221682). Those owners cannot get the Mac binary the current pipeline recompiles, and the
project ships no game code. What they can download is the **32-bit x86 Windows** game: MSVC ABI, PE,
Direct3D 9, packed with VMProtect (the real `.text` / `.rdata` / `.data` exist only in an encrypted
section on disk, and both the entry point and the TLS callback are virtualised).

## The two games side by side

| | Mac game (today) | Windows game (goal) |
|---|---|---|
| Binary | x86-64 Mach-O, SysV ABI | x86-32 PE, MSVC ABI (`cdecl`, `stdcall`, `thiscall`, `fastcall`) |
| Protection | Steam DRM: decrypts itself at start | VMProtect: unpacks itself at start |
| Getting a clean image | `tools/steamless`: launch the game once, dump it | **offline unpack** in the installer: the game never runs |
| Renderer | OpenGL 2.1 + Cg | Direct3D 9 (+ Cg if the PC build uses it: to confirm) |
| Audio sink | Wwise CoreAudio sink | Wwise Windows sink (XAudio2 / WASAPI: to confirm) |
| Shell | Cocoa (already replaced by `shims/platform`) | `WinMain` + Win32 message loop |
| Game data | Mac `*.psarc` | PC `*.psarc` (`_p` CDLC) |

Gameplay (note detection, modes, lessons, Session Mode, menus) is the same C++ source compiled twice, so
once translated it behaves the same. What the Windows build cannot share with the Mac one is the PC game's
own assets: it will look like the PC game, drawn through the same Metal renderer.

## The design: shared sources, per-game data

Today every address-keyed file is pinned to the Mac binary: the override skip lists, `hooks.skip`, the
thunk prototypes, the vtables, the platform seam (`app/seam_platform.c`), and offsets into game structs
inside the override C. The core decision is to make all of them **name-keyed**, with one small data
directory per supported binary.

### Targets

```
tools/targets/
  mac-x64/            the macOS game (today's tables, moved here)
    target.toml       image identity (SHA-256 of the decrypted image), front end, image base
    symbols.map       NAME  ADDRESS  CONVENTION  PROTOTYPE   one line per game function we touch
    offsets.map       NAME  OFFSET                           struct fields the native code reads
  win-x86/            the Windows game, same files, same names
  names/              the shared lists: which names are overridden, hooked or called, and by what
    overrides/*.list  replaces overrides/*.skip (names instead of addresses)
    hooks.list        replaces hooks.skip
    seam.list         the functions app/seam_platform.c calls
```

- A build has exactly one target (`RC_TARGET=mac-x64` or `win-x86`; the installer sets it from the game
  folder). The build resolves every name through that target's `symbols.map`, so `gen_image.py`, the
  override modules and the seam never see a raw address again.
- `tools/targets/check.py` (and a test) fails when a name in `names/` has no entry in a target, unless
  that target marks it `unsupported` with a reason. That turns "did I remember the other game?" into a
  build error.
- The Mac move comes first and changes no behaviour: the generated C for `mac-x64` must be byte for byte
  what it is today.

### One ABI layer for native code

Override and seam code reads arguments and returns values through `rc_ov.h` (`OV_ARGS`, `OV_U32`,
`OV_PTR`, `ov_ret_*`), which today hard-code SysV and 8-byte return addresses. They become
convention-aware, driven by the prototype in `symbols.map`:

- **Arguments**: `rc_arg_i` / `rc_arg_f` read SysV registers on `mac-x64`, and the stack (plus `ecx` for
  `thiscall`, `ecx`/`edx` for `fastcall`) on `win-x86`.
- **Returns**: `ov_ret_*` pop the return address, and on `win-x86` also the callee-cleaned arguments of a
  `stdcall` / `thiscall` function; float results go to `xmm0` or to x87 `st0`.
- **Pointers**: `OV_PTR` / `RC_HOST(guest)` turn a guest pointer into a host one, and `RC_GUEST(host)` the
  other way. On `mac-x64` both are what they are today (the image slide); on `win-x86` they add or subtract the
  base of the 32-bit guest window (see [Memory](#memory-a-4-gb-guest-window)).
- **Struct fields**: native code reads game structs with `RC_OFF(name)` from `offsets.map` instead of
  literal offsets. Layouts differ between the two games because pointers are 4 bytes on Windows.
- **Calls into the game**: `OV_GUEST(name, ...)` and `OV_VCALL` take a name and marshal by the target's
  convention.

The rule for every new native line: no literal address, no literal offset, no direct register access.
Written that way, an override, a seam function or a fix is the same source for both games.

### Mods stay portable

Mods use `rc_hook_add(vmaddr, pre, post, user)` and read arguments with `rc_args`. With the layer above:

- `rc_hook_add_name("game_frame", ...)` is added, and becomes the documented call. Hook point names are
  the ones in `hooks.list`, which already exist as comments in `hooks.skip`.
- `rc_hook_add(vmaddr)` keeps working: on `win-x86` the runtime maps a Mac hook address to the hook point
  of the same name, so a mod built against 1.x with Mac addresses still finds its hook.
- A mod that only uses hook points, `rc_args` / `ov_ret_*` and `RC_OFF` is one `.dylib` for both games.
  A mod that hard-codes other Mac addresses or offsets stays Mac-only; `rc_target()` lets it check.
- The overlay (`rs_overlay_*`) and the rest of the exported runtime are unchanged.

## Getting the Windows game onto a Mac

Steam for Mac does not offer to install a Windows-only game, but its console downloads any depot the
account owns:

1. The installer opens `steam://open/console` and puts
   `download_depot 221680 221681 <manifest>` on the clipboard; the user pastes it and presses Return.
   The manifest ID is pinned to the one build the `win-x86` tables were made for.
2. Steam downloads into its own content folder (`…/steamapps/content/app_221680/depot_221681`); the
   installer looks there by default. A folder copied from a Windows PC works as well.
3. The installer checks the SHA-256 of `Rocksmith2014.exe` against `win-x86/target.toml` and says
   plainly when the copy is a different build.

Pinning one build is what makes the committed tables valid for every user, exactly as on the Mac side.

## Unpacking, with no extra tools

The Windows counterpart of the steamless dump, run by the installer, offline:

- **What runs it**: a Python script in the build's existing virtual environment, using
  [Unicorn](https://www.unicorn-engine.org/) (one more line in `requirements.txt`; it ships prebuilt
  arm64 macOS wheels, as Capstone does). No Qiling, no Keystone, no Homebrew packages.
- **What it emulates**: just enough Windows for VMProtect's bootstrap: the PE loader (sections, TLS
  callback, relocations at the preferred base `0x400000`), flat 32-bit segments with `FS` on a TEB, a PEB
  with its loader list, a heap, the handful of APIs the bootstrap calls (file mapping of its own exe for
  the self-checksum, `LoadLibrary` / `GetProcAddress`, `NtSetInformationThread`, memory queries), and a
  structured-exception dispatcher (VMProtect deliberately raises an `int3` and continues from its SEH
  handler).
- **No Windows DLLs**: VMProtect reads DLL export tables but never needs their code, because the unpack
  stops at the original entry point. The script synthesises export-only stub DLLs from committed name and
  ordinal lists (`win-x86/exports/*.txt`, API names only, nothing from Microsoft). Each stub address maps
  back to its name, which also makes rebuilding the import table trivial.
- **Deterministic**: clock, tick count, `rdtsc` and `cpuid` return fixed values, so every user gets the
  same image, and the script checks its SHA-256 against `target.toml`.
- **Output**: the unpacked image (sections as at the entry point, imports rebuilt, entry point set),
  cached in the build folder by the exe's hash; a reinstall skips the unpack.
- **Time**: the bootstrap checksums the whole 11.6 MB exe. Run without per-instruction Python hooks
  (only API stubs and exceptions trap), this should take seconds to a few minutes; the installer shows it
  as its own step, the counterpart of "launch Rocksmith once".

The research harness that got VMProtect through its exception wall used Qiling and real DLLs. That
proved the method; the shipped script replaces both with the pieces above.

## Front end: 32-bit x86, MSVC, PE

The recompiler keeps its back end (one `F_<addr>(rc_ctx *c)` per function, flags as C locals, jump
tables, the runtime). The new front end is `xlat/` with a mode switch, not a fork: decoding is Capstone
in 32-bit mode, and `emit_int` / `emit_sse` / `jumptab` gain operand-width and addressing options.

### Memory: a 4 GB guest window

arm64 macOS processes keep the low 4 GB unmapped, so 32-bit guest addresses cannot be host addresses.
The runtime reserves one 4 GB region at start and every guest access is `rc_mem + (uint32_t)addr`; the
image goes at `rc_mem + 0x400000`, and every guest heap, stack, TEB and object the guest may hold a
pointer to (Steam interfaces, D3D objects, COM vtables) is allocated inside the window. Native pointers
crossing into native libraries go through `RC_H` / `RC_G`. Address wrap-around comes for free from the
32-bit cast.

### What 32-bit MSVC code adds

- **x87**: every `cdecl` float or double result comes back in `st0`, and code of this age uses x87
  arithmetic in places. The emitter needs an x87 stack model in `rc_ctx` (80-bit values computed as
  `double`, which is what MSVC's default precision control gives anyway).
- **Calling conventions**: callee-cleaned stacks (`ret N`) and `thiscall` in `ecx`; recorded per function
  in `symbols.map` and inferred for the rest from the `ret` form.
- **SEH and C++ exceptions**: frame-based handlers chained from `FS:[0]`, `_CxxThrowException` →
  `RaiseException`. The runtime gets a per-thread TEB and a dispatcher that walks the chain and calls the
  (translated) CRT frame handler; unwinding to a catch reuses the `rc_setjmp` machinery the Mac build uses
  for `longjmp`.
- **Imports**: the rebuilt import table resolves to thunks generated by `thunks/` as today, from
  per-target prototypes, with `stdcall` / `cdecl` marshalling.
- **Code VMProtect left virtualised**: some functions beyond the bootstrap may still run as VMProtect
  bytecode (typically licence or integrity checks). They are found by their jumps into the VMProtect
  sections and replaced by native overrides (normally "check passed"). Runtime self-checksums of the code
  pass unchanged, because the guest window holds the original unpacked bytes.

### Testing the front end

Unicorn, already in the environment for the unpack, is the oracle: a differential test runs a translated
function and the same bytes under Unicorn from the same state and compares registers and memory. This
covers x87, flags and conventions long before the game boots.

## Platform seams

Everything native that sits behind the game is shared; only the thin layer that connects it to each game
differs, and that layer is written against the names above.

| Piece | Shared | Per game |
|---|---|---|
| Shell | `shims/platform` (window, input queue, paths, `Rocksmith.ini`, launcher) | `seam_platform.c` calls by name; the Windows entry skips `WinMain`'s message loop the way the Mac one skips Cocoa's |
| OS | — | `shims/win32`: file, threads, sync, TLS, time, heap, registry (an in-memory `Rocksmith.ini`-backed store), all on POSIX |
| Steam | `shims/steam` (offline) | a 32-bit `steam_api.dll` face: the same flat calls, interface vtables as guest-callable thunks in the window |
| Uplay | — | an offline stub that reports "owned, offline" (if the Learn & Play build still loads it: to confirm) |
| Network | `netguard` | — |
| Audio | `overrides/src/audio_*` (CoreAudio out, guitar input, ring, latency) | a sink and an input adapter for the Windows Wwise sink and `SoundInput`, in place of the Mac ones |
| Video | `shims/bink` (FFmpeg) | `binkw32` names (`_BinkOpen@8`, ...) mapped onto the same shim |
| Libraries | `overrides/src/*_glue.c` (zlib, png, jpeg, crypto, Lua, EFD, DSP) | addresses and pointer marshalling only, from the ABI layer |
| Built-in fixes | `overrides/mods/mods.c` (RSBypass, rsmacfixes) | names and offsets; a fix that has no Windows counterpart is marked `unsupported` |

## Renderer

Decision: **Direct3D 9 on top of `libRSOpenGL`**, not a second Metal backend. A new `shims/d3d9`
implements the `IDirect3DDevice9` subset the game uses by calling the same `gl*` entry points the Mac game
calls, so the Metal core, its caches, the shader corpus, the install-time warm-up, the overlay and the
OpenGL fallback are all reused unchanged and fixes there reach both games.

- **States and draws**: vertex declarations, render and sampler states, `DrawPrimitive` /
  `DrawIndexedPrimitive`, `Lock` / `Unlock`, render targets; with the usual D3D9 → GL care (half-pixel
  offset, depth range, row-major constants, `BGRA` vertex colours).
- **Shaders**: D3D9 shader model 2/3 bytecode is translated to GLSL 1.20, the dialect the GLSL → MSL path
  and the corpus already take. The corpus step gains a source: bytecode from the Windows image (Scaleform's
  D3D9 HAL) and from the PC `static.psarc`. If the PC build uses the Cg runtime (`cgD3D9`), its programs
  go through the existing Cg shim instead; which one it is decides how much of this is needed.
- A direct D3D9 → Metal layer stays an option only if profiling shows the extra hop costs frames.

## Producing the Windows tables (maintainers only)

Users never run this; they get committed `win-x86` files. Maintainers fill `symbols.map` and
`offsets.map` for every name in `names/` by matching the Windows image against the Mac one: shared strings
and their references, RTTI and vtable layouts, call-graph shape, constants. `tools/targets/match.py`
proposes addresses with a confidence and the evidence; a person confirms each before it is committed. The
categories and names are already known from the Mac side, so this is matching, not discovery. The same
tool re-runs when a new Steam build appears.

## The installer flow

```
choose game folder ─┬─ Rocksmith2014.app (macOS) → RC_TARGET=mac-x64 → steamless dump (launch once)  ─┐
                    └─ Rocksmith2014.exe (Windows) → RC_TARGET=win-x86 → offline unpack (no launch)    ─┤
                                                                                                         ↓
              build-m4.sh: overrides, recompile, compile, link, shader corpus → install → warm shaders → launcher
```

One branch, at "provide the game image". The only Windows-only screen is the depot download help. Both
targets use the same build folder layout, the same logs and the same update path.

## Keeping the two in step

- Runtime, shell, shims, renderer, audio core, built-in fixes, installer and launcher: one source,
  automatically both.
- A change that touches a new game function or field: add the name to `names/`, its address or offset to
  both targets (`match.py` helps), and `check.py` refuses the build until both have it or one says
  `unsupported`.
- Tests run per target where they need a game image (`app/test`, the seam test, the Metal tests), and the
  differential front-end tests run in CI without any game.
- One version number and one set of release notes; a feature that is Mac-only for a while says so.

## Phases and gates

| Phase | Work | Done when |
|---|---|---|
| **P0** Mac refactor | `tools/targets/`, name-keyed lists, the convention-aware ABI layer, `rc_hook_add_name`, `check.py` | the `mac-x64` build is byte-identical and all tests pass. Useful on its own; no Windows work yet. **Done**, see below |
| **P1** Spikes (in parallel) | (a) unpack to the entry point with Unicorn and synthetic DLLs only; (b) inventory of the unpacked image: imports (CRT, `d3d9`, `d3dx9`, Cg, XInput, Uplay), leftover virtualised functions; (c) shader source of the PC build, and one PC shader through bytecode → GLSL → MSL | each answer written here. **Gate**: if (a) needs real DLLs or (b) shows much virtualised game code, stop and rethink |
| **P2** Front end | 32-bit decode, window memory model, x87, conventions, SEH, PE thunks; differential tests | the whole image translates and links against the runtime with stub seams |
| **P3** Boot to menu | `shims/win32`, Steam/Uplay faces, the core `win-x86` tables, `shims/d3d9` far enough for Scaleform | the Windows build reaches the main menu on Metal. **Gate**: effort and fidelity review |
| **P4** Parity | every name in `names/`, audio adapters (latency and Direct Connect), Bink, songs, DLC and CDLC, modes, built-in fixes, the hook points | the Mac test checklist passes on `win-x86`; a sample mod runs unchanged on both |
| **P5** Installer | game detection, depot help, the unpack step, `RC_TARGET`, docs | one-click install from a fresh Learn & Play download |

### P0 status

Done on the `win-port` branch. What it is, as built:

- **Tables.** `tools/targets/mac-x64/` holds `target.toml` (image SHA-256, front end `x86-64-macho`, ABI
  `sysv64`, `__text` bounds), `symbols.map` (1,078 names: 739 overridden functions, 11 hook points, the
  seam's 16 functions and 5 globals, 259 efd class-info tables, and what the override and app code calls or
  reads), `offsets.map` (70 fields, sizes and vtable slots) and `drops.map` (742 dead functions of the
  libraries replaced wholesale: per target and unnamed, since another binary has its own dead set).
  `tools/targets/names/` has `overrides/<module>.list` (replacing `overrides/*.skip` and `mods/mods.skip`),
  `hooks.list` (replacing `hooks.skip`), `seam.list`, `refs.list`, `offsets.list` and `relaxed.list` (the
  translator's fence-free functions, formerly addresses in `xlat/relaxed.py`). Map conventions so far:
  `sysv`, `data`, `code` (an address inside a function, such as a return site); `cdecl`, `stdcall`,
  `thiscall`, `fastcall` are accepted for win-x86. `NAME unsupported REASON` marks a missing counterpart.
- **Build.** `RC_TARGET` (default `mac-x64`) is read by `build-m4.sh`, which runs
  `tools/targets/targets.py gen OUTDIR/target`: `rc_target_names.h` (`RC_ADDR_` / `RC_FN_` / `RC_CC_` /
  `RC_OFF_` defines), `rc_target.c` (hook names for the runtime) and the skip files. `overrides/build.sh`,
  `mods/build.sh`, `app/build.sh` and the seam test use it through `RC_TARGET_DIR` or make their own.
  `gen_image.py --target` (default `$RC_TARGET`) refuses a target whose front end it lacks and warns when
  the image's SHA-256 is not the target's.
- **ABI layer.** `runtime/rc_abi.h`: `RC_ADDR`, `RC_FN`, `RC_GA`, `RC_OFF`, `RC_SLOT`, `RC_CC`,
  `RC_HOST` / `RC_GUEST`, `RC_RET_ADDR`, `RC_FRAME_PTR`, with the mac-x64 (`RC_TARGET_ABI_SYSV64`)
  implementation and an `#error` for any other ABI. `overrides/rc_ov.h`: `OV_GUEST(name, ...)`,
  `OV_GUESTV(name, ints, n)`, `OV_ALIAS(name, fn)`, `OV_ARGS_OF(name, a, c)` (reads arguments by
  `RC_CC_<name>`). Every adapter, the seam and `app/rc_main.c` now use names only; the names are pasted,
  never macro-expanded first. The `RC_H` / `RC_G` of the design became `RC_HOST` / `RC_GUEST` (`RC_H` is
  `rc.h`'s include guard). Decided for win-x86: callee-cleaned argument bytes of a `stdcall` / `thiscall`
  override are popped by a per-name wrapper that `targets.py gen` will emit from `symbols.map`, so adapters
  and `ov_ret_*` stay convention-free; `OV_ARGS_OF` covers `thiscall` / `fastcall` register arguments.
- **Mods.** `rc_hook_add_name(name, pre, post, user)` and `rc_target()` are exported next to `rc_hook_add`,
  documented in `rc_hook.h` and the README. `rc_hook_add` also accepts the mac-x64 address of a hook point
  on any target (the generated table carries both addresses).
- **Check.** `tools/targets/check.py` (and `test_targets.py`, ctest `targets.check`): every name in `names/`
  resolves in every target or is `unsupported`; overridden and hooked names are functions; no stale map
  entries; and every name the native sources use (`RC_*`, `OV_*`, the seam's `G`, `rc_hook_add_name`) is
  listed.
- **Verification.** Against a build of `main` from the same image: the generated C (`gen/`, `thunks.c`)
  and `skip.txt` are byte-identical; every override object is byte-identical except `mods.o` and the
  `crypto_mods` copy of `crypto.o`, whose only difference is the renamed DSA-verify symbol
  (`rs_crypto_dsa_verify`); the adapter tables in `gen/*_adapters.h` preprocess to the same tokens (one
  offset now written `(-0x3f0)` instead of `-1008`); the full build passes its smoke and `--init-only`
  checks; the app reaches the main menu on Metal with the test profile and input script, nothing blocked by
  the network guard. `check.py` and `test_targets.py` pass (also as ctest `targets.check`).

Later work found on the way (not P0):

- `shims/game/include/*.h` still carry about 225 `RS_ORIG_*` Mac addresses (used by the shims' difftests,
  and `RS_GAME_INTERFACE_SLOT_EVENT` by `online_glue.c`); they move to names when the shims gain a win-x86
  side.
- Pointer-width layouts in `mods.c` (the JSON number table's `T[]` words and nodes) and `audio_in.c` (the
  queue's 8-byte buffer pointers) are written for 8-byte pointers; P4 names or marks them per target.
- Helpers shared by many adapters (`qi` in `gen/efd_qi.c`, `key_event` in `mods.c`) read arguments with
  plain `OV_ARGS`; on win-x86 they need `thiscall` (an `OV_ARGS_CC` variant).
- Hooks: `rc_args` in a mod's hook reads SysV registers; a win-x86 hook needs the point's convention
  (`rc_hook_call` can pass it).
- The thunks (`thunks/`) are keyed by import symbol names and the vtables tool (`vtables/macho.py`) is the
  Mach-O reader: neither holds game addresses; the win-x86 PE imports are P2 front-end work.
- `app/test/build.sh` (the seam unit test) no longer links, on `main` too: `rc_native.c` needs `rc_fail`,
  `rc_lookup`, `rc_tramp_guest_of` and `rc_unwind_sync`, which the test does not build.

## Risks, largest first

1. **Renderer fidelity**: PC shaders and D3D9 semantics through GL to Metal. Spike P1c answers the shader
   half early.
2. **Leftover virtualisation**: game functions still run as VMProtect bytecode after the unpack. P1b
   counts them.
3. **The unpack without real DLLs**: VMProtect may read more of a DLL than its export table. P1a answers
   it; a fallback is to synthesise more of each stub (headers, entry bytes), still from public data.
4. **Volume**: matching every name for a second binary. Bounded by the names the Mac build uses today
   (about 1,500 table lines, most of them library functions that match by their code).
5. **Steam updating the Windows build**: a pinned manifest keeps installs working; a new build means a
   `match.py` run and a new `target.toml`.

## Decisions

- Two targets, one source tree; per-game data only in `tools/targets/<id>/`; every native reference by
  name.
- The Mac build is refactored first and must stay byte-identical.
- Mods: name-based hooks and convention-aware argument access; old Mac-address hooks keep working.
- 32-bit guest in a reserved 4 GB window.
- Offline unpack with Unicorn from the build's own Python environment and synthetic export-only DLLs; no
  Qiling, no Windows files.
- One pinned Windows build, downloaded with Steam's own console.
- Renderer: Direct3D 9 implemented on `libRSOpenGL`, so the Metal backend and the shader corpus are shared.
