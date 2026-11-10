# Offline VMProtect unpack (Learn & Play Windows exe)

The Learn & Play edition is Windows-only at the Steam-license level, so a Mac-only owner
can download only the **Windows** game (depot 221683) — a 32-bit, VMProtect-packed exe whose real
`.text`/`.rdata`/`.data` are empty on disk and only decrypt in memory while the game runs. This module
produces the clean image the recompiler starts from on the user's Mac, from the user's own exe,
**without running Windows and without redistributing any game code**.

## How it works

VMProtect's bootstrap (decompress → relocate → resolve imports → jump to OEP) is virtualised and
guards itself with an anti-debug verdict that no emulator input can clear. But the **decompression**
of the four sections runs *before and independently of* that verdict, and it is a **deterministic**
function of the VM state.

So the VM state at the point where decompression begins, a small *seed*, is captured **once** and
shipped. On the user's Mac the unpacker:

1. lays the user's on-disk exe out at its section VAs (the game-derived content — never shipped
   by us),
2. applies the seed (a small `.UBX` scratch delta + the VM registers + GDT + VM stack/heap
   scratch),
3. maps synthetic kernel32 stubs (VMProtect only reads one anti-hook byte per call target; a
   benign `8b ff 55 8b ec` prologue passes — **no real DLL bytes**),
4. runs the decompression in Unicorn and dumps the image.

The output is deterministic: the same exe and seed give the same SHA-256 on every run.

## Current status

**It works for the depot build (SHA-256 `bb056959...`, 24.5 MB), through to playing a song.** The seed is
about 190 KB, nearly all of it relocation sites. A run takes about half a minute on an M2. The installer
and `learn-play.sh` use this route whenever a seed is there (`lp_install.py game-image`).

The output is the game as loaded at `0xf80000`, the base the port's tables are for, with its `.data` as at
load. `OUT.relocs` beside it holds the exact relocation sites; without them the recompiler misses
functions reached only through a pointer. Against the image of a capture on Windows
([harvest](../harvest/README.md)):

| In the game image | Against a capture's |
|---|---|
| `.text`, `.rdata`, `.rsrc`, the import tables | identical |
| `.data` | identical where the unpacked image stores it; past that the capture has 3,889 non-zero bytes of runtime state, and the game runs without them |
| the wrapper table | identical |
| `.UBX0/1/2` | a few thousand bytes differ; VMProtect's own sections, not translated |
| translated functions | the same but for one, leaving out the overrides in `skip.txt` |

The relocation sites come from comparing the output with a copy of the game loaded elsewhere
(`make_seed.py --loaded`); VMProtect's own table stays packed. For this build that copy was a capture.

`make_seed.py` still reads a Qiling snapshot of an older build; that route never worked on the depot build.

## Making a seed: the QEMU capture

A full-system QEMU guest runs real Windows with real DLLs, and VMProtect's bootstrap runs through to the
game in it, with the GDB stub's watchpoint outside the guest's sight. This is a one-time developer step
per exe build, on the Mac, about two hours, most of it the Windows install.

1. **Create the VM**: `./qemu_vm.sh create <windows.iso>`, a 32-bit Windows 10. With the QEMU default
   CPU Windows never gets past its boot manager; the script uses `-cpu max,hypervisor=off`.
2. **Put the exe in**: copy it into the shared folder with the VM off (the folder is read at boot),
   then `./qemu_vm.sh run`.
3. **Boot**: `python3 -I qemu_capture.py <exe> capture.bin`, and stop it at the desktop.
4. **Start the game once** in the guest, from a folder with `steam_api.dll`, `bink2w32.dll` and
   `d3dx9_42.dll`, with the Visual C++ 2010 runtime installed. It stops at "Please make sure Steam is
   running". **Leave that open** and read its address:
   `powershell -noexit -c "(Get-Process Rocksmith2014).MainModule.BaseAddress"`.
5. **Capture**: `python3 -I qemu_capture.py --base <address> <exe> capture.bin`, then start the game
   again. Windows randomises the address once per boot (`MoveImages=0` does not stop it), but a second
   copy lands where the first one is. The guest stops within seconds, on VMProtect's first write to
   `.text`.
6. **Make the seed**: `python3 -I make_seed.py --from-gdb capture.bin <exe> seed.bin --loaded
   rs_harvest2.bin`. It keeps only the pages the decompression touches, blanks the text in them (the
   guest's environment, device paths), checks the result is repeatable and records the output's
   SHA-256. `--loaded` is a flat copy of the game loaded at `--run-base` (default `0xf80000`), for the
   relocation sites. A later build would need one too; the first copy left open in step 4 is such a
   copy at its own address, but nothing reads it out yet.

`capture.bin` holds the process' resident memory, Windows DLLs included. It stays on the machine it
was made on; only `seed.bin` ships.

## Builds

A seed belongs to one exe build: `make_seed.py` records the exe's SHA-256 in it and `lp_unpack.py`
refuses any other exe. A new Steam build needs a fresh capture and seed.

## What ships, and what does not

- **Shipped (in the release bundle, not this repo)**: `seed.bin` (~190 KB gzip) — VMProtect VM
  scratch + register state: nine pages, a delta of 285 bytes against the exe, of which one is
  `.text`'s first decompressed byte, and the relocation sites, which are offsets only. **No
  third-party code.** It is read with an unpickler that
  accepts plain data only, so a tampered file cannot run code.
- **From the user**: their own `Rocksmith2014.exe` (depot 221683, pinned build). We ship none of it.

`seed.bin` is kept out of this (public) source repo (`.gitignore` enforces this): it is a VM-state blob
keyed to a specific commercial game build. `lp_install.py` looks for `$LP_SEED`, then for it next to this
module; the installer app passes its own copy as `LP_SEED`. Absent, the image comes from a capture.

## Shipping the seed

A release's installer carries the seed, from wherever the maintainer keeps it. `tools/release/package.sh`
builds and zips the installer and refuses to go on without it ([docs/release](../../docs/release/README.md)):

```
RS_LP_SEED=/path/to/seed.bin tools/release/package.sh
```

The installer alone:

```
RS_LP_SEED=/path/to/seed.bin tools/installer/build.sh
ls "tools/installer/RS Re-ARMed Installer.app/Contents/Resources/seed.bin"
```

`build.sh` refuses a file that is not a seed, and a path that is not there. Without `RS_LP_SEED` it takes
`win-x86/unpack/seed.bin` if that is there, and otherwise builds an app without a seed, which falls back to
a capture and says so: check the line it prints, `with the seed: ...`.
The source archive of the tag has no seed, and needs none: the app keeps it in its bundle.

## Files

| File | Purpose |
|---|---|
| `lp_unpack.py` | Shippable unpacker. `unpack(exe, seed) -> bytes`, or run as a CLI. Deps: `unicorn` + `pefile`. |
| `make_seed.py` | Build-time seed generator (developer only). `--from-gdb` for QEMU captures, plain arg for Qiling snapshots. |
| `qemu_capture.py` | Captures VMProtect state from a QEMU guest through its GDB stub. Standard library only, no gdb. |
| `qemu_vm.sh` | QEMU VM launcher: `create`, `setup`, `run` (with GDB stub). |
| `test_lp_unpack.py` | Unit tests. Fast tests always; integration test opt-in via env vars. |
| `test_qemu_capture.py` | The capture against a fake GDB stub. |

## Running

```
pip install -r win-x86/install/requirements.txt
python3 -I lp_unpack.py Rocksmith2014.exe seed.bin output.bin
```

Or the whole image, as the installers do:

```
python3 win-x86/install/lp_install.py game-image GAME_FOLDER Rocksmith2014.exe --py VENV/bin/python
```

## Tests

```
python3 -m unittest discover -s win-x86/unpack -p 'test_*.py'
```

For the full integration test (needs the exe + a seed):

```
RS_UNPACK_EXE=Rocksmith2014.exe RS_UNPACK_SEED=seed.bin python3 -m unittest win-x86/unpack/test_lp_unpack.py
```
