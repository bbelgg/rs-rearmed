# Mac port: status and plan

The Windows port's plan: [Windows plan](../windows/PLAN.md). Results of the last runs are in [TESTING.md](TESTING.md) and [PERFORMANCE.md](PERFORMANCE.md); earlier releases
are in git and on the [Releases](https://github.com/bbelgg/rs-rearmed/releases) page.

## Status

- **Released**: `VERSION` says which release this is.
- **Works**: the whole game as a native arm64 app, offline: songs, Score Attack, DLC and custom songs, Session Mode, lessons with their videos, the Guitarcade; the Real Tone
  cable, the microphone and other interfaces through Direct Connect, with lower latency than under Rosetta; every channel in the input list, input channel and gain, calibration
  that can boost; the output following the system's default; Metal by default with OpenGL as choice and fallback; full screen, Retina; Enumerate; the built-in fixes, no intro
  screens or Auto Save warning; the GUI installer.
- **Mods**: loaded at startup, with name-keyed hook points. A mod built with `rc_mod.h` is one dylib for this app and Learn & Play. `rearmed-mods.dylib` 0.8.0 loads here, with its 23 mods.
- **Colors of notes, the highway and strings** (new in 1.9.0): mods change them, in rearmed-mods from 0.8.0, and they were checked in both editions with its 0.5.0. Notes and the highway go through the
  [render API](METAL.md#render-api-shimsincluders_renderh) on Metal. The game colors them through small palette textures, found with the texture log ([TESTING.md](TESTING.md#diagnostics)) in one song in each
  edition: a 256x128 ramp for note heads, tails and open-string bars, a second on the indicators' shapes, and 256x32 and 512x32 ones for the highway. Other modes aren't checked. The string colors are in the
  game's own table, named `game_color_manager` and `game_color_table`, the same in both games.
- **The note the game hears** (new in 1.11.0): named for mods, as RSMods reads it on Windows: `detection_service`, its detector and its pitch detection's note (`detection_note`, `detection_note_bass` while the input is a bass), found by the game's own check of a note in a song (0x10022dca0; Learn & Play's 0x1067130). rearmed-mods' Show Current Note and Guitar Speak read it. Read in a run on a relinked build (2026-11-08): the open low E to an A#5 on a guitar, each as played, and between notes the game's end of note. The detector is Player 1's (+0x18; Player 2's at +0x20 has no pitch detection in single player). Learn & Play not yet run.
- **The note counts and the grey-note time** (new in 1.11.0): named for mods: Player 1's note counts (`player_note_stats`: `note_stats_hits`, `note_stats_misses`), reached from a song's menu (`song_menu_game`, `song_game_player`), and their track's grey-note time (`track_grey_time`, which the game raises to the song position when the song pauses, 0x1004380eb; notes before it are grey and not detected). rearmed-mods' Show Current Accuracy and the grey-note reset of its looping and rewinding read them, where RSMods reads Learn & Play's memory. Found by scanning the game's memory in runs (2026-11-09) and checked in two later runs: Learn a Song and Score Attack, playing and paused. The counts and the track are matched by the code that counts a hit and a miss (0x1002542c5, 0x100254464); Learn & Play's are too, and its track times were read in runs; there the counts are reached by RSMods' path from the UI manager (`ui_note_stats_*`, unsupported here), in Learn a Song ([Windows plan](../windows/PLAN.md#status)).
- **A hook on the tone change** (new in 1.11.0): `tone_change` (0x1002bebc0, Learn & Play's 0x1352620) asks for a player's tone, and waits while a change is under way (`tone_change_busy`). rearmed-mods' Prevent Buggy Tones clears that wait, where RSMods jumps past it on Windows. A mod hooked it in a run on a relinked build, with no refusal; the stuck tone itself, a rare bug, has not been met.
- **A hook on the song lists' names** (new in 1.11.0): `song_list_name` (0x1005dd8d0, Learn & Play's 0x10d2470) gives the menus a song list's name, the localized "SONG LIST" with the list's number after it, and `efd_str_destroy` (0x10000a060) is the game's string destructor. rearmed-mods' Custom Song List Titles puts the player's own titles in that name, where RSMods changes the function's code on Windows. Played on a build of the branch (2026-11-09): Learn a Song's song list filter and Non-Stop Play's choice of list showed the titles, short ones and ones longer than the game's name.
- **A hook on decoded song audio** (new in 1.12.0): `ak_src_submit` (0x100b0e6a0, Learn & Play's 0x1a98ed0) is where every Wwise source hands a decoded block to the mixer (16-bit interleaved from the Vorbis decoder, 256 frames at a time for a song), with the source's file (`ak_src_pbi`, `ak_pbi_src_info`, `ak_src_info_file_id`: the number of the `.wem` in the song's archive) and the block's position (`ak_src_position`). For rearmed-mods' Song Stems, which takes the guitar or bass part out of the song being played, at a level of the player's own: played on a build of the branch (2026-11-09) with its helper separating the song on the Neural Engine while the song loaded (the first 30 s in 2.2 s), the guitar out from the song's first notes, the level changed by its key within a block, and aligned after a rewind; its helper decodes a song's `.wem` with the game's own Vorbis codebook library, named for mods as `ak_vorbis_codebooks` (599 codebooks, `ak_vorbis_codebook_count`; the same bytes in both editions). Checked in a run on a build of the branch (2026-11-09): a test mod recorded a custom song's blocks and compared them with the `.wem` decoded by vgmstream: each block at its position, to -92 dB (the two decoders' rounding), and again after a rewind.
- **Edits and mid points, and 7-string charts** (new in 1.13.0): the tables (`tools/targets/mac-x64/edits.map` and `mids.map`) now carry
  rearmed-mods' Seven-String Guitar, after [rs7string](https://github.com/SomeeName/rs7string) (`seven_string`: 618 edits, 122 mid points), and Plain Packages
  (`plain_packages`: 2 edits, 1 mid point), the whole unit as Learn & Play has it: the strings' colours, the strings, the fret grid, pitch detection, the
  tuner, chords, chord detection and drawing, the chord diagrams, the tuning names and the seven-peg headstock. 34 of the points the two editions share are
  `none` here (this game gets to the same result another way), and 29 points are this edition's own,
  `none` in Learn & Play: mostly loops Learn & Play's edits lengthen, unrolled here, whose extra strings need a call, and
  functions inlined here. The 64-bit front end's edits and mid
  points have their own test (`recomp.inner64`, [TESTING.md](TESTING.md#tests-in-the-repository)). Run 2026-11-10 by scripted input with rearmed-mods'
  branch, with no fault: a six-string song; DotCharter's 7-string chart with its tuner, the pause menu's TUNER and CHORDS pages; a six-string song and the
  chart again in one run; a bass arrangement. The snapshots show seven strings, the low B's lane and notes, chord frames over the low B, the seven-peg
  headstock and B STANDARD 7STR in the tuner; leaving a song rebuilt the guitar with six strings, and four for the bass. Played with a guitar by the maintainer
  the same day, from the launcher: it works. What that play covered was not written down, so the tuner's reading of the low B and a seven-row chord
  diagram (the chart's chords on the low B have no name, and the CHORDS page lists only named ones) are not checked off one by one. One difference from Learn & Play: there the edits let the game's list of a chord's expected notes
  hold eight; here that list is in the same function as chord detection, with arrays for six, and keeps six. A chord of seven or eight notes is judged by
  the mod's own chord detection in both editions, before that list is filled, so here it never gets more than six and such a chord loses nothing. Where Guitarcade, Session Mode and the tuner menu store a tuning was read from the code,
  not visited in a run. A chart published for Windows (`_p.psarc`) has to be a Mac archive first; RS Re-ARMed does not convert it ([Next work](#next-work)).
- **Built-in mods switched off in the installer** (new in 1.11.0): **Built-in Mods…** writes `settings.env`, which the launcher reads at every start, so a change needs no reinstall: custom songs, the rs-mac-fixes fixes, the intro and Auto Save skips, the menu additions, two players on any input, the title logo ([INSTALL.md](INSTALL.md#settings-and-switches)). Learn & Play reads its own file the same way, and one test runs both readers. The sheet was checked in a test home for both games, and a start of each edition with the intro skip switched off in the file played the intro (2026-11-09).
- **Exit Game** (new in 1.9.0): EXIT GAME in the main menu, in place of UPLAY, pressed in a test install of each edition.
- **Game Mode** (new in 1.9.1): the app's category is music, so macOS leaves Game Mode off, which made a plug-in host crackle. Played on an installed game changed by hand; not yet run in Learn & Play.
- **Two players** (new in 1.8.0): a song with each player on their own input, played on two channels of one interface, SWAP CABLE ASSIGNMENT included; a cable and an interface and two interfaces have not been played.
- **Rocksmith 1 songs**: the disc pack's and the DLC pack's are listed and played; the DLC pack's need a refresh with the owning account logged in to Steam.
- **Installer**: decrypting is static (`tools/steamless/decrypt.py`): no Rosetta, no launch of the game, and Steam only has to have installed it.
- **Performance**: about the CPU of the original under Rosetta. 6.1 ms a frame on [Learn & Play's route](PERFORMANCE.md#learn--plays-route), 45% of a core, against its 7.2 ms.
  The 12 functions relaxed and the renderer's change of 2026-11-06 played well with a guitar; an install of them by the installer app is still to do.
- **Screen shape** (new in 1.10.0): the game sets its cameras from its resolution (their height kept, the width the resolution's) and lays out its menus to
  the width, so a 16:10 window draws undistorted (checked 2026-11-08, frustums 0.925 x 0.577 at 1153x720 against 1.026 x 0.577 at 16:9).
  Full screen renders at the screen's size the same way; not yet run on a 16:10 or ultrawide screen. A window now takes any shape
  (`RS_WINDOW_ANY_SHAPE`; the original kept 16:9). Learn & Play now draws at any shape too, its 16:9 fit taken out ([Windows plan](../windows/PLAN.md#status)).
- **Retina and colour** (new in 1.10.0, Metal): on a Retina screen each frame is scaled up to the screen's pixels with MetalFX while the
  game renders in points, and the picture is tagged sRGB, so wide-colour screens show the game's colours as made
  ([METAL.md](METAL.md#metal-core-shimsopenglsrcmetal)). The same renderer, and the same request from its window, serve Learn & Play.
- **Sharper picture** (new in 1.10.0, Metal): the game's own texture LOD bias, which Metal had dropped, 16x anisotropic filtering where the game asks for
  1x to 4x, and 4x MSAA on its screen-sized 3D targets ([METAL.md](METAL.md#metal-core-shimsopenglsrcmetal)). The same renderer serves Learn & Play.
  1.10.0's build was played with a guitar (an HX Stomp) by the maintainer on 2026-11-08, full screen on the 75 Hz 16:9 display: 74.9 fps, a game frame
  of about 4 ms, once the song's first-play stutters (pipelines built on first use, gone on a second play) were done.
- **Checked on**: one Mac (M2) with a 75 Hz external display and the built-in Retina screen.

- **Your own plug-in** (rearmed-mods' ToneHost add-on): played with Helix Native, with the game's gear mapped onto it. Its open work is kept with it.

## Open issues

- **The published installer**: 1.12.0's hasn't been run, nor a full install from the tag. Exit Game and the colors were checked on test installs. The last full install from a published installer was 1.6.0's.
- **The title screen's own Auto Save call**: in Learn & Play the title has a second call of the dialog ([Windows plan](../windows/PLAN.md#open-issues)). This game's counterpart has not been read.
- The cable screen's Direct Connect button hasn't been checked.

- The list of inputs and the level, not checked yet: unplugging an interface while the game runs (the game's property listeners get the real device, not a channel's id), a Real Tone cable with this
  build, and the level with a second profile, which counts as calibrated with the first.
- With the legacy fast-load `cache.psarc`, a start can stay on the white loading screen now and then (a
  race in the game). Metal's safe load avoids the stall shader compiles would add; on OpenGL quit and start
  again.
- The first play of a song after an install stutters briefly on Metal (pipelines built on first use).
- Mods that draw with OpenGL show nothing on Metal; they need the overlay API.
- Renderer details: `RS_GL_CHECK` keeps one pending error per thread, not per context;
  `GL_TEXTURE_BASE_LEVEL` is ignored on Metal; shared CGL contexts are not
  supported on Metal; only the first GPU gets a disk cache.
- Not checked yet: a second Mac or GPU, 120 Hz ProMotion displays, sleep and wake, unplugging a display, the
  Retina mouse in songs, and a fresh player's first answer to macOS's
  question about another disk with the compiled launcher (an earlier Allow covered the one run).
- The fixes for what the code review of 2026-11-09 left open (not released), run in their suites and in one game
  run of each edition to the note highway and through 150 s of a song (`tools/perf/run.py`, 2026-11-10: this
  edition at 60.0 fps, 43.3% of a core and 1.23 GB, as 1.13.0): a draw that waits for a shader compile raises the worker that has it (`wait_boosted` in
  `RS_METAL_CACHE_STATS`); a draw keeps its pipeline key while nothing it is made of has changed
  (`RS_METAL_PKEY_CHECK=1` builds it anyway and says when the kept one was stale; in both games' runs no draw
  kept a key at all, since the game sets vertex state or the program before every draw, so this saves nothing
  there until the marks compare values, which is open); `GL_R11F_G11F_B10F` pixel data is converted both ways; a callback's
  trampoline is found without the process-wide lock (`rc_tramp.c`); the Steam layer's `auto` mode keeps the
  listeners registered before a failed `SteamAPI_Init`; a refresh onto a volume with coarse file times reads no
  file again; the standalone installer builds only a download that adds up to the source it was made from. None
  of the three renderer changes shows in the numbers. The boost was taken in Learn & Play's run (98 waits of 98,
  on a cold shader cache), with nothing to compare its 25 s of waits against; the Mac run had no wait.
- The installer keeps its preferences in the user's defaults even when run with a test `HOME`
  ([TESTING.md](TESTING.md#installer-tests)).

## Next work

Being looked into, with no promise of if or when:

- **RSMods**: more of [RSMods](https://github.com/Lovrom8/RSMods)' features on RS Re-ARMed.
- **7-string charts**: the tables are in and played with a guitar ([Status](#status)). Left: the tuner on the low B and
  a seven-row diagram, each looked at on its own. DotCharter publishes its 7-string charts as
  Windows archives (`_p.psarc`, unencrypted); the Mac game reads `_m.psarc`, with the song files under `songs/bin/macos/`,
  the audio under `audio/mac/`, the graph naming them so and the song files encrypted with the Mac's key. The other files
  are the same bytes. RS Re-ARMed has no converter for that and will not get one: it would need the archives' keys, and
  nothing from the game is in this repository. A chart has to be a Mac archive before it is put in `dlc`.
- **Choosing both players' inputs on the Multiplayer screen**: each picks through their own profile today. The game's input list is
  hard-wired to Player 1 (0x1001e6660), and SWAP CABLE ASSIGNMENT could open it for Player 2.
- **Measured runs** (`tools/perf`, [TESTING.md](TESTING.md#measured-runs)): the script runs both editions the same way. The first
  set is in [PERFORMANCE.md](PERFORMANCE.md#measured-runs-for-1130). The GPU's time by pass is in (`RS_METAL_PASS_TIMES`). Left: the free frame rate and the passes in Learn & Play;
  a check of the GPU's heat before a run; the guitar input from a file, so that
  notes are hit and no interface is needed; draws and passes a frame, to see that two runs drew the same; and a loop of songs that
  watches the memory for leaks.
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
