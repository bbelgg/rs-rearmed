// Offscreen context through rs_gfx.h, same GL calls on either backend
// (RS_GL_BACKEND=gl|metal): clear framebuffer 0, draw a triangle with a GLSL
// 1.20 program from a VBO, read the pixels back and check them exactly. Then
// rs_gl_current_program across binds, links, deletes and context switches,
// framebuffer 0 after deleting the bound framebuffer, and the render API for mods.
//
//   gfx_offscreen_test        prints "N passed, 0 failed"
#include "gl_api.h"
#include "rs_gfx.h"
#include "../../include/rs_render.h"

#include <pthread.h>
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
#define GL_READ_FRAMEBUFFER_BINDING 0x8CAA
#define GL_TEXTURE_2D 0x0DE1
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_NEAREST 0x2600
#define GL_TEXTURE0 0x84C0

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

static int px_eq(const unsigned char *p, unsigned r, unsigned g, unsigned b, unsigned a);

static GLuint link_program(void) {
    GLuint prog = glCreateProgram();
    GLuint vs = shader(GL_VERTEX_SHADER, kVS), fs = shader(GL_FRAGMENT_SHADER, kFS);
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glBindAttribLocation(prog, 0, "pos");
    glLinkProgram(prog);
    glDeleteShader(vs);
    glDeleteShader(fs);
    return prog;
}

static void *other_thread(void *arg) {
    *(int *)arg = rs_gl_current_program();
    return NULL;
}

// rs_gl_current_program: exact or -1, per context.
static void current_program(rs_gfx_context *ctx) {
    rs_gfx_make_current(ctx);
    glGetError();
    GLuint a = link_program(), b = link_program();
    glUseProgram(a);
    check(rs_gl_current_program() == (int)a, "current program: a");
    glUseProgram(b);
    check(rs_gl_current_program() == (int)b, "current program: b");
    glUseProgram(0);
    check(rs_gl_current_program() == 0, "current program: 0");

    // Binds that can fail (GL keeps the old program): unknown.
    GLuint unlinked = glCreateProgram();
    glUseProgram(unlinked);
    check(rs_gl_current_program() == -1, "current program: unlinked is unknown");
    glGetError();
    glUseProgram(a);
    glUseProgram(4000);  // never created
    check(rs_gl_current_program() == -1, "current program: bad name is unknown");
    glGetError();

    // Relinking the current program, deleting it.
    glUseProgram(a);
    glLinkProgram(a);
    check(rs_gl_current_program() == -1, "current program: relinked is unknown");
    glUseProgram(a);
    check(rs_gl_current_program() == (int)a, "current program: a again after relink");
    glDeleteProgram(a);
    check(rs_gl_current_program() == -1, "current program: deleted is unknown");
    glUseProgram(a);  // a deleted name
    check(rs_gl_current_program() == -1, "current program: deleted name stays unknown");
    glUseProgram(b);
    check(rs_gl_current_program() == (int)b, "current program: b after the delete");

    // Another context: its own state, also when switched to behind rs_gfx's back.
    rs_gfx_context *ctx2 = rs_gfx_create_offscreen(8, 8);
    check(ctx2 != NULL, "second context");
    rs_gfx_make_current(ctx2);
    check(rs_gl_current_program() == -1, "current program: unknown in a new context");
    glUseProgram(b);  // a name of the first context's namespace
    check(rs_gl_current_program() == -1, "current program: another group's name is unknown");
    glGetError();
    glUseProgram(0);
    check(rs_gl_current_program() == 0, "current program: 0 in the second context");
    rs_gfx_make_current(ctx);
    check(rs_gl_current_program() == (int)b, "current program: b back in the first");
    CGLSetCurrentContext(rs_gfx_cgl(ctx2));
    check(rs_gl_current_program() == 0, "current program: CGLSetCurrentContext switch");
    CGLSetCurrentContext(NULL);
    check(rs_gl_current_program() == -1, "current program: no context");
    rs_gfx_make_current(ctx);
    rs_gfx_destroy(ctx2);
    check(rs_gl_current_program() == (int)b, "current program: kept over another's destroy");

    int other = 0;
    pthread_t t;
    pthread_create(&t, NULL, other_thread, &other);
    pthread_join(t, NULL);
    check(other == -1, "current program: another thread without a context");

    glUseProgram(0);
    glDeleteProgram(b);
    glDeleteProgram(unlinked);
    check(glGetError() == 0, "current program: no GL error left");
}

// Deleting the bound framebuffer leaves framebuffer 0 bound and usable.
static void delete_bound_framebuffer(void) {
    GLuint fb = 0;
    glGenFramebuffers(1, &fb);
    glBindFramebuffer(GL_FRAMEBUFFER, fb);
    glDeleteFramebuffers(1, &fb);
    GLint draw = -1, read = -1;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &draw);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read);
    check(draw == 0 && read == 0, "framebuffer 0 bound after deleting the bound one");
    check(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE,
          "framebuffer 0 complete after deleting the bound one");
    GLfloat f = -1;
    GLboolean bo = 1;
    glGetFloatv(GL_FRAMEBUFFER_BINDING, &f);
    glGetBooleanv(GL_FRAMEBUFFER_BINDING, &bo);
    check(f == 0 && bo == 0, "framebuffer 0 through glGetFloatv / glGetBooleanv");
    glClearColor(1.0f, 1.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    unsigned char p[4] = {0};
    glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p);
    check(px_eq(p, 255, 255, 0, 255), "framebuffer 0 draws after deleting the bound one");
    check(glGetError() == 0, "no GL error deleting the bound framebuffer");
}

static int px_eq(const unsigned char *p, unsigned r, unsigned g, unsigned b, unsigned a) {
    return p[0] == r && p[1] == g && p[2] == b && p[3] == a;
}

static const char kTexFS[] =
    "#version 120\n"
    "uniform sampler2D tex;\n"
    "void main() { gl_FragColor = texture2D(tex, vec2(0.5, 0.5)); }\n";

static GLuint new_texture(const unsigned char *rgba) {
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    return t;
}

// Clears to blue, draws a quad of the bound texture over framebuffer 0 and reads a pixel
static void draw_texture(unsigned char *out) {
    glClearColor(0.0f, 0.0f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glReadPixels(4, 4, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, out);
}

typedef struct {
    rs_tex_rule *rule;
    const unsigned char *rgba;
    int result;
} replace_args;

static void *replace_thread(void *arg) {
    replace_args *a = arg;
    a->result = rs_render_replace(a->rule, 1, 1, a->rgba);
    return NULL;
}

// rs_render.h: what a rule does to draws, and to the uploads that follow it.
static void render_api(void) {
    static const unsigned char red[16] = {255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255};
    static const unsigned char white[16] = {255, 255, 255, 255, 255, 255, 255, 255,
                                            255, 255, 255, 255, 255, 255, 255, 255};
    static const unsigned char green[4] = {0, 255, 0, 255}, yellow[4] = {255, 255, 0, 255};
    if (rs_gfx_backend() != RS_GFX_METAL) {
        check(rs_render_api_version() == 0, "render API: no version on GL");
        check(rs_render_match(2, 2, 1) == NULL, "render API: no rules on GL");
        return;
    }
    check(rs_render_api_version() == RS_RENDER_API_VERSION, "render API: version");
    uint64_t hash = rs_render_hash(red, sizeof red);
    check(rs_render_hash(white, sizeof white) != hash, "render API: hash tells contents apart");
    rs_tex_rule *rule = rs_render_match(2, 2, hash);
    check(rule != NULL, "render API: a rule");
    check(rs_render_match(2, 2, hash) == rule, "render API: the same rule for the same texture");
    check(rs_render_match(4, 2, hash) != rule, "render API: another size is another rule");
    check(rs_render_replace(NULL, 1, 1, green) == -1, "render API: no rule");
    check(rs_render_replace(rule, 0, 1, green) == -1, "render API: size out of range");
    check(rs_render_hide(NULL, 1) == -1, "render API: hide with no rule");

    GLuint prog = glCreateProgram();
    GLuint vs = shader(GL_VERTEX_SHADER, kVS), fs = shader(GL_FRAGMENT_SHADER, kTexFS);
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glBindAttribLocation(prog, 0, "pos");
    glLinkProgram(prog);
    glUseProgram(prog);
    glUniform1i(glGetUniformLocation(prog, "tex"), 0);
    static const GLfloat quad[] = {-1, -1, 1, -1, 1, 1, -1, -1, 1, 1, -1, 1};
    GLuint vbo;
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof quad, quad, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, 0, 0, (const void *)0);
    glEnableVertexAttribArray(0);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glActiveTexture(GL_TEXTURE0);

    unsigned char px[4];
    GLuint own = new_texture(red), other = new_texture(white);
    glBindTexture(GL_TEXTURE_2D, own);
    draw_texture(px);
    check(px_eq(px, 255, 0, 0, 255), "render API: a rule alone changes nothing");

    rs_render_replace(rule, 1, 1, green);
    draw_texture(px);
    check(px_eq(px, 0, 255, 0, 255), "render API: replaced");
    glBindTexture(GL_TEXTURE_2D, other);
    draw_texture(px);
    check(px_eq(px, 255, 255, 255, 255), "render API: another content is left alone");
    glBindTexture(GL_TEXTURE_2D, own);

    pthread_t th;
    replace_args a = {rule, yellow, -1};
    pthread_create(&th, NULL, replace_thread, &a);
    pthread_join(th, NULL);
    check(a.result == 0, "render API: replaced from another thread");
    draw_texture(px);
    check(px_eq(px, 255, 255, 0, 255), "render API: the new pixels are drawn");

    rs_render_hide(rule, 1);
    draw_texture(px);
    check(px_eq(px, 0, 0, 255, 255), "render API: hidden");
    rs_render_hide(rule, 0);
    draw_texture(px);
    check(px_eq(px, 255, 255, 0, 255), "render API: shown again");

    rs_render_replace(rule, 0, 0, NULL);
    draw_texture(px);
    check(px_eq(px, 255, 0, 0, 255), "render API: back to the game's texture");

    // Learn & Play's upload: the level defined empty, then filled
    rs_render_replace(rule, 1, 1, green);
    GLuint filled = new_texture(NULL);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 2, 2, GL_RGBA, GL_UNSIGNED_BYTE, red);
    draw_texture(px);
    check(px_eq(px, 0, 255, 0, 255), "render API: a level filled by a sub-image");
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, white);
    draw_texture(px);
    check(px_eq(px, 255, 0, 0, 255) || px_eq(px, 255, 255, 255, 255), "render API: part of a level ends the rule");

    // Another upload to the texture
    glBindTexture(GL_TEXTURE_2D, own);
    draw_texture(px);
    check(px_eq(px, 0, 255, 0, 255), "render API: still replaced before an upload");
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
    draw_texture(px);
    check(px_eq(px, 255, 255, 255, 255), "render API: another upload ends the rule");
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, red);
    draw_texture(px);
    check(px_eq(px, 0, 255, 0, 255), "render API: the first content again finds it");
    rs_render_replace(rule, 0, 0, NULL);

    glDeleteTextures(1, &own);
    glDeleteTextures(1, &other);
    glDeleteTextures(1, &filled);
    glDeleteBuffers(1, &vbo);
    glUseProgram(0);
    glDeleteProgram(prog);
    glDeleteShader(vs);
    glDeleteShader(fs);
    check(glGetError() == 0, "render API: no GL error");
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
    // Safe load: GL does nothing; Metal without a disk cache (not an app bundle) has nothing to warm.
    check(rs_gfx_prepare(ctx, 1.0) == 0, "rs_gfx_prepare with nothing to warm");
    check(rs_gfx_prepare(NULL, 1.0) == (rs_gfx_backend() == RS_GFX_METAL ? -1 : 0), "rs_gfx_prepare(NULL)");
    rs_gfx_make_current(ctx);
    check(rs_gfx_current() == ctx, "rs_gfx_current");
    check(rs_gl_current_program() == -1, "current program: unknown before any bind");
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

    delete_bound_framebuffer();
    current_program(ctx);
    render_api();

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
