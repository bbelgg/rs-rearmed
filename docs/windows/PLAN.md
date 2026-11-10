# Windows port: status and plan

The Mac port's plan: [Mac plan](../mac/PLAN.md). How things were checked is in [TESTING.md](TESTING.md), the
measurements in [PERFORMANCE.md](PERFORMANCE.md); earlier releases are in git and on the
[Releases](https://github.com/bbelgg/rs-rearmed/releases) page.

## Status

- **Released**: since 1.4.0; `VERSION` says which release this is.
- **Works**, checked on an Apple Silicon Mac: the game offline with the Uplay screens skipped; songs to the end, Score
  Attack, the Guitarcade, Session Mode, Nonstop Play, video lessons, the Tone Designer's Edit Tone; custom songs and
  DLC; Enumerate, EXIT GAME and Refresh Game Files; a Real Tone cable device (a virtual one named like the cable), the
  microphone and other interfaces through Direct Connect; every channel in the input list, input channel and gain,
  calibration that can boost; the output following the system's default; window, full screen and Retina; Command-Q and
  the close button, in 0.3-0.5 s; the built-in fixes, no intro screens or Auto Save warning; the GUI installer.
- **Checked on**: one Mac (M2) with a 75 Hz external display and the built-in Retina screen.
- **Performance**: a song holds the display's rate, 60 fps on a 60 Hz display and 74.9 on 75 Hz, at 52-54% of a core
  and 1.5 GB, about the CPU of the original under Rosetta and more than the Mac edition's 42-43% and 1.2 GB
  ([PERFORMANCE.md](PERFORMANCE.md#toward-the-mac-edition)). The title is up in about 7 s. Guest memory doesn't grow
  over songs in a row.
- **Mods**: 18 of the 21 hook points (`si_io`, the Mac game's CoreAudio input, has no counterpart, and the two of two
  players are in `win32_overrides.c`), the overlay API, the render API, the `mods/` folder and one mod ABI with the Mac
  ([DESIGN.md](DESIGN.md#built-in-fixes-and-mods)). `rearmed-mods.dylib` runs as the same file: 0.6.0 played here, and
  0.8.0, which the Mac has loaded, not run here yet. No mod code runs without the network guard.
- **Files a mod serves** (not released): as in the [Mac plan](../mac/PLAN.md#status), through the Win32 seam's file
  calls ([DESIGN.md](DESIGN.md#built-in-fixes-and-mods)). Covered by unit tests and the same run to the note highway as
  the Mac's ([TESTING.md](TESTING.md)). A provider that changes what an archive holds has not been run.
- **Edits and mid points, and 7-string charts** (new in 1.12.0): a mod changes a function from the inside
  ([DESIGN.md](DESIGN.md#built-in-fixes-and-mods)). The tables (`tools/targets/win-x86/edits.map`, `mids.map`) carry
  rearmed-mods' Seven-String Guitar, after [rs7string](https://github.com/SomeeName/rs7string), and Plain Packages.
  Run by scripted input and played with a guitar, a 7-string chart through several times. The Mac edition has the same
  units (new in 1.13.0); the points that are its own are `none` here.
- **What the game names for mods**, as in the [Mac plan](../mac/PLAN.md#status):
  - *Colors of notes, the highway and strings* (new in 1.9.0): checked here in one song. The palette textures are BGRA
    here, so their hashes differ from the Mac's.
  - *The note the game hears* (new in 1.11.0): named by RSMods' own chain on this game. Read on the Mac; not yet run
    here.
  - *The note counts and the grey-note time* (new in 1.11.0): the counts are reached as RSMods reaches them, from the
    UI manager, in Learn a Song and, by its own names (not released), in Score Attack; both seen in runs, as was the
    grey-note time.
  - *A hook on the tone change* (new in 1.11.0): wrapped at the function RSMods patches; the game ran on it with
    rearmed-mods loaded, but no mod hooked it here yet.
  - *A hook on the song lists' names* (new in 1.11.0): wrapped at the function RSMods patches; played on the Mac, not
    yet here.
  - *A hook on decoded song audio* (new in 1.12.0): checked against another decoder block by block here too, also
    after a rewind; Song Stems played here with a stand-in for its helper.
- **Built-in mods switched off in the installer** (new in 1.11.0): as in the [Mac plan](../mac/PLAN.md#status);
  `lp_settings.c` reads `learn-play/settings.env` before anything reads a switch, and logs what it used at the top of
  `last-run.log`, as the Mac launcher does.
- **Game Mode** (new in 1.9.1): the app's category is music, as in the [Mac plan](../mac/PLAN.md#status). Not yet run
  in this edition.
- **Two players** (new in 1.8.0): a song with each player on their own input, played on two channels of one interface.
  A cable with an interface, two interfaces, and SWAP CABLE ASSIGNMENT on interfaces have not been played.
- **Rocksmith 1 songs**: not played in a game run; the test account owns none (`test_steam32` covers `RS_STEAM_OWNED`).
- **Screen shape** (new in 1.10.0): the game draws at its screen's or window's own shape, with no bars, as the Mac
  edition does; the original fits a 16:9 picture into the screen ([DESIGN.md](DESIGN.md#screen-and-window)). A window
  resized in Windowed mode becomes the game's resolution. `RS_WINDOW_ANY_SHAPE=0` keeps the original. Checked full
  screen on a MacBook's built-in screen and in a resized window. Not run: an ultrawide screen, a drag by hand, macOS
  full screen from a window.
- **Retina, colour and a sharper picture** (new in 1.10.0): the renderer is shared, so MetalFX upscaling, the sRGB tag,
  the game's texture LOD bias, 16x anisotropic filtering and 4x MSAA are here too
  ([Mac plan](../mac/PLAN.md#status)). Run once, full screen on a built-in Retina screen; the upscaled picture itself
  wasn't looked at (snapshots are the back buffer), and nothing was compared with them off.
- **Your own plug-in** (rearmed-mods' ToneHost add-on): played with Helix Native at 64 frames, as on the Mac. Its open
  work is kept with it.
- **Present but not tested**: the Workshop's other screens, the DLC preview, and the prebuilt harvester
  (`win-x86/harvest/rsharvest2.exe`).
- **The tables** (`tools/targets/win-x86`): every name has an entry, and `check.py` covers both targets. Most symbols
  and offsets are read from the Windows code; the map says why for the rest (inlined, folded or specialised by LTCG,
  or no counterpart). 88 names have `custom` conventions (LTCG register arguments), which their overrides read by hand.
- **The game image**: unpacked on the Mac from the user's own exe with the release's seed, or a capture for a build
  without one ([win-x86/unpack](../../win-x86/unpack/README.md)). A new exe build needs a loaded copy of the game for
  its relocation sites, which nothing reads out of the capture VM yet.

## Open issues

- **The published installer**: recent releases' hasn't been run, nor a full install from the tag; the install scripts
  and test installs were.
- **The title screen's own Auto Save call**: read from the code, not seen in a run. The title's tick can show the
  warning for 4 s when a flag is set; what sets it isn't known, so whether a player can reach it isn't either. If so,
  the skip needs the tick wrapped too, to clear its timer.
- The cable screen's Direct Connect button hasn't been checked.
- The list of inputs and the level, not checked yet: unplugging an interface while the game runs, a Real Tone cable
  with this build, and the level with a second profile, which counts as calibrated with the first.
- winmm's device names stop at 31 characters, so on that fallback path a long interface name can lose its channel.
- **Two crashes seen once each**: one on the stage at the tuner skip (`lp: signal 10: guest address 0x0`), and a fault
  at guest address 0x168 before it. Neither has come back.
- **A song ending a few seconds in**: with the machine heavily loaded by other builds, a song went back to its page
  soon after it started in most runs, with the profile saved as at a song's end and no game exception
  (`RC32_LOG_RAISE`). Not looked into further.
- **Two COM object tables**: the D3D9, audio and Steam faces share the seam's thunk cells, vtables and guest
  memory (`win32_guestobj.c`), but the audio and D3D9 faces still each keep their own table of objects and
  their own `IUnknown` (D3D9's also has internal references and forwarded counts).
- Guitar latency against the Mac port has not been measured.
- **CPU and memory**: more than the Mac edition on the same route
  ([PERFORMANCE.md](PERFORMANCE.md#toward-the-mac-edition)). The memory order costs about 1 ms a frame at the
  display's rate ([PERFORMANCE.md](PERFORMANCE.md#memory-order)).
- **Shaders the list doesn't have**: the corpus has the programs seen in runs. The Guitarcade's games wait for a guitar
  at calibration, so it has only their effects, and one venue of Venue Mode. Run them with `LP_SHADER_LOG` and add them
  ([TESTING.md](TESTING.md#shaders-of-a-run)). A program not in it stalls about 0.25 s the first time.
- **After a macOS update** the first play compiles again unless the warm-up is run again; the Mac edition does it at
  its next start when safe load is on. Here it would be `rs_gfx_prepare` at context creation, with a line on the black
  window.
- **One DLC archive differs between depots**: one name (`notegew2_p.psarc`) has two versions 2 bytes apart. The copy
  keeps one; what the bytes do has not been read.
- **Song archives per platform**: this game reads the PC archives (`*_p.psarc`), the Mac game `*_m.psarc`. Official DLC
  has both; a custom song often exists for one platform only. The song files are encrypted with a key per platform,
  which is not in the game a player has here, and nothing from the game may be in this repository, so RS Re-ARMed has
  no converter and will not get one.
- **The fixes from the code review of 2026-11-09** (not released) were run in their suites and in one song: a thread
  that ends gives its stack, TEB and TLS blocks to the next, a guest path that starts with `/` is a host path only
  inside the game's own folders, and `HeapAlloc` clears a block it uses again. A long session, where the reused blocks
  matter, has not been played. What the shared code got from the same review is in the
  [Mac plan](../mac/PLAN.md#open-issues).
- D3DX paths not exercised yet: `D3DXCreateEffectCompiler`, `D3DXCreateTextureFromFileExA`, the cube and
  volume loaders, `D3DXSaveSurfaceToFileInMemory`, effects with textures.
- Fixed function not implemented (logged once): vertex blending, sphere maps, bump mapping, user clip
  planes, point sprites.

## Next work

Being looked into, with no promise of if or when:

- **Parity**: the checks the Mac port has had that are left here: long sessions, and guitar latency against it.
- **Note detection** (not released): one more of its loops runs relaxed, about 1.6 points of a core less in a song
  ([PERFORMANCE.md](PERFORMANCE.md#where-the-cpu-goes-in-1130)); to be played with a guitar before a release. What is
  left on that thread is its own work, or is not safe to relax.
- **Performance** ([PERFORMANCE.md](PERFORMANCE.md#opportunities-by-expected-payoff)), by expected payoff: the note
  detection and the Wwise functions still fenced; the D3D9 layer's bookkeeping; the index-range scan of the draw-time
  upload; zero-copy dynamic buffers; memory (guest copies of D3D9 buffers).
- **Measured runs**: what is left of `tools/perf` is in the [Mac plan](../mac/PLAN.md#next-work), for both editions.
- **Choosing both players' inputs on the Multiplayer screen**, as in the [Mac plan](../mac/PLAN.md#next-work).

## Risks

1. **Renderer fidelity**: D3D9 semantics through GL to Metal. The menus, the stages and the note highway
   render; scenes not played yet may show gaps.
2. **Steam updating the Windows build**: the pinned manifest keeps installs working; a new build means a new
   seed, new import tables and a new `target.toml`.
3. **The seed**: the release's installer carries it, and a new exe build needs a new one, from a loaded copy of the game
   ([Status](#status)). Without a seed a user needs a Windows PC once.

## Decisions

- Two targets, one source tree; per-game data only in per-target tables; every native reference by name.
- A 32-bit guest in a reserved 4 GB window.
- Microsoft's DLLs translated, not rewritten; native overrides only for leaf functions with plain contracts
  (the math exports so far).
- Direct3D 9 implemented on `libRSOpenGL`, so the Metal core and its caches are shared.
- Metal only: every Mac this edition runs on has Metal, so no OpenGL renderer or fallback is planned here.
- One pinned Windows build (depot 221683, manifest 8918188356915973501), downloaded by the Mac Steam
  client; no steamcmd.
- No Rosetta, no Homebrew, no .NET in the install: native arm64 and tools macOS ships. The Microsoft DLLs are
  the user's own, from Steam's Common Redistributables or Microsoft's redistributables, checked by SHA-256.
- The game image from the user's own exe, unpacked on the Mac with a seed that holds no game code; for a build without
  one, a one-time capture of the user's own game, with the harvester prebuilt beside its source.
