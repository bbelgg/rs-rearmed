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
             RS_GL_BACKEND=gl (default)          RS_GL_BACKEND=metal
             system OpenGL.framework             src/metal/: rsmtl_<name> implementations
                                                 src/msl/:   GLSL 1.20 -> MSL (offline corpus + runtime fallback)
```

- **One entry-point table.** `libRSOpenGL` wraps the union of the game's 107 imports
  (`game-gl-imports.txt`) and the extra functions the Cg shim, the shell and the overlay call
  (`extra-gl-imports.txt`). Everything goes through `rs_impl_<name>`. With `RS_GL_BACKEND=metal`
  `rs_impl_<name> = rsmtl_<name>`. The generator emits a weak `rsmtl_<name>` for every entry point that
  logs `unimplemented <name>` once and returns 0; a strong definition in `src/metal/` replaces it. The
  backend is chosen once, at load, and `rs_gfx_backend()` reports it.
- The Cg shim links `libRSOpenGL` instead of `OpenGL.framework`, so its calls take the same path. On Metal
  the Cg shim always uses its `glsl` backend: ARB programs never reach the Metal core.
- `CGL*` (game imports `CGLLockContext`/`CGLUnlockContext`) and `NSOpenGLContext` are only used on GL. On
  Metal the shell uses `rs_gfx_*` and `CGLLockContext` takes the context's mutex.

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
every shader seen in the game; keyed by a hash of both sources), then translates at run time and logs it
(`RS_MSL_LOG`). Binding conventions: vertex attributes `[[attribute(i)]]` from `[[stage_in]]`; vertex
buffers at buffer index 14 + attribute index (14..29: one buffer per enabled attribute, Metal stops at 30); uniforms at buffer 0 in
each stage; fragment alpha-test reference at buffer 1; textures and samplers at the sampler's texture unit.

## Overlay API (`shims/include/rs_overlay.h`)

Exported by the executable. A mod registers a callback that runs after the game's frame and before
present; inside it only `rs_overlay_*` calls are allowed (draw an RGBA image / text into a rect). The GL
implementation uses only VBO + GLSL 1.20 + texture calls and saves/restores what it touches, so it runs
unchanged on the Metal core. Rules as X-Plane's: no `glGet`, no game resources, state restored.
