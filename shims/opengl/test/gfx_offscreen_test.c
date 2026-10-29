// Offscreen context through rs_gfx.h, same GL calls on either backend
// (RS_GL_BACKEND=gl|metal): clear framebuffer 0, draw a triangle with a GLSL
// 1.20 program from a VBO, read the pixels back and check them exactly.
//
//   gfx_offscreen_test        prints "N passed, 0 failed"
#include "gl_api.h"
#include "rs_gfx.h"

#include <stdio.h>
#include <string.h>

#define GL_COLOR_BUFFER_BIT 0x4000
#define GL_DEPTH_BUFFER_BIT 0x0100
#define GL_TRIANGLES 0x0004
#define GL_FLOAT 0x1406
#define GL_ARRAY_BUFFER 0x8892
#define GL_STATIC_DRAW 0x88E4
#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_RGBA 0x1908
#define GL_UNSIGNED_BYTE 0x1401
#define GL_PACK_ALIGNMENT 0x0D05
#define GL_FRAMEBUFFER 0x8D40
#define GL_FRAMEBUFFER_BINDING 0x8CA6
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_VIEWPORT 0x0BA2
#define GL_DEPTH_TEST 0x0B71
#define GL_BLEND 0x0BE2

enum { W = 64, H = 48 };

static int passed, failed;
static void check(int ok, const char *what) {
    if (ok)
        passed++;
    else {
        failed++;
        printf("FAIL %s\n", what);
    }
}

static GLuint shader(GLenum type, const char *src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024] = "";
        glGetShaderInfoLog(s, sizeof log, NULL, log);
        printf("shader: %s\n", log);
    }
    check(ok, "shader compiles");
    return s;
}

static const char kVS[] =
    "#version 120\n"
    "attribute vec2 pos;\n"
    "void main() { gl_Position = vec4(pos, 0.0, 1.0); }\n";
static const char kFS[] =
    "#version 120\n"
    "uniform vec4 color;\n"
    "void main() { gl_FragColor = color; }\n";

static int px_eq(const unsigned char *p, unsigned r, unsigned g, unsigned b, unsigned a) {
    return p[0] == r && p[1] == g && p[2] == b && p[3] == a;
}

int main(void) {
    const char *be = rs_gfx_backend() == RS_GFX_METAL ? "metal" : "gl";
    printf("backend %s\n", be);
    rs_gfx_context *ctx = rs_gfx_create_offscreen(W, H);
    check(ctx != NULL, "rs_gfx_create_offscreen");
    if (!ctx) {
        printf("%d passed, %d failed\n", passed, failed);
        return 1;
    }
    check(rs_gfx_current() == NULL, "not current before make_current");
    rs_gfx_make_current(ctx);
    check(rs_gfx_current() == ctx, "rs_gfx_current");
    check(rs_gfx_cgl(ctx) != NULL, "rs_gfx_cgl");
    rs_gfx_lock(ctx);

    glGetError();
    GLint fb = -1, vp[4] = {0};
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fb);
    check(fb == 0, "framebuffer 0 is bound");
    check(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE, "framebuffer 0 complete");
    glGetIntegerv(GL_VIEWPORT, vp);
    check(vp[0] == 0 && vp[1] == 0 && vp[2] == W && vp[3] == H, "initial viewport is w x h");

    GLuint prog = glCreateProgram();
    GLuint vs = shader(GL_VERTEX_SHADER, kVS), fs = shader(GL_FRAGMENT_SHADER, kFS);
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glBindAttribLocation(prog, 0, "pos");
    glLinkProgram(prog);
    GLint linked = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &linked);
    check(linked, "program links");
    glUseProgram(prog);
    glUniform4fv(glGetUniformLocation(prog, "color"), 1, (const GLfloat[]){1.0f, 0.0f, 0.0f, 1.0f});

    // Triangle in the bottom-left corner: (-1,-1), (0,-1), (-1,1).
    const GLfloat tri[] = {-1.0f, -1.0f, 0.0f, -1.0f, -1.0f, 1.0f};
    GLuint vbo;
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof tri, tri, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, 0, 0, (const void *)0);
    glEnableVertexAttribArray(0);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glClearColor(0.0f, 0.0f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    check(glGetError() == 0, "no GL error drawing");

    static unsigned char px[W * H * 4];
    memset(px, 0x55, sizeof px);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px);
    check(glGetError() == 0, "no GL error reading");
    rs_gfx_present(ctx);

    // Row 0 is the bottom. The hypotenuse is 2x + y + 1 = 0 in NDC: pixel centres
    // clearly inside are red, clearly outside blue (the edge itself is skipped).
    int bad = 0;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            double s = 2 * ((x + 0.5) / W * 2 - 1) + ((y + 0.5) / H * 2 - 1) + 1;
            const unsigned char *p = px + (y * W + x) * 4;
            if (s < -0.15) {
                if (!px_eq(p, 255, 0, 0, 255) && bad++ < 4)
                    printf("pixel %d,%d = %u,%u,%u,%u, want red\n", x, y, p[0], p[1], p[2], p[3]);
            } else if (s > 0.15) {
                if (!px_eq(p, 0, 0, 255, 255) && bad++ < 4)
                    printf("pixel %d,%d = %u,%u,%u,%u, want blue\n", x, y, p[0], p[1], p[2], p[3]);
            }
        }
    check(bad == 0, "pixels");
    check(px_eq(px, 255, 0, 0, 255), "bottom-left red");
    check(px_eq(px + ((H - 1) * W + W - 1) * 4, 0, 0, 255, 255), "top-right blue");

    // Resize: the default framebuffer follows.
    rs_gfx_resize(ctx, W / 2, H / 2);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, W / 2, H / 2);
    glClearColor(0.0f, 1.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    memset(px, 0x55, sizeof px);
    glReadPixels(0, 0, W / 2, H / 2, GL_RGBA, GL_UNSIGNED_BYTE, px);
    check(px_eq(px, 0, 255, 0, 255) && px_eq(px + ((H / 2) * (W / 2) - 1) * 4, 0, 255, 0, 255),
          "resized framebuffer 0");
    check(glGetError() == 0, "no GL error after resize");

    glDeleteBuffers(1, &vbo);
    glDeleteProgram(prog);
    glDeleteShader(vs);
    glDeleteShader(fs);
    rs_gfx_unlock(ctx);
    rs_gfx_make_current(NULL);
    check(rs_gfx_current() == NULL, "make_current(NULL)");
    rs_gfx_destroy(ctx);

    printf("%d passed, %d failed\n", passed, failed);
    return failed != 0;
}
