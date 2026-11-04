# RS Re-ARMed

![RS Re-ARMed title screen](docs/images/title-screen.png)

RS Re-ARMed is a native arm64 port of Rocksmith 2014 for Apple Silicon Macs. The game is statically
recompiled to C from your own copy at build time, and small native libraries take the place of the parts
that tie it to Intel Macs, Windows, Steam and online services. It builds from either of two games:

- **The Mac game** (the Steam macOS version of Rocksmith 2014 Remastered, an x86-64 app that otherwise
  runs under Rosetta). Released; this is what most of this page is about. Docs: [docs/mac](docs/mac/README.md).
- **The Windows game, Learn & Play** (a purchased edition that Steam offers for Windows only, so it has no
  Mac download). **Experimental.** Docs: [docs/windows](docs/windows/README.md).

Both are translated by the same recompiler ([docs/recompiler](docs/recompiler/README.md)) and share the
renderer, the offline Steam layer and the audio code.

Not affiliated with or endorsed by Ubisoft. Rocksmith is a trademark of Ubisoft.

This project does not include any game code or assets; you need your own copy of the game.

## Table of Contents

- [System Requirements](#system-requirements)
- [Mac and Windows compared](#mac-and-windows-compared)
- [Features](#features)
- [Built-in patches and mods](#built-in-patches-and-mods)
- [How to Install](#how-to-install)
- [Known Issues](#known-issues)
- [Under Investigation](#under-investigation)
- [FAQ](#faq)
- [Documentation](#documentation)
- [Credits](#credits)

## System Requirements

- A Mac with Apple Silicon (M1 or later).
- macOS 12 Monterey or later.
- The Steam macOS version of **Rocksmith 2014 (Remastered)**, installed through Steam (no Rosetta: the game is
  never started, the build reads it from disk); or, for the
  experimental Windows port, **Rocksmith 2014 Learn & Play** on your Steam account
  ([details](docs/windows/INSTALL.md#requirements)). No Windows PC: the installer unpacks the game on the Mac.
- A Real Tone cable, or another audio input the game accepts. macOS asks for microphone access the first
  time the game listens for your guitar.

## Mac and Windows compared

The **Windows (Learn & Play) port is experimental**: it plays, but has had far less testing than the Mac
port, and several features are not there yet.

✅ works (tested) · ⚠️ present, not tested · ❌ not implemented · — not applicable

| Feature | Mac | Windows (Learn & Play) |
|---|---|---|
| Learn a Song (songs to the end, scoring) | ✅ | ✅ |
| Score Attack | ⚠️ | ✅ |
| Guitarcade | ⚠️ menus only; no game played yet | ✅ |
| Session Mode | ✅ | ✅ |
| Nonstop Play | ✅ | ✅ |
| Lessons and their videos | ✅ | ✅ |
| Rocksmith Workshop, tone designer | ⚠️ | ✅ the tone designer opens |
| Multiplayer (local) | ⚠️ | ⚠️ |
| DLC | ✅ | ✅ ¹ |
| Custom songs (CDLC) | ✅ | ✅ ¹ |
| Mods (`mods/` folder) and hook points | ✅ | ❌ |
| Built-in fixes (rs-mac-fixes, RSBypass) | ✅ | ✅ |
| Loading-screen fix (fast-load `cache.psarc` with Metal) | ✅ | ❌ |
| "Re-ARMed" title logo | ✅ | ✅ |
| Offline, no Ubisoft sign-in, Uplay screens skipped | ✅ | ✅ |
| No intro screens (`RS_MODS_INTRO=0` plays them) | ✅ | ✅ |
| Your Steam profiles copied in once | ✅ | ✅ |
| Audio output, backing track in time | ✅ | ✅ |
| Real Tone cable | ✅ | ✅ ² |
| Microphone | ⚠️ | ✅ |
| Other interfaces, Direct Connect | ✅ | ⚠️ |
| DIRECT CONNECT in the INPUT setting (overlay of your own `cache.psarc`) | ✅ | ✅ |
| ENUMERATE in the main menu: new songs without a restart | ✅ | ✅ |
| Lower guitar latency (buffers, real-time audio thread) | ✅ | ⚠️ |
| Bink videos with sound | ✅ | ✅ intro video, lessons |
| Window and full screen | ✅ | ✅ |
| Retina rendering (`RS_METAL_RETINA=1`) | ✅ | ✅ |
| Frame rate follows the display (75/120 Hz) | ✅ | ✅ 75 Hz |
| Metal renderer | ✅ | ✅ |
| OpenGL renderer and automatic fallback | ✅ | ❌ |
| Shaders prepared at install | ✅ | ❌ |
| Installer app and install script | ✅ | ✅ |
| Game image made on the Mac, with nothing from the user | ✅ | ✅ |
| Performance | ✅ about Rosetta's CPU | ✅ the display's frame rate, about 43% more CPU than the Mac port |

¹ The Windows game reads the PC song archives (`*_p.psarc`), the Mac game the Mac ones (`*_m.psarc`). Official DLC
has both, and the installer has Steam fetch every DLC the account owns. Custom songs often exist for one platform only.
² Tested with a virtual device named like the cable.

Details: [Mac plan](docs/mac/PLAN.md#status), [Windows plan](docs/windows/PLAN.md#status).

## Features

- **Native arm64.** The game runs as an arm64 app. No Rosetta, no x86 code at run time.
- **As light as Rosetta.** In gameplay it uses about the same CPU as the original game under Rosetta,
  a little less on average (about half a core at 60 fps, measured over a scripted song). It follows the
  display's refresh rate, so 75 Hz and 120 Hz displays get 75 and 120 fps.
- **Offline by design.** The Steam client is not needed while you play: an offline Steam layer stands in
  for it. A network guard (`tools/ws8/netguard`) is loaded into the game on every launch and refuses every
  connection that is not local, every host name lookup and every attempt to open a web page, and logs
  each one. The launcher will not start the game without it. Learn & Play opens no network connection of
  its own to guard: its Windows layer refuses the same things when the game asks.
- **Your profiles, kept separately.** On the first launch your Steam saves for Rocksmith 2014 are copied
  once into the RS Re-ARMed folder. From then on the native build uses only that copy; your Steam saves
  are never written. Stored Ubisoft credentials (`crd`) are never copied.
- **Metal renderer.** The game draws through Apple's Metal by default; OpenGL stays available as a choice
  and as an automatic fallback (see [Renderer](#renderer-metal-or-opengl)).
- **Its own copy of the game files.** The install keeps a copy of what the game reads (the game's archives,
  your DLC and custom songs), so a Steam update, uninstalling the game in Steam or moving your Steam library
  can't break it. With the game on the same disk it is an APFS clone: no extra space. New DLC or custom
  songs: **Refresh Game Files** in the installer.
- **Custom songs (CDLC) out of the box.** The CDLC signature bypass (RSBypass's behaviour) is built in and
  on by default: put custom songs in your game's `dlc` folder as usual, with no CDLC patch to install.

## Built-in patches and mods

These are compiled into the Mac app. Learn & Play has those marked "Both editions", and for the others the [table above](#mac-and-windows-compared) and [its design](docs/windows/DESIGN.md#built-in-fixes-and-mods) say what it has. Where a switch exists, it is an environment variable.

| What | Default | Switch |
|---|---|---|
| **rs-mac-fixes** behaviours ([rs-mac-fixes](https://github.com/glebb/rs-mac-fixes) by glebb): more keyboard keys in the game, the End key in menus, faster loading with large song libraries, higher priority for the game's worker threads (fewer hitches) and a higher open-file limit. Learn & Play has the loading and open-file fixes; its keys and thread priorities need no fix | on | `RS_MODS_FIXES=0` turns all of them off. `RS_MODS_BUDGET_MS=N` sets the time per frame for mounting DLC archives (default 50; 0 mounts one per frame, as the original game does) |
| **RSBypass** custom-song (CDLC) signature skip (an independent rewrite of [RSBypass](https://github.com/aik002/RSBypass) by aik002): custom songs load. Both editions | on | `RS_MODS_BYPASS=0` enforces the signature check again |
| **No intro screens**: the game starts at its title screen, without the logos, legal screens and intro video. No game file is changed. Both editions | on | `RS_MODS_INTRO=0` plays the intro |
| **Direct Connect choice**: DIRECT CONNECT (any audio interface) in the INPUT setting (My Path) and on the cable screen. The game supports it but its menus leave it out; the installer adds it in a small overlay made from your own `cache.psarc`, which stays as it is, so a modded cache still works. Both editions | on | `RS_MODS_OVERLAY=0` reads `cache.psarc` alone |
| **Enumerate**: the main menu's SHOP, which can't work offline, is ENUMERATE: add a song to Steam's `dlc` folder (or the copy's), press it, and the song is in the list, with no restart. Changed and removed songs still need **Refresh Game Files**. Does what [RSMods](https://github.com/Lovrom8/RSMods)' menu entry does. Both editions | on | `RS_MODS_OVERLAY=0`: SHOP, as in the game, and no scan |
| **Uplay sign-in prompt skipped**: Enter on the title screen goes straight to the profile list, without the "Uplay account" dialogs | on | none |
| **Offline Steam layer**: a replacement `libsteam_api` that works without the Steam client, with its cloud saves in a local folder | on | none |
| **Song ownership**: the game lists a song only when Steam reports its AppID as owned. The offline Steam layer reports the base game, your DLC and the AppID of every archive in the copy's `dlc/`, so custom songs show up whatever AppID they carry. The Rocksmith 1 songs you bought show up too: a copy or refresh asks Steam which you own, with your account logged in. Both editions | on | `RS_STEAM_DLC=all` reports every AppID as owned; a list of AppIDs reports only those |
| **"Re-ARMed" title logo**: a tag over the title-screen logo, built automatically from your own `static.psarc` by `tools/loadscreen`. Both editions | on | `RS_REARMED=0` shows the original logo; or delete `static.psarc.rearmed` |
| **Frame rate**: the game renders at the display's refresh rate (a 75 Hz or 120 Hz display gets 75 or 120 fps). A fixed rate can be set instead, with vsync off, for example 30 to save power | display | `RS_FPS=N` |
| **Lower guitar latency**: a smaller output buffer (see the FAQ), a real-time audio thread, and stale guitar audio dropped instead of played late | on | `RS_AUDIO_FRAMES=N` sets the output buffer (e.g. 128; 0 leaves the system's), `RS_LATENCY_BUFFER=N` the game's (1-16). `RS_AUDIO_RT=0` and `RS_AUDIO_INPUT_TRIM=0` turn off the other two. `RS_AUDIO_LOG=1` logs the audio setup |
| **All guitar interfaces listed**: an interface no longer goes missing from Direct Connect because another device, such as a webcam, failed to open | on | `RS_AUDIO_STABLE_UIDS=0` and `RS_AUDIO_INPUTS_FIRST=0` turn them off |
| **Renderer**: Metal, or Apple's OpenGL (see [Renderer](#renderer-metal-or-opengl)) | Metal | the `renderer` file, the installer, or `RS_GL_BACKEND=gl` |
| **Retina** (Metal only): the game renders at the screen's full resolution on a Retina screen | off | `RS_METAL_RETINA=1` |
| **Multithreaded GL engine** (OpenGL only): Apple's driver does its work on a thread of its own | off | `RS_GL_MTENGINE=1` |

`RS_MODS_LOG=1` writes what the mods apply to `logs/last-run.log`.

### Switching options

The launcher starts the game through macOS, so variables set in a Terminal window do not reach it. Set
them for your login session with `launchctl` before starting the game, for example:

```bash
launchctl setenv RS_MODS_BYPASS 0     # takes effect on the next launch
launchctl unsetenv RS_MODS_BYPASS     # back to the default
```

Learn & Play takes its switches from `open --env` instead ([its switches](docs/windows/INSTALL.md#settings-and-switches)).

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

The install produces the Re-ARMed title logo automatically, in RS Re-ARMed's own copy of the game files (it
needs Python with Pillow, numpy and etcpak: the build's environment has them, or with `RS_PY` unset it makes its
own). To rebuild it manually, or if the automatic step was skipped:

```bash
game="$HOME/Library/Application Support/RS Re-ARMed/game"
RS_GAME_DIR="$game" tools/loadscreen/build.sh "$game/static.psarc.rearmed"
```

It reads the copy's `static.psarc.original`, changes only the two title-logo textures, and writes the result
as a new file next to it. The original is not changed, and the launcher uses the new file whenever it is
there. Learn & Play does the same in its own copy of the game files
([docs/windows/INSTALL.md](docs/windows/INSTALL.md#the-title-logo)).

### Mods loaded at startup

From 1.2.0 the game loads every `.dylib` in `~/Library/Application Support/RS Re-ARMed/mods/` at startup, so
mods can change how it works without a rebuild. A reinstall keeps that folder. The build gives them **hook
points**, game functions they can run code before and after (`tools/targets/names/hooks.list`,
`tools/ws7/recomp/runtime/rc_hook.h`), and the game executable exports its runtime to them. A mod finds a
hook point by its name, `rc_hook_add_name("game_frame", pre, post, user)`; `rc_hook_add` with a game
address keeps working, and `rc_target()` says which game the app was built from (`mac-x64`).

`RS_LOAD_MODS=0` starts the game without them; `RS_MODS_PATH` loads another folder or a single library.

### Legacy mods

Legacy mods, the ones made for the original game that change its files, might work as before. Verified:

- A modified `cache.psarc` in your install (for example the fast-load mod) is used as-is.
- Custom songs (CDLC) in your game's `dlc` folder load as they are; the CDLC bypass they need is built in.

## How to Install

You build the app yourself from your own copy of the game. The game app is never shared, so never
download builds of it from anywhere.

The easiest way is the **RS Re-ARMed Installer**, the one download this project offers: get
`RS.Re-ARMed.Installer.app.zip` from the [Releases](https://github.com/bbelgg/rs-rearmed/releases)
page, unzip it and open it. It contains no game code; it builds and installs the app on your Mac from
your own copy of the game. You click Build & Install, and it tells you whenever it needs you, step by step. The
first time, macOS refuses to open it, as it isn't notarized: click **Open Anyway** in **System Settings**,
**Privacy & Security**. See [docs/mac/INSTALL.md](docs/mac/INSTALL.md) for its prerequisites.

To build by hand instead, follow the manual build in [docs/mac/INSTALL.md](docs/mac/INSTALL.md#build-by-hand).
When the build is done, install it:

```bash
tools/play-native/install.sh
```

Then start **RS Re-ARMed** from `~/Applications` (double-click, Launchpad or Spotlight).

### Learn & Play (the Windows game): experimental

**Learn & Play** is a purchased edition of Rocksmith 2014 that Steam offers for Windows only, so its owners
can't use the Mac steps above. RS Re-ARMed can build its Windows game into a native Mac app too. This is
**experimental** (see [the table](#mac-and-windows-compared)). In the installer choose **Game: Learn &
Play** and click Build & Install, or run it from a clone of this repository:

```bash
tools/play-native/learn-play.sh            # or: tools/play-native/install.sh --learn-play
```

It has Steam download the Windows game, with nothing to paste (if Steam isn't signed in with the account that
owns it, it says so and goes on when it is), takes the Microsoft libraries it needs from Steam's Common
Redistributables, keeps its own copy of the game files (so Steam clearing its download folder can't break it;
an APFS clone, no extra space, on the same disk), and unpacks your own game's code on the Mac. No Windows PC is
needed: the released installer carries a small seed for that, with no game code in it. A clone of this
repository has no seed, and then builds from a one-time capture on a Windows PC instead. The first build takes
about 25 minutes on an M2. It needs Apple's command line tools (the installer starts their installation) and
nothing else: no Rosetta, no Homebrew, no .NET. Then start **RS Re-ARMed Learn & Play** from `~/Applications`. The whole procedure,
keys, window modes and troubleshooting: [docs/windows/INSTALL.md](docs/windows/INSTALL.md).

## Known Issues

For the Mac port; the Windows port's are in [docs/windows/INSTALL.md](docs/windows/INSTALL.md#known-issues).

- No online features. Ubisoft (Uplay) services are not available.
- Progress made in the native build is kept separately and is not copied back to your Steam saves
  automatically.
- Only the one Steam macOS build of Rocksmith 2014 is supported.
- With the legacy fast-load mod (a modified `cache.psarc`), a start can now and then stay on the white
  loading screen, more often when the Mac is busy. Quit and start again. With Metal, RS Re-ARMed prepares
  its shaders before the game loads when it sees such a mod, which avoids the stall it would otherwise
  cause.
- With Metal, the first time a song, venue or mode is played, a few short stutters (up
  to about half a second) can show in its first half-minute while Metal compiles what it draws for the
  first time. It keeps the result, so later plays are smooth.
- Mods that draw on the screen with OpenGL themselves show nothing with Metal; the rest of the mod
  works. Use a version that draws through the overlay API (`shims/include/rs_overlay.h`), or the OpenGL
  renderer.

## Under Investigation

Being looked into for future releases, with no promise of if or when:

- **RSMods:** more of [RSMods](https://github.com/Lovrom8/RSMods)' features on RS Re-ARMed.
- **Input channel selection:** choosing which input of an external audio interface the game listens to,
  on interfaces with more than one input.
- **The Windows game:** bringing the Learn & Play port to parity with the Mac port (mods and hook
  points, the shaders prepared at install, the OpenGL renderer). See [docs/windows/PLAN.md](docs/windows/PLAN.md).

## FAQ

**Is this an emulator?**
No. The game's x86 code (x86-64 for the Mac game, 32-bit x86 for the Windows game) is translated to C once, at build time (static recompilation), and compiled
into an arm64 app. Nothing is translated while you play.

**Does it change my Steam install?**
No. `install.sh` copies the app and the game files into its own folder (APFS clones when the game is on
the same disk, which take no extra space), and the game reads only that copy. Your install and your Steam
folder are only read, never written. (Earlier versions left `static.psarc.rearmed` in the game folder; you can
delete it.)

**Where are my saves?**
In `~/Library/Application Support/RS Re-ARMed/steam/<account>/221680/remote/`. The logs are in
`~/Library/Application Support/RS Re-ARMed/logs/`. Learn & Play's are in `steam/` and `logs/` of
`~/Library/Application Support/RS Re-ARMed/learn-play/`.

**Do I need Steam running?**
No. The game needs to be installed through Steam to install RS Re-ARMed, but the Steam client is not used
while you play, and the installed game keeps working if Steam updates, moves or uninstalls the game. Your
Steam saves are read once, on the first launch.

**Do DLC and custom songs work?**
The DLC and custom songs in your game's `dlc` folder are copied into RS Re-ARMed's game files at install
time. (Learn & Play's installer has Steam fetch every DLC your account owns first.) To add a song while you play, put it in that folder and press **ENUMERATE** in the main menu; it is in
the song list a moment later. After changing or removing songs, click **Refresh Game Files** in the installer (or run
`tools/play-native/install.sh --refresh-game`). Custom songs load because the RSBypass behaviour is built in
and on by default: there is no separate CDLC patch to install.

**How do I update?**
Download the new installer from the [Releases](https://github.com/bbelgg/rs-rearmed/releases) page and run it. By
hand: pull the latest changes, rebuild (see [docs/mac/INSTALL.md](docs/mac/INSTALL.md)) and run
`tools/play-native/install.sh` again. Your saves are kept.

**How do I uninstall?**
Delete `~/Applications/RS Re-ARMed.app` and `~/Library/Application Support/RS Re-ARMed`, which also removes
the copy of the game files. That folder holds your native-build progress, and Learn & Play's in `learn-play/`, so back
it up first if you want to keep it. With the game on another drive the copy is there too:
[Mac](docs/mac/INSTALL.md#update-and-uninstall), [Learn & Play](docs/windows/INSTALL.md#update-and-uninstall). An
earlier version may have left `static.psarc.rearmed` in your Steam game folder; you can delete it.

**Can I get a prebuilt app?**
No. A built app contains the game's code, recompiled from your copy, so it can't be shared. Build it
yourself from your own copy, with the installer or by hand.

**How do I get the lowest delay between playing and hearing the guitar?**
1. In the game's Audio settings, set the audio engine setting to 1, then quit and start the game again (it
   is read at start-up). 1 is the lowest; if the sound crackles, use 2. The game's default is 4.
2. Listen through headphones or a USB audio interface, not your Mac's built-in speakers, which process the
   sound for about 20 ms.

The audio engine setting is `LatencyBuffer` in `Rocksmith.ini`: the game's output buffer, in steps of 256
frames (5.3 ms). The launcher keeps what you choose in the game; `launchctl setenv RS_LATENCY_BUFFER 1`
overrides it. The output device's buffer is `RS_AUDIO_FRAMES` (default 256, and 128 at `LatencyBuffer` 1,
which is why 1 does not crackle here as it does under Rosetta). At 1 only the game's output buffer is that
small; the rest of its audio engine runs as at 2, double-buffered, which adds no delay
(`RS_AUDIO_RING_SPLIT=0` turns that off). Both are read when the game starts.

**Where do I report bugs?**
In [GitHub Issues](https://github.com/bbelgg/rs-rearmed/issues). First check that the bug doesn't also happen in the original game
(under Rosetta, or on Windows for Learn & Play); if it does, it is a bug in the game, not in this port.

## Documentation

| | Mac port | Windows port (Learn & Play) |
|---|---|---|
| Index | [docs/mac](docs/mac/README.md) | [docs/windows](docs/windows/README.md) |
| Install, build, play | [INSTALL.md](docs/mac/INSTALL.md) | [INSTALL.md](docs/windows/INSTALL.md) |
| How it works | [DESIGN.md](docs/mac/DESIGN.md), [METAL.md](docs/mac/METAL.md) | [DESIGN.md](docs/windows/DESIGN.md) |
| Performance | [PERFORMANCE.md](docs/mac/PERFORMANCE.md) | [PERFORMANCE.md](docs/windows/PERFORMANCE.md) |
| Status and plan | [PLAN.md](docs/mac/PLAN.md) | [PLAN.md](docs/windows/PLAN.md) |
| Testing | [TESTING.md](docs/mac/TESTING.md) | [TESTING.md](docs/windows/TESTING.md) |

The recompiler both use: [docs/recompiler](docs/recompiler/README.md).

## Credits

Projects referenced and thanked:

- [N64: Recompiled](https://github.com/N64Recomp/N64Recomp) and
  [XenonRecomp](https://github.com/hedge-dev/XenonRecomp), for the static-recompilation approach.
- [rs-mac-fixes](https://github.com/glebb/rs-mac-fixes) by glebb.
- [RSBypass](https://github.com/aik002/RSBypass) by aik002.
- [RSMods](https://github.com/Lovrom8/RSMods) by Lovrom8, for the menu entry that Enumerate does the job of.
- The Rocksmith modding community.
- [FFmpeg](https://ffmpeg.org/), for Bink 2 decoding.
- [Lua](https://www.lua.org/), [libpng](http://www.libpng.org/pub/png/libpng.html) and
  [libjpeg](https://www.ijg.org/).
- [Capstone](https://www.capstone-engine.org/), the disassembler used by the recompiler.

RS Re-ARMed's own code is MIT-licensed: see [LICENSE](LICENSE). Third-party components keep their own
licences: see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
