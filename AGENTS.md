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
(`shims/coreaudio`), the offline Steam behaviour, the mod loader and its ABI
(`tools/ws7/recomp/runtime/rc_mods.c`, `rc_mod.h`), the installer app (`tools/installer`), the game-file copy
(`tools/play-native/gamefiles.py`), the per-game tables (`tools/targets`) and the plug-in host (`tools/tonehost`).

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
either edition's docs.

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
- Tests that need game files skip when the files aren't there; say so when reporting results.
- Before treating something as a port bug, check that the original game doesn't do the same (under
  Rosetta for Mac, on Windows for Learn & Play).
- When you drive the game's UI (scripted input or computer use), write down how you got through each
  screen in the edition's `TESTING.md`, so the next run doesn't have to rediscover it.
