# How the Windows port works

The Windows port takes the 32-bit Windows build of Rocksmith 2014 that the Learn & Play edition grants
(Steam depot 221683) and makes a native arm64 Mac app of it. The game's code and the Microsoft DLLs it
loads are translated to C at build time by the shared recompiler's 32-bit front end
([recompiler](../recompiler/README.md#the-32-bit-front-end-windows)); a Win32 seam on macOS stands in for
Windows underneath. It reuses the Mac port's renderer, Steam layer and audio policy. User side:
[INSTALL.md](INSTALL.md). Status: [PLAN.md](PLAN.md).

## Overview

```
game image (win-x86/image/lp_image.py, seed or capture)     msvcr100, msvcp100, d3dx9_42, D3DCompiler_42 (Steam's
   |  gen_image_pe.py --wrappers                              Common Redistributables), bink2w32 (game folder)
   v                                                             |  gen_image_pe.py --module NAME
libgame_rc.a                                                     v
   \__________________________  modules/NAME/libgame_rc.a ______/
                              v  link (win-x86/build/build.sh)
   app/lp_main.c + win-x86/shims (Win32 seam, D3D9 on libRSOpenGL, audio, offline Steam) + rc32 runtime
                              v
   rs2014_lp in ~/Applications/RS Re-ARMed Learn & Play.app (with libRSOpenGL, the game and DLL images)
```

| | Mac port | Windows port |
|---|---|---|
| Game binary | x86-64 Mach-O, SysV | 32-bit x86 PE, MSVC conventions |
| Protection | Steam DRM, a repeating XOR | VMProtect, decrypts in memory only |
| Game image | `tools/steamless`, on the Mac | `win-x86/unpack` with the release's seed, on the Mac; else a one-time capture on a Windows PC (`win-x86/harvest`) |
| Renderer | OpenGL 2.1 + Cg on `libRSOpenGL` | Direct3D 9 on `libRSOpenGL` |
| Audio | Wwise CoreAudio sink | Wwise on PortAudio's WASAPI, and winmm, on CoreAudio |
| Shell | `shims/platform` (Cocoa) | `win-x86/app/lp_window.m` under a Win32 seam |
| Libraries | native replacements (overrides) | Microsoft's DLLs, translated |

Gameplay is the same C++ source compiled twice, so once translated it behaves the same. The Windows build
looks like the PC game, drawn through the same Metal renderer.

## Getting the game image

The exe on disk (24,514,584 bytes, SHA-256 `bb056959…2778eb`, pinned in `tools/targets/win-x86/target.toml`) is
VMProtect-packed: the real `.text`, `.rdata` and `.data` exist only in memory while the game runs.

The released installer carries a seed, a small piece of VMProtect's state with no game code in it. With it
`win-x86/unpack/lp_unpack.py` runs the exe's own decompression in Unicorn on the Mac, and `lp_image.py` makes the
game image from the result, with nothing from the user ([win-x86/unpack](../../win-x86/unpack/README.md)). A build
without a seed starts from a capture, which is also where the facts below were first read. `lp_install.py build` stops
on an image other than the pinned one, as a capture with pages missing has the right `.text`, builds and crashes at the
start, and a capture's image, which can differ, goes through with `--unpinned`. `learn-play.sh` and the installer make an
image that isn't the pinned one again from the seed:

- The user runs the game once on a Windows PC where they own it, with the harvester
  ([win-x86/harvest](../../win-x86/harvest/README.md)): a 32-bit .NET Framework 4.8 program that reads the
  game's memory with `ReadProcessMemory` (no debugger, no injection, so the anti-debugging does not
  trigger) and records every page of the game where Windows loaded it, plus each loaded module's exports.
- `win-x86/image/lp_image.py` builds a PE of the game alone from the capture, at the base it ran at
  (0xf80000): the true section table, `.data` as at load (the `.bss` tail zeroed), an import directory over
  the real IAT, a rebuilt relocation table (the game's own is inside VMProtect's packed data; the recompiler
  needs it to find functions reached only through pointers), the entry point `WinMainCRTStartup`, and the
  VMProtect sections without the execute flag. Words that only look like pointers are kept as captured
  (moving them corrupted the SNG signature key, so no song loaded).
- **Imports.** VMProtect encrypted 506 of the 585 import slots and replaced 3,842 call sites with
  single-use stubs. `win-x86/imports` names the plain slots from the capture's exports (`lp_slots.py`) and
  runs each stub site as a black box on the captured image until it reaches an export
  (`lp_wrappers_run.py`, `lp_wrappers.py`). The result, `wrappers.tsv` (3,841 rows) and `slots.tsv`, is
  committed (addresses and names only) and passed to the translator with `--wrappers`.
- No game function was left virtualised: a sweep of `.text` finds no transfer into VMProtect code other
  than the import stubs.

RS Re-ARMed ships no game code: the seed holds none, and a capture is the user's own game's data and stays on their
machines.

## Translation

`win-x86/install/lp_build.py` (run by `lp_install.py build`) translates six images with
`gen_image_pe.py`: the game (about 62,000 functions; finding them is most of its 13 minutes on an M2) and
five DLLs as modules at their preferred bases (msvcr100 `0x78aa0000`, msvcp100 `0x78050000`, d3dx9_42
`0x10000000`, D3DCompiler_42 rebased to `0x11000000`, bink2w32 rebased to `0x12000000`). It compiles each
tree with a small ninja-subset runner of its own and links with `win-x86/build/build.sh`, so CMake and Ninja
are not needed. `win-x86/build/` also has the scripts to do each step by hand
([win-x86/README.md](../../win-x86/README.md)).

## Per-game tables

`tools/targets/win-x86` has the Learn & Play image's names (966 of the Mac's 1,156 symbols read, an
`unsupported` line with the reason for the rest), and in its `target.toml` the pins (the depot, manifest, exe
and DLL hashes, build figures). Addresses it needs are in `win-x86/imports/*.tsv`
(import sites) and `win-x86/online/names.tsv` (the vtables the online layer patches, with their Mac
counterparts). The build uses them for the hook points and what a mod asks for by name; the built-in fixes name their addresses in
`win32_overrides.c`, and the Mac overrides are not built here ([PLAN.md](PLAN.md#next-work)).

## Runtime

The 32-bit runtime (`tools/ws7/recomp/runtime/rc32_*`,
[recompiler](../recompiler/README.md#pe-images-several-per-process)):

- **The 4 GB guest window.** arm64 macOS keeps the low 4 GB unmapped, so the runtime reserves one 4 GB
  region and every guest access is `rc_mem + (uint32_t)addr`. Images, guest heaps, stacks, TEBs and every
  object the guest holds a pointer to (Steam interfaces, D3D9 objects, COM vtables) live inside it.
- **The image registry** maps each image at its base, binds imports between images (data exports
  included; `msvcrt.dll` is an alias of msvcr100), runs `DllMain` in dependency order and answers
  `GetModuleHandle`, `GetProcAddress` and `LoadLibrary` for translated modules (D3DCompiler_42 is
  deferred: d3dx9 loads it with `LoadLibraryA`).
- **SEH** for the game's `__try` blocks, C++ exceptions, `setjmp`/`longjmp` and the anti-debugging checks
  ([recompiler](../recompiler/README.md#seh-and-c-exceptions)). A fault in guest code is not an exception
  here: it ends the process, with the fault logged.
- **Memory order.** Everything translated is compiled for x86's order, as on the Mac: the game relies on it
  between its threads ([recompiler](../recompiler/README.md#memory-ordering)).
- `win-x86/app/lp_main.c` registers the native overrides, binds the images, installs the online layer and
  calls `WinMainCRTStartup` on a thread with a large host stack. An import no image exports goes to the
  Win32 seam by name (`win-x86/shims/win32_dispatch.c`).

## Platform

`win-x86/shims` is Windows, as much as the game and the DLLs use, on POSIX and Cocoa:

- **kernel32** (`win32_kernel32.c`, `win32_kfile.c`, `win32_ksync.c`): files (the game folder is
  `C:\Rocksmith2014`, read-only, its writes redirected to the user folder; `C:x` paths relative to the
  drive's current directory), threads, events, mutexes, semaphores, waits (each wait sleeps on its own
  condition and is woken only by its objects; `LP_WAKE_ALL=1` restores the old broadcast), TLS and FLS,
  time, heaps, `GetVersion` (6.x, so the game takes its Vista paths).
- **user32** (`win32_user32.c`): window classes, the message queue, `SetWindowLong`, `MoveWindow`, system
  metrics from the real screen. The host window is `win-x86/app/lp_window.m`.
- **The rest** (`win32_misc.c`): advapi32 (no registry), ole32 (one apartment; the audio classes), shell32
  (`ShellExecuteA` opens nothing), setupapi (no devices), rpcrt4, dinput8, xinput1_3 (no controller),
  gdi32, avrt.dll (all six MMCSS functions, which PortAudio's WASAPI start-up needs).
- `win32_guestobj.h`: COM objects as guest vtables of stdcall host cells, the way every COM face here
  (audio, D3D9, Steam) is built.

### Screen and window

The game sees the host's main screen as its desktop (`GetSystemMetrics`, `EnumDisplaySettings`, the D3D9
display mode): in points, or in backing pixels with `RS_METAL_RETINA=1` (`RS_D3D9_DISPLAY=WxH` overrides).
`[Renderer.Win32] Fullscreen` in `Rocksmith.ini` (in the game: Options > Visual Settings > Windowed Mode):

| `Fullscreen` | Device | Window |
|---|---|---|
| `1` Non-Exclusive Fullscreen (the default the game writes) | windowed, the desktop's size | borderless over the screen |
| `2` Exclusive Fullscreen | full screen, `ScreenWidth` x `ScreenHeight` (0: 1280x720) | borderless over the screen, the back buffer letterboxed; the display mode never changes |
| `0` Windowed | windowed, `ScreenWidth` x `ScreenHeight` | a titled, resizable window of that size in points, shrunk to fit the screen |

Changing it in the game resets the device and restyles the window at once (`lp_window_style` from
`SetWindowLong`, `d3d9_platform_resize` from `IDirect3DDevice9::Reset`). `RS_FULLSCREEN=1|0` pins the mode.
The Metal layer's drawable is always the back buffer, shown aspect-fit in the window, so resizing and
macOS full screen need nothing from the game; the pointer goes back through the same fit. On a screen that is not
16:9 the bars inside a desktop-sized back buffer are the game's own: `GRDisplayRatioAdjuster` fits the picture to 16:9
(its rectangle at +0x28, the pointer mapped back at 0x13ab9e0). The Mac game has no such fit and stretches to the
screen, the original too ([Mac plan](../mac/PLAN.md#status)). The rules are
plain C in `win-x86/app/lp_screen.c`.

The screen saver and display sleep stay off while the game is open, in a window or full screen: `lp_window.m`
takes a display-sleep assertion when it makes the window and holds it until the process ends, as the Mac game's
view does. An offscreen run (`LP_OFFSCREEN=1`) makes no window and takes none.

A hidden window (another app in front of the full-screen one, minimised, covered) has no layer pacing its presents, so
`windowDidChangeOcclusionState` in `lp_window.m` gives the D3D9 layer the display's period, and Present keeps to it
(`d3d9_hidden_wait`).

## Renderer

### Direct3D 9

The game renders with Gamebryo's `NiDX9Renderer` and Scaleform's D3D9 HAL. `win-x86/shims/d3d9` implements
the `IDirect3DDevice9` subset they use on the same `gl*` entry points the Mac game calls, so the Metal
core and its caches are shared with the Mac port
([Mac renderer](../mac/METAL.md)). Only Metal presents to the window: `rs_gfx_create_layer` has no
OpenGL path, so `RS_GL_BACKEND=gl` runs offscreen (the D3D9 tests use it on both backends).

- **States and draws**: vertex declarations and FVFs, render and sampler states, state blocks,
  `DrawPrimitive`/`DrawIndexedPrimitive` (and the `UP` forms), `Lock`/`Unlock`, render targets, `Clear`,
  `StretchRect`, `ColorFill`, `Reset`; with the D3D9-to-GL conventions (half-pixel offset, depth range,
  row-major constants, BGRA vertex colours). X8R8G8B8 textures sample with alpha 1 (Bink writes 0 there).
- **Dynamic buffers**: an `Unlock` of a dynamic buffer only marks the range; a draw uploads the part it
  reads (`LP_D3D9_EAGER=1` uploads at `Unlock`). The game locks its 1 MB dynamic vertex buffer whole about
  2,900 times a second.
- **Shaders**: SM2/SM3 bytecode is translated to GLSL 1.20 (`d3d9_shader.c`), the dialect the GLSL to MSL
  path takes; all 1,226 shaders in the game's files translate. No shader of a game run has come from
  `D3DXCompileShader`, though the game can use it: a material's shader is found in the Gamebryo caches of `static.psarc`.
  The build's shader corpus and the install's warm-up compile the programs ahead ([Shader corpus](#shader-corpus)); one
  they don't know is compiled by Metal the first time it is used, and is in the disk cache from then on.
- **Fixed function** (`d3d9_ffp.c`): Gamebryo sets fixed-function state every frame and keeps a
  fixed-function shader for meshes without one. A draw with no vertex shader, no pixel shader or neither
  gets a generated program, cached by everything it depends on: `XYZ`/`XYZW`/`XYZRHW` positions, world x
  view x projection, per-vertex lighting (directional, point, spot; material sources, specular, ambient),
  texture coordinates by `TEXCOORDINDEX` with generation and transforms, vertex and table fog, up to 8
  texture stages with every operation but `PREMODULATE` and the bump-map ones. Logged once and not
  implemented: vertex blending, sphere maps, bump mapping, user clip planes, point sprites.

### Shader corpus

As on the Mac ([METAL.md](../mac/METAL.md#shader-translation)): MSL made at build time from the user's game files, in the
app (`Contents/Resources/rsmsl-corpus.bin`), found by `rsmsl_translate` and compiled by the install's warm-up. What differs is
where the shaders come from and how they pair.

**Where a program comes from**, from `LP_SHADER_LOG` runs of the game's modes: 1,039 shaders created at every start, all of them
bytecode in the game's files; the 1,226 there are in the image (Scaleform's), in the Gamebryo caches `shaders/generated/*.cache`
(the standard material's), in `shaders/dx9/*.vso`, `*.pso` and in the effects `*.fxo` of `static.psarc`. None
was compiled at run time and no fixed-function program was linked. A first play to the highway links 98 programs.

**Pairs**: a vertex shader's GLSL depends on the pixel shader it is linked with, so a program is a pair, and any pair could happen.
By their semantics 103,012 pairs could; the game has used 119 in every mode we could reach.
For effects the files tell: every effect program seen is the vertex and pixel shader of one `.fxo`, or a `.vso` and `.pso` of one
name, so those pairs are all made (208, where the pixel shader's inputs are among the vertex shader's outputs). For Scaleform's and
the standard material's shaders nothing in the files says which: their cache keys are per stage. Those pairs come from runs:
`win-x86/shaders/programs.tsv` lists each program seen as `pair VS PS`, hashes of the bytecode, or `ffp VS PS KEY` with the
fixed-function state key. No shader text or bytecode, so it can be in the repository; `record_programs.py` adds a run's
`LP_SHADER_LOG` to it. Runs: menus, Learn a Song, Score Attack, Session Mode, a video and a practice lesson, Tone Designer,
split-screen Multiplayer, Riff Repeater, Venue Mode (one venue), and the game's quality settings, which added none. The Guitarcade's games need a
guitar to get past calibration, so only their effects are in.

**The build**: `tools/metal/make-corpus.sh --learn-play` takes the shaders of `static.psarc`, and `win-x86/shaders/lp_shaders.c` looks
through them and the image for bytecode (at any byte offset) and writes the GLSL of each listed program, by the layer's own
`d3d9_sm_glsl` and `d3d9_ffp_sources` (`d3d9_ffp_gen.c`, which depends on the state key alone), plus the layer's blit. Then the Mac's
steps: `build-msl-corpus.sh --pairs`, `msl_verify --translate-only`, `merge_corpus.py`. A list line whose shader isn't found is
skipped. A failed corpus doesn't fail the build.

**The warm-up**: `--warm-shaders` in `lp_main.c`, before anything of the game is loaded, calls `rs_gfx_warm_only`, the loop the Mac
app runs ([METAL.md](../mac/METAL.md#safe-load)). There is no safe load: its reason on the Mac is the loading screen's race with a
fast-load `cache.psarc` and a busy CPU, and after the warm-up nothing here keeps the CPU busy while the game loads
([PERFORMANCE.md](PERFORMANCE.md#shader-preparation)). A program the list doesn't have is compiled when the game first draws with
it, and warmed from the manifest at the next start.

### D3DX

The game imports 22 functions of `d3dx9_42.dll`: matrices, the HLSL compiler and assembler, constant
tables, the effect framework, texture and image loading. They run as Microsoft wrote them: d3dx9_42 and
D3DCompiler_42 (which d3dx9 loads at run time for compiling) are translated, and their `msvcrt.dll`
imports are served by the translated msvcr100. Device calls go through the vtable to the D3D9 layer. 3DNow!
paths in d3dx9's math stay unused (the reported CPU is GenuineIntel without 3DNow!, and
`IsProcessorFeaturePresent` says no to it); its SSE2 table is what runs, as in the D3DX probe.

## Libraries

The game and D3DX need msvcr100 and msvcp100 (VC++ 2010 SP1 x86) and d3dx9_42 and D3DCompiler_42 (DirectX
June 2010). They are translated, not rewritten: the guest runs Microsoft's own heap, stdio, locale, C++
exceptions, RTTI, iostreams and `std::string`, with exactly the layouts the game's code expects, and the
seam is the kernel32 the game needs anyway. None of these DLLs, nor anything generated from them, is
shipped: the installer takes the user's copies ([INSTALL.md](INSTALL.md#install-from-a-clone)).

**Native math overrides** (`win-x86/shims/win32_crtmath.c`, `rc32_override_add`): the note detection calls `_CIpow` four times per spectrum element, so every math export
the five images import runs on the host's libm, with the CRT's error rules and calling convention. Within 4 ulp of the translated code, 2-6 times faster, and the note detection
uses 30% less CPU. `LP_NATIVE_MATH=0` turns them off. `memcpy`, `memmove`, `memset` and `memcpy_s` are native the same way (`LP_NATIVE_MEM=0`), fenced once around the call.

## Audio

The game reaches its audio devices through PortAudio (linked into the exe) on WASAPI, and winmm. Wwise's
sink is a PortAudio output stream on the default render device (48 kHz, stereo or 5.1, 256 frames, 128 in
the game's low-latency mode; exclusive with `[Audio] ExclusiveMode=1`). The guitar input is a PortAudio
input stream per device (48 kHz mono float, always exclusive), or waveIn on the fallback paths.

`win-x86/shims/audio` gives it CoreAudio underneath: the COM objects PortAudio calls (`IMMDeviceEnumerator`,
`IMMDevice`, `IPropertyStore`, `IAudioClient`, the render and capture clients, `IAudioEndpointVolume`) in
`wa_com.c`, winmm's waveIn and mixer in `wa_winmm.c`, and one CoreAudio HAL unit per stream with a ring
between it and the guest in `wa_engine.c`. Every CoreAudio device is a render and/or capture endpoint,
inputs first, with stable ids. In exclusive event mode the ring holds two periods and the device's I/O
thread signals the guest's event.

**Latency, as the Mac port does it** (shared code in `shims/coreaudio/rs_audio_host.c`): the device's I/O
buffer is `RS_AUDIO_FRAMES` (default 256), at most 128 on a device with inputs; `GetDevicePeriod` reports 256
frames as the default and 128 as the minimum; the feeding guest thread joins the device's audio workgroup
as a real-time thread (`RS_AUDIO_RT`); stale input is trimmed (`RS_AUDIO_INPUT_TRIM`); devices are set to
48 kHz when they can be (`RS_AUDIO_SET_RATE`). Exclusive output at a 256-frame period is about the Mac
build at `LatencyBuffer=2`.

**The guitar input's channel and gain** (shared code in `shims/coreaudio/rs_audio_input.c`): the capture stream reads
the device's channels and gives the game the one `guitar-input.ini` sets, whatever number of channels the game opened,
with the gain and a clip at full scale ([INSTALL.md](INSTALL.md#guitar-input)). The Mac port does this in a hook on the
game's own input; here the stream is ours, so it needs no hook point (`si_io` is `unsupported`).

**The list of inputs and an input's level** (the same shared code; [Mac design](../mac/DESIGN.md#built-in-fixes-and-mods) says
why). `wa_engine.c` lists an interface's channels as mono endpoints, with the channel in the endpoint, in the priority
order the game's limit of 10 needs. The game sets the level through `IAudioEndpointVolume` and the winmm mixer, which share
one value per endpoint (`wa_com.c`), and calibration steps it as on the Mac. For any input but the cable that value is
the shared level, applied in the capture callback with the clip and the peak report. Here the value started as a volume
of up to 1.0, so it could only cut.

**Two players on any input** (the [Mac design](../mac/DESIGN.md#built-in-fixes-and-mods) says why). `win32_overrides.c` has the same
two changes as `mods.c`: `F_11f08d0` is the Mac's three inlined checks in one function, which sets Player 2 to Real Tone, and
`F_1344640` is `input_bind`, with the input in `eax`. The profile's string is the Mac's with 32-bit pointers.

**Cable detection.** On the PortAudio path the game accepts an input whose USB IDs are in its table
(vendor/product 12BA/00FF is the Real Tone cable), read from the endpoint's device instance id; on the
waveIn path it takes the first device named "Rocksmith". The layer gives each USB device an instance id
`{1}.USB\VID_xxxx&PID_xxxx&MI_00\…` from its CoreAudio model UID, and a device named "Rocksmith USB Guitar
Adapter" (the cable's name on both systems, also a virtual device of that name) is 12BA/00FF. Any other
device's "Rocksmith" is renamed "RS", so a virtual device does not raise the game's "Real Tone Cable is not
configured for best performance" warning or win the waveIn scan (`RS_AUDIO_RS_NAMES=0` keeps the names).
A player's input mode picks the slot: a Real Tone slot only in Real Tone mode, a microphone slot only in
Microphone mode, any other input in Microphone or Other Cable mode.

The system's default output changing reaches the game's `IMMNotificationClient`s (`wa_com.c`): Wwise's acts only on
`OnDefaultDeviceChanged(eRender, eMultimedia)`, and then makes a new sink on the new default, which is what the Mac
edition's override does by hand. Other device changes are seen at the game's next scan. Exclusive mode takes any format
the HAL unit can convert, not only the device's own.

### Your own plug-in

rearmed-mods' ToneHost add-on plays the guitar through an Audio Unit in place of the game's tones, with a host of its own beside
the game. Its mod is the game's side, with the same `ak_set_rtpc_name` hook point on this game, and played the same as on the
Mac. The input takes any buffer size (`input_cb`), so it needs no change like the Mac's `audio_chan.c`.

## Video (Bink)

The game plays Bink 2 videos (`video.psarc`: first-run tutorials and credits; `etudes.psarc`: 360 lesson
videos; 1024x576 at 24 fps, 48 kHz stereo). `bink2w32.dll` from the game folder is rebased to `0x12000000`
and translated as a fifth module. The game reads the files through its own file system (`BINKIOPROCESSOR`)
and copies each frame into a D3D9 texture it locks. The sound track is decoded by `BinkGetTrackData` into
the player's buffer and fed to Wwise as an audio-input source, so it reaches the speakers through the
game's own output, with nothing Bink-specific in the audio layer.

## Steam

`win-x86/shims/steam` is an offline `steam_api.dll` face: the flat API of the 2019 SDK, interfaces as guest
objects with MSVC `thiscall` vtables (slot and argument sizes generated from the game's DLL,
`steam32_slots.h`), and the behaviour of the Mac stub from the same code (`shims/steam/src/interfaces.cpp`):
the identity from `loginusers.vdf`, the installed DLC and 248750 as owned, Steam Cloud in
`<data_dir>/<account>/221680/remote` seeded on the first start from the real Steam folder for app 221680
(which the Mac and Windows games share) without `crd`, local stats. It never loads Valve's library. The
game does not use Uplay's loader; `steam32_dll_blocked()` names the Ubisoft and Valve libraries a
`LoadLibrary` must refuse. See [win-x86/shims/steam/README.md](../../win-x86/shims/steam/README.md).

The game mounts a `dlc/` archive only when the AppID in its `appid.appid` is a DLC the face lists
(`GetDLCCount` / `BGetDLCDataByIndex`) and answers `BIsDlcInstalled` for (`0x1449a10`, through the platform
service's slot `+0xbc` at `0x1448b10`); an official song re-packed with an AppID the account does not own
disappears from the list. As on the Mac ([DESIGN.md](../mac/DESIGN.md#steam)), the install's
`gamefiles.py` adds the AppID of every archive in the copy's `dlc/` to `RS_STEAM_DLC` in
`game/.rearmed/steam.env` at each copy and refresh, so songs show whatever AppID they carry and official
DLC is reported as before. No game code is changed for it.

The songs of `rs1compatibilitydlc` need the first Rocksmith's DLC as well, the same way as on the Mac
([DESIGN.md](../mac/DESIGN.md#steam)): `dlc_update_tail` (`0x1449660`) asks `BIsSubscribedApp` for the AppIDs in the
game folder's `metadata` file. The same `rs1_owned` asks the Steam client at a copy or refresh when the edition's
account is logged in, and the face answers `RS_STEAM_OWNED` from `steam.env` through `BIsSubscribedApp` only.

## Online services

`win-x86/online` gives the Windows build the Mac port's offline behaviour: no login is started, Enter on
the title screen goes straight to the profile list, and anything that asks the RSConnect extension for a
session gets the answers the game gives when Ubisoft's servers are unreachable. `crd` is never read.

Every function it changes is a virtual method the game only calls through its vtable, so `on32_install`
(called by `lp_main.c` once the images are bound) writes a host thunk cell into each slot it replaces; the
wrapped originals stay callable. Before writing, it checks that every slot still holds the function it
expects, else it leaves that group alone with a message (another build of the game). Replaced:
`RSConnectExtension_RDV` (Init, Login, Tick, the session, leaderboard and Uplay queries),
`RSConnectGameInterface_Impl::GetConfigBool` (`RendezVous.SkipLogin` defaults to true) and
`RSProfileService_Win32::StartSignIn` (skips the "Already have a Uplay account?" dialog). The addresses
and their Mac counterparts are in `win-x86/online/names.tsv`. The DLC preview's download jobs fail at once on the
calling thread, as on the Mac: the manager's queue functions `0x144bc80` and `0x144bdb0` and its flush `0x144bbd0`
are native (`win32_overrides.c`).

The Win32 seam is offline too: ws2_32, iphlpapi and wininet refuse every connection (`win32_net.c`), and
`ShellExecuteA` opens nothing. Without mods the Mac port's network guard is not loaded, and has nothing to guard: no
host socket call is linked into the binary (`nm -u` shows no `socket`, `connect` or `getaddrinfo`).

## Input

The game reads the keyboard and mouse through DirectInput (it quits without a keyboard), and also the
virtual-key state and window messages. `win-x86/shims/win32_input.c` feeds all three from the host window:
each macOS key code maps to a DirectInput scan code and a Windows virtual key, so a Mac key arrives as the
Windows key in the same place (plain Delete is Backspace; fn + Delete is Windows' Delete). Mouse positions
go through the window's aspect fit into the back buffer. XInput reports no controller. `LP_INPUT_SCRIPT`
plays key presses for unattended runs ([TESTING.md](TESTING.md#game-runs)).

Losing the keyboard (`windowDidResignKey:`) lets go of every key and button, since a key's release would go
to the application in front, and posts `WM_ACTIVATEAPP`, as Windows sends it and as the Mac edition calls
`rsp_set_focus`; the game's window procedure (`0x130d790`) then releases Wwise's sink until it comes back.
Modifier keys follow each event's flags, so Command-Tab, whose release goes elsewhere, leaves none inverted.

**Quitting.** Command-Q and the close button post `WM_CLOSE`, as the Mac edition's `rsp_request_quit` asks the
engine: `DefWindowProc` destroys the window, the game's `WM_DESTROY` posts `WM_QUIT`, and its loop (`0x149eda8`)
tells its service to quit. A game that does not answer in 8 s goes anyway. Every end of the process runs
`w32_host_exit` (the shader cache saved, the streams flushed, `_exit`), since the game's threads still run there,
and DLLs get `DLL_PROCESS_DETACH` with `lpReserved` set, the process-exit form, which leaves the CRT's locks and
TLS to those threads.

## Built-in fixes and mods

The Mac port's built-in fixes that change the game are in, as native overrides: functions listed in
`win-x86/build/skip.txt` (image VAs) are left out of translation and `win-x86/shims/win32_overrides.c`
defines their `F_<addr>` against the 32-bit runtime (`rc32_call` for the game functions they call). The
switches are the Mac ones:

- batched DLC mounting: the DLC coordinator's per-frame update `0x1449310` runs its install step `0x1449580`
  in a loop within `RS_MODS_BUDGET_MS` (default 50) instead of once (`RS_MODS_FIXES=0`: once);
- the RSBypass song-file signature skip: the SNG loader's check `0x10d96a0` returns true
  (`RS_MODS_BYPASS=0`: the original check, rebuilt from the game's own calls). As on the Mac only the song
  loader's check is skipped, not the DSA verifier itself;
- the song-key table skip (Windows only): the Learn a Song list builder `0x11da6a0` keeps a song only when
  `0x10f41c0` finds the CRC-32 of its `SongKey` in a table of 1590 known keys in the image (`0x1d1a0d0`), so
  a custom song was left out even with its archive mounted and its manifest read. `F_10f41c0` answers true
  (`skip.txt` `wrap`; `RS_MODS_BYPASS=0`: the table again, through `W_10f41c0`). The Mac image has no such
  table.

The open-file limit needs no game function: `lp_mods_init()` in `win32_overrides.c` raises it at the start with
the Mac's code (`rc_nofile.h`, `RS_MODS_FIXES=0` leaves it). The batched mounting depends on it: 441 archives
peaked at 461 open descriptors, over the 256 an app started from Finder has. Two fixes are not needed here: the keyboard ones
(keys, End) fill the Mac game's table of Cocoa keys, and this game reads DirectInput (`win32_input.c`), and the
thread priority one works around macOS refusing the game's values, which `LP_QOS` covers.

The song path cache and the number-table hash are the rs-mac-fixes of the same names; both defects are in this
build too, and `RS_MODS_FIXES=0` turns both off:

- the path cache: the path intern `0x141c130` (the Mac `path_intern`; the wrapper the Mac hooks is inlined into
  its callers) takes each path's MD5 under the path table's lock on every file check, as on the Mac.
  `F_141c130` keeps the object of 1024 recent paths with a reference of its own and raises its count under the
  same lock (`*0x1ed915c`, a `CRITICAL_SECTION` with the game's owner and depth after it);
- the number-table hash: the game keeps one table of number objects, keyed by value, and hashes a double by its
  integer part, so every value in [0, 1) lands in one bucket. Here the table is a Dinkumware `hash_map` and the
  hash a function of its own, `0x1400650`, so only that function changes (`F_1400650`), where the Mac replaces the
  three functions it is inlined into. As on the Mac the hash is the value's bits with -0 made +0; the table masks
  it rather than taking a prime modulus, so the bits are mixed down first.

`RS_MODS_LOG=1` logs them. The Mac's loading-screen fix isn't needed here ([Shader corpus](#shader-corpus)).

- **Mods** (`tools/ws7/recomp/runtime/rc_mods.c`, shared, called from `win-x86/app/lp_main.c`): every `.dylib` in
  `<home>/mods/` is loaded at startup, before the game runs, with the Mac's switches and log lines. A mod is a
  native arm64 dylib as on the Mac and hooks a game function by name with `rc_hook_add_name("game_frame", pre, post,
  user)`; a mod built with `tools/ws7/recomp/runtime/rc_mod.h` is the same dylib for both editions. The hooks run
  on a view of the call, the Mac's SysV one, made from `tools/targets/win-x86`
  ([recompiler](../recompiler/README.md#overrides-and-hook-points)). A name the game doesn't have gives -1 and a log
  line; `rc_target()` returns `win-x86`. A hook point that is also a built-in fix is `N_<addr>` in
  `win32_overrides.c`, and a mod's hooks run around the fix. A library without `rc_mod_abi` is left out, with a
  log line: built for the Mac alone, it would misread this game's memory.

  A hook point of a name is the Mac's call here too, and `targets.py` compares the prototypes. Thirteen of the 18
  are functions of this game. Six of those are this game's own shape, adapted in `symbols.map`: `game_frame` is the ServiceManager tick,
  its `this` hidden, and answers the Mac's "quit" from the service's state (7 ends the Run loop here, as in the
  Mac function); `ui_event` gets its two folded arguments as their one value; `rr_speed_ratio` shows and takes 100 /
  the game's percentage, which is where the Mac's same curve ends; `too_many_inputs` and `frontend_open_file` read
  their registers; `si_open` is `InputOn`, the slot the first of its four stack arguments and its `PaError` the Mac's
  bool (`bool<zero>`).

  `si_io` is the Mac game's CoreAudio input and has no counterpart ([Audio](#audio)). `input_sink` and `set_app_focus` have
  no such function here either (`unsupported` in the tables): the game has window messages
  and an inlined tick. `win32_input.c` runs them instead (`rc_hook_add_seam`), with the Mac's values: macOS key codes, the
  character, the pointer from the bottom left. While a mod hooks either, events wait for the game thread, as the Mac's
  queue waits for its flush (`w32_input_frame`, at the start of each Present), so they reach the game a frame later; a pre
  hook that answers nonzero keeps the event from the game, and `set_app_focus`'s argument, changed by a pre hook, is what the
  game hears; `input_sink`'s changed arguments are not taken. A mod's own call to `input_sink` (`OV_GUEST`, `rc_call_seam`) goes the
  way of a key from the host. More than 1,023 events
  waiting go past the hooks. With no such mod nothing waits. A modifier key is a down and an up here; the Mac
  sends a down for both.

  The render API (`rs_render.h`, [METAL.md](../mac/METAL.md#render-api-shimsincluders_renderh)) is in libRSOpenGL, under the D3D9 layer: a texture this layer defines empty and fills with one `glTexSubImage2D` counts as uploaded then. The overlay API is the Mac's `rsp_overlay.c`, run in `d3d9_dev_present` on the back buffer before it is shown, and
  `OV_VCALL` is a thiscall through the guest's vtable (`rc32_hook.c`).

  Two things differ with the call the same: a mod's hooks run around a built-in fix here, where on the Mac the fix is a pre hook that can answer first, and `game_frame` is a tick
  of the Run loop, so it runs more often than frames are presented.

  The rearmed-mods library (`rearmed-mods.dylib`, from 0.3.0) runs here as the same file as on the Mac. What it needs of the game
  is in the tables ("the mods library" in `tools/targets/names`): Wwise by name, the UI manager, the DLC service, the Gamebryo
  fields and the search key. A mod finds its settings in the game folder: with mods the installed app goes to `LP_GAME_DIR`,
  the Mac's working directory, before it loads them. A mod's strings and results are memory from `rc_guest_alloc`.

  The network rule holds with mods in the folder. `win32_net.c` answers the game's own calls, but a mod is
  native code, so before anything else the app looks whether it has mods to load and, if so, starts itself again
  with `libnetguard.dylib` inserted (from its own `Contents/MacOS`; dyld inserts only at a start), which is
  how the Mac edition always runs. Without the guard no mods load. Without mods there is no guard, and nothing
  to guard.

More native replacements of single game functions by address (`win-x86/build/skip.txt`,
`win-x86/shims/win32_overrides.c`):

- **Direct Connect choice.** The same overlay as the Mac's ([Mac design](../mac/DESIGN.md#built-in-fixes-and-mods)):
  `tools/play-native/cache_overlay.py` makes `game/Update/patcharchives.psarc` from the copy's own
  `cache.psarc` at each copy or refresh, and the engine mounts it at `PatchUpdate/`. `FrontEnd_OpenFile`
  (0x101ba80; path in `ecx`, the result pointer on the stack, removed by the caller) is a native function
  here: for a `cacheN.7z` it opens `PatchUpdate/<name>` first,
  then does what the original does (the lower-case path to the front-end file system's `Open`, slot +0x10
  of the singleton at 0x1ee67c4, and an `AddRef` for the caller). `RS_MODS_OVERLAY=0` as on the Mac.
- **Exit Game.** The same entry as the Mac's ([Mac design](../mac/DESIGN.md#built-in-fixes-and-mods)), and the same edit:
  the game has the action.
- **Enumerate.** The same entry and the same code as the Mac's ([Mac design](../mac/DESIGN.md#built-in-fixes-and-mods)):
  the overlay's two edits, and `shims/steam/src/enumerate.h`, compiled in through `interfaces.cpp`. The overlay
  check is RSConnectService slot 125 here (four callers), `steam32.c` answers `IsOverlayEnabled` and posts the
  `DlcInstalled_t` (the listener is registered at 0x145ac7c), and the game folder is `LP_GAME_DIR`.
  `RS_MODS_OVERLAY=0` as on the Mac.
- **Song ownership during a mount**, as on the Mac: `0x1448b10` (slot 47 of the Steam wrapper, the Mac
  `steam_dlc_owned`) stays translated as `W_1448b10` (`skip.txt`, `1448b10 wrap`), and `F_1448b10` answers yes while
  `F_1449310` runs the install step. `RS_MODS_BYPASS=0` asks the list again (and brings back the song-key table,
  which hides custom songs here whatever their AppID).
- **No intro screens**, as on the Mac (`RS_MODS_INTRO=0` plays the intro): the Mac `boot_intro_update` is
  `0x1126950` here, found by its `mPlaybackState` / `mScreenState` reads and the same five-state switch. It is
  translated as `W_1126950` (`win-x86/build/skip.txt`, `1126950 wrap`) and `F_1126950`
  (`win-x86/shims/win32_overrides.c`) runs first, with the Windows build's field offsets (boot phase `+0x48`,
  phase 3 step counter `+0x28`, intro state `+0x60`, playing flag `+0x2d`). The behaviour is the Mac one
  ([Mac DESIGN](../mac/DESIGN.md#built-in-fixes-and-mods)).
- **Auto Save warning skipped**, as on the Mac (`RS_MODS_AUTOSAVE=0` shows it): the Mac `autosave_warning` is `0x120adf0` here, a
  virtual with `this` in `ecx`, found by the same strings and branches. On `OnSignInFlowComplete` it runs the input mode step
  (inlined here, `0x120ae72`-`0x120ae8d`), makes `RS2AutosaveWarning` with the navigate call `0x115db90`, stores it in the dialog
  slot `+0x14` and writes 4.0 to the timer `+0x20`. The Mac copies that branch into a pre hook; here the handler runs as it is and
  only the call is left out: `F_115db90` (`win-x86/shims/win32_overrides.c`; `win-x86/build/skip.txt`, `115db90 wrap` and `120adf0 wrap`) answers
  for that one return address with a token, and every other caller gets `W_115db90`. So the input mode step, the slot, the string
  and the release of the handler's third argument, which the Mac's call doesn't have, all stay the game's own. `N_120adf0`, around the
  handler, clears the timer once it has returned. The menu's tick `0x1208fe0` (the same fields as `+0x2d4` and `+0x2e0`: the handler's
  `this` is the menu's listener at `+0x2c0`) then goes on as when the dialog's time is up: like the Mac's, it needs a slot that isn't
  NULL and a timer at most zero, and its dismiss by name does nothing for a dialog that isn't there.
- **Title logo.** The same `tools/loadscreen` and layout as the Mac's. `lp_install.py build` makes `static.psarc.rearmed` in the copy, and `title_logo()` in `lp_main.c` links
  `static.psarc` at each start as `launch.sh` does (`RS_REARMED=0` for the original). It isn't in the manifest, so a refresh leaves it, except that a new `static.psarc` removes it.

## Installer and build

```
choose the game ─┬─ Rocksmith 2014 for Mac → static decrypt → shims, app → install → warm shaders → launcher
                 └─ Learn & Play → free space → Steam downloads the depot and the DLC → game-copy → Microsoft DLLs
                                    → code image → lp_install.py build → app-install → build-clean → warm shaders
```

`win-x86/install/lp_install.py` does the steps (standard-library Python, only tools macOS ships):

1. **The game** (`game-locate`, `game-space`, `game-redirect`, `game-download`). `steam_osx +download_depot 221680 221683 8918188356915973501` hands the running Steam the
   download with nothing to paste; only if it ignores that for 30 seconds does `steam://open/console` open with the command on the clipboard. A second command during a download makes
   Steam fetch every chunk twice, so one goes out only when no download has started since Steam did, or nothing was written for ten minutes. `gamefiles.SteamGate` reads Steam's state from
   disk, with no macOS permission, and what's left for the user comes out as `guide` lines, the same words as the Mac edition's. The installer never handles credentials.
   Steam makes every file at full size first, so the whole depot (5.9 GB) must be free up front; when it isn't, `game-redirect` links Steam's `steamapps/content` to another disk. The
   download counts as finished on Steam's word or at the depot's full size, and the exe's SHA-256 is checked against `target.toml`. steamcmd is not used: it is an Intel binary.
1a. **The DLC** (`gamefiles.py dlc-fetch`): each DLC is a depot of its own, with the AppID as its id, and the game's
   depot has none. The DLC the account owns are read from the app ownership ticket in Steam's `localconfig.vdf`, and
   Steam fetches them one after another with the same `+download_depot`. A state file remembers what is done, so a
   stopped run and a later refresh fetch only the rest, and one that fails is named and doesn't stop the install.
1b. **The game files** (`game-copy`, `game-refresh`; `tools/play-native/gamefiles.py`, shared with the Mac install): the app's own copy in `LP_HOME/game`, as Steam may clear its depot
   folder. It is the depot folder plus the PC archives `*_p.psarc` of its `dlc/` and the Mac game's `dlc/`, as APFS clones where it can. `.rearmed/manifest.json`, written last, marks it complete,
   and `lp_main.c` refuses to start without it. `.rearmed/steam.env` carries the DLC, language, AppIDs and account, which `lp_main.c` sets as `RS_STEAM_*` defaults.
2. **The Microsoft DLLs** (`redist`): from Steam's Common Redistributables on the Mac
   (`Steamworks Shared/_CommonRedist`: `vcredist/2010/vcredist_x86.exe`,
   `DirectX/Jun2010/Aug2009_d3dx9_42_x86.cab`, `Aug2009_D3DCompiler_42_x86.cab`), else Microsoft's two
   redistributables downloaded with `/usr/bin/curl` (SHA-256 pinned; the only network access besides Steam),
   or a folder given with `--from`. `/usr/bin/tar` unpacks the cabinets; every DLL's SHA-256 is checked.
   D3DCompiler_42 is written rebased.
3. **The code image** (`game-image`): the image an earlier run made
   (`$RS_BUILD_DIR/win-x86/image/Rocksmith2014.exe`); else the copy's exe unpacked with the seed (`LP_SEED`,
   `win-x86/unpack/seed.bin`, or the installer app's Resources) and built with `lp_image.py`, with nothing from
   the user; else `lp_image.py` from a capture folder. Without any, `harvest-export` writes the harvester folder
   to take to the PC and the run stops.
4. **The build** (`build`, `lp_build.py`): each translation is keyed by the SHA-256 of its input, its options,
   the translator's sources, the Python and Capstone versions, and the compiler and runtime headers. A step
   whose key matches is skipped, a stopped run continues (finished objects are kept), and after an update
   only what changed is redone. A key is deleted before the step it vouches for is redone, so a stopped run
   never leaves stale output. Paths with spaces are refused up front. About 26 minutes the first time on an
   M2, with a peak of about 2 GB in the build folder.
5. **The install** (`app-install`): one ad-hoc signed bundle, `~/Applications/RS Re-ARMed Learn & Play.app`:
   `Contents/MacOS` has `rs2014_lp` and `libRSOpenGL.dylib`, `Contents/Resources` the game image
   (`lp_game.exe`), the DLL images and the game's icon. It sets no environment; `lp_main.c` takes its
   folders from `~/Library/Application Support/RS Re-ARMed/learn-play/` (`LP_HOME`).
6. **Clean** (`build-clean`): the intermediates go; the translated libraries (about 140 MB) stay as the
   cache that makes the next run skip to the link. It refuses anything that does not look like a build
   folder.
7. **The shaders** (`tools/play-native/warm-shaders.sh`, the Mac's): the installed app's `--warm-shaders`
   ([Shader corpus](#shader-corpus)).

`tools/play-native/learn-play.sh` runs all of it in a terminal; `RS Re-ARMed Installer.app` runs the same
commands with its own steps ("Downloading the Windows game", "Downloading your DLC", "Copying the game files", "Getting Microsoft
libraries", "Code image", "Building the game", "Installing", "Preparing shaders") and its Refresh Game Files button runs the
installed `gamefiles.py refresh`. Neither touches the Mac game install or its saves.
