# Getting the game image on the Mac

The Mac edition needs no capture: no Windows PC, no harvester and no seed. Those belong to Learn & Play, whose
Windows exe is packed with VMProtect ([Windows capture](../windows/CAPTURE.md)). The Steam macOS game is only
encrypted by SteamStub, so everything happens on your Mac during the install.

## Path A: harvest the image

Not needed. The installer decrypts the Steam copy's code from the file on disk, in a few seconds, without
starting the game. By hand: [the game image](INSTALL.md#3-the-game-image). How it works:
[DESIGN.md](DESIGN.md#getting-the-game-image).

## Path B: capture the seed

Not applicable. A seed is VMProtect state, and the Mac game has no VMProtect.

## Which path do I need?

Neither. Run the installer.
