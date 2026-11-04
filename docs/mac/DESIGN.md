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

The Steam copy is encrypted by Valve's SteamStub DRM, which decrypts `__text` in memory at start: a repeating
8-byte XOR. `tools/steamless/decrypt.py` undoes it on the file, with the key it finds in your own binary (nothing
ships, and it prints nothing of it), and checks the result against the pin. The result is the input of every build.
The game is not started, the Steam copy isn't changed, and no Rosetta or Steam account is involved. Earlier
versions dumped `__text` from a game started through Steam; the maintainer chose this route for 1.4.0, as Rosetta is
going away and a first-time user shouldn't have to launch anything. See
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
original lacked and follows the display's refresh rate (`RS_FPS` fixes a rate). `RS_METAL_RETINA=1`
renders at backing-pixel size on a Retina screen.

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
(`.rearmed/steam.env`) to the offline Steam layer, seeds the offline Steam saves once, loads the network
guard and starts the game with `open -n -a`. Nothing of Steam's is read while the game runs, except
`loginusers.vdf` for the account, and without it (Steam uninstalled) the account whose saves are there.

## Renderer

The game draws OpenGL 2.1 with Cg programs (Gamebryo) and GLSL 1.20 (Scaleform). Everything goes through
`libRSOpenGL` (`shims/opengl`), which runs it on Metal by default or on Apple's OpenGL; the Cg shim
(`shims/cg`) translates the ARB programs to GLSL. Design, defaults, fallback and status:
[METAL.md](METAL.md).

## Audio

The game's Wwise engine has a CoreAudio sink and its own guitar input. The audio override module
(`tools/ws7/recomp/overrides/src/audio_*.c`) replaces their CoreAudio side:

- the output device runs with a 256-frame buffer (`RS_AUDIO_FRAMES`; at most half the game's own buffer,
  so 128 at `LatencyBuffer=1`), and the game's ring is split so `LatencyBuffer=1` does not crackle
  (`RS_AUDIO_RING_SPLIT`);
- the Wwise thread is a real-time thread in the output device's audio workgroup (`RS_AUDIO_RT`);
- stale guitar audio queued from the cable is dropped instead of played late (`RS_AUDIO_INPUT_TRIM`);
- devices get stable identifiers, so an interface is not dropped from Direct Connect after another device
  fails to open, and input devices are listed first (`RS_AUDIO_STABLE_UIDS`, `RS_AUDIO_INPUTS_FIRST`).

The game-independent parts (buffer sizes, real-time threads, workgroups, stable UIDs, input-first order)
are in `shims/coreaudio/rs_audio_host.c`, which the Windows port shares. `RS_AUDIO_LOG=1` logs the setup.

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
  ([README](../../README.md#built-in-patches-and-mods)). `RS_MODS_LOG=1` logs what they apply. The song path
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
- **Mods** (`tools/ws7/recomp/app/rc_main.c`): every `.dylib` in `<home>/mods/` is loaded at startup. The
  executable exports the runtime. A mod hooks a game function by name with
  `rc_hook_add_name("game_frame", pre, post, user)`; the hook points are listed in
  `tools/targets/names/hooks.list` and declared in `tools/ws7/recomp/runtime/rc_hook.h`. `rc_hook_add` with
  a game address keeps working; `rc_target()` returns `mac-x64`. Mods that draw use the overlay API
  (`shims/include/rs_overlay.h`), which works on both renderers.
- **Loading-screen fix.** With a community fast-load `cache.psarc`, the game's loading can stall for good
  on the white loading screen when the CPU is busy (a race in the game). With Metal, when `cache.psarc` is
  not the original, the shell compiles the shaders before the game starts loading and then throttles the
  compiler ("safe load", [METAL.md](METAL.md#safe-load)).
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
  as Learn & Play's download does ([Windows DESIGN](../windows/DESIGN.md#installer-and-build)).
- `tools/play-native/install.sh` installs a build: the app and the network guard into
  `~/Library/Application Support/RS Re-ARMed/`, the launcher into `~/Applications` (`launch.sh`, run by the compiled
  `launch-stub.c`, so that macOS holds the app and not the shell responsible for its privacy questions), the renderer
  setting, then `warm-shaders.sh`.
- The generated C, objects and shader corpus stay in the build folder and are reused by the next build;
  a changed optimisation switch recompiles everything in that folder.
