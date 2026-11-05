# Testing the Windows port

What this repository can test, how to run it, and how the game is run unattended and by hand. The Mac
port's tests: [Mac testing](../mac/TESTING.md).

## Tests in the repository

None of these needs the game. The probes need Microsoft's or the game's DLLs as inputs; they are read from
where you point them and never copied into the repository.

| Area | Command | What it checks |
|---|---|---|
| Installer | `python3 -m unittest discover -s win-x86/install -p 'test_*.py'` (also ctest `win-x86.install`) | the install steps with stand-ins made by the test: cabinets and self-extracting exes for the redistributables, a Steam folder and its logs, `file://` downloads, the build with a fake translator and compiler, the app's copy of the game files (`game-copy`, `game-refresh`), the app bundle, cleaning; the committed harvester against its SHA-256. `LP_REDIST_REAL=1` also extracts the real DLLs from this Mac's Steam, `LP_GAME_REAL=<depot folder>` checks a real download |
| Shader corpus sources | `python3 -m unittest discover -s win-x86/shaders -p 'test_*.py'` (also ctest `win-x86.shaders`) | `lp_shaders` on hand-assembled bytecode at odd offsets: a pair is made for its pixel shader, a shader that isn't there is only counted; `record_programs.py` drops the run's shader numbers and merges. The warm-up itself is `platform.warm.metal` of the [Mac tests](../mac/TESTING.md) |
| Menu overlay | `python3 tools/play-native/test_cache_overlay.py` (also ctest `play-native.cache_overlay`) | shared with the Mac port ([Mac testing](../mac/TESTING.md#tests-in-the-repository)): the menu edits, the psarc and 7z readers and writers, the overlay made from a cache and left out when the cache has the entries; with a real `cache.psarc` (`RS_TEST_CACHE=<depot folder>/cache.psarc`, or the installs' copies) also the real files |
| Kernel32 waits, input hooks | `make -C win-x86/shims/tests test OUT=DIR` | `input_sink` and `set_app_focus` (`test_input`: events held for the game thread, the Mac's values, a pre hook keeping one from the game); events, mutexes, semaphores, wait-any and wait-all, timeouts; an idle waiter is not woken by other objects; a wait keeps objects whose handles are closed; `PulseEvent`; `CloseHandle` against the other handle calls (`test_close`, under the address sanitizer); the open-file limit from 256 (`rc_nofile.h`, shared with the Mac port) |
| Audio layer | `make -C win-x86/shims/audio test OUT=DIR` (`test_audio --list` prints the endpoints the game would see) | the WASAPI and winmm faces on CoreAudio: endpoints and names, formats, rings, exclusive event mode, trimming |
| Guitar input | ctest `audio.input` (`shims/coreaudio/test`) | shared with the Mac port: `guitar-input.ini` and the switches, the default file, a number in any locale, a channel across streams, the gain, the clip, the peak and clip counts, on made-up buffers |
| Steam face | `make -C win-x86/shims/steam test OUT=DIR` | the guest ABI end to end: exports, interface vtables, `thiscall`, callbacks and call results |
| Online layer | `make -C win-x86/online test OUT=DIR` | the three vtables in guest memory with fake game functions: every replaced slot, the posted events, a layout that does not match left alone |
| D3D9 layer | `win-x86/test/d3d9_test.sh <cmake build dir> [out dir]` (needs `cmake --build <dir> --target opengl`) | the layer's host test on both libRSOpenGL backends: fixed-function draws checked pixel by pixel, every stage operation, lights, fog, generated texture coordinates, the draw-time upload of dynamic buffers, the wait in Present while the window is hidden (1,272 checks) |
| Window rules | `cc -I win-x86/app win-x86/test/lp_screen_test.c win-x86/app/lp_screen.c -o lp_screen_test && ./lp_screen_test` | the window mode a style or back buffer means, `RS_FULLSCREEN`, sizes on a small screen and with Retina pixels, the pointer in a letterboxed back buffer |
| CRT | `win-x86/crt/tests/build.sh <msvcr100.dll> [<msvcp100.dll>] OUT`, then `OUT/crt_probe <msvcr100.dll> [<msvcp100.dll>] [-v] [-bench] [-eh OUT/eh.dll]` | the translated CRT without the game: start-up, heap, strings, `printf`/`sscanf`, iostreams, `DllMain` attach and detach; `math_probe` (linked in): every native math override against the translated export over 3,000-5,500 inputs each (result bits, errno, x87 depth, return address); `-bench` times them; `-eh`: C++ throw and catch, unwinding through inner frames, `__except`, `setjmp` and `longjmp` on that CRT (`test/x86/ehcases.py`, needs clang's i386 PE target) |
| D3DX | `win-x86/d3dx/tests/build.sh <dir with the DLLs> OUT [<cmake dir>]`, then `OUT/d3dx_probe OUT [-v] [step ...]` | the translated d3dx9_42 and D3DCompiler_42: matrices, buffers, the assembler, the HLSL compiler and constant tables, image info, an effect against the D3D9 layer |
| Bink | `win-x86/bink/tests/build.sh <bink2w32.dll> OUT <cmake dir>`, then `OUT/bink_probe OUT/in/bink2w32.dll <file.bk2> [-n frames] [-png out.png] [-wait]` | the translated Bink on the game's Win32 seam: header, every frame a picture, the PCM; `.bk2` files come from the game's `video.psarc` and `etudes.psarc` (`tools/psarc_extract.py <psarc> <dir> --match .bk2`). The audio check wants a peak over 64 in the frames it decodes, so a video that opens quietly fails it with `-n 60`: use another video or more frames |
| Tables | `python3 tools/targets/check.py` | shared with the Mac port: every name in `mac-x64` and `win-x86`, and every hook point's prototype against the Mac's |
| Hook points and mods | `make -C tools/ws7/recomp/test/hooks test OUT=DIR` (also ctest `recomp.hooks`) | shared with the Mac port: one mod dylib in the 64-bit and the 32-bit runtime, for each convention: what a hook reads, an argument it changes, a result it sets, a call it does itself, game code it calls first; adapted calls, refusals, names by run-time lookup, the mods folder; the cost of a point without hooks; `probe_mod.dylib` for the game runs |

### The recompiler

The 32-bit front end's differential test runs each translated function and the original bytes under
Unicorn and compares the end state ([recompiler](../recompiler/README.md#tests)). It needs a Python with
Capstone and Unicorn (Unicorn is for maintainers only and not in `requirements.txt`'s packages:
`pip install unicorn`):

```sh
cd tools/ws7/recomp
UC_IGNORE_REG_BREAK=1 "$PY" -m test.x86.handcases --out DIR                    # no DLL needed
UC_IGNORE_REG_BREAK=1 "$PY" -m test.x86.difftest IMAGE.dll --out DIR --cases 6 --jobs 8
```

`difftest` takes the DLLs from the command line or `RC32_TEST_DLLS` and skips without one. The plain DLLs
in the game folder are the usual inputs (`bink2w32.dll`, `steam_api.dll`, `systemdetection.dll`). Two known
mismatches, neither in code the game runs: one case in `systemdetection.dll`'s CPUID parser (`10010c50`), and
`steam_api.dll`'s `log10` on its x87 path (`3b4213e0`, five of six cases). That one is the CRT's hand-written
assembler, which reads the flags a helper left across a call and `ret` (`call 0x3b421e45` then `je`); the flags
are C locals ([recompiler](../recompiler/README.md#what-the-c-looks-like)), so the translation does not see them.
The game never loads `steam_api.dll`: the Steam face stands in for it. `msvcr100.dll` as an input shows the same in
41 functions (the math exports, `_inp`, `_outp`): every math export an image imports is a native override
(`win32_crtmath.c`), so the translated ones do not run.

## Installer tests

Besides the unit tests above, the install is checked by running `tools/play-native/learn-play.sh` with a
test build folder (`RS_BUILD_DIR`) and a test app path (`LP_NATIVE_APP`); the app then still uses
`~/Library/Application Support/RS Re-ARMed/learn-play` unless it is started with `LP_HOME`. The installer
app has no automated test; its preferences go to the real user's defaults even with a test `HOME`
([Mac testing](../mac/TESTING.md#installer-tests)).

### The installer app

How each screen was driven, with the computer-use `app_*` tools (bundle id
`io.github.bbelgg.rs-rearmed.installer`), in the standalone form a user gets. The app is copied out of the
repository, so that it downloads its source, and started from a shell, so that it takes these:

```bash
env HOME=<test home> CFFIXED_USER_HOME=<test home> TMPDIR=<folder> \
    RS_STEAM_DIR="<real home>/Library/Application Support/Steam" RS_SOURCE_URL=file://<source.tar.gz> \
    "<copy>/RS Re-ARMed Installer.app/Contents/MacOS/RS Re-ARMed Installer" -BuildFolder <folder>
```

- `RS_SOURCE_URL`: a `git archive --prefix=rs-rearmed/` of the commit under test, for the tag that isn't published
  yet. Delete `.installer-version` in the source folder to make it unpack a new one.
- `-BuildFolder`: typing into the field does not work from the background (`app_click` then `app_type` is refused),
  so the folder comes as a launch argument. Starting a run writes it to the real defaults: note
  `defaults read io.github.bbelgg.rs-rearmed.installer` first and put `BuildFolder` back afterwards.
- `RS_STEAM_DIR`: a fake `HOME` must not lose Steam. `steam_command` runs Steam's launcher with the home Steam's
  folder is in, so the running Steam gets the command; with the fake home it would start a second, new Steam.
- A fresh download: `content` in `Steam.AppBundle/Steam/Contents/MacOS/steamapps` is a link: point it at an
  empty folder, and back as soon as the Copying step is over (Steam's folder is the same disk as the test home, so
  the copy is clones and stays). Don't send a second `+download_depot` while one runs: it works, but every chunk is
  fetched twice and Steam's log fills with assertions.
- Screens: **Build & Install** is `app_click` by element index; the box above the log is plain AX buttons (the
  first button, then Stop); alerts are a window of their own (`app_list_windows`, then `app_screenshot` with its
  `window_id`). The app's Quit menu item reads as disabled from the background; the window's close button works.
  Steam's windows are not listed by `app_list_windows`, and it has no menu bar for `app_menu`: read its logs.

Every message of the box can be seen without touching Steam, with a stand-in:

- `RS_STEAM_DIR=<folder>` with `logs/connection_log.txt`, `logs/console_log.txt` and `config/loginusers.vdf` written
  by hand: a `[Logged On, 4, 7] [U:1:43] ...` line signs in, a new account number is another account.
- `LP_STEAM_EXE=<script>`: Steam's launcher. The script writes to `console_log.txt` what Steam would:
  `Downloading depot 221683 (103 files, 5363 MB) ...`, `Depot download failed : missing license for depot
  (No subscription) `, `Depot download complete : "..." (manifest 1) `, and clones the clean depot into
  `steamapps/content/app_221680/`. When the path doesn't exist, Steam isn't installed.
- `LP_STEAM_PROCESS=<name>`: the process that counts as Steam: a small program under that name, running or not.
  A copy of `/bin/sleep` is killed by macOS; compile one. Have it running before Build & Install: with it not running, the DLC step
  opens the real Steam by its bundle identifier, which `LP_STEAM_EXE` doesn't stand in for.
- `RS_XCODE_SELECT=<script>`: fails `-p` until a file is there, for Apple's tools.
- `LP_CONFIG=<target.toml with a huge depot_disk_bytes>`: no room. Change the file with an atomic `mv` while the
  alert is up (a `cp` can be read half written), then Check Again.
- `hdiutil attach` is refused in the agent's sandbox, so a really small disk was not tried.

- Skip on Preparing shaders: the step is 16 s on Learn & Play, so for a test put a `sleep` at the top of the build folder's copy of `warm-shaders.sh`;
  Build Again takes under a minute and leaves it there. `app_click` on Skip answers `cannotComplete` when the finish sheet opens before it returns:
  the click worked. The sheet says the shaders are not prepared. A finished run, then Build Again and Skip, is the case to try: the note went by what the run before found.

- `RS_XCODEBUILD=<script>` with `RS_XCODE_SELECT` printing a path in a `.app`: Xcode not set up. Exit 69 until a
  file is there. Stop before the run goes on: with the real Steam the next step would fetch DLC.

Fetching DLC writes to Steam's content folder like the game: point the link at a test folder first, always. A first
run on a depot folder of its own is also the way to see `Downloading your DLC… 16 of 73`, Stop in the middle, and
Build & Install going on from there (`dlc-fetched.json` in `LP_HOME`).

A whole first install with a stand-in Steam folder (the account that owns the game not signed in): `RS_STEAM_DIR` at a
folder with `logs/console_log.txt`, a `[Logged On, 4, 7]` line in `logs/connection_log.txt` and `steamapps/content` a link
to a real download of the depot and DLC, so `game-locate` finds them and Steam isn't asked. No `_CommonRedist` there, so
the redistributables come from Microsoft, and no account, so `steam.env` has no `RS_STEAM_ID` and the copied saves aren't
seen ("New profile" only): add the line by hand, as a real install writes it. A new build is a new app to macOS: with the
game files on another disk its first start stands still, in `open`, until someone answers the removable volume question.

A run that reaches the build needs the gate. `startup-space` says no on a Mac short of free space, so a test there
goes through **Go On Anyway**.

**Never test "no link" on a Mac whose startup disk has room for the depot**: the check passes, as it should, and
Steam really does download to the internal disk. Stopping that needs Steam force-quit (`steam_osx -shutdown` waits
for the download to end, `steam://ExitSteam` too).

Checked this way, through the window: Install Steam, Steam is starting, Sign in to Steam, the wrong account and then
the right one, One step in Steam (and the console URL reaching the real Steam), Try Again, Stop and Build & Install
after it, no room with Choose Folder…, Cancel and Check Again, Apple's tools missing and arriving, Steam's failure
retried and then the failure alert, closing the window during a run. With the real Steam: the wrong account, with
the account that doesn't own Learn & Play signed in, through the app (Stop ends it); a whole first-time run with the
owning account; the DLC step with its Stop and resume; a Steam told to quit during the download, which waits
for the download to end and then quits.

## Game runs

Use a copy of the build and a user folder of its own, so a rebuild or a run does not touch the installed
game or your progress. A build's `out/rs2014_lp` runs directly:

```sh
LP_GAME_DIR=<game folder> LP_USER_DIR=<test user folder> RS_STEAM_DATA_DIR=<test steam folder> \
  <build>/out/rs2014_lp 2> log.txt
```

The installed app takes a folder of its own with `open -n --env LP_HOME=<folder> <app>`: give it a copy of
the game files (`lp_install.py game-copy GAME --dest <folder>/game`, clones on the same volume) or pass
`--env LP_GAME_DIR=<game folder>`, which reads that folder as it is (no copy check); `--env` passes any
other variable.

**An interface's channel and gain**, as on the Mac ([Mac TESTING](../mac/TESTING.md#game-runs)): the same steps, with
`RS_AUDIO_LOG=1` and `RS_AUDIO_INPUT_FILE`. The log has `guitar input "<device>": channel N, gain G dB` when the capture
stream opens, and a peak for each open input every five seconds, the microphones among them.

**No intro screens** (`RS_MODS_INTRO`, as on the Mac, [Mac TESTING](../mac/TESTING.md#game-runs)): the
game should reach the title without the logos and legal screens, and with `RS_MODS_INTRO=0` play them. The
failure to look for is the title never coming (an empty screen after the loading screen). The scripts
below wait for the title on a log line, so they need no change.

**Title logo** (`RS_REARMED`, as on the Mac): a copy with `static.psarc.rearmed` shows the red "Re-ARMed 2026"
tag across REMASTERED on the title, and with `RS_REARMED=0` the game's own logo. No keys are needed: with
`RS_GL_SNAPSHOT_SECS=3` the title is in the snapshots by about 30 s, on a busy Mac too. `static.psarc` in
the copy shows which one the app linked (`ls -l`).

### Unattended runs

`LP_INPUT_SCRIPT` plays key presses (`win-x86/shims/win32_input.c`), comma-separated steps:

- `seconds:mac-keycode` presses a key for 100 ms, that many seconds after the window appeared or the last
  wait ended: Return is `24`, Esc `35`, forward delete `75` (hex);
- `@text` waits until `text` appears in the log (at most `LP_INPUT_WAIT` seconds, default 120).
  `@callback 1101` (the Steam stats callback, logged with `RS_STEAM_LOG=1`) means the title screen takes
  keys.

The verified script, from the title to the note highway (title, profile, Learn a Song, the song, Play Song,
Skip Tuner, "Yes, continue without tuning"):

```
RS_STEAM_LOG=1 LP_INPUT_SCRIPT='@callback 1101,3:24,10:24,20:24,32:24,44:24,62:75,68:24'
```

With the intro skip the title is up about 9 s after the start. On 2026-11-04 it took Return by 12 s in a run
pressing Return every 1.5 s, but runs on 2026-11-03 lost Returns up to 35 s (the cause was not found), so scripts keep
their margin. What held in every run on 2026-11-03: Return 40 s and 46 s after the title (title, profile), then
`@DLC:` (with `RS_MODS_LOG=1`, the `mods: DLC: N archives mounted` line once the profile has loaded), then
the main menu steps counted from there:

```
RS_MODS_LOG=1 RS_STEAM_LOG=1 LP_INPUT_SCRIPT='@callback 1101,40:24,46:24,@DLC:,10:24,20:38,24:02,26:00,28:2d,30:08,34:24,46:24,64:75,70:24'
```

(Learn a Song, Shift, `danc`, the first match, Play Song, Skip Tuner, "Yes, continue without tuning".) Once
in eight runs "The Ubisoft servers are not available" came up over the main menu and took a Return (the
cursor then sits on UPLAY); run again. For Path/Input use `10:3b,15:7d,19:7c,23:7c,27:7c` after `@DLC:`.

The INPUT setting (the Direct Connect overlay check): from the main menu Ctrl opens Path/Input, Down
moves to INPUT, Right steps through its values. Times count from the last wait, not from the step before:

```
RS_STEAM_LOG=1 LP_INPUT_SCRIPT='@callback 1101,3:24,10:24,18:3b,23:7d,27:7c,31:7c,35:7c'
```

With the overlay the slider has four dots and its last value is DIRECT CONNECT ("Use a cable other than
the Real Tone Cable…"), after which Available Inputs is enabled and shows the current input device; with
`RS_MODS_OVERLAY=0` it has three (NORMAL, MICROPHONE, DISCONNECTED). Snapshots every 2 s show each step.

A user folder with no profile: the title's Return goes to New profile, which takes a letter and Return (`@callback
1101,25:24,32:24,38:00,41:24,@DLC:`), then the path choice (Return), and first-run screens up to Calibration, which
wait for a guitar; Esc is its Cancel. Without `RS_STEAM_ID` the Steam folder is `steam/1`, so a profile under another
ID is not seen. Return 25 s after the Stats callback got through in both runs that tried it on 2026-11-04.

The tuner is up about 30 s after Play Song on a warm cache. A sixth Return before the tuner starts
calibration instead, which waits for a guitar. The script's log lines carry the seconds since it started and
the wall time, to match them with snapshots. `LP_WINDOW_SCRIPT=20:900x700,30:toggle` resizes the window and
toggles full screen at those seconds; `quit`, `unfocus` and `focus` are the same steps as Command-Q and as the
window losing and regaining the keyboard, `audio` as the system's default output changing. `LP_OFFSCREEN=1` runs without a window.

**Mods**: `make -C tools/ws7/recomp/test/hooks OUT=DIR`, `DIR/probe_mod.dylib` into `<LP_HOME>/mods/`, and the script to the note
highway above, with `RC_HOOK_TRACE=game_frame,camera_set_frustum,too_many_inputs,si_open`. The log has `[rc] mod loaded:
probe_mod.dylib`, `[probe]` lines for the overlay, `OV_VCALL`, `si_open`, each key and focus change; snapshots have its magenta square at the top left; other `[probe]` lines (`[probe] file sha256`: the file that ran), and
`logs/netguard.log` its `mode=block` line. The screens are the script's own: the mod changes nothing. No mod without the guard:
a copy of the app without `Contents/MacOS/libnetguard.dylib` logs `[rc] mods not loaded: no libnetguard.dylib beside the app`, and
`LP_MODS_GUARD=1` (a restart that lost the guard) `the network guard did not load`; neither has a `[probe]` line. With the game files on another APFS drive, `game-copy --dest` makes `game/` a link to that drive; `RS_GAME_COPY_DIR=home`
makes a real copy at DEST, for a test home wholly on the startup disk. A test copy on
another volume and `open`: a bundle without the permission stops in `open()` at its first look at that volume, until the
question is answered (`sample` shows `rc_mods_count`, `scandir`, `__open_nocancel`). It is not a hang.

Display sleep: during a run with a window, `pmset -g assertions` lists `rs2014_lp` with `NoDisplaySleepAssertion`
named "Rocksmith 2014 is preventing your Mac from sleeping.", counted under `PreventUserIdleDisplaySleep`. It is
gone after the quit, and an `LP_OFFSCREEN=1` run has only the `game` activity's `PreventUserIdleSystemSleep`.
Checked on 2026-11-04 with `RS_FULLSCREEN=0 LP_WINDOW_SCRIPT=30:quit`. Waiting out a real screen saver timeout was not.

Screenshots: `RS_GL_SNAPSHOT_DIR=DIR RS_GL_SNAPSHOT_SECS=3` (libRSOpenGL's, as on the Mac). Frame times:
`RS_GL_FRAMETIMES=FILE` (the Mac port's format). For anything measured keep the game's window in front (keyfifo's `front`,
and nothing else activated during the run): the full-screen window hides when another app is active, and frame rate
and CPU then are not a player's. There is no virtual audio cable in this repository, so
unattended runs have no guitar input.

Checking the song list (Learn a Song): Shift opens the search, letters filter by title and artist. The
script drops a letter now and then, so space the letters at least 1-2 s apart (`48:02,50:00,52:2d,54:08`
types `danc`), read the search text in a snapshot before trusting the result, and prefer a short distinctive
string; Esc clears the search, and its empty-search count (`197 Matches` with the 150-archive `dlc/`) is
another check. A song with two equal letters in a row needs the second one a second later, or it is lost.
`RS_MODS_LOG=1` logs `mods: DLC: N archives mounted in F frames, T s` when the DLC mounting ends, which is
the enumeration's end. The song list's default sort is DATE ADDED, so search rather than count rows.

Enumerate (a song added during a run, as on the [Mac](../mac/TESTING.md#game-runs)): take a custom song out of the
test copy's `dlc/`, point `dlc_from` in its `.rearmed/source.json` at a folder of your own, and copy the song
there once `mods: DLC:` is logged. From the main menu, the script below goes Down to ENUMERATE, Return, Return
on the notice's Accept, Up to LEARN A SONG, Return, Shift, `amor`, Return on the match, Play Song, Skip Tuner, "Yes,
continue without tuning":

```
RS_MODS_LOG=1 RS_STEAM_LOG=1 LP_INPUT_SCRIPT='@callback 1101,40:24,46:24,@DLC:,10:7d,14:24,22:24,27:7e,31:24,41:38,45:00,47:2e,49:1f,51:0f,55:24,67:24,85:75,91:24'
```

The log has `enumerate: pressed` once, `1 new archive(s) in the copy`, `dispatch callback 1005 to 1 listener(s)` and
a second `mods: DLC:` line with one archive more. `song folder not there` for the depot's own `dlc` is normal
here: a depot has none. Checked on 2026-11-04 with The Bee: 147 archives, then 148 in 4 frames, 0.23 s; the song
was found and played to the note highway, with no restart, and the same with The Bee re-packed as 999999 (see
below), which a build without the ownership override does not list ("amor": 0 Matches). `RS_MODS_BYPASS=0` is no
check of ownership here: it brings back the song-key table too, which hides every custom song. With
`RS_MODS_OVERLAY=0` the seventh Down reaches SHOP, Return shows "You must enable Steam Community In-Game to access
the Shop.", and nothing is scanned: the game as it was.

Song ownership (whatever AppID an archive carries): re-pack an official archive with another
`appid.appid` (read every entry with `tools/play-native/cache_overlay.py`'s `Psarc`, write them back with
`write_psarc`, the AppID changed; the name hashes stay the same), put it in the `dlc/` of a game folder of
its own and make the copy with `gamefiles.Copier` (`steam.env` then lists the new AppID). Searching for the
song finds it; with `RS_STEAM_DLC` set to the list without that AppID (as before the change) it finds
nothing ("0 Matches"). Checked on 2026-11-03 with Dancing Queen as 999999. To test a changed archive, run with a game folder of its own (`cp -Rc` of the game
folder clones it; set `LP_GAME_DIR`) rather than changing the installed copy.

Rocksmith 1 songs (`RS_STEAM_OWNED`): not run on Learn & Play yet, the test account owns none of them. The route is
the Mac one ([TESTING.md](../mac/TESTING.md#game-runs)): set `RS_STEAM_OWNED` to the AppIDs of
`gamefiles.rs1_dlc_apps`, and a search for "countd" finds The Final Countdown; unset, it does not. The face's answer
is covered by `test_steam32`.

Custom songs (the song-key table skip): with CDLC in the copy's `dlc/` (here Amorphis' The Bee), search
for the artist and play it, from the title to the note highway:

```
RS_MODS_LOG=1 RS_STEAM_LOG=1 LP_INPUT_SCRIPT='@callback 1101,40:24,46:24,@DLC:,10:24,20:38,24:00,26:2e,28:1f,30:0f,34:24,46:24,64:75,70:24'
```

types `amor` (1 Match, The Bee), selects it and skips the tuner; `mods: CDLC: song file signature check
skipped` is logged as the song loads. With `RS_MODS_BYPASS=0` the same search finds nothing. Checked on
2026-11-03; the empty search then counts 201 songs with the 150-archive `dlc/` (three of them CDLC).

All the song overrides in one build (2026-11-03): make the test copy with
the install code, `gamefiles.Copier("learn-play", SRC, DEST, dlc_from=[]).run()`, from a clone of the game
folder without its `.rearmed/` (the depot's original `cache.psarc`, the `dlc/` with the CDLC), so
`steam.env` and `Update/patcharchives.psarc` come from the real code path. Then one run per check, each
from the title, with `RS_GL_SNAPSHOT_SECS=2`:

- each song: the play script above with the letters of a short distinctive part of the title or artist
  (`uend` is `20,0e,2d,02`, Innuendo; `amor` is `00,2e,1f,0f`, The Bee; `alic` is `00,25,22,08`, four
  Alice Cooper songs). A match further down the list takes Downs after the last letter:
  `...,30:08,33:7d,35:7d,37:7d,40:24,52:24,70:75,76:24` plays the fourth row. The search box and its
  match count are in the snapshot about 42 (84 s);
- Path/Input: `@callback 1101,40:24,46:24,@DLC:,10:3b,15:7d,19:7c,23:7c,27:7c`; DIRECT CONNECT in the
  snapshot about 44;
- `RS_MODS_BYPASS=0` with the `amor` search: "No Matching Songs". Esc there closes the search rather than
  clearing it, so it shows no count.

In the snapshots the title (the Rocksmith logo, no Ubisoft or legal screens) is up at about 9 s and
"ENTER Begin" at about 15 s. A song with only an Alternate Lead shows an X instead of a percentage on the
Lead path; it still plays.

Leaving the stage over and over, the check for a hang on the stage or between songs: Esc on the highway opens
the pause menu (RESUME, RESTART, TUNER, MIXER, EXIT); four Downs and Return exit to the song's page in about 8 s,
with no dialog; there Return is Play Song, the tuner comes up in about 15 s, forward delete skips it, Return
answers "Yes, continue without tuning", and the highway is back about 15 s later. One round is 74 s, so ten
rounds after the first song are this, each time counted from the wait as the script's times are (the first round
is `58 + 28` = 86 for the Esc, and so on, 74 s more for each):

```
...,@DLC:,10:24,22:24,34:24,52:75,58:24,86:35,89:7d,90:7d,91:7d,92:7d,94:24,108:24,126:75,132:24,160:35,...
```

Checked on 2026-11-04, ten rounds of Cherub Rock in one run with snapshots every 2 s: the stage and the highway
came back every time, and the same on the same code without memory fences, so this script has not reproduced a
hang on the stage on either. In 5.5 minutes of the same run with `RC32_LOG_RAISE` high, no `longjmp` or C++ exception
was logged, only the anti-debugging single steps (about 60 a second) and thread names. A profile is needed for it: a binary run by itself with an empty test folder stops at
"Enter new profile name". Copy a test folder's `<account>/221680` into `RS_STEAM_DATA_DIR` and set `RS_STEAM_ID`
to that account's ID, or the shim's default ID looks in another folder. A binary outside the app reads no
`LP_HOME`: without `LP_GAME_DIR` it looks for `out/game`, and shows a grey screen.

Finding which check drops a song: a `wrap` line in `skip.txt` and an `F_<addr>` in `win32_overrides.c` that
calls `W_<addr>` and logs its arguments or result (filtered by the return address, `[esp]` on entry) show
what the game does for one song without changing it; one re-translation per set of probes. Ghidra's
headless analyzer on a copy of the image without the packer's sections (`.UBX*`, `.lpreloc`) finishes in
about 30 minutes and decompiles single functions after that.

**RSMods' library** (`rearmed-mods.dylib` in `<LP_HOME>/mods/`, `RSMods.ini` in the game folder, `RS_MODS_LOG=1`): the same route as
the [Mac's](../mac/TESTING.md#game-runs), by keyfifo, the next key waiting for the mods' own log. Its `menu:` lines say which screen it
is, so a key that only makes sense on one screen is sent only there. The screens in this edition: with a profile, `ForceProfileLoad`
reaches MainMenu by itself; Return is LearnASong_Songs; `lpshift` opens the search; Esc closes it, and Esc with it closed leaves the
list; Return on a row is LearnASong_SongOptions; Return there, Return on TUNE UP! (SelectionListDialog, in a profile with no input),
and LearnASong_Game. In the song: Esc, Space is LearnASong_RiffRepeater, with a notice the first time; Down twice, Left a few times moves
SPEED; `key fwddelete` resumes. A Disconnected song ends on LearnASong_SongOptions: no score screen without an input.

Hotkeys in the search, all quiet, then firing again after Esc: `lpshift`, then `i`, `l`, `5`, `f`; `lpshift`, `qq`, Return (no match: the box
stays open), `i`; a click, `i`. In a text entry dialog (a new profile's name) too. `song timer`, `volume:`, `lyrics` and `enumeration:` lines are what firing looks like.

**Keep a test profile**: without one, Return on ProfileSelect starts a new profile and every key after it goes through the first-time
screens; and `fwddelete` on ProfileSelect deletes the profile. Copy `steam/` before a run, restore it after one that went astray, and let
a route stop when the screen it expects doesn't come.

To find a route rather than repeat one, `tools/ws8/keyfifo` takes keys one at a time while the game runs
([Mac testing](../mac/TESTING.md#game-runs)): start `Contents/MacOS/rs2014_lp` with
`RS_KEY_FIFO=FILE DYLD_INSERT_LIBRARIES=<dir>/libkeyfifo.dylib`, then `echo "key return" > FILE`.

The screens by key (`tools/ws8/keyfifo`, the installed app, 2026-11-04): send `front` before keys, or the game takes
none. With `LP_HOME` and a `DYLD_INSERT_LIBRARIES` of keyfifo through `open -n --env`, the title takes Return about 35 s
after the start and the menu is up 20 s after the profile. In this edition Shift and Ctrl are `lpshift` and `lpctrl`;
`key fwddelete` is Skip Tuner. Ctrl: Path/Input; Down to INPUT, Right steps REAL TONE CABLE, MICROPHONE, DISCONNECTED,
DIRECT CONNECT, Down and Return lists the inputs, Return takes one. Main menu, from LEARN A SONG down: SESSION MODE (a
first visit shows two notices; Space starts it, then the tuner; Esc, Down, Return ends it), NONSTOP PLAY (Return on START,
the tuner, the song; Esc pauses with SKIP on top; Return on it shows the playlist), LESSONS (Down to a lesson, Return
twice; Esc, Down, Return leaves), GUITARCADE, MULTIPLAYER (the second profile, CONTINUE, then the modes; a song stops at
"It sounds like Player 2's cable is unplugged"), TONE DESIGNER (Return on EDIT TONE), SHOP. A finished song shows
notices that each take Return, and a Return too many starts the next song. Esc from the main menu, Return on "Yes, quit",
Esc on the title, Return on EXIT GAME: the game's own Exit.

### Shaders of a run

`LP_SHADER_LOG=DIR` tells what a run made. `python3 win-x86/shaders/record_programs.py DIR/log.tsv` adds its programs to
`win-x86/shaders/programs.tsv`; a line in the list holds hashes and state keys only, but the rest of `DIR` is the game's, so it stays
on this Mac. To see what the corpus missed, `RS_MSL_LOG=1` logs each program translated at run time, and with
`RS_METAL_CACHE_STATS=1` the exit line's `lib_ms` is the time spent compiling: 7.8 ms for a first play after the warm-up.

A first play with no shader cache, without touching the user's: give the test app a bundle identifier of its own (`CFBundleIdentifier`
in `Info.plist`, then `codesign --force --sign -`), and before each run delete `$(getconf DARWIN_USER_CACHE_DIR)<that identifier>` and
the test home's `Library/Caches/rs-rearmed`. Start the executable directly, with `HOME` and `CFFIXED_USER_HOME` at the test home: with
`open`, a test bundle with its files on another drive waits for macOS's question about the volume, with no window to show for it (a
process stuck in `dyld4::open`, no log), and nobody may answer it in an unattended run. Metal's cache is the same either way
(`lib_ms` 7.7 ms by `open` from the internal disk, 7.8 ms started directly; [METAL.md](../mac/METAL.md#safe-load)). For a run by
`open`, keep the app, its home and the game copy on the internal disk (`RS_GAME_COPY_DIR=home`).

Screens not in the other routes, by key from the main menu (2026-11-05, keyfifo, a test profile, no guitar; `key` is left out):

- Space: Tools. OPTIONS (down 3), VISUAL SETTINGS (down 2), VENUE MODE (down 7), Return, Return on On: from then on a song opens
  a portal to a venue about 30 s in, with or without a guitar. Esc four times to the menu. With Venue Mode Off (the test profile's)
  a song stays in the rehearsal room. The rooms of Session Mode are reverbs, not venues.
- GUITARCADE: TECHNIQUE GAMES, Return on a game, then Calibration "Make some noise!" waits for a guitar; Esc goes back. So the games
  need a guitar. SCORE ATTACK works without: Return on the song, Return on EASY, Skip Tuner, Return; three strikes end it ("Song
  failed!") about 30 s in, Return goes back to the list.
- MULTIPLAYER: Return on the second profile, Return on CONTINUE, Return on the mode and the first song, Return on Play Song, Skip
  Tuner, Return: a split-screen song.
- A song's Esc: RESUME, RESTART, TUNER, MIXER, EXIT, Space Riff Repeater. Esc, Down four times, Return leaves it for the song's page; Esc is
  the list, Esc again clears its search. Down moves a row, Return on a row opens the song's page, Return there is Play Song.
- Riff Repeater's speed (2026-11-05, `rr_speed_ratio` with a mod): in a song Esc, Space, Down twice to SPEED, Left once a second, each 1 off the
  100; Delete (`key fwddelete`) resumes. The slider calls the point on each step, ACCELERATE on goes on calling it in the song.
- Focus loss (`ui_event`): another app in front during a song (`open -a Finder`, as a player would); keyfifo's `front` comes back.
- LESSONS, a lesson with practice: Bass 101 is the third Down from the first video one; Return twice, Skip Tuner, Return. Esc there is
  a menu with RESUME LESSON, EXIT LESSON, RESTART LESSON, TUNER, TECHNIQUE GUIDE, MIXER.

### By hand and with computer use

The keys and the way from the title to a song are in [INSTALL.md](INSTALL.md#playing). Keys reach the game
only while its window is in front, so screen-control tools need full-screen control (keyfifo's `front` brings it
there). They find
applications by bundle: the installed app is one; launch it with `open` and grant it. For a build that is
not installed, wrap the binary in a small `.app` (an `Info.plist` with a bundle identifier,
`CFBundleExecutable` the binary, `LSEnvironment` for `LP_GAME_DIR`, `LP_USER_DIR` and other variables, the
runtime library next to it).

A build without the online layer (or a log without `lp: online: offline RSConnect layer +extension
+SkipLogin +sign-in`) shows Ubisoft's screens: Esc on the Uplay login and on "Sign in to access online
features", Return on "The Ubisoft servers are not available". Never sign in.

### The capture VM

The seed capture ([win-x86/unpack/README.md](../../win-x86/unpack/README.md)) drives a Windows guest in QEMU,
over VNC or from the monitor socket (`sendkey`, `screendump`). What it took to get through it:

- The guest's keyboard follows the layout chosen at install, and clipboard paste does not reach it. Look at the
  screen before pressing Enter: a wrong layout turns `\` and `"` into other keys, and keys sent faster than
  about three a second get dropped.
- Keys sent right after logging in are lost: wait for the tray icons. The Run box keeps its last command,
  selected, so Enter on an unchanged box runs it again.
- `Rocksmith2014.exe` needs its DLLs beside it. A disc image built with `hdiutil makehybrid` and put in with
  the monitor's `change ide1-cd0` brings files in without a reboot; the shared folder is read only at boot.
- The game's window covers the desktop while its Steam error is open. `powershell -noexit` from the Run box
  opens on top of it. PowerShell takes about two minutes to start in the guest.
- A game folder with no `dlc` songs has no use for the search in the script above, and `@DLC:` can pass before
  the wait starts. On a clean depot copy this went from the title to the highway:
  `@callback 1101,40:24,46:24,75:24,90:24,102:24,122:75,128:24` (Learn a Song, the first song, Play Song, Skip
  Tuner, "Yes, continue without tuning").
- The first boot after Windows' Shut down powered off by itself after about 15 seconds, once; the next one
  was normal. The cause was not found. The evaluation image also says its license is expired; a capture
  takes minutes, and setting the guest's clock back did not change it.

## Diagnostics

All off unless set (from `getenv` in `win-x86/`, the runtime and the shared shims):

| Switch | What |
|---|---|
| `LP_LOG=1\|2\|3` | imports: the first call of each / every call / and every call into the seam's COM objects (D3D9, audio, Steam) with its result; also every `LoadLibrary` and `GetProcAddress` |
| `LP_LOG_FILES=1\|2` | files the game opens, including the missing ones it probes for |
| `RC_HOOK_TRACE=NAME,NAME` | the raw calls of those hook points, the first 8 of each, refused ones too: return address, `ecx edx esi edi`, four stack words, then `eax` and `st0`. The way to check a table's address and prototype in a run |
| `LP_LOG_TO=FILE` | append the log there (the app logs to `logs/last-run.log` by default) |
| `LP_FRAME_LOG=MS` | every frame longer than MS ms, with the game thread's CPU, context switches and what ran in it (links, file reads, first import calls, exceptions, the slowest COM methods); the frame rate every 10 s |
| `LP_THREAD_CPU=S` | each thread's CPU over the last S seconds, by name (`w32 game`, `w32 thread <start address>`), and a line of what has gone of the guest's memory: the arena, `VirtualAlloc` and `VirtualFree`, threads made, handles open, GL programs |
| `LP_PROF_IMPORTS=S` | import calls and indirect-call targets per interval |
| `LP_PROF_NEW=N` | indirect-call targets seen for the first time from profile round N on |
| `LP_DUMP_WAITS=S` | each guest thread's pending wait, its handles and their state, every S seconds |
| `RC32_LOG_RAISE=N` | the first N guest exceptions: code, C++ type, guest and host stack |
| `LP_SHADER_LOG=DIR` | a line in `DIR/log.tsv` per shader created and program linked, the bytecode in `DIR/sm`, the GLSL in `DIR/glsl` ([Shaders of a run](#shaders-of-a-run)) |
| `RS_GL_FRAMETIMES=FILE` | a line per Present |
| `RS_GL_SNAPSHOT_DIR=DIR`, `RS_GL_SNAPSHOT_SECS=N` | a PNG of the frame every N seconds |
| `RS_AUDIO_LOG=1` | device buffers, stream setups, real-time threads, dropped input, each input's peak and clips every few seconds |
| `RS_STEAM_LOG=1` | the Steam face's calls (`[steam-shim]`) |
| `RS_MODS_LOG=1` | the built-in fixes: the open-file limit, the DLC mounting's end (archives, frames, time, and the path cache's and number hash's counts), each song-file signature skip, each DLC preview job that fails offline |
| `RS_MODS_FIXES=0`, `RS_MODS_BUDGET_MS=N`, `RS_MODS_BYPASS=0` | the built-in fixes off, the DLC mounting's time per frame (0: one archive a frame), the song-file signature check on |
| `RS_D3D9_DISPLAY=WxH` | the desktop size the game sees |
| `RS_FULLSCREEN=1\|0` | full screen or a window, whatever the game's setting |
| `RS_METAL_RETINA=1` | render in backing pixels |
| `RS_AUDIO_RS_NAMES=0` | keep CoreAudio's device names |
| `RS_AUDIO_FRAMES`, `RS_AUDIO_RT`, `RS_AUDIO_INPUT_TRIM`, `RS_AUDIO_SET_RATE` | the audio policy ([INSTALL.md](INSTALL.md#settings-and-switches)) |
| `RS_REARMED=0` | the game's own title logo ([INSTALL.md](INSTALL.md#the-title-logo)) |
| `LP_NATIVE_MATH=0` | the CRT's math translated instead of libm |
| `LP_QOS=0` | the default QoS for the game's threads |
| `LP_WAKE_ALL=1` | every wait woken on every change (the old way) |
| `LP_D3D9_EAGER=1` | dynamic buffers uploaded at `Unlock` instead of at the draw |
| `LP_INPUT_SCRIPT`, `LP_INPUT_WAIT`, `LP_WINDOW_SCRIPT`, `LP_OFFSCREEN=1` | unattended runs (above) |
| `LP_GAME_DIR`, `LP_USER_DIR`, `LP_HOME`, `LP_IMAGE`, `LP_DLL_DIR`, `LP_ARGS`, `RS_STEAM_DATA_DIR` | folders, images and the command line (`win-x86/app/lp_main.c`) |

The Steam face also reads the Mac layer's `RS_STEAM_DLC`, `RS_STEAM_ID`, `RS_STEAM_PERSONA`,
`RS_STEAM_LANGUAGE` and `RS_STEAM_KEEP_CREDENTIALS` ([shims/steam/README.md](../../shims/steam/README.md));
libRSOpenGL's `RS_GL_*`, `RS_MTL_*` and `RS_METAL_*` switches apply as on the Mac
([Mac testing](../mac/TESTING.md#diagnostics)). The game's own audio log: `[Audio] DumpAudioLog=1` in
`Rocksmith.ini` writes `audiodump.txt` beside it, listing each input with its USB IDs.

## Offline checks

- The Win32 seam's ws2_32, iphlpapi and wininet refuse every connection and `ShellExecuteA` opens nothing
  (`win-x86/shims/win32_net.c`, `win32_misc.c`); with `LP_LOG=1` the log shows each attempt.
- The Steam face never loads Valve's library and refuses the Ubisoft and Valve DLLs by name.
- The online layer never reads `crd`; with `LP_LOG_FILES=1`, no `open "crd"` should appear in a run.
- Without mods the Mac port's network guard is not loaded; these checks are the seam's own.
- With a mod in `mods/` the app restarts with `libnetguard.dylib` inserted, and no mod runs without it
  ([Mods](#unattended-runs)); `logs/netguard.log` then lists every refused attempt.
  `tools/ws8/netguard/build.sh --test` tests the guard, as on the Mac.
