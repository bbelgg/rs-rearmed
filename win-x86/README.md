# Win-x86: Learn & Play static recompiler

Translates the 32-bit Windows "Learn & Play" build of Rocksmith 2014 to an arm64 macOS binary with
the ws7 recompiler's 32-bit front end (`tools/ws7/recomp`, PE mode). Playable (Learn a Song,
Score Attack, Guitarcade, Session Mode, Nonstop Play, lessons, window or full screen, the Real Tone cable, Direct Connect). The docs are in
[docs/windows](../docs/windows/README.md): install and play ([INSTALL.md](../docs/windows/INSTALL.md)), how it
works ([DESIGN.md](../docs/windows/DESIGN.md)), status and open work ([PLAN.md](../docs/windows/PLAN.md)),
tests and diagnostics ([TESTING.md](../docs/windows/TESTING.md)).

## Architecture

```
game image (image/lp_image.py, unpack/ or a harvest)    msvcr100 / msvcp100 / d3dx9_42 / D3DCompiler_42
        |  gen_image_pe.py (--wrappers)                      |  gen_image_pe.py --module NAME
        v                                                    v
   game/obj/libgame_rc.a                           modules/NAME/obj/libgame_rc.a
        \_______________________  ________________________/
                                v  link
   app/lp_main.c + shims/ (Win32 seam, D3D9 on libRSOpenGL, offline Steam) + rc32 runtime
                                v
                            rs2014_lp
```

At run time `rs2014_lp` maps every image at its preferred base in the 4 GB guest window, binds the
imports between them (`msvcrt.dll` is an alias of `msvcr100`), starts the DLLs (DllMain), and calls
the game's entry point on a thread with a large host stack. An import no image exports goes to the
Win32 seam by name (`shims/win32_dispatch.c`).

## Components

- `unpack/` — the exe unpacked on the Mac with the release's seed (`lp_unpack.py`), and how a seed is made
- `image/` — the game image from the unpacked exe or a harvest (`lp_image.py`) and its P1 facts (`lp_p1.py`)
- `install/` — install steps: the game from Steam, the code image, the Microsoft DLLs (`lp_install.py`; pins in `tools/targets/win-x86/target.toml`),
  the build, the app bundle and the clean-up (`lp_build.py`, through `lp_install.py build`, `app-install`, `build-clean`)
- `imports/` — the import tables (IAT slots and VMProtect wrapper sites) at link addresses
- `build/` — `translate.sh` (the game, ~15-25 min), `build_modules.sh` (the DLLs), `build_shims.sh`,
  `build_gl.sh` (libRSOpenGL without CMake), `build.sh` (everything, then the link)
- `shims/` — the Win32 seam: `win32_kernel32.c` / `win32_kfile.c` / `win32_ksync.c` (kernel32),
  `win32_user32.c`, `win32_net.c` (ws2_32, iphlpapi, wininet: offline), `win32_misc.c` (advapi32,
  ole32, shell32, winmm, setupapi, rpcrt4, dinput8, xinput1_3, gdi32, bink2w32), `win32_d3d9.c`
  and `d3d9/` (Direct3D 9 on libRSOpenGL), `steam/` (offline steam_api), `audio/` (WASAPI and
  winmm on CoreAudio)
- `shaders/` — the Metal shader corpus's sources: the list of programs the game links (`programs.tsv`, hashes only),
  `lp_shaders.c`, which makes their GLSL from the user's game files, and `record_programs.py`, which adds a run to the list
- `online/` — the offline online layer: RSConnect's vtables patched at start-up (`online32.c`)
- `app/` — `lp_main.c`: load, bind, start, run
- `harvest/` — the one-time capture tool for a Windows PC (prebuilt, with its source)
- `crt/`, `d3dx/`, `bink/` — the CRT, D3DX and Bink probes (the DLLs without the game)
- `test/` — the D3D9 layer's and the window rules' host tests

## Build and run

The installer does all of it: `tools/play-native/learn-play.sh` ([INSTALL.md](../docs/windows/INSTALL.md)) builds into
`$RS_BUILD_DIR/win-x86/lp` with `lp_install.py build` and installs `~/Applications/RS Re-ARMed Learn &
Play.app`, which reads its folders from `~/Library/Application Support/RS Re-ARMed/learn-play`
(`LP_HOME`: `game/`, the app's own copy of the game files, `game-dir`, `userdata/`, `steam/`, `logs/`;
`app/lp_main.c`). By hand:

Every input path is an environment variable; nothing derived from Microsoft's or the game's
binaries is committed.

```sh
export LP_BUILD=<build root> LP_PY=<python with capstone>
export LP_GAME_DIR=<the Windows game folder>       # the build reads bink2w32.dll from it (or LP_BINK2W32)
export LP_IMAGE=<lp_game.exe from image/lp_image.py>
export LP_MSVCR100=<msvcr100.dll> LP_MSVCP100=<msvcp100.dll> LP_D3DX9=<d3dx9_42.dll> LP_D3DCOMPILER=<D3DCompiler_42.dll>
export LP_CMAKE_DIR=<cmake build dir with libRSOpenGL (cmake --build DIR --target opengl)>
#   or, without CMake: OUT_DIR=<dir> win-x86/build/build_gl.sh; export LP_GL_DIR=<dir>
LP_TRANSLATE=1 LP_MODULES_BUILD=1 win-x86/build/build.sh

$LP_BUILD/out/rs2014_lp
```

Run-time and diagnostics variables (all read with `getenv` in `app/`, `shims/` and the runtime) are
listed in [docs/windows/TESTING.md](../docs/windows/TESTING.md#diagnostics).
