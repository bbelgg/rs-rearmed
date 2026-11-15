# Mac port: status and plan

The Windows port's plan: [Windows plan](../windows/PLAN.md). How things are checked is in [TESTING.md](TESTING.md), the
measurements in [PERFORMANCE.md](PERFORMANCE.md); earlier releases are in git and on the
[Releases](https://github.com/bbelgg/rs-rearmed/releases) page.

## Status

What the edition does today. What has not been tested is under [Open issues](#open-issues).

**The game**

- **Released**: `VERSION` says which release this is; an item marked as not released is on `main` only.
- **Checked on**: one Mac (M2) with a 75 Hz external display and the built-in Retina screen.
- **Works**: the whole game as a native arm64 app, offline: songs, Score Attack, DLC and custom songs, Session Mode,
  lessons with their videos, the Guitarcade; Enumerate and EXIT GAME in the main menu; the built-in fixes, no intro
  screens or Auto Save warning.
- **Performance**: a song holds 60 fps on a 60 Hz display at 42-43% of a core and 1.2 GB, about a sixth less CPU than
  the original under Rosetta ([PERFORMANCE.md](PERFORMANCE.md#measured-runs)); 74.9 fps on 75 Hz.
- **Rocksmith 1 songs**: the disc pack's and the DLC pack's are listed and played; the DLC pack's need a refresh with
  the owning account logged in to Steam.
- **Two players**: a song with each player on their own input, SWAP CABLE ASSIGNMENT included.

**Sound and input**

- **Guitar input**: the Real Tone cable, the microphone and other interfaces through Direct Connect, with lower latency
  than under Rosetta; every channel in the input list, input channel and gain, calibration that can boost.
- **Output**: follows the system's default.
- **Game Mode**: the app's category is music, so macOS leaves Game Mode off, which made a plug-in host crackle.

**Picture**

- **Renderer**: Metal by default, with OpenGL as choice and fallback; a window, full screen, Retina.
- **Screen shape**: the game sets its cameras and lays out its menus from its resolution, so a window or a screen of any
  shape draws undistorted (`RS_WINDOW_ANY_SHAPE`; the original kept 16:9).
- **Retina, colour and a sharper picture** (Metal): each frame is scaled up to a Retina screen's pixels with MetalFX and
  tagged sRGB; the game's own texture LOD bias, 16x anisotropic filtering and 4x MSAA on its screen-sized 3D targets
  ([METAL.md](METAL.md#metal-core-shimsopenglsrcmetal)). The same renderer serves Learn & Play.

**Built-in fixes** ([DESIGN.md](DESIGN.md#built-in-fixes-and-mods) says what each is for)

- **Switched off in the installer**: **Built-in Mods…** writes `settings.env`, which the launcher reads at every
  start, so a change needs no reinstall ([INSTALL.md](INSTALL.md#settings-and-switches)).
- **Batched DLC mounting** (not released): the log says what a mounting did (`RS_MODS_LOG=1`), in Learn & Play's
  words. This game's loop drops no archive: the fault Learn & Play's had was looked for here and not found
  ([TESTING.md](TESTING.md#unattended-runs)).
- **Calibration and the tuner at any frame rate** (not released): what the game counts in frames is put right by the
  frame's own time, for frames shorter than a 60 Hz one; at 60 Hz the game runs as before.
- **Skip Tuner in the Guitarcade** (not released): a Guitarcade game's tuner has the Skip Tuner a song's tuner has,
  with the same warning. The game left it out there, so only a finished tuning got a player into a game.
- **A fixed frame rate** (not released): the rule and clock of `RS_FPS=N`, this edition's test switch, are one
  source that Learn & Play uses too (`shims/platform/include/rsp_fps.h`); this edition runs as before.
- **Shared with Learn & Play** (not released): the switches, the log and the start of the built-in fixes, the intro
  skip, the song ownership during a mount, Player 2's saved input and the frame rate fixes are one source for both
  editions (`overrides/mods/mods_shared.c`, `framerate.c`), and Learn & Play's own fixes name the game through the
  tables as these do. One thing changes for this edition's player: the shared hook for Player 2's saved input frees
  the name's text when the game had to allocate it (a name longer than about 15 characters), which this edition's own
  hook left behind at every call.

**Installer and game files**

- **Installer**: the GUI installer decrypts the game statically (no Rosetta, no launch of the game).
- **Where the game files are kept**: for a game on another drive the installer's Game files choice keeps the copy on
  that drive as clones or on this Mac as a real copy, and an install or a refresh moves a copy that is in the other
  place (`place()` in `gamefiles.py`, shared with Learn & Play).
- **The game without a Steam install**: when the Mac game isn't installed in Steam, the installer and
  `gamefiles.py mac-game` have Steam download depot 221682 and the account's DLC, with the code Learn & Play's download
  uses ([DESIGN.md](DESIGN.md#installer-and-build)). Such an install is in English (no appmanifest), and DLC a family
  member shares is not fetched: the ownership ticket lists only the account's own, as for Learn & Play.

**Mods**

- **Loading and hook points**: mods are loaded at startup from `mods/`, with name-keyed hook points (all 21). A mod
  built with `rc_mod.h` is one dylib for this app and Learn & Play, as `rearmed-mods.dylib` is.
- **Files a mod serves**: a mod's file provider answers for the files the game opens, their sizes and folder listings
  (`rc_file.h`, [DESIGN.md](DESIGN.md#built-in-fixes-and-mods)), the same interface in both editions.
- **Edits and mid points, and 7-string charts**: a mod changes a function from the inside. The tables
  (`tools/targets/mac-x64/edits.map`, `mids.map`) carry rearmed-mods' Seven-String Guitar, after
  [rs7string](https://github.com/SomeeName/rs7string), and Plain Packages, the same units as Learn & Play. The game's
  list of a chord's expected notes keeps six here (eight in Learn & Play); the mod's own chord detection judges larger
  chords in both editions.
- **What the game names for mods** (in `tools/targets`, the same names in both editions): the colors of notes and the
  highway, through the [render API](METAL.md#render-api-shimsincluders_renderh) on Metal by the game's small palette
  textures, and of the strings, in the game's own color table; the note the game hears (Player 1's pitch detection,
  guitar and bass); Player 1's hits and misses and the track's grey-note time, in Learn a Song and Score Attack; and
  hooks on the tone change (`tone_change`, for clearing the wait behind a stuck tone), on the song lists' names
  (`song_list_name`) and on decoded song audio (`ak_src_submit`: each Wwise source's decoded block on its way to the
  mixer, with the source's file and position, and the game's Vorbis codebooks).
- **Your own plug-in** (rearmed-mods' ToneHost add-on): played with Helix Native, with the game's gear mapped onto it.
  Its open work is kept with it.

## Open issues

**Faults and limits**

- With the legacy fast-load `cache.psarc`, a start can stay on the white loading screen now and then (a race in the
  game). Metal's safe load avoids the stall shader compiles would add; on OpenGL quit and start again.
- The first play of a song after an install stutters briefly on Metal (pipelines built on first use). A draw that
  waits for a shader compile hurries it; the gain isn't measured.
- Mods that draw with OpenGL show nothing on Metal; they need the overlay API.
- Renderer details: `RS_GL_CHECK` keeps one pending error per thread, not per context; `GL_TEXTURE_BASE_LEVEL` is
  ignored on Metal; shared CGL contexts are not supported on Metal; only the first GPU gets a disk cache. A draw keeps
  its pipeline key while nothing it is made of has changed, but the game sets vertex state or the program before every
  draw, so no draw keeps one until the marks compare values.
- **Song archives per platform**: this game reads the Mac archives (`*_m.psarc`), Learn & Play the PC ones
  (`*_p.psarc`). Official DLC has both; a custom song, or a 7-string chart, often exists for one platform only. The song
  files are encrypted with a key per platform, which the other game does not have, and nothing from the game may be in
  this repository, so RS Re-ARMed has no converter and will not get one.
- The installer keeps its preferences in the user's defaults even when run with a test `HOME`
  ([TESTING.md](TESTING.md#installer-tests)).

**Not tested or not done yet**

- **The published installer**: recent releases' hasn't been run, nor a full install from the tag (the last was
  1.6.0's); the install scripts and test installs were.
- **Game files**: a move of an installed copy back to the game's drive, by hand. For a game Steam downloads: the
  download from the real Steam, the installer's window on that route, and a game run from such a copy
  ([TESTING.md](TESTING.md#installer-tests)).
- **Two of 1.12.0's fixes**, in the game, which no test reaches: the restart with the network guard when the game's app
  is started directly, and the launcher forcing the game's offline settings when there is no `Rocksmith.ini` yet.
- **Auto Save**: the skip with a Real Tone cable or a Microphone profile. The title screen's own call of the dialog,
  which Learn & Play's title has ([Windows plan](../windows/PLAN.md#open-issues)), has not been read in this game.
- **Guitar input**: the cable screen's Direct Connect button, unplugging an interface while the game runs, a Real Tone
  cable with this build, and the level with a second profile, which counts as calibrated with the first.
- **Two players**: a cable with an interface, and two interfaces. Two inputs have not been played since Player 2's
  saved input became the shared hook: what it hands the game is now memory kept for it, where it was the hook's own
  stack.
- **Calibration and the tuner at any frame rate**: with a guitar above 60 Hz, alone and with two players. The fixes
  came from a player's report of the Guitarcade's games going round between the tuner and calibration on a 120 Hz
  MacBook Pro; whether this was the cause of that is not known.
- **Batched DLC mounting**: with a library of thousands (it was compared with a step a frame on 147 archives).
- **The fixes from the code review of 2026-11-09**: six songs played with a guitar since, with no fault; no long
  session.
- **Mods**: a file provider that changes what an archive holds; the tuner's reading of a 7-string chart's low B and a
  seven-row chord diagram; the colors in other modes (they were checked in one song); the stuck tone that the tone
  change hook is for, a rare bug that has not been met.
- **Screen shape**: full screen on a 16:10 or ultrawide screen.
- **Hardware**: a second Mac or GPU, 120 Hz ProMotion displays, sleep and wake, unplugging a display, the Retina mouse
  in songs, and a fresh player's first answer to macOS's question about another disk.

## Next work

Being looked into, with no promise of if or when:

- **RSMods**: more of [RSMods](https://github.com/Lovrom8/RSMods)' features on RS Re-ARMed.
- **7-string charts**: left are the tuner on the low B and a seven-row diagram, each looked at on its own.
- **Choosing both players' inputs on the Multiplayer screen**: each picks through their own profile today. The game's
  input list is hard-wired to Player 1, and SWAP CABLE ASSIGNMENT could open it for Player 2.
- **Performance**: what the profile found to work on, `dsp_fft` and the two Vorbis functions still fenced first
  ([PERFORMANCE.md](PERFORMANCE.md#opportunities-by-expected-payoff)).
- **Measured runs** (`tools/perf`, [TESTING.md](TESTING.md#measured-runs)), for both editions. Left: the free frame
  rate and the GPU's time by pass in Learn & Play; a check of the GPU's heat before a run; the guitar input from a
  file, so that notes are hit and no interface is needed; draws and passes a frame, to see that two runs drew the same;
  and a loop of songs that watches the memory for leaks.
- **One code base for both games**: shared today are the recompiler with its two front ends, the renderer, CoreAudio,
  the offline Steam behaviour, the mod loader with its hook points and ABI, the per-game tables, the built-in fixes
  that need nothing of one game alone, the installer app, the game-file copy and Steam download, the measured runs and
  the release tools. The largest pieces still written twice: the start-up (`tools/play-native/launch.sh`, in shell,
  here; `win-x86/app/lp_main.c`, in C, for Learn & Play), the two window shells (`shims/platform/src/rsp_shell.m` and
  `win-x86/app/lp_window.m`), and the two builds, which compile the shared C with different flags. The rest of the
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
- A built-in fix that needs nothing of one game alone is one source for both editions, written as a mod is
  (`tools/ws7/recomp/overrides/mods/mods_shared.c`, `framerate.c`); the rest is each edition's own.
- No Rosetta and no Homebrew in the install: the game image is decrypted statically, and the build uses Apple's
  command line tools and its own Python packages.
- The user's installed game and saves are only read; the native build keeps its own copy. A game Steam downloads
  for the installer goes to Steam's own download folder, which the installer links to another disk when the user
  chooses one.
