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
- **Exit Game** (new in 1.9.0): EXIT GAME in the main menu, in place of UPLAY, pressed in a test install of each edition.
- **Game Mode** (new in 1.9.1): the app's category is music, so macOS leaves Game Mode off, which made a plug-in host crackle. Played on an installed game changed by hand; not yet run in Learn & Play.
- **Two players** (new in 1.8.0): a song with each player on their own input, played on two channels of one interface, SWAP CABLE ASSIGNMENT included; a cable and an interface and two interfaces have not been played.
- **Rocksmith 1 songs**: the disc pack's and the DLC pack's are listed and played; the DLC pack's need a refresh with the owning account logged in to Steam.
- **Installer**: decrypting is static (`tools/steamless/decrypt.py`): no Rosetta, no launch of the game, and Steam only has to have installed it.
- **Performance**: about the CPU of the original under Rosetta. 6.1 ms a frame on [Learn & Play's route](PERFORMANCE.md#learn--plays-route), 45% of a core, against its 7.2 ms.
  The 12 functions relaxed and the renderer's change of 2026-11-06 played well with a guitar; an install of them by the installer app is still to do.
- **Screen shape**: the game sets its cameras from its resolution (their height kept, the width the resolution's) and lays out its menus to
  the width, so a 16:10 window draws undistorted (checked 2026-11-08, frustums 0.925 x 0.577 at 1153x720 against 1.026 x 0.577 at 16:9).
  Full screen renders at the screen's size the same way; not yet run on a 16:10 or ultrawide screen. A window now takes any shape
  (`RS_WINDOW_ANY_SHAPE`; the original kept 16:9). Learn & Play fits its picture to 16:9 with bars ([Windows plan](../windows/PLAN.md#next-work)).
- **Retina and colour** (new, Metal): on a Retina screen each frame is scaled up to the screen's pixels with MetalFX while the
  game renders in points, and the picture is tagged sRGB, so wide-colour screens show the game's colours as made
  ([METAL.md](METAL.md#metal-core-shimsopenglsrcmetal)). The same renderer, and the same request from its window, serve Learn & Play.
- **Sharper picture** (new, Metal): the game's own texture LOD bias, which Metal had dropped, 16x anisotropic filtering where the game asks for
  1x to 4x, and 4x MSAA on its screen-sized 3D targets ([METAL.md](METAL.md#metal-core-shimsopenglsrcmetal)). The same renderer serves Learn & Play.
- **Checked on**: one Mac (M2) with a 75 Hz external display and the built-in Retina screen.

- **Your own plug-in** (rearmed-mods' ToneHost add-on): played with Helix Native, with the game's gear mapped onto it. Its open work is kept with it.

## Open issues

- **The published installer**: 1.9.1's hasn't been run, nor a full install from the tag. Exit Game and the colors were checked on test installs. The last full install from a published installer was 1.6.0's.
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
- The installer keeps its preferences in the user's defaults even when run with a test `HOME`
  ([TESTING.md](TESTING.md#installer-tests)).

## Next work

Being looked into, with no promise of if or when:

- **RSMods**: more of [RSMods](https://github.com/Lovrom8/RSMods)' features on RS Re-ARMed.
- **Choosing both players' inputs on the Multiplayer screen**: each picks through their own profile today. The game's input list is
  hard-wired to Player 1 (0x1001e6660), and SWAP CABLE ASSIGNMENT could open it for Player 2.
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
