# Mac port: status and plan

The Windows port's plan: [Windows plan](../windows/PLAN.md).

## Status

- **Released**: `VERSION` says which release this is. 1.4.0 is the first with the experimental Windows (Learn & Play)
  port.
- **What works**: the whole game as a native arm64 app, offline: songs, DLC and custom songs, Session
  Mode, lessons with their videos, Guitarcade; the Real Tone cable and other audio interfaces (Direct
  Connect) with lower latency than under Rosetta; Metal by default with OpenGL as choice and fallback;
  full screen, Retina; the built-in fixes; mods loaded at startup with name-keyed hook points; the GUI
  installer. Gameplay CPU is about that of the original under Rosetta ([PERFORMANCE.md](PERFORMANCE.md)).
- **Installer**: the same guide box and Steam steps as Learn & Play. Decrypting is static (`tools/steamless/decrypt.py`):
  no Rosetta, no launch of the game, and Steam only has to have installed it, whichever account is signed in now.
- **Checked on**: one Mac (M2) with a 75 Hz external display and the built-in Retina screen
  ([METAL.md](METAL.md#status) lists what the 1.3.0 release check covered).
- **Checked for 1.4.0** (2026-11-04, the app the installer built, by keys and snapshots): the image matches the pin; the
  title with the logo and no intro screens; DIRECT CONNECT offered, and a guitar through a UR22mkII heard by the
  maintainer; official and custom songs, a video lesson, Session Mode and Nonstop Play; Enumerate and Refresh Game
  Files; OpenGL boots and plays a song; Quit and Exit gone in 0.5 s or less; no signal, nothing refused by the network
  guard. Guitarcade: the menus and the list of games open, but a game stands behind a tuner with no skip, so none was
  played. All suites of [TESTING.md](TESTING.md) pass; the figures are in [PERFORMANCE.md](PERFORMANCE.md).
- **Full-screen shape**: on a 16:10 screen the 16:9 picture is stretched, as in the original under Rosetta. The Windows
  game's bars are its own code, which the Mac game does not have.
- **Enumerate** (new in 1.4.0, both editions): SHOP is ENUMERATE, second in the main menu, and brings new songs in
  without a restart ([DESIGN.md](DESIGN.md#built-in-fixes-and-mods)). Changed and removed songs still need a refresh.

## Open issues

- With the legacy fast-load `cache.psarc`, a start can stay on the white loading screen now and then (a
  race in the game). Metal's safe load avoids the stall shader compiles would add; on OpenGL quit and start
  again.
- The first play of a song after an install stutters briefly on Metal (pipelines built on first use).
- Mods that draw with OpenGL show nothing on Metal; they need the overlay API.
- Renderer details: `RS_GL_CHECK` keeps one pending error per thread, not per context;
  `GL_TEXTURE_BASE_LEVEL` and `GL_TEXTURE_LOD_BIAS` are ignored on Metal; shared CGL contexts are not
  supported on Metal; only the first GPU gets a disk cache.
- Not checked yet: a second Mac or GPU, 120 Hz ProMotion displays, sleep and wake, unplugging a display, the
  Retina mouse in songs, a Guitarcade game played with an instrument, and a fresh player's first answer to macOS's
  question about another disk with the compiled launcher (an earlier Allow covered the one run).
- Changing the default output during a run doesn't move the game: the sink stays on its device. Learn & Play
  follows the change ([TESTING.md](TESTING.md#game-runs)).
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

## Next work

Being looked into, with no promise of if or when:

- **RSMods**: more of [RSMods](https://github.com/Lovrom8/RSMods)' features on RS Re-ARMed.
- **Input channel selection**: choosing which input of an audio interface with more than one input the game
  listens to.
- **One code base for both games**: the shared pieces (renderer, Steam layer, audio policy, installer)
  already serve the Windows port; its parity work (hook points and built-in fixes by name on both targets)
  is in the [Windows plan](../windows/PLAN.md#next-work).
- **Build options**: `-mno-outline` for the generated C gave the Windows port 4% of its game thread
  ([Windows performance](../windows/PERFORMANCE.md#opportunities-by-expected-payoff)); not measured on the
  Mac yet.

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
