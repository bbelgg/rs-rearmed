# Metal backend plan

A Metal renderer for RS Re-ARMed, as an alternative to Apple's OpenGL. Work happens on the `metal-backend`
branch; OpenGL stays the default until Metal matches it.

## Why

Apple's `OpenGL.framework` is already implemented on Metal (`AppleMetalOpenGLRenderer`) and covers everything
the game uses, so a Metal backend is not about speed:

- Gameplay CPU is already at or below the original under Rosetta, and the frame rate follows the display
  (`RS_FPS`). The multithreaded GL engine (`RS_GL_MTENGINE=1`) moves most of the driver's work off the frame
  thread and shortens it by only ~4%, which bounds what Metal can save.
- The reason is **insurance**: OpenGL has been deprecated since macOS 10.14, macOS 27 is the last release with
  full Rosetta, and there are unconfirmed reports that OpenGL may be removed. Nothing else implements legacy
  GL 2.1 with ARB programs on Metal (ANGLE is OpenGL ES only, MGL is GL 4.6 core and incomplete, Zink on
  MoltenVK is experimental), so if OpenGL goes, the port needs its own backend.
- Side benefits: Retina rendering (optionally MetalFX upscaling), Xcode's Metal frame capture and shader
  debugger, full control over presentation.

## What the game needs from GL

- One `NSOpenGLContext`, legacy 2.1 profile (no `NSOpenGLPFAOpenGLProfile`), 24-bit colour, 16-bit depth,
  8-bit stencil, double buffered. Created by the shell rewrite, `shims/platform/src/rsp_shell.m`
  (`OpenGLView`: display link, render thread, `drawView` → `flushBuffer`, `RS_FPS`).
- 107 GL/CGL imports (`shims/opengl/game-gl-imports.txt`); ~66 used in menus. Fixed-function leftovers:
  `glAlphaFunc` / `GL_ALPHA_TEST` every frame, `glPolygonMode`.
- Two renderers:
  - **Engine (Gamebryo) through Cg**: precompiled ARB vertex/fragment programs (`arbvp1`/`arbfp1`), driven by
    `shims/cg`. The default `arb` backend hands them to `GL_ARB_*_program`; the `glsl` backend translates them
    (`arb_glsl.cpp`) but sometimes never leaves the loading screen in the game.
  - **UI (Scaleform GFx 4.2 GL HAL)**: GLSL 1.20 shaders built at run time from fragments (213 seen in the
    menus); program-binary cache unused (Apple's GL reports 0 formats).
- DXT1/3/5 textures (`glCompressedTexImage2D`), BGRA, FBOs and renderbuffers, instancing entry points.
- **Mods**: rearmed-mods' text overlay draws on the game's context after the frame with immediate-mode GL
  (`glBegin`/`glEnd`, matrix stack, `glPushAttrib`, `glReadPixels`) and links `OpenGL.framework` itself.
- The app links the system `OpenGL.framework` directly (`tools/ws7/recomp/app/build.sh`); `libRSOpenGL`
  (`shims/opengl`, wrappers for the 107 imports) is built but not bundled.

## Lessons from other projects

| Project | Lesson |
|---|---|
| Dolphin, DuckStation | Native Metal beat MoltenVK; both kept the old path to compare against. |
| PCSX2 | Its 3x came from GL 4.1 limits (depth precision, texture barriers) we do not hit. Shipped with known accuracy gaps, fixed later. |
| Ship of Harkinian | Backend is a setting, with OpenGL as the crash fallback. |
| Blender, Godot 4.4 | Early Metal releases had machine-specific bugs or were slower at first; measure before changing defaults. Pick the minimum macOS early. |
| Dolphin (iOS fork), Apple (WWDC 2020) | Shader/pipeline compilation is the stutter source: compile ahead and keep a `MTLBinaryArchive`. Tune `CAMetalLayer` for frame pacing. |
| X-Plane 11.50 | Plugins could only draw with GL; it kept a hidden GL context with heavy synchronisation and dropped 3D plugin callbacks under Metal. Its rules: plugins don't `glGet`, restore state, don't hold the app's resources; overlays use a 2D callback. **Same problem as our mod overlay.** |
| ANGLE / WebKit, MGL | GL-on-Metal layers exist only for ES or core profile; MGL's GL-state → Metal-state mapping (Apache-2.0) is a useful reference. |

## Steps

Interfaces and the Metal core's object model: [METAL-DESIGN.md](METAL-DESIGN.md).


Each step is useful on its own and lands with tests. GL stays the default throughout.

1. **Route GL through `libRSOpenGL`.** Link the game (and the shell) against the shim instead of the system
   framework, bundle it, change nothing else. Check: `gl_native_test`, the Cg render tests, a game boot.
2. **Overlay API for mods.** A backend-neutral 2D call exported by the executable (e.g. draw a texture or
   RGBA image into a rect after the frame), implemented on GL first; rules as X-Plane's. Move rearmed-mods'
   overlay onto it (separate repo, separate release; keep the GL path working for old mod builds while GL is
   the backend).
3. **One shader path.** Fix the Cg `glsl` backend's loading-screen hang and give it the ARB backend's
   parameter caching (dirty-row uploads, handle cache). Then every shader the game draws with is GLSL.
4. **Offline shader translation.** GLSL 1.20 (both families) → MSL, via glslang + SPIRV-Cross or a direct
   translator from the ARB IR. Translate the whole corpus at build time; verify each against GL with the
   render-diff harness (`shims/cg/tests`). Runtime translation only as a fallback for shaders not in the
   corpus, logged.
5. **Metal core in `libRSOpenGL`** behind `RS_GL_BACKEND=metal`:
   - objects: buffers (versioned / ring for `glBufferSubData` on in-flight data), textures (DXT via BC formats,
     BGRA), FBOs and renderbuffers (D16S8 → `Depth32Float_Stencil8`);
   - state tracker → cached `MTLRenderPipelineState` (program, vertex layout, blend, attachment formats,
     alpha-test variant) and `MTLDepthStencilState`; encoders opened per FBO bind / clear;
   - uniforms from shadow storage per draw (`setVertexBytes` / `setFragmentBytes` or a ring buffer);
   - legacy: alpha test as a shader variant per function, `glPolygonMode` → `setTriangleFillMode`, clip-space
     z (`z = (z + w) / 2`) and Y-origin handling, `glGetTexImage` / `glReadPixels` via blit + wait;
   - `glGet*` answered from tracked state; a GL error model good enough for the game.
6. **Presentation.** `rsp_shell.m` gets a `CAMetalLayer` path (drawable count, `displaySyncEnabled`, Retina
   scale) with the render thread, display link and `RS_FPS` unchanged; `CGLLockContext` becomes a mutex.
7. **Caching.** Pipelines compiled ahead from the corpus and kept in a `MTLBinaryArchive` across runs.
8. **Verification and default switch.** `gl_native_test` on both backends with pixel comparison; scripted
   game runs with frame diffs against GL; `perf-song` CPU and frame times; overlay check. Metal becomes the
   default only at parity; GL remains selectable.

Optional, independent: try Retina on GL (`wantsBestResolutionOpenGLSurface`) to see what the game and the
overlay do at 2x.

## Working rules

- Build output and large temporary files go under `build/` (kept off the system disk).
- No in-game runs or anything that opens windows while the Mac is in use; offscreen tests are fine.
- Commit each finished piece; never push without asking.

## Status

| Step | State | Notes |
|---|---|---|
| 1 | done, game boot unchecked | App, shell (in the app) and Cg link `libRSOpenGL` (bundled as `Contents/MacOS/libRSOpenGL.dylib`); the image loader binds the game's OpenGL imports to it. Table = 107 game imports + `extra-gl-imports.txt`; `RS_GL_BACKEND=metal` routes all to weak `rsmtl_<name>` stubs; `rs_gfx` on GL (offscreen = CGL + FBO as framebuffer 0), test `opengl.gfx.offscreen.gl`. Checked: `gl_native_test` (no window), Cg tests, app relink, `otool -L`. Not checked: a game boot. |
| 2 | done, in-game check pending | `shims/include/rs_overlay.h` (version 1): register a callback that drawView runs after the frame, before `flushBuffer`; images (premultiplied RGBA), `draw_image`, `fill_rect`, `read_pixels`, top-left pixels. `rsp_overlay.c`: game-import GL calls only (plus `glGen/BindVertexArrayAPPLE` on Apple's legacy context, which rejects the core VAO calls), state read on first use per frame and restored. Test `platform.overlay.*` (offscreen, via libRSOpenGL: pixels, restored state, no GL errors); `platform.overlay.metal` disabled until step 5. rearmed-mods 0.2.0 (branch `overlay-api`) uses it when exported, old GL path otherwise; checked headless against `rsp_overlay.c`. Not checked: in the game. |
| 3 | done, in-game check pending | Hang cause: the game binds a Cg program only when it differs from the last one per stage and never disables/unbinds, while Scaleform ends its frames with `glUseProgram(0)`; the old glsl backend then drew the unchanged pair with fixed function (white) and dropped its uniform updates. Now every bind, changed parameter and stream setup re-takes GL (one `glGetIntegerv`) unless a program of the game's own is current. Also: pairs linked lazily (no (new vertex, old fragment) links), one vertex shader per program, no redundant `glUseProgram`, row-run uploads, GL objects per share group, deleted current program left first. `RS_GL_BACKEND=metal` forces glsl; default stays arb. Tests `cg.stress.{glsl,arb}` (game call order both ways, Scaleform in between, shared/unshared contexts, 6000 random corpus pairs: no GL errors), `--bench`. GLSL 1.20, no extensions: `RS_CG_DUMP_GLSL=<dir>`, `cg_corpus_load --dump-glsl <dir>` (`cg.corpus.glsl_dump`), corpus in `build/metal/corpus/cg-glsl/`. Render diff 785/785, one known (reference-side) diff. |
| 4 | not started | |
| 5 | not started | |
| 6 | done, in-game check pending | `rsp_present_metal.m` / `rsp_present.h`, runtime branch in `rsp_shell.m` when `rs_gfx_backend()` is Metal (rs_gfx looked up with `dlsym`, so the shell still builds against the system framework). Layer-hosting view with a `CAMetalLayer` (BGRA8Unorm, framebufferOnly); drawable = view size in points (1x, as GL), `RS_METAL_RETINA=1` backing pixels; the overlay gets the drawable size. Swap interval (`RS_FPS`, no_vsync) → `displaySyncEnabled` + `maximumDrawableCount` 3 with sync / 2 without (`RS_METAL_DRAWABLES` overrides), applied at the next present. `makeCurrentContext` / `CGLLock` / `update` / `flushBuffer` → `rs_gfx_make_current` / `lock` / `resize` / `present`; display link on the window's display (`CVDisplayLinkSetCurrentCGDisplay`, follows screen changes). The game's `gl_context` hook and `rsp_g_gl_context` get nil (the game only copies g_glContext to a global nothing reads). Frame times: the shell emits F/D/V itself on Metal. `rs_gfx_create_layer` NULL → abort with a message, no GL fallback. Test `platform.present.gl.*` on; `platform.present.metal` (unattached layer: settings, lock, 30 clears + readback + present, swap toggle, resize) disabled until the core is in the build; passes against its working tree. Checked: app relink, root and standalone platform builds. Not checked: a window, the game. |
| 7 | not started | |
| 8 | not started | |
