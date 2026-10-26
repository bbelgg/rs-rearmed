// End-to-end test of the Cg shim on a real GL context: create programs from
// hand-written Cg-style ARB objects, set uniforms/textures/attributes through the
// Cg API, draw into a float FBO and check exact pixel values. Needs no game data.
//
//   RS_CG_BACKEND=glsl render_test      (ctest runs it for both backends)
#define GL_SILENCE_DEPRECATION
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../src/cg_api.h"

#define W 4
#define H 4

static int g_failures;

#define CHECK(cond, ...)                                                       \
    do {                                                                       \
        if (!(cond)) {                                                         \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                        \
            printf(__VA_ARGS__);                                               \
            printf("\n");                                                      \
            g_failures++;                                                      \
        }                                                                      \
    } while (0)

static void check_pixel(const char *what, int x, int y, const float want[4]) {
    float px[4];
    glReadPixels(x, y, 1, 1, GL_RGBA, GL_FLOAT, px);
    for (int c = 0; c < 4; c++)
        if (fabsf(px[c] - want[c]) > 1e-4f) {
            printf("FAIL %s: pixel (%d,%d) = (%g %g %g %g), want (%g %g %g %g)\n", what, x, y, px[0], px[1],
                   px[2], px[3], want[0], want[1], want[2], want[3]);
            g_failures++;
            return;
        }
}

static CGcontext g_ctx;

static CGprogram load(const char *src, CGprofile prof) {
    CGprogram p = cgCreateProgram(g_ctx, CG_OBJECT, src, prof, "main", NULL);
    cgGLLoadProgram(p);
    int e = cgGetError();
    CHECK(p && !e, "load failed: %s\n%s", cgGetErrorString(e), cgGetLastListing(g_ctx));
    return p;
}

static CGparameter named(CGprogram p, const char *n) {
    CGparameter q = cgGetNamedParameter(p, n);
    CHECK(q != NULL, "no parameter %s", n);
    return q;
}

// Full-screen quad; every input of `p` other than POSITION gets per-vertex data
// from the caller.
static const float QUAD[4][4] = {{-1, -1, 0, 1}, {1, -1, 0, 1}, {-1, 1, 0, 1}, {1, 1, 0, 1}};

static void clear(void) {
    glClearColor(0.5f, 0.5f, 0.5f, 0.5f);
    glClear(GL_COLOR_BUFFER_BIT);
}

// --- programs ----------------------------------------------------------------

// texcoord[0] = M * texcoord[1] + offset + (0.5, 1, 0, 0); M row-major in c[0..3].
static const char *VP_XFORM =
    "!!ARBvp1.0\n#program main\n"
    "#var float4 pos : $vin.POSITION : POSITION : 0 : 1\n"
    "#var float4 v : $vin.TEXCOORD1 : TEXCOORD1 : 1 : 1\n"
    "#var float4x4 M :  : c[0], 4 : -1 : 1\n"
    "#var float4 offset :  : c[4] : -1 : 1\n"
    "#var float4 unused :  :  : -1 : 0\n"
    "#var float4 main.pos : $vout.POSITION : HPOS : -1 : 1\n"
    "#var float4 main.t : $vout.TEXCOORD0 : TEX0 : -1 : 1\n"
    "#const c[5] = 0.5 1 0 0\n"
    "PARAM c[6] = { program.local[0..4],\n\t\t{ 0.5, 1, 0, 0 } };\n"
    "TEMP R0;\n"
    "DP4 R0.x, c[0], vertex.texcoord[1];\nDP4 R0.y, c[1], vertex.texcoord[1];\n"
    "DP4 R0.z, c[2], vertex.texcoord[1];\nDP4 R0.w, c[3], vertex.texcoord[1];\n"
    "ADD R0, R0, c[4];\nADD result.texcoord[0], R0, c[5];\n"
    "MOV result.position, vertex.position;\nEND\n";

// color = texcoord[0] * tint + 0.25
static const char *FP_TINT =
    "!!ARBfp1.0\n#program main\n"
    "#var float4 t : $vin.TEXCOORD0 : TEX0 : 0 : 1\n"
    "#var float4 tint :  : c[0] : -1 : 1\n"
    "#var float4 main : $vout.COLOR : COL : -1 : 1\n"
    "#const c[1] = 0.25 0.25 0.25 0.25\n"
    "PARAM c[2] = { program.local[0], { 0.25, 0.25, 0.25, 0.25 } };\n"
    "MAD result.color, fragment.texcoord[0], c[0], c[1];\nEND\n";

// texcoord[0] = uv, color = vertex color (unsigned byte, normalized)
static const char *VP_TEX =
    "!!ARBvp1.0\n#program main\n"
    "#var float4 pos : $vin.POSITION : POSITION : 0 : 1\n"
    "#var float4 col : $vin.COLOR0 : COLOR0 : 1 : 1\n"
    "#var float2 uv : $vin.TEXCOORD0 : TEXCOORD0 : 2 : 1\n"
    "#var float4 main.pos : $vout.POSITION : HPOS : -1 : 1\n"
    "MOV result.position, vertex.position;\nMOV result.color, vertex.color;\n"
    "MOV result.texcoord[0], vertex.texcoord[0];\nEND\n";

static const char *FP_TEX =
    "!!ARBfp1.0\n#program main\n"
    "#var float2 uv : $vin.TEXCOORD0 : TEX0 : 0 : 1\n"
    "#var float4 c : $vin.COLOR0 : COL0 : 1 : 1\n"
    "#var sampler2D unusedSampler : TEXUNIT0 :  : -1 : 0\n"
    "#var sampler2D img : TEXUNIT3 : texunit 3 : -1 : 1\n"
    "TEMP R0;\nTEX R0, fragment.texcoord[0], texture[3], 2D;\n"
    "MUL result.color, R0, fragment.color;\nEND\n";

// Bone-palette style lookup: color = pal[index] with index from ATTR7.
static const char *VP_PALETTE =
    "!!ARBvp1.0\n#program main\n"
    "#var float4 pos : $vin.POSITION : POSITION : 0 : 1\n"
    "#var float4 idx : $vin.ATTR7 : ATTR7 : 1 : 1\n"
    "#var float4 pal[0] :  : c[1] : -1 : 1\n#var float4 pal[1] :  : c[2] : -1 : 1\n"
    "#var float4 pal[2] :  : c[3] : -1 : 1\n"
    "#const c[0] = 1 0 0 0\n"
    "PARAM c[4] = { { 1, 0, 0, 0 },\n\t\tprogram.local[1..3] };\n"
    "ADDRESS A0;\nARL A0.x, vertex.attrib[7].x;\n"
    "MOV result.texcoord[0], c[A0.x + 1];\nMOV result.position, vertex.position;\nEND\n";

static const char *FP_PASS =
    "!!ARBfp1.0\n#program main\n"
    "MOV result.color, fragment.texcoord[0];\nEND\n";

// Discard where texcoord[0].x < 0.
static const char *FP_KILL =
    "!!ARBfp1.0\n#program main\n"
    "KIL fragment.texcoord[0].x;\nMOV result.color, {1, 0, 1, 1};\nEND\n";

// --- tests -------------------------------------------------------------------

static void feed(CGparameter q, int size, GLenum type, const void *data) {
    cgGLEnableClientState(q);
    cgGLSetParameterPointer(q, size, type, 0, data);
}

static void test_introspection(void) {
    CGprogram vp = load(VP_XFORM, CG_PROFILE_ARBVP1);
    CGprogram fp = load(FP_TEX, CG_PROFILE_ARBFP1);
    CGparameter m = named(vp, "M");
    CHECK(cgGetParameterType(m) == CG_FLOAT4x4, "M type %d", cgGetParameterType(m));
    CHECK(cgGetParameterVariability(m) == CG_UNIFORM, "M variability");
    CHECK(cgGetParameterResourceIndex(m) == 0, "M index");
    CHECK(cgGetParameterResourceIndex(named(vp, "offset")) == 4, "offset index");
    CHECK(!cgIsParameterUsed(named(vp, "unused"), vp), "unused is used");
    CHECK(cgGetFirstLeafParameter(vp, CG_PROGRAM) == named(vp, "pos"), "first program param");
    CHECK(!strcmp(cgGetParameterSemantic(named(vp, "v")), "TEXCOORD1"), "semantic");
    CHECK(cgGetParameterDirection(named(vp, "main.t")) == CG_OUT, "output direction");
    int n = 0;
    for (CGparameter q = cgGetFirstLeafParameter(vp, CG_GLOBAL); q; q = cgGetNextLeafParameter(q)) n++;
    CHECK(n == 5, "%d global params, want 5", n);
    CHECK(cgGLGetTextureEnum(named(fp, "img")) == GL_TEXTURE3, "texture enum");
    CHECK(cgGLGetTextureEnum(named(fp, "unusedSampler")) == GL_INVALID_OPERATION, "unused sampler enum");
    cgGetError();
    CHECK(!strcmp(cgGetProgramString(vp, CG_PROGRAM_SOURCE), VP_XFORM), "program source");
    CHECK(cgGetNamedParameter(vp, "nope") == NULL, "missing param");
    CHECK(cgGetParameterName((CGparameter)(void *)0x1234) == NULL && cgGetError() != 0, "bad handle");
    cgDestroyProgram(vp);
    cgDestroyProgram(fp);
}

static void test_uniforms(void) {
    CGprogram vp = load(VP_XFORM, CG_PROFILE_ARBVP1);
    CGprogram fp = load(FP_TINT, CG_PROFILE_ARBFP1);
    cgGLEnableProfile(CG_PROFILE_ARBVP1);
    cgGLEnableProfile(CG_PROFILE_ARBFP1);
    cgGLBindProgram(vp);
    cgGLBindProgram(fp);
    // Not symmetric, so a column-major upload would show.
    const float M[16] = {1, 2, 0, 0, 0, 1, 0, 3, 0, 0, 2, 0, 1, 0, 0, 1};
    const float off[4] = {0.125f, 0, 0, 0}, tint[4] = {1, 0.5f, 2, 1};
    cgGLSetMatrixParameterfr(named(vp, "M"), M);
    cgGLSetParameter4fv(named(vp, "offset"), off);
    cgGLSetParameter4fv(named(fp, "tint"), tint);
    float v[4][4];
    for (int i = 0; i < 4; i++) {
        v[i][0] = 0.25f; v[i][1] = 0.5f; v[i][2] = -0.25f; v[i][3] = 0.125f;
    }
    feed(named(vp, "pos"), 4, GL_FLOAT, QUAD);
    feed(named(vp, "v"), 4, GL_FLOAT, v);
    clear();
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    cgGLDisableClientState(named(vp, "pos"));
    cgGLDisableClientState(named(vp, "v"));
    float t[4];
    for (int r = 0; r < 4; r++) {
        t[r] = 0;
        for (int c = 0; c < 4; c++) t[r] += M[4 * r + c] * v[0][c];
    }
    const float k[4] = {0.5f, 1, 0, 0};
    float want[4];
    for (int c = 0; c < 4; c++) want[c] = (t[c] + off[c] + k[c]) * tint[c] + 0.25f;
    check_pixel("uniforms", 1, 2, want);
    CHECK(!cgGetError() && !glGetError(), "errors in uniforms test");
    cgDestroyProgram(vp);
    cgDestroyProgram(fp);
}

static void test_texture(void) {
    CGprogram vp = load(VP_TEX, CG_PROFILE_ARBVP1);
    CGprogram fp = load(FP_TEX, CG_PROFILE_ARBFP1);
    const float texels[2][2][4] = {{{1, 0, 0, 1}, {0, 1, 0, 1}}, {{0, 0, 1, 1}, {1, 1, 1, 0.5f}}};
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F_ARB, 2, 2, 0, GL_RGBA, GL_FLOAT, texels);
    glBindTexture(GL_TEXTURE_2D, 0);
    cgGLBindProgram(vp);
    cgGLBindProgram(fp);
    CGparameter img = named(fp, "img");
    cgGLSetTextureParameter(img, tex);
    cgGLEnableTextureParameter(img);
    GLint active = 0;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
    CHECK(active == GL_TEXTURE0, "active texture changed to 0x%x", active);
    const float uv[4][2] = {{0, 0}, {1, 0}, {0, 1}, {1, 1}};
    const unsigned char col[4][4] = {{255, 128, 0, 255}, {255, 128, 0, 255}, {255, 128, 0, 255}, {255, 128, 0, 255}};
    feed(named(vp, "pos"), 4, GL_FLOAT, QUAD);
    feed(named(vp, "uv"), 2, GL_FLOAT, uv);
    feed(named(vp, "col"), 4, GL_UNSIGNED_BYTE, col);
    clear();
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    cgGLDisableClientState(named(vp, "pos"));
    cgGLDisableClientState(named(vp, "uv"));
    cgGLDisableClientState(named(vp, "col"));
    cgGLDisableTextureParameter(img);
    const float c[4] = {1, 128 / 255.0f, 0, 1};
    for (int y = 0; y < H; y += 3)
        for (int x = 0; x < W; x += 3) {
            const float *t = texels[y / 2][x / 2];
            float want[4] = {t[0] * c[0], t[1] * c[1], t[2] * c[2], t[3] * c[3]};
            check_pixel("texture", x, y, want);
        }
    CHECK(!cgGetError() && !glGetError(), "errors in texture test");
    glDeleteTextures(1, &tex);
    cgDestroyProgram(vp);
    cgDestroyProgram(fp);
}

static void test_palette(void) {
    CGprogram vp = load(VP_PALETTE, CG_PROFILE_ARBVP1);
    CGprogram fp = load(FP_PASS, CG_PROFILE_ARBFP1);
    cgGLBindProgram(vp);
    cgGLBindProgram(fp);
    const float pal[3][4] = {{1, 0, 0, 1}, {0, 1, 0, 1}, {0, 0, 1, 0.75f}};
    cgGLSetParameter4fv(named(vp, "pal[0]"), pal[0]);
    cgGLSetParameter4fv(named(vp, "pal[1]"), pal[1]);
    cgGLSetParameter4fv(named(vp, "pal[2]"), pal[2]);
    for (int i = 0; i < 3; i++) {
        float idx[4][4];
        for (int v = 0; v < 4; v++) idx[v][0] = idx[v][1] = idx[v][2] = idx[v][3] = (float)i + 0.25f;
        feed(named(vp, "pos"), 4, GL_FLOAT, QUAD);
        feed(named(vp, "idx"), 4, GL_FLOAT, idx);
        clear();
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        check_pixel("palette", 2, 2, pal[i]);
    }
    cgGLDisableClientState(named(vp, "pos"));
    cgGLDisableClientState(named(vp, "idx"));
    CHECK(!cgGetError() && !glGetError(), "errors in palette test");
    cgDestroyProgram(vp);
    cgDestroyProgram(fp);
}

// Values set while another pair is bound must survive switching back.
static void test_rebind(void) {
    CGprogram vp = load(VP_XFORM, CG_PROFILE_ARBVP1);
    CGprogram a = load(FP_TINT, CG_PROFILE_ARBFP1), b = load(FP_TINT, CG_PROFILE_ARBFP1);
    const float I[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}, zero[4] = {0};
    const float ta[4] = {1, 0, 0, 1}, tb[4] = {0, 0, 1, 1};
    cgGLBindProgram(vp);
    cgGLSetMatrixParameterfr(named(vp, "M"), I);
    cgGLSetParameter4fv(named(vp, "offset"), zero);
    cgGLBindProgram(a);
    cgGLSetParameter4fv(named(a, "tint"), ta);
    cgGLBindProgram(b);
    cgGLSetParameter4fv(named(b, "tint"), tb);
    float v[4][4];
    for (int i = 0; i < 4; i++) { v[i][0] = v[i][1] = v[i][2] = 0.5f; v[i][3] = 1; }
    feed(named(vp, "pos"), 4, GL_FLOAT, QUAD);
    feed(named(vp, "v"), 4, GL_FLOAT, v);
    // t = v + (0.5, 1, 0, 0) = (1, 1.5, 0.5, 1)
    const float wa[4] = {1.25f, 0.25f, 0.25f, 1.25f}, wb[4] = {0.25f, 0.25f, 0.75f, 1.25f};
    cgGLBindProgram(a);
    clear();
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    check_pixel("rebind a", 0, 0, wa);
    cgGLBindProgram(b);
    clear();
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    check_pixel("rebind b", 0, 0, wb);
    cgGLSetParameter4fv(named(a, "tint"), tb);   // set while not bound
    cgGLBindProgram(a);
    clear();
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    check_pixel("rebind a after unbound set", 0, 0, wb);
    cgGLDisableClientState(named(vp, "pos"));
    cgGLDisableClientState(named(vp, "v"));
    CHECK(!cgGetError() && !glGetError(), "errors in rebind test");
    cgDestroyProgram(vp);
    cgDestroyProgram(a);
    cgDestroyProgram(b);
}

static void test_kill(void) {
    CGprogram vp = load(VP_TEX, CG_PROFILE_ARBVP1);
    CGprogram fp = load(FP_KILL, CG_PROFILE_ARBFP1);
    cgGLBindProgram(vp);
    cgGLBindProgram(fp);
    const float uv[4][2] = {{-1, 0}, {1, 0}, {-1, 0}, {1, 0}};
    feed(named(vp, "pos"), 4, GL_FLOAT, QUAD);
    feed(named(vp, "uv"), 2, GL_FLOAT, uv);
    clear();
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    cgGLDisableClientState(named(vp, "pos"));
    cgGLDisableClientState(named(vp, "uv"));
    const float kept[4] = {1, 0, 1, 1}, cleared[4] = {0.5f, 0.5f, 0.5f, 0.5f};
    check_pixel("kill left", 0, 1, cleared);
    check_pixel("kill right", 3, 1, kept);
    CHECK(!cgGetError() && !glGetError(), "errors in kill test");
    cgDestroyProgram(vp);
    cgDestroyProgram(fp);
}

// The game mixes Cg with GL programs of its own and with fixed function. Cg must
// not write into a program it doesn't own, must keep values set meanwhile, and
// must leave GL on program 0 once its profiles are disabled.
static GLuint own_program(void) {
    const char *src[2] = {"uniform vec4 u; void main() { gl_Position = gl_Vertex + u * 0.0; }",
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
    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    CHECK(ok, "own program doesn't link");
    return prog;
}

static void test_foreign_program(void) {
    CGprogram vp = load(VP_XFORM, CG_PROFILE_ARBVP1);
    CGprogram fp = load(FP_TINT, CG_PROFILE_ARBFP1);
    const float I[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}, zero[4] = {0};
    const float t1[4] = {1, 1, 1, 1}, t2[4] = {0, 0.5f, 0, 1}, nine[4] = {9, 9, 9, 9};
    cgGLBindProgram(vp);
    cgGLBindProgram(fp);
    cgGLSetMatrixParameterfr(named(vp, "M"), I);
    cgGLSetParameter4fv(named(vp, "offset"), zero);
    cgGLSetParameter4fv(named(fp, "tint"), t1);

    GLuint own = own_program();
    GLint u = glGetUniformLocation(own, "u");
    glUseProgram(own);
    glUniform4fv(u, 1, nine);
    cgGLSetParameter4fv(named(fp, "tint"), t2);   // while the game's program is current
    float got[4] = {0};
    glGetUniformfv(own, u, got);
    CHECK(got[0] == 9 && got[1] == 9 && got[2] == 9 && got[3] == 9,
          "Cg parameter landed in the game's program: u = (%g %g %g %g)", got[0], got[1], got[2], got[3]);
    cgGLDisableProfile(CG_PROFILE_ARBVP1);
    cgGLDisableProfile(CG_PROFILE_ARBFP1);
    GLint cur = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &cur);
    CHECK((GLuint)cur == own, "disabling the Cg profiles replaced the game's program (now %d)", cur);

    // Back to Cg: the tint set while the game's program was current must apply.
    glUseProgram(0);
    cgGLEnableProfile(CG_PROFILE_ARBVP1);
    cgGLEnableProfile(CG_PROFILE_ARBFP1);
    cgGLBindProgram(vp);
    cgGLBindProgram(fp);
    float v[4][4];
    for (int i = 0; i < 4; i++) { v[i][0] = v[i][1] = v[i][2] = 0.5f; v[i][3] = 1; }
    feed(named(vp, "pos"), 4, GL_FLOAT, QUAD);
    feed(named(vp, "v"), 4, GL_FLOAT, v);
    clear();
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    cgGLDisableClientState(named(vp, "pos"));
    cgGLDisableClientState(named(vp, "v"));
    // t = v + (0.5, 1, 0, 0) = (1, 1.5, 0.5, 1); color = t * t2 + 0.25
    const float want[4] = {0.25f, 1.0f, 0.25f, 1.25f};
    check_pixel("tint set while the game's program was current", 0, 0, want);

    cgGLDisableProfile(CG_PROFILE_ARBVP1);
    cgGLDisableProfile(CG_PROFILE_ARBFP1);
    glGetIntegerv(GL_CURRENT_PROGRAM, &cur);
    CHECK(cur == 0, "disabled Cg profiles left program %d current", cur);
    cgGLEnableProfile(CG_PROFILE_ARBVP1);
    cgGLEnableProfile(CG_PROFILE_ARBFP1);
    CHECK(!cgGetError() && !glGetError(), "errors in foreign program test");
    glDeleteProgram(own);
    cgDestroyProgram(vp);
    cgDestroyProgram(fp);
}

static void test_errors(void) {
    CGprogram src = cgCreateProgram(g_ctx, CG_SOURCE, "float4 main() : COLOR { return 1; }",
                                    CG_PROFILE_ARBFP1, "main", NULL);
    CHECK(src == NULL && cgGetError() != 0, "CG_SOURCE should fail without a compiler");
    CGprogram bad = cgCreateProgram(g_ctx, CG_OBJECT, "float4 main() : COLOR { return 1; }",
                                    CG_PROFILE_ARBFP1, "main", NULL);
    CHECK(bad != NULL, "non-object text is accepted at create time, like NVIDIA's runtime");
    cgGLLoadProgram(bad);
    CHECK(cgGetError() != 0, "loading non-object text must fail");
    cgGLBindProgram(bad);
    CHECK(cgGetError() != 0, "binding an unloaded program must fail");
    cgDestroyProgram(bad);
}

int main(void) {
    CGLPixelFormatAttribute attrs[] = {kCGLPFAAccelerated, 0};
    CGLPixelFormatObj pf;
    GLint npf;
    CGLContextObj glctx;
    if (CGLChoosePixelFormat(attrs, &pf, &npf) || CGLCreateContext(pf, NULL, &glctx) ||
        CGLSetCurrentContext(glctx)) {
        printf("no GL context\n");
        return 1;
    }
    GLuint fbo, color;
    glGenTextures(1, &color);
    glBindTexture(GL_TEXTURE_2D, color);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F_ARB, W, H, 0, GL_RGBA, GL_FLOAT, NULL);
    glBindTexture(GL_TEXTURE_2D, 0);
    glGenFramebuffersEXT(1, &fbo);
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fbo);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_2D, color, 0);
    glViewport(0, 0, W, H);
    g_ctx = cgCreateContext();
    cgGLSetContextOptimalOptions(g_ctx, CG_PROFILE_ARBVP1);
    cgGLEnableProfile(CG_PROFILE_ARBVP1);
    cgGLEnableProfile(CG_PROFILE_ARBFP1);

    test_introspection();
    test_uniforms();
    test_texture();
    test_palette();
    test_rebind();
    test_kill();
    test_foreign_program();
    test_errors();

    cgDestroyContext(g_ctx);
    printf("%s (%d failures)\n", g_failures ? "FAILED" : "ok", g_failures);
    return g_failures ? 1 : 0;
}
