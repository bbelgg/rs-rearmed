# The next release: what's new

<!-- One entry for each change a player or a mod's author will notice, added in the same piece of work as the
change, in the words the release notes will use: the bold lead says what is now true, the rest says what it was
like before. Say which edition when it isn't both. tools/release/bump.py moves these entries into the release
notes and empties this file. -->

- **Learn a Song's scroll bar can be dragged from anywhere on it:** before, it could be grabbed only near the top of
  the list.
- **The Mac edition no longer can crash as it quits.** While the game closed, a timer of its window could run once
  more on a part of the app that was already freed, and macOS could then say that Rocksmith2014 quit unexpectedly.
  The game had already shut down by then. Learn & Play ends another way and never had this.
- **For mod authors: Learn & Play has the hook point `si_io`, the guitar input before the game hears it.** A mod's
  hook gets each buffer of a guitar input on the device's I/O thread, as in the Mac edition, and what it writes
  there is what the game's note detection and tones get. Until now the point was the Mac edition's alone, and a mod
  that changes the guitar's sound on the way in stayed off in Learn & Play. Learn & Play now has 20 of the 21 hook
  points. The first argument only tells the inputs apart there: it is not the game's input object.
- **The tuner shows in red which peg to turn, and Learn & Play's tuner can no longer turn purple.** The game lights
  the tuning peg of the string it is listening to in red, with a glow it blurs in a way the renderer drew wrong:
  the peg stayed grey in both editions. In Learn & Play at some window and picture sizes (seen at 1102x717 and
  1104x718) purple streaks also grew from the right edge of the screen before a song, until the whole tuner was
  purple; the note highway after it was normal.
