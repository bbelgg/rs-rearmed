# Installing the Windows port (Learn & Play)

Rocksmith 2014 **Learn & Play** is a purchased edition that Steam offers for Windows only: its license has no
Mac download, so the [Mac edition](../mac/INSTALL.md) can't use it. RS Re-ARMed builds that Windows game into a
native arm64 Mac app, on your Mac, from your own copy. Nothing it produces may be shared. Songs, scoring, the
Guitarcade, Session Mode, Nonstop Play, lessons and the window modes work; what hasn't been tested is under
[Known issues](#known-issues).

## Requirements

- A Mac with Apple Silicon (M1 or later), macOS 12 Monterey or later.
- Rocksmith 2014 **Learn & Play** on your Steam account. **Steam** for Mac, signed in with that account, downloads
  the Windows game for you (about 5.5 GB, plus about 16 MB for each DLC; all of it must be free up front, on a
  disk you choose or on the startup disk).
- A Real Tone cable, or another audio input the game accepts. macOS asks for microphone access the first
  time the game listens for your guitar ([What macOS will ask](#what-macos-will-ask)).
- Apple's command line tools, with their `python3`. The installer starts their installation when they are
  missing, and you click Install in Apple's window. Nothing else: no Windows PC (with the released installer:
  [Without the seed](#without-the-seed-the-one-time-capture)), no Rosetta, no Homebrew, no CMake, no steamcmd,
  no .NET.
- An internet connection while building: Steam's download, the source of the release, the build's Python
  packages (Capstone, Unicorn and pefile, with pip) and, only if Steam's copy is missing, Microsoft's
  redistributables.
- About 25 minutes for the first build on an M2 (8 cores), about 2 GB free in the build folder and 8 GB of
  memory ([what it needs](PERFORMANCE.md#the-build-memory-and-time)).
- The installer keeps 2 GB of the startup disk free for macOS, and counts that in before it starts.
- Room for the app's own copy of the game files ([Game files](#game-files)): none extra when Steam's
  download is on an APFS volume (the startup disk, or an external drive it was redirected to: the copy then
  goes onto that drive), else about 6 GB plus the DLC on the startup disk.

### What macOS will ask

The windows that come from macOS, and what to answer, are in the
[Mac edition's table](../mac/INSTALL.md#what-macos-will-ask). One differs: at the game's first start with its
files on another disk, macOS asks once, for the game, as this edition has no launcher.

### Without the seed: the one-time capture

The Windows game's code is encrypted on disk and only decrypted in memory while it runs. The released installer
carries a small seed (`seed.bin`, no game code) with which it unpacks your own exe on the Mac, so **nothing here
applies to it**. A build from a clone of this repository has no seed: it needs one you supply (`LP_SEED`), or a
capture of your own game's code, made once on a Windows PC in about 5 minutes. The install prepares the folder to
take there and says what to do; the steps are in [CAPTURE.md](CAPTURE.md#path-a-harvest-the-image). The capture
stays with you, and the image built from it is kept for later runs.

## Install with the installer

Download and open **RS Re-ARMed Installer** as for the
[Mac edition](../mac/INSTALL.md#install-with-the-installer): one installer builds both games, and macOS has to be
told to open it the first time.

Choose **Game: Learn & Play** (preselected when the Mac game is not installed) and click **Build & Install**. That
is all, if Steam is on this Mac and signed in with the account that owns Learn & Play. It does the same steps as
`tools/play-native/learn-play.sh`, from the source of its own release; the box above the log and **Stop** work as
in the Mac edition.

### The steps

1. **Checking Apple's tools**, then **Downloading source** and **Checking prerequisites**, as in the
   [Mac edition](../mac/INSTALL.md#the-steps).
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
   downloaded moves along. The [Mac edition](../mac/INSTALL.md#the-steps) gets its game the same way when it
   isn't installed in Steam, into the same download folder: a folder chosen for one edition is the other's too.
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
- **Refresh Game Files** (shown once the game is installed) has Steam fetch DLC you bought since, and syncs the
  app's copy of the game files with Steam's folders again ([Game files](#game-files)).
- **Game files**, **Settings and switches…**, **Build folder**, quitting, the log and a newer release are as in the
  [Mac edition](../mac/INSTALL.md#the-steps); for **Game files**, the game's drive is the one Steam's download
  is on. **Renderer** has no effect here ([Renderer](#renderer)).

A run ends as in the Mac edition: the box above the log says how, and the log's last lines say where everything
is, with the folder for [custom songs](#how-the-game-finds-dlc). Later, start **RS Re-ARMed Learn & Play** from
`~/Applications` (double-click, Launchpad or Spotlight).

## Install from a clone

```bash
tools/play-native/learn-play.sh [CAPTURE]      # or: tools/play-native/install.sh --learn-play [CAPTURE]
```

The same steps as the installer, in a terminal. With a seed (`LP_SEED`, or `win-x86/unpack/seed.bin`; git has none)
it needs nothing else. `CAPTURE` is an unzipped capture folder (or `LP_HARVEST`) to use instead; with neither,
the first run prepares the capture folder, opens it and stops, and you run it again with the folder. Re-run it to
update. `tools/play-native/learn-play.sh --refresh` only refreshes the game files.

An earlier run's code image is used again when it is the one this version is made for (`image_sha256` in
`tools/targets/win-x86/target.toml`; `lp_install.py image-check EXE` says whether it is). Otherwise the seed makes
it again; without a seed, the image of an earlier capture is used as it is.

| Variable | Default | Meaning |
|---|---|---|
| `RS_BUILD_DIR` | `build` in the repository | the build folder (no spaces in its path): the build goes to its `win-x86/lp`, the DLLs to `win-x86/dlls`, the image to `win-x86/image` |
| `LP_GAME_DIR` | Steam's download folder | the Windows game folder to copy from (for example a copy from a Windows PC) |
| `LP_HOME` | `~/Library/Application Support/RS Re-ARMed/learn-play` | the app's folder; the copy goes to its `game/` (or, for a game on another APFS drive, that is a link to the copy there) |
| `RS_GAME_COPY_DIR` | `LP_HOME`, or the game's drive when that is another APFS drive | where a new copy goes: `DIR/learn-play/game`, with `game/` a link to it; `home`: in `LP_HOME` whatever the drive, `drive`: on the game's drive, and these two also move a copy that is in the other place ([Game files](#game-files)) |
| `LP_DOWNLOAD_DIR` | Steam's own folder | a folder on another disk for Steam's download, when the startup disk is short of space |
| `LP_SEED` | `win-x86/unpack/seed.bin` | the unpacker's seed |
| `LP_HARVEST`, `LP_HARVEST_V1` | none | the capture folder, when it is not given as `CAPTURE`; a harvester v1 capture of the same build, as its reference |
| `RS_STEAM_DIR` | `~/Library/Application Support/Steam` | the Steam folder |
| `LP_NATIVE_APP` | `~/Applications/RS Re-ARMed Learn & Play.app` | where to install the app |
| `LP_KEEP_BUILD` | the translated code is kept | `1`: keep all build files; `0`: delete them all, the cache too |
| `LP_JOBS` | what the cores and the memory allow | parallel jobs |
| `RS_WARM_SHADERS` | on | `0`: no "Preparing shaders" at the end; run `tools/play-native/warm-shaders.sh "<the app>"` later to do it then |
| `RS_SHADER_CORPUS` | on | `0`: no shader corpus |
| `RS_MSL_CORPUS_FILE` | none | bundle this `rsmsl-corpus.bin` instead of making one |
| `RC_PY` | `venv/bin/python` in `RS_BUILD_DIR` | a Python with the packages of `win-x86/install/requirements.txt` |

The steps one at a time: `python3 win-x86/install/lp_install.py --help`.

## Game files

The app reads only its own copy, `~/Library/Application Support/RS Re-ARMed/learn-play/game/`, as Steam may
clear its download folder. The copy (`lp_install.py game-copy`, with `tools/play-native/gamefiles.py`) has the
Windows game folder and, from its `dlc/` and the Mac game's `dlc` folder in each Steam library, the PC song
archives, `*_p.psarc`. It also records your DLC, the language and the account the game plays as in
`game/.rearmed/steam.env`: the account whose license has Learn & Play (depot 221683), whichever is logged in
later. To choose it yourself, set `RS_STEAM_ID` (a SteamID64 or the number in `Steam/userdata/<number>`) for a
refresh.

Where each of those comes from, for this game and for a Mac game from Steam's download, which is copied the same
way: [the Mac edition's table](../mac/INSTALL.md#game-files). The language is English unless the Mac game's
appmanifest is in Steam's library or `RS_STEAM_LANGUAGE` is set.

The **Direct Connect overlay** is made as in the Mac edition, from the copy's `cache.psarc`. The copy's tools read
the game's archives the same way too, but Learn & Play's exe shows what they need only once unpacked: the first
copy leaves your DLC's list and the overlay to the build step, which keeps a note of it (`game/.rearmed/toc-key`),
and a refresh after that needs neither the image nor the game folder.

- **Disk use.** On the same APFS volume the copy is clones: seconds, and no extra space, though `du` and the
  Finder count the full size. Once it's made, Steam's download may be deleted, though that frees nothing if the
  copy is clones of it.
- **Steam's download on another drive.** As for a Steam library on another drive in the
  [Mac edition](../mac/INSTALL.md#game-files), the installer's **Game files** choice and `RS_GAME_COPY_DIR`
  included. Here the copy on the drive is `RS Re-ARMed/learn-play/game` at its top, the link to it is
  `…/learn-play/game`, a real copy takes about 6 GB plus DLC, and `RS_GAME_COPY_DIR=DIR` puts a new copy in
  `DIR/learn-play/game`.
- **Refresh** after new DLC: **Refresh Game Files** in the installer, or
  `tools/play-native/learn-play.sh --refresh`. It has Steam fetch the DLC bought since, copies new and changed
  files, removes what the sources no longer have, and touches nothing else (`RS_DLC_FETCH=0` skips the fetch,
  and one that fails does not stop the refresh). With Steam's download gone the copy keeps the game's files and
  still takes new DLC, and a copy on Steam's drive can still be moved home; while Steam is still downloading
  the game nothing is copied.
- **Stopped copies** continue on the next run. `game/.rearmed/manifest.json`, written last, marks a copy
  complete; until then the app asks for a refresh and quits.

### How the game finds DLC

The game looks in one place only, the `dlc` folder of its own copy (`…/RS Re-ARMed/learn-play/game/dlc`), when
it starts, as in the [Mac edition](../mac/INSTALL.md#how-the-game-finds-dlc). The same five points, with what
differs here:

1. **What gets into `game/dlc`.** The Windows game is not installed by Steam here, so nothing installs its DLC:
   the installer has Steam download each DLC your account owns as a download of its own
   ([the steps](#the-steps)). A copy or a refresh then takes every `*_p.psarc`, in subfolders too, from those
   downloads, from the Windows game folder's `dlc`, and from the Mac game's `dlc` folder in each Steam library
   (Steam puts the PC archives there as well), and nothing else: the Mac archives, `*_m.psarc`, stay behind. A
   file the sources no longer have is removed from the copy at the next refresh.
2. **Which archives the game shows.** As in the Mac edition, from `game/.rearmed/steam.env`: the DLC your
   account's license lists, and the AppID of every archive in `game/dlc`.
3. **Custom songs** need one more thing, the built-in CDLC signature skip, which is on unless you switched it
   off in Settings and switches…. Put them straight into `game/dlc`, as the PC version, `_p.psarc`: every install ends
   by naming that folder (it is there even with no DLC), the game has them at its next start, whatever AppID
   they carry, and no refresh removes a song you put there. Songs in the Mac game's `dlc` folder in a Steam
   library work too: a copy or a refresh brings them over.
4. **Songs of the first Rocksmith.** As in the Mac edition.
5. **New songs.** While you play, **ENUMERATE** in the main menu takes new archives from the folders the copy is
   made from (and finds ones put straight into `game/dlc`) with no restart. DLC bought since, and changed or
   removed songs, need a refresh, which has Steam fetch the new DLC first.

The design, with the game's own calls, is in [DESIGN.md](DESIGN.md#steam).

### The title logo

As in the [Mac edition](../mac/INSTALL.md#the-title-logo), except that the build makes the logo, not the install:
a refresh that brings a new `static.psarc` removes it, and the next build makes it again.

## Where things are

In the table, `…` is `~/Library/Application Support/RS Re-ARMed`.

| Where | What |
|---|---|
| `~/Applications/RS Re-ARMed Learn & Play.app` | the game: `rs2014_lp`, libRSOpenGL, the network guard, the game image and the DLL images; ad-hoc signed |
| `~/Library/Application Support/RS Re-ARMed/learn-play/game/` | the game folder the app reads: its own copy of the game files ([Game files](#game-files)); for a game on another APFS drive a link to `<drive>/RS Re-ARMed/learn-play/game`, where the copy is |
| `…/learn-play/game/dlc/` | the songs the game has: your DLC, and the place for custom songs ([How the game finds DLC](#how-the-game-finds-dlc)) |
| `…/learn-play/game/.rearmed/` | the copy's manifest (written last), where it comes from (`source.json`) and the Steam facts (`steam.env`: your DLC, the language, the account) |
| `…/learn-play/game/static.psarc` | a link the app sets at each start: to `static.psarc.original`, the game's own, or `static.psarc.rearmed`, the [title logo](#the-title-logo) |
| `…/learn-play/game-dir` | the folder the game reads (one line) |
| `…/learn-play/gamefiles.py` | the tool that refreshes the copy |
| `…/learn-play/userdata/` | the game's writes: `game/Rocksmith.ini` (audio and display settings) |
| `…/learn-play/steam/<account>/221680/remote/` | your profiles and progress |
| `…/learn-play/logs/` | `last-run.log`, `previous-run.log`, `netguard.log` (with mods) |
| `…/learn-play/mods/` | mods loaded at startup |
| `…/learn-play/settings.env` | if you switched any off: the built-in mods switched off in the installer ([Settings and switches](#settings-and-switches)) |
| `…/learn-play/options.env` | if you set any: every other switch set in the installer ([Settings and switches](#settings-and-switches)) |
| `…/guitar-input.ini` | if you made one: the guitar input's channel and gain, for both editions ([Guitar input](#guitar-input)) |
| `…/learn-play/input-level-calibrated` | made by the game's first calibration: the level saved in your profile counts from then on; delete it to start over ([Guitar input](#guitar-input)) |

On the first start your Steam saves for the game, if Steam has any on this Mac, are copied once into `steam/`,
without `crd` (stored Ubisoft credentials). RS Re-ARMed never writes your Steam saves.

## Playing

Start **RS Re-ARMed Learn & Play** from `~/Applications`. It runs offline: no Steam client, no Ubisoft login,
no network. Enter on the title screen goes straight to the profile list. Custom songs (CDLC) load as they are,
in their PC version, `*_p.psarc` ([Known issues](#known-issues)): put them in `game/dlc`
([How the game finds DLC](#how-the-game-finds-dlc)). To add a song while you play, put it there (or in the Mac
game's `dlc` folder) and press **ENUMERATE** in the main menu: it is in the list a moment later, with no restart.
Changed or removed songs and new DLC need a [refresh](#game-files).

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
shape. The green button, Control-Command-F or Shift-Command-F gives macOS full screen, at the screen's size.
Either key in the game's own full screen gives a window of the screen's shape instead, which scales the picture
when resized: the game stays in full-screen mode. Command-Q, the close button and the game's own Exit all quit
through the game's shutdown. The display mode never changes.

The game follows the shape itself, with no bars, as in the [Mac edition](../mac/INSTALL.md#window-modes). On
Windows this game keeps a 16:9 picture with bars; `RS_WINDOW_ANY_SHAPE=0` does that here too, and a resized
window then scales the picture with its shape kept.

### Renderer

Metal only, so there is no choice and no fallback ([Known issues](#known-issues)).

### Audio latency

The two steps of the [Mac edition](../mac/INSTALL.md#audio-latency) hold here. The setting is
`[Audio] LatencyBuffer` in `userdata/game/Rocksmith.ini`, and only the game sets it: there is no
`RS_LATENCY_BUFFER`. The output device's buffer is `RS_AUDIO_FRAMES` (default 256; 128 on an interface that also
has inputs). The delay has not been measured against the Mac edition.

### Guitar input

The same as in the Mac edition, from the same code and the same `guitar-input.ini`:
[Guitar input](../mac/INSTALL.md#guitar-input). The list with each channel of an interface, the calibration that
can boost, the per-device `Channel` and `GainDb` and the `RS_AUDIO_INPUT…` switches all hold here. Only a device
named "Rocksmith USB Guitar Adapter" counts as the Real Tone cable.

### Two players

As in the Mac edition: [Two players](../mac/INSTALL.md#two-players). SWAP CABLE ASSIGNMENT on interfaces hasn't
been played in this edition yet.

## Settings and switches

Every `RS_*` switch below is set in the installer (**Settings and switches…**), except `RS_MODS_PATH` (only
with `launchctl setenv` or `open --env`) and the other diagnostics switches in [TESTING.md](TESTING.md#diagnostics).
The installer writes the built-in mods to `settings.env` and every other switch to `options.env` in
`~/Library/Application Support/RS Re-ARMed/learn-play/`, which the app reads at every start; without the files
everything is the game's own setting. By hand they are lines of `KEY=0` or `KEY=1` for the built-in mods and
the other on/off switches, and `KEY=value` for the rest: other lines are ignored (a switch only the Mac edition
has is passed on and does nothing), and the top of `logs/last-run.log` says what was used. The Mac edition has
its own files, read the same way.

```
RS_MODS_INTRO=0
RS_REARMED=0
```

```
RS_METAL_MSAA=2
RS_AUDIO_FRAMES=128
```

Every switch is an environment variable, and the app reads its own: there is no launcher between. Start the app
with `open --env`, or set the switch for the login session with `launchctl setenv`, as for the
[Mac edition](../mac/INSTALL.md#settings-and-switches), which then sees it too. Either wins over both files:

```bash
open -n --env RS_FULLSCREEN=0 "$HOME/Applications/RS Re-ARMed Learn & Play.app"
```

The installer's Settings and switches… names every switch set with `launchctl setenv`, since it wins over the
page, and its Clear overrides… unsets them after asking.

Each row is a switch set away from its default, and what that does. Everything is compiled into the app.

| Switch | Default | What |
|---|---|---|
| `RS_MODS_BYPASS=0` | on | the CDLC signature skip, a rewrite of [RSBypass](https://github.com/aik002/RSBypass), off: the song-file signature check and the list of known song keys work as in the original, custom songs are not listed, and of the first Rocksmith's DLC songs only those your account owns ([How the game finds DLC](#how-the-game-finds-dlc)) |
| `RS_MODS_FIXES=0` | on | the [rs-mac-fixes](https://github.com/glebb/rs-mac-fixes) behaviours this game has, off: faster loading with large libraries (the batched DLC mounting, the song path cache and the number-table hash) and a higher open-file limit |
| `RS_MODS_BUDGET_MS=N` | 50 | the time per frame for mounting DLC archives, in ms; 0 mounts one per frame and asks every file question of the whole file system, as the game does |
| `RS_MODS_INTRO=0` | on | the intro screens play |
| `RS_MODS_AUTOSAVE=0` | on | the Auto Save warning shows after profile select |
| `RS_MODS_OVERLAY=0` | on | the game reads `cache.psarc` alone, without the overlay: no DIRECT CONNECT, SHOP instead of ENUMERATE and UPLAY instead of EXIT GAME ([Game files](#game-files)) |
| `RS_MODS_MULTIPLAYER=0` | on | [two players](#two-players): Player 2 on a Real Tone cable only |
| `RS_REARMED=0` | on | the game's own title logo ([The title logo](#the-title-logo)); set for the build, no logo is made |
| `RS_MODS_FRAME_RATE=0` | on | calibration and the tuner count in frames, as in the original, which goes wrong above 60 Hz |
| `RS_MODS_GUITARCADE_SKIP=0` | on | a Guitarcade game's tuner has no Skip Tuner, as in the original |
| `RS_MODS_LOG=1` | off | log what the built-in fixes apply to `logs/last-run.log` |
| `RS_STEAM_DLC=all`, `none`, or a list of AppIDs | your DLC, and the AppID of every archive in the copy's `dlc/` | the songs the game counts as owned ([How the game finds DLC](#how-the-game-finds-dlc)) |
| `RS_STEAM_ID=N` | the account whose license has the game | the Steam account the game plays as ([Game files](#game-files)) |
| `RS_STEAM_LANGUAGE=NAME` | the Mac game's in Steam, else English | the game's language, as Steam names it (`german`; [Game files](#game-files)) |
| `RS_STEAM_OWNED=LIST` | what a refresh recorded | the AppIDs outside this game's DLC that count as owned with `RS_MODS_BYPASS=0`: the first Rocksmith's DLC, for its songs ([How the game finds DLC](#how-the-game-finds-dlc)) |
| `RS_STEAM_PERSONA=NAME` | the name of the account Steam last logged in, else `Player` | the player name the game gets from Steam |
| `RS_METAL_UPSCALE=0` | on | no MetalFX upscaling on a Retina screen: a plain bilinear scale, as on macOS 12, which has no MetalFX |
| `RS_METAL_RETINA=1` | off | render every pixel of a Retina screen, about 4x the GPU work (otherwise the game renders in points and each frame is scaled up to the screen's pixels) |
| `RS_METAL_COLORS=vivid` | sRGB | leave the picture untagged, so a wide-colour (P3) screen, such as a Mac's own, shows it more saturated than the game made it |
| `RS_METAL_ANISO=N` | 16 | the least anisotropic filtering of mipmapped textures, 1-16; 1 keeps the game's own (1x to 4x) |
| `RS_METAL_MSAA=N` | 4 | MSAA on the game's 3D picture (its screen-sized targets with depth): 1 (off), 2, 4 or 8 |
| `RS_WINDOW_ANY_SHAPE=0` | on | 16:9 with bars, as on Windows, and a resized window keeps the game's resolution ([Window modes](#window-modes)) |
| `RS_FULLSCREEN=1\|0` | the game's setting | force full screen, or a window |
| `RS_D3D9_DISPLAY=WxH` | the screen's size | the desktop size the game sees |
| `RS_FPS=N` | the display's rate | a fixed frame rate, a whole number from 1 to 1000, with vsync off |
| `RS_AUDIO_FRAMES=N` | 256 | the output device's buffer, in frames; 0 leaves the device's ([Audio latency](#audio-latency)) |
| `RS_AUDIO_RT=0` | on | the audio thread is not a real-time thread |
| `RS_AUDIO_INPUT_TRIM=0` | on | stale guitar audio is not dropped |
| `RS_AUDIO_SET_RATE=0` | on | a device is not switched to 48 kHz |
| `RS_AUDIO_RS_NAMES=0` | on | keep CoreAudio's device names (by default only the cable is called "Rocksmith") |
| `RS_AUDIO_INPUT…`, `RS_AUDIO_CHANNEL_ENTRIES=N` | | the guitar input's channel, gain, level and list: [Guitar input](../mac/INSTALL.md#guitar-input) |
| `RS_AUDIO_LOG=1` | off | log the audio setup to `logs/last-run.log` |
| `RS_STEAM_LOG=1`, `RS_FILE_LOG=1` | off | log the offline Steam layer's calls, and the files a mod serves, to `logs/last-run.log` |
| `RS_LOAD_MODS=0`, `RS_MODS_PATH=PATH` | every `.dylib` in `mods/` | no mods; another folder of mods, or one library (`launchctl setenv` or `open --env` only) |
| `LP_HOME=DIR` | `~/Library/Application Support/RS Re-ARMed/learn-play` | another folder for settings, saves and logs; the installer's Settings and switches… writes to the default folder only |
| `LP_GAME_DIR=DIR` | the folder in `game-dir` | another game folder (any folder with the game; the copy check is skipped) |

Without a switch: the offline Steam layer and the Uplay sign-in prompt skipped, as in the Mac edition, and the
game's own network calls refused. Diagnostics switches: [TESTING.md](TESTING.md#diagnostics).

Mods: every `.dylib` in `~/Library/Application Support/RS Re-ARMed/learn-play/mods/` is loaded at startup and
survives a reinstall. A mod has to be built with `rc_mod.h` ([how](../mac/INSTALL.md#settings-and-switches)); the
same dylib then runs in the Mac edition, and one built without it is left out, with a line in the log. Mods load
only with the network guard in the process, which logs to `logs/netguard.log`. How mods hook the game:
[DESIGN.md](DESIGN.md#built-in-fixes-and-mods); what a mod can't do here yet: [Known issues](#known-issues).

## Update and uninstall

- **Update**: open the installer. It offers a newer release at its start, and **Update Now** replaces the
  installer with that release's, as in the [Mac edition](../mac/INSTALL.md#update-and-uninstall) (one installer
  serves both games). Then choose Learn & Play and click **Build & Install**: the game stays as it is until you
  do. By hand, pull and run `learn-play.sh` again. The game image and the translated code are reused, and only
  what changed is rebuilt. Your saves are kept; the game files are refreshed.
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

- As in the [Mac edition](../mac/INSTALL.md#known-issues): no online features, progress is not copied back to
  your Steam saves, the game lists 10 inputs at most, and a calibration stopped partway still counts as run.
- Only the one pinned build of the Windows game is supported.
- It has had far less testing than the Mac edition. Not tested yet: the Workshop beyond the tone designer, the
  DLC preview ([PLAN.md](PLAN.md#status)).
- Two crashes have been seen once each and not reproduced yet.
- Song archives: the Windows game reads the PC files, `*_p.psarc`, not the Mac `*_m.psarc`. Custom songs
  need the `_p.psarc` (PC) version; a Mac-only collection does not load. Official DLC has both in each DLC's
  Steam depot, and only the `_p` one goes into the copy.
- A shader the install's list doesn't have is compiled the first time it is used: a short stall, once. That
  goes for the Guitarcade's games, and may for some venues.
- After a macOS update every shader is compiled again as the game uses it; run
  `tools/play-native/warm-shaders.sh "$HOME/Applications/RS Re-ARMed Learn & Play.app"` or click Build & Install
  again to have it done first.
- Metal only: there is no OpenGL choice or fallback, and none is planned (`RS_GL_BACKEND=gl` draws offscreen,
  for the tests).
- Mods: 19 of the 21 hook points, the overlay API and `OV_VCALL` are there. A mod can't yet: use what else the
  Mac app exports beyond `rc_mod.h`, such as `ov_fn` (the mod then fails to load, with the reason in the log);
  change an event's arguments in `input_sink` (it can keep the event from the game); pass floating point
  arguments to `rc_call_guest` (`rc_call_name_f` can); call a function that takes its own registers by name; give
  the game a pointer to the mod's own memory (the game gets null, with a line in the log; `rc_guest_alloc` gives
  memory it can have); or quit by `game_frame`'s result (not taken, with a line in the log; a pre hook still
  skips the tick). A field this game doesn't have gives `RC_NO_OFF`: test `RC_HAS_OFF`.

## Troubleshooting

Logs: `~/Library/Application Support/RS Re-ARMed/learn-play/logs/` (`last-run.log`, `previous-run.log`, and
`netguard.log` with mods); the installer's in `~/Library/Logs/RS Re-ARMed Installer/`. `RS_MODS_LOG=1` and
`RS_AUDIO_LOG=1` add what the built-in fixes and the audio setup do, and `open -n --env LP_LOG=1 …` the game's
Windows calls ([TESTING.md](TESTING.md#diagnostics)). A line `lp: online: offline RSConnect layer` means the
offline layer is in place. If a Ubisoft login screen still shows, press Esc (Cancel); never type an email or
password there.

The app's alerts (no copy of the game files or an incomplete one, the drive with them not connected, macOS not
letting it read the files) say what to do themselves. The others:

| Message | What to do |
|---|---|
| `the Xcode command line tools are missing` | In a terminal: `xcode-select --install`, then run it again. The installer starts this by itself. |
| `a different build of the game` | Steam served another build than the one RS Re-ARMed is made for; the message names both. |
| "The startup disk is short of space" | Free the space it names: macOS needs it, wherever the build folder is. A folder on another disk doesn't help with that. |
| `not enough free space for the download` | Choose (or set `LP_DOWNLOAD_DIR` to) a folder on a disk with room. |
| `Steam's download folder is …, on a drive that isn't connected` | Connect the drive, or choose another folder. |
| `could not install the Python packages` | pip needs the network once. |
| `no seed.bin` | This copy has no offline unpacker: use the released installer, or make a capture. |
| `no harvest folder at …` | Unzip `rs-harvest.zip` and pass the folder, not the zip. |
| `not enough free space for the build` | Free some space, or set `RS_BUILD_DIR` to a folder on another disk. |
| a path with spaces refused | Use a build folder and a clone whose paths have no spaces. |
| The game starts in the wrong mode or size | `RS_FULLSCREEN=0`, or fix `[Renderer.Win32]` in `userdata/game/Rocksmith.ini`. |

The rest is the same as in the [Mac edition's table](../mac/INSTALL.md#troubleshooting): Apple's command line
tools that don't run, `not enough free space for the game files`, Rocksmith 1 DLC songs that are missing, songs or
profiles missing after switching Steam accounts, no guitar sound, and a crash. Where it says
`install.sh --refresh-game`, use `learn-play.sh --refresh`; the account a refresh records is the one whose license
has Learn & Play.
