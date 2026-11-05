# Windows port: status and plan

**Experimental.** It is merged into `main` and goes out first with 1.4.0. The Mac port's plan:
[Mac plan](../mac/PLAN.md).

## Status

Checked by hand on an Apple Silicon Mac (2026-11-01):

- the game boots offline, with the Uplay screens skipped;
- Learn a Song played to the end; Score Attack with notes detected and scored; the Guitarcade games;
- the backing track in time with guitar input, through a Real Tone cable device (a virtual device named
  like the cable) or the microphone;
- the sound of the intro video;
- window and full screen (the in-game Windowed Mode switch was checked from screenshots of a scripted run).

Performance: a song holds the display's rate, 74.9 fps on 75 Hz, with no frame over 33 ms, at 63% of a core against the Mac
port's 44% on the same song and screen ([PERFORMANCE.md](PERFORMANCE.md#final-pass-for-140)). The first play after an install
has about 21 s more of shader stalls up to the highway and four short ones in the song; the second has none.

Checked in the app installed by the installer, started with `open` (2026-11-04, a build of `main` with the memory
ordering, a test home, by keys and snapshots, [TESTING.md](TESTING.md#unattended-runs)): songs to their end and back to back, a
Session Mode session, Nonstop Play and its playlist, a video lesson, the Tone Designer's Edit Tone, the Learn a Song
list, the Multiplayer menus, DIRECT CONNECT with an audio interface listed and chosen, Steam profiles copied in once
(without `crd`), the Metal cache in its own folder, the game's own Exit and the app's Quit (gone in 0.3 s), 74.9 fps on
a 75 Hz display. The Real Tone cable is seen; a guitar through it was not played in these runs. The default output
changed during a song: the render stream reopens on the new device (`audio: the default output is now ...`) and the
sound follows, there and back; the Mac edition stays on the device it opened ([Mac testing](../mac/TESTING.md#game-runs)).
`RS_METAL_RETINA=1` on the built-in screen: a 2940x1912 back buffer and frames of that size, the menu and a song on the
highway at 60 fps, 54% of a core.

Final pass for 1.4.0 (2026-11-04, the app the installer built with the seed, nothing from a capture, started with `open` in
a test home, by keys and snapshots, no guitar): an official song to the note highway; a custom song added during the run
with Enumerate, found and played there, for the first time on an installed app; Refresh Game Files keeps it, and after a
restart it plays from the copy; Quit from inside a song and the game's own Exit, gone in 0.3 s; no signal, no `pipeline:` or
`shader writes to depth` line. All suites of [TESTING.md](TESTING.md) pass on that code.

Enumerate, as on the Mac: SHOP is ENUMERATE, second in the main menu, and a song added during a run is in the list
and plays without a restart ([TESTING.md](TESTING.md#unattended-runs)). The Shop itself never opens offline: the
game checks the Steam overlay first and shows a notice.

Present but not tested: a multiplayer song (the tuner asks for Player 2's cable, and this Mac has one), the Workshop's
other screens, the DLC preview, playing through Direct Connect and other inputs, and the prebuilt harvester
(`win-x86/harvest/rsharvest2.exe`: the capture used so far was made with an earlier build of the same tool).

Built in, as on the Mac: DLC archives mounted in batches within a frame budget and the song-file signature skip
(RSBypass's behaviour; an official song still plays to the highway with it), the song path cache and the
number-table hash (both defects are in this build too; [DESIGN.md](DESIGN.md#built-in-fixes-and-mods)). With all
of them 147 archives mount in 5 frames, 0.27-0.29 s (two runs on 2026-11-04; 13-30 frames, 0.8-2.1 s with only
the batches, in a build without the x86 memory ordering either); of about 56,100 paths, 13,200 came from the
cache, and 34,300 numbers were hashed. The two were not timed apart: `RS_MODS_FIXES=0` stops the batches too.

With the intro skip the title is up about 9 s after the start and took Return by 12 s, in a run pressing Return
every 1.5 s (2026-11-04, one run, warm shader cache, quiet Mac), so the slow title is gone. The 27 s of the first
measurements was the logo video, and the 15-35 s since then was not seen again. The first run after an install, with
its shaders cold, reaches the title about 10 s later ([PERFORMANCE.md](PERFORMANCE.md#final-pass-for-140)).

The system's default output changing reaches the game: Wwise then makes a new sink, and the sound follows, there and
back. Checked through the window script's `audio` step and by switching a real device during a song (2026-11-04). The
Mac edition stays on the device it opened ([Mac testing](../mac/TESTING.md#game-runs)).

Command-Q and the close button quit through the game's own shutdown (`WM_CLOSE`), and losing the keyboard lets go
of held keys and tells the game; checked on 2026-11-04 (the quit ended in `ExitProcess(0)` 0.4-0.5 s after
the key, from the New profile, path and Calibration screens, and in 0.3-0.5 s from inside a song).

Built in with the Mac edition: song archives listed whatever AppID they carry (the install reports every
`dlc/` archive's AppID owned; [Built in for 1.4.0](#built-in-for-140)), and no intro screens.

The "Re-ARMed" title logo, as on the Mac ([INSTALL.md](INSTALL.md#the-title-logo)). Checked on 2026-11-04 in the
installed app: the install code makes `static.psarc.rearmed` (only the two logo textures differ), `title_logo()` links
it, the title shows the tag, and with `RS_REARMED=0` the link goes back and the title shows the game's own logo.

Custom songs: listed and played (the signature skip, the AppID ownership and the song-key table skip;
[Built in for 1.4.0](#built-in-for-140)).

Rocksmith 1 DLC songs: `RS_STEAM_OWNED` reaches the face as on the Mac and `test_steam32` covers it, but no game run
yet: the test account owns none of those DLC. Open: one run with an account that does, or with the variable set.

All of these together, in one build with an install copy made by `gamefiles.py`, were checked in the game on
2026-11-03 ([Built in for 1.4.0](#built-in-for-140)): every pre-release item is done.

The open-file limit goes up at the start, as on the Mac (`RS_MODS_FIXES=0` leaves it). The batched mounting is what
needs it: 441 archives peak at 461 descriptors, over the 256 of an app started from Finder.

Guest memory doesn't grow over songs in a row: 415 MB at the menu, 449 MB in the second song.

Not there yet: mods and hook points,
the loading-screen fix, the OpenGL renderer (no window path), the install-time shader warm-up.

Where it stands against the plan's phases:

| Phase | What | State |
|---|---|---|
| P0 | the Mac build made name-keyed (`tools/targets`), the convention-aware ABI layer, `rc_hook_add_name` | done; the Mac build's generated C stayed byte-identical |
| P1 | the game image | done: unpacked on the Mac from the release's seed; a capture on a Windows PC for a build without one ([DESIGN.md](DESIGN.md#getting-the-game-image)) |
| P2 | the 32-bit front end | done: the game, msvcr100, msvcp100, d3dx9_42, D3DCompiler_42 and bink2w32 translate |
| P3 | boot to the menu | done, and past it: songs play |
| P4 | parity with the Mac port | in progress: audio, Bink, songs, the main modes and the window modes work; the rest below |
| P5 | the installer | done in the shell (`learn-play.sh`) and in the installer app ([TESTING.md](TESTING.md#the-installer-app) says what was driven) |

The game image comes from the user's own exe, unpacked on the Mac with the release's seed; a capture is the fallback
for a build without one ([win-x86/unpack](../../win-x86/unpack/README.md)). Still open: a new exe build needs a loaded
copy of the game for its relocation sites, which nothing reads out of the capture VM yet.

The download needs no paste: the running Steam takes the depot command on its command line, and `game-download`
leads the user through what only they can do, in the same words as the Mac edition's Steam steps.

Exceptions and memory order are done (2026-11-04): every image is compiled with x86's memory order, as the
Mac image always was; a caught C++ exception, an unwind through inner frames and `longjmp` work and are tested on
the real CRT (`crt_probe -eh`); a guest stack has the size asked for and a guard. The Mac edition had all of these
already, or has no need of them.

## Open issues

- **A hidden window runs at about 120 fps**: the full-screen window hides when another app is active, its layer then
  stops pacing presents, and the game runs free up to some limit of about 8.3 ms (not in our code; not found). Measured
  on 2026-11-04 on a 75 Hz display: 74.9 fps in front, 117.6 with Finder active, 74.9 again in front. The Mac edition
  stays at the display's rate there (a display link drives its frames). Harmless but wasteful; a fix is a wait in
  Present while the window is occluded (`windowDidChangeOcclusionState`), and a build to test it.
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
- **First play**: without a warm-up at install, the first start has about 21 s more of stalls up to the highway
  (worst 3.6 s) and four of about 0.32 s in the song; the second play has none
  ([PERFORMANCE.md](PERFORMANCE.md#final-pass-for-140)).
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

## Next work

**Parity (P4):**

- The `tools/targets/win-x86` target filled in: every name in `tools/targets/names` has an entry (`check.py`
  covers both targets), and `target.toml` has the image's `.text` and SHA-256 and, since it moved there,
  the install pins. 957 of the 1,142 symbols and 56 of the 75 offsets are read from the Windows code; 12 of
  the 14 hook points are there (not `input_sink`: events are window messages, nor `set_app_focus`: inlined in
  the platform tick; the map names a place a hook can use for each). The 185 symbols left say why in the map:
  94 are inlined by LTCG (46 of them the Lua API), 32 are not needed here (the Mac's differential tests and
  rebuilt online factory), 22 are Mac-only (CoreAudio, the keyboard stand-in, pthread), 7 have no counterpart
  of the Mac's shape (CRITICAL_SECTION locks, QueryInterfaces nothing calls), and 30 are not proved (the Wwise
  plug-in DSP, small efd helpers): those have candidates in the map, and a wrong address would be worse than
  the gap. 19 offsets are left for the same reasons. 76 overridden functions have `custom` conventions (LTCG
  register arguments), which their overrides have to read by hand.
- With it: the hook points (a mod built for the Mac then runs on both when it uses only hook points,
  `rc_args`/`ov_ret_*` and `RC_OFF`) and the `mods/` folder (the built-in fixes are in, or not needed: the keys and End key
  are the Mac game's own table, thread priorities are `LP_QOS`).
  What it needs on the way: `thiscall` argument access for shared adapter helpers (an `OV_ARGS_CC`), the
  hook point's convention for `rc_args`, and the pointer-width layouts in `mods.c` and `audio_in.c` named per
  target.
- A build-time shader corpus from the Windows image and the PC `static.psarc`, and the install-time warm-up,
  as on the Mac.
- The checks the Mac port has had that are left: a guitar through Direct Connect, long sessions.

**Performance** ([PERFORMANCE.md](PERFORMANCE.md#opportunities-by-expected-payoff)), by expected payoff:
the x87 stack in C locals within blocks of known depth (5-10% of the game thread); the index-range scan of
the draw-time upload; zero-copy dynamic buffers; `-mno-outline` for the generated C (4% of the game thread,
15% larger binary); memory (guest copies of D3D9 buffers).

## Risks

1. **Renderer fidelity**: D3D9 semantics through GL to Metal. The menus, the stages and the note highway
   render; scenes not played yet may show gaps.
2. **Volume of the name matching** for the second binary: bounded by the names the Mac build uses (about
   1,500 table lines, most of them library functions that match by their code).
3. **Steam updating the Windows build**: the pinned manifest keeps installs working; a new build means a new
   seed, new import tables and a new `target.toml`.
4. **The seed**: the release's installer carries it, and a new exe build needs a new one, from a loaded copy of the game
   ([Status](#status)). Without a seed a user needs a Windows PC once.

## Decisions

- Two targets, one source tree; per-game data only in per-target tables; every native reference by name.
- A 32-bit guest in a reserved 4 GB window.
- Microsoft's DLLs translated, not rewritten; native overrides only for leaf functions with plain contracts
  (the math exports so far).
- Direct3D 9 implemented on `libRSOpenGL`, so the Metal core and its caches are shared.
- One pinned Windows build (depot 221683, manifest 8918188356915973501), downloaded by the Mac Steam
  client; no steamcmd.
- No Rosetta, no Homebrew, no .NET in the install: native arm64 and tools macOS ships. The Microsoft DLLs are
  the user's own, from Steam's Common Redistributables or Microsoft's redistributables, checked by SHA-256.
- The game image from the user's own exe, unpacked on the Mac with a seed that holds no game code; for a build without
  one, a one-time capture of the user's own game, with the harvester prebuilt beside its source.
