# Performance of the Windows port (Learn & Play)

Machine: Apple M2 (4 performance + 4 efficiency cores), 16 GB, macOS 27, Apple clang 21. The current figures are in
[Final pass](#final-pass-for-140), [Shader preparation](#shader-preparation), [Hook points](#hook-points) and
[The build](#the-build-memory-and-time). The sections from [Baseline](#baseline-as-merged) to [Under load](#under-load) are
from 2026-11-01/02, on the built-in display at 1470x956 points and 60 Hz, and tell how the seam's two big costs were found: there "final"
is the build of 2026-11-02, before the memory order and the intro skip, so compare within those sections only.

## Summary

A song holds the display's rate, 74.9 fps on 75 Hz, with no frame over 33 ms, at 63% of a core against the Mac build's 44% on the same
song and screen, and 1.3 GB against 0.8 GB ([Final pass](#final-pass-for-140)). Most of the first gap to the Mac build was not translated
code but two costs in the Win32 / D3D9 seam: every SetEvent woke every waiting guest thread (a fifth of a core of system time), and the game's
whole-buffer vertex buffer locks were each uploaded whole (2.7 GB/s of copying). Fixing both took a song from 69% to 48% of a core at 60 fps.
Keeping x86's memory order then cost about 10 to 15 points of a core at 120 fps and changed no frame times ([Memory order](#memory-order)).
Load times are short and paced by the game itself, not by the translation.

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
- **Noise**: the same configuration measured 69.0, 69.0, 69.5, 70.1% (as merged) and 52.4, 52.5%
  (after the first fix): about +-1 point of a core between runs. Differences under 2 points are
  not results.

## Baseline (as merged)

| | |
|---|---|
| First frame | 0.3 s after start |
| Title takes keys | 27.4 s (three runs: 27.4, 27.4, 27.4); 31 fps during the logos, about 40% of a core |
| Title -> profile list | under 1 s |
| Profile -> main menu | about 6.5 s (the autosave notice, then the menu flies in: the game's pacing; the Mac build skips the notice with `RS_MODS_AUTOSAVE`) |
| Play Song -> tuner | 2-3 s (the first run after a build: one 3.5 s frame, caches cold) |
| Skip tuner -> highway | about 5 s (the venue intro and song title) |
| Song: frame rate | 60.0 fps, the display's rate |
| Song: frame interval p50 / p95 / p99 / max | 16.7 / 17.2 / 17.5 / 18-27 ms |
| Song: frames > 33 ms / > 50 ms | 0 / 0 (four runs, 6,600 frames each) |
| Song: CPU | 69.4% of one core (69.0, 69.0, 69.5, 70.1), of it 21 points system time |
| Song: GPU | 65% (full screen 1470x956, the game's default 4x MSAA) |
| Song: memory | footprint 1.04-1.06 GB, RSS 0.97-1.0 GB, flat over the song |

Threads in the song (as merged, `LP_THREAD_CPU`, % of one core):

| Thread | CPU | What it is |
|---|---|---|
| `w32 game` | 35 | the game's main thread: simulation, Scaleform, all D3D9 calls |
| `w32 thread 1a823c0` | 9 | a worker thread, not identified (busy in every frame of the song) |
| `w32 thread 78afc59c` (4) | 8 | threads started through msvcr100's `_beginthreadex`; 0.5% after the wake-up fix |
| `w32 thread 149ab10` (2) | 8 | one of them the guitar note detection (`F_13f3690`, `_CIpow`) |
| `w32 thread 1aa1f80` (2) | 3 | in long waits (one for the whole song); 0 after the wake-up fix |
| Metal and CoreAudio threads | 4 | completion handlers, the audio I/O threads |

Most threads other than the game's spent 1.5-2% each in system time with almost no user time,
among them one whose single `WaitForMultipleObjects` lasted the whole run (`LP_DUMP_WAITS`): see
"Changes made", 1.

## Hot spots (as merged)

Game thread, inclusive share of its samples (two 10 s samples in the song):

| | share |
|---|---|
| D3D9 calls (`d3d9_hostfn_call` when measured, now `w32_hostfn_call`: all COM methods of the device and resources) | 32% |
| of it `IDirect3DVertexBuffer9::Unlock` (`b_unlock` -> `glBufferSubData` -> `memmove`) | 18% |
| of it draws (`rsm_draw`, Metal encoding) | 6% |
| import calls (`rc32_import_thunk`) | 8% |
| indirect-call dispatch (`rc32_icall`, `rc32_lookup`, `lookup_slow`, `_tlv_get_addr`) | 5% self |
| translated game code, self | ~40% |
| machine-outlined fragments of it (`OUTLINED_FUNCTION_*`) | 7% |

All threads by category (self samples): translated game 41%, `memmove` 18%, other code in the
binary (outlined fragments, the runtime's `run`) 13%, Metal driver 10%, runtime (`rc32_*`) 7%,
libRSOpenGL 2%, seam 2%, libm 0.3%.

The translated code's profile is flat: the hottest function has 3% of the samples. The top ones
are x87 transform code: `F_ff80f0` (a 3x3 matrix product, `fld`/`fmul`/`faddp`) and `F_ff81e0`
(composes a transform with it), the scene graph around `F_1577050`, `F_155ee30`, `F_1561fd0`,
and msvcr100's `memcpy` (`F_78aa1e80`). The x87 stack lives in `rc_ctx` (`ST(k)` is
`c->fpu.st[(top + k) & 7]`, every push and pop updates `top` and the tag word), and with
`-fno-strict-aliasing` each guest store may alias it, so each x87 instruction is several loads
and stores; clang cannot keep the stack in registers.

Indirect calls are cheap after the lookup cache (rc32_lookup 1.2%, `_tlv_get_addr` 0.7% for its
thread-local table): not worth more work now.

Boot and the song load: in samples taken during the logos and right after Play Song the game
thread renders (30 fps logos, then the menus) and the other threads are idle; nothing is CPU
bound (37-43% of a core during the logos). The waits are the game's own pacing: logo and
licence screens of fixed length, fades, the venue intro.

## Changes made

### 1. Wake only the threads waiting for an object

Every kernel32 wait slept on one process-wide condition variable, and every SetEvent,
ReleaseMutex, ReleaseSemaphore, timer arm, thread exit and overlapped completion broadcast it:
each one woke every waiting guest thread to re-check under the global lock and sleep again. A
wait now sleeps on a condition of its own, listed with the objects it waits for, and a change
signals only the waits on that object (`w32_hwake_obj`, win32_ksync.c). `LP_WAKE_ALL=1` wakes
every waiter again, for debugging. Test: `win-x86/shims/tests` (`make test`): an idle waiter is
not woken by other objects (0 wake-ups for 1,000 SetEvents, the old way 100-200), auto-reset
hand-offs (2,000 in 25 ms against 62 ms the old way), wait-any, wait-all, timeout.

| Song | CPU (% of a core) | game thread | other guest threads |
|---|---|---|---|
| before (LP_WAKE_ALL=1, two runs) | 75.9, 76.7 | 34.1 | 36 |
| after (two runs) | 52.4, 52.5 | 34.6 | 13 |
| as merged (one broadcast, four runs) | 69.4 | 35 | 30 |

(`LP_WAKE_ALL=1` signals each waiter separately, which costs more than the one broadcast did.)
Frames are unchanged (60 fps, p99 17.4-17.5 ms).

### 2. Dynamic vertex buffers upload at the draw

The game locks its 1 MB dynamic vertex buffer whole (offset 0, size 0) about 2,900 times a second
in the song (each frame: 4 DISCARD, ~37 NOOVERWRITE, ~7 plain locks) and appends a few vertices
each time. The layer uploaded the locked range at each Unlock: 2.7 GB/s of `memmove` into Metal
buffers, measured at 65 ms of every second of the game thread. Now Unlock of a dynamic buffer
only notes the range as pending, and a draw uploads the part it reads (the vertex range of the
draw; for an indexed draw from the smallest and largest index in the guest's index data, plus the
base vertex; per instance for instanced streams; the index range of a dynamic index buffer).
`LP_D3D9_EAGER=1` uploads at Unlock as before. `d3d9_test` streams quads the game's way and checks
each is drawn (it fails if the draw-time upload is skipped).

| Song (two runs each, same binary) | CPU | game thread |
|---|---|---|
| `LP_D3D9_EAGER=1` | 52.8, 52.2 | 34.7 |
| upload at the draw | 48.9, 47.1 | 30.2 |

Highway, notes, sustains and text checked by eye on snapshots.

### 3. Diagnostics

`LP_THREAD_CPU=S` (per-thread CPU log, named threads: `w32 game`, `w32 thread <start>`, also
visible in `sample` and Instruments) and `RS_GL_FRAMETIMES=FILE` (frame times in the Mac build's
format). Neither costs anything when unset.

## After: the final build

| Song, 120-230 s | as merged | now |
|---|---|---|
| CPU (% of one core) | 69.4 | 47.9 (48.9, 47.1, 47.6) |
| game thread | 35 | 29-31 |
| system time (all threads) | 20-21 | 4.7-4.8 |
| frames | 60 fps, p99 17.5 ms, none > 33 ms | the same |
| footprint | 1.05 GB | 1.05 GB |

The game thread's profile afterwards (two 10 s samples): D3D9 calls 18% inclusive (was 32%),
`IDirect3DVertexBuffer9::Unlock` 1.4% (was 18%), the draw-time upload 0.3%, Metal encoding of
draws 7.5%; the hottest single symbols are now msvcr100's translated `memcpy` (4% of the game
thread), `rc32_icall` (3%) and the x87 matrix code `F_ff80f0` / `F_ff81e0` (3% and 2%).

## Options (final build, one run each unless noted)

| Run | CPU | GPU | Frames | Notes |
|---|---|---|---|---|
| default (full screen 1470x956) | 47.6 | 65% | 60 fps, p99 17.4 | |
| `LP_NATIVE_MATH=0` | 49.7 | | same | the note-detection thread 6.9% instead of 4.8%: native math saves 30% of it |
| `RS_METAL_RETINA=1` (2940x1912, two runs) | 48.5, 49.1 | 73% | same | footprint +150 MB (1.20 GB) |
| `RS_FULLSCREEN=0` (1280x720 window) | 47.7 | 38% | same | |
| `LP_QOS=0` | 47.0 | 65% | same | a quiet machine does not need it |

Compiler options (the game's generated C recompiled with ninja, 8 jobs, everything else the same;
the CPU column is two or three runs, game thread in brackets):

| `RC_OPT` | CPU | compile of the game (8 jobs) | binary |
|---|---|---|---|
| `-O2` (shipped) | 69.2 (34.7) | 4.1 min | 97.0 MB |
| `-O2 -mno-outline` | 68.0 (33.1) | 4.0 min | 111.6 MB |
| `-O3` | 67.8 (34.4) | 5.0 min | 97.4 MB |

These were measured before the two fixes, on a build with a native `memcpy`/`memmove`/`memset`
override (no measurable change: 69.2 against 69.4, so it was not kept). Turning off the machine
outliner gains 1.5 points of the game thread (4%) for a 15% larger binary; `-O3` nothing
measurable. As on the Mac build ([Mac performance](../mac/PERFORMANCE.md#build-options): ThinLTO no change, ThinLTO + PGO about 2%), the
compiler is not where the time is. ThinLTO and PGO were not tried here: `gen_image_pe.py` has no
switches for them yet, and the Mac's results bound what they can give.

## Under load

Eight busy loops (`perl -e 'while (1) {}'`, one per core) for the whole run:

| Build | CPU | Frames (120-230 s) |
|---|---|---|
| as merged (QoS on) | 133.5 | 60 fps, p95 17.7, p99 18.5, max 28 ms, none > 33 ms |
| final, QoS on | 45.1 | 60 fps, p95 17.7, p99 18.8, max 23 ms, none > 33 ms |
| final, `LP_QOS=0` | 71.7 | 60 fps, p95 21.7, p99 27.4, max 38 ms, 5 > 33 ms |

The user-interactive QoS of the game thread is what keeps the frames on a busy machine; the
wake-up fix is what keeps the game from adding to the load (as merged, the process used 1.3 cores
under contention, against 0.45 now). Without QoS the game thread likely runs more on the
efficiency cores, which needs more CPU time for the same work (72% against 45%).

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
about 120 fps ([plan](PLAN.md#fixed-in-151)), which may be why.

So the fences cost 10 to 15 points of a core, about 17%, spread over all threads; frames do not change. Their cost at
the display's rate was not measured: a build without them for the same song at 75 fps was started on 2026-11-04 and
stopped before it was done, so the only figure for the current build is with them ([Final pass](#final-pass-for-140)), and what
of the 19 points over the Mac build is fences is not known. The
binary grows from 97 to 106 MB. Of the guest loads the translator emits, 834,000 are fenced, 563,000 are on the
stack and 23,000 read constants (`RC_LDC`).

Tried, not kept: native `memcpy`, `memmove` and `memset` fenced once per call, as translated they fence every 16
bytes: 81.5 and 85.7, no better than without. Not done: `relaxed.list`. None of its 14 functions is matched in
`win-x86` yet, and the front end does not emit the relaxed form. On the Mac it takes about half the time of the
effect kernels; here the worker and note detection threads grew most, but whether those kernels run there has not
been checked.

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

## Against the Mac build

The figures to use are in [Final pass](#final-pass-for-140): the same song on both, 43% more CPU and 0.5 GB more memory. An earlier
comparison on 2026-11-02, with other songs, in the 1280x720 window and before the memory order: the Mac build at 42.2% of a core and 0.81 GB,
Learn & Play at 47.7% and 1.05 GB, both at 60 fps with no frame over 33 ms. The extra memory is likely the DLLs' images in the guest window and
the D3D9 layer's guest copies of resources next to the Metal ones.

## Opportunities, by expected payoff

1. **x87 stack in locals** (translator, `xlat/emit_x87.py`): within a block whose x87 stack depth
   is known statically (all of the hot transform code), keep `st(i)` in C locals and write the
   stack back only at calls, exits and unknown-depth instructions. The x87-heavy functions are a
   large part of the game thread's translated time; expect 5-10% of the game thread, more in
   physics-heavy screens (Guitarcade). Medium effort, needs the differential tests of the
   front end.
2. **Index-range scan** (D3D9 layer): the draw-time upload reads every index of an indexed draw
   from a dynamic buffer to find its vertex range. Trusting `MinVertexIndex` / `NumVertices`
   instead (D3D9's contract; some games lie) or caching the range per (buffer, start, count)
   until the next lock would save part of the remaining upload cost. Small; measure first.
3. **Zero-copy dynamic buffers**: back each dynamic D3D9 buffer's guest memory with the Metal
   buffer itself (`newBufferWithBytesNoCopy` on page-aligned guest memory, a ring of a few such
   regions per buffer for DISCARD, Lock handing out the next one): no copy at all, and NOOVERWRITE
   then means exactly what it means in D3D9. Removes the rest of the upload (under 1 point of a
   core now) and the guest-side duplicate memory of big dynamic buffers. Medium-large; needs a
   libRSOpenGL entry point for client-memory buffers.
4. **Relaxed audio kernels**: match `relaxed.list` in `win-x86` and emit the relaxed form in the 32-bit front
   end ([Memory order](#memory-order)). The worker and note detection threads went from 11 to about 16 points of a
   core with the fences; find out first what of that is in those kernels. Medium effort: the matching is by hand.
5. **`-mno-outline` for the generated C**: 4% of the game thread for 15% more binary size, the
   same compile time. Cheap (`--opt` in `TRANSLATE_OPTS`, which rebuilds every cache);
   worth taking with the next translator change that invalidates the caches anyway.
6. **Memory**: 0.5 GB over the Mac build. Every D3D9 buffer keeps its guest copy for its whole
   life (`w32_galloc`); dropping it for write-only static buffers after the upload (and
   re-reading from Metal on the rare readback) would return some of that. Medium; measure the
   split first (`footprint -v`, the guest window against the Metal heaps).
Not worth pursuing now: the indirect-call lookup (5% of the samples with its cache, little left
to take), the import thunks (8% inclusive, mostly the D3D9 work behind them), `-O3`, a native
`memcpy` override, QoS changes.

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
