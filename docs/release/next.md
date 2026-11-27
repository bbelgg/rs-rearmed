# The next release: what's new

<!-- One entry for each change a player or a mod's author will notice, added in the same piece of work as the
change, in the words the release notes will use: the bold lead says what is now true, the rest says what it was
like before. Say which edition when it isn't both. tools/release/bump.py moves these entries into the release
notes and empties this file. -->

- **The game doesn't crash as it quits right after a profile is loaded:** Command-Q at the main menu soon after
  choosing a profile, and EXIT GAME (which goes back through the title and the profile first), end cleanly. Before,
  about one such quit in two ended in a crash report, because the game's own shutdown crashes on purpose when the
  profile's gear is still loading; in the game tests it never happened after a minute at the menu. This is probably
  the crash on EXIT GAME after a long tour of the menus that was reported since 1.17.1. Mac only: Learn & Play's game
  has no such check.
- **For testers: a test input in place of a guitar:** `RS_AUDIO_FAKE_INPUT` adds an input device, "RS Re-ARMed Test
  Input", whose channels play WAV files, plucked tones (`tone:MIDI[:SECONDS]`), a round of loud plucks and silence
  that calibration takes (`calibrate`) or silence, comma-separated, e.g. `RS_AUDIO_FAKE_INPUT=tone:40,silence`;
  `RS_AUDIO_FAKE_GAIN_DB` sets its level. It is listed under DIRECT CONNECT a channel an entry, as an interface, and
  the game's note detection, the tuner and calibration hear it, so a game run needs no one to play. Both editions.
  Without the switch nothing changes. Before, a run without a guitar had no input but a microphone.
- **ADV DISPLAY SETTINGS takes effect at once, with SHARPENING:** SHARPENING is among RS Re-ARMed's rows in the game's
  menu in both editions (Metal only), for the switch below, and rows change the picture as soon as they're set:
  SHARPENING, ANTI-ALIASING, TEXTURE FILTERING and COLOR in both editions, and on the Mac LOW LATENCY too (and
  PERFORMANCE OVERLAY, in a game started with it on). A row that can't apply is greyed with why: the other renderer's,
  LOW LATENCY at a fixed frame rate (DISPLAY RATE ONLY), METALFX UPSCALING with nothing to upscale (NOT UPSCALING).
  Before, the menu had twelve of the switches (Learn & Play ten), each for the next start.
- **Sharpening:** `RS_METAL_SHARPEN=N` (0-100), Sharpening in the installer and SHARPENING in the game's ADV DISPLAY
  SETTINGS, sharpens the picture on the screen after any upscaling. Before, there was none.
- **A menu doesn't stay on the white loading screen:** when the game asks for a menu's file before it has indexed its
  files at start-up, it is asked for again once the file is indexed (at the latest every 5 s, up to six times, if the
  indexing is not seen). Before, the request was refused, nothing asked again, and the screen stayed white for good; it
  happened with a community fast-load `cache.psarc` on a busy machine. Both editions.
  `RS_MODS_MENU_RETRY=0` is the game's own behaviour. Not yet run in either game.
- **Learn a Song doesn't crash on a song with more than five arrangements, and its other arrangements scroll:** the
  page shows four of the other arrangements at a time, and the arrow keys and the mouse wheel scroll the rest (Down
  on the last one shown, Up on the first), the first and last entry saying how many are out of sight, "(+3)". Before,
  the page asked its movie for a slot it hasn't and crashed when it drew. The co-op page's list of arrangements, which
  shows about nine and draws the rest invisibly below the panel, shows eight and scrolls the same way, and Enter picks
  the arrangement in focus. Page Up and Page Down do not scroll. Both editions. `RS_MODS_ARRANGEMENTS=0` is the game's
  own behaviour. Not yet run in either game.
- **For mod authors: the tables have what rearmed-mods' Seven-String Guitar needs for RS Extended Range's charts,**
  in both editions: mid points for its native charts (the `PSXR` package, the manifest's native arrangements, the
  chart after its inflate: `seven_native_psarc`, `_json`, `_sng`), for eight-string guitars and five- and six-string
  basses (`seven_gkb`, `seven_gkp`, `seven_psn3`, `seven_lt0`, `seven_pbh`, `seven_s1_bass`, `seven_s1b_bass`; the
  chord loops of `seven_string` now run over eight strings, which `seven_e8` says), for local co-op (`seven_gkr`), the
  song camera, the fret range glow, the barre markers and the tuner's camera (`seven_cam`, `seven_frg*`,
  `seven_pubfa`, `seven_pubgf`, `seven_pubbc_*`, `seven_tf2`), the extended headstock's two offsets, and three edit
  groups: `seven_line` (the section string mask's bits of strings 6 and 7), and on Learn & Play `seven_glow` and
  `seven_barre` (injected `Entity_SetScale` calls, `unsupported` on the Mac, which calls it from the points
  `*_setscale`). The section string mask's edits moved from `seven_string` to `seven_line`: a rearmed-mods that switches
  on `seven_string` alone (0.18.0 and earlier) no longer gets them, and the added strings' lines are left out as with
  its `string7_line = 0`. Before, the tables had seven-string guitars alone.
