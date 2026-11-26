# Testing the Mac port

What this repository can test, how to run it, and how the game is run unattended and by hand. Learn & Play's
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
| Game files | `play-native.gamefiles` (`tools/play-native/test_gamefiles.py`) | shared with Learn & Play: the installs' own copy of the game files (`gamefiles.py`) on made-up game folders: what each edition copies, clones and real copies, the space check, resuming, refreshes, the earlier mirror's links, the completeness check; the Mac game's folder (chosen, installed in Steam, Steam's download), the download of the Mac depot with a Steam the test plays, the copy and the refresh of a downloaded game; the first Rocksmith's DLC found as owned, from each place a copy looks and with a stand-in for Valve's library (needs a C compiler), and what the copy says about them |
| Menu overlay | `play-native.cache_overlay` (`tools/play-native/test_cache_overlay.py`) | shared with Learn & Play: the menu edits (DIRECT CONNECT, the INPUT slider's fourth value, ENUMERATE, the notice, the lists' scroll bars in cache.psarc and static.psarc, and the bar of a list that had none, run in a small ActionScript interpreter), the psarc and 7z readers and writers on made-up archives, the overlay made, and left out for a cache that has the entries. With the game's `cache.psarc` (`RS_TEST_CACHE`, or the installs' copies) also the real files; skipped without one |
| Built-in mod settings | `play-native.settings` (`tools/play-native/test_settings.py`) | shared with Learn & Play: `settings.env`, `options.env` and the copy's `steam.env`. The launchers' readers (macOS's `/bin/bash`) and Learn & Play's (`win-x86/app/lp_settings.c`, compiled by the test) read the same made-up files and must set and log the same; the launcher's `--env` pairs; the same keys in both and in the installer |
| Measuring tools | `perf.tools` (`tools/perf/test_perf.py`) | shared with Learn & Play: the frame-time statistics, the settings file's pins and the report of [measured runs](#measured-runs), on made-up data |
| Installer update | `installer.update` (`tools/installer/test_update.sh`) | shared with Learn & Play: the installer built with a key made for the test and updated, without its window, from made-up releases (`RS_UPDATE_URL`, `file://`): a signed newer release replaces the app and the new one verifies; a zip changed after signing, another key's or an empty signature, a release without one, a signed installer of another version than the release names, and an app in a folder that can't be written to are refused with the app as it was; the same or an older version is no update. Needs `swiftc`, no game and no network |
| Release tools | `release.tools` (`tools/release/test_release.py`) | shared with Learn & Play: the rules that say what a change calls for (every file of the repository has one), and a release's edits and notes on a made-up tree ([Release checks](#release-checks)) |
| Per-game tables | `targets.check` (`tools/targets/test_targets.py`); also `python3 tools/targets/check.py` | shared with Learn & Play: every name resolves in every target or is `unsupported`; no stale entries; native code uses only listed names, Learn & Play's own only names `win-x86` has; the mid points, values, continuations and edit groups the built-in fixes name are in each target's table |
| Hook points and mods | `recomp.hooks` (`make -C tools/ws7/recomp/test/hooks test OUT=DIR`) | shared with Learn & Play: one mod dylib in the 64-bit and the 32-bit runtime, for each convention (`test_hooks64`, `test_hooks32`): what a hook reads, changes, sets and calls; adapted calls, refusals, names by run-time lookup, the mods folder; the cost of a point without hooks. `test_files`: the files a mod serves (`rc_file.h`) as descriptors, as a `FILE` and in a folder's listing. `test_framerate`: the arithmetic of the frame rate fixes (`overrides/mods/framerate.h`), with the game's whole-millisecond frame times from 30 to 360 Hz. `test_fftsetup`: this edition's FFT setups (`overrides/mods/fftsetup.c`): one of each size made and handed out again, also to eight threads at once, a kept one known as such, and the game's own call with the fixes off. `test_shared64`, `test_shared32` (`test_mods_shared.c`): the built-in fixes that are one source for both editions (`overrides/mods/mods_shared.c`), as one object beside each runtime: the switches, the log lines and the start, each fix's hook against a game of the test's own, called by that runtime's convention, and the batched DLC mounting's shared part: the manifests' counts, hold and log lines, a manifest that never comes, the file questions step by step with each safeguard, the check at the mounting's end, and a mounting that does not go by the book (an enumeration during the hold, a flag cleared elsewhere, a step a frame); the white loading screen's table (a refused request asked again when its file is indexed, by each game's form of the request, a movie that is gone, the 5 s fallback, the switch) and the arrangement slots' continuation. The frame rate fixes' hooks are not in it. It also builds `probe_mod.dylib` and `files_mod.dylib` for the game runs |
| Edits and mid points | `recomp.inner64` (`tools/ws7/recomp/test/x64/innercases.py`) | [The recompiler](#the-recompiler) |
| Guitar input | `audio.input` (`shims/coreaudio/test`) | shared with Learn & Play: `guitar-input.ini` and the switches, the default file, a number in any locale, a channel across streams, the gain, the clip, the peak and clip counts, on made-up buffers |
| Platform shell | `platform.core.*`, `platform.shell.*`, `platform.overlay.*`, `platform.present.*`, `platform.warm.metal`, `platform.difftest.x86_64` (`shims/platform/test`) | the shell's logic, paths and `Rocksmith.ini`, the renderer decision and fallback (`rsp_renderer_decide`), the render scale's sizes and `RS_METAL_SCALE`, the overlay API on both backends, Metal presentation, the shader warm-up; the difftest compares rewritten shell functions with the original image's |
| Renderer | `opengl.*` (`shims/opengl/test`) | the GL entry-point table and its clients (`opengl.table.clients`), offscreen contexts on both backends, 44 GL sequences run on GL and Metal with their pixels compared (`opengl.metal.*`; `own_target`, a draw that samples the texture it draws into, checks its pixels on Metal only, GL leaving them undefined), every game import with `RS_GL_CHECK=1` (`opengl.metal.coverage`), the Metal disk cache (`opengl.metal.cache`) |
| Shader translator | `msl.unit`, `msl.verify.builtin`, `msl.verify.corpus` | GLSL to MSL; `msl_verify` runs programs on offscreen GL and Metal with the same inputs and compares pixels, depth and vertex outputs (the corpus test needs shader dumps) |
| Cg | `cg.*` (`shims/cg/tests`) | exports, ARB, GLSL and Metal rendering, the shader corpus (load, parameters, GLSL dump, render diff against the ARB path), a stress test in the game's call order |
| Bink | `bink.exports`, `bink.decode.*`, `bink.realtime` (`shims/bink/test`) | the exports against the game's imports; decoding and real-time playback of game videos (need extracted videos) |
| Game libraries | `game.offline.no_network`, `game.difftest.x86_64`, `game.native.arm64` (`shims/game/test`) | the online stub and crypto import nothing that talks to a network; the rewritten crypto, efd, hash and Lua functions against the original x86-64 code (under Rosetta, which writes a golden file), then the arm64 build against that file |
| libstdc++ | `libstdcxx.*` | the system library provides the GNU ABI the game needs |
| Shims | `shim.<name>.macho` | each built shim has the expected architectures and install name (`tools/check-shim.sh`) |

`win-x86.install` and `win-x86.shaders` are in the same CTest run:
[Windows testing](../windows/TESTING.md#tests-in-the-repository).

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

The installer app's window has no automated test; `steamless.decrypt`, `play-native.gamefiles`,
`play-native.settings` and `play-native.cache_overlay` cover what it runs, and `installer.update` the app
replacing itself. The app is checked by hand, end to end, with a test home:

```bash
HOME=/path/to/test-home "tools/installer/RS Re-ARMed Installer.app/Contents/MacOS/RS Re-ARMed Installer"
```

Its build folder, logs, the launcher and `~/Library/Application Support/RS Re-ARMed` then go under the test
home. Its preferences do not: the installer keeps its choices (game, build folder, renderer) in the real user's
defaults (`UserDefaults`), so a test run changes what the next real run starts with.

- **Cases**: a fresh build and install, Skip during Preparing shaders, an update after a simulated renderer
  fallback (the popup shows OpenGL), quitting mid-build, the download of a release's source, and the Settings
  and switches sheet (the switches a `settings.env` and `options.env` have shown set, Save writing the files
  into the test home, Reset to Defaults and Save removing them, Advanced collapsed at first unless one of its
  switches is set, and the orange line for a game installed before the page's files were read).
- **A whole first install**, for a home of its own: link `<home>/Library/Application Support/Steam/steamapps/
  common/Rocksmith2014` to the clean depot, start the app with `-Game mac` from outside the repository (it takes
  the current folder's repository if there is one), and click Build & Install: the only click until the run
  ends, about 20 minutes, with `Rocksmith2014_x64_decrypted` matching the pin.
- **The game without a Steam install** (`gamefiles.py mac-game`, and `install.sh` with a downloaded game) runs
  without touching Steam, on the stand-ins of [Windows testing](../windows/TESTING.md#the-installer-app)
  (`RS_STEAM_DIR`, `LP_STEAM_EXE`, `LP_STEAM_PROCESS`). For this game the made-up Steam folder signs in account
  42 (`[Logged On, 4, 7] [U:1:42]`) and has its ticket in `userdata/42/config/localconfig.vdf`, for the DLC step;
  the script takes `+download_depot APP DEPOT [MANIFEST]` and fills `steamapps/content/app_APP/depot_DEPOT`; and
  `RS_MAC_TARGET` is a `target.toml` with the made-up program's `exe_size` and `exe_sha256` and
  `depot_disk_bytes = 0`, so the real depot is not needed. `mac-game --locate` then fails, `mac-game` sends one
  command and prints `game-from download` and the folder, and `RS_NATIVE_HOME=… RS_NATIVE_LAUNCHER=…
  tools/play-native/install.sh <a built app>` with `RS_GAME_DIR` at that folder fetches each DLC of the ticket
  and copies only the `_m` archives. A clean download of the Mac depot made earlier is found by
  `mac-game --locate` with nothing sent, and `gamefiles.py sync <that folder> <test home>/game --edition mac`
  shows what an install from it copies. With the real Steam, the rules of the Windows page hold. Not yet run:
  the download itself from the real Steam through this code, the installer's window on this route (Finding the
  Mac game, Downloading your DLC), and a game started from such a copy.
- **The window as a run leaves it**, with nothing run: `RS_INSTALLER_PREVIEW=installed`, `failed`, `update` or
  `updated` in the app's environment shows the box of a finished install (Start the Game, Show in Finder), of a
  failed step (Try Again, Show Log File), the title row's button for a newer release (which has no installer to
  take, so its offer is the page), or the box of an installer that has just replaced an older one. Close (Escape) is at the bottom
  right once a run has ended, stopped ones too, with Build Again (Build & Install after a run that did not
  finish) beside it. Looked at in the three previews, not yet after a real run. AppKit draws and lays out an app by
  the SDK it was built with, and GitHub's runner may have an older one than this Mac (1.18.0's installer, built with
  macOS 15's, had the box's texts over the line above it). To look at a build as that SDK's, change the version its
  program names and sign it again: `vtool -set-build-version macos 12.0 15.5 -replace -output OUT PROGRAM`.
- **The installer replacing itself, in its window**: `RS_TEST_KEEP=1 tools/installer/test_update.sh` keeps its
  scratch folder, with the installer built with the test's key (`tree/tools/installer`) and a release in each
  `r-*` folder. Those releases are the same program with another version written on it, so for the window build
  a second copy of `tree` with another `VERSION`, zip it (`ditto -c -k --norsrc --noextattr --keepParent`), sign
  the zip with `test.key` (`swift tools/release/sign.swift sign`) and write a `feed.json` like the `r-*` ones.
  Then, with a copy of the first app outside any checkout:
  `open --env RS_UPDATE_URL=file://<folder>/feed.json "<copy>/RS Re-ARMed Installer.app"`. The offer names both
  versions and shows the release's text; Update Now puts the new app in the copy's place and opens it, and its
  box above the log says "This installer is now v…" with what it replaced. The buttons answer to System Events
  (`click button "Update Now" of window 1`), and the box's texts are in `groups of window 1`. Run this way on
  2026-11-17. Not yet run: a copy macOS itself approved at its first start (downloaded with a browser, opened
  through Privacy & Security), where macOS may run it from a read-only place (the offer then says to move it)
  or guard the app against being replaced (the update then stops with macOS's reason and the installer as it
  was); and the download from GitHub itself, which needs a release that has a signature.
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
minutes without the loading-screen fix (`RS_MODS_MENU_RETRY=0`; [PLAN.md](PLAN.md#open-issues)), its short intro hides intro problems, and the overlay changes nothing
in a cache that has the entries.

### Unattended runs

The repository has no virtual audio cable (a recording played into the Real Tone input), so an unattended run
of either edition has no guitar input. What it has:

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
  or `nohup` macOS drops every `DYLD_` variable, so neither the guard nor keyfifo loads (a command then leaves
  no `keyfifo:` line in the log, and no key arrives). In zsh a job started with `&` also runs at nice 5. Check
  that `netguard.log` says `mode=block` before going on. A launcher in a test home on an external drive stops at
  macOS's prompt for that drive; the executable started directly does not.
- **The focus**: a key reaches the window only while the game has the focus, and any other app coming forward
  loses it quietly: send `front` before keys. With `RS_KEY_DIRECT=1` keys arrive in front or not. Started from a
  shell the game never has the focus until `front`.
- **`tools/perf/run.py CONFIG OUT --route-only`** ([Measured runs](#measured-runs)): the whole route to the note
  highway and out, each key waiting for its screen.
- **Seeing a run**: snapshots (`RS_GL_SNAPSHOT_DIR=DIR`, `RS_GL_SNAPSHOT_SECS=N`; each is a readback of about
  70 ms, so judge smoothness without them; one a second can fall behind the game on a busy machine, by 20 s after
  ten minutes and by more under load, so take one every 3 s to see the screen a key led to), frame times
  (`RS_GL_FRAMETIMES=FILE`; keep the window in front for anything measured) and the logs of
  [Diagnostics](#diagnostics). The rearmed-mods library
  (`rearmed-mods.dylib` in `<home>/mods/`, `RSMods.ini` in the game folder, `RS_MODS_LOG=1`) logs a `menu:` line
  for each screen, which is what a script should wait for.
- **The installed app by `open`**: the launcher hands the game only what it lists, so a test copy of its
  `Contents/Resources/launch.sh` adds keyfifo, `HOME`, `CFFIXED_USER_HOME` and any other variable to `open`'s
  `--env`s; without `HOME` the game writes to the real `~/Library/Caches`.

**The screens by key**, from the start to the note highway (the `menu:` name in brackets). The times are what
test runs showed: wait for the `menu:` line, or look at a snapshot, rather than count on them.

| Screen | Keys | Notes |
|---|---|---|
| Title (TitleScreen) | Return | up about 8 s after the start with the intro skip, about 31 s without. Give it 6-8 s after it appears: a Return in the fade-in is lost, and Esc there opens EXIT GAME |
| Profile list (ProfileSelect) | Return | `fwddelete` here deletes the profile. With no profile, Return starts the first-time screens (Down to New profile, Return, a name, Return, then "Have you played Rocksmith before?"). With rearmed-mods' `ForceProfileLoad` the game goes to the main menu by itself |
| Main menu (MainMenu) | Return on LEARN A SONG | up about 25 s after the profile with a few DLC archives; a large library's mounting takes 5 to 8 s (2,326 archives; the time to the menu has not been taken since the mounting got faster), and a Return during it can be lost: wait for the `[rs-mods] DLC:` line. The list: LEARN A SONG, ENUMERATE, SESSION MODE, NONSTOP PLAY, LESSONS, GUITARCADE, MULTIPLAYER, TONE DESIGNER, EXIT GAME. ENUMERATE and EXIT GAME are the overlay's: without it (`RS_MODS_OVERLAY=0`) there is no ENUMERATE, and SHOP and UPLAY follow TONE DESIGNER |
| Song list (LearnASong_Songs) | `/`, the letters, Down, Return | `/` opens the search (Shift in Learn & Play), Tab opens SORT BY. The open box says how many songs match: with nothing typed, how many the list has (`263 Matches`). Letters 1.5 s apart; a key down and up in one frame is lost for text. Return with no match leaves the box open |
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
  Esc; a kill loses it, so each such run sets it). DISCONNECTED plays songs but greys out Session Mode, Lessons,
  Guitarcade, Multiplayer and the Tone Designer. Exactly one device named "Rocksmith USB Guitar Adapter" (a
  Loopback device of that name works) gives REAL TONE CABLE; two give "Two Rocksmith Real Tone Cables have been
  detected" at the tuner.
- **In a song**: Esc pauses (LearnASong_Pause): RESUME, RESTART, TUNER, MIXER, CHORDS, EXIT; Bass has no
  CHORDS. The menu keeps its cursor, so press Up six times before counting Down, keys 0.6 s apart, and look
  before Return: a key is lost now and then. CHORDS lists only the named chords met so far; Esc goes back to the
  pause menu and a second Esc resumes. Space in the pause menu is Riff Repeater: Down twice to SPEED, Left once a
  second, `key delete` resumes. EXIT goes to the song's page; Esc from there is the song list with the search
  still in it (`/`, Delete ten times, then the new letters).
- **The other modes**: LESSONS: Down to a lesson, Return, Return to start it; Esc gives RESUME, EXIT LESSON.
  SESSION MODE: Space on the band screen starts it, after the tuner. NONSTOP PLAY: Return on START NONSTOP PLAY,
  the tuner, Delete, Return. GUITARCADE (the sixth entry: Down five times), Return on TECHNIQUE GAMES, Down to the
  second game (the first, Gone Wailin', has no tuner), Return: the game's tuner (Guitarcade_Tuner). `fwddelete` is
  Skip Tuner there too, Return answers its warning, and the game is up about 10 s later (Guitarcade_Game) on
  "MAKE SOME NOISE!", which waits for a guitar; Esc goes back to the list. A Return on the tuner is Calibrate, which
  comes back to the tuner. MULTIPLAYER:
  [By hand](#by-hand-and-with-computer-use).
- **The lists**: the profile list is one of the game's scrolling lists, and so are these. NONSTOP PLAY's songs:
  Down and Return on its page is SONG LIST EDITOR (`NonStopPlay_SongList`); Return there ticks a song, Esc goes
  back. A tuner of Nonstop Play that is cancelled (Esc) shows the set list, which brings the tuner again by
  itself: a second Esc within two seconds leaves to the page. TONE DESIGNER (the eighth entry: Down seven
  times): Return on EDIT TONE (`ToneDesigner_EditTone`: PRE, AMP, LOOP, CABINET, RACK), Return on a slot with
  gear in it (`ToneDesigner_EditGear`: its knobs, then CHANGE, MOVE, REMOVE, FINISH; Return on a knob gives it
  Left and Right until the next Return), Return on CHANGE for the categories (`ToneDesigner_CategoryList`) and
  Return on one for its gear (`ToneDesigner_PedalList`, `ToneDesigner_RackList`). Esc goes back a screen at a
  time; from the Tone Designer's own menu it asks SAVE TONE?, where Down and Return is No. Keys sent while
  a screen moves in are lost, and the main menu loses them for longer (of seven Downs sent 20 s after it came
  up, on a busy machine, three arrived): wait, send them a second apart, and look at a snapshot before a Return
  that depends on the cursor. The key's name is `esc`: a name keyfifo doesn't know is skipped (`keyfifo: no key` in
  the log).
- **Tools** (Space on the main menu): DIFFICULTY SETTINGS, TONE DESIGNER, TUNER, OPTIONS (Down three times),
  SHOP, CHORD BOOK, TECHNIQUE GUIDE, STATS. Return on OPTIONS, then on MIXER, is the mixer's page;
  [Measured runs](#measured-runs) has the way to Venue Mode.
- **ADV DISPLAY SETTINGS** (RS Re-ARMed's options): Return on OPTIONS (`OptionsMenu`), Down three times and Return
  (`AdvancedDisplaySettings`). RS Re-ARMed's rows come after the game's own; Return on one opens it (`EditSetting`),
  Left and Right change it, and Backspace goes back to the list. Each change is a `menu options:` line in the log,
  with the switch and its value. A switch set with `launchctl setenv` to another value than `options.env`'s (for the
  renderer, `RS_GL_BACKEND`) greys its row, which says the value and SET FOR THIS SESSION, with a `menu options:`
  line at the start. With the mouse (a 1280x720 window, engine pixels as the movie's): the list's scroll bar is at x 686
  from y 40 to 680, a press there and a move drag the focus along it, and a press on a row (x 250) opens it; Esc
  while the button is held, then the release and Return on ADV DISPLAY SETTINGS, gives a list that doesn't follow
  the pointer.
- **Quitting**: Esc on the main menu, Return on "Are you sure you want to quit?" goes to the title; Esc there
  and Return on EXIT GAME? is a normal quit, which saves the profile.

**Checks that have a route.** Learn & Play's counterparts are in
[Windows testing](../windows/TESTING.md#unattended-runs). The `[rs-mods] ` lines are the built-in fixes', logged
with `RS_MODS_LOG=1`.

- **No intro screens** (`RS_MODS_INTRO`): boot to the title several times, with the original `cache.psarc`.
  Snapshots show white, about three seconds of black, then the title, and the log has `[rs-mods] intro:
  skipped`; with `RS_MODS_INTRO=0` the Ubisoft logo and the legal screens must still show. The failure to look
  for is the loft fading in with no title on it. Switched off through `settings.env`, the log starts with
  `[settings] RS_MODS_INTRO=0` and the fixes' `[rs-mods] fixes on, …` line says `intro skip off`; an
  `RS_NETGUARD=log` line in that file is logged as ignored.
- **Auto Save warning** (`RS_MODS_AUTOSAVE`): the main menu comes up after the profile with no dialog, and the
  log has `[rs-mods] auto save warning skipped`; with `RS_MODS_AUTOSAVE=0` the RS2AutosaveWarning dialog comes
  back. The skip must keep the game's input mode step: a DIRECT CONNECT profile with an interface connected must
  reach the tuner, not "It sounds like your cable is unplugged". Not checked: a Real Tone cable or Microphone
  profile, and a cached mode to restore.
- **Frame rate fixes** (`RS_MODS_FRAME_RATE`): the log has `[rs-mods] frame rate: on, 8 of 8 points`, and
  `[rs-mods] frame rate: off` with `RS_MODS_FRAME_RATE=0`; `--init-only` is enough to see it. `RS_FPS=120` makes
  the frames short enough for the hooks to act on a 60 Hz screen, and a route to the highway then goes through
  the tuner with them. What they do takes a guitar, by hand: at `RS_FPS=120` or `240`, Return on a tuner opens
  calibration, which should settle as at 60 Hz, and a Guitarcade game's tuner should complete. Not done yet.
- **Skip Tuner in the Guitarcade** (`RS_MODS_GUITARCADE_SKIP`): the log has `[rs-mods] Guitarcade Skip Tuner: on`
  (`--init-only` is enough to see it), a Guitarcade game's tuner shows DELETE Skip Tuner beside Cancel and
  Calibrate, and the route above ends in the game: `menu: Guitarcade_Tuner`, `SelectionListDialog`,
  `Guitarcade_Tuner`, `Guitarcade_Game`. With `tools/perf/run.py`: `--profile-keys "space esc:4 down down down down
  down return:8 return:8 down return:15 fwddelete:6 return:25"` (the first key has to change the screen, hence
  Space and Esc). Not run: the switch off, and a game played after a skip.
- **The white loading screen** (`RS_MODS_MENU_RETRY`): the log has `[rs-mods] menu movie request again: on` (`--init-only`
  is enough). The bug needs the file index to be late: start the game with a community fast-load `cache.psarc` in the
  copy and the CPU kept busy (`yes > /dev/null` a few times, as many as the machine has cores), with
  `RS_MODS_MENU_RETRY=0` for a start that stays white and then without it. When a menu's request is refused the log
  has `menu load: 'NAME' was rejected (not indexed yet); waiting for the indexer`, then `is indexed now; asking again
  (request 2)` and `accepted after 2 requests`; a `WARNING ... fallback N of 6` line means the index's file was not
  seen being published and the fix is going by its 5 s timer, which is worth a report. A start with no refused request
  has none of these lines. Not run in the game: the Mac's sites are matched by their code only.
- **Learn a Song with more than five arrangements** (`RS_MODS_ARRANGEMENTS`): the log has `[rs-mods] Learn a Song
  alternate arrangements: on`. The crash needs a song with six arrangements or more (a custom song with, say, Lead,
  Lead 2, Rhythm, Rhythm 2, Bass and Bass 2): Learn a Song, search for it, Return. Without the fix the page crashes in
  the renderer; with it the page shows four of the other arrangements and `Learn a Song: a song with more than five
  arrangements shows four alternates at a time; ...` is in the log once (`...: on, scrolling` at the start; plain `on`
  means the game lacks a point of the scroll and the page shows its first four alternates, as `Learn a Song: ... shows
  the first four alternates` says). Scrolling: on the last of the four, arrow Down (or the wheel) moves the window
  one arrangement, the last entry's label says how many are below (`(+2)`), the first one's how many are above
  (`(-1)`), the focus stays on the entry it was on, and arrow Up on the first goes back; the log has `Learn a Song:
  Down, window 0 -> 1 of 7 alternates`. Enter on a shown entry plays that arrangement (check an entry after a scroll,
  not only before); the header (title, artist) is still there after a scroll and the recommendation buttons still take
  the focus. Leave the page and open it again (or another song): it starts at the top. A song with five or fewer
  arrangements shows what it did. Report what the log says if a scroll shows nothing or the renderer stops. Not run
  in the game.
- **Batched DLC mounting** (`RS_MODS_BUDGET_MS`): the log has `[rs-mods] DLC: N archives mounted in F frames,
  T s` once the profile has loaded, which is the end of the mounting: every archive's package is loaded and every
  song manifest asked for is in by then, and the menu's "Enumerating downloadable content..." goes with it. When
  the manifests came later than the last archive the line ends `, their songs in the lists S s later`, F and T
  being the frames and the time to the last archive. A second line, `DLC: A of the steps' Q file questions asked
  of the step's archive alone`, says how many of the steps' questions about the shader cache did not go to every
  mounted archive: all but those of the first step (1,460 of 1,470 with 147 archives), and of any step that mounted
  nothing or something the fix cannot answer for (with 2,326 the line read 23,210 of 23,240);
  a line `DLC: PATH is there, and a step was told it was not` would say that an answer was wrong. Three checks of
  the end:
  - Nothing is left open: count the archives the game has open a few times a second from the start,
    `lsof -p PID | grep -c '/dlc/.*\.psarc'`, which is 0 by the time the line is in the log (147 archives:
    `mounted in 6 frames, 0.32 s, their songs in the lists 0.15 s later`, none open; 1.16.0 had 138 open at its
    line, after 0.40 s, and the last closed 2.7 s later).
  - No song comes later: wait for the line, go to Learn a Song, search for `a` and note the matches; then leave
    to the main menu, come back and search again. A search keeps its matches until it is cleared, so the first
    count is of the moment the list was opened. With 2,326 archives: 2,037 both times, the list opened 1.5 s after
    the line (two runs). 1.16.0: 1,227 in a list opened 3 s after its line, and 2,037 on coming back.
  - The time: 2,326 archives in 5.4 to 7.7 s (five runs; the longer ones with the song list opened meanwhile),
    where 1.16.0's line came after 23.9 s with 1,946 archives still open, the last of them for 33 s more.

  A step that took an archive and mounted nothing would lose its songs, so compare the song list of a start with
  the budget with one at `RS_MODS_BUDGET_MS=0`, a step a frame as in the original, its file questions the game's
  own too. The two counts were the same with 147 archives in `dlc/`, in 1.16.0; not compared since, nor with a
  large library.
- **Title logo** (`RS_REARMED`): a copy with `static.psarc.rearmed` shows the red "Re-ARMed 2026" tag across
  REMASTERED on the title, and with `RS_REARMED=0` the game's own logo. `ls -l` of `static.psarc` in the copy
  shows which one the launcher linked.
- **Direct Connect overlay**: with `game/Update/patcharchives.psarc` made by `cache_overlay.py`, INPUT offers
  DIRECT CONNECT as a fourth value, and the log has `[rs-mods] cache overlay: cache7.7z from
  Update/patcharchives.psarc`. Without screen control, change the title's `pressstart` label in the overlay's
  copy of `ui_menu_pillar_startup` and look at a snapshot.
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
- **Rocksmith 1 songs** (`RS_STEAM_OWNED`): one `tools/perf/run.py CONFIG OUT --route-only` run whose config has
  `"song": "FinalCountdown"`, `"search": "countd"` and `"env": {"RS_STEAM_OWNED": "206166"}`: the search finds The
  Final Countdown and the run reaches its note highway (`song: FinalCountdown` in `game.log`); without the AppID
  the run stops at the search, which doesn't list the song. How a copy finds the owned ones is `Rs1Owned` in
  `test_gamefiles.py`, and `AskSteam` there runs the asking against a stand-in for Valve's library. Against the
  real Steam, signed in to the edition's account, with an archive key at hand (`psarc_key.have_key(COPY)`):
  `gamefiles.rs1_owned(STEAM, DEST, ACCOUNT, print)` into a scratch `DEST` whose `dlc` is a link to the copy's
  prints what a refresh would, and `gamefiles.ask_steam(STEAM, {221680, 258341, 205190, 206166})`, `rs1_tickets`
  and `rs1_packages` with the same set answer for apps the account does own. Run that way for an account that owns
  none of the DLC: 0 of 141, and yes for the game, the import pack and the first Rocksmith. The launchers' adding of
  the AppIDs set by hand, and their `[steam]` line, are in `test_settings.py`.
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
- **A mod's hook on the guitar input** (`si_io`): Learn & Play's check
  ([Windows testing](../windows/TESTING.md#unattended-runs)) fits here as it is: a test mod that fills each buffer
  with a quiet sine of 110 Hz, and rearmed-mods' Show Current Note for the note the game hears, 45. Here the point is
  the game's own function, and a mod's hook comes after `audio_chan.c`'s: the buffer is that hook's mono one, small
  ones collected into slots of 128 frames, or the device's own where that hook has nothing to do (no channel, gain
  or level to apply, or `RS_AUDIO_INPUT=0`). Not run here.
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
  takes the focus (`menu: LearnASong_Pause`), and such a run is left out of the report, as is one during which
  other processes took more than three cores' worth of load.
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
  With a few thousand archives there the mounting takes 5 to 8 s after the main menu comes up, and a key during
  it can be lost, so a key there is pressed again until the screen changes.

What is measured over the window, into `OUT/<edition>-<n>/result.json` with what the run was (the commit, the
executable's and the mods' hashes, the settings, the display): when frames went on the screen
(`RS_GL_FRAMETIMES`' `P` events, from Metal: intervals, late frames, refreshes without a new one), when the game
handed them over (`F`: rate, median, p95, p99, worst; this moves with the game's work and is not what is seen),
this edition's game frame, a command buffer's time on the GPU (`U`), the CPU of the game, of each thread
(`libproc`, no sampler) and of the helpers, the memory footprint and its growth, and the GPU's utilisation
(`ioreg`, the whole system's).

At a fixed frame rate the GPU's utilisation and start-to-end time are not its work: it slows its clock to fill
the refresh. For what it can do, run with `"RS_FPS": "300"` in `CONFIG`'s `env` (measured in the Mac edition so
far; Learn & Play reads the switch too), one run at a time on a cool Mac. A `--trace` run records a Time Profiler
trace (`xcrun xctrace`) from the settle on, which `profile.py` adds up by thread and function; it gives no
numbers, as the sampler slows the game.

Results are in [PERFORMANCE.md](PERFORMANCE.md). Not looked at yet: that the venue shows with no guitar played,
and a run on an external display.

### Long runs

`tools/perf/soak.py CONFIG OUT mac|lp MINUTES guitar|bass` plays Nonstop Play for that long and samples the game,
for leaks and for what changes over time; `tools/perf/soak_report.py OUT/EDITION` says what the run shows
([PERFORMANCE.md](PERFORMANCE.md#a-long-run)). `CONFIG` is a measured run's, and so are the checks before it and the
rule about the screen: the game pauses when another app takes the focus.

- **The way in**, from the main menu: Down three times and Return (NONSTOP PLAY, the fourth entry; the mods log
  `menu: NonStopPlay_Main`). The page's rows, by Down: START NONSTOP PLAY, SONG LIST EDITOR, PLAY TIME (60 MINUTES),
  SONG LIST (ALL SONGS), TUNING (E STANDARD), PLAY ORDER (SHUFFLE), ARRANGEMENTS (LEAD + RHYTHM); Left and Right
  change a row, and the set list is built again after each change ("Building Set List...", a few seconds: 178
  songs and 303 arrangements become 233 and 479 at ANY TUNING with 147 archives). TUNING's first value, one Left
  from E STANDARD, is ANY TUNING. ARRANGEMENTS on a guitar path: LEAD, RHYTHM, LEAD + RHYTHM, LEAD + ALT LEAD,
  RHYTHM + ALT RHYTHM, LEAD + RHYTHM + ALT; on the bass path BASS and BASS + ALT BASS. The path is Path/Input's
  first row (Ctrl on the main menu, `menu: MissionMenu`): Right to its last value is BASS GUITAR, Esc goes back.
- **Playing**: Return on START shows the set list (`NonStopPlay_Hub`, which goes on by itself) and the first
  song's tuner (`NonStopPlay_PreSongTuner`: `fwddelete`, Return, as a song's), then `NonStopPlay_Game`. Every song
  after it brings the set list and its tuner again. A mission that completes shows MISSION COMPLETE!
  (`MissionInformationDialog`) between two songs and waits for Return. A new profile has the page at its
  defaults each time.
- **What is sampled**: every 5 s the CPU, footprint, RSS, GPU, threads and screen; every minute the open files
  (the song archives by name); at 1 minute, every 10 and at the end `vmmap --summary` and `footprint`, and at 1
  minute and the end `leaks`. Those tools stop the game for a second or two (`leaks` for five): the report tells
  such frames from a screen change's and from ones inside a song. `SOAK_ENV="MallocStackLogging=1"
  SOAK_TOOLS=heap,mh` adds the allocations' stacks: `leaks` then names each leak's, `heap-*.txt` has the blocks
  by class and `mh-*.txt` the live blocks by stack, to compare between the first minute and the end.
- **Run**: thirty minutes of each edition on a copy of a 2,326-archive library (`dlc` of the test home replaced
  for the run), and after their fixes ten minutes of Learn & Play and nine of this edition (with the stacks, on
  the test home's own 147 archives).

### Shaders of a run

`RS_GL_DUMP_SHADERS=DIR` writes the GLSL a run links, `RS_MSL_LOG=1` logs each program translated at run time
because the corpus doesn't have it, and `RS_METAL_CACHE_STATS=1` prints the compile times at exit. With Metal,
pipelines are built the first time they are used, so the first play of a song, venue or mode after an install has
a few 0.3-0.8 s stalls: measure frame times on a second play. For a first play with no shader cache that leaves
the user's alone, see [Learn & Play's notes](../windows/TESTING.md#shaders-of-a-run).

### By hand and with computer use

- **By hand**: menus, a song, Session Mode, lessons, Guitarcade, full screen and back, quitting; on both
  renderers. The keys are in [INSTALL.md](INSTALL.md#playing).
- **The full-screen keys**, without a person: System Events brings the game to the front and types the key,
  and the window's frame says what happened (`osascript -e 'tell application "System Events" to keystroke "f"
  using {control down, command down}'`, then `get {position, size} of window 1` of the process `Rocksmith2014`).
  At the title each of Control-Command-F and Shift-Command-F goes from the screen's size to a 1280x720 window
  and back.
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
  cable is unplugged". With `RS_MODS_LOG=1` the log has `[rs-mods] player 2 on "NAME"` when the fix gives Player
  2's saved input its slot. Not played since that fix became one source with Learn & Play
  ([PLAN.md](PLAN.md#open-issues)).
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
[Learn & Play's notes](../windows/TESTING.md#the-capture-vm).

## Diagnostics

Logs are in `<home>/logs/`: `last-run.log`, `previous-run.log`, `netguard.log`, `seed.log`. Set switches for
the launcher with `launchctl setenv` ([INSTALL.md](INSTALL.md#settings-and-switches)); the four logs
(`RS_MODS_LOG`, `RS_AUDIO_LOG`, `RS_STEAM_LOG`, `RS_FILE_LOG`) are also in the installer's Settings and
switches…, under Advanced. All off unless set.

| Switch | What |
|---|---|
| `RS_MODS_LOG=1` | what the built-in fixes apply and the DLC mounting's end (archives, frames, time), in lines that start `[rs-mods] ` |
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
| `RS_GL_FRAMETIMES=FILE` | frame times, an event a line, among them a frame handed over (`F`), the game's frame (`G`), and on Metal a frame on the screen (`P`) and a command buffer on the GPU (`U`) |
| `RS_METAL_PASS_TIMES=FILE` | Metal: the GPU's time in each render pass of a frame, by its place in the frame, from every eighth frame; with the present, and "between" for the upscaler. The file is rewritten about every 8 s. Read the shares: at a fixed frame rate the GPU's clock follows the load |
| `RS_METAL_MSAA=1\|2\|4\|8`, `RS_METAL_ANISO=1-16` | MSAA on the game's screen-sized targets with depth (1 off), the least anisotropy of mipmapped textures (1: the game's) |
| `RS_METAL_UPSCALE=0`, `RS_METAL_COLORS=vivid` | no MetalFX upscaling on a Retina screen (the log has `upscaling WxH to WxH with MetalFX` when it runs); the layer left untagged (not sRGB) |
| `RS_METAL_SCALE=N` | the game at N% of its size (50-100), scaled up: the log has `Metal drawable WxH (points at N%)` and `render scale` with the engine's size |
| `RS_MODS_…=0`, `RS_MODS_BUDGET_MS=N` | one of the built-in fixes off, and the DLC mounting's time per frame (0: one archive a frame): [INSTALL.md](INSTALL.md#settings-and-switches) |
| `--renderer-check` | the renderer a start would use, without starting the game |
| `RS_RENDERER_TEST_NO_METAL=1` | act as if Metal could not start |
| `RS_METAL_SAFE_LOAD=0\|1`, `RS_METAL_CORPUS_WARM=0\|1`, `RS_METAL_CORPUS_LIMIT=N` | force safe load, the corpus warm, or warm only part of the corpus |
| `RS_METAL_CACHE=0`, `RS_MTL_COMPILE_THREADS=N` | no disk cache, the compile pool's size |
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
in [Tests in the repository](#tests-in-the-repository) and Learn & Play's; a new suite gets a line in
`scope.py`'s table and a rule for the paths it covers. The tools' own test is `release.tools`
(`tools/release/test_release.py`).

The Mac edition's run for a release is `tools/release/smoke.sh --mac`, which takes its route from
`RS_SMOKE_MAC_CMD`: a command that plays to the note highway and exits 0 (it gets `RS_SMOKE_OUT`, the folder for
its log and snapshots), such as `tools/perf/run.py CONFIG "$RS_SMOKE_OUT" --editions mac --route-only`
([Measured runs](#measured-runs)), which fails when the game has ended or left the highway twelve seconds after
it got there. Without one, play it by hand to the highway and say so in the release commit.
