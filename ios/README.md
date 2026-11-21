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
`win-x86/shims/audio/wa_engine.c` (RemoteIO in place of the HAL unit), `shims/coreaudio/rs_audio_input.c`
(`guitar-input.ini` in the app's folder), `shims/opengl/src/metal` (no managed storage, no
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

The touch screen alone plays the game; no keyboard or mouse is needed.

| Touch | Does |
|---|---|
| One finger | the mouse: a touch moves the pointer there and holds the left button until it lifts. The game's menus, lists, sliders and the hints at the bottom of a screen ("ESC Back") take a tap |
| Two fingers up or down | the mouse wheel: a list moves a row every few millimetres, fingers up bringing up what is below |
| Two-finger tap | Escape: back, and in a song the pause menu |
| The key bar | the keys the game's screens name, in two columns at the right edge: KEYBOARD, ENTER, ESC, SPACE, DELETE (Skip Tuner), CTRL (Path/Input), SHIFT (the song search), MORE; the arrows, TAB and ALT. A button is the key held while it is touched |
| Three-finger tap | shows or hides the key bar until the next screen |
| KEYBOARD | iOS's keyboard, for the song search (SHIFT first) and a profile's name; its Return is ENTER and puts it away |
| MORE | the app's menu: the song folder |

The key bar fades after 5 seconds without a touch on any screen and comes back at the next one (which goes to the
game too). It is up in the menus and goes away at once while a song, Session Mode or a Guitarcade game is played, so the
picture is the game's alone; the pause menu (a two-finger tap) brings it back. A keyboard works as on the Mac.

From the title to a song: ENTER (title), ENTER (profile), LEARN A SONG, the song, Play Song, then DELETE (Skip Tuner)
and ENTER on "Yes, continue without tuning"; in the song a two-finger tap pauses, and EXIT leaves it.

### Audio

The session's route, at 48 kHz with a 128-frame I/O buffer (2.7 ms). Plug the interface or the cable in
before starting the game: the game lists its inputs once, at its start (a device plugged in later takes the game's
sound, not its guitar input, until the game is started again).
- A class-compliant USB interface is the input and the output, as one device, with an entry per channel as on the
  Mac: an HX Stomp is listed as "HX Stomp channel 1" to "channel 8", and its DI is channel 5. As the game lists ten
  inputs and the touch layout can't yet pick every entry, the channel can be set instead in `guitar-input.ini` in the
  app's folder (the Files app), as on the Mac ([Guitar input](../docs/windows/INSTALL.md#guitar-input)); the interface
  is then one entry:

  ```ini
  [HX Stomp]
  Channel=5
  ```

  Measured on an iPhone 15 Plus: 1.5 ms of input and 1.0 ms of output latency from iOS on top of the buffers.
- The Real Tone Cable is "Rocksmith USB Guitar Adapter", the name the game knows; the sound then comes from the
  speaker (iOS adds 15.6 ms there) or from headphones.
- Without either, the built-in microphone and the speaker.
- The sound comes from the interface when one is plugged in: iOS routes a USB interface's input and output together
  and ignores the speaker override while it is there, so the game's sound can't be on the phone's speaker with the
  guitar on the interface. Use the interface's own outputs or headphones.
- If the game doesn't list the interface, unplug it and plug it in again (or start the game again): `RS_AUDIO_LOG=1`
  in `options.env` logs the route iOS gives (`[rs-audio] ios:` lines, with every input it offers).

### Picture and heat

The game fills the screen at the screen's own shape, clear of the Dynamic Island or the notch (thin black margins at
both sides, the safe area iOS gives): its desktop is that area 720 pixels high (1362x720 on an iPhone 15 Plus), and the app asks the game for that resolution at its start, as the Mac app does for a resized window;
the game keeps it in Rocksmith.ini. `RS_METAL_RETINA=1` makes it the screen's own pixels (2796x1290 there: more heat),
`RS_METAL_SCALE=N` fewer.

A phone's GPU gets less to do than a Mac's by default: no MSAA (`RS_METAL_MSAA=1`), no MetalFX upscaling
(`RS_METAL_UPSCALE=0`), the game's own texture filtering (`RS_METAL_ANISO=1`). `options.env` in the app's folder sets
them otherwise, as on the Mac ([the switches](../docs/windows/INSTALL.md#settings-and-switches)); `RS_METAL_SCALE=N`
(50-100) renders fewer pixels still.

## Status

See [docs/windows/PLAN.md](../docs/windows/PLAN.md) (iPhone and iPad). Open: mods (iOS loads no
library from outside the app: they would have to be built into it), a controller.
