# Windows port: status and plan

Released since 1.4.0. The Mac port's plan: [Mac plan](../mac/PLAN.md). Results of the last runs are in [TESTING.md](TESTING.md) and
[PERFORMANCE.md](PERFORMANCE.md); earlier releases are in git and on the [Releases](https://github.com/bbelgg/rs-rearmed/releases) page.

## Status

- **Works**, checked on an Apple Silicon Mac: the game offline with the Uplay screens skipped; songs to the end, Score Attack, the Guitarcade, Session Mode,
  Nonstop Play, video lessons, the Tone Designer's Edit Tone, a Multiplayer song with each player on their own input (new in 1.8.0); custom songs and DLC; Enumerate, EXIT GAME (new in 1.9.0) and Refresh Game Files; a Real Tone cable
  device (a virtual one named like the cable), the microphone and other interfaces through Direct Connect; every channel in the input list, input channel and gain,
  calibration that can boost; the output following the system's default; window, full screen and Retina; Command-Q and the close button, in 0.3-0.5 s.
- **Mods**: 15 of the 18 hook points (`si_io`, the Mac game's CoreAudio input, has no counterpart, and the two of two players are in `win32_overrides.c`), the overlay API, the `mods/` folder and one mod ABI with the Mac
  ([DESIGN.md](DESIGN.md#built-in-fixes-and-mods)). `rearmed-mods.dylib` runs as the same file: 0.6.0 played here, and 0.8.0, which the Mac has loaded, not run here yet. No mod code runs without the network guard.
- **Colors of notes, the highway and strings** (new in 1.9.0): as in the [Mac plan](../mac/PLAN.md#status), through the render API and the game's string color table, checked here with rearmed-mods 0.5.0. Here the
  ramps are BGRA, so their hashes differ from the Mac's; 210 of the game's textures have the same hash in both editions, the compressed ones among them.
- **Game Mode** (new in 1.9.1): the app's category is music, as in the [Mac plan](../mac/PLAN.md#status). Not yet run in this edition.
- **Retina upscaling and sRGB colour**: as in the [Mac plan](../mac/PLAN.md#status), `lp_window.m` asks for the backing-pixel
  output and the shared renderer scales with MetalFX and tags the layer sRGB. Not yet run in this edition.
- **Sharper textures and MSAA**: the renderer is shared, so the game's texture LOD bias, 16x anisotropic filtering and 4x MSAA on
  screen-sized targets with depth are here too ([Mac plan](../mac/PLAN.md#status)). Not yet run in this edition.
- **Performance**: a song holds the display's rate, 74.9 fps on 75 Hz, with no frame over 20 ms, at 7.2 ms of CPU a frame, 54% of a core, where the Mac takes
  [6.1 ms](../mac/PERFORMANCE.md#learn--plays-route). The Mac's figure has the renderer's last change (fewer references, one pass descriptor); this edition's 7.2 ms predates it, and it hasn't been measured here. The title is up in 7-9 s. Guest memory doesn't grow over songs in a row.
- **Present but not tested**: a cable and an interface or two interfaces in Multiplayer, SWAP CABLE ASSIGNMENT on interfaces, the Workshop's other screens, the DLC preview, the prebuilt harvester
  (`win-x86/harvest/rsharvest2.exe`), and Rocksmith 1 DLC songs in a game run (`test_steam32` covers `RS_STEAM_OWNED`; the test account owns none).
- **The tables** (`tools/targets/win-x86`): every name has an entry, and `check.py` covers both targets. 987 of the 1,178 symbols and 79 of the 100 offsets are read from
  the Windows code; the map says why for the rest. 88 names have `custom` conventions (LTCG register arguments), which their overrides read by hand.
  No entry is left "not proved" (2026-11-08): of the 42, 20 have their address, from the Wwise plug-ins' registration (`0x17c4ab0`, the Mac's order and ids) down
  to each kernel's call site, and the Vorbis, mixer, FFT and efd code by their tables and callers. The other 22 are inlined, folded or specialised by LTCG, have no
  counterpart, are called only by the Mac's differential tests, or are found but left out because they can't be relaxed (a static's guard, a CRT call).
- **The game image**: unpacked on the Mac from the user's own exe with the release's seed, or a capture for a build without one
  ([win-x86/unpack](../../win-x86/unpack/README.md)). A new exe build needs a loaded copy of the game for its relocation sites, which nothing reads out of the capture VM yet.

- **Your own plug-in** (rearmed-mods' ToneHost add-on): played with Helix Native at 64 frames, as on the Mac. Its open work is kept with it.

## Open issues

- **The published installer**: 1.9.1's hasn't been run, nor a full install from the tag. Exit Game and the colors were checked on test installs.
- **The title screen's own Auto Save call** (`0x12bd061`): read from the code, not seen in a run. The title listens for the same `OnSignInFlowComplete` (`0x12bdd00`), and with
  slot 0 of the object at `[[0x1ee0bfc] + 0x30]` and its byte `+0x14` set, its tick (`0x12bcfd0`) shows the warning for 4 s. What sets that byte isn't known, so whether a player can
  reach it isn't either. If so, the skip needs the tick wrapped too, to clear its timer.
- The cable screen's Direct Connect button hasn't been checked.
- The list of inputs and the level, not checked yet: unplugging an interface while the game runs, a Real Tone cable with this
  build, and the level with a second profile, which counts as calibrated with the first.
- winmm's device names stop at 31 characters, so on that fallback path a long interface name can lose its channel.
- **Two crashes seen once each**: one on the stage at the tuner skip (`lp: signal 10: guest address
  0x0`), a fault at guest address 0x168 before it. Neither reproduced on demand yet; neither came back in the
  runs of 2026-11-04 (about ten tuner skips, on builds with x86 memory ordering and one without).
- **Two COM object tables**: the D3D9, audio and Steam faces share the seam's thunk cells, vtables and guest
  memory (`win32_guestobj.c`), but the audio and D3D9 faces still each keep their own table of objects and
  their own `IUnknown` (D3D9's also has internal references and forwarded counts).
- Guitar latency against the Mac port has not been measured.
- **CPU and memory**: about 1.1 ms a frame over the Mac edition on one route, and 1.3 GB against 0.9 GB. The memory order costs about 1 ms a frame at the display's rate ([PERFORMANCE.md](PERFORMANCE.md#memory-order)).
- **Shaders the list doesn't have**: the corpus has the programs seen in runs, and the Guitarcade's games wait for a guitar at
  calibration, so of them it has only their effects, and of the venues (Venue Mode) one was seen, with four programs of its own. Run them
  with `LP_SHADER_LOG` and add them ([TESTING.md](TESTING.md#shaders-of-a-run)). A program not in it stalls about 0.25 s the first time, as before.
- **After a macOS update** the first play compiles again unless the warm-up is run again; the Mac edition does it at its next
  start when safe load is on. In Learn & Play it would be `rs_gfx_prepare` at context creation, with a line on the black window.
- **One DLC archive differs between depots**: 113 PC archive files in the depots are 73 different archives, and one
  name (`notegew2_p.psarc`) has two versions 2 bytes apart. The copy keeps one; what the bytes do has not been read.
- **Song archives per platform**: the Windows game reads the PC archives, `*_p.psarc`; the Mac game
  reads `*_m.psarc`. Official DLC has both (each DLC is a depot with the two), but custom songs often
  exist only for one platform, so a Mac-only CDLC collection (`_m`) will not load in this build, and a PC one
  not in the Mac port. Compared on one official song, the two archives differ only in folder names
  (`songs/bin/macos`, `audio/mac` against `songs/bin/generic`, `audio/windows`), the `.nt` graph, and the `.sng`
  files, which are the same size but encrypted with a key per platform; the audio banks are byte for byte the
  same. So an install-time converter is simple in itself, but it needs the other platform's key, which is
  not in the game a player has here, and nothing from the game may be in this repository. Left for the
  maintainer to decide.
- D3DX paths not exercised yet: `D3DXCreateEffectCompiler`, `D3DXCreateTextureFromFileExA`, the cube and
  volume loaders, `D3DXSaveSurfaceToFileInMemory`, effects with textures.
- Fixed function not implemented (logged once): vertex blending, sphere maps, bump mapping, user clip
  planes, point sprites.

## Next work

**Parity (P4):**

- The checks the Mac port has had that are left: long sessions, and guitar latency against it.

**Guitar input:** choosing both players' inputs on the Multiplayer screen, as in the [Mac plan](../mac/PLAN.md#next-work).

**Screen shape**, as in the Mac edition ([Mac plan](../mac/PLAN.md#status)): the Mac game sets its cameras and menus from its resolution, so
a 16:10 or ultrawide picture needs nothing more there. This game fits its picture to 16:9 inside the back buffer itself
(`GRDisplayRatioAdjuster`, [DESIGN.md](DESIGN.md#screen-and-window)); its rectangle would have to be the whole back buffer, and the cameras
and menus then checked. Not started.

**Performance** ([PERFORMANCE.md](PERFORMANCE.md#opportunities-by-expected-payoff)), by expected payoff: the note detection and the
Wwise functions still fenced; the D3D9 layer's bookkeeping; the index-range scan of the draw-time upload; zero-copy dynamic buffers; memory (guest copies of D3D9 buffers).

## Risks

1. **Renderer fidelity**: D3D9 semantics through GL to Metal. The menus, the stages and the note highway
   render; scenes not played yet may show gaps.
2. **Steam updating the Windows build**: the pinned manifest keeps installs working; a new build means a new
   seed, new import tables and a new `target.toml`.
3. **The seed**: the release's installer carries it, and a new exe build needs a new one, from a loaded copy of the game
   ([Status](#status)). Without a seed a user needs a Windows PC once.

## Decisions

- Two targets, one source tree; per-game data only in per-target tables; every native reference by name.
- A 32-bit guest in a reserved 4 GB window.
- Microsoft's DLLs translated, not rewritten; native overrides only for leaf functions with plain contracts
  (the math exports so far).
- Direct3D 9 implemented on `libRSOpenGL`, so the Metal core and its caches are shared.
- Metal only: every Mac this edition runs on has Metal, so no OpenGL renderer or fallback is planned here.
- One pinned Windows build (depot 221683, manifest 8918188356915973501), downloaded by the Mac Steam
  client; no steamcmd.
- No Rosetta, no Homebrew, no .NET in the install: native arm64 and tools macOS ships. The Microsoft DLLs are
  the user's own, from Steam's Common Redistributables or Microsoft's redistributables, checked by SHA-256.
- The game image from the user's own exe, unpacked on the Mac with a seed that holds no game code; for a build without
  one, a one-time capture of the user's own game, with the harvester prebuilt beside its source.
