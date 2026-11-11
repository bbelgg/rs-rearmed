# Testing the Windows port

What this repository can test, how to run it, and how the game is run unattended and by hand. The Mac
port's tests: [Mac testing](../mac/TESTING.md).

## Tests in the repository

None of these needs the game. The probes need Microsoft's or the game's DLLs as inputs; they are read from
where you point them and never copied into the repository. `OUT=DIR` is a build folder outside the tree.

| Area | Command | What it checks |
|---|---|---|
| Installer | `python3 -m unittest discover -s win-x86/install -p 'test_*.py'` (also ctest `win-x86.install`) | the install steps with stand-ins made by the test: cabinets and self-extracting exes for the redistributables, a Steam folder and its logs, `file://` downloads, the build with a fake translator and compiler, the app's copy of the game files (`game-copy`, `game-refresh`), the app bundle, cleaning; the committed harvester against its SHA-256. `LP_REDIST_REAL=1` also extracts the real DLLs from this Mac's Steam, `LP_GAME_REAL=<depot folder>` checks a real download |
| Offline unpack | `python3 -m unittest discover -s win-x86/unpack -p 'test_*.py'` | the seed container and the QEMU capture against a fake GDB stub. With `RS_UNPACK_EXE`, `RS_UNPACK_SEED` (and `RS_UNPACK_SHA256`) also a whole unpack; skipped without them. The release suite `lp.unpack`; not a CTest test |
| Shader corpus sources | `python3 -m unittest discover -s win-x86/shaders -p 'test_*.py'` (also ctest `win-x86.shaders`) | `lp_shaders` on hand-assembled bytecode at odd offsets: a pair is made for its pixel shader, a shader that isn't there is only counted; `record_programs.py` drops the run's shader numbers and merges. The warm-up itself is `platform.warm.metal` of the [Mac tests](../mac/TESTING.md#tests-in-the-repository) |
| Game files | `python3 tools/play-native/test_gamefiles.py` (also ctest `play-native.gamefiles`) | shared with the Mac port: the app's own copy of the game files, for both editions, on made-up game folders |
| Menu overlay | `python3 tools/play-native/test_cache_overlay.py` (also ctest `play-native.cache_overlay`) | shared with the Mac port: the menu edits, the psarc and 7z readers and writers, the overlay made from a cache and left out when the cache has the entries; with a real `cache.psarc` (`RS_TEST_CACHE=<depot folder>/cache.psarc`, or the installs' copies) also the real files |
| Built-in mod settings | `python3 tools/play-native/test_settings.py` (also ctest `play-native.settings`) | shared with the Mac port: `settings.env`. This app's reader (`win-x86/app/lp_settings.c`, compiled by the test) and the Mac launcher's read the same made-up files and must set and log the same; the same keys in both and in the installer |
| Kernel32 and input | `make -C win-x86/shims/tests test OUT=DIR` | waits (`test_ksync`, run again with `LP_WAKE_ALL=1`): events, mutexes, semaphores, wait-any and wait-all, timeouts, `PulseEvent`, an idle waiter not woken by other objects, a wait keeping objects whose handles are closed; `CloseHandle` against the other handle calls (`test_close`, under the address sanitizer); files (`test_kfile`: paths per thread, finds, the overlay folder, moves, a file a mod's provider serves); the open-file limit from 256 (`test_nofile`, `rc_nofile.h`, shared with the Mac port); `input_sink` and `set_app_focus` (`test_input`: events held for the game thread, the Mac's values, a pre hook keeping one from the game) |
| Audio layer | `make -C win-x86/shims/audio test OUT=DIR` (`test_audio --list` prints the endpoints the game would see) | the WASAPI and winmm faces on CoreAudio: endpoints and names, formats, rings, exclusive event mode, trimming |
| Guitar input | ctest `audio.input` (`shims/coreaudio/test`) | shared with the Mac port: `guitar-input.ini` and the switches, on made-up buffers |
| Steam face | `make -C win-x86/shims/steam test OUT=DIR` | the guest ABI end to end (`test_steam32`): exports, interface vtables, `thiscall`, callbacks and call results |
| Online layer | `make -C win-x86/online test OUT=DIR` | the three vtables in guest memory with fake game functions: every replaced slot, the posted events, a layout that does not match left alone |
| D3D9 layer | `win-x86/test/d3d9_test.sh <cmake build dir> [out dir]` (needs `cmake --build <dir> --target opengl`) | the layer's host test on both libRSOpenGL backends: fixed-function draws checked pixel by pixel, every stage operation, lights, fog, generated texture coordinates, the draw-time upload of dynamic buffers, the wait in Present while the window is hidden |
| Window rules | `cc -I win-x86/app win-x86/test/lp_screen_test.c win-x86/app/lp_screen.c -o lp_screen_test && ./lp_screen_test` | the window mode a style or back buffer means, `RS_FULLSCREEN`, sizes on a small screen and with Retina pixels, the pointer in a letterboxed back buffer, `RS_WINDOW_ANY_SHAPE` and the resolution a resized window asks for |
| CRT | `win-x86/crt/tests/build.sh <msvcr100.dll> [<msvcp100.dll>] OUT`, then `OUT/crt_probe <msvcr100.dll> [<msvcp100.dll>] [-v] [-bench] [-eh OUT/eh.dll]` | the translated CRT without the game: start-up, heap, strings, `printf`/`sscanf`, iostreams, `DllMain` attach and detach; `math_probe` (linked in): every native math override against the translated export (result bits, errno, x87 depth, return address); `-bench` times them; `-eh`: C++ throw and catch, unwinding through inner frames, `__except`, `setjmp` and `longjmp` on that CRT (`test/x86/ehcases.py`, needs clang's i386 PE target) |
| D3DX | `win-x86/d3dx/tests/build.sh <dir with the DLLs> OUT [<cmake dir>]`, then `OUT/d3dx_probe OUT [-v] [step ...]` | the translated d3dx9_42 and D3DCompiler_42: matrices, buffers, the assembler, the HLSL compiler and constant tables, image info, an effect against the D3D9 layer |
| Bink | `win-x86/bink/tests/build.sh <bink2w32.dll> OUT <cmake dir>`, then `OUT/bink_probe OUT/in/bink2w32.dll <file.bk2> [-n frames] [-png out.png] [-wait]` | the translated Bink on the game's Win32 seam: header, every frame a picture, the PCM; `.bk2` files come from the game's `video.psarc` and `etudes.psarc` (`tools/psarc_extract.py <psarc> <dir> --match .bk2`). The audio check wants a peak over 64 in the frames it decodes, so a video that opens quietly fails it with `-n 60`: use another video or more frames |
| Measuring tools | `python3 tools/perf/test_perf.py` (also ctest `perf.tools`) | shared with the Mac port: frame-time statistics, the settings file's pins, the report ([Measured runs](#measured-runs)) |
| Release tools | `python3 tools/release/test_release.py` (also ctest `release.tools`) | shared with the Mac port: what a change calls for, and a release's edits and notes |
| Tables | `python3 tools/targets/check.py` (ctest `targets.check` runs `test_targets.py`) | shared with the Mac port: every name in `mac-x64` and `win-x86`, and every hook point's prototype against the Mac's |
| Hook points and mods | `make -C tools/ws7/recomp/test/hooks test OUT=DIR` (also ctest `recomp.hooks`) | shared with the Mac port: one mod dylib in the 64-bit and the 32-bit runtime, for each convention; the files a mod serves (`test_files`); `probe_mod.dylib` and `files_mod.dylib` for the game runs ([Mac testing](../mac/TESTING.md#tests-in-the-repository)) |

### The recompiler

The 32-bit front end's differential test runs each translated function and the original bytes under
Unicorn and compares the end state ([recompiler](../recompiler/README.md#tests)). It needs a Python with
Capstone and Unicorn (Unicorn is for maintainers only and not in `requirements.txt`'s packages:
`pip install unicorn`):

```sh
cd tools/ws7/recomp
UC_IGNORE_REG_BREAK=1 "$PY" -m test.x86.handcases --out DIR                    # no DLL needed
UC_IGNORE_REG_BREAK=1 "$PY" -m test.x86.innercases --out DIR                   # edits and mid points, no DLL needed
"$PY" -m test.x64.innercases --out DIR                                        # the same for the Mac's front end (ctest recomp.inner64)
UC_IGNORE_REG_BREAK=1 "$PY" -m test.x86.difftest IMAGE.dll --out DIR --cases 6 --jobs 8
```

`difftest` takes the DLLs from the command line or `RC32_TEST_DLLS` and skips without one. The usual inputs are
the plain DLLs in the game folder (`bink2w32.dll`, `steam_api.dll`, `systemdetection.dll`) and `msvcr100.dll`
from the redistributables. None of the four shows a mismatch, so any mismatch is new and worth reading. A case
where the reference is known not to be hardware counts as `reference_limit`, not as a match: 32 of
`msvcr100.dll`'s, 5 of `steam_api.dll`'s. `msvcr100.dll` leaves out its import slots: what Unicorn runs there is the value bound to the slot, what was translated is the file's bytes.
Every math export an image imports is a native override (`win32_crtmath.c`), so the translated ones do not run
in the game either way.

The app build's checks, the seam unit test and the translation's determinism are the Mac edition's:
[Mac testing](../mac/TESTING.md#the-recompiler).

## Installer tests

Besides the unit tests above, the install is checked by running `tools/play-native/learn-play.sh` with a
test build folder (`RS_BUILD_DIR`) and a test app path (`LP_NATIVE_APP`); the app then still uses
`~/Library/Application Support/RS Re-ARMed/learn-play` unless it is started with `LP_HOME`. The installer
app has no automated test; its preferences go to the real user's defaults even with a test `HOME`, and what
is the same for both games (the previews of a run's end, the check of the download, a test home on another
disk) is in [Mac testing](../mac/TESTING.md#installer-tests).

### The installer app

One app installs both games; this is how each screen is driven, with the computer-use `app_*` tools (bundle
id `io.github.bbelgg.rs-rearmed.installer`), in the standalone form a user gets. The app is copied out of the
repository, so that it downloads its source, and started from a shell, so that it takes these:

```bash
env HOME=<test home> CFFIXED_USER_HOME=<test home> TMPDIR=<folder> \
    RS_STEAM_DIR="<real home>/Library/Application Support/Steam" RS_SOURCE_URL=file://<source.tar.gz> \
    "<copy>/RS Re-ARMed Installer.app/Contents/MacOS/RS Re-ARMed Installer" -BuildFolder <folder> -Game learnplay
```

- `RS_SOURCE_URL`: a `git archive --prefix=rs-rearmed/` of the commit under test, for the tag that isn't
  published yet. Delete `.installer-version` in the source folder to make it unpack a new one. It has to be the
  commit the app was built at: the app checks the download against the digest `build.sh` built in.
- `RS_INSTALLER_PREVIEW=installed|failed|update`: the window as such a run leaves it, with nothing run. A run's
  end is the box above the log: its buttons and Close are in the window.
- `-BuildFolder`: typing into the field does not work from the background (`app_click` then `app_type` is
  refused), so the folder comes as a launch argument. Starting a run writes it to the real defaults: note
  `defaults read io.github.bbelgg.rs-rearmed.installer` first and put `BuildFolder` back afterwards.
- `-Game learnplay` or `-Game mac`: the game, without touching the real defaults.
- `RS_STEAM_DIR`: a fake `HOME` must not lose Steam. `steam_command` runs Steam's launcher with the home Steam's
  folder is in, so the running Steam gets the command; with the fake home it would start a second, new Steam.
- **Screens**: Build & Install is `app_click` by element index; the box above the log is plain AX buttons (the
  first button, then Stop); alerts are a window of their own (`app_list_windows`, then `app_screenshot` with its
  `window_id`). The app's Quit menu item reads as disabled from the background; the window's close button works.
  Steam's windows are not listed by `app_list_windows`, and it has no menu bar for `app_menu`: read its logs.
- **Built-in Mods…**: the button on the Renderer row, or the app menu's item through `app_menu`. The checkboxes
  and the sheet's buttons are plain AX elements; Save writes `settings.env` in the test home's `RS Re-ARMed/` or
  `RS Re-ARMed/learn-play/`, and Turn All On with Save removes it.
- **Skip on Preparing shaders**: the step is 16 s on Learn & Play, so put a `sleep` at the top of the build
  folder's copy of `warm-shaders.sh`; Build Again takes under a minute and leaves it there. The box says the
  shaders are not prepared. A finished run, then Build Again and Skip, is the case to try.

Every message of the box can be seen without touching Steam, with stand-ins:

| Stand-in | What it does |
|---|---|
| `RS_STEAM_DIR=<folder>` | Steam's folder, with `logs/connection_log.txt`, `logs/console_log.txt` and `config/loginusers.vdf` written by hand: a `[Logged On, 4, 7] [U:1:43] ...` line signs in, a new account number is another account |
| `LP_STEAM_EXE=<script>` | Steam's launcher. The script writes to `console_log.txt` what Steam would (`Downloading depot 221683 (103 files, 5363 MB) ...`, `Depot download failed : missing license for depot (No subscription) `, `Depot download complete : "..." (manifest 1) `) and clones the clean depot into `steamapps/content/app_221680/`. When the path doesn't exist, Steam isn't installed |
| `LP_STEAM_PROCESS=<name>` | the process that counts as Steam: a small compiled program under that name (a copy of `/bin/sleep` is killed by macOS). Have it running before Build & Install: with it not running, the DLC step opens the real Steam by its bundle identifier, which `LP_STEAM_EXE` doesn't stand in for |
| `RS_XCODE_SELECT=<script>` | fails `-p` until a file is there: Apple's tools missing and arriving |
| `RS_XCODEBUILD=<script>` | with `RS_XCODE_SELECT` printing a path in a `.app`: Xcode not set up. Exit 69 until a file is there. Stop before the run goes on: with the real Steam the next step would fetch DLC |
| `LP_CONFIG=<target.toml with a huge depot_disk_bytes>` | no room. Change the file with an atomic `mv` while the alert is up (a `cp` can be read half written), then Check Again |

With the real Steam:

- **A fresh download**: `content` in `Steam.AppBundle/Steam/Contents/MacOS/steamapps` is a link: point it at an
  empty folder, and back as soon as the Copying step is over (on the test home's disk the copy is clones and
  stays). Don't send a second `+download_depot` while one runs: every chunk is fetched twice and Steam's log
  fills with assertions.
- **Fetching DLC** writes to Steam's content folder like the game: point the link at a test folder first, always.
  A first run on a depot folder of its own is also the way to see `Downloading your DLC… 16 of 73`, Stop in the
  middle, and Build & Install going on from there (`dlc-fetched.json` in `LP_HOME`).
- **Never test "no link" on a Mac whose startup disk has room for the depot**: the check passes, as it should,
  and Steam really does download to the internal disk. Stopping that needs Steam force-quit (`steam_osx
  -shutdown` and `steam://ExitSteam` wait for the download to end).
- A run that reaches the build needs the gate: `startup-space` says no on a Mac short of free space, so a test
  there goes through **Go On Anyway**.

**A whole first install** with a stand-in Steam folder (the account that owns the game not signed in):
`RS_STEAM_DIR` at a folder with `logs/console_log.txt`, a `[Logged On, 4, 7]` line in `logs/connection_log.txt`
and `steamapps/content` a link to a real download of the depot and DLC, so `game-locate` finds them and Steam
isn't asked. No `_CommonRedist` there, so the redistributables come from Microsoft, and no account, so
`steam.env` has no `RS_STEAM_ID` and the copied saves aren't seen ("New profile" only): add the line by hand, as
a real install writes it. A new build is a new app to macOS: with the game files on another disk its first start
stands still, in `open`, until someone answers the removable volume question.

**Cases**, through the window: Install Steam, Steam is starting, Sign in to Steam, the wrong account and then
the right one, One step in Steam, Try Again, Stop and Build & Install after it, no room with Choose Folder…,
Cancel and Check Again, Apple's tools missing and arriving, Steam's failure retried and then the failure alert,
closing the window during a run; with the real Steam, the account that doesn't own Learn & Play, a whole
first-time run with the owning account, the DLC step with its Stop and resume, and a Steam told to quit during
the download. Not tried: a really small disk (`hdiutil attach` is refused in an agent's sandbox).

## Game runs

Use a copy of the build and a user folder of its own, so a rebuild or a run does not touch the installed
game or your progress. A build's `out/rs2014_lp` runs directly:

```sh
LP_GAME_DIR=<game folder> LP_USER_DIR=<test user folder> RS_STEAM_DATA_DIR=<test steam folder> \
  <build>/out/rs2014_lp 2> log.txt
```

- **The installed app** takes a folder of its own with `open -n --env LP_HOME=<folder> <app>`: give it a copy of
  the game files (`lp_install.py game-copy GAME --dest <folder>/game`, clones on the same volume) or pass
  `--env LP_GAME_DIR=<game folder>`, which reads that folder as it is (no copy check); `--env` passes any other
  variable. A binary outside the app reads no `LP_HOME`: without `LP_GAME_DIR` it looks for `out/game`, and
  shows a grey screen.
- **A profile**: an empty user folder stops at "Enter new profile name". Copy a test folder's `<account>/221680`
  into `RS_STEAM_DATA_DIR` and set `RS_STEAM_ID` to that account's ID. With `HOME` and `CFFIXED_USER_HOME` at a
  test home and no `RS_STEAM_ID` the Steam folder is `steam/1`, so a profile under another ID is not seen. With
  the real `HOME`, `RS_STEAM_DATA_DIR` alone is not a test folder: the Steam layer takes the account from
  Steam's `loginusers.vdf` and copies that account's saves into an empty folder (without `crd`). Keep a copy of
  `steam/`: a run that goes astray can change or delete the profile.
- **A changed game file**: run with a game folder of its own (`cp -Rc` clones it; set `LP_GAME_DIR`) rather than
  changing the installed copy. With the game files on another APFS drive, `game-copy --dest` makes `game/` a
  link to that drive; `RS_GAME_COPY_DIR=home` makes a real copy at DEST, or moves the one on the drive there.
- **Another volume and `open`**: a bundle without the permission stops in `open()` at its first look at that
  volume, with no window, until macOS's question is answered. It is not a hang; start the executable directly,
  or keep the app, its home and the game copy on the internal disk.
- A test copy of the app has the installed app's bundle id, so the computer-use tools find no window of it:
  snapshots are the way to see it.
- Before reporting a game bug, check whether the original game does the same on Windows. On a loaded machine a
  song may end by itself a few seconds in ([PLAN.md](PLAN.md#open-issues)): look at `uptime` first.

### Unattended runs

`LP_INPUT_SCRIPT` plays key presses (`win-x86/shims/win32_input.c`), comma-separated steps:

- `seconds:mac-keycode` presses a key for 100 ms, that many seconds after the window appeared or the last
  wait ended (not after the step before). Key codes are hex: Return `24`, Esc `35`, forward delete `75`, Shift
  `38`, Ctrl `3b`, Down `7d`, Up `7e`, Right `7c`; letters as on a Mac keyboard (`02,00,2d,08` is `danc`,
  `00,2e,1f,0f` `amor`).
- `@text` waits until `text` appears in the log (at most `LP_INPUT_WAIT` seconds, default 120). `@callback 1101`
  (the Steam stats callback, logged with `RS_STEAM_LOG=1`) means the title screen takes keys; `@DLC:` (with
  `RS_MODS_LOG=1`, the `mods: DLC: N archives mounted in F frames, T s` line) is the end of the DLC mounting,
  after the profile has loaded.

The script's log lines carry the seconds since it started and the wall time, to match them with snapshots. The
verified script, from the title to the note highway (title, profile, Learn a Song, the song, Play Song, Skip
Tuner, "Yes, continue without tuning"), which `tools/release/smoke.sh --lp` runs:

```
RS_STEAM_LOG=1 LP_INPUT_SCRIPT='@callback 1101,3:24,10:24,20:24,32:24,44:24,62:75,68:24'
```

Runs have lost Returns up to 35 s after the title came up (the cause was not found), so longer scripts keep a
margin and wait for the DLC (add `RS_MODS_LOG=1`):

| Route | Script |
|---|---|
| Search `danc`, the first match, to the highway | `@callback 1101,40:24,46:24,@DLC:,10:24,20:38,24:02,26:00,28:2d,30:08,34:24,46:24,64:75,70:24` |
| The same, the fourth row of the matches | `...,30:08,33:7d,35:7d,37:7d,40:24,52:24,70:75,76:24` |
| Path/Input, INPUT to its last value | `@callback 1101,40:24,46:24,@DLC:,10:3b,15:7d,19:7c,23:7c,27:7c` |
| Enumerate, then search `amor` and play | `@callback 1101,40:24,46:24,@DLC:,10:7d,14:24,22:24,27:7e,31:24,41:38,45:00,47:2e,49:1f,51:0f,55:24,67:24,85:75,91:24` |
| A game folder with no `dlc` songs (a clean depot), the first song | `@callback 1101,40:24,46:24,75:24,90:24,102:24,122:75,128:24` |
| No profile: New profile, a letter, Return | `@callback 1101,25:24,32:24,38:00,41:24,@DLC:` |
| Leaving the stage over and over (rounds of 74 s) | `...,@DLC:,10:24,22:24,34:24,52:75,58:24,86:35,89:7d,90:7d,91:7d,92:7d,94:24,108:24,126:75,132:24,160:35,...` |

Beside it:

- **`LP_WINDOW_SCRIPT=20:900x700,30:toggle`** resizes the window and toggles full screen at those seconds (a
  resize is the end of a user's drag: the log has `asked for as the game's resolution`, `d3d9: Reset 900x700`
  and `the game draws 900x700 at its own shape`); `quit`, `unfocus` and `focus` are the same steps as Command-Q
  and the window losing and regaining the keyboard, `audio` as the system's default output changing.
- **`LP_OFFSCREEN=1`** runs without a window and starts neither script, so the game stays on the title screen
  and a `quit` step never comes. Use `RS_FULLSCREEN=0` for a scripted run.
- **`tools/ws8/keyfifo`** ([Mac testing](../mac/TESTING.md#unattended-runs)) takes keys one at a time while the
  game runs, to find a route rather than repeat one: start `rs2014_lp` with `RS_KEY_FIFO=FILE
  DYLD_INSERT_LIBRARIES=<dir>/libkeyfifo.dylib`, then `echo "key return" > FILE`. In this edition Shift and Ctrl
  are `lpshift` and `lpctrl`. Send `front` before keys, or the game takes none. `nohup` and `env` drop
  `DYLD_INSERT_LIBRARIES` (no `keyfifo:` line in the log, and no key arrives): export the variables in the shell
  and start the binary from it. In zsh a job started with `&` also runs at nice 5. If the log says `lp: the
  window is hidden` at the title, keys don't arrive, `front` and `RS_KEY_DIRECT=1` included: click the window
  and press Return by hand, then go on.
- **Seeing a run**: `RS_GL_SNAPSHOT_DIR=DIR RS_GL_SNAPSHOT_SECS=3` and `RS_GL_FRAMETIMES=FILE`, as on the Mac.
  For anything measured keep the game's window in front (keyfifo's `front`, and nothing else activated during
  the run): the full-screen window hides when another app is active, and frame rate and CPU then are not a
  player's. The rearmed-mods library (`rearmed-mods.dylib` in `<LP_HOME>/mods/`, `RSMods.ini` in the game
  folder, `RS_MODS_LOG=1`) logs a `menu:` line for each screen. There is no virtual audio cable in this
  repository, so unattended runs have no guitar input.
- **Display sleep**: during a run with a window, `pmset -g assertions` lists `rs2014_lp` with
  `NoDisplaySleepAssertion`, gone after the quit. Waiting out a real screen saver timeout has not been tried.

**The screens by key**, from the start to the note highway (the `menu:` name in brackets). Times are from test
runs: wait for the log line, or look at a snapshot, rather than count on them.

| Screen | Keys | Notes |
|---|---|---|
| Title (TitleScreen) | Return | up about 9 s after the start with the intro skip, "ENTER Begin" at about 15 s; about 30 s with the intro. 10 s after `menu: TitleScreen` or the `callback 1101` line is safe |
| Profile list (ProfileSelect) | Return | `fwddelete` here deletes the profile. With no profile, Return starts a new one (below). With rearmed-mods' `ForceProfileLoad` the game goes to the main menu by itself |
| Main menu (MainMenu) | Return on LEARN A SONG | up about 20 s after the profile; wait for `mods: DLC:` and 10 s more. From LEARN A SONG down: SESSION MODE, NONSTOP PLAY, LESSONS, GUITARCADE, MULTIPLAYER, TONE DESIGNER, SHOP; with the overlay the first Down is ENUMERATE. Once in eight runs "The Ubisoft servers are not available" came up over it and took a Return: run again |
| Mission notices (MissionMenu) | Return on each | Mission Complete, New Mission, on a profile that has them |
| Song list (LearnASong_Songs) | Shift, the letters, Down, Return | Shift opens the search (`/` on the Mac). The default sort is DATE ADDED, so search rather than count rows. Letters 1.5-2 s apart, the second of two equal letters a second later; a letter is dropped now and then, so read the search text in a snapshot. Esc clears the search (with no match it closes it), and Esc with it closed leaves the list. A new account owns no DLC: Down to the first song with no DLC tag |
| Song page (LearnASong_SongOptions) | Return on Play Song | a song with only an Alternate Lead shows an X instead of a percentage on the Lead path; it still plays |
| Tuner (LearnASong_PreSongTuner) | forward delete, then Return | up about 30 s after Play Song on a warm cache. Forward delete is Skip Tuner, Return answers "Yes, continue without tuning". A Return on the tuner starts Calibration, which waits for a guitar (Esc is its Cancel) |
| "TUNE UP!" (SelectionListDialog) | Return | instead of the tuner on a profile with no input (DISCONNECTED) |
| Note highway (LearnASong_Game) | | 15-30 s after the tuner |

- **A new profile**: Return on New profile, a letter, Return. "Have you played Rocksmith before?": Down, Return
  on YES. "Would you like to brush up": Down, Return on NO. Return on LEAD GUITAR, RIGHT-HANDED and 3+3. Input
  selection: DIRECT CONNECT lists the inputs and goes on to Calibration, which needs a guitar (Esc comes back),
  so without one take DISCONNECTED (Down twice from REAL TONE CABLE, Return), Return on its two pages and on its
  notice over the main menu. Return on the Welcome notice and on New Mission.
- **Path/Input** (Ctrl on the main menu): Down to INPUT, Right steps REAL TONE CABLE, MICROPHONE, DISCONNECTED,
  DIRECT CONNECT; on DIRECT CONNECT, Down and Return list the inputs (10 rows and no more, with no scrolling)
  and Return takes one, shown as CURRENT INPUT. Esc asks "Calibrate now?" for a new input: Down, Return on "Not
  now" saves the profile; a second Esc cancels the question and stays on Path/Input. The choice reaches the
  profile only when the game saves it: a kill loses it.
- **In a song**: Esc opens the pause menu (LearnASong_Pause): RESUME, RESTART, TUNER, MIXER, CHORDS, EXIT (TUNER
  and MIXER greyed without an input; in the runs of the stage loop four Downs reached EXIT, so look at a
  snapshot before Return). EXIT leaves for the song's page in about 8 s, with no dialog; Esc there is the list,
  Esc again clears its search. Space in the pause menu is Riff Repeater (LearnASong_RiffRepeater, with a notice
  the first time): Down twice to SPEED, Left moves it, `key fwddelete` resumes. A finished song shows notices
  that each take Return, and a Return too many starts the next song; a DISCONNECTED song ends on the song's
  page, with no score screen.
- **The other modes**: SESSION MODE: a first visit shows two notices; Space starts it, then the tuner; Esc, Down,
  Return ends it. NONSTOP PLAY: Return on START, the tuner, the song; Esc pauses with SKIP on top. LESSONS: Down
  to a lesson, Return twice; Esc, Down, Return leaves. A lesson with practice (Bass 101, the third Down from the
  first video one): Return twice, Skip Tuner, Return; Esc there gives RESUME LESSON, EXIT LESSON, RESTART
  LESSON, TUNER, TECHNIQUE GUIDE, MIXER. GUITARCADE: TECHNIQUE GAMES, Return on a game, then Calibration "Make
  some noise!" waits for a guitar (Esc goes back); SCORE ATTACK works without one: Return on the song, Return on
  EASY, Skip Tuner, Return; three strikes end it about 30 s in, and Return goes back to the list. MULTIPLAYER:
  Return on the notice MULTIPLAYER INPUT SUPPORT, on the second profile, on CONTINUE, on the mode and the first
  song, on Play Song, Skip Tuner, Return: a split-screen song; Player 2's profile needs an input, or it stops at
  "It sounds like Player 2's cable is unplugged". TONE DESIGNER: Return on EDIT TONE.
- **Tools** (Space on the main menu): OPTIONS (Down 3), VISUAL SETTINGS (Down 2), VENUE MODE (Down 7), Return,
  Return on On: from then on a song opens a portal to a venue about 30 s in, with or without a guitar. Esc four
  times to the menu. The rooms of Session Mode are reverbs, not venues.
- **Quitting**: Esc on the main menu, Return on "Yes, quit" goes to the title, not out of the game; Esc there
  and Return on EXIT GAME is the game's own Exit, which saves the profile. keyfifo's `quit` ends the run.

**Checks that have a route.** What is the same in both editions is said once, in
[Mac testing](../mac/TESTING.md#unattended-runs); this is what differs here.

- **No intro screens** (`RS_MODS_INTRO`): this app logs nothing about the intro, so use snapshots every 3 s.
  With `RS_MODS_INTRO=0` the Ubisoft logo is up at about 6 s and the title at about 30 s. The failure to look
  for is an empty screen after the loading screen.
- **Auto Save warning** (`RS_MODS_AUTOSAVE`): with `RS_MODS_AUTOSAVE=0` "WARNING! This game uses an auto save
  feature" comes after "Loading profile…" and clears itself about 4 s later, so take snapshots every 2 s.
  `RC_HOOK_TRACE=autosave_warning` shows the handler either way. Not checked: a Real Tone cable or Microphone
  profile, and the tuner with the switch off.
- **Title logo** (`RS_REARMED`): a copy with `static.psarc.rearmed` shows the red "Re-ARMed 2026" tag across
  REMASTERED on the title, and with `RS_REARMED=0` the game's own logo. `ls -l` of `static.psarc` in the copy
  shows which one the app linked.
- **Direct Connect overlay**: the INPUT slider has four dots and its last value is DIRECT CONNECT, after which
  Available Inputs is enabled and shows the current input device. With `RS_MODS_OVERLAY=0` it has three (NORMAL,
  MICROPHONE, DISCONNECTED), and on the main menu the seventh Down reaches SHOP, whose Return shows "You must
  enable Steam Community In-Game to access the Shop." with nothing scanned: the game as it was.
- **Enumerate**: only `dlc_from` in `.rearmed/source.json` needs a folder of your own; copy the song there once
  `mods: DLC:` is logged, then the Enumerate script of the table. The log also has a second `mods: DLC:` line
  with one archive more. `song folder not there` for the depot's own `dlc` is normal: a depot has none.
- **Song ownership**: put the re-packed archive in the `dlc/` of a game folder of its own and make the copy with
  `gamefiles.Copier` (`steam.env` then lists the new AppID; `RS_PSARC_KEY_IMAGE` is the unpacked code image
  here). A search finds the song; with `RS_STEAM_DLC` set to the list without that AppID it finds nothing.
- **Custom songs** (the song-key table skip): search for a custom song in the copy's `dlc/` and play it;
  `mods: CDLC: song file signature check skipped` is logged as it loads. With `RS_MODS_BYPASS=0` the search
  gives "No Matching Songs": the song-key table is back and hides every custom song, so that switch is no check
  of ownership here.
- **All the song overrides in one build**: make the test copy with the install code,
  `gamefiles.Copier("learn-play", SRC, DEST, dlc_from=[]).run()`, from a clone of the game folder without its
  `.rearmed/`, so `steam.env` and `Update/patcharchives.psarc` come from the real code path; then one run per
  check, each from the title.
- **Rocksmith 1 songs** (`RS_STEAM_OWNED`): not run on Learn & Play yet, the test account owns none of them. The
  face's answer is covered by `test_steam32`.
- **Files a mod serves**, **Decoded song audio**, **The render API in a game**: as on the Mac, with the same
  dylibs. The render mod logs `[render_mod] on win-x86, API 1`; a song's `.wem` is in `audio/windows/` of
  `dlc/<song>_p.psarc`.
- **Mods**: `DIR/probe_mod.dylib` in `<LP_HOME>/mods/` and a script to the note highway, with
  `RC_HOOK_TRACE=game_frame,camera_set_frustum,too_many_inputs,si_open`. The log has `[rc] mod loaded:
  probe_mod.dylib` and `[probe]` lines (the overlay, `OV_VCALL`, `si_open`, each key and focus change, `[probe]
  file sha256`), snapshots have its magenta square at the top left, and `logs/netguard.log` its `mode=block`
  line. **No mod without the guard**: a copy of the app without `Contents/MacOS/libnetguard.dylib` logs `[rc]
  mods not loaded: no libnetguard.dylib beside the app`, and `LP_MODS_GUARD=1` (a restart that lost the guard)
  `the network guard did not load`; neither has a `[probe]` line.
- **The rearmed-mods library**: hotkeys must stay quiet in the song search and in a text entry dialog (a new
  profile's name), and fire again after Esc: `lpshift`, then `i`, `l`, `5`, `f`; `lpshift`, `qq`, Return (no
  match: the box stays open), `i`; a click, `i`. `song timer`, `volume:`, `lyrics` and `enumeration:` lines are
  what firing looks like.
- **7-string charts**: the log starts with `[rc] edits seven_string: on (N)` and `seven-string: on, N mid
  points`; a chart logs its RS7X block, its tuning's name (`names: ... -> "B STANDARD 7STR"`), the guitar
  rebuilt with 7 strings and the headstock bound, and `chords: SNG ... freed` on leaving. The snapshots show the
  seventh string, grey, above the low E.
- **The screen shape**: with `ScreenWidth` and `ScreenHeight` the main screen's size and `Fullscreen=0`, the
  game opens full screen at that shape: the log has `the game draws WxH at its own shape`, and the mods' `loft:
  camera` lines show each frustum keeping its 16:9 height. A window's resize: a size that isn't the screen's and
  `LP_WINDOW_SCRIPT=45:1200x600`; with `Fullscreen=1` and `RS_FULLSCREEN=0` the log says `the game is in full
  screen (Windowed Mode is not Windowed): its resolution stays`.
- **Riff Repeater's speed and the focus** (`rr_speed_ratio` and `ui_event` with a mod): the slider calls the
  point on each step, and ACCELERATE on goes on calling it in the song. Focus loss: another app in front during
  a song (`open -a Finder`); keyfifo's `front` comes back.
- **Leaving the stage over and over**, the check for a hang on the stage or between songs: the last script of
  the table. Esc on the highway, four Downs and Return exit to the song's page; Return is Play Song, forward
  delete and Return pass the tuner; the first Esc is at `58 + 28`, and each round adds 74 s. `RC32_LOG_RAISE`
  shows any `longjmp` or C++ exception on the way.
- **Finding which check drops a song**: a `wrap` line in `skip.txt` and an `F_<addr>` in `win32_overrides.c`
  that calls `W_<addr>` and logs its arguments or result (filtered by the return address, `[esp]` on entry) show
  what the game does for one song without changing it; one re-translation per set of probes.

### Measured runs

`tools/perf/run.py` measures this edition and the Mac edition the same way: the same settings file, profile,
mods, song and window, and one report with a column for each. It is described once, in
[Mac testing](../mac/TESTING.md#measured-runs). What differs here:

- The edition's entry in `CONFIG` names a build's `out/rs2014_lp`, and `LP_GAME_DIR` and `LP_USER_DIR` in its
  `env`; its `Rocksmith.ini` is `userdata/game/Rocksmith.ini`, where full screen is `Fullscreen=1`.
- The keys are keyfifo's, not `LP_INPUT_SCRIPT`'s, so that both editions get the same route from the same
  script; Ctrl and Shift go as `lpctrl` and `lpshift`.
- The network guard comes with the mods ([Unattended runs](#unattended-runs)): a run stops if the log says the
  mods were not loaded.
- The frame times have no game-frame events here, so that row of the report is empty; the threads have their
  names (`w32 game`, `w32 thread <start address>`).
- Cherub Rock needs no DLC of the test account in this edition.

A build's first play of the song on a screen has a stall or two in the window: the warm-up run is there for it.
Results are in [PERFORMANCE.md](PERFORMANCE.md).

### Shaders of a run

`LP_SHADER_LOG=DIR` tells what a run made. `python3 win-x86/shaders/record_programs.py DIR/log.tsv` adds its
programs to `win-x86/shaders/programs.tsv`; a line in the list holds hashes and state keys only, but the rest of
`DIR` is the game's, so it stays on this Mac. To see what the corpus missed, `RS_MSL_LOG=1` logs each program
translated at run time, and with `RS_METAL_CACHE_STATS=1` the exit line's `lib_ms` is the time spent compiling.

A first play with no shader cache, without touching the user's: give the test app a bundle identifier of its own
(`CFBundleIdentifier` in `Info.plist`, then `codesign --force --sign -`), and before each run delete
`$(getconf DARWIN_USER_CACHE_DIR)<that identifier>` and the test home's `Library/Caches/rs-rearmed`. Start the
executable directly, with `HOME` and `CFFIXED_USER_HOME` at the test home, or keep the app, its home and the
game copy on the internal disk (`RS_GAME_COPY_DIR=home`) for a run by `open`
([Game runs](#game-runs)). Metal's cache is the same either way ([METAL.md](../mac/METAL.md#safe-load)).

### By hand and with computer use

The keys and the way from the title to a song are in [INSTALL.md](INSTALL.md#playing). Keys reach the game
only while its window is in front, so screen-control tools need full-screen control (keyfifo's `front` brings
it there). They find applications by bundle: the installed app is one; launch it with `open` and grant it. For
a build that is not installed, wrap the binary in a small `.app` (an `Info.plist` with a bundle identifier,
`CFBundleExecutable` the binary, `LSEnvironment` for `LP_GAME_DIR`, `LP_USER_DIR` and other variables, the
runtime library next to it).

The full-screen keys are pressed as on the Mac ([Mac testing](../mac/TESTING.md#by-hand-and-with-computer-use)),
with the process `rs2014_lp`: at the title each of Control-Command-F and Shift-Command-F went from the game's
full screen to a window of the screen's shape and back, with `lp: window macOS:` lines in the log.

- **An interface's channel and gain**, as on the Mac ([Mac testing](../mac/TESTING.md#by-hand-and-with-computer-use)),
  with `RS_AUDIO_LOG=1` and `RS_AUDIO_INPUT_FILE`. The log has `guitar input "<device>": channel N, gain G dB`
  when the capture stream opens, `input level:` for each level set, and a peak for each open input every five
  seconds, the microphones among them.
- **Two players**: load the second profile on its own, `lpctrl`, Down to INPUT, DIRECT CONNECT, Down and Return
  lists the inputs, Return takes `HX Stomp channel 6`, say; leave by the menus, or it isn't saved. Load the
  first profile the same way on another channel, then MULTIPLAYER: a song with each player on their channel.
- **Full screen on a second display**: not run on Learn & Play. The Mac edition's way of moving the window,
  sending the keys and reading the frame applies; here the game's own full screen comes from its Windowed Mode
  setting.
- **The output device**: as on the Mac; `LP_WINDOW_SCRIPT`'s `audio` step stands in for the change.
- **Ubisoft's screens**: a build without the online layer (or a log without `lp: online: offline RSConnect layer
  +extension +SkipLogin +sign-in`) shows them: Esc on the Uplay login and on "Sign in to access online
  features", Return on "The Ubisoft servers are not available". Never sign in.

### The capture VM

The seed capture ([win-x86/unpack/README.md](../../win-x86/unpack/README.md)) drives a Windows guest in QEMU,
over VNC or from the monitor socket (`sendkey`, `screendump`):

- The guest's keyboard follows the layout chosen at install, and clipboard paste does not reach it. Look at the
  screen before pressing Enter: a wrong layout turns `\` and `"` into other keys, and keys sent faster than
  about three a second get dropped.
- Keys sent right after logging in are lost: wait for the tray icons. The Run box keeps its last command,
  selected, so Enter on an unchanged box runs it again.
- `Rocksmith2014.exe` needs its DLLs beside it. A disc image built with `hdiutil makehybrid` and put in with
  the monitor's `change ide1-cd0` brings files in without a reboot; the shared folder is read only at boot.
- The game's window covers the desktop while its Steam error is open. `powershell -noexit` from the Run box
  opens on top of it. PowerShell takes about two minutes to start in the guest.
- The evaluation image says its license is expired; setting the guest's clock back did not change it. A capture
  takes minutes.

## Diagnostics

The app logs to `<LP_HOME>/logs/last-run.log`, and with mods to `logs/netguard.log`. All off unless set (from
`getenv` in `win-x86/`, the runtime and the shared shims):

| Switch | What |
|---|---|
| `LP_LOG=1\|2\|3` | imports: the first call of each / every call / and every call into the seam's COM objects (D3D9, audio, Steam) with its result; also every `LoadLibrary` and `GetProcAddress` |
| `LP_LOG_FILES=1\|2` | files the game opens, including the missing ones it probes for |
| `LP_LOG_DLC_CLOSE=1` | for the first closes of a song archive the game read from, the code addresses on the caller's stack (how the DLC update's head was found) |
| `RS_FILE_LOG=1` | each open a mod's file provider served, and each folder listing it added names to (`[rc] file:`) |
| `RC_HOOK_TRACE=NAME,NAME` | the raw calls of those hook points, the first 8 of each, refused ones too: return address, `ecx edx esi edi`, four stack words, then `eax` and `st0`. The way to check a table's address and prototype in a run |
| `LP_LOG_TO=FILE` | append the log there |
| `LP_FRAME_LOG=MS` | every frame longer than MS ms, with the game thread's CPU, context switches and what ran in it (links, file reads, first import calls, exceptions, the slowest COM methods); the frame rate every 10 s |
| `LP_THREAD_CPU=S` | each thread's CPU over the last S seconds, by name (`w32 game`, `w32 thread <start address>`), and a line of what has gone of the guest's memory: the arena, `VirtualAlloc` and `VirtualFree`, threads made, handles open, GL programs |
| `LP_PROF_IMPORTS=S` | import calls and indirect-call targets per interval |
| `LP_PROF_NEW=N` | indirect-call targets seen for the first time from profile round N on |
| `LP_DUMP_WAITS=S` | each guest thread's pending wait, its handles and their state, every S seconds |
| `RC32_LOG_RAISE=N` | the first N guest exceptions: code, C++ type, guest and host stack |
| `LP_SHADER_LOG=DIR` | a line in `DIR/log.tsv` per shader created and program linked, the bytecode in `DIR/sm`, the GLSL in `DIR/glsl` ([Shaders of a run](#shaders-of-a-run)) |
| `RS_GL_FRAMETIMES=FILE` | a line per Present |
| `RS_METAL_PASS_TIMES=FILE` | the GPU's time in each render pass of a frame, as on the [Mac](../mac/TESTING.md#diagnostics) |
| `RS_GL_SNAPSHOT_DIR=DIR`, `RS_GL_SNAPSHOT_SECS=N` | a PNG of the frame every N seconds |
| `RS_GL_TEX_LOG=FILE`, `RS_GL_TEX_DUMP=DIR`, `RS_GL_TEX_MARK=HASH,HASH` | the texture log, as on the [Mac](../mac/TESTING.md#diagnostics) |
| `RS_AUDIO_LOG=1` | device buffers, stream setups, real-time threads, dropped input, each level set, each input's peak and clips every few seconds |
| `RS_STEAM_LOG=1` | the Steam face's calls (`[steam-shim]`) |
| `RS_MODS_LOG=1` | the built-in fixes: the open-file limit, the DLC mounting's end (archives, frames, time, and the path cache's and number hash's counts), each song-file signature skip, each DLC preview job that fails offline |
| `RS_MODS_FIXES=0`, `RS_MODS_BUDGET_MS=N`, `RS_MODS_BYPASS=0` | the built-in fixes off, the DLC mounting's time per frame (0: one archive a frame), the song-file signature check on |
| `RS_NETGUARD_BT=1` | with mods: the backtrace of each call the network guard refuses, in `netguard.log` |
| `RS_D3D9_DISPLAY=WxH` | the desktop size the game sees |
| `RS_FULLSCREEN=1\|0` | full screen or a window, whatever the game's setting |
| `RS_WINDOW_ANY_SHAPE=0` | 16:9 with the game's bars, and a resized window keeps the resolution ([DESIGN.md](DESIGN.md#screen-and-window)) |
| `RS_METAL_RETINA=1` | render in backing pixels |
| `RS_METAL_UPSCALE=0`, `RS_METAL_COLORS=vivid` | no MetalFX upscaling on a Retina screen; the layer left untagged (not sRGB) |
| `RS_METAL_MSAA=1\|2\|4\|8`, `RS_METAL_ANISO=1-16` | MSAA on the screen-sized targets with depth (1 off), the least anisotropy of mipmapped textures (1: the game's) |
| `RS_AUDIO_RS_NAMES=0` | keep CoreAudio's device names |
| `RS_AUDIO_FRAMES`, `RS_AUDIO_RT`, `RS_AUDIO_INPUT_TRIM`, `RS_AUDIO_SET_RATE` | the audio policy ([INSTALL.md](INSTALL.md#settings-and-switches)) |
| `RS_REARMED=0` | the game's own title logo ([INSTALL.md](INSTALL.md#the-title-logo)) |
| `LP_NATIVE_MATH=0` | the CRT's math translated instead of libm |
| `LP_NATIVE_MEM=0` | the CRT's `memcpy`, `memmove`, `memset` and `memcpy_s` translated instead of the host's |
| `LP_QOS=0` | the default QoS for the game's threads |
| `LP_WAKE_ALL=1` | every wait woken on every change (the old way) |
| `LP_D3D9_EAGER=1` | dynamic buffers uploaded at `Unlock` instead of at the draw |
| `LP_INPUT_SCRIPT`, `LP_INPUT_WAIT`, `LP_WINDOW_SCRIPT`, `LP_OFFSCREEN=1` | [Unattended runs](#unattended-runs) |
| `LP_GAME_DIR`, `LP_USER_DIR`, `LP_HOME`, `LP_IMAGE`, `LP_DLL_DIR`, `LP_ARGS`, `RS_STEAM_DATA_DIR` | folders, images and the command line (`win-x86/app/lp_main.c`) |

The Steam face also reads the Mac layer's `RS_STEAM_DLC`, `RS_STEAM_OWNED`, `RS_STEAM_ID`, `RS_STEAM_PERSONA`,
`RS_STEAM_LANGUAGE` and `RS_STEAM_KEEP_CREDENTIALS` ([shims/steam/README.md](../../shims/steam/README.md));
libRSOpenGL's `RS_GL_*`, `RS_MTL_*` and `RS_METAL_*` switches apply as on the Mac
([Mac testing](../mac/TESTING.md#diagnostics)). The game's own audio log: `[Audio] DumpAudioLog=1` in
`Rocksmith.ini` writes `audiodump.txt` beside it, listing each input with its USB IDs.

## Offline checks

The game must never call home.

- The Win32 seam's ws2_32, iphlpapi and wininet refuse every connection and `ShellExecuteA` opens nothing
  (`win-x86/shims/win32_net.c`, `win32_misc.c`); with `LP_LOG=1` the log shows each attempt.
- The Steam face never loads Valve's library and refuses the Ubisoft and Valve DLLs by name.
- The online layer never reads `crd`; with `LP_LOG_FILES=1`, no `open "crd"` should appear in a run.
- Without mods the Mac port's network guard is not loaded; these checks are the seam's own. With a mod in
  `mods/` the app restarts with `libnetguard.dylib` inserted, and no mod runs without it
  ([Mods](#unattended-runs)); `logs/netguard.log` then starts with `mode=block` and lists every refused
  attempt. The guard's own test is the Mac's (`tools/ws8/netguard/build.sh --test`,
  [Mac testing](../mac/TESTING.md#offline-checks)): the same library is the one beside this app.

## Release checks

As on the Mac ([Mac testing](../mac/TESTING.md#release-checks), [Making a release](../release/README.md)):
`tools/release/scope.py` says which of the suites in [Tests in the repository](#tests-in-the-repository) and
which game runs a release's changes call for, and `tools/release/check.py` runs the suites. The CRT, D3DX and
Bink probes and the 32-bit difftest on real DLLs are not among them: run them by
hand for a change to what they cover.

Learn & Play's run for a release is `tools/release/smoke.sh --lp`: the first input script of
[Unattended runs](#unattended-runs) on a test build (`RS_SMOKE_LP_EXE`, with `LP_GAME_DIR`, `LP_USER_DIR` and
`RS_STEAM_DATA_DIR` as in [Game runs](#game-runs); `RS_SMOKE_LP_SCRIPT` for another script, `RS_SMOKE_HOLD` for
the seconds on the highway), a wait once the last key is sent, and a check that the game is still up; the last
snapshot shows the highway. Not run yet as a script: the input script itself is the verified one.
