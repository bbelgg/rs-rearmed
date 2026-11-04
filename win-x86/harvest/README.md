# The game capture (harvester)

The Windows game's exe is packed with VMProtect: its code is decrypted only in memory while the game runs.
The released installer unpacks the exe on the Mac with its seed and needs none of this. A build without the seed,
from a clone of this repository, makes its game image from a one-time capture of that code, made by you on a Windows
PC where you own the game. RS Re-ARMed ships no game code; the capture is your own game's data and stays with
you. How it fits into the install: [docs/windows/INSTALL.md](../../docs/windows/INSTALL.md).

## What's here

- `rsharvest2.exe`, `rsharvest2.exe.config`: the harvester, prebuilt. A 32-bit .NET Framework 4.8 console
  program; .NET Framework 4.8 is part of Windows 10 and 11, so the PC needs nothing installed, and the Mac
  needs no .NET either.
- `rsharvest2.cs`, `rsharvest2.csproj`: its source. It reads the running game's memory with
  `ReadProcessMemory` and copies the decrypted code pages to a few files: no debugger and no code injection,
  so it does not trip the game's anti-debugging.
- `harvest-game.bat`, `harvest-game.ps1`: the one-click wrapper. It finds your installed game, starts it
  with the recorder and zips the result to `Desktop\rs-harvest.zip`.
- `build.sh`: rebuilds the exe from the source (on a Mac or anywhere with the .NET SDK).

`lp_install.py harvest-export DIR` (run by `tools/play-native/learn-play.sh` and the installer) copies the
exe, its config and the wrapper into a folder to take to the PC.

## On the Windows PC

1. Put `rsharvest2.exe`, `rsharvest2.exe.config`, `harvest-game.bat` and `harvest-game.ps1` in one folder.
2. Install Rocksmith 2014 Learn & Play from Steam if it is not there.
3. Double-click `harvest-game.bat`. It starts the game with the recorder.
4. Reach the main menu, start any song and play about 30 seconds, so the game has run all of its code.
5. Go back to the recorder's window and press Enter. It writes `Desktop\rs-harvest.zip`: `rs_harvest2.bin`,
   `rs_harvest2_last.bin`, `rs_modules.tsv`, `rs_exports.tsv`, `rs_harvest2.txt`.
6. Copy the zip to your Mac, unzip it and pass the folder: `tools/play-native/learn-play.sh ~/Downloads/rs-harvest`,
   or choose it in the installer.

The wrapper expects the supported build of the game (SHA-256 `bb056959c0c6371d4ecf78f84c5d27e2fae93a9d0258830c2a36f33e6a2778eb`).
Another build still captures, with a warning, but the Mac side is pinned to the supported one.

**Tested:** the capture the port is built and played from (2026-11-01) was made with an earlier,
self-contained build of the same harvester. This net48 build of `rsharvest2.cs` has not been run on
Windows yet.

## Verify it yourself

The build is reproducible (`rsharvest2.csproj`: deterministic, no PDB, source paths mapped, no git revision
in the version), so the same source gives the same exe wherever it is built:

SHA-256 of rsharvest2.exe: `7a8ac85d3a1ad81a15de0c00fb0f5cd7d83483b6a9763b7e0a14c1ecc4a2a1e7` (14,336 bytes)

`./build.sh` builds `dist/rsharvest2.exe` with the .NET SDK (Microsoft's installer:
https://dotnet.microsoft.com/download) and prints its SHA-256 next to this one. `./build.sh --install`
copies a new build over the committed files after a change; then update the hash here (the installer tests
check that the committed exe matches it).
