# steam_api.dll for the Windows build (offline)

The win-x86 counterpart of the Mac layer in [shims/steam](../../../shims/steam/README.md): what the
32-bit Learn & Play binary imports from `steam_api.dll`, answered inside the process. It never
loads Valve's library and never talks to Steam or the network.

```
make test                                   # guest-ABI test, no game needed
make slots STEAM_API_DLL=<game>/steam_api.dll PY=<python with capstone>
```

## What it does

- **Exports.** The flat API of the 2019 SDK the game's DLL comes from (`SteamAPI_Init`,
  `SteamAPI_RunCallbacks`, `SteamAPI_RegisterCallback` / `RegisterCallResult` and their
  `Unregister`s, `SteamInternal_ContextInit` / `CreateInterface` /
  `FindOrCreateUserInterface`, `SteamAPI_GetHSteamUser` / `Pipe`, `RestartAppIfNecessary`, ...)
  plus the crash-reporting no-ops (`SteamAPI_SetMiniDumpComment`, `WriteMiniDump`, ...) and
  the game-server entry points (all "no"). The pre-1.43 accessors (`SteamUser()`, ...) are
  served too, although this DLL no longer has them. All are cdecl.
- **Interfaces** are guest objects whose vtables are thunk cells
  ([win32_guestobj.h](../win32_guestobj.h)): MSVC thiscall, `this` in ecx, the callee removes
  its stack words, a `CSteamID` result through a hidden pointer. Every slot of every interface
  in the DLL's flat API has its stack-word count, so even a method nothing implements returns
  cleanly (0 / false / invalid handle). [steam32_slots.h](steam32_slots.h) is generated from the
  game's DLL by [gen_slots.py](gen_slots.py) (slot and argument size from each
  `SteamAPI_I<Interface>_<Method>` wrapper's code). MSVC orders overloaded virtuals in reverse,
  so e.g. `ISteamUserStats::GetStat(float *)` is slot 1 here and 2 on the Mac.
- **Behaviour** is the Mac layer's stub mode, from the Mac layer's sources (the `Makefile` here):
  `steam32_host.cpp` compiles its `interfaces.cpp`, and its `shim.cpp` is linked for the settings and the
  log; that file's own `SteamAPI_*` functions, and with them the real backend, are never called. So the
  same answers: the identity, the language and the owned apps
  ([Settings](#settings)), Steam Cloud in `<data_dir>/<account>/221680/remote` seeded from Steam's
  copy without `crd`, local stats and achievements. Implemented: ISteamClient, ISteamUser,
  ISteamFriends, ISteamUtils, ISteamApps, ISteamUserStats, ISteamRemoteStorage; ISteamHTTP,
  ISteamMatchmaking and the rest are all-zero objects with the right layouts; an unknown version gets a
  160-slot null object. The Enumerate entry of the main menu comes with that code (`enumerate.h`):
  `IsOverlayEnabled` counts as the press, and `steam32.c` posts the `DlcInstalled_t`.
- **Callbacks** are dispatched by `SteamAPI_RunCallbacks` on the calling thread:
  `UserStatsReceived_t` after `RequestCurrentStats`, `UserStatsStored_t` after `StoreStats`,
  the call results of `FileWriteAsync` / `FileReadAsync`. Structs use the Windows packing (8).
  `CCallbackBase`'s vtable is `[0] Run(void *, bool, SteamAPICall_t)`, `[1] Run(void *)`,
  `[2] GetCallbackSizeBytes()` (MSVC order; checked against the `CCallResult` objects the DLL
  itself builds).
- **Uplay.** The game does not reference `uplay_r1_loader.dll` (no import, no name in the
  captured image or the packed exe). `steam32_dll_blocked()` names it, the other Ubisoft
  loaders and Valve's client libraries, for a `LoadLibrary` shim to refuse.

## Settings

The Mac layer's `RS_STEAM_*` variables ([shims/steam](../../../shims/steam/README.md#settings)), which the app
sets before the game starts (`win-x86/app/lp_main.c`):

- `RS_STEAM_DATA_DIR`: `learn-play/steam`, the saves.
- From `game/.rearmed/steam.env`, which the copy of the game files records, each unless it is set already:
  `RS_STEAM_DLC` (the installed DLC, 248750 and every AppID the copy's `dlc/` archives carry),
  `RS_STEAM_OWNED` (the first Rocksmith's DLC the account owns, for the RS1 compatibility pack's songs;
  `BIsSubscribedApp` only), `RS_STEAM_LANGUAGE` and `RS_STEAM_ID` (the account whose license has Learn & Play).
- Without Steam's `loginusers.vdf` (Steam uninstalled), `RS_STEAM_ID` is the one account with saves in the data
  folder.

`steam_shim.ini` is not how this edition is set up. The Mac layer looks for it in `.rearmed/` of the folder
that holds the app bundle and beside the binary; Learn & Play's app is in `~/Applications`, so neither place is
its game folder, and the second is inside the signed bundle. Without the variables the Mac layer's defaults
apply: the most recent account of `loginusers.vdf`, and the language and DLC of an appmanifest for the game in
Steam's own library, plus 248750. `RS_STEAM_MODE` changes nothing here.

## How the seam uses it

- `win32_dispatch.c` answers every `steam_api.dll!*` import with `steam32_import(name)`.
- `win32_kernel32.c`'s `LoadLibraryA/W` fail with `ERROR_MOD_NOT_FOUND` when `steam32_dll_blocked(name)`.
- `win-x86/build/build_shims.sh` builds `libsteam32.a`, linked with `win32_guestobj.o` and `-lc++`.
- `GetUserDataFolder` hands out a host (POSIX) path, which the kernel32 file shims take as it is;
  `GetAppInstallDir` the guest's path of the game, `C:\Rocksmith2014`.

The Windows port's design: [docs/windows/DESIGN.md](../../../docs/windows/DESIGN.md#steam).
