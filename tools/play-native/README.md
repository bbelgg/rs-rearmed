# Everyday play: the native arm64 build

```bash
tools/play-native/install.sh [APP]  # after a build (tools/ws7/recomp/app/build-m4.sh); re-run to update
```

`APP` defaults to the build-m4.sh output under `$RS_BUILD_DIR` (default `build/` in the repo). Then start
**RS Re-ARMed** from `~/Applications` (double-click, Launchpad, Spotlight). The launcher uses the icon
copied from the user's install at install time (`RS2014.icns`); so does the running game.

| Where | What |
|---|---|
| `~/Applications/RS Re-ARMed.app` | the launcher (`launch.sh`) |
| `~/Library/Application Support/RS Re-ARMed/game/` | the game folder mirrored with symlinks, plus this build's `Rocksmith2014.app` (a copy, so the build directory is not needed to play) and an offline `Rocksmith.ini` |
| `…/steam/<account>/221680/remote/` | **your profiles and progress for the native build**: on the first launch a copy of the real `Steam/userdata/<account>/221680/remote` (`*_PRFLDB`, `LocalProfiles.json`), without `crd` |
| `…/logs/` | `last-run.log`, `previous-run.log`, `netguard.log`, `seed.log` |

How it runs, every launch:
- **Offline.** Steam shim in stub mode (no Steam client, no Steam Cloud), `[Net] AutoLogin=0`,
  `[Renderer.MacOS] DisableBrowser=1`, and libnetguard refusing every non-local connection. Enter on
  the title goes straight to the profile list: no Uplay login is shown (override of `0x100a13950`).
- **Your saves, copied once.** The real Steam folder is only read, on the first launch, and never
  written; native progress stays in `…/steam`. `crd` (stored Ubisoft credentials) is not copied, and
  any `crd` in the copy is deleted before each launch. Copying native progress back to the real saves
  is not planned (do it by hand, with a backup, only if you go back to playing under Rosetta).
- If a boot sticks on the white loading screen (with a fast-load `cache.psarc` mod, now and then when the
  Mac is busy), quit and start again.
- **Your mods and assets.** The install's `cache.psarc` (e.g. a fast-load mod: title in ~11 s) and the
  RS Re-ARMed title logo (`static.psarc.rearmed`, `tools/loadscreen`; `RS_REARMED=0` for the
  original) are used; the real Real Tone cable as usual.
- New DLC folders in the install are linked on the next launch. The install itself is never changed,
  so the Rosetta game keeps working as before.
- The game is started with `open -n -a … --env …`: exec'ing it from the launcher leaves it without a
  window when the launcher was opened from the Finder.
