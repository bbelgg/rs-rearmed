// Differential render test: draw every program through the shim and write the
// float framebuffer to <outdir>. Run once with RS_CG_BACKEND=arb (Apple's GL
// executes the ARB object natively, as NVIDIA's runtime would have it) and once
// with RS_CG_BACKEND=glsl (the translation); tests/compare_renders.py diffs them.
//
//   diff_render <outdir> <program files...>
//
// Fragment programs: a probe vertex program feeds every varying with
// interpolated random values over a full-screen quad.
// Vertex programs: drawn as 512 points with random attributes, once per varying
// they write, each time with a probe fragment program that outputs that varying.
// Uniforms and textures are random but seeded by file name, so both runs see
// the same inputs.
#define GL_SILENCE_DEPRECATION
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/cg_api.h"

#define W 64
#define H 64
#define NPOINTS 512

static uint32_t g_rng;
static float frand(float lo, float hi) {
    g_rng = g_rng * 1664525u + 1013904223u;
    return lo + (hi - lo) * ((g_rng >> 8) / 16777216.0f);
}

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

static const char *PROBE_VP =
    "!!ARBvp1.0\n"
    "#var float4 p : $vin.POSITION : POSITION : 0 : 1\n"
    "#var float4 t0 : $vin.TEXCOORD0 : TEXCOORD0 : 1 : 1\n"
    "#var float4 t1 : $vin.TEXCOORD1 : TEXCOORD1 : 2 : 1\n"
    "#var float4 t2 : $vin.TEXCOORD2 : TEXCOORD2 : 3 : 1\n"
    "#var float4 t3 : $vin.TEXCOORD3 : TEXCOORD3 : 4 : 1\n"
    "#var float4 t4 : $vin.TEXCOORD4 : TEXCOORD4 : 5 : 1\n"
    "#var float4 t5 : $vin.TEXCOORD5 : TEXCOORD5 : 6 : 1\n"
    "#var float4 t6 : $vin.TEXCOORD6 : TEXCOORD6 : 7 : 1\n"
    "#var float4 t7 : $vin.TEXCOORD7 : TEXCOORD7 : 8 : 1\n"
    "#var float4 c0 : $vin.COLOR0 : COLOR0 : 9 : 1\n"
    "MOV result.position, vertex.position;\n"
    "MOV result.texcoord[0], vertex.texcoord[0];\nMOV result.texcoord[1], vertex.texcoord[1];\n"
    "MOV result.texcoord[2], vertex.texcoord[2];\nMOV result.texcoord[3], vertex.texcoord[3];\n"
    "MOV result.texcoord[4], vertex.texcoord[4];\nMOV result.texcoord[5], vertex.texcoord[5];\n"
    "MOV result.texcoord[6], vertex.texcoord[6];\nMOV result.texcoord[7], vertex.texcoord[7];\n"
    "MOV result.color, vertex.color;\nMOV result.color.secondary, vertex.texcoord[1];\n"
    "MOV result.fogcoord.x, vertex.texcoord[2].z;\n"
    "END\n";

static GLuint g_tex2d, g_texcube, g_tex1d;

static void make_textures(void) {
    static uint8_t px[6 * 16 * 16 * 4];   // six cube faces
    g_rng = 12345;
    for (size_t i = 0; i < sizeof px; i++) px[i] = (uint8_t)frand(0, 255.99f);
    glGenTextures(1, &g_tex2d);
    glBindTexture(GL_TEXTURE_2D, g_tex2d);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 16, 16, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    glGenTextures(1, &g_tex1d);
    glBindTexture(GL_TEXTURE_1D, g_tex1d);
    glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage1D(GL_TEXTURE_1D, 0, GL_RGBA8, 16, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    glGenTextures(1, &g_texcube);
    glBindTexture(GL_TEXTURE_CUBE_MAP, g_texcube);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    for (int f = 0; f < 6; f++)
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + f, 0, GL_RGBA8, 16, 16, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, px + f * 16 * 16 * 4);
}

static int is_matrix(CGtype t) {
    return t == CG_FLOAT4x4 || t == CG_FLOAT3x4 || t == CG_FLOAT3x3 || t == CG_FLOAT4x3 ||
           t == CG_FLOAT4x2 || t == CG_FLOAT2x2 || t == CG_HALF4x4;
}

// Random uniforms; matrices near identity so vertex programs keep points on screen.
static void set_uniforms(CGprogram p) {
    CGenum spaces[] = {CG_PROGRAM, CG_GLOBAL};
    for (int s = 0; s < 2; s++)
        for (CGparameter q = cgGetFirstLeafParameter(p, spaces[s]); q; q = cgGetNextLeafParameter(q)) {
            if (cgGetParameterVariability(q) != CG_UNIFORM || cgGetParameterDirection(q) != CG_IN) continue;
            CGtype t = cgGetParameterType(q);
            if (t >= CG_SAMPLER1D && t <= CG_SAMPLERCUBE) continue;
            if (t == CG_SAMPLER || t == CG_TEXTURE) continue;
            if (is_matrix(t)) {
                float m[16];
                for (int i = 0; i < 16; i++) m[i] = (i % 5 == 0 ? 0.6f : 0.0f) + frand(-0.1f, 0.1f);
                cgGLSetMatrixParameterfr(q, m);
            } else {
                float v[4] = {frand(-1, 1), frand(-1, 1), frand(-1, 1), frand(-1, 1)};
                cgGLSetParameter4fv(q, v);
            }
        }
}

static void bind_textures(CGprogram p) {
    // Every unit gets a known texture first, so a sampler the parameter table
    // doesn't cover can't read the render target or a stale binding.
    for (int u = 0; u < 16; u++) {
        glActiveTexture(GL_TEXTURE0 + u);
        glBindTexture(GL_TEXTURE_2D, g_tex2d);
        glBindTexture(GL_TEXTURE_CUBE_MAP, g_texcube);
        glBindTexture(GL_TEXTURE_1D, g_tex1d);
    }
    glActiveTexture(GL_TEXTURE0);
    CGenum spaces[] = {CG_PROGRAM, CG_GLOBAL};
    for (int s = 0; s < 2; s++)
    for (CGparameter q = cgGetFirstLeafParameter(p, spaces[s]); q; q = cgGetNextLeafParameter(q)) {
        CGtype t = cgGetParameterType(q);
        if (!(t >= CG_SAMPLER1D && t <= CG_SAMPLERCUBE) && t != CG_SAMPLER) continue;
        if (!cgIsParameterUsed(q, p)) continue;
        GLuint tex = t == CG_SAMPLERCUBE ? g_texcube : t == CG_SAMPLER1D ? g_tex1d : g_tex2d;
        cgGLSetTextureParameter(q, tex);
        cgGLEnableTextureParameter(q);
    }
    cgGetError();
}

static float g_attr[16][NPOINTS * 4];

// Feed every vertex input of `p` from client arrays of random data.
static int bind_inputs(CGprogram p, int npoints, int clip_positions) {
    int n = 0;
    for (CGparameter q = cgGetFirstLeafParameter(p, CG_PROGRAM); q; q = cgGetNextLeafParameter(q)) {
        if (cgGetParameterVariability(q) != CG_VARYING || cgGetParameterDirection(q) != CG_IN) continue;
        const char *sem = cgGetParameterSemantic(q);
        float *a = g_attr[n % 16];
        for (int i = 0; i < npoints * 4; i++) {
            // Bone indices (Cg binds them to ATTR7) must stay inside the palette.
            if (!strncmp(sem, "BLENDINDICES", 12) || !strcmp(sem, "ATTR7")) a[i] = (float)(int)frand(0, 3);
            else if (!strncmp(sem, "COLOR", 5)) a[i] = frand(0, 1);
            else a[i] = frand(-0.9f, 0.9f);
        }
        if (clip_positions && !strncmp(sem, "POSITION", 8))
            for (int i = 0; i < npoints; i++) a[4 * i + 3] = 1.0f;
        cgGLEnableClientState(q);
        cgGLSetParameterPointer(q, 4, GL_FLOAT, 16, a);
        n++;
    }
    cgGetError();
    return n;
}

static void unbind_inputs(CGprogram p) {
    for (CGparameter q = cgGetFirstLeafParameter(p, CG_PROGRAM); q; q = cgGetNextLeafParameter(q))
        if (cgGetParameterVariability(q) == CG_VARYING && cgGetParameterDirection(q) == CG_IN)
            cgGLDisableClientState(q);
    cgGetError();
}

static void save(const char *dir, const char *name, const char *pass) {
    static float px[W * H * 4];
    glReadPixels(0, 0, W, H, GL_RGBA, GL_FLOAT, px);
    char path[2048];
    snprintf(path, sizeof path, "%s/%s.%s.f32", dir, name, pass);
    FILE *f = fopen(path, "wb");
    if (f) {
        fwrite(px, sizeof px, 1, f);
        fclose(f);
    }
}

// Components of `out` the program writes, as a write mask ("xyz"), or "" if
// none. "result.color" must not match "result.color.secondary". Unwritten
// components are undefined in ARB, so the probe only looks at written ones.
static void written_mask(const char *src, const char *out, char mask[5]) {
    int bits = 0;
    size_t n = strlen(out);
    for (const char *h = strstr(src, out); h; h = strstr(h + 1, out)) {
        const char *t = h + n;
        if (!strcmp(out, "result.color") && !strncmp(t, ".secondary", 10)) continue;
        if (*t == '.') {
            for (t++; *t && strchr("xyzw", *t); t++) bits |= 1 << (strchr("xyzw", *t) - "xyzw");
        } else {
            bits = 15;
        }
    }
    int k = 0;
    for (int c = 0; c < 4; c++)
        if (bits & (1 << c)) mask[k++] = "xyzw"[c];
    mask[k] = 0;
}

static uint32_t name_seed(const char *s) {
    uint32_t h = 2166136261u;
    while (*s) h = (h ^ (uint8_t)*s++) * 16777619u;
    return h;
}

static CGprogram create(CGcontext ctx, const char *src, CGprofile prof) {
    CGprogram p = cgCreateProgram(ctx, CG_OBJECT, src, prof, "main", NULL);
    cgGLLoadProgram(p);
    if (cgGetError()) {
        cgDestroyProgram(p);
        return NULL;
    }
    return p;
}

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: %s outdir files...\n", argv[0]); return 64; }
    CGLPixelFormatAttribute attrs[] = {kCGLPFAAccelerated, 0};
    CGLPixelFormatObj pf;
    GLint npf;
    CGLContextObj glctx;
    if (CGLChoosePixelFormat(attrs, &pf, &npf) || CGLCreateContext(pf, NULL, &glctx) ||
        CGLSetCurrentContext(glctx)) {
        fprintf(stderr, "no GL context\n");
        return 1;
    }
    GLuint fbo, color;
    glGenTextures(1, &color);
    glBindTexture(GL_TEXTURE_2D, color);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F_ARB, W, H, 0, GL_RGBA, GL_FLOAT, NULL);
    glGenFramebuffersEXT(1, &fbo);
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fbo);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_2D, color, 0);
    if (glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT) != GL_FRAMEBUFFER_COMPLETE_EXT) {
        fprintf(stderr, "FBO incomplete\n");
        return 1;
    }
    glViewport(0, 0, W, H);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glPointSize(1);
    make_textures();

    CGcontext ctx = cgCreateContext();
    CGprogram probe_vp = create(ctx, PROBE_VP, CG_PROFILE_ARBVP1);
    if (!probe_vp) { fprintf(stderr, "probe VP failed: %s\n", cgGetLastListing(ctx)); return 1; }
    cgGLEnableProfile(CG_PROFILE_ARBVP1);
    cgGLEnableProfile(CG_PROFILE_ARBFP1);
    int rendered = 0;
    for (int i = 2; i < argc; i++) {
        char *src = slurp(argv[i]);
        if (!src || strncmp(src, "!!ARB", 5)) { free(src); continue; }
        const char *name = strrchr(argv[i], '/') ? strrchr(argv[i], '/') + 1 : argv[i];
        int frag = !strncmp(src, "!!ARBfp", 7);
        CGprogram p = create(ctx, src, frag ? CG_PROFILE_ARBFP1 : CG_PROFILE_ARBVP1);
        if (!p) { printf("load failed: %s\n", name); free(src); continue; }
        if (frag) {
            g_rng = name_seed(name);
            glClearColor(0.25f, 0.5f, 0.75f, 1);
            glClear(GL_COLOR_BUFFER_BIT);
            cgGLBindProgram(probe_vp);
            cgGLBindProgram(p);
            set_uniforms(p);
            bind_textures(p);
            static const float corners[4][2] = {{-1, -1}, {1, -1}, {-1, 1}, {1, 1}};
            bind_inputs(probe_vp, 4, 1);
            for (int v = 0; v < 4; v++) {   // position attribute is g_attr[0]
                g_attr[0][4 * v] = corners[v][0];
                g_attr[0][4 * v + 1] = corners[v][1];
                g_attr[0][4 * v + 2] = 0;
            }
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
            unbind_inputs(probe_vp);
            save(argv[1], name, "frag");
        } else {
            static const char *outs[] = {"result.texcoord[0]", "result.texcoord[1]", "result.texcoord[2]",
                                         "result.texcoord[3]", "result.texcoord[4]", "result.texcoord[5]",
                                         "result.texcoord[6]", "result.texcoord[7]", "result.color",
                                         "result.color.secondary", "result.fogcoord"};
            static const char *reads[] = {"fragment.texcoord[0]", "fragment.texcoord[1]", "fragment.texcoord[2]",
                                          "fragment.texcoord[3]", "fragment.texcoord[4]", "fragment.texcoord[5]",
                                          "fragment.texcoord[6]", "fragment.texcoord[7]", "fragment.color",
                                          "fragment.color.secondary", "fragment.fogcoord"};
            // Points land where result.position puts them; if some of its
            // components are never written they are undefined and so is the image.
            char pos_mask[5];
            written_mask(src, "result.position", pos_mask);
            if (strcmp(pos_mask, "xyzw")) {
                printf("skipped %s: result.position.%s only (w undefined)\n", name, pos_mask);
                cgDestroyProgram(p);
                free(src);
                continue;
            }
            for (int k = 0; k < 11; k++) {
                char mask[5];
                written_mask(src, outs[k], mask);
                if (k == 10 && strchr(mask, 'x')) strcpy(mask, "x");   // only fog.x is passed on
                if (!mask[0]) continue;
                char fp_src[256];
                snprintf(fp_src, sizeof fp_src,
                         "!!ARBfp1.0\nMOV result.color, {0.125, 0.125, 0.125, 0.125};\n"
                         "MOV result.color.%s, %s;\nEND\n", mask, reads[k]);
                CGprogram probe_fp = create(ctx, fp_src, CG_PROFILE_ARBFP1);
                g_rng = name_seed(name);
                glClearColor(0.25f, 0.5f, 0.75f, 1);
                glClear(GL_COLOR_BUFFER_BIT);
                cgGLBindProgram(p);
                cgGLBindProgram(probe_fp);
                set_uniforms(p);
                bind_inputs(p, NPOINTS, 1);
                glDrawArrays(GL_POINTS, 0, NPOINTS);
                unbind_inputs(p);
                char pass[16];
                snprintf(pass, sizeof pass, "vary%d", k);
                save(argv[1], name, pass);
                cgDestroyProgram(probe_fp);
            }
        }
        GLenum e = glGetError();
        if (e) printf("GL error 0x%x after %s\n", e, name);
        cgDestroyProgram(p);
        rendered++;
        free(src);
    }
    printf("rendered %d programs\n", rendered);
    return 0;
}
