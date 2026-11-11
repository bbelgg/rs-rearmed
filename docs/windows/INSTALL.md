# Installing the Windows port (Learn & Play)

Rocksmith 2014 **Learn & Play** is a purchased edition that Steam offers for Windows only: its license has no
Mac download, so the [Mac port](../mac/INSTALL.md) can't use it. RS Re-ARMed builds that Windows game into a
native arm64 Mac app, on your Mac, from your own copy. Songs, scoring, the Guitarcade, Session Mode, Nonstop
Play, lessons and the window modes work, but it has had far less testing than the Mac port
([PLAN.md](PLAN.md#status)).

## Requirements

- A Mac with Apple Silicon (M1 or later), macOS 12 Monterey or later.
- Rocksmith 2014 Learn & Play on your Steam account. Steam for Mac downloads the Windows game for you
  (about 5.5 GB, plus about 16 MB for each DLC; all of it must be free up front, on a disk you choose or on the
  startup disk).
- Apple's command line tools. The installer starts their installation when they are missing, and you click
  Install in Apple's window. Nothing else: no Windows PC (with the released installer:
  [Without the seed](#without-the-seed-the-one-time-capture)), no Rosetta, no Homebrew, no CMake, no steamcmd, no .NET.
- An internet connection while building: Steam's download, the source of the release, the build's Python
  packages (Capstone, Unicorn and pefile, with pip) and, only if Steam's copy is missing, Microsoft's
  redistributables.
- About 25 minutes for the first build on an M2 (8 cores), about 2 GB free in the build folder and 8 GB of
  memory ([what it needs](PERFORMANCE.md#the-build-memory-and-time)).
- The installer keeps 2 GB of the startup disk free for macOS, and counts that in before it starts.
- Room for the app's own copy of the game files ([Game files](#game-files)): none extra when Steam's
  download is on an APFS volume (the startup disk, or an external drive it was redirected to: the copy then
  goes onto that drive), else about 6 GB plus the DLC on the startup disk.
- A Real Tone cable, or another audio input. macOS asks for microphone access the first time the game
  listens for your guitar ([What macOS will ask](#what-macos-will-ask)).

### What macOS will ask

These windows come from macOS, not from the installer, and can be hidden behind other windows. If a step stands
still, look for one; after two minutes without news the installer says so too.

| When | The window | Answer | If you say no |
|---|---|---|---|
| The first step, without Apple's command line tools | Install the command line developer tools? | Install, then Agree. No password. | The installer waits; Open Apple's Installer shows it again. |
| The first step, with Xcode installed but new or just updated | Xcode's license, then "install additional required components" | Agree; Install, and your Mac's password (it goes to macOS, not to the installer) | The build can't run. The installer waits: Open Xcode shows it again. |
| A build folder or Steam download on another disk | RS Re-ARMed Installer, or Steam, would like to access files on a removable volume | Allow | That step fails. Turn it on in System Settings, Privacy & Security, Files & Folders, under the app's name, then Try Again. |
| The game's first start, when its files are on another disk | The game would like to access files on a removable volume | Allow | The game says it can't read its files, and stops. Same place in System Settings, then start it again. |
| The game first listens for your guitar | The game would like to access the microphone | Allow | No guitar sound. Turn it on in System Settings, Privacy & Security, Microphone, then restart the game. |

The game does not use your Documents, Desktop or Downloads folders, so it never asks for them. If you put the build
folder in one of those, macOS asks the installer once.

### Without the seed: the one-time capture

The Windows game's code is encrypted on disk and only decrypted in memory while it runs. The released installer
carries a small seed (`seed.bin`, no game code) with which it unpacks your own exe on the Mac, so **nothing here
applies to it**. A build from a clone of this repository has no seed: it needs one you supply (`LP_SEED`), or a
capture of your own game's code, made once on a Windows PC in about 5 minutes. The install prepares the folder to
take there and says what to do; the steps are in [CAPTURE.md](CAPTURE.md#path-a-harvest-the-image). The capture
stays with you, and the image built from it is kept for later runs.

## Install with the installer

Download `RS.Re-ARMed.Installer.app.zip` from the [Releases](https://github.com/bbelgg/rs-rearmed/releases) page and
open it, which unzips it. Then open **RS Re-ARMed Installer**.

The installer is not notarized, so the first time macOS refuses to open it. Double-click it once and close the
message; then open **System Settings**, **Privacy & Security**, scroll down to the line about RS Re-ARMed
Installer, click **Open Anyway** and confirm. It can stay in Downloads.

Choose **Game: Learn & Play** (preselected when the Mac game is not installed) and click **Build & Install**. That
is all, if Steam is on this Mac and signed in with the account that owns Learn & Play. It downloads the source of
its own release (and builds it only if it is, file for file, the source the installer was made from), and does
the same steps as `tools/play-native/learn-play.sh`. Whenever the installer needs you, a box above the log says
exactly what to do, with a button for it, and it goes on by itself when that is done. **Stop** in the box ends the
run; what is done is kept, and Build & Install goes on from there.

### The steps

1. **Checking Apple's tools.** If Apple's command line tools are missing, macOS's own window for them opens (it
   may be behind the installer): click Install, then Agree. It takes a few minutes. If you have Xcode and it
   isn't set up, it opens: click Agree and Install, with your Mac's password. **Downloading source** and
   **Checking prerequisites** follow by themselves.
2. **Downloading the Windows game.** The installer opens Steam if it isn't open and tells it to download the
   game, with your own account, into a folder of its own. Your Mac game and saves are never touched. Steam's
   window doesn't show this download; the progress is in the installer. What can come up:

   | The box says | What to do |
   |---|---|
   | **Install Steam** | Click Get Steam, install it and open it. |
   | **Steam is starting** | Wait. Steam may update itself first. |
   | **Sign in to Steam** | Sign in, in Steam's window, with the account that owns Learn & Play. |
   | **This Steam account doesn't have the game** | Open the Steam menu, choose Change Account…, and sign in with the one that owns it. Steam restarts, and the download starts by itself. |
   | **Steam lost its connection**, **The download has stopped moving**, **Steam couldn't download the game** | Check the Mac's internet connection. It goes on by itself, or click Try Again. If Steam was closed, the installer opens it again, and the download continues from where it was. |
   | **One step in Steam, please** | Rare: Steam ignored the installer. Its console is open and the command is copied: click in the box at the bottom of the console, press ⌘V, then Return. |

   If there isn't room on the startup disk, it asks for a folder on another disk (an external drive works; if
   macOS asks whether Steam may use it, click Allow), or free some space and click Check Again. What Steam already
   downloaded moves along.
3. **Downloading your DLC.** Each DLC is a Steam download of its own. The installer reads which DLC your Steam
   account owns and has Steam fetch them one after another ("12 of 73"). If it is stopped, the next run
   continues. One that fails doesn't stop the install: the box at the end names it, and Refresh Game Files tries
   again.
4. **Copying the game files**: the app's own copy of the game, `…/RS Re-ARMed/learn-play/game`
   ([Game files](#game-files)). It goes on the same APFS disk as Steam's download, where it takes seconds
   and no extra space.
5. **Getting Microsoft libraries**: the four DLLs the game loads, from Steam's Common Redistributables on
   your Mac, or Microsoft's own redistributables if those are missing; every file is checked.
6. **Code image**: your own exe is unpacked on the Mac, in a few minutes. (A build without the seed asks for a
   capture folder here, or stops and says what to do.)
7. **Building the game**: the game and the libraries are translated to C and compiled into one arm64 app.
   This is the long part. If it is stopped, run it again and it continues where it was.
8. **Installing**: `~/Applications/RS Re-ARMed Learn & Play.app`. The build files are deleted afterwards,
   except about 140 MB of translated code that makes a later run much quicker.
9. **Preparing shaders**: the installed app compiles the game's Metal shaders once, so the first play doesn't
   stall on them: about 20 seconds the first time, a second on later installs. **Skip** ends it early; the game
   then compiles the rest as it first uses them.

- **Windows game folder** stays empty for Steam's download; choose a folder only for a game copied from
  elsewhere, such as a Windows PC.
- **Game files** matters only when Steam's download is on another drive than your home folder: **On the game's
  drive** keeps the app's copy there, with no extra space, and the game needs that drive connected; **On this
  Mac** makes a full copy in your home folder, and the game plays without the drive. It shows where the copy is
  now. Choose the other and click Build & Install or Refresh Game Files, and the copy is moved
  ([Game files](#game-files)).
- **Refresh Game Files** (shown once the game is installed) has Steam fetch DLC you bought since, and syncs the
  app's copy of the game files with Steam's folders again ([Game files](#game-files)).
- **Built-in Mods…** (also ⌘,) switches off built-in mods of the game chosen above: custom songs, the
  rs-mac-fixes fixes, the intro and Auto Save skips, the menu additions, two players on any input, the title
  logo. A change takes effect the next time the game starts, with nothing to reinstall
  ([Settings and switches](#settings-and-switches)).
- **Build folder**, quitting, the log and a newer release are as in the
  [Mac edition](../mac/INSTALL.md#the-steps). **Renderer** has no effect here ([Renderer](#renderer)).

When a run ends, the box above the log says how: **Start the Game** and **Show in Finder**, or **Try Again** and
**Show Log File** when a step didn't work. **Close** (or Escape) ends the installer. Later, start
**RS Re-ARMed Learn & Play** from `~/Applications` (double-click, Launchpad or Spotlight).

## Install from a clone

```bash
tools/play-native/learn-play.sh [CAPTURE]      # or: tools/play-native/install.sh --learn-play [CAPTURE]
```

The same steps as the installer, in a terminal. With a seed (`LP_SEED`, or `win-x86/unpack/seed.bin`; git has none)
it needs nothing else. `CAPTURE` is an unzipped capture folder (or `LP_HARVEST`) to use instead; with neither,
the first run prepares the capture folder, opens it and stops, and you run it again with the folder. Re-run it to
update. `tools/play-native/learn-play.sh --refresh` only refreshes the game files.

An earlier run's code image is kept only if it is the one this version is made for (`image_sha256` in
`tools/targets/win-x86/target.toml`); `lp_install.py image-check EXE` says whether it is.

| Variable | Meaning |
|---|---|
| `RS_BUILD_DIR` | the build folder (default `build/` in the repository; no spaces in its path). The build goes to `$RS_BUILD_DIR/win-x86/lp`, the DLLs to `…/win-x86/dlls`, the image to `…/win-x86/image` |
| `LP_GAME_DIR` | the Windows game folder to copy from, when it is not Steam's download folder (for example a copy from a Windows PC) |
| `LP_HOME` | the app's folder (default `~/Library/Application Support/RS Re-ARMed/learn-play`); the copy goes to its `game/` (or, for a game on another APFS drive, that is a link to the copy there) |
| `RS_GAME_COPY_DIR` | where a new copy goes: `DIR/learn-play/game`, with `game/` a link to it; `home`: in `LP_HOME` whatever the drive, `drive`: on the game's drive, and these two also move a copy that is in the other place ([Game files](#game-files)) |
| `LP_DOWNLOAD_DIR` | a folder on another disk for Steam's download, when the startup disk is short of space |
| `LP_SEED` | the unpacker's seed, when it is not `win-x86/unpack/seed.bin` |
| `LP_HARVEST`, `LP_HARVEST_V1` | the capture folder, when it is not given as `CAPTURE`; a harvester v1 capture of the same build, as its reference |
| `RS_STEAM_DIR` | the Steam folder (default `~/Library/Application Support/Steam`) |
| `LP_NATIVE_APP` | where to install the app (default `~/Applications/RS Re-ARMed Learn & Play.app`) |
| `LP_KEEP_BUILD` | `1`: keep all build files; `0`: delete them all, the cache too |
| `LP_JOBS` | parallel jobs |
| `RS_WARM_SHADERS` | `0`: no "Preparing shaders" at the end; run `tools/play-native/warm-shaders.sh "<the app>"` later to do it then |
| `RS_SHADER_CORPUS` | `0`: no shader corpus |
| `RS_MSL_CORPUS_FILE` | bundle this `rsmsl-corpus.bin` instead of making one |
| `RC_PY` | a Python with the packages of `win-x86/install/requirements.txt`, instead of the one made in `$RS_BUILD_DIR/venv` |

The steps one at a time: `python3 win-x86/install/lp_install.py --help`.

## Game files

The app reads only its own copy, `~/Library/Application Support/RS Re-ARMed/learn-play/game/`, as Steam may
clear its download folder. The copy (`lp_install.py game-copy`, with `tools/play-native/gamefiles.py`) has the
Windows game folder and, from its `dlc/` and the Mac game's `dlc` folder in each Steam library, the PC song
archives, `*_p.psarc`. It also records your DLC, the language and the account the game plays as in
`game/.rearmed/steam.env`: the account whose license has Learn & Play (depot 221683), whichever is logged in later. To choose it
yourself, set `RS_STEAM_ID` (a SteamID64 or the number in `Steam/userdata/<number>`) for a refresh.

Each copy or refresh also makes the **Direct Connect overlay**, `game/Update/patcharchives.psarc`
(`tools/play-native/cache_overlay.py`), from the copy's `cache.psarc`, which isn't changed: it adds DIRECT CONNECT
and turns SHOP into ENUMERATE and UPLAY into EXIT GAME. If the cache can't be read, the copy goes on without it
and says so.

The copy's tools read the game's archives with what the game itself holds (`tools/play-native/psarc_key.py`;
nothing for it is in RS Re-ARMed). Learn & Play's exe shows it only once unpacked, so the first copy leaves your
DLC's list and the overlay to the build step, which keeps a note of it (`game/.rearmed/toc-key`); a refresh after
that needs neither the image nor the game folder.

- **Disk use.** On the same APFS volume the copy is clones: seconds, and no extra space, though `du` and the
  Finder count the full size. Once it's made, Steam's download may be deleted, though that frees nothing if the
  copy is clones of it.
- **Steam's download on another drive.** On APFS the copy goes onto that drive as clones, in
  `RS Re-ARMed/learn-play/game` at its top, and `…/learn-play/game` is a link to it, so the game needs that drive
  connected. From a drive that can't clone it is a real copy, about 6 GB plus DLC, after a check for room.
  The installer's **Game files** choice picks the place, and moves a copy that is in the other one: to the
  app's folder as a real copy (after a check for room; the game then starts without the drive and macOS doesn't
  ask about it), or to the game's drive as clones. Everything in the copy moves, and the old one is removed only
  when the new one is whole. By hand that is `RS_GAME_COPY_DIR=home` or `RS_GAME_COPY_DIR=drive` for an install
  or a refresh. `RS_GAME_COPY_DIR=DIR` puts a new copy in `DIR/learn-play/game` (for the installer,
  `launchctl setenv`; its choice is then switched off). Without any of them a refresh keeps the copy where it
  is.
- **New songs while you play**: put a song in a folder the copy is made from (the Mac game's `dlc/` for its
  `*_p.psarc`) or in `game/dlc`, and press **ENUMERATE** in the main menu: it is in the list a moment later,
  with no restart. Changed or removed songs need a refresh.
- **Refresh** after new DLC: **Refresh Game Files** in the installer, or
  `tools/play-native/learn-play.sh --refresh`. It copies new and changed files, removes what the sources no
  longer have, and touches nothing else. If Steam's download is gone it changes nothing and says so.
- **Stopped copies** continue on the next run. `game/.rearmed/manifest.json`, written last, marks a copy
  complete; until then the app asks for a refresh and quits.

### How the game finds DLC

The game looks in one place only, the `dlc` folder of its own copy (`…/RS Re-ARMed/learn-play/game/dlc`), when
it starts. Nothing is downloaded by the game, and Steam's folders are read only when you press ENUMERATE.

1. **What gets into `game/dlc`.** The Windows game is not installed by Steam here, so nothing installs its DLC:
   the installer has Steam download each DLC your account owns as a download of its own
   ([the steps](#the-steps)). A copy or a refresh then takes every `*_p.psarc`, in subfolders too, from those
   downloads, from the Windows game folder's `dlc`, and from the Mac game's `dlc` folder in each Steam library
   (Steam puts the PC archives there as well), and nothing else: the Mac archives, `*_m.psarc`, stay behind. A
   file the sources no longer have is removed from the copy at the next refresh.
2. **Which archives the game shows.** Each archive carries an AppID, and the game mounts it only when Steam
   lists that AppID as a DLC of the game and says it is installed. The app's offline Steam layer answers that
   from `game/.rearmed/steam.env` (`RS_STEAM_DLC`), which the copy writes: the DLC your account's license
   lists, and the AppID of every archive in `game/dlc`, read from the archive itself. So a song shows whatever
   AppID it carries, with the Steam client closed. `RS_STEAM_DLC=all` counts every AppID as owned
   ([Settings and switches](#settings-and-switches)).
3. **Custom songs** need one more thing, the built-in CDLC signature skip, which is on unless you switched it
   off in Built-in Mods…, and they must be the PC version, `_p.psarc`.
4. **Songs of the first Rocksmith.** They are all in one archive that comes with the game
   (`rs1compatibilitydlc`), and the game shows each only when your account owns the first Rocksmith's DLC it
   came from. Nothing on disk says that, so a copy or refresh asks the Steam client, which must be running with
   the account the app plays as. The answer is kept (`RS_STEAM_OWNED` in `steam.env`), so Steam can be closed
   afterwards.
5. **New songs.** While you play, **ENUMERATE** in the main menu takes new archives from the folders the copy is
   made from (and finds ones put straight into `game/dlc`) with no restart. DLC bought since, and changed or
   removed songs, need a refresh, which has Steam fetch the new DLC first.

The design, with the game's own calls, is in [DESIGN.md](DESIGN.md#steam).

### The title logo

The build makes the logo once, in the copy; if it can't, the game shows its own. To rebuild it, or for what it
needs: [tools/loadscreen](../../tools/loadscreen/README.md). `RS_REARMED=0` (or the logo off in the installer's
Built-in Mods…) shows the original. A refresh that brings a new `static.psarc` removes it, and the next build
makes it again.

## Where things are

| Where | What |
|---|---|
| `~/Applications/RS Re-ARMed Learn & Play.app` | the game: `rs2014_lp`, libRSOpenGL, the game image and the DLL images; ad-hoc signed |
| `~/Library/Application Support/RS Re-ARMed/learn-play/game/` | the app's own copy of the game files, the only game folder it reads ([Game files](#game-files)); for a game on another APFS drive a link to `<drive>/RS Re-ARMed/learn-play/game`, where the copy is |
| `…/learn-play/game/.rearmed/` | the copy's manifest (written last), where it comes from (`source.json`) and the Steam facts (`steam.env`: your installed DLC, the language, the account) |
| `…/learn-play/game/static.psarc` | a link the app sets at each start: to `static.psarc.original`, the game's own, or `static.psarc.rearmed`, the [title logo](#the-title-logo) |
| `…/learn-play/game-dir` | the game folder the app reads (one line): `game/` |
| `…/learn-play/gamefiles.py` | the tool that refreshes the copy |
| `…/learn-play/userdata/` | the game's writes: `game/Rocksmith.ini` (audio and display settings) |
| `…/learn-play/steam/` | your profiles and progress |
| `…/learn-play/logs/` | `last-run.log`, `previous-run.log`, `netguard.log` (with mods) |
| `…/learn-play/mods/` | mods loaded at startup |
| `…/learn-play/settings.env` | if you switched any off: the built-in mods switched off in the installer ([Settings and switches](#settings-and-switches)) |
| `~/Library/Application Support/RS Re-ARMed/guitar-input.ini` | if you made one: the guitar input's channel and gain, for both editions ([Guitar input](#guitar-input)) |
| `~/Library/Application Support/RS Re-ARMed/learn-play/input-level-calibrated` | made by the game's first calibration: the level saved in your profile counts from then on; delete it to start over ([Guitar input](#guitar-input)) |

On the first start, profiles in your Steam cloud folder for the game, if Steam has one on this Mac, are
copied into `steam/` (without `crd`).

## Playing

Start **RS Re-ARMed Learn & Play** from `~/Applications`. It runs offline: no Steam client, no Ubisoft login,
no network. Enter on the title screen goes straight to the profile list. Custom songs (CDLC) load as they are,
in their PC version, `*_p.psarc` ([Known issues](#known-issues)); to add one while you play, see
[Game files](#game-files).

### Keys

The game reads the keyboard through DirectInput: a Mac key arrives as the Windows key in the same place. The
window must be in front.

| Game prompt | Press |
|---|---|
| ENTER | Return |
| ESC | Esc |
| DELETE (for example "Skip Tuner") | **fn + Delete** (plain Delete is Backspace, which the game takes as "back") |
| SPACE / CTRL / TAB / SHIFT / ALT | the same keys |

From the title to a song: Return (title), Return (profile, or New profile), Return (LEARN A SONG), Return
(the song), Return (Play Song), then tune, or skip with fn + Delete and Return on "Yes, continue without
tuning". Return on the tuner itself is Calibrate, which waits for the guitar.

### Window modes

The game starts full screen (a borderless window over the main screen). Its own setting picks the mode, at
once and without a restart: from the main menu, Space (Tools) > Options > Visual Settings > Windowed Mode,
then Windowed, Non-Exclusive Fullscreen or Exclusive Fullscreen. Resolutions, below it, sets the size. It
is saved as `[Renderer.Win32] Fullscreen` in `userdata/game/Rocksmith.ini`. Full screen from a window covers
the display the window is on.

In a window, drag the edges to resize: the new size becomes the game's resolution, and the window takes any
shape. The green button, Control-Command-F or Shift-Command-F gives macOS full screen, at the screen's size. Either key in
the game's own full screen gives a window of the screen's shape instead, which scales the picture when resized:
the game stays in full-screen mode. Command-Q, the close button and the game's own Exit all quit through the
game's shutdown. The display mode never changes.

The game follows the shape itself, with no bars, as in the [Mac edition](../mac/INSTALL.md#window-modes): its
cameras keep their height and take the width, and its menus are laid out to the width. On Windows this game keeps
a 16:9 picture with bars; `RS_WINDOW_ANY_SHAPE=0` does that here too, and a resized window then scales the
picture with its shape kept.

### Renderer

Metal only, so there is no choice and no fallback ([Known issues](#known-issues)).

### Audio latency

1. In the game's Audio settings, set the audio engine setting to 1, then restart the game. If the sound
   crackles, use 2. The game's default is 4.
2. Listen through headphones or a USB audio interface, not the built-in speakers (about 20 ms of
   processing).

The setting is `[Audio] LatencyBuffer` in `userdata/game/Rocksmith.ini`. The output device's buffer is
`RS_AUDIO_FRAMES` (default 256; 128 on an interface that also has inputs). The delay has not been measured
against the Mac port.

### Guitar input

The same as in the Mac edition, from the same code and the same `guitar-input.ini`:
[Guitar input](../mac/INSTALL.md#guitar-input). The list with each channel of an interface, the calibration that
can boost, the per-device `Channel` and `GainDb` and the `RS_AUDIO_INPUT…` switches all hold here. Only a device
named "Rocksmith USB Guitar Adapter" counts as the Real Tone cable.

### Two players

As in the Mac edition: [Two players](../mac/INSTALL.md#two-players). SWAP CABLE ASSIGNMENT on interfaces hasn't
been played in this edition yet. `RS_MODS_MULTIPLAYER=0` is the original rule: Player 2 takes a Real Tone cable
only.

## Settings and switches

The rows below that say **Built-in Mods…** can be switched off in the installer. It writes them to `settings.env`
in `~/Library/Application Support/RS Re-ARMed/learn-play/`, which the app reads at every start; without the file
every one is on. By hand it is lines of `KEY=0` (off) or `KEY=1` of those switches: other lines are ignored, and
the top of `logs/last-run.log` says what was used. The Mac edition has its own file, read the same way.

```
RS_MODS_INTRO=0
RS_REARMED=0
```

The other switches are environment variables. To pass one, start the app with `open`, or set it with
`launchctl setenv`; either wins over `settings.env` too:

```bash
open -n --env RS_FULLSCREEN=0 "$HOME/Applications/RS Re-ARMed Learn & Play.app"
```

| Switch | Default | What |
|---|---|---|
| `RS_FULLSCREEN=1\|0` | the game's setting | force full screen, or a window |
| `RS_WINDOW_ANY_SHAPE=0` | on | 16:9 with bars, as on Windows, and a resized window keeps the game's resolution ([Window modes](#window-modes)) |
| `RS_METAL_RETINA=1` | off | render every pixel of a Retina screen (otherwise each frame is scaled up to them with MetalFX), about 4x the GPU work |
| `RS_METAL_UPSCALE=0` | on | no MetalFX upscaling on a Retina screen: a plain bilinear scale |
| `RS_METAL_COLORS=vivid` | sRGB | leave the picture untagged, so a wide-colour screen shows it more saturated |
| `RS_METAL_ANISO=N` | 16 | the least anisotropic filtering of mipmapped textures, 1-16; 1 keeps the game's |
| `RS_METAL_MSAA=N` | 4 | MSAA on the game's screen-sized targets with depth: 1 (off), 2, 4 or 8 |
| `RS_AUDIO_FRAMES=N` | 256 | the output device's buffer, in frames (0 leaves the device's) |
| `RS_AUDIO_LOG=1` | off | log the audio setup to `logs/last-run.log` |
| `RS_AUDIO_RT=0`, `RS_AUDIO_INPUT_TRIM=0`, `RS_AUDIO_SET_RATE=0` | on | the real-time audio thread, the stale-input trim, switching devices to 48 kHz |
| `RS_AUDIO_INPUT…`, `RS_AUDIO_CHANNEL_ENTRIES=N` | | the guitar input's channel, gain, level and list: [Guitar input](../mac/INSTALL.md#guitar-input) |
| `RS_MODS_MULTIPLAYER=0` | any input | [two players](#two-players): Player 2 on a Real Tone cable only (Built-in Mods…) |
| `RS_AUDIO_RS_NAMES=0` | on | keep CoreAudio's device names (by default only the cable is called "Rocksmith") |
| `RS_MODS_BYPASS=0` | on | the song-file signature check and the list of known song keys work as in the original: custom songs are not listed (Built-in Mods…) |
| `RS_MODS_FIXES=0` | on | the batched DLC mounting, the open-file limit, the song path cache and the number-table hash, off (Built-in Mods…) |
| `RS_MODS_BUDGET_MS=N` | 50 | the time per frame for mounting DLC |
| `RS_MODS_INTRO=0` | on | the intro screens play (Built-in Mods…) |
| `RS_MODS_AUTOSAVE=0` | on | the Auto Save warning shows after profile select (Built-in Mods…) |
| `RS_MODS_LOG=1` | off | log what the built-in fixes apply to `logs/last-run.log` |
| `RS_MODS_OVERLAY=0` | on | the game reads `cache.psarc` alone, without the overlay: no DIRECT CONNECT, SHOP instead of ENUMERATE and UPLAY instead of EXIT GAME ([Game files](#game-files); Built-in Mods…) |
| `RS_REARMED=0` | on | the game's own title logo ([The title logo](#the-title-logo); Built-in Mods…); set for the build, no logo is made |
| `RS_STEAM_DLC=all`, or a list of AppIDs | your DLC, and the AppID of every archive in the copy's `dlc/` | the songs the game counts as owned |
| `LP_NATIVE_MATH=0` | on | the CRT's math translated instead of the host's libm |
| `LP_NATIVE_MEM=0` | on | the CRT's `memcpy`, `memmove`, `memset` and `memcpy_s` translated instead of the host's |
| `LP_QOS=0` | on | the default thread priority for the game's threads |
| `LP_HOME=DIR` | `~/Library/Application Support/RS Re-ARMed/learn-play` | another folder for settings, saves and logs |
| `LP_GAME_DIR=DIR` | the folder in `game-dir` | another game folder (any folder with the game; the copy check is skipped) |

Diagnostics switches: [TESTING.md](TESTING.md#diagnostics).

Mods: every `.dylib` in `~/Library/Application Support/RS Re-ARMed/learn-play/mods/` is loaded at startup
(`RS_LOAD_MODS=0` starts without them, `RS_MODS_PATH` loads another folder or one library). A mod has to be
built with `rc_mod.h` ([how](../mac/INSTALL.md#settings-and-switches)); the same dylib then runs in the Mac
edition, and one built without it is left out, with a line in the log. Mods load only with the network guard in
the process, which logs to `logs/netguard.log`. How mods hook the game:
[DESIGN.md](DESIGN.md#built-in-fixes-and-mods). A mod can also serve files to the game (new in 1.14.0): ones it
opens for reading, their size and date, and names in a folder's listing (`rc_file.h`, which `rc_mod.h` includes).

## Update and uninstall

- **Update**: with the installer, download the new release and run it. By hand, pull and run `learn-play.sh`
  again. The game image and the translated code are reused, and only what changed is rebuilt. Your saves are
  kept; the game files are refreshed.
- **New DLC**: a [refresh](#game-files).
- **Uninstall**: delete the app and `~/Library/Application Support/RS Re-ARMed/learn-play` (back up
  `steam/` first to keep your progress); that removes the copy of the game files too, unless the game is on
  another drive: then `learn-play/game` is only a link, and the copy is on that drive, in
  `RS Re-ARMed/learn-play/game` at its top (or the folder `ls -l` of that link shows): delete it too. The
  Windows game Steam downloaded stays in Steam's download folder
  (`~/Library/Application Support/Steam/Steam.AppBundle/Steam/Contents/MacOS/steamapps/content/app_221680`,
  or where you redirected it); delete it there if you want the space back (the app no longer needs it).
  `guitar-input.ini` stays too, as the Mac edition reads it.

## Known issues

- The guitar input's two, as in the [Mac edition](../mac/INSTALL.md#known-issues): the game lists 10 inputs at
  most, and a calibration stopped partway still counts as run.
- It has had far less testing than the Mac port. Not tested yet: the Workshop beyond the tone designer, the DLC
  preview ([PLAN.md](PLAN.md#status)).
- Mods: 18 of the 21 hook points, the overlay API and `OV_VCALL` are there. A mod can't yet: use what else the
  Mac app exports beyond `rc_mod.h`, such as `ov_fn` (the mod then fails to load, with the reason in the log);
  change an event's arguments in `input_sink` (it can keep the event from the game); pass floating point
  arguments to `rc_call_guest` (`rc_call_name_f` can); call a function that takes its own registers by name; give
  the game a pointer to the mod's own memory (the game gets null, with a line in the log; `rc_guest_alloc` gives
  memory it can have); or quit by `game_frame`'s result (not taken, with a line in the log; a pre hook still
  skips the tick). A field this game doesn't have gives `RC_NO_OFF`: test `RC_HAS_OFF`.
- Song archives: the Windows game reads the PC files, `*_p.psarc`, not the Mac `*_m.psarc`. Custom songs
  need the `_p.psarc` (PC) version; a Mac-only collection does not load. Official DLC has both in each DLC's
  Steam depot, and only the `_p` one goes into the copy.
- Two crashes have been seen once each and not reproduced yet.
- A shader the install's list doesn't have is compiled the first time it is used: a short stall, once. That
  goes for the Guitarcade's games, and may for some venues.
- After a macOS update every shader is compiled again as the game uses it; run
  `tools/play-native/warm-shaders.sh "$HOME/Applications/RS Re-ARMed Learn & Play.app"` or click Build & Install
  again to have it done first.
- Metal only: there is no OpenGL choice or fallback, and none is planned (`RS_GL_BACKEND=gl` draws offscreen,
  for the tests).
- Only the one pinned build of the Windows game is supported.

## Troubleshooting

The log is `~/Library/Application Support/RS Re-ARMed/learn-play/logs/last-run.log` (the run before:
`previous-run.log`). A line `lp: online: offline RSConnect layer` means the offline layer is in place. If a
Ubisoft login screen still shows, press Esc (Cancel); never type an email or password there. For more in the
log, start with `open -n --env LP_LOG=1 …` ([TESTING.md](TESTING.md#diagnostics)).

The app's alerts (no copy of the game files or an incomplete one, the drive with them not connected, macOS not
letting it read the files) say what to do themselves. The others:

| Message | What to do |
|---|---|
| `the Xcode command line tools are missing` | In a terminal: `xcode-select --install`, then run it again. The installer starts this by itself. |
| The installer: "Apple's command line tools are there but don't run" | You have Xcode and its license isn't accepted yet: open Xcode once and accept it. |
| `a different build of the game` | Steam served another build than the one RS Re-ARMed is made for; the message names both. |
| "The startup disk is short of space" | Free the space it names: macOS needs it, wherever the build folder is. A folder on another disk doesn't help with that. |
| `not enough free space for the download` | Choose (or set `LP_DOWNLOAD_DIR` to) a folder on a disk with room. |
| `Steam's download folder is …, on a drive that isn't connected` | Connect the drive, or choose another folder. |
| `could not install the Python packages` | pip needs the network once. |
| `no seed.bin` | This copy has no offline unpacker: use the released installer, or make a capture. |
| `no harvest folder at …` | Unzip `rs-harvest.zip` and pass the folder, not the zip. |
| `not enough free space for the build` | Free some space, or set `RS_BUILD_DIR` to a folder on another disk. |
| `not enough free space for the game files` | The game is on a volume that can't clone (not APFS), so the copy is a real one: free the space it names on the disk of your home folder and run it again (what is copied is kept), or set `RS_GAME_COPY_DIR` to a folder on a drive with room. |
| a path with spaces refused | Use a build folder and a clone whose paths have no spaces. |
| The game starts in the wrong mode or size | `RS_FULLSCREEN=0`, or fix `[Renderer.Win32]` in `userdata/game/Rocksmith.ini`. |
| No guitar sound | Check the INPUT setting in the game (Real Tone cable, microphone, DIRECT CONNECT) and that macOS allowed microphone access. |
| Rocksmith 1 DLC songs you own are missing, or the refresh says "Rocksmith 1 DLC: not checked" | Only Steam knows which of them you own, and a refresh can ask it only while the account RS Re-ARMed plays as is logged in: log in to that account in Steam, then **Refresh Game Files** (or `learn-play.sh --refresh`). The answer is kept, so Steam can be closed afterwards. |
| Songs or profiles missing after switching Steam accounts | **Refresh Game Files** (or `learn-play.sh --refresh`): it records the account whose license has Learn & Play in `game/.rearmed/steam.env`, and the app uses it whoever is logged in to Steam. Set `RS_STEAM_ID` to choose another ([Game files](#game-files)). |
| The game crashes | Open an issue with `last-run.log` and `previous-run.log`. |
