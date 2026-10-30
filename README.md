# RS Re-ARMed

![RS Re-ARMed title screen](docs/images/title-screen.png)

RS Re-ARMed is a native arm64 port of the macOS version of Rocksmith 2014 for Apple Silicon Macs. The
game is statically recompiled to C from your own copy at build time, and small native libraries take
the place of the parts that tie it to Intel Macs, Steam and online services.

Not affiliated with or endorsed by Ubisoft. Rocksmith is a trademark of Ubisoft.

This project does not include any game code or assets; you need your own copy of the game.

## Table of Contents

- [System Requirements](#system-requirements)
- [Features](#features)
- [Built-in patches and mods](#built-in-patches-and-mods)
- [How to Install](#how-to-install)
- [Known Issues](#known-issues)
- [Under Investigation](#under-investigation)
- [FAQ](#faq)
- [Building](#building)
- [Credits](#credits)

## System Requirements

- A Mac with Apple Silicon (M1 or later).
- macOS 11 Big Sur or later.
- The Steam macOS version of **Rocksmith 2014 (Remastered)**, installed through Steam.
- A Real Tone cable, or another audio input the game accepts. macOS asks for microphone access the first
  time the game listens for your guitar.

## Features

- **Native arm64.** The game runs as an arm64 app. No Rosetta, no x86 code at run time.
- **As light as Rosetta.** In gameplay it uses about the same CPU as the original game under Rosetta,
  a little less on average (about half a core at 60 fps, measured over a scripted song). It follows the
  display's refresh rate, so 75 Hz and 120 Hz displays get 75 and 120 fps.
- **Offline by design.** The Steam client is not needed while you play: an offline Steam layer stands in
  for it. A network guard (`tools/ws8/netguard`) is loaded into the game on every launch and refuses every
  connection that is not local, every host name lookup and every attempt to open a web page, and logs
  each one. The launcher will not start the game without it.
- **Your profiles, kept separately.** On the first launch your Steam saves for Rocksmith 2014 are copied
  once into the RS Re-ARMed folder. From then on the native build uses only that copy; your Steam saves
  are never written. Stored Ubisoft credentials (`crd`) are never copied.
- **Metal renderer.** The game draws through Apple's Metal by default; OpenGL stays available as a choice
  and as an automatic fallback (see [Renderer](#renderer-metal-or-opengl)).
- **Your DLC, linked automatically.** The launcher mirrors your game folder with symlinks on every launch,
  so new DLC folders in your install show up without reinstalling.

## Built-in patches and mods

These are compiled into the app. Where a switch exists, it is an environment variable.

| What | Default | Switch |
|---|---|---|
| **rs-mac-fixes** behaviours ([rs-mac-fixes](https://github.com/glebb/rs-mac-fixes) by glebb): more keyboard keys in the game (space, Return, Esc, Tab, Backspace, arrows, F1-F12, Home/End/Page Up/Page Down, forward delete, punctuation), the End key in menus, faster loading with large song libraries (a cache for song paths and several DLC archives mounted per frame), a better hash for the game's number table, higher priority for the game's worker threads (fewer hitches), and a higher open-file limit | on | `RS_MODS_FIXES=0` turns all of them off. `RS_MODS_BUDGET_MS=N` sets the time per frame for mounting DLC archives (default 50; 0 mounts one per frame, as the original game does) |
| **RSBypass** custom-song (CDLC) signature skip (an independent rewrite of [RSBypass](https://github.com/aik002/RSBypass) by aik002): song files that fail the signature check still load. Only the song loader's check is skipped | on | `RS_MODS_BYPASS=0` enforces the signature check again |
| **Uplay sign-in prompt skipped**: Enter on the title screen goes straight to the profile list, without the "Uplay account" dialogs | on | none |
| **Offline Steam layer**: a replacement `libsteam_api` that works without the Steam client, with its cloud saves in a local folder | on | none |
| **Song ownership**: the offline Steam layer reports the base game, the DLC your account owns (the DLC Steam installed) and 248750, the free DLC that custom songs carry, as owned, so their songs show up | on | `RS_STEAM_DLC=all` reports every AppID as owned, for custom songs that carry another AppID |
| **"Re-ARMed" title logo**: a tag over the title-screen logo, built automatically from your own `static.psarc` by `tools/loadscreen` | on | `RS_REARMED=0` shows the original logo; or delete `static.psarc.rearmed` |
| **Frame rate**: the game renders at the display's refresh rate (a 75 Hz or 120 Hz display gets 75 or 120 fps; the game keeps song time at any rate). A fixed rate can be set instead, with vsync off: higher to test, or lower (e.g. 30) to save power | display | `RS_FPS=N` |
| **Lower guitar latency**: the output device runs with a 256-frame buffer instead of the system's 512 (at most half the game's own buffer, `LatencyBuffer` x 256 frames, so 128 at `LatencyBuffer=1`; see the FAQ); the game's audio thread runs as a real-time thread in the output device's audio workgroup; stale guitar audio queued from the cable is dropped instead of played late | on | `RS_AUDIO_FRAMES=N` sets the output buffer (e.g. 128; 0 leaves the system's), `RS_LATENCY_BUFFER=N` the game's (1-16). `RS_AUDIO_RT=0` and `RS_AUDIO_INPUT_TRIM=0` turn off the other two. `RS_AUDIO_LOG=1` logs the audio setup |
| **All guitar interfaces listed**: Direct Connect offers input devices first, so an interface still shows up on a Mac with more than 10 audio devices (the game only looks at the first 10, outputs included) | on | `RS_AUDIO_INPUTS_FIRST=0` turns it off |
| **Renderer**: Metal, or Apple's OpenGL (see [Renderer](#renderer-metal-or-opengl)) | Metal | the `renderer` file, the installer, or `RS_GL_BACKEND=gl` |
| **Retina** (Metal only): the game renders at the screen's full resolution on a Retina screen | off | `RS_METAL_RETINA=1` |
| **Multithreaded GL engine** (OpenGL only): Apple's driver does its work on a thread of its own. Measured: the frame thread 4% shorter, total CPU 2% higher | off | `RS_GL_MTENGINE=1` |

`RS_MODS_LOG=1` writes what the mods apply to `logs/last-run.log`.

### Switching options

The launcher starts the game through macOS, so variables set in a Terminal window do not reach it. Set
them for your login session with `launchctl` before starting the game, for example:

```bash
launchctl setenv RS_MODS_BYPASS 0     # takes effect on the next launch
launchctl unsetenv RS_MODS_BYPASS     # back to the default
```

### Renderer: Metal or OpenGL

The game draws through **Metal** by default. **OpenGL** (Apple's, which macOS still ships) is the
alternative; both look the same. Metal uses a little less CPU per frame and does not depend on
OpenGL, which Apple deprecated.

- **Choose** in the installer (Renderer: Metal / OpenGL), or with `RS_RENDERER=opengl` when running
  `tools/play-native/install.sh`, or later without reinstalling:

  ```bash
  echo opengl > ~/Library/Application\ Support/RS\ Re-ARMed/renderer   # or: echo metal
  ```

- **Fallback:** if Metal can't start on your Mac, the game starts with OpenGL. If two starts with Metal
  in a row end abnormally (a crash, or a force quit; not a crash while quitting), the next start uses
  OpenGL, sets the `renderer` file to `opengl` and says so in `renderer-fallback.txt` in the same
  folder. Write `metal` into the file to try Metal again, or choose Metal in the installer.
- **Shaders:** Metal compiles the game's shaders once. The installer does this in its "Preparing
  shaders" step (a few minutes the first time; Skip leaves it to the game's first start). After a macOS
  update the game recompiles them on its
  next start, showing "Preparing shaders… N of M" on a black screen before it loads.

### The title logo

The build produces the Re-ARMed title logo automatically (it needs Python with Pillow, numpy and
etcpak; with `RS_PY` unset it makes its own virtual environment). To rebuild it manually, or if the
automatic step was skipped:

```bash
tools/loadscreen/build.sh "$HOME/Library/Application Support/Steam/steamapps/common/Rocksmith2014/static.psarc.rearmed"
```

It reads the game's `static.psarc`, changes only the two title-logo textures, and writes the result as a
new file next to it. The original `static.psarc` is not changed, and the launcher uses the new file
whenever it is there.

### Mods loaded at startup

From 1.2.0 the game loads every `.dylib` in `~/Library/Application Support/RS Re-ARMed/mods/` at startup, so
mods can change how it works without a rebuild. A reinstall keeps that folder. The build gives them **hook
points**, game functions they can run code before and after (`tools/ws7/recomp/overrides/hooks.skip`,
`tools/ws7/recomp/runtime/rc_hook.h`), and the game executable exports its runtime to them.
[rearmed-mods](https://github.com/glebb/rearmed-mods) uses this to bring many [RSMods](https://github.com/Lovrom8/RSMods)
features to RS Re-ARMed.

`RS_LOAD_MODS=0` starts the game without them; `RS_MODS_PATH` loads another folder or a single library.

### Works with your mods

- A modified `cache.psarc` in your install (for example a fast-load mod) is used as-is.
- Custom songs (CDLC) in your install are used as-is, with the RSBypass behaviour above.

## How to Install

You build the app yourself from your own copy of the game. The game app is never shared, so never
download builds of it from anywhere.

The easiest way is the **RS Re-ARMed Installer**, the one download this project offers: get
`RS.Re-ARMed.Installer.app.zip` from the [Releases](https://github.com/bbelgg/rs-rearmed/releases)
page, unzip it and open it. It contains no game code; it builds and installs the app on your Mac from
your own copy of the game. See [docs/BUILDING.md](docs/BUILDING.md) for its prerequisites.

To build by hand instead, follow the manual build in [docs/BUILDING.md](docs/BUILDING.md). When the
build is done, install it:

```bash
tools/play-native/install.sh
```

Then start **RS Re-ARMed** from `~/Applications` (double-click, Launchpad or Spotlight).

## Known Issues

- No online features. Ubisoft (Uplay) services are not available.
- Progress made in the native build is kept separately and is not copied back to your Steam saves
  automatically.
- Only the one Steam macOS build of Rocksmith 2014 is supported.
- With a fast-load `cache.psarc` mod, a start can now and then stay on the white loading screen,
  more often when the Mac is busy. Quit and start again. With Metal, RS Re-ARMed prepares its shaders
  before the game loads when it sees such a mod, which avoids the stall it would otherwise cause.
- With Metal, the first time a song, venue or mode is played, a few short stutters (up
  to about half a second) can show in its first half-minute while Metal compiles what it draws for the
  first time. It keeps the result, so later plays are smooth.
- Mods that draw on the screen with OpenGL themselves, such as rearmed-mods 0.1.0 (its song timer,
  accuracy and other on-screen text), show nothing with Metal; the rest of the mod works. Use a version
  that draws through the overlay API (`shims/include/rs_overlay.h`), or the OpenGL renderer.

## Under Investigation

Being looked into for future releases, with no promise of if or when:

- **RSMods:** more of [RSMods](https://github.com/Lovrom8/RSMods)' features on RS Re-ARMed.
- **Input channel selection:** choosing which input of an external audio interface the game listens to,
  on interfaces with more than one input.

## FAQ

**Is this an emulator?**
No. The game's x86_64 code is translated to C once, at build time (static recompilation), and compiled
into an arm64 app. Nothing is translated while you play.

**Does it change my Steam install?**
No. `install.sh` copies the app into its own folder, and the launcher mirrors your install there with
symlinks. Both only read your install and your Steam folder and never write to them. The one exception
is a file you add yourself: the optional title logo, `static.psarc.rearmed`.

**Where are my saves?**
In `~/Library/Application Support/RS Re-ARMed/steam/<account>/221680/remote/`. The logs are in
`~/Library/Application Support/RS Re-ARMed/logs/`.

**Do I need Steam running?**
No. The game needs to be installed through Steam, but the Steam client is not used while you play. Your
Steam saves are read once, on the first launch.

**Do DLC and custom songs work?**
The DLC folders in your install are linked in on every launch, including ones you add later. Custom songs
load because the RSBypass behaviour is built in and on by default.

**How do I update?**
Pull the latest changes, rebuild (see [docs/BUILDING.md](docs/BUILDING.md)) and run
`tools/play-native/install.sh` again. Your saves are kept.

**How do I uninstall?**
Delete `~/Applications/RS Re-ARMed.app` and `~/Library/Application Support/RS Re-ARMed`. That folder
holds your native-build progress, so back it up first if you want to keep it. If you built the title
logo, also delete `static.psarc.rearmed` from your game folder.

**Can I get a prebuilt app?**
No. A built app contains the game's code, recompiled from your copy, so it can't be shared. Build it
yourself from your own copy, with the installer or by hand.

**How do I lower the delay between playing and hearing the guitar?**
Three things add most of it. Your Mac's built-in speakers process the sound for about 20 ms: headphones or
a USB audio interface avoid that. The game's own output buffer is `LatencyBuffer`, in steps of 256
frames (5.3 ms): 1 is the lowest, 2 a safe value, 4 the game's default. It is the audio engine setting
in the game's Audio settings, and the launcher keeps what you choose there; `launchctl setenv
RS_LATENCY_BUFFER 1` overrides it. The output device's buffer is `RS_AUDIO_FRAMES` (default 256, and 128 at
`LatencyBuffer` 1, which is why 1 does not crackle here as it does under Rosetta). If the sound crackles,
raise one of them again. Both are read when the game starts, so a change in the game takes effect on the
next start.

**Where do I report bugs?**
In [GitHub Issues](https://github.com/bbelgg/rs-rearmed/issues). First check that the bug doesn't also happen in the original game
under Rosetta; if it does, it is a bug in the game, not in this port.

## Building

See [docs/BUILDING.md](docs/BUILDING.md).

## Credits

Projects referenced and thanked:

- [N64: Recompiled](https://github.com/N64Recomp/N64Recomp) and
  [XenonRecomp](https://github.com/hedge-dev/XenonRecomp), for the static-recompilation approach.
- [rs-mac-fixes](https://github.com/glebb/rs-mac-fixes) by glebb.
- [RSBypass](https://github.com/aik002/RSBypass) by aik002.
- The Rocksmith modding community.
- [FFmpeg](https://ffmpeg.org/), for Bink 2 decoding.
- [Lua](https://www.lua.org/), [libpng](http://www.libpng.org/pub/png/libpng.html) and
  [libjpeg](https://www.ijg.org/).
- [Capstone](https://www.capstone-engine.org/), the disassembler used by the recompiler.

RS Re-ARMed's own code is MIT-licensed: see [LICENSE](LICENSE). Third-party components keep their own
licences: see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
