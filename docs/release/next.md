# The next release: what's new

<!-- One entry for each change a player or a mod's author will notice, added in the same piece of work as the
change, in the words the release notes will use: the bold lead says what is now true, the rest says what it was
like before. Say which edition when it isn't both. tools/release/bump.py moves these entries into the release
notes and empties this file. -->

- **RS Re-ARMed's picture switches are in the game's own menu (Mac):** OPTIONS → ADV DISPLAY SETTINGS lists ten of
  them after the game's own rows, from FRAME RATE and RENDERER to ANTI-ALIASING and COLOR, saved where the installer
  saves them, for the next start. The other renderer's rows are greyed, as are those a switch set with `launchctl
  setenv` holds (SET FOR THIS SESSION), and the longer list scrolls, with a scroll bar that can be dragged. Before,
  they could be set in the installer only.
- **Mac: mods reach the note counts as the game does** (for mod authors: `game_root_state`, `game_state_current`,
  `game_components`, `components_list`, `component_entry_size`, `component_entry_component`, `component_type`; the
  counts are the component of the running game whose type is `note_stats_type`). The earlier path
  (`song_menu_game`, `song_game_player`, `player_note_stats`) reached the counts in some runs and nothing in others,
  so rearmed-mods' Show Current Accuracy could stay empty; those names are kept. Learn & Play's counts are reached as
  before, and the new names are unsupported there.
