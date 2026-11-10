# Getting the Learn & Play game image on a Windows PC

A player with the released installer needs neither path: it unpacks the game on the Mac. These are for a build
from a clone of the repository, and for making a release. Each takes about 5 minutes on a Windows PC where you own the
game on Steam.

## Path A: harvest the image

For a build without the seed. The harvester reads the running game's memory, without a debugger or injection. It
ships prebuilt at `win-x86/harvest/rsharvest2.exe` ([its README](../../win-x86/harvest/README.md)), and the PC
needs nothing installed for it.

### On your Mac

Export the harvester files to a folder you can take to the PC (the installer and `learn-play.sh` do this by
themselves when they find no seed, into `harvest` in the build folder):

```bash
python3 win-x86/install/lp_install.py harvest-export ~/Desktop/harvest-kit
```

### On the Windows PC

1. Install **Rocksmith 2014 Learn & Play** from Steam if it isn't there.
2. In the `harvest-kit` folder, double-click **`harvest-game.bat`**. The game starts, with the recorder.
3. Reach the main menu, start any song, play about 30 seconds.
4. Switch to the recorder window and press **Enter**. It writes **`Desktop\rs-harvest.zip`**.

### Back on your Mac

Copy `rs-harvest.zip` to the Mac, unzip it, and choose the folder in the installer app, or:

```bash
tools/play-native/learn-play.sh ~/Downloads/rs-harvest
```

The capture is your own game's data and stays with you. It is needed once: the image built from it is kept for
later runs.

## Path B: capture the seed

Once per Steam build, by a maintainer. The seed lets `lp_unpack.py` unpack the exe on any Mac without Windows. It
ships in the released installer app (about 190 KB, no game code), never in git, and with it the installer and
`learn-play.sh` make the game image by themselves.

The seed is VMProtect's state partway through unpacking. It is made in a QEMU VM running Windows with a GDB stub
(`qemu_vm.sh`, `qemu_capture.py` and `make_seed.py --from-gdb` in `win-x86/unpack/`), which needs a 32-bit Windows
ISO. The steps are in [win-x86/unpack/README.md](../../win-x86/unpack/README.md); to put a seed into a release, see
[Shipping the seed](../../win-x86/unpack/README.md#shipping-the-seed). To try one from a clone:

```bash
LP_SEED=/path/to/seed.bin tools/play-native/learn-play.sh
```

## Which path do I need?

| I want to… | Use |
|---|---|
| Install the game with the released installer | Neither: it has the seed |
| Build a clone of the repository, with no seed | **Path A** (harvest) |
| Ship a release for a new Steam build of the game | **Path B** (seed) |
