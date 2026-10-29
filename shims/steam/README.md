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
make test     # vtable-slot check and ABI tests, arm64 and x86_64 (Rosetta)
```

## Backends

- **real**: forwards to Valve's library, which talks to the Steam client.
  Valve doesn't need to provide an arm64 build: the Steam client already ships
  a universal `libsteam_api.dylib` inside
  `Steam.AppBundle/.../Frameworks/Steam Helper.app/Contents/MacOS/`, and the shim
  finds it automatically. That library is from a modern SDK and **no longer
  exports `SteamAPI_Init`** (newer SDKs make it an inline wrapper), so it can't
  be dropped in directly. The shim maps `SteamAPI_Init` onto
  `SteamAPI_InitSafe`, or onto `SteamInternal_SteamAPI_Init` if `InitSafe` is
  missing. The old interface version strings (`SteamUser020`, ...) are passed
  through unchanged, and the Steam client still serves them.
- **stub**: works fully offline:
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
    with it the game signs in to Ubisoft by itself.
  - The identity (SteamID, persona) comes from `config/loginusers.vdf`, and
    the language from the game's appmanifest.
  - Owned: the base app (221680), the DLC that Steam installed, read from
    the `dlcappid` entries of the appmanifest's `InstalledDepots` (Steam
    installs only DLC the account owns), and 248750, a free DLC that most
    custom songs carry (the toolkit's default). The game hides every song
    whose archive's AppID is not owned: with only the base app owned, the
    native build listed far fewer songs than the Steam version. When the
    game folder is a stage of symlinks (the native launcher's), the
    appmanifest is found next to the real install through the stage's
    `static.psarc` link, in whichever Steam library that is (e.g. one on an
    external drive). `RS_STEAM_DLC` / `dlc`: `installed` (default), `all`
    (opt-in: every app id is owned, so every archive shows up whatever its
    AppID), `none`, or a comma-separated list of app ids (only those).

`auto` (the default) uses real when Valve's library loads and
`SteamAPI_Init` succeeds through it, and stub otherwise. Settings go in
`steam_shim.ini`, or in `RS_STEAM_*` environment variables (see
[steam_shim.ini.example](steam_shim.ini.example)).

The native app bundles the arm64 slice (`tools/ws7/recomp/app/build.sh`); the launcher
(`tools/play-native`) runs it in stub mode.
