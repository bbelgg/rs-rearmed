# Performance of the Mac port

Measurements of the native build, all on one Mac: Apple M2 (4 performance and 4 efficiency cores), a 75 Hz
external display and the built-in 60 Hz Retina screen. The Windows port's measurements:
[Windows performance](../windows/PERFORMANCE.md).

## Summary

In gameplay the native build uses about the same CPU as the original game under Rosetta, a little less on
average: about half a core at 60 fps, measured over a scripted song. It renders at the display's refresh
rate, so a 75 Hz or 120 Hz display gets 75 or 120 fps (`RS_FPS=N` fixes a rate). Metal uses about 5% less
CPU per frame than OpenGL. The compiler switches (ThinLTO, PGO, `-O3`) change little.

## Method

- A test install separate from the user's (the installed game untouched), driven by an input script and a
  virtual audio cable playing a recording into the Real Tone input: menu, a song, about a minute of play.
  The input injector, the virtual cable and the scripts that run and measure these are not in this
  repository ([TESTING.md](TESTING.md#game-runs)).
- CPU: the game process' CPU time over a fixed window of the song, in seconds per second (1.0 = one core).
  Builds alternated, each measured more than once.
- Frame times: `RS_GL_FRAMETIMES=FILE` (one line per frame); snapshot runs (`RS_GL_SNAPSHOT_DIR`) excluded,
  since each snapshot is a synchronous readback of about 70 ms.
- Rosetta: the original game under Rosetta on the same Mac, over a scripted song.

## Results

### Against Rosetta

| Song | CPU |
|---|---|
| original under Rosetta | about half a core at 60 fps |
| native, OpenGL, 60 fps | about the same, a little less on average |

(Measured before Metal existed; the exact figures were not kept in this repository.)

The latency figures are the other difference: the native build runs the output device at 256 frames
instead of the system's 512, and at 128 with `LatencyBuffer=1` without crackling, which the game under
Rosetta does not manage.

### Renderer

Song, 45 s from the intro, warm caches, original `cache.psarc`, 3 alternating rounds:

| | frame rate | CPU | CPU per frame | game frame |
|---|---|---|---|---|
| OpenGL | 60 fps | 0.415-0.42 s/s | 6.9-7.0 ms | 4.37 ms |
| Metal | 75 fps (the display's rate) | 0.48-0.49 s/s | 6.6-6.7 ms | 3.96 ms |

Metal costs about 5% less CPU per frame and has a 9% shorter game frame; it uses more CPU in total only
because it renders 75 frames a second where OpenGL stays at 60.

Frame times over a 53 s song: OpenGL median 16.66 ms, p99 18.0 ms; Metal median 13.34 ms, p99 15.7 ms; one
frame over 20 ms on each.

- **Retina** (`RS_METAL_RETINA=1`, the built-in 60 Hz screen, 2560x1440): median 16.65 ms, p99 17.95 ms,
  game frame 4.12 ms (3.82 at 1x).
- **Multithreaded GL engine** (`RS_GL_MTENGINE=1`, OpenGL only): the frame thread 4% shorter, total CPU 2%
  higher. Off by default.
- **Long session** (Metal, six full songs in a row, 37 minutes): 74.7 fps, memory footprint 943-983 MB from
  the first song to the last, no frame over 50 ms outside song loads.

### Shader preparation

The Metal shader corpus (3880 distinct programs) compiles in about 270-375 s on a cold system cache
(install-time warm-up, 8 threads), and in about 0.2 s when the system cache is warm. The first play of a
song after an install has a few 0.3-0.8 s stalls in its first half-minute while pipelines are built for
the first time; the second play has none. Learn & Play's corpus is 281 programs, from a list of what the game was seen linking:
[its Shader preparation](../windows/PERFORMANCE.md#shader-preparation).

### Against the Windows port

Same Mac, same day, the 1280x720 window: 42.2% of a core in a song, GPU 41%, footprint 0.81 GB, 60 fps with
p99 19.5 ms and no frame over 33 ms; the title takes keys after 15-18 s. Table and conditions:
[Windows performance](../windows/PERFORMANCE.md#against-the-mac-build). Those are from before the memory order and the
intro skip; the same song on both at 75 fps: [Final pass](#final-pass-for-140).

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
install then; the Mac edition's Preparing shaders ran first and took about 330 and 390 s. Frames over 33 ms, and the time they took:

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
prepares its shaders now too, in 16.5 s: [its numbers](../windows/PERFORMANCE.md#shader-preparation).

**Against 1.3.0**: a comparison with the figures recorded above, not a same-day run: 1.3.0 was not built or run in this
pass, and those figures come from other songs and a shorter window. CPU 44% of a core against the recorded 48-49%; game
frame 3.95 ms against 3.96 ms; median 13.2-13.3 ms against 13.34 ms; footprint 0.80-0.81 GB against 0.81 GB; Preparing
shaders about 330-390 s against 270-375 s. p99 is higher, 18.1-18.6 ms against 15.7 ms, and a run had 2 to 8 frames over 20 ms
in 150 s against one in 53 s; Learn & Play shows the same p99 on this song, which suggests the song or the display and not
the code, but no run shows that. Nothing recorded got clearly worse, and nothing here proves it did not.

## Hook points

Unchanged for this edition: the hook table is built in as it always was, and a function without hooks is called as
before. The run-time table and what it costs are Learn & Play's ([Windows](../windows/PERFORMANCE.md#hook-points)).

## The build: memory and time

A first install through the app on 2026-11-04 took 12 minutes, with the system's shader cache warm so that Preparing
shaders took a second; with it cold, add 5.5 to 6.5 minutes. Memory not measured yet.
[Windows](../windows/PERFORMANCE.md#the-build-memory-and-time) has the numbers for its translator, which is a different front
end.

## Build options

The switches change only how the generated C is compiled; the normal build uses none of them.

| Build | Gameplay CPU | Cost |
|---|---|---|
| `RC_LTO=thin` | no measurable change | about 3.5 minutes more build time |
| `RC_LTO=thin` with `RC_PGO=use:` a played profile | about 2% less | two builds and a playing session |
| `RC_OPT=-O3` | little change | |

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

**`RC_RELAXED`** (on by default): the Wwise effect kernels listed in `tools/targets/names/relaxed.list` are
translated without a memory fence per access, which was about half their time; they fence at entry and
exit instead. `RC_RELAXED=0` fences them too, for tracking down a suspected memory-ordering problem.
