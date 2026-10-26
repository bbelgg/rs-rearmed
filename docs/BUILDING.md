# Building RS Re-ARMed

You build RS Re-ARMed yourself, on your Mac, from your own Steam copy of Rocksmith 2014. The build reads
the game from your install, recompiles it to C and compiles that into an arm64 app. Nothing it produces
may be shared.

## Installer (recommended)

Download `RS.Re-ARMed.Installer.app.zip` from the
[Releases](https://github.com/bbelgg/rs-rearmed/releases) page, unzip it and open the app. It downloads
the source of its own release, checks prerequisites, builds everything and installs a self-contained
`~/Applications/RS Re-ARMed.app` — delete it to uninstall. A progress bar and live log keep you informed;
if anything fails the log tells you what went wrong.

The build files go in `~/Library/Caches/RS Re-ARMed Build` unless you choose another **Build folder**
(for example on an external drive); the installer makes an `RS Re-ARMed Build` folder inside the one you
pick and remembers it. Tick **Delete build files after install** to remove them when it's done. If you
choose a folder on an external drive, macOS asks once whether the installer may use it.

The installer needs the same prerequisites as a manual build (see below). It will ask you to launch
Rocksmith 2014 through Steam once, to decrypt the game binary.

## Manual build

All commands below are run from the repository root.

### Prerequisites

- An Apple Silicon Mac with macOS 11 or later.
- Rocksmith 2014 (Remastered) for macOS, installed through Steam in the default library
  (`~/Library/Application Support/Steam/steamapps/common/Rocksmith2014`). For another location, set
  `RS_GAME_DIR` to the game folder.
- Xcode command line tools: `xcode-select --install`.
- CMake and Ninja from [Homebrew](https://brew.sh): `brew install cmake ninja`.
- `python3` (the one from the command line tools is enough).
- An internet connection while building: the Bink shim downloads the FFmpeg 7.1.2 source from
  ffmpeg.org (the checksum is verified) and builds a minimal static copy of it, and pip downloads the
  Python packages.

### 1. Clone

```bash
git clone https://github.com/bbelgg/rs-rearmed.git
cd rs-rearmed
```

### 2. Python environment

The recompiler needs Python with [Capstone](https://www.capstone-engine.org/). Make a virtual
environment inside `build/` (which git ignores) and install `requirements.txt` into it:

```bash
python3 -m venv build/venv
build/venv/bin/pip install -r requirements.txt
```

The build scripts find Python through `RC_PY`. Its default, `analysis/venv/bin/python`, is not part of
the repository, so point it at the new environment with an absolute path:

```bash
export RC_PY="$PWD/build/venv/bin/python"
```

The optional title logo (`tools/loadscreen`) reads `RS_PY` instead; set it to the same Python if you
installed the optional packages, or leave it unset and the logo script makes its own environment.

### 3. Provide the game image

The recompiler reads a DRM-free x86_64 binary of Rocksmith 2014. The Steam copy on disk is encrypted, so you decrypt it from a running game process with `tools/steamless`:

1. **Quit Rocksmith 2014** if it is running: it has to start after the next step.

2. **Dump the code:**

   ```bash
   ./tools/steamless/dump-decrypted.sh
   ```

   Then **launch Rocksmith 2014 through Steam**. The script makes your Steam copy load a small dylib
   that saves the decrypted code once the game has decrypted itself, usually within a minute. It
   writes it to `build/steamless/` and puts your Steam copy back as it was (on Ctrl-C or a timeout
   too). You can quit the game then.

3. **Reconstruct the binary:**

   ```bash
   python3 tools/steamless/patch-binary.py
   ```
   The output is
   `build/steamless/Rocksmith2014_x64_decrypted`.

4. **Point the build at it:**

   ```bash
   export RC_IMAGE="$PWD/build/steamless/Rocksmith2014_x64_decrypted"
   ```

### 4. Build the shims

```bash
cmake -S . -B build -G Ninja
cmake --build build                # the Bink (with FFmpeg), Cg and OpenGL shims, the game libraries
make -C shims/steam                # the offline Steam layer: shims/steam/build/libsteam_api.dylib
tools/ws8/netguard/build.sh        # the network guard: build/ws8/libnetguard.dylib
```

Use `build` in the repository root as the CMake build directory: the later steps look for the shims in
`build/shims/` and for the network guard in `build/ws8/`.

### 5. Build the app

```bash
export RC_IMAGE=/path/to/the/game/image     # from step 3
tools/ws7/recomp/app/build-m4.sh build/m4
```

This builds the native override libraries, recompiles the game image to C, compiles it (the slowest
part) and links `build/m4/app/Rocksmith2014.app`. At the end it starts the new app once with
`--init-only` as a check and prints `app: <path>`.

The argument is the output directory. Without it, the output goes to `$RS_BUILD_DIR/m4`.

Environment variables the build reads:

| Variable | Default | Meaning |
|---|---|---|
| `RS_BUILD_DIR` | `build` in the repository | where the app build goes when no output directory is given (the shims and network guard stay in `build/`) |
| `RC_IMAGE` | `analysis/cg/Rocksmith2014_x64_plain` | the game image to recompile (step 3) |
| `RC_PY` | `analysis/venv/bin/python` | Python with Capstone (step 2) |
| `RS_GAME_DIR` | the default Steam library | the game folder; the app's icon is copied from it |
| `SHIM_STEAM`, `SHIM_BINK`, `SHIM_CG` | the builds from step 4 | other arm64 builds of the Steam, Bink and Cg shims to bundle |
| `SHIMS_BUILD` | `build/shims/game` | where the game libraries from step 4 are |
| `RC_OPT` | `-O2` | optimisation level of the generated C |

The generated C goes to `<output>/gen` and the objects to `<output>/obj`. Both are derived from the game:
keep them on your Mac.

### 6. Install

```bash
tools/play-native/install.sh
```

It copies the app (by default `$RS_BUILD_DIR/m4/app/Rocksmith2014.app`; pass another path as the first
argument) and the network guard into `~/Library/Application Support/RS Re-ARMed/`, and creates the
launcher `~/Applications/RS Re-ARMed.app`. Quit the game first. Start **RS Re-ARMed** from
`~/Applications`.

To update later, pull, repeat steps 4 and 5, and run `install.sh` again. Your saves are kept.
`RS_NATIVE_HOME` and `RS_NATIVE_LAUNCHER` change the two install locations.

### 7. Troubleshooting

| Message | What to do |
|---|---|
| `missing .../build/shims/game/librs_game.a: build shims/game first` | Run step 4 with `build` as the build directory. |
| `warning: no arm64 libsteam_api.dylib` (or `libBink2Macx64.dylib`, `Cg`) | That shim was not built: run step 4. The app links, but won't work without it. |
| `thunk generation failed, see .../thunks.log` | Check that `RC_PY` points at the environment from step 2 and `RC_IMAGE` at the game image. |
| `warning: no RS2014.icns in ...` | The game is not in the default Steam library: set `RS_GAME_DIR`. |
| `link: undefined symbols, see .../undefined.txt` or `skip list did not converge` | The build did not complete. Open an issue with that file and the logs in the output directory. |
| FFmpeg checksum `FAILED` | The download was incomplete. Delete `build/ffmpeg-bink2/src` and build again. |
| `build libnetguard first (tools/ws8/netguard/build.sh)` | Run the last command of step 4. |
| `no native build at ...` or `... is not arm64` | Run step 5, or pass `install.sh` the path of the app it printed. |
| `quit Rocksmith 2014 first` | Quit the game, then run `install.sh` again. |
| Launcher: "The native build is not installed" | Run step 6. |
| Launcher: "libnetguard.dylib is missing" | Run the last command of step 4, then step 6. The game never starts without the network guard. |
| Launcher: "Rocksmith 2014 is not installed at ..." | Install the game through Steam, or tell the launcher where it is: `launchctl setenv RS_GAME_DIR "/path/to/Rocksmith2014"`. |
| Launcher: "Rocksmith 2014 is already running" | Quit the running game (native or Rosetta) first. |
