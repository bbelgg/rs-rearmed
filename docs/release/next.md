# The next release: what's new

<!-- One entry for each change a player or a mod's author will notice, added in the same piece of work as the
change, in the words the release notes will use: the bold lead says what is now true, the rest says what it was
like before. Say which edition when it isn't both. tools/release/bump.py moves these entries into the release
notes and empties this file. -->

- **ADV DISPLAY SETTINGS has Low Latency and Performance Overlay (Mac):** LOW LATENCY and PERFORMANCE OVERLAY are
  among RS Re-ARMed's rows in the game's menu (Metal only), for the two switches below; LOW LATENCY is greyed at a fixed
  frame rate (DISPLAY RATE ONLY), except while `RS_METAL_DRAWABLES` has a value it has no choice for (3), which the row
  can then take out. Before, the menu had ten of the switches, and these two were set in the installer only.
- **ADV DISPLAY SETTINGS has RS Re-ARMed's picture switches in Learn & Play too:** FRAME RATE, ANY WINDOW SHAPE,
  LOW LATENCY, METALFX UPSCALING, FULL RETINA RESOLUTION, RENDER SCALE, ANTI-ALIASING, TEXTURE FILTERING, COLOR and
  PERFORMANCE OVERLAY are rows of the game's own menu, saved for the next start as the installer saves them. Before,
  Learn & Play's were set in the installer only.
- **A performance overlay:** `RS_METAL_HUD=1`, Performance overlay in the installer, shows Apple's Metal HUD over the
  game (macOS 13 and later): its frame rate, frame times and GPU memory. Before, there was none.
- **Low-latency presentation:** `RS_METAL_DRAWABLES=2`, Low-latency presentation in the installer (under Advanced),
  queues two frames for the screen instead of three, so each is shown a refresh sooner. Before, only the Mac edition
  read it, and the installer didn't offer it.
- **Lists scroll with the trackpad and the mouse wheel.** On the Mac, neither moved a list before; on Learn & Play, a trackpad swipe moved one row an event whatever its size. Both editions now move the same rows for the same swipe.
