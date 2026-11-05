# Windows port: status and plan

**Experimental.** It is merged into `main` and goes out first with 1.4.0. The Mac port's plan:
[Mac plan](../mac/PLAN.md).

## Status

Guitar input channel and gain (new in 1.6.0; 2026-11-05, a build by hand, a test home): an HX Stomp's channel 5 at 0 dB through Direct
Connect, heard right by the maintainer in a short run; peaks up to -16.4 dBFS, nothing clipped. Not in this pass: a gain
above 0 dB in a run, a whole song, an install by the installer.

Other interfaces through Direct Connect (2026-11-05): work, by the maintainer's own check.

Every channel in the input list, and calibration that can boost (new in 1.6.0; 2026-11-05, by the
maintainer, a test home): an HX Stomp's 8 channels and a UR22's 2 listed, a guitar through a channel, a choice kept over a restart;
calibration settled at +6.1 dB with the meter at the top. Not in this pass: the Real Tone cable with this build.

Checked by hand on an Apple Silicon Mac (2026-11-01):

- the game boots offline, with the Uplay screens skipped;
- Learn a Song played to the end; Score Attack with notes detected and scored; the Guitarcade games;
- the backing track in time with guitar input, through a Real Tone cable device (a virtual device named
  like the cable) or the microphone;
- the sound of the intro video;
- window and full screen (the in-game Windowed Mode switch was checked from screenshots of a scripted run).

Performance: a song holds the display's rate, 74.9 fps on 75 Hz, with no frame over 33 ms, at 63% of a core against the Mac
port's 44% on the same song and screen ([PERFORMANCE.md](PERFORMANCE.md#final-pass-for-140)). The install prepares the shaders:
the first play after it has about 1.2 s more of frames over 33 ms up to the highway than the second, and none in the
song ([PERFORMANCE.md](PERFORMANCE.md#shader-preparation)).

Checked in the app installed by the installer, started with `open` (2026-11-04, a test home, by keys and snapshots,
[TESTING.md](TESTING.md#unattended-runs)): songs to their end and back to back, a Session Mode session, Nonstop Play and its
playlist, a video lesson, the Tone Designer's Edit Tone, the Learn a Song list, the Multiplayer menus, DIRECT CONNECT with an
audio interface listed and chosen, Steam profiles copied in once (without `crd`), the Metal cache in its own folder, the game's
own Exit and the app's Quit, 74.9 fps on a 75 Hz display. `RS_METAL_RETINA=1` on the built-in screen: a 2940x1912 back buffer, the
menu and a song on the highway at 60 fps, 54% of a core.

Final pass for 1.5.1 (2026-11-05, the final code, `learn-play.sh`, a test home): a song to the note highway and a quit with RSMods' library, the
same route and results as before the last fixes; a start and a clean quit without mods; nothing refused by the network guard. The 32-bit
difftest shows only its two known mismatches, and all suites of [TESTING.md](TESTING.md) pass. Not in this pass: an install by the installer app,
a real Steam download, a guitar.

Final pass for 1.5.0 (2026-11-05, nothing from an earlier build): `learn-play.sh` and the installer app each installed from nothing, with the
source given by `RS_SOURCE_URL` and a stand-in Steam, through Preparing shaders; the image's SHA-256 is the pin in `target.toml`. The warm-up
and `open` share the system's cache ([PERFORMANCE.md](PERFORMANCE.md#shader-preparation)). Not in these runs: a real Steam download, a guitar.

Enumerate, as on the Mac: SHOP is ENUMERATE, second in the main menu, and a song added during a run is in the list and plays without a
restart; on the installed app (2026-11-04) Refresh Game Files keeps it, and after a restart it plays from the copy
([TESTING.md](TESTING.md#unattended-runs)). The Shop itself never opens offline: the game checks the Steam overlay first and shows a notice.

Present but not tested: a multiplayer song played by two (by keys it reaches a split-screen song, [TESTING.md](TESTING.md#shaders-of-a-run)), the
Workshop's other screens, the DLC preview, and the prebuilt harvester (`win-x86/harvest/rsharvest2.exe`: the capture used so far was made with
an earlier build of the same tool).

Built in, as on the Mac: DLC archives mounted in batches within a frame budget and the song-file signature skip (RSBypass's behaviour), the song
path cache and the number-table hash ([DESIGN.md](DESIGN.md#built-in-fixes-and-mods)); song archives listed whatever AppID they carry, custom
songs, no intro screens and the "Re-ARMed" title logo ([Built in for 1.4.0](#built-in-for-140)). With all of them 147 archives mount in 5 frames,
0.27-0.29 s (two runs on 2026-11-04). The open-file limit goes up at the start (`RS_MODS_FIXES=0` leaves it): 441 archives peak at 461 descriptors,
over the 256 of an app started from Finder.

With the intro skip the title is up in 7-9 s and takes Return by 12 s, on a first run too with the shaders prepared
([PERFORMANCE.md](PERFORMANCE.md#shader-preparation)).

The system's default output changing reaches the game: the render stream reopens on the new device (`audio: the default output is now ...`) and
the sound follows, there and back. Checked through the window script's `audio` step and by switching a real device during a song (2026-11-04).
The Mac edition follows too since 1.5.1 ([Mac testing](../mac/TESTING.md#game-runs)).

Command-Q and the close button quit through the game's own shutdown (`WM_CLOSE`), in 0.3-0.5 s, and losing the keyboard lets go of held keys and
tells the game (2026-11-04).

Rocksmith 1 DLC songs: `RS_STEAM_OWNED` reaches the face as on the Mac and `test_steam32` covers it, but no game run
yet: the test account owns none of those DLC. Open: one run with an account that does, or with the variable set.

Guest memory doesn't grow over songs in a row: 415 MB at the menu, 449 MB in the second song.

Mods and hook points (2026-11-05): the 14 hook points of 1.5.1 (the 15th since, `si_io`, is the Mac game's CoreAudio input and has no counterpart), the overlay API, the `mods/` folder and one mod ABI
with the Mac ([DESIGN.md](DESIGN.md#built-in-fixes-and-mods)). The same `probe_mod.dylib`, its SHA-256 in both logs, ran to the
note highway and a quit in this app and the Mac edition's, in test homes: the network guard inserted, nothing refused, the 11 points of 1.5.0
reached with the arguments the Mac run shows, `game_frame`'s quit answered on both. No mod code runs without the guard (no
`libnetguard.dylib`, or a restart that lost it) or from a library built without `rc_mod.h`. With no mod the table doesn't show on the
game thread ([PERFORMANCE.md](PERFORMANCE.md#hook-points)). Started with `open`, from a test home on the internal disk, the app
restarts with the guard, keeps what was already inserted, and plays a whole song with the mod; with an empty `mods/` folder it
doesn't restart.

RSMods' library (2026-11-05): `rearmed-mods.dylib` 0.3.0 loads and runs here as the same file as on the Mac, all 19 mods on, in a test home
to the note highway and a quit ([Fixed in 1.5.1](#fixed-in-151) says what a run covered). The configurator installs it for the Mac edition
only; `Mac/build.sh --install` puts it in both folders.

The tables (`tools/targets/win-x86`): every name in `tools/targets/names` has an entry (`check.py` covers both targets), and `target.toml` has the
image's `.text`, its SHA-256 and the install pins. 965 of the 1,152 symbols and 74 of the 95 offsets are read from the Windows code; 12 of the 15
hook points are there, `input_sink` and `set_app_focus`, which have no function here, are served from the seam
([DESIGN.md](DESIGN.md#built-in-fixes-and-mods)), and `si_io` has no counterpart. The 187 symbols left say why in the map: 32 are not needed here, 24 are Mac-only, 30 are not
proved, and the rest are inlined by LTCG or have no counterpart of the Mac's shape (48 of them the Lua API). 19 of the 21 offsets left are Mac-only.
88 names have `custom` conventions (LTCG register arguments), which their overrides have to read by hand.

Where it stands against the plan's phases:

| Phase | What | State |
|---|---|---|
| P0 | the Mac build made name-keyed (`tools/targets`), the convention-aware ABI layer, `rc_hook_add_name` | done; the Mac build's generated C stayed byte-identical |
| P1 | the game image | done: unpacked on the Mac from the release's seed; a capture on a Windows PC for a build without one ([DESIGN.md](DESIGN.md#getting-the-game-image)) |
| P2 | the 32-bit front end | done: the game, msvcr100, msvcp100, d3dx9_42, D3DCompiler_42 and bink2w32 translate |
| P3 | boot to the menu | done, and past it: songs play |
| P4 | parity with the Mac port | the game, audio, Bink, mods and the window modes work; left are the checks in [Next work](#next-work) |
| P5 | the installer | done in the shell (`learn-play.sh`) and in the installer app ([TESTING.md](TESTING.md#the-installer-app) says what was driven) |

The game image comes from the user's own exe, unpacked on the Mac with the release's seed; a capture is the fallback
for a build without one ([win-x86/unpack](../../win-x86/unpack/README.md)). Still open: a new exe build needs a loaded
copy of the game for its relocation sites, which nothing reads out of the capture VM yet.

The download needs no paste: the running Steam takes the depot command on its command line, and `game-download`
leads the user through what only they can do, in the same words as the Mac edition's Steam steps.

Every image is compiled with x86's memory order, as the Mac image always was; a caught C++ exception, an unwind through
inner frames and `longjmp` work and are tested on the real CRT (`crt_probe -eh`); a guest stack has the size asked for and a guard.

## Open issues

- The list of inputs and the level, not checked yet: unplugging an interface while the game runs, a Real Tone cable with this
  build, and the level with a second profile, which counts as calibrated with the first.
- winmm's device names stop at 31 characters, so on that fallback path a long interface name can lose its channel.
- **Two crashes seen once each**: one on the stage at the tuner skip (`lp: signal 10: guest address
  0x0`), a fault at guest address 0x168 before it. Neither reproduced on demand yet; neither came back in the
  runs of 2026-11-04 (about ten tuner skips, on builds with x86 memory ordering and one without).
- **Two COM object tables**: the D3D9, audio and Steam faces share the seam's thunk cells, vtables and guest
  memory (`win32_guestobj.c`), but the audio and D3D9 faces still each keep their own table of objects and
  their own `IUnknown` (D3D9's also has internal references and forwarded counts).
- Guitar latency against the Mac port has not been measured.
- **CPU and memory**: 63% of a core against the Mac edition's 44%, and 1.3 GB against 0.8 GB, on the same song and
  screen. How much of the CPU is the memory order at the display's rate is not measured: the only figures are at
  120 fps ([PERFORMANCE.md](PERFORMANCE.md#memory-order)).
- **Shaders the list doesn't have**: the corpus has the programs seen in runs, and the Guitarcade's games wait for a guitar at
  calibration, so of them it has only their effects, and of the venues (Venue Mode) one was seen, with four programs of its own. Run them
  with `LP_SHADER_LOG` and add them ([TESTING.md](TESTING.md#shaders-of-a-run)). A program not in it stalls about 0.25 s the first time, as before.
- **After a macOS update** the first play compiles again unless the warm-up is run again; the Mac edition does it at its next
  start when safe load is on. In Learn & Play it would be `rs_gfx_prepare` at context creation, with a line on the black window.
- **One DLC archive differs between depots**: 113 PC archive files in the depots are 73 different archives, and one
  name (`notegew2_p.psarc`) has two versions 2 bytes apart. The copy keeps one; what the bytes do has not been read.
- **Song archives per platform**: the Windows game reads the PC archives, `*_p.psarc`; the Mac game
  reads `*_m.psarc`. Official DLC has both (each DLC is a depot with the two), but custom songs often
  exist only for one platform, so a Mac-only CDLC collection (`_m`) will not load in this build, and a PC one
  not in the Mac port. Compared on one official song, the two archives differ only in folder names
  (`songs/bin/macos`, `audio/mac` against `songs/bin/generic`, `audio/windows`), the `.nt` graph, and the `.sng`
  files, which are the same size but encrypted with a key per platform; the audio banks are byte for byte the
  same. So an install-time converter is simple in itself, but it needs the other platform's key, which is
  not in the game a player has here, and nothing from the game may be in this repository. Left for the
  maintainer to decide.
- D3DX paths not exercised yet: `D3DXCreateEffectCompiler`, `D3DXCreateTextureFromFileExA`, the cube and
  volume loaders, `D3DXSaveSurfaceToFileInMemory`, effects with textures.
- Fixed function not implemented (logged once): vertex blending, sphere maps, bump mapping, user clip
  planes, point sprites.

## Built in for 1.4.0

Learn & Play ships as an experimental release with these built in, the same way in both editions. All were checked
together in one build ([TESTING.md](TESTING.md#unattended-runs)); how each works is in
[DESIGN.md](DESIGN.md#built-in-fixes-and-mods).

- **Custom songs**: unsigned `*_p.psarc` songs are listed and play, whatever AppID they carry. Three gates are passed:
  the song loader's signature check, ownership (the install lists every AppID of the copy's `dlc/` in `steam.env`) and
  the Windows game's table of 1590 known song keys, which the Mac game doesn't have. `RS_MODS_BYPASS=0` brings the
  checks back. A song whose only lead part is an Alternate Lead shows an X in the list: the game's own behaviour.
- **Faster song enumeration**: DLC archives mount in batches, within `RS_MODS_BUDGET_MS` per frame.
- **Direct Connect**: a fourth value of the INPUT setting, and a button on the cable screen (not checked), from an
  overlay archive made at install from the user's own `cache.psarc`, which stays as it is on disk.
- **No intro screens**: skipped in code, with no game asset changed; `RS_MODS_INTRO=0` plays them. Once in eight runs
  a notice "The Ubisoft servers are not available" came up over the main menu; Return closes it.

## Fixed in 1.4.1

A patch release of what was found after 1.4.0, the same in the [Mac plan](../mac/PLAN.md#fixed-in-141) where it applies:

- **Rocksmith 1 DLC songs** show for the account that owns them (`RS_STEAM_OWNED`). Covered by `test_steam32`; no game
  run, as the test account owns none of them.
- **Screen saver and display sleep** stay off while the game is open, as on the Mac.
- **Audio buffer**: a device with inputs keeps a 128-frame buffer. Capture here takes any size, so this only stops the
  render buffer depending on the opening order.
- **Installer**: closing its window quits without a crash.

## Built in for 1.5.0

Goes with the same section of the [Mac plan](../mac/PLAN.md#built-in-for-150):

- **Shaders prepared at install**: a Metal corpus made from the user's game files at build time and a Preparing shaders step, the
  Mac's, so a first play has about 1.2 s more of long frames than the second instead of 22 s
  ([PERFORMANCE.md](PERFORMANCE.md#shader-preparation)).
- **Mods and hook points**: the `mods/` folder and 11 of the 14 hook points, with the network guard loaded for any mod. The same mod
  binary serves both editions ([DESIGN.md](DESIGN.md#built-in-fixes-and-mods)).
- **The `win-x86` target tables**: 957 of 1,142 symbols, 57 of 76 offsets, 12 of 14 hook points, and the image's `.text` and SHA-256.

## Fixed in 1.5.1

- **A hidden window keeps the display's rate**: the full-screen window hides when another app is active, its layer then stops
  pacing presents, and the game ran at about 120 fps. `windowDidChangeOcclusionState` now sets a wait in Present, outside the device
  lock. On a 60 Hz display, 60 fps in front, behind Finder and in front again, against 60, 120 and 60 for an earlier build; the D3D9
  host test covers the wait. Goes with the same section of the [Mac plan](../mac/PLAN.md#fixed-in-151).
- **Mods have what the Mac's have**: the last three hook points, the overlay API and `OV_VCALL`
  ([DESIGN.md](DESIGN.md#built-in-fixes-and-mods)). The probe mod ran in the game on 2026-11-05: its square on the frame, read back and on a
  snapshot; `si_open` with slots 0 to 3 and true; keys with the Mac's codes; focus lost and gained; a reference count through
  `OV_VCALL` going 2, 3, 2. A mod keeping an event from the game is covered by `test_input`, not by a run.
- **RSMods' library runs here**: `rearmed-mods.dylib` 0.3.0, the same file as on the Mac, its SHA-256 the same in both logs (2026-11-05, test
  homes, by keys, no guitar). For it: the tables have its names (`tools/targets/names`), `rc_call_name_f`, `rc_guest_alloc`, a mod's
  call to `input_sink`, `set_app_focus`'s changed argument, and the game folder as the working directory with mods. Seen in the game, as on the
  Mac: profile autoload, the menu, the song key, the timer, the volume and mute calls, loops, rewind, speed above 100%, the linear
  slider, the loft, headstock, skyline, fretless, inlays, lane markers, the scene dump, enumeration, the focus loss kept from the game, and
  no hotkey in the song search (Shift here, `/` on the Mac) or a text entry dialog. By the log and snapshots, not by ear: dropped previews,
  volume, and audio in the background. Called, without its case coming up: the two-cable check (one cable).
  Not exercised here: the lyrics mod's hiding (the test song has no lyric steps; its key ran), a score screen (a Disconnected song has none: a
  screenshot was taken by a test build, not on that screen), and the input volume, which is Core Audio in both.

## Built in for 1.6.0

Goes with the same section of the [Mac plan](../mac/PLAN.md#built-in-for-160):

- **Every channel in the input list**: under Direct Connect each channel of an audio interface is an input of its own, with the
  game's level bar. The game shows 10 inputs at most and the list doesn't scroll, so they go in priority order
  ([DESIGN.md](DESIGN.md#audio)). `RS_AUDIO_CHANNEL_ENTRIES=0` turns it off.
- **Calibration that can boost**: the level it sets is a gain of ours, up to +24 dB, and it no longer changes the volume of the
  player's interface. A level saved before counts once calibration has run. `RS_AUDIO_INPUT_LEVEL=0` turns it off. The Real Tone
  cable keeps its own path.
- **Guitar input channel and gain**: `guitar-input.ini`, which came after 1.5.1, is in this release too.
- **The installer**: a Learn & Play build stops on a game image that isn't the pinned one, and an image kept from an earlier
  version is made again; the Mac edition decrypts on every run. Its shader messages say what the game does.

## Next work

**Parity (P4):**

- The 30 symbols not proved (the Wwise plug-in DSP, small efd helpers): they have candidates in the map, and a wrong address would be
  worse than the gap.
- The checks the Mac port has had that are left: long sessions, and guitar latency against it.

**Guitar input:** multiplayer with any input, as in the [Mac plan](../mac/PLAN.md#next-work).

**Performance** ([PERFORMANCE.md](PERFORMANCE.md#opportunities-by-expected-payoff)), by expected payoff:
the x87 stack in C locals within blocks of known depth (5-10% of the game thread); the index-range scan of
the draw-time upload; zero-copy dynamic buffers; the relaxed audio kernels; `-mno-outline` for the generated C (4% of the game thread,
15% larger binary; on the Mac build it changes nothing, [Mac performance](../mac/PERFORMANCE.md#build-options)); memory (guest copies of D3D9 buffers).

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
