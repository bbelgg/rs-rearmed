# Performance of the Windows port (Learn & Play)

Machine: Apple M2 (4 performance + 4 efficiency cores), 16 GB, macOS 27, Apple clang 21. The Mac port's: [Mac performance](../mac/PERFORMANCE.md).

## Summary

A song holds the display's rate, 74.9 fps on 75 Hz, with no frame over 20 ms, at 54% of a core and 7.2 ms of CPU a frame on the route of
[Toward the Mac edition](#toward-the-mac-edition), where the Mac takes 6.1 ms. Load times are short and paced by the game itself. As on the
Mac ([Mac performance](../mac/PERFORMANCE.md#summary)), the figures predate the renderer's 4x MSAA and 16x anisotropic filtering, which
haven't been measured here.

## Method

- **Build**: `win-x86/install/lp_install.py build` (what `learn-play.sh` runs), game image from a
  harvest, the Microsoft DLLs from the redistributables; the variants below relink that build's
  translated libraries with a changed seam, or recompile the game's generated C with other flags.
- **Run**: the binary started directly with a fresh copy of a test user folder (one profile,
  `Rocksmith.ini` absent so the game writes its defaults: `Fullscreen=1`, rendered at the screen's
  size in points), the verified input script of [TESTING.md](TESTING.md#unattended-runs)
  (title, profile, Learn a Song, Brannan Street Blues, Play Song, skip the tuner) and no guitar.
  The song (2:20) is on the highway from about 100 s to 240 s after start; the statistics use the
  window 120-230 s (6,600 frames at 60 fps). No other user programs active.
- **Frame times**: `RS_GL_FRAMETIMES` (one event per Present; the Mac build's format, so the Mac
  port's tools read both), intervals between consecutive Presents.
- **CPU**: the process' cumulated CPU time (`ps`, every 2 s) over the window, in % of one core;
  per thread `LP_THREAD_CPU=10` (each thread's CPU with its name, the guest's threads named
  by their start address); memory `footprint` (phys_footprint) every 10 s; GPU the
  IOAccelerator "Device Utilization %" (`ioreg`, no root needed) every 2 s.
- **Hot spots**: `sample` 10 s at a time during the song, aggregated by self samples per
  symbol (leaves in kernel waits dropped) and by inclusive samples on the game thread. `sample`
  stops the threads to walk their stacks, about 6 ms per sample here: under it the frame rate
  falls to ~47 fps and the game thread runs all the time, so shares within a thread are usable,
  absolute numbers and frame times of sampled runs are not.
- **Hot spots of every thread**: `xcrun xctrace record --template 'Time Profiler' --attach PID --time-limit 15s`, then `xctrace export` of
  the `time-profile` table. `sample` shows the audio threads idle (the Wwise thread at 2 of 1,093 samples when it uses 10% of a core), so its
  figures for them are not usable.
- **From 2026-11-10** the runs are `tools/perf/run.py`'s ([TESTING.md](TESTING.md#measured-runs)), as the
  [Mac edition's](../mac/PERFORMANCE.md#method): the tables below were made by hand before it.
- **Noise**: the same configuration measured 69.0, 69.0, 69.5, 70.1% (as merged) and 52.4, 52.5%
  (after the first fix): about +-1 point of a core between runs. Differences under 2 points are
  not results.

## What the seam cost

The first build took 69% of a core in a song at 60 fps, 48% after two fixes in the Win32 / D3D9 seam, both found by profile (2026-11-01/02):

- **Wake only the threads waiting for an object**: every `SetEvent` woke every waiting guest thread, a fifth of a core of system time.
- **Dynamic vertex buffers upload at the draw**: the game's whole-buffer locks were each uploaded whole, 2.7 GB/s of copying.

Also measured then: native math saves 30% of the note detection thread (`LP_NATIVE_MATH=0` turns it off). User-interactive QoS on the game thread keeps the frames on a
loaded machine (with eight busy loops 45% of a core and none over 33 ms, against 72% and 5 with `LP_QOS=0`). `-O3` gives nothing measurable, and ThinLTO and PGO weren't tried: as on
the [Mac](../mac/PERFORMANCE.md#build-options), the compiler is not where the time is.

## Shader preparation

The install compiles the game's Metal shaders, as the Mac's does ([DESIGN.md](DESIGN.md#shader-corpus) has how): the build makes a
corpus of 281 programs from the user's game files, and the app's `--warm-shaders` compiles it into the system's Metal cache, in 16.5 s on
the M2 with that cache cold and in a second when it is warm. The Mac's corpus of 3880 takes 270-390 s: every pair its shaders could form
is in it, where Learn & Play's has the pairs the game was seen using. There is no safe load: it is for a fast-load `cache.psarc`
and a busy CPU, and the measurements below show no need for one.

libRSOpenGL's Metal disk cache also warms what earlier runs used on the compile pool at the next start
([the Mac edition's notes](../mac/METAL.md)). The installed app keeps
that cache in `~/Library/Caches/rs-rearmed/learn-play/metal` (`RS_METAL_CACHE_DIR` overrides it), beside
the Mac edition's `~/Library/Caches/rs-rearmed/metal`. With one shared cache each edition compiled the
other's shaders at every start, and eight runs of one dropped the other's from the manifest. A build run
from `out/` has no disk cache, as a Mac build outside an app has none.

Measured on 2026-11-05 the way the table in [Final pass](#final-pass-for-140) was, same machine, screen and song: the same keys, frames over
33 ms and the time they took, frame times over 150 s of the song, one run of each. The test app had a bundle identifier of its own, so
"no shader cache" touched nothing of the user's: the app's cache and the system's Metal cache for that identifier were deleted, and
the app was started directly rather than with `open` ([TESTING.md](TESTING.md#shaders-of-a-run)).

| | First, no warm-up | First, after the warm-up | Second |
|---|---|---|---|
| Start to the main menu | 54, 15.9 s, worst 3.1 s | 56, 4.9 s, worst 1.4 s | 55, 4.2 s, worst 1.4 s |
| Song list | 6, 2.0 s, worst 0.7 s | 5, 0.3 s | 3, 0.2 s |
| Play Song to the highway | 15, 8.7 s, worst 3.5 s | 10, 0.6 s, worst 0.15 s | 3, 0.2 s |
| 150 s of the song | 4, 1.2 s, each about 0.3 s | none | none |
| First frame | 4.4 s | 0.5 s | 0.4 s |
| Title up | 16.5 s ([Final pass](#final-pass-for-140)) | 7.8 s | 6.9, 7.3 s ([Final pass](#final-pass-for-140)) |
| Title takes keys | by 20.5 s (the same) | by 10.9 s | by 10.0-10.4 s (the same) |
| `lib_ms`: compiling libraries | 25,480 ms | 7.8 ms | 1.4 ms |
| `pipe_ms`: building pipelines | 1,040 ms | 1,064 ms | 0 |

So the first play after an install has about 1.2 s more of frames over 33 ms up to the highway than the second, against
about 22 s without the warm-up. What is left is pipelines: the corpus has libraries, and the pipelines need draw state
it doesn't have, as on the Mac; the disk cache has them from the second play on. All 98 programs of the run came out of the corpus.
The worst frame of 1.4 s, 3 s after the start, is in every run and has nothing to do with shaders. By `open` from the internal disk it
is 1.1 s, on the first and the second play alike, so starting the app directly isn't the cause; in the Final pass it was 0.7 s, and it
was not looked into further. The warm-up followed by a first play by `open` has the same picture: to the highway 10 frames over 33 ms in
0.6 s, none in the song, 7.7 ms of compiling, 1,117 ms of pipelines.

A program the corpus doesn't have costs what it did before, a stall of about 0.25 s the first time. After a macOS update the system's
cache and the disk cache are both new, and so is the first play: the warm-up has to be run again ([INSTALL.md](INSTALL.md#known-issues)).

## Memory order

Everything translated is now compiled with `-DRC_MO_TSO` (a fence with each guest access that is not on the
stack or a constant). Without it a thread would hang for good at any of the spins on memory that clang had turned
into `b .`, and stores the game publishes to other threads could reach them out of order. Measured on 2026-11-04 with the same
generated C compiled both ways, the same song (Cherub Rock, Lead, no guitar), 135-220 s after the start:

| Song | CPU (% of a core) | game thread | worker `1a823c0` | note detection `149ab10` | frames |
|---|---|---|---|---|---|
| no fences (no snapshots, two runs) | 67.1, 67.9 | 47-48 | 6.5 | 4.4 | p99 9.7-25 ms |
| fenced (no snapshots, two runs) | 74.5, 82.9 | 52-66 | 8-10 | 6-8 | p99 9.9-11 ms |
| no fences (snapshots every 2 s) | 74.3 | | | | p99 9.8 ms, none > 33 ms |
| fenced (snapshots every 2 s) | 89.5 | | | | p99 10.1 ms, none > 33 ms |

All of these ran at 120 fps, twice the rate of the runs above, on a machine with two displays in use, so the
absolute numbers and the noise are higher than in the rest of this page: compare within this table only. A run earlier
the same day, the same way, showed 60 fps at 60% of a core. These runs were not looked into again; 1.5.1 found that a hidden full-screen window ran at
about 120 fps, which may be why.

So the fences cost 10 to 15 points of a core, about 17%, spread over all threads; frames do not change. Their cost at
the display's rate was not measured: a build without them for the same song at 75 fps was started on 2026-11-04 and
stopped before it was done, so the only figure for the current build is with them ([Final pass](#final-pass-for-140)), and what
of the 19 points over the Mac build is fences is not known. The
binary grows from 97 to 106 MB. Of the guest loads the translator emits, 834,000 are fenced, 563,000 are on the
stack and 23,000 read constants (`RC_LDC`).

Tried then, not kept: native `memcpy`, `memmove` and `memset` fenced once per call, as translated they fence every 16
bytes: 81.5 and 85.7, no better than without. Nearly all those calls come through `memcpy_s`, which that try left translated; with it
they are kept ([Toward the Mac edition](#toward-the-mac-edition)).

At the display's rate (2026-11-06, the final build's generated C for the game compiled without `RC_MO_TSO`, the DLLs as they were, the route
of [Toward the Mac edition](#toward-the-mac-edition), 60 s of the song, one run): 46.5% of a core and 6.21 ms a frame against 53.8% and 7.19 ms, so about 1 ms a frame.
The game thread 29.7 against 31.8, the Wwise thread 6.7 against 8.1, the note detection 3.6 against 6.0. Frames the same. That build is only a
measurement: a thread can hang at a spin without the fences. `ldapr` and `stlr` instead of the fences don't help: 0.53 ns an access against 0.48 on a
microbenchmark, and they fault on an access across 16 bytes.

## Final pass for 1.4.0

Measured on 2026-11-04 on the apps the installer built from the code that goes into 1.4.0, each started with `open` in a
test home: Apple M2, 16 GB, macOS 27, the 75 Hz 2560x1440 external display with the built-in screen also attached. Both
editions full screen and rendering 2560x1440, the same song (Cherub Rock, bass arrangement, no guitar), the window in
front, nothing else heavy running, keys by `tools/ws8/keyfifo`, no snapshots. Frame times and CPU over 150 s of the song, 172
to 322 s after the start, about 11,200 frames a run; CPU and memory sampled every 2 s and 10 s, GPU from `ioreg`.

| Song | Mac | Learn & Play |
|---|---|---|
| Frame rate | 74.9 fps (three runs) | 74.9 fps (three runs) |
| Frame interval, median | 13.21-13.26 ms | 13.35-13.39 ms |
| p95 | 16.7-17.1 ms | 16.6-16.7 ms |
| p99 | 18.1-18.6 ms | 17.5-17.7 ms |
| Worst | 21.6, 29.2, 33.8 ms | 23.2, 23.7, 24.2 ms |
| Frames over 33 ms | 0, 0, 1 | 0, 0, 0 |
| Frames over 50 ms | 0 | 0 |
| CPU (% of a core) | 44.1, 44.3, 44.4 | 62.6, 63.2, 63.2 |
| CPU per frame | 5.9 ms | 8.4 ms |
| GPU | 56-57% | 56-57% |
| Footprint | 0.80-0.81 GB | 1.28-1.36 GB |
| RSS | 0.78 GB | 1.10-1.17 GB |

So Learn & Play holds the same frames at about 43% more CPU than the Mac edition (19 points of a core) and 0.5 GB more
memory. Learn & Play in the 1280x720 window (`RS_FULLSCREEN=0`, one run): 63.4% of a core, GPU 35%, so its CPU does not
follow the size of the picture.

| Start | Mac | Learn & Play |
|---|---|---|
| Title up, warm shader cache (two runs each) | 7.4, 7.8 s | 6.9, 7.3 s |
| Title takes keys, Return every 1.5 s, frames 1 s apart | by 11.5-11.9 s | by 10.0-10.4 s |
| Mounting the archives | 148 archives: held open for 2.2 s after the profile's Return (`lsof`, one run; this edition logs no end of it) | 75 archives in 6 frames, 0.32 s (two runs); 73 in 8 frames, 0.45 s |
| A rescan by Enumerate | not timed | 74 archives in 3 frames, 0.14 s |
| Quit from inside a song | 0.3-0.5 s | 0.3-0.5 s |

The first play after an install against the second, one run of each, with no shader cache at all: the app's cache emptied and
the system's Metal cache for the bundle moved aside for the runs and put back after. Learn & Play had nothing to prepare at
install then ([it has now](#shader-preparation)); the Mac edition's Preparing shaders ran first and took about 330 and 390 s. Frames over 33 ms, and the time they took:

| | Mac, first | Mac, second | Learn & Play, first | Learn & Play, second |
|---|---|---|---|---|
| Start to the main menu | 25, 2.4 s, worst 0.7 s | 21, 1.8 s, worst 0.7 s | 44, 14.6 s, worst 3.2 s | 48, 3.0 s, worst 0.7 s |
| Song list | 6, 0.3 s | 3, 0.2 s | 5, 1.4 s, worst 0.7 s | 3, 0.2 s |
| Play Song to the highway | 9, 1.0 s, worst 0.6 s | 2, 0.1 s | 14, 8.7 s, worst 3.6 s | 3, 0.2 s |
| 150 s of the song | none | none | 4, 1.3 s, each about 0.32 s | none |
| Title up | 7.5 s | | 16.5 s | |
| Title takes keys | by 9.6 s | | by 20.5 s | |

So with the shaders prepared at install the Mac's first play has one 0.6 s stall at Play Song and none in the song. Learn &
Play's first play was what that step saves: about 21 s more of stalls up to the highway, the title 10 s later, and four short stalls
in the song. The second play is clean on both. A player's first run may differ: it depends on what is drawn first. Learn & Play
prepares its shaders now, and [Shader preparation](#shader-preparation) has its first play with that.

Against the numbers above: 48% of a core at 60 fps (8.0 ms a frame, another song, no memory order) is now 63% at 75 fps (8.4 ms
a frame). Footprint is 1.28-1.36 GB against 1.05 GB before, at a larger picture and with the memory order. Not done: the
memory order's cost at this frame rate ([Memory order](#memory-order)).

## Measured runs for 1.13.0

2026-11-10, `tools/perf/run.py` ([TESTING.md](TESTING.md#measured-runs)): three runs of each edition in turn after a warm-up of each, an Apple M2 MacBook Air (16 GB,
macOS 27) on its own screen, full screen at 1470x956 points and 60 Hz with MetalFX upscaling and 4x MSAA. Builds of the code that 1.13.0 has (Learn & Play's from the
7-string branch before its last commits, which were for the Mac edition), rearmed-mods 0.11.0 with the same `RSMods.ini` in both, Song Stems on. `VisualQuality=2`
with every effect on, `LatencyBuffer=1`, Venue Mode on, 147 and 150 archives in `dlc`. Cherub Rock, Lead, Override to Max, a profile on DIRECT CONNECT with the Mac's
microphone and no guitar; 150 s from 45 s after the note highway came up, about 9,000 frames a run.

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

What it says is in the [Mac edition's](../mac/PERFORMANCE.md#measured-runs-for-1130): 10 points of a core and 0.29 GB over the Mac edition, with the steadier frames.
The threads are `w32 game`, `w32 thread 1a823c0` (Wwise) and `w32 thread 149ab10` (note detection).

## Hook points

What the hook table costs the game thread with no mod loaded: nothing that shows (2026-11-04). The same song and screen by
the same input script, 75 Hz display, no snapshots, 120 s of frames from the tuner into the song, three runs of each in
turn, after a run that warmed the shader cache:

| | with hook points | without |
|---|---|---|
| Mean frame | 13.362, 13.359, 13.359 ms | 13.359, 13.359, 13.359 ms |
| Median frame | 13.34-13.35 ms | 13.34-13.35 ms |
| p99 | 14.64, 14.64, 15.40 ms | 14.60, 15.21, 14.92 ms |
| Frames over 20 ms | 12, 9, 9 | 10, 9, 10 |
| Frames over 33 ms, worst | 2, 92.6 ms | 2, 92.4 ms |
| `w32 game`, the song | 38.1% of a core (37.2-38.7) | 38.0% (37.3-38.5) |
| Process, the song | 63.3% | 63.1% |

"Without" is the same sources, translator and tables translated without `--target`, so the nine hook points that are
not built-in fixes are plain functions (the three that are keep a one-line `F_` to their `N_`); both have the same 1,198,690
memory fences. The difference is inside what runs of one build differ by. The two long frames are in both, at the Return
that leaves the tuner for the song. In the unit test a point without hooks is 0.01 ns a call more than the function, with
a mod's pre and post hook 36 ns; a song calls `ak_set_rtpc_name` 9 times a frame and `camera_set_frustum` 6.4 times. One run
with the probe mod on every point has the same figures: mean 13.362 ms, `w32 game` 37.8%.

Compare like with like: a first try used the build from before the x86 memory order as "without". That build has no fences
at all, and it showed 32% against 38% of a core and no frame over 20 ms, which is the memory order and says
nothing about hook points.

## The build: memory and time

Through the app on 2026-11-04, with the seed and nothing placed first: 25 minutes in all, the build 24 of them and the game 19.

Measured on 2026-11-04 on the M2 above (16 GB), the installer's build with 8 jobs, nothing else building. The
whole build took 19.5 minutes (68 minutes of CPU), the game 15 of them.

| Step | What runs | Memory |
|---|---|---|
| Finding the game's functions | one process, about 8 minutes | 1.4 GB |
| Translating | a worker per job | 0.37 GB each (flat after the first functions; 3 GB with 8) |
| Compiling the generated C | a compiler per job | at most 0.8 GB each, 2.9 GB with 8 |

`-DRC_MO_TSO` is a compiler flag: the translator does the same work with or without it. It adds about 10% to the
binary (97 to 106 MB).

The translator used to keep every function's decoded instructions while it looked for functions, and
400,000 more in each worker's cache. The game took it to 23 GB, which on a 16 GB Mac meant swap on the startup
disk and, with two translations at once, a kill or a full disk. It now keeps what later steps read of a function
and caches 50,000 instructions (msvcr100: 1.3 GB to 0.34 GB, the same C at the same speed).

`lp_build.py` runs one job per core, as many as fit in half the memory at 600 MB a job (`default_jobs`): 6 on an 8
GB Mac, every core from 10 GB up; `--jobs N` overrides it. So an 8 GB Mac should peak at about 3 GB. That is
worked out from the numbers above, not run on an 8 GB Mac.

## Toward the Mac edition

2026-11-06, an Apple M2, the 75 Hz 2560x1440 display. One run each, `out/rs2014_lp` started directly in a test home, `RS_FULLSCREEN=0`, a warm Metal
cache, the window in front, the route of [TESTING.md](TESTING.md#unattended-runs): Cherub Rock, Lead, a DIRECT CONNECT profile on a UR22mkII, no
guitar, measured from 45 s after the tuner's skip. Started from zsh with `&`, so at nice 5, all alike. This is a heavier route than the
[Final pass](#final-pass-for-140)'s (the bass arrangement, Disconnected): 9.4 ms a frame here against 8.4 there, for builds with the same code. So
don't compare these with that table figure by figure.

| | Before | Batch 1 | Batch 2 | Final | Without fences |
|---|---|---|---|---|---|
| CPU (% of a core) | 70.2 | 57.7 | 55.6 | 53.8 | 46.5 |
| CPU per frame | 9.38 ms | 7.70 ms | 7.42 ms | 7.19 ms | 6.21 ms |
| game thread | 44.8 | 32.7 | 32.0 | 31.8 | 29.7 |
| Wwise thread (`1a823c0`) | 10.2 | 9.7 | 9.0 | 8.1 | 6.7 |
| note detection (`149ab10`) | 7.9 | 7.2 | 6.3 | 6.0 | 3.6 |
| Frame rate | 74.93 | 74.92 | 74.92 | 74.93 | 74.92 |
| median / p99 / worst (ms) | 13.35 / 14.53 / 18.9 | 13.34 / 14.54 / 18.1 | 13.34 / 14.36 / 18.5 | 13.34 / 14.44 / 18.8 | 13.34 / 14.59 / 16.5 |
| Frames over 20 ms | 0 | 0 | 0 | 0 | 0 |
| Footprint | 1.30 GB | 1.33 GB | 1.33 GB | 1.33 GB | 1.33 GB |
| Seconds measured | 150 | 90 | 90 | 90 | 60 |

A batch is one rebuild and one run, so within a batch the shares below come from profiles (Time Profiler, 15 s of the song), not from a run each.

- **Batch 1**: x87 runs in C locals, `-mno-outline`, the shared lookup cache, native `memcpy`, `memmove`, `memset`, `memcpy_s`. Nearly all on the game thread,
  a quarter of it. In the profile the x87 matrix code (`F_ff80f0`, `F_ff81e0`) went from 6.4% of that thread to 4.4%; the outlined fragments, which
  were 15%, are gone; `memcpy` was 6.2% translated and is 4.7% as the host's.
- **Batch 2**: an indirect call's lookup at its site, the reference counts as one atomic, 11 DSP functions relaxed. `rc32_icall` went from 3.7% of
  the game thread to 1.3%, the dispatcher's `run` from 2.8% to 0.9%. The relaxed functions dropped by a factor of five in the profile (`F_1ab4100`
  13.7% of the Wwise thread to 2.6%), but the thread by 0.7 points: most of what they took now sits in functions they are inlined into, or wasn't fences.
- **Final**: 6 more relaxed.
- **Since** (2026-11-08, not measured): the Mac's relaxed kernels proved here by name (`symbols.map`), with the helpers they call where the Mac
  has them inline (the FFT's butterflies, the analysis window's cosine), so 16 more relaxed, 33 in all: the Vorbis decoder's Huffman and bit
  reader, the stereo mixer, the FFT, the room reverb's second network, FutzBox's filters and the note detection's analysis
  window. Eight of the 17 by address are now named.
- **1.10.0, a short check** (2026-11-08, one run each, not this table's setup: the built-in 60 Hz Retina screen, so MetalFX upscaling and 4x MSAA on,
  DISCONNECTED with no interface, so no note detection; 60 s of Cherub Rock in a 1280x720 window): 37.1% of a core, 6.33 ms a frame at 58.6 fps,
  game thread 27.4%, Wwise 3.4%, 1.54 GB; the Mac build on the same route 5.83 ms and 1.24 GB ([Mac](../mac/PERFORMANCE.md#learn--plays-route)).
  The first run stalled 0.3-0.6 s a few times in the song, the game thread waiting on 63 shader library compiles; a second run of the same
  route had none (a test build is a new path to macOS's shader cache, and wasn't warmed). With a guitar the note detection took its usual 6%.

The binary is 122 MB, from 107: the inline lookups and `-mno-outline`. Audio buffers and the audio path were not changed; the song played with the highway and lyrics up, not checked by ear.

Against the Mac: the same route on the Mac build, [run later the same day](../mac/PERFORMANCE.md#learn--plays-route), is 6.1 ms a frame (6.2 before its 12 more relaxed
functions), so 7.2 ms is about 1.1 ms from it, 17% more, and not the 0.6 ms estimated here first. The game thread is 4.2 ms a frame here against the Mac's 4.0, the Wwise and
note detection threads 1.9 ms against 1.1. What is left, by what the profiles show:

- the Wwise and note detection threads, 1.9 ms a frame. Fences take a good part of it: 0.5 ms a frame of these two threads went without them. The note
  detection is x87 here and SSE on the Mac (`F_105f420`, `F_13f3690`), and calls into functions that can't be relaxed as they are.
- the D3D9 layer, 23% of the game thread, of which the Metal work the Mac does too is about half: the state and object lookups, the device lock.
- the memory order itself is the same on both editions, so it isn't part of the gap, but it is the largest single cost left in this one.

## Opportunities, by expected payoff

1. **The note detection and the rest of the Wwise thread**: what is left of [Toward the Mac edition](#toward-the-mac-edition)'s audio threads. Three
   functions as hot as those in `relaxed.map` set a static's guard (`0x1a35660` the Mac's `peak_limiter_follower`, `0x1a35ba0`, `0x1a08300` its
   `expander_gain`, and `0x1a01550` its `compressor_gain` too), three more call the CRT (`memset`, `floor`: the Mac's `meter_peak_rms`,
   `ak_buffer_zero_pad`, `level_shaper`), and the note detection's `F_105f420` and `F_13f3690` call out. A relaxed form that allows those, or a native note detection window, would take part of the 0.5 ms a frame the fences cost there. Medium.
2. **The D3D9 layer's bookkeeping**: `d3d9_obj_get`, `d3d9_dev_lock`, the buffer sync, about a tenth of the game thread besides Metal's own work.
   The layer has one lock and a table lookup per call, on a device only the game thread uses.
3. **Index-range scan** (D3D9 layer): the draw-time upload reads every index of an indexed draw
   from a dynamic buffer to find its vertex range: 0.7% of the game thread, so small. Trusting `MinVertexIndex` / `NumVertices`
   instead (D3D9's contract; some games lie) or caching the range per (buffer, start, count) until the next lock would save part of it.
4. **Zero-copy dynamic buffers**: back each dynamic D3D9 buffer's guest memory with the Metal
   buffer itself (`newBufferWithBytesNoCopy` on page-aligned guest memory, a ring of a few such
   regions per buffer for DISCARD, Lock handing out the next one): no copy at all, and NOOVERWRITE
   then means exactly what it means in D3D9. Removes the rest of the upload (under 1 point of a
   core now) and the guest-side duplicate memory of big dynamic buffers. Medium-large; needs a
   libRSOpenGL entry point for client-memory buffers.
5. **Memory**: 0.5 GB over the Mac build. Every D3D9 buffer keeps its guest copy for its whole
   life (`w32_galloc`); dropping it for write-only static buffers after the upload (and
   re-reading from Metal on the rare readback) would return some of that. Medium; measure the
   split first (`footprint -v`, the guest window against the Metal heaps).

Done, in [Toward the Mac edition](#toward-the-mac-edition): the x87 stack in locals, `-mno-outline`, relaxed audio kernels (by address), the indirect-call
lookup, a native `memcpy`. Not worth pursuing now: `-O3`, QoS changes (every busy thread already runs on the performance cores), `ldapr`/`stlr`.

## Reproducing

```sh
# a run of the scripted song with frame times and the per-thread CPU log
LP_GAME_DIR=<game folder> LP_USER_DIR=<test user folder> RS_STEAM_DATA_DIR=<test steam folder> \
RS_STEAM_LOG=1 RS_GL_FRAMETIMES=ft.txt LP_THREAD_CPU=10 \
LP_INPUT_SCRIPT='@callback 1101,3:24,10:24,20:24,32:24,44:24,62:75,68:24' \
  <build>/out/rs2014_lp 2> log.txt
```

`ft.txt`: lines `F <t0 ns> <duration ns> ...`, one per Present; the frame interval is the
difference of consecutive `t0`. `log.txt`: `lp: thread cpu (10 s): [name %] ... total %` every 10
seconds. Compare one variable at a time, alternate the builds, and repeat each at least twice.
