# Mac port: status and plan

The Windows port's plan: [Windows plan](../windows/PLAN.md). How things were checked is in [TESTING.md](TESTING.md), the
measurements in [PERFORMANCE.md](PERFORMANCE.md); earlier releases are in git and on the
[Releases](https://github.com/bbelgg/rs-rearmed/releases) page.

## Status

- **Released**: `VERSION` says which release this is.
- **Works**: the whole game as a native arm64 app, offline: songs, Score Attack, DLC and custom songs, Session Mode,
  lessons with their videos, the Guitarcade; the Real Tone cable, the microphone and other interfaces through Direct
  Connect, with lower latency than under Rosetta; every channel in the input list, input channel and gain, calibration
  that can boost; the output following the system's default; Metal by default with OpenGL as choice and fallback; full
  screen, Retina; Enumerate and EXIT GAME in the main menu; the built-in fixes, no intro screens or Auto Save warning;
  the GUI installer, which decrypts the game statically (no Rosetta, no launch of the game).
- **Checked on**: one Mac (M2) with a 75 Hz external display and the built-in Retina screen.
- **Performance**: a song holds 60 fps on a 60 Hz display at 42-43% of a core and 1.2 GB, about a sixth less CPU than
  the original under Rosetta ([PERFORMANCE.md](PERFORMANCE.md#measured-runs-for-1130)); 74.9 fps on 75 Hz.
- **Mods**: loaded at startup from `mods/`, with name-keyed hook points (all 21). A mod built with `rc_mod.h` is one
  dylib for this app and Learn & Play. `rearmed-mods.dylib` 0.8.0 loads here, with its 23 mods.
- **Files a mod serves** (new in 1.14.0): a mod's file provider answers for the files the game opens, their sizes and
  folder listings (`rc_file.h`, [DESIGN.md](DESIGN.md#built-in-fixes-and-mods)), the same interface in both editions.
  Covered by unit tests and one run to the note highway with a mod serving every `.psarc` the game opens
  ([TESTING.md](TESTING.md)). A provider that changes what an archive holds has not been run.
- **Edits and mid points, and 7-string charts** (new in 1.13.0): a mod changes a function from the inside. The tables
  (`tools/targets/mac-x64/edits.map`, `mids.map`) carry rearmed-mods' Seven-String Guitar, after
  [rs7string](https://github.com/SomeeName/rs7string), and Plain Packages, the same units as Learn & Play. Run by
  scripted input and played with a guitar. Not checked one by one: the tuner's reading of the low B and a seven-row
  chord diagram. The game's list of a chord's expected notes keeps six here (eight in Learn & Play); the mod's own
  chord detection judges larger chords in both editions, so nothing is lost. A chart published for Windows
  (`_p.psarc`) has to be a Mac archive first ([Next work](#next-work)).
- **What the game names for mods** (the names are in `tools/targets`, the same in both editions):
  - *Colors of notes, the highway and strings* (new in 1.9.0): notes and the highway through the
    [render API](METAL.md#render-api-shimsincluders_renderh) on Metal, by the game's small palette textures; the
    strings in the game's own color table. Checked in one song; other modes aren't.
  - *The note the game hears* (new in 1.11.0): Player 1's pitch detection, for guitar and bass. Read in a run, each
    note as played.
  - *The note counts and the grey-note time* (new in 1.11.0): Player 1's hits and misses and the track's grey-note
    time, read in Learn a Song and Score Attack, playing and paused.
  - *A hook on the tone change* (new in 1.11.0): `tone_change`, for clearing the wait behind a stuck tone. Hooked in a
    run; the stuck tone itself, a rare bug, has not been met.
  - *A hook on the song lists' names* (new in 1.11.0): `song_list_name`. Played: the player's own titles showed in
    Learn a Song and Nonstop Play.
  - *A hook on decoded song audio* (new in 1.12.0): `ak_src_submit`, where every Wwise source hands a decoded block to
    the mixer, with the source's file and position, and the game's Vorbis codebooks. Checked against another decoder
    block by block, also after a rewind; rearmed-mods' Song Stems played on it.
- **Built-in mods switched off in the installer** (new in 1.11.0): **Built-in Mods…** writes `settings.env`, which the
  launcher reads at every start, so a change needs no reinstall ([INSTALL.md](INSTALL.md#settings-and-switches)). One
  test runs both editions' readers.
- **Game Mode** (new in 1.9.1): the app's category is music, so macOS leaves Game Mode off, which made a plug-in host
  crackle.
- **Two players** (new in 1.8.0): a song with each player on their own input, played on two channels of one interface,
  SWAP CABLE ASSIGNMENT included. A cable with an interface, and two interfaces, have not been played.
- **Rocksmith 1 songs**: the disc pack's and the DLC pack's are listed and played; the DLC pack's need a refresh with
  the owning account logged in to Steam.
- **Screen shape** (new in 1.10.0): the game sets its cameras and lays out its menus from its resolution, so a window
  of any shape draws undistorted (`RS_WINDOW_ANY_SHAPE`; the original kept 16:9). Full screen renders at the screen's
  size the same way; not yet run on a 16:10 or ultrawide screen.
- **Retina, colour and a sharper picture** (new in 1.10.0, Metal): each frame is scaled up to a Retina screen's pixels
  with MetalFX and tagged sRGB; the game's own texture LOD bias, 16x anisotropic filtering and 4x MSAA on its
  screen-sized 3D targets ([METAL.md](METAL.md#metal-core-shimsopenglsrcmetal)). The same renderer serves Learn & Play.
- **Your own plug-in** (rearmed-mods' ToneHost add-on): played with Helix Native, with the game's gear mapped onto it.
  Its open work is kept with it.

## Open issues

- **The published installer**: recent releases' hasn't been run, nor a full install from the tag; the install scripts
  and test installs were. The last full install from a published installer was 1.6.0's.
- **The title screen's own Auto Save call**: Learn & Play's title has a second call of the dialog
  ([Windows plan](../windows/PLAN.md#open-issues)). This game's counterpart has not been read.
- The cable screen's Direct Connect button hasn't been checked.
- The list of inputs and the level, not checked yet: unplugging an interface while the game runs, a Real Tone cable
  with this build, and the level with a second profile, which counts as calibrated with the first.
- With the legacy fast-load `cache.psarc`, a start can stay on the white loading screen now and then (a race in the
  game). Metal's safe load avoids the stall shader compiles would add; on OpenGL quit and start again.
- The first play of a song after an install stutters briefly on Metal (pipelines built on first use). A draw that
  waits for a shader compile now hurries it (new in 1.14.0); the gain isn't measured.
- Mods that draw with OpenGL show nothing on Metal; they need the overlay API.
- Renderer details: `RS_GL_CHECK` keeps one pending error per thread, not per context; `GL_TEXTURE_BASE_LEVEL` is
  ignored on Metal; shared CGL contexts are not supported on Metal; only the first GPU gets a disk cache. A draw keeps
  its pipeline key while nothing it is made of has changed (new in 1.14.0), but the game sets vertex state or the
  program before every draw, so no draw keeps one until the marks compare values.
- **The fixes from the code review of 2026-11-09** (new in 1.14.0) were run in their suites and in one song of each
  edition, with the numbers unchanged. Played with a guitar since, six songs with no fault; a long session has
  not been played on them.
- Not checked yet: a second Mac or GPU, 120 Hz ProMotion displays, sleep and wake, unplugging a display, the Retina
  mouse in songs, and a fresh player's first answer to macOS's question about another disk.
- The installer keeps its preferences in the user's defaults even when run with a test `HOME`
  ([TESTING.md](TESTING.md#installer-tests)).

## Next work

Being looked into, with no promise of if or when:

- **RSMods**: more of [RSMods](https://github.com/Lovrom8/RSMods)' features on RS Re-ARMed.
- **7-string charts**: left are the tuner on the low B and a seven-row diagram, each looked at on its own. A chart
  published as a Windows archive (`_p.psarc`) has to be a Mac archive (`_m.psarc`) before it is put in `dlc`. RS
  Re-ARMed has no converter and will not get one: the song files are encrypted with a key per platform, and nothing
  from the game is in this repository.
- **Choosing both players' inputs on the Multiplayer screen**: each picks through their own profile today. The game's
  input list is hard-wired to Player 1, and SWAP CABLE ASSIGNMENT could open it for Player 2.
- **Measured runs** (`tools/perf`, [TESTING.md](TESTING.md#measured-runs)): one script runs both editions the same way,
  with the GPU's time by pass and a profile ([PERFORMANCE.md](PERFORMANCE.md#where-the-cpu-goes-in-1130)). The
  profile's candidates here are `dsp_fft` and the two Vorbis functions still fenced. Left: the free frame rate and the
  passes in Learn & Play; a check of the GPU's heat before a run; the guitar input from a file, so that notes are hit
  and no interface is needed; draws and passes a frame, to see that two runs drew the same; and a loop of songs that
  watches the memory for leaks.
- **One code base for both games**: the renderer, Steam layer, audio policy, installer, hook points and mods folder are
  shared; the rest of the parity work is in the [Windows plan](../windows/PLAN.md#next-work).

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
