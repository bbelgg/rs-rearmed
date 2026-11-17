# Performance of the Mac port

Measurements of the native build, all on one Mac: an Apple M2 MacBook Air (4 performance and 4 efficiency cores, 16 GB,
macOS 27), on its own 60 Hz Retina screen or a 75 Hz 2560x1440 external display. The Windows port's:
[Windows performance](../windows/PERFORMANCE.md).

## Summary

The current figures are the [measured runs](#measured-runs) on 1.13.0's code: a song, full screen on the Mac's
own screen with MetalFX upscaling, 4x MSAA and 16x anisotropic filtering, the game's High quality, mods loaded.

| Song | Mac | |
|---|---|---|
| Frame rate | 59.9-60.0 fps | the display's rate; 74.9 fps on a 75 Hz display ([Start-up and first play](#start-up-and-first-play)). `RS_FPS=N` fixes a rate |
| CPU | 41.7-43.1% of a core, 6.97-7.18 ms a frame | game thread 26.7-27.7, Wwise 5.8, note detection 2.5-2.7 |
| Memory footprint | 1.22-1.23 GB | 0.80-0.81 GB on a 1x display without MSAA, upscaling and mods |
| GPU | about 9.7 ms of work a frame, 58% of a 60 Hz refresh | a quarter of it the upscaler |

- **Against the original under Rosetta**: about a sixth less CPU (the original 51.0% of a core and 0.93 GB), with a better
  picture and half the audio buffer ([Against Rosetta](#against-rosetta)).
- **Against Learn & Play**: Learn & Play takes 10 points of a core more, 1.7 ms a frame, and 0.29 GB more
  ([Against the Windows port](#against-the-windows-port)).
- **Metal against OpenGL**: about 5% less CPU per frame ([Renderer](#renderer)).
- **The start**: the title is up in 7.4-7.8 s ([Start-up and first play](#start-up-and-first-play)).
- The compiler switches (ThinLTO, PGO, `-O3`) change little ([Build options](#build-options)).

## Method

- **Measured runs**: `tools/perf/run.py` ([Reproducing](#reproducing), [TESTING.md](TESTING.md#measured-runs)): one script
  for both editions, on the Mac's own screen, with the settings, profile, mods and song pinned and the Mac's state
  checked first. CPU is exact (`libproc`), by thread; frame times are `RS_GL_FRAMETIMES`' events; memory is the
  footprint; GPU is `ioreg`'s utilisation.
- **Figures from before that script** ([Renderer](#renderer), [Start-up and first play](#start-up-and-first-play),
  [Learn & Play's route](#learn--plays-route)) were taken by hand, each on the setup its text gives: test installs
  driven by `tools/ws8/keyfifo`, CPU from `ps` (`ps -M` by thread), no snapshots (`RS_GL_SNAPSHOT_DIR` is a synchronous
  readback of about 70 ms each). They compare within their section and not with the measured runs.
- **Hot spots**: a Time Profiler trace (`tools/perf/run.py --trace`, `tools/perf/profile.py`). Shares within a thread are
  usable. A thread's total is not, for the audio threads: the Wwise thread wakes every 5.33 ms for a burst shorter than the
  1 ms between samples, and comes out at about half its CPU. Take a thread's CPU from a measured run.
- **Noise**: about +-1 point of a core between runs of one build. Differences under 2 points are not results.

## Measured runs

Three runs of each edition in turn after a warm-up of each (2026-11-10): full screen at 1470x956 points and 60 Hz with
MetalFX upscaling to 2940x1912 and 4x MSAA. Builds of 1.13.0's code, rearmed-mods 0.11.0 with the same `RSMods.ini` in
both, Song Stems on. `VisualQuality=2` with every effect on, `LatencyBuffer=1`, Venue Mode on, 147 and 150 archives in
`dlc`. Cherub Rock, Lead, Override to Max, a profile on DIRECT CONNECT with the Mac's microphone and no guitar; 150 s
from 45 s after the note highway came up, about 9,000 frames a run.

| Song | Mac | Learn & Play |
|---|---|---|
| Frame rate | 59.9-60.0 | 59.9-60.0 |
| Frame interval, median | 16.61-16.64 ms | 16.66-16.67 ms |
| p95 | 18.7-18.9 ms | 17.3-17.4 ms |
| p99 | 19.4-19.7 ms | 17.7-17.8 ms |
| Frames over 20 ms | 27, 38, 62 | 10, 12, 13 |
| Frames over 50 ms | 0-1 | 0-1 |
| Game frame, median | 4.22-4.33 ms | not logged |
| CPU (% of a core) | 41.7, 43.1, 43.1 | 52.0, 52.8, 53.6 |
| CPU per frame | 6.97-7.18 ms | 8.66-8.93 ms |
| game thread | 26.7-27.7 | 32.6-33.3 |
| Wwise thread | 5.8 | 7.2-7.5 |
| note detection | 2.5-2.7 | 5.8-6.3 |
| GPU | 89% | 90% |
| Footprint | 1.22-1.23 GB | 1.51-1.52 GB |

Learn & Play takes 10 points of a core more than this edition, 1.7 ms a frame, and 0.29 GB more memory; its note
detection has come down since ([its file](../windows/PERFORMANCE.md#where-the-cpu-goes)). The stems helper used no
CPU: the song's backing track was in its cache from the warm-up. Not looked at: whether the venue showed, with no notes
played.

**What the screen got** (one more run of each with the renderer's own times in the log: `P`, a frame on the screen, and
`U`, a command buffer on the GPU). This edition's 8,976 frames went on the screen one refresh apart but one, which stayed
for two; Learn & Play's 9,001 all did. So the intervals in the table are not what is seen: they are taken where the game
hands a frame to Metal, and this edition's game frame is 4 ms on most frames and 6 ms on one in five, which moves that
moment and not the refresh the frame makes. The frame waits about 3 ms from there to the screen here and 10 ms in Learn &
Play.

**The GPU is not near its limit.** Its 89% at 60 fps is the GPU slowing its clock to fill the refresh. With the frame
rate free (`RS_FPS=300`, 40 s, the Mac cool) this edition drew 102 frames a second at these settings and 107 without
MSAA, the GPU at 99.6% and the game thread at 40% of a core: about 9.7 ms of GPU work a frame, 58% of a refresh, MSAA
about 4% of it. The MacBook Air has no fan, so later runs in a row say nothing.

**Where the GPU's time goes** (`RS_METAL_PASS_TIMES`, 960 frames of the song at 60 fps; shares, since the clock is slowed):

| | of the frame | |
|---|---|---|
| MetalFX upscaling, 1470x956 to 2940x1912 | 27% | 3.7 of 14.1 ms; 0.006 ms with `RS_METAL_UPSCALE=0`, where the frame is 12.4 ms |
| The scene, 34 draws at full size with 4x MSAA | 25% | pass 6 |
| Two half-size passes (735x478), 20 and 8 draws | 24% | passes 7 and 8: full-screen effects, about three times the scene's cost for each pixel |
| The pass before the scene, 54 draws | 4% | pass 5 |
| The game's last passes onto the screen's target, the note highway among them, about 100 draws | 4% | |
| The copy to the drawable | 2% | |
| Twenty small passes | the rest | |

So half of it is the game's own scene and effects, a quarter is the upscaler, and the note highway is cheap.

## Where the CPU goes

A Time Profiler trace of 30 s of the song from 45 s in (`tools/perf/run.py --trace 30`, `tools/perf/profile.py`), one of
each edition on the setup of the [measured runs](#measured-runs). Points of a core; a thread's total from the
measured runs, shares from the trace.

| Thread | Mac | Learn & Play |
|---|---|---|
| game | 27.6 | 33.9 |
| Wwise | 5.8 (6.5 in the trace) | 7.4 (5.1) |
| note detection | 2.6 (3.1) | 6.0 (5.9) |

**Note detection** is one function here: `dsp_fft`, 86% of the thread, 2.7 points of a core, relaxed already. Learn &
Play's FFT is 0.9 points: its build has the twiddle factors in a table, where this one's FFT works them out as it goes.

**Wwise**: `expander_gain` 28% of the thread, `parametric_eq_biquad` 15%, both relaxed; then two functions beside the
Vorbis decoder, 25% together and not relaxed: about 1.6 points of a core fenced
([Opportunities](#opportunities-by-expected-payoff) names them).

**The game thread** is flat: 72% in translated game code with no function over 3.2% of the thread, the renderer 20% with
what it calls. The one copy that shows is `glBufferSubData`'s, 3.6% of the thread, 1.0 point of a core: the game's vertex
data for the frame into Metal's buffers. In Learn & Play that data is copied twice, by the game through `memcpy_s` (2.8%
of its thread) and then by `glBufferSubData` (1.2%). rearmed-mods' settings lookups (`rsm_get`, a string compare for each)
are 1.4% of the thread here and 1.9% there.

## Results

### Against Rosetta

The original game under Rosetta beside the [measured runs](#measured-runs) of both editions: the same Mac and
screen, Cherub Rock, 150 s from about 45 s into the song, one run.

| Song | Original under Rosetta | Mac | Learn & Play |
|---|---|---|---|
| Frame rate | 59.95 | 59.9-60.0 | 59.9-60.0 |
| CPU (% of a core) | 51.0 | 41.7-43.1 | 52.0-53.6 |
| CPU per frame | 8.5 ms | 6.97-7.18 ms | 8.66-8.93 ms |
| the frame's thread | 35.1 | 26.7-27.7 | 32.6-33.3 |
| note detection | 1.9 | 2.5-2.7 | 5.8-6.3 |
| Frames handed over more than 33 ms apart | 8 | 0-3 | 0-1 |
| Footprint | 0.93 GB | 1.22-1.23 GB | 1.51-1.52 GB |

So the Mac edition takes about a sixth less processor time than the original under Rosetta and Learn & Play about the
same, each while doing more for the picture and the sound. What was not the same:

- **The picture.** All three draw 1470x956 with the game's High quality and every effect on. The original draws through
  Apple's OpenGL and the screen scales the frame up; the editions draw through Metal with 4x MSAA, 16x anisotropic
  filtering and MetalFX upscaling, about a quarter of their GPU time. The original's GPU read 68% and the editions'
  89-90%; neither is the GPU's work at a fixed frame rate.
- **The audio engine.** The original ran at `LatencyBuffer=2`: at 1 it does not hold on this Mac. The editions ran at 1,
  half the output buffer: the native build runs the output device at 256 frames instead of the system's 512, and at 128
  with `LatencyBuffer=1`, without crackling.
- **Mods.** The original has none. The editions ran rearmed-mods with Song Stems.
- **The songs.** The original's copy had the game's own songs and Cherub Rock's archive; the editions had 147 and 150
  archives in `dlc`, which is part of their memory.
- **Input.** The same profile, whose INPUT is DIRECT CONNECT, a value the original has no name for. Its tuner came up and
  its note detection ran; on which input was not looked at. No guitar was played in any of the runs.
- **How it was run.** The original is the clean download's own binary, decrypted (`tools/steamless/decrypt.py`), started
  directly with the offline Steam layer on a copy of the profile and the network guard, which refused its one lookup of a
  Ubisoft host. Its keys were timed by hand, so its window starts within a few seconds of where the editions' does. Its
  frame times are the calls of `flushBuffer` (a spy library), not what the screen got.

### Renderer

Metal is the default; `RS_GL_BACKEND=gl` chooses OpenGL. Measured before 1.4.0 on the 75 Hz display at 1x, without MSAA:
a song, 45 s from the intro, warm caches, 3 alternating rounds.

| | frame rate | CPU | CPU per frame | game frame |
|---|---|---|---|---|
| OpenGL | 60 fps | 0.415-0.42 s/s | 6.9-7.0 ms | 4.37 ms |
| Metal | 75 fps (the display's rate) | 0.48-0.49 s/s | 6.6-6.7 ms | 3.96 ms |

Metal costs about 5% less CPU per frame and has a 9% shorter game frame; it uses more CPU in total only because it renders
75 frames a second where OpenGL stays at 60.

- **Multithreaded GL engine** (`RS_GL_MTENGINE=1`, OpenGL only): the frame thread 4% shorter, total CPU 2% higher. Off by
  default.
- **Long session** (Metal, six full songs in a row, 37 minutes): 74.7 fps, memory footprint 943-983 MB from the first song
  to the last, no frame over 50 ms outside song loads.

### Shader preparation

The Metal shader corpus (3880 distinct programs) compiles in about 270-390 s on a cold system cache (install-time warm-up,
8 threads), and in about 0.2 s when the system cache is warm. With that done, the first play of a song after an install
had one 0.6 s stall at Play Song, while pipelines are built for the first time, and none in the song
([Start-up and first play](#start-up-and-first-play)). The second play has none. Learn & Play's corpus is 281 programs, from a list of what
the game was seen linking: [its Shader preparation](../windows/PERFORMANCE.md#shader-preparation).

### Against the Windows port

On the same song, screen and settings Learn & Play takes 52.0-53.6% of a core where this edition takes 41.7-43.1%, and
1.5 GB where this takes 1.2 GB ([Measured runs](#measured-runs)). Where its extra time is:
[Where the CPU goes](#where-the-cpu-goes) and [Toward the Mac edition](../windows/PERFORMANCE.md#toward-the-mac-edition).

## Start-up and first play

The apps the installer built from 1.4.0's code, each started with `open` in a test home, full screen on the 75 Hz
2560x1440 display at 1x, no mods, before the renderer had MSAA and upscaling. The [measured runs](#measured-runs)
have replaced its song figures but for the frame rate on that display: both editions 74.9 fps, median 13.2-13.4 ms, p99
17.5-18.6 ms, one frame over 33 ms in six runs of 150 s. What has not been measured since is the start and the first play.

| Start | Mac | Learn & Play |
|---|---|---|
| Title up, warm shader cache (two runs each) | 7.4, 7.8 s | 6.9, 7.3 s |
| Title takes keys, Return every 1.5 s, frames 1 s apart | by 11.5-11.9 s | by 10.0-10.4 s |
| Mounting the archives | 147 archives in 0.32 s and their songs in the lists 0.15 s later, none open after it (`lsof`, one run); 2,326 archives in 5.4 to 7.7 s (five runs). Up to 1.16.0: the steps of 147 in 0.40 s and their archives open for 2.7 s more; of 2,326 in 23.9 s and 33 s more | 75 archives in 6 frames, 0.32 s (two runs); 73 in 8 frames, 0.45 s. After 1.16.0: 150 in 0.44 s and their songs in the lists 0.18 s later; 2,250, of them 2,100 the Mac edition's through a mod, in 19.2 and 19.4 s, the songs 0.6 and 0.7 s later, where the line came after 42 s with songs still to come |
| A rescan by Enumerate | 147 archives in 3 frames, 0.18 s, with nothing new to mount (one run) | 74 archives in 3 frames, 0.14 s |
| Quit from inside a song | 0.3-0.5 s | 0.3-0.5 s |

The first play after an install against the second, one run of each: the app's cache emptied and the system's Metal cache
for the bundle moved aside, then Preparing shaders, which took about 330 and 390 s. Frames over 33 ms, and the time they
took:

| | Mac, first | Mac, second |
|---|---|---|
| Start to the main menu | 25, 2.4 s, worst 0.7 s | 21, 1.8 s, worst 0.7 s |
| Song list | 6, 0.3 s | 3, 0.2 s |
| Play Song to the highway | 9, 1.0 s, worst 0.6 s | 2, 0.1 s |
| 150 s of the song | none | none |
| Title up | 7.5 s | |
| Title takes keys | by 9.6 s | |

A player's first run may differ: it depends on what is drawn first. Learn & Play's first play, with and without its
warm-up: [its Shader preparation](../windows/PERFORMANCE.md#shader-preparation).

## Learn & Play's route

The route of [Toward the Mac edition](../windows/PERFORMANCE.md#toward-the-mac-edition) run on this edition (2026-11-06,
the 75 Hz display, `RS_FULLSCREEN=0`, no mods, the same keys on the same timing, one run each), to compare the editions
there and to try two changes to 1.7.0's build. This edition took 6.1 ms of CPU a frame and 0.89 GB where Learn & Play
took 7.2 ms and 1.33 GB. The two changes, both kept, are 1.2 points of a core together, 0.15 ms a frame; each alone is
within the noise of one run.

- **Relaxed**: 12 more functions of `relaxed.list`, the note detection's FFT among them (`dsp_fft`, 2.0 points of a core
  in the profile before and 1.4 after); `___stack_chk_fail` joined `PURE_IMPORTS` for the stack guard. None sets a
  static's guard. Left out: `F_100a6dc96` (0.4 points, indirect calls) and `F_10010bbc0` (0.2, calls `vpowf` and code
  that isn't relaxed).
- **Renderer**: one `MTLRenderPassDescriptor` for all passes instead of a new one each, and lookups that take no reference
  (`rsm_get`, `rsm_map_get`, the texture, buffer, uniform and query calls), with the context and objects in
  `__unsafe_unretained` locals where the name tables keep them alive. A function that creates what it looks up keeps a
  strong local: an unretained one frees the new object at once. The Objective-C runtime fell from 4.0% of the game thread
  to 0.9%. A song draws 21 passes a frame and about 250 draws; a render encoder for each pass
  (`renderCommandEncoderWithDescriptor`, 4.3% of the thread) is Metal's own work and stays.

## Hook points

Unchanged for this edition: the hook table is built in as it always was, and a function without hooks is called as
before. The run-time table and what it costs are Learn & Play's ([Windows](../windows/PERFORMANCE.md#hook-points)).

## The build: memory and time

A first install through the app took 12 minutes, with the system's shader cache warm so that Preparing shaders took a
second; with it cold, add 4.5 to 6.5 minutes ([Shader preparation](#shader-preparation)). Memory not measured.
[Windows](../windows/PERFORMANCE.md#the-build-memory-and-time) has the numbers for its translator, which is a different
front end.

## Build options

The switches change only how the generated C is compiled; the normal build uses none of them.

| Build | Gameplay CPU | Cost |
|---|---|---|
| `RC_LTO=thin` | no measurable change | about 3.5 minutes more build time |
| `RC_LTO=thin` with `RC_PGO=use:` a played profile | about 2% less | two builds and a playing session |
| `RC_OPT=-O3` | little change | |
| `RC_OPT="-O2 -mno-outline"` | none: the build is the same | all 244 objects byte for byte the default build's |

With `RC_LTO=thin` the objects are LLVM bitcode and the final link does the optimisation, so it takes
longer; `<output>/lto-cache` makes relinks incremental. A changed switch recompiles everything in that
output directory. Profile-guided optimisation:

```bash
RC_PGO=gen tools/ws7/recomp/app/build-m4.sh build/pgo-gen        # instrumented, slower game
tools/play-native/install.sh build/pgo-gen/app/Rocksmith2014.app
# play: menus, a few songs, the modes you use; quit normally
tools/ws7/recomp/build/pgo-merge.sh build/rs.profdata build/pgo-gen/pgo
RC_PGO=use:build/rs.profdata tools/ws7/recomp/app/build-m4.sh build/pgo-use
tools/play-native/install.sh build/pgo-use/app/Rocksmith2014.app
```

Use the same `RC_OPT` and `RC_LTO` for the gen and the use build. The instrumented game writes
`<output>/pgo/rs-<pid>.profraw` in continuous mode, so a crash or the game's own fast exit keeps it. To
write it elsewhere, set the path before starting the launcher and keep `%c` in it:
`launchctl setenv LLVM_PROFILE_FILE "$HOME/Library/Application Support/RS Re-ARMed/logs/rs-%p%c.profraw"`
(`launchctl unsetenv LLVM_PROFILE_FILE` afterwards). A profile stays usable after small source changes.

**`RC_RELAXED`** (on by default): the Wwise effect kernels and the note detector's window and FFT listed in
`tools/targets/names/relaxed.list` are translated without a memory fence per access, which was about half their time; they
fence at entry and exit instead. `RC_RELAXED=0` fences them too, for tracking down a suspected memory-ordering problem.
Learn & Play's are by address, in `tools/targets/win-x86/relaxed.map`
([its Build options](../windows/PERFORMANCE.md#build-options)).

## Opportunities, by expected payoff

1. **The note detection's FFT**: `dsp_fft` is 2.7 points of a core. A native FFT in its place, or the same loop with the
   twiddle factors in a table as Learn & Play's build has, would take about 2 points off; it has to give the detector the
   same answers, which is the work.
2. **Two Wwise functions beside the Vorbis decoder**, `F_100b2c560` and `F_100b2c240`: about 1.6 points of a core fenced,
   not relaxed yet. `F_100a6dc96` (0.4) makes indirect calls.
3. **The frame's vertex data**: `glBufferSubData`'s copy, 1.0 point of a core.
4. **A clear kept back until its target is next used**: about 3 of the 21 passes are only a clear, about 0.2 points of a
   core by their share of the encoder time. It has to hold against every read of the target.
5. **The upscaler writing to the drawable itself**: spares the copy (2% of the GPU's frame, and a texture of 22 MB), at
   the price of a drawable that is not framebuffer-only, which the system may then composite. With the GPU at 58% of a
   refresh there is no need.

Tried, no gain: ThinLTO and `-O3` ([Build options](#build-options)), the multithreaded GL engine ([Renderer](#renderer)),
merging passes (none follows a pass on the same target without a clear between).

## Reproducing

```bash
tools/perf/run.py CONFIG OUT              # a warm-up and three measured runs of each edition, in turn
tools/perf/report.py OUT [--against BASE] # the table, and the change from another set of runs
tools/perf/run.py CONFIG OUT --trace 30   # a Time Profiler trace of each edition
tools/perf/profile.py OUT/mac-trace/game.trace [--under NAME]
```

`CONFIG` names test installs of your own (`tools/perf/example.json`); what a run pins and checks is in
[TESTING.md](TESTING.md#measured-runs). The GPU's time by pass: `RS_METAL_PASS_TIMES=FILE`. The
GPU's work with the frame rate free: `"RS_FPS": "300"` in `CONFIG`'s `env`, one run at a time on a cool Mac. Compare one
variable at a time, alternate the builds, and repeat each.
