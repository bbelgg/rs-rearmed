# Getting the Learn & Play game image on a Windows PC

A player with the released installer needs neither path: it unpacks the game on the Mac. These are for a build
from a clone of the repository, and for making a release. Each takes about 5 minutes on a Windows PC where you own the
game on Steam.

## Path A: harvest the image

For a build without the seed. The harvester reads the running game's memory — no debugger, no injection, no anti-debug
trouble. It ships prebuilt at `win-x86/harvest/rsharvest2.exe`.

### On your Mac

Export the harvester files to a folder you can take to the PC:

```bash
python3 win-x86/install/lp_install.py harvest-export ~/Desktop/harvest-kit
```

Copy that folder to a USB stick or transfer it to the PC.

### On the Windows PC

1. Install **Rocksmith 2014 Learn & Play** from Steam if it isn't there.
2. Open the `harvest-kit` folder.
3. Double-click **`harvest-game.bat`**.
4. The game starts. Reach the main menu, start any song, play about 30 seconds.
5. Switch to the recorder window and press **Enter**.
6. It writes **`Desktop\rs-harvest.zip`**.

### Back on your Mac

Copy `rs-harvest.zip` to the Mac, unzip, and either:

```bash
tools/play-native/learn-play.sh ~/Downloads/rs-harvest
```

or choose the folder in the installer app.

Done — you have a working arm64 Learn & Play build.

---

## Path B: capture the seed

Once per Steam build, by a maintainer. The seed lets `lp_unpack.py` decompress the exe on any Mac without Windows. It ships in the
released installer app (about 190 KB, no game code), never in git, and with it the installer and
`learn-play.sh` make the game image by themselves. It is made once per Steam build of the game, by a
maintainer. A player with the released installer needs neither path.

The seed captures VMProtect's VM state mid-decompression — before the anti-debug verdict,
at the point where it starts writing to `.text`. This requires either:

- A **QEMU VM** running Windows with a GDB stub (the tooling is ready in `win-x86/unpack/`:
  `qemu_vm.sh` + `qemu_capture.py` + `make_seed.py --from-gdb`), or
- A **real Windows PC** with a debugger that can hide from VMProtect (x32dbg + ScyllaHide,
  hardware write breakpoint on `.text`).

The QEMU route is documented in [win-x86/unpack/README.md](../../win-x86/unpack/README.md), and
is how the seed for the current build was made. It needs a 32-bit Windows ISO for the VM.

To put a seed into a release, see [Shipping the seed](../../win-x86/unpack/README.md#shipping-the-seed).
To try one from a clone:

```bash
LP_SEED=/path/to/seed.bin tools/play-native/learn-play.sh
```

---

## Which path do I need?

| I want to… | Use |
|---|---|
| Install the game with the released installer | Neither: it has the seed |
| Build a clone of the repository, with no seed | **Path A** (harvest) |
| Ship a release for a new Steam build of the game | **Path B** (seed) |

Path A stays as the fallback: without a seed the installer asks for a harvest folder, and
`learn-play.sh` takes one.
