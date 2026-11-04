// Direct3D 9 shader bytecode (vs_2_0 / vs_3_0 / ps_2_0 / ps_3_0) -> GLSL 1.20, the dialect
// libRSOpenGL's GLSL -> MSL translator takes (docs/windows/DESIGN.md "Direct3D 9").
//
// What the generated GLSL expects from the device (d3d9_device.c):
//   attributes   vertex input register vN is `attribute vec4 rs_vN`, bound to location N
//   constants    `uniform vec4 vc[]` (vertex) / `pc[]` (pixel): float constants c0..; integer
//                and boolean constants a shader does not define itself are `uniform int
//                rs_vi[16]` / `rs_pi[16]` (the x of each iN) and `rs_vb[16]` / `rs_pb[16]`
//   samplers     sN is `uniform sampler2D sN` (samplerCube for a cube dcl), unit N
//   varyings     linked by D3D semantic: `varying vec4 rs_<usage><index>` (rs_texcoord0,
//                rs_color1, ...); a vertex shader is generated for the pixel shader it is
//                linked with, so it writes exactly the varyings that one reads
//   position     D3D -> GL in the vertex epilogue with `uniform vec4 rs_fix`:
//                x += fix.x * w; y = -y + fix.y * w; z = z * fix.z + fix.w * w
//                fix = (1/vpW, 1/vpH, 2 (MaxZ - MinZ), 2 MinZ - 1): the half-pixel offset, the
//                flip that keeps every render target top row first (as D3D addresses it), and
//                the viewport depth range
// Anything the translator does not implement fails the shader with a message (the draw is then
// skipped and logged), never silently wrong code.
#ifndef D3D9_SHADER_H
#define D3D9_SHADER_H

#include <stddef.h>
#include <stdint.h>

typedef struct d3d9_sm d3d9_sm;

typedef struct {
    int used;
    int usage, index;  // D3DDECLUSAGE_*, usage index
} d3d9_sm_input;

typedef struct {
    int is_ps;
    int major, minor;
    uint32_t ntokens;           // length of the bytecode in DWORDs, end token included
    int nconst;                 // float constants the GLSL declares (array size; 0 = none)
    int uses_int, uses_bool;    // rs_?i / rs_?b uniforms declared
    d3d9_sm_input input[16];    // vertex shader inputs by register
    int sampler_type[16];       // 0 none, 2 2D, 3 cube, 4 volume
} d3d9_sm_info;

// Parse; 0 with a message in err on failure. The tokens are copied.
d3d9_sm *d3d9_sm_parse(const uint32_t *tok, uint32_t maxtok, char *err, size_t errlen);
void d3d9_sm_free(d3d9_sm *sm);
const d3d9_sm_info *d3d9_sm_get_info(const d3d9_sm *sm);
const uint32_t *d3d9_sm_tokens(const d3d9_sm *sm);

// GLSL source (malloc'd) or 0 with a message. For a vertex shader, `ps` is the pixel shader it
// will be linked with (0: emit a varying for every output it writes).
char *d3d9_sm_glsl(const d3d9_sm *sm, const d3d9_sm *ps, char *err, size_t errlen);

// A pixel shader's inputs as (usage, index) semantics, in declaration order; returns the count.
int d3d9_sm_ps_semantics(const d3d9_sm *ps, d3d9_sm_input *out, int max);
// The varying a semantic travels in: "rs_<usage><index>" (rs_texcoord0, rs_color1, ...).
const char *d3d9_sm_varying(int usage, int index, char *o, size_t n);

#endif
