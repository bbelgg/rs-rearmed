# The next release: what's new

<!-- One entry for each change a player or a mod's author will notice, added in the same piece of work as the
change, in the words the release notes will use: the bold lead says what is now true, the rest says what it was
like before. Say which edition when it isn't both. tools/release/bump.py moves these entries into the release
notes and empties this file. -->

- **Calibration and the tuner are the same at any frame rate.** The game counts some things in frames, so on a
  display faster than 60 Hz (the 120 Hz screen of a MacBook Pro, say) calibration moved the input level too fast for
  its own average, its samples could run past their buffer (with two players they did from 100 frames a second), and
  the tuner gave a string that drifted less time and was quicker with its volume and wrong instrument warnings. Now
  these run as at 60 Hz whatever the display's rate; at 60 Hz nothing changes. `RS_MODS_FRAME_RATE=0` is the game's own
  behaviour. Not yet played with a guitar on a 120 Hz display: if calibration or the tuner still misbehaves there,
  please open an issue with `logs/last-run.log`.
