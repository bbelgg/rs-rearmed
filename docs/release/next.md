# The next release: what's new

<!-- One entry for each change a player or a mod's author will notice, added in the same piece of work as the
change, in the words the release notes will use: the bold lead says what is now true, the rest says what it was
like before. Say which edition when it isn't both. tools/release/bump.py moves these entries into the release
notes and empties this file. -->

- **ADV DISPLAY SETTINGS takes effect at once, with SHARPENING:** SHARPENING is among RS Re-ARMed's rows in the game's
  menu in both editions (Metal only), for the switch below, and rows change the picture as soon as they're set:
  SHARPENING, ANTI-ALIASING, TEXTURE FILTERING and COLOR in both editions, and on the Mac LOW LATENCY too (and
  PERFORMANCE OVERLAY, in a game started with it on). A row that can't apply is greyed with why: the other renderer's,
  LOW LATENCY at a fixed frame rate (DISPLAY RATE ONLY), METALFX UPSCALING with nothing to upscale (NOT UPSCALING).
  Before, the menu had twelve of the switches (Learn & Play ten), each for the next start.
- **Sharpening:** `RS_METAL_SHARPEN=N` (0-100), Sharpening in the installer and SHARPENING in the game's ADV DISPLAY
  SETTINGS, sharpens the picture on the screen after any upscaling. Before, there was none.
