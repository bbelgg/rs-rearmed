# The next release: what's new

<!-- One entry for each change a player or a mod's author will notice, added in the same piece of work as the
change, in the words the release notes will use: the bold lead says what is now true, the rest says what it was
like before. Say which edition when it isn't both. tools/release/bump.py moves these entries into the release
notes and empties this file. -->

- **An install ends by saying where custom songs go.** The last lines of the installer's log, and of
  `install.sh` and `learn-play.sh`, name the game's own `dlc` folder (`~/Library/Application Support/RS
  Re-ARMed/game/dlc`, or `…/learn-play/game/dlc` for Learn & Play) as the place for custom songs: the Mac
  archives (`*_m.psarc`) for the Mac game, the PC ones (`*_p.psarc`) for Learn & Play. The game has a song put
  there at its next start, or at once with ENUMERATE, and no refresh removes it. The folder is now there after
  every install; a Learn & Play install with no DLC had none. Before, the folder was only in the install guide.
- **Calibration and the tuner are the same at any frame rate.** The game counts some things in frames, so on a
  display faster than 60 Hz (the 120 Hz screen of a MacBook Pro, say) calibration moved the input level too fast for
  its own average, its samples could run past their buffer (with two players they did from 100 frames a second), and
  the tuner gave a string that drifted less time and was quicker with its volume and wrong instrument warnings. Now
  these run as at 60 Hz whatever the display's rate; at 60 Hz nothing changes. `RS_MODS_FRAME_RATE=0` is the game's own
  behaviour. Not yet played with a guitar on a 120 Hz display: if calibration or the tuner still misbehaves there,
  please open an issue with `logs/last-run.log`.
- **Learn & Play: a mod finds the input names of two players.** `input_mode`, `input_profile_string`,
  `input_manager`, `input_by_name` and `input_binding` are in Learn & Play's table with their calls, and so are
  the two profile keys, `input_key_direct` and `input_key_mic`, pointers to the keys' text as on the Mac; before,
  `rc_addr` and `rc_call_name` gave nothing for them there.
- **The built-in fixes log the same lines in both editions.** With `RS_MODS_LOG=1`, Learn & Play's lines start
  `[rs-mods] ` as the Mac's do (they started `mods: `), and its log has the Mac's line of which fixes are on at the
  start; the Mac's log has Learn & Play's line of what a DLC mounting did (`DLC: N archives mounted in F frames, T s`).
  The switches and their defaults were the same already; they are now read by one piece of code.
- **Learn & Play: the intro skip and the song ownership during a mount are the Mac's, to a mod as well.** Each was
  a function of Learn & Play's own around the game's, so a mod's hook on `boot_intro_update` or `steam_dlc_owned` ran
  before it; they are now the same pre hooks as on the Mac, added before any mod's, and a mod's hook there runs after
  them and not at all on the calls they answer. The player sees no difference, and `RS_MODS_LOG=1` now logs
  `intro: skipped` here too.
- **Learn & Play: `input_bind` is a hook point, 19 of the 21 now.** A mod's hook on it was refused there, because
  the game's function takes one argument where the Mac's takes two. The second is 0 in every call of either game,
  so a hook now sees the Mac's call with that 0; a hook that changes it changes nothing there, the game's code
  having no place for it. The fix that finds Player 2's saved input is the Mac's pre hook on it, added before any
  mod's. The player sees no difference. Not yet played with two inputs since.
- **Two players: looking for Player 2's input no longer costs the game memory.** With Player 2's profile on Direct
  Connect or the microphone and a saved input whose name is longer than about 15 characters, each time the game
  looked for a slot for their guitar a copy of the name stayed in the game's memory, at every look while that
  input was unplugged. Both editions, since 1.8.0. Not yet played with two inputs since.
- **The Guitarcade's games can be started without tuning.** A game's tuner now has Skip Tuner, the one a song's
  tuner has, with the same warning. Before, the tuner there offered only Cancel and Calibrate, and Calibrate comes
  back to the tuner: a player whose tuner would not finish could play no game but Gone Wailin', which has no tuner.
  The games listen for pitch, so an instrument out of tune plays them badly; that is the player's choice, as in a
  song. `RS_MODS_GUITARCADE_SKIP=0` is the game's own behaviour.
