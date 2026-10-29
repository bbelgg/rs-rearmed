// Stress test and benchmark of the Cg shim with the game's call patterns.
//
//   stress_test                     replay test (RS_CG_BACKEND selects the backend)
//   stress_test <program files...>  + corpus churn: random pairs of the game's programs
//   stress_test --bench [frames]    GL calls and time per replayed frame
//   stress_test --pairs <pass>      pair cache (RS_CG_PAIR_CACHE): pass 1 records the
//                                   pairs of a replay; later passes must link them all
//                                   while the programs load and none while drawing
//
// RS_CG_TEST_TRACKED=gl makes this program's rs_gl_current_program (which the
// shim looks up in place of libRSOpenGL's) answer from GL, =unknown answer -1;
// unset, libRSOpenGL's is used when it has one.
//
// What the game does (from its renderer, see shims/cg/README.md):
// - It binds a Cg program only when it differs from the one it bound last for
//   that stage (two globals for the whole run), with cgGLEnableProfile right
//   before, and never disables a profile or unbinds.
// - Per draw: parameters (matrices, vectors; most unchanged from the last draw),
//   cgGLSetTextureParameter + cgGLEnableTextureParameter, then for every vertex
//   stream cgGLEnableClientState + cgGLSetParameterPointer, the draw, and
//   cgGLDisableClientState for every stream.
// - Scaleform draws in between with GLSL programs of its own, changes the active
//   texture unit, and ends its frames with glUseProgram(0).
// Each frame draws a grid of cells, each with a known expected colour, and
// checks every cell and glGetError. Then the same frames run on a second
// context in the same share group and (glsl only: ARB program objects aren't
// shared) on one in a share group of its own (not on libRSOpenGL's Metal
// backend, which has no shared contexts yet).
#define GL_SILENCE_DEPRECATION
#include <dlfcn.h>
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../src/cg_api.h"
#include "../src/cg_stats.h"

#define CELL 4
#define GRID 8
#define W (CELL * GRID)
#define H (CELL * GRID)
#define NVP 6
#define NFP 8
#define NTEX 4

static int g_failures;
#define CHECK(cond, ...)                                                       \
    do {                                                                       \
        if (!(cond)) {                                                         \
            if (g_failures < 40) {                                             \
                printf("FAIL %s:%d: ", __FILE__, __LINE__);                    \
                printf(__VA_ARGS__);                                           \
                printf("\n");                                                  \
            }                                                                  \
            g_failures++;                                                      \
        }                                                                      \
    } while (0)

static uint32_t g_rng = 1;
static uint32_t rnd(void) {
    g_rng = g_rng * 1664525u + 1013904223u;
    return g_rng >> 8;
}
static float frand(float lo, float hi) { return lo + (hi - lo) * (rnd() / 16777216.0f); }

static int g_glsl;
static CGcontext g_ctx;

// See the top. Exported from the executable, so the shim's dlsym finds it first.
__attribute__((visibility("default"))) int rs_gl_current_program(void) {
    static int mode = -1;
    static int (*next)(void);
    if (mode < 0) {
        const char *e = getenv("RS_CG_TEST_TRACKED");
        mode = !e ? 0 : !strcmp(e, "gl") ? 1 : 2;
        if (!mode) next = (int (*)(void))dlsym(RTLD_NEXT, "rs_gl_current_program");
    }
    if (mode == 1) {
        GLint cur = 0;
        glGetIntegerv(GL_CURRENT_PROGRAM, &cur);
        return cur;
    }
    return mode == 0 && next ? next() : -1;
}

// Whether the shim gets the current program without asking GL.
static int tracked(void) {
    const char *e = getenv("RS_CG_TEST_TRACKED");
    return (e && !strcmp(e, "gl")) || (!e && dlsym(RTLD_NEXT, "rs_gl_current_program"));
}

static void print_times(const char *what, const unsigned long long *a, const unsigned long long *b) {
    printf("  %s: %llu links (%.1f ms, longest %.2f ms), %llu shader compiles (%.1f ms), %llu warmed\n", what,
           b[ST_Links] - a[ST_Links], (b[ST_LinkUs] - a[ST_LinkUs]) / 1000.0, b[ST_LinkMaxUs] / 1000.0,
           b[ST_Compile] - a[ST_Compile], (b[ST_CompileUs] - a[ST_CompileUs]) / 1000.0, b[ST_Warmed] - a[ST_Warmed]);
}

// --- programs ------------------------------------------------------------------

// vp i: position = M * pos; texcoord[s] = col * k + (i / 8 + s / 4, 1 / 16, 1 / 32, 0)
// for s = 0..2 (unwritten varyings read differently on the two backends, which
// GL leaves undefined); color = col.
static char *make_vp(int i) {
    char *s = malloc(2048);
    float a = i / 8.0f;
    snprintf(s, 2048,
             "!!ARBvp1.0\n#program main\n"
             "#var float4 pos : $vin.POSITION : POSITION : 0 : 1\n"
             "#var float4 col : $vin.COLOR0 : COLOR0 : 1 : 1\n"
             "#var float4x4 M :  : c[0], 4 : -1 : 1\n"
             "#var float4 k :  : c[4] : -1 : 1\n"
             "#var float4 main.pos : $vout.POSITION : HPOS : -1 : 1\n"
             "#const c[5] = %g 0.0625 0.03125 0\n#const c[6] = %g 0.0625 0.03125 0\n"
             "#const c[7] = %g 0.0625 0.03125 0\n"
             "PARAM c[8] = { program.local[0..4],\n\t\t{ %g, 0.0625, 0.03125, 0 },\n"
             "\t\t{ %g, 0.0625, 0.03125, 0 },\n\t\t{ %g, 0.0625, 0.03125, 0 } };\n"
             "TEMP R0;\n"
             "DP4 result.position.x, c[0], vertex.position;\n"
             "DP4 result.position.y, c[1], vertex.position;\n"
             "DP4 result.position.z, c[2], vertex.position;\n"
             "DP4 result.position.w, c[3], vertex.position;\n"
             "MUL R0, vertex.color, c[4];\n"
             "ADD result.texcoord[0], R0, c[5];\nADD result.texcoord[1], R0, c[6];\n"
             "ADD result.texcoord[2], R0, c[7];\n"
             "MOV result.color, vertex.color;\nEND\n",
             a, a + 0.25f, a + 0.5f, a, a + 0.25f, a + 0.5f);
    return s;
}

static float vp_const(int i, int slot, int c) {
    const float k[4] = {i / 8.0f + slot / 4.0f, 0.0625f, 0.03125f, 0};
    return k[c];
}

// fp j: t = texcoord[j % 3]; color = (t * tint + (0.125 j, 0, 0.5, 0.25)) [* texture[unit] when j is odd]
static char *make_fp(int j) {
    char *s = malloc(2048);
    int unit = 1 + j % 3;
    if (j & 1)
        snprintf(s, 2048,
                 "!!ARBfp1.0\n#program main\n"
                 "#var float4 t : $vin.TEXCOORD%d : TEX%d : 0 : 1\n"
                 "#var float4 tint :  : c[0] : -1 : 1\n"
                 "#var sampler2D img : TEXUNIT%d : texunit %d : -1 : 1\n"
                 "#var float4 main : $vout.COLOR : COL : -1 : 1\n"
                 "#const c[1] = %g 0 0.5 0.25\n"
                 "PARAM c[2] = { program.local[0], { %g, 0, 0.5, 0.25 } };\n"
                 "TEMP R0, R1;\nTEX R1, fragment.texcoord[%d], texture[%d], 2D;\n"
                 "MAD R0, fragment.texcoord[%d], c[0], c[1];\n"
                 "MUL result.color, R0, R1;\nEND\n",
                 j % 3, j % 3, unit, unit, j / 8.0, j / 8.0, j % 3, unit, j % 3);
    else
        snprintf(s, 2048,
                 "!!ARBfp1.0\n#program main\n"
                 "#var float4 t : $vin.TEXCOORD%d : TEX%d : 0 : 1\n"
                 "#var float4 tint :  : c[0] : -1 : 1\n"
                 "#var float4 main : $vout.COLOR : COL : -1 : 1\n"
                 "#const c[1] = %g 0 0.5 0.25\n"
                 "PARAM c[2] = { program.local[0], { %g, 0, 0.5, 0.25 } };\n"
                 "MAD result.color, fragment.texcoord[%d], c[0], c[1];\nEND\n",
                 j % 3, j % 3, j / 8.0, j / 8.0, j % 3);
    return s;
}

static CGprogram load(const char *src, CGprofile prof) {
    CGprogram p = cgCreateProgram(g_ctx, CG_OBJECT, src, prof, "main", NULL);
    cgGLLoadProgram(p);
    int e = cgGetError();
    CHECK(p && !e, "load failed: %s\n%s", cgGetErrorString(e), cgGetLastListing(g_ctx));
    return p;
}

static CGprogram g_vp[NVP], g_fp[NFP];
static CGparameter g_vp_pos[NVP], g_vp_col[NVP], g_vp_M[NVP], g_vp_k[NVP], g_fp_tint[NFP], g_fp_img[NFP];

// The game's bind (0x1010ff8d0) with its cache of the last bound program per stage.
static CGprogram g_last[2];
static void game_bind(CGprogram p, int fragment) {
    if (g_last[fragment] == p) return;
    cgGLEnableProfile(fragment ? CG_PROFILE_ARBFP1 : CG_PROFILE_ARBVP1);
    cgGLBindProgram(p);
    g_last[fragment] = p;
}

// --- per-context GL objects ------------------------------------------------------

typedef struct {
    CGLContextObj cgl;
    GLuint fbo, color;
    GLuint tex[NTEX];
    GLuint own;          // "Scaleform" program
    GLint own_u;
    int shares_textures; // with the first context
} Gpu;

static const float TEXCOL[NTEX][4] = {{1, 0.5f, 0.25f, 1}, {0.5f, 1, 1, 0.5f}, {0.25f, 0.75f, 0.5f, 1}, {1, 1, 0, 0.25f}};

static GLuint own_program(GLint *u) {
    const char *src[2] = {"void main() { gl_Position = gl_Vertex; }",
                          "uniform vec4 u; void main() { gl_FragColor = u; }"};
    GLuint prog = glCreateProgram();
    for (int i = 0; i < 2; i++) {
        GLuint s = glCreateShader(i ? GL_FRAGMENT_SHADER : GL_VERTEX_SHADER);
        glShaderSource(s, 1, &src[i], NULL);
        glCompileShader(s);
        glAttachShader(prog, s);
        glDeleteShader(s);
    }
    glLinkProgram(prog);
    *u = glGetUniformLocation(prog, "u");
    return prog;
}

static int make_gpu(Gpu *g, CGLContextObj share, const Gpu *share_gpu) {
    CGLPixelFormatAttribute attrs[] = {kCGLPFAAccelerated, 0};
    CGLPixelFormatObj pf;
    GLint npf;
    if (CGLChoosePixelFormat(attrs, &pf, &npf) || CGLCreateContext(pf, share, &g->cgl)) return 0;
    CGLDestroyPixelFormat(pf);
    CGLSetCurrentContext(g->cgl);
    glGenTextures(1, &g->color);
    glBindTexture(GL_TEXTURE_2D, g->color);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F_ARB, W, H, 0, GL_RGBA, GL_FLOAT, NULL);
    glGenFramebuffersEXT(1, &g->fbo);
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, g->fbo);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_2D, g->color, 0);
    if (share_gpu) {
        memcpy(g->tex, share_gpu->tex, sizeof g->tex);
        g->shares_textures = 1;
    } else {
        glGenTextures(NTEX, g->tex);
        for (int i = 0; i < NTEX; i++) {
            glBindTexture(GL_TEXTURE_2D, g->tex[i]);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F_ARB, 1, 1, 0, GL_RGBA, GL_FLOAT, TEXCOL[i]);
        }
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    g->own = own_program(&g->own_u);
    return glGetError() == GL_NO_ERROR;
}

// --- one replayed frame -------------------------------------------------------------

static const float QUAD[4][4] = {{-1, -1, 0, 1}, {1, -1, 0, 1}, {-1, 1, 0, 1}, {1, 1, 0, 1}};

typedef struct {
    int vp, fp, tex;
    float col[4], k[4], tint[4];
    float scale;          // M = scale * identity (same coverage)
    int own;              // drawn by "Scaleform" instead
    float own_u[4];
    int keep_streams;     // no cgGLDisableClientState after the draw
    int minimal;          // a repeat of the last game draw: no binds or parameters, only streams (and textures)
} Draw;

static float g_expect[GRID * GRID][4];
static int g_has_expect[GRID * GRID];

static void expected(const Draw *d, float out[4]) {
    if (d->own) { memcpy(out, d->own_u, sizeof d->own_u); return; }
    float t[4];
    for (int c = 0; c < 4; c++) t[c] = d->col[c] * d->k[c] + vp_const(d->vp, d->fp % 3, c);
    const float cj[4] = {d->fp / 8.0f, 0, 0.5f, 0.25f};
    for (int c = 0; c < 4; c++) {
        out[c] = t[c] * d->tint[c] + cj[c];
        if (d->fp & 1) out[c] *= TEXCOL[d->tex][c];
    }
}

// State carried between draws, like the game's: values repeat unless changed.
static Draw g_prev_draw;

static void plan_frame(Draw *draws, int n, int realistic) {
    // Materials come in runs (the renderer sorts by shader); each draw changes
    // its object's colour and constant, the shared tint changes now and then.
    Draw d = g_prev_draw;
    for (int i = 0; i < n; i++) {
        if (i == 0 || rnd() % (realistic ? 6 : 2) == 0) {
            d.vp = rnd() % NVP;
            d.fp = rnd() % NFP;
        }
        if (rnd() % 3 == 0) d.tex = rnd() % NTEX;
        for (int c = 0; c < 4; c++) d.col[c] = frand(0, 1);
        if (rnd() % 2) for (int c = 0; c < 4; c++) d.k[c] = frand(-1, 1);
        if (rnd() % 5 == 0) for (int c = 0; c < 4; c++) d.tint[c] = frand(-1, 1);
        d.scale = rnd() % 4 == 0 ? 2.0f : 1.0f;
        d.own = !realistic && rnd() % 9 == 0;
        for (int c = 0; c < 4; c++) d.own_u[c] = frand(0, 1);
        draws[i] = d;
    }
    g_prev_draw = d;
}

// Draw call order. The renderer's shader object has SetupGeometry (the stream
// loop) and FirstPass/NextPass (binds) + SetupShaderPrograms (parameters) as
// separate calls; both orders are replayed.
static int g_geometry_first;

static void streams(const Draw *d, float (*col)[4]) {
    cgGLEnableClientState(g_vp_pos[d->vp]);
    cgGLSetParameterPointer(g_vp_pos[d->vp], 4, GL_FLOAT, 0, QUAD);
    cgGLEnableClientState(g_vp_col[d->vp]);
    cgGLSetParameterPointer(g_vp_col[d->vp], 4, GL_FLOAT, 0, col);
}

static void game_draw(const Gpu *g, const Draw *d, int cell) {
    glViewport((cell % GRID) * CELL, (cell / GRID) * CELL, CELL, CELL);
    if (d->own) {
        // Scaleform: its own program, its own texture unit, then program 0 as at
        // the end of its scene.
        glUseProgram(g->own);
        glUniform4fv(g->own_u, 1, d->own_u);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, g->tex[0]);
        glEnableClientState(GL_VERTEX_ARRAY);
        glVertexPointer(4, GL_FLOAT, 0, QUAD);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glDisableClientState(GL_VERTEX_ARRAY);
        glActiveTexture(GL_TEXTURE0);
        glUseProgram(0);
        return;
    }
    float col[4][4];
    for (int v = 0; v < 4; v++) memcpy(col[v], d->col, sizeof col[v]);
    if (g_geometry_first || d->minimal) streams(d, col);
    if (!d->minimal) {
        game_bind(g_vp[d->vp], 0);
        game_bind(g_fp[d->fp], 1);
        float M[16] = {0};
        for (int i = 0; i < 4; i++) M[5 * i] = d->scale;
        cgGLSetMatrixParameterfr(g_vp_M[d->vp], M);
        cgGLSetParameter4fv(g_vp_k[d->vp], d->k);
        cgGLSetParameter4fv(g_fp_tint[d->fp], d->tint);
    }
    if (d->fp & 1) {
        cgGLSetTextureParameter(g_fp_img[d->fp], g->tex[d->tex]);
        cgGLEnableTextureParameter(g_fp_img[d->fp]);
    }
    if (!g_geometry_first && !d->minimal) streams(d, col);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    if (!d->keep_streams) {
        cgGLDisableClientState(g_vp_pos[d->vp]);
        cgGLDisableClientState(g_vp_col[d->vp]);
    }
    if (d->fp & 1) cgGLDisableTextureParameter(g_fp_img[d->fp]);
}

// A renderer per context: the game's bind cache describes one context's ARB
// bindings, so it starts over on another.
static void switch_to(const Gpu *g) {
    CGLSetCurrentContext(g->cgl);
    g_last[0] = g_last[1] = NULL;
}

static void run_frame(const Gpu *g, int frame, const char *what) {
    Draw draws[GRID * GRID];
    plan_frame(draws, GRID * GRID, 0);
    // A draw that leaves its streams enabled, Scaleform, and the same draw again
    // with nothing but its streams: only the stream calls can give GL back to the
    // pair, which takes the tracked current program (without it the shim checks
    // once per run of stream enables and relies on the disables in between).
    if (g_glsl && tracked())
        for (int i = 3; i + 2 < GRID * GRID; i += 11) {
            draws[i].own = 0;
            draws[i].keep_streams = 1;
            draws[i + 1].own = 1;
            draws[i + 2] = draws[i];
            draws[i + 2].keep_streams = 0;
            draws[i + 2].minimal = 1;
        }
    g_geometry_first = frame % 2;
    glClearColor(-1, -1, -1, -1);
    glClear(GL_COLOR_BUFFER_BIT);
    for (int i = 0; i < GRID * GRID; i++) {
        game_draw(g, &draws[i], i);
        expected(&draws[i], g_expect[i]);
    }
    glUseProgram(0);   // Scaleform's EndScene
    float px[H][W][4];
    glReadPixels(0, 0, W, H, GL_RGBA, GL_FLOAT, px);
    GLenum e = glGetError();
    CHECK(e == GL_NO_ERROR, "%s frame %d: GL error 0x%x", what, frame, e);
    int e2 = cgGetError();
    CHECK(e2 == 0, "%s frame %d: Cg error %s", what, frame, cgGetErrorString(e2));
    for (int i = 0; i < GRID * GRID; i++) {
        const float *got = px[(i / GRID) * CELL + CELL / 2][(i % GRID) * CELL + CELL / 2];
        int bad = 0;
        for (int c = 0; c < 4; c++) bad |= !(fabsf(got[c] - g_expect[i][c]) <= 1e-4f * (1 + fabsf(g_expect[i][c])));
        CHECK(!bad, "%s frame %d cell %d (vp %d fp %d%s): got (%g %g %g %g), want (%g %g %g %g)", what, frame, i,
              draws[i].vp, draws[i].fp, draws[i].own ? ", own program" : "", got[0], got[1], got[2], got[3],
              g_expect[i][0], g_expect[i][1], g_expect[i][2], g_expect[i][3]);
    }
}

static void make_programs(void) {
    for (int i = 0; i < NVP; i++) {
        char *s = make_vp(i);
        g_vp[i] = load(s, CG_PROFILE_ARBVP1);
        free(s);
        g_vp_pos[i] = cgGetNamedParameter(g_vp[i], "pos");
        g_vp_col[i] = cgGetNamedParameter(g_vp[i], "col");
        g_vp_M[i] = cgGetNamedParameter(g_vp[i], "M");
        g_vp_k[i] = cgGetNamedParameter(g_vp[i], "k");
    }
    for (int j = 0; j < NFP; j++) {
        char *s = make_fp(j);
        g_fp[j] = load(s, CG_PROFILE_ARBFP1);
        free(s);
        g_fp_tint[j] = cgGetNamedParameter(g_fp[j], "tint");
        g_fp_img[j] = cgGetNamedParameter(g_fp[j], "img");
    }
}

// --- corpus churn ---------------------------------------------------------------------

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

static int is_matrix(CGtype t) {
    return t == CG_FLOAT4x4 || t == CG_FLOAT3x4 || t == CG_FLOAT3x3 || t == CG_FLOAT4x3 ||
           t == CG_FLOAT4x2 || t == CG_FLOAT2x2 || t == CG_HALF4x4;
}

typedef struct { CGprogram p; const char *name; } CorpusProg;

static void set_all(CGprogram p, const Gpu *g, GLuint cube, GLuint tex1d, int enable_streams, float (*data)[4]) {
    CGenum spaces[] = {CG_PROGRAM, CG_GLOBAL};
    for (int s = 0; s < 2; s++)
        for (CGparameter q = cgGetFirstLeafParameter(p, spaces[s]); q; q = cgGetNextLeafParameter(q)) {
            if (cgGetParameterDirection(q) != CG_IN) continue;
            CGtype t = cgGetParameterType(q);
            if (cgGetParameterVariability(q) == CG_VARYING) {
                if (!enable_streams || !cgIsParameterUsed(q, p)) continue;
                cgGLEnableClientState(q);
                cgGLSetParameterPointer(q, 4, GL_FLOAT, 0, data);
                continue;
            }
            if (cgGetParameterVariability(q) != CG_UNIFORM) continue;
            if (t >= CG_SAMPLER1D && t <= CG_SAMPLERCUBE) {
                if (!cgIsParameterUsed(q, p)) continue;
                cgGLSetTextureParameter(q, t == CG_SAMPLERCUBE ? cube : t == CG_SAMPLER1D ? tex1d : g->tex[rnd() % NTEX]);
                cgGLEnableTextureParameter(q);
                continue;
            }
            if (t == CG_SAMPLER || t == CG_TEXTURE) continue;
            float m[16];
            for (int i = 0; i < 16; i++) m[i] = (i % 5 == 0 ? 0.6f : 0.0f) + frand(-0.1f, 0.1f);
            if (is_matrix(t)) cgGLSetMatrixParameterfr(q, m);
            else if (rnd() % 2) cgGLSetParameter4fv(q, m);   // half of them change per draw
        }
}

static void disable_streams(CGprogram p) {
    for (CGparameter q = cgGetFirstLeafParameter(p, CG_PROGRAM); q; q = cgGetNextLeafParameter(q))
        if (cgGetParameterDirection(q) == CG_IN && cgGetParameterVariability(q) == CG_VARYING &&
            cgIsParameterUsed(q, p))
            cgGLDisableClientState(q);
}

static void corpus_churn(const Gpu *g, int argc, char **argv) {
    CorpusProg *vps = calloc(argc, sizeof *vps), *fps = calloc(argc, sizeof *fps);
    int nv = 0, nf = 0;
    unsigned long long st_load[RS_CG_STAT_COUNT];
    rs_cg_gl_stats(st_load);
    for (int i = 0; i < argc; i++) {
        char *src = slurp(argv[i]);
        if (!src || strncmp(src, "!!ARB", 5)) { free(src); continue; }
        int frag = strstr(src, "!!ARBfp") != NULL;
        CGprogram p = cgCreateProgram(g_ctx, CG_OBJECT, src, frag ? CG_PROFILE_ARBFP1 : CG_PROFILE_ARBVP1, "main", NULL);
        free(src);
        cgGLLoadProgram(p);
        if (cgGetError()) { cgDestroyProgram(p); continue; }
        const char *base = strrchr(argv[i], '/');
        CorpusProg cp = {p, base ? base + 1 : argv[i]};
        if (frag) fps[nf++] = cp; else vps[nv++] = cp;
    }
    GLuint cube, tex1d;
    static uint8_t px[6 * 4 * 4 * 4];
    for (size_t i = 0; i < sizeof px; i++) px[i] = (uint8_t)rnd();
    glGenTextures(1, &cube);
    glBindTexture(GL_TEXTURE_CUBE_MAP, cube);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    for (int f = 0; f < 6; f++)
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + f, 0, GL_RGBA8, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, px + 64 * f);
    glGenTextures(1, &tex1d);
    glBindTexture(GL_TEXTURE_1D, tex1d);
    glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexImage1D(GL_TEXTURE_1D, 0, GL_RGBA8, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    glBindTexture(GL_TEXTURE_1D, 0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    glGetError();
    float data[3][4];
    for (int v = 0; v < 3; v++)
        for (int c = 0; c < 4; c++) data[v][c] = frand(-0.9f, 0.9f);
    data[0][3] = data[1][3] = data[2][3] = 1;
    unsigned long long st0[RS_CG_STAT_COUNT], st1[RS_CG_STAT_COUNT];
    rs_cg_gl_stats(st0);
    print_times("corpus load", st_load, st0);
    int draws = nv && nf ? 6000 : 0, gl_errors = 0, cg_errors = 0;
    clock_t t0 = clock();
    int vi = 0, fi = 0;
    for (int d = 0; d < draws; d++) {
        if (d == 0 || rnd() % 4 == 0) vi = rnd() % nv;
        if (d == 0 || rnd() % 3 == 0) fi = rnd() % nf;
        if (rnd() % 50 == 0) {   // Scaleform in between
            glUseProgram(g->own);
            glUseProgram(0);
        }
        game_bind(vps[vi].p, 0);
        game_bind(fps[fi].p, 1);
        set_all(vps[vi].p, g, cube, tex1d, 0, data);
        set_all(fps[fi].p, g, cube, tex1d, 0, data);
        set_all(vps[vi].p, g, cube, tex1d, 1, data);
        glDrawArrays(GL_POINTS, 0, 3);
        disable_streams(vps[vi].p);
        GLenum e = glGetError();
        int ce = cgGetError();
        if (e && gl_errors++ < 10) printf("FAIL corpus: GL error 0x%x drawing %s + %s\n", e, vps[vi].name, fps[fi].name);
        if (ce && cg_errors++ < 10)
            printf("FAIL corpus: Cg error %s drawing %s + %s\n", cgGetErrorString(ce), vps[vi].name, fps[fi].name);
    }
    glFinish();
    double ms = 1000.0 * (clock() - t0) / CLOCKS_PER_SEC;
    rs_cg_gl_stats(st1);
    printf("corpus churn: %d vertex x %d fragment programs, %d draws in %.0f ms CPU, %llu links, "
           "%d GL errors, %d Cg errors\n",
           nv, nf, draws, ms, st1[ST_Links] - st0[ST_Links], gl_errors, cg_errors);
    print_times("corpus draws", st0, st1);
    g_failures += gl_errors + cg_errors;
    glUseProgram(0);
    for (int i = 0; i < nv; i++) cgDestroyProgram(vps[i].p);
    for (int i = 0; i < nf; i++) cgDestroyProgram(fps[i].p);
    g_last[0] = g_last[1] = NULL;
    free(vps);
    free(fps);
    glDeleteTextures(1, &cube);
    glDeleteTextures(1, &tex1d);
}

// --- benchmark --------------------------------------------------------------------------

static void bench(Gpu *g, int frames, int mt) {
    if (mt) CGLEnable(g->cgl, kCGLCEMPEngine);
    enum { N = 200 };
    static Draw plan[N];
    g_rng = 7;
    plan_frame(plan, N, 1);
    // Same frame every time: the steady state of a scene. Warm up (links, first uploads).
    unsigned long long a[RS_CG_STAT_COUNT], b[RS_CG_STAT_COUNT];
    rs_cg_gl_stats(a);
    for (int w = 0; w < 3; w++)
        for (int i = 0; i < N; i++) game_draw(g, &plan[i], i % (GRID * GRID));
    glUseProgram(0);
    glFinish();
    rs_cg_gl_stats(b);
    unsigned long long warm_links = b[ST_Links] - a[ST_Links], warm_compiles = b[ST_Compile] - a[ST_Compile];
    rs_cg_gl_stats(a);
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int f = 0; f < frames; f++) {
        for (int i = 0; i < N; i++) game_draw(g, &plan[i], i % (GRID * GRID));
        glUseProgram(0);
        glFlush();
    }
    glFinish();
    clock_gettime(CLOCK_MONOTONIC, &t1);
    rs_cg_gl_stats(b);
    int vb = 0, fb = 0;
    for (int i = 1; i < N; i++) {
        vb += plan[i].vp != plan[i - 1].vp;
        fb += plan[i].fp != plan[i - 1].fp;
    }
    double us = ((t1.tv_sec - t0.tv_sec) * 1e6 + (t1.tv_nsec - t0.tv_nsec) / 1e3) / frames;
    printf("bench %s%s: %d draws/frame (%d vertex, %d fragment program changes), %.0f us/frame\n",
           g_glsl ? "glsl" : "arb", mt ? " +MT engine" : "", N, vb, fb, us);
    if (!mt) printf("  first frames: %llu links, %llu shader compiles\n", warm_links, warm_compiles);
    unsigned long long total = 0;
    for (int s = 0; s < RS_CG_STAT_COUNT; s++) {
        double per = (double)(b[s] - a[s]) / frames;
        if (s == ST_CurProgram && per > 0) printf("  %-16s %8.1f per frame (no GL call)\n", rs_cg_stat_names[s], per);
        if (!RS_CG_STAT_IS_CALL(s)) continue;
        total += b[s] - a[s];
        if (per > 0) printf("  %-16s %8.1f per frame\n", rs_cg_stat_names[s], per);
    }
    printf("  %-16s %8.1f per frame\n", "total", (double)total / frames);
    if (mt) CGLDisable(g->cgl, kCGLCEMPEngine);
}

int main(int argc, char **argv) {
    const char *b = getenv("RS_CG_BACKEND");
    // libRSOpenGL's backend (Metal unless RS_GL_BACKEND=gl), as the shim sees it.
    int (*be)(void) = (int (*)(void))dlsym(RTLD_DEFAULT, "rs_gfx_backend");
    int metal = be && be() == 1;
    g_glsl = (b && !strcmp(b, "glsl")) || metal;
    Gpu a = {0}, shared = {0}, alone = {0};
    if (!make_gpu(&a, NULL, NULL)) {
        printf("no GL context\n");
        return 1;
    }
    g_ctx = cgCreateContext();
    unsigned long long st0[RS_CG_STAT_COUNT], st1[RS_CG_STAT_COUNT], st2[RS_CG_STAT_COUNT];
    rs_cg_gl_stats(st0);
    make_programs();
    rs_cg_gl_stats(st1);

    if (argc >= 3 && !strcmp(argv[1], "--pairs")) {
        int pass = atoi(argv[2]);
        for (int f = 0; f < 20; f++) run_frame(&a, f, "context A");
        rs_cg_gl_stats(st2);
        print_times("load", st0, st1);
        print_times("frames", st1, st2);
        unsigned long long warmed = st1[ST_Warmed] - st0[ST_Warmed], drawn = st2[ST_Links] - st1[ST_Links];
        if (pass == 1) {
            CHECK(warmed == 0 && drawn > 0, "pass 1: %llu warmed, %llu linked while drawing", warmed, drawn);
        } else {
            CHECK(warmed > 0 && drawn == 0, "pass %d: %llu warmed, %llu linked while drawing", pass, warmed, drawn);
            CHECK(st1[ST_Links] - st0[ST_Links] == warmed, "pass %d: links at load besides the warmed ones", pass);
        }
        cgDestroyContext(g_ctx);
        printf("pairs pass %d %s (%d failures)\n", pass, g_failures ? "FAILED" : "ok", g_failures);
        return g_failures ? 1 : 0;
    }

    if (argc >= 2 && !strcmp(argv[1], "--bench")) {
        int frames = argc >= 3 ? atoi(argv[2]) : 300;
        bench(&a, frames, 0);
        bench(&a, frames, 1);
        return g_failures ? 1 : 0;
    }

    for (int f = 0; f < 40; f++) run_frame(&a, f, "context A");
    if (metal) {
        // No shared contexts on the Metal backend yet: context A only.
        if (argc > 1) corpus_churn(&a, argc - 1, argv + 1);
        for (int i = 0; i < NVP; i++) cgDestroyProgram(g_vp[i]);
        for (int j = 0; j < NFP; j++) cgDestroyProgram(g_fp[j]);
        CHECK(glGetError() == GL_NO_ERROR, "GL error after destroying the programs");
        cgDestroyContext(g_ctx);
        printf("glsl on metal %s (%d failures)\n", g_failures ? "FAILED" : "ok", g_failures);
        return g_failures ? 1 : 0;
    }

    // A second context sharing A's objects, and back.
    if (!make_gpu(&shared, a.cgl, &a)) { printf("no shared GL context\n"); return 1; }
    for (int f = 0; f < 10; f++) {
        switch_to(f % 2 ? &a : &shared);
        run_frame(f % 2 ? &a : &shared, f, f % 2 ? "context A (after B)" : "context B (shared)");
    }
    if (g_glsl) {
        if (!make_gpu(&alone, NULL, NULL)) { printf("no third GL context\n"); return 1; }
        for (int f = 0; f < 10; f++) {
            switch_to(f % 2 ? &a : &alone);
            run_frame(f % 2 ? &a : &alone, f, f % 2 ? "context A (after C)" : "context C (own share group)");
        }
    }
    switch_to(&a);
    if (argc > 1) corpus_churn(&a, argc - 1, argv + 1);

    // Destroying programs while bound must leave no dangling GL program current.
    for (int i = 0; i < NVP; i++) cgDestroyProgram(g_vp[i]);
    for (int j = 0; j < NFP; j++) cgDestroyProgram(g_fp[j]);
    glUseProgram(0);
    CHECK(glGetError() == GL_NO_ERROR, "GL error after destroying the programs");
    cgDestroyContext(g_ctx);
    printf("%s %s (%d failures)\n", g_glsl ? "glsl" : "arb", g_failures ? "FAILED" : "ok", g_failures);
    return g_failures ? 1 : 0;
}
