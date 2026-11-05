// The fixed-function pipeline's program generator (d3d9_ffp.c has the device state and the uniforms):
// GLSL from a state key and nothing else, so the install's shader corpus (win-x86/shaders) makes the same
// text the game's draws do.
#ifndef D3D9_FFP_GEN_H
#define D3D9_FFP_GEN_H

#include <stddef.h>
#include <stdint.h>

#include "d3d9_shader.h"

// --- vertex uniform layout (vec4s) -----------------------------------------------------------
enum {
    FFV_WVP = 0,        // columns of world * view * projection
    FFV_WV = 4,         // columns of world * view (eye-space position)
    FFV_NRM = 8,        // rows of (world * view)^-1, 3x3: eye-space normal (inverse transpose)
    FFV_VIEWPORT = 11,  // X Y W H (pre-transformed positions)
    FFV_MAT = 12,       // material diffuse, ambient, specular, emissive, (power)
    FFV_AMBIENT = 17,   // RS_AMBIENT
    FFV_LIGHT = 18,     // 8 x: diffuse, specular, ambient, eye position (w range), eye direction
                        // towards the light, attenuation (a0 a1 a2 falloff), spot (cos theta/2, cos phi/2)
    FFV_FOG = 74,       // start, end, density, 1 / (end - start)
    FFV_TEXM = 75,      // 8 x columns of the texture matrix
    FFV_COUNT = 107,
};
enum { FFP_TFACTOR = 0, FFP_FOGCOLOR = 1, FFP_FOG = 2, FFP_CONST = 3, FFP_COUNT = 11 };
enum { LIGHT_VEC4S = 7 };

// varyings (need / avail masks)
enum { VY_COLOR0 = 1, VY_COLOR1 = 2, VY_TEX0 = 4, VY_FOG = 1 << 10 };
// FF vertex attributes: the register each input semantic is bound to
enum { A_POS = 0, A_NORMAL = 1, A_COLOR0 = 2, A_COLOR1 = 3, A_TEX0 = 4 };

// --- the state key ---------------------------------------------------------------------------
typedef struct {
    uint8_t tci, gen, count, proj;  // texture coordinate set, generation, transform count, projected
} ffp_tc;

typedef struct {
    uint8_t cop, ca0, ca1, ca2, aop, aa0, aa1, aa2, result, tex;  // tex: 0 none, 2 2D, 3 cube
} ffp_stage;

typedef struct {
    uint32_t vs_id, ps_id;  // the bound shaders (0: that stage is fixed function)
    // fixed-function vertex stage
    uint8_t pos_t;          // pre-transformed (XYZRHW)
    uint8_t normal, color0, color1;
    uint8_t tc_dims[8];     // texture coordinate sets in the declaration: components, 0 absent
    uint8_t lighting, nlights, light_type[8];
    uint8_t mat_src[4];     // diffuse, ambient, specular, emissive: 0 material, 1 colour 0, 2 colour 1
    uint8_t specular, localviewer, normalize;
    uint8_t range_fog;
    ffp_tc tc[8];           // per output texture coordinate
    uint16_t need;          // varyings the pixel stage reads (VY_*)
    // fog: 0 none, 1 factor in rs_fog0.x (vertex fog, or specular alpha), 2..4 table linear / exp /
    // exp2 from the distance in rs_fog0.x
    uint8_t fog;
    uint8_t vfog;           // vertex fog mode for the factor (1 exp, 2 exp2, 3 linear; 0: specular alpha)
    // fixed-function pixel stage
    uint8_t nstages;
    ffp_stage st[8];
} ffp_key;

void d3d9_log_once(const char *fmt, ...);  // d3d9_main.c
extern const char d3d9_blit_vs[], d3d9_blit_fs[];  // the layer's own blit program

// The varying a semantic reads (VY_*), 0 for none.
int d3d9_ffp_vy_of(int usage, int index);
// Both stages' GLSL (malloc'd) of the program for key k: vs and ps are the bound shaders (0: that stage is
// fixed function), ps_glsl the pixel shader's GLSL. 0 with a message in err on failure.
int d3d9_ffp_sources(const ffp_key *k, const d3d9_sm *vs, const d3d9_sm *ps, const char *ps_glsl, char **vsrc, char **fsrc,
                     char *err, size_t errlen);

#endif
