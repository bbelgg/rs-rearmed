# The next release: what's new

<!-- One entry for each change a player or a mod's author will notice, added in the same piece of work as the
change, in the words the release notes will use: the bold lead says what is now true, the rest says what it was
like before. Say which edition when it isn't both. tools/release/bump.py moves these entries into the release
notes and empties this file. -->

- **The installer sets the switches.** Settings and switches… replaces Built-in Mods… and offers the switches of
  the install guides' Settings and switches (all but `RS_MODS_PATH`, the Mac's `RS_GL_BACKEND` and `RS_NATIVE_HOME`,
  Learn & Play's `LP_` ones, and the diagnostics): the built-in mods plus audio, picture, songs and logging, with
  the common ones first and the rest under Advanced, which opens by itself when one of its switches is set. The
  built-in mods stay in `settings.env`; the others go to `options.env` beside it. The page starts at what the
  files have, imports the other edition's (what only this edition has stays), names every switch that
  `launchctl setenv` overrides and can clear those, and Reset to Defaults returns everything to the defaults. A
  change takes effect at the next start, with no reinstall, once the game has been installed by this version: a
  game installed by 1.11.0 to 1.16.0 takes only the seven built-in mods it knew, and the page says so. Skip Tuner
  in the Guitarcade and the frame rate fixes of 1.16.0 are among the built-in mods there.
- **A large song library loads several times faster in the Mac edition and in half the time in Learn & Play, and
  "Enumerating downloadable content..." stays until the songs are in.** At the start, and after ENUMERATE, the
  text in the menu went away while the game was still taking in the song archives: Learn a Song lacked songs and
  the menus answered late, for a few seconds with a hundred archives and for half a minute with thousands. In the
  Mac edition 2,326 archives showed the text for 24 s and the songs kept coming for 33 s more; now everything is
  in after 5 to 8 s, and the text stays for exactly that long. In Learn & Play 2,250 archives showed the text for
  42 s, and a song list opened right after it lacked a quarter of its songs; now the text and the last song end
  together after 20 s. An ENUMERATE pressed while the text is still up for the last songs mounts its archives
  right away. With `RS_MODS_LOG=1` the log's `DLC:` line says when the songs were in the lists, and a second one
  how many of the steps' file questions were asked of the step's archive alone.
- **New names for mods, in both editions.** Four mid points of the DLC mounting: `manifest_request` and
  `manifest_response` (a mounted archive's song manifest asked for, and answered), `fs_mount` (an archive joins
  the game's file system) and `fs_exists` (the game asks its file system for a file; a hook may answer in its
  place, and the built-in fix does during a mounting). With them the fields `asset_load_name` and
  `fs_exists_slot`, and `asset_loader_thread`, the asset loader thread's loop, a function both apps now replace.
  The Mac edition alone has `dlc_package` and `dlc_package_queue`.
- **Files a mod serves: a name is listed once whatever its letter case, and a seek costs less.** A name a mod
  added to a folder that differed from a real file's only by case was listed twice, and the game mounted one
  archive twice. A file the Mac game reads as a stream asked the mod for 64 KB after every seek, now for 4 KB, as
  a real file is read. In Learn & Play a mod is no longer asked about a file the game opens for writing, and its
  close no longer holds up the game's other threads. A mod's `read` that says more than it was asked for is taken
  at what was asked.
- **Moving the game files is safer.** "On this Mac" removed whatever folder the link led to once the files were
  home: now only a copy that records this install, and it moves only a finished copy. "On the game's drive" no
  longer replaces an earlier copy found there (songs put in its `dlc` folder by hand went with it): the new copy
  goes beside it, and the installer says so. A copy on the game's drive also comes home at a refresh when Steam's
  download is gone, and a move that was stopped is taken up or cleared at the next run.
- **A download stopped partway is taken up again.** A game folder Steam had not filled yet could be read as "a
  different build", which stopped both installers for good; it counts as unfinished now, and Steam is asked to go
  on. Nothing is copied from a folder Steam is still downloading into.
- **The installer says what went wrong, and its log has the run's end.** After the Mac game was looked for and
  not found in Steam (so Steam downloaded it), a later step that failed showed that search's message as its
  reason. The kept log now ends with where everything is and how to uninstall, as the window does. Close is there
  after Stop or a cancelled question too, and Learn & Play's terminal install no longer stops when the DLC
  download does (`RS_DLC_FETCH=0` skips it, as for the Mac game).
- **Smaller fixes.** `RS_FPS` takes a whole number only (`60.5` or `1e3` ran the game at 60 or at 1 frame a
  second). Control-Command-F works with Caps Lock on in the Mac edition. At 60 Hz, the short frame after a late
  one keeps its tuner checks. `RS_MODS_BUDGET_MS=0` leaves a step's file questions to the game too. A 32-bit
  function the tables edit, or one an override replaces, is never translated in place as a stack check.
