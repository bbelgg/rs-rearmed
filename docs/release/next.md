# The next release: what's new

<!-- One entry for each change a player or a mod's author will notice, added in the same piece of work as the
change, in the words the release notes will use: the bold lead says what is now true, the rest says what it was
like before. Say which edition when it isn't both. tools/release/bump.py moves these entries into the release
notes and empties this file. -->

- **Mac: 7-string charts.** [rearmed-mods](https://github.com/glebb/rearmed-mods)' Seven-String Guitar (after
  [rs7string](https://github.com/SomeeName/rs7string)) and Plain Packages now run in the Mac edition as they do in
  Learn & Play: seven strings on the highway, in the tuner and in chords. The Mac game reads Mac song archives
  (`_m.psarc`) only, so a chart published for Windows has to be a Mac archive first.
- **Mac: mods read their settings when the game is started from the launcher.** A mod that reads its settings as it
  loads found none when the game was started the usual way, so what it switched on there stayed off.
- **For mods, in both editions:** a mid point can be `none` on a game that needs no call at that moment, so one mod
  serves both games without asking which one it is in.
- **Installer:** streamlining in how the installer and the archive tools handle the game's archives. Nothing changes
  in what gets installed.
