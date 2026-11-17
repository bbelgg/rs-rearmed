# Known issues

<!-- The release notes' Known issues section, kept here between releases. Edit it in the same piece of work that
opens or closes an issue, in both editions' parts where it applies. Before a release, bring the "Not tested yet"
entry up to date with what this release was and was not run with. tools/release/bump.py copies everything below
into the notes as it stands. -->

**Both editions**

- No online features: Ubisoft services are not available.
- **Not run in the game yet:** two of 1.12.0's fixes, which a test can't reach: the Mac game's restart with the network
  guard when its app is started directly and not through the launcher, and the launcher forcing the game's offline
  settings when there is no `Rocksmith.ini` yet. Both editions have been run on this code started the usual way. If the
  game doesn't start after updating, `logs/last-run.log` says why; please open an issue with it.
- **Played briefly:** the fixes from the code review of 2026-11-09 that 1.14.0 added were played with a guitar for a
  few songs in each edition, not in a long session or in every mode. If the game misbehaves after updating,
  `logs/last-run.log` says why; please open an issue with it.
- **Not tried with an account that owns them:** the first Rocksmith's DLC songs show for the DLC a copy or refresh
  finds as owned, and no account that owns one was at hand. The search was run against the real Steam with other apps
  the test account owns, and both editions were played with a DLC named by hand. If songs you own in Steam are missing
  after **Refresh Game Files**, please open an issue with the installer's log (its `Rocksmith 1 DLC:` lines).
- The game lists 10 inputs at most. Two interfaces can fill that, and then the built-in microphone isn't listed. A
  `Channel` in `guitar-input.ini` makes an interface one entry, which leaves room.
- Stopped partway, calibration still counts as run, so the level saved before it comes back until you run it to the end.
- **Not played yet:** the frame rate fixes, which make calibration and the tuner run above 60 Hz as they do at 60 Hz,
  have not been played with a guitar on a display faster than 60 Hz, in either edition, and their hooks have no test
  of their own (the arithmetic has). Above 100 frames a second calibration's average covers less than the second it
  covers at 60 Hz. Settings and switches… in the installer turns them off (Calibration and the tuner the same at any
  frame rate), as does `RS_MODS_FRAME_RATE=0`.
- Not tested yet with this release: the installer replacing itself on a copy that was downloaded with a browser (tried
  on test copies; if Update Now stops, the installer is as it was and its message offers the download page), the Mac
  game downloaded by Steam when it isn't installed there (checked with a stand-in for Steam and with a download made
  earlier, not yet by a download through the installer; if it stops, install the game in Steam and run the installer
  again, and please open an issue with the installer's log), moving the game files back to the game's drive, the Game
  files choice in Learn & Play, a run of the installer app for Learn & Play, a build from scratch, a mod serving files
  in the Mac edition on this code (Learn & Play's was played with 2250 archives), the Mac edition's tuner on a
  7-string chart's low B and a seven-row chord diagram, full screen on a 16:10 or ultrawide screen in the Mac edition,
  an ultrawide screen in Learn & Play, full screen on a second display in Learn & Play, the music category (Game Mode
  off) in Learn & Play, a Real Tone cable, a Real Tone or Microphone profile with the Auto Save skip, unplugging an
  interface while the game runs, a Guitarcade game played with a guitar after Skip Tuner (the button and its warning
  were pressed in a run of each edition, and the game started), a whole install that ends with the new lines about
  custom songs, ENUMERATE with new archives to mount, a step a frame (`RS_MODS_BUDGET_MS=0`) with the new mounting, a
  large library of Learn & Play's own archives (its 2,250 were mostly the Mac edition's, through a mod), an archive
  that brings a shader's hash file, Settings and switches… since the two newest built-in mods and Learn & Play's Frame
  rate joined it, a game started with those in its files, the fixes of the code review of 2026-11-17 beyond runs of
  each edition to the note highway (not played with a guitar, and the installer app with them was not run), the two
  memory fixes over a long session (each was run for ten minutes, where the growth had shown in thirty), and in
  Multiplayer a cable with an interface, two interfaces, the swap in Learn & Play, or any two inputs since 1.17.0
  changed how Player 2's saved input is found. The stuck tone that Prevent Buggy Tones fixes is rare and hasn't been
  met in testing.
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
- This edition reads PC song archives (`*_p.psarc`), so a Mac-only custom song collection doesn't load. Official
  DLC has both kinds.
- Two crashes have been seen once each and not reproduced.
- It uses more memory than the Mac edition (1.5 GB against 1.2 GB) and more CPU (about 10 points of a core in a song).
