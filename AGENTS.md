# AGENTS.md

Guidance for coding agents (and people) working in this repository. The player-facing overview is
[README.md](README.md); the contribution rules are [CONTRIBUTING.md](CONTRIBUTING.md).

## Two editions, one project

RS Re-ARMed builds a native arm64 Mac app from either of two games:

| | Mac | Windows (Learn & Play) |
|---|---|---|
| Game | Steam macOS Rocksmith 2014 Remastered, x86-64 | Learn & Play, 32-bit Windows (Windows-only on Steam) |
| Status | released (see `VERSION`) | released (see `VERSION`) |
| Code | `shims/`, `tools/ws7`, `tools/play-native`, root `CMakeLists.txt` | `win-x86/` (on top of the shared pieces) |
| Docs | [docs/mac](docs/mac/README.md) | [docs/windows](docs/windows/README.md) |
| Installs as | `RS Re-ARMed.app` | `RS Re-ARMed Learn & Play.app` |

Shared by both: the recompiler (`tools/ws7/recomp`, 64-bit and 32-bit PE front ends,
[docs/recompiler](docs/recompiler/README.md)), the renderer (`shims/opengl`, libRSOpenGL with its Metal
backend; Learn & Play's D3D9 layer draws through it), the shader corpus tools (`tools/metal`), CoreAudio
(`shims/coreaudio`), the offline Steam behaviour (`shims/steam/src`, which Learn & Play's Steam face compiles),
the mod loader and its ABI (`tools/ws7/recomp/runtime/rc_mods.c`, `rc_mod.h`), the built-in fixes both games do
the same way (`tools/ws7/recomp/overrides/mods/mods_shared.c`, `framerate.c`), the installer app
(`tools/installer`), the game-file copy (`tools/play-native/gamefiles.py`) and the per-game tables
(`tools/targets`).

The network guard is the same rule in two forms: the Mac edition loads `tools/ws8/netguard` into the game on
every launch; Learn & Play has no host socket to guard (no `socket`, `connect` or name lookup is linked into
it), and its Win32 seam refuses the same things at the game's own calls (`win-x86/shims/win32_net.c`). A mod is
native code and could make a socket, so Learn & Play loads mods only with the same guard in the process
(`win-x86/app/lp_main.c`).

**The aim is to keep the two editions as consistent as possible.** A player should get the same
features, switches, folders, messages and behaviour from either app, and a developer should find the same
structure in either half of the code and docs. So every change has to consider the other edition.

### For every change

1. **Ask whether it applies to the other edition.** A fix, feature, switch, mod, installer step,
   diagnostic or doc change made for one edition almost always has a counterpart in the other.
2. **Prefer one shared implementation.** If both editions need it, put it in shared code (or make the
   existing Mac piece usable from `win-x86/`) rather than writing it twice. The Windows port reuses the
   Mac port's working pieces wherever it can; carry names, fixes, hooks and mods across by matching
   functions, not by reimplementing them.
3. **Do both in the same piece of work when you can.** If the other edition can't get it now (different
   game code, not yet reachable, needs a run on the other game), say so in the commit message and record
   it as open work in that edition's `PLAN.md`.
4. **Changing shared code changes both editions.** Build and run the tests of both
   (see [Testing](#testing)), and keep the Mac edition's released behaviour unchanged unless the change
   is meant for it too.
5. **Keep the names in step.** Environment variables, folder layout under
   `~/Library/Application Support/RS Re-ARMed/`, log names, installer wording and key bindings should
   match between the editions. Where one has to differ (e.g. `LP_` build variables), keep the shape
   parallel and document both.
6. **Update the comparison.** If a change alters what one edition supports, update the
   "Mac and Windows compared" table in [README.md](README.md) and the `Status` section of both
   `PLAN.md` files.

### Docs mirror each other

`docs/mac` and `docs/windows` have the same set of files (README, INSTALL, CAPTURE, DESIGN, PERFORMANCE,
PLAN, TESTING; Mac also has METAL) with **the same headings where the topics match**, so they can be read side
by side. When you add or change a section in one, add or adjust the matching section in the other, even
if it only says "not yet" or "not applicable". Recompiler material goes in `docs/recompiler`, not in
either edition's docs. What a release needs (the notes' entries, the known issues, the steps) goes in
`docs/release`, once for both.

## Rules that hold for both

- **No game content in the repository.** No game files, extracted or decrypted binaries, generated C,
  build output, or logs and dumps with game content. Anything from the game, or from Microsoft's DLLs, is
  read from the user's install at build time ([CONTRIBUTING.md](CONTRIBUTING.md)).
- **Offline.** The game must never contact Ubisoft, Steam or anything else. Keep the network guard (and
  Learn & Play's `win32_net.c`), the offline Steam layer and the online stub in force; never run with switches that weaken them
  (`RS_NETGUARD=log`, `RS_STEAM_KEEP_CREDENTIALS`) unless the maintainer asks, and never print or
  decode stored credentials (`crd`).
- **Users' saves are not written.** Steam profiles are copied in once; the app uses only its own copy.
- **Public repository.** No personal names, emails, home paths, host names, machine-specific volumes or
  session scratch paths in files or commit messages. Commit as the repository's configured identity,
  with `TZ=UTC`.
- **Ask before any push, tag, release or other remote change.**

## Projects that build on this one

Three projects of their own run inside the game as mods. Players have them installed, each is released on its own,
and no test of this repository builds or runs them: a change here can break one with nothing here failing.

| Project | What it is | What it takes from here |
|---|---|---|
| [rearmed-mods](https://github.com/glebb/rearmed-mods) | RSMods' features: `rearmed-mods.dylib`, one file for both editions, with its settings app, the ToneHost add-on and the Song Stems helper | its own copy of the mod headers (its `include/rs-rearmed`); the runtime the executables export; by name, the hook points, offsets, symbols, edits and mid points of `tools/targets`; the overlay and render APIs |
| [RS Companion](https://github.com/glebb/rs-mac-companion) | `librscompanion.dylib`, which follows the song and its tone changes for its app | no header of ours: it declares `rc_mod_abi`, `rc_target`, `rc_slide`, `rc_hook_add_name` and the first 16 registers of `rc_ctx` itself, hooks `frontend_open_file` and `ak_post_event_name`, and reads the game's memory from `rc_slide` (the Mac image at the original game's addresses, Learn & Play's 4 GB guest window) |
| rearmed-songbridge | `songbridge.dylib`: each edition reads the other platform's song archives | builds against a checkout of this repository (its `RS_REARMED`): `rc_mod.h`, `rc_file.h`; its tests compile `runtime/rc_file.c` and read archives with `tools/psarc_extract.py` |

### What they hold us to

- **The mod interface.** The headers a mod includes (`rc.h`, `rc_rt.h`, `rc_call.h`, `rc_hook.h`, `rc_abi.h`, `rc_cc.h`,
  `rc_mod.h`, `rc_mid.h`, `rc_file.h` in `tools/ws7/recomp/runtime`, `tools/ws7/recomp/overrides/rc_ov.h`,
  `shims/include/rs_overlay.h`, `rs_render.h`) and everything the executables export to mods. Adding is safe.
  Removing or renaming something, changing a prototype or a struct's layout (`rc_ctx`, the mid point frame,
  `rc_file_info`), or which libraries the loader takes (`rc_mod_abi`) is a break. The mods players have are binaries
  built with an earlier copy of the headers: what those call has to keep working, not only what compiles today.
- **Names** (`tools/targets/names` and each target's maps). A hook point, offset, symbol, edit or mid point keeps its
  name, its meaning and, for a hook point, its arguments, in both targets. The groups marked as a mod's ("the mods
  library", "a mod's: rearmed-mods") are used by a released library, and so may every hook point be.
- **What a mod sees happen.** A hook sees the Mac's call on either target, the game's files go through `rc_file_*`,
  mods load from `mods/` at startup. A change of route breaks a mod with every header and name intact: when 1.15
  bound the Mac game's file imports to the runtime, RS Companion before 1.9.0 stopped seeing songs.
- **The install.** What their installers and libraries use under `~/Library/Application Support/RS Re-ARMed/`
  (`mods/`, `learn-play/mods/`, `game/`, `game-dir`, `libnetguard.dylib`), the process names `Rocksmith2014` and
  `rs2014_lp`, and `RS_NATIVE_HOME`, `RS_MODS_PATH`, `RS_LOAD_MODS`.

### When a change touches any of that

1. **Prefer not to break.** Keep the old name, call or file working beside the new one.
2. **Check each project that has a checkout** (usually beside this one: `../rearmed-mods`, `../rearmed-songbridge`;
   RS Companion's is `rs-mac-companion`). With `RS` set to this checkout:
   - rearmed-mods, from its folder, its sources against this tree's headers (its own build uses its copies, so it
     passes whatever changed here), and `grep -rn NAME src` for a name that changes:

     ```sh
     for c in src/*.c; do clang -arch arm64 -fsyntax-only -Wall -Wno-unused-function -Isrc \
       -I$RS/tools/ws7/recomp/runtime -I$RS/tools/ws7/recomp/overrides -I$RS/shims/include \
       -DREARMED_MODS_VERSION='"check"' "$c"; done
     ```
   - rearmed-songbridge, from its folder: `RS_REARMED=$RS ./build.sh` and `RS_REARMED=$RS ./build.sh --test`.
   - RS Companion: nothing of it builds against this repository, and its tests run against a stand-in for this one.
     Read the change against "RS Re-ARMed's mod interface" in its `native/rscompanion.c` and its
     `docs/when-the-game-updates.md`.

   These only read the other repository (build output aside). Change nothing there unless that is the task.
3. **A compile shows that the source still fits, not that the installed libraries still work.** When the change gets
   a game run, make it with the three libraries in the mods folders and read the log for them: `[rc] mod loaded:`
   for each, no `mod not loaded`, and no missing hook point or name in their own lines (`[rsmods]`, `[songbridge]`,
   the companion's log). Without a game run, say that the installed libraries were not run.

### Tell the maintainer

Whenever one of the three breaks, may break, or could not be checked (no checkout, no game run), say so to the
maintainer at the top of the report of the work: which project, what stops working for a player who has it, which
change does it, and what each side would need. Do this when the break is intended too; the commit message alone is
not telling. A break that isn't the point of the task waits, uncommitted, for the maintainer's word on how to go on.

What follows is the maintainer's call: the entry in `docs/release/next.md` names the version of the other project
the release needs, a released version of it that stops working is also a known issue, and the fix on the other side
is that repository's own work, with its own commit and release.

## Code and commits

- Follow the style of the surrounding code: comment density, naming, idiom. C for the runtime and shims,
  Python for the tooling, Swift for the installer app, shell for the build and install scripts.
- On a branch or in a pull request, one change per commit. The subject starts with the area, then says what is
  now true, e.g.
  `Installer: each edition keeps the Steam account that owns it`, `Win-x86: …`, `Recompiler (32-bit): …`,
  `Docs: …`, `Shell: …`, `Metal: …`, `Audio: …`. The body says why, and notes what the other edition got
  or still needs.
- `main` stays clean: larger work goes on a branch or pull request and lands as a squash merge, one commit per
  piece of work, with the same kind of subject and a body that sums up the whole of it. What a branch needed on
  the way (merges from other branches, work in progress, fix-ups, review fixes) stays out of `main`'s history.
  Unrelated pieces land as separate commits. Before any push, check the unpushed history reads this way.

## Releasing

One release covers both editions; the steps and their scripts (`tools/release`) are in
[docs/release/README.md](docs/release/README.md), and Claude Code's `/release` follows them.

- As work lands: a change a player or a mod's author will notice gets its entry in `docs/release/next.md`, and an
  issue opened or closed changes `docs/release/known-issues.md`, in the same piece of work. An entry in a `PLAN.md`
  that isn't out yet says `(not released)`.
- A release checks what its changes call for (`tools/release/scope.py`), and says what it left unrun. A new
  directory or test suite gets its rule there.
- A release that changed anything of [what the mods of other projects use](#what-they-hold-us-to) is checked against
  them before it is published, and the maintainer hears the result with the rest of what the release checked.
- A release's installer is signed with the maintainer's update key (`RS_UPDATE_KEY`), which the installers in use
  check before they replace themselves with it. Like the seed it is never in the repository; never print it, and
  sign nothing but a release's zip with it ([docs/release/README.md](docs/release/README.md#the-update-signature)).
- Publishing (`tools/release/publish.sh … --yes`, or the Release workflow with Publish on,
  [docs/release/README.md](docs/release/README.md#on-github)) is the remote change to ask about, every time.

## Building

- Mac: [docs/mac/INSTALL.md](docs/mac/INSTALL.md#build-by-hand) (CMake and Ninja from the build's venv,
  no Homebrew), or the installer (`tools/installer`, `tools/play-native/install.sh`).
- Learn & Play: `tools/play-native/learn-play.sh`, or by hand per [win-x86/README.md](win-x86/README.md).
  Translating the game takes 15-25 minutes; run long builds in the background.
- Build output goes under `build/` or `$RS_BUILD_DIR`, never into the tree.
- Don't build while a game run is in progress.

## Testing

- Mac: [docs/mac/TESTING.md](docs/mac/TESTING.md). Mostly `ctest --test-dir build --output-on-failure`,
  plus the Steam layer and netguard checks listed there.
- Learn & Play: [docs/windows/TESTING.md](docs/windows/TESTING.md). The installer unit tests
  (`python3 -m unittest discover -s win-x86/install -p 'test_*.py'`), the shim `make … test` targets,
  the D3D9 host test, and the recompiler's 32-bit difftest.
- Shared: `python3 tools/targets/check.py`, `tools/play-native/test_gamefiles.py`, the `opengl.*` tests.
- Either: `tools/release/check.py` runs the suites a change calls for (`tools/release/scope.py` says which,
  `--all` runs every one). It builds, so not during a game run.
- Other projects' mods: none of the suites above builds or runs them. A change to what they use gets the checks of
  [Projects that build on this one](#projects-that-build-on-this-one).
- Tests that need game files skip when the files aren't there; say so when reporting results.
- Before treating something as a port bug, check that the original game doesn't do the same (under
  Rosetta for Mac, on Windows for Learn & Play).
- When you drive the game's UI (scripted input or computer use), write down how you got through each
  screen in the edition's `TESTING.md`, so the next run doesn't have to rediscover it.
