# The next release: what's new

<!-- One entry for each change a player or a mod's author will notice, added in the same piece of work as the
change, in the words the release notes will use: the bold lead says what is now true, the rest says what it was
like before. Say which edition when it isn't both. tools/release/bump.py moves these entries into the release
notes and empties this file. -->

- **A performance overlay:** `RS_METAL_HUD=1`, Performance overlay in the installer, shows Apple's Metal HUD over the
  game (macOS 13 and later): its frame rate, frame times and GPU memory. Before, there was none.
- **Low-latency presentation:** `RS_METAL_DRAWABLES=2`, Low-latency presentation in the installer (under Advanced),
  queues two frames for the screen instead of three, so each is shown a refresh sooner. Before, only the Mac edition
  read it, and the installer didn't offer it.
