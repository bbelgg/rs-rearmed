# rs-tonehost for Helix Native

What makes Helix Native work with [rs-tonehost](../README.md), and what was learned about its files, none of it documented by
Line 6. Run [`setup.sh`](setup.sh) with the game closed; run it again after a Helix update or a change to `gear.json`.

| File | What it does |
|---|---|
| `gear.json` | The game's gear onto Helix models. **Edit this one** |
| `make_presets.py` | Writes `setlist.hls`, one preset per Helix amp, and `tones.json`, the rules from game knobs to parameters |
| `base.hlx` | An empty preset, the frame of Helix's file |
| `capture_states.m` | Selects each imported preset by MIDI and saves its state |
| `check_states.m` | Tells whether a state has the chain just made: toggles the last block and listens |
| `make_config.py` | Writes `config.json`, keeping the player's settings |
| `setup.sh` | All of it, with one import by hand |

## Correcting the gear

An amp that sounds wrong: change its line in `gear.json`, `"Amp_GB38": "AmpBritJ45Nrm"`, to another Helix amp. Helix's amps are
in `HX_ModelCatalog.json` in `/Library/Application Support/Line 6/Helix Native/res`; drop `HD2_` from the id. Then run `setup.sh`:
a Helix amp not used so far needs a new preset, so it takes the import again. Moving a game amp to a Helix amp already in use
needs only `make_presets.py` and `make_config.py`, as the states are kept by Helix amp. The names of disguised amps are a best
guess, and the cab is always Helix's default for the amp, whatever the song has.

`high_gain` lists Helix amps that get less input and drive for the same knob, and a tighter gate.

## The chain

Every preset is the same chain, so knobs and switches mean the same everywhere. The numbers are Audio Unit parameters.

| Path | Block | Model | Switch | Knobs |
|---|---|---|---|---|
| 1 | Octave up | Simple Pitch, +12, mix 50% | 16 | |
| 1 | Octave down | Simple Pitch, -12, mix 50% | 17 | |
| 1 | Wah | Auto Filter: there is no pedal to rock | 18 | |
| 1 | Fuzz | Triangle Fuzz | 19 | 8 Sustain |
| 1 | Metal | Swedish Chainsaw | 20 | 14 Drive |
| 1 | Drive | Minotaur | 21 | 5 Gain, 6 Tone |
| 1 | Amp and cab | from `gear.json`, Helix's default cab | | 0 Drive, 1 Bass, 2 Mid, 3 Treble, 4 Presence |
| 1 | Compressor | Deluxe Comp, 4:1 | 22 | 7 Threshold |
| 2 | Chorus | Chorus | 23 | 10 Mix |
| 2 | Flanger | Gray Flanger | 24 | 9 Rate |
| 2 | Phaser | Script Mod Phase | 25 | |
| 2 | Tremolo | Optical Trem | 26 | |
| 2 | Delay | Simple Delay | 27 | 11 Time, 12 Feedback, 13 Mix |
| 2 | Reverb | Plate | 28 | 15 Mix |

All sixteen knobs are in use, so a new one has to replace another. Parameters 32 and 33 are Master Input and Output Level, in dB;
32 is how hard a tone hits the amp. It follows a rule from fits on a few tones, not a measurement of each amp. With a drive pedal
it rises faster; with fuzz or metal it doesn't, as those distort plenty themselves. That hasn't been compared with the game.

A pedal stands ahead of the amp even where the game has it after the amp, as a `PostPedal`. A knob set together with its switch
takes effect: rendered both ways, it makes no difference.

Left out on purpose: the game's EQs. Not mapped: the acoustic emulator, Lo-Fi Filter, its noise gate, pitch shifters.

## Helix's files

- **Models.** The `.models` files in `res` are JSON: every model's parameters with `min`, `max` and `default`, and `ircablink`,
  the amp's default cab. A block with only some parameters isn't trusted: `make_presets.py` writes them all from the defaults.
- **`.hlx`** is JSON, and a preset for the plug-in has `device` 2162944, which is Helix compatibility mode. Other devices ask the
  user to translate.
- **`.hls`** is a setlist: JSON around `encoded_data`, base64 of zlib of `{"meta": ..., "presets": [...]}`, with `crc32` and
  `decompressed_size`. It takes 128 presets, and imports in one go over the setlist in view.
- **Automation.** `tone.automation["@assignments"]` is a list by position: 0-15 are Knob 1-16 and 16-31 Switch 1-16, unused ones
  `null`. An entry is `{"@param": "dsp0.block2.Drive", "@min": 0, "@max": 1}`, and for a switch `"@param": "dsp0.block2.@enabled"`
  with `@min` false and `@max` true. `@min` and `@max` set the Audio Unit parameter's range in the block's units. One entry
  per knob. An enum such as the compressor's Ratio is its index, 0-5.
- **Audio Unit parameters** 0-15 are the knobs, 16-31 the switches, 32 Master Input, 33 Master Output, 34 Master Bypass, 36 Snapshot
  Index. A knob's value from a state isn't reported: set every knob you rely on after loading one.
- **Paths.** A path holds eight blocks; an amp and its cab take one. `dsp0.outputA["@output"] = 2` sends path 1 to path 2A. In
  the editor brackets round a block's name mean it has a bypass control; a dim block is off.
- **Snapshots** list `blocks` per `dsp`, which set what is on: keep them in step with `@enabled`.
- **Preset recall by MIDI** works in the Audio Unit only, not VST3: CC 32 picks the setlist, 6 being USER 5, then Program Change.
  A change takes up to half a second and dips, so never on the instance being heard.
- **State.** `kAudioUnitProperty_ClassInfo` has `Line6PresetData`, about 90 KB, and the library's `#PRESET` files are scrambled.
  They stay unread: states are saved and restored whole, and a state loads in about 34 ms. They come from one Helix version,
  so run `setup.sh` again after an update.
- **CPU.** A bypassed block costs the same as an active one: about 2.7% of a core for an amp alone, about 7% for the chain.
- **Network.** After authorization Helix runs without it. Its editor window tries a server and carries on when refused.

Line 6's files are read from the user's install and aren't kept here. This page is notes on formats, with no data of theirs.
