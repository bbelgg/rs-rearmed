// GLSL 1.20 (Scaleform GFx and the Cg shim's glsl backend) -> Metal Shading
// Language, for the Metal core (docs/METAL-DESIGN.md "Shader translation").
//
// Binding conventions of the generated MSL:
//   entry points        rs_vs (vertex), rs_fs (fragment), one library per program
//   attributes          [[stage_in]] struct, attribute i at [[attribute(i)]]
//                       (i = the GL attribute location; built-ins use NVIDIA aliasing:
//                       gl_Vertex 0, gl_Normal 2, gl_Color 3, gl_MultiTexCoordN 8+N)
//   uniforms            one struct per stage at [[buffer(0)]], layout in rsmsl_uniform
//   alpha test          fragment [[function_constant(0)]] uint (GL func - GL_NEVER, 8 = off),
//                       reference float at fragment [[buffer(1)]]
//   samplers            texture + sampler at index = the sampler uniform's unit, bound by
//                       the core from the uniform's value (rsmsl_sampler.index is the slot)
//   epilogue            gl_Position: y = -y, z = (z + w) / 2
#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum rsmsl_type {  // GL type enums, kept numeric so the core can compare with glUniform calls
    RSMSL_FLOAT = 0x1406, RSMSL_VEC2 = 0x8B50, RSMSL_VEC3 = 0x8B51, RSMSL_VEC4 = 0x8B52,
    RSMSL_INT = 0x1404, RSMSL_IVEC2 = 0x8B53, RSMSL_IVEC3 = 0x8B54, RSMSL_IVEC4 = 0x8B55,
    RSMSL_BOOL = 0x8B56, RSMSL_MAT2 = 0x8B5A, RSMSL_MAT3 = 0x8B5B, RSMSL_MAT4 = 0x8B5C,
    RSMSL_SAMPLER_2D = 0x8B5E, RSMSL_SAMPLER_CUBE = 0x8B60,
};

enum { RSMSL_STAGE_VS = 1, RSMSL_STAGE_FS = 2 };

typedef struct {
    const char *name;    // as glGetAttribLocation sees it
    int location;        // -1: unassigned, the core binds it (glBindAttribLocation or next free)
    int type;            // rsmsl_type
} rsmsl_attrib;

typedef struct {
    const char *name;    // as glGetUniformLocation sees it, arrays without "[0]"
    int type;            // rsmsl_type
    int array_size;      // 1 for non-arrays
    int stages;          // RSMSL_STAGE_* mask
    int vs_offset;       // byte offset in the vertex uniform block (-1 if unused there)
    int fs_offset;       // same for the fragment block
    int stride;          // bytes between array elements (MSL layout: vec3 is 16, mat4 64)
} rsmsl_uniform;

typedef struct {
    const char *name;    // the sampler uniform (also listed in uniforms, no block storage)
    int type;            // RSMSL_SAMPLER_*
    int stages;
    int index;           // texture/sampler slot in the MSL; the core binds unit = uniform value
} rsmsl_sampler;

typedef struct rsmsl_program {
    char *msl;                      // complete MSL source
    rsmsl_attrib *attribs;   int n_attribs;
    rsmsl_uniform *uniforms; int n_uniforms;
    rsmsl_sampler *samplers; int n_samplers;
    int vs_uniform_size, fs_uniform_size;  // bytes, 16-aligned
    int from_corpus;                // 1: found in the offline corpus
} rsmsl_program;

// Translate one vertex + fragment pair (as passed to glShaderSource, joined).
// `bound_attribs` lists glBindAttribLocation calls made before link (may be NULL).
// Returns NULL and sets *error (malloc'd) on failure.
rsmsl_program *rsmsl_translate(const char *vs_glsl, const char *fs_glsl,
                               const rsmsl_attrib *bound_attribs, int n_bound, char **error);
void rsmsl_free(rsmsl_program *p);

// Key of a pair in the offline corpus: a 64-bit hash of both sources (the corpus also checks a
// second 64-bit hash and the lengths). Stable across runs and builds; it changes only with the
// corpus format (RSMSL_VERSION).
uint64_t rsmsl_key(const char *vs_glsl, const char *fs_glsl);

#ifdef __cplusplus
}
#endif
