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
- **Rocksmith 1 songs**: the disc pack's and the DLC pack's are listed and played. The DLC pack's show for the DLC that
  count as owned (new in 1.17.1): what a copy or refresh finds from the Steam client, the account's ownership tickets,
  Steam's package cache and an installed first Rocksmith. The copy's log says what each found. One was found by a
  search and played to the note highway with its DLC counted as owned, and was not listed without. 1.17.0 showed all
  of them while the CDLC skip was on.
- **Two players**: a song with each player on their own input, SWAP CABLE ASSIGNMENT included.

**Sound and input**

- **Guitar input**: the Real Tone cable, the microphone and other interfaces through Direct Connect, with lower latency
  than under Rosetta; every channel in the input list, input channel and gain, calibration that can boost.
- **Output**: follows the system's default.
- **Game Mode**: the app's category is music, so macOS leaves Game Mode off, which made a plug-in host crackle.
- **Trackpad scrolling** (new in 1.20.0): checked in game runs 2026-11-24. Lists scroll with
  the trackpad, as on Learn & Play, and with a mouse wheel, which did nothing before. The shell posts one
  wheel event a step (`rsp_scroll_step`), and a built-in fix gives the game's own wheel route the store it
  misses (the mouse setter beside `key_down`), so both editions move the same number of rows for the same
  swipe. The trackpad's steps accumulate `scrollingDeltaY` at 25 px a step, capped at 8 a single event with
  the excess dropped, and a new gesture starts clean; a wheel notch is one step. The runs showed inertia that stops, the same movement with the pointer off the list, one
  row a wheel notch, nothing moving in the Tone Designer or in a song, and the Guitarcade's lists scrolling.

**Picture**

- **Renderer**: Metal by default, with OpenGL as choice and fallback; a window, full screen, Retina.
- **Screen shape**: the game sets its cameras and lays out its menus from its resolution, so a window or a screen of any
  shape draws undistorted (`RS_WINDOW_ANY_SHAPE`; the original kept 16:9).
- **Retina, colour and a sharper picture** (Metal): each frame is scaled up to a Retina screen's pixels with MetalFX and
  tagged sRGB; the game's own texture LOD bias, 16x anisotropic filtering and 4x MSAA on its screen-sized 3D targets
  ([METAL.md](METAL.md#metal-core-shimsopenglsrcmetal)). The same renderer serves Learn & Play.
- **Sharpening** (not released): `RS_METAL_SHARPEN=N` sharpens the picture on the screen after any upscaling, never
  past a pixel's neighbours, so edges don't ring ([METAL.md](METAL.md#metal-core-shimsopenglsrcmetal)).
- **Performance overlay and low latency** (new in 1.20.0): `RS_METAL_HUD=1` shows Apple's Metal Performance HUD over
  the game (macOS 13 and later), and `RS_METAL_DRAWABLES=2` queues two frames for the screen instead of three with
  vsync, each shown a refresh sooner ([INSTALL.md](INSTALL.md#settings-and-switches)).
- **The tuner's red peg** (new in 1.18.0): the game lights the peg of the string it listens to in red, with a glow it
  blurs by draws that sample the texture they draw into. Metal gave such a draw the last frame's picture, so the
  peg stayed grey; now it gets a copy of what was drawn before it
  ([METAL.md](METAL.md#metal-core-shimsopenglsrcmetal)). Learn & Play's tuner turned purple from the same cause
  at some sizes; this edition's was not seen to (one run at 1103x717 before the rule).
- **Render scale** (new in 1.18.0): `RS_METAL_SCALE=N`, the installer's Render scale (under Advanced), renders at N%
  (50-100) and scales up with MetalFX. Played at 75% and 50% on an M1 MacBook Air: on its own Retina screen 50% took
  0.5 W off the GPU's 6.1 W, where `RS_METAL_UPSCALE=0` takes 2.7 W off; on a 1x external display nothing measurable
  ([PERFORMANCE.md](PERFORMANCE.md#measured-runs)).
- **In the game's menu** (new in 1.19.0; LOW LATENCY and PERFORMANCE OVERLAY new in 1.20.0; SHARPENING and the rows
  taking effect at once not released): OPTIONS → ADV DISPLAY SETTINGS lists thirteen of the picture switches after the
  game's own rows, saved where the installer saves them, five of them taking effect at once (PERFORMANCE OVERLAY too, in
  a game started with it); a row that can't apply is greyed with why, as is one a switch set for the session
  (`launchctl setenv`) holds, and the longer list scrolls, with a scroll bar that can be dragged
  ([INSTALL.md](INSTALL.md#settings-and-switches)). Learn & Play lists eleven of them (new in 1.20.0), four taking
  effect at once (not yet run there; [its plan](../windows/PLAN.md#status)).

**Built-in fixes** ([DESIGN.md](DESIGN.md#built-in-fixes-and-mods) says what each is for)

- **Switched off in the installer**: **Settings and switches…** writes `settings.env` (the built-in mods
  switched off) and `options.env` (every other switch) (new in 1.17.0), which the launcher reads at every start,
  so a change needs no reinstall ([INSTALL.md](INSTALL.md#settings-and-switches)).
- **Batched DLC mounting** (new in 1.16.0): the log says what a mounting did (`RS_MODS_LOG=1`), in Learn & Play's
  words. This game's loop drops no archive: the fault Learn & Play's had was looked for here and not found
  ([TESTING.md](TESTING.md#unattended-runs)). "Enumerating downloadable content..." stays until every song is in
  the lists, and a large library mounts several times faster (new in 1.17.0): the loop loads each archive's package
  before it opens the next, the asset loader thread does not pause 10 ms between files while archives are mounted,
  the flag is held for manifests still to come, and a step no longer asks every mounted archive for the shader
  cache's files. With 2,326 archives 1.16.0 showed the text for 24 s and went on loading for 33 s after it (a
  song list opened 3 s after the text had gone had 1,227 of 2,037 matches); now the text and the last song end together
  after 5 to 8 s, and 147 archives take half a second.
- **FFT setups** (new in 1.17.0): the game made a vDSP FFT setup for each song and freed none, about 1 MB a song
  for the whole run. One of each size is kept and handed out again (`RS_MODS_FIXES`). Checked for nine minutes over
  two songs, with a guitar not played.
- **Calibration and the tuner at any frame rate** (new in 1.16.0): what the game counts in frames is put right by the
  frame's own time, for frames shorter than a 60 Hz one; at 60 Hz the game runs as before.
- **Skip Tuner in the Guitarcade** (new in 1.16.0): a Guitarcade game's tuner has the Skip Tuner a song's tuner has,
  with the same warning. The game left it out there, so only a finished tuning got a player into a game.
- **The white loading screen and Learn a Song's long songs** (not released): a menu movie the game asked the file index
  for too early is asked for again when its file is indexed, and Learn a Song's page shows four alternate arrangements
  for a song with more than five, which crashed it, and the arrow keys and the wheel scroll the rest. Both are one
  source with Learn & Play; neither has been run in the game yet ([Open issues](#open-issues)).
- **Learn a Song's scroll bar** (new in 1.18.0): it can be dragged from anywhere on it, through the menu overlay's copy
  of the song list; the game let it be grabbed only near the top of the list. The same for the game's 24 other lists,
  the Tone Designer's among them, through the overlay's copies of their movies, and the 16 of them that had no bar
  have one: Nonstop Play's songs, the inlays, the Tone Designer's amps, pedals, cabinets and racks, Session Mode's and
  more.
- **A fixed frame rate** (new in 1.16.0): the rule and clock of `RS_FPS=N`, the installer's Frame rate, are one
  source that Learn & Play uses too (`shims/platform/include/rsp_fps.h`); this edition runs as before.
- **Shared with Learn & Play** (new in 1.16.0): the switches, the log and the start of the built-in fixes, the intro
  skip, the song ownership during a mount, Player 2's saved input, the frame rate fixes, and what the batched DLC
  mounting does around its steps (new in 1.17.0), are one source for both
  editions (`overrides/mods/mods_shared.c`, `framerate.c`), and Learn & Play's own fixes name the game through the
  tables as these do. One thing changes for this edition's player: the shared hook for Player 2's saved input frees
  the name's text when the game had to allocate it (a name longer than about 15 characters), which this edition's own
  hook left behind at every call.

**Installer and game files**

- **Installer**: the GUI installer decrypts the game statically (no Rosetta, no launch of the game).
- **The installer updates itself** (new in 1.17.0): a newer release is offered at the installer's start, and Update
  Now replaces the installer with that release's once its signature checks out
  ([DESIGN.md](DESIGN.md#installer-and-build)). The game is rebuilt only when the user clicks Build & Install.
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
  guitar and bass); Player 1's hits and misses and the track's grey-note time, in Learn a Song and Score Attack, reached
  as the game's own code reaches them, from the game root's running game to its component of the note counts' type,
  `game_root_state` to `component_type` (new in 1.19.0); the earlier path from the song's menu, `song_menu_game`, did
  not hold from run to run and keeps its names; and hooks on the tone change (`tone_change`, for clearing the wait
  behind a stuck tone), on the song lists' names (`song_list_name`) and on decoded song audio (`ak_src_submit`: each
  Wwise source's decoded block on its way to the mixer, with the source's file and position, and the game's Vorbis
  codebooks).
- **Your own plug-in** (rearmed-mods' ToneHost add-on): played with Helix Native, with the game's gear mapped onto it.
  Its open work is kept with it.

## Open issues

**Faults and limits**

- With the legacy fast-load `cache.psarc`, a start can stay on the white loading screen now and then (a race in the
  game). Metal's safe load avoids the stall shader compiles would add. The built-in fix (`RS_MODS_MENU_RETRY`) asks for
  the menu again once its file is indexed; it is built and tested against a game of the test's own, and the game's
  sites are matched by their code, but it has not been played with such a `cache.psarc` yet: until it has, on a white
  screen quit and start again.
- A song with more than five arrangements shows four alternates at a time on Learn a Song's page (it crashed the
  page), and the arrow keys and the wheel scroll the rest, as RS Extended Range's page does (Page Up and Down do not:
  not done). The scroll re-runs the page's builder and handles the movie's nodes, so it is the part of the fix most
  likely to need the game: built and tested against a game of the test's own (both ABIs), its sites matched by their
  code; not run in either game. The co-op page's list (every arrangement in one column, about nine drawn) is windowed to
  eight the same way, by the entries' sort order.
- The first play of a song after an install stutters briefly on Metal (pipelines built on first use). A draw that
  waits for a shader compile hurries it; the gain isn't measured.
- Mods that draw with OpenGL show nothing on Metal; they need the overlay API.
- The tuner's red peg has not been compared with the original game, and after the rule that brought it back this
  edition was run at the built-in screen's size only.
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

- **Rocksmith 1 DLC found as owned** (new in 1.17.1): no account that owns one has been at hand. The asking of the
  Steam client, the tickets and the package cache were run against the real Steam with apps the test account does own
  (the first Rocksmith, the import pack, a DLC of the game: yes for each, no for a DLC it lacks), an installed first
  Rocksmith only with a made-up appmanifest, and the rest with stand-ins ([TESTING.md](TESTING.md#unattended-runs)).
  In the game the DLC was named by hand (`RS_STEAM_OWNED`): the song is listed and plays with it, and is not listed
  without. Not looked at: an account that owns a first Rocksmith DLC and not the first Rocksmith itself, which players
  of the original reported as losing these songs.
- **The published installer**: recent releases' hasn't been run, nor a full install from the tag (the last was
  1.6.0's); the install scripts and test installs were.
- **The installer's update of itself** (new in 1.17.0): run on test copies with made-up releases, in the window too.
  Not yet on a copy macOS approved at its first start (it may run from a read-only place, or be guarded against
  being replaced; both end at the release's page), nor against a real release, which needs the first one that is
  signed ([TESTING.md](TESTING.md#installer-tests)).
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
- **Batched DLC mounting**: not run with the new loop: Enumerate with new archives to mount (a rescan with none
  was: `147 archives mounted in 3 frames, 0.18 s`), an enumeration started while the flag is held for manifests
  (the hold then ends and the new archives are stepped: in the shared source's test only), the song count against
  a step a frame (`RS_MODS_BUDGET_MS=0`; the same with 147 archives in 1.16.0, not compared since, nor with a
  large library), an
  archive in `dlc/` that brings a shader's hash file (the step's question then goes to
  the whole file system; none exists to try), and a package or manifest that does not come (the mounting then goes
  on after 5 s, with a line in the log; the hold itself was run, with the loader's pause left at the game's and
  without, and a manifest that does not come is in the shared source's test). Not run with a large library since
  the manifests, the loader's pause and the file questions became one source with Learn & Play: 147 archives gave
  the lines they gave before. What a mounting still costs is the game's own: reading each archive's table and its
  song manifest, 2 to 3 ms an archive on the game's thread, and each file opened asks the archives mounted before it.
- **The fixes from the code review of 2026-11-09**: six songs played with a guitar since, with no fault; no long
  session.
- **A long run** (`tools/perf/soak.py`, thirty minutes of Nonstop Play on a 2,326-archive library;
  [PERFORMANCE.md](PERFORMANCE.md#a-long-run)): memory, CPU, frames and threads stayed level. What it showed and
  is left: each played song's archive stays open, as the game mounts it, 25 more after eight songs,
  which the raised open-file limit is for; and rearmed-mods writes a `game note` line for every note to the log,
  2 a second here and 40 a second in Learn & Play. Not run: longer than half an hour, a guitar, Song Stems
  making stems for new songs (the helper's CPU stayed at 1% of a core).
- **The code review of 2026-11-17** (everything since the one before; new in 1.17.0): its fixes passed the suites,
  and each edition was built on them and run once to the note highway with rearmed-mods, RS Companion and
  songbridge loaded (this one: `147 archives mounted in 6 frames, 0.32 s, their songs in the lists 0.10 s later`,
  1,460 of the 1,470 file questions alone, as before them). Not played with a guitar; the installer app with its
  fixes was type-checked and not run, and no copy was moved or downloaded with the real Steam. What it found and
  left:
  - The frame rate fixes' hooks have no test of their own (their arithmetic has), and three of their points return
    from a function at its start when a 60 Hz game would not have made the call: what else those functions do each
    call, and what a tuner's miss past the third takes off its hold, has not been read in the game.
  - Batched DLC mounting: the archive a step mounted is asked its file questions by the pointer taken at the mount
    call, with no reference held (a mount that fails and still asks has not been met); an answer to a request made
    before a mounting ends its hold one manifest early; a request to clear the coordinator's map that comes while
    the flag is held is dropped, as one is during any enumeration.
  - Files a mod serves: a provider gets paths in the game's spelling (a doubled `/` or a `.` is not removed, and one
    file spelled two ways has two identities); one thread closing a served file while another reads it is a use
    after free where the system gives an error.
  - The installer: with no saved choice it preselects Learn & Play whenever the Mac game is not installed in Steam,
    which no longer means the player has no Mac game; Settings and switches… has not been opened since the two
    newest built-in mods joined it, and no game has run with those in `settings.env`. A copy stopped before its
    manifest was written and then moved to the drive lands there as real copies, not clones.
  - `RS_FPS` with `RS_RENDER_THREAD=0` only turns vsync off. The seam's unit test
    (`tools/ws7/recomp/app/test/build.sh`) is in no suite of `tools/release/scope.py`.
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
