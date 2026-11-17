# Windows port: status and plan

The Mac port's plan: [Mac plan](../mac/PLAN.md). How things are checked is in [TESTING.md](TESTING.md), the
measurements in [PERFORMANCE.md](PERFORMANCE.md); earlier releases are in git and on the
[Releases](https://github.com/bbelgg/rs-rearmed/releases) page.

## Status

What the edition does today, in the order of the [Mac plan](../mac/PLAN.md#status): an item that says "as the Mac
edition" is described there. What has not been tested is under [Open issues](#open-issues).

**The game**

- **Released**: `VERSION` says which release this is; an item marked as not released is on `main` only.
- **Checked on**: one Mac (M2) with a 75 Hz external display and the built-in Retina screen.
- **Works**: the game offline with the Uplay screens skipped; songs to the end, Score Attack, the Guitarcade, Session
  Mode, Nonstop Play, video lessons, the Tone Designer's Edit Tone; custom songs and DLC; Enumerate, EXIT GAME and
  Refresh Game Files; Command-Q and the close button, in 0.3-0.5 s; the built-in fixes, no intro screens or Auto Save
  warning.
- **Performance**: a song holds the display's rate, 60 fps on a 60 Hz display and 74.9 on 75 Hz, at 52-54% of a core
  and 1.5 GB, about the CPU of the original under Rosetta and more than the Mac edition's 42-43% and 1.2 GB
  ([PERFORMANCE.md](PERFORMANCE.md#toward-the-mac-edition)). The title is up in about 7 s. Guest memory doesn't grow
  over songs in a row.
- **Rocksmith 1 songs**: by the Mac edition's rule (`RS_STEAM_OWNED`); none has been played here.
- **Two players**: a song with each player on their own input, as the Mac edition.

**Sound and input**

- **Guitar input**: a Real Tone cable device (a virtual one named like the cable), the microphone and other interfaces
  through Direct Connect; every channel in the input list, input channel and gain, calibration that can boost.
- **Output**: follows the system's default.
- **Game Mode**: the app's category is music, as the Mac edition.

**Picture**

- **Renderer**: Metal only, through the Mac edition's renderer; a window, full screen, Retina.
- **Screen shape**: the game draws at its screen's or window's own shape, with no bars, as the Mac edition; the
  original fits a 16:9 picture into the screen ([DESIGN.md](DESIGN.md#screen-and-window)). A window resized in Windowed
  mode becomes the game's resolution. `RS_WINDOW_ANY_SHAPE=0` keeps the original.
- **Retina, colour and a sharper picture**: as the Mac edition, the renderer being shared.

**Built-in fixes** (the Mac edition's, on this game's own functions and with the same switches:
[DESIGN.md](DESIGN.md#built-in-fixes-and-mods))

- **Switched off in the installer**: as the Mac edition; `lp_settings.c` reads `learn-play/settings.env` and
  `learn-play/options.env` (not released) before anything reads a switch, and logs what it used at the top of
  `last-run.log`, as the Mac launcher does.
- **Batched DLC mounting**: the loop waits for each mount to be finished, so every archive of a large library is
  mounted. "Enumerating downloadable content..." stays until every song is in the lists, and a large library
  mounts in half the time (not released), as in the Mac edition and from its source: the asset loader thread does
  not pause 10 ms between files while archives are mounted, the flag is held for manifests still to come, and a
  step no longer asks every mounted archive for the shader cache's files. With 2,250 archives, 2,100 of them the
  Mac edition's through a mod, the text showed for 42 s and a search right after it found 1,494 of 1,977 matches;
  now the text and the last song end together after 20 s, and 150 archives take 0.6 s.
- **Calibration and the tuner at any frame rate** (new in 1.16.0): as the Mac edition, from the same source with this
  game's eight points.
- **Skip Tuner in the Guitarcade** (new in 1.16.0): as the Mac edition, from the same source with this game's point.
- **A fixed frame rate** (new in 1.16.0): `RS_FPS=N`, the Mac edition's test switch, with its rule and clock
  (`shims/platform/include/rsp_fps.h`): vsync off and a frame every 1/N s, whatever the display's rate.
- **Shared with the Mac edition** (new in 1.16.0): the switches, the log (its lines start `[rs-mods] ` as the Mac's)
  and the start, the intro skip, the song ownership during a mount, Player 2's saved input and the frame rate fixes are
  the Mac's source (`overrides/mods/mods_shared.c`, `framerate.c`; DESIGN.md says what stays this port's own and why),
  and so is what the batched DLC mounting does around its steps (not released).
  For Player 2's saved input this game's `input_bind` is a hook point, the Mac's call with its second argument folded
  away, and the shared hook frees the name's text, which this port's own version left behind as the Mac's did.
- **By name** (new in 1.16.0): `win32_overrides.c` names every function, global and field of the game through
  `tools/targets/win-x86`, as the Mac's overrides do through `mac-x64` ([DESIGN.md](DESIGN.md#per-game-tables)). The
  list of functions it replaces is a table too (`names/overrides/win32.list`), and the translation's skip file is made
  of it.

**Installer and game files**

- **Installer**: the Mac edition's GUI installer, which has Steam download the pinned Windows build and the
  account's DLC.
- **Where the game files are kept**: as the Mac edition, by the same code.
- **The download code is shared with the Mac edition**: the Steam steps, the room check, the link to another disk,
  Steam's log and the wait are in `tools/play-native/gamefiles.py`, where the Mac game uses them when it isn't
  installed in Steam; `lp_install.py` keeps this game's figures.
- **The game image**: unpacked on the Mac from the user's own exe with the release's seed, or a capture for a build
  without one ([win-x86/unpack](../../win-x86/unpack/README.md)).

**Mods**

- **Loading and hook points**: 19 of the 21 hook points, `input_bind` the latest (new in 1.16.0); `si_io`, the Mac
  game's CoreAudio input, has no counterpart, and `input_mode_is_cable` is inlined into a function
  `win32_overrides.c` replaces. The overlay API, the render API, the `mods/` folder and one mod ABI with the Mac
  ([DESIGN.md](DESIGN.md#built-in-fixes-and-mods)): `rearmed-mods.dylib` runs as the same file. No mod code runs
  without the network guard.
- **Files a mod serves**: as the Mac edition, through the Win32 seam's file calls.
- **Edits and mid points, and 7-string charts**: as the Mac edition, with this game's tables
  (`tools/targets/win-x86/edits.map`, `mids.map`); the points that are the Mac's own are `none` here.
- **What the game names for mods**: as the Mac edition. The palette textures are BGRA here, so their hashes differ
  from the Mac's; the note the game hears and the note counts are reached as RSMods reaches them on this game (the
  counts from the UI manager, in Score Attack by its own names); the hooks on the tone change and the song lists'
  names are wrapped at the functions RSMods patches.
- **Your own plug-in** (rearmed-mods' ToneHost add-on): played with Helix Native at 64 frames, as on the Mac. Its open
  work is kept with it.
- **The tables** (`tools/targets/win-x86`): every name has an entry, and `check.py` covers both targets. Most symbols
  and offsets are read from the Windows code; the map says why for the rest (inlined, folded or specialised by LTCG,
  or no counterpart). 104 names have `custom` conventions (LTCG register arguments), which their overrides read by
  hand.

## Open issues

**Faults and limits**

- winmm's device names stop at 31 characters, so on that fallback path a long interface name can lose its channel.
- **A large library's mounting time**: 2,250 archives take 19 to 20 s here, 8.6 ms each, where the Mac edition
  takes 2.3 to 3.3 ms an archive. Three things differ, none of them measured apart: 2,100 of these archives are the
  Mac edition's, which a mod serves in this game's form as they are opened; this loop waits for each archive's file
  to be closed, by a thread of the game's, where the Mac's waits on the game's own queue of packages (`dlc_package`
  and `dlc_package_queue`, not matched here); and a frame of the mounting took 87 ms, 50 of them the loop's budget.
- **Two crashes seen once each**: one on the stage at the tuner skip (`lp: signal 10: guest address 0x0`), and a fault
  at guest address 0x168 before it. Neither has come back.
- **A song ending a few seconds in**: with the machine heavily loaded by other builds, a song went back to its page
  soon after it started in most runs, with the profile saved as at a song's end and no game exception
  (`RC32_LOG_RAISE`). Not looked into further.
- **Two COM object tables**: the D3D9, audio and Steam faces share the seam's thunk cells, vtables and guest
  memory (`win32_guestobj.c`), but the audio and D3D9 faces still each keep their own table of objects and
  their own `IUnknown` (D3D9's also has internal references and forwarded counts).
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
- **Song archives per platform**: this game reads the PC archives (`*_p.psarc`), and RS Re-ARMed has no converter
  ([Mac plan](../mac/PLAN.md#open-issues)).
- **A new exe build** needs a loaded copy of the game for its relocation sites, which nothing reads out of the capture
  VM yet.
- Fixed function not implemented (logged once): vertex blending, sphere maps, bump mapping, user clip
  planes, point sprites.

**Not tested or not done yet**

- **The published installer**: recent releases' hasn't been run, nor a full install from the tag; the install scripts
  and test installs were.
- **Game files**: the Game files choice and the move of the copy, by hand on this edition; a download from the real
  Steam since the download code moved into `gamefiles.py`.
- **Batched DLC mounting**: not run with the new code: a library of thousands of this game's own archives (the
  large one here is mostly the Mac edition's, through a mod), Enumerate with new archives to mount,
  `RS_MODS_BUDGET_MS=0`, an archive in `dlc/` that brings a shader's hash file (the step's question then goes to
  the whole file system; none exists to try), and a mount or manifest that does not come (the mounting then goes on
  after 5 s, with a line in the log: the shared source's test has the manifest's case, no game run has either).
- **Auto Save**: the skip with a Real Tone cable or a Microphone profile. The title screen's own call of the dialog is
  read from the code, not seen in a run: the title's tick can show the warning for 4 s when a flag is set; what sets it
  isn't known, so whether a player can reach it isn't either. If so, the skip needs the tick wrapped too, to clear its
  timer.
- **Guitar input**: as in the [Mac plan](../mac/PLAN.md#open-issues); and guitar latency against the Mac edition has not
  been measured.
- **Two players**: a cable with an interface, two interfaces, and SWAP CABLE ASSIGNMENT on interfaces. Two inputs have
  not been played since Player 2's saved input became the shared hook; a single player's way to the note highway never
  calls `input_bind`, so in the game only the hook's place in the table is checked, and the unit tests stand for what
  it does.
- **Calibration and the tuner at any frame rate**: with a guitar above 60 Hz, alone and with two players. One route
  to the note highway ran at `RS_FPS=120` with the hooks on (119-120 fps in the menus); what they do to calibration
  and the tuner there has not been played. `RS_FPS` itself has not been run on a 60 Hz or a 120 Hz screen, in a
  window, or above 120.
- **The fixes from the code review of 2026-11-09** (a thread that ends gives its stack, TEB and TLS blocks to the next,
  a guest path that starts with `/` is a host path only inside the game's own folders, and `HeapAlloc` clears a block it
  uses again): four songs played with a guitar since, in Learn a Song, with no fault; no long session, where the reused
  blocks matter. What the shared code got from the same review is in the [Mac plan](../mac/PLAN.md#open-issues).
- **Mods**: a file provider that changes what an archive holds; the note the game hears and the song lists' names,
  both played on the Mac only; a mod's hook on the tone change (the game ran on the wrapped function with rearmed-mods
  loaded); Song Stems with its own helper (it was played with a stand-in for it); the colors in other modes (they were
  checked in one song).
- **Screen shape**: an ultrawide screen, a drag of the window by hand, macOS full screen from a window, and full
  screen on a second display.
- **Retina, colour and a sharper picture**: run once, full screen on a built-in Retina screen; the upscaled picture
  itself wasn't looked at (snapshots are the back buffer), and nothing was compared with them off.
- **Rocksmith 1 songs** in a game run: the test account owns none (`test_steam32` covers `RS_STEAM_OWNED`).
- **Game Mode**: that macOS leaves it off for this app.
- **Present but not tested**: the Workshop's other screens, the DLC preview, and the prebuilt harvester
  (`win-x86/harvest/rsharvest2.exe`).
- D3DX paths not exercised yet: `D3DXCreateEffectCompiler`, `D3DXCreateTextureFromFileExA`, the cube and
  volume loaders, `D3DXSaveSurfaceToFileInMemory`, effects with textures.

## Next work

Being looked into, with no promise of if or when:

- **Parity**: the checks the Mac port has had that are left here: long sessions, and guitar latency against it.
- **Choosing both players' inputs on the Multiplayer screen**, as in the [Mac plan](../mac/PLAN.md#next-work).
- **Performance** ([PERFORMANCE.md](PERFORMANCE.md#opportunities-by-expected-payoff)), by expected payoff: the note
  detection and the Wwise thread, where relaxing more functions gave nothing and what is left is the threads' own work
  or not safe to relax; the D3D9 layer's bookkeeping; zero-copy dynamic buffers; the index-range scan of the draw-time
  upload; memory (guest copies of D3D9 buffers).
- **Measured runs**: what is left of `tools/perf` is in the [Mac plan](../mac/PLAN.md#next-work), for both editions.
- **One code base for both games**: what is shared and what is still written twice is in the
  [Mac plan](../mac/PLAN.md#next-work).

## Risks

1. **Renderer fidelity**: D3D9 semantics through GL to Metal. The menus, the stages and the note highway
   render; scenes not played yet may show gaps.
2. **Steam updating the Windows build**: the pinned manifest keeps installs working; a new build means a new
   seed, new import tables and a new `target.toml`.
3. **The seed**: the release's installer carries it, and a new exe build needs a new one, from a loaded copy of the game
   ([Open issues](#open-issues)). Without a seed a user needs a Windows PC once.

## Decisions

- Two targets, one source tree. Per-game data is in per-target tables, and native code names the game through them
  (`tools/targets`), the built-in fixes included; the import sites and the vtables the online layer patches have
  tables of their own (`win-x86/imports`, `win-x86/online`).
- A built-in fix that needs nothing of one game alone is the Mac's source ([Mac plan](../mac/PLAN.md#decisions)); one
  that does is written here, for a function left out of translation or wrapped (`names/overrides/win32.list`).
- Offline by design, as the Mac edition: the offline Steam layer and the offline online layer; no host socket is linked
  in, the Win32 seam refuses the game's own network calls (`win-x86/shims/win32_net.c`), and mods load only with the
  network guard in the process.
- A 32-bit guest in a reserved 4 GB window.
- Microsoft's DLLs translated, not rewritten; native overrides only for leaf functions with plain contracts
  (the CRT's math exports and its `memcpy`, `memmove`, `memset` and `memcpy_s`).
- Direct3D 9 implemented on `libRSOpenGL`, so the Metal core and its caches are shared.
- Metal only: every Mac this edition runs on has Metal, so no OpenGL renderer or fallback is planned here.
- One pinned Windows build (depot 221683, manifest 8918188356915973501), downloaded by the Mac Steam
  client; no steamcmd.
- No Rosetta, no Homebrew, no .NET in the install: native arm64 and tools macOS ships. The Microsoft DLLs are
  the user's own, from Steam's Common Redistributables or Microsoft's redistributables, checked by SHA-256.
- The game image from the user's own exe, unpacked on the Mac with a seed that holds no game code; for a build without
  one, a one-time capture of the user's own game, with the harvester prebuilt beside its source.
- The user's installed game and saves are only read, and Steam's download folder is used as in the
  [Mac plan](../mac/PLAN.md#decisions).
