# The next release: what's new

<!-- One entry for each change a player or a mod's author will notice, added in the same piece of work as the
change, in the words the release notes will use: the bold lead says what is now true, the rest says what it was
like before. Say which edition when it isn't both. tools/release/bump.py moves these entries into the release
notes and empties this file. -->

- **The installer has a Game files choice for a game on another drive.** On the game's drive keeps RS Re-ARMed's
  copy there with no extra space, as before; On this Mac makes a full copy in your home folder, so the game plays
  without that drive and macOS doesn't ask whether it may use it. Choosing the other one and clicking Build &
  Install or Refresh Game Files moves the copy. Before, only `RS_GAME_COPY_DIR=home` set with `launchctl` did
  this, and only for a new install. Both editions.
