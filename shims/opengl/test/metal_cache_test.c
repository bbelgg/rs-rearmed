// Metal shader/pipeline cache test (src/metal/mtl_cache.m), headless.
//
//   metal_cache_test <dir>                 runs the scenario below, children share <dir>
//   metal_cache_test --child <nonce>       one run: link N programs, draw each with V pipeline
//                                          variants (checking a pixel), print timings
//   metal_cache_test --async <nonce>       one run: GL semantics of asynchronous library compiles
//                                          (status, uniforms and draws right after the link, a
//                                          compile that fails after the link reported success)
//   metal_cache_test --bench <dir> [N]     not a test: time from the first link to all N programs
//                                          drawn, cold (RS_MTL_SALT: no system Metal cache) and
//                                          warm, synchronous compiles vs the compile pool, and
//                                          safe load (rs_gfx_prepare, throttled pool)
//
// Scenario (fresh MSL per invocation via <nonce>, so the system's Metal cache starts cold too):
//   cold        empty cache: every pipeline is an archive miss
//   warm        RS_METAL_CACHE_WARM_SYNC=1: warming built every library and pipeline from the
//               manifest, all archive hits, nothing compiled on the drawing thread
//   warm-async  as the game sees it: warming races the first draws; no misses
//   no-cache    RS_METAL_CACHE=0, same MSL: what the system Metal cache alone gives
//   prepare     RSM_TEST_PREPARE (rs_gfx_prepare before the links, the shell's safe load): warm,
//               with a cold system Metal cache (RS_MTL_SALT_COMPILE), and timing out; then the
//               throttled pool on a cold first run (2 workers, utility QoS; RS_MTL_COMPILE_THREADS)
//   corrupt     garbage archive and a torn manifest; then a garbage manifest: discarded, no crash
//   stale       directories of other formats/OS/GPUs are removed, foreign files left alone
//   async       --async children with the compile pool and with RS_MTL_COMPILE_THREADS=0
//   prune       the game links other MSL for a few runs: the old libraries leave the manifest
//   corpus      safe load with an offline shader corpus (a small one written here): the first run
//               compiles its distinct libraries before the game (progress monotonic, reaching the
//               total), the game's own compiles of them are then system-cache hits; a recorded
//               complete warm and a big enough manifest skip it; a new corpus, the override and the
//               timeout; nothing without prepare
// Output ends with "N passed, M failed".
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>

#include "rs_gfx.h"
#include "msl_internal.h"  // the corpus writer (rsmsl, linked into this test too)

#include <fcntl.h>
#include <mach/mach_time.h>
#include <pthread.h>
#include <stdatomic.h>
#include <spawn.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern char **environ;

enum { W = 16, H = 16, NPROG = 16, NVAR = 4 };

static int passed, failed;

static void check(int ok, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void check(int ok, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    if (ok) {
        passed++;
    } else {
        failed++;
        printf("FAIL ");
        vprintf(fmt, ap);
        printf("\n");
    }
    va_end(ap);
}

static double now_ms(void) {
    static mach_timebase_info_data_t tb;
    if (!tb.denom)
        mach_timebase_info(&tb);
    return (double)mach_absolute_time() * tb.numer / tb.denom / 1e6;
}

// --- Child ---------------------------------------------------------------------------------
// RSM_TEST_PREPARE=<seconds>: rs_gfx_prepare before the first link, as the shell's safe load does.
// Warming starts with the context: total_ms is from rs_gfx_create_offscreen to prepare's return.
static double g_created_ms;
static rs_gfx_context *create_ctx(void) {
    g_created_ms = now_ms();
    return rs_gfx_create_offscreen(W, H);
}
// RSM_TEST_PROGRESS=1: another thread polls rs_gfx_prepare_progress while prepare runs.
static rs_gfx_context *g_pctx;
static atomic_int g_pstop;
static int g_psamples, g_prunning, g_pbad, g_plast_done, g_plast_total;
static void *progress_poller(void *arg) {
    (void)arg;
    int ld = 0, lt = 0;
    while (!atomic_load(&g_pstop)) {
        int d = -1, t = -1;
        int r = rs_gfx_prepare_progress(g_pctx, &d, &t);
        g_psamples++;
        g_prunning += r == 1;
        if (d < ld || t < lt || d > t || d < 0 || (r != 0 && r != 1))
            g_pbad++;
        ld = d, lt = t;
        usleep(500);
    }
    g_plast_done = ld, g_plast_total = lt;
    return NULL;
}

static void maybe_prepare(rs_gfx_context *c) {
    const char *e = getenv("RSM_TEST_PREPARE");
    if (!e || !*e)
        return;
    const char *pe = getenv("RSM_TEST_PROGRESS");
    pthread_t t;
    int poll = pe && *pe == '1';
    if (poll) {
        g_pctx = c;
        pthread_create(&t, NULL, progress_poller, NULL);
    }
    double t0 = now_ms();
    int rc = rs_gfx_prepare(c, atof(e));
    printf("PREPARE rc=%d ms=%.1f total_ms=%.1f\n", rc, now_ms() - t0, now_ms() - g_created_ms);
    if (poll) {
        atomic_store(&g_pstop, 1);
        pthread_join(t, NULL);
        int d = -1, tt = -1;
        int r = rs_gfx_prepare_progress(c, &d, &tt);
        printf("PROGRESS samples=%d running=%d bad=%d last_done=%d last_total=%d after_rc=%d after_done=%d "
               "after_total=%d\n",
               g_psamples, g_prunning, g_pbad, g_plast_done, g_plast_total, r, d, tt);
    }
}

static GLuint shader(GLenum type, const char *src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    return s;
}

static const char *kChildVS =
    "attribute vec2 pos;\n"
    "attribute vec4 col;\n"
    "varying vec4 v_col;\n"
    "void main() { v_col = col; gl_Position = vec4(pos, 0.0, 1.0); }\n";

// The child's fragment shader i (the corpus scenario writes the same ones into its corpus).
static void child_fs(char *fs, size_t n, int i, int nonce) {
    snprintf(fs, n,
             "varying vec4 v_col;\n"
             "uniform vec4 tint;\n"
             "void main() {\n"
             "    vec4 c = v_col * tint;\n"
             "    c.r = %d.0 / 255.0;\n"
             "    c.b = float(%d) * 1.0e-12;\n"  // per-run code: the system's Metal cache starts cold
             "    gl_FragColor = c;\n"
             "}\n",
             i * 8, nonce);
}

static int child(int nonce) {
    rs_gfx_context *c = create_ctx();
    if (!c) {
        printf("no context\n");
        return 1;
    }
    maybe_prepare(c);
    rs_gfx_make_current(c);
    const char *vs = kChildVS;
    GLuint prog[NPROG];
    double t0 = now_ms();
    for (int i = 0; i < NPROG; i++) {
        char fs[512];
        child_fs(fs, sizeof fs, i, nonce);
        GLuint p = glCreateProgram();
        glAttachShader(p, shader(GL_VERTEX_SHADER, vs));
        glAttachShader(p, shader(GL_FRAGMENT_SHADER, fs));
        glBindAttribLocation(p, 0, "pos");
        glBindAttribLocation(p, 1, "col");
        glLinkProgram(p);
        GLint ok = 0;
        glGetProgramiv(p, GL_LINK_STATUS, &ok);
        if (!ok) {
            char log[1024] = "";
            glGetProgramInfoLog(p, sizeof log, NULL, log);
            printf("link %d failed: %s\n", i, log);
            return 1;
        }
        prog[i] = p;
    }
    double link_ms = now_ms() - t0;

    // Two triangles covering the target; colour as floats (0, 0.5, 0, 1) or normalised bytes.
    static const float quad[] = {-1, -1, 1, -1, 1, 1, -1, -1, 1, 1, -1, 1};
    float fcol[6][4];
    unsigned char bcol[6][4];
    for (int v = 0; v < 6; v++) {
        fcol[v][0] = 0, fcol[v][1] = 128 / 255.0f, fcol[v][2] = 0, fcol[v][3] = 1;
        bcol[v][0] = 0, bcol[v][1] = 128, bcol[v][2] = 0, bcol[v][3] = 255;
    }
    GLuint vb[3];
    glGenBuffers(3, vb);
    glBindBuffer(GL_ARRAY_BUFFER, vb[0]);
    glBufferData(GL_ARRAY_BUFFER, sizeof quad, quad, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, vb[1]);
    glBufferData(GL_ARRAY_BUFFER, sizeof fcol, fcol, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, vb[2]);
    glBufferData(GL_ARRAY_BUFFER, sizeof bcol, bcol, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glBindBuffer(GL_ARRAY_BUFFER, vb[0]);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
    glViewport(0, 0, W, H);

    int bad = 0;
    double pass_ms[2];
    for (int pass = 0; pass < 2; pass++) {
        double p0 = now_ms();
        for (int i = 0; i < NPROG; i++) {
            glUseProgram(prog[i]);
            glUniform4f(glGetUniformLocation(prog[i], "tint"), 1, 1, 1, 1);
            for (int v = 0; v < NVAR; v++) {
                // Variants: plain; blending; byte colours (another vertex layout); alpha test.
                glBindBuffer(GL_ARRAY_BUFFER, vb[v == 2 ? 2 : 1]);
                if (v == 2)
                    glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, 0, 0);
                else
                    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 0, 0);
                if (v == 1) {
                    glEnable(GL_BLEND);
                    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                } else {
                    glDisable(GL_BLEND);
                }
                if (v == 3) {
                    glEnable(GL_ALPHA_TEST);
                    glAlphaFunc(GL_GREATER, 0.5f);
                } else {
                    glDisable(GL_ALPHA_TEST);
                }
                glClearColor(0, 0, 1, 1);
                glClear(GL_COLOR_BUFFER_BIT);
                glDrawArrays(GL_TRIANGLES, 0, 6);
                unsigned char px[4] = {0};
                glReadPixels(W / 2, H / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
                if (abs(px[0] - i * 8) > 1 || abs(px[1] - 128) > 1 || px[2] > 1 || px[3] < 254) {
                    if (bad++ < 4)
                        printf("program %d variant %d: pixel %d,%d,%d,%d want %d,128,0,255\n", i, v, px[0], px[1],
                               px[2], px[3], i * 8);
                }
            }
        }
        pass_ms[pass] = now_ms() - p0;
    }
    GLenum err = glGetError();
    printf("CHILD link_ms=%.1f draw1_ms=%.1f draw2_ms=%.1f bad=%d glerror=0x%x\n", link_ms, pass_ms[0], pass_ms[1],
           bad, err);
    rs_gfx_make_current(NULL);
    rs_gfx_destroy(c);
    return bad || err ? 1 : 0;
}

// --- Child: asynchronous link semantics ------------------------------------------------------
// Run with a fresh RS_MTL_SALT (compiles really pending at link time) and
// RS_MTL_TEST_FAIL=rs_fail_me. argv: sync = 1 when RS_MTL_COMPILE_THREADS=0.
enum { NASYNC = 24 };

static GLuint link_pair(const char *vs, const char *fs) {
    GLuint p = glCreateProgram();
    glAttachShader(p, shader(GL_VERTEX_SHADER, vs));
    glAttachShader(p, shader(GL_FRAGMENT_SHADER, fs));
    glBindAttribLocation(p, 0, "pos");
    glBindAttribLocation(p, 1, "col");
    glLinkProgram(p);
    return p;
}

static void quad_buffers(void) {
    static const float quad[] = {-1, -1, 1, -1, 1, 1, -1, -1, 1, 1, -1, 1};
    float col[6][4];
    for (int v = 0; v < 6; v++)
        col[v][0] = 0, col[v][1] = 128 / 255.0f, col[v][2] = 0, col[v][3] = 1;
    GLuint vb[2];
    glGenBuffers(2, vb);
    glBindBuffer(GL_ARRAY_BUFFER, vb[0]);
    glBufferData(GL_ARRAY_BUFFER, sizeof quad, quad, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
    glBindBuffer(GL_ARRAY_BUFFER, vb[1]);
    glBufferData(GL_ARRAY_BUFFER, sizeof col, col, GL_STATIC_DRAW);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 0, 0);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glViewport(0, 0, W, H);
}

// Clear to blue, draw, read the centre pixel.
static void draw_px(unsigned char px[4]) {
    glClearColor(0, 0, 1, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glReadPixels(W / 2, H / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
}

static int child_async(int nonce, int sync) {
    rs_gfx_context *c = create_ctx();
    if (!c) {
        printf("no context\n");
        return 1;
    }
    maybe_prepare(c);
    rs_gfx_make_current(c);
    quad_buffers();
    static const char *vs =
        "attribute vec2 pos;\n"
        "attribute vec4 col;\n"
        "varying vec4 v_col;\n"
        "void main() { v_col = col; gl_Position = vec4(pos, 0.0, 1.0); }\n";
    GLuint prog[NASYNC + 1];
    GLint ktab[NASYNC + 1];
    for (int i = 0; i <= NASYNC; i++) {
        // Program NASYNC has program 0's sources: one compile for both.
        int n = i == NASYNC ? 0 : i;
        char fs[768];
        snprintf(fs, sizeof fs,
                 "varying vec4 v_col;\n"
                 "uniform vec4 tint;\n"
                 "uniform float k[3];\n"
                 "void main() {\n"
                 "    vec4 c = v_col * tint;\n"
                 "    c.r = k[1] + float(%d) * 1.0e-12;\n"
                 "    c.b = float(%d) * 1.0e-12;\n"
                 "    gl_FragColor = c;\n"
                 "}\n",
                 n, nonce);
        GLuint p = link_pair(vs, fs);
        prog[i] = p;
        // Right after the link: status, log, reflection, uniforms.
        GLint ok = 0, loglen = -1;
        glGetProgramiv(p, GL_LINK_STATUS, &ok);
        glGetProgramiv(p, GL_INFO_LOG_LENGTH, &loglen);
        check(ok == 1 && loglen == 0, "program %d: LINK_STATUS %d, log length %d right after the link", i, ok,
              loglen);
        GLint tint = glGetUniformLocation(p, "tint"), k = glGetUniformLocation(p, "k"),
              k1 = glGetUniformLocation(p, "k[1]");
        check(tint >= 0 && k >= 0 && k1 == k + 1, "program %d: uniform locations tint %d k %d k[1] %d", i, tint, k,
              k1);
        GLint nu = 0, na = 0;
        glGetProgramiv(p, GL_ACTIVE_UNIFORMS, &nu);
        glGetProgramiv(p, GL_ACTIVE_ATTRIBUTES, &na);
        check(nu == 2 && na == 2, "program %d: %d active uniforms, %d attributes", i, nu, na);
        glUseProgram(p);
        glUniform4f(tint, 1, 1, 1, 1);
        float kv[3] = {0.25f, (float)(i * 8) / 255.0f, 0.75f};
        glUniform1fv(k, 3, kv);
        GLfloat back[4] = {0};
        glGetUniformfv(p, k1, back);
        check(back[0] == kv[1], "program %d: glGetUniformfv right after the link: %g want %g", i, back[0], kv[1]);
        ktab[i] = k1;
        // Draw right after the link (every fourth program; the rest are drawn below).
        if (i % 4 == 0) {
            unsigned char px[4];
            draw_px(px);
            check(abs(px[0] - i * 8) <= 1 && abs(px[1] - 128) <= 1 && px[2] <= 1,
                  "program %d drawn right after the link: %d,%d,%d want %d,128,0", i, px[0], px[1], px[2], i * 8);
        }
    }
    for (int i = 0; i <= NASYNC; i++) {
        glUseProgram(prog[i]);
        unsigned char px[4];
        draw_px(px);
        check(abs(px[0] - i * 8) <= 1 && abs(px[1] - 128) <= 1 && px[2] <= 1, "program %d: %d,%d,%d want %d,128,0",
              i, px[0], px[1], px[2], i * 8);
        // Uniform changes after the compile finished still land.
        float kv = (float)(255 - i * 8) / 255.0f;
        glUniform1f(ktab[i], kv);
        draw_px(px);
        check(abs(px[0] - (255 - i * 8)) <= 1, "program %d after glUniform1f: red %d want %d", i, px[0], 255 - i * 8);
    }
    check(glGetError() == GL_NO_ERROR, "no GL errors in the good programs");

    // A program whose MSL fails to compile (RS_MTL_TEST_FAIL): the link reports success when the
    // compile runs on the pool (sync: failure at once). The first draw finds the failure: it draws
    // nothing, raises no error, and the program is unlinked from then on with the compiler's log.
    static const char *bad_fs =
        "varying vec4 v_col;\n"
        "uniform float rs_fail_me;\n"
        "void main() { gl_FragColor = v_col * rs_fail_me; }\n";
    GLuint bad = link_pair(vs, bad_fs);
    GLint ok = -1;
    glGetProgramiv(bad, GL_LINK_STATUS, &ok);
    if (sync) {
        check(ok == 0, "sync: the failing program does not link (%d)", ok);
    } else {
        // Normally still compiling here (queued behind nothing, but a compile takes milliseconds).
        printf("async: failing program's LINK_STATUS right after the link: %d\n", ok);
        if (ok) {
            check(glGetUniformLocation(bad, "rs_fail_me") >= 0, "failing program: uniform location while pending");
            glUseProgram(bad);
            unsigned char px[4];
            draw_px(px);
            check(px[0] == 0 && px[1] == 0 && px[2] == 255, "failing program draws nothing: %d,%d,%d", px[0], px[1],
                  px[2]);
            check(glGetError() == GL_NO_ERROR, "failing program's draw raises no GL error");
        }
    }
    glGetProgramiv(bad, GL_LINK_STATUS, &ok);
    char log[4096] = "";
    GLint loglen = 0;
    glGetProgramiv(bad, GL_INFO_LOG_LENGTH, &loglen);
    glGetProgramInfoLog(bad, sizeof log, NULL, log);
    check(ok == 0, "failing program is unlinked after its compile (LINK_STATUS %d)", ok);
    check(strstr(log, "MSL compile failed") && strstr(log, "RS_MTL_TEST_FAIL") && loglen == (GLint)strlen(log) + 1,
          "failing program's info log (%d): %.200s", loglen, log);
    glGetError();
    glUseProgram(bad);
    check(glGetError() == GL_INVALID_OPERATION, "glUseProgram of the unlinked program: GL_INVALID_OPERATION");
    // The good programs still work.
    glUseProgram(prog[3]);
    glUniform1f(ktab[3], 24 / 255.0f);
    unsigned char px[4];
    draw_px(px);
    check(abs(px[0] - 24) <= 1 && abs(px[1] - 128) <= 1, "a good program after the failure: %d,%d", px[0], px[1]);
    rs_gfx_make_current(NULL);
    rs_gfx_destroy(c);
    printf("CHILD async done: %d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}

// --- Child: benchmark ----------------------------------------------------------------------
// N programs with fragment shaders of a size like the game's (loops over texture fetches and
// some arithmetic); time from the first link to the last one drawn.
static int child_bench(int n) {
    rs_gfx_context *c = create_ctx();
    if (!c)
        return 1;
    maybe_prepare(c);
    rs_gfx_make_current(c);
    quad_buffers();
    static const char *vs =
        "attribute vec2 pos;\n"
        "attribute vec4 col;\n"
        "varying vec4 v_col;\n"
        "varying vec2 v_uv;\n"
        "uniform mat4 mvp;\n"
        "void main() { v_col = col; v_uv = pos * 0.5 + 0.5; gl_Position = mvp * vec4(pos, 0.0, 1.0); }\n";
    GLuint *prog = calloc((size_t)n, sizeof *prog);
    double t0 = now_ms();
    for (int i = 0; i < n; i++) {
        char fs[4096];
        snprintf(fs, sizeof fs,
                 "varying vec4 v_col;\n"
                 "varying vec2 v_uv;\n"
                 "uniform sampler2D t0;\n"
                 "uniform sampler2D t1;\n"
                 "uniform vec4 p[8];\n"
                 "vec4 blur(sampler2D t, vec2 uv, float r) {\n"
                 "    vec4 s = vec4(0.0);\n"
                 "    for (int j = 0; j < %d; j++) {\n"
                 "        float a = float(j) * 0.7 + r;\n"
                 "        s += texture2D(t, uv + vec2(cos(a), sin(a)) * r * p[j - (j / 8) * 8].x);\n"
                 "    }\n"
                 "    return s / %d.0;\n"
                 "}\n"
                 "void main() {\n"
                 "    vec4 a = blur(t0, v_uv, p[1].y + %d.0 * 0.001);\n"
                 "    vec4 b = blur(t1, v_uv.yx, p[2].z);\n"
                 "    vec3 l = normalize(vec3(p[3].xy, 1.0));\n"
                 "    float d = max(dot(l, normalize(b.xyz * 2.0 - 1.0)), 0.0);\n"
                 "    vec4 c = mix(a, b, smoothstep(0.0, 1.0, d)) * v_col;\n"
                 "    c.rgb = pow(c.rgb, vec3(p[4].w + 1.0)) + vec3(%d.0 / 1.0e9);\n"
                 "    gl_FragColor = c * 0.0 + v_col;\n"
                 "}\n",
                 4 + i % 13, 4 + i % 13, i % 7, i);
        prog[i] = link_pair(vs, fs);
        GLint ok = 0;
        glGetProgramiv(prog[i], GL_LINK_STATUS, &ok);
        if (!ok) {
            char log[2048] = "";
            glGetProgramInfoLog(prog[i], sizeof log, NULL, log);
            printf("bench: link %d failed: %s\n", i, log);
            return 1;
        }
    }
    double link_ms = now_ms() - t0;
    static const float id[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    int bad = 0;
    for (int i = 0; i < n; i++) {
        glUseProgram(prog[i]);
        glUniformMatrix4fv(glGetUniformLocation(prog[i], "mvp"), 1, GL_FALSE, id);
        unsigned char px[4];
        glClearColor(0, 0, 1, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        if (i == n - 1 || i % 16 == 15) {
            glReadPixels(W / 2, H / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
            bad += abs(px[1] - 128) > 1;
        }
    }
    double total_ms = now_ms() - t0;
    printf("CHILD bench n=%d link_ms=%.1f total_ms=%.1f bad=%d\n", n, link_ms, total_ms, bad);
    rs_gfx_make_current(NULL);
    rs_gfx_destroy(c);
    return bad ? 1 : 0;
}

// --- Parent --------------------------------------------------------------------------------
typedef struct {
    int rc;
    double link_ms, draw1_ms, draw2_ms;
    int bad;
    // From the cache's stats line (the last one).
    int lib_compiled, lib_reused, lib_waited, lib_warmed, lib_failed;
    int pipe_built, pipe_reused, pipe_waited, pipe_warmed, hits, misses, discarded, writes;
    double lib_ms, pipe_ms, wait_ms, warm_ms;
    char log[8192];
} run_result;

static double num(const char *s, const char *key) {
    const char *p = s ? strstr(s, key) : NULL;
    return p ? atof(p + strlen(key)) : -1;
}

static char g_self[1024], g_dir[1024];
static int g_nonce;
static const char *g_mode = "--child", *g_arg2;  // child mode, extra child argument

static run_result run(const char *name, const char *extra_env[]) {
    run_result r;
    memset(&r, 0, sizeof r);
    char logpath[1300];
    snprintf(logpath, sizeof logpath, "%s.%s.log", g_dir, name);
    char nonce[32];
    snprintf(nonce, sizeof nonce, "%d", g_nonce);
    char *argv[] = {g_self, (char *)g_mode, nonce, (char *)g_arg2, NULL};
    // Environment: ours minus the cache knobs, then the run's.
    char *env[512];
    int n = 0;
    for (char **e = environ; *e && n < 480; e++)
        if (strncmp(*e, "RS_METAL_", 9) && strncmp(*e, "RS_GL_BACKEND=", 14) && strncmp(*e, "RS_MTL_", 7) &&
            strncmp(*e, "RS_MSL_CORPUS=", 14))
            env[n++] = *e;
    // No shader corpus unless the run names one.
    int has_corpus = 0;
    for (int i = 0; extra_env && extra_env[i]; i++)
        has_corpus |= !strncmp(extra_env[i], "RS_MSL_CORPUS=", 14);
    if (!has_corpus)
        env[n++] = "RS_MSL_CORPUS=";
    static char dirvar[1100];
    snprintf(dirvar, sizeof dirvar, "RS_METAL_CACHE_DIR=%s", g_dir);
    env[n++] = dirvar;
    env[n++] = "RS_GL_BACKEND=metal";
    env[n++] = "RS_METAL_CACHE_STATS=1";
    for (int i = 0; extra_env && extra_env[i]; i++)
        env[n++] = (char *)extra_env[i];
    env[n] = NULL;
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, 1, logpath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    posix_spawn_file_actions_adddup2(&fa, 1, 2);
    pid_t pid;
    double t0 = now_ms();
    if (posix_spawn(&pid, g_self, &fa, NULL, argv, env)) {
        r.rc = -1;
        return r;
    }
    posix_spawn_file_actions_destroy(&fa);
    int status = 0;
    waitpid(pid, &status, 0);
    double wall = now_ms() - t0;
    r.rc = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
    FILE *f = fopen(logpath, "r");
    size_t len = f ? fread(r.log, 1, sizeof r.log - 1, f) : 0;
    if (f)
        fclose(f);
    r.log[len] = 0;
    const char *ch = strstr(r.log, "CHILD ");
    r.link_ms = num(ch, "link_ms=");
    r.draw1_ms = num(ch, "draw1_ms=");
    r.draw2_ms = num(ch, "draw2_ms=");
    r.bad = (int)num(ch, "bad=");
    const char *s = NULL;
    for (const char *p = r.log; (p = strstr(p, "cache stats")); p++)
        s = p;
    r.lib_compiled = (int)num(s, "lib_compiled=");
    r.lib_ms = num(s, "lib_ms=");
    r.lib_reused = (int)num(s, "lib_reused=");
    r.lib_waited = (int)num(s, "lib_waited=");
    r.lib_warmed = (int)num(s, "lib_warmed=");
    r.lib_failed = (int)num(s, "lib_failed=");
    r.pipe_built = (int)num(s, "pipe_built=");
    r.pipe_ms = num(s, "pipe_ms=");
    r.pipe_reused = (int)num(s, "pipe_reused=");
    r.pipe_waited = (int)num(s, "pipe_waited=");
    r.pipe_warmed = (int)num(s, "pipe_warmed=");
    r.hits = (int)num(s, "archive_hits=");
    r.misses = (int)num(s, "archive_misses=");
    r.wait_ms = num(s, "wait_ms=");
    r.warm_ms = num(s, "warm_ms=");
    r.writes = (int)num(s, "archive_writes=");
    r.discarded = (int)num(s, "discarded=");
    printf("%-10s rc=%d wall %7.1f ms | link %7.1f  draw(first) %7.1f  draw(again) %6.1f | "
           "libs: compiled %2d (%7.1f ms) reused %2d waited %2d warmed %2d | pipelines: built %2d (%6.1f ms) "
           "reused %2d waited %2d warmed %2d | archive hits %2d misses %2d | warm %7.1f ms | discarded %d\n",
           name, r.rc, wall, r.link_ms, r.draw1_ms, r.draw2_ms, r.lib_compiled, r.lib_ms, r.lib_reused,
           r.lib_waited, r.lib_warmed, r.pipe_built, r.pipe_ms, r.pipe_reused, r.pipe_waited, r.pipe_warmed, r.hits,
           r.misses, r.warm_ms, r.discarded);
    if (r.rc || !ch || !s)
        printf("--- %s log ---\n%s--- end ---\n", name, r.log);
    return r;
}

static int sh(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static int sh(const char *fmt, ...) {
    char cmd[4096];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(cmd, sizeof cmd, fmt, ap);
    va_end(ap);
    return system(cmd);
}

// The single cache subdirectory (rsm*) of g_dir that is not one of the planted ones.
static int cache_subdir(char *out, size_t n) {
    char cmd[2048];
    snprintf(cmd, sizeof cmd, "ls -d '%s'/rsm[0-9]*-*-* 2>/dev/null | grep -v -e '-plantedold-' | head -1", g_dir);
    FILE *p = popen(cmd, "r");
    if (!p)
        return 0;
    int ok = fgets(out, (int)n, p) != NULL;
    pclose(p);
    if (ok)
        out[strcspn(out, "\n")] = 0;
    return ok && *out;
}

static off_t file_size(const char *path) {
    struct stat sb;
    return stat(path, &sb) ? -1 : sb.st_size;
}

// --- Benchmark (not a test) ------------------------------------------------------------------
static double bench_one(const char *name, int n, const char *salt, const char *const *env) {
    static char saltvar[128];
    snprintf(saltvar, sizeof saltvar, "RS_MTL_SALT=%s", salt);
    const char *e[8];
    int k = 0;
    e[k++] = saltvar;
    for (int i = 0; env && env[i]; i++)
        e[k++] = env[i];
    e[k] = NULL;
    char ns[16];
    snprintf(ns, sizeof ns, "%d", n);
    g_mode = "--bench-child";
    g_arg2 = ns;
    run_result r = run(name, e);
    double t = num(strstr(r.log, "CHILD bench"), "total_ms=");
    printf("  => %-22s first link to all %d drawn: %8.1f ms (links %.1f ms) rc=%d | pool: %d threads, limit %d, "
           "peak %d, compiled on the pool %d, by the drawing thread %d\n",
           name, n, t, num(strstr(r.log, "CHILD bench"), "link_ms="), r.rc, (int)num(r.log, "pool_threads="),
           (int)num(r.log, "pool_limit="), (int)num(r.log, "pool_peak="), (int)num(r.log, "lib_async="),
           (int)num(r.log, "lib_stolen="));
    const char *pl = strstr(r.log, "PREPARE ");
    if (pl)
        printf("     prepare: rc %d, %.1f ms in rs_gfx_prepare, %.1f ms from context creation; warmed %d libraries\n",
               (int)num(pl, "rc="), num(pl, "ms="), num(pl, "total_ms="), r.lib_warmed);
    return t;
}

static int bench(const char *self, const char *dir, int n) {
    if (!realpath(self, g_self))
        snprintf(g_self, sizeof g_self, "%s", self);
    snprintf(g_dir, sizeof g_dir, "%s", dir);
    char s1[64], s2[64];
    snprintf(s1, sizeof s1, "b%d-%ld-sync", (int)getpid(), (long)time(NULL));
    snprintf(s2, sizeof s2, "b%d-%ld-async", (int)getpid(), (long)time(NULL));
    const char *sync_nocache[] = {"RS_MTL_COMPILE_THREADS=0", "RS_METAL_CACHE=0", NULL};
    const char *sync_cache[] = {"RS_MTL_COMPILE_THREADS=0", NULL};
    // Cold: new salt (system Metal cache cold), empty disk cache.
    sh("rm -rf '%s' && mkdir -p '%s'", g_dir, g_dir);
    bench_one("cold sync", n, s1, sync_nocache);
    sh("rm -rf '%s' && mkdir -p '%s'", g_dir, g_dir);
    bench_one("cold pool", n, s2, NULL);
    // Warm: same salts. The system Metal cache alone, then with the disk cache of the cold pool
    // run (archive + manifest warming), as a second game start sees it.
    bench_one("warm sync, sys cache", n, s1, sync_nocache);
    bench_one("warm pool, sys cache", n, s2, (const char *[]){"RS_METAL_CACHE=0", NULL});
    bench_one("warm sync, +disk", n, s2, sync_cache);
    bench_one("warm pool, +disk", n, s2, NULL);
    // Safe load (rs_gfx_prepare before the first link, as with a modded cache.psarc): a cold first
    // run on the throttled pool; then a second start (manifest known) with a warm system Metal
    // cache, and with a cold one (RS_MTL_SALT_COMPILE: as after an OS update).
    char s3[64], s4[80];
    snprintf(s3, sizeof s3, "b%d-%ld-safe", (int)getpid(), (long)time(NULL));
    snprintf(s4, sizeof s4, "RS_MTL_SALT_COMPILE=%s-os", s3);
    sh("rm -rf '%s' && mkdir -p '%s'", g_dir, g_dir);
    bench_one("cold safe (throttled)", n, s3, (const char *[]){"RSM_TEST_PREPARE=120", NULL});
    bench_one("warm safe, +disk", n, s3, (const char *[]){"RSM_TEST_PREPARE=120", NULL});
    bench_one("cold-sys safe, +disk", n, s3, (const char *[]){"RSM_TEST_PREPARE=120", s4, NULL});
    return 0;
}

// --- Corpus warm ---------------------------------------------------------------------------
enum { NEXTRA = 6, NCORPUS = NPROG + NEXTRA };  // distinct MSL in the test corpus

// The child's NPROG programs, NEXTRA more it never links, and one more pair with program 0's MSL
// (a duplicate text: compiled once).
static int write_corpus(const char *path, int nonce) {
    rsmsl_attrib bound[] = {{"pos", 0, 0}, {"col", 1, 0}};
    rsmsl_corpus_writer *w = rsmsl_corpus_begin();
    rsmsl_program *p0 = NULL;
    for (int i = 0; i < NCORPUS; i++) {
        char fs[512];
        child_fs(fs, sizeof fs, i, nonce);
        char *err = NULL;
        rsmsl_program *p = rsmsl_translate_direct(kChildVS, fs, bound, 2, &err);
        if (!p) {
            printf("corpus: translation %d failed: %s\n", i, err ? err : "?");
            return -1;
        }
        rsmsl_corpus_add(w, kChildVS, fs, p, RSMSL_CORPUS_VERIFIED);
        if (i)
            rsmsl_free(p);
        else
            p0 = p;
    }
    char fs[512];
    child_fs(fs, sizeof fs, 0, nonce);
    char vs2[512];
    snprintf(vs2, sizeof vs2, "%s// another pair, the same MSL\n", kChildVS);
    rsmsl_corpus_add(w, vs2, fs, p0, 0);
    rsmsl_free(p0);
    return rsmsl_corpus_finish(w, path);
}

static int progress_ok(const char *log, int total) {
    const char *p = strstr(log, "PROGRESS ");
    return p && num(p, "samples=") > 0 && num(p, "bad=") == 0 && num(p, "after_rc=") == 0 &&
           num(p, "after_done=") == total && num(p, "after_total=") == total;
}

static void test_corpus(void) {
    char saved[1024], corpus[1100], corpus_var[1200], rec[1700];
    snprintf(saved, sizeof saved, "%s", g_dir);
    snprintf(corpus, sizeof corpus, "%s-corpus.bin", saved);
    snprintf(corpus_var, sizeof corpus_var, "RS_MSL_CORPUS=%s", corpus);
    check(write_corpus(corpus, g_nonce) == 0, "test corpus written");
    snprintf(g_dir, sizeof g_dir, "%s-corpus", saved);
    sh("rm -rf '%s' && mkdir -p '%s'", g_dir, g_dir);
    char salt[96], salt2[96], salt3[96], salt4[96];
    snprintf(salt, sizeof salt, "RS_MTL_SALT_COMPILE=cw%d-%d", g_nonce, (int)getpid());
    snprintf(salt2, sizeof salt2, "RS_MTL_SALT_COMPILE=cx%d-%d", g_nonce, (int)getpid());
    snprintf(salt3, sizeof salt3, "RS_MTL_SALT_COMPILE=cy%d-%d", g_nonce, (int)getpid());
    snprintf(salt4, sizeof salt4, "RS_MTL_SALT_COMPILE=cz%d-%d", g_nonce, (int)getpid());
    const char *minm = "RS_METAL_CORPUS_MIN_MANIFEST=8";

    // Control: the same first run without the corpus warm (cold compiles while "loading").
    const char *ctl_env[] = {"RSM_TEST_PREPARE=60", corpus_var, minm, "RS_METAL_CORPUS_WARM=0", salt4, NULL};
    run_result ctl = run("corpus-off", ctl_env);
    check(ctl.rc == 0 && ctl.bad == 0 && num(ctl.log, "corpus_warmed=") == 0,
          "RS_METAL_CORPUS_WARM=0: no corpus warm (rc %d, %d)", ctl.rc, (int)num(ctl.log, "corpus_warmed="));
    sh("rm -rf '%s' && mkdir -p '%s'", g_dir, g_dir);

    // First run: empty manifest -> every distinct corpus library before the game links anything.
    const char *first_env[] = {"RSM_TEST_PREPARE=60", "RSM_TEST_PROGRESS=1", corpus_var, minm, salt, NULL};
    run_result first = run("corpus-first", first_env);
    const char *pl = strstr(first.log, "PREPARE ");
    check(first.rc == 0 && first.bad == 0 && pl && num(pl, "rc=") == 0, "corpus-first: draws, prepare rc %d",
          (int)num(pl, "rc="));
    check(num(first.log, "corpus_warmed=") == NCORPUS && num(first.log, "corpus_failed=") == 0,
          "corpus-first: %d corpus libraries compiled (want %d distinct of %d entries)",
          (int)num(first.log, "corpus_warmed="), NCORPUS, NCORPUS + 1);
    check(progress_ok(first.log, NCORPUS), "corpus-first: progress monotonic, ends at %d of %d: %.200s", NCORPUS,
          NCORPUS, strstr(first.log, "PROGRESS ") ? strstr(first.log, "PROGRESS ") : "(none)");
    // The game's own compiles of those libraries are system-cache hits now.
    printf("corpus: game's library compiles after the corpus warm %.1f ms (%d), without it %.1f ms (%d); "
           "corpus warm %.1f ms in prepare\n",
           first.lib_ms, first.lib_compiled, ctl.lib_ms, ctl.lib_compiled, num(pl, "ms="));
    check(first.lib_compiled >= NPROG && first.lib_ms < ctl.lib_ms * 0.5,
          "corpus-first: the game's compiles are cheap (%.1f ms vs %.1f ms cold)", first.lib_ms, ctl.lib_ms);
    char sub[1500];
    check(cache_subdir(sub, sizeof sub), "corpus cache directory");
    snprintf(rec, sizeof rec, "%s/corpus-warmed", sub);
    check(file_size(rec) == 17, "corpus-first: complete warm recorded (%lld bytes)", (long long)file_size(rec));
    check(!strstr(first.log, "keeping"), "corpus libraries the game did not link are not in the manifest");

    // Second start: recorded and the manifest is big enough -> the manifest only.
    const char *again_env[] = {"RSM_TEST_PREPARE=60", "RSM_TEST_PROGRESS=1", corpus_var, minm, salt, NULL};
    run_result again = run("corpus-again", again_env);
    check(again.rc == 0 && again.bad == 0 && num(again.log, "corpus_warmed=") == 0 && again.lib_warmed == NPROG &&
              strstr(again.log, "compiled here before"),
          "corpus-again: no corpus warm, the manifest's %d libraries warmed", again.lib_warmed);
    check(progress_ok(again.log, NPROG), "corpus-again: progress ends at %d: %.200s", NPROG,
          strstr(again.log, "PROGRESS ") ? strstr(again.log, "PROGRESS ") : "(none)");
    // Without the test's lower limit the 16-library manifest counts as small: warmed again, minus
    // what the manifest has (and cheap now).
    const char *small_env[] = {"RSM_TEST_PREPARE=60", corpus_var, salt, NULL};
    run_result small = run("corpus-small", small_env);
    check(small.rc == 0 && num(small.log, "corpus_warmed=") == NCORPUS - NPROG && strstr(small.log, "manifest is small"),
          "corpus-small: %d corpus libraries (want %d: the manifest's are skipped)",
          (int)num(small.log, "corpus_warmed="), NCORPUS - NPROG);
    // Forced, and another corpus identity (the compiled source changes: as with a new translator).
    const char *force_env[] = {"RSM_TEST_PREPARE=60", corpus_var, minm, salt, "RS_METAL_CORPUS_WARM=1", NULL};
    run_result forced = run("corpus-forced", force_env);
    check(forced.rc == 0 && num(forced.log, "corpus_warmed=") == NCORPUS - NPROG,
          "RS_METAL_CORPUS_WARM=1: warmed (%d)", (int)num(forced.log, "corpus_warmed="));
    const char *new_env[] = {"RSM_TEST_PREPARE=60", corpus_var, minm, salt2, NULL};
    run_result renewed = run("corpus-new", new_env);
    check(renewed.rc == 0 && renewed.bad == 0 && num(renewed.log, "corpus_warmed=") == NCORPUS - NPROG &&
              strstr(renewed.log, "another corpus"),
          "another corpus: warmed again (%d)", (int)num(renewed.log, "corpus_warmed="));
    // No prepare (the original cache.psarc): nothing of this.
    const char *noprep_env[] = {corpus_var, minm, salt3, NULL};
    run_result noprep = run("corpus-noprep", noprep_env);
    check(noprep.rc == 0 && noprep.bad == 0 && num(noprep.log, "corpus_warmed=") == 0,
          "no prepare: no corpus warm (%d)", (int)num(noprep.log, "corpus_warmed="));
    // Timeout: what was not started is skipped, no record, the game still draws.
    sh("rm -rf '%s' && mkdir -p '%s'", g_dir, g_dir);
    const char *to_env[] = {"RSM_TEST_PREPARE=0.001", "RSM_TEST_PROGRESS=1", "RS_MTL_COMPILE_THREADS=1", corpus_var,
                            minm, salt3, NULL};
    run_result to = run("corpus-timeout", to_env);
    pl = strstr(to.log, "PREPARE ");
    check(to.rc == 0 && to.bad == 0 && pl && num(pl, "rc=") == 1 && num(to.log, "corpus_warmed=") < NCORPUS,
          "corpus-timeout: rc %d, %d corpus libraries, draws", (int)num(pl, "rc="), (int)num(to.log, "corpus_warmed="));
    check(strstr(to.log, "corpus libraries are skipped") != NULL, "corpus-timeout: the rest skipped");
    check(cache_subdir(sub, sizeof sub), "corpus cache directory (timeout)");
    snprintf(rec, sizeof rec, "%s/corpus-warmed", sub);
    check(file_size(rec) < 0, "corpus-timeout: no record");
    snprintf(g_dir, sizeof g_dir, "%s", saved);
    unlink(corpus);
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (argc >= 3 && !strcmp(argv[1], "--child"))
        return child(atoi(argv[2]));
    if (argc >= 4 && !strcmp(argv[1], "--async"))
        return child_async(atoi(argv[2]), atoi(argv[3]));
    if (argc >= 4 && !strcmp(argv[1], "--bench-child"))
        return child_bench(atoi(argv[3]));
    if (argc >= 3 && !strcmp(argv[1], "--bench"))
        return bench(argv[0], argv[2], argc >= 4 ? atoi(argv[3]) : 256);
    if (argc < 2) {
        fprintf(stderr, "usage: %s <cache dir>\n", argv[0]);
        return 2;
    }
    if (!realpath(argv[0], g_self))
        snprintf(g_self, sizeof g_self, "%s", argv[0]);
    snprintf(g_dir, sizeof g_dir, "%s", argv[1]);
    g_nonce = (int)((getpid() * 2654435761u ^ (unsigned)time(NULL)) & 0x3fffffff);
    sh("rm -rf '%s' && mkdir -p '%s'", g_dir, g_dir);
    const int P = NPROG * NVAR;

    run_result cold = run("cold", NULL);
    check(cold.rc == 0 && cold.bad == 0, "cold run draws correctly (rc %d)", cold.rc);
    check(cold.misses == P && cold.hits == 0, "cold: %d misses, %d hits (want %d, 0)", cold.misses, cold.hits, P);
    check(cold.lib_compiled >= NPROG, "cold: %d libraries compiled", cold.lib_compiled);
    char sub[1500], path[1600];
    check(cache_subdir(sub, sizeof sub), "cache directory created under %s", g_dir);
    snprintf(path, sizeof path, "%s/archive-000000.bin", sub);
    check(file_size(path) > 0, "archive written (%lld bytes)", (long long)file_size(path));
    snprintf(path, sizeof path, "%s/manifest.bin", sub);
    off_t man = file_size(path);
    check(man > 0, "manifest written (%lld bytes)", (long long)man);

    const char *sync_env[] = {"RS_METAL_CACHE_WARM_SYNC=1", NULL};
    run_result warm = run("warm", sync_env);
    check(warm.rc == 0 && warm.bad == 0, "warm run draws correctly (rc %d)", warm.rc);
    check(warm.misses == 0 && warm.hits >= P, "warm: %d hits, %d misses (want >= %d, 0)", warm.hits, warm.misses, P);
    check(warm.lib_warmed == NPROG && warm.pipe_warmed == P, "warm: warmed %d libraries, %d pipelines (want %d, %d)",
          warm.lib_warmed, warm.pipe_warmed, NPROG, P);
    // Only the core's internal shaders are compiled on the drawing thread.
    check(warm.lib_compiled <= 1 && warm.pipe_built == 0, "warm: drawing thread compiled %d libraries, %d pipelines",
          warm.lib_compiled, warm.pipe_built);
    // Links only queue the compiles: compare link + first draws.
    check(warm.link_ms + warm.draw1_ms < cold.link_ms + cold.draw1_ms, "warm link + first draws %.1f ms < cold %.1f ms",
          warm.link_ms + warm.draw1_ms, cold.link_ms + cold.draw1_ms);

    run_result async = run("warm-async", NULL);
    check(async.rc == 0 && async.bad == 0, "warm-async run draws correctly (rc %d)", async.rc);
    check(async.misses == 0 && async.hits >= P, "warm-async: %d hits, %d misses", async.hits, async.misses);

    const char *off_env[] = {"RS_METAL_CACHE=0", NULL};
    run_result off = run("no-cache", off_env);
    check(off.rc == 0 && off.bad == 0, "no-cache run draws correctly (rc %d)", off.rc);
    check(off.hits == 0 && off.misses == 0 && off.lib_warmed == 0, "no-cache: no archive, no warming");
    // Safe load (rs_gfx_prepare before the first link). Warm: the manifest's libraries and
    // pipelines are ready when it returns, nothing compiles on the drawing thread, and the pool is
    // throttled afterwards.
    const char *prep_env[] = {"RSM_TEST_PREPARE=60", NULL};
    run_result prep = run("prepare", prep_env);
    const char *pl = strstr(prep.log, "PREPARE ");
    check(prep.rc == 0 && prep.bad == 0, "prepare run draws correctly (rc %d)", prep.rc);
    check(pl && num(pl, "rc=") == 0, "prepare: everything ready (rc %d)", (int)num(pl, "rc="));
    check(prep.lib_warmed == NPROG && prep.pipe_warmed == P && prep.lib_compiled <= 1 && prep.pipe_built == 0 &&
              prep.misses == 0,
          "prepare: warmed %d libraries + %d pipelines, drawing thread compiled %d + %d", prep.lib_warmed,
          prep.pipe_warmed, prep.lib_compiled, prep.pipe_built);
    check(num(prep.log, "pool_limit=") == 2, "prepare: pool throttled to 2 (%d)", (int)num(prep.log, "pool_limit="));
    printf("prepare (warm system Metal cache): %.1f ms, %.1f ms from context creation, %d libraries\n",
           num(pl, "ms="), num(pl, "total_ms="), prep.lib_warmed);
    // The manifest is known but the system Metal cache is cold (a new salt in the compiled source
    // only, like after an OS update): prepare compiles it all (warming started with the context).
    // With a 1 ms limit it times out and the rest compiles while the program draws.
    char csalt[96];
    snprintf(csalt, sizeof csalt, "RS_MTL_SALT_COMPILE=c%d-%d", g_nonce, (int)getpid());
    const char *prep_cold[] = {"RSM_TEST_PREPARE=60", csalt, NULL};
    run_result pc = run("prepare-cold", prep_cold);
    pl = strstr(pc.log, "PREPARE ");
    check(pc.rc == 0 && pc.bad == 0 && pl && num(pl, "rc=") == 0 && pc.lib_warmed == NPROG && pc.lib_compiled <= 1,
          "prepare-cold: rc %d, warmed %d, drawing thread compiled %d", (int)num(pl, "rc="), pc.lib_warmed,
          pc.lib_compiled);
    printf("prepare (cold system Metal cache, manifest known): %.1f ms, %.1f ms from context creation, "
           "%d libraries\n", num(pl, "ms="), num(pl, "total_ms="), pc.lib_warmed);
    snprintf(csalt, sizeof csalt, "RS_MTL_SALT_COMPILE=t%d-%d", g_nonce, (int)getpid());
    // One worker, so warming outlasts the context's own setup.
    const char *prep_to[] = {"RSM_TEST_PREPARE=0.001", "RS_MTL_COMPILE_THREADS=1", csalt, NULL};
    run_result pt = run("prepare-timeout", prep_to);
    pl = strstr(pt.log, "PREPARE ");
    check(pt.rc == 0 && pt.bad == 0, "prepare-timeout run draws correctly (rc %d)", pt.rc);
    check(pl && num(pl, "rc=") == 1 && num(pl, "ms=") < 500, "prepare-timeout: rc %d after %.1f ms",
          (int)num(pl, "rc="), num(pl, "ms="));
    // Throttle: a cold first run (new MSL, no disk cache) after prepare uses at most 2 workers, at
    // utility QoS; RS_MTL_COMPILE_THREADS overrides the size.
    char tsalt[96];
    snprintf(tsalt, sizeof tsalt, "RS_MTL_SALT=th%d-%d", g_nonce, (int)getpid());
    const char *thr_env[] = {"RSM_TEST_PREPARE=60", "RS_METAL_CACHE=0", tsalt, NULL};
    run_result th = run("throttle", thr_env);
    check(th.rc == 0 && th.bad == 0, "throttle run draws correctly (rc %d)", th.rc);
    check(num(th.log, "pool_limit=") == 2 && num(th.log, "pool_threads=") <= 2 && num(th.log, "pool_peak=") >= 1 &&
              num(th.log, "pool_peak=") <= 2,
          "throttle: limit %d, %d threads, peak %d", (int)num(th.log, "pool_limit="), (int)num(th.log, "pool_threads="),
          (int)num(th.log, "pool_peak="));
    check(strstr(th.log, "pool_qos=utility") != NULL, "throttle: link compiles at utility QoS");
    check(th.lib_compiled >= NPROG, "throttle: %d libraries compiled (%d on the pool, %d by the drawing thread)",
          th.lib_compiled, (int)num(th.log, "lib_async="), (int)num(th.log, "lib_stolen="));
    snprintf(tsalt, sizeof tsalt, "RS_MTL_SALT=to%d-%d", g_nonce, (int)getpid());
    const char *thr3_env[] = {"RSM_TEST_PREPARE=60", "RS_METAL_CACHE=0", "RS_MTL_COMPILE_THREADS=3", tsalt, NULL};
    run_result th3 = run("throttle-3", thr3_env);
    check(th3.rc == 0 && th3.bad == 0 && num(th3.log, "pool_limit=") == 3 && num(th3.log, "pool_peak=") <= 3,
          "RS_MTL_COMPILE_THREADS=3 overrides the throttled size (limit %d, peak %d)", (int)num(th3.log, "pool_limit="),
          (int)num(th3.log, "pool_peak="));
    // Without prepare (the original cache.psarc) the pool is not throttled.
    check(num(async.log, "pool_limit=") >= 2 && strstr(async.log, "prepare_ms=0.0"),
          "no prepare: pool not throttled (limit %d)", (int)num(async.log, "pool_limit="));

    printf("pipeline compile on the drawing thread: cold %.1f ms, no-cache (system Metal cache only) %.1f ms, "
           "warm 0 builds (archive lookups in warming: %.1f ms for %d libraries + %d pipelines)\n",
           cold.pipe_ms, off.pipe_ms, warm.warm_ms, warm.lib_warmed, warm.pipe_warmed);

    // Corrupt: garbage archive, torn manifest (cut inside a record).
    // Compaction: more than 8 archive files (copies of the one there) become one after warming.
    for (int i = 1; i <= 9; i++)
        sh("cp '%s/archive-000000.bin' '%s/archive-%06d.bin'", sub, sub, 100 + i);
    run_result comp = run("compact", sync_env);
    char cmd[2048];
    snprintf(cmd, sizeof cmd, "test $(ls '%s' | grep -c '^archive-.*\\.bin$') -eq 1", sub);
    check(comp.rc == 0 && comp.bad == 0 && comp.misses == 0 && comp.hits >= P,
          "compact: runs from the copies (%d hits, %d misses)", comp.hits, comp.misses);
    check(num(comp.log, "compacted=") == 1 && sh("%s", cmd) == 0, "compact: 10 archive files became one");
    run_result after = run("compacted", sync_env);
    check(after.rc == 0 && after.misses == 0 && after.hits >= P, "after compaction: %d hits, %d misses", after.hits,
          after.misses);

    // Corrupt: garbage archives, torn manifest (cut inside a record).
    sh("for f in '%s'/archive-*.bin; do head -c 4096 /dev/urandom > \"$f\"; done", sub);
    snprintf(path, sizeof path, "%s/manifest.bin", sub);
    truncate(path, man * 2 / 3);
    run_result bad1 = run("corrupt", sync_env);
    check(bad1.rc == 0 && bad1.bad == 0, "corrupt archive + torn manifest: runs and draws (rc %d)", bad1.rc);
    check(bad1.discarded >= 1, "corrupt: %d files discarded", bad1.discarded);
    check(bad1.lib_warmed > 0 && bad1.lib_warmed < NPROG, "corrupt: the manifest's good part warmed (%d libraries)",
          bad1.lib_warmed);
    check(bad1.misses == P && bad1.hits == 0, "corrupt: the new archive misses everything (%d misses, %d hits)",
          bad1.misses, bad1.hits);
    // Garbage manifest from the first byte.
    sh("head -c 3000 /dev/urandom > '%s'", path);
    run_result bad2 = run("corrupt2", sync_env);
    check(bad2.rc == 0 && bad2.bad == 0, "garbage manifest: runs and draws (rc %d)", bad2.rc);
    check(bad2.discarded >= 1 && bad2.lib_warmed == 0, "garbage manifest discarded (%d), nothing warmed (%d)",
          bad2.discarded, bad2.lib_warmed);
    run_result again = run("rebuilt", sync_env);
    check(again.rc == 0 && again.misses == 0 && again.pipe_warmed == P,
          "after the garbage manifest the cache is whole again (%d warmed, %d misses)", again.pipe_warmed,
          again.misses);

    // Temp files of interrupted archive writes go, unless their process still runs (another
    // instance writing right now).
    sh("echo x > '%s/archive-new.tmp.%d' && echo x > '%s/archive-new.tmp.99999999'", sub, (int)getpid(), sub);
    run_result tmps = run("tmp-files", NULL);
    check(tmps.rc == 0 && tmps.bad == 0, "tmp-files run (rc %d)", tmps.rc);
    snprintf(path, sizeof path, "%s/archive-new.tmp.%d", sub, (int)getpid());
    check(file_size(path) > 0, "a running process's temp archive is left alone");
    unlink(path);
    snprintf(path, sizeof path, "%s/archive-new.tmp.99999999", sub);
    check(file_size(path) < 0, "a dead process's temp archive is removed");

    // Stale directories: ours from another build go, other files stay.
    sh("mkdir -p '%s/rsm1-plantedold-0-gpu' '%s/rsm1-plantedother-0-gpu' && "
       "echo x > '%s/rsm1-plantedold-0-gpu/archive-000000.bin' && echo x > '%s/rsm1-plantedold-0-gpu/manifest.bin' && "
       "echo keep > '%s/rsm1-plantedother-0-gpu/notes.txt' && echo keep > '%s/keep.txt'",
       g_dir, g_dir, g_dir, g_dir, g_dir, g_dir);
    run_result stale = run("stale", NULL);
    check(stale.rc == 0, "stale run (rc %d)", stale.rc);
    snprintf(path, sizeof path, "%s/rsm1-plantedold-0-gpu", g_dir);
    check(file_size(path) < 0, "other build's cache directory removed");
    snprintf(path, sizeof path, "%s/rsm1-plantedother-0-gpu/notes.txt", g_dir);
    check(file_size(path) > 0, "unknown files in a stale directory left alone");
    snprintf(path, sizeof path, "%s/keep.txt", g_dir);
    check(file_size(path) > 0, "unrelated files in the cache directory left alone");

    // The cache directory's name has no build identity (rebuilding the app keeps the cache).
    const char *leaf = strrchr(sub, '/');
    check(leaf && !strncmp(leaf, "/rsm3-k", 7), "cache directory %s is rsm<format>-k<conventions>-<os>-<gpu>",
          leaf ? leaf + 1 : sub);

    // Asynchronous compiles: GL semantics with the pool and without (RS_MTL_COMPILE_THREADS=0).
    char salt[64], saltvar[96];
    snprintf(salt, sizeof salt, "%d-%d", g_nonce, (int)getpid());
    snprintf(saltvar, sizeof saltvar, "RS_MTL_SALT=%s", salt);
    g_mode = "--async";
    g_arg2 = "0";
    const char *async_env[] = {saltvar, "RS_MTL_TEST_FAIL=rs_fail_me", NULL};
    run_result as = run("async", async_env);
    check(as.rc == 0, "async semantics with the compile pool (rc %d)", as.rc);
    check(as.lib_compiled >= 24 && num(as.log, "lib_async=") >= 12 && as.lib_reused >= 1,
          "async: %d compiled, %d on the pool, %d shared", as.lib_compiled, (int)num(as.log, "lib_async="),
          as.lib_reused);
    snprintf(saltvar, sizeof saltvar, "RS_MTL_SALT=%s-sync", salt);
    g_arg2 = "1";
    const char *sync_env2[] = {saltvar, "RS_MTL_TEST_FAIL=rs_fail_me", "RS_MTL_COMPILE_THREADS=0", NULL};
    run_result sy = run("async-off", sync_env2);
    check(sy.rc == 0, "the same semantics with synchronous compiles (rc %d)", sy.rc);
    check(num(sy.log, "lib_async=") == 0, "RS_MTL_COMPILE_THREADS=0: nothing on the pool");
    g_mode = "--child";
    g_arg2 = NULL;

    // Pruning: two runs link other MSL (another nonce); at the third start the first MSL's
    // libraries (unlinked for 2 runs, RS_METAL_CACHE_KEEP_RUNS=2) leave the manifest.
    int nonce1 = g_nonce;
    g_nonce = nonce1 ^ 0x5a5a5;
    const char *keep_env[] = {"RS_METAL_CACHE_KEEP_RUNS=2", NULL};
    const char *keep_sync[] = {"RS_METAL_CACHE_KEEP_RUNS=2", "RS_METAL_CACHE_WARM_SYNC=1", NULL};
    run_result b1 = run("other-msl1", keep_env);
    run_result b2 = run("other-msl2", keep_sync);
    run_result b3 = run("pruned", keep_sync);
    check(b1.rc == 0 && b2.rc == 0 && b3.rc == 0, "other-MSL runs (rc %d %d %d)", b1.rc, b2.rc, b3.rc);
    check(b2.lib_warmed >= 2 * NPROG - 2, "before pruning both sets are warmed (%d)", b2.lib_warmed);
    check(b3.lib_warmed == NPROG && b3.misses == 0 && strstr(b3.log, "keeping 16 of"),
          "pruned: only the current MSL is warmed (%d libraries, %d misses)", b3.lib_warmed, b3.misses);
    g_nonce = nonce1;
    run_result back = run("old-msl", sync_env);
    check(back.rc == 0 && back.bad == 0, "the pruned MSL still works (compiled again, rc %d)", back.rc);

    test_corpus();

    printf("%d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
