# Installing the Mac port

You build RS Re-ARMed yourself, on your Mac, from your own Steam copy of Rocksmith 2014 for macOS. The build
reads the game from your install, recompiles it to C and compiles that into an arm64 app. Nothing it
produces may be shared.

## Requirements

- A Mac with Apple Silicon (M1 or later), macOS 12 Monterey or later.
- The Steam macOS version of **Rocksmith 2014 (Remastered)**, installed through Steam. Its default
  folder is `~/Library/Application Support/Steam/steamapps/common/Rocksmith2014`; for another one, choose
  it in the installer or set `RS_GAME_DIR`.
- A Real Tone cable, or another audio input the game accepts. macOS asks for microphone access the first
  time the game listens for your guitar ([What macOS will ask](#what-macos-will-ask)).
- To build: Apple's command line tools with their `python3` (the installer starts their installation when
  they are missing, and you click Install in Apple's window), and an internet connection while building (the Bink shim downloads the FFmpeg 7.1.2 source from ffmpeg.org, checksum
  verified; pip downloads the Python packages, CMake and Ninja among them, into the build's own Python
  environment). Nothing from Homebrew, no Rosetta. The game is never started: the build reads it from disk.
- About 15 minutes for a first build and install on an M2, plus a few minutes for "Preparing shaders".
- The installer keeps 2 GB of the startup disk free for macOS, and counts that in before it starts.
- Room for RS Re-ARMed's own copy of the game files ([Game files](#game-files)): none extra when the game
  is on an APFS volume, the startup disk's (Steam's default library) or an external drive's (the copy then
  goes onto that drive); else (an exFAT or HFS+ drive) as much as the game and its DLC take on the startup
  disk (about 6.5 GB for the game alone).

### What macOS will ask

These windows come from macOS, not from the installer, and can be hidden behind other windows. If a step stands
still, look for one; after two minutes without news the installer says so too.

| When | The window | Answer | If you say no |
|---|---|---|---|
| The first step, without Apple's command line tools | Install the command line developer tools? | Install, then Agree. No password. | The installer waits; Open Apple's Installer shows it again. |
| The first step, with Xcode installed but new or just updated | Xcode's license, then "install additional required components" | Agree; Install, and your Mac's password (it goes to macOS, not to the installer) | The build can't run. The installer waits: Open Xcode shows it again. |
| A build folder or Steam download on another disk | RS Re-ARMed Installer, or Steam, would like to access files on a removable volume | Allow | That step fails. Turn it on in System Settings, Privacy & Security, Files & Folders, under the app's name, then Try Again. |
| The game's first start, when its files are on another disk | RS Re-ARMed would like to access files on a removable volume: once for the launcher, once for the game | Allow, both times | The game says it can't read its files, and stops. Same place in System Settings, then start it again. |
| The game first listens for your guitar | The game would like to access the microphone | Allow | No guitar sound. Turn it on in System Settings, Privacy & Security, Microphone, then restart the game. |

The game does not use your Documents, Desktop or Downloads folders, so it never asks for them. If you put the build
folder in one of those, macOS asks the installer once.

## Install with the installer

Download `RS.Re-ARMed.Installer.app.zip` from the [Releases](https://github.com/bbelgg/rs-rearmed/releases) page and
open it, which unzips it. Then open **RS Re-ARMed Installer**.

The installer is not notarized, so the first time macOS refuses to open it. Double-click it once and close the
message; then open **System Settings**, **Privacy & Security**, scroll down to the line about RS Re-ARMed
Installer, click **Open Anyway** and confirm. After that it opens normally. If it is in Downloads, that is fine.

Leave **Game** on Rocksmith 2014 for Mac and click **Build & Install**. It downloads the source of its own
release, checks the prerequisites, builds everything and installs it the same way as
`tools/play-native/install.sh`. Whenever the installer needs you, a box above the log says exactly what to do, with
a button for it, and it goes on by itself when that is done. **Stop** in the box ends the run; what is done is
kept, and Build & Install goes on from there.

### The steps

1. **Checking Apple's tools.** Before anything else. If Apple's command line tools are missing, macOS's own window
   for them opens (it may be behind the installer): click Install, then Agree. It takes a few minutes. If you have
   Xcode and it isn't set up, it opens: click Agree and Install, with your Mac's password.
2. **The game.** Your game folder is found in any Steam library. If it isn't installed, the installer leads you
   there, with the same messages as for Learn & Play:

   | The box says | What to do |
   |---|---|
   | **Install Steam** | Click Get Steam, install it and open it. |
   | **Steam is starting** | Wait. Steam may update itself first. |
   | **Sign in to Steam** | Sign in, in Steam's window, with the account that owns the game. |
   | **Install the game in Steam** | Steam's install window is open: click Install (or Next). If Steam says the game isn't available for Mac, your account has Learn & Play: click Stop and choose it under Game. |
   | **Steam is installing the game** | Wait, with Steam open. |

3. **Decrypting the game.** A few seconds, by itself: the game's code is read from the Steam copy on disk, which
   doesn't change. The game isn't started, and it doesn't matter which account Steam is signed in with, once the
   game is installed. If it isn't the build RS Re-ARMed is made for, it says so; Verify integrity in Steam usually
   mends that.
4. **Building**, about 15 minutes, and **Preparing shaders**.

Your DLC needs no step of its own: Steam installs it with the game, and the copy takes it from the game's `dlc`
folder. (Learn & Play has a step for it, as its download comes without Steam's install:
[Windows INSTALL](../windows/INSTALL.md#the-steps).)

- **Renderer** chooses how the game draws: **Metal (recommended)** or **OpenGL** (see
  [Renderer](#renderer)). It starts on the renderer your install uses (OpenGL after a fallback), else on
  the one chosen last time.
- With Metal the last step, **Preparing shaders**, compiles the game's Metal shaders once: a few minutes on
  a first install, seconds after an update. **Skip** ends it early; the game then prepares the rest on its
  first start.
- **Build folder**: `~/Library/Caches/rs-rearmed-build` unless you choose another (the installer makes a
  `rs-rearmed-build` folder inside the one you pick and remembers it). Its path must not contain spaces.
  **Delete build files after install** removes them when it is done. For a folder on an external drive,
  macOS asks once whether the installer may use it.
- **Rocksmith 2014 game folder** starts on the folder your install's game files come from, else Steam's
  default library.
- **Refresh Game Files** (shown once the game is installed) syncs RS Re-ARMed's copy of the game files
  with that folder: after new DLC or custom songs, or a Steam update ([Game files](#game-files)).
- Quitting the installer while it works stops the build (it asks first); Build & Install starts it again.
  The log of every run is kept in `~/Library/Logs/RS Re-ARMed Installer/`.

When it is done it offers to start the game. Later, start **RS Re-ARMed** from `~/Applications`
(double-click, Launchpad or Spotlight).

## Build by hand

All commands run from the repository root.

### 1. Clone

```bash
git clone https://github.com/bbelgg/rs-rearmed.git
cd rs-rearmed
```

Clone into a folder whose path has no spaces: the build scripts can't handle them.

### 2. Python environment

The recompiler needs Python with [Capstone](https://www.capstone-engine.org/), and the build uses the CMake
and Ninja that pip installs next to it (`requirements.txt`):

```bash
python3 -m venv build/venv
build/venv/bin/pip install -r requirements.txt
export RC_PY="$PWD/build/venv/bin/python"
export PATH="$PWD/build/venv/bin:$PATH"     # the venv's cmake and ninja for step 4
```

The app build (step 5) runs the `ninja` next to `RC_PY` (`RS_NINJA` names another one).

The title logo (`tools/loadscreen`) reads `RS_PY` instead; set it to the same Python, or leave it unset and
the logo script makes its own environment.

### 3. The game image

The recompiler reads a DRM-free x86-64 binary. The Steam copy on disk is encrypted, and `tools/steamless` makes
the decrypted image from it, in a few seconds, without running anything and without changing your Steam copy:

```bash
python3 tools/steamless/decrypt.py        # RS_GAME_DIR or --game: another game folder
export RC_IMAGE="$PWD/build/steamless/Rocksmith2014_x64_decrypted"
```

It checks the result against the build RS Re-ARMed is made for and refuses anything else.

### 4. The shims

```bash
cmake -S . -B build -G Ninja
cmake --build build                # the Bink (with FFmpeg), Cg and OpenGL shims, the game libraries
make -C shims/steam                # the offline Steam layer: shims/steam/build/libsteam_api.dylib
tools/ws8/netguard/build.sh        # the network guard: build/ws8/libnetguard.dylib
```

Use `build` in the repository root as the CMake build directory: the later steps look for the shims in
`build/shims/` and for the network guard in `build/ws8/`.

### 5. The app

```bash
tools/ws7/recomp/app/build-m4.sh build/m4
```

This builds the native override libraries, recompiles the game image to C, compiles it (the slowest part),
makes the Metal renderer's shader corpus and links `build/m4/app/Rocksmith2014.app`. At the end it starts
the new app once with `--init-only` as a check and prints `app: <path>`. Without an argument the output
goes to `$RS_BUILD_DIR/m4`.

The shader corpus (`tools/metal/make-corpus.sh`, under a minute) is the Metal translation of every shader
the game can compile, made from the game files alone ([METAL.md](METAL.md#shader-translation)). If it
can't be made, the build goes on without it.

The generated C goes to `<output>/gen`, the objects to `<output>/obj`, the corpus to `<output>/shaders`.
All three are derived from the game: keep them on your Mac.

Variables the build reads:

| Variable | Default | Meaning |
|---|---|---|
| `RS_BUILD_DIR` | `build` in the repository | where the app build goes when no output directory is given |
| `RC_IMAGE` | `analysis/cg/Rocksmith2014_x64_plain` | the game image (step 3) |
| `RC_TARGET` | `mac-x64` | the per-game tables in `tools/targets/<target>` |
| `RC_PY` | `analysis/venv/bin/python` | Python with Capstone (step 2) |
| `RS_GAME_DIR` | the default Steam library | the game folder; the app's icon is copied from it |
| `SHIM_STEAM`, `SHIM_BINK`, `SHIM_CG` | the builds from step 4 | other arm64 builds of those shims |
| `SHIMS_BUILD` | `build/shims/game` | where the game libraries from step 4 are |
| `RC_OPT` | `-O2` | optimisation level of the generated C |
| `RC_O1` | none | file of function addresses compiled alone at `-O1` |
| `RC_LTO` | none | `thin`: ThinLTO across the generated C, the runtime and the overrides |
| `RC_LTO_JOBS` | `6` | ThinLTO threads while linking |
| `RC_PGO` | none | `gen`: an instrumented build; `use:FILE.profdata`: optimise with a profile |
| `RC_PGO_DIR` | `<output>/pgo` | where an instrumented game writes its profiles |
| `RC_JOBS` | all cores | parallel compile jobs |
| `RS_SHADER_CORPUS` | on | `0`: no shader corpus |
| `RS_MSL_CORPUS_FILE` | none | bundle this `rsmsl-corpus.bin` instead of making one |
| `RS_MSL_VERIFY` | off | `1`: also check every corpus program against OpenGL (a few minutes) |
| `RC_RELAXED` | on | `0`: fence the audio kernels too (for a suspected memory-ordering problem) |

The optimisation switches (`RC_LTO`, `RC_PGO`, `RC_OPT`) are optional; what they measured and how to make a
profile-guided build is in [PERFORMANCE.md](PERFORMANCE.md#build-options).

### 6. Install

```bash
tools/play-native/install.sh
```

It copies the game files from your game folder (`RS_GAME_DIR`, default Steam's library) into
`~/Library/Application Support/RS Re-ARMed/game/` ([Game files](#game-files)), the app (by default
`$RS_BUILD_DIR/m4/app/Rocksmith2014.app`; pass another path as the first argument) next to them and the
network guard into `~/Library/Application Support/RS Re-ARMed/`, and creates the launcher
`~/Applications/RS Re-ARMed.app`. Quit the game first. Start the game through the launcher, which prepares
the game folder (the offline `Rocksmith.ini`, the title logo) and starts it offline; opening
`Rocksmith2014.app` directly doesn't work.

Last, `install.sh` prepares the Metal shaders (`tools/play-native/warm-shaders.sh`): it runs the installed
game once with `--warm-shaders`, which opens no window, and compiles the shader corpus into the caches the
game reads, showing `Preparing shaders… N of M` (about 4.5 minutes for 3880 shaders on an M2 the first
time, about a second on later installs). Metal keeps compiled shaders per app and per install location, so
only the installed game can do this, and moving the install means preparing again. `RS_WARM_SHADERS=0`
skips the step; run `tools/play-native/warm-shaders.sh` later to do it then.

`RS_RENDERER=metal` or `RS_RENDERER=opengl` chooses the renderer; without it `install.sh` keeps the one the
install has, else Metal. `RS_NATIVE_HOME` and `RS_NATIVE_LAUNCHER` change the two install locations.

### Game files

The game reads only RS Re-ARMed's own copy of the game files, `~/Library/Application Support/RS Re-ARMed/game/`,
never Steam's folder, so a Steam update, uninstalling the game in Steam or moving your Steam library does not
change or break it. `install.sh` makes the copy (`tools/play-native/gamefiles.py`) with what the game reads:
the `.psarc` archives and folders of the game, `metadata`, `Rocksmith.ini` (the launcher makes the offline
one from it), the title logo when you built it, and from `dlc/` (official DLC and custom songs, in their
subfolders) the Mac archives, `*_m.psarc`. Not copied: Steam's own `Rocksmith2014.app` (the install has its
build), the PC archives (`*_p.psarc`) and other files. It also records the DLC your account has installed
and the game's language from Steam's `appmanifest_221680.acf` (`game/.rearmed/steam.env`), so the offline
Steam layer does not need Steam's folder for them, and the Steam account the game plays as (`RS_STEAM_ID`):
the account whose Steam license has the Mac game. Your saves and profiles belong to that account, so logging
in to Steam with another account later (for example one that owns Learn & Play) does not change them. To
choose the account yourself, set `RS_STEAM_ID` (a SteamID64, or the account number of
`Steam/userdata/<number>`) when you refresh, e.g. `RS_STEAM_ID=12345678 tools/play-native/install.sh
--refresh-game`, or for the launcher with `launchctl setenv RS_STEAM_ID …`.

After each copy or refresh it also makes the **Direct Connect overlay**, `game/Update/patcharchives.psarc`
(`tools/play-native/cache_overlay.py`): from your own `cache.psarc` it takes the two menu files that leave
out the "DIRECT CONNECT" choice (on the cable screen and in the INPUT setting), adds it, and stores the
archive that holds them, rewritten, where the game reads it instead of the one in `cache.psarc`. The same
overlay turns the main menu's SHOP into ENUMERATE.
`cache.psarc` itself is not changed, so a modded cache works as before; a cache that has both entries already
gets no overlay. If your cache cannot be read, the copy goes on without the overlay and says so.

- **Disk use.** When the game folder is on the same APFS volume as your home folder (Steam's default
  library is), every file is an APFS clone: the copy takes seconds and no extra space; macOS shares the
  data until one side changes (a Steam update then stores only what changed). `du` and the Finder count
  the copy at its full size anyway; the free space on the disk shows what it really takes.
- **A Steam library on another drive** (an external disk): when that drive is APFS, the copy goes onto it,
  as clones again (no extra space): `RS Re-ARMed/game` at the top of the drive (when the top is not
  writable, in the first writable folder on the way to the game, e.g. `SteamLibrary/RS Re-ARMed/game`), and
  `~/Library/Application Support/RS Re-ARMed/game` is a link to it. The game then needs that drive
  connected; without it the launcher says "The drive with the game files (*name*) isn't connected". On a
  drive that cannot clone (exFAT, HFS+, a network volume) the copy is a real one in your home folder, as
  large as the game and its DLC (about 6.5 GB without DLC); `install.sh` checks there is room first, says
  how much is needed, and shows its progress. `RS_GAME_COPY_DIR=DIR` puts a new copy in `DIR/game` instead
  (any drive; `RS_GAME_COPY_DIR=home`: in your home folder whatever the drive; for the installer app, set it
  with `launchctl setenv RS_GAME_COPY_DIR …` before opening it). This decides where a new
  copy goes: a copy made by an earlier version stays where it is (in your home folder; delete
  `~/Library/Application Support/RS Re-ARMed/game` and install again to have it moved), and a refresh
  keeps the copy where it is.
- **Refresh** after new DLC or custom songs, or a Steam update: the installer's **Refresh Game Files**, or
  `tools/play-native/install.sh --refresh-game`. It copies new and changed files (by size and date, by
  contents when only the date differs) and removes the files of the copy that the game folder no longer
  has; it touches nothing else (your settings, saves, the app). Running `install.sh` again also refreshes.
  If the game folder is gone (Steam uninstalled the game), a refresh changes nothing and says so.
- **Stopped copies** continue where they were on the next run. The copy is complete when
  `game/.rearmed/manifest.json` is there (written last); until then the launcher asks for a refresh.
- `game-source` names the folder the copy comes from; `game/.rearmed/source.json` holds what a refresh
  uses. The installed `gamefiles.py` next to them refreshes without the repository.

### The title logo

The install makes the Re-ARMed title logo automatically, once, in the copy of the game files (Python with
Pillow, numpy and etcpak: the build's environment has them; with neither that nor `RS_PY` it makes its own
environment, which needs the network the first time). If it can't, the install goes on and the game shows its
own logo. To rebuild it by hand:

```bash
game="$HOME/Library/Application Support/RS Re-ARMed/game"
RS_GAME_DIR="$game" tools/loadscreen/build.sh "$game/static.psarc.rearmed"
```

It reads the copy's `static.psarc.original`, changes only the two title-logo textures and writes a new file
next to it. The original is not changed, and the launcher uses the new file whenever it is there
(`RS_REARMED=0`: the original). When a refresh brings a new `static.psarc`, it removes the logo made from the old
one; the next install makes it again, or the command above. Steam's folder is not touched. Learn & Play does the
same in its own copy ([Windows INSTALL](../windows/INSTALL.md#the-title-logo)).

## Where things are

| Where | What |
|---|---|
| `~/Applications/RS Re-ARMed.app` | the launcher |
| `~/Library/Application Support/RS Re-ARMed/game/` | the game folder the game reads: RS Re-ARMed's own copy of the game files ([Game files](#game-files)), the installed `Rocksmith2014.app` and an offline `Rocksmith.ini`; for a game on another APFS drive a link to `<drive>/RS Re-ARMed/game`, where all of that is |
| `…/game/.rearmed/` | the copy's manifest (written last), where it comes from (`source.json`), the Steam facts (`steam.env`: the installed DLC, the language, the account) and the install's `Rocksmith.ini` |
| `…/game-source`, `…/gamefiles.py` | the folder the copy comes from, and the tool that refreshes it |
| `…/steam/<account>/221680/remote/` | your profiles and progress for the native build |
| `…/logs/` | `last-run.log`, `previous-run.log`, `netguard.log`, `seed.log` |
| `…/mods/` | mods loaded at startup |
| `…/renderer` | `metal` or `opengl` |

On the first launch your Steam saves are copied once into `steam/`, without `crd` (stored Ubisoft
credentials). The native build never writes your Steam saves.

## Playing

Start **RS Re-ARMed** from `~/Applications`. The Steam client is not needed. Enter on the title screen goes
straight to the profile list. Custom songs (CDLC) in the game's `dlc` folder load as they are. To add a song
while you play, put it in Steam's `dlc` folder (or `game/dlc`) and press **ENUMERATE** in the main menu: the song
is in the list a moment later, with no restart. Changed or removed songs and new DLC reach the game with
**Refresh Game Files** (or `install.sh --refresh-game`).

### Keys

The game takes the Mac keyboard directly: space, Return, Esc, Tab, Backspace, the arrows, F1-F12, Home, End, Page
Up, Page Down, forward delete and punctuation. The window must be in front.

| Game prompt | Press |
|---|---|
| ENTER | Return |
| ESC | Esc |
| DELETE (for example "Skip Tuner") | **fn + Delete** |
| SPACE / TAB | the same keys |

From the title to a song: Return (title), Return (profile, or New profile), Return (LEARN A SONG), Return
(the song), Return (Play Song), then tune, or skip with fn + Delete and Return on "Yes, continue without tuning".

### Window modes

Shift-Command-F switches between full screen and a window. In a window, drag the edges to resize: the new size
becomes the game's resolution. On a 16:10 screen the 16:9 picture fills the screen, stretched, as in the original
game.

### Renderer

The game draws with **Metal** by default, through RS Re-ARMed's own GL-on-Metal layer; **OpenGL** (Apple's,
as in the original Mac game) stays available. The choice is the file `renderer` in
`~/Library/Application Support/RS Re-ARMed/`; to switch without reinstalling, quit the game and write the
other one:

```bash
echo opengl > ~/Library/Application\ Support/RS\ Re-ARMed/renderer   # or: echo metal > ...
```

For a single start, `RS_GL_BACKEND=gl` or `RS_GL_BACKEND=metal` wins over the file. `logs/last-run.log`
says which one ran and why (`[rsp] renderer: metal (default)`).
`Rocksmith2014.app/Contents/MacOS/Rocksmith2014 --renderer-check` prints what a start would use.

**Fallback.** If Metal can't start on your Mac, the game starts with OpenGL (this start only). If two
starts with Metal in a row end abnormally (a crash, or a force quit; a normal quit resets the count, and so
does a crash in the game's own shutdown), the next start uses OpenGL, writes `opengl` into `renderer`,
shows a notification and explains it in `renderer-fallback.txt`. Write `metal` into the file to try Metal
again, or choose Metal in the installer.

### Audio latency

1. In the game's Audio settings, set the audio engine setting to 1, then restart the game. If the sound
   crackles, use 2. The game's default is 4.
2. Listen through headphones or a USB audio interface, not the built-in speakers (about 20 ms of
   processing).

The setting is `LatencyBuffer` in `Rocksmith.ini`: the game's output buffer in steps of 256 frames
(5.3 ms). The launcher keeps it across launches. The output device's buffer is `RS_AUDIO_FRAMES`
(default 256; 128 at `LatencyBuffer=1` and on an interface that also has inputs).

## Settings and switches

The built-in fixes and their switches are listed in the [README](../../README.md#built-in-patches-and-mods).
The launcher starts the game through macOS, so variables set in a Terminal do not reach it. Set them for
the login session:

```bash
launchctl setenv RS_MODS_BYPASS 0     # takes effect on the next launch
launchctl unsetenv RS_MODS_BYPASS     # back to the default
```

Mods: every `.dylib` in `~/Library/Application Support/RS Re-ARMed/mods/` is loaded at startup
(`RS_LOAD_MODS=0` starts without them, `RS_MODS_PATH` loads another folder or one library). How mods hook
the game: [DESIGN.md](DESIGN.md#built-in-fixes-and-mods).

## Update and uninstall

- **Update**: with the installer, download the new release and run it. By hand, pull, repeat steps 4 and 5
  and run `install.sh` again. Your saves are kept; the game files are refreshed.
- **New DLC or custom songs, a Steam update**: **Refresh Game Files** in the installer, or
  `tools/play-native/install.sh --refresh-game`.
- **Uninstall**: delete `~/Applications/RS Re-ARMed.app` and `~/Library/Application Support/RS Re-ARMed`
  (back up `steam/` first to keep your progress; the folder also holds Learn & Play's `learn-play/`, if you
  have it). That removes the copy of the game files too, unless the game is on another drive: then
  `…/RS Re-ARMed/game` is only a link, and the copy is on that drive, in `RS Re-ARMed/game` at its top (or
  in the folder `ls -l ~/Library/Application\ Support/RS\ Re-ARMed/game` shows): delete that `RS Re-ARMed`
  folder too (it also holds Learn & Play's `learn-play/game`, if you have it there). If you built the title
  logo, also delete `static.psarc.rearmed` from your game folder, if an earlier version put it there.

## Known issues

- No online features: Ubisoft services are not available.
- Progress made in the native build is not copied back to your Steam saves.
- Only the one Steam macOS build of Rocksmith 2014 is supported.
- With the legacy fast-load mod (a modified `cache.psarc`), a start can now and then stay on the white
  loading screen, more often when the Mac is busy. Quit and start again. With Metal, RS Re-ARMed prepares
  its shaders before the game loads when it sees such a mod, which avoids the stall it would otherwise
  cause.
- With Metal, the first time a song, venue or mode is played, a few short stutters (up to about half a
  second) can show in its first half-minute while Metal compiles what it draws for the first time. Later
  plays are smooth.
- Mods that draw with OpenGL themselves show nothing with Metal; the rest of the mod works. Use a version
  that draws through the overlay API (`shims/include/rs_overlay.h`), or the OpenGL renderer.

## Troubleshooting

Logs: `~/Library/Application Support/RS Re-ARMed/logs/` (`last-run.log`, `previous-run.log`,
`netguard.log`); the installer's in `~/Library/Logs/RS Re-ARMed Installer/`. `RS_MODS_LOG=1` and
`RS_AUDIO_LOG=1` add what the built-in fixes and the audio setup do.

| Message | What to do |
|---|---|
| `missing .../build/shims/game/librs_game.a: build shims/game first` | Run step 4 with `build` as the build directory. |
| `warning: no arm64 libsteam_api.dylib` (or `libBink2Macx64.dylib`, `Cg`) | That shim was not built: run step 4. |
| `UnicodeDecodeError` from Python's `site.py` in step 2 | The repository is on a drive such as exFAT, where macOS writes `._` files. Run `find build/venv -name '._*' -delete`, step 2 again, then the `find` once more. |
| `thunk generation failed, see .../thunks.log` | Check that `RC_PY` points at the environment from step 2 and `RC_IMAGE` at the game image. |
| `warning: no RS2014.icns in ...` | The game is not in the default Steam library: set `RS_GAME_DIR`. |
| `link: undefined symbols, see .../undefined.txt` or `skip list did not converge` | The build did not complete. Open an issue with that file and the logs in the output directory. |
| FFmpeg checksum `FAILED` | The download was incomplete. Delete `build/ffmpeg-bink2/src` and build again. |
| `build libnetguard first (tools/ws8/netguard/build.sh)` | Run the last command of step 4. |
| `no native build at ...` or `... is not arm64` | Run step 5, or pass `install.sh` the path of the app it printed. |
| `quit Rocksmith 2014 first`, `quit the game first` | Quit the game, then run `install.sh` (or the refresh) again. |
| `not enough free space for the game files` | The game is on another volume that cannot clone (not APFS), so the copy is a real one: free the space it names on the disk of your home folder, then run it again (what is copied is kept), or set `RS_GAME_COPY_DIR` to a folder on a drive with room. |
| `... is not the game's folder` | `RS_GAME_DIR` (or the installer's game folder) must be the folder with `static.psarc` and `songs.psarc`. A refresh says this when the game was uninstalled in Steam or its library is not mounted: the copy is left as it is and keeps working. |
| `shader corpus: not made ...` | Check that step 4 built `build/shims/opengl/msl_verify` and `build/shims/cg/cg_corpus_load`, and that `RC_IMAGE` and `RS_GAME_DIR` are right; details in `<output>/shaders.log`. The app works without it. |
| `warm-shaders: failed ...` or `timed out` | Not fatal: the game prepares its shaders on its first start with Metal. Run `tools/play-native/warm-shaders.sh` to try again. |
| The game now starts with OpenGL, `renderer-fallback.txt` | Metal ended abnormally twice in a row (see [Renderer](#renderer)). Write `metal` into `renderer` to try again; if it keeps crashing, open an issue with `last-run.log` and `previous-run.log`. |
| Launcher: "The native build is not installed" | Run step 6. |
| Launcher: "libnetguard.dylib is missing" | Run the last command of step 4, then step 6. The game never starts without the network guard. |
| Launcher: "RS Re-ARMed has no copy of the game files yet" | Run the installer or `install.sh` again (installs made by earlier versions have no copy). |
| Launcher: "The copy of the game files is incomplete" | A copy or refresh was stopped: **Refresh Game Files**, or `install.sh --refresh-game`. |
| Launcher or refresh: "The drive with the game files (*name*) isn't connected" | The game and its copy are on that drive: connect it and start again. |
| Launcher: "The game files on the other drive are gone" | The copy on the other drive was deleted: run the installer or `install.sh` again for a new one. |
| Launcher: "Rocksmith 2014 is already running" | Quit the running game (native or Rosetta) first. |
| Rocksmith 1 DLC songs you own are missing (e.g. The Final Countdown), or the refresh says "Rocksmith 1 DLC: not checked" | Only Steam knows which of them you own, and a refresh can ask it only while the account RS Re-ARMed plays as is logged in: log in to that account in Steam, then **Refresh Game Files** (or `install.sh --refresh-game`). The answer is kept, so Steam can be closed afterwards. The same songs in the Steam version need that account logged in too. |
| Songs missing after switching Steam accounts (the base game's and the Rocksmith 1 import songs gone, your profiles not listed) | The game played as the account now logged in to Steam, with that account's profiles. **Refresh Game Files** (or `install.sh --refresh-game`) records the account whose license has the Mac game in `game/.rearmed/steam.env`; the launcher uses it from then on. Set `RS_STEAM_ID` to choose another (see [Game files](#game-files)). |
