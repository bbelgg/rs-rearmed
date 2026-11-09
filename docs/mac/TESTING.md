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
(`cmake -DRS_GAME_PLAIN=<the image>`, no default: its differential tests need Rosetta), the installed game (`RS_GAME_APP`,
`RS_GAME_DIR`), or extracted videos and shader dumps under `analysis/`. Nothing derived from the game is
in the repository.

| Area | Tests | What they check |
|---|---|---|
| Decryption | `steamless.decrypt` (`tools/steamless/test_decrypt.py`) | the key found in a made-up Mach-O, what is refused, in plain words; the real pin with `RS_DEPOT_GAME` |
| Game files | `play-native.gamefiles` (`tools/play-native/test_gamefiles.py`) | the installs' own copy of the game files (`gamefiles.py`) with made-up game folders: what each edition copies, clones and real copies, the space check, resuming a stopped copy, refreshes (only the copied set is removed, the game's own writes stay), the earlier mirror's links removed without following them, the completeness check |
| Menu overlay | `play-native.cache_overlay` (`tools/play-native/test_cache_overlay.py`) | the menu edits (the cable screen's DIRECT CONNECT button, the INPUT slider's fourth value, the main menu's ENUMERATE, the notice) and that a file which has them is left alone; the psarc and 7z readers and writers with made-up archives (a rewritten 7z keeps every other file's stored bytes, order, times and attributes); the overlay made, and left out for a cache that has the entries; `gamefiles.py` making it after a copy. With the game's `cache.psarc` (`RS_TEST_CACHE`, or the installs' copies) also the real files; skipped without one |
| Built-in mod settings | `play-native.settings` (`tools/play-native/test_settings.py`) | shared with the Windows port: `settings.env`, the built-in mods switched off in the installer. The launcher's reader (run with macOS's `/bin/bash`) and Learn & Play's (`win-x86/app/lp_settings.c`, compiled by the test) read the same made-up files and must set and log the same: comments, spaces, CRLF, a repeated key, the environment winning, other lines ignored (the network guard's and Steam's switches among them); the launcher's `--env` pairs carrying the value it logs as used, the environment's too, as `open` gives the game only launchd's environment; the same keys in both and in the installer |
| Per-game tables | `targets.check` (`tools/targets/test_targets.py`, also `python3 tools/targets/check.py`) | every name resolves in every target or is `unsupported`; no stale entries; native code uses only listed names |
| Hook points and mods | `recomp.hooks` (`tools/ws7/recomp/test/hooks`) | shared with the Windows port: one mod dylib in the 64-bit and the 32-bit runtime, for each convention: what a hook reads, an argument it changes, a result it sets, a call it does itself, game code it calls first; adapted calls, refusals, names by run-time lookup, the mods folder; the cost of a point without hooks; `probe_mod.dylib` for the game runs |
| Guitar input | `audio.input` (`shims/coreaudio/test`) | shared with the Windows port: `guitar-input.ini` and the switches, the default file, a number in any locale, a channel across streams, the gain, the clip, the peak and clip counts, on made-up buffers |
| Platform shell | `platform.core.*`, `platform.shell.*`, `platform.overlay.*`, `platform.present.*`, `platform.warm.metal`, `platform.difftest.x86_64` (`shims/platform/test`) | the shell's logic, paths and `Rocksmith.ini`, the renderer decision and fallback (`rsp_renderer_decide`), the overlay API on both backends, Metal presentation, the shader warm-up; the difftest compares rewritten shell functions with the original image's |
| Renderer | `opengl.*` (`shims/opengl/test`) | the GL entry-point table and its clients (`opengl.table.clients`), offscreen contexts on both backends, 40 GL sequences run on GL and Metal with their pixels compared (`opengl.metal.*`), every game import with `RS_GL_CHECK=1` (`opengl.metal.coverage`), the Metal disk cache (`opengl.metal.cache`) |
| Shader translator | `msl.unit`, `msl.verify.builtin`, `msl.verify.corpus` | GLSL to MSL; `msl_verify` runs programs on offscreen GL and Metal with the same inputs and compares pixels, depth and vertex outputs (the corpus test needs shader dumps) |
| Cg | `cg.*` (`shims/cg/tests`) | exports, ARB and GLSL rendering, the shader corpus (load, parameters, GLSL dump, render diff against the ARB path), a stress test in the game's call order |
| Bink | `bink.exports`, `bink.decode.*`, `bink.realtime` (`shims/bink/test`) | the exports against the game's imports; decoding and real-time playback of game videos (need extracted videos) |
| Game libraries | `game.offline.no_network`, `game.difftest.x86_64`, `game.native.arm64` (`shims/game/test`) | the online stub and crypto import nothing that talks to a network; the rewritten crypto, efd, hash and Lua functions against the original x86-64 code (run under Rosetta, which writes a golden file), then the arm64 build against that file |
| libstdc++ | `libstdcxx.*` | the system library provides the GNU ABI the game needs |
| Shims | `shim.<name>.macho` | each built shim has the expected architectures and install name (`tools/check-shim.sh`) |

Outside CTest:

- **Steam layer**: `make -C shims/steam check` (exports against the game's imports, both slices) and
  `make -C shims/steam test` (vtable slots and ABI tests, arm64 and x86_64; the x86_64 run needs Rosetta).
- **Network guard**: `tools/ws8/netguard/build.sh --test` builds `netguard_test` and runs it on both
  architectures.
- **App build**: `tools/ws7/recomp/app/build-m4.sh` starts the new app once with `--init-only` (maps the image, binds the shims,
  then exits) and fails if that does not print its `init-only:` line. `tools/ws7/recomp/build/build.sh`
  links a smoke test of the translated image and checks zlib's `adler32`/`crc32` against the system's.
- **Seam unit test**: `tools/ws7/recomp/app/test/build.sh OUTDIR` (or `RC_UNIT_TEST=1` for `app/build.sh`): the
  engine seam without the game, 166 checks.
- **Translation**: the generated C of the whole image is the same across runs and scheduling
  (`--jobs`, `--batch`); compare two `gen/` directories with `diff -r`.

## Installer tests

The installer app has no automated test. It is checked by hand, end to end, with a test home:

```bash
HOME=/path/to/test-home "tools/installer/RS Re-ARMed Installer.app/Contents/MacOS/RS Re-ARMed Installer"
```

Its build folder, logs, the launcher and `~/Library/Application Support/RS Re-ARMed` then go under the test
home. Its preferences do not: the installer keeps its choices (game, build folder, renderer) in the real
user's defaults (`UserDefaults`), so a test run changes what the next real run starts with. The cases
checked for 1.3.0: a fresh build and install, Skip during Preparing shaders, an update after a simulated
renderer fallback (the popup shows OpenGL), quitting mid-build, and the download of a release's source. How the
screens were driven for 1.4.0, the launch options, and the stand-ins for Steam and Apple's tools that show every
message of the box above the log: [Windows testing](../windows/TESTING.md#the-installer-app) (one app, one set of
messages). The Built-in Mods sheet was checked the same way for both games: the switches a `settings.env` has shown
unchecked, Save writing the file into the test home, Turn All On and Save removing it. `tools/steamless/test_decrypt.py` (ctest `steamless.decrypt`) tries the decryption on a made-up Mach-O;
with `RS_DEPOT_GAME` set to the clean Mac depot folder it also checks the real output against the pin.

A whole first install through the app, for a home of its own: link `<home>/Library/Application Support/Steam/
steamapps/common/Rocksmith2014` to the clean depot, start the app as in that section with `-Game mac`, from outside the
repository (it takes the current folder's repository if there is one), and click Build & Install: that is the only
click until the last window, about 20 minutes, with `Rocksmith2014_x64_decrypted` matching the pin.

A test home on another disk needs two things a real home doesn't: its `logs` folder has to be a link to the startup disk
(the launcher's `open --stdout` there fails with -10810, launchd's spawn refused with EPERM), and each new build asks
for the removable volume once per app, the launcher and the game. Someone has to click Allow; until then the process
stands still in `open`, with no window and 0.01 s of CPU.

`tools/play-native/install.sh` takes `RS_NATIVE_HOME` and `RS_NATIVE_LAUNCHER` for a test install that
leaves the real one alone; the launcher's `RS_NATIVE_SEED=0` skips copying the Steam saves (for a prepared
`steam/` folder). The test install gets its own copy of the game files from `RS_GAME_DIR` (clones when the
test folder is on the game's volume; a real copy of the whole game and its DLC otherwise, so check the free
space first). To prove a run reads no Steam folder, start the game with `HOME` set to a folder without
one: the copy, `.rearmed/steam.env` and the `steam/` saves are all it needs.

## Game runs

The game is tested in a **separate test install** (`RS_NATIVE_HOME`, `RS_NATIVE_LAUNCHER`), never the
user's, with a test profile.

- **Unattended runs** used an input injector (key presses and waits for log lines or screens) and a
  virtual audio cable that plays a recording into the Real Tone input, with the scripts that run them and
  their reference screenshots. These come from the private project RS Re-ARMed was split from and are
  **not in this repository**. What the repository has for such runs:
  - `RS_GL_SNAPSHOT_DIR=DIR` (with `RS_GL_SNAPSHOT_SECS=N`): a PNG of the frame every N seconds, on both
    renderers. Each snapshot is a synchronous readback (about 70 ms), so judge smoothness without them.
  - `RS_GL_FRAMETIMES=FILE`: one line per frame, for frame-time statistics (median, p99, frames over
    20/33/50 ms). Keep the game's window in front for anything measured: see [Windows testing](../windows/TESTING.md#unattended-runs).
  - `logs/last-run.log` with `RS_MODS_LOG=1`, `RS_AUDIO_LOG=1`, `RS_STEAM_LOG=1`, `RS_GL_LOG=1`.
  - `tools/ws8/keyfifo`: keys for a running game from a FIFO, in either edition, a key at a time with the
    snapshots in between. Build it with `tools/ws8/keyfifo/build.sh OUTDIR`, start the game's executable with
    `RS_KEY_FIFO=FILE DYLD_INSERT_LIBRARIES=OUTDIR/libkeyfifo.dylib`, then `echo "key return" > FILE`, `text danc`
    or `click X Y`. Keys go into the game's own event queue, so nothing else on the Mac gets them. (The launcher's
    netguard stays in the list: `DYLD_INSERT_LIBRARIES=<home>/libnetguard.dylib:OUTDIR/libkeyfifo.dylib`.) Export the variables and start
    the executable itself: through `env` or `nohup`, system programs, macOS drops every `DYLD_` variable, so neither the network guard
    nor keyfifo loads (no `netguard.log`, keys never read). Check that `netguard.log` says `mode=block` before going on.
- **By hand**: menus, a song, Session Mode, lessons, Guitarcade, full screen and back, quitting; on both
  renderers (`RS_GL_BACKEND=gl` for OpenGL).
- **An interface's channel and gain**: `RS_AUDIO_LOG=1` with a `guitar-input.ini` (`RS_AUDIO_INPUT_FILE` for a test
  one). The log should have `listening to channel N of M` at the start, and a peak every five seconds while the game
  listens: for a person, the INPUT setting to DIRECT CONNECT, the interface, the calibration, then play. An input of more
  than two channels without the setting is the case to compare with: scrambled and quiet on a build before this.
- **The list of inputs and the level** (2026-11-05, by hand, a test install, an HX Stomp and a UR22mkII): Ctrl, Down to
  INPUT, Right to DIRECT CONNECT, Down, Return lists the inputs; Return takes one, and Path/Input shows it as CURRENT INPUT.
  All ten channels played, and the choice came back after the game's own Exit and a new start. Calibration settled at
  +5.6 dB with the meter at the top. `RS_AUDIO_LOG=1` has `input level:` for each level set, and `listening to channel N
  of M` for each entry. If only a device's first channel is heard, its I/O isn't started (the I/O id, [DESIGN.md](DESIGN.md#built-in-fixes-and-mods)). An aggregate
  device (`AudioHardwareCreateAggregateDevice`, or Audio MIDI Setup) makes a device of many channels from what is connected,
  with nothing to install, and is a virtual device to the list. keyfifo's keys didn't get past the title in the one run
  of this, with `RS_KEY_DIRECT=1` and `front`.
  A new launcher in a test home on an external drive stops at macOS's prompt for that drive; start the game's executable
  directly, as above.
- **First play of a song stutters** with Metal after an install: Metal builds pipelines the first time they
  are used, so the first half-minute of a song, venue or mode has a few 0.3-0.8 s stalls. Measure frame
  times on a second play.
- **Fallback**: two forced kills (`kill -9`) of Metal starts in a row; the next start must use OpenGL, write
  `opengl` into `renderer` and `renderer-fallback.txt`; a clean quit removes `metal-session`.
- **Full screen on a second display** (2026-11-09, a test install, a 4K TV at 1x beside the built-in screen): start the
  executable with the network guard and the input injector, without the virtual cable (see the next point); the title is
  enough. Move the window with System Events (`set position of window 1` of the process with the game's pid, to a point on
  the second display): computer use's background drag didn't move it. Keys need the game in front: System Events' `set
  frontmost` of the process, then `keystroke "f" using {command down, shift down}` (a background key from computer use
  doesn't reach it). Read the window's frame from `CGWindowListCopyWindowInfo` (the bounds of the pid's windows, a few lines of
  Swift) and the render size from a snapshot. Full screen is the display's frame at window level 25 and a 3840x2160
  snapshot; with another app in front the window hides and keeps that frame; Shift-Command-F again gives a 1280x720 window
  on the same display, at its top. The two crashes of the next point had moved that install to OpenGL: write `metal` back
  into its `renderer` and delete `renderer-fallback.txt`.
- **The virtual cable crashes the game at the title** since the per-channel input list (7333a2a): its device id,
  `0x7a7a0001`, has the bit the list marks its channel entries with (`0x40000000`, `audio_devs.c`), so its reads go to a
  device that doesn't exist, and the game's device scan, after a failed UID read, releases a NULL string (`CFRelease()
  called with NULL`). Real devices' ids are small numbers. Until the cable takes another id, use a real interface, or no
  cable for checks that don't need input. In that run the title was the stock one, which the injector's reference (the
  Re-ARMed title) doesn't match (difference 5.1), so `wait-title` never fired: look at a snapshot and go on by hand.
- **No intro screens** (`RS_MODS_INTRO`): boot to the title several times with the original `cache.psarc`
  (a fast-load `cache.psarc` has a short intro of its own and hides problems) and with a fast-load one;
  with the skip, snapshots show white, about three seconds of black, then the title (about 8 s from
  launch, against about 31 s with `RS_MODS_INTRO=0`, which must still show the Ubisoft logo and the legal
  screens). The failure to look for is the title never coming: the loft fades in with no title on it.
  `RS_MODS_LOG=1` logs `intro: skipped`. Checked 2026-11-03 with the test harness's title reference: 4
  boots with the original cache (one scripted on to the main menu and 60 s of a song) and 3 with a
  fast-load one reached the title, and 6 more with a test build of the same logic. Switched off through
  `settings.env` (2026-11-09, the repository's `launch.sh` run against the installed game): the log starts with
  `[settings] RS_MODS_INTRO=0`, the mods say `intro skip off` and no `intro: skipped` follows; an
  `RS_NETGUARD=log` line in the file is logged as ignored and doesn't reach the game.
- **Auto Save warning** (`RS_MODS_AUTOSAVE`): with a profile in the test install, Return on the title once
  it takes keys, Return on the profile list. With the skip the main menu comes up with no warning dialog;
  `RS_MODS_LOG=1` logs `auto save warning skipped`. With `RS_MODS_AUTOSAVE=0` the RS2AutosaveWarning dialog
  appears after profile select instead. Checked 2026-11-05: profile select reaches the main menu with no
  dialog, and the dialog returns with the switch off.
  The skip must keep the game's input mode step, so test it with a profile on DIRECT CONNECT and an interface connected:
  from the main menu Ctrl, Down, Right three times, Esc, Esc, Return on "Yes, quit" to save it. After a new start, Return on
  LEARN A SONG, on the first song and on Play Song must reach the tuner; Delete and Return then reach the note highway. Without
  the step the tuner stops at "It sounds like your cable is unplugged". Checked 2026-11-06 by keyfifo (`front` before every
  key, or a key is lost when another app has the focus), a test install, a UR22mkII: to the highway with the skip, to the
  tuner with the switch off, and the message twice with the step left out. A new profile (Down to New profile, Return, a
  name, Return) goes on to "Have you played Rocksmith before?" with the same log line and no dialog. Not checked: a Real Tone
  cable or Microphone profile, and a cached mode to restore.
- **Direct Connect overlay**: with the original `cache.psarc` (not one that has the entries already, or the
  overlay changes nothing) and `game/Update/patcharchives.psarc` made by `cache_overlay.py`, the INPUT
  setting (in My Path, `ui_menu_pillar_mission`) offers DIRECT CONNECT as a fourth value, and choosing it
  works with an audio interface. `RS_MODS_LOG=1` logs
  `cache overlay: cache7.7z from Update/patcharchives.psarc`. Checked by the user on 2026-11-03. Without
  screen control, a marker label is the quickest check that the overlay is read: change the title screen's
  `pressstart` label in the overlay's copy of `ui_menu_pillar_startup` and look at a snapshot
  (`RS_GL_SNAPSHOT_DIR`).
- **Enumerate** (`shims/steam/tests/test_enumerate.cpp` covers the copying): take a custom song out of the test
  install's copy, point `source` and `dlc_from` in its `.rearmed/source.json` at folders of your own, start the game,
  and when the main menu is up put the song in one of them. By keyfifo, counted from the start: Return at 42 s
  (title) and 50 s (profile), song in at 80 s, Down at 84 s to ENUMERATE, Return at 88 s: the notice "Looking for new
  songs…"; Return at 96 s closes it, Up, Return at 105 s for LEARN A SONG, `/` at 117 s, `amor` at 120 s, Down and
  Return on the match, Return on Play Song at 145 s, `fwddelete` at 172 s on the tuner (Skip Tuner), Return at 178 s
  on "Yes, continue without tuning". A Return on the tuner instead starts Calibration, which waits for a guitar. With
  `RS_STEAM_LOG=1` the log has `enumerate: pressed` once, `1 new archive(s) in the copy` and `dispatch callback 1005`.
  Use a song with an AppID the game's list lacks, re-packed as under Song ownership. Checked on 2026-11-04 with The
  Bee as 999999: "amor" gives 2 Matches (Paramore too), the song plays to the note highway with no restart;
  with `RS_MODS_BYPASS=0` 1 Match, without the song.
- **Song ownership** (songs listed whatever AppID they carry): re-pack an official archive with another
  `appid.appid` (with `tools/play-native/cache_overlay.py`'s `Psarc` and `write_psarc`), put it in the
  `dlc/` of the test install's game source and refresh the copy (`gamefiles.py`): `steam.env`'s
  `RS_STEAM_DLC` gains the AppID and a search finds the song; with the copy's `steam.env` holding the list
  without it, the search does not. Checked on 2026-11-03 with Dancing Queen as 999999 ("danc": 9 matches
  against 8), played to the note highway. The way through the screens from a test key mod (key events
  through the engine's input sink 0x100ba1190): Return on the title about 35 s after the first frame (this
  test build plays the intro), Return on the profile list, Return on the main menu once it is up (the DLC
  enumeration there took about 30 s in a first run); in the song list `/` opens the search (the Mac
  binding; Shift is the Windows one), and letters only register when the key stays down for a few frames
  (a down and up in the same frame is lost for text, though it works for Return); Tab opens SORT BY. Down
  then Return opens a match, Return plays it, Return on "TUNE UP!" (the test profile has no tuner skip
  set), and the song starts. Use the original `cache.psarc` in the test copy: with a modded one a start
  stayed on the loading screen for minutes (the known fast-load stall, [PLAN.md](PLAN.md#open-issues)).
- **Rocksmith 1 songs** (`RS_STEAM_OWNED`): an APFS clone of the copy (`cp -cR`), the Steam folder copied without
  `crd`, the game started by keyfifo with `RS_STEAM_OWNED` set to the AppIDs of `gamefiles.rs1_dlc_apps`. "countd"
  gives 2 Matches (Megadeth and The Final Countdown), which plays to the note highway (`Play_FinalCountdown` in the
  log, audio from `songs.psarc`); unset, 1 Match. "rebel" finds Rebel Rebel either way: the disc pack has no such
  check. Netguard logged nothing refused. Checked on 2026-11-04. `gamefiles.ask_steam` was checked against a running
  Steam for an account that owns none of them (0 of 141, and the account id right); not yet for one that does.
- **Mods**: `DIR/probe_mod.dylib` of `tools/ws7/recomp/test/hooks` in `<home>/mods/`; by keyfifo, as under The screens by key,
  Return at 42 s and 50 s, Return 40 s later, 12 s later `/`, `cherub` a key every 2 s, 6 s later Down, 3 s later Return, 10 s later
  Return, 26 s later `fwddelete`, 6 s later Return: the highway about 20 s after that. `[probe]` lines in the log, `[probe] file
  sha256` the file that ran; the numbers to compare with Learn & Play's, whose first camera call is the same (-1 1 1 -1, near
  0.85, far 0). A mod on `steam_dlc_owned` sees 1 call here against 148 on Learn & Play: the built-in hook answers first here.
- **The rearmed-mods library** (`rearmed-mods.dylib` in `<home>/mods/`, `RSMods.ini` in the game folder, `RS_MODS_LOG=1`): by keyfifo, each key
  waiting for the mods' own log, whose `menu:` lines say which screen it is. With a profile, `ForceProfileLoad` reaches MainMenu by
  itself; Return is LearnASong_Songs; `/` opens the search; Return on a row is LearnASong_SongOptions; Return is LearnASong_PreSongTuner;
  `fwddelete`, Return; LearnASong_Game. Hotkeys in the search, all quiet, then firing again after Esc: `/`, then `i`, `l`, `5`, `f`; `/`, `qq`,
  Return (no match: the box stays open), `i`; a click, `i`. The same dylib ran the same route in Learn & Play ([Windows
  testing](../windows/TESTING.md#unattended-runs)), with the same SHA-256 in both logs and the same results but for the lyrics (Learn & Play's test song has no lyric steps). Keep a copy of
  `steam/`: Return on ProfileSelect with no profile starts the first-time screens, and `fwddelete` there deletes the profile.
- **Sharper picture and screen shape** (2026-11-08, a test install, the colors run's profile on DISCONNECTED, `RS_MODS_LOG=1`,
  `RS_GL_SNAPSHOT_SECS=5`): the log has `MSAA 4x on the game's screen-sized targets`. By keyfifo, Return on TitleScreen, ProfileSelect,
  SelectionListDialog, ProfileSelect, MainMenu, LearnASong_Songs and LearnASong_SongOptions (each a few seconds after its `menu:` line)
  reach LearnASong_PreSongTuner; `fwddelete` there went back to the song options, and Return then gave "TUNE UP!" (no input on the
  profile), whose Return (OK) starts the song: LearnASong_Game. The highway drew with MSAA, no errors. The window opens at 1280x720 whatever
  `ScreenWidth` says; dragging its right edge in to 1152 points (computer use, the test executable's own window) made a 1153x720 game,
  and Return on the song options (the song had ended) and on TUNE UP! started it again: the rearmed-mods log's frustums were
  0.925 x 0.577 and 0.583 x 0.364 against 1.026 x 0.577 and 0.647 x 0.364 at 16:9, and the picture and menus undistorted.
  Shift-Command-F then rendered 2560x1440 on the 16:9 screen. Not run: a 16:10 or ultrawide screen in full screen, Learn & Play.
- **The measured route** (2026-11-06, for [Learn & Play's route](PERFORMANCE.md#learn--plays-route): the executable started directly, `RS_FULLSCREEN=0`, a test home with APFS
  clones of the game files, a copy of a test profile with a DIRECT CONNECT input): Cherub Rock is DLC, so `dlc/cherubrock_m.psarc` has to be in the home's `dlc/`, and with no other
  file there the main menu is up 25 s after the profile. With `front` 1 s before each step: 16 s after the start Return on the title, 9 s, Return on the profile, 28 s, Return on LEARN A SONG,
  10 s, `key /`, 2 s, `text cherub`, 11 s, Return on the match, 7 s, Return on Play Song, 29 s, `key fwddelete`, 4 s, Return: the highway within 30 s, and the window starts 46 s after that Return. To take the UR22mkII
  on such a profile: Ctrl, Down twice to AVAILABLE INPUTS, Return, Down to it, Return, Esc; a normal quit saves it. Snapshots every 265 s give one look at the highway after the window
  without one in it. Check the route by snapshots first, as `fwddelete` on ProfileSelect deletes the profile.
- **The virtual cable with the list of inputs** (2026-11-07, a scripted run to a song with its signal recorded): a fake input
  device's id must leave bit 30 clear, as `audio_devs.c` takes such an id for a channel's and the game then dies on `CFRelease(NULL)` in the
  device scan. Reference images must have the snapshots' size (2560x1440 on a 1440p screen), and the old main menu's has no ENUMERATE,
  so time that step. Give the title 8 s after it matches before Return: a Return in the fade is lost, and Escape there opens EXIT GAME.
- **A song with no interface connected** (2026-11-07, the texture log's runs, a test profile on REAL TONE CABLE): the tuner stops at "It sounds like your
  cable is unplugged". So on the main menu, 22 s after `menu: MainMenu` (a Return sooner is lost in the transition): `front`, Ctrl, Down, Right twice to
  DISCONNECTED, Esc, 8 s, Return on LEARN A SONG. Search, Return on the match, Return on Play Song, 26 s on the TUNE UP! dialog, `fwddelete`, 5 s, Return:
  `menu: LearnASong_Game`. A kill loses the input, so each run sets it.
- **The render API in a game** (2026-11-07): `shims/opengl/test/render_mod.c` turns the hues of the notes' two ramps once a second, in either edition. In a folder with `rearmed-mods.dylib`
  and `RS_MODS_PATH` set to it, the route above, then snapshots of the highway: the notes change color from frame to frame, and strings, the highway and the numbers don't. The log has
  `[render_mod] on mac-x64, API 1` and two `[rs-gl] render: a rule found its texture` lines. The ramps' hashes are in the mod.
- **Keys and the focus**: without `RS_KEY_DIRECT=1` a key goes to the window only while the game has the focus, and
  any other app coming forward loses it quietly (`[rsmods] key down` is missing from the log). With it, keys arrive
  in front or not. Search text then still needs 1.5 s between letters.
- **The screens by key** (`tools/ws8/keyfifo`, 2026-11-04, a test install with a profile; snapshots every 2 s): the
  title takes Return about 40 s after the start. Return on the profile, then wait about 26 s for the main menu.
  Ctrl opens Path/Input: Down to INPUT, Right and Left step through REAL TONE CABLE, MICROPHONE, DISCONNECTED and
  DIRECT CONNECT, Esc goes back (the change reaches the profile only when the game saves it, so a kill loses it).
  Return on LEARN A SONG; `/` opens the search, letters 1.5 s apart, and the list shows the matches; Down and Return
  on a song, Return on Play Song, Return on "TUNE UP!". In a song Esc pauses; Down moves through RESUME, RESTART,
  CHORDS and EXIT, with a key now and then lost, so look before Return. The main menu's list is LEARN A SONG,
  ENUMERATE, SESSION MODE, NONSTOP PLAY, LESSONS, GUITARCADE, MULTIPLAYER, TONE DESIGNER, UPLAY.
  LESSONS: Down to a lesson, Return, Return to start it; Esc gives RESUME, EXIT LESSON. SESSION MODE: Space on the
  band screen starts it, after the tuner. Esc from the main menu leads to "Are you sure you want to quit?", Return
  on it to the title, Esc there to EXIT GAME?, and Return on that is a normal quit, gone in 0.2 s. keyfifo's `quit` (the app
  menu's Quit item) also ends the game, from inside a song too, in 0.3-0.5 s.
- **Riff Repeater's speed and the focus** (2026-11-05, `rr_speed_ratio` and `ui_event` with a mod): in a song Esc, Space, Down twice to
  SPEED, Left once a second, each 1 off the 100, `key delete` resumes. The Mac game calls the point once here, where Learn & Play
  calls it on every step. For `ui_event` the game must have had the focus: send `front` first, then another app in front (`open -a
  Finder`); started from a shell it never has it, and with `RS_KEY_DIRECT=1` nothing shows that.
- **A copy with the whole `dlc` folder** (2,400 files): the main menu is up about 85 s after the profile rather than 26 s, and a
  Return before that is lost, with the rest of a counted script going astray. Look at a snapshot before the first key after the profile.
- **The installed app by `open`** (2026-11-04, the launcher in a test home): the launcher hands the game only what it
  lists, so a test copy of its `Contents/Resources/launch.sh` adds keyfifo, `HOME`, `CFFIXED_USER_HOME` and the lines of a
  file to `open`'s `--env`s; without `HOME` the game writes to the real `~/Library/Caches`. A home with no Steam account
  takes the account whose saves were copied in. The same route as above, with the title taking Return from about 11 s.
  Search `cherub`, Return, Return on Play Song, 26 s later `fwddelete` on the tuner, 6 s later Return: the highway about
  20 s after that, with no snapshot needed. NONSTOP PLAY: Return on START NONSTOP PLAY, the tuner, Delete, Return; Esc in
  a song puts the cursor on the entry last used, so look before pressing Down to EXIT.
- **Two players** (2026-11-06, by hand, an HX Stomp with no `Channel` in `guitar-input.ini`): load the second profile on its own, Ctrl,
  Down to INPUT, DIRECT CONNECT, Down and Return lists the inputs, Return takes `HX Stomp channel 6`; Esc back and leave by the menus, or
  it isn't saved. Load the first profile the same way on channel 5. MULTIPLAYER, Return on any notice, Return on the second profile, Return on CONTINUE, a mode
  and song, Play Song: a split-screen song with each player on their channel. SWAP CABLE ASSIGNMENT is lit with both on Direct Connect, and swaps the
  channels. Without the change the song stops at "It sounds like
  Player 2's cable is unplugged".
- **Guitarcade**: Down five times to GUITARCADE, Return, Return on TECHNIQUE GAMES, a game, Return, Return. A game has a tuner
  with no Skip Tuner: Return starts Calibration (Esc cancels) and only a tuned instrument gets past it. So the menus and
  the list of games are what keys reach; the games themselves are a check by hand (the maintainer's, 2026-11-05). A virtual cable next to a Loopback device of the same name gives "Two
  Rocksmith Real Tone Cables have been detected"; with the input on the microphone the wave doesn't reach it at all.
- **Without a guitar**: DISCONNECTED plays songs ("TUNE UP!" then the note highway) but greys out Session Mode,
  Lessons, Guitarcade, Multiplayer and the Tone Designer. Exactly one device named "Rocksmith USB Guitar Adapter"
  (a Loopback device of that name works) gives REAL TONE CABLE with `CURRENT INPUT: Real Tone Cable`; two give "Two
  Rocksmith Real Tone Cables have been detected" at the tuner. The tuner stands before every session: Delete,
  then Return on "Yes, continue without tuning". With another app in front the engine takes no keys: send keyfifo's
  `front` before them.
- **The output device** (2026-11-04, a test library logging the buffer sets, UR22 as output and input): the
  sink sets 256 frames at about 1 s, the input scan sets 128 on the same device at 3-4 s and stays. Changing the
  default output during a run only adds a mute listener in the game, and the override moves the sink. Unplugging the
  device moves the output to another by the HAL; replugging reopens the input only.
- **Following the default output** (2026-11-05, on the title and the profile screen, a test library answering another device as
  the default and calling the listeners, so the system's setting is not touched): `audio: the default output is now ...`; the
  output callbacks and the game's sound (peak 0.3) on the new device at its own buffer size, 128 frames on one with inputs, 256 on
  the speakers, there and back, and a clean quit. On the installed app, the same day: a real change in Sound settings during a song, and the sound moved, heard by the maintainer.
- Before reporting a game bug, check whether it also happens in the original game under Rosetta.

### Shaders of a run

`RS_GL_DUMP_SHADERS=DIR` writes the GLSL a run links, `RS_MSL_LOG=1` logs each program translated at run time because the corpus doesn't
have it, and `RS_METAL_CACHE_STATS=1` prints the compile times at exit. For a first play with no shader cache that leaves the user's
alone, see [Learn & Play's notes](../windows/TESTING.md#shaders-of-a-run).

### The capture VM

Not applicable: the Mac edition's game is not packed. See [the Windows port's notes](../windows/TESTING.md#the-capture-vm).

## Diagnostics

Logs are in `<home>/logs/`: `last-run.log`, `previous-run.log`, `netguard.log`, `seed.log`. Set switches for
the launcher with `launchctl setenv` ([INSTALL.md](INSTALL.md#settings-and-switches)).

| Switch | What |
|---|---|
| `RS_MODS_LOG=1` | what the built-in fixes apply |
| `RS_AUDIO_LOG=1` | device buffers, the real-time thread, dropped input, each level set, each input's channel and its peak and clips every few seconds |
| `RS_STEAM_LOG=1` | the offline Steam layer's calls (`[steam-shim]`) |
| `RS_GL_LOG`, `RS_GL_CHECK`, `RS_GL_STATS`, `RS_GL_VERBOSE=1` | GL calls, errors after each call, counts, the load and every first error |
| `RS_GL_TRACE_DIR=DIR`, `RS_GL_TRACE_START=S`, `RS_GL_TRACE_KEY=1\|PATH`, `RS_GL_TRACE_FRAMES=N` | a trace of N frames (default 30), from the second frame, S seconds after the first, or when `DIR/go` or PATH appears |
| `RS_MSL_LOG=1`, `RS_MTL_DUMP_MSL` | shaders translated at run time, the MSL of slow compiles |
| `RS_GL_DUMP_SHADERS` | the GLSL the game compiles |
| `RS_GL_TEX_LOG=FILE`, `RS_GL_TEX_DUMP=DIR`, `RS_GL_TEX_MARK=HASH,HASH` | Metal: each new texture content with its hash, RSMods' checksum, size and format, and in `FILE.draws` each kind of draw (program, mode, count, vertex stride, the textures of units 0-3); each content saved to DIR; textures of those hashes drawn as solid magenta, to see what uses them |
| `RS_GL_SNAPSHOT_DIR`, `RS_GL_SNAPSHOT_SECS` | frame snapshots |
| `RS_GL_FRAMETIMES` | frame times |
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
- `tools/ws8/netguard/build.sh --test`: the guard refuses non-local connections, host-name lookups and URL
  opens.
- In a game run, `logs/netguard.log` lists every refused attempt; a run from the title to a song should add
  nothing unexpected. The launcher refuses to start without `libnetguard.dylib`.
- The Steam layer runs in stub mode (`RS_STEAM_MODE=stub` in the launcher): it never loads Valve's library.
  `crd` is never copied into `steam/` and is deleted from a copy before each launch.
