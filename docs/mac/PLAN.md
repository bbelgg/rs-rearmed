# Mac port: status and plan

The Windows port's plan: [Windows plan](../windows/PLAN.md).

## Status

- **Released**: `VERSION` says which release this is. 1.4.0 is the first with the Windows (Learn & Play)
  port.
- **What works**: the whole game as a native arm64 app, offline: songs, Score Attack, DLC and custom songs, Session
  Mode, lessons with their videos, Guitarcade; the Real Tone cable, the microphone and other audio interfaces (Direct
  Connect) with lower latency than under Rosetta; Metal by default with OpenGL as choice and fallback;
  full screen, Retina; the built-in fixes; mods loaded at startup with name-keyed hook points; the GUI
  installer. Gameplay CPU is about that of the original under Rosetta ([PERFORMANCE.md](PERFORMANCE.md)).
- **Score Attack, the Guitarcade and the microphone** (2026-11-05): work, by the maintainer's own check.
- **Guitar input channel and gain** (new in 1.6.0; 2026-11-05, a test home, started directly): an HX Stomp, 8 channels, the dry guitar
  on channel 5 at 0 dB, through Direct Connect and the game's calibration, heard right by the maintainer; peaks up to
  -14.1 dBFS, nothing clipped. Before, the game opened it with its 8 channels mixed up. The UR22 and the other
  inputs opened as before. Not in this pass: a gain above 0 dB in a run, an install by the installer app.
- **Checked for 1.5.1** (2026-11-05, the app from `install.sh` over an existing install): the default output changed in Sound settings during
  a song and the sound moved, heard by the maintainer. All suites of [TESTING.md](TESTING.md) pass. Not in this pass: an install by the
  installer app, a guitar.
- **Checked for 1.5.0** (2026-11-05, a build by hand and one by the installer with the source given by `RS_SOURCE_URL`, a test home, by
  keys and snapshots): the image matches the pin; a song to the note highway and out, with a mod and without, no signal, nothing
  refused by the network guard; the shader warm-up. The game was started directly with the launcher's environment, not by `open`.
- **Checked for 1.4.0** (2026-11-04, the app the installer built, by keys and snapshots): the title with the logo and no intro
  screens; DIRECT CONNECT offered, and a guitar through a UR22mkII heard by the maintainer; official and custom songs, a video lesson,
  Session Mode and Nonstop Play; Enumerate and Refresh Game Files; OpenGL boots and plays a song; Quit and Exit gone in 0.5 s or
  less. The figures are in [PERFORMANCE.md](PERFORMANCE.md).
- **Every channel in the input list, and calibration that can boost** (new in 1.6.0; 2026-11-05, by
  the maintainer, a test install): an HX Stomp's 8 channels and a UR22's 2 listed, all heard, a choice kept over a restart;
  calibration settled at +5.6 dB with the meter at the top. Not in this pass: the Real Tone cable with this build.
- **RSMods' library** (2026-11-05): the same `rearmed-mods.dylib` 0.3.0 as Learn & Play's, all its mods on, a test home, to the note highway and
  a quit ([Fixed in 1.5.1](#fixed-in-151)).
- **Mods on both editions**: a mod built with `rc_mod.h` is one dylib for this app and Learn & Play ([Windows plan](../windows/PLAN.md#status)).
  This edition's own mod interface is as it was.
- **Rocksmith 1 songs**: the disc pack's and the DLC pack's are listed and played; the DLC pack's need a refresh with
  the owning account logged in to Steam, since only the Steam client knows which of them it owns.
- **Installer**: the same guide box and Steam steps as Learn & Play. Decrypting is static (`tools/steamless/decrypt.py`):
  no Rosetta, no launch of the game, and Steam only has to have installed it, whichever account is signed in now.
- **Enumerate** (both editions): SHOP is ENUMERATE, second in the main menu, and brings new songs in
  without a restart ([DESIGN.md](DESIGN.md#built-in-fixes-and-mods)). Changed and removed songs still need a refresh.
- **Full-screen shape**: on a 16:10 screen the 16:9 picture is stretched, as in the original under Rosetta. The Windows
  game's bars are its own code, which the Mac game does not have.
- **Checked on**: one Mac (M2) with a 75 Hz external display and the built-in Retina screen
  ([METAL.md](METAL.md#status) lists what the 1.3.0 release check covered).
- **Auto Save warning skipped** (new in 1.7.0; 2026-11-06, a test install, by keys and snapshots): after profile select the game
  goes straight on without the autosave warning dialog (`RS_MODS_AUTOSAVE`), and a Direct Connect profile keeps its input: a
  UR22mkII to the note highway. Learn & Play skips it too ([Windows plan](../windows/PLAN.md#status)). Not in this pass: a Real
  Tone cable or Microphone profile, an install by the installer app.

- **CPU in a song** (2026-11-06, a test home, started directly): 6.1 ms a frame on [Learn & Play's route](PERFORMANCE.md#learn--plays-route), 45% of a core, measured
  for the first time, against its 7.2 ms. 12 more functions are relaxed (the note detector's FFT, Wwise mixes, filters and delay network), and the Metal layer reuses one pass
  descriptor and takes no references in its hot lookups: 0.6 points of a core each, within the noise of one run. Played with a guitar on this build, installed by `install.sh`, by the maintainer's own check. Not in this pass: an
  install by the installer app. Learn & Play has its own relaxed list by address, and the same renderer: its host test passes, its route was not run with it.

## Open issues

- **The title screen's own Auto Save call**: in Learn & Play the title has a second call of the dialog, behind the same sign-in event and a byte no
  tried route sets ([Windows plan](../windows/PLAN.md#status)). This game's counterpart has not been read.

- The list of inputs and the level, not checked yet: unplugging an interface while the game runs (the game's property listeners get the real device, not a channel's id), a Real Tone cable with this
  build, and the level with a second profile, which counts as calibrated with the first.
- With the legacy fast-load `cache.psarc`, a start can stay on the white loading screen now and then (a
  race in the game). Metal's safe load avoids the stall shader compiles would add; on OpenGL quit and start
  again.
- The first play of a song after an install stutters briefly on Metal (pipelines built on first use).
- Mods that draw with OpenGL show nothing on Metal; they need the overlay API.
- Renderer details: `RS_GL_CHECK` keeps one pending error per thread, not per context;
  `GL_TEXTURE_BASE_LEVEL` and `GL_TEXTURE_LOD_BIAS` are ignored on Metal; shared CGL contexts are not
  supported on Metal; only the first GPU gets a disk cache.
- Not checked yet: a second Mac or GPU, 120 Hz ProMotion displays, sleep and wake, unplugging a display, the
  Retina mouse in songs, and a fresh player's first answer to macOS's
  question about another disk with the compiled launcher (an earlier Allow covered the one run).
- The installer keeps its preferences in the user's defaults even when run with a test `HOME`
  ([TESTING.md](TESTING.md#installer-tests)).

## Built in for 1.4.0

With the Learn & Play release ([Windows plan](../windows/PLAN.md#built-in-for-140)), both editions have these built in;
how each works is in [DESIGN.md](DESIGN.md#built-in-fixes-and-mods):

- **Direct Connect**: a value of the INPUT setting, and a button on the cable screen (not checked), from an overlay
  archive made at install from the user's own `cache.psarc`, which stays as it is on disk.
- **No intro screens**: skipped in code, with no game asset changed (`RS_MODS_INTRO`).
- **Custom songs whatever their AppID**: the install lists the AppID of every archive in the copy's `dlc/` in
  `steam.env`'s `RS_STEAM_DLC`, at each copy and refresh. The Windows game's table of known song keys has no
  counterpart here.
- **Title logo, open-file limit, song path cache and number-table hash**: unchanged here, and now in Learn & Play too.

## Fixed in 1.4.1

A patch release of what was found after 1.4.0, the same in the [Windows plan](../windows/PLAN.md#fixed-in-141):

- **Rocksmith 1 DLC songs** show for the account that owns them (`RS_STEAM_OWNED`, from a copy or refresh with that
  account logged in to Steam).
- **Guitar on an interface also used for output**: a device with inputs keeps a 128-frame buffer, so a sink rebuilt
  after the input opens can't leave the guitar with half of each buffer.
- **Installer**: closing its window quits without a crash.

## Built in for 1.5.0

Nothing here is meant to change how this edition plays. What changed is shared with Learn & Play, whose news are in the
[Windows plan](../windows/PLAN.md#built-in-for-150):

- **One mod binary for both editions**: a mod built with `rc_mod.h` loads in either app. This edition's mod interface is as it was.
- **Shader warm-up**: the loop moved to libRSOpenGL, unchanged, and the corpus made by the same scripts is byte for byte 1.4.1's.

## Fixed in 1.5.1

- **The default output**: the sound now follows a change of the default output during a run, as in Learn & Play. The game opens
  its sink once and never moves it, so the override stops the graph, moves it, and starts it again
  ([TESTING.md](TESTING.md#game-runs)).
- **Learn & Play's hidden window** is that edition's fix ([Windows plan](../windows/PLAN.md#fixed-in-151)); this edition's
  display link already kept the rate.
- **RSMods' library serves both editions**: `rearmed-mods.dylib` 0.3.0 is built with `rc_mod.h`, so this edition runs the same file as
  Learn & Play, and it needs 1.5.1 for `rc_call_name_f` and `rc_guest_alloc`. The same route in test homes on 2026-11-05 gave the same results but for the lyrics, which only this edition's song showed ([Windows
  plan](../windows/PLAN.md#fixed-in-151) has what it covered). A score screen and the input volume were not reached here either, and the run was
  not compared with 0.2.2's. Changed here on purpose: hotkeys stay off in the song search after Return with
  no match, and after a click, until Esc or the next screen.

## Built in for 1.6.0

Goes with the same section of the [Windows plan](../windows/PLAN.md#built-in-for-160):

- **Every channel in the input list**: under Direct Connect each channel of an audio interface is an input of its own, with the
  game's level bar. The game shows 10 inputs at most and the list doesn't scroll, so they go in priority order
  ([DESIGN.md](DESIGN.md#built-in-fixes-and-mods)). `RS_AUDIO_CHANNEL_ENTRIES=0` turns it off.
- **Calibration that can boost**: the level it sets is a gain of ours, up to +24 dB, and it no longer changes the volume of the
  player's interface. A level saved before counts once calibration has run. `RS_AUDIO_INPUT_LEVEL=0` turns it off. The Real Tone
  cable keeps its own path.
- **Guitar input channel and gain**: `guitar-input.ini`, which came after 1.5.1, is in this release too.
- **The installer**: a Learn & Play build stops on a game image that isn't the pinned one, and an image kept from an earlier
  version is made again; the Mac edition decrypts on every run. Its shader messages say what the game does.

## Built in for 1.7.0

Goes with the same section of the [Windows plan](../windows/PLAN.md#built-in-for-170):

- **No Auto Save warning**: after profile select the game goes straight on, and a Direct Connect profile keeps its input. `RS_MODS_AUTOSAVE=0` shows it.
- **Learn & Play's CPU** is that edition's. Nothing here is meant to change how this edition plays: `relaxed.py` is the one shared translator file touched, and
  this edition's list works as before.

## Next work

Being looked into, with no promise of if or when:

- **RSMods**: more of [RSMods](https://github.com/Lovrom8/RSMods)' features on RS Re-ARMed.
- **Multiplayer with any input**: the game asks for two Real Tone cables. The audio layer has what two players on other inputs
  need, as each channel of an interface is an input of its own since 1.6.0. What's left is the game's own rule and letting each
  player take any input: two channels of one interface, two interfaces, or a cable and an interface. Not tried yet, in either
  edition.
- **One code base for both games**: the shared pieces (renderer, Steam layer, audio policy, installer)
  already serve the Windows port; hook points and the mods folder are shared now, and the rest of its
  parity work is in the [Windows plan](../windows/PLAN.md#next-work).

## Risks

1. **Steam updating the Mac build**: the tables match one image (`image_sha256` in
   `tools/targets/mac-x64/target.toml`); a new build of the game means new tables before it can be built.
   The install's own copy of the game files keeps an existing install playing.
2. **OpenGL on macOS**: deprecated by Apple. Metal is the default, so a removal would cost only the choice
   and the fallback.
3. **Renderer fidelity**: GL semantics on Metal. The game's scenes were checked for 1.3.0; mods that draw
   with OpenGL need the overlay API.
4. **Hardware coverage**: checked on one Mac; other GPUs, ProMotion displays and sleep and wake are not
   ([Open issues](#open-issues)).

## Decisions

- Static recompilation at build time from the user's own copy; nothing of the game is shipped.
- Offline by design: the offline Steam layer, the offline online layer and the network guard on every
  launch.
- Metal by default (OpenGL is deprecated), OpenGL kept as the choice and the automatic fallback.
- Every native reference to the game by name through `tools/targets`, so a change is written once for
  every target.
- The user's Steam install and saves are only read; the native build keeps its own copy.
