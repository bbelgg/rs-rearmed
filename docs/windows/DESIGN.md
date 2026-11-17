# How the Windows port works

The Windows port takes the 32-bit Windows build of Rocksmith 2014 that the Learn & Play edition grants (Steam depot
221683) and makes a native arm64 Mac app of it, RS Re-ARMed Learn & Play. The game's code and the Microsoft DLLs it
loads are translated to C at build time by the shared recompiler's 32-bit front end
([recompiler](../recompiler/README.md#the-32-bit-front-end-windows)); a Win32 seam on macOS stands in for Windows
underneath. It reuses the Mac edition's renderer, Steam layer and audio policy. User side: [INSTALL.md](INSTALL.md).
Status: [PLAN.md](PLAN.md).

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

| | Mac edition | Learn & Play |
|---|---|---|
| Game binary | x86-64 Mach-O, SysV | 32-bit x86 PE, MSVC conventions |
| Protection | Steam DRM, a repeating XOR | VMProtect, decrypts in memory only |
| Game image | `tools/steamless`, on the Mac | `win-x86/unpack` with the release's seed, on the Mac; else a one-time capture on a Windows PC (`win-x86/harvest`) |
| Renderer | OpenGL 2.1 + Cg on `libRSOpenGL` | Direct3D 9 on `libRSOpenGL` |
| Audio | Wwise CoreAudio sink | Wwise on PortAudio's WASAPI, and winmm, on CoreAudio |
| Shell | `shims/platform` (Cocoa) | `win-x86/app/lp_window.m` under a Win32 seam |
| Libraries | native replacements (overrides) | Microsoft's DLLs, translated |

Gameplay is the same C++ source compiled twice, so once translated it behaves the same. Learn & Play looks like the PC
game, drawn through the same Metal renderer.

## Getting the game image

The exe on disk (pinned by size and SHA-256 in `tools/targets/win-x86/target.toml`) is VMProtect-packed: the
real `.text`, `.rdata` and `.data` exist only in memory while the game runs.

The released installer carries a seed, a small piece of VMProtect's state with no game code in it. With it
`win-x86/unpack/lp_unpack.py` runs the exe's own decompression in Unicorn on the Mac, and `lp_image.py` makes the
game image from the result, with nothing from the user ([win-x86/unpack](../../win-x86/unpack/README.md)).
`lp_install.py build` stops on an image other than the pinned one, as an image with pages missing has the right
`.text`, builds and crashes at the start; `learn-play.sh` and the installer make such an image again from the
seed.

Without a seed a build starts from a capture (`--unpinned` lets its image, which can differ, through). The user
runs the game once on a Windows PC where they own it, with the harvester
([win-x86/harvest](../../win-x86/harvest/README.md)): it reads the game's memory with `ReadProcessMemory` (no
debugger, no injection, so the anti-debugging does not trigger) and records the game's pages and each loaded
module's exports.

`win-x86/image/lp_image.py` builds a PE of the game alone, at the base it ran at (0xf80000): the true section
table, `.data` as at load, an import directory over the real IAT, a rebuilt relocation table (the game's own is
inside VMProtect's packed data; the recompiler needs it to find functions reached only through pointers), the
entry point `WinMainCRTStartup`, and the VMProtect sections without the execute flag. Words that only look like
pointers are kept as they were: moving them corrupts the SNG signature key, and no song loads.

**Imports.** VMProtect encrypted 506 of the 585 import slots and replaced the call sites with single-use stubs.
`win-x86/imports` names the plain slots from a capture's exports (`lp_slots.py`) and runs each stub site as a
black box until it reaches an export (`lp_wrappers_run.py`, `lp_wrappers.py`). The result, `wrappers.tsv` (3,841
rows) and `slots.tsv`, is committed (addresses and names only) and passed to the translator with `--wrappers`.
No game function is virtualised: a sweep of `.text` finds no transfer into VMProtect code other than the import
stubs.

RS Re-ARMed ships no game code: the seed holds none, and a capture is the user's own game's data and stays on their
machines.

## Translation

`win-x86/install/lp_build.py` (run by `lp_install.py build`) translates six images with
`gen_image_pe.py`: the game (about 62,000 functions) and five DLLs as modules, msvcr100, msvcp100 and d3dx9_42
at their preferred bases, D3DCompiler_42 rebased to `0x11000000` and bink2w32 to `0x12000000`. It compiles each
tree with a small ninja-subset runner of its own and links with `win-x86/build/build.sh`, so CMake and Ninja
are not needed. `win-x86/build/` also has the scripts to do each step by hand
([win-x86/README.md](../../win-x86/README.md)). The front end, its memory-order fences, relaxed functions and
the stack check: [recompiler](../recompiler/README.md#the-32-bit-front-end-windows).

## Per-game tables

`tools/targets/win-x86` has the Learn & Play image's names (`symbols.map`: 1,063 of the 1,263 symbols read, an
`unsupported` line with the reason for the rest: inlined, folded or specialised by LTCG, or no counterpart),
`offsets.map`, `edits.map`, `mids.map` and `relaxed.map`, and in its `target.toml` the pins (the depot, manifest, exe
and DLL hashes, build figures). The build uses them for the hook points, what a mod asks for by name, and the built-in
fixes, which name every function, global and field of the game they touch, as the Mac edition's overrides do
([tools/targets/README.md](../../tools/targets/README.md)). The other addresses the build needs are in
`win-x86/imports/*.tsv` (import sites) and `win-x86/online/names.tsv` (the vtables the online layer patches, with their
Mac counterparts). The Mac edition's overrides are not built here.

## Runtime

The 32-bit runtime (`tools/ws7/recomp/runtime/rc32_*`,
[recompiler](../recompiler/README.md#pe-images-several-per-process)):

- **The 4 GB guest window.** arm64 macOS keeps the low 4 GB unmapped, so the runtime reserves one 4 GB
  region and every guest access is `rc_mem + (uint32_t)addr`. Images, guest heaps, stacks, TEBs and every
  object the guest holds a pointer to (Steam interfaces, D3D9 objects, COM vtables) live inside it. A thread
  that ends gives its stack, TEB and TLS blocks to the next (`rc32_thread_end`), so short threads don't use the
  window up.
- **The image registry** maps each image at its base, binds imports between images (data exports
  included; `msvcrt.dll` is an alias of msvcr100), runs `DllMain` in dependency order and answers
  `GetModuleHandle`, `GetProcAddress` and `LoadLibrary` for translated modules (D3DCompiler_42 is
  deferred: d3dx9 loads it with `LoadLibraryA`).
- **SEH** for the game's `__try` blocks, C++ exceptions, `setjmp`/`longjmp` and the anti-debugging checks
  ([recompiler](../recompiler/README.md#seh-and-c-exceptions)). A fault in guest code is not an exception
  here: it ends the process, with the fault logged.
- **Memory order.** Translated code is compiled for x86's order, as on the Mac: the game relies on it
  between its threads. The exceptions are the relaxed functions of `tools/targets/win-x86/relaxed.map`
  ([recompiler](../recompiler/README.md#memory-ordering)).
- `win-x86/app/lp_main.c` registers the native overrides, binds the images, installs the online layer and
  calls `WinMainCRTStartup` on a thread with a large host stack. An import no image exports goes to the
  Win32 seam by name (`win-x86/shims/win32_dispatch.c`).

## Platform

`win-x86/shims` is Windows, as much as the game and the DLLs use, on POSIX and Cocoa:

- **kernel32** (`win32_kernel32.c`, `win32_kfile.c`, `win32_ksync.c`): files (the game folder is
  `C:\Rocksmith2014`, read-only, its writes redirected to the user folder; `C:x` paths relative to the
  drive's current directory; a path that starts with a slash is a host path only inside the game folder, the
  user folder or a folder the seam handed out), threads, events, mutexes, semaphores, waits (each wait sleeps on
  its own condition and is woken only by its objects; `LP_WAKE_ALL=1` broadcasts instead), TLS and FLS,
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
display mode): in points, or in backing pixels with `RS_METAL_RETINA=1` (`RS_D3D9_DISPLAY=WxH` overrides). In points on a
Retina screen, `lp_window.m` asks libRSOpenGL for an output of the back buffer times the backing scale
(`rs_gfx_set_output_size`), so each frame is scaled up with MetalFX before the layer fits it to the window, as on the Mac
([Mac METAL.md](../mac/METAL.md#metal-core-shimsopenglsrcmetal)).
`[Renderer.Win32] Fullscreen` in `Rocksmith.ini` (in the game: Options > Visual Settings > Windowed Mode):

| `Fullscreen` | Device | Window |
|---|---|---|
| `1` Non-Exclusive Fullscreen (the default the game writes) | windowed, the desktop's size | borderless over the screen |
| `2` Exclusive Fullscreen | full screen, `ScreenWidth` x `ScreenHeight` (0: 1280x720) | borderless over the screen, the back buffer letterboxed; the display mode never changes |
| `0` Windowed | windowed, `ScreenWidth` x `ScreenHeight` | a titled, resizable window of that size in points, shrunk to fit the screen |

Changing it in the game resets the device and restyles the window at once (`lp_window_style` from
`SetWindowLong`, `d3d9_platform_resize` from `IDirect3DDevice9::Reset`). `RS_FULLSCREEN=1|0` pins the mode.
The Metal layer's drawable is always the back buffer, shown aspect-fit in the window; the pointer goes back
through the same fit. The rules are plain C in `win-x86/app/lp_screen.c`.

**Any shape.** The game draws at its back buffer's shape, as the Mac game does (`RS_WINDOW_ANY_SHAPE`, on unless `0`,
the Mac edition's switch). The original draws 16:9 whatever its resolution: its camera setup and its
`GRDisplayRatioAdjuster`, which squeezes the picture into a 16:9 band with bars, both go by one 16/9 constant. Nothing
else reads it, so `lp_game_shape` (`win-x86/shims/win32_overrides.c`) sets it to the back buffer's width / height at
each `CreateDevice` and `Reset`, on the game's thread before its next frame.

**Resizing.** A window the user resizes, or takes in or out of macOS full screen, becomes the game's resolution,
as on the Mac. The game has no path for that (its window procedure takes no `WM_SIZE`), so `lp_window.m` asks
(`lp_request_resolution`) and the game's thread applies the size at its next `PeekMessage` (`w32_pump`) with the
game's own settings calls, as its F11 does. Only in the game's Windowed mode: in its full-screen modes the
device keeps its size, and a window showing them (`RS_FULLSCREEN=0`, or Control-Command-F from the borderless
full screen) has the layer fit that back buffer.

The screen saver and display sleep stay off while the game is open: `lp_window.m` holds a display-sleep
assertion from the window's making to the process' end, as the Mac game's view does (not in an offscreen run,
`LP_OFFSCREEN=1`). A hidden window (covered, minimised) has no layer pacing its presents, so `lp_window.m`
gives the D3D9 layer the display's period, and Present keeps to it (`d3d9_present_wait`).

The frames follow the display's refresh rate: the layer holds each present for a refresh. `RS_FPS=N` fixes a
rate, as in the Mac edition, and with the Mac's rule and clock (`shims/platform/include/rsp_fps.h`):
`lp_window.m` takes the layer's vsync off and gives the D3D9 layer a period of 1/N s, which Present keeps to
whether the window shows or not. The Mac's render thread waits before a frame; here the game's own thread makes
the frames, so the wait is at the end of Present.

## Renderer

### Direct3D 9

The game renders with Gamebryo's `NiDX9Renderer` and Scaleform's D3D9 HAL. `win-x86/shims/d3d9` implements the
`IDirect3DDevice9` subset they use on the same `gl*` entry points the Mac game calls, so the Metal core and its caches
are shared with the Mac edition ([Mac renderer](../mac/METAL.md)). Only Metal presents to the window:
`rs_gfx_create_layer` has no OpenGL path, so `RS_GL_BACKEND=gl` runs offscreen (the D3D9 tests use it on both backends).

- **States and draws**: vertex declarations and FVFs, render and sampler states, state blocks,
  `DrawPrimitive`/`DrawIndexedPrimitive` (and the `UP` forms), `Lock`/`Unlock`, render targets, `Clear`,
  `StretchRect`, `ColorFill`, `Reset`; with the D3D9-to-GL conventions (half-pixel offset, depth range,
  row-major constants, BGRA vertex colours). X8R8G8B8 textures sample with alpha 1 (Bink writes 0 there).
- **Dynamic buffers**: an `Unlock` of a dynamic buffer only marks the range; a draw uploads the part it
  reads (`LP_D3D9_EAGER=1` uploads at `Unlock`). The game locks its 1 MB dynamic vertex buffer whole about
  2,900 times a second.
- **Shaders**: SM2/SM3 bytecode is translated to GLSL 1.20 (`d3d9_shader.c`), the dialect the GLSL to MSL
  path takes; all 1,226 shaders in the game's files translate. The corpus and the warm-up compile the programs
  ahead ([Shader corpus](#shader-corpus)).
- **Fixed function** (`d3d9_ffp.c`): Gamebryo sets fixed-function state every frame and keeps a
  fixed-function shader for meshes without one. A draw with no vertex shader, no pixel shader or neither
  gets a generated program, cached by the state it depends on (positions, transforms, per-vertex lighting,
  texture coordinates, fog, up to 8 texture stages). Logged once and not implemented: vertex blending, sphere
  maps, bump mapping, `PREMODULATE`, user clip planes, point sprites.

### Shader corpus

As on the Mac ([METAL.md](../mac/METAL.md#shader-translation)): MSL made at build time from the user's game files, in the
app (`Contents/Resources/rsmsl-corpus.bin`), found by `rsmsl_translate` and compiled by the install's warm-up. What differs is
where the shaders come from and how they pair.

**Where a program comes from**: every shader the game creates is bytecode in its files: in the image (Scaleform's), in the
Gamebryo caches `shaders/generated/*.cache` (the standard material's), in `shaders/dx9/*.vso`, `*.pso` and in the effects
`*.fxo` of `static.psarc`. The game can use `D3DXCompileShader`, but none has come from it in the modes we could reach.

**Pairs**: a vertex shader's GLSL depends on the pixel shader it is linked with, so a program is a pair. By their semantics
103,012 pairs could happen, far too many to make; the game has used 119. Every effect program seen is the vertex and pixel
shader of one `.fxo`, or a `.vso` and `.pso` of one name, so those pairs are all made (208). For Scaleform's and the standard
material's shaders nothing in the files says which, so those pairs come from runs: `win-x86/shaders/programs.tsv` lists each
program seen as `pair VS PS`, hashes of the bytecode, or `ffp VS PS KEY` with the fixed-function state key (no shader text or
bytecode, so it can be in the repository), and `record_programs.py` adds a run's `LP_SHADER_LOG` to it. The Guitarcade's
games need a guitar to get past calibration, so only their effects are in.

**The build**: `tools/metal/make-corpus.sh --learn-play` takes the shaders of `static.psarc`, and `win-x86/shaders/lp_shaders.c` looks
through them and the image for bytecode and writes the GLSL of each listed program, by the layer's own
`d3d9_sm_glsl` and `d3d9_ffp_sources` (`d3d9_ffp_gen.c`, which depends on the state key alone), plus the layer's blit. Then the Mac's
steps: `build-msl-corpus.sh --pairs`, `msl_verify --translate-only`, `merge_corpus.py`. A failed corpus doesn't fail the build.

**The warm-up**: `--warm-shaders` in `lp_main.c`, before anything of the game is loaded, calls `rs_gfx_warm_only`, the loop the Mac
app runs ([METAL.md](../mac/METAL.md#safe-load)). There is no safe load: its reason on the Mac is the loading screen's race with a
fast-load `cache.psarc` and a busy CPU, and after the warm-up nothing here keeps the CPU busy while the game loads
([PERFORMANCE.md](PERFORMANCE.md#shader-preparation)). A program the list doesn't have is compiled when the game first draws with
it, kept in the disk cache and warmed from the manifest at the next start.

### D3DX

The game imports 22 functions of `d3dx9_42.dll`: matrices, the HLSL compiler and assembler, constant
tables, the effect framework, texture and image loading. They run as Microsoft wrote them: d3dx9_42 and
D3DCompiler_42 (which d3dx9 loads at run time for compiling) are translated, and their `msvcrt.dll`
imports are served by the translated msvcr100. Device calls go through the vtable to the D3D9 layer. 3DNow!
paths in d3dx9's math stay unused (the reported CPU is GenuineIntel without 3DNow!, and
`IsProcessorFeaturePresent` says no to it); its SSE2 table is what runs.

## Libraries

The game and D3DX need msvcr100 and msvcp100 (VC++ 2010 SP1 x86) and d3dx9_42 and D3DCompiler_42 (DirectX
June 2010). They are translated, not rewritten: the guest runs Microsoft's own heap, stdio, locale, C++
exceptions, RTTI, iostreams and `std::string`, with exactly the layouts the game's code expects, and the
seam is the kernel32 the game needs anyway. None of these DLLs, nor anything generated from them, is
shipped: the installer takes the user's copies ([INSTALL.md](INSTALL.md#install-from-a-clone)).

**Native math overrides** (`win-x86/shims/win32_crtmath.c`, `rc32_override_add`): the note detection calls `_CIpow` four times
per spectrum element, so every math export the five images import runs on the host's libm, with the CRT's error rules and
calling convention (within 4 ulp of the translated code, 2-6 times faster). `LP_NATIVE_MATH=0` turns them off. `memcpy`,
`memmove`, `memset` and `memcpy_s` are native the same way (`LP_NATIVE_MEM=0`), fenced once around the call.

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
thread signals the guest's event; it takes any format the HAL unit can convert.

The policy is the Mac edition's, from the same code (`shims/coreaudio/rs_audio_host.c`, `rs_audio_input.c`; the
[Mac design](../mac/DESIGN.md#audio) says why):

- **Output.** The device's I/O buffer is `RS_AUDIO_FRAMES` (default 256), at most 128 on a device with inputs;
  `GetDevicePeriod` reports 256 frames as the default and 128 as the minimum; the feeding guest thread joins the
  device's audio workgroup as a real-time thread (`RS_AUDIO_RT`); devices are set to 48 kHz when they can be
  (`RS_AUDIO_SET_RATE`). A change of the system's default output reaches the game's `IMMNotificationClient`s
  (`wa_com.c`), and Wwise makes a new sink on the new default, which the Mac edition's override does by hand.
- **Input buffers.** Stale input is trimmed (`RS_AUDIO_INPUT_TRIM`). The capture stream takes any buffer size
  (`input_cb`), so it needs no collection like the Mac edition's `audio_chan.c`.
- **The guitar input's channel and gain**: the capture stream reads the device's channels and gives the game the
  one `guitar-input.ini` sets, with the gain and a clip at full scale ([INSTALL.md](INSTALL.md#guitar-input)).
  The stream is ours, so it needs no hook point (`si_io` is `unsupported`).
- **The list of inputs.** `wa_engine.c` lists an interface's channels as mono endpoints, in the priority order
  the game's limit of 10 needs.
- **An input's level.** The game sets the level through `IAudioEndpointVolume` and the winmm mixer, which share
  one value per endpoint (`wa_com.c`). For any input but the cable that value is the shared level, applied in
  the capture callback.
- **Device identifiers and cable detection.** On the PortAudio path the game accepts an input whose USB IDs are
  in its table (12BA/00FF is the Real Tone cable), read from the endpoint's device instance id; on the waveIn
  path it takes the first device named "Rocksmith". The layer gives each USB device an instance id from its
  CoreAudio model UID, and a device named "Rocksmith USB Guitar Adapter" (the cable's name on both systems, also
  a virtual device of that name) is 12BA/00FF. Any other device's "Rocksmith" is renamed "RS", so a virtual
  device does not raise the game's cable warning or win the waveIn scan (`RS_AUDIO_RS_NAMES=0` keeps the names).
  A player's input mode picks the slot: a Real Tone slot only in Real Tone mode, a microphone slot only in
  Microphone mode, any other input in Microphone or Other Cable mode.

### Your own plug-in

rearmed-mods' ToneHost add-on plays the guitar through an Audio Unit in place of the game's tones, with a host of its own beside
the game ([Mac design](../mac/DESIGN.md#your-own-plug-in)). Its mod is the game's side, with the same `ak_set_rtpc_name` hook
point on this game. Nothing here changes for it ([Audio](#audio)).

## Video (Bink)

The game plays Bink 2 videos (`video.psarc`: first-run tutorials and credits; `etudes.psarc`: the lesson videos).
`bink2w32.dll` from the game folder is rebased to `0x12000000` and translated as a fifth module, where the Mac edition
replaces Bink with FFmpeg. The game reads the files through its own file system (`BINKIOPROCESSOR`) and copies each
frame into a D3D9 texture it locks. The sound track is decoded by `BinkGetTrackData` into the player's buffer and fed to
Wwise as an audio-input source, so it reaches the speakers through the game's own output, with nothing Bink-specific in
the audio layer.

## Steam

`win-x86/shims/steam` is an offline `steam_api.dll` face: the flat API of the 2019 SDK, interfaces as guest objects with
MSVC `thiscall` vtables (slot and argument sizes generated from the game's DLL, `steam32_slots.h`), and the behaviour of
the Mac edition's stub from the same code (`shims/steam/src/interfaces.cpp`; [Mac design](../mac/DESIGN.md#steam)). Its
Steam Cloud folder is seeded on the first start from the real Steam folder for app 221680, which the Mac and Windows
games share, without `crd`. It never loads Valve's library. The game does not use Uplay's loader;
`steam32_dll_blocked()` names the Ubisoft and Valve libraries a `LoadLibrary` must refuse. See
[win-x86/shims/steam/README.md](../../win-x86/shims/steam/README.md).

Custom songs show whatever AppID they carry, and the songs of `rs1compatibilitydlc` show for the first Rocksmith's DLC
the account owns, by the Mac edition's means: the install's `gamefiles.py` writes `RS_STEAM_DLC` and `RS_STEAM_OWNED` to
`game/.rearmed/steam.env` at each copy and refresh, and the face answers from them (`GetDLCCount`, `BGetDLCDataByIndex`
and `BIsDlcInstalled` for the first, `BIsSubscribedApp` alone for the second). This game also keeps a song out of its
list when it doesn't know the song's key, which a built-in fix answers
([Built-in fixes and mods](#built-in-fixes-and-mods)).

## Online services

The game never reaches the network, by the Mac edition's three layers in the form this game needs
([Mac design](../mac/DESIGN.md#online-services)): the offline Steam layer ([Steam](#steam)), the online stub, and a
Win32 seam that refuses the game's network calls where the Mac edition has its network guard.

**The online stub.** `win-x86/online` gives this game the Mac edition's offline behaviour: no login, Enter on the title
screen straight to the profile list, the answers of unreachable servers for anything that asks the RSConnect extension
for a session. `crd` is never read. Every function it changes is a virtual method the game only calls through its
vtable, so `on32_install` (called by `lp_main.c` once the images are bound) writes a host thunk cell into each slot it
replaces; the wrapped originals stay callable. Before writing, it checks that every slot still holds the function it
expects, else it leaves that group alone with a message (another build of the game). Replaced: `RSConnectExtension_RDV`
(Init, Login, Tick, the session, leaderboard and Uplay queries), `RSConnectGameInterface_Impl::GetConfigBool`
(`RendezVous.SkipLogin` defaults to true) and `RSProfileService_Win32::StartSignIn` (skips the "Already have a Uplay
account?" dialog). The addresses and their Mac counterparts are in `win-x86/online/names.tsv`. The DLC preview's
download jobs fail at once on the calling thread, as on the Mac: the manager's queue functions and its flush are native
(`win32_overrides.c`).

**The Win32 seam and the network guard.** ws2_32, iphlpapi and wininet refuse every connection and datagram, and every
name lookup but a literal loopback address (`win-x86/shims/win32_net.c`; `select` with no timeout waits 100 ms and finds
nothing ready, as with one), and `ShellExecuteA` opens nothing. No host socket call is linked into the binary (`nm -u`
shows no `socket`, `connect` or `getaddrinfo`), so without mods the Mac edition's network guard is not loaded, and has
nothing to guard. A mod is native code and could make a socket, so at its start, once `settings.env` is read, the app
looks whether it has mods to load and, if so, starts itself again with `libnetguard.dylib` inserted (`mods_guard` in
`lp_main.c`; from its own `Contents/MacOS`, as dyld inserts only at a start): the same library with the same rules as
the Mac edition always runs with. Without the guard no mods load.

**What the app passes.** `lp_main.c` takes four keys of `game/.rearmed/steam.env` as `RS_STEAM_*` defaults and no others
(`RS_STEAM_DLC`, `RS_STEAM_OWNED`, `RS_STEAM_LANGUAGE`, `RS_STEAM_ID`), as the Mac launcher does.

## Input

The game reads the keyboard and mouse through DirectInput (it quits without a keyboard), and also the
virtual-key state and window messages. `win-x86/shims/win32_input.c` feeds all three from the host window:
each macOS key code maps to a DirectInput scan code and a Windows virtual key, so a Mac key arrives as the
Windows key in the same place (plain Delete is Backspace; fn + Delete is Windows' Delete). Mouse positions
go through the window's aspect fit into the back buffer. XInput reports no controller. `LP_INPUT_SCRIPT`
plays key presses for unattended runs ([TESTING.md](TESTING.md#game-runs)).

Losing the keyboard (`windowDidResignKey:`) lets go of every key and button, since a key's release would go
to the application in front, and posts `WM_ACTIVATEAPP`, as Windows sends it and as the Mac edition calls
`rsp_set_focus`; the game's window procedure then releases Wwise's sink until it comes back.
Modifier keys follow each event's flags, so Command-Tab, whose release goes elsewhere, leaves none inverted.

**Quitting.** Command-Q and the close button post `WM_CLOSE`, as the Mac edition's `rsp_request_quit` asks the
engine: `DefWindowProc` destroys the window, the game's `WM_DESTROY` posts `WM_QUIT`, and its loop
tells its service to quit. A game that does not answer in 8 s goes anyway. Every end of the process runs
`w32_host_exit` (the shader cache saved, the streams flushed, `_exit`), since the game's threads still run there,
and DLLs get `DLL_PROCESS_DETACH` with `lpReserved` set, the process-exit form, which leaves the CRT's locks and
TLS to those threads.

## Built-in fixes and mods

Learn & Play has the Mac edition's built-in fixes and mods, with the same switches and the same `[rs-mods] ` log lines.
The [Mac design](../mac/DESIGN.md#built-in-fixes-and-mods) says what each is for and which are one source for both
editions; this section says what differs here, and why.

How a fix gets into this app:

- **The shared ones** (`tools/ws7/recomp/overrides/mods`: `mods_shared.c`, `framerate.c`) are built into it as a mod is
  built. `lp_mods_init()` starts them from `lp_main.c`, once `settings.env` is in the environment, where the Mac edition
  starts them from a constructor.
- **This edition's own** are in `win-x86/shims/win32_overrides.c`, written against the 32-bit runtime. A function named
  in `tools/targets/names/overrides/win32.list` is left out of the translation and defined there; one marked `wrap`
  stays translated beside the native one, which calls it for the original. The build makes the translator's skip file of
  that list and `tools/targets/win-x86`'s addresses. One that is also a hook point stays one: a mod's hooks run around
  the native function.

What differs, fix by fix:

- **Built-in fixes** (`RS_MODS_FIXES`, `RS_MODS_BYPASS`):
  - Batched DLC mounting: this game's loop has to wait for each mount. An install step starts mounting one archive and
    leaves its file open; the update's head, which the game calls once a frame, finishes the mount and closes the file,
    and a step taken before that drops the next archive. So the loop calls the head until the file is closed (the seam
    counts the open song archives of `dlc/`, `win32_ksync.c`) before the next step. The Mac edition's loop waits the
    same way, on what the game itself has left to load (`dlc_package`, `dlc_package_queue`), and does three things
    this one does not yet: it shortens the asset loader thread's pause while archives are mounted, it holds the flag
    that "Enumerating downloadable content..." follows until every song manifest asked for is in, and it keeps a
    step from asking every mounted archive for the shader cache's files
    ([Mac design](../mac/DESIGN.md#built-in-fixes-and-mods)). Here the flag clears when the last mount is finished.
    The loader reads the manifests the same way in this game, so with a library of thousands the last songs are
    expected to come into the lists after the text has gone, as they did on the Mac: not measured, and open
    ([PLAN.md](PLAN.md#open-issues)).
  - The song path cache wraps the path intern itself, as this build has the Mac game's wrapper around it inlined.
  - The number-table hash is a function of its own here, where the Mac edition replaces the three functions it is
    inlined into. This table masks the hash rather than taking a prime modulus, so the bits are mixed down first.
  - The signature skip: the song loader's check is a function of its own here, and it answers true. As in the Mac
    edition only the song loader's check is skipped, not the DSA verifier itself.
  - The song-key table skip (this game only): the Learn a Song list keeps a song only when the CRC-32 of its `SongKey`
    is in a table of known keys in the image, so a custom song is left out even with its archive mounted. With the fix
    every key counts as known (`RS_MODS_BYPASS=0`: the table again). The Mac image has no such table.
  - Not needed here: the keyboard fixes fill the Mac game's table of Cocoa keys, and this game reads DirectInput
    ([Input](#input)); the thread priority fix works around macOS refusing the Mac game's values, which `LP_QOS` covers.
- **No intro screens**: the shared hook. This game's tick is a stdcall function with the same states, and its fields are
  in `tools/targets/win-x86/offsets.map`.
- **Auto Save warning skipped**: the handler runs as it is and only the dialog's call is left out, where the Mac
  edition's hook does the handler's branch itself. This game's branch has a release the Mac's lacks, so the rest stays
  the game's own: the navigate call (`ui_navigate`) answers that one caller with a token and every other caller gets the
  original, and the native function around the handler (`autosave_warning`) clears the timer once it has returned.
- **Two players on any input**: the check of Player 2's input mode is inlined here into one function,
  `input_p2_force_cable`, which sets Player 2 to Real Tone; a native function in front of it leaves a profile on Direct
  Connect or the microphone as it is. Player 2's saved input is the shared hook.
- **The same at any frame rate**: the shared source, with this game's eight mid points in
  `tools/targets/win-x86/mids.map`. The Windows code has the same counts and the same buffers, and its frame time is
  whole milliseconds too, from `QueryPerformanceCounter`. It keeps the tuner's count of misses in memory, which is why
  the hook reads the count and picks where the function goes on instead of changing it.
- **Skip Tuner in the Guitarcade**: the shared hook, with this game's mid point. The game makes the same test and
  keeps its answer in a local, where the Mac's is in a register.
- **Loading-screen fix.** Not needed here ([Shader corpus](#shader-corpus)).
- **Direct Connect choice**: the same overlay, made by the same `tools/play-native/cache_overlay.py`. The archives' key
  comes from the unpacked code image, which the build step hands to the copy (`gamefiles.key_from_image`).
  `frontend_open_file` is a native function here: the original's lower-casing of the path is behind a packer stub. For a
  `cacheN.7z` it opens `PatchUpdate/<name>` first, then does what the original does.
- **Exit Game.** The same entry and the same edit as the Mac edition's: the game has the action.
- **Enumerate.** The same entry and the same code as the Mac edition's: the overlay's two edits, and
  `shims/steam/src/enumerate.h`, compiled in through `interfaces.cpp`. `steam32.c` answers `IsOverlayEnabled` and posts
  the `DlcInstalled_t`, and the game folder is `LP_GAME_DIR`.
- **Song ownership during a mount**: the shared hook, which this edition's mounting loop tells when its steps run.
  `RS_MODS_BYPASS=0` also brings back the song-key table, which hides custom songs here whatever their AppID.
- **Title logo.** The same `tools/loadscreen` and layout as the Mac edition's. `lp_install.py build` makes
  `static.psarc.rearmed` in the copy, and `title_logo()` in `lp_main.c` links `static.psarc` at each start as
  `launch.sh` does (`RS_REARMED=0` for the original).
- **Mods** (`tools/ws7/recomp/runtime/rc_mods.c`, shared, called from `win-x86/app/lp_main.c`): every `.dylib` in
  `<home>/mods/` is loaded at startup, before the game runs, with the Mac edition's switches and log lines and the same
  `rc_mod.h`; `rc_target()` returns `win-x86`. A library without `rc_mod_abi` is left out, with a log line: built for
  the Mac edition alone, it would misread this game's memory. The network guard is in the process whenever mods load
  ([Online services](#online-services)). With mods the installed app goes to `LP_GAME_DIR`, the Mac edition's working
  directory, before it loads them, so a mod finds its settings there. What a mod has:
  - **Hook points.** `rc_hook_add_name` as on the Mac; the hooks run on a view of the call, the Mac's SysV one, made
    from `tools/targets/win-x86` ([recompiler](../recompiler/README.md#overrides-and-hook-points)). Of the 21 hook
    points (`tools/targets/names/hooks.list`) 19 work here. Seventeen are functions of this game, nine of them in this
    game's own shape, adapted in `symbols.map`. Two, `input_sink` and `set_app_focus`, have no function here (the game
    has window messages and an inlined tick), and `win32_input.c` runs them instead, with the Mac's values: macOS key
    codes, the character, the pointer from the bottom left. Two are `unsupported`, and a hook on them gives -1 and a log
    line: `si_io` is the Mac game's CoreAudio input ([Audio](#audio)), and `input_mode_is_cable` is inlined into its
    caller, which the built-in fix replaces.

    What differs with the call the same:
    - `game_frame` is the ServiceManager tick of the Run loop, so it runs more often than frames are presented.
    - A hook point that is also a fix of this edition's own (`autosave_warning`, `frontend_open_file`) runs a mod's
      hooks around the fix, where the Mac edition's fix is a pre hook that can answer first. The shared fixes
      (`boot_intro_update`, `steam_dlc_owned`, `input_bind`) are pre hooks in both, so a mod's hook on those comes after
      the fix in both.
    - While a mod hooks `input_sink` or `set_app_focus`, events wait for the game thread (at the start of each Present)
      and reach the game a frame later. `input_sink`'s arguments, changed by a pre hook, are not taken
      (`set_app_focus`'s is). A modifier key is a down and an up here, where the Mac sends a down for both.
    - An argument this game's code has folded away is not taken (`input_bind`'s `force`, `ui_event`'s last two): a hook
      reads its one value, and a change to it goes nowhere.
  - **Edits and mid points** (`tools/targets/win-x86/edits.map` and `mids.map`): the Mac edition's two groups,
    `seven_string` (735 edits and 127 mid points: the game's arrays for six strings grown to eight) and `plain_packages`
    (2 edits and a mid point on the song file's decrypt), and the eight `fps_*` mid points of the built-in frame rate
    fixes. The 29 points only the Mac needs are `none` here.
  - **Files** (`rc_file_provide`, `rc_file.h`): the same interface as the Mac edition's. The Win32 seam's opens for
    reading, reads, seeks, sizes, attributes, finds and mapped views (`win32_kfile.c`, `win32_ksync.c`) go through the
    runtime's `rc_file_*`, which ask the mod's provider first. The provider sees host paths, after the seam's mapping of
    `C:\Rocksmith2014` and its lookup without case.
  - **Drawing.** The render API (`rs_render.h`, [METAL.md](../mac/METAL.md#render-api-shimsincluders_renderh)) is in
    libRSOpenGL, under the D3D9 layer. The overlay API is the Mac edition's `rsp_overlay.c`, run in `d3d9_dev_present`
    on the back buffer before it is shown.

  The rearmed-mods library (`rearmed-mods.dylib`, from 0.3.0) runs here as the same file as on the Mac. What it
  needs of the game is in the tables ("the mods library" in `tools/targets/names`).

## Installer and build

`win-x86/install/lp_install.py` does the steps (standard-library Python, only tools macOS ships).
`tools/play-native/learn-play.sh` runs all of them in a terminal, and `RS Re-ARMed Installer.app` runs the same commands
with its own steps ([Mac design](../mac/DESIGN.md#installer-and-build), which has the installer app, the flow of both
editions and the parts both share). Neither touches the Mac game install or its saves.

1. **The game** (`game-locate`, `game-space`, `game-redirect`, `game-download`): Steam's download of depot 221683, got
   as the Mac edition gets a Mac game that is not installed
   ([the game from Steam's console](../mac/DESIGN.md#installer-and-build)), with the same `guide` lines. `lp_install.py`
   has this game's figures and words, and counts the room its build needs with the download's. The exe's SHA-256 is
   checked against `target.toml`.
2. **The DLC** (`gamefiles.py dlc-fetch`): from the DLC depots, as for a downloaded Mac game.
3. **The game files** (`game-copy`, `game-refresh`; `tools/play-native/gamefiles.py`, shared with the Mac install): the
   app's own copy in `LP_HOME/game`, as Steam may clear its depot folder. It is the depot folder plus the PC archives
   `*_p.psarc` of its `dlc/` and the Mac game's `dlc/`. `lp_main.c` refuses to start without the copy's
   `.rearmed/manifest.json`.
4. **The Microsoft DLLs** (`redist`): from Steam's Common Redistributables on the Mac
   (`Steamworks Shared/_CommonRedist`), else Microsoft's two redistributables downloaded with `/usr/bin/curl`
   (SHA-256 pinned; the only network access besides Steam), or a folder given with `--from`. `/usr/bin/tar`
   unpacks the cabinets; every DLL's SHA-256 is checked. D3DCompiler_42 is written rebased.
5. **The code image**: from a capture folder when one is given (`lp_image.py`); else the pinned image an earlier run
   made; else the copy's exe unpacked with the seed (`game-image`; the seed is `LP_SEED`, `win-x86/unpack/seed.bin`, or
   in the installer app's Resources). Without any, `harvest-export` writes the harvester folder to take to the PC and
   the run stops ([Getting the game image](#getting-the-game-image)).
6. **The build** (`build`, `lp_build.py`): each translation is keyed by the SHA-256 of its input, its options,
   the translator's sources, the Python and Capstone versions, and the compiler and runtime headers. A step
   whose key matches is skipped, a stopped run continues, and after an update only what changed is redone. A key
   is deleted before the step it vouches for is redone, so a stopped run never leaves stale output. About 25
   minutes the first time on an M2, with a peak of about 2 GB in the build folder.
7. **The install** (`app-install`): one ad-hoc signed bundle, `~/Applications/RS Re-ARMed Learn & Play.app`:
   `Contents/MacOS` has `rs2014_lp`, `libRSOpenGL.dylib` and `libnetguard.dylib`, `Contents/Resources` the game image
   (`lp_game.exe`), the DLL images, the shader corpus and the game's icon. It sets no environment; `lp_main.c` takes its
   folders from `~/Library/Application Support/RS Re-ARMed/learn-play/` (`LP_HOME`), and the built-in mods switched off
   from `settings.env` there (`lp_settings.c`, the Mac launcher's rules: only those nine switches, and a value in the
   environment wins), read before `mods_guard`, so the start again with the network guard keeps them.
8. **Clean** (`build-clean`): the intermediates go; the translated libraries (about 140 MB) stay as the
   cache that makes the next run skip to the link.
9. **The shaders** (`tools/play-native/warm-shaders.sh`, the Mac edition's): the installed app's `--warm-shaders`
   ([Shader corpus](#shader-corpus)).

`tools/play-native/learn-play.sh` runs all of it in a terminal; `RS Re-ARMed Installer.app` runs the same
commands with its own steps and its Refresh Game Files button runs the installed `gamefiles.py refresh`. Its
Settings and switches… sheet, with Learn & Play chosen, writes `learn-play/settings.env` and
`learn-play/options.env`, as it writes the Mac edition's
([Mac DESIGN](../mac/DESIGN.md#installer-and-build)). Neither touches the Mac game install or its saves.
