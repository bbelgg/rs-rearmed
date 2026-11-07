---
name: tonehost-plugin
description: Set up another Audio Unit guitar plug-in with rs-tonehost, so the game's tones play through it. Use when someone wants a plug-in other than Helix Native in place of Rocksmith's tones, wants a plug-in's presets or knobs to follow the game's tones, or asks why the host hangs, is silent, is noisy or is too loud.
---

# Adding a plug-in to rs-tonehost

`tools/tonehost` plays the guitar through an Audio Unit in place of the game's tones; rearmed-mods' `ToneHost` is the game's side.
Read `tools/tonehost/README.md` first: it has the config. The host knows no plug-in, so a new one is config and saved states.
Only `tools/tonehost/helix/` is for Helix Native. It's the example for a full mapping, not something to copy unread.

## First ask what the player wants

Don't map more than the player needs. They usually want volume and how distorted a tone is to match, and the plug-in's own
sound to stand: the reason to use one is to sound better than the game. So don't match EQ or cabs by measurement.

| Level | What it takes | Good for |
|---|---|---|
| 1. A few sounds | Save clean, crunch and heavy states by hand, list them as templates | Any plug-in, an amp sim with one sound per preset |
| 2. Knobs follow the game | Level 1, plus `follow` rules from the game's knobs to parameters | A plug-in that exposes parameters |
| 3. Gear per tone | One state per amp model, generated, plus a gear table | A plug-in with many models and a preset format you can write |

Start at level 1, play it, and go further only if the player asks.

## Steps

1. **Find it.** `auval -a` gives the three codes, such as `aufx SND1 Shed`. Only Audio Units work: not VST3-only plug-ins. Effects
   are `aufx`, or `aumf` if they take MIDI. Check it's arm64: `lipo -archs` on the binary in the `.component`.
2. **Save states.** `build/tonehost/rs-tone-editor TYPE SUBTYPE MANUFACTURER clean.plist` opens its window, and closing it saves.
   Saved state is the plug-in's own format: don't decode it. If it's scrambled, as Helix's is, that is the plug-in's business.
3. **Read its parameters** before writing rules: ids, names, ranges and current values through `kAudioUnitProperty_ParameterList`.
   Helix exposes only 16 knobs and 16 switches, assigned inside each preset. Many plug-ins expose everything.
4. **Write the config** in `RS Re-ARMed/tonehost/config.json`: `plugin`, `templates`, and `map` with `clean`, `driven` and
   `driven_from_gain`. The game's tone data is what the mod sends: to see real knob names, read the `tone:` lines in `host.log`
   and the gear manifests. A rule's value is `scale * knob + offset`. Game knobs are mostly 0-100, some in dB or ms.
5. **Levels.** Set `target_db` and leave `output_db` to the measurement: each tone is measured through the instance nobody
   hears. Tones with and without a compressor differed by up to 9 dB before that. Use `trim_db` for a template that sits wrong.
6. **Gain into the plug-in.** Input level sets how distorted it gets, and input raises noise. Give high-gain amps less, and
   drive-pedal tones more, with `with` and `without` rules and a template's own `follow`. The gate tightens with `tight`.
7. **Play it.** Only the player can judge the sound. Ask which song and tone is off and how: too dirty, too clean, wrong
   character, too dark. Check `host.log` for what was picked, then read that song's gear before guessing. "Barracuda sounds
   like an octaver" was a flanger the mapping had put on a chorus.

## What goes wrong

- **The host or anything else hangs opening audio.** A macOS microphone prompt is waiting. A build gets a new ad-hoc signature,
  so every rebuild asks again. While one prompt is unanswered, every process hangs on every device with an input, approved apps
  too, in `AudioDeviceCreateIOProcID`; output-only devices still open. Ask the player to look for it. Never kill the process
  while it waits, as that loses the question, and don't blame the buffer or restart coreaudiod first. Start the host with `open`,
  as `start.sh` does, so the prompt is the app's. After a long wait, restart the host: its audio may not have started.
- **The network guard isn't loaded.** `nohup`, `env` from an exported variable and the system `perl` drop `DYLD_INSERT_LIBRARIES`
  with no error. Check that `netguard.log` starts with `mode=block`. A plug-in's window may try the network; the guard refuses it
  and Helix carries on. Never turn the guard off to make a plug-in work without asking the maintainer.
- **Notes stop registering** below 128 frames on a device the game also reads: that needs the game build with the input change
  (after 1.8.0). A device has one buffer size, and the last to set it wins.
- **Too loud after going straight to a device.** Sound sent to a device directly skips a virtual device's volume. The host
  follows the system output's volume anyway, unless `ignore_system_volume` is set.
- **Doubled sound, dry under wet.** Check the plug-in by sending a high tone through it: a dry path passes it at full level.
  Otherwise look at the interface, whose own preset or direct monitoring feeds the same outputs.
- **Scripted game tests.** `start.sh` in the test rig kills any running game: never run it while the player is playing. No builds
  during a game run. A fake input device's id must leave bit 30 clear.

## Rules of the repository

No game content, and nothing of a plug-in maker's, in the repository: read their files from the user's install at run time, as
`helix/make_presets.py` does. Keep what is specific to one plug-in out of `tonehost.m`. Ask before any push. Every change is
for both editions: see `AGENTS.md`.
