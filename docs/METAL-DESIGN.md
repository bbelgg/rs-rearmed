# Metal backend: design

How the pieces of [METAL-PLAN.md](METAL-PLAN.md) fit together. Interfaces named here are fixed; change
them only together with every user.

## Layers

```
 game (recompiled, Scaleform GL HAL, Gamebryo)   Cg shim (glsl backend)   shell (rsp_shell.m)   mods
          \__________________ gl* / CGL* ________________/                    |            |
                                   |                                    rs_gfx_* API   rs_overlay_* (executable)
                            libRSOpenGL.dylib  ── rsgl.c wrappers: rs_impl_<name> per entry point
                              /                         \
             RS_GL_BACKEND=gl                    RS_GL_BACKEND=metal (default)
             system OpenGL.framework             src/metal/: rsmtl_<name> implementations
                                                 src/msl/:   GLSL 1.20 -> MSL (offline corpus + runtime fallback)
```

- **One entry-point table.** `libRSOpenGL` wraps the union of the game's 107 imports
  (`game-gl-imports.txt`) and the extra functions the Cg shim, the shell and the overlay call
  (`extra-gl-imports.txt`). Everything goes through `rs_impl_<name>`. With `RS_GL_BACKEND=metal`
  `rs_impl_<name> = rsmtl_<name>`. The generator emits a weak `rsmtl_<name>` for every entry point that
  logs `unimplemented <name>` once and returns 0; a strong definition in `src/metal/` replaces it. The
  backend is chosen once, at load, and `rs_gfx_backend()` reports it: Metal unless `RS_GL_BACKEND` is
  `gl` (or `opengl`). The spy (`libglspy`) always wraps the system framework.
- The Cg shim links `libRSOpenGL` instead of `OpenGL.framework`, so its calls take the same path. On Metal
  (asked from `rs_gfx_backend()`, not from the environment) the Cg shim always uses its `glsl` backend:
  ARB programs never reach the Metal core.
- `CGL*` (game imports `CGLLockContext`/`CGLUnlockContext`) and `NSOpenGLContext` are only used on GL. On
  Metal the shell uses `rs_gfx_*` and `CGLLockContext` takes the context's mutex.

## Defaults and fallback (`shims/platform/include/rsp_renderer.h`)

Metal is the default; OpenGL stays a choice and the fallback. Since `libRSOpenGL` fixes its backend when
it loads (before `main`), the app's `main()` decides first thing, before the image, the shims, the
engine or a window (`rsp_renderer_startup`, `rsp_renderer_decide`: pure, unit-tested in
`platform.core`), and when the decision differs from what `libRSOpenGL` loaded with it `execv`s its own
executable with the same arguments and `RS_GL_BACKEND` set (same path: the same app for Metal's shader
cache; no `NSApplication` yet, so LaunchServices sees nothing). Inputs, strongest first:

1. `RS_GL_BACKEND=gl|metal`: always wins; `metal` also over the crash count.
2. Metal cannot start: no `MTLCreateSystemDefaultDevice()`, or a `libRSOpenGL` without the `rs_gfx`
   presentation (`rsp_mtl_usable`; `RS_RENDERER_TEST_NO_METAL=1` simulates it) -> OpenGL, this start only.
3. Crash count: the last two Metal sessions ended without a clean exit -> OpenGL; the setting becomes
   `opengl` (so the next starts need no count), the marker goes, `renderer-fallback.txt` explains it and
   an `osascript` notification says so (fire and forget, never blocks the game; `RS_RENDERER_NOTIFY=0`
   turns it off). Writing `metal` into the setting tries Metal again with a fresh count; `install.sh`
   also resets the count (a new build).
4. `<home>/renderer`: `metal` or `opengl` (`install.sh`'s `RS_RENDERER`, the installer's Renderer
   popup). The launcher passes `opengl` on as `RS_GL_BACKEND=gl` (no restart) and `metal` not at all (so
   the crash fallback still applies), plus `RS_NATIVE_HOME`.
5. Default: Metal.

`<home>`: `RS_NATIVE_HOME`, else the install when the app is `<home>/game/<app>` and `<home>/game-dir`
exists, else `~/Library/Caches/rs-rearmed` (builds run from elsewhere). **Session marker**: a Metal game
start (not `--warm-shaders`, `--init-only`, `--bind-only`, `--renderer-check`) writes
`<home>/metal-session` holding N, the abnormal endings in a row before it; clean exits remove it:
`rsp_exit` (the shell's quit path, `-[OpenGLView dealloc]` after `MainController`'s shutdown, ending in
`_Exit(0)`), `rsp_request_quit` without an engine, `exit()` (atexit) and SIGTERM/SIGINT/SIGHUP (a polite
stop is not Metal's fault; only installed when nobody else handles them). A marker found at start means
that session crashed, hung and was force-quit, or was killed: count N + 1. Force quit counts as
abnormal (unavoidable), hence the threshold of 2. A GL session neither writes nor clears it.

Late failure: `rs_gfx_create_layer` failing in `rsp_mtl_attach` (window already up) also restarts with
OpenGL (`rsp_renderer_restart_gl`: not counted as a crash); only if that exec fails does the shell abort
with a message. The re-exec'd process logs the original reason
(`[rsp] renderer: opengl (Metal cannot start here; restarted with RS_GL_BACKEND set)`).
`--renderer-check` prints the decision and exits (a pending crash fallback is applied as by a start).

Tests: GL-only tests set `RS_GL_BACKEND=gl` (the Cg directory adds it to every test not named
`*.metal`; `platform.overlay.<arch>`, `opengl.shim.window.*`), tests of both backends set it either way.

## Context and presentation API (`shims/opengl/src/rs_gfx.h`)

Exported by `libRSOpenGL`, implemented for both backends, used by the shell (step 6) and by tests:
offscreen contexts let every test run the same GL calls on both backends and compare pixels. See the
header for the calls.

## Metal core (`shims/opengl/src/metal/`)

Objective-C (`-fobjc-arc`), one context object per `rs_gfx_context`, current per thread.

- **Names**: per-type tables GLuint → object (buffers, textures, renderbuffers, framebuffers, VAOs, shaders,
  programs). Gen/Delete/Is/Bind follow GL rules for what the game does (names created on bind are fine).
- **Buffers**: `MTLBuffer` (shared storage). Every use in a command buffer records the frame serial. A
  `glBufferSubData` / `glMapBuffer` on a buffer still in flight first renames it (new `MTLBuffer`, old
  contents copied, old one released when its command buffer completes). `glBufferData` always reallocates.
- **Textures**: `MTLTexture` per GL texture; level 0 size/format fixes the descriptor, mips allocated on
  `glGenerateMipmap` or on a non-zero level upload. DXT1/3/5 → BC1/2/3, RGBA/BGRA 8-bit, luminance/alpha via
  swizzle. Uploads into a texture still in flight go through a staging blit. Sampler state from the GL
  texture parameters → cached `MTLSamplerState`.
- **Framebuffers**: attachments → `MTLRenderPassDescriptor`, rebuilt on change. Default framebuffer = the
  context's own colour + depth/stencil textures (drawable-sized). Depth+stencil formats →
  `Depth32Float_Stencil8`.
- **Programs**: `glLinkProgram` hands vertex + fragment GLSL to `rsmsl_translate` (`src/msl/msl_xlat.h`)
  and keeps the reflection (attributes, uniforms, samplers), so status, locations and `glUniform*` work at
  once; the MSL compile (`newLibraryWithSource:`, 0.1–0.5 s cold) goes to a compile pool (CPUs − 2 worker
  threads, `RS_MTL_COMPILE_THREADS`; programs with the same MSL share one compile). The first draw with
  the program waits for its library, or compiles it itself if no worker has started it. The link reports
  success when the translation succeeded and the MSL is not already known to fail: the Cg shim (and
  Scaleform) query `GL_LINK_STATUS` right after every link, so waiting there would serialise the compiles
  again. A compile that fails later unlinks the program at its first draw or status query (logged with
  the MSL, draws skipped, `LINK_STATUS` false and the compiler's message in the info log).
  Libraries and pipelines are cached in memory per device and on disk (`mtl_cache.m`: an
  `MTLBinaryArchive` plus a manifest of the libraries and pipelines used, warmed on the compile pool at
  the next start behind the game's own links). The disk cache's key is the cache format, a
  core-conventions constant, the OS build and the GPU, not the build of libRSOpenGL: libraries are keyed
  by their MSL text, and ones the game has not linked for 8 runs leave the manifest.
- **Safe load** (`rs_gfx_prepare`): with a community "fast-load" `cache.psarc` the game's loading can
  stall for good on its loading screen when the CPU is busy (a race in the game, seen on GL under load
  too), and a cold Metal start (first run, OS update, new translator) keeps every core compiling during
  loading. So when `<game>/cache.psarc` is not the original (size, then MD5; `RS_METAL_SAFE_LOAD=1|0`
  forces it), the shell runs the second half of `MainController awakeFromNib` (from `rsp_game_bootstrap`
  on: before it the engine, its loading threads and the display link do not exist) only after
  `rs_gfx_prepare(ctx, 600)` has returned on a background thread; the main run loop goes on meanwhile
  and the window stays black. From 0.5 s on it shows a centred white line, "Preparing shaders… 120 of
  3880" (a `CATextLayer` on the view's `CAMetalLayer`, updated 4 times a second from
  `rs_gfx_prepare_progress`, a second line when over 1000 libraries; removed before the bootstrap, so
  nothing of it is left when the game draws). `rs_gfx_prepare` gives warming (started when the context
  was made) the whole pool, every CPU, at user-initiated QoS and waits for it (milliseconds on a warm
  system Metal cache, about the cold compile time otherwise). **Corpus warm** (first run): the manifest
  only knows what earlier runs linked, so when it lists fewer than 32 libraries or the cache directory
  has no record (`corpus-warmed`, the identity of the corpus's MSL set) that this corpus was compiled
  completely there, it also compiles the MSL of every program of the offline shader corpus
  (`rsmsl_corpus_count`/`rsmsl_corpus_msl`; 4081 programs, 3880 distinct texts, minus the manifest's).
  Those libraries are not kept (some 60 KB each): the system's Metal cache keeps them, and the game's own
  compile of one later is a cache hit (all 3880 again take 0.1 s). Pipelines are not warmed from the
  corpus: they need draw state it does not have, cost far less, and the manifest has them from the next
  run on. Cost on an M2 (8 cores, cold system Metal cache): 83 s once (666 s of compiles on 8 threads);
  the next start (record written, the manifest's libraries only) 0.04 s; a forced corpus warm on a warm
  system cache 0.1 s. A new corpus or translator, or a new cache directory (OS update,
  GPU), warms again. At the timeout (600 s) the corpus libraries not started yet are skipped and no
  record is written (the next start goes on where the system's cache stopped), the manifest's continue,
  and the game starts and compiles what it lacks while it loads. `RS_METAL_CORPUS_WARM=0|1` turns the
  corpus warm off / forces it. Then the pool is throttled for the rest of the run: at most 2 jobs at
  once (`RS_MTL_COMPILE_THREADS` when set), all at utility QoS, so libraries the manifest did not know
  cannot starve the loading threads; a draw that needs one still waits for it or compiles it itself.
  **Install-time warm-up** (`--warm-shaders`, `rsp_mtl_warm_only` in `rsp_present_metal.m`,
  `tools/play-native/warm-shaders.sh`, run by `install.sh` and the installer's "Preparing shaders"
  step): the installed app's own executable, before it loads anything of the game (no image, no
  window, no NSApplication, so no Dock icon), re-executes itself with `RS_GL_BACKEND=metal` if needed,
  makes an offscreen context (which opens the disk cache and starts the manifest's warming) and runs
  `rs_gfx_prepare` with the same corpus rule, printing `progress F Preparing shaders… N of M` (F never
  decreasing, 1 at the end, also on a timeout; `RS_METAL_WARM_MAX_SECONDS`, default 3600); exit 0
  ready, 1 timed out, 2 no Metal. The game's later safe load then finds the `corpus-warmed` record and
  system-cache hits, and without safe load the game's own compiles are hits. It has to be the
  installed app, run at its resolved path: Metal's system shader cache
  (`$(getconf DARWIN_USER_CACHE_DIR)<bundle id>/com.apple.metal/`; a process without a bundle uses
  `…/com.apple.metal/` itself) is keyed by the bundle identifier and, as measured on macOS 26, the
  length of the bundle's path (a copy of the app at a path of another length misses, one of the same
  length with other names hits; a path through a symlink counts with its own length; argv[0] does not
  matter). LaunchServices starts the app by its resolved path, and a launch through `open -n -a` hit
  every library of a warm-up done by exec'ing the executable directly (500 of 500 in 10 ms, 31 s cold;
  a copy at another path length: 84 of 500 in 5 s). So a separate tool cannot do it, the warm-up
  stays valid across reinstalls to the same place, and moving the install warms again (by the
  manifest; the corpus too while the manifest is small). Our own disk cache is per user, not per app
  (`~/Library/Caches/rs-rearmed/metal`), the same for both. Cost on this M2, cold: 3880 libraries in
  270 s (8 threads, nothing else running); again: 0.2 s.
  Pipelines are built on the drawing thread (warming: on the pool), archive adds on one serial utility
  queue. With the original `cache.psarc` nothing of this applies. GL: `rs_gfx_prepare` is a no-op.
  Uniforms live in CPU shadow blocks per stage, uploaded per draw with `setVertexBytes`/`setFragmentBytes`
  (or a ring buffer when over 4 KB).
- **Pipelines**: key = program, vertex layout (formats, strides, step functions), colour/depth formats,
  sample count, blend (enable, factors, equation), colour mask, alpha-test function → cached
  `MTLRenderPipelineState`. Depth/stencil state → cached `MTLDepthStencilState`. Dynamic state (viewport,
  scissor, cull, winding, fill mode, stencil ref, blend colour, depth bias) is set on the encoder.
- **Encoders**: a render encoder is opened lazily at the first draw after a framebuffer bind; a `glClear`
  before the first draw becomes load actions, a later one (or a scissored one) draws a quad. Reads
  (`glReadPixels`, `glGetTexImage`) end the pass, commit and wait. Present ends the pass, draws the default
  colour texture into the drawable (y flip), commits, and limits frames in flight to 2 (semaphore).
- **Conventions** (applied by the translator, so the core passes GL state through unchanged):
  - row 0 of every texture and render target is GL's bottom row; the vertex shader epilogue negates
    `y`, so viewport and scissor rectangles map 1:1 and uploads/reads need no flip; only present flips;
  - winding is reversed by the y flip: the core inverts `glFrontFace`;
  - clip-space depth: epilogue `z = (z + w) * 0.5`;
  - alpha test: function constant 0 (`uint`, the GL compare function minus `GL_NEVER`, 8 = off) and the
    reference in fragment buffer 1 (`float`).
- **Errors and queries**: `glGet*` answered from tracked state; `glGetError` returns the first recorded
  error. `RS_GL_CHECK` works the same on both backends.
- **Built-in attributes**: GLSL `gl_Vertex`, `gl_Normal`, `gl_Color`, `gl_MultiTexCoordN` use NVIDIA's
  aliasing (0, 2, 3, 8+N), so `glVertexAttribPointer` indices drive them.

## Shader translation (`shims/opengl/src/msl/`, `tools/metal/`)

`rsmsl_translate` (see `msl_xlat.h`) first looks in the offline corpus (MSL generated at build time from
every shader the game can compile; keyed by a hash of both sources), then translates at run time and logs it
(`RS_MSL_LOG`).

The corpus of a user's build is made from the game files alone (`tools/metal/make-corpus.sh`, run by
`build-m4.sh`, a few seconds): Scaleform's GL HAL keeps the full source of each shader in the image,
one set per GLSL version; `extract_scaleform.py` takes the `#version 120` set (66 vertex, 147 fragment
shaders) and sizes the batch uniform arrays (`vfuniforms`, `vfmuniforms`) for 12 batches instead of the
strings' 24, as the HAL does at run time with the GL limits both backends report. The result is byte
for byte the 213 shaders `RS_GL_DUMP_SHADERS` dumped in menu and song runs. The Cg side comes from
`static.psarc` (`shims/cg/tools/extract_corpus.py`, then `cg_corpus_load --dump-glsl`). Pairing
(`pair_shaders.py`) and translation (`build-msl-corpus.sh --translate-only`, i.e. `msl_verify
--translate-only`: no MSL compile, no GL) give 6250 programs, 4081 entries, 3880 distinct MSL texts:
the same entries as the corpus built from game-run dumps and checked against GL (only the "verified"
flag differs; `RS_MSL_VERIFY=1` runs that check too, about 5 minutes). Not covered: a shader the game
would build at run time from other sources (none seen in game runs), and a game version whose image
does not hold the expected set (`extract_scaleform.py` then fails and the build goes on without a
corpus). Binding conventions: vertex attributes `[[attribute(i)]]` from `[[stage_in]]`; vertex
buffers at buffer index 14 + attribute index (14..29: one buffer per enabled attribute, Metal stops at 30); uniforms at buffer 0 in
each stage; fragment alpha-test reference at buffer 1; textures and samplers at the sampler's texture unit.

## Overlay API (`shims/include/rs_overlay.h`)

Exported by the executable. A mod registers a callback that runs after the game's frame and before
present; inside it only `rs_overlay_*` calls are allowed (draw an RGBA image / text into a rect). The GL
implementation uses only VBO + GLSL 1.20 + texture calls and saves/restores what it touches, so it runs
unchanged on the Metal core. Rules as X-Plane's: no `glGet`, no game resources, state restored.
