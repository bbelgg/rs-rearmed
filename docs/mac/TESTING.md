# Testing the Mac port

What this repository can test, how to run it, and what needs tooling that is not in it. The Windows port's
tests: [Windows testing](../windows/TESTING.md).

## Tests in the repository

Most tests are CTest tests of the root CMake build (step 4 of the [manual build](INSTALL.md#build-by-hand),
with the venv's `cmake`, `ninja` and `ctest` first on `PATH` as in its step 2):

```bash
cmake -S . -B build -G Ninja && cmake --build build
ctest --test-dir build --output-on-failure            # or -R <name> for some of them
```

Tests that need a game file skip or are left out when it is not there: the decrypted image
(`cmake -DRS_GAME_PLAIN=<the image>`, no default: its differential tests need Rosetta), the installed game
(`RS_GAME_APP`, `RS_GAME_DIR`), or extracted videos and shader dumps under `analysis/`. Nothing derived from the
game is in the repository.

| Area | Tests | What they check |
|---|---|---|
| Decryption | `steamless.decrypt` (`tools/steamless/test_decrypt.py`) | the key found in a made-up Mach-O, and what is refused; with `RS_DEPOT_GAME` set to a clean Mac depot folder, the real output against the pin |
| Game files | `play-native.gamefiles` (`tools/play-native/test_gamefiles.py`) | the installs' own copy of the game files (`gamefiles.py`) on made-up game folders: what each edition copies, clones and real copies, the space check, resuming, refreshes, the earlier mirror's links, the completeness check |
| Menu overlay | `play-native.cache_overlay` (`tools/play-native/test_cache_overlay.py`) | the menu edits (DIRECT CONNECT, the INPUT slider's fourth value, ENUMERATE, the notice), the psarc and 7z readers and writers on made-up archives, the overlay made, and left out for a cache that has the entries. With the game's `cache.psarc` (`RS_TEST_CACHE`, or the installs' copies) also the real files; skipped without one |
| Built-in mod settings | `play-native.settings` (`tools/play-native/test_settings.py`) | shared with the Windows port: `settings.env`. The launcher's reader (macOS's `/bin/bash`) and Learn & Play's (`win-x86/app/lp_settings.c`, compiled by the test) read the same made-up files and must set and log the same; the launcher's `--env` pairs; the same keys in both and in the installer |
| Measuring tools | `perf.tools` (`tools/perf/test_perf.py`) | shared with the Windows port: the frame-time statistics, the settings file's pins and the report of [measured runs](#measured-runs), on made-up data |
| Release tools | `release.tools` (`tools/release/test_release.py`) | shared with the Windows port: the rules that say what a change calls for (every file of the repository has one), and a release's edits and notes on a made-up tree ([Release checks](#release-checks)) |
| Per-game tables | `targets.check` (`tools/targets/test_targets.py`); also `python3 tools/targets/check.py` | every name resolves in every target or is `unsupported`; no stale entries; native code uses only listed names |
| Hook points and mods | `recomp.hooks` (`make -C tools/ws7/recomp/test/hooks test OUT=DIR`) | shared with the Windows port: one mod dylib in the 64-bit and the 32-bit runtime, for each convention: what a hook reads, changes, sets and calls; adapted calls, refusals, names by run-time lookup, the mods folder; the cost of a point without hooks; the files a mod serves (`test_files`: `rc_file.h` as descriptors, as a `FILE` and in a folder's listing). It also builds `probe_mod.dylib` and `files_mod.dylib` for the game runs |
| Edits and mid points | `recomp.inner64` (`tools/ws7/recomp/test/x64/innercases.py`) | [The recompiler](#the-recompiler) |
| Guitar input | `audio.input` (`shims/coreaudio/test`) | shared with the Windows port: `guitar-input.ini` and the switches, the default file, a number in any locale, a channel across streams, the gain, the clip, the peak and clip counts, on made-up buffers |
| Platform shell | `platform.core.*`, `platform.shell.*`, `platform.overlay.*`, `platform.present.*`, `platform.warm.metal`, `platform.difftest.x86_64` (`shims/platform/test`) | the shell's logic, paths and `Rocksmith.ini`, the renderer decision and fallback (`rsp_renderer_decide`), the overlay API on both backends, Metal presentation, the shader warm-up; the difftest compares rewritten shell functions with the original image's |
| Renderer | `opengl.*` (`shims/opengl/test`) | the GL entry-point table and its clients (`opengl.table.clients`), offscreen contexts on both backends, 43 GL sequences run on GL and Metal with their pixels compared (`opengl.metal.*`), every game import with `RS_GL_CHECK=1` (`opengl.metal.coverage`), the Metal disk cache (`opengl.metal.cache`) |
| Shader translator | `msl.unit`, `msl.verify.builtin`, `msl.verify.corpus` | GLSL to MSL; `msl_verify` runs programs on offscreen GL and Metal with the same inputs and compares pixels, depth and vertex outputs (the corpus test needs shader dumps) |
| Cg | `cg.*` (`shims/cg/tests`) | exports, ARB, GLSL and Metal rendering, the shader corpus (load, parameters, GLSL dump, render diff against the ARB path), a stress test in the game's call order |
| Bink | `bink.exports`, `bink.decode.*`, `bink.realtime` (`shims/bink/test`) | the exports against the game's imports; decoding and real-time playback of game videos (need extracted videos) |
| Game libraries | `game.offline.no_network`, `game.difftest.x86_64`, `game.native.arm64` (`shims/game/test`) | the online stub and crypto import nothing that talks to a network; the rewritten crypto, efd, hash and Lua functions against the original x86-64 code (under Rosetta, which writes a golden file), then the arm64 build against that file |
| libstdc++ | `libstdcxx.*` | the system library provides the GNU ABI the game needs |
| Shims | `shim.<name>.macho` | each built shim has the expected architectures and install name (`tools/check-shim.sh`) |

`win-x86.install` and `win-x86.shaders` are in the same CTest run: [Windows testing](../windows/TESTING.md#tests-in-the-repository).

Outside CTest:

- **Steam layer**: `make -C shims/steam check` (exports against the game's imports, both slices) and
  `make -C shims/steam test` (vtable slots, the ABI tests and Enumerate's copying, `tests/test_enumerate.cpp`;
  arm64 and x86_64, the x86_64 runs need Rosetta).
- **Network guard**: `tools/ws8/netguard/build.sh --test` builds `netguard_test` and runs it on both
  architectures ([Offline checks](#offline-checks)).

### The recompiler

The recompiler's own tests are in [its README](../recompiler/README.md#tests); the 32-bit front end's
differential test is in [Windows testing](../windows/TESTING.md#the-recompiler). For this edition:

- **Edits and mid points** (`recomp.inner64`, or `python -m test.x64.innercases --out DIR` in `tools/ws7/recomp`):
  the 64-bit front end's edits and mid points (`xlat/inner.py`, `rc_mid.h`) on a made-up x86-64 Mach-O: edited
  code against Unicorn with the switches off and on, mid point hooks against answers, bad tables refused. It
  skips without Capstone and Unicorn. Learn & Play's counterpart is `test/x86/innercases.py`.
- **App build**: `tools/ws7/recomp/app/build-m4.sh` starts the new app once with `--init-only` (maps the image,
  binds the shims, then exits) and fails if that does not print its `init-only:` line.
  `tools/ws7/recomp/build/build.sh` links a smoke test of the translated image and checks zlib's
  `adler32`/`crc32` against the system's (`RC_SMOKE=0` skips it; the default with `RC_LTO`).
- **Seam unit test**: `tools/ws7/recomp/app/test/build.sh OUTDIR` (or `RC_UNIT_TEST=1` for `app/build.sh`): the
  engine seam without the game.
- **Translation**: the generated C of the whole image is the same across runs and scheduling (`--jobs`,
  `--batch`); compare two `gen/` directories with `diff -r`.

## Installer tests

The installer app has no automated test; `steamless.decrypt`, `play-native.gamefiles`, `play-native.settings`
and `play-native.cache_overlay` cover what it runs. The app is checked by hand, end to end, with a test home:

```bash
HOME=/path/to/test-home "tools/installer/RS Re-ARMed Installer.app/Contents/MacOS/RS Re-ARMed Installer"
```

Its build folder, logs, the launcher and `~/Library/Application Support/RS Re-ARMed` then go under the test
home. Its preferences do not: the installer keeps its choices (game, build folder, renderer) in the real user's
defaults (`UserDefaults`), so a test run changes what the next real run starts with.

- **Cases**: a fresh build and install, Skip during Preparing shaders, an update after a simulated renderer
  fallback (the popup shows OpenGL), quitting mid-build, the download of a release's source, and the Built-in
  Mods sheet (the switches a `settings.env` has shown unchecked, Save writing the file into the test home, Turn
  All On and Save removing it).
- **A whole first install**, for a home of its own: link `<home>/Library/Application Support/Steam/steamapps/
  common/Rocksmith2014` to the clean depot, start the app with `-Game mac` from outside the repository (it takes
  the current folder's repository if there is one), and click Build & Install: the only click until the run
  ends, about 20 minutes, with `Rocksmith2014_x64_decrypted` matching the pin.
- **The window as a run leaves it**, with nothing run: `RS_INSTALLER_PREVIEW=installed`, `failed` or `update` in
  the app's environment shows the box of a finished install (Start the Game, Show in Finder), of a failed step
  (Try Again, Show Log File), or the title row's button for a newer release. Close (Escape) is at the bottom
  right once a run has ended, with Build Again beside it. This window (not released) has been looked at in the
  three previews, not yet after a real run.
- **The standalone installer's check of its download**: `tools/installer/build.sh` prints `source digest:` and
  builds it in, and the app logs `The source is the one this installer was made from` before it runs anything.
  An archive of another commit in `RS_SOURCE_URL` must stop it at Downloading source. Not yet tried through the
  app.
- **A test home on another disk**: its `logs` folder has to be a link to the startup disk (the launcher's
  `open --stdout` there fails with -10810), and each new build asks for the removable volume once per app, the
  launcher and the game. Someone has to click Allow; until then the process stands still in `open`, with no
  window.
- **`tools/play-native/install.sh`** takes `RS_NATIVE_HOME` and `RS_NATIVE_LAUNCHER` for a test install that
  leaves the real one alone; the launcher's `RS_NATIVE_SEED=0` skips copying the Steam saves (for a prepared
  `steam/` folder). The test install gets its own copy of the game files from `RS_GAME_DIR` (clones when the
  test folder is on the game's volume; a real copy of the whole game and its DLC otherwise, so check the free
  space first). To prove a run reads no Steam folder, start the game with `HOME` set to a folder without one:
  the copy, `.rearmed/steam.env` and the `steam/` saves are all it needs.

### The installer app

One app installs both games. How each of its screens is driven, its launch options, and the stand-ins for Steam
and Apple's tools that show every message of the box above the log:
[Windows testing](../windows/TESTING.md#the-installer-app).

## Game runs

The game is tested in a **separate test install** (`RS_NATIVE_HOME`, `RS_NATIVE_LAUNCHER`), never the user's,
with a test profile, and on both renderers (`RS_GL_BACKEND=gl` for OpenGL). Keep a copy of the test home's
`steam/`: a run that goes astray can change or delete the profile. Before reporting a game bug, check whether it
also happens in the original game under Rosetta.

Use the original `cache.psarc` in a test copy: with a fast-load one a start can stay on the loading screen for
minutes ([PLAN.md](PLAN.md#open-issues)), its short intro hides intro problems, and the overlay changes nothing
in a cache that has the entries.

### Unattended runs

The input injector, the virtual audio cable (a recording played into the Real Tone input) and the scripts and
reference screenshots of the first unattended runs are from the private project RS Re-ARMed was split from and
are **not in this repository**. What the repository has:

- **`tools/ws8/keyfifo`**: keys for a running game from a FIFO, in either edition. Build it with
  `tools/ws8/keyfifo/build.sh OUTDIR`, start the game's executable with `RS_KEY_FIFO=FILE` and
  `DYLD_INSERT_LIBRARIES=<home>/libnetguard.dylib:OUTDIR/libkeyfifo.dylib`, then write a command a line to
  `FILE`: `key NAME [MS]` (`return esc delete fwddelete tab space up down left right shift ctrl alt cmd`, a
  letter or digit, or a hex key code), `text STRING [MS]` (1.5 s between letters by default, which the song
  search needs), `click X Y`, `front` (the game's app and window in front) and `quit` (the app menu's Quit, which
  ends the game in under a second, from inside a song too). Keys go into the game's own event queue, so nothing
  else on the Mac gets them.
- **Starting it**: export the variables and start the executable itself, with the launcher's environment
  (`tools/play-native/launch.sh`: `RS_STEAM_MODE=stub`, `RS_STEAM_DATA_DIR`, `RS_NATIVE_HOME`). Through `env`
  or `nohup` macOS drops every `DYLD_` variable, so neither the guard nor keyfifo loads. Check that
  `netguard.log` says `mode=block` before going on. A launcher in a test home on an external drive stops at
  macOS's prompt for that drive; the executable started directly does not.
- **The focus**: a key reaches the window only while the game has the focus, and any other app coming forward
  loses it quietly: send `front` before keys. With `RS_KEY_DIRECT=1` keys arrive in front or not. Started from a
  shell the game never has the focus until `front`.
- **`tools/perf/run.py CONFIG OUT --route-only`** ([Measured runs](#measured-runs)): the whole route to the note
  highway and out, each key waiting for its screen.
- **Seeing a run**: snapshots (`RS_GL_SNAPSHOT_DIR=DIR`, `RS_GL_SNAPSHOT_SECS=N`; each is a readback of about
  70 ms, so judge smoothness without them), frame times (`RS_GL_FRAMETIMES=FILE`; keep the window in front for
  anything measured) and the logs of [Diagnostics](#diagnostics). The rearmed-mods library
  (`rearmed-mods.dylib` in `<home>/mods/`, `RSMods.ini` in the game folder, `RS_MODS_LOG=1`) logs a `menu:` line
  for each screen, which is what a script should wait for.
- **The installed app by `open`**: the launcher hands the game only what it lists, so a test copy of its
  `Contents/Resources/launch.sh` adds keyfifo, `HOME`, `CFFIXED_USER_HOME` and any other variable to `open`'s
  `--env`s; without `HOME` the game writes to the real `~/Library/Caches`.

**The screens by key**, from the start to the note highway (the `menu:` name in brackets). Times are from test
runs: look at a snapshot, or wait for the `menu:` line, rather than count on them.

| Screen | Keys | Notes |
|---|---|---|
| Title (TitleScreen) | Return | up about 8 s after the start with the intro skip, about 31 s without. Give it 6-8 s after it appears: a Return in the fade-in is lost, and Esc there opens EXIT GAME |
| Profile list (ProfileSelect) | Return | `fwddelete` here deletes the profile. With no profile, Return starts the first-time screens (Down to New profile, Return, a name, Return, then "Have you played Rocksmith before?"). With rearmed-mods' `ForceProfileLoad` the game goes to the main menu by itself |
| Main menu (MainMenu) | Return on LEARN A SONG | up about 25 s after the profile with a few DLC archives, about 85 s with 2,400; no key is taken while the DLC is enumerated, and a Return sooner is lost. The list: LEARN A SONG, ENUMERATE, SESSION MODE, NONSTOP PLAY, LESSONS, GUITARCADE, MULTIPLAYER, TONE DESIGNER, UPLAY |
| Song list (LearnASong_Songs) | `/`, the letters, Down, Return | `/` opens the search (the Mac binding; Shift is the Windows one), Tab opens SORT BY. Letters 1.5 s apart; a key down and up in one frame is lost for text. Return with no match leaves the box open |
| Song page (LearnASong_SongOptions) | Return on Play Song | |
| Tuner (LearnASong_PreSongTuner) | `fwddelete`, then Return | up 15-30 s after Play Song. `fwddelete` is Skip Tuner, Return answers "Yes, continue without tuning" about 5 s later. Return on the tuner itself starts Calibration, which waits for a guitar (Esc cancels) |
| "TUNE UP!" (SelectionListDialog) | Return | instead of the tuner on a profile with no input (DISCONNECTED) |
| Note highway (LearnASong_Game) | | 20-40 s after the tuner, after the venue's intro |

- **Path/Input** (Ctrl on the main menu or the song list): Down to INPUT, Right and Left step through REAL TONE
  CABLE, MICROPHONE, DISCONNECTED and DIRECT CONNECT; on DIRECT CONNECT, Down and Return list the inputs and
  Return takes one, shown as CURRENT INPUT. Right twice on PATH moves Lead Guitar to Bass (Left twice back, or
  the profile stays on Bass). Esc goes back. A change reaches the profile only when the game saves it: leave by
  the menus, as a kill loses it.
- **Without a guitar**: on REAL TONE CABLE or DIRECT CONNECT with nothing connected the tuner stops at "It
  sounds like your cable is unplugged", so set DISCONNECTED first (from REAL TONE CABLE: Ctrl, Down, Right twice,
  Esc; a kill loses it, so each such run sets it). DISCONNECTED
  plays songs but greys out Session Mode, Lessons, Guitarcade, Multiplayer and the Tone Designer. Exactly one
  device named "Rocksmith USB Guitar Adapter" (a Loopback device of that name works) gives REAL TONE CABLE; two
  give "Two Rocksmith Real Tone Cables have been detected" at the tuner.
- **In a song**: Esc pauses (LearnASong_Pause): RESUME, RESTART, TUNER, MIXER, CHORDS, EXIT; Bass has no
  CHORDS. The menu keeps its cursor, so press Up six times before counting Down, keys 0.6 s apart, and look
  before Return: a key is lost now and then. CHORDS lists only the named chords met so far; Esc goes back to the
  pause menu and a second Esc resumes. Space in the pause menu is Riff Repeater: Down twice to SPEED, Left once a
  second, `key delete` resumes. EXIT goes to the song's page; Esc from there is the song list with the search
  still in it (`/`, Delete ten times, then the new letters).
- **The other modes**: LESSONS: Down to a lesson, Return, Return to start it; Esc gives RESUME, EXIT LESSON.
  SESSION MODE: Space on the band screen starts it, after the tuner. NONSTOP PLAY: Return on START NONSTOP PLAY,
  the tuner, Delete, Return. GUITARCADE: Return on TECHNIQUE GAMES, a game, Return, Return; a game's tuner has no
  Skip Tuner, so only a tuned instrument gets past it and the games are a check by hand. MULTIPLAYER:
  [By hand](#by-hand-and-with-computer-use).
- **Tools** (Space on the main menu): OPTIONS is Down three times; [Measured runs](#measured-runs) has the way
  to Venue Mode.
- **Quitting**: Esc on the main menu, Return on "Are you sure you want to quit?" goes to the title; Esc there
  and Return on EXIT GAME? is a normal quit, which saves the profile.

**Checks that have a route.** Learn & Play's counterparts are in
[Windows testing](../windows/TESTING.md#unattended-runs).

- **No intro screens** (`RS_MODS_INTRO`): boot to the title several times, with the original `cache.psarc`.
  Snapshots show white, about three seconds of black, then the title, and `RS_MODS_LOG=1` logs `intro: skipped`;
  with `RS_MODS_INTRO=0` the Ubisoft logo and the legal screens must still show. The failure to look for is the
  loft fading in with no title on it. Switched off through `settings.env`, the log starts with `[settings]
  RS_MODS_INTRO=0` and the mods say `intro skip off`; an `RS_NETGUARD=log` line in that file is logged as
  ignored.
- **Auto Save warning** (`RS_MODS_AUTOSAVE`): the main menu comes up after the profile with no dialog, and the
  log has `auto save warning skipped`; with `RS_MODS_AUTOSAVE=0` the RS2AutosaveWarning dialog comes back. The
  skip must keep the game's input mode step: a DIRECT CONNECT profile with an interface connected must reach the
  tuner, not "It sounds like your cable is unplugged". Not checked: a Real Tone cable or Microphone profile, and
  a cached mode to restore.
- **Direct Connect overlay**: with `game/Update/patcharchives.psarc` made by `cache_overlay.py`, INPUT offers
  DIRECT CONNECT as a fourth value, and the log has `cache overlay: cache7.7z from Update/patcharchives.psarc`.
  Without screen control, change the title's `pressstart` label in the overlay's copy of
  `ui_menu_pillar_startup` and look at a snapshot.
- **Enumerate**: take a custom song out of the test install's copy, point `source` and `dlc_from` in its
  `.rearmed/source.json` at folders of your own, and put the song in one of them once the main menu is up. Down
  to ENUMERATE, Return: the notice "Looking for new songs…"; Return closes it, Up, Return on LEARN A SONG, then
  search for the song and play it, with no restart. With `RS_STEAM_LOG=1` the log has `enumerate: pressed` once,
  `1 new archive(s) in the copy` and `dispatch callback 1005`. Use a song with an AppID the game's list lacks.
- **Song ownership** (songs listed whatever AppID they carry): re-pack an official archive with another
  `appid.appid` (`tools/play-native/cache_overlay.py`'s `Psarc` and `write_psarc`; they find the game's key in a
  game folder or a copy, else set `RS_PSARC_KEY_IMAGE` to the game's program), put it in the `dlc/` of the test
  install's game source and refresh the copy: `steam.env`'s `RS_STEAM_DLC` gains the AppID and a search finds
  the song; with the list without it, the search does not.
- **Rocksmith 1 songs** (`RS_STEAM_OWNED`): on a clone of the copy (`cp -cR`), with the Steam folder copied
  without `crd` and `RS_STEAM_OWNED` set to the AppIDs of `gamefiles.rs1_dlc_apps`, a search for "countd" finds
  The Final Countdown, and it plays; unset, it is not listed. `gamefiles.ask_steam` has been checked against a
  running Steam only for an account that owns none of them.
- **Files a mod serves**: `DIR/files_mod.dylib` of `tools/ws7/recomp/test/hooks` in the mods folder serves every
  `.psarc` the game opens through the file provider (`rc_file.h`) and logs `files_mod: serving NAME` for each.
  One `tools/perf/run.py CONFIG OUT --route-only` run with it (the config's `mods` a folder with it beside
  rearmed-mods) reads the song list, the song and its audio that way: a served file is a descriptor on
  `/dev/null`, so a read past the provider would get nothing.
- **Mods**: `DIR/probe_mod.dylib` of the same folder in `<home>/mods/`: `[probe]` lines in the log, `[probe] file
  sha256` the file that ran. Its numbers are the ones to compare with Learn & Play's.
- **The rearmed-mods library**: hotkeys must stay quiet in the song search and fire again after Esc: `/`, then
  `i`, `l`, `5`, `f`; `/`, `qq`, Return, `i`; a click, `i`. The same dylib runs the same route in Learn & Play:
  compare the two logs.
- **7-string charts** (rearmed-mods' Seven-String Guitar; `SevenString = on` and `PlainPackages = on` in
  `RSMods.ini`, the chart as a Mac archive in `dlc/`): the log starts with `[rc] edits seven_string: on (N)`,
  `[rc] edits plain_packages: on (N)` and `seven-string: on, N mid points`; with a table `unsupported` it says
  which part is missing and that the guitar keeps six strings. A chart logs its RS7X block (`chords: SNG ...:
  RS7X v1 block`), the guitar rebuilt with 7 strings and the seven-peg headstock, and leaving it the song file
  freed, 6 strings and `back to the stock headstock`. The pause menu's TUNER shows seven pegs and B STANDARD
  7STR. Play 100 s to see the chords on the low B, then a six-string song and a bass arrangement.
- **Decoded song audio** (`ak_src_submit`): a mod on the hook point that writes each stereo block with its file
  ID and position; 30 s of a song, then rearmed-mods' rewind (`z`, `AllowRewind = on`). The blocks should match
  vgmstream's decode of the song's `.wem` to about -90 dB.
- **Sharper picture and screen shape**: the log has `MSAA 4x on the game's screen-sized targets`. Drag an edge
  of the window and start a song: the picture and menus stay undistorted, and rearmed-mods logs the new
  frustum. Not run: a 16:10 or ultrawide screen in full screen.
- **The render API in a game**: `shims/opengl/test/render_mod.c` turns the hues of the notes' two ramps once a
  second. In a folder with `rearmed-mods.dylib` and `RS_MODS_PATH` set to it, snapshots of the highway show the
  notes change color and nothing else. The log has `[render_mod] on mac-x64, API 1` and two `[rs-gl] render: a
  rule found its texture` lines.
- **Riff Repeater's speed and the focus** (`rr_speed_ratio` and `ui_event` with a mod): each Left on SPEED is 1
  off the 100, and the game calls the point once (Learn & Play calls it on every step). For `ui_event` the game
  must have had the focus: `front`, then another app in front (`open -a Finder`).

### Measured runs

`tools/perf/run.py` runs either edition the same way every time and measures the same part of the same song, so
that two builds, or the two editions, can be compared. The keys go through `tools/ws8/keyfifo`, and each waits
for the screen it belongs to, by the `menu:` and `song:` lines that
[rearmed-mods](https://github.com/glebb/rearmed-mods) logs with `RS_MODS_LOG=1`. A screen it doesn't know stops
the run.

```bash
tools/perf/run.py CONFIG OUT                         # a warm-up and three measured runs of each edition, in turn
tools/perf/run.py CONFIG OUT --editions mac --rounds 1 --no-warmup
tools/perf/run.py CONFIG OUT --route-only            # to the note highway and out, with snapshots
tools/perf/run.py CONFIG OUT --profile-keys bench    # once: the profile's settings (below)
tools/perf/run.py CONFIG OUT --trace 30              # a Time Profiler trace instead of numbers
tools/perf/report.py OUT [--against OTHER_OUT]       # the table, and the change from another set of runs
tools/perf/profile.py OUT/<edition>-trace/game.trace [--under NAME]
```

`CONFIG` is a JSON file of your own test installs (`tools/perf/example.json`): for each edition the executable,
its folder, its `Rocksmith.ini`, its mods and its network guard, and for both the profile folder, the mods'
`RSMods.ini`, a `guitar-input.ini`, switches and the helper processes to count. Test installs only: a run writes
`Rocksmith.ini`, and refuses the installed game's folder.

What is the same in every run, in both editions:

- **The Mac**: on mains power, Low Power Mode off, no heat limit, a load under 3, no other game and no build, the
  built-in screen as the main display. A run refuses otherwise (`--force` runs and says so in its result), and
  waits between runs until the load is down. Leave the Mac alone during a run: the game pauses when another app
  takes the focus (`menu: LearnASong_Pause`), and such a run is left out of the report.
- **The settings file**: full screen at the screen's size in points, `VisualQuality=2` (High) with post effects,
  shadows, the high-res scope, depth of field and per-pixel lighting on, `MsaaSamples=4`, `LatencyBuffer=1`,
  `EnableMicrophone=0`. Full screen is `Fullscreen=2` in this edition's file and `1` in Learn & Play's.
- **The switches**: a clean environment with `RS_METAL_MSAA=4`, `RS_METAL_ANISO=16`, `RS_METAL_UPSCALE=1`,
  `RS_MODS_LOG=1` and what `CONFIG` adds. `RS_NETGUARD` and `RS_STEAM_KEEP_CREDENTIALS` are refused, and no key
  is sent before the guard's log says `mode=block`.
- **The profile**: a copy of one folder for every run, so each starts from the same progress. Its settings are
  made once, with `--profile-keys`: the keys from the main menu, then out to the title, which saves it;
  `OUT/<edition>-profile/steam` is then the next `"profile"`. `bench` is INPUT to DIRECT CONNECT (Ctrl, Down,
  Right four times) and Venue Mode on (Space for Tools, Down three times to OPTIONS, Return, Down three times to
  ADV DISPLAY SETTINGS, Return, Down three times to VENUE MODE, Return opens it, Return ticks it). Such a
  setting is a tick box that Return turns over, so start from a profile where it is off and look at the run's
  snapshots; a menu keeps its cursor, so visit each once. The other two that count: Tools, DIFFICULTY SETTINGS,
  OVERRIDE TO MAX, and OPTIONS, PLAY SETTINGS, MASTER MODE.
- **The mods**: the same libraries and the same `RSMods.ini` for both; the report lists what differed.
- **The song**: Cherub Rock by the search (`/` here, Shift in Learn & Play, then `cher` and Down until the log
  says `song: CherubRock`), Play Song, Skip Tuner and its "continue". The window is 150 s from 45 s after
  `menu: LearnASong_Game`. Cherub Rock is DLC in this edition: its archive has to be in the test home's `dlc/`.
  With a few thousand archives there the main menu takes no key for about 20 s after it comes up, so the first
  key there is pressed until the screen changes.

What is measured over the window, into `OUT/<edition>-<n>/result.json` with what the run was (the commit, the
executable's and the mods' hashes, the settings, the display): when frames went on the screen
(`RS_GL_FRAMETIMES`' `P` events, from Metal: intervals, late frames, refreshes without a new one), when the game
handed them over (`F`: rate, median, p95, p99, worst; this moves with the game's work and is not what is seen),
this edition's game frame, a command buffer's time on the GPU (`U`), the CPU of the game, of each thread
(`libproc`, no sampler) and of the helpers, the memory footprint and its growth, and the GPU's utilisation
(`ioreg`, the whole system's).

At a fixed frame rate the GPU's utilisation and start-to-end time are not its work: it slows its clock to fill
the refresh. For what it can do, run with `"RS_FPS": "300"` in `CONFIG`'s `env` (the Mac edition), one run at a
time on a cool Mac. A `--trace` run records a Time Profiler trace (`xcrun xctrace`) from the settle on, which
`profile.py` adds up by thread and function; it gives no numbers, as the sampler slows the game.

Results are in [PERFORMANCE.md](PERFORMANCE.md). Not looked at yet: that the venue shows with no guitar played,
and a run on an external display.

### Shaders of a run

`RS_GL_DUMP_SHADERS=DIR` writes the GLSL a run links, `RS_MSL_LOG=1` logs each program translated at run time
because the corpus doesn't have it, and `RS_METAL_CACHE_STATS=1` prints the compile times at exit. With Metal,
pipelines are built the first time they are used, so the first play of a song, venue or mode after an install has
a few 0.3-0.8 s stalls: measure frame times on a second play. For a first play with no shader cache that leaves
the user's alone, see [Learn & Play's notes](../windows/TESTING.md#shaders-of-a-run).

### By hand and with computer use

- **By hand**: menus, a song, Session Mode, lessons, Guitarcade, full screen and back, quitting; on both
  renderers. The keys are in [INSTALL.md](INSTALL.md#playing).
- **An interface's channel and gain**: `RS_AUDIO_LOG=1` with a `guitar-input.ini` (`RS_AUDIO_INPUT_FILE` for a
  test one). INPUT to DIRECT CONNECT, the interface from the list, the calibration, then play: the log has
  `listening to channel N of M` for each entry taken, `input level:` for each level set, and a peak every five
  seconds while the game listens. If only a device's first channel is heard, its I/O isn't started
  ([DESIGN.md](DESIGN.md#built-in-fixes-and-mods)). An aggregate device (Audio MIDI Setup) makes a device of
  many channels from what is connected; the choice must come back after the game's own Exit and a new start.
- **Two players**: load the second profile on its own, take a channel for it on Path/Input (`HX Stomp channel
  6`, say), and leave by the menus so it is saved; the first profile the same way on another channel.
  MULTIPLAYER, Return on any notice, Return on the second profile, Return on CONTINUE, a mode and song, Play
  Song: a split-screen song with each player on their channel. SWAP CABLE ASSIGNMENT is lit with both on Direct
  Connect, and swaps the channels. A second player with no input stops the song at "It sounds like Player 2's
  cable is unplugged".
- **Fallback**: two forced kills (`kill -9`) of Metal starts in a row; the next start must use OpenGL and write
  `opengl` into `renderer` and `renderer-fallback.txt`; a clean quit removes `metal-session`. To take a test
  install back, write `metal` into its `renderer` and delete `renderer-fallback.txt`.
- **Full screen on a second display**: the title is enough. Move the window with System Events (`set position of
  window 1` of the process with the game's pid), bring the game in front (`set frontmost`), then `keystroke "f"
  using {command down, shift down}`: computer use's background drag and keys don't reach the game. Read the
  window's frame from `CGWindowListCopyWindowInfo` and the render size from a snapshot. Full screen is the
  display's frame at window level 25 and a snapshot of the display's size; with another app in front the window
  hides and keeps that frame; Shift-Command-F again gives a 1280x720 window on the same display.
- **The output device**: change the default output in Sound settings during a song: the sound moves, and the log
  has `audio: the default output is now ...`. Unplugging the output device moves the output to another;
  replugging reopens the input only.

### The capture VM

Not applicable: the Mac edition's game is not packed. See
[the Windows port's notes](../windows/TESTING.md#the-capture-vm).

## Diagnostics

Logs are in `<home>/logs/`: `last-run.log`, `previous-run.log`, `netguard.log`, `seed.log`. Set switches for
the launcher with `launchctl setenv` ([INSTALL.md](INSTALL.md#settings-and-switches)). All off unless set.

| Switch | What |
|---|---|
| `RS_MODS_LOG=1` | what the built-in fixes apply |
| `RS_AUDIO_LOG=1` | device buffers, the real-time thread, dropped input, each level set, each input's channel and its peak and clips every few seconds |
| `RS_STEAM_LOG=1` | the offline Steam layer's calls (`[steam-shim]`) |
| `RS_FILE_LOG=1` | each open a mod's file provider served, and each folder listing it added names to (`[rc] file:`) |
| `RC_HOOK_TRACE=NAME,NAME` | the raw calls of those hook points, the first 8 of each, refused ones too |
| `RS_NETGUARD_BT=1` | the backtrace of each call the network guard refuses, in `netguard.log` |
| `RS_GL_LOG`, `RS_GL_CHECK`, `RS_GL_STATS`, `RS_GL_VERBOSE=1` | GL calls, errors after each call, counts, the load and every first error |
| `RS_METAL_PKEY_CHECK=1` | every draw that keeps its pipeline key builds it too: `pipeline key check: the kept key is stale` when they differ (and the built one is used), a count every 100,000 draws |
| `RS_GL_TRACE_DIR=DIR`, `RS_GL_TRACE_START=S`, `RS_GL_TRACE_KEY=1\|PATH`, `RS_GL_TRACE_FRAMES=N` | a trace of N frames (default 30), from the second frame, S seconds after the first, or when `DIR/go` or PATH appears |
| `RS_MSL_LOG=1`, `RS_MTL_DUMP_MSL`, `RS_METAL_CACHE_STATS=1` | shaders translated at run time, the MSL of slow compiles, the compile times at exit |
| `RS_GL_DUMP_SHADERS=DIR` | the GLSL the game compiles |
| `RS_CG_LOG=1`, `RS_BINK_DEBUG=1` | the Cg shim's and the Bink shim's own logs |
| `RS_GL_TEX_LOG=FILE`, `RS_GL_TEX_DUMP=DIR`, `RS_GL_TEX_MARK=HASH,HASH` | Metal: each new texture content with its hash, RSMods' checksum, size and format, and in `FILE.draws` each kind of draw (program, mode, count, vertex stride, the textures of units 0-3); each content saved to DIR; textures of those hashes drawn as solid magenta, to see what uses them |
| `RS_GL_SNAPSHOT_DIR=DIR`, `RS_GL_SNAPSHOT_SECS=N` | a PNG of the frame every N seconds |
| `RS_GL_FRAMETIMES=FILE` | frame times, a line per frame |
| `RS_METAL_PASS_TIMES=FILE` | Metal: the GPU's time in each render pass of a frame, by its place in the frame, from every eighth frame; with the present, and "between" for the upscaler. The file is rewritten about every 8 s. Read the shares: at a fixed frame rate the GPU's clock follows the load |
| `--renderer-check` | the renderer a start would use, without starting the game |
| `RS_RENDERER_TEST_NO_METAL=1` | act as if Metal could not start |
| `RS_METAL_SAFE_LOAD=0\|1`, `RS_METAL_CORPUS_WARM=0\|1`, `RS_METAL_CORPUS_LIMIT=N` | force safe load, the corpus warm, or warm only part of the corpus |
| `RS_METAL_CACHE=0`, `RS_MTL_COMPILE_THREADS=N`, `RS_METAL_DRAWABLES=2\|3` | no disk cache, the compile pool's size, the drawable count |
| `RS_METAL_MSAA=1\|2\|4\|8`, `RS_METAL_ANISO=1-16` | MSAA on the game's screen-sized targets with depth (1 off), the least anisotropy of mipmapped textures (1: the game's) |
| `RS_METAL_UPSCALE=0`, `RS_METAL_COLORS=vivid` | no MetalFX upscaling on a Retina screen (the log has `upscaling WxH to WxH with MetalFX` when it runs); the layer left untagged (not sRGB) |
| `RS_FPS=N`, `RS_NO_VSYNC=1\|0`, `RS_NO_VSYNC_AFTER=S`, `RS_RENDER_THREAD=0` | a fixed frame rate; swap interval 0 or not, from S seconds on (default 30); the original's frame in the display link's callback |
| `RS_LUA_TRACE=1` | Lua chunk loads, failed calls and errors |
| `RC_LOG_UNRESOLVED=1`, `RC_TRACE_INITS=1` | every unresolved bind, each initializer before it runs |

## Offline checks

The game must never call home.

- `game.offline.no_network` (CTest): the online stub and the crypto import no network function.
- `tools/ws8/netguard/build.sh --test`: the guard refuses non-local connections (a cut address too), host-name
  lookups (`gethostbyname`, `getaddrinfo`, `getipnodebyname`), reverse lookups of an address that is not this
  machine's, and URL opens (any scheme, the `open` command); loopback, `localhost` and a plain command pass.
- In a game run, `logs/netguard.log` starts with `mode=block` and lists every refused attempt; a run from the
  title to a song should add nothing unexpected. The launcher refuses to start without `libnetguard.dylib`, and
  the game's app started directly starts again with the install's guard inserted (not run in the game yet: check
  `netguard.log` after opening `game/Rocksmith2014.app` from the Finder).
- The Steam layer runs in stub mode (`RS_STEAM_MODE=stub` in the launcher): it never loads Valve's library.
  `crd` is never copied into `steam/` and is deleted from a copy before each launch.

## Release checks

A release runs the suites and the game runs its changes call for, not all of them: `tools/release/scope.py` maps
the files changed since the last release to editions, suites and runs, and `tools/release/check.py` runs those
suites, several at a time, with a line each ([Making a release](../release/README.md)). The suites are the ones
in [Tests in the repository](#tests-in-the-repository); a new suite gets a line in `scope.py`'s table and a rule
for the paths it covers. The tools' own test is `release.tools` (`tools/release/test_release.py`).

The Mac edition's run for a release is `tools/release/smoke.sh --mac`, which takes its route from
`RS_SMOKE_MAC_CMD` since the injector is not in this repository ([Game runs](#game-runs)): a command that plays
to the note highway and exits 0 (it gets `RS_SMOKE_OUT`, the folder for its log and snapshots). Without one, play
it by hand to the highway and say so in the release commit.
