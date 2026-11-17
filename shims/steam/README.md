# Steam API replacement

`libsteam_api.dylib` is a universal (arm64 + x86_64) drop-in for the
Steamworks library Rocksmith 2014 loads from `@loader_path/libsteam_api.dylib`.
It exports the 12 symbols the game imports, plus `SteamAPI_InitSafe`,
`SteamAPI_IsSteamRunning`, `SteamAPI_GetHSteamPipe`,
`SteamAPI_ReleaseCurrentThreadMemory` and
`SteamInternal_FindOrCreateGameServerInterface`.

```
make          # builds build/libsteam_api.dylib and ./libsteam_api.dylib
make check    # exports vs. the game's imports, both slices
make test     # vtable-slot check, ABI tests (arm64, and x86_64 under Rosetta), Enumerate, mode auto
```

The native app bundles the arm64 slice (`tools/ws7/recomp/app/build.sh`), and the launcher
(`tools/play-native/launch.sh`) always runs it in stub mode. Learn & Play compiles the same sources into its
32-bit Steam face: [win-x86/shims/steam](../../win-x86/shims/steam/README.md).

## Backends

- **stub**: works fully offline, and is what an installed game runs:
  - Mock `ISteamClient/User/Friends/Utils/Apps/UserStats/RemoteStorage`
    objects. Every other interface (including `ISteamHTTP`) gets a
    160-slot object that returns 0 from every slot, so the game never gets a
    NULL interface.
  - A local callback and call-result dispatcher run by `SteamAPI_RunCallbacks`.
    `RequestCurrentStats` produces `UserStatsReceived_t`, `StoreStats` produces
    `UserStatsStored_t`, and the async cloud reads and writes produce their
    call results.
  - Steam Cloud is a directory, `<data_dir>/<account>/221680/remote`. Writes
    are atomic (temp file + rename). On first use the directory is **seeded
    with a copy** of the real `Steam/userdata/<account>/221680/remote`
    (`*_PRFLDB` profiles, `LocalProfiles.json`). The real Steam folder
    is never written. `crd`, the game's stored Uplay credentials, is left out
    (and removed from an existing copy) unless `RS_STEAM_KEEP_CREDENTIALS=1`:
    with it the game signs in to Ubisoft by itself, so no install sets it.
  - The identity (SteamID, persona) comes from `config/loginusers.vdf`, and
    the language from the game's appmanifest.
  - Owned: the base app (221680), the DLC that Steam installed, read from
    the `dlcappid` entries of the appmanifest's `InstalledDepots` (Steam
    installs only DLC the account owns), and 248750, a free DLC that most
    custom songs carry (the toolkit's default). The game hides every song
    whose archive's AppID is not owned. When the
    game folder is a folder of symlinks, the appmanifest is found next to
    the real install through its `steam_appid.txt` or `static.psarc` link,
    in whichever Steam library that is (e.g. one on an external drive).
  - The Enumerate entry of the main menu (`src/enumerate.h`): the game's `IsOverlayEnabled`
    question counts as the press, new archives are copied into the install's `dlc/`, and a
    `DlcInstalled_t` for 248750 makes the game scan them
    ([DESIGN.md](../../docs/mac/DESIGN.md#built-in-fixes-and-mods)).
- **real**: forwards to Valve's library, which talks to the Steam client. The Steam client ships a
  universal `libsteam_api.dylib` inside
  `Steam.AppBundle/.../Frameworks/Steam Helper.app/Contents/MacOS/`, and the shim
  finds it. That library is from a modern SDK and **no longer exports `SteamAPI_Init`**, so it can't
  be dropped in directly: the shim maps `SteamAPI_Init` onto
  `SteamAPI_InitSafe`, or onto `SteamInternal_SteamAPI_Init` if `InitSafe` is
  missing. The old interface version strings (`SteamUser020`, ...) are passed
  through unchanged, and the Steam client still serves them.
- **auto** (the default without a setting): real when Valve's library loads and
  `SteamAPI_Init` succeeds through it, and stub otherwise; listeners the game
  registered before a failed `SteamAPI_Init` are the stub's from then on.

## Settings

Each setting is an `RS_STEAM_*` environment variable or a key of `steam_shim.ini`
([steam_shim.ini.example](steam_shim.ini.example) lists them); the variable wins. The file is looked for in two
places (`load_config` in `src/shim.cpp`): `.rearmed/steam_shim.ini` in the folder that holds the app bundle,
then `steam_shim.ini` beside the library. For an installed Mac game the first is the copy of the game files,
`…/RS Re-ARMed/game/.rearmed/`, which a reinstall keeps.

An install needs no file. The launcher passes `RS_STEAM_MODE=stub` and `RS_STEAM_DATA_DIR` (`…/RS Re-ARMed/steam`),
and from `game/.rearmed/steam.env`, which `tools/play-native/gamefiles.py` writes with the copy, these four,
each unless it is set already (`launchctl setenv`, or the installer's `options.env`):

- `RS_STEAM_DLC` / `dlc`: `installed` (the default: the appmanifest's DLC and 248750), `none`, or a
  comma-separated list of app ids (only those). The install passes such a list: the installed DLC, 248750 and
  every AppID the copy's `dlc/` archives carry, so custom songs show up whatever AppID they carry. Set by hand it
  takes the place of that list, which only a test has a use for.
- `RS_STEAM_OWNED` / `owned`: a comma-separated list of apps the account owns outside this game's DLC, reported
  by `BIsSubscribedApp` only (not `BIsDlcInstalled`, not `GetDLCCount`). The install fills it with the first
  Rocksmith's DLC that count as owned, which the game asks about for each song of the RS1 compatibility pack
  ([DESIGN.md](../../docs/mac/DESIGN.md#steam)). This is the one key a value set by hand doesn't replace: the
  launcher adds its AppIDs to the copy's, which a test uses to make one more DLC count.
- `RS_STEAM_LANGUAGE` / `language`, and `RS_STEAM_ID` / `steam_id`: the account whose license has the game,
  whichever is logged in to Steam later.
