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

- **The installer forgets a finished run when you choose the other game.** After installing one edition, picking
  the other in the Game list left the first one's "installed" box, log and buttons in the window until the
  installer was restarted.

- **The Mac game doesn't have to be installed in Steam.** When it isn't, the installer has Steam download it and
  the DLC your account owns, as it does for Learn & Play: sign in to Steam with the account that owns the game,
  and the installer does the rest, with the same check for room and the same choice of a folder on another disk.
  Before, it sent you to Steam to install the game there first. A game that is installed in Steam is used as
  before. The installer's game folder field is now empty unless you chose a folder yourself.
