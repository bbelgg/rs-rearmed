# How the Mac port works

The Mac port takes the x86-64 Steam build of Rocksmith 2014 for macOS, which would otherwise need Rosetta,
and makes a native arm64 app of it. The game's code is translated to C at build time by the shared
recompiler ([recompiler](../recompiler/README.md)); the parts of the game that tie it to Intel Macs,
Steam, Ubisoft's services and old system libraries are replaced by native code. User side:
[INSTALL.md](INSTALL.md). Status: [PLAN.md](PLAN.md).

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
The game is not started, the Steam copy isn't changed, and no Rosetta or Steam account is involved. The installer
decrypts on every run, in seconds, so an image kept from an earlier version can't be used by mistake. See
[tools/steamless/README.md](../../tools/steamless/README.md).

## Translation

`tools/ws7/recomp/app/build-m4.sh` drives it: the per-game tables (`targets.py gen`), the override
libraries, the translation (`build/build.sh` with the override skip lists), the compile, the shader corpus
and the link. The 64-bit front end and its switches (memory-order fences, relaxed functions, `-O1` list):
[recompiler](../recompiler/README.md#the-64-bit-front-end-mac).

## Per-game tables

`tools/targets/mac-x64` holds the image's identity (SHA-256), and `symbols.map`, `offsets.map`, `drops.map`,
`edits.map` and `mids.map`: every function, global and struct field the native code uses, by name. The override,
seam and app sources use names only. See [tools/targets/README.md](../../tools/targets/README.md).

## Runtime

`tools/ws7/recomp/runtime`: the context and call machinery (`rc.h`, `rc_call.h`), indirect-call lookup,
trampolines between native code and translated code (`rc_tramp`, `rc_native`), `setjmp`/`longjmp` across
guest frames (`rc_setjmp`), threads (`rc_thread`), Objective-C message sends (`rc_objc`), varargs
(`rc_va`), the hook points and mid points (`rc_hook`, `rc_mid`), the mods folder (`rc_mods`) and the files a mod
serves (`rc_file`).

## Platform

`shims/platform` rewrites the game's Cocoa shell in native code: the app and its window, the view, the
display link and render thread, input, file paths and `Rocksmith.ini` (`rsp_shell.m`, `rsp_paths.c`,
`rsp_config.c`). The engine sits behind function-pointer hooks; `tools/ws7/recomp/app/seam_platform.c`
connects them to the translated `GameBootstrap` and `GameFrame`. The shell takes the folder that holds the
`.app` as the game root, so the game reads only RS Re-ARMed's own copy of the game files
([Installer and build](#installer-and-build)).

The launcher (`tools/play-native/launch.sh`, run in the `~/Applications/RS Re-ARMed.app` bundle by `launch-stub.c`)
checks the copy is complete (telling a drive that is not connected from an incomplete copy), links `static.psarc` to the
title logo or the original, keeps the game's `Rocksmith.ini` offline ([Online services](#online-services)), passes the
built-in mods switched off in `settings.env` (only its nine switches; a value in the environment wins), seeds the
offline Steam saves once, and starts the game with the network guard inserted (`open -n -a`). Nothing of Steam's is read
while the game runs, except `loginusers.vdf` for the account, and without it (Steam uninstalled) the account whose saves
are there.

### Screen and window

The shell follows the display's refresh rate (`RS_FPS` fixes a rate; its rule and clock are
`shims/platform/include/rsp_fps.h`, which Learn & Play uses too). On a Retina screen the game renders in
points and each frame is scaled up to the screen's pixels with MetalFX (`rs_gfx_set_output_size`,
[METAL.md](METAL.md#metal-core-shimsopenglsrcmetal)); `RS_METAL_RETINA=1` renders at backing-pixel size instead.
A window takes any shape, where the original kept it at 16:9 (`RS_WINDOW_ANY_SHAPE=0` does too): the game sets
its cameras from its resolution and lays out its menus to the width, so it needs nothing more
([INSTALL.md](INSTALL.md#window-modes)). The screen saver and display sleep stay off while the game is open, in
a window or full screen: the view holds a display-sleep assertion for its whole life, as the original's does.

## Renderer

The game draws OpenGL 2.1 with Cg programs (Gamebryo) and GLSL 1.20 (Scaleform). Everything goes through
`libRSOpenGL` (`shims/opengl`), which runs it on Metal by default or on Apple's OpenGL; the Cg shim
(`shims/cg`) translates the ARB programs to GLSL. Design, defaults, fallback and status:
[METAL.md](METAL.md).

### Shader corpus

Made at build time from the game files and compiled by the install's warm-up: [METAL.md](METAL.md#shader-translation) and
[METAL.md](METAL.md#safe-load). Every pair the shaders could form is in it. Learn & Play's shaders pair too freely for that:
[its corpus](../windows/DESIGN.md#shader-corpus) has the programs seen in runs.

## Libraries

The game's statically linked libraries are replaced by native builds of the same code (override modules
`zlib`, `png`, `jpeg`, `lua`, `crypto`, `efd`, `dsp`; sources in `shims/game` and
`tools/ws7/recomp/overrides/src`). `shims/libstdcxx` provides the link stub for the system's
`libstdc++.6.dylib`, which the game links. `shims/game` has a differential harness that runs the original
x86-64 functions under Rosetta against the rewrites ([TESTING.md](TESTING.md)).

## Audio

The game's Wwise engine has a CoreAudio sink and its own guitar input. The audio override module
(`tools/ws7/recomp/overrides/src/audio_*.c`) replaces their CoreAudio side:

- **Output.** The device runs with a 256-frame buffer (`RS_AUDIO_FRAMES`; at most half the game's own buffer,
  so 128 at `LatencyBuffer=1`, and 128 on an interface that also has inputs, whose buffer the guitar input
  shares and opens at 128), and the game's ring is split so `LatencyBuffer=1` does not crackle
  (`RS_AUDIO_RING_SPLIT`). The Wwise thread is a real-time thread in the output device's audio workgroup
  (`RS_AUDIO_RT`). The game opens its sink once and never moves it, so when the default output changes the
  override stops the game's audio graph, gives its output unit the new device and starts it again.
- **Input buffers.** Stale guitar audio queued from the cable is dropped instead of played late
  (`RS_AUDIO_INPUT_TRIM`). A smaller buffer set on the guitar's device by another program, such as `rs-tonehost`
  at 64 frames, is collected into whole 128-frame slots before the game's input takes it (`audio_chan.c`).
- **The guitar input's channel and gain.** The game's I/O function handles one channel, or picks one of two; of
  a device with more it queues the start of the buffer as it is, a mix-up of channels. A hook (`si_io`,
  `audio_chan.c`) gives it a mono buffer of the chosen channel with the gain instead, and the game's mono path
  does the rest. Settings: `guitar-input.ini` ([INSTALL.md](INSTALL.md#guitar-input)).
- **The list of inputs.** Each channel of an interface is an input of its own (`shims/coreaudio/rs_audio_input.c`,
  `audio_devs.c`). The game opens a device per slot, 10 at most, so a channel is a device to it: the list it reads has
  ids that hold the real device and the channel, in priority order, and the import thunks of the calls that take a
  device reach the real one.
- **An input's level.** Calibration raises the level in small steps until its meter reaches the top. It reaches
  a device through `volm`, which most interfaces lack (so it never finished) and which on the others is the
  player's own setting. So every input but the cable reports a volume of ours (`audio_devs.c`), and the level is
  a gain in the `si_io` hook: 0.375, where the game starts a device, is unity, and 1 is +24 dB. A level saved
  before counts for nothing until calibration has run once (`input-level-calibrated` in the folder keeps that).
  The cable keeps its hardware level and the game's own path.
- **Device identifiers.** Devices get stable identifiers, so an interface is not dropped from Direct Connect after
  another device fails to open, and input devices are listed first (`RS_AUDIO_STABLE_UIDS`; `RS_AUDIO_INPUTS_FIRST`
  applies with the list of inputs off, as the list has its own order). A device whose identifier can't be read is
  skipped (`audio_devs.c`).

The game-independent parts (buffer sizes, real-time threads, workgroups, stable UIDs, input-first order, the input's
settings and gain) are in `shims/coreaudio/rs_audio_host.c` and `rs_audio_input.c`, which Learn & Play shares
([Windows design](../windows/DESIGN.md#audio)). `RS_AUDIO_LOG=1` logs the setup.

### Your own plug-in

rearmed-mods' ToneHost add-on plays the guitar through an Audio Unit in place of the game's tones, with a host of
its own beside the game. Its mod is the game's side: on a tone change the game sets each knob of the gear as a
Wwise RTPC by name, ahead of the change, and the mod passes them to the host (the hook point `ak_set_rtpc_name`)
and keeps `Mixer_Player1` at 0. The one change here is the 64-frame collection in `audio_chan.c`
([Audio](#audio)). The game's own tones don't get faster: Wwise's 256-frame blocks set their latency.

## Video (Bink)

The game plays its videos with Bink 2 (`libBink2Macx64.dylib`). `shims/bink` replaces it with a decoder on
a minimal static FFmpeg 7.1.2 build (downloaded and checksum-verified at build time).

## Steam

`shims/steam` is a drop-in `libsteam_api.dylib`. It runs in stub mode, which the launcher sets and the app defaults to:
no Steam client, Steam Cloud as a local folder (`steam/<account>/221680/remote`, seeded once from your Steam saves
without `crd`), the identity from `loginusers.vdf`, the base game, the installed DLC and 248750 (the DLC custom songs
carry) as owned. See [shims/steam/README.md](../../shims/steam/README.md). Learn & Play has the same behaviour from the
same code ([Windows design](../windows/DESIGN.md#steam)).

The game mounts a `dlc/` archive only when the AppID in its `appid.appid` is a DLC Steam lists and reports installed, so
custom songs that carry another AppID (or 248750, for an account without it) would not show.
`tools/play-native/gamefiles.py` reads the AppID of every archive in the copy's `dlc/` at each copy and refresh and adds
them to `RS_STEAM_DLC` in `game/.rearmed/steam.env`. Done at install rather than in the Steam layer: no psarc reading in
the game process, and one implementation for both editions.

The songs of `rs1compatibilitydlc` need more. The archive comes with the game and carries the base AppID, but each song
names the DLC of the first Rocksmith it came from, and the game asks `BIsSubscribedApp` for each and hides the songs of
those not owned. Steam installs nothing for these, so nothing on disk says who owns them: a copy or refresh asks the
Steam client, when the edition's account is the one logged in (`rs1_owned` in `gamefiles.py`, a process of its own
through Valve's library, gone before the game starts). The answer goes into `RS_STEAM_OWNED` in `steam.env`, which the
Steam layer reports through `BIsSubscribedApp` only: as with real Steam, these are no DLC of this game.
`.rearmed/rs1-owned.json` keeps it for a refresh with Steam closed or on another account.

## Online services

The game never reaches the network. Three layers see to it: the offline Steam layer ([Steam](#steam)), the online stub
and the network guard.

**The online stub.** The game's Rendez-Vous/Uplay layer is replaced by an offline one (the `online` override module:
`shims/game/src/online_stub.cpp`, `tools/ws7/recomp/overrides/src/online_glue.c`): no login is started, Enter on the
title screen goes straight to the profile list, and requests get the answers the game gives when Ubisoft's servers are
unreachable. `crd` is never read.

**The network guard.** `tools/ws8/netguard` is inserted into the game at every launch (the launcher refuses to start
without it). It refuses every connection and datagram that is not local (`connect`, `connectx`, `sendto`, `sendmsg`; an
address shorter than its family's counts as not local), every host-name lookup but `localhost`'s (`gethostbyname`,
`gethostbyname2`, `getipnodebyname`, `getaddrinfo`; the reverse lookups `gethostbyaddr` and `getnameinfo` for an address
that is not this machine's) and every attempt to open a web page (`system` and `popen` of a command with a URL of any
scheme, a `www.` address or the `open` command), and logs each to `logs/netguard.log`. It does not cover the resolver
library, dns_sd or CFHost, which nothing of the game or the port calls; an address from them would still be refused at
the connection.

A start that is not the launcher's (the game's app opened from the Finder or a debugger) has no guard inserted, as dyld
inserts only at a start: the app then starts itself again with the install's `libnetguard.dylib` inserted
(`netguard_start` in `rc_main.c`), and does not run when the guard still isn't in the process. A build with no install's
guard to find (a build folder's tests) says so in its log and runs with the other two layers. Learn & Play starts again
with the same guard when it has mods to load ([Windows design](../windows/DESIGN.md#online-services)).

**What the launcher passes.** The launcher gives the game four keys of `game/.rearmed/steam.env` and no others
(`RS_STEAM_DLC`, `RS_STEAM_OWNED`, `RS_STEAM_LANGUAGE`, `RS_STEAM_ID`): the file never chooses the Steam layer's mode,
folder or library, or keeps the stored credentials. The game's `Rocksmith.ini` is the one it saved itself (the first
time, the copy of the install's), with `[Net] AutoLogin=0` and `[Renderer.MacOS] DisableBrowser=1` forced at every
launch, also in a file that lacks the key or its section; with no file at all the launcher writes one with just those.

## Input

The shell (`rsp_shell.m`) hands key, pointer and focus events to the engine through the game's own input
function and `SetAppFocus` (the hook points `input_sink` and `set_app_focus`), and adds the keyboard keys the
original lacked ([INSTALL.md](INSTALL.md#keys)). Learn & Play's input is a layer of its own
([Windows design](../windows/DESIGN.md#input)).

## Built-in fixes and mods

Each fix is a native replacement of a game function or a hook on one, with a switch
([INSTALL.md](INSTALL.md#settings-and-switches)). `RS_MODS_LOG=1` logs what they apply, as `[rs-mods] ` lines in the
edition's log. The sources are in `tools/ws7/recomp/overrides/mods`:

- **One source for both editions.** A fix that needs nothing of one game alone is written as a mod is (`rc_mod.h`): it
  names the game's functions and fields and looks them up when it runs, so the same file is built into both apps.
  `mods_shared.c` has the switches, the log, the start (`rs_mods_start`: it reads the switches, raises the open-file
  limit and adds the hooks), the intro skip, the song ownership during a mount, what the two batched DLC mountings
  share (the song manifests, the loader thread's pause, a step's file questions) and Player 2's saved input;
  `framerate.c` has the frame rate fixes. This edition starts them from a constructor, as the launcher has set the
  environment by then.
- **Each edition's own.** The batched DLC mounting loop, the Auto Save skip, the Direct Connect overlay's file open, the
  path cache, the number hash and the signature skip sit where the two games' code differs. This edition's are in
  `mods.c`, with two only it needs (the keys and the thread priority): a function named in
  `tools/targets/names/overrides/mods.list` is left out of the translation and defined there, and the rest are pre hooks
  on hook points. Learn & Play's, and why each differs: [Windows design](../windows/DESIGN.md#built-in-fixes-and-mods).

What each does:

- **Built-in fixes** (`RS_MODS_FIXES`, `RS_MODS_BYPASS`): the rs-mac-fixes behaviours and the RSBypass signature skip.
  - Batched DLC mounting, the faster loading with a large library: the DLC coordinator's update runs its install step,
    which mounts one archive, for `RS_MODS_BUDGET_MS` a frame (default 50) instead of once. A step is a third of what
    puts an archive's songs in the lists: it opens the archive, asks for its song manifest and queues its package; the
    update's head, which the game calls once a frame, loads one package a call; and the asset loader thread reads the
    manifest, which the game takes in on its own thread. The menu's "Enumerating downloadable content..." and the
    song lists' filling in follow one flag, which the step that finds the list empty clears. So the loop does four
    things beside the steps:
    - it calls the head until nothing is left to load before the next step, so a package is loaded before the next
      archive is opened, several a frame;
    - while archives are mounted, the loader thread's pause between passes is 0.2 ms, not the game's 10 ms (the
      thread's loop is replaced; a pass ends at the first file it finishes, so the game's pause reads 85 manifests
      a second at the most);
    - if manifests asked for are not yet answered when the list is empty (two mid points count them), the flag is
      set again and held until they are, or until none has come for 5 s;
    - it keeps a step from growing with the library. Mounting an archive, the game asks its file system for the
      hash file of each of the ten shaders in its shader cache (`data/Shaders/Generated/NAME.sha1`), should the
      archive bring a newer one, and the file system asks every archive mounted there in turn: 2 ms a step with a
      hundred archives mounted, 19 ms with 2,300. Within one mounting, a question the step before had "no" for is
      asked of the archive this step mounted alone, by the game's own call on it (two more mid points, `fs_mount`
      and `fs_exists`); a "yes" goes to the whole file system after all, and at the mounting's end every question
      is asked of the whole file system once more, a different answer switching this off for the run.

    Steps alone left packages and manifests behind: the text went with songs still coming into the lists and the
    menus slow to answer, for 2.7 s with 147 archives and for 33 s with 2,326 (1.16.0: the text for 24 s, and a
    search 6 s after it had 1,227 matches of 2,037). Now the text and the last song end together, after 5 to 8 s
    with 2,326. The first of the four is this edition's own (`mods.c`: the packages the game has left to load,
    `dlc_package` and `dlc_package_queue`), and so is the replaced loop of the loader thread; the other three are
    one source with Learn & Play (`mods_shared.c`), whose loop waits for each mount in its own way, there because a
    step taken too early drops an archive ([Windows design](../windows/DESIGN.md#built-in-fixes-and-mods)). This
    game's steps drop none either way ([TESTING.md](TESTING.md#unattended-runs)).
  - The open-file limit, which the batched mounting needs: a large library has more archives open than the 256
    descriptors of an app started from Finder. The shared start raises it.
  - The song path cache: every mounted archive's file checks take the MD5 of the same paths again, under the path
    table's lock. The fix keeps the objects of 1,024 recent paths.
  - The number-table hash: the game hashes a double by its integer part, so every value in [0, 1) lands in one bucket.
    The fix hashes the value's bits.
  - The RSBypass signature skip: the song loader's call of the DSA verifier answers true, so unsigned custom songs load.
    The verifier's other caller keeps the real check.
  - This edition only: the keyboard keys the game's character table lacks and the End key in menus; and worker-thread
    priority, where macOS takes only the two of the game's values that put a thread at the bottom of the scheduler, so a
    request below the default gets the default.
- **No intro screens** (`RS_MODS_INTRO=0` plays the intro; shared, a pre hook on `boot_intro_update`): the boot service
  ticks the intro movie (logos, legal screens, intro video) in its boot phases 2 and 3 and shows the title only once the
  intro has ended. The movie's model is loaded as usual, but the movie is never started: the tick reports "not busy"
  until phase 3 has done its steps (archives mounted, venues), and then the intro takes the transition of the game's own
  skip key. Both parts matter: without the model loaded, or with the intro ended before phase 3's steps, the title
  screen never gets its menu movie.
- **Auto Save warning skipped** (`RS_MODS_AUTOSAVE=0` shows the dialog; a pre hook on `autosave_warning`): on
  `OnSignInFlowComplete` the game's UI event handler restores the player's input mode, shows `RS2AutosaveWarning` and
  starts a 4 s timer. The hook does the first and leaves out the rest: it stores a token in the handler's dialog slot
  and a zero in the timer, so the next tick goes on, as it does when the dialog's time is up. A NULL slot would stop the
  flow on the profile screen.
- **Two players on any input** (`RS_MODS_MULTIPLAYER=0` is the original). Two rules of the game's made two players need
  two cables. The Multiplayer screens ask whether Player 2 is in Real Tone mode and set them to it when not; here a
  profile on Direct Connect or the microphone counts as in the right mode (a pre hook on `input_mode_is_cable`, this
  edition's own). And a saved input is restored for Player 1 alone, while Player 2 takes the first free slot; a shared
  pre hook on `input_bind` finds Player 2's saved input first. It has to be that early: moving the input of a running
  stream crashes the I/O thread. The menu overlay rewords the notice MULTIPLAYER INPUT SUPPORT, which said Player 2 must
  use the cable.
- **The same at any frame rate** (`RS_MODS_FRAME_RATE=0` is the original; shared: `framerate.c`, hooks at eight mid
  points, `fps_*`, with each game's places in its `mids.map`). The game's frame time is whole milliseconds with no lower
  bound, and some of its code counts frames, which goes wrong above 60 a second. Calibration sizes its averaging window
  as the frames of one second over buffers of 100 samples a player, so above 100 frames a second player 1's samples run
  into player 2's, and it moves the input level one step a frame, so its loop runs faster with the frame rate. The tuner
  gives a string that drifts two frames before its hold loses ground, and averages levels and notes one a frame for its
  volume and wrong instrument warnings. A menu panel's hold takes at least 1/15 s off its timer each frame. The hooks
  put each right by the frame's own time (`framerate.h` has the arithmetic), and only for a frame shorter than a 60 Hz
  one (under 14.5 ms): at 60 Hz and below the game runs as it always has.
- **Skip Tuner in the Guitarcade** (`RS_MODS_GUITARCADE_SKIP=0` is the original; shared: a hook at the mid point
  `tuner_skip_offer`, with each game's place in its `mids.map`). Where the tuner menu sets its buttons up, it offers
  Skip Tuner unless the stage its `OnComplete` names is `Guitarcade_Game`: that one test kept the button from a
  Guitarcade game's tuner, which left Cancel and Calibrate, and Calibrate comes back to the tuner. The skip itself is
  the menu's own whatever the stage: the same warning, then the step on that a finished tuning takes. So the hook only
  makes the test's answer yes. A player whose tuner would not finish could play no game but Gone Wailin', which has no
  tuner.
- **Loading-screen fix.** With a community fast-load `cache.psarc`, the game's loading can stall for good
  on the white loading screen when the CPU is busy (a race in the game). With Metal, when `cache.psarc` is
  not the original, the shell compiles the shaders before the game starts loading and then throttles the
  compiler ("safe load", [METAL.md](METAL.md#safe-load)).
- **Direct Connect choice** (`RS_MODS_OVERLAY=0` reads `cache.psarc` alone). The game code handles a fourth
  button on the cable screen, `othercable` (Direct Connect), and a fourth value of the INPUT slider, but the menu
  data (two JSON files in one of `cache.psarc`'s `cacheN.7z` archives) leaves them out. At each copy or refresh
  of the game files, `tools/play-native/cache_overlay.py` adds them to the user's own copies of the two files and
  stores the rewritten archive in `game/Update/patcharchives.psarc`, the patch container the engine mounts at
  `PatchUpdate/` when it is there. The tools read and write the archives' tables with the game's own key, found
  in the game's program (`tools/play-native/psarc_key.py`). The engine's patch layer takes only its own
  `.new`/`.delta` archives from the container, so a pre hook on `frontend_open_file` opens
  `PatchUpdate/cacheN.7z` first and the `cache.psarc` one when the overlay has none. `cache.psarc` is never
  changed, so a modded cache still works; one that has the entries gets no overlay.
- **Exit Game.** The main menu's UPLAY, which can't work here, becomes EXIT GAME in the same overlay
  (`ui_menu_pillar_main`: the label, a row the game has in every language, and the `OnSelect`, `ExitGame`). The
  game asks with its own dialog, then quits as it does on Cmd+Q.
- **Enumerate.** The main menu's SHOP becomes ENUMERATE in the same overlay (the label and sort orders only; its
  `OnSelect` still opens the Shop). Before it opens the Shop the game asks `ISteamUtils::IsOverlayEnabled`, and it asks
  nowhere else. The answer here is no, so the Shop never opens: the game stays in the menu and shows a notice, which the
  overlay rewords. That question is the press. The Steam layer (`shims/steam/src/enumerate.h`, shared with Learn & Play)
  copies new archives on a thread of its own from the folders in `.rearmed/source.json` into the copy's `dlc/`, then
  posts `DlcInstalled_t` for 248750, whose listener asks the DLC service to enumerate, and the game mounts what it
  finds. Only new names are written, so nothing the game has open is touched; `.rearmed/enumerated.txt` names them, so a
  refresh takes them into the manifest and `steam.env`, or removes one the source no longer has.
- **Song ownership during a mount** (shared, a pre hook on `steam_dlc_owned`). The game checks an archive's AppID
  against the list it made once at start from `GetDLCCount`, so `RS_STEAM_DLC` can't answer for a song added later.
  While the install steps run the hook answers yes (each edition's mounting loop says when they do): an archive in
  `dlc/` counts as owned, whatever AppID it carries. `RS_MODS_BYPASS=0` asks the list again, and such a song then needs
  a refresh and a restart.
- **Title logo.** `tools/loadscreen` builds `static.psarc.rearmed` in the copy (`install.sh`; never in Steam's folder)
  from the copy's `static.psarc.original` with a "Re-ARMed" tag on the title logo; the launcher uses it when it is there
  (`RS_REARMED=0` for the original). It isn't in the copy's manifest, so a refresh leaves it, except that a new
  `static.psarc` removes it.
- **Mods** (`tools/ws7/recomp/runtime/rc_mods.c`, shared): every `.dylib` in `<home>/mods/` is loaded at startup
  (`RS_MODS_PATH` names another folder or one library, `RS_LOAD_MODS=0` loads none). The executable exports the
  runtime, and `tools/ws7/recomp/runtime/rc_mod.h` is what a mod includes: one source and one arm64 dylib for
  this edition and Learn & Play ([recompiler](../recompiler/README.md#overrides-and-hook-points)). What a mod
  has:
  - **Hook points.** `rc_hook_add_name("game_frame", pre, post, user)` hooks a game function by name. The 21 hook points
    are listed in `tools/targets/names/hooks.list`; all are functions of this game (Learn & Play has 19 of them).
    `rc_hook_add` with a game address keeps working; `rc_target()` returns `mac-x64`.
  - **Edits and mid points** (`rc_mid.h`), for a change inside a function. The tables (`tools/targets/mac-x64/edits.map`
    and `mids.map`) have two groups, both for rearmed-mods: `seven_string`, its Seven-String Guitar (618 edits and 122
    mid points), and `plain_packages`, its Plain Packages (2 edits and a mid point). Eight more mid points, `fps_*`, are
    the built-in frame rate fixes'. Learn & Play has 735 edits: many of this game's six-string loops are unrolled, so
    where Learn & Play's edits lengthen a loop, 29 mid points of this edition's own (`none` in Learn & Play) do the
    extra strings. The 34 points only Learn & Play needs are `none` here.
  - **Files** (`rc_file_provide`, `rc_file.h`): a mod can serve files to the game. The game's file imports (`open`,
    `read`, `stat`, `fopen`, `opendir` and the rest) are bound to the runtime's `rc_file_*`, which ask the mod's
    provider first; the same interface as Learn & Play's.
  - **Drawing.** The overlay API (`shims/include/rs_overlay.h`) works on both renderers; a mod that changes a
    game texture uses the render API (`shims/include/rs_render.h`, Metal alone). Both: [METAL.md](METAL.md).

  A mod built for 1.4.x, without `rc_mod.h`, keeps working here as it was, and is left out on Learn & Play. The
  rearmed-mods library (`rearmed-mods.dylib`, from 0.3.0) is built with it: its names are under "the mods
  library" in `tools/targets/names`.

## Installer and build

```
choose the game ─┬─ Rocksmith 2014 for Mac → the game in Steam, else Steam downloads the depot and the DLC
                 │                            → static decrypt → shims, app → install → warm shaders → launcher
                 └─ Learn & Play → free space → Steam downloads the depot and the DLC → game-copy → Microsoft DLLs
                                    → code image → lp_install.py build → app-install → build-clean → warm shaders
```

- `tools/installer/RSReARMedInstaller.swift` (built by `tools/installer/build.sh`) is the GUI for both editions. For the
  Mac game it runs the manual steps of [INSTALL.md](INSTALL.md) with a progress bar: prerequisites, the Python
  environment, the game image (`decrypt.py`), the shims, the app, the install and the shader warm-up. For Learn & Play
  it runs [that edition's steps](../windows/DESIGN.md#installer-and-build). What the user has to do is shown in a box
  above the log, from the `guide` lines of the step that waits. Its Refresh Game Files button runs the installed
  `gamefiles.py refresh`. Its Settings and switches… sheet writes the chosen edition's `settings.env`
  (the built-in mods switched off, no file when every mod is on) and `options.env` (every other switch set), which
  that edition's launcher reads at every start; `tools/play-native/test_settings.py` checks
  that the installer and both launchers have the same keys. The standalone installer builds only a download that is,
  file for file, the source it was made from: `build.sh` builds in a digest of the source, and the app works it out over
  what it unpacked before anything runs.
- **The game from Steam's console** (`tools/play-native/gamefiles.py`, shared with Learn & Play). Neither edition needs
  its game installed in Steam. `mac-game` takes, in this order, the folder the user names, the game installed in a Steam
  library (the appmanifest says "installed"), and Steam's download of depot 221682, which it starts when the game is
  nowhere; a game Steam is installing or updating right then is waited for instead.
  - The download: `steam_osx +download_depot` with the app, depot and manifest of the edition's `target.toml` hands the
    running Steam the command; only if Steam ignores it for 30 seconds does `steam://open/console` open with the command
    on the clipboard. Steam's state is read from disk (`SteamGate`), and what is left for the user (install Steam, sign
    in, the right account) comes out as `guide` lines. The installer never handles credentials. steamcmd is not used: it
    is an Intel binary.
  - Room: Steam makes every file at full size first, so the whole depot must be free up front (`mac-game-space`); when
    it isn't, Steam's `steamapps/content` is linked to another disk (`mac-game-redirect`). The game's program is checked
    against the SHA-256 in `target.toml`.
  - The DLC (`dlc-fetch`): a downloaded game has none with it. Each DLC is a depot of its own, with the AppID as its id
    and both the `_m` and the `_p` archive. The DLC the account owns are read from the app ownership ticket in Steam's
    `localconfig.vdf`, and Steam fetches them one after another with the same command. A state file remembers what is
    done, and one that fails is named and doesn't stop the install.
  - A downloaded game has no appmanifest either: the account is the one whose license has depot 221682, custom songs
    come from a Steam library's `common/Rocksmith2014/dlc`, the language is the Steam layer's default, and the saves are
    there only when the game has run through Steam on this Mac. A refresh of such a copy fetches new DLC first, and
    works with Steam's download folder cleared.
- **The game files** (`tools/play-native/gamefiles.py`, shared with Learn & Play): the install is
  self-contained, with RS Re-ARMed's own copy of the game files next to the installed app: APFS clones, which
  share the data with Steam's files until one side changes, when both are on one volume, else a real copy.
  `.rearmed/manifest.json`, written last, marks the copy complete; a refresh copies what changed and removes
  only files of the earlier copy. A game on another APFS drive is copied on that drive instead, as clones
  (`<drive>/RS Re-ARMed/game`, Learn & Play's `…/learn-play/game`), and the edition's `game` path is a symbolic
  link to it, so the launchers and a refresh use the same path; the copy's `.rearmed/link.json` names that link,
  and a second install does not take it over.
- `tools/play-native/install.sh` installs a build: the app and the network guard into
  `~/Library/Application Support/RS Re-ARMed/`, the launcher into `~/Applications` (`launch.sh`, run by the
  compiled `launch-stub.c`, so that macOS holds the app and not the shell responsible for its privacy
  questions), the renderer setting, then `warm-shaders.sh`.
- The generated C, objects and shader corpus stay in the build folder and are reused by the next build;
  a changed optimisation switch recompiles everything in that folder.
