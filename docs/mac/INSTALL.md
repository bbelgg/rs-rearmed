# Installing the Mac port

You build RS Re-ARMed yourself, on your Mac, from your own Steam copy of Rocksmith 2014 for macOS: the build
recompiles the game to C and compiles that into an arm64 app. Nothing it produces may be shared.

## Requirements

- A Mac with Apple Silicon (M1 or later), macOS 12 Monterey or later.
- The Steam macOS version of **Rocksmith 2014 (Remastered)**, installed through Steam. The installer finds it
  in any Steam library; by hand, set `RS_GAME_DIR` when it isn't in
  `~/Library/Application Support/Steam/steamapps/common/Rocksmith2014`.
- A Real Tone cable, or another audio input the game accepts. macOS asks for microphone access the first
  time the game listens for your guitar ([What macOS will ask](#what-macos-will-ask)).
- Apple's command line tools, with their `python3`. The installer starts their installation when they are
  missing, and you click Install in Apple's window. Nothing from Homebrew, no Rosetta. The game is never
  started: the build reads it from disk.
- An internet connection while building: the source of the release, the FFmpeg 7.1.2 source for the Bink shim
  (from ffmpeg.org, checksum verified) and the build's Python packages (pip; CMake and Ninja among them).
- About 15 minutes for a first build and install on an M2, plus a few minutes for "Preparing shaders".
- The installer keeps 2 GB of the startup disk free for macOS, and counts that in before it starts.
- Room for RS Re-ARMed's own copy of the game files ([Game files](#game-files)): none extra when the game
  is on an APFS volume (the startup disk, or an external drive: the copy then goes onto that drive), else
  about 6.5 GB plus the DLC on the startup disk.

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
Installer, click **Open Anyway** and confirm. It can stay in Downloads.

Leave **Game** on Rocksmith 2014 for Mac and click **Build & Install**. It downloads the source of its own
release (and builds it only if it is, file for file, the source the installer was made from), builds everything
and installs it the same way as `tools/play-native/install.sh`. Whenever the installer needs you, a box above the
log says exactly what to do, with a button for it, and it goes on by itself when that is done. **Stop** in the box
ends the run; what is done is kept, and Build & Install goes on from there.

### The steps

1. **Checking Apple's tools.** If Apple's command line tools are missing, macOS's own window for them opens (it
   may be behind the installer): click Install, then Agree. It takes a few minutes. If you have Xcode and it
   isn't set up, it opens: click Agree and Install, with your Mac's password.
2. **Downloading source**, then **Checking prerequisites**, which finds your game folder in any Steam library. If
   the game isn't installed, the installer leads you there box by box, as for
   [Learn & Play](../windows/INSTALL.md#the-steps): install Steam, sign in with the account that owns the game,
   click Install in Steam's window, wait. If Steam says the game isn't available for Mac, your account has
   Learn & Play: click Stop and choose it under Game.
3. **Setting up Python environment**, then **Decrypting the game**, a few seconds: the game's code is read from
   the Steam copy on disk, which doesn't change. The game isn't started, and it doesn't matter which account
   Steam is signed in with. If it isn't the build RS Re-ARMed is made for, it says so; Verify integrity in Steam
   usually mends that.
4. **Building shims**, **Building the app** and **Packaging**, about 15 minutes in all, then **Preparing shaders**.

Your DLC needs no step of its own: Steam installs it with the game, and the copy takes it from the game's `dlc`
folder ([Learn & Play](../windows/INSTALL.md#the-steps) has a step for it).

- **Renderer** chooses how the game draws: **Metal (recommended)** or **OpenGL** (see
  [Renderer](#renderer)). It starts on the renderer your install uses (OpenGL after a fallback), else on
  the one chosen last time.
- With Metal, **Preparing shaders** compiles the game's Metal shaders once: a few minutes on a first install,
  seconds after an update. **Skip** ends it early; the game then compiles the rest as it first uses them.
- **Build folder**: `~/Library/Caches/rs-rearmed-build` unless you choose another (the installer makes a
  `rs-rearmed-build` folder inside the one you pick and remembers it). Its path must not contain spaces.
  **Delete build files after install** removes them when it is done. For a folder on an external drive,
  macOS asks once whether the installer may use it.
- **Rocksmith 2014 game folder** starts on the folder your install's game files come from, else Steam's
  default library. **Refresh Game Files** (shown once the game is installed) syncs RS Re-ARMed's copy with it:
  after new DLC or custom songs, or a Steam update ([Game files](#game-files)).
- **Built-in Mods…** (also ⌘,) switches off built-in mods of the game chosen above: custom songs, the
  rs-mac-fixes fixes, the intro and Auto Save skips, the menu additions, two players on any input, the title
  logo. A change takes effect the next time the game starts, with nothing to reinstall
  ([Settings and switches](#settings-and-switches)).
- Quitting the installer while it works stops the build (it asks first); Build & Install starts it again.
  The log of every run is kept in `~/Library/Logs/RS Re-ARMed Installer/`.
- A newer release shows as a button in the title row, which opens its download page.

When a run ends, the box above the log says how: **Start the Game** and **Show in Finder**, or **Try Again** and
**Show Log File** when a step didn't work. **Close** (or Escape) ends the installer. Later, start **RS Re-ARMed**
from `~/Applications` (double-click, Launchpad or Spotlight).

## Build by hand

All commands run from the repository root.

### 1. Clone

```bash
git clone https://github.com/bbelgg/rs-rearmed.git
cd rs-rearmed
```

Clone into a folder whose path has no spaces: the build scripts can't handle them.

### 2. Python environment

The recompiler needs Capstone, and the build uses the CMake and Ninja that pip installs next to it:

```bash
python3 -m venv build/venv
build/venv/bin/pip install -r requirements.txt
export RC_PY="$PWD/build/venv/bin/python"
export PATH="$PWD/build/venv/bin:$PATH"     # the venv's cmake and ninja for step 4
```

### 3. The game image

`tools/steamless` makes the decrypted image from your Steam copy in a few seconds, without running or changing it:

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

Use `build` in the repository root: the later steps look there.

### 5. The app

```bash
tools/ws7/recomp/app/build-m4.sh build/m4
```

This recompiles the game image to C, compiles it (the slowest part), makes the Metal shader corpus and links
`build/m4/app/Rocksmith2014.app`, then starts it once with `--init-only` as a check. Everything in the output
folder is derived from the game: keep it on your Mac.

Variables the build reads:

| Variable | Default | Meaning |
|---|---|---|
| `RS_BUILD_DIR` | `build` in the repository | where the app build goes when no output directory is given |
| `RC_IMAGE` | `build/steamless/Rocksmith2014_x64_decrypted` | the game image (step 3) |
| `RC_TARGET` | `mac-x64` | the per-game tables in `tools/targets/<target>` |
| `RC_PY` | `venv/bin/python` in `RS_BUILD_DIR` | Python with Capstone (step 2) |
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

What the optimisation switches measured: [PERFORMANCE.md](PERFORMANCE.md#build-options).

### 6. Install

```bash
tools/play-native/install.sh
```

It makes the [copy of the game files](#game-files), puts the app (`$RS_BUILD_DIR/m4/app/Rocksmith2014.app`, or
the path you pass) and the network guard in `~/Library/Application Support/RS Re-ARMed/`, creates the launcher
`~/Applications/RS Re-ARMed.app`, and prepares the Metal shaders (4.5 to 6.5 minutes the first time on an M2, a second on
later installs; `RS_WARM_SHADERS=0` skips it, `tools/play-native/warm-shaders.sh` does it later). Quit the game
first. Always start the game through the launcher: opening `Rocksmith2014.app` directly doesn't work.

`RS_RENDERER=metal` or `opengl` chooses the renderer (default: the one the install has, else Metal).
`RS_NATIVE_HOME` and `RS_NATIVE_LAUNCHER` change the two install locations.

### Game files

The game reads only its own copy, `~/Library/Application Support/RS Re-ARMed/game/`, so a Steam update or
uninstall can't break it. `install.sh` makes the copy (`tools/play-native/gamefiles.py`): the game's archives and
folders, `metadata`, `Rocksmith.ini`, and from `dlc/` the Mac archives, `*_m.psarc`. It also records your installed
DLC, the language and the account the game plays as in `game/.rearmed/steam.env`: the account whose license has
the Mac game, whichever is logged in later. To choose it yourself, set `RS_STEAM_ID` (a SteamID64 or the number in
`Steam/userdata/<number>`) when you refresh, or with `launchctl setenv` for the launcher.

Each copy or refresh also makes the **Direct Connect overlay**, `game/Update/patcharchives.psarc`
(`tools/play-native/cache_overlay.py`), from your own `cache.psarc`, which isn't changed: it adds DIRECT CONNECT
and turns SHOP into ENUMERATE and UPLAY into EXIT GAME. If your cache can't be read, the copy goes on without it
and says so.

The copy's tools read the game's archives with what the game itself holds (`tools/play-native/psarc_key.py`;
nothing for it is in RS Re-ARMed). The game's program in the game folder shows it, and the copy keeps a note of it
(`game/.rearmed/toc-key`), so a refresh works with the game folder away.

- **Disk use.** On the same APFS volume as your home folder every file is a clone: seconds, and no extra space,
  though `du` and the Finder count the full size.
- **A Steam library on another drive.** On APFS the copy goes onto that drive as clones, in `RS Re-ARMed/game` at
  its top, and `…/RS Re-ARMed/game` is a link to it, so the game needs that drive connected. On a drive that can't
  clone (exFAT, HFS+, a network volume) it is a real copy in your home folder, about 6.5 GB without DLC, after a
  check for room. `RS_GAME_COPY_DIR=DIR` puts a new copy in `DIR/game` (`home`: your home folder; for the
  installer, `launchctl setenv`). A refresh keeps the copy where it is.
- **Refresh** after new DLC or custom songs, or a Steam update: **Refresh Game Files**, or
  `tools/play-native/install.sh --refresh-game`. It copies new and changed files, removes what the game folder no
  longer has, and touches nothing else. If the game folder is gone it changes nothing and says so.
- **Stopped copies** continue on the next run. `game/.rearmed/manifest.json`, written last, marks a copy complete;
  until then the launcher asks for a refresh.

### The title logo

The install makes the logo once, in the copy; if it can't, the game shows its own. To rebuild it, or for what it
needs: [tools/loadscreen](../../tools/loadscreen/README.md). `RS_REARMED=0` (or the logo off in the installer's
Built-in Mods…) shows the original. A refresh that brings a new `static.psarc` removes it, and the next install
makes it again.

## Where things are

| Where | What |
|---|---|
| `~/Applications/RS Re-ARMed.app` | the launcher |
| `~/Library/Application Support/RS Re-ARMed/game/` | the game folder the game reads: RS Re-ARMed's own copy of the game files ([Game files](#game-files)), the installed `Rocksmith2014.app` and an offline `Rocksmith.ini`; for a game on another APFS drive a link to `<drive>/RS Re-ARMed/game`, where all of that is |
| `…/game/.rearmed/` | the copy's manifest (written last), where it comes from (`source.json`), the Steam facts (`steam.env`: the installed DLC, the language, the account) and the install's `Rocksmith.ini` |
| `…/game-dir`, `…/game-source`, `…/gamefiles.py` | the folder the game reads, the folder the copy comes from, and the tool that refreshes it |
| `…/libnetguard.dylib` | the network guard, loaded into the game at every start |
| `…/steam/<account>/221680/remote/` | your profiles and progress for the native build |
| `…/logs/` | `last-run.log`, `previous-run.log`, `netguard.log`, `seed.log` |
| `…/mods/` | mods loaded at startup |
| `…/settings.env` | if you switched any off: the built-in mods switched off in the installer ([Settings and switches](#settings-and-switches)) |
| `…/renderer`, `…/renderer-fallback.txt` | `metal` or `opengl`, and why a fallback changed it ([Renderer](#renderer)) |
| `…/guitar-input.ini` | if you made one: the guitar input's channel and gain ([Guitar input](#guitar-input)) |
| `…/input-level-calibrated` | made by the game's first calibration: the level saved in your profile counts from then on; delete it to start over ([Guitar input](#guitar-input)) |

On the first launch your Steam saves are copied once into `steam/`, without `crd` (stored Ubisoft
credentials). The native build never writes your Steam saves.

## Playing

Start **RS Re-ARMed** from `~/Applications`. The Steam client is not needed. Enter on the title screen goes
straight to the profile list. Custom songs (CDLC) in the game's `dlc` folder load as they are. To add a song
while you play, put it in Steam's `dlc` folder (or `game/dlc`) and press **ENUMERATE** in the main menu: it is in
the list a moment later, with no restart. Changed or removed songs and new DLC need a
[refresh](#game-files).

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

Shift-Command-F or Control-Command-F switches between full screen and a window. Full screen covers the display the window is on, so
with a second display, move the window there first. In a window, drag the edges to resize: the new size becomes
the game's resolution, and the window takes any shape (the original kept it at 16:9; `RS_WINDOW_ANY_SHAPE=0` does
too). The game follows the shape itself, in a window and in full screen alike: its cameras keep their height and
take the resolution's width, and its menus are laid out to the width, so nothing is stretched. A 16:10 window shows
a little less at the sides than 16:9, an ultrawide one more. Full screen hasn't been run on a 16:10 or ultrawide
screen yet.

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
starts with Metal in a row end abnormally (a crash or a force quit; a normal quit resets the count), the next
start uses OpenGL, writes `opengl` into `renderer`, shows a notification and explains it in
`renderer-fallback.txt`. Write `metal` into the file to try Metal again, or choose Metal in the installer.

### Audio latency

1. In the game's Audio settings, set the audio engine setting to 1, then restart the game. If the sound
   crackles, use 2. The game's default is 4.
2. Listen through headphones or a USB audio interface, not the built-in speakers (about 20 ms of
   processing).

The setting is `LatencyBuffer` in `Rocksmith.ini`: the game's output buffer in steps of 256 frames
(5.3 ms). The launcher keeps it across launches. The output device's buffer is `RS_AUDIO_FRAMES`
(default 256; 128 at `LatencyBuffer=1` and on an interface that also has inputs).

### Guitar input

The game's list of inputs under Direct Connect (Path/Input, DIRECT CONNECT, then Available Inputs) shows each channel of
an audio interface: `HX Stomp channel 1` to `channel 8`, say. The bar beside each moves with its signal, so play and
pick the one that moves. The game stops at 10 inputs and the list doesn't scroll, so they go in order: the Real Tone
cable, interfaces, the built-in microphone, others such as a webcam, then virtual devices. Every interface keeps at
least one entry, and with too many channels they share the room.

The game's calibration sets the input's level, and can boost it by up to 24 dB, so a quiet interface reaches the top of
its meter. It doesn't change the volume of your interface in the system. Run calibration once: until then the level
saved in your profile is left out and the input stays as it is. The Real Tone cable works as in the original.

Both can also be set per device, in `~/Library/Application Support/RS Re-ARMed/guitar-input.ini` (Learn & Play reads the same
file):

```ini
[HX Stomp]
Channel=5
```

The section is the device's name in Audio MIDI Setup. `Channel` counts from 1, and such a device has one entry in the
list. `GainDb=6` adds a fixed gain to the level: -24 to 36, default 0 (an HX Stomp needs none); above full scale the
signal clips, as in an interface. The file is read when an input opens, so restart the game after a change and calibrate
again.

Switches: `RS_AUDIO_INPUT_CHANNEL=N` and `RS_AUDIO_INPUT_GAIN_DB=N` (for every device), `RS_AUDIO_INPUT_FILE=PATH`
(another file), `RS_AUDIO_CHANNEL_ENTRIES=N` (the list's length, default 10; 0: one entry per device, in the system's
order), `RS_AUDIO_INPUT_LEVEL=0` (the level stays the device's own volume, with no boost) and `RS_AUDIO_INPUT=0` (none
of this). With `RS_AUDIO_LOG=1` the log has the channel, each level set, and every few seconds the peak and the samples
clipped.

### Two players

Each player's input is in their own profile. Load the profile on its own, set Path/Input to DIRECT CONNECT or MICROPHONE, pick the
input, and leave through the menus so it saves. In MULTIPLAYER each player then has their own: `HX Stomp channel 5` for Player 1 and
`HX Stomp channel 6` for Player 2, say. Two interfaces, or a cable and an interface, go the same way but haven't been played yet. A
profile on REAL TONE CABLE needs a cable, as in the original. With both players in the same mode, SWAP CABLE ASSIGNMENT swaps their inputs and saves them.

For two channels of one interface, leave `Channel` out of `guitar-input.ini` for it, so its channels are listed.
`RS_MODS_MULTIPLAYER=0` is the original rule: Player 2 takes a Real Tone cable only.

## Settings and switches

The rows below that say **Built-in Mods…** can be switched off in the installer. It writes them to `settings.env`
in `~/Library/Application Support/RS Re-ARMed/`, which the launcher reads at every start; without the file every
one is on. By hand it is lines of `KEY=0` (off) or `KEY=1` of those switches: other lines are ignored, and the top
of `logs/last-run.log` says what was used. Learn & Play has its own file, read the same way.

```
RS_MODS_INTRO=0
RS_REARMED=0
```

The other switches are environment variables. The launcher starts the game through macOS, so variables set in a
Terminal do not reach it. Set them for the login session; they win over `settings.env` too:

```bash
launchctl setenv RS_MODS_BYPASS 0     # takes effect on the next launch
launchctl unsetenv RS_MODS_BYPASS     # back to the default
```

Everything below is compiled into the app and on by default unless it says otherwise. LP: Learn & Play has it
too ([its switches](../windows/INSTALL.md#settings-and-switches)). `RS_MODS_LOG=1` writes what the mods apply to
`logs/last-run.log`.

| What | LP | Switch |
|---|---|---|
| [rs-mac-fixes](https://github.com/glebb/rs-mac-fixes) behaviours: more keyboard keys, the End key in menus, faster loading with large libraries, higher priority for worker threads, a higher open-file limit | loading, open files | Built-in Mods…, `RS_MODS_FIXES=0`; `RS_MODS_BUDGET_MS=N`: time per frame for mounting DLC archives (default 50; 0 mounts one per frame, as the game does) |
| CDLC signature skip, a rewrite of [RSBypass](https://github.com/aik002/RSBypass) | yes | Built-in Mods…, `RS_MODS_BYPASS=0` |
| No intro screens; no game file changed | yes | Built-in Mods…, `RS_MODS_INTRO=0` |
| DIRECT CONNECT in the INPUT setting and on the cable screen, from an overlay of your own `cache.psarc` | yes | Built-in Mods…, `RS_MODS_OVERLAY=0` |
| ENUMERATE in place of SHOP: adds new songs, with no restart. Changed and removed songs need **Refresh Game Files** | yes | Built-in Mods…, `RS_MODS_OVERLAY=0` |
| EXIT GAME in place of UPLAY | yes | Built-in Mods…, `RS_MODS_OVERLAY=0` |
| Uplay sign-in prompt skipped | yes | none |
| Auto Save warning skipped | yes | Built-in Mods…, `RS_MODS_AUTOSAVE=0` |
| Offline Steam layer: `libsteam_api` with no client, cloud saves in a local folder | yes | none |
| Song ownership: the base game, your DLC, the AppID of every archive in the copy's `dlc/`, and the Rocksmith 1 songs you own (a copy or refresh asks Steam, with your account logged in) | yes | `RS_STEAM_DLC=all`, or a list of AppIDs |
| Title logo, from your own `static.psarc` | yes | Built-in Mods…, `RS_REARMED=0`, or delete `static.psarc.rearmed` |
| Frame rate follows the display; a fixed rate turns vsync off | | `RS_FPS=N` |
| Lower guitar latency: a smaller output buffer, a real-time audio thread, stale guitar audio dropped | | `RS_AUDIO_FRAMES=N` (0 leaves the system's), `RS_LATENCY_BUFFER=N` (1-16), `RS_AUDIO_RT=0`, `RS_AUDIO_INPUT_TRIM=0`, `RS_AUDIO_LOG=1` |
| [Guitar input](#guitar-input): channel, gain, every channel listed, calibration that can boost | yes | see there |
| [Two players](#two-players) on any input | yes | Built-in Mods…, `RS_MODS_MULTIPLAYER=0` |
| An interface isn't lost when another device, such as a webcam, fails to open | | `RS_AUDIO_STABLE_UIDS=0`, `RS_AUDIO_INPUTS_FIRST=0` |
| A device whose identifier can't be read is skipped (the original crashes at the title) | not needed | always on |
| [Renderer](#renderer) | Metal only | the `renderer` file, the installer, `RS_GL_BACKEND=gl` |
| Retina, Metal only: on a Retina screen the game renders in points and each frame is scaled up to the screen's own pixels with MetalFX (macOS 13+; bilinear on macOS 12) | yes | `RS_METAL_UPSCALE=0` (a plain bilinear scale); `RS_METAL_RETINA=1` renders every pixel instead (about 4x the GPU work) |
| Colours as the game made them (sRGB): the picture is colour-matched to the screen, so a wide-colour (P3) screen, such as a Mac's own, doesn't show it oversaturated | yes | `RS_METAL_COLORS=vivid` (untagged, more saturated on such a screen) |
| Sharper textures, Metal only: the game's own mipmap bias, and 16x anisotropic filtering where the game asks for 1x to 4x | yes | `RS_METAL_ANISO=N` (1-16; 1 keeps the game's) |
| Anti-aliasing, Metal only: 4x MSAA on the game's 3D picture (its screen-sized targets with depth) | yes | `RS_METAL_MSAA=N` (1 off, 2, 4, 8) |
| A window of any shape ([Window modes](#window-modes)) | yes | `RS_WINDOW_ANY_SHAPE=0` keeps it at 16:9 |
| Multithreaded GL engine, OpenGL only (off by default) | | `RS_GL_MTENGINE=1` |

Mods: every `.dylib` in `~/Library/Application Support/RS Re-ARMed/mods/` is loaded at startup and survives a reinstall
(`RS_LOAD_MODS=0` starts without them, `RS_MODS_PATH` loads another folder or one library). Include
`tools/ws7/recomp/runtime/rc_mod.h` and one dylib serves both editions:

```bash
clang -arch arm64 -dynamiclib -undefined dynamic_lookup -Itools/ws7/recomp/runtime -Itools/ws7/recomp/overrides \
    mod.c -o mod.dylib
```

`tools/ws7/recomp/test/hooks/probe_mod.c` is a small one; how mods hook the game:
[DESIGN.md](DESIGN.md#built-in-fixes-and-mods). A mod can also serve files to the game (new in 1.14.0): ones it
opens for reading, their size and date, and names in a folder's listing (`rc_file.h`, which `rc_mod.h` includes).
Mods made for the original game that change its files may work as before: a modified `cache.psarc` is used as it
is.

## Update and uninstall

- **Update**: with the installer, download the new release and run it. By hand, pull, repeat steps 4 and 5
  and run `install.sh` again. Your saves are kept; the game files are refreshed.
- **New DLC or custom songs, a Steam update**: a [refresh](#game-files).
- **Uninstall**: delete `~/Applications/RS Re-ARMed.app` and `~/Library/Application Support/RS Re-ARMed`
  (back up `steam/` first to keep your progress; the folder also holds Learn & Play's `learn-play/`, if you
  have it). That removes the copy of the game files too, unless the game is on another drive: then
  `…/RS Re-ARMed/game` is only a link, and the copy is on that drive, in `RS Re-ARMed/game` at its top (or
  in the folder `ls -l ~/Library/Application\ Support/RS\ Re-ARMed/game` shows): delete that `RS Re-ARMed`
  folder too (it also holds Learn & Play's `learn-play/game`, if you have it there). `static.psarc.rearmed` in
  your Steam game folder, if an early version left one, can be deleted.

## Known issues

- The game lists 10 inputs at most. Two interfaces can fill that, as an 8-channel and a 2-channel one do, and then the built-in
  microphone isn't listed. A `Channel` in `guitar-input.ini` makes such an interface one entry, which leaves room
  ([Guitar input](#guitar-input)).
- Stopped partway, calibration still counts as run, so the level saved before it comes back until you run it to the end.
- No online features: Ubisoft services are not available.
- Progress made in the native build is not copied back to your Steam saves.
- Only the one Steam macOS build of Rocksmith 2014 is supported.
- With Metal, the first time a song, venue or mode is played, a few short stutters (up to about half a
  second) can show in its first half-minute while Metal compiles what it draws for the first time. Later
  plays are smooth.
- After a macOS update every shader is compiled again as the game uses it; run `tools/play-native/warm-shaders.sh` or
  click Build & Install again to have it done first.
- Mods that draw with OpenGL themselves show nothing with Metal; the rest of the mod works. Use a version
  that draws through the overlay API (`shims/include/rs_overlay.h`), or the OpenGL renderer.

## Troubleshooting

Logs: `~/Library/Application Support/RS Re-ARMed/logs/` (`last-run.log`, `previous-run.log`,
`netguard.log`); the installer's in `~/Library/Logs/RS Re-ARMed Installer/`. `RS_MODS_LOG=1` and
`RS_AUDIO_LOG=1` add what the built-in fixes and the audio setup do.

The launcher's alerts (not installed, no copy of the game files or an incomplete one, the drive with them not
connected, the game already running, macOS not letting it read the files) and the scripts' `quit … first` and
`build … first` say what to do themselves. The others:

| Message | What to do |
|---|---|
| `missing .../build/shims/game/librs_game.a` | Run step 4 with `build` as the build directory. |
| `warning: no arm64 libsteam_api.dylib` (or `libBink2Macx64.dylib`, `Cg`) | That shim was not built: run step 4. |
| `UnicodeDecodeError` from Python's `site.py` in step 2 | The repository is on a drive such as exFAT, where macOS writes `._` files. Run `find build/venv -name '._*' -delete`, step 2 again, then the `find` once more. |
| `thunk generation failed, see .../thunks.log` | Check that `RC_PY` points at the environment from step 2 and `RC_IMAGE` at the game image. |
| `warning: no RS2014.icns in ...` | The game is not in the default Steam library: set `RS_GAME_DIR`. |
| `link: undefined symbols, see .../undefined.txt` or `skip list did not converge` | The build did not complete. Open an issue with that file and the logs in the output directory. |
| FFmpeg checksum `FAILED` | The download was incomplete. Delete `build/ffmpeg-bink2/src` and build again. |
| `no native build at ...` or `... is not arm64` | Run step 5, or pass `install.sh` the path of the app it printed. |
| `not enough free space for the game files` | The game is on a volume that can't clone (not APFS), so the copy is a real one: free the space it names on the disk of your home folder and run it again (what is copied is kept), or set `RS_GAME_COPY_DIR` to a folder on a drive with room. |
| `... is not the game's folder` | `RS_GAME_DIR` (or the installer's game folder) must be the folder with `static.psarc` and `songs.psarc`. A refresh says this when the game was uninstalled in Steam or its library is not mounted: the copy is left as it is and keeps working. |
| `shader corpus: not made ...` | Check that step 4 built `build/shims/opengl/msl_verify` and `build/shims/cg/cg_corpus_load`, and that `RC_IMAGE` and `RS_GAME_DIR` are right; details in `<output>/shaders.log`. The app works without it. |
| `warm-shaders: failed ...` or `timed out` | Not fatal: the game compiles its shaders as it first uses them. Run `tools/play-native/warm-shaders.sh` to try again. |
| The game now starts with OpenGL, `renderer-fallback.txt` | Metal ended abnormally twice in a row ([Renderer](#renderer)). Write `metal` into `renderer` to try again; if it keeps crashing, open an issue with `last-run.log` and `previous-run.log`. |
| Launcher: "libnetguard.dylib is missing" | Run the last command of step 4, then step 6. The game never starts without the network guard. |
| Rocksmith 1 DLC songs you own are missing, or the refresh says "Rocksmith 1 DLC: not checked" | Only Steam knows which of them you own, and a refresh can ask it only while the account RS Re-ARMed plays as is logged in: log in to that account in Steam, then **Refresh Game Files** (or `install.sh --refresh-game`). The answer is kept, so Steam can be closed afterwards. |
| Songs or profiles missing after switching Steam accounts | **Refresh Game Files** (or `install.sh --refresh-game`): it records the account whose license has the Mac game in `game/.rearmed/steam.env`, and the launcher uses it whoever is logged in to Steam. Set `RS_STEAM_ID` to choose another ([Game files](#game-files)). |
| No guitar sound | Check the INPUT setting in the game (Real Tone cable, microphone, DIRECT CONNECT) and that macOS allowed microphone access. |
| The game crashes | Open an issue with `last-run.log` and `previous-run.log`. |
