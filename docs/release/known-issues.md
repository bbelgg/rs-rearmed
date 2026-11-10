# Known issues

<!-- The release notes' Known issues section, kept here between releases. Edit it in the same piece of work that
opens or closes an issue, in both editions' parts where it applies. Before a release, bring the "Not tested yet"
entry up to date with what this release was and was not run with. tools/release/bump.py copies everything below
into the notes as it stands. -->

**Both editions**

- No online features: Ubisoft services are not available.
- **Not run in the game yet:** two of 1.12.0's fixes, which a test can't reach: the Mac game's restart with the network
  guard when its app is started directly and not through the launcher, and the launcher forcing the game's offline
  settings when there is no `Rocksmith.ini` yet. Both games have been run on this code started the usual way. If the
  game doesn't start after updating, `logs/last-run.log` says why; please open an issue with it.
- **Played once each:** the fixes from the code review of 2026-11-09 that this release adds were run in one song of
  each game, not in a long session or in every mode. If the game misbehaves after updating, `logs/last-run.log`
  says why; please open an issue with it.
- The game lists 10 inputs at most. Two interfaces can fill that, and then the built-in microphone isn't listed. A `Channel`
  in `guitar-input.ini` makes an interface one entry, which leaves room.
- Stopped partway, calibration still counts as run, so the level saved before it comes back until you run it to the end.
- Not tested yet with this release: a run of the installer app itself (the install scripts it runs were run for both
  editions), the Mac edition's tuner on a 7-string chart's low B and a seven-row chord diagram, full screen on a 16:10 or ultrawide screen in the Mac
  edition, an ultrawide screen in Learn & Play, full screen on a second display in Learn & Play, the music category (Game
  Mode off) in Learn & Play, a Real Tone cable, a Real Tone or Microphone profile with the Auto Save skip, unplugging an
  interface while the game runs, and in Multiplayer a cable with an interface, two interfaces, or the swap in Learn & Play.
  The stuck tone that Prevent Buggy Tones fixes is rare and hasn't been met in testing.
- In Multiplayer each player chooses their input through their own profile, not on the Multiplayer screen.

**Mac**

- The first time you play a song, venue or mode with Metal, a few short stutters can show. Later plays are smooth.
- Mods that draw on the screen with OpenGL themselves show nothing with Metal; the rest of the mod works. Use a version
  that draws through the overlay API, or the OpenGL renderer.
- 7-string charts (rearmed-mods' Seven-String Guitar) need a Mac song archive (`_m.psarc`); one published for Windows
  doesn't load here, and RS Re-ARMed has no converter.

**Learn & Play**

- Metal only: there is no OpenGL renderer.
- Mods built without `rc_mod.h` are left out, with a line in the log. While a mod hooks `input_sink` or `set_app_focus`,
  input reaches the game a frame later, and a mod can't change `input_sink`'s arguments.
- The note the game hears, the tone change hook and the song list titles have been played on the Mac edition only so far.
- Shaders are prepared for what was played in testing. The Guitarcade's games and venues not reached there can stall
  briefly the first time. If you skip Preparing shaders, or after a macOS update, the first play stalls as before: run the
  installer again.
- The Windows game reads PC song archives (`*_p.psarc`), so a Mac-only custom song collection doesn't load. Official
  DLC has both kinds.
- Two crashes have been seen once each and not reproduced.
- It uses more memory than the Mac edition (1.5 GB against 1.2 GB) and more CPU (about 10 points of a core in a song).
- Rocksmith 1 DLC songs have not been played in this edition yet: the test account owns none.
