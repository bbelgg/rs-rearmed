// Metal core tests: one GL call sequence per case, run through an rs_gfx
// offscreen context on the backend RS_GL_BACKEND selects, with its own checks.
//
//   metal_core_test <case> [--dump FILE]   run on the current backend; FILE gets
//                                          the case's pixels (and other dumps)
//   metal_core_test --compare <case>       run <case> on gl and on metal (child
//                                          processes) and compare the dumps
//   metal_core_test --list                 case names
//
// Output ends with "N passed, M failed". Geometry sits on pixel boundaries (or
// off pixel centres), so coverage is the same on both backends and pixels can
// be compared exactly (blending: +-1).
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>

#include "gl_api.h"
#include "rs_gfx.h"

#include <math.h>
#include <spawn.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define GL_VERTEX_ARRAY_BINDING 0x85B5
#define GL_MAX_VERTEX_UNIFORM_VECTORS 0x8DFB
#define GL_PROGRAM_BINARY_RETRIEVABLE_HINT 0x8257

extern char **environ;

enum { W = 64, H = 64 };

static int passed, failed;
static const char *g_backend;

static void check(int ok, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void check(int ok, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    if (ok) {
        passed++;
    } else {
        failed++;
        printf("FAIL [%s] ", g_backend);
        vprintf(fmt, ap);
        printf("\n");
    }
    va_end(ap);
}

static void glok(const char *what) {
    GLenum e = glGetError();
    check(e == GL_NO_ERROR, "%s: glGetError 0x%04x", what, e);
}

// --- Dump ---------------------------------------------------------------------------
static unsigned char *g_dump;
static size_t g_dump_len, g_dump_cap;

static void dump(const void *p, size_t n) {
    if (g_dump_len + n > g_dump_cap) {
        g_dump_cap = (g_dump_len + n) * 2;
        g_dump = realloc(g_dump, g_dump_cap);
    }
    memcpy(g_dump + g_dump_len, p, n);
    g_dump_len += n;
}

static void dump_int(int v) { dump(&v, sizeof v); }

static unsigned char g_px[W * H * 4];

// Read the bound framebuffer (W x H) into g_px and the dump.
static void snap(void) {
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    memset(g_px, 0x5A, sizeof g_px);
    glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, g_px);
    dump(g_px, sizeof g_px);
}

static const unsigned char *px(int x, int y) { return g_px + ((size_t)y * W + (size_t)x) * 4; }

static int near(int a, int b, int tol) { return abs(a - b) <= tol; }

static void check_px(int x, int y, int r, int g, int b, int a, const char *what) {
    const unsigned char *p = px(x, y);
    int ok = near(p[0], r, 1) && near(p[1], g, 1) && near(p[2], b, 1) && (a < 0 || near(p[3], a, 1));
    check(ok, "%s: pixel (%d,%d) = %d,%d,%d,%d want %d,%d,%d,%d", what, x, y, p[0], p[1], p[2], p[3], r, g, b, a);
}

// --- GL helpers -------------------------------------------------------------------------
static GLuint shader(GLenum type, const char *src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048] = "";
        glGetShaderInfoLog(s, sizeof log, NULL, log);
        printf("shader log: %s\n", log);
    }
    check(ok, "shader compiles");
    return s;
}

// Program with attributes bound in order (NULL-terminated list).
static GLuint program(const char *vs, const char *fs, ...) {
    GLuint p = glCreateProgram();
    GLuint v = shader(GL_VERTEX_SHADER, vs), f = shader(GL_FRAGMENT_SHADER, fs);
    glAttachShader(p, v);
    glAttachShader(p, f);
    va_list ap;
    va_start(ap, fs);
    const char *a;
    for (GLuint i = 0; (a = va_arg(ap, const char *)); i++)
        glBindAttribLocation(p, i, a);
    va_end(ap);
    glLinkProgram(p);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[4096] = "";
        glGetProgramInfoLog(p, sizeof log, NULL, log);
        printf("link log: %s\n", log);
    }
    check(ok, "program links");
    glDeleteShader(v);
    glDeleteShader(f);
    glUseProgram(p);
    return p;
}

static void uniform4(GLuint p, const char *name, float r, float g, float b, float a) {
    glUniform4f(glGetUniformLocation(p, name), r, g, b, a);
}

static const char kVS[] =
    "#version 120\n"
    "attribute vec2 pos;\n"
    "void main() { gl_Position = vec4(pos, 0.0, 1.0); }\n";
static const char kVSz[] =
    "#version 120\n"
    "attribute vec3 pos;\n"
    "void main() { gl_Position = vec4(pos, 1.0); }\n";
static const char kFScolor[] =
    "#version 120\n"
    "uniform vec4 color;\n"
    "void main() { gl_FragColor = color; }\n";
static const char kVScol[] =
    "#version 120\n"
    "attribute vec2 pos;\n"
    "attribute vec4 col;\n"
    "varying vec4 v_col;\n"
    "void main() { v_col = col; gl_Position = vec4(pos, 0.0, 1.0); }\n";
static const char kFSvcol[] =
    "#version 120\n"
    "varying vec4 v_col;\n"
    "void main() { gl_FragColor = v_col; }\n";
static const char kVStex[] =
    "#version 120\n"
    "attribute vec2 pos;\n"
    "attribute vec2 tc;\n"
    "varying vec2 v_tc;\n"
    "void main() { v_tc = tc; gl_Position = vec4(pos, 0.0, 1.0); }\n";
static const char kFStex[] =
    "#version 120\n"
    "uniform sampler2D tex;\n"
    "varying vec2 v_tc;\n"
    "void main() { gl_FragColor = texture2D(tex, v_tc); }\n";

// Pixel rectangle -> NDC quad as a triangle strip (x0,y0 bottom-left, in pixels).
static void rect(float *v, int x0, int y0, int x1, int y1) {
    float l = x0 * 2.0f / W - 1, r = x1 * 2.0f / W - 1, b = y0 * 2.0f / H - 1, t = y1 * 2.0f / H - 1;
    float q[8] = {l, b, r, b, l, t, r, t};
    memcpy(v, q, sizeof q);
}

static GLuint g_vbo;

// Draw a pixel rectangle with the current program (attribute 0 = pos).
static void draw_rect(int x0, int y0, int x1, int y1) {
    float v[8];
    rect(v, x0, y0, x1, y1);
    if (!g_vbo)
        glGenBuffers(1, &g_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof v, v, GL_STREAM_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
    glEnableVertexAttribArray(0);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

// Textured rectangle: pos at 0, tc at 1 (0..1 over the rectangle).
static void draw_tex_rect(int x0, int y0, int x1, int y1) {
    float v[16];
    float p[8];
    rect(p, x0, y0, x1, y1);
    static const float tc[8] = {0, 0, 1, 0, 0, 1, 1, 1};
    for (int i = 0; i < 4; i++) {
        v[i * 4] = p[i * 2];
        v[i * 4 + 1] = p[i * 2 + 1];
        v[i * 4 + 2] = tc[i * 2];
        v[i * 4 + 3] = tc[i * 2 + 1];
    }
    if (!g_vbo)
        glGenBuffers(1, &g_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof v, v, GL_STREAM_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 16, 0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 16, (void *)8);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

static GLuint texture_rgba(int w, int h, const unsigned char *data, GLenum filter) {
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, (GLint)filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, (GLint)(filter == GL_LINEAR ? GL_LINEAR : GL_NEAREST));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    return t;
}

static void reset_state(void) {
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_CULL_FACE);
    glViewport(0, 0, W, H);
}

// --- Cases --------------------------------------------------------------------------------
static void case_clear(void) {
    glClearColor(0.25f, 0.5f, 0.75f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    snap();
    check_px(0, 0, 64, 128, 191, 255, "clear");
    check_px(63, 63, 64, 128, 191, 255, "clear");
    // Scissored clear and masked clear (quad path), and a clear after a draw.
    glEnable(GL_SCISSOR_TEST);
    glScissor(8, 8, 16, 16);
    glClearColor(1, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_FALSE, GL_TRUE, GL_FALSE, GL_TRUE);
    glClearColor(1, 1, 1, 1);
    glScissor(0, 0, W, H);
    glClear(GL_COLOR_BUFFER_BIT);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    GLuint p = program(kVS, kFScolor, "pos", NULL);
    uniform4(p, "color", 0, 0, 1, 1);
    draw_rect(40, 40, 56, 56);
    glEnable(GL_SCISSOR_TEST);
    glScissor(48, 48, 16, 16);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_SCISSOR_TEST);
    snap();
    check_px(10, 10, 255, 255, 0, 255, "scissored clear then green-masked clear");
    check_px(2, 2, 64, 255, 191, 255, "green/alpha-masked clear");
    check_px(44, 44, 0, 0, 255, 255, "draw after clears");
    check_px(52, 52, 0, 0, 0, 0, "scissored clear after a draw");
    glok("clear");
}

static void case_triangle(void) {
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    // Apple's legacy context has APPLE VAOs only (the overlay uses them).
    GLuint vao;
    glGenVertexArraysAPPLE(1, &vao);
    glBindVertexArrayAPPLE(vao);
    GLuint p = program(kVScol, kFSvcol, "pos", "col", NULL);
    (void)p;
    // Interleaved pos + ubyte colour; a triangle, a strip and a fan (diagonals off pixel centres).
    struct V {
        float x, y;
        unsigned char c[4];
    };
    const float e = 1.0f / 256;
    struct V v[] = {
        {-1, -1, {255, 0, 0, 255}}, {0 + e, -1, {255, 0, 0, 255}}, {-1, 0 + e, {255, 0, 0, 255}},
        {0, 0, {0, 255, 0, 255}}, {0.5f, 0, {0, 255, 0, 255}}, {0, 0.5f, {0, 255, 0, 255}}, {0.5f, 0.5f, {0, 255, 0, 255}},
        {0.75f, -0.75f, {0, 0, 255, 255}}, {0.5f, -1, {0, 0, 255, 255}}, {1, -1, {0, 0, 255, 255}},
        {1, -0.5f, {0, 0, 255, 255}}, {0.5f, -0.5f, {0, 0, 255, 255}}, {0.5f, -1, {0, 0, 255, 255}},
    };
    GLuint vbo;
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof v, v, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(struct V), 0);
    glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(struct V), (void *)8);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glDrawArrays(GL_TRIANGLE_STRIP, 3, 4);
    glDrawArrays(GL_TRIANGLE_FAN, 7, 6);
    glBindVertexArrayAPPLE(0);
    snap();
    check_px(4, 4, 255, 0, 0, 255, "glDrawArrays GL_TRIANGLES");
    check_px(28, 28, 0, 0, 0, 255, "outside the triangle");
    check_px(40, 40, 0, 255, 0, 255, "GL_TRIANGLE_STRIP");
    check_px(56, 8, 0, 0, 255, 255, "GL_TRIANGLE_FAN");
    glDeleteBuffers(1, &vbo);
    glok("triangle");
}

static void case_indexed(void) {
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    GLuint p = program(kVS, kFScolor, "pos", NULL);
    // Four quads' corners; indices pick them.
    float v[32];
    rect(v, 0, 0, 16, 16);
    rect(v + 8, 16, 0, 32, 16);
    rect(v + 16, 32, 0, 48, 16);
    rect(v + 24, 48, 0, 64, 16);
    GLuint vbo, ibo;
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof v, v, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
    glEnableVertexAttribArray(0);
    static const GLushort i16[] = {0, 1, 2, 2, 1, 3, 4, 5, 6, 6, 5, 7};
    static const GLuint i32[] = {8, 9, 10, 10, 9, 11};
    static const GLubyte i8[] = {12, 13, 14, 14, 13, 15};
    glGenBuffers(1, &ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof i16, i16, GL_STATIC_DRAW);
    uniform4(p, "color", 1, 0, 0, 1);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0);
    uniform4(p, "color", 0, 1, 0, 1);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, (void *)12);  // offset into the buffer
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    uniform4(p, "color", 0, 0, 1, 1);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, i32);           // client indices
    uniform4(p, "color", 1, 1, 0, 1);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_BYTE, i8);
    // A strip from a ubyte index buffer at an odd offset.
    static const GLubyte strip[] = {99, 0, 1, 2, 3};
    float v2[8];
    rect(v2, 0, 32, 64, 48);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof v2, v2);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof strip, strip, GL_STATIC_DRAW);
    uniform4(p, "color", 1, 0, 1, 1);
    glDrawElements(GL_TRIANGLE_STRIP, 4, GL_UNSIGNED_BYTE, (void *)1);
    snap();
    check_px(8, 8, 255, 0, 0, 255, "ushort indices");
    check_px(24, 8, 0, 255, 0, 255, "ushort indices at an offset");
    check_px(40, 8, 0, 0, 255, 255, "uint client indices");
    check_px(56, 8, 255, 255, 0, 255, "ubyte client indices");
    check_px(30, 40, 255, 0, 255, 255, "ubyte buffer indices at an odd offset");
    check_px(30, 24, 0, 0, 0, 255, "between");
    glok("indexed");
}

static void case_instanced(void) {
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    static const char vs[] =
        "#version 120\n"
                "attribute vec2 pos;\n"
        "attribute vec2 off;\n"
        "attribute vec4 col;\n"
        "varying vec4 v_col;\n"
        "void main() {\n"
        "  v_col = col * (off.x == 0.5 || off.x == 1.5 ? 0.5 : 1.0);\n"
        "  gl_Position = vec4(pos + off, 0.0, 1.0);\n"
        "}\n";
    program(vs, kFSvcol, "pos", "off", "col", NULL);
    float quad[8];
    rect(quad, 0, 0, 16, 16);
    static const float offs[] = {0, 0, 0.5f, 0, 1, 0, 1.5f, 0, 0, 1, 0.5f, 1, 1, 1, 1.5f, 1};
    static const unsigned char cols[] = {255, 0, 0, 255, 0, 255, 0, 255};
    static const GLushort idx[] = {0, 1, 2, 2, 1, 3};
    GLuint b[4];
    glGenBuffers(4, b);
    glBindBuffer(GL_ARRAY_BUFFER, b[0]);
    glBufferData(GL_ARRAY_BUFFER, sizeof quad, quad, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
    glEnableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, b[1]);
    glBufferData(GL_ARRAY_BUFFER, sizeof offs, offs, GL_STATIC_DRAW);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, 0);
    glEnableVertexAttribArray(1);
    glVertexAttribDivisorARB(1, 1);
    glBindBuffer(GL_ARRAY_BUFFER, b[2]);
    glBufferData(GL_ARRAY_BUFFER, sizeof cols, cols, GL_STATIC_DRAW);
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, 0, 0);
    glEnableVertexAttribArray(2);
    glVertexAttribDivisorARB(2, 2);  // one colour per two instances
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, b[3]);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof idx, idx, GL_STATIC_DRAW);
    glDrawArraysInstancedARB(GL_TRIANGLE_STRIP, 0, 4, 4);
    glBindBuffer(GL_ARRAY_BUFFER, b[1]);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, (void *)32);
    glDrawElementsInstanced(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0, 2);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, (void *)48);
    glDrawElementsInstancedARB(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0, 2);
    glVertexAttribDivisorARB(1, 0);
    glVertexAttribDivisorARB(2, 0);
    snap();
    check_px(8, 8, 255, 0, 0, 255, "instance 0");
    check_px(24, 8, 128, 0, 0, -1, "instance 1");
    check_px(40, 8, 0, 255, 0, 255, "instance 2 (divisor 2 colour)");
    check_px(56, 8, 0, 128, 0, -1, "instance 3");
    check_px(8, 40, 255, 0, 0, 255, "glDrawElementsInstanced 0");
    check_px(24, 40, 128, 0, 0, -1, "glDrawElementsInstanced 1");
    check_px(40, 40, 255, 0, 0, 255, "glDrawElementsInstancedARB 0");
    check_px(56, 40, 128, 0, 0, -1, "glDrawElementsInstancedARB 1");
    glok("instanced");
}

// The same through gl_InstanceIDARB (Scaleform's batched shaders).
static void case_instance_id(void) {
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    static const char vs[] =
        "#version 120\n"
        "#extension GL_ARB_draw_instanced : enable\n"
        "attribute vec2 pos;\n"
        "attribute vec2 off;\n"
        "attribute vec4 col;\n"
        "varying vec4 v_col;\n"
        "void main() {\n"
        "  v_col = col * (gl_InstanceIDARB == 1 ? 0.5 : 1.0);\n"
        "  gl_Position = vec4(pos + off, 0.0, 1.0);\n"
        "}\n";
    program(vs, kFSvcol, "pos", "off", "col", NULL);
    float quad[8];
    rect(quad, 0, 0, 16, 16);
    static const float offs[] = {0, 0, 0.5f, 0, 1, 0, 1.5f, 0, 0, 1, 0.5f, 1, 1, 1, 1.5f, 1};
    static const unsigned char cols[] = {255, 0, 0, 255, 0, 255, 0, 255};
    static const GLushort idx[] = {0, 1, 2, 2, 1, 3};
    GLuint b[4];
    glGenBuffers(4, b);
    glBindBuffer(GL_ARRAY_BUFFER, b[0]);
    glBufferData(GL_ARRAY_BUFFER, sizeof quad, quad, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
    glEnableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, b[1]);
    glBufferData(GL_ARRAY_BUFFER, sizeof offs, offs, GL_STATIC_DRAW);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, 0);
    glEnableVertexAttribArray(1);
    glVertexAttribDivisorARB(1, 1);
    glBindBuffer(GL_ARRAY_BUFFER, b[2]);
    glBufferData(GL_ARRAY_BUFFER, sizeof cols, cols, GL_STATIC_DRAW);
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, 0, 0);
    glEnableVertexAttribArray(2);
    glVertexAttribDivisorARB(2, 2);  // one colour per two instances
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, b[3]);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof idx, idx, GL_STATIC_DRAW);
    glDrawArraysInstancedARB(GL_TRIANGLE_STRIP, 0, 4, 4);
    glBindBuffer(GL_ARRAY_BUFFER, b[1]);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, (void *)32);
    glDrawElementsInstanced(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0, 2);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, (void *)48);
    glDrawElementsInstancedARB(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0, 2);
    glVertexAttribDivisorARB(1, 0);
    glVertexAttribDivisorARB(2, 0);
    snap();
    check_px(8, 8, 255, 0, 0, 255, "instance 0");
    check_px(24, 8, 128, 0, 0, -1, "instance 1 (gl_InstanceIDARB)");
    check_px(40, 8, 0, 255, 0, 255, "instance 2 (divisor 2 colour)");
    check_px(56, 8, 0, 255, 0, 255, "instance 3");
    check_px(8, 40, 255, 0, 0, 255, "glDrawElementsInstanced 0");
    check_px(24, 40, 128, 0, 0, -1, "glDrawElementsInstanced 1");
    check_px(40, 40, 255, 0, 0, 255, "glDrawElementsInstancedARB 0");
    check_px(56, 40, 128, 0, 0, -1, "glDrawElementsInstancedARB 1");
    glok("instance id");
}

static void case_client_arrays(void) {
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    // Generic attributes from client memory, colour as packed ubyte3 (stride 3).
    program(kVScol, kFSvcol, "pos", "col", NULL);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    float pos[8];
    rect(pos, 0, 0, 32, 32);
    static const unsigned char col3[] = {255, 0, 0, 255, 0, 0, 255, 0, 0, 255, 0, 0};
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, pos);
    glVertexAttribPointer(1, 3, GL_UNSIGNED_BYTE, GL_TRUE, 3, col3);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    // Client indices + client vertices starting at a high index.
    float pos2[16] = {0};
    rect(pos2 + 8, 32, 0, 64, 32);
    static const unsigned char col4[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                         0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255};
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, pos2);
    glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, 0, col4);
    static const GLushort idx[] = {4, 5, 6, 6, 5, 7};
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, idx);
    glDisableVertexAttribArray(1);
    glDisableVertexAttribArray(0);
    // Conventional arrays and built-ins (gl_Vertex, gl_Color, gl_MultiTexCoord0) as the Cg shim uses them.
    static const char vs[] =
        "#version 120\n"
        "varying vec4 v_col;\n"
        "void main() { v_col = gl_Color * gl_MultiTexCoord0.x; gl_Position = gl_Vertex; }\n";
    program(vs, kFSvcol, NULL);
    float pos3[8];
    rect(pos3, 0, 32, 64, 64);
    static const float colf[] = {0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1};
    static const float tc[] = {0.5f, 0.5f, 0.5f, 0.5f};
    glVertexPointer(2, GL_FLOAT, 0, pos3);
    glColorPointer(4, GL_FLOAT, 0, colf);
    glClientActiveTexture(GL_TEXTURE0);
    glTexCoordPointer(1, GL_FLOAT, 0, tc);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    snap();
    check_px(16, 16, 255, 0, 0, 255, "client arrays, ubyte3 colours");
    check_px(48, 16, 0, 255, 0, 255, "client indices into client arrays");
    check_px(32, 48, 0, 0, 128, -1, "conventional arrays + built-ins");
    glok("client arrays");
}

static void case_blend(void) {
    glClearColor(0.5f, 0.5f, 0.5f, 0.5f);
    glClear(GL_COLOR_BUFFER_BIT);
    GLuint p = program(kVS, kFScolor, "pos", NULL);
    glEnable(GL_BLEND);
    struct {
        GLenum s, d, eq;
        float c[4];
    } m[] = {
        {GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_FUNC_ADD, {1, 0, 0, 0.25f}},
        {GL_ONE, GL_ONE, GL_FUNC_ADD, {0.25f, 0.25f, 0, 0}},
        {GL_DST_COLOR, GL_ZERO, GL_FUNC_ADD, {0.5f, 1, 0, 1}},
        {GL_ONE, GL_ONE, GL_FUNC_SUBTRACT, {0.25f, 0, 0.5f, 0}},
        {GL_ONE, GL_ONE, GL_FUNC_REVERSE_SUBTRACT, {0.25f, 0, 0.125f, 0}},
        {GL_ONE, GL_ONE, GL_MIN, {0.25f, 1, 0, 1}},
        {GL_ONE, GL_ONE, GL_MAX, {0.75f, 0, 1, 0}},
        {GL_CONSTANT_COLOR, GL_ONE_MINUS_CONSTANT_ALPHA, GL_FUNC_ADD, {1, 1, 1, 1}},
    };
    glBlendColor(0.25f, 0.5f, 0.75f, 0.5f);
    for (int i = 0; i < 8; i++) {
        glBlendFunc(m[i].s, m[i].d);
        glBlendEquation(m[i].eq);
        uniform4(p, "color", m[i].c[0], m[i].c[1], m[i].c[2], m[i].c[3]);
        draw_rect(i * 8, 0, i * 8 + 8, 32);
    }
    // Separate alpha.
    glBlendEquation(GL_FUNC_ADD);
    glBlendFuncSeparate(GL_ONE, GL_ZERO, GL_ZERO, GL_ONE);
    uniform4(p, "color", 0, 1, 0, 1);
    draw_rect(0, 32, 32, 64);
    // Overlapping alpha blends.
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    uniform4(p, "color", 0, 0, 1, 0.5f);
    draw_rect(32, 32, 64, 64);
    uniform4(p, "color", 1, 1, 0, 0.5f);
    draw_rect(48, 48, 64, 64);
    glDisable(GL_BLEND);
    snap();
    check_px(4, 4, 160, 96, 96, -1, "SRC_ALPHA / ONE_MINUS_SRC_ALPHA");
    check_px(12, 4, 191, 191, 128, -1, "ONE / ONE");
    check_px(20, 4, 64, 128, 0, -1, "DST_COLOR / ZERO");
    check_px(28, 4, 0, 0, 0, 0, "subtract (clamped)");
    check_px(36, 4, 64, 128, 96, -1, "reverse subtract");
    check_px(44, 4, 64, 128, 0, -1, "min");
    check_px(52, 4, 191, 128, 255, -1, "max");
    check_px(60, 4, 128, 191, 255, -1, "constant colour / alpha");
    check_px(8, 40, 0, 255, 0, 128, "separate alpha keeps destination alpha");
    check_px(56, 56, 159, 159, 96, 128, "two alpha blends");
    glok("blend");
}

static void case_depth(void) {
    glClearColor(0, 0, 0, 1);
    glClearDepth(1.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    GLuint p = program(kVSz, kFScolor, "pos", NULL);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    float v[12];
    // Near quad (z = -0.5) then a far one (z = 0.5) over it: the far one fails.
    struct {
        int x0, y0, x1, y1;
        float z, c[3];
        GLenum func;
        GLboolean mask;
    } q[] = {
        {0, 0, 32, 32, -0.5f, {1, 0, 0}, GL_LESS, GL_TRUE},
        {16, 16, 48, 48, 0.5f, {0, 1, 0}, GL_LESS, GL_TRUE},
        {32, 0, 64, 32, 0.0f, {0, 0, 1}, GL_LESS, GL_FALSE},  // no depth write
        {40, 8, 56, 24, 0.5f, {1, 1, 0}, GL_LESS, GL_TRUE},   // passes: z of the blue quad wasn't written
        {0, 32, 64, 64, 0.25f, {1, 0, 1}, GL_GREATER, GL_TRUE},
    };
    for (int i = 0; i < 5; i++) {
        float r[8];
        rect(r, q[i].x0, q[i].y0, q[i].x1, q[i].y1);
        for (int k = 0; k < 4; k++) {
            v[k * 3] = r[k * 2];
            v[k * 3 + 1] = r[k * 2 + 1];
            v[k * 3 + 2] = q[i].z;
        }
        if (!g_vbo)
            glGenBuffers(1, &g_vbo);
        glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof v, v, GL_STREAM_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, 0);
        glEnableVertexAttribArray(0);
        glDepthFunc(q[i].func);
        glDepthMask(q[i].mask);
        uniform4(p, "color", q[i].c[0], q[i].c[1], q[i].c[2], 1);
        if (i == 4)
            glClear(GL_DEPTH_BUFFER_BIT);  // depth 1.0 again: GL_GREATER now fails everywhere
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }
    glDepthMask(GL_TRUE);
    glDisable(GL_DEPTH_TEST);
    snap();
    check_px(20, 20, 255, 0, 0, 255, "near quad kept");
    check_px(40, 40, 0, 255, 0, 255, "far quad where nothing was near");
    check_px(36, 28, 0, 0, 255, 255, "no-depth-write quad drawn");
    check_px(48, 16, 255, 255, 0, 255, "depth mask off kept the old depth");
    check_px(8, 56, 0, 0, 0, 255, "GL_GREATER against 1.0 fails");
    glok("depth");
}

static void case_stencil(void) {
    glClearColor(0, 0, 0, 1);
    glClearStencil(0);
    glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    GLuint p = program(kVS, kFScolor, "pos", NULL);
    glEnable(GL_STENCIL_TEST);
    // Write 1 where a quad is (colour masked off), then draw where stencil == 1.
    glStencilFunc(GL_ALWAYS, 1, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    draw_rect(8, 8, 40, 40);
    // Increment inside a second quad.
    glStencilOp(GL_KEEP, GL_KEEP, GL_INCR);
    glStencilFunc(GL_ALWAYS, 0, 0xFF);
    draw_rect(24, 24, 56, 56);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
    glStencilFunc(GL_EQUAL, 1, 0xFF);
    uniform4(p, "color", 1, 0, 0, 1);
    draw_rect(0, 0, 64, 64);
    glStencilFunc(GL_EQUAL, 2, 0xFF);
    uniform4(p, "color", 0, 1, 0, 1);
    draw_rect(0, 0, 64, 64);
    // Separate front/back: a back-facing (clockwise) quad uses the back function.
    glStencilFuncSeparate(GL_FRONT, GL_NEVER, 0, 0xFF);
    glStencilFuncSeparate(GL_BACK, GL_ALWAYS, 0, 0xFF);
    uniform4(p, "color", 0, 0, 1, 1);
    float v[8];
    rect(v, 0, 56, 16, 64);
    float cw[8] = {v[0], v[1], v[4], v[5], v[2], v[3], v[6], v[7]};  // strip with reversed winding
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof cw, cw, GL_STREAM_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    draw_rect(48, 0, 64, 8);  // front-facing: GL_NEVER
    // Masked stencil clear: only bit 1 set, so stencil 1 becomes 3.
    glStencilMask(0x02);
    glClearStencil(0xFF);
    glClear(GL_STENCIL_BUFFER_BIT);
    glStencilMask(0xFF);
    glStencilFunc(GL_EQUAL, 3, 0xFF);
    uniform4(p, "color", 1, 1, 1, 1);
    glEnable(GL_SCISSOR_TEST);
    glScissor(0, 0, 64, 16);
    draw_rect(0, 0, 64, 16);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_STENCIL_TEST);
    snap();
    check_px(12, 30, 255, 0, 0, 255, "stencil == 1");
    check_px(30, 30, 0, 255, 0, 255, "stencil == 2 (incremented)");
    check_px(60, 60, 0, 0, 0, 255, "stencil == 0 fails");
    check_px(4, 60, 0, 0, 255, 255, "back face uses the back function");
    check_px(56, 4, 0, 0, 0, 255, "front face GL_NEVER");
    check_px(12, 12, 255, 255, 255, 255, "masked stencil clear");
    glok("stencil");
}

static void case_scissor(void) {
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    GLuint p = program(kVS, kFScolor, "pos", NULL);
    glEnable(GL_SCISSOR_TEST);
    glScissor(10, 20, 30, 7);
    uniform4(p, "color", 1, 0, 0, 1);
    draw_rect(0, 0, 64, 64);
    glScissor(-10, 50, 30, 100);  // partly outside
    uniform4(p, "color", 0, 1, 0, 1);
    draw_rect(0, 0, 64, 64);
    glScissor(5, 5, 0, 10);  // empty
    uniform4(p, "color", 0, 0, 1, 1);
    draw_rect(0, 0, 64, 64);
    glDisable(GL_SCISSOR_TEST);
    snap();
    check_px(10, 20, 255, 0, 0, 255, "scissor corner");
    check_px(39, 26, 255, 0, 0, 255, "scissor far corner");
    check_px(40, 26, 0, 0, 0, 255, "outside the scissor");
    check_px(9, 20, 0, 0, 0, 255, "outside the scissor (left)");
    check_px(0, 63, 0, 255, 0, 255, "clipped scissor");
    check_px(6, 6, 0, 0, 0, 255, "empty scissor");
    GLint box[4];
    glGetIntegerv(GL_SCISSOR_BOX, box);
    check(box[0] == 5 && box[2] == 0, "GL_SCISSOR_BOX");
    glok("scissor");
}

static void case_viewport(void) {
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    GLuint p = program(kVScol, kFSvcol, "pos", "col", NULL);
    (void)p;
    // Full-viewport quad with corner colours, in three viewports.
    float v[8] = {-1, -1, 1, -1, -1, 1, 1, 1};
    static const unsigned char c[] = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255};
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, v);
    glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, 0, c);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glViewport(0, 0, 32, 32);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glViewport(32, 0, 32, 16);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glViewport(40, 40, 64, 64);  // extends past the target
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glViewport(0, 0, W, H);
    snap();
    check_px(0, 0, 247, 4, 4, -1, "viewport 1 bottom-left = red");
    check_px(31, 31, 247, 251, 251, -1, "viewport 1 top-right = white");
    check_px(0, 31, 0, 4, 251, -1, "viewport 1 top-left = blue");
    GLint vp[4];
    glGetIntegerv(GL_VIEWPORT, vp);
    check(vp[2] == W && vp[3] == H, "GL_VIEWPORT");
    glok("viewport");
}

static GLuint make_fbo(GLuint *color, GLuint *ds, int w, int h) {
    glGenTextures(1, color);
    glBindTexture(GL_TEXTURE_2D, *color);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glGenRenderbuffers(1, ds);
    glBindRenderbuffer(GL_RENDERBUFFER, *ds);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);
    GLuint fbo;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, *color, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, *ds);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, *ds);
    return fbo;
}

static void case_fbo(void) {
    GLuint color, ds;
    GLuint fbo = make_fbo(&color, &ds, W, H);
    check(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE, "FBO complete");
    GLint type = 0, name = 0, rbw = 0, fbb = 0;
    glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                          GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &type);
    glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                          GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &name);
    glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_WIDTH, &rbw);
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fbb);
    check(type == GL_TEXTURE && (GLuint)name == ds && rbw == W && (GLuint)fbb == fbo &&
              glIsFramebuffer(fbo) && glIsRenderbuffer(ds),
          "FBO queries (type 0x%x name %d rb w %d binding %d)", type, name, rbw, fbb);
    glClearColor(0, 0, 0.5f, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    GLuint p = program(kVSz, kFScolor, "pos", NULL);
    glEnable(GL_DEPTH_TEST);
    float v[12] = {-1, -1, 0, 0.5f, -1, 0, -1, 0.5f, 0, 0.5f, 0.5f, 0};
    glGenBuffers(1, &g_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof v, v, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, 0);
    glEnableVertexAttribArray(0);
    uniform4(p, "color", 1, 0, 0, 1);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    for (int i = 0; i < 12; i += 3)
        v[i + 2] = 0.5f;
    glBufferData(GL_ARRAY_BUFFER, sizeof v, v, GL_STATIC_DRAW);
    uniform4(p, "color", 0, 1, 0, 1);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);  // behind: hidden
    glDisable(GL_DEPTH_TEST);
    // Switch to the default framebuffer and back (load, not clear).
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glClearColor(1, 1, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    GLuint pc = program(kVS, kFScolor, "pos", NULL);
    uniform4(pc, "color", 1, 1, 1, 1);
    draw_rect(56, 56, 64, 64);
    snap();
    check_px(10, 10, 255, 0, 0, 255, "FBO draw with depth");
    check_px(60, 10, 0, 0, 128, 255, "FBO clear");
    check_px(60, 60, 255, 255, 255, 255, "FBO contents kept across a rebind");
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    snap();
    check_px(10, 10, 255, 255, 0, 255, "default framebuffer separate");
    glDeleteFramebuffers(1, &fbo);
    glDeleteRenderbuffers(1, &ds);
    glDeleteTextures(1, &color);
    glok("fbo");
}

static void case_render_to_texture(void) {
    GLuint color, ds;
    GLuint fbo = make_fbo(&color, &ds, 32, 32);
    glViewport(0, 0, 32, 32);
    glClearColor(0, 0, 1, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    GLuint p = program(kVS, kFScolor, "pos", NULL);
    uniform4(p, "color", 1, 0, 0, 1);
    float v[8] = {-1, -1, 0, -1, -1, 0, 0, 0};  // bottom-left quarter of the texture
    glGenBuffers(1, &g_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof v, v, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
    glEnableVertexAttribArray(0);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, W, H);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    GLuint pt = program(kVStex, kFStex, "pos", "tc", NULL);
    glUniform1i(glGetUniformLocation(pt, "tex"), 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, color);
    draw_tex_rect(0, 0, 64, 64);
    snap();
    check_px(8, 8, 255, 0, 0, 255, "rendered quarter sampled at the bottom-left");
    check_px(56, 56, 0, 0, 255, 255, "rest of the texture");
    // glGetTexImage of the render target: row 0 is the bottom.
    static unsigned char t[32 * 32 * 4];
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, t);
    dump(t, sizeof t);
    check(t[0] == 255 && t[2] == 0 && t[(31 * 32 + 31) * 4 + 2] == 255, "glGetTexImage orientation");
    glDeleteFramebuffers(1, &fbo);
    glok("render to texture");
}


// Depth texture in an FBO (a shadow map): render depth, then sample it. (A colour
// attachment too: GL 2.1 without glDrawBuffer(GL_NONE) needs one.)
static void case_depth_texture(void) {
    GLuint dt;
    glGenTextures(1, &dt);
    glBindTexture(GL_TEXTURE_2D, dt);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, 32, 32, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, NULL);
    GLuint fbo;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, dt, 0);
    GLuint ct = texture_rgba(32, 32, NULL, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, ct, 0);
    check(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE, "depth texture FBO complete");
    glViewport(0, 0, 32, 32);
    glClearDepth(1.0);
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_ALWAYS);
    GLuint p = program(kVSz, kFScolor, "pos", NULL);
    uniform4(p, "color", 1, 1, 1, 1);
    float v[12] = {-1, -1, 0, 0, -1, 0, -1, 1, 0, 0, 1, 0};  // left half at z 0 -> depth 0.5
    glGenBuffers(1, &g_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof v, v, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, 0);
    glEnableVertexAttribArray(0);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDisable(GL_DEPTH_TEST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, W, H);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    GLuint pt = program(kVStex, kFStex, "pos", "tc", NULL);
    glUniform1i(glGetUniformLocation(pt, "tex"), 0);
    glBindTexture(GL_TEXTURE_2D, dt);
    draw_tex_rect(0, 0, 64, 64);
    snap();
    check(near(px(8, 32)[0], 128, 1), "sampled depth 0.5 in red (%d)", px(8, 32)[0]);
    check(near(px(56, 32)[0], 255, 1), "cleared depth 1.0 (%d)", px(56, 32)[0]);
    float d[2] = {0};
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glReadPixels(4, 4, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, d);
    glReadPixels(28, 4, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, d + 1);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    check(fabsf(d[0] - 0.5f) < 0.001f && d[1] == 1.0f, "glReadPixels depth (%f, %f)", d[0], d[1]);
    // Green/blue of a sampled depth texture differ between implementations (depth texture mode).
    for (int i = 0; i < W * H; i++)
        g_px[i * 4 + 1] = g_px[i * 4 + 2] = 0;
    g_dump_len = 0;
    dump(g_px, sizeof g_px);
    glDeleteFramebuffers(1, &fbo);
    glok("depth texture");
}


// Cube map: one colour per face, looked up along each axis.
static void case_cube_map(void) {
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    static const char vs[] =
        "#version 120\n"
        "attribute vec2 pos;\n"
        "attribute vec3 dir;\n"
        "varying vec3 v_dir;\n"
        "void main() { v_dir = dir; gl_Position = vec4(pos, 0.0, 1.0); }\n";
    static const char fs[] =
        "#version 120\n"
        "uniform samplerCube cube;\n"
        "varying vec3 v_dir;\n"
        "void main() { gl_FragColor = textureCube(cube, v_dir); }\n";
    GLuint p = program(vs, fs, "pos", "dir", NULL);
    glUniform1i(glGetUniformLocation(p, "cube"), 1);
    GLuint t;
    glGenTextures(1, &t);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_CUBE_MAP, t);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    static const unsigned char faces[6][4] = {{255, 0, 0, 255}, {0, 255, 0, 255}, {0, 0, 255, 255},
                                              {255, 255, 0, 255}, {0, 255, 255, 255}, {255, 0, 255, 255}};
    for (int f = 0; f < 6; f++) {
        unsigned char img[4 * 4 * 4];
        for (int i = 0; i < 16; i++)
            memcpy(img + i * 4, faces[f], 4);
        img[0] = 255;  // one corner texel differs: orientation
        img[1] = img[2] = 255;
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + (GLenum)f, 0, GL_RGBA, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, img);
    }
    glActiveTexture(GL_TEXTURE0);
    static const float dirs[6][3] = {{1, 0.1f, 0.2f}, {-1, 0.1f, 0.2f}, {0.1f, 1, 0.2f},
                                     {0.1f, -1, 0.2f}, {0.1f, 0.2f, 1}, {0.1f, 0.2f, -1}};
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    for (int f = 0; f < 6; f++) {
        float pos[8], dir[12];
        rect(pos, f * 10, 0, f * 10 + 10, 10);
        for (int k = 0; k < 4; k++)
            memcpy(dir + k * 3, dirs[f], sizeof dirs[f]);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, pos);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 0, dir);
        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }
    // Corner directions: which texel of +Z is the "first" one.
    static const float corners[4][3] = {{-0.9f, -0.9f, 1}, {0.9f, -0.9f, 1}, {-0.9f, 0.9f, 1}, {0.9f, 0.9f, 1}};
    for (int c = 0; c < 4; c++) {
        float pos[8], dir[12];
        rect(pos, c * 10, 20, c * 10 + 10, 30);
        for (int k = 0; k < 4; k++)
            memcpy(dir + k * 3, corners[c], sizeof corners[c]);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, pos);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 0, dir);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }
    snap();
    for (int f = 0; f < 6; f++)
        check_px(f * 10 + 5, 5, faces[f][0], faces[f][1], faces[f][2], 255, "cube face");
    int whites = 0;
    for (int c = 0; c < 4; c++)
        whites += px(c * 10 + 5, 25)[0] == 255 && px(c * 10 + 5, 25)[1] == 255 && px(c * 10 + 5, 25)[2] == 255;
    check(whites == 1, "one corner of +Z is the first texel (%d)", whites);
    glok("cube map");
}

// DXT blocks: colour endpoints and indices chosen to decode exactly.
static void case_dxt(void) {
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    GLuint p = program(kVStex, kFStex, "pos", "tc", NULL);
    glUniform1i(glGetUniformLocation(p, "tex"), 0);
    // 8x8 texture = 2x2 blocks: red, green, blue, white/black split.
    unsigned char bc1[4][8] = {
        {0x00, 0xF8, 0x00, 0x00, 0, 0, 0, 0},
        {0xE0, 0x07, 0x00, 0x00, 0, 0, 0, 0},
        {0x1F, 0x00, 0x00, 0x00, 0, 0, 0, 0},
        {0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x55, 0x55},  // rows 0-1 white, rows 2-3 black
    };
    GLuint t[3];
    glGenTextures(3, t);
    glBindTexture(GL_TEXTURE_2D, t[0]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGB_S3TC_DXT1_EXT, 8, 8, 0, sizeof bc1, bc1);
    draw_tex_rect(0, 0, 32, 32);
    // DXT3: explicit alpha 0x8 per texel over red.
    unsigned char bc2[16] = {0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x00, 0xF8, 0x00, 0x00, 0, 0, 0, 0};
    glBindTexture(GL_TEXTURE_2D, t[1]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGBA_S3TC_DXT3_EXT, 4, 4, 0, sizeof bc2, bc2);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    draw_tex_rect(32, 0, 64, 32);
    // DXT5: alpha endpoints 255/0 with index 1 (= 0) everywhere over green.
    unsigned char bc3[16] = {255, 0, 0x49, 0x92, 0x24, 0x49, 0x92, 0x24, 0xE0, 0x07, 0x00, 0x00, 0, 0, 0, 0};
    glBindTexture(GL_TEXTURE_2D, t[2]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, 4, 4, 0, sizeof bc3, bc3);
    draw_tex_rect(0, 32, 32, 64);
    glDisable(GL_BLEND);
    snap();
    check_px(4, 4, 255, 0, 0, 255, "DXT1 block 0");
    check_px(20, 4, 0, 255, 0, 255, "DXT1 block 1");
    check_px(4, 20, 0, 0, 255, 255, "DXT1 block 2");
    check_px(20, 18, 255, 255, 255, 255, "DXT1 block 3 white rows");
    check_px(20, 30, 0, 0, 0, 255, "DXT1 block 3 black rows");
    check_px(40, 8, 136, 0, 0, -1, "DXT3 alpha 8/15");
    check_px(8, 40, 0, 0, 0, -1, "DXT5 alpha 0");
    static unsigned char dec[8 * 8 * 4];
    glBindTexture(GL_TEXTURE_2D, t[0]);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, dec);
    dump(dec, sizeof dec);
    check(dec[0] == 255 && dec[1] == 0 && dec[3] == 255, "DXT1 glGetTexImage decodes");
    GLint comp = 0, size = 0;
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_COMPRESSED, &comp);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_COMPRESSED_IMAGE_SIZE, &size);
    check(comp == 1 && size == 32, "compressed level queries (%d, %d)", comp, size);
    glok("dxt");
}

static void case_bgra(void) {
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    GLuint p = program(kVStex, kFStex, "pos", "tc", NULL);
    glUniform1i(glGetUniformLocation(p, "tex"), 0);
    // 2x2 BGRA: red, green / blue, white.
    static const unsigned char bgra[] = {0, 0, 255, 255, 0, 255, 0, 255, 255, 0, 0, 255, 255, 255, 255, 255};
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 2, 0, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, bgra);
    draw_tex_rect(0, 0, 32, 32);
    // RGBA sub-image into the BGRA texture.
    static const unsigned char yellow[] = {255, 255, 0, 255};
    glTexSubImage2D(GL_TEXTURE_2D, 0, 1, 1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, yellow);
    draw_tex_rect(32, 0, 64, 32);
    snap();
    check_px(8, 8, 255, 0, 0, 255, "BGRA texel 0");
    check_px(24, 8, 0, 255, 0, 255, "BGRA texel 1");
    check_px(8, 24, 0, 0, 255, 255, "BGRA texel 2");
    check_px(56, 24, 255, 255, 0, 255, "RGBA sub-image into BGRA");
    unsigned char rb[4 * 4];
    glReadPixels(6, 6, 2, 2, GL_BGRA, GL_UNSIGNED_BYTE, rb);
    dump(rb, sizeof rb);
    check(rb[0] == 0 && rb[2] == 255, "glReadPixels as BGRA");
    glok("bgra");
}

static void case_luminance(void) {
    glClearColor(0, 0, 1, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    GLuint p = program(kVStex, kFStex, "pos", "tc", NULL);
    glUniform1i(glGetUniformLocation(p, "tex"), 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    static const unsigned char l[] = {64}, a[] = {128}, la[] = {200, 100};
    GLuint t[3];
    glGenTextures(3, t);
    const unsigned char *data[3] = {l, a, la};
    GLenum fmt[3] = {GL_LUMINANCE, GL_ALPHA, GL_LUMINANCE_ALPHA};
    for (int i = 0; i < 3; i++) {
        glBindTexture(GL_TEXTURE_2D, t[i]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D, 0, (GLint)fmt[i], 1, 1, 0, fmt[i], GL_UNSIGNED_BYTE, data[i]);
        draw_tex_rect(i * 16, 0, i * 16 + 16, 16);
    }
    // RGB texture: alpha reads 1.
    static const unsigned char rgb[] = {10, 20, 30};
    GLuint trgb;
    glGenTextures(1, &trgb);
    glBindTexture(GL_TEXTURE_2D, trgb);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 1, 1, 0, GL_RGB, GL_UNSIGNED_BYTE, rgb);
    draw_tex_rect(48, 0, 64, 16);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    snap();
    check_px(8, 8, 64, 64, 64, 255, "GL_LUMINANCE");
    check_px(24, 8, 0, 0, 0, 128, "GL_ALPHA");
    check_px(40, 8, 200, 200, 200, 100, "GL_LUMINANCE_ALPHA");
    check_px(56, 8, 10, 20, 30, 255, "GL_RGB alpha is 1");
    glok("luminance");
}

static void case_mipmaps(void) {
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    GLuint p = program(kVStex, kFStex, "pos", "tc", NULL);
    glUniform1i(glGetUniformLocation(p, "tex"), 0);
    // 32x32 with explicit levels of different colours.
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    static unsigned char lv[32 * 32 * 4];
    static const unsigned char cols[6][4] = {{255, 0, 0, 255}, {0, 255, 0, 255}, {0, 0, 255, 255},
                                             {255, 255, 0, 255}, {0, 255, 255, 255}, {255, 0, 255, 255}};
    for (int l = 0, s = 32; s >= 1; l++, s /= 2) {
        for (int i = 0; i < s * s; i++)
            memcpy(lv + i * 4, cols[l], 4);
        glTexImage2D(GL_TEXTURE_2D, l, GL_RGBA, s, s, 0, GL_RGBA, GL_UNSIGNED_BYTE, lv);
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    draw_tex_rect(0, 0, 32, 32);  // level 0
    draw_tex_rect(32, 0, 40, 8);  // 32 texels over 8 pixels: level 2
    draw_tex_rect(48, 0, 52, 4);  // level 3
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 1);
    draw_tex_rect(56, 0, 60, 4);  // clamped to level 1
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 1000);
    // glGenerateMipmap of a 2-colour checker averages to grey.
    GLuint g;
    glGenTextures(1, &g);
    glBindTexture(GL_TEXTURE_2D, g);
    static unsigned char ck[16 * 16 * 4];
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++)
            memset(ck + (y * 16 + x) * 4, ((x + y) & 1) ? 255 : 0, 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 16, 16, 0, GL_RGBA, GL_UNSIGNED_BYTE, ck);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
    draw_tex_rect(0, 32, 4, 36);
    GLint w2 = 0;
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 2, GL_TEXTURE_WIDTH, &w2);
    unsigned char l2[4 * 4 * 4];
    glGetTexImage(GL_TEXTURE_2D, 2, GL_RGBA, GL_UNSIGNED_BYTE, l2);
    snap();
    check_px(16, 16, 255, 0, 0, 255, "level 0");
    check_px(36, 4, 0, 0, 255, 255, "level 2");
    check_px(50, 2, 255, 255, 0, 255, "level 3");
    check_px(58, 2, 0, 255, 0, 255, "GL_TEXTURE_MAX_LEVEL 1");
    check(near(px(2, 34)[0], 128, 2), "generated mip is grey (%d)", px(2, 34)[0]);
    check(w2 == 4 && near(l2[0], 128, 2), "glGetTexLevelParameteriv / glGetTexImage of a generated level (%d, %d)",
          w2, l2[0]);
    glok("mipmaps");
}

static void case_alpha_test(void) {
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    GLuint p = program(kVScol, kFSvcol, "pos", "col", NULL);
    (void)p;
    // Alpha ramps 0..1 from left to right in four bands, each with another test.
    float v[8];
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    static const unsigned char c[16] = {255, 255, 255, 0, 255, 255, 255, 255, 255, 255, 255, 0, 255, 255, 255, 255};
    glEnable(GL_ALPHA_TEST);
    struct {
        GLenum f;
        float ref;
    } t[] = {{GL_GREATER, 0.5f}, {GL_LESS, 0.25f}, {GL_EQUAL, 1.0f}, {GL_ALWAYS, 0}};
    for (int i = 0; i < 4; i++) {
        rect(v, 0, i * 16, 64, i * 16 + 16);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, v);
        glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, 0, c);
        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);
        glAlphaFunc(t[i].f, t[i].ref);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }
    GLint f = 0;
    glGetIntegerv(GL_ALPHA_TEST_FUNC, &f);
    check(f == GL_ALWAYS && glIsEnabled(GL_ALPHA_TEST), "GL_ALPHA_TEST_FUNC reads back");
    glDisable(GL_ALPHA_TEST);
    snap();
    check_px(8, 8, 0, 0, 0, 255, "GREATER 0.5 rejects the left");
    check_px(56, 8, 255, 255, 255, -1, "GREATER 0.5 keeps the right");
    check_px(4, 24, 255, 255, 255, -1, "LESS 0.25 keeps the left");
    check_px(56, 24, 0, 0, 0, 255, "LESS 0.25 rejects the right");
    check_px(30, 40, 0, 0, 0, 255, "EQUAL 1 rejects the middle");
    check_px(30, 56, 255, 255, 255, -1, "ALWAYS");
    glok("alpha test");
}

static void case_polygon_mode(void) {
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    GLuint p = program(kVS, kFScolor, "pos", NULL);
    uniform4(p, "color", 1, 1, 1, 1);
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    draw_rect(8, 8, 56, 56);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    GLint m[2] = {0};
    glGetIntegerv(GL_POLYGON_MODE, m);
    check(m[0] == GL_FILL, "GL_POLYGON_MODE");
    snap();
    // Line rasterisation differs between implementations: interior empty, some edge pixels lit.
    int lit = 0;
    for (int i = 0; i < W * H; i++)
        lit += g_px[i * 4] > 128;
    check(px(32, 20)[0] == 0 && px(20, 40)[0] == 0, "wireframe interior empty");
    check(lit > 100 && lit < 600, "wireframe edges drawn (%d pixels)", lit);
    g_dump_len = 0;  // not compared pixel for pixel
    dump_int(lit > 100 && lit < 600);
    glok("polygon mode");
}

// Several draws in one frame from one buffer rewritten in between: each draw
// must see its own data (renaming of in-flight buffers).
static void case_subdata_in_flight(void) {
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    program(kVScol, kFSvcol, "pos", "col", NULL);
    struct V {
        float x, y;
        unsigned char c[4];
    } v[4];
    GLuint vbo;
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof v * 2, NULL, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(struct V), 0);
    glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(struct V), (void *)8);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    for (int i = 0; i < 16; i++) {
        float r[8];
        rect(r, (i % 4) * 16, (i / 4) * 16, (i % 4) * 16 + 16, (i / 4) * 16 + 16);
        for (int k = 0; k < 4; k++) {
            v[k].x = r[k * 2];
            v[k].y = r[k * 2 + 1];
            v[k].c[0] = (unsigned char)(i * 16);
            v[k].c[1] = (unsigned char)(255 - i * 16);
            v[k].c[2] = (unsigned char)(i & 1 ? 255 : 0);
            v[k].c[3] = 255;
        }
        if (i % 3 == 2)
            glBufferData(GL_ARRAY_BUFFER, sizeof v * 2, NULL, GL_DYNAMIC_DRAW);
        glBufferSubData(GL_ARRAY_BUFFER, (i & 1) * (GLintptr)sizeof v, sizeof v, v);
        glDrawArrays(GL_TRIANGLE_STRIP, (i & 1) * 4, 4);
    }
    snap();
    int bad = 0;
    for (int i = 0; i < 16; i++) {
        const unsigned char *q = px((i % 4) * 16 + 8, (i / 4) * 16 + 8);
        if (q[0] != i * 16 || q[1] != 255 - i * 16 || q[2] != (i & 1 ? 255 : 0))
            bad++;
    }
    check(!bad, "16 draws see 16 versions of the buffer (%d wrong)", bad);
    glDeleteBuffers(1, &vbo);
    glok("subdata in flight");
}

static void case_map_buffer(void) {
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    GLuint p = program(kVS, kFScolor, "pos", NULL);
    GLuint vbo;
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, 32, NULL, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
    glEnableVertexAttribArray(0);
    for (int i = 0; i < 2; i++) {
        float *m = glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY);
        check(m != NULL, "glMapBuffer");
        if (m)
            rect(m, i * 32, 0, i * 32 + 32, 32);
        check(glUnmapBuffer(GL_ARRAY_BUFFER) == GL_TRUE, "glUnmapBuffer");
        uniform4(p, "color", i ? 0.0f : 1.0f, i ? 1.0f : 0.0f, 0, 1);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }
    // Read back through a read-only map.
    const float *r = glMapBuffer(GL_ARRAY_BUFFER, GL_READ_ONLY);
    float expect[8];
    rect(expect, 32, 0, 64, 32);
    check(r && !memcmp(r, expect, sizeof expect), "read-only map sees the data");
    glUnmapBuffer(GL_ARRAY_BUFFER);
    snap();
    check_px(16, 16, 255, 0, 0, 255, "first mapped draw");
    check_px(48, 16, 0, 255, 0, 255, "second mapped draw");
    glok("map buffer");
}

static void case_readback_orientation(void) {
    // Four coloured quadrants, read back whole, as a sub-rectangle, with pack
    // alignment / row length / skip, and partly outside the framebuffer.
    GLuint p = program(kVS, kFScolor, "pos", NULL);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    float c[4][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {1, 1, 1}};
    for (int i = 0; i < 4; i++) {
        uniform4(p, "color", c[i][0], c[i][1], c[i][2], 1);
        draw_rect((i & 1) * 32, (i / 2) * 32, (i & 1) * 32 + 32, (i / 2) * 32 + 32);
    }
    snap();
    check_px(0, 0, 255, 0, 0, 255, "row 0 is the bottom");
    check_px(63, 63, 255, 255, 255, 255, "top-right");
    unsigned char sub[3 * 5 * 3 + 16];
    memset(sub, 0xEE, sizeof sub);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(30, 30, 5, 3, GL_RGB, GL_UNSIGNED_BYTE, sub);
    dump(sub, sizeof sub);
    check(sub[0] == 255 && sub[1] == 0 && sub[3 * 4 + 1] == 255 && sub[(2 * 5) * 3 + 2] == 255,
          "GL_RGB sub-rectangle");
    unsigned char rl[10 * 4 * 4];
    memset(rl, 0, sizeof rl);
    glPixelStorei(GL_PACK_ROW_LENGTH, 10);
    glPixelStorei(GL_PACK_SKIP_PIXELS, 2);
    glPixelStorei(GL_PACK_SKIP_ROWS, 1);
    glReadPixels(31, 31, 2, 2, GL_RGBA, GL_UNSIGNED_BYTE, rl);
    glPixelStorei(GL_PACK_ROW_LENGTH, 0);
    glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
    glPixelStorei(GL_PACK_SKIP_ROWS, 0);
    dump(rl, sizeof rl);
    check(rl[(10 + 2) * 4] == 255 && rl[(10 + 3) * 4 + 1] == 255 && rl[(20 + 2) * 4 + 2] == 255,
          "pack row length / skip");
    unsigned char edge[4 * 4 * 4];
    memset(edge, 0x77, sizeof edge);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glReadPixels(62, 62, 4, 4, GL_RGBA, GL_UNSIGNED_BYTE, edge);
    check(edge[0] == 255 && edge[3 * 4] == 0x77, "partly outside: inside pixels read, outside untouched");
    float f[4];
    glReadPixels(0, 0, 1, 1, GL_RGBA, GL_FLOAT, f);
    check(f[0] == 1.0f && f[1] == 0.0f, "GL_FLOAT read");
    glok("readback");
}

static void case_tex_subimage(void) {
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    GLuint p = program(kVStex, kFStex, "pos", "tc", NULL);
    glUniform1i(glGetUniformLocation(p, "tex"), 0);
    static unsigned char d[4 * 4 * 4];
    memset(d, 0, sizeof d);
    GLuint t = texture_rgba(4, 4, d, GL_NEAREST);
    // Update between draws in the same frame: each draw sees its version.
    for (int i = 0; i < 4; i++) {
        unsigned char c[4] = {(unsigned char)(i * 80), 255, (unsigned char)(255 - i * 80), 255};
        unsigned char px4[2 * 2 * 4];
        for (int k = 0; k < 4; k++)
            memcpy(px4 + k * 4, c, 4);
        glTexSubImage2D(GL_TEXTURE_2D, 0, (i & 1) * 2, (i / 2) * 2, 2, 2, GL_RGBA, GL_UNSIGNED_BYTE, px4);
        draw_tex_rect(i * 16, 0, i * 16 + 16, 16);
    }
    snap();
    check_px(4, 4, 0, 255, 255, 255, "draw 0 sees update 0");
    check_px(16 + 12, 4, 80, 255, 175, 255, "draw 1 sees update 1");
    check_px(16 * 3 + 12, 12, 240, 255, 15, 255, "draw 3 sees all updates");
    check_px(12, 12, 0, 0, 0, 0, "draw 0 doesn't see later updates");
    glDeleteTextures(1, &t);
    glok("tex subimage");
}

static void case_unpack_alignment(void) {
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    GLuint p = program(kVStex, kFStex, "pos", "tc", NULL);
    glUniform1i(glGetUniformLocation(p, "tex"), 0);
    // 3x2 RGB with 4-byte row alignment (rows of 9 bytes padded to 12).
    static const unsigned char rgb4[] = {255, 0, 0, 0, 255, 0, 0, 0, 255, 0xAA, 0xAA, 0xAA,
                                         255, 255, 0, 0, 255, 255, 255, 0, 255, 0xAA, 0xAA, 0xAA};
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 3, 2, 0, GL_RGB, GL_UNSIGNED_BYTE, rgb4);
    draw_tex_rect(0, 0, 24, 16);
    // Row length + skip: a 2x2 window of a 4x4 RGBA image at (1, 2).
    static unsigned char big[4 * 4 * 4];
    for (int i = 0; i < 16; i++) {
        big[i * 4] = (unsigned char)(i * 16);
        big[i * 4 + 1] = 0;
        big[i * 4 + 2] = 0;
        big[i * 4 + 3] = 255;
    }
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 4);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 1);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, 2);
    GLuint t2;
    glGenTextures(1, &t2);
    glBindTexture(GL_TEXTURE_2D, t2);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, big);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
    draw_tex_rect(32, 0, 48, 16);
    // 565 packed.
    static const unsigned short p565[] = {0xF800, 0x07E0, 0x001F, 0xFFFF};
    GLuint t3;
    glGenTextures(1, &t3);
    glBindTexture(GL_TEXTURE_2D, t3);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 2, 2, 0, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, p565);
    draw_tex_rect(0, 32, 16, 48);
    snap();
    check_px(4, 4, 255, 0, 0, 255, "RGB row 0 texel 0");
    check_px(20, 4, 0, 0, 255, 255, "RGB row 0 texel 2");
    check_px(4, 12, 255, 255, 0, 255, "RGB row 1 after padding");
    check_px(36, 4, 9 * 16, 0, 0, 255, "skip rows/pixels texel (1,2)");
    check_px(44, 12, 14 * 16, 0, 0, 255, "row length texel (2,3)");
    check_px(4, 36, 255, 0, 0, 255, "565 red");
    check_px(12, 44, 255, 255, 255, 255, "565 white");
    glok("unpack");
}

static void case_uniforms(void) {
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    static const char vs[] =
        "#version 120\n"
        "attribute vec2 pos;\n"
        "uniform mat4 m;\n"
        "uniform vec4 off[3];\n"
        "void main() { gl_Position = m * vec4(pos, 0.0, 1.0) + off[1]; }\n";
    static const char fs[] =
        "#version 120\n"
        "uniform vec3 c3;\n"
        "uniform float f[2];\n"
        "uniform int sel;\n"
        "uniform bool flag;\n"
        "uniform vec2 c2;\n"
        "uniform sampler2D tex[2];\n"
        "void main() {\n"
        "  vec4 t = sel == 1 ? texture2D(tex[1], vec2(0.5)) : texture2D(tex[0], vec2(0.5));\n"
        "  gl_FragColor = vec4(c3 * f[1] + vec3(c2, 0.0), 1.0) * (flag ? 1.0 : 0.0) + t;\n"
        "}\n";
    GLuint p = program(vs, fs, "pos", NULL);
    // m translates x by +0.5 (row-major, uploaded transposed); off[1] moves y by +0.25.
    static const GLfloat m[16] = {1, 0, 0, 0.5f, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    glUniformMatrix4fv(glGetUniformLocation(p, "m"), 1, GL_TRUE, m);
    GLint loff = glGetUniformLocation(p, "off[1]");
    check(loff >= 0 && loff == glGetUniformLocation(p, "off") + 1, "array element location");
    glUniform4f(loff, 0, 0.25f, 0, 0);
    glUniform3f(glGetUniformLocation(p, "c3"), 1, 0.5f, 0);
    glUniform1fv(glGetUniformLocation(p, "f"), 2, (const GLfloat[]){0, 0.5f});
    glUniform1i(glGetUniformLocation(p, "sel"), 1);
    glUniform1i(glGetUniformLocation(p, "flag"), 1);
    glUniform2fv(glGetUniformLocation(p, "c2"), 1, (const GLfloat[]){0, 0.25f});
    GLint tl = glGetUniformLocation(p, "tex");
    glUniform1iv(tl, 2, (const GLint[]){2, 3});
    static const unsigned char blue[4] = {0, 0, 128, 0}, red[4] = {255, 0, 0, 0};
    glActiveTexture(GL_TEXTURE2);
    texture_rgba(1, 1, red, GL_NEAREST);
    glActiveTexture(GL_TEXTURE3);
    texture_rgba(1, 1, blue, GL_NEAREST);
    glActiveTexture(GL_TEXTURE0);
    draw_rect(0, 0, 16, 16);  // lands at x 16..32, y 8..24
    GLfloat back[4] = {0};
    glGetUniformfv(p, loff, back);
    check(back[1] == 0.25f, "glGetUniformfv");
    // A second program with its own values; switch back and forth.
    GLuint p2 = program(kVS, kFScolor, "pos", NULL);
    uniform4(p2, "color", 0, 1, 0, 1);
    draw_rect(48, 48, 64, 64);
    glUseProgram(p);
    glUniform1i(glGetUniformLocation(p, "sel"), 0);  // tex[0] = unit 2 = red
    draw_rect(-16, 32, 0, 48);                        // lands at x 0..16, y 40..56
    snap();
    check_px(24, 16, 128, 128, 128, 255, "uniform types");
    check_px(8, 48, 255, 128, 0, 255, "sampler array element 0");
    check_px(56, 56, 0, 255, 0, 255, "second program");
    check_px(8, 8, 0, 0, 0, 255, "matrix moved the quad");
    glok("uniforms");
}

// glBindAttribLocation after link: no effect until the next link, which uses it (attribute 15:
// the highest vertex buffer index).
static void case_rebind_attrib(void) {
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    GLuint p = program(kVScol, kFSvcol, "pos", "col", NULL);  // col at 1
    float v[8];
    rect(v, 0, 0, W, H);
    static const unsigned char red[16] = {255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255};
    static const unsigned char green[16] = {0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255};
    GLuint b[3];
    glGenBuffers(3, b);
    glBindBuffer(GL_ARRAY_BUFFER, b[0]);
    glBufferData(GL_ARRAY_BUFFER, sizeof v, v, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
    glBindBuffer(GL_ARRAY_BUFFER, b[1]);
    glBufferData(GL_ARRAY_BUFFER, sizeof red, red, GL_STATIC_DRAW);
    glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, 0, 0);
    glBindBuffer(GL_ARRAY_BUFFER, b[2]);
    glBufferData(GL_ARRAY_BUFFER, sizeof green, green, GL_STATIC_DRAW);
    glVertexAttribPointer(15, 4, GL_UNSIGNED_BYTE, GL_TRUE, 0, 0);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(15);
    glViewport(0, 0, W / 2, H);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);  // left half: col from 1 (red)
    glBindAttribLocation(p, 15, "col");
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);  // not linked yet: still red
    glLinkProgram(p);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    check(ok, "relink");
    glUseProgram(p);
    glViewport(W / 2, 0, W / 2, H);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);  // right half: col from 15, the last one (green)
    glDisableVertexAttribArray(1);
    glDisableVertexAttribArray(15);
    glViewport(0, 0, W, H);
    snap();
    check_px(W / 4, H / 2, 255, 0, 0, 255, "binding before the relink");
    check_px(3 * W / 4, H / 2, 0, 255, 0, 255, "binding after the relink");
    glok("rebind_attrib");
}

static void case_queries(void) {
    // State round trips; the values go into the dump and must match GL.
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ZERO);
    glBlendEquation(GL_FUNC_REVERSE_SUBTRACT);
    glDepthFunc(GL_GEQUAL);
    glDepthMask(GL_FALSE);
    glStencilFuncSeparate(GL_BACK, GL_NOTEQUAL, 3, 0x0F);
    glStencilOp(GL_INCR_WRAP, GL_DECR, GL_INVERT);
    glStencilMask(0x33);
    glCullFace(GL_FRONT);
    glFrontFace(GL_CW);
    glEnable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glViewport(1, 2, 30, 40);
    glScissor(3, 4, 5, 6);
    glColorMask(GL_TRUE, GL_FALSE, GL_TRUE, GL_FALSE);
    glClearColor(0.25f, 0.5f, 0.75f, 1);
    glClearDepth(0.5);
    glClearStencil(7);
    glActiveTexture(GL_TEXTURE3);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 2);
    glAlphaFunc(GL_LEQUAL, 0.5f);
    static const GLenum q[] = {
        GL_BLEND_SRC_RGB, GL_BLEND_DST_RGB, GL_BLEND_SRC_ALPHA, GL_BLEND_DST_ALPHA, GL_BLEND_EQUATION_RGB,
        GL_BLEND_EQUATION_ALPHA, GL_DEPTH_FUNC, GL_DEPTH_WRITEMASK, GL_STENCIL_FUNC, GL_STENCIL_BACK_FUNC,
        GL_STENCIL_BACK_REF, GL_STENCIL_BACK_VALUE_MASK, GL_STENCIL_FAIL, GL_STENCIL_PASS_DEPTH_FAIL,
        GL_STENCIL_PASS_DEPTH_PASS, GL_STENCIL_WRITEMASK, GL_STENCIL_BACK_WRITEMASK, GL_CULL_FACE_MODE,
        GL_FRONT_FACE, GL_CULL_FACE, GL_BLEND, GL_DEPTH_TEST, GL_STENCIL_TEST, GL_SCISSOR_TEST, GL_VIEWPORT,
        GL_SCISSOR_BOX, GL_COLOR_WRITEMASK, GL_STENCIL_CLEAR_VALUE, GL_ACTIVE_TEXTURE, GL_UNPACK_ALIGNMENT,
        GL_PACK_ALIGNMENT, GL_ALPHA_TEST_FUNC, GL_TEXTURE_BINDING_2D, GL_ARRAY_BUFFER_BINDING,
        GL_ELEMENT_ARRAY_BUFFER_BINDING, GL_CURRENT_PROGRAM, GL_FRAMEBUFFER_BINDING, GL_RENDERBUFFER_BINDING,
        GL_MAX_TEXTURE_SIZE, GL_MAX_VERTEX_ATTRIBS, GL_MAX_TEXTURE_IMAGE_UNITS, GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS,
        GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS, GL_MAX_RENDERBUFFER_SIZE, GL_MAX_VERTEX_UNIFORM_COMPONENTS,
        GL_MAX_FRAGMENT_UNIFORM_COMPONENTS, GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, GL_MAX_VIEWPORT_DIMS,
        GL_RED_BITS, GL_ALPHA_BITS, GL_DEPTH_BITS, GL_STENCIL_BITS,
    };
    for (size_t i = 0; i < sizeof q / sizeof q[0]; i++) {
        GLint v[4] = {-1, -1, -1, -1};
        glGetIntegerv(q[i], v);
        GLenum e = glGetError();
        check(e == 0, "glGetIntegerv(0x%04x)", q[i]);
        for (int k = 0; k < 4; k++)
            dump_int(v[k]);
        // Booleans of names differ: GL's offscreen framebuffer 0 is an FBO.
        GLboolean b[4] = {0};
        if (q[i] != GL_FRAMEBUFFER_BINDING)
            glGetBooleanv(q[i], b);
        dump(b, sizeof b);
    }
    GLfloat f[4];
    glGetFloatv(GL_COLOR_CLEAR_VALUE, f);
    check(f[0] == 0.25f && f[2] == 0.75f, "GL_COLOR_CLEAR_VALUE");
    dump(f, sizeof f);
    glGetFloatv(GL_DEPTH_CLEAR_VALUE, f);
    check(f[0] == 0.5f, "GL_DEPTH_CLEAR_VALUE");
    glGetFloatv(GL_ALPHA_TEST_REF, f);
    check(f[0] == 0.5f, "GL_ALPHA_TEST_REF");
    // What Apple's legacy context doesn't have.
    GLint v = 0;
    glGetIntegerv(GL_MAX_VERTEX_UNIFORM_VECTORS, &v);
    dump_int((int)glGetError());
    const char *ext = (const char *)glGetString(GL_EXTENSIONS);
    check(ext && strstr(ext, "GL_EXT_bgra") && strstr(ext, "GL_EXT_texture_compression_s3tc") &&
              !strstr(ext, "GL_ARB_get_program_binary"),
          "extension string");
    check(!strncmp((const char *)glGetString(GL_VERSION), "2.1", 3), "GL_VERSION 2.1");
    check(!strcmp((const char *)glGetString(GL_SHADING_LANGUAGE_VERSION), "1.20"), "GLSL 1.20");
    glok("queries");
}

static void case_errors(void) {
    // The first error is kept until glGetError; each call below records one.
    GLenum e[16];
    int n = 0;
    glEnable(0x1234);
    e[n++] = glGetError();
    glBlendFunc(GL_ONE, 0x1234);
    e[n++] = glGetError();
    glViewport(0, 0, -1, 4);
    e[n++] = glGetError();
    glUseProgram(9999);
    e[n++] = glGetError();
    glBindBuffer(0x1234, 1);
    e[n++] = glGetError();
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBufferData(GL_ARRAY_BUFFER, 4, NULL, GL_STATIC_DRAW);  // nothing bound
    e[n++] = glGetError();
    glClear(0x1);
    e[n++] = glGetError();
    glEnable(0x1234);
    glViewport(0, 0, -1, 4);
    e[n++] = glGetError();  // the first of two
    e[n++] = glGetError();  // then clean
    glPixelStorei(GL_UNPACK_ALIGNMENT, 3);
    e[n++] = glGetError();
    for (int i = 0; i < n; i++) {
        printf("INFO error %d = 0x%04x\n", i, e[i]);
        dump_int((int)e[i]);
    }
    check(e[0] == GL_INVALID_ENUM && e[2] == GL_INVALID_VALUE && e[7] == GL_INVALID_ENUM && e[8] == 0,
          "error model");
}


// The imports no other case calls (program binaries, info logs, CGL, ...).
static void case_api(void) {
    rs_gfx_context *ctx = rs_gfx_current();
    check(CGLLockContext(rs_gfx_cgl(ctx)) == kCGLNoError, "CGLLockContext");
    check(CGLUnlockContext(rs_gfx_cgl(ctx)) == kCGLNoError, "CGLUnlockContext");
    GLuint p = glCreateProgram();
    GLuint vs = shader(GL_VERTEX_SHADER, kVS), fs = shader(GL_FRAGMENT_SHADER, kFScolor);
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glBindAttribLocation(p, 0, "pos");
    glBindFragDataLocation(p, 0, "gl_FragColor_unused");
    glGetError();
    glProgramParameteri(p, GL_PROGRAM_BINARY_RETRIEVABLE_HINT, GL_TRUE);
    glGetError();  // Apple's legacy context: may be an error
    glLinkProgram(p);
    char log[256];
    GLsizei n = -1;
    glGetShaderInfoLog(vs, sizeof log, &n, log);
    check(n >= 0, "glGetShaderInfoLog");
    glGetProgramInfoLog(p, sizeof log, &n, log);
    check(n >= 0, "glGetProgramInfoLog");
    // Program binaries: no formats, so a reload fails cleanly.
    static char bin[4096];
    GLsizei got = 0;
    GLenum fmt = 0;
    glGetProgramBinary(p, sizeof bin, &got, &fmt, bin);
    glGetError();
    GLuint p2 = glCreateProgram();
    glProgramBinary(p2, fmt, bin, got);
    glGetError();
    GLint ok2 = 1;
    glGetProgramiv(p2, GL_LINK_STATUS, &ok2);
    check(!ok2, "glProgramBinary without formats leaves the program unlinked");
    glDeleteProgram(p2);
    glDeleteProgram(p);
    glDeleteShader(vs);
    glDeleteShader(fs);
    // Texture parameter / string queries.
    GLuint t = texture_rgba(1, 1, (const unsigned char[]){1, 2, 3, 4}, GL_LINEAR);
    GLint mf = 0;
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, &mf);
    check(mf == GL_LINEAR, "glGetTexParameteriv");
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, (GLfloat)GL_NEAREST);
    glDeleteTextures(1, &t);
    const GLubyte *e0 = glGetStringi(GL_EXTENSIONS, 0);
    glGetError();  // GL 3 only on Apple's legacy context
    (void)e0;
    GLint rb = 0;
    glGenRenderbuffers(1, (GLuint *)&rb);
    glBindRenderbuffer(GL_RENDERBUFFER, (GLuint)rb);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, 4, 4);
    GLint w = 0;
    glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_WIDTH, &w);
    check(w == 4 && glIsRenderbuffer((GLuint)rb), "renderbuffer");
    glDeleteRenderbuffers(1, (GLuint *)&rb);
    GLuint fb;
    glGenFramebuffers(1, &fb);
    glBindFramebuffer(GL_FRAMEBUFFER, fb);
    check(glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE, "empty FBO incomplete");
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &fb);
    GLuint vao;
    glGenVertexArrays(1, &vao);
    glGetError();
    glBindVertexArray(0);
    glGetError();
    glDeleteVertexArrays(1, &vao);
    glGetError();
    glUniform2f(-1, 0, 0);
    glUniform1iv(-1, 0, NULL);
    glUniform2fv(-1, 0, NULL);
    glUniform1f(-1, 0);
    glUniform3fv(-1, 0, NULL);
    glUniform4fv(-1, 0, NULL);
    glGetError();
    glStencilFunc(GL_ALWAYS, 0, 0xFF);
    glDrawArraysInstancedARB(GL_TRIANGLES, 0, 0, 1);
    glDrawElementsInstanced(GL_TRIANGLES, 0, GL_UNSIGNED_SHORT, NULL, 1);
    glDrawElementsInstancedARB(GL_TRIANGLES, 0, GL_UNSIGNED_SHORT, NULL, 1);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    snap();
    glok("api");
}

typedef struct {
    const char *name;
    void (*fn)(void);
    int tol;  // per-channel tolerance between backends
} test_case;

static const test_case kCases[] = {
    {"clear", case_clear, 0},
    {"triangle", case_triangle, 0},
    {"indexed", case_indexed, 0},
    {"instanced", case_instanced, 1},
    {"instance_id", case_instance_id, 1},
    {"client_arrays", case_client_arrays, 1},
    {"blend", case_blend, 1},
    {"depth", case_depth, 0},
    {"stencil", case_stencil, 0},
    {"scissor", case_scissor, 0},
    {"viewport", case_viewport, 2},
    {"fbo", case_fbo, 0},
    {"render_to_texture", case_render_to_texture, 0},
    {"depth_texture", case_depth_texture, 1},
    {"cube_map", case_cube_map, 0},
    {"dxt", case_dxt, 1},
    {"bgra", case_bgra, 0},
    {"luminance", case_luminance, 0},
    {"mipmaps", case_mipmaps, 2},
    {"alpha_test", case_alpha_test, 2},
    {"polygon_mode", case_polygon_mode, 0},
    {"subdata_in_flight", case_subdata_in_flight, 0},
    {"map_buffer", case_map_buffer, 0},
    {"readback_orientation", case_readback_orientation, 0},
    {"tex_subimage", case_tex_subimage, 0},
    {"unpack_alignment", case_unpack_alignment, 0},
    {"uniforms", case_uniforms, 1},
    {"rebind_attrib", case_rebind_attrib, 0},
    {"queries", case_queries, 0},
    {"errors", case_errors, 0},
    {"api", case_api, 0},
};
enum { N_CASES = sizeof kCases / sizeof kCases[0] };

static const test_case *find_case(const char *name) {
    for (int i = 0; i < N_CASES; i++)
        if (!strcmp(kCases[i].name, name))
            return &kCases[i];
    return NULL;
}

static int run_case(const test_case *c, const char *dump_path) {
    g_backend = rs_gfx_backend() == RS_GFX_METAL ? "metal" : "gl";
    rs_gfx_context *ctx = rs_gfx_create_offscreen(W, H);
    check(ctx != NULL, "rs_gfx_create_offscreen");
    if (ctx) {
        rs_gfx_make_current(ctx);
        rs_gfx_lock(ctx);
        glGetError();
        reset_state();
        c->fn();
        rs_gfx_present(ctx);
        rs_gfx_unlock(ctx);
        rs_gfx_make_current(NULL);
        rs_gfx_destroy(ctx);
    }
    if (dump_path) {
        FILE *f = fopen(dump_path, "wb");
        if (f) {
            fwrite(g_dump, 1, g_dump_len, f);
            fclose(f);
        }
    }
    // Not "N passed": only the --compare summary may match the ctest pass pattern.
    printf("%s on %s: %d checks ok, %d failed\n", c->name, g_backend, passed, failed);
    return failed != 0;
}

static unsigned char *read_file(const char *path, size_t *n) {
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *b = malloc((size_t)len + 1);
    *n = fread(b, 1, (size_t)len, f);
    fclose(f);
    return b;
}

static int spawn(const char *self, const char *backend, const char *name, const char *dump_path) {
    char envb[64];
    snprintf(envb, sizeof envb, "RS_GL_BACKEND=%s", backend);
    int n = 0;
    while (environ[n])
        n++;
    char **env = calloc((size_t)n + 2, sizeof *env);
    int k = 0;
    for (int i = 0; i < n; i++)
        if (strncmp(environ[i], "RS_GL_BACKEND=", 14))
            env[k++] = environ[i];
    env[k++] = envb;
    char *argv[] = {(char *)self, (char *)name, "--dump", (char *)dump_path, NULL};
    pid_t pid;
    fflush(stdout);
    if (posix_spawn(&pid, self, NULL, NULL, argv, env)) {
        free(env);
        return -1;
    }
    free(env);
    int st = 0;
    waitpid(pid, &st, 0);
    return WIFEXITED(st) ? WEXITSTATUS(st) : 128;
}

static int compare(const char *self, const test_case *c) {
    g_backend = "compare";
    char pg[256], pm[256];
    snprintf(pg, sizeof pg, "metal_core_%s.gl.bin", c->name);
    snprintf(pm, sizeof pm, "metal_core_%s.metal.bin", c->name);
    int rg = spawn(self, "gl", c->name, pg), rm = spawn(self, "metal", c->name, pm);
    check(rg == 0, "%s passes on gl (exit %d)", c->name, rg);
    check(rm == 0, "%s passes on metal (exit %d)", c->name, rm);
    size_t ng = 0, nm = 0;
    unsigned char *g = read_file(pg, &ng), *m = read_file(pm, &nm);
    check(g && m && ng == nm && ng > 0, "dumps exist and have the same size (%zu, %zu)", ng, nm);
    if (g && m && ng == nm) {
        size_t bad = 0, first = (size_t)-1;
        int maxd = 0;
        for (size_t i = 0; i < ng; i++) {
            int d = abs(g[i] - m[i]);
            if (d > c->tol) {
                if (!bad)
                    first = i;
                bad++;
            }
            maxd = d > maxd ? d : maxd;
        }
        if (bad) {
            size_t pxi = first / 4;
            printf("first difference at byte %zu (pixel %zu,%zu of a %dx%d snap): gl %d metal %d\n", first,
                   pxi % W, (pxi / W) % H, W, H, g[first], m[first]);
        }
        check(!bad, "gl and metal dumps match within %d (%zu bytes differ, max %d)", c->tol, bad, maxd);
        if (!bad) {
            remove(pg);
            remove(pm);
        }
    }
    free(g);
    free(m);
    printf("%d passed, %d failed\n", passed, failed);
    return failed != 0;
}


// --- Benchmark (not a test): a Scaleform-like frame of small draws -------------------
#include <time.h>
static double now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

static int bench(int frames, int draws, int subdata) {
    g_backend = rs_gfx_backend() == RS_GFX_METAL ? "metal" : "gl";
    rs_gfx_context *ctx = rs_gfx_create_offscreen(1280, 720);
    if (!ctx)
        return 1;
    rs_gfx_make_current(ctx);
    GLuint p = program(kVStex, kFStex, "pos", "tc", NULL);
    glUniform1i(glGetUniformLocation(p, "tex"), 0);
    static unsigned char tex[64 * 64 * 4];
    memset(tex, 200, sizeof tex);
    GLuint t[4];
    for (int i = 0; i < 4; i++)
        t[i] = texture_rgba(64, 64, tex, GL_LINEAR);
    GLuint vbo;
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, 64 * 1024, NULL, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 16, 0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 16, (void *)8);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    GLint lc = glGetUniformLocation(p, "tex");
    double t0 = 0;
    for (int f = 0; f < frames + 10; f++) {
        if (f == 10)
            t0 = now_ms();
        glClear(GL_COLOR_BUFFER_BIT);
        for (int d = 0; d < draws; d++) {
            float v[16] = {-0.1f, -0.1f, 0, 0, 0.1f, -0.1f, 1, 0, -0.1f, 0.1f, 0, 1, 0.1f, 0.1f, 1, 1};
            v[0] += (float)(d % 10) * 0.05f;
            if (subdata || f == 0)
                glBufferSubData(GL_ARRAY_BUFFER, (d % 64) * 64, sizeof v, v);
            glBindTexture(GL_TEXTURE_2D, t[d & 3]);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glUniform1i(lc, 0);
            glEnable(GL_SCISSOR_TEST);
            glDisable(GL_SCISSOR_TEST);
            glDrawArrays(GL_TRIANGLE_STRIP, (d % 64) * 4, 4);
        }
        rs_gfx_present(ctx);
    }
    unsigned char sync[4];
    glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, sync);  // waits for the GPU
    double ms = now_ms() - t0;
    printf("%s: %d frames x %d draws (%s): %.3f ms/frame wall, %.2f us/draw\n", g_backend, frames, draws,
           subdata ? "glBufferSubData per draw" : "static buffer",
           ms / frames, ms * 1000 / frames / draws);
    rs_gfx_make_current(NULL);
    rs_gfx_destroy(ctx);
    return 0;
}


// Every case in one process on the current backend, then the API coverage:
// all of the game's imports must have been called.
static int run_all(void) {
    for (int i = 0; i < N_CASES; i++) {
        g_dump_len = 0;
        g_vbo = 0;
        run_case(&kCases[i], NULL);
    }
    struct rs_gl_stat st[512];
    int n = rs_gl_stats(NULL), used = 0, game = 0;
    if (n > 512)
        n = 512;
    rs_gl_stats(st);
    // The game's imports come first in the table (game-gl-imports.txt).
    for (int i = 0; i < n && i < RS_GL_GAME_COUNT; i++) {
        game++;
        if (st[i].calls)
            used++;
        else
            printf("UNCALLED %s\n", st[i].name);
    }
    printf("COVERAGE %d/%d game GL imports called\n", used, game);
    check(used == game, "every game import was called");
    printf("all on %s: %d passed, %d failed\n", g_backend, passed, failed);
    return failed != 0;
}

int main(int argc, char **argv) {
    if (argc >= 2 && !strcmp(argv[1], "--all"))
        return run_all();
    if (argc >= 2 && !strcmp(argv[1], "--bench"))
        return bench(argc > 2 ? atoi(argv[2]) : 200, argc > 3 ? atoi(argv[3]) : 500, argc > 4 ? atoi(argv[4]) : 1);
    if (argc >= 2 && !strcmp(argv[1], "--list")) {
        for (int i = 0; i < N_CASES; i++)
            printf("%s\n", kCases[i].name);
        return 0;
    }
    if (argc >= 3 && !strcmp(argv[1], "--compare")) {
        const test_case *c = find_case(argv[2]);
        if (!c) {
            fprintf(stderr, "no case %s\n", argv[2]);
            return 2;
        }
        return compare(argv[0], c);
    }
    if (argc >= 2) {
        const test_case *c = find_case(argv[1]);
        if (!c) {
            fprintf(stderr, "no case %s\n", argv[1]);
            return 2;
        }
        const char *dump_path = argc >= 4 && !strcmp(argv[2], "--dump") ? argv[3] : NULL;
        return run_case(c, dump_path);
    }
    fprintf(stderr, "usage: %s <case> [--dump FILE] | --compare <case> | --list\n", argv[0]);
    return 2;
}
