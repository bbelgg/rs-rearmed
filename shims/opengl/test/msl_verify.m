// Render-diff harness for the GLSL -> MSL translator (src/msl/): every program is drawn once with
// the system OpenGL (offscreen CGL context + FBO, the GLSL as is) and once with Metal (offscreen
// texture, the translated MSL, a minimal Metal setup), with the same deterministic attributes,
// uniforms and textures; the read-backs are compared pixel by pixel. Headless: no windows.
//
//   msl_verify --builtin [options]          hand-written programs (msl_cases.h)
//   msl_verify [options] <pairs.tsv>        lines "<vs file>\t<fs file>[\t<name>=<loc>,...]"
//
// Options:
//   --corpus-out <file>   write the offline corpus (every pair that translated and compiled;
//                         flag RSMSL_CORPUS_VERIFIED on the ones that matched GL)
//   --report <file>       one line per pair: status, max colour error (1/255 steps), depth error,
//                         coverage (best variant), pixels over the tolerance, vertex probe relative
//                         error (-1: not captured), files, error
//                         (status VDIFF: pixels match but a vertex output differs)
//   --tol <n>             allowed colour error per channel, in 1/255 steps (default 2)
//   --fast-math           compile the MSL with fast math (default: safe math, which is what the
//                         Metal core must use; see docs/METAL-DESIGN.md)
//   --dump <dir>          for failures: the MSL and GL / Metal / diff PNGs
//   --limit <n>           first n pairs only
//   --outliers <n>        status NEAR instead of DIFF when at most n pixels (over all variants) exceed
//                         the tolerance, none by more than 32/255, and the vertex outputs match
//                         (default 8 of 3 x 4096)
//   --check-corpus <file> only check that rsmsl_translate finds every pair in this corpus with the
//                         MSL a fresh translation gives (no rendering)
//   --verbose             print the first differing pixels of each variant
//
// Conventions checked (msl_xlat.h): row 0 of the Metal target is GL's bottom row, so glReadPixels
// rows compare directly with Metal texture rows; winding is inverted (front face clockwise);
// attribute i -> vertex buffer VB_BASE + i; uniforms at buffer 0 per stage with the reflected offsets;
// alpha test via function constant 0 and fragment buffer 1; samplers at their slot, bound from the
// texture unit in the sampler uniform.
//
// Exit status: 0 if every pair passed (PASS or NEAR), 1 otherwise, 77 when the pairs file has no usable entries
// (ctest SKIP_RETURN_CODE).
#define GL_SILENCE_DEPRECATION
#import <Foundation/Foundation.h>
#import <ImageIO/ImageIO.h>
#import <Metal/Metal.h>
#import <OpenGL/OpenGL.h>
#import <OpenGL/gl.h>
#import <OpenGL/glext.h>
#include <ctype.h>
#include <math.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/msl/msl_internal.h"
#include "msl_cases.h"

#define W 64
#define H 64
#define GRID 8
#define NVERT (GRID * GRID * 6)
#define TEX 32
// Vertex buffer of attribute i. docs/METAL-DESIGN.md says 16 + i, but Metal's last vertex buffer
// index is 30 and the Cg shim uses location 15, so the base must be <= 15 (here 14).
#define VB_BASE 14

static int g_tol = 2;
static int g_verbose;
// Depth is compared as float; a gl_FragDepth computed from filtered texture values differs by up
// to ~1.5e-4 (texture filtering precision, invisible in 8-bit colour).
static const float g_depth_tol = 5e-4f;
static int g_outliers = 8;  // pixels over the tolerance (all variants) still reported as NEAR  // --verbose: print the first differing pixels
static BOOL g_fast_math = NO;
static const char *g_dump;
static id<MTLDevice> g_dev;
static id<MTLCommandQueue> g_queue;

// --- Deterministic data ------------------------------------------------------------------------

static uint32_t hash32(uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    uint64_t x = ((uint64_t)a * 0x9E3779B97F4A7C15ull) ^ ((uint64_t)b << 32 | c) ^ ((uint64_t)d * 0xC2B2AE3D27D4EB4Full);
    x ^= x >> 33; x *= 0xff51afd7ed558ccdull; x ^= x >> 33; x *= 0xc4ceb9fe1a85ec53ull; x ^= x >> 33;
    return (uint32_t)x;
}
static float unit(uint32_t h) { return (h >> 8) * (1.0f / 16777216.0f); }
static uint32_t strhash(const char *s) {
    uint32_t h = 2166136261u;
    while (*s) h = (h ^ (unsigned char)*s++) * 16777619u;
    return h;
}

// Vertex attribute value: grid position for the position attribute, smooth values in [0,1] otherwise
// ("index" attributes, used to index uniform arrays, get small values so indices stay in range).
static void attrib_value(int loc, int is_pos, int is_index, int gx, int gy, uint32_t seed, float out[4]) {
    if (is_pos) {
        float jx = (unit(hash32(seed, 1, gx, gy)) - 0.5f) * 0.11f, jy = (unit(hash32(seed, 2, gx, gy)) - 0.5f) * 0.11f;
        if (gx == 0 || gx == GRID) jx = 0;
        if (gy == 0 || gy == GRID) jy = 0;
        out[0] = -1.0f + 2.0f * gx / GRID + jx * 2.0f / GRID;
        out[1] = -1.0f + 2.0f * gy / GRID + jy * 2.0f / GRID;
        out[2] = (unit(hash32(seed, 3, gx, gy)) - 0.5f) * 1.6f;
        out[3] = 1.0f;
        return;
    }
    for (int c = 0; c < 4; c++) {
        if (is_index) {
            out[c] = (float)((gx + gy + c) % 2) / 255.0f;
        } else {
            // smooth: bilinear between coarse random values
            float u = (float)gx / GRID, v = (float)gy / GRID;
            float a = unit(hash32(seed, 10 + loc, c, 0)), b = unit(hash32(seed, 10 + loc, c, 1));
            float cc = unit(hash32(seed, 10 + loc, c, 2)), d = unit(hash32(seed, 10 + loc, c, 3));
            out[c] = (a * (1 - u) + b * u) * (1 - v) + (cc * (1 - u) + d * u) * v;
            out[c] = out[c] * 0.8f + unit(hash32(seed, 20 + loc, gx * 31 + gy, c)) * 0.2f;
        }
    }
    if (loc == 4) out[3] = 1.0f;  // gl_SecondaryColor has 3 components in GL
}

// Uniform component value: floats in [-1,-0.25] or [0.25,1], ints in [0,3], bools 0/1.
// Variant with matrices near identity and vec4s with a positive w (keeps transformed geometry on screen).
static int g_near_identity;
static float uniform_float(const char *name, int elem, int comp, uint32_t seed, int rows, int cols) {
    // Magnitude 0.25..1, random sign: no near-zero divisors (1/x, normalize) that would turn
    // last-bit differences into visible ones.
    float r = unit(hash32(seed, strhash(name), elem, comp)) * 2.0f - 1.0f;
    r = r < 0 ? r * 0.75f - 0.25f : r * 0.75f + 0.25f;
    if (g_near_identity && cols > 1) return (comp / rows == comp % rows ? 1.0f : 0.0f) + 0.25f * r;
    if (g_near_identity && rows == 4) return comp == 3 ? 0.75f + 0.25f * r : 0.5f * r;  // vec4 rows: positive w
    return r;
}
static int uniform_int(const char *name, int elem, int comp, uint32_t seed) {
    return (int)(hash32(seed, strhash(name), elem, comp + 100) % 4);
}

static void texel(int unit_, int face, int x, int y, unsigned char out[4]) {
    uint32_t h = hash32(77, unit_ * 8 + face, x, y);
    out[0] = (unsigned char)(x * 255 / (TEX - 1));
    out[1] = (unsigned char)(y * 255 / (TEX - 1));
    out[2] = (unsigned char)(h & 255);
    out[3] = (unsigned char)(128 + ((h >> 8) & 127) - unit_ * 9 - face * 5);
}

// --- Matrices for gl_ built-in uniforms -----------------------------------------------------------

static void mat_mul(const float *a, const float *b, float *r) {  // column-major r = a * b
    float t[16];
    for (int c = 0; c < 4; c++)
        for (int rr = 0; rr < 4; rr++) {
            float s = 0;
            for (int k = 0; k < 4; k++) s += a[k * 4 + rr] * b[c * 4 + k];
            t[c * 4 + rr] = s;
        }
    memcpy(r, t, sizeof t);
}
static void mat_transpose(const float *a, float *r) {
    float t[16];
    for (int c = 0; c < 4; c++)
        for (int rr = 0; rr < 4; rr++) t[rr * 4 + c] = a[c * 4 + rr];
    memcpy(r, t, sizeof t);
}
static int mat_inverse(const float *m, float *out) {
    float inv[16];
    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
    float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    if (det == 0) return 0;
    for (int i = 0; i < 16; i++) out[i] = inv[i] / det;
    return 1;
}
static const float kModelView[16] = {0.8f, -0.1f, 0.05f, 0, 0.1f, 0.8f, 0.0f, 0, 0.0f, 0.1f, 0.9f, 0, 0.05f, -0.02f, 0.1f, 1};
static const float kProjection[16] = {1.1f, 0, 0, 0, 0, 0.95f, 0, 0, 0, 0, -0.9f, -0.15f, 0, 0, -0.05f, 1.0f};
static void texture_matrix(int i, float *m) {
    memset(m, 0, 64);
    for (int k = 0; k < 4; k++) m[k * 5] = 1.0f - 0.1f * i;
    m[12] = 0.05f * i;
    m[13] = -0.03f * i;
}
// Value of a gl_ matrix uniform (16 floats, column-major; gl_NormalMatrix: 9).
static int builtin_matrix(const char *name, int elem, float *out) {
    float base[16];
    char root[64];
    snprintf(root, sizeof root, "%s", name);
    const char *suffix = "";
    static const char *sufs[] = {"InverseTranspose", "Inverse", "Transpose"};
    for (int i = 0; i < 3; i++) {
        size_t n = strlen(root), m = strlen(sufs[i]);
        if (n > m && !strcmp(root + n - m, sufs[i])) { suffix = sufs[i]; root[n - m] = 0; break; }
    }
    if (!strcmp(root, "gl_NormalMatrix")) {
        float inv[16];
        mat_inverse(kModelView, inv);
        // transpose(inverse(mat3(MV))) == transpose(mat3(inverse(MV))) for an affine MV
        for (int c = 0; c < 3; c++)
            for (int r = 0; r < 3; r++) out[c * 3 + r] = inv[r * 4 + c];
        return 9;
    }
    if (!strcmp(root, "gl_ModelViewMatrix")) memcpy(base, kModelView, 64);
    else if (!strcmp(root, "gl_ProjectionMatrix")) memcpy(base, kProjection, 64);
    else if (!strcmp(root, "gl_ModelViewProjectionMatrix")) mat_mul(kProjection, kModelView, base);
    else if (!strcmp(root, "gl_TextureMatrix")) texture_matrix(elem, base);
    else return 0;
    if (strstr(suffix, "Inverse")) mat_inverse(base, base);
    if (strstr(suffix, "Transpose")) mat_transpose(base, base);
    memcpy(out, base, 64);
    return 16;
}

// --- Type helpers -------------------------------------------------------------------------------

typedef struct { int base, rows, cols; } tinfo;  // base: 0 float, 1 int, 2 bool, 3 sampler
static tinfo type_info(int t) {
    switch (t) {
    case 0x1406: return (tinfo){0, 1, 1};
    case 0x8B50: return (tinfo){0, 2, 1};
    case 0x8B51: return (tinfo){0, 3, 1};
    case 0x8B52: return (tinfo){0, 4, 1};
    case 0x1404: return (tinfo){1, 1, 1};
    case 0x8B53: return (tinfo){1, 2, 1};
    case 0x8B54: return (tinfo){1, 3, 1};
    case 0x8B55: return (tinfo){1, 4, 1};
    case 0x8B56: return (tinfo){2, 1, 1};
    case 0x8B57: return (tinfo){2, 2, 1};
    case 0x8B58: return (tinfo){2, 3, 1};
    case 0x8B59: return (tinfo){2, 4, 1};
    case 0x8B5A: return (tinfo){0, 2, 2};
    case 0x8B5B: return (tinfo){0, 3, 3};
    case 0x8B5C: return (tinfo){0, 4, 4};
    case 0x8B65: return (tinfo){0, 3, 2};  // mat2x3: 2 columns of 3
    case 0x8B66: return (tinfo){0, 4, 2};
    case 0x8B67: return (tinfo){0, 2, 3};
    case 0x8B68: return (tinfo){0, 4, 3};
    case 0x8B69: return (tinfo){0, 2, 4};
    case 0x8B6A: return (tinfo){0, 3, 4};
    default: return (tinfo){3, 1, 1};
    }
}

// --- GL -----------------------------------------------------------------------------------------

static CGLContextObj g_gl;
static GLuint g_fbo, g_rb_color, g_rb_depth, g_gltex[16];
static GLenum g_gltex_target[16];

static int gl_init(void) {
    CGLPixelFormatAttribute attrs[] = {kCGLPFAAccelerated, kCGLPFAColorSize, 24, kCGLPFADepthSize, 24, 0};
    CGLPixelFormatObj pf;
    GLint npf;
    if (CGLChoosePixelFormat(attrs, &pf, &npf) || !pf || CGLCreateContext(pf, NULL, &g_gl) || CGLSetCurrentContext(g_gl))
        return 0;
    CGLDestroyPixelFormat(pf);
    glGenFramebuffers(1, &g_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
    glGenRenderbuffers(1, &g_rb_color);
    glBindRenderbuffer(GL_RENDERBUFFER, g_rb_color);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, W, H);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, g_rb_color);
    glGenRenderbuffers(1, &g_rb_depth);
    glBindRenderbuffer(GL_RENDERBUFFER, g_rb_depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, W, H);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, g_rb_depth);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) return 0;
    glGenTextures(16, g_gltex);
    // Metal always filters cube maps across faces; compare like with like (the translation, not
    // the seam behaviour; see docs/METAL-DESIGN.md).
    glEnable(0x884F /* GL_TEXTURE_CUBE_MAP_SEAMLESS */);
    if (glGetError() != GL_NO_ERROR) fprintf(stderr, "note: GL_TEXTURE_CUBE_MAP_SEAMLESS unavailable, cube seams will differ\n");
    return 1;
}

static GLuint gl_compile(GLenum kind, const char *src, char *log, size_t nlog) {
    GLuint s = glCreateShader(kind);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        glGetShaderInfoLog(s, (GLsizei)nlog, NULL, log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

// Texture of the type a sampler needs, on `unit`, identical in GL and Metal.
static void gl_texture(int unit_, int type) {
    GLenum target = type == 0x8B60 ? GL_TEXTURE_CUBE_MAP : type == 0x8B5D ? GL_TEXTURE_1D
                  : type == 0x8B63 ? GL_TEXTURE_RECTANGLE_ARB : type == 0x8B5F ? GL_TEXTURE_3D : GL_TEXTURE_2D;
    glActiveTexture(GL_TEXTURE0 + unit_);
    if (g_gltex_target[unit_] && g_gltex_target[unit_] != target) {
        // a texture object's target is fixed by its first bind
        glBindTexture(g_gltex_target[unit_], 0);
        glDeleteTextures(1, &g_gltex[unit_]);
        glGenTextures(1, &g_gltex[unit_]);
    }
    glBindTexture(target, g_gltex[unit_]);
    g_gltex_target[unit_] = target;
    unsigned char *px = malloc(TEX * TEX * TEX * 4);
    int faces = target == GL_TEXTURE_CUBE_MAP ? 6 : 1, depth = target == GL_TEXTURE_3D ? 4 : 1;
    for (int f = 0; f < faces; f++) {
        for (int z = 0; z < depth; z++)
            for (int y = 0; y < TEX; y++)
                for (int x = 0; x < TEX; x++) texel(unit_, f + z, x, y, px + ((z * TEX + y) * TEX + x) * 4);
        if (target == GL_TEXTURE_CUBE_MAP)
            glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + f, 0, GL_RGBA8, TEX, TEX, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
        else if (target == GL_TEXTURE_1D)
            glTexImage1D(target, 0, GL_RGBA8, TEX, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
        else if (target == GL_TEXTURE_3D)
            glTexImage3D(target, 0, GL_RGBA8, TEX, TEX, depth, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
        else
            glTexImage2D(target, 0, GL_RGBA8, TEX, TEX, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    }
    free(px);
    GLenum wrap = target == GL_TEXTURE_RECTANGLE_ARB || target == GL_TEXTURE_CUBE_MAP ? GL_CLAMP_TO_EDGE : GL_REPEAT;
    glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(target, GL_TEXTURE_WRAP_S, wrap);
    glTexParameteri(target, GL_TEXTURE_WRAP_T, wrap);
    if (target == GL_TEXTURE_3D || target == GL_TEXTURE_CUBE_MAP) glTexParameteri(target, GL_TEXTURE_WRAP_R, wrap);
    if (glGetError() != GL_NO_ERROR) fprintf(stderr, "GL error setting up texture unit %d target %#x\n", unit_, target);
}

// --- Per-pair inputs ------------------------------------------------------------------------------

typedef struct {
    const rsmsl_program *p;
    uint32_t seed;
    int alpha_func;   // GL func - GL_NEVER; 8 = off
    float alpha_ref;
    unsigned index_attribs;  // locations used to index uniform arrays
    int pos_loc;             // location that gets the grid positions
    float verts[16][NVERT][4];
} inputs;

static void build_vertices(inputs *in) {
    for (int a = 0; a < in->p->n_attribs; a++) {
        int loc = in->p->attribs[a].location;
        int k = 0;
        for (int gy = 0; gy < GRID; gy++)
            for (int gx = 0; gx < GRID; gx++) {
                // Two triangles; the second is wound the other way so both faces are drawn.
                static const int quad[6][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 0}, {0, 1}, {1, 1}};
                for (int v = 0; v < 6; v++)
                    attrib_value(loc, loc == in->pos_loc, (in->index_attribs >> loc) & 1, gx + quad[v][0], gy + quad[v][1], in->seed, in->verts[loc][k++]);
            }
    }
}

// Writes the value of every element of uniform u into a Metal block at `off`.
static void fill_block(const rsmsl_uniform *u, unsigned char *blk, int off, uint32_t seed) {
    tinfo ti = type_info(u->type);
    if (ti.base == 3) return;
    for (int e = 0; e < u->array_size; e++) {
        unsigned char *d = blk + off + e * u->stride;
        if (!strncmp(u->name, "gl_", 3)) {
            float m[16];
            int n = builtin_matrix(u->name, e, m);
            if (n == 9) for (int c = 0; c < 3; c++) memcpy(d + c * 16, m + c * 3, 12);
            else if (n == 16) memcpy(d, m, 64);
            else *(float *)d = 1.0f;  // gl_NormalScale
            continue;
        }
        int colstride = ti.cols > 1 ? (ti.rows == 2 ? 8 : 16) : 0;
        for (int c = 0; c < ti.cols; c++)
            for (int r = 0; r < ti.rows; r++) {
                int comp = c * ti.rows + r;
                unsigned char *q = d + c * colstride + r * 4;
                if (ti.base == 0) { float v = uniform_float(u->name, e, comp, seed, ti.rows, ti.cols); memcpy(q, &v, 4); }
                else if (ti.base == 1) { int v = uniform_int(u->name, e, comp, seed); memcpy(q, &v, 4); }
                else { int v = uniform_int(u->name, e, comp, seed) & 1; memcpy(q, &v, 4); }
            }
    }
}

static void gl_set_uniforms(GLuint prog, const rsmsl_program *p, uint32_t seed) {
    for (int i = 0; i < p->n_uniforms; i++) {
        const rsmsl_uniform *u = &p->uniforms[i];
        if (!strncmp(u->name, "gl_", 3)) continue;  // matrix state, set below
        tinfo ti = type_info(u->type);
        for (int e = 0; e < u->array_size; e++) {
            char nm[256];
            if (u->array_size > 1) snprintf(nm, sizeof nm, "%s[%d]", u->name, e);
            else snprintf(nm, sizeof nm, "%s", u->name);
            GLint loc = glGetUniformLocation(prog, nm);
            if (loc < 0) continue;
            if (ti.base == 3) continue;  // samplers below
            int n = ti.rows * ti.cols;
            float f[16];
            int iv[4];
            for (int k = 0; k < n; k++) {
                f[k] = uniform_float(u->name, e, k, seed, ti.rows, ti.cols);
                if (k < 4) iv[k] = ti.base == 1 ? uniform_int(u->name, e, k, seed) : uniform_int(u->name, e, k, seed) & 1;
            }
            if (ti.base != 0) {
                if (n == 1) glUniform1iv(loc, 1, iv);
                else if (n == 2) glUniform2iv(loc, 1, iv);
                else if (n == 3) glUniform3iv(loc, 1, iv);
                else glUniform4iv(loc, 1, iv);
            } else if (ti.cols == 1) {
                if (n == 1) glUniform1fv(loc, 1, f);
                else if (n == 2) glUniform2fv(loc, 1, f);
                else if (n == 3) glUniform3fv(loc, 1, f);
                else glUniform4fv(loc, 1, f);
            } else {
                switch (u->type) {
                case 0x8B5A: glUniformMatrix2fv(loc, 1, GL_FALSE, f); break;
                case 0x8B5B: glUniformMatrix3fv(loc, 1, GL_FALSE, f); break;
                case 0x8B5C: glUniformMatrix4fv(loc, 1, GL_FALSE, f); break;
                case 0x8B65: glUniformMatrix2x3fv(loc, 1, GL_FALSE, f); break;
                case 0x8B66: glUniformMatrix2x4fv(loc, 1, GL_FALSE, f); break;
                case 0x8B67: glUniformMatrix3x2fv(loc, 1, GL_FALSE, f); break;
                case 0x8B68: glUniformMatrix3x4fv(loc, 1, GL_FALSE, f); break;
                case 0x8B69: glUniformMatrix4x2fv(loc, 1, GL_FALSE, f); break;
                case 0x8B6A: glUniformMatrix4x3fv(loc, 1, GL_FALSE, f); break;
                }
            }
        }
    }
    // Sampler units = the translator's slots (the core binds unit u's texture at the slot whose
    // sampler uniform holds u; using unit == slot keeps the harness simple).
    for (int i = 0; i < p->n_samplers; i++) {
        GLint loc = glGetUniformLocation(prog, p->samplers[i].name);
        if (loc >= 0) glUniform1i(loc, p->samplers[i].index);
        gl_texture(p->samplers[i].index, p->samplers[i].type);
    }
    // Fixed-function matrix state for gl_ uniforms.
    glMatrixMode(GL_MODELVIEW);
    glLoadMatrixf(kModelView);
    glMatrixMode(GL_PROJECTION);
    glLoadMatrixf(kProjection);
    for (int t = 0; t < 8; t++) {
        float m[16];
        texture_matrix(t, m);
        glActiveTexture(GL_TEXTURE0 + t);
        glMatrixMode(GL_TEXTURE);
        glLoadMatrixf(m);
    }
    glMatrixMode(GL_MODELVIEW);
}

typedef struct {
    unsigned char color[W * H * 4];
    float depth[W * H];
} image;

// probe_buf != 0: capture the vertex outputs with transform feedback instead of drawing.
// Programs whose vertex shader writes gl_PointSize are drawn as point sprites (one per vertex),
// the rest as triangles.
static int draws_points(const rsmsl_program *p) { return strstr(p->msl, "[[point_size]]") != NULL; }

static int gl_render(GLuint prog, inputs *in, image *out, GLuint probe_buf) {
    const rsmsl_program *p = in->p;
    glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
    glViewport(0, 0, W, H);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_ALWAYS);
    glDepthMask(GL_TRUE);
    glClearColor(0.2f, 0.4f, 0.6f, 0.8f);
    glClearDepth(1.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glUseProgram(prog);
    gl_set_uniforms(prog, p, in->seed);
    if (glGetError() != GL_NO_ERROR) fprintf(stderr, "GL error setting uniforms\n");
    if (in->alpha_func != 8) {
        glEnable(GL_ALPHA_TEST);
        glAlphaFunc(GL_NEVER + in->alpha_func, in->alpha_ref);
    } else {
        glDisable(GL_ALPHA_TEST);
    }
    for (int a = 0; a < p->n_attribs; a++) {
        const rsmsl_attrib *at = &p->attribs[a];
        const float *d = &in->verts[at->location][0][0];
        if (!strcmp(at->name, "gl_Vertex")) { glEnableClientState(GL_VERTEX_ARRAY); glVertexPointer(4, GL_FLOAT, 0, d); }
        else if (!strcmp(at->name, "gl_Normal")) { glEnableClientState(GL_NORMAL_ARRAY); glNormalPointer(GL_FLOAT, 16, d); }
        else if (!strcmp(at->name, "gl_Color")) { glEnableClientState(GL_COLOR_ARRAY); glColorPointer(4, GL_FLOAT, 0, d); }
        else if (!strcmp(at->name, "gl_SecondaryColor")) { glEnableClientState(GL_SECONDARY_COLOR_ARRAY); glSecondaryColorPointer(3, GL_FLOAT, 16, d); }
        else if (!strcmp(at->name, "gl_FogCoord")) { glEnableClientState(GL_FOG_COORD_ARRAY); glFogCoordPointer(GL_FLOAT, 16, d); }
        else if (!strncmp(at->name, "gl_MultiTexCoord", 16)) {
            glClientActiveTexture(GL_TEXTURE0 + at->name[16] - '0');
            glEnableClientState(GL_TEXTURE_COORD_ARRAY);
            glTexCoordPointer(4, GL_FLOAT, 0, d);
        } else {
            glEnableVertexAttribArray(at->location);
            glVertexAttribPointer(at->location, 4, GL_FLOAT, GL_FALSE, 0, d);
        }
    }
    if (probe_buf) {
        glBindBufferBaseEXT(GL_TRANSFORM_FEEDBACK_BUFFER_EXT, 0, probe_buf);
        glEnable(GL_RASTERIZER_DISCARD_EXT);
        glBeginTransformFeedbackEXT(GL_POINTS);
        glDrawArrays(GL_POINTS, 0, NVERT);
        glEndTransformFeedbackEXT();
        glDisable(GL_RASTERIZER_DISCARD_EXT);
    } else if (draws_points(p)) {
        glEnable(GL_POINT_SPRITE);  // gl_PointCoord (origin upper left, GL's default)
        glEnable(GL_VERTEX_PROGRAM_POINT_SIZE);
        glDrawArrays(GL_POINTS, 0, NVERT);
        glDisable(GL_POINT_SPRITE);
        glDisable(GL_VERTEX_PROGRAM_POINT_SIZE);
        glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, out->color);
        glReadPixels(0, 0, W, H, GL_DEPTH_COMPONENT, GL_FLOAT, out->depth);
    } else {
        glDrawArrays(GL_TRIANGLES, 0, NVERT);
        glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, out->color);
        glReadPixels(0, 0, W, H, GL_DEPTH_COMPONENT, GL_FLOAT, out->depth);
    }
    for (int a = 0; a < 16; a++) glDisableVertexAttribArray(a);
    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_NORMAL_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_SECONDARY_COLOR_ARRAY);
    glDisableClientState(GL_FOG_COORD_ARRAY);
    for (int t = 0; t < 8; t++) { glClientActiveTexture(GL_TEXTURE0 + t); glDisableClientState(GL_TEXTURE_COORD_ARRAY); }
    glUseProgram(0);
    GLenum e = glGetError();
    return e == GL_NO_ERROR;
}

// --- Metal --------------------------------------------------------------------------------------

static id<MTLTexture> mtl_texture(int unit_, int type) {
    MTLTextureDescriptor *d = [MTLTextureDescriptor new];
    d.pixelFormat = MTLPixelFormatRGBA8Unorm;
    d.width = TEX;
    d.height = type == 0x8B5D ? 1 : TEX;  // sampler1D -> 2D texture of height 1
    d.textureType = type == 0x8B60 ? MTLTextureTypeCube : type == 0x8B5F ? MTLTextureType3D : MTLTextureType2D;
    if (type == 0x8B5F) d.depth = 4;
    d.usage = MTLTextureUsageShaderRead;
    d.storageMode = MTLStorageModeShared;
    id<MTLTexture> t = [g_dev newTextureWithDescriptor:d];
    unsigned char *px = malloc(TEX * TEX * 4 * 4);
    int faces = type == 0x8B60 ? 6 : 1, depth = type == 0x8B5F ? 4 : 1;
    for (int f = 0; f < faces; f++) {
        for (int z = 0; z < depth; z++)
            for (int y = 0; y < (int)d.height; y++)
                for (int x = 0; x < TEX; x++) texel(unit_, f + z, x, y, px + ((z * TEX + y) * TEX + x) * 4);
        if (depth > 1)
            [t replaceRegion:MTLRegionMake3D(0, 0, 0, TEX, TEX, depth) mipmapLevel:0 slice:0 withBytes:px bytesPerRow:TEX * 4 bytesPerImage:TEX * TEX * 4];
        else
            [t replaceRegion:MTLRegionMake2D(0, 0, TEX, d.height) mipmapLevel:0 slice:f withBytes:px bytesPerRow:TEX * 4 bytesPerImage:0];
    }
    free(px);
    return t;
}

static id<MTLSamplerState> mtl_sampler(int type) {
    MTLSamplerDescriptor *s = [MTLSamplerDescriptor new];
    s.minFilter = s.magFilter = MTLSamplerMinMagFilterLinear;
    s.mipFilter = MTLSamplerMipFilterNotMipmapped;
    MTLSamplerAddressMode m = type == 0x8B63 || type == 0x8B60 ? MTLSamplerAddressModeClampToEdge : MTLSamplerAddressModeRepeat;
    s.sAddressMode = s.tAddressMode = s.rAddressMode = m;
    return [g_dev newSamplerStateWithDescriptor:s];
}

typedef struct {
    id<MTLLibrary> lib;
    char error[2048];
} mtl_lib;

static id<MTLLibrary> mtl_compile(const char *msl, char *err, size_t nerr) {
    MTLCompileOptions *o = [MTLCompileOptions new];
    if (@available(macOS 15.0, *)) o.mathMode = g_fast_math ? MTLMathModeFast : MTLMathModeSafe;
    else {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        o.fastMathEnabled = g_fast_math;
#pragma clang diagnostic pop
    }
    NSError *e = nil;
    id<MTLLibrary> lib = [g_dev newLibraryWithSource:@(msl) options:o error:&e];
    if (!lib) snprintf(err, nerr, "%s", e.localizedDescription.UTF8String);
    return lib;
}

// Checks the reflected uniform offsets against Metal's own reflection of the buffer(0) structs.
static int check_offsets(MTLRenderPipelineReflection *refl, const rsmsl_program *p, char *err, size_t nerr) {
    if (@available(macOS 13.0, *)) {
    for (int stage = 1; stage <= 2; stage++) {
        NSArray<id<MTLBinding>> *bs = stage == 1 ? refl.vertexBindings : refl.fragmentBindings;
        int size = stage == 1 ? p->vs_uniform_size : p->fs_uniform_size;
        for (id<MTLBinding> b in bs) {
            if (b.type != MTLBindingTypeBuffer || b.index != 0) continue;
            id<MTLBufferBinding> bb = (id<MTLBufferBinding>)b;
            if ((int)bb.bufferDataSize > size) { snprintf(err, nerr, "stage %d block is %d bytes, reflection says %d", stage, (int)bb.bufferDataSize, size); return 0; }
            for (MTLStructMember *m in bb.bufferStructType.members) {
                const char *mn = m.name.UTF8String;
                const char *un = !strncmp(mn, "v_", 2) ? mn + 2 : !strncmp(mn, "rs_gl_", 6) ? mn + 3 : mn;
                int found = 0;
                for (int i = 0; i < p->n_uniforms; i++) {
                    if (strcmp(p->uniforms[i].name, un)) continue;
                    int off = stage == 1 ? p->uniforms[i].vs_offset : p->uniforms[i].fs_offset;
                    if (off != (int)m.offset) { snprintf(err, nerr, "uniform %s at %d, Metal says %d", un, off, (int)m.offset); return 0; }
                    MTLArrayType *at = m.arrayType;
                    if (at && (int)at.stride != p->uniforms[i].stride) { snprintf(err, nerr, "uniform %s stride %d, Metal says %d", un, p->uniforms[i].stride, (int)at.stride); return 0; }
                    found = 1;
                }
                if (!found) { snprintf(err, nerr, "Metal block member %s not in the reflection", mn); return 0; }
            }
        }
    }
    }
    return 1;
}

// probe: run rs_vs_probe (no rasterisation), outputs into probe (float4s).
static int mtl_render(id<MTLLibrary> lib, inputs *in, image *out, int check, id<MTLBuffer> probe, char *err, size_t nerr) {
    const rsmsl_program *p = in->p;
    MTLFunctionConstantValues *fc = [MTLFunctionConstantValues new];
    uint32_t af = (uint32_t)in->alpha_func;
    [fc setConstantValue:&af type:MTLDataTypeUInt atIndex:0];
    NSError *e = nil;
    id<MTLFunction> vf = [lib newFunctionWithName:probe ? @"rs_vs_probe" : @"rs_vs" constantValues:fc error:&e];
    id<MTLFunction> ff = vf && !probe ? [lib newFunctionWithName:@"rs_fs" constantValues:fc error:&e] : nil;
    if (!vf || (!ff && !probe)) { snprintf(err, nerr, "function: %s", e.localizedDescription.UTF8String); return 0; }
    MTLRenderPipelineDescriptor *pd = [MTLRenderPipelineDescriptor new];
    pd.vertexFunction = vf;
    pd.fragmentFunction = ff;
    if (probe) pd.rasterizationEnabled = NO;
    pd.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA8Unorm;
    pd.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
    MTLVertexDescriptor *vd = [MTLVertexDescriptor new];
    for (int a = 0; a < p->n_attribs; a++) {
        int loc = p->attribs[a].location;
        vd.attributes[loc].format = MTLVertexFormatFloat4;  // as glVertexAttribPointer(size 4) whatever the GLSL type
        vd.attributes[loc].offset = 0;
        vd.attributes[loc].bufferIndex = VB_BASE + loc;
        vd.layouts[VB_BASE + loc].stride = 16;
    }
    pd.vertexDescriptor = vd;
    MTLRenderPipelineReflection *refl = nil;
    id<MTLRenderPipelineState> ps = [g_dev newRenderPipelineStateWithDescriptor:pd
                                                                        options:MTLPipelineOptionBindingInfo | MTLPipelineOptionBufferTypeInfo
                                                                     reflection:&refl
                                                                          error:&e];
    if (!ps) { snprintf(err, nerr, "pipeline: %s", e.localizedDescription.UTF8String); return 0; }
    if (check && !check_offsets(refl, p, err, nerr)) return 0;

    MTLTextureDescriptor *cd = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:W height:H mipmapped:NO];
    cd.usage = MTLTextureUsageRenderTarget;
    cd.storageMode = MTLStorageModeShared;
    id<MTLTexture> color = [g_dev newTextureWithDescriptor:cd];
    MTLTextureDescriptor *dd = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float width:W height:H mipmapped:NO];
    dd.usage = MTLTextureUsageRenderTarget;
    dd.storageMode = MTLStorageModePrivate;
    id<MTLTexture> depth = [g_dev newTextureWithDescriptor:dd];
    MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
    rp.colorAttachments[0].texture = color;
    rp.colorAttachments[0].loadAction = MTLLoadActionClear;
    rp.colorAttachments[0].storeAction = MTLStoreActionStore;
    rp.colorAttachments[0].clearColor = MTLClearColorMake(0.2, 0.4, 0.6, 0.8);
    rp.depthAttachment.texture = depth;
    rp.depthAttachment.loadAction = MTLLoadActionClear;
    rp.depthAttachment.storeAction = MTLStoreActionStore;
    rp.depthAttachment.clearDepth = 1.0;
    MTLDepthStencilDescriptor *dsd = [MTLDepthStencilDescriptor new];
    dsd.depthCompareFunction = MTLCompareFunctionAlways;
    dsd.depthWriteEnabled = YES;
    id<MTLDepthStencilState> ds = [g_dev newDepthStencilStateWithDescriptor:dsd];

    // Uniform blocks, padded with zeros past the end (out-of-range array reads see 0).
    size_t vsz = (size_t)p->vs_uniform_size + 4096, fsz = (size_t)p->fs_uniform_size + 4096;
    id<MTLBuffer> vub = [g_dev newBufferWithLength:vsz options:MTLResourceStorageModeShared];
    id<MTLBuffer> fub = [g_dev newBufferWithLength:fsz options:MTLResourceStorageModeShared];
    memset(vub.contents, 0, vsz);
    memset(fub.contents, 0, fsz);
    for (int i = 0; i < p->n_uniforms; i++) {
        const rsmsl_uniform *u = &p->uniforms[i];
        if (u->vs_offset >= 0) fill_block(u, vub.contents, u->vs_offset, in->seed);
        if (u->fs_offset >= 0) fill_block(u, fub.contents, u->fs_offset, in->seed);
    }

    id<MTLCommandBuffer> cb = [g_queue commandBuffer];
    id<MTLRenderCommandEncoder> enc = [cb renderCommandEncoderWithDescriptor:rp];
    [enc setRenderPipelineState:ps];
    [enc setDepthStencilState:ds];
    [enc setCullMode:MTLCullModeNone];
    [enc setFrontFacingWinding:MTLWindingClockwise];  // GL's CCW, inverted by the y flip
    [enc setViewport:(MTLViewport){0, 0, W, H, 0, 1}];
    for (int a = 0; a < p->n_attribs; a++) {
        int loc = p->attribs[a].location;
        [enc setVertexBytes:in->verts[loc] length:sizeof in->verts[loc] atIndex:VB_BASE + loc];
    }
    if (p->vs_uniform_size) [enc setVertexBuffer:vub offset:0 atIndex:0];
    if (p->fs_uniform_size) [enc setFragmentBuffer:fub offset:0 atIndex:0];
    float ref = in->alpha_ref;
    [enc setFragmentBytes:&ref length:4 atIndex:1];
    for (int i = 0; i < p->n_samplers; i++) {
        const rsmsl_sampler *s = &p->samplers[i];
        id<MTLTexture> t = mtl_texture(s->index, s->type);
        id<MTLSamplerState> ss = mtl_sampler(s->type);
        if (s->stages & RSMSL_STAGE_VS) { [enc setVertexTexture:t atIndex:s->index]; [enc setVertexSamplerState:ss atIndex:s->index]; }
        if (s->stages & RSMSL_STAGE_FS) { [enc setFragmentTexture:t atIndex:s->index]; [enc setFragmentSamplerState:ss atIndex:s->index]; }
    }
    if (probe) [enc setVertexBuffer:probe offset:0 atIndex:30];
    [enc drawPrimitives:probe || draws_points(p) ? MTLPrimitiveTypePoint : MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:NVERT];
    [enc endEncoding];
    id<MTLBuffer> dbuf = [g_dev newBufferWithLength:W * H * 4 options:MTLResourceStorageModeShared];
    id<MTLBlitCommandEncoder> bl = [cb blitCommandEncoder];
    [bl copyFromTexture:depth sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:MTLSizeMake(W, H, 1)
               toBuffer:dbuf destinationOffset:0 destinationBytesPerRow:W * 4 destinationBytesPerImage:W * H * 4];
    [bl endEncoding];
    [cb commit];
    [cb waitUntilCompleted];
    if (cb.error) { snprintf(err, nerr, "command buffer: %s", cb.error.localizedDescription.UTF8String); return 0; }
    if (out) {
        [color getBytes:out->color bytesPerRow:W * 4 fromRegion:MTLRegionMake2D(0, 0, W, H) mipmapLevel:0];
        memcpy(out->depth, dbuf.contents, sizeof out->depth);
    }
    return 1;
}

// --- Vertex probe: every vertex output, GL (transform feedback) vs Metal (rs_vs_probe) ----------

static double g_probe_tol = 1e-4;  // relative

// Returns the largest relative difference over all vertex outputs, or -1 (with a note) when GL
// can't capture them.
static double probe(const char *vs, const char *fs, const char *msl, id<MTLLibrary> lib, inputs *in,
                    const rsmsl_program *p, char *note, size_t nn) {
    const char *c = strstr(msl, "// rs_probe:");
    if (!c) { snprintf(note, nn, "no probe entry"); return -1; }
    char members[32][64], glnames[32][64];
    const char *glptr[32];
    int comps[32], n = 0;
    c += strlen("// rs_probe:");
    while (*c == ' ' && n < 32) {
        int k = 0;
        c++;
        while (*c && *c != ':' && k < 63) members[n][k++] = *c++;
        members[n][k] = 0;
        comps[n] = atoi(++c);
        while (*c && *c != ' ' && *c != '\n') c++;
        const char *mm = members[n];
        if (!strcmp(mm, "rs_Position")) snprintf(glnames[n], 64, "gl_Position");
        else if (!strcmp(mm, "rs_PointSize")) snprintf(glnames[n], 64, "gl_PointSize");
        else if (!strcmp(mm, "rs_Color")) snprintf(glnames[n], 64, "gl_FrontColor");
        else if (!strcmp(mm, "rs_SecondaryColor")) snprintf(glnames[n], 64, "gl_FrontSecondaryColor");
        else if (!strcmp(mm, "rs_FogFragCoord")) snprintf(glnames[n], 64, "gl_FogFragCoord");
        else if (!strncmp(mm, "rs_TexCoord", 11)) snprintf(glnames[n], 64, "gl_TexCoord[%s]", mm + 11);
        else snprintf(glnames[n], 64, "%s", mm + 2);  // v_<name>
        glptr[n] = glnames[n];
        n++;
    }
    char log[2048];
    GLuint v = gl_compile(GL_VERTEX_SHADER, vs, log, sizeof log), f = v ? gl_compile(GL_FRAGMENT_SHADER, fs, log, sizeof log) : 0;
    if (!v || !f) { snprintf(note, nn, "probe: GL compile failed"); return -1; }
    GLuint prog = glCreateProgram();
    glAttachShader(prog, v);
    glAttachShader(prog, f);
    for (int a = 0; a < p->n_attribs; a++)
        if (strncmp(p->attribs[a].name, "gl_", 3)) glBindAttribLocation(prog, p->attribs[a].location, p->attribs[a].name);
    glTransformFeedbackVaryingsEXT(prog, n, glptr, GL_INTERLEAVED_ATTRIBS_EXT);
    glLinkProgram(prog);
    glDeleteShader(v);
    glDeleteShader(f);
    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        glGetProgramInfoLog(prog, sizeof log, NULL, log);
        for (char *q = log; *q; q++) if (*q == '\n' || *q == '\t') *q = ' ';
        snprintf(note, nn, "probe: GL can't capture (%.200s)", log);
        glDeleteProgram(prog);
        return -1;
    }
    int total = 0;
    for (int i = 0; i < n; i++) total += comps[i];
    GLuint buf;
    glGenBuffers(1, &buf);
    glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER_EXT, buf);
    glBufferData(GL_TRANSFORM_FEEDBACK_BUFFER_EXT, NVERT * total * 4, NULL, GL_STATIC_READ);
    float *gv = calloc(NVERT * total, 4);
    gl_render(prog, in, NULL, buf);
    glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER_EXT, buf);
    glGetBufferSubData(GL_TRANSFORM_FEEDBACK_BUFFER_EXT, 0, NVERT * total * 4, gv);
    glDeleteBuffers(1, &buf);
    glDeleteProgram(prog);
    GLenum ge = glGetError();
    id<MTLBuffer> mb = [g_dev newBufferWithLength:NVERT * n * 16 options:MTLResourceStorageModeShared];
    char err[1024];
    if (ge != GL_NO_ERROR || !mtl_render(lib, in, NULL, 0, mb, err, sizeof err)) {
        snprintf(note, nn, "probe failed: %s", ge != GL_NO_ERROR ? "GL error" : err);
        free(gv);
        return -1;
    }
    const float *mv = mb.contents;
    double worst = 0;
    for (int vtx = 0; vtx < NVERT; vtx++) {
        const float *g = gv + vtx * total;
        for (int i = 0; i < n; i++) {
            float gg[4] = {0, 0, 0, 0};
            memcpy(gg, g, comps[i] * 4);
            if (i == 0) { gg[1] = -gg[1]; gg[2] = (gg[2] + gg[3]) * 0.5f; }  // the epilogue
            if (!strcmp(members[i], "rs_Color") || !strcmp(members[i], "rs_SecondaryColor"))
                for (int k = 0; k < 4; k++) gg[k] = gg[k] < 0 ? 0 : gg[k] > 1 ? 1 : gg[k];  // GL clamps after capture
            for (int k = 0; k < comps[i]; k++) {
                float a = gg[k], b = mv[(vtx * n + i) * 4 + k];
                double e;
                if (isnan(a) || isnan(b)) e = isnan(a) && isnan(b) ? 0 : INFINITY;
                else if (isinf(a) || isinf(b)) e = a == b ? 0 : INFINITY;
                else e = fabs((double)a - b) / fmax(1.0, fmax(fabs(a), fabs(b)));
                if (e > worst) {
                    worst = e;
                    if (g_verbose && e > g_probe_tol)
                        fprintf(stderr, "  vertex %d %s.%c GL %g Metal %g\n", vtx, glnames[i], "xyzw"[k], a, b);
                }
            }
            g += comps[i];
        }
    }
    free(gv);
    return worst;
}

// --- Compare / dump -----------------------------------------------------------------------------

typedef struct {
    int max_err, bad_px, covered;
    float depth_err;
} diff;

static diff compare(const image *a, const image *b) {
    diff d = {0, 0, 0, 0};
    static const unsigned char clear[4] = {51, 102, 153, 204};
    for (int i = 0; i < W * H; i++) {
        int m = 0, cov = 0;
        for (int c = 0; c < 4; c++) {
            int e = abs((int)a->color[i * 4 + c] - (int)b->color[i * 4 + c]);
            if (e > m) m = e;
            if (abs((int)a->color[i * 4 + c] - clear[c]) > 1) cov = 1;
        }
        if (m > d.max_err) d.max_err = m;
        if (m > g_tol && g_verbose && d.bad_px < 8)
            fprintf(stderr, "  pixel (%d,%d) GL %3d %3d %3d %3d depth %.6f  Metal %3d %3d %3d %3d depth %.6f\n", i % W, i / W,
                    a->color[i * 4], a->color[i * 4 + 1], a->color[i * 4 + 2], a->color[i * 4 + 3], a->depth[i],
                    b->color[i * 4], b->color[i * 4 + 1], b->color[i * 4 + 2], b->color[i * 4 + 3], b->depth[i]);
        if (m > g_tol) d.bad_px++;
        d.covered += cov;
        float de = fabsf(a->depth[i] - b->depth[i]);
        if (de > d.depth_err) d.depth_err = de;
    }
    return d;
}

static void write_png(const char *path, const unsigned char *rgba) {
    // Flip so the PNG shows GL's bottom row at the bottom.
    unsigned char *flip = malloc(W * H * 4);
    for (int y = 0; y < H; y++) memcpy(flip + y * W * 4, rgba + (H - 1 - y) * W * 4, W * 4);
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    CGContextRef ctx = CGBitmapContextCreate(flip, W, H, 8, W * 4, cs, (CGBitmapInfo)kCGImageAlphaNoneSkipLast);
    CGImageRef img = CGBitmapContextCreateImage(ctx);
    CFURLRef url = CFURLCreateFromFileSystemRepresentation(NULL, (const UInt8 *)path, strlen(path), false);
    CGImageDestinationRef dst = CGImageDestinationCreateWithURL(url, CFSTR("public.png"), 1, NULL);
    if (dst) { CGImageDestinationAddImage(dst, img, NULL); CGImageDestinationFinalize(dst); CFRelease(dst); }
    CFRelease(url);
    CGImageRelease(img);
    CGContextRelease(ctx);
    CGColorSpaceRelease(cs);
    free(flip);
}

static void dump(const char *tag, const rsmsl_program *p, const image *g, const image *m) {
    if (!g_dump) return;
    char path[2048];
    snprintf(path, sizeof path, "%s/%s.metal", g_dump, tag);
    FILE *f = fopen(path, "w");
    if (f) { fputs(p->msl, f); fclose(f); }
    if (!g || !m) return;
    snprintf(path, sizeof path, "%s/%s.gl.png", g_dump, tag);
    write_png(path, g->color);
    snprintf(path, sizeof path, "%s/%s.mtl.png", g_dump, tag);
    write_png(path, m->color);
    unsigned char *df = malloc(W * H * 4);
    for (int i = 0; i < W * H * 4; i++) df[i] = (unsigned char)(i % 4 == 3 ? 255 : (abs(g->color[i] - m->color[i]) * 8 > 255 ? 255 : abs(g->color[i] - m->color[i]) * 8));
    snprintf(path, sizeof path, "%s/%s.diff.png", g_dump, tag);
    write_png(path, df);
    free(df);
}

// --- Driver -------------------------------------------------------------------------------------

static char *slurp(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    rewind(f);
    char *b = malloc(n + 1);
    if (fread(b, 1, n, f) != (size_t)n) n = 0;
    b[n] = 0;
    fclose(f);
    return b;
}

// Attributes that appear inside an array index ("[...]") or a Cg ARL ("int(floor(") in the VS.
static unsigned index_attribs(const char *vs, const rsmsl_program *p) {
    unsigned m = 0;
    for (int a = 0; a < p->n_attribs; a++) {
        const char *nm = p->attribs[a].name;
        size_t n = strlen(nm);
        for (const char *q = vs; (q = strstr(q, nm)); q += n) {
            if ((q > vs && (isalnum((unsigned char)q[-1]) || q[-1] == '_')) || isalnum((unsigned char)q[n]) || q[n] == '_') continue;
            // inside brackets?
            int depth = 0;
            for (const char *r = q; r > vs && *r != ';' && *r != '\n'; r--) {
                if (*r == ']') depth--;
                if (*r == '[') depth++;
            }
            const char *ls = q;
            while (ls > vs && ls[-1] != '\n') ls--;
            const char *le = strchr(q, '\n');
            int arl = 0;
            for (const char *r = ls; r && r < (le ? le : q + n); r++)
                if (!strncmp(r, "int(floor(", 10)) arl = 1;
            if (depth > 0 || arl) m |= 1u << p->attribs[a].location;
        }
    }
    return m;
}

// The attribute that gets grid positions: by name, else the lowest location not used as an index.
static int position_attrib(const rsmsl_program *p, unsigned index_mask) {
    static const char *names[] = {"gl_Vertex", "pos", "position", "rs_attr0"};
    for (int k = 0; k < 4; k++)
        for (int a = 0; a < p->n_attribs; a++)
            if (!strcmp(p->attribs[a].name, names[k])) return p->attribs[a].location;
    int best = -1;
    for (int a = 0; a < p->n_attribs; a++)
        if (!((index_mask >> p->attribs[a].location) & 1) && (best < 0 || p->attribs[a].location < best)) best = p->attribs[a].location;
    return best;
}

typedef struct {
    int near;
    int total, translate_fail, gl_fail, compile_fail, render_fail, mismatch, pass;
    int hist[6];  // max error 0, 1, 2, 3-8, 9-32, >32
    int probe_skipped;
    double probe_max;
} stats;

static rsmsl_corpus_writer *g_corpus;
static FILE *g_report;

static const char *g_check_corpus;

static void run_pair(const char *tag, const char *vs, const char *fs, const char *bindings, stats *st) {
    st->total++;
    rsmsl_attrib bound[16];
    char names[16][64];
    int nb = 0;
    if (bindings) {
        const char *q = bindings;
        while (*q && nb < 16) {
            const char *eq = strchr(q, '=');
            if (!eq) break;
            snprintf(names[nb], sizeof names[nb], "%.*s", (int)(eq - q), q);
            bound[nb].name = names[nb];
            bound[nb].location = atoi(eq + 1);
            bound[nb].type = 0;
            nb++;
            q = strchr(eq, ',');
            if (!q) break;
            q++;
        }
    }
    char *xerr = NULL;
    if (g_check_corpus) {
        // The public entry point must find the pair, with the same MSL as a fresh translation.
        rsmsl_program *c = rsmsl_translate(vs, fs, bound, nb, NULL), *d = rsmsl_translate_direct(vs, fs, bound, nb, NULL);
        int ok = c && d && c->from_corpus && !strcmp(c->msl, d->msl) && c->n_uniforms == d->n_uniforms &&
                 c->n_attribs == d->n_attribs && c->vs_uniform_size == d->vs_uniform_size;
        if (ok) st->pass++;
        else { st->mismatch++; fprintf(g_report, "CORPUS-MISS\t%s\n", tag); }
        rsmsl_free(c);
        rsmsl_free(d);
        return;
    }
    rsmsl_program *p = rsmsl_translate_direct(vs, fs, bound, nb, &xerr);
    if (!p) {
        st->translate_fail++;
        fprintf(g_report, "TRANSLATE\t-\t-\t-\t-\t%s\t%s\n", tag, xerr);
        free(xerr);
        return;
    }
    char err[4096] = "";
    // Compiled with the probe entry (rs_vs / rs_fs are the same code as in p->msl).
    rsmsl_program *pp = rsmsl_translate_ex(vs, fs, bound, nb, RSMSL_XLAT_PROBE, NULL);
    id<MTLLibrary> lib = mtl_compile(pp ? pp->msl : p->msl, err, sizeof err);
    if (!lib) {
        st->compile_fail++;
        for (char *c = err; *c; c++) if (*c == '\n' || *c == '\t') *c = ' ';
        fprintf(g_report, "COMPILE\t-\t-\t-\t-\t%s\t%.1500s\n", tag, err);
        dump(tag, p, NULL, NULL);
        rsmsl_free(p); rsmsl_free(pp);
        return;
    }
    unsigned flags = 0;
    // GL program, bound to the translator's attribute locations.
    char log[4096] = "";
    GLuint v = gl_compile(GL_VERTEX_SHADER, vs, log, sizeof log), f = v ? gl_compile(GL_FRAGMENT_SHADER, fs, log, sizeof log) : 0;
    GLuint prog = 0;
    if (v && f) {
        prog = glCreateProgram();
        glAttachShader(prog, v);
        glAttachShader(prog, f);
        for (int a = 0; a < p->n_attribs; a++)
            if (strncmp(p->attribs[a].name, "gl_", 3)) glBindAttribLocation(prog, p->attribs[a].location, p->attribs[a].name);
        glLinkProgram(prog);
        GLint ok = 0;
        glGetProgramiv(prog, GL_LINK_STATUS, &ok);
        if (!ok) { glGetProgramInfoLog(prog, sizeof log, NULL, log); glDeleteProgram(prog); prog = 0; }
    }
    if (v) glDeleteShader(v);
    if (f) glDeleteShader(f);
    if (!prog) {
        st->gl_fail++;
        for (char *c = log; *c; c++) if (*c == '\n' || *c == '\t') *c = ' ';
        fprintf(g_report, "GL-FAIL\t-\t-\t-\t-\t%s\t%s\n", tag, log);
        if (g_corpus) rsmsl_corpus_add(g_corpus, vs, fs, p, 0);
        rsmsl_free(p); rsmsl_free(pp);
        return;
    }
    // Reflection against GL's: every active GL uniform / attribute must be known with its type.
    GLint n = 0;
    glGetProgramiv(prog, GL_ACTIVE_UNIFORMS, &n);
    for (GLint i = 0; i < n && !err[0]; i++) {
        char nm[256];
        GLint size;
        GLenum type;
        glGetActiveUniform(prog, i, sizeof nm, NULL, &size, &type, nm);
        char *br = strstr(nm, "[0]");
        if (br) *br = 0;
        int found = 0;
        for (int k = 0; k < p->n_uniforms; k++)
            if (!strcmp(p->uniforms[k].name, nm)) {
                found = 1;
                if (p->uniforms[k].type != (int)type || p->uniforms[k].array_size < size)
                    snprintf(err, sizeof err, "uniform %s: GL says type %#x size %d, reflection %#x %d", nm, type, size, p->uniforms[k].type, p->uniforms[k].array_size);
            }
        if (!found) snprintf(err, sizeof err, "GL uniform %s missing from the reflection", nm);
    }
    glGetProgramiv(prog, GL_ACTIVE_ATTRIBUTES, &n);
    for (GLint i = 0; i < n && !err[0]; i++) {
        char nm[256];
        GLint size;
        GLenum type;
        glGetActiveAttrib(prog, i, sizeof nm, NULL, &size, &type, nm);
        int found = 0;
        for (int k = 0; k < p->n_attribs; k++)
            if (!strcmp(p->attribs[k].name, nm)) {
                found = 1;
                GLint gl_loc = glGetAttribLocation(prog, nm);
                if (p->attribs[k].type != (int)type || (strncmp(nm, "gl_", 3) && gl_loc != p->attribs[k].location))
                    snprintf(err, sizeof err, "attribute %s: GL says type %#x location %d, reflection %#x %d", nm, type, gl_loc, p->attribs[k].type, p->attribs[k].location);
            }
        if (!found) snprintf(err, sizeof err, "GL attribute %s missing from the reflection", nm);
    }
    int ok = !err[0];
    diff worst = {0, 0, 0, 0};
    double probe_err = 0;
    char probe_note[512] = "";
    static image g, m;
    static inputs in;
    // Variants: 0 random uniforms; 1 alpha test GL_GREATER 0.5; 2 matrices near identity.
    for (int variant = 0; variant < 3 && ok; variant++) {
        in.p = p;
        in.seed = 1234 + variant * 7919;
        in.alpha_func = variant == 1 ? 4 : 8;
        g_near_identity = variant == 2;
        in.alpha_ref = 0.5f;
        in.index_attribs = index_attribs(vs, p);
        in.pos_loc = position_attrib(p, in.index_attribs);
        build_vertices(&in);
        if (!gl_render(prog, &in, &g, 0)) { snprintf(err, sizeof err, "GL error while drawing"); ok = 0; break; }
        if (!mtl_render(lib, &in, &m, variant == 0, nil, err, sizeof err)) { ok = 0; break; }
        diff d = compare(&g, &m);
        if (d.max_err > worst.max_err) worst.max_err = d.max_err;
        if (d.depth_err > worst.depth_err) worst.depth_err = d.depth_err;
        worst.bad_px += d.bad_px;
        if (variant != 1 && d.covered > worst.covered) worst.covered = d.covered;
        if (variant != 1 && pp) {
            char note[512] = "";
            double pe = probe(vs, fs, pp->msl, lib, &in, p, note, sizeof note);
            if (pe < 0) { if (!probe_note[0]) snprintf(probe_note, sizeof probe_note, "%s", note); }
            else if (pe > probe_err) probe_err = pe;
        }
        if (d.max_err > g_tol || d.depth_err > g_depth_tol) {
            char t2[1100];
            snprintf(t2, sizeof t2, "%s.v%d", tag, variant);
            for (char *c = t2; *c; c++) if (*c == '/') *c = '_';
            dump(t2, p, &g, &m);
        }
    }
    glDeleteProgram(prog);
    const char *status;
    if (!ok) {
        status = err[0] && strstr(err, "reflection") ? "REFLECT" : "RENDER";
        if (!strcmp(status, "REFLECT")) st->mismatch++; else st->render_fail++;
    } else if (worst.max_err <= 32 && worst.bad_px > 0 && worst.bad_px <= g_outliers && worst.depth_err <= g_depth_tol &&
               probe_err <= g_probe_tol) {
        // A few isolated pixels of last-bit math differences, amplified by the random inputs.
        status = "NEAR";
        st->near++;
        flags |= RSMSL_CORPUS_VERIFIED;
    } else if (worst.max_err > g_tol || worst.depth_err > g_depth_tol || probe_err > g_probe_tol) {
        status = probe_err > g_probe_tol && worst.max_err <= g_tol && worst.depth_err <= g_depth_tol ? "VDIFF" : "DIFF";
        st->mismatch++;
    } else {
        status = "PASS";
        st->pass++;
        flags |= RSMSL_CORPUS_VERIFIED;
    }
    if (ok) {
        int e = worst.max_err;
        st->hist[e == 0 ? 0 : e == 1 ? 1 : e == 2 ? 2 : e <= 8 ? 3 : e <= 32 ? 4 : 5]++;
    }
    if (probe_note[0]) st->probe_skipped++;
    if (probe_err > st->probe_max) st->probe_max = probe_err;
    fprintf(g_report, "%s\t%d\t%.2g\t%.2f\t%d\t%.2g\t%s\t%s%s\n", status, worst.max_err, worst.depth_err, worst.covered / (double)(W * H),
            worst.bad_px, probe_note[0] ? -1.0 : probe_err, tag, err, probe_note);
    if (g_corpus) rsmsl_corpus_add(g_corpus, vs, fs, p, flags);
    rsmsl_free(p);
    rsmsl_free(pp);
}

int main(int argc, char **argv) {
    @autoreleasepool {
        const char *pairs = NULL, *corpus_out = NULL, *report = NULL;
        int builtin = 0, limit = 1 << 30;
        for (int i = 1; i < argc; i++) {
            if (!strcmp(argv[i], "--builtin")) builtin = 1;
            else if (!strcmp(argv[i], "--corpus-out") && i + 1 < argc) corpus_out = argv[++i];
            else if (!strcmp(argv[i], "--report") && i + 1 < argc) report = argv[++i];
            else if (!strcmp(argv[i], "--tol") && i + 1 < argc) g_tol = atoi(argv[++i]);
            else if (!strcmp(argv[i], "--dump") && i + 1 < argc) g_dump = argv[++i];
            else if (!strcmp(argv[i], "--limit") && i + 1 < argc) limit = atoi(argv[++i]);
            else if (!strcmp(argv[i], "--fast-math")) g_fast_math = YES;
            else if (!strcmp(argv[i], "--verbose")) g_verbose = 1;
            else if (!strcmp(argv[i], "--check-corpus") && i + 1 < argc) g_check_corpus = argv[++i];
            else if (!strcmp(argv[i], "--outliers") && i + 1 < argc) g_outliers = atoi(argv[++i]);
            else if (argv[i][0] != '-') pairs = argv[i];
            else { fprintf(stderr, "unknown option %s\n", argv[i]); return 2; }
        }
        if (!builtin && !pairs) { fprintf(stderr, "usage: msl_verify --builtin | <pairs.tsv> [options]\n"); return 2; }
        if (g_check_corpus) rsmsl_corpus_override(g_check_corpus);
        g_dev = MTLCreateSystemDefaultDevice();
        if (!g_dev) { fprintf(stderr, "no Metal device\n"); return 1; }
        g_queue = [g_dev newCommandQueue];
        if (!gl_init()) { fprintf(stderr, "no GL context\n"); return 1; }
        g_report = report ? fopen(report, "w") : stdout;
        if (!g_report) { perror(report); return 1; }
        if (g_dump) mkdir(g_dump, 0755);
        if (corpus_out) g_corpus = rsmsl_corpus_begin();
        stats st = {0};
        if (builtin) {
            for (size_t i = 0; i < sizeof msl_cases / sizeof msl_cases[0] && (int)i < limit; i++)
                run_pair(msl_cases[i].name, msl_cases[i].vs, msl_cases[i].fs, msl_cases[i].bindings, &st);
        } else {
            FILE *f = fopen(pairs, "r");
            if (!f) { perror(pairs); return 77; }
            char line[8192];
            while (fgets(line, sizeof line, f) && st.total < limit) {
                line[strcspn(line, "\r\n")] = 0;
                if (!line[0] || line[0] == '#') continue;
                char *vsp = line, *fsp = strchr(line, '\t'), *bind = NULL;
                if (!fsp) continue;
                *fsp++ = 0;
                if ((bind = strchr(fsp, '\t'))) *bind++ = 0;
                char *vs = slurp(vsp), *fs = slurp(fsp);
                if (!vs || !fs) { fprintf(stderr, "can't read %s / %s\n", vsp, fsp); free(vs); free(fs); continue; }
                const char *bv = strrchr(vsp, '/'), *bf = strrchr(fsp, '/');
                char tag[1024];
                snprintf(tag, sizeof tag, "%s+%s", bv ? bv + 1 : vsp, bf ? bf + 1 : fsp);
                @autoreleasepool { run_pair(tag, vs, fs, bind && *bind ? bind : NULL, &st); }
                free(vs);
                free(fs);
            }
            fclose(f);
            if (!st.total) { fprintf(stderr, "no pairs in %s\n", pairs); return 77; }
        }
        if (g_corpus && rsmsl_corpus_finish(g_corpus, corpus_out)) { fprintf(stderr, "can't write %s\n", corpus_out); return 1; }
        if (g_report != stdout) fclose(g_report);
        printf("msl_verify: %d pairs, %d passed, %d near (isolated precision outliers), %d differ, %d translate failures, %d MSL compile failures, "
               "%d GL failures, %d render errors (tolerance %d/255, %s math)\n",
               st.total, st.pass, st.near, st.mismatch, st.translate_fail, st.compile_fail, st.gl_fail, st.render_fail, g_tol,
               g_fast_math ? "fast" : "safe");
        printf("vertex probe: largest relative difference %.3g (tolerance %g), %d pairs not captured by GL\n", st.probe_max,
               g_probe_tol, st.probe_skipped);
        printf("max error histogram: 0: %d, 1: %d, 2: %d, 3-8: %d, 9-32: %d, >32: %d\n", st.hist[0], st.hist[1], st.hist[2],
               st.hist[3], st.hist[4], st.hist[5]);
        if (corpus_out) printf("corpus: %s\n", corpus_out);
        return st.pass + st.near == st.total ? 0 : 1;
    }
}
