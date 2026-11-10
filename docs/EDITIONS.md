# Which game to build from

RS Re-ARMed builds from either the Steam macOS game or Learn & Play, the edition Steam offers for Windows only. If you
own one, build that one. If you own both, this page compares them. The feature table is in the
[README](../README.md#mac-and-windows-compared).

## In short

**Build from the Mac game** unless you want Learn & Play's tone effects or have custom songs only in PC format. It comes
with far more songs, has had more testing, and uses less CPU and memory.

You can also install both: they are separate apps with separate folders. Each copies your Steam profiles in once and
then keeps its own progress, so progress in one doesn't reach the other.

| | Mac game | Learn & Play |
|---|---|---|
| Songs in the game itself | 70 | 2 |
| Official DLC you own | ✅ | ✅ |
| Custom songs | Mac format (`*_m.psarc`) | PC format (`*_p.psarc`) |
| Lessons, Guitarcade, Session Mode, venues | ✅ | ✅ the same |
| iZotope tone effects | ❌ | ✅ [see below](#tone) |
| Testing | the most | far less |
| Lower guitar latency | ✅ tested | ⚠️ present, not tested |
| Renderer | Metal, with OpenGL as a choice and fallback | Metal only |
| Mod hook points | 21 of 21 | 18 of 21; the same mods library works |
| CPU in a song at 60 fps | 42-43% of a core | 52-54% of a core ([details](windows/PERFORMANCE.md#toward-the-mac-edition)) |
| Memory | about 1.2 GB | about 1.5 GB |
| First build on an M2 | about 15 minutes | about 25 minutes |
| Game download | about 6.5 GB | about 5.5 GB |

## Same game, different contents

Learn & Play is the newer package: the files in its `cache.psarc` are dated November 2024, the Mac game's December 2022.
But the two are the same generation of the game, on the same version of the Wwise audio engine, and Learn & Play adds no
gameplay. The data for the game's flow and menus, the Guitarcade, venues and lessons is the same in both, apart from a
few lesson files, sound banks and per-platform files such as shaders.

Learn & Play's changes mostly take things out:

- **Songs.** The Mac game has 70, and Learn & Play has two: Brannan Street Blues, which the Mac game doesn't have, and the
  Rocksmith theme. Official DLC you own works in either. The song archives of the Mac game itself can't be used by
  Learn & Play.
- **What depended on those songs.** 9 of the 127 prizes, which unlock songs, and one mission, to play a song from the 80s.
- **Lessons.** A few lessons replace a voice line that had a licensed song in it with a plain one. One lesson video differs.

What it adds is small: PS4 and Xbox button art, Uplay key tips and a second cut of the credits video.

## Tone

This is the one difference worth choosing for. Learn & Play's game has four iZotope Trash effects (Multiband Distortion,
Filters, Delay and Dynamics), and its amps and pedals use the first three. The Mac game has none of them, so its gear is
built differently:

| Gear | Learn & Play | Mac game |
|---|---|---|
| 52 of the 63 amps | an extra Trash Multiband Distortion stage | no such stage, and some of the amp's other settings differ |
| 9 delays and echoes | Stereo Delay and Trash Delay | Stereo Delay |
| 5 phasers | Flanger and Trash Filters | Guitar Distortion moved by LFOs |
| The studio pitch rack | Harmonizer | Pitch Shifter |

The other 11 amps, all of them bass amps, use the same effects in both, and so do the cabinets, the mod delay and the
studio delay.

**Nobody has compared the two by ear yet**, so this says the tones are built differently, not how large the difference
is or which you would prefer. It probably isn't an improvement in the newer build: it looks like a difference between
the Windows and Mac versions of the game, though the full Windows game isn't available here to check.

The Mac edition doesn't have these effects, and none are planned for it. Their settings exist only in Learn & Play's
files, which this project can't ship, so a player with only the Mac game couldn't get them. The effects themselves are
Learn & Play's 32-bit Windows code.

## Custom songs

Each game reads only its own format, and the song files are encrypted per platform, so RS Re-ARMed can't convert them.
Official DLC has both. A custom song often exists for one platform only, so look at the format of your collection before
choosing.

## How this was compared

From clean Steam downloads of both games (Mac depot 221682, Learn & Play depot 221683): every file in every game archive,
the effects in every soundbank, and the text and effects in both programs.

The comparison would miss a fix inside the game's code that leaves no trace in its text. The two programs were built by
different compilers, so they weren't compared function by function.
