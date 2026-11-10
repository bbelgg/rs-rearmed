# Performance of the Windows port (Learn & Play)

Machine: an Apple M2 MacBook Air (4 performance + 4 efficiency cores, 16 GB, macOS 27, Apple clang 21), on its own 60 Hz
Retina screen or a 75 Hz 2560x1440 external display. The Mac port's: [Mac performance](../mac/PERFORMANCE.md).

## Summary

The current figures are the [measured runs](#measured-runs-for-1130) of 2026-11-10 on 1.13.0's code: a song, full screen on
the Mac's own screen with MetalFX upscaling, 4x MSAA and 16x anisotropic filtering, the game's High quality, mods loaded.

| Song | Learn & Play | |
|---|---|---|
| Frame rate | 59.9-60.0 fps | the display's rate; 74.9 fps on a 75 Hz display ([Toward the Mac edition](#toward-the-mac-edition)) |
| CPU | 52.0-53.6% of a core, 8.66-8.93 ms a frame | game thread 32.6-33.3, Wwise 7.2-7.5, note detection 5.8-6.3 |
| Memory footprint | 1.51-1.52 GB | 1.28-1.36 GB on 2026-11-04, on a 1x display before MSAA and upscaling, without mods |

- **Since 1.13.0** (new in 1.14.0): the note detection's spectrum loop is relaxed, one run at 50.4% of a core with that
  thread at 4.4 points ([Where the CPU goes](#where-the-cpu-goes-in-1130)).
- **Against the Mac edition**: 10 points of a core more, 1.7 ms a frame, and 0.29 GB more. On an earlier, lighter route
  the difference was 1.1 ms a frame, 17% ([Toward the Mac edition](#toward-the-mac-edition)).
- **Against the original Mac game under Rosetta**: about the same CPU (the original 51.0% of a core), with a better
  picture and half the audio buffer ([Mac](../mac/PERFORMANCE.md#against-rosetta)).
- The memory order (a fence with each guest access) costs about 1 ms a frame ([Memory order](#memory-order)).
- The title is up in about 7 s, and the shaders are prepared at install in 16.5 s ([Shader preparation](#shader-preparation)).

## Method

- **Measured runs**: `tools/perf/run.py` ([Reproducing](#reproducing), [TESTING.md](TESTING.md#measured-runs)), from
  2026-11-10, as the [Mac edition's](../mac/PERFORMANCE.md#method): one script for both editions with the settings,
  profile, mods and song pinned.
- **Earlier tables** were made by hand, each on the setup its text gives: `out/rs2014_lp` started directly in a test home
  with the input script of [TESTING.md](TESTING.md#unattended-runs), frame times from `RS_GL_FRAMETIMES` (one event per
  Present), CPU from `ps` and per thread from `LP_THREAD_CPU=10` (the guest's threads named by their start address),
  memory from `footprint`, GPU from `ioreg`. They compare within a table and not with each other.
- **Hot spots**: a Time Profiler trace (`tools/perf/run.py --trace`, `tools/perf/profile.py`). Not `sample`: it stops the
  threads for about 6 ms a sample, so the frame rate falls to about 47 fps, and it shows the audio threads idle (the Wwise
  thread at 2 of 1,093 samples when it uses 10% of a core).
- **Noise**: about +-1 point of a core between runs of one build (69.0, 69.0, 69.5, 70.1% for one configuration).
  Differences under 2 points are not results.

## Measured runs for 1.13.0

2026-11-10, three runs of each edition in turn after a warm-up of each: the setup and the table of both are in the
[Mac edition's](../mac/PERFORMANCE.md#measured-runs-for-1130). This edition's column:

| Song | Learn & Play |
|---|---|
| Frame rate | 59.9-60.0 |
| Frame interval, median / p95 / p99 | 16.66-16.67 / 17.3-17.4 / 17.7-17.8 ms |
| Frames over 20 ms / over 50 ms | 10, 12, 13 / 0-1 |
| CPU (% of a core) | 52.0, 52.8, 53.6 |
| CPU per frame | 8.66-8.93 ms |
| game thread (`w32 game`) | 32.6-33.3 |
| Wwise thread (`w32 thread 1a823c0`) | 7.2-7.5 |
| note detection (`w32 thread 149ab10`) | 5.8-6.3 |
| GPU | 90% |
| Footprint | 1.51-1.52 GB |

That is 10 points of a core and 0.29 GB over the Mac edition, with the steadier frames (p99 17.7 ms against 19.4-19.7, a
third as many over 20 ms). The original Mac game under Rosetta on the same song and screen took 51.0% of a core, with a
plainer picture and twice the audio buffer ([what differed](../mac/PERFORMANCE.md#against-rosetta)).

With the renderer's own times in the log, one more run: all 9,001 frames went on the screen one refresh apart. The GPU's
90% is its clock slowed to fill the refresh, not its limit, and the one frame of 0.37 s is a pipeline's first use. The
free frame rate and the GPU's time by pass were run in the Mac edition only; Learn & Play draws through the same renderer.

## Where the CPU goes in 1.13.0

One trace of each edition, 2026-11-10: the table and the Mac edition's side are in
[its file](../mac/PERFORMANCE.md#where-the-cpu-goes-in-1130). This edition's:

- **Note detection**, 6.0 points of a core against the Mac edition's 2.6: `0x13f3690` was 34% of the thread, 2.0 points;
  the FFT (`0x13f23e0`, `0x13f2d50`, relaxed) 0.9; `pow` through the translated CRT (`ci_pow`, the import thunk) 0.7.
- **Wwise**: `0x1a08300` (`expander_gain`) 22% of the thread, Vorbis' Huffman decode 8%.
- **The game thread**, 33.9 points: flat, 76% in translated code. The frame's vertex data is copied twice, by the game
  through `memcpy_s` from `0x176b550` (2.8% of the thread, about a point of a core) and by `glBufferSubData` (1.2%): the
  zero-copy buffers of [Opportunities](#opportunities-by-expected-payoff).
- Seven of Metal's own threads that submit the frame's command buffer take about 3.3 points between them, in both editions.

**Changed since, the same day** (new in 1.14.0):

- **`0x13f3690` is relaxed.** It copies its input with the CRT's `memcpy` and calls its `_CIpow`, which a relaxed function
  could not do in this front end. An entry of `relaxed.map` marked `+calls` now may, with the function's exit fence before
  each such call and its entry fence after it ([the recompiler](../recompiler/README.md)): 61 loads and 14 stores lost
  their fences, 7 calls got a pair. One measured run, the same setup: the thread at 4.4 points where four runs before had
  5.8 to 6.3, the game at 50.4% of a core where they had 52.0 to 53.9, the other threads as they were. No guitar was
  played in it: that the game still hears the notes right is for a person to check.
- **The stack check is done in place.** MSVC's `/GS` ends a function that has a buffer with a call of
  `__security_check_cookie` (`0x1adb478`): 11,054 call sites in the game and 697 in its DLLs, each a full call that
  spilled and filled every register. The translator now emits the compare where the call was and makes the call only when
  the cookie is wrong. In the profile the function was 0.46 points of a core on the game thread and is gone. The measured
  runs do not show it: five runs of that build gave 49.3 to 53.1% of a core where three before gave 49.8 to 51.0, with
  another job running on the Mac between them.

**Tried, no gain**: eight more functions relaxed and taken out again (`0x1a08300`, `0x1a35ba0`, `0x1a35660`, `0x1a1ff00`
and `0x1aab890` on the Wwise thread; `0xff80f0`, `0x1560ac0` and `0x1666110`, small matrix functions of the game thread).
Two runs: 49.8 and 51.0% of a core and both threads where they were. A fence costs about half a nanosecond, so it counts
in a loop of little else over thousands of samples, as `0x13f3690` was, and not beside x87 arithmetic for each sample.

**Not to be relaxed**, though nothing in the instructions says so: `0x106b590` and `0x106b6d0`, the write and read of the
ring that carries the guitar's samples from the audio thread to the note detection, and `0x105f420`, the note detection's
main routine (0.7 points, the largest fenced function left there), which writes the flags and results the game thread
reads.

## Shader preparation

The install compiles the game's Metal shaders, as the Mac's does ([DESIGN.md](DESIGN.md#shader-corpus) has how): the build
makes a corpus of 281 programs from the user's game files, and the app's `--warm-shaders` compiles it into the system's
Metal cache, in 16.5 s on the M2 with that cache cold and in a second when it is warm. The Mac's corpus of 3880 takes
270-390 s: every pair its shaders could form is in it, where Learn & Play's has the pairs the game was seen using.

libRSOpenGL's Metal disk cache also warms what earlier runs used at the next start
([the Mac edition's notes](../mac/METAL.md)). The installed app keeps it in
`~/Library/Caches/rs-rearmed/learn-play/metal` (`RS_METAL_CACHE_DIR` overrides it), beside the Mac edition's
`~/Library/Caches/rs-rearmed/metal`: with one shared cache each edition compiled the other's shaders at every start. A
build run from `out/` has no disk cache, as a Mac build outside an app has none.

2026-11-05, the setup of the [Final pass](#final-pass-for-140), one run of each, the app started directly with a bundle
identifier of its own ([TESTING.md](TESTING.md#shaders-of-a-run)). Frames over 33 ms and the time they took:

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
about 22 s without the warm-up. What is left is pipelines: the corpus has libraries, and the pipelines need draw state it
doesn't have, as on the Mac; the disk cache has them from the second play on. The worst frame of 1.4 s, 3 s after the
start, is in every run and has nothing to do with shaders; it was not looked into further.

A program the corpus doesn't have costs a stall of about 0.25 s the first time. After a macOS update the system's cache
and the disk cache are both new, and so is the first play: the warm-up has to be run again
([INSTALL.md](INSTALL.md#known-issues)).

## Memory order

Everything translated is compiled with `-DRC_MO_TSO` (a fence with each guest access that is not on the stack or a
constant). Without it a thread would hang for good at any of the spins on memory that clang had turned into `b .`, and
stores the game publishes to other threads could reach them out of order. Of the guest loads the translator emits, 834,000
are fenced, 563,000 are on the stack and 23,000 read constants (`RC_LDC`).

What it costs at the display's rate (2026-11-06, the game's generated C compiled without `RC_MO_TSO`, the DLLs as they
were, the route of [Toward the Mac edition](#toward-the-mac-edition), 60 s of the song, one run): 46.5% of a core and
6.21 ms a frame against 53.8% and 7.19 ms, so about 1 ms a frame. The game thread 29.7 against 31.8, the Wwise thread 6.7
against 8.1, the note detection 3.6 against 6.0. Frames the same. That build is only a measurement. Earlier (2026-11-04, at
120 fps): 10 to 15 points of a core.

Tried, no gain: `ldapr` and `stlr` instead of the fences (0.53 ns an access against 0.48 on a microbenchmark, and they
fault on an access across 16 bytes); native `memcpy`, `memmove` and `memset` fenced once a call while `memcpy_s` stayed
translated (with it they are kept, [Toward the Mac edition](#toward-the-mac-edition)).

## What the seam cost

The first build took 69% of a core in a song at 60 fps, 48% after two fixes in the Win32 / D3D9 seam, both found by
profile (2026-11-01/02):

- **Wake only the threads waiting for an object**: every `SetEvent` woke every waiting guest thread, a fifth of a core of
  system time.
- **Dynamic vertex buffers upload at the draw**: the game's whole-buffer locks were each uploaded whole, 2.7 GB/s of
  copying.

## Final pass for 1.4.0

2026-11-04, the apps the installer built from 1.4.0's code, on the 75 Hz display at 2560x1440, before the memory order's
later savings and the renderer's MSAA and upscaling: the setup, the start times of both editions (the title up in 6.9 and
7.3 s, taking keys by 10.0-10.4 s, 75 archives mounted in 0.32 s, quit in 0.3-0.5 s) and the Mac edition's first play are
in the [Mac edition's](../mac/PERFORMANCE.md#final-pass-for-140).

This edition in the song, three runs: 74.9 fps, median 13.35-13.39 ms, p99 17.5-17.7 ms, worst 24.2 ms, 62.6-63.2% of a
core, 8.4 ms a frame, GPU 56-57%, footprint 1.28-1.36 GB (RSS 1.10-1.17 GB), where the Mac edition took 44.1-44.4% and
0.80-0.81 GB. In the 1280x720 window (`RS_FULLSCREEN=0`, one run): 63.4% of a core, GPU 35%, so its CPU does not follow
the size of the picture. The CPU has come down since ([Toward the Mac edition](#toward-the-mac-edition)); the first play
without shaders prepared, measured here first, is in [Shader preparation](#shader-preparation).

## Toward the Mac edition

2026-11-06, the 75 Hz 2560x1440 display. One run each, `out/rs2014_lp` started directly in a test home, `RS_FULLSCREEN=0`,
a warm Metal cache, no mods, the window in front, the route of [TESTING.md](TESTING.md#unattended-runs): Cherub Rock, Lead,
a DIRECT CONNECT profile on a UR22mkII, no guitar, measured from 45 s after the tuner's skip, at nice 5. This is a heavier
route than the [Final pass](#final-pass-for-140)'s (the bass arrangement, Disconnected): 9.4 ms a frame here against 8.4
there for the same code. It predates the renderer's MSAA and upscaling; on today's settings the figures are the
[measured runs](#measured-runs-for-1130)'.

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

- **Batch 1**: x87 runs in C locals, `-mno-outline`, the shared lookup cache, native `memcpy`, `memmove`, `memset`,
  `memcpy_s`. Nearly all on the game thread, a quarter of it. The outlined fragments, which were 15% of that thread, are
  gone.
- **Batch 2**: an indirect call's lookup at its site, the reference counts as one atomic, 11 DSP functions relaxed.
  `rc32_icall` went from 3.7% of the game thread to 1.3%, the dispatcher's `run` from 2.8% to 0.9%. The relaxed functions
  dropped by a factor of five in the profile, but the Wwise thread by 0.7 points: most of what they took now sits in
  functions they are inlined into, or wasn't fences.
- **Final**: 6 more relaxed.
- **Since** (2026-11-08, not measured on this route): the Mac's relaxed kernels proved here by name (`symbols.map`), with
  the helpers they call where the Mac has them inline, so 16 more relaxed, 33 in all: the Vorbis decoder's Huffman and bit
  reader, the stereo mixer, the FFT, the room reverb's second network, FutzBox's filters and the note detection's analysis
  window.
- **1.10.0, a short check** (2026-11-08, the built-in screen with upscaling and 4x MSAA, DISCONNECTED, a 1280x720 window,
  60 s, one run each): 6.33 ms a frame and 1.54 GB, the Mac build 5.83 ms and 1.24 GB. The
  [measured runs](#measured-runs-for-1130) have replaced it.

The binary is 122 MB, from 107: the inline lookups and `-mno-outline`.

Against the Mac: the same route on the Mac build, [run later the same day](../mac/PERFORMANCE.md#learn--plays-route), is
6.1 ms a frame, so 7.2 ms is about 1.1 ms from it, 17% more. The game thread is 4.2 ms a frame here against the Mac's 4.0,
the Wwise and note detection threads 1.9 ms against 1.1. What is left, by what the profiles show:

- the Wwise and note detection threads. Fences take a good part: 0.5 ms a frame of these two threads went without them.
  The note detection is x87 here and SSE on the Mac.
- the D3D9 layer, 23% of the game thread, of which the Metal work the Mac does too is about half: the state and object
  lookups, the device lock.
- the memory order itself is the same on both editions, so it isn't part of the gap, but it is the largest single cost
  left in this one.

## Hook points

What the hook table costs the game thread with no mod loaded: nothing that shows (2026-11-04, the 75 Hz display, 120 s
of frames from the tuner into the song, three runs of each in turn). "Without" is the same sources translated without
`--target`, so the nine hook points that are not built-in fixes are plain functions; both have the same 1,198,690 memory
fences.

| | with hook points | without |
|---|---|---|
| Mean frame | 13.362, 13.359, 13.359 ms | 13.359, 13.359, 13.359 ms |
| p99 | 14.64, 14.64, 15.40 ms | 14.60, 15.21, 14.92 ms |
| `w32 game`, the song | 38.1% of a core (37.2-38.7) | 38.0% (37.3-38.5) |
| Process, the song | 63.3% | 63.1% |

In the unit test a point without hooks is 0.01 ns a call more than the function, with a mod's pre and post hook 36 ns; a
song calls `ak_set_rtpc_name` 9 times a frame and `camera_set_frustum` 6.4 times. One run with the probe mod on every point
has the same figures: mean 13.362 ms, `w32 game` 37.8%.

## The build: memory and time

2026-11-04, the M2 (16 GB), 8 jobs, nothing else building. Through the app, with the seed and nothing placed first:
25 minutes in all, the build 24 of them and the game 19. The installer's build run by itself: 19.5 minutes (68 minutes of
CPU), the game 15 of them.

| Step | What runs | Memory |
|---|---|---|
| Finding the game's functions | one process, about 8 minutes then, under 3 now | 1.4 GB |
| Translating | a worker per job | 0.37 GB each (flat after the first functions; 3 GB with 8) |
| Compiling the generated C | a compiler per job | at most 0.8 GB each, 2.9 GB with 8 |

`lp_build.py` runs one job per core, as many as fit in half the memory at 600 MB a job (`default_jobs`): 6 on an 8 GB Mac,
every core from 10 GB up; `--jobs N` overrides it. So an 8 GB Mac should peak at about 3 GB: worked out from the numbers
above, not run on one. Earlier the translator kept every function's decoded instructions and took 23 GB on the game.

## Build options

- **`-O2 -mno-outline`** is what the installer compiles the generated C with (`win-x86/build/translate.sh`): the machine
  outliner's fragments were 15% of the game thread. `-O3` gives nothing measurable, and ThinLTO and PGO weren't tried: as
  on the [Mac](../mac/PERFORMANCE.md#build-options), the compiler is not where the time is.
- **`RC_MO_TSO`**: the [memory order](#memory-order). A compiler flag: the translator does the same work with or without
  it. It adds about 10% to the binary.
- **`RC_RELAXED`** (on by default): the functions of `tools/targets/win-x86/relaxed.map`, and those named in
  `tools/targets/names/relaxed.list` that `symbols.map` has here, fence at entry and exit instead of at each access.
  `RC_RELAXED=0` at the build fences them too.
- **`LP_NATIVE_MATH=0`** (at run time): the CRT's math runs translated. Native math saves 30% of the note detection
  thread.
- **`LP_QOS=0`** (at run time): the default QoS for the game's threads. User-interactive QoS on the game thread keeps the
  frames on a loaded machine: with eight busy loops 45% of a core and no frame over 33 ms, against 72% and 5 without.

## Opportunities, by expected payoff

1. **The note detection and the Wwise thread**: 1.9 ms a frame against the Mac's 1.1 on
   [one route](#toward-the-mac-edition). Relaxing more functions gave nothing
   ([Where the CPU goes](#where-the-cpu-goes-in-1130)). What is left there: the FFT pass and bit reversal, 1.7 points of a
   core together and relaxed already, and `pow`, which reaches the system's through the CRT's error rules at 0.5.
2. **The D3D9 layer's bookkeeping**: `d3d9_obj_get`, `d3d9_dev_lock`, the buffer sync, about a tenth of the game thread
   besides Metal's own work. The layer has one lock and a table lookup per call, on a device only the game thread uses.
3. **Zero-copy dynamic buffers**: back each dynamic D3D9 buffer's guest memory with the Metal buffer itself
   (`newBufferWithBytesNoCopy` on page-aligned guest memory, a ring of a few such regions per buffer for DISCARD, Lock
   handing out the next one). Removes the rest of the upload (about a point of a core) and the guest-side duplicate of
   big dynamic buffers. Medium-large; needs a libRSOpenGL entry point for client-memory buffers.
4. **Index-range scan** (D3D9 layer): the draw-time upload reads every index of an indexed draw from a dynamic buffer to
   find its vertex range, 0.7% of the game thread. Trusting `MinVertexIndex` / `NumVertices` (D3D9's contract; some games
   lie) or caching the range until the next lock would save part of it.
5. **Memory**: 0.3 to 0.5 GB over the Mac build. Every D3D9 buffer keeps its guest copy for its whole life (`w32_galloc`);
   dropping it for write-only static buffers after the upload would return some of that. Medium; measure the split first
   (`footprint -v`, the guest window against the Metal heaps).

Done, in [Toward the Mac edition](#toward-the-mac-edition): the x87 stack in locals, `-mno-outline`, relaxed audio kernels,
the indirect-call lookup, a native `memcpy`. Not worth pursuing: `-O3`, QoS changes (every busy thread already runs on the
performance cores), `ldapr`/`stlr`, relaxing more of the audio functions.

## Reproducing

```sh
tools/perf/run.py CONFIG OUT              # a warm-up and three measured runs of each edition, in turn
tools/perf/report.py OUT [--against BASE] # the table, and the change from another set of runs
tools/perf/run.py CONFIG OUT --trace 30   # a Time Profiler trace of each edition
tools/perf/profile.py OUT/lp-trace/game.trace [--under NAME]
```

`CONFIG` names test installs of your own (`tools/perf/example.json`); what a run pins and checks is in
[Mac testing](../mac/TESTING.md#measured-runs), and what differs here in [TESTING.md](TESTING.md#measured-runs). By hand,
a run of the scripted song with frame times and the per-thread CPU log:

```sh
LP_GAME_DIR=<game folder> LP_USER_DIR=<test user folder> RS_STEAM_DATA_DIR=<test steam folder> \
RS_STEAM_LOG=1 RS_GL_FRAMETIMES=ft.txt LP_THREAD_CPU=10 \
LP_INPUT_SCRIPT='@callback 1101,3:24,10:24,20:24,32:24,44:24,62:75,68:24' \
  <build>/out/rs2014_lp 2> log.txt
```

`ft.txt`: lines `F <t0 ns> <duration ns> ...`, one per Present; the frame interval is the difference of consecutive `t0`.
`log.txt`: `lp: thread cpu (10 s): [name %] ... total %` every 10 seconds. Compare one variable at a time, alternate the
builds, and repeat each at least twice.
