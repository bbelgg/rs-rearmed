# iPhone and iPad (experimental)

Learn & Play on iOS: the same translated game, Win32 seam, D3D9 layer, libRSOpenGL and 32-bit runtime as the Mac app
([win-x86](../win-x86/README.md)), compiled for iOS, in an app of its own that the developer signs with their own
Apple team and puts on their own device from a Mac. Nothing of it can go to the App Store: the app carries a game
image made from the user's own copy of the game.

Learn & Play comes first because its game only talks to our Win32 seam, so of the Mac app only the window
(`win-x86/app/lp_window.m`) and the audio device list are the Mac's. The Mac edition's game calls AppKit itself, so
it would need an AppKit stand-in first (not yet: [docs/mac/PLAN.md](../docs/mac/PLAN.md#next-work)).

## What's here

| File | What it is |
|---|---|
| `build_lp.sh` | the build: translation, DLLs, libRSOpenGL, seam, runtime, the app's resources, the Xcode build |
| `deploy_lp.sh` | install, the game files and the user's folder to the device (or the simulator), launch, the log back |
| `app/lp_ios_app.m` | the app: UIKit, the window scene, the Metal layer the game draws into, touch and keys to the game |
| `app/Info.plist`, `app/LearnPlay.entitlements` | landscape, full screen, the microphone, the Files app; the 4 GB guest window and more memory |
| `compat/CoreAudio/CoreAudio.h`, `compat/rs_hal_ios.m` | the Mac's audio device API on the iOS audio session, so the WASAPI faces stay one source |
| `retarget.py` | marks simulator objects as the device's (the generated C is the same for both) |
| `project.yml`, `LearnPlay.xcodeproj`, `LearnPlay.xcconfig` | the Xcode project (XcodeGen's spec and its output) |

What changed in the shared code for it, all of it compiled out or unchanged on the Mac:
`win-x86/app/lp_main.c` (the app's folders on iOS, no mods, `lp_main` run from the app's own `main`),
`win-x86/shims/audio/wa_engine.c` (RemoteIO in place of the HAL unit), `shims/opengl/src/metal` (no managed storage, no
MetalFX in the simulator), and `LP_CFLAGS` in `win-x86/build/translate.sh` and `build_modules.sh`.

## Requirements

- A Mac with Learn & Play installed by the RS Re-ARMed Installer (the game files, the image, the DLLs and the saves
  come from it) and its build's Python (`build/venv`)
- Xcode with the iOS SDK, signed in to an Apple account (Xcode > Settings > Accounts)
- An iPhone or iPad with Developer Mode on (Settings > Privacy & Security > Developer Mode), about 7 GB free
  (5.5 GB without `dlc/`)
- A paid Apple Developer account for the two entitlements (`extended-virtual-addressing`, `increased-memory-limit`);
  a free account's provisioning refuses them, and the app's builds last 7 days

## Build and install

```sh
ios/build_lp.sh device               # 30-40 minutes the first time: the translation
ios/deploy_lp.sh device install files
ios/deploy_lp.sh device launch       # or tap Learn & Play on the device
```

With the simulator built already, the device build can take its libraries instead of compiling them again:

```sh
ios/build_lp.sh device from-simulator gl shims runtime bundle app
```

The simulator (`ios/build_lp.sh simulator`, `ios/deploy_lp.sh simulator install files launch`) gets a link to the
Mac's copy of the game files instead of a copy.

## Where things are

The app's Documents folder, which the Files app (On My iPhone > Learn & Play) and the Finder (the device's Files) show,
is what `~/Library/Application Support/RS Re-ARMed/learn-play` is on the Mac: `game/`, `steam/`, `userdata/`, `logs/`,
`settings.env`, `options.env`. `ios/deploy_lp.sh device log` copies `logs/last-run.log` back to
`build/ios/last-run-device.log`.

## Playing

- One finger is the mouse: a touch moves the pointer there and holds the left button until it lifts.
- A two-finger tap is Escape (back).
- A keyboard works as on the Mac.
- Audio: the session's route. A class-compliant USB interface or the Real Tone Cable plugged in is the input, with one
  entry per channel as on the Mac; without one, the built-in microphone. The output is the speaker, headphones or the
  interface.

## Status

See [docs/windows/PLAN.md](../docs/windows/PLAN.md) (iPhone and iPad). Open: a touch layout of its own (the game's
menus by touch, the note highway's controls), audio interfaces tested (an HX Stomp, the cable), mods (iOS loads no
library from outside the app: they would have to be built into it), a controller.
