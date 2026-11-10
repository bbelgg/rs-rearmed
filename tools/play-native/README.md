# Everyday play: the native arm64 build

The install scripts and the Mac launcher. The user documentation is [docs/mac/INSTALL.md](../../docs/mac/INSTALL.md)
for the Mac game and [docs/windows/INSTALL.md](../../docs/windows/INSTALL.md) for Learn & Play; what an install
puts where is under "Where things are" in each
([Mac](../../docs/mac/INSTALL.md#where-things-are), [Learn & Play](../../docs/windows/INSTALL.md#where-things-are)).

```bash
tools/play-native/install.sh [APP]       # after a build (tools/ws7/recomp/app/build-m4.sh); re-run to update
tools/play-native/install.sh --refresh-game   # sync the install's copy of the game files (new DLC, CDLC)
tools/play-native/warm-shaders.sh [APP]       # compile the Metal shaders again (after a macOS update)
python3 tools/play-native/test_gamefiles.py   # the copy's tests
python3 tools/play-native/test_cache_overlay.py   # the menu overlay's tests
python3 tools/play-native/test_settings.py   # settings.env, read by both editions' launchers
```

`APP` defaults to the build-m4.sh output under `$RS_BUILD_DIR` (default `build/` in the repo). Then start
**RS Re-ARMed** from `~/Applications`. `RS_NATIVE_HOME` and `RS_NATIVE_LAUNCHER` move the install's folder and
the launcher; `RS_RENDERER=metal|opengl` sets the renderer, `RS_WARM_SHADERS=0` skips
the shader warm-up, `RS_GAME_COPY_DIR=DIR` puts the copy of the game files in `DIR/game`.

| File | What |
|---|---|
| `install.sh` | the Mac install: the copy of the game files, the app, the title logo, the launcher, the shader warm-up |
| `launch.sh`, `launch-stub.c` | the launcher in `RS Re-ARMed.app`; the small program runs the script, so macOS's privacy answers go to the app |
| `learn-play.sh` | Learn & Play, from the game files to the installed app (the steps are `win-x86/install/lp_install.py`'s) |
| `gamefiles.py` | the install's own copy of the game files, for both editions: `sync`, `refresh`, `check`, `dlc-fetch`; APFS clones when the game is on the same volume, else a real copy |
| `cache_overlay.py` | the menu overlay (Direct Connect, Enumerate, Exit Game), a patch archive made from the user's own `cache.psarc` |
| `psarc_key.py` | the archives' key, read from the player's own game image; nothing of it is in the repository |
| `warm-shaders.sh` | runs the installed app with `--warm-shaders`, either edition's |

How the Mac launcher runs the game, every launch:
- **Offline.** Steam shim in stub mode (no Steam client, no Steam Cloud), `[Net] AutoLogin=0` and
  `[Renderer.MacOS] DisableBrowser=1` forced in the copy's `Rocksmith.ini`, and libnetguard refusing every
  non-local connection. Enter on the title goes straight to the profile list: no Uplay login is shown
  (`signin_start_no_uplay`).
- **Saves copied once.** The real Steam folder is only read, on the first launch, and never written; native
  progress stays in `…/steam`. `crd` (stored Ubisoft credentials) is not copied, and any `crd` in the copy is
  deleted before each launch.
- **The game's own files.** The game reads only the copy in `game/`, so a Steam update, uninstall or library
  move does not change it, and the Steam install itself is never changed. `--refresh-game` copies only new
  and changed files, and removes from the copy what the game folder lost.
- **The ini.** `Rocksmith.ini` in the copy is the game's own from its first save on (the install's, when it
  has one, is only the start). `RS_LATENCY_BUFFER=N` (1-16, `launchctl setenv`) overrides its
  `[Audio] LatencyBuffer`.
- **The title logo.** `static.psarc` is linked to `static.psarc.rearmed` when it is there
  (`tools/loadscreen`; `RS_REARMED=0` for the original).
- The game is started with `open -n -a … --env …`: exec'ing it from the launcher leaves it without a
  window when the launcher was opened from the Finder.

## Learn & Play (the Windows game)

```bash
tools/play-native/learn-play.sh [HARVEST]   # or install.sh --learn-play; re-run to update
tools/play-native/learn-play.sh --refresh   # sync the app's copy of the game files (new DLC)
```

One run: the game and your DLC (Steam downloads them, nothing to paste), the Microsoft DLLs, the code image, the build
(about 25 minutes the first time on an M2, about 2 GB free; resumable), the install and the Metal shaders. Then start
**RS Re-ARMed Learn & Play** from `~/Applications`. There is no launcher script: the app reads its folders itself
(`win-x86/app/lp_main.c`).

The build is in `$RS_BUILD_DIR/win-x86/lp`; afterwards only the translated libraries stay, a cache for the next
run (`LP_KEEP_BUILD=1` keeps everything, `0` nothing). `LP_GAME_DIR`, `LP_SEED`, `LP_HARVEST` and `LP_DOWNLOAD_DIR`
name the game folder, the unpacker's seed, a capture and a folder for Steam's download; the script's header has
each step.

Uninstall: [INSTALL.md](../../docs/windows/INSTALL.md#update-and-uninstall).
