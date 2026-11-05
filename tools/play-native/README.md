# Everyday play: the native arm64 build

The install scripts. The user documentation is [docs/mac/INSTALL.md](../../docs/mac/INSTALL.md) for the Mac game
and [docs/windows/INSTALL.md](../../docs/windows/INSTALL.md) for Learn & Play.

```bash
tools/play-native/install.sh [APP]       # after a build (tools/ws7/recomp/app/build-m4.sh); re-run to update
tools/play-native/install.sh --refresh-game   # sync the install's copy of the game files (new DLC, CDLC)
tools/play-native/warm-shaders.sh [APP]       # compile the Metal shaders again (after a macOS update)
python3 tools/play-native/test_gamefiles.py   # the copy's tests
python3 tools/play-native/test_cache_overlay.py   # the Direct Connect overlay's tests
```

`APP` defaults to the build-m4.sh output under `$RS_BUILD_DIR` (default `build/` in the repo). Then start
**RS Re-ARMed** from `~/Applications` (double-click, Launchpad, Spotlight). The launcher uses the icon
copied from the user's install at install time (`RS2014.icns`); so does the running game.

| Where | What |
|---|---|
| `~/Applications/RS Re-ARMed.app` | the launcher (`launch.sh`, run by the small program `launch-stub.c`, so macOS's privacy answers go to the app) |
| `~/Library/Application Support/RS Re-ARMed/game/` | the install's own copy of the game files (`gamefiles.py`: APFS clones when the game is on the same volume, else a real copy), plus this build's `Rocksmith2014.app` (a copy, so the build directory is not needed to play) and an offline `Rocksmith.ini` |
| `…/game-source`, `…/gamefiles.py` | where the copy comes from, and the tool for `install.sh --refresh-game` |
| `…/steam/<account>/221680/remote/` | **your profiles and progress for the native build**: on the first launch a copy of the real `Steam/userdata/<account>/221680/remote` (`*_PRFLDB`, `LocalProfiles.json`), without `crd` |
| `…/logs/` | `last-run.log`, `previous-run.log`, `netguard.log`, `seed.log` |
| `…/mods/` | mods loaded at startup |

How it runs, every launch:
- **Offline.** Steam shim in stub mode (no Steam client, no Steam Cloud), `[Net] AutoLogin=0`,
  `[Renderer.MacOS] DisableBrowser=1`, and libnetguard refusing every non-local connection. Enter on
  the title goes straight to the profile list: no Uplay login is shown (`signin_start_no_uplay`).
- **Your saves, copied once.** The real Steam folder is only read, on the first launch, and never
  written; native progress stays in `…/steam`. `crd` (stored Ubisoft credentials) is not copied, and
  any `crd` in the copy is deleted before each launch. Copying native progress back to the real saves
  is not planned (do it by hand, with a backup, only if you go back to playing under Rosetta).
- **Your audio settings.** The `Rocksmith.ini` copy is made from the install's on every launch, but
  `[Audio] LatencyBuffer` (the audio engine setting in the game's Audio settings, which the game saves
  to the copy) is kept; `RS_LATENCY_BUFFER=N` (1-16, `launchctl setenv`) overrides it. See the latency
  FAQ in the main README.
- **Your mods and assets.** The install's `cache.psarc` and the
  RS Re-ARMed title logo (`static.psarc.rearmed`, `tools/loadscreen`; `RS_REARMED=0` for the
  original) are used; the real Real Tone cable as usual.
- **The game's own files.** The game reads only the copy in `game/`, so a Steam update, uninstall or
  library move does not change it. New DLC and custom songs reach it with `install.sh --refresh-game` (or
  the installer's Refresh Game Files): only new and changed files are copied, and files the game folder
  lost are removed from the copy. The Steam install itself is never changed, so the Rosetta game keeps
  working as before.
- The game is started with `open -n -a … --env …`: exec'ing it from the launcher leaves it without a
  window when the launcher was opened from the Finder.

## Learn & Play (the Windows game, experimental)

```bash
tools/play-native/learn-play.sh [HARVEST]   # or install.sh --learn-play; re-run to update
tools/play-native/learn-play.sh --refresh   # sync the app's copy of the game files (new DLC)
```

One run: the game and your DLC (Steam downloads them, nothing to paste), the Microsoft DLLs, the code image, the build
(about 25 minutes the first time on an M2, about 2 GB free; resumable), the install and the Metal shaders. Then start
**RS Re-ARMed Learn & Play** from `~/Applications`.

| Where | What |
|---|---|
| `~/Applications/RS Re-ARMed Learn & Play.app` | the game: `rs2014_lp`, libRSOpenGL, the game image and the DLL images; ad-hoc signed |
| `~/Library/Application Support/RS Re-ARMed/learn-play/game/` | the app's own copy of the game files: the Windows game folder and the PC DLC (`*_p.psarc`) |
| `…/learn-play/game-dir` | the game folder the app reads (one line): `game/` |
| `…/learn-play/userdata/` | the game's writes: `game/Rocksmith.ini` (your audio and display settings) |
| `…/learn-play/steam/` | **your profiles and progress** (the offline Steam layer's saves) |
| `…/learn-play/logs/` | `last-run.log`, `previous-run.log`, `netguard.log` (with mods) |
| `…/learn-play/mods/` | mods loaded at startup |
| `$RS_BUILD_DIR/win-x86/lp/` | the build; afterwards only the translated libraries, a cache for the next run (`LP_KEEP_BUILD=1` keeps everything, `0` nothing) |

Uninstall: delete the app and `~/Library/Application Support/RS Re-ARMed/learn-play` (back up `steam/`
first to keep your progress); that removes the copy of the game files too, unless the game is on another drive
([INSTALL.md](../../docs/windows/INSTALL.md#update-and-uninstall)).
