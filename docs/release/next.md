# The next release: what's new

<!-- One entry for each change a player or a mod's author will notice, added in the same piece of work as the
change, in the words the release notes will use: the bold lead says what is now true, the rest says what it was
like before. Say which edition when it isn't both. tools/release/bump.py moves these entries into the release
notes and empties this file. -->

- **The installer says how a run ended in its window, and has a Close button.** The result is in the box above the
  log, with Start the Game and Show in Finder, or Try Again and Show Log File when a step didn't work; Close (or
  Escape) is at the bottom right. Before, a separate window said it, and its Close closed only that window. A newer
  release also stays as a button in the title row after the question at the start.
- **The installer checks the source it downloads.** It builds only a download that is, file for file, the source
  it was made from, and says so if it is not. Before, it checked only that the archive was whole.
- **Refresh Game Files is quick on every drive.** On a drive that keeps coarse file times (exFAT, for one), every
  refresh read every game file again; now none. The first refresh after updating still reads them once.
- **Learn & Play: long sessions no longer use up the game's address space with ended threads**, and the game can
  only open files in its own folders by a Mac path.
- **A draw that waits for a shader compile hurries it**, which should shorten a first-play stutter. Not measured.
