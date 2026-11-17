# The next release: what's new

<!-- One entry for each change a player or a mod's author will notice, added in the same piece of work as the
change, in the words the release notes will use: the bold lead says what is now true, the rest says what it was
like before. Say which edition when it isn't both. tools/release/bump.py moves these entries into the release
notes and empties this file. -->

- **The installer sets every switch.** Settings and switches… replaces Built-in Mods… and offers all of the
  Settings and switches: the built-in mods plus audio, picture, songs and logging, with the common ones first
  and the rest under Advanced. The page starts at what the installed app has, imports the other edition's, and
  Reset to Defaults returns everything to the defaults; a change still takes effect at the next start with no reinstall.
  Skip Tuner in the Guitarcade and the frame rate fixes of 1.16.0 are among the built-in mods there.
- **A large song library loads several times faster, and "Enumerating downloadable content..." stays until the
  songs are in.** At the start, and after ENUMERATE, the text in the menu went away while the game was still
  taking in the song archives: Learn a Song lacked songs and the menus answered late, for a few seconds with a
  hundred archives and for half a minute with thousands. In the Mac edition 2,326 archives showed the text for
  24 s and the songs kept coming for 33 s more; now everything is in after 5 to 8 s, the text stays for exactly
  that long, and the main menu and the song lists can be used meanwhile. In Learn & Play 2,250 archives showed the
  text for 42 s, and a song list opened right after it lacked a quarter of its songs; now the text and the last
  song end together after 20 s.
