// Exercises every GL/CGL function Rocksmith2014 imports, through a real
// NSOpenGLContext, and checks the rendered pixels.
//
//   gl_native_test [--legacy] [--window] [--frames N]
//
//   (default)   3.2 core profile, offscreen (FBO only)
//   --legacy    2.1 legacy profile: only the checks that make sense there
//               (glAlphaFunc / GL_ALPHA_TEST), to compare with core
//   --window    also attach the context to an NSWindow and render N frames
//               (default 30) from a CVDisplayLink thread under
//               CGLLockContext, the way the game's imports suggest it renders
//
// Built twice: against the system OpenGL.framework, and against the
// libRSOpenGL shim, where it also prints API coverage (every one of the
// game's 107 imports must have been called). Output ends with
// "N passed, M failed".
#define GL_SILENCE_DEPRECATION
#import <Cocoa/Cocoa.h>
#import <CoreVideo/CoreVideo.h>
#include <OpenGL/gl3.h>
#include <OpenGL/OpenGL.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

#include "gl_api.h"

#pragma clang diagnostic ignored "-Wdeprecated-declarations"

#define GL_ALPHA_TEST 0x0BC0
#define GL_COMPRESSED_RGB_S3TC_DXT1_EXT 0x83F0
#ifndef GL_PROGRAM_BINARY_RETRIEVABLE_HINT
#define GL_PROGRAM_BINARY_RETRIEVABLE_HINT 0x8257
#endif
#ifndef GL_PROGRAM_BINARY_LENGTH
#define GL_PROGRAM_BINARY_LENGTH 0x8741
#endif
#ifndef GL_NUM_PROGRAM_BINARY_FORMATS
#define GL_NUM_PROGRAM_BINARY_FORMATS 0x87FE
#endif

static int passed, failed;

static void check(int ok, const char *what) {
    if (ok)
        passed++;
    else
        failed++;
    printf("%s %s\n", ok ? "PASS" : "FAIL", what);
}

static void info(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void info(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    printf("INFO ");
    vprintf(fmt, ap);
    printf("\n");
    va_end(ap);
}

// glGetError must be clean after a group of calls.
static void glok(const char *what) {
    GLenum e = glGetError();
    char buf[256];
    snprintf(buf, sizeof buf, "%s (glGetError 0x%04x)", what, e);
    check(e == GL_NO_ERROR, buf);
}

static NSOpenGLContext *make_context(NSOpenGLPixelFormat **pf_out, int legacy) {
    NSOpenGLPixelFormatAttribute attrs[] = {
        NSOpenGLPFAAccelerated,
        NSOpenGLPFADoubleBuffer,
        NSOpenGLPFAColorSize, 24,
        NSOpenGLPFAAlphaSize, 8,
        NSOpenGLPFADepthSize, 24,
        NSOpenGLPFAStencilSize, 8,
        NSOpenGLPFAOpenGLProfile,
        legacy ? NSOpenGLProfileVersionLegacy : NSOpenGLProfileVersion3_2Core,
        0,
    };
    NSOpenGLPixelFormat *pf = [[NSOpenGLPixelFormat alloc] initWithAttributes:attrs];
    check(pf != nil, "NSOpenGLPixelFormat initWithAttributes");
    if (!pf)
        return nil;
    NSOpenGLContext *ctx = [[NSOpenGLContext alloc] initWithFormat:pf shareContext:nil];
    check(ctx != nil, "NSOpenGLContext initWithFormat");
    *pf_out = pf;
    return ctx;
}

static int has_extension(const char *name) {
    GLint n = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &n);
    for (GLint i = 0; i < n; i++)
        if (!strcmp((const char *)glGetStringi(GL_EXTENSIONS, (GLuint)i), name))
            return 1;
    return 0;
}

static GLuint compile(GLenum type, const char *src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0, len = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    glGetShaderiv(s, GL_INFO_LOG_LENGTH, &len);
    char log[2048] = "";
    glGetShaderInfoLog(s, sizeof log, NULL, log);
    if (!ok)
        printf("shader log: %s\n", log);
    check(ok, type == GL_VERTEX_SHADER ? "vertex shader compiles" : "fragment shader compiles");
    return s;
}

static const char *kVS =
    "#version 150\n"
    "in vec2 pos;\n"
    "in vec2 offset;\n"
    "uniform mat4 u_mvp;\n"
    "void main() { gl_Position = u_mvp * vec4(pos + offset, 0.0, 1.0); }\n";

// Every uniform type the game uses feeds the output, so none is optimized
// out; the test sets all but u_v4 so they contribute zero.
static const char *kFS =
    "#version 150\n"
    "uniform vec4 u_v4;\n"
    "uniform vec3 u_v3;\n"
    "uniform vec2 u_v2;\n"
    "uniform vec2 u_v2b;\n"
    "uniform float u_f;\n"
    "uniform float u_fv[2];\n"
    "uniform int u_iv[2];\n"
    "uniform sampler2D u_tex;\n"
    "out vec4 frag;\n"
    "void main() {\n"
    "  frag = u_v4 * u_f + vec4(u_v3, 0.0) + vec4(u_v2, u_v2b)\n"
    "       + vec4(u_fv[0] + u_fv[1] + float(u_iv[0] + u_iv[1]))\n"
    "       + texture(u_tex, vec2(0.5)) * u_fv[1];\n"
    "}\n";

static void read_px(int x, int y, unsigned char out[4]) {
    glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, out);
}

static int px_is(const unsigned char p[4], int r, int g, int b) {
    return abs(p[0] - r) <= 2 && abs(p[1] - g) <= 2 && abs(p[2] - b) <= 2;
}

static void check_px(int x, int y, int r, int g, int b, const char *what) {
    unsigned char p[4];
    read_px(x, y, p);
    char buf[256];
    snprintf(buf, sizeof buf, "%s: pixel (%d,%d) = %d,%d,%d want %d,%d,%d", what, x, y, p[0],
             p[1], p[2], r, g, b);
    check(px_is(p, r, g, b), buf);
}

// --- Core-profile run: every import ---------------------------------------------
static void run_core(void) {
    enum { W = 64, H = 64 };

    printf("INFO GL_VENDOR   %s\n", glGetString(GL_VENDOR));
    printf("INFO GL_RENDERER %s\n", glGetString(GL_RENDERER));
    printf("INFO GL_VERSION  %s\n", glGetString(GL_VERSION));
    printf("INFO GLSL        %s\n", glGetString(GL_SHADING_LANGUAGE_VERSION));
    check(!strncmp((const char *)glGetString(GL_VERSION), "4.1", 3) ||
              !strncmp((const char *)glGetString(GL_VERSION), "3.", 2),
          "core context is GL 3.2+");
    int s3tc = has_extension("GL_EXT_texture_compression_s3tc");
    info("GL_EXT_texture_compression_s3tc: %s", s3tc ? "yes" : "no");

    GLint max_tex = 0;
    GLfloat clear[4] = {-1, -1, -1, -1};
    GLboolean dmask = GL_FALSE;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_tex);
    glGetFloatv(GL_COLOR_CLEAR_VALUE, clear);
    glGetBooleanv(GL_DEPTH_WRITEMASK, &dmask);
    info("GL_MAX_TEXTURE_SIZE %d", max_tex);
    check(max_tex >= 4096 && clear[0] == 0 && dmask == GL_TRUE, "glGetIntegerv/Floatv/Booleanv");
    glok("queries");

    // Framebuffer: RGBA8 texture + depth/stencil renderbuffer.
    GLuint color, rb, fbo;
    glGenTextures(1, &color);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, color);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, (GLfloat)GL_NEAREST);
    glGenRenderbuffers(1, &rb);
    glBindRenderbuffer(GL_RENDERBUFFER, rb);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, W, H);
    GLint rbw = 0;
    glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_WIDTH, &rbw);
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, rb);
    GLint attach_type = 0;
    glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                          GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &attach_type);
    check(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE, "FBO complete");
    check(glIsFramebuffer(fbo) && glIsRenderbuffer(rb) && rbw == W && attach_type == GL_TEXTURE,
          "glIsFramebuffer/glIsRenderbuffer/attachment queries");
    glok("framebuffer setup");

    // Program, with the binary-retrievable hint the game sets.
    GLuint vs = compile(GL_VERTEX_SHADER, kVS), fs = compile(GL_FRAGMENT_SHADER, kFS);
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glBindAttribLocation(prog, 0, "pos");
    glBindAttribLocation(prog, 1, "offset");
    glBindFragDataLocation(prog, 0, "frag");
    glProgramParameteri(prog, GL_PROGRAM_BINARY_RETRIEVABLE_HINT, GL_TRUE);
    glLinkProgram(prog);
    GLint linked = 0;
    char plog[2048] = "";
    glGetProgramiv(prog, GL_LINK_STATUS, &linked);
    glGetProgramInfoLog(prog, sizeof plog, NULL, plog);
    if (!linked)
        printf("link log: %s\n", plog);
    check(linked, "program links");
    glok("program setup");

    // Program binaries: the game caches them. Apple's GL may offer no format.
    GLint nformats = 0, blen = 0;
    glGetIntegerv(GL_NUM_PROGRAM_BINARY_FORMATS, &nformats);
    glGetProgramiv(prog, GL_PROGRAM_BINARY_LENGTH, &blen);
    info("GL_NUM_PROGRAM_BINARY_FORMATS %d, GL_PROGRAM_BINARY_LENGTH %d", nformats, blen);
    {
        static char bin[1 << 20];
        GLsizei got = 0;
        GLenum fmt = 0;
        glGetProgramBinary(prog, sizeof bin, &got, &fmt, bin);
        GLenum e = glGetError();
        GLuint p2 = glCreateProgram();
        glProgramBinary(p2, fmt, bin, got);
        GLint ok2 = 0;
        glGetProgramiv(p2, GL_LINK_STATUS, &ok2);
        GLenum e2 = glGetError();
        info("program binary round trip: got %d bytes fmt 0x%x (err 0x%x), reload linked=%d "
             "(err 0x%x)", got, fmt, e, ok2, e2);
        // Either it works, or it fails cleanly (link status 0) and the game
        // falls back to compiling from source.
        check(nformats > 0 ? (got > 0 && ok2) : !ok2,
              "glGetProgramBinary/glProgramBinary behave consistently with format count");
        glDeleteProgram(p2);
    }

    glUseProgram(prog);
    const char *names[] = {"u_mvp", "u_v4", "u_v3", "u_v2", "u_v2b", "u_f", "u_fv", "u_iv", "u_tex"};
    GLint loc[9];
    int all = 1;
    for (int i = 0; i < 9; i++)
        all &= (loc[i] = glGetUniformLocation(prog, names[i])) >= 0;
    check(all, "all uniform locations found");
    static const GLfloat ident[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    static const GLfloat zero[4] = {0, 0, 0, 0};
    static const GLint izero[2] = {0, 0};
    glUniformMatrix4fv(loc[0], 1, GL_FALSE, ident);
    glUniform3fv(loc[2], 1, zero);
    glUniform2f(loc[3], 0, 0);
    glUniform2fv(loc[4], 1, zero);
    glUniform1f(loc[5], 1.0f);
    glUniform1fv(loc[6], 2, zero);
    glUniform1iv(loc[7], 2, izero);
    glUniform1i(loc[8], 1);  // texture unit 1, bound below
    glok("uniforms");

    // A 4x4 texture on unit 1: TexSubImage, mipmaps, level queries.
    GLuint small;
    glActiveTexture(GL_TEXTURE1);
    glGenTextures(1, &small);
    glBindTexture(GL_TEXTURE_2D, small);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    unsigned char grey[4 * 4 * 4];
    memset(grey, 0x80, sizeof grey);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 4, 4, GL_RGBA, GL_UNSIGNED_BYTE, grey);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glGenerateMipmap(GL_TEXTURE_2D);
    GLint l1w = 0, minf = 0;
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 1, GL_TEXTURE_WIDTH, &l1w);
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, &minf);
    unsigned char lvl2[4] = {0};
    glGetTexImage(GL_TEXTURE_2D, 2, GL_RGBA, GL_UNSIGNED_BYTE, lvl2);
    check(l1w == 2 && minf == GL_LINEAR_MIPMAP_LINEAR && px_is(lvl2, 0x80, 0x80, 0x80),
          "TexSubImage2D + GenerateMipmap + GetTexLevelParameteriv/GetTexParameteriv/GetTexImage");
    glActiveTexture(GL_TEXTURE0);
    glok("small texture");

    // DXT1 block: color0 = pure red (565 0xF800), all indices 0.
    GLuint dxt;
    glGenTextures(1, &dxt);
    glBindTexture(GL_TEXTURE_2D, dxt);
    static const unsigned char block[8] = {0x00, 0xF8, 0x00, 0x00, 0, 0, 0, 0};
    glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGB_S3TC_DXT1_EXT, 4, 4, 0,
                           sizeof block, block);
    unsigned char dec[4 * 4 * 4] = {0};
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, dec);
    check(px_is(dec, 255, 0, 0), "glCompressedTexImage2D DXT1 decodes to red");
    glDeleteTextures(1, &dxt);
    glBindTexture(GL_TEXTURE_2D, color);
    glok("compressed texture");

    // Geometry: one 16x32-pixel quad (x -1..-0.5, y -1..0), placed by a
    // per-instance offset. Offsets e0..e3 bottom row, e4..e7 top row.
    static const GLfloat quad_init[8] = {0};
    static const GLfloat quad[8] = {-1, -1, -0.5f, -1, -1, 0, -0.5f, 0};
    static const GLfloat offs[16] = {0, 0, 0.5f, 0, 1, 0, 1.5f, 0, 0, 1, 0.5f, 1, 1, 1, 1.5f, 1};
    static const GLushort idx[6] = {0, 1, 2, 2, 1, 3};
    GLuint vao, vbo[3];
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glGenBuffers(3, vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo[0]);
    glBufferData(GL_ARRAY_BUFFER, sizeof quad, quad_init, GL_DYNAMIC_DRAW);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof quad / 2, quad);
    GLfloat *map = glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY);
    check(map != NULL, "glMapBuffer");
    if (map)
        memcpy(map + 4, quad + 4, sizeof quad / 2);
    check(glUnmapBuffer(GL_ARRAY_BUFFER) == GL_TRUE, "glUnmapBuffer");
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
    glEnableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, vbo[1]);
    glBufferData(GL_ARRAY_BUFFER, sizeof offs, offs, GL_STATIC_DRAW);
    glEnableVertexAttribArray(1);
    glVertexAttribDivisorARB(1, 1);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, vbo[2]);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof idx, idx, GL_STATIC_DRAW);
    glok("buffers and vertex arrays");

    // Fixed state the game sets every frame.
    glViewport(0, 0, W, H);
    glScissor(0, 0, W, H);
    glEnable(GL_SCISSOR_TEST);
    glClearColor(0, 0, 1, 1);
    glClearDepth(1.0);
    glClearStencil(0);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);
    glStencilMask(0xFF);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glBlendColor(0, 0, 0, 0);
    glBlendFunc(GL_ONE, GL_ZERO);
    glBlendFuncSeparate(GL_ONE, GL_ZERO, GL_ONE, GL_ZERO);
    glEnable(GL_STENCIL_TEST);
    glStencilFunc(GL_ALWAYS, 1, 0xFF);
    glStencilFuncSeparate(GL_FRONT_AND_BACK, GL_ALWAYS, 1, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glok("render state");

    // Choose the first instance by moving the offset pointer (no base instance in 3.2).
#define AT(e) (glBindBuffer(GL_ARRAY_BUFFER, vbo[1]), \
               glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, (void *)(sizeof(GLfloat) * 2 * (e))))
#define COLOR(r, g, b) glUniform4fv(loc[1], 1, (const GLfloat[]){r, g, b, 1})
    AT(0); COLOR(1, 0, 0);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    AT(1); COLOR(0, 1, 0);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0);
    AT(2); COLOR(1, 1, 0);
    glDrawArraysInstancedARB(GL_TRIANGLE_STRIP, 0, 4, 2);
    AT(4); COLOR(1, 0, 1);
    glDrawElementsInstanced(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0, 2);
    AT(6); COLOR(0, 1, 1);
    glDrawElementsInstancedARB(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0, 2);
    glok("draws");

    // Depth test: an equal-depth quad with GL_LESS must not overwrite.
    AT(0); COLOR(1, 1, 1);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    // Wireframe (desktop-only; no GL ES equivalent): must at least be accepted.
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    glok("glPolygonMode(GL_LINE)");
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    check_px(8, 16, 255, 0, 0, "glDrawArrays (and depth test kept it)");
    check_px(24, 16, 0, 255, 0, "glDrawElements");
    check_px(40, 16, 255, 255, 0, "glDrawArraysInstancedARB instance 0");
    check_px(56, 16, 255, 255, 0, "glDrawArraysInstancedARB instance 1");
    check_px(8, 48, 255, 0, 255, "glDrawElementsInstanced instance 0");
    check_px(24, 48, 255, 0, 255, "glDrawElementsInstanced instance 1");
    check_px(40, 48, 0, 255, 255, "glDrawElementsInstancedARB instance 0");
    check_px(56, 48, 0, 255, 255, "glDrawElementsInstancedARB instance 1");

    static unsigned char tex_px[W * H * 4];
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, tex_px);
    check(px_is(&tex_px[(16 * W + 24) * 4], 0, 255, 0), "glGetTexImage of the render target");
    glok("readback");

    // glAlphaFunc is legacy-only: in a core context it's an error, so the game
    // either never calls it there or relies on the error being harmless.
    glAlphaFunc(GL_GREATER, 0.5f);
    GLenum ae = glGetError();
    info("core profile glAlphaFunc -> glGetError 0x%04x", ae);
    check(ae == GL_INVALID_OPERATION || ae == GL_NO_ERROR, "glAlphaFunc in core is harmless");

    glDisable(GL_BLEND);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_SCISSOR_TEST);
    glDisableVertexAttribArray(1);
    glUseProgram(0);
    glBindVertexArray(0);
    glDeleteVertexArrays(1, &vao);
    glDeleteBuffers(3, vbo);
    glDeleteShader(vs);
    glDeleteShader(fs);
    glDeleteProgram(prog);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &fbo);
    glDeleteRenderbuffers(1, &rb);
    glDeleteTextures(1, &color);
    glDeleteTextures(1, &small);
    glok("cleanup");
}

static void run_legacy(void) {
    printf("INFO GL_VERSION  %s\n", glGetString(GL_VERSION));
    glEnable(GL_ALPHA_TEST);
    glAlphaFunc(GL_GREATER, 0.5f);
    glok("legacy glEnable(GL_ALPHA_TEST) + glAlphaFunc");
    GLint func = 0;
    glGetIntegerv(0x0BC1 /* GL_ALPHA_TEST_FUNC */, &func);
    check(func == GL_GREATER, "legacy GL_ALPHA_TEST_FUNC reads back");
    glDisable(GL_ALPHA_TEST);
}

// --- Window + CVDisplayLink -------------------------------------------------------
static NSOpenGLContext *g_ctx;
static volatile int g_frames;

static CVReturn on_vsync(CVDisplayLinkRef link, const CVTimeStamp *now, const CVTimeStamp *out,
                         CVOptionFlags fin, CVOptionFlags *fout, void *ud) {
    (void)link; (void)now; (void)out; (void)fin; (void)fout; (void)ud;
    CGLContextObj cgl = [g_ctx CGLContextObj];
    CGLLockContext(cgl);
    [g_ctx makeCurrentContext];
    float t = (g_frames % 2) ? 1.0f : 0.0f;
    glClearColor(t, 0.5f, 1.0f - t, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    [g_ctx flushBuffer];
    g_frames++;
    CGLUnlockContext(cgl);
    return kCVReturnSuccess;
}

static void run_window(NSOpenGLPixelFormat *pf, int frames) {
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
    NSWindow *win = [[NSWindow alloc] initWithContentRect:NSMakeRect(100, 100, 320, 240)
                                                styleMask:NSWindowStyleMaskTitled
                                                  backing:NSBackingStoreBuffered
                                                    defer:NO];
    win.releasedWhenClosed = NO;  // ARC owns it
    NSView *view = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 320, 240)];
    [view setWantsBestResolutionOpenGLSurface:YES];
    [win setContentView:view];
    [win orderFront:nil];
    [g_ctx setView:view];
    GLint swap = 1;
    [g_ctx setValues:&swap forParameter:NSOpenGLContextParameterSwapInterval];
    [g_ctx update];

    CGDirectDisplayID disp = CGMainDisplayID();
    CGOpenGLDisplayMask mask = CGDisplayIDToOpenGLDisplayMask(disp);
    info("CGDisplayIDToOpenGLDisplayMask(main) = 0x%x", mask);
    check(mask != 0, "CGDisplayIDToOpenGLDisplayMask");

    CVDisplayLinkRef link = NULL;
    check(CVDisplayLinkCreateWithActiveCGDisplays(&link) == kCVReturnSuccess, "CVDisplayLinkCreate");
    check(CVDisplayLinkSetCurrentCGDisplayFromOpenGLContext(link, [g_ctx CGLContextObj],
                                                            [pf CGLPixelFormatObj]) ==
              kCVReturnSuccess,
          "CVDisplayLinkSetCurrentCGDisplayFromOpenGLContext");
    CVDisplayLinkSetOutputCallback(link, on_vsync, NULL);
    [NSOpenGLContext clearCurrentContext];
    CVDisplayLinkStart(link);
    check(CVDisplayLinkIsRunning(link), "CVDisplayLinkIsRunning");
    NSDate *deadline = [NSDate dateWithTimeIntervalSinceNow:5];
    while (g_frames < frames && [deadline timeIntervalSinceNow] > 0)
        [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode
                                 beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
    CVDisplayLinkStop(link);
    CVDisplayLinkRelease(link);
    char buf[128];
    snprintf(buf, sizeof buf, "rendered %d/%d vsync frames on the display-link thread", g_frames,
             frames);
    check(g_frames >= frames, buf);

    CGLContextObj cgl = [g_ctx CGLContextObj];
    CGLLockContext(cgl);
    [g_ctx makeCurrentContext];
    glClearColor(0, 1, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    check_px(1, 1, 0, 255, 0, "default framebuffer of the window");
    [g_ctx flushBuffer];
    CGLUnlockContext(cgl);
    [g_ctx clearDrawable];
    [win close];
}

// --- Coverage (only when linked against the shim) ------------------------------
static void report_coverage(int must_cover_all) {
    int (*stats)(struct rs_gl_stat *) = (int (*)(struct rs_gl_stat *))dlsym(RTLD_DEFAULT, "rs_gl_stats");
    if (!stats)
        return;
    struct rs_gl_stat st[RS_GL_COUNT];
    int n = stats(st), used = 0;
    for (int i = 0; i < n; i++) {
        if (st[i].calls)
            used++;
        else if (must_cover_all)
            printf("UNCALLED %s\n", st[i].name);
    }
    printf("COVERAGE %d/%d game GL imports called through the shim\n", used, n);
    if (must_cover_all)
        check(used == n, "every game GL import was exercised");
}

int main(int argc, char **argv) {
    int legacy = 0, window = 0, frames = 30;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--legacy"))
            legacy = 1;
        else if (!strcmp(argv[i], "--window"))
            window = 1;
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc)
            frames = atoi(argv[++i]);
    }
    @autoreleasepool {
        NSOpenGLPixelFormat *pf = nil;
        g_ctx = make_context(&pf, legacy);
        if (!g_ctx)
            return 1;
        CGLContextObj cgl = [g_ctx CGLContextObj];
        check(CGLLockContext(cgl) == kCGLNoError, "CGLLockContext");
        [g_ctx makeCurrentContext];
        glGetError();
        if (legacy)
            run_legacy();
        else
            run_core();
        check(CGLUnlockContext(cgl) == kCGLNoError, "CGLUnlockContext");
        if (window)
            run_window(pf, frames);
        report_coverage(!legacy && window);
    }
    printf("%d passed, %d failed\n", passed, failed);
    return failed != 0;
}
