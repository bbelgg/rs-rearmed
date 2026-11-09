# How the Mac port works

The Mac port takes the x86-64 Steam build of Rocksmith 2014 for macOS, which would otherwise need Rosetta,
and makes a native arm64 app of it. The game's code is translated to C at build time by the shared
recompiler ([recompiler](../recompiler/README.md)); the parts of the game that tie it to Intel Macs,
Steam, Ubisoft's services and old system libraries are replaced by native code. User side:
[INSTALL.md](INSTALL.md).

## Overview

```
game image (tools/steamless, from your install)
   |  gen_image.py --skip <override lists>          tools/targets/mac-x64 (names -> addresses)
   v
libgame_rc.a (translated game) + overrides (native replacements) + runtime (rc_*)
   + platform shell (shims/platform) + seam (app/seam_platform.c)
   v  link (tools/ws7/recomp/app/build.sh, run by build-m4.sh)
Rocksmith2014.app, bundling libRSOpenGL, libsteam_api, the Bink and Cg shims
   v  tools/play-native/install.sh
~/Library/Application Support/RS Re-ARMed/game/   +   ~/Applications/RS Re-ARMed.app (launcher)
```

At run time the app maps the original image (cloned into `Contents/Resources`) for its data, and runs the
translated functions in its place. The game's own imports (system libraries, Steam, Bink, Cg, OpenGL) are
bound to the bundled replacements.

## Getting the game image

The installer decrypts on every run, in seconds, so an image kept from an earlier version can't be used by mistake.

The Steam copy is encrypted by Valve's SteamStub DRM, which decrypts `__text` in memory at start: a repeating
8-byte XOR. `tools/steamless/decrypt.py` undoes it on the file, with the key it finds in your own binary (nothing
ships, and it prints nothing of it), and checks the result against the pin. The result is the input of every build.
The game is not started, the Steam copy isn't changed, and no Rosetta or Steam account is involved. See
[tools/steamless/README.md](../../tools/steamless/README.md).

## Translation

`tools/ws7/recomp/app/build-m4.sh` drives it: the per-game tables (`targets.py gen`), the override
libraries, the translation (`build/build.sh` with the override skip lists), the compile, the shader corpus
and the link. The 64-bit front end and its switches (memory-order fences, relaxed functions, `-O1` list):
[recompiler](../recompiler/README.md#the-64-bit-front-end-mac).

## Per-game tables

`tools/targets/mac-x64` holds the image's identity (SHA-256), and `symbols.map`, `offsets.map` and
`drops.map`: every function, global and struct field the native code uses, by name. The override, seam
and app sources use names only. See [tools/targets/README.md](../../tools/targets/README.md).

## Runtime

`tools/ws7/recomp/runtime`: the context and call machinery (`rc.h`, `rc_call.h`), indirect-call lookup,
trampolines between native code and translated code (`rc_tramp`, `rc_native`), `setjmp`/`longjmp` across
guest frames (`rc_setjmp`), threads (`rc_thread`), Objective-C message sends (`rc_objc`), varargs
(`rc_va`) and the hook points (`rc_hook`).

## Platform

`shims/platform` rewrites the game's Cocoa shell in native code: the app and its window, the view, the
display link and render thread, input, file paths and `Rocksmith.ini` (`rsp_shell.m`, `rsp_paths.c`,
`rsp_config.c`). The engine sits behind function-pointer hooks; `tools/ws7/recomp/app/seam_platform.c`
connects them to the translated `GameBootstrap` and `GameFrame`. The shell adds the keyboard keys the
original lacked and follows the display's refresh rate (`RS_FPS` fixes a rate). On a Retina screen the game
renders in points and each frame is scaled up to the screen's pixels with MetalFX (`rs_gfx_set_output_size`,
[METAL.md](METAL.md#metal-core-shimsopenglsrcmetal)); `RS_METAL_RETINA=1` renders at backing-pixel size instead. A window takes any shape, where the original kept it at 16:9
(`RS_WINDOW_ANY_SHAPE=0` does too): the game sets its cameras from its resolution and lays out its menus to the width,
so it needs nothing more ([INSTALL.md](INSTALL.md#window-modes)). The screen saver and display sleep stay off while the game
is open, in a window or full screen: the view holds a display-sleep assertion for its whole life, as the
original's does.

The install is self-contained: `install.sh` puts RS Re-ARMed's own copy of the game files next to the
installed app (`tools/play-native/gamefiles.py`: APFS clones, which share the data with Steam's files until
one side changes, when both are on one volume, else a real copy; `.rearmed/manifest.json` written last marks
it complete; a refresh copies what changed and removes only files of the earlier copy). A new copy of a game
on another APFS drive (a Steam library on an external disk) is made on that drive instead, as clones
(`<drive>/RS Re-ARMed/game`, Learn & Play's `…/learn-play/game`; when a test clone there fails, a real copy
at home), and the edition's `game` path is a symbolic link to it, so the launchers and a refresh use the same
path; the copy's `.rearmed/link.json` names that link, and a second install does not take it over. The
launchers tell a dangling link (the drive not connected) from an incomplete copy. The shell takes the
folder that holds the `.app` as the game root, so the game reads only that copy. The launcher
(`tools/play-native/launch.sh`, run in the `~/Applications/RS Re-ARMed.app` bundle by `launch-stub.c`) checks the copy is complete,
links `static.psarc` to the title logo or the original, writes an offline `Rocksmith.ini` from the copy of
the install's (keeping the player's `LatencyBuffer`), passes the DLC and language Steam's appmanifest had
(`.rearmed/steam.env`) to the offline Steam layer, passes the built-in mods switched off in `settings.env` (the
installer's Built-in Mods…; only those seven switches, and a value in the environment wins, which it passes on too, as
`open` starts the game with launchd's environment), seeds the offline Steam saves once, loads the network
guard and starts the game with `open -n -a`. Nothing of Steam's is read while the game runs, except
`loginusers.vdf` for the account, and without it (Steam uninstalled) the account whose saves are there.

## Renderer

The game draws OpenGL 2.1 with Cg programs (Gamebryo) and GLSL 1.20 (Scaleform). Everything goes through
`libRSOpenGL` (`shims/opengl`), which runs it on Metal by default or on Apple's OpenGL; the Cg shim
(`shims/cg`) translates the ARB programs to GLSL. Design, defaults, fallback and status:
[METAL.md](METAL.md).

### Shader corpus

Made at build time from the game files and compiled by the install's warm-up: [METAL.md](METAL.md#shader-translation) and
[METAL.md](METAL.md#safe-load). Every pair the shaders could form is in it. Learn & Play's shaders pair too freely for that:
[its corpus](../windows/DESIGN.md#shader-corpus) has the programs seen in runs.

## Audio

The game's Wwise engine has a CoreAudio sink and its own guitar input. The audio override module
(`tools/ws7/recomp/overrides/src/audio_*.c`) replaces their CoreAudio side:

- the output device runs with a 256-frame buffer (`RS_AUDIO_FRAMES`; at most half the game's own buffer,
  so 128 at `LatencyBuffer=1`, and 128 on an interface that also has inputs, whose buffer the guitar input
  shares and opens at 128), and the game's ring is split so `LatencyBuffer=1` does not crackle
  (`RS_AUDIO_RING_SPLIT`);
- a smaller buffer set on the guitar's device by another program, such as `rs-tonehost` at 64 frames, is collected
  into whole 128-frame slots before the game's input takes it (`audio_chan.c`); at 128 nothing changes;
- the Wwise thread is a real-time thread in the output device's audio workgroup (`RS_AUDIO_RT`);
- the sink follows the default output: the game opens it once and never moves it, so when the default changes the override
  stops the game's audio graph, gives its output unit the new device with the sink's 48 kHz and buffer, and starts it again;
- stale guitar audio queued from the cable is dropped instead of played late (`RS_AUDIO_INPUT_TRIM`);
- the guitar input's channel and gain: the game's I/O function handles one channel, or picks one of two, and of a
  device of more channels it queues the start of the buffer as it is, so 8 channels give 16 frames of mixed-up audio
  per 128. A hook (`si_io`, `audio_chan.c`) gives it a mono buffer of the chosen channel with the gain instead, and the
  game's mono path does the rest. Settings: `guitar-input.ini` ([INSTALL.md](INSTALL.md#guitar-input));
- each channel of an interface is an input of its own (`shims/coreaudio/rs_audio_input.c`, `audio_devs.c`). The game opens
  a device per slot, 10 at most, and lists what it opened, so a channel is a device to it: the list it reads has ids that
  hold the real device and the channel, the import thunks of the calls that take a device reach the real one, and only
  the name and UID differ. The list is in priority order, as the game stops at 10. Two habits of the game had to be met:
  it compares the device its I/O function gets with its own id, so `audio_chan.c` gives it that id, and it starts and
  stops its I/O by the function's address, which is the id only for a function's first registration on a device, so a
  channel's real id is kept and used in its place;
- an input's level. Calibration is a loop: from about 0.4 it raises the level in small steps until its meter reaches
  the top. It reaches a device through `volm`, which most interfaces lack, so it pegged at 1 and never finished, and
  which on the others is the player's own setting. So every input but the cable reports a volume of ours (`audio_devs.c`),
  and the level is a gain in the `si_io` hook: 0.375, where the game starts a device, is unity, and 1 is +24 dB. A level
  saved before counts for nothing until calibration has run once, seen as a run of changes on one input, as a start sets
  them all; `input-level-calibrated` in the folder keeps that. The cable keeps its hardware level and the game's own path.
  The game reads the list about 26 times at the start and not again in play, so its device reads cost nothing there;
- devices get stable identifiers, so an interface is not dropped from Direct Connect after another device
  fails to open, and input devices are listed first (`RS_AUDIO_STABLE_UIDS`; `RS_AUDIO_INPUTS_FIRST` applies with the
  list of inputs off, as the list has its own order).

The game-independent parts (buffer sizes, real-time threads, workgroups, stable UIDs, input-first order, the
input's settings and gain) are in `shims/coreaudio/rs_audio_host.c` and `rs_audio_input.c`, which the Windows port shares. `RS_AUDIO_LOG=1` logs the setup.

### Your own plug-in

rearmed-mods' ToneHost add-on plays the guitar through an Audio Unit in place of the game's tones, with a host of its own beside
the game. Its mod is the game's side: on a tone change the game sets each knob of the gear as a Wwise RTPC by name, about
two seconds ahead of the change in the songs tried, and the mod passes them to the host and keeps `Mixer_Player1` at 0. So
it needs no game code. The one change here is in `audio_chan.c`: the host can run the guitar's device at 64 frames, and the
input collects that into the 128-frame slots the game queues. The game's own tones don't get faster: Wwise's 256-frame blocks
set their latency.

## Video (Bink)

The game plays its videos with Bink 2 (`libBink2Macx64.dylib`). `shims/bink` replaces it with a decoder on
a minimal static FFmpeg 7.1.2 build (downloaded and checksum-verified at build time).

## Steam

`shims/steam` is a drop-in `libsteam_api.dylib`. The launcher runs it in stub mode: no Steam client, Steam
Cloud as a local folder (`steam/<account>/221680/remote`, seeded once from your Steam saves without `crd`),
the identity from `loginusers.vdf`, the base game, the installed DLC and 248750 (the DLC custom songs
carry) as owned. See [shims/steam/README.md](../../shims/steam/README.md).

The game mounts a `dlc/` archive only when the AppID in its `appid.appid` is a DLC Steam lists and reports
installed, so custom songs that carry another AppID (or 248750, for an account without it) would not show.
`tools/play-native/gamefiles.py` reads the AppID of every archive in the copy's `dlc/` at each copy and
refresh and adds them to `RS_STEAM_DLC` in `game/.rearmed/steam.env` (the base game's AppID, which an RS1
compatibility archive carries, left out); `.rearmed/dlc-appids.json` keeps them by size and time, so a
refresh reads only new and changed archives. Steam installs only the official DLC the account owns, so the
list gains only the custom songs' AppIDs, and official DLC is reported as before. Done at install rather than
in the Steam layer: no psarc reading in the game process, and one implementation for both editions
(Windows: [DESIGN.md](../windows/DESIGN.md#steam)).

The songs of `rs1compatibilitydlc` need more. The archive comes with the game and carries the base AppID, and its
audio is in `songs.psarc`, but each song names the DLC of the first Rocksmith it came from (`DLCRS1Key`'s `WIN32`,
141 AppIDs for 143 songs). `dlc_update_tail` walks the entries of the game folder's encrypted `metadata` file
(`STEAM<AppID>`), asks `BIsSubscribedApp` for each AppID up to 222140 and keeps those owned; the list filter
(`0x100a02e60`) hides an RS1 song whose AppID is not kept. Steam installs nothing for these and keeps no ticket for an
app that never ran, so nothing on disk says who owns them: a copy or refresh asks the Steam client, when the
edition's account is the one logged in (`rs1_owned` in `gamefiles.py`, a process of its own through Valve's library,
gone before the game starts). The answer goes into `RS_STEAM_OWNED` in `steam.env`, which the Steam layer reports
through `BIsSubscribedApp` only: as with real Steam, these are no DLC of this game. `.rearmed/rs1-owned.json` keeps
it for a refresh with Steam closed or on another account. The 52 songs of `rs1compatibilitydisc` have no such check:
AppID 258341 and audio of their own are all they need.

## Online services

The game's Rendez-Vous/Uplay layer is replaced by an offline one (the `online` override module:
`shims/game/src/online_stub.cpp`, `tools/ws7/recomp/overrides/src/online_glue.c`): no login is started,
Enter on the title screen goes straight to the profile list, and requests get the answers the game gives
when Ubisoft's servers are unreachable. `crd` is never read.

`tools/ws8/netguard` is loaded into the game on every launch (the launcher refuses to start without it). It
refuses every connection that is not local, every host-name lookup and every attempt to open a web page,
and logs each to `logs/netguard.log`.

## Libraries

The game's statically linked libraries are replaced by native builds of the same code (override modules
`zlib`, `png`, `jpeg`, `lua`, `crypto`, `efd`, `dsp`; sources in `shims/game` and
`tools/ws7/recomp/overrides/src`). `shims/libstdcxx` provides the link stub for the system's
`libstdc++.6.dylib`, which the game links. `shims/game` has a differential harness that runs the original
x86-64 functions under Rosetta against the rewrites ([TESTING.md](TESTING.md)).

## Built-in fixes and mods

- **Built-in fixes** (`tools/ws7/recomp/overrides/mods/mods.c`): the rs-mac-fixes behaviours (keys, the End
  key in menus, faster loading with large libraries, the number-table hash, worker-thread priority, the
  open-file limit, in `rc_nofile.h`, which the Windows build shares) and the RSBypass CDLC signature skip,
  each with a switch
  ([INSTALL.md](INSTALL.md#settings-and-switches)). `RS_MODS_LOG=1` logs what they apply. The song path
  cache and the number-table hash have Windows counterparts on the same switch, on that build's own functions
  ([Windows design](../windows/DESIGN.md#built-in-fixes-and-mods)).
- **No intro screens** (`mods.c`, `before_intro_update`, a pre-hook on the hook point `boot_intro_update`):
  the boot service (`RSBootService`) ticks the intro movie (logos, legal screens, intro video) in its boot
  phases 2 and 3 and shows the title only once the intro has ended. The movie's model is loaded as usual,
  but the movie is never started: the tick reports "not busy" (as while the movie plays) until phase 3 has
  done its steps (archives mounted, venues), and then the intro takes the transition of the game's own
  skip key (intro state 4). Both parts matter: without the model loaded, or with the intro ended before
  phase 3's steps, the title screen never gets its menu movie and the game sits on an empty screen (the
  stall RSMods' short "fast load" intros also run into). The screen is white, then black for about three
  seconds, then the title (about 8 s from launch instead of about 31 s). No game file is changed.
  `RS_MODS_INTRO=0` plays the intro. The Windows build does the same ([Windows
  DESIGN](../windows/DESIGN.md#built-in-fixes-and-mods)).
- **Auto Save warning skipped** (`mods.c`, `before_autosave_warning`, a pre-hook on the hook point `autosave_warning`):
  on `OnSignInFlowComplete` the game's UI event handler restores the player's input mode (`input_mode_restore`), shows
  `RS2AutosaveWarning` and starts a 4 s timer. The hook does the first and leaves out the rest: it stores a token in the
  handler's dialog slot and a zero in the timer, so the next tick goes on, as it does when the dialog's time is up. A NULL
  slot would stop the flow on the profile screen. `RS_MODS_AUTOSAVE=0` shows the dialog. The Windows build does the same, by leaving out only the dialog's call
  ([Windows DESIGN](../windows/DESIGN.md#built-in-fixes-and-mods)).
- **Mods** (`tools/ws7/recomp/runtime/rc_mods.c`, shared): every `.dylib` in `<home>/mods/` is loaded at startup. The
  executable exports the runtime. A mod hooks a game function by name with
  `rc_hook_add_name("game_frame", pre, post, user)`; the hook points are listed in
  `tools/targets/names/hooks.list` and declared in `tools/ws7/recomp/runtime/rc_hook.h`. A name the game doesn't
  have gives -1 and a log line. `rc_hook_add` with a game address keeps working; `rc_target()` returns `mac-x64`.
  Mods that draw use the overlay API (`shims/include/rs_overlay.h`), which works on both renderers; a mod that changes a game texture uses the render API (`shims/include/rs_render.h`, Metal alone). A mod built
  with `tools/ws7/recomp/runtime/rc_mod.h` is the same dylib for this edition and Learn & Play: it looks offsets
  up by name when it runs and keeps pointer width out of its source
  ([recompiler](../recompiler/README.md#overrides-and-hook-points)). A mod built for 1.4.x keeps working here as it
  was, and is left out on Learn & Play. The rearmed-mods library (`rearmed-mods.dylib`, from 0.3.0) is
  built this way: its names are under "the mods library" in `tools/targets/names`. Its song search check knows the key
  from the tables (`song_search_key`), as a game binding: `/` here, Shift in Learn & Play.
- **Loading-screen fix.** With a community fast-load `cache.psarc`, the game's loading can stall for good
  on the white loading screen when the CPU is busy (a race in the game). With Metal, when `cache.psarc` is
  not the original, the shell compiles the shaders before the game starts loading and then throttles the
  compiler ("safe load", [METAL.md](METAL.md#safe-load)).
- **Two players on any input** (`mods.c`, pre hooks on `input_mode_is_cable` and `input_bind`). A player's input mode and input
  are in their profile, and the game serves up to four inputs: each player's guitar and microphone (`input_binding`, a slot each).
  Two rules of the game's made two players need two cables. The Multiplayer screens ask whether Player 2 is in Real Tone mode
  and set them to it when not; here a profile on Direct Connect or the microphone counts as in the right mode. And a saved input is
  restored for Player 1 alone, after the profile screen, while Player 2 takes the first free slot; here `input_bind` finds Player 2's
  saved input first. It has to be that early: moving the input of a running stream crashed the I/O thread, in the callback that feeds
  the player. SWAP CABLE ASSIGNMENT is the game's own, lit when both modes match: it swaps the two slots and saves both
  names. The flow that counts cables (`MultiplayerGuitarsNotPluggedIn`) belongs to the two-cable calibration. The menu
  overlay rewords the notice MULTIPLAYER INPUT SUPPORT (row 37359), which said Player 2 must use the cable.
- **Direct Connect choice.** The game code handles a fourth button on the cable screen, `othercable`
  (Direct Connect), and a fourth value of the INPUT slider, but the menu data (two JSON files in one of
  `cache.psarc`'s `cacheN.7z` archives) leaves them out. At each copy or refresh of the game files,
  `tools/play-native/cache_overlay.py` adds them to the user's own copies of the two files and stores the
  rewritten archive in `game/Update/patcharchives.psarc`, the patch container the engine mounts at
  `PatchUpdate/` when it is there. The engine's patch layer takes only its own `.new`/`.delta` archives from
  the container, so a pre hook on `FrontEnd_OpenFile` (the hook point `frontend_open_file`, in
  `overrides/mods/mods.c`) opens `PatchUpdate/cacheN.7z` first and the `cache.psarc` one when the overlay
  has none. `cache.psarc` is never changed, so a modded cache still works; one that has the entries gets no
  overlay. `RS_MODS_OVERLAY=0` reads `cache.psarc` alone.
- **Exit Game.** The main menu's UPLAY, which can't work here, becomes EXIT GAME in the same overlay
  (`ui_menu_pillar_main`: the label, the game's own row 19651 in every language, and the `OnSelect`, `ExitGame`, which the
  file's help list has too). The game asks with its own dialog, then quits as it does on Cmd+Q.
- **Enumerate.** The main menu's SHOP becomes ENUMERATE, second in the list, in the same overlay
  (`ui_menu_pillar_main`: the label and sort orders only; its `OnSelect` is still `ChangeMenu` to `Shop`). Before
  it opens the Shop the game asks `ISteamUtils::IsOverlayEnabled`, and it asks nowhere else (RSConnectService slot
  126, four callers). The answer here is no, so the Shop never opens: the game stays in the menu and shows notice
  36675, which the overlay rewords (`localization/maingame.csv`, in `cache4.7z`, all languages). That question
  is the press. The Steam layer (`shims/steam/src/enumerate.h`, shared with Learn & Play) copies new archives on a
  thread of its own from the folders in `.rearmed/source.json` into the copy's `dlc/`, then posts `DlcInstalled_t`
  for 248750, whose listener (`RSSteamDownloadableContent::OnDlcInstalled`) asks the DLC service to enumerate:
  slot 13, flag `dlc_pending`, the flag RSMods sets by hand. The game then mounts what it finds and the song
  list has it. Only new names are written, through a `.part` file and a rename, so nothing the game has open is
  touched; an archive keeps its size and time and is named in `.rearmed/enumerated.txt`, so a refresh takes it
  into the manifest and `steam.env`, or removes it when the source no longer has it. A source folder that is gone
  or too little room (the 256 MB margin of `gamefiles.py`, for a real copy) leaves the copy as it is and is logged.
  Two of the four callers are on the Shop stage; the other two are not (one carries the Rocksmith 1 import's
  AppID, so they look like store pages). The Steam layer can't tell them apart: they show the same notice and scan
  too, which does no harm. `RS_MODS_OVERLAY=0` leaves all of this out.
- **Song ownership during a mount.** The game checks an archive's AppID against the list
  `RSSteamDownloadableContent` made once at start from `GetDLCCount`, so `RS_STEAM_DLC` can't answer for a song
  added later. While the install step runs, a hook on `steam_dlc_owned` (`mods.c`) answers yes: an archive in `dlc/`
  counts as owned, whatever AppID it carries. `RS_MODS_BYPASS=0` asks the list again, and such a song then needs
  a refresh and a restart.
- **Title logo.** `tools/loadscreen` builds `static.psarc.rearmed` in the copy (`install.sh`; never in Steam's
  folder) from the copy's `static.psarc.original` with a "Re-ARMed" tag on the title logo; the launcher uses it when it is there (`RS_REARMED=0` for the original).
  The Windows build does the same ([Windows DESIGN](../windows/DESIGN.md#built-in-fixes-and-mods)).

## Installer and build

- `tools/installer/RSReARMedInstaller.swift` (built by `tools/installer/build.sh`) is the GUI. For the Mac
  game it runs the manual steps of [INSTALL.md](INSTALL.md) with a progress bar: prerequisites, the Python
  environment, the game image (`decrypt.py`), the shims, the
  app, the install and the shader warm-up. What the user has to do is shown in a box above the log, from the
  `guide` lines of `gamefiles.py mac-game`, which finds the game in every Steam library and leads through Steam
  as Learn & Play's download does ([Windows DESIGN](../windows/DESIGN.md#installer-and-build)). Its Built-in Mods…
  sheet writes the chosen edition's `settings.env` (`KEY=0` lines, no file when every mod is on), which that
  edition's launcher reads at every start; `tools/play-native/test_settings.py` checks that the installer and both
  launchers have the same keys.
- `tools/play-native/install.sh` installs a build: the app and the network guard into
  `~/Library/Application Support/RS Re-ARMed/`, the launcher into `~/Applications` (`launch.sh`, run by the compiled
  `launch-stub.c`, so that macOS holds the app and not the shell responsible for its privacy questions), the renderer
  setting, then `warm-shaders.sh`.
- The generated C, objects and shader corpus stay in the build folder and are reused by the next build;
  a changed optimisation switch recompiles everything in that folder.
