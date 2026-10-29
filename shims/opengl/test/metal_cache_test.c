// Metal shader/pipeline cache test (src/metal/mtl_cache.m), headless.
//
//   metal_cache_test <dir>                 runs the scenario below, children share <dir>
//   metal_cache_test --child <nonce>       one run: link N programs, draw each with V pipeline
//                                          variants (checking a pixel), print timings
//
// Scenario (fresh MSL per invocation via <nonce>, so the system's Metal cache starts cold too):
//   cold        empty cache: every pipeline is an archive miss
//   warm        RS_METAL_CACHE_WARM_SYNC=1: warming built every library and pipeline from the
//               manifest, all archive hits, nothing compiled on the drawing thread
//   warm-async  as the game sees it: warming races the first draws; no misses
//   no-cache    RS_METAL_CACHE=0, same MSL: what the system Metal cache alone gives
//   corrupt     garbage archive and a torn manifest; then a garbage manifest: discarded, no crash
//   stale       directories of other builds are removed, foreign files left alone
// Output ends with "N passed, M failed".
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>

#include "rs_gfx.h"

#include <fcntl.h>
#include <mach/mach_time.h>
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
static GLuint shader(GLenum type, const char *src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    return s;
}

static int child(int nonce) {
    rs_gfx_context *c = rs_gfx_create_offscreen(W, H);
    if (!c) {
        printf("no context\n");
        return 1;
    }
    rs_gfx_make_current(c);
    static const char *vs =
        "attribute vec2 pos;\n"
        "attribute vec4 col;\n"
        "varying vec4 v_col;\n"
        "void main() { v_col = col; gl_Position = vec4(pos, 0.0, 1.0); }\n";
    GLuint prog[NPROG];
    double t0 = now_ms();
    for (int i = 0; i < NPROG; i++) {
        char fs[512];
        snprintf(fs, sizeof fs,
                 "varying vec4 v_col;\n"
                 "uniform vec4 tint;\n"
                 "void main() {\n"
                 "    vec4 c = v_col * tint;\n"
                 "    c.r = %d.0 / 255.0;\n"
                 "    c.b = float(%d) * 1.0e-12;\n"  // per-run code: the system's Metal cache starts cold
                 "    gl_FragColor = c;\n"
                 "}\n",
                 i * 8, nonce);
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

static run_result run(const char *name, const char *extra_env[]) {
    run_result r;
    memset(&r, 0, sizeof r);
    char logpath[1200];
    snprintf(logpath, sizeof logpath, "%s.%s.log", g_dir, name);
    char nonce[32];
    snprintf(nonce, sizeof nonce, "%d", g_nonce);
    char *argv[] = {g_self, "--child", nonce, NULL};
    // Environment: ours minus the cache knobs, then the run's.
    char *env[512];
    int n = 0;
    for (char **e = environ; *e && n < 480; e++)
        if (strncmp(*e, "RS_METAL_CACHE", 14) && strncmp(*e, "RS_GL_BACKEND=", 14))
            env[n++] = *e;
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

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (argc >= 3 && !strcmp(argv[1], "--child"))
        return child(atoi(argv[2]));
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
    check(warm.link_ms < cold.link_ms, "warm link %.1f ms < cold %.1f ms", warm.link_ms, cold.link_ms);
    check(warm.draw1_ms < cold.draw1_ms, "warm first draws %.1f ms < cold %.1f ms", warm.draw1_ms, cold.draw1_ms);

    run_result async = run("warm-async", NULL);
    check(async.rc == 0 && async.bad == 0, "warm-async run draws correctly (rc %d)", async.rc);
    check(async.misses == 0 && async.hits >= P, "warm-async: %d hits, %d misses", async.hits, async.misses);

    const char *off_env[] = {"RS_METAL_CACHE=0", NULL};
    run_result off = run("no-cache", off_env);
    check(off.rc == 0 && off.bad == 0, "no-cache run draws correctly (rc %d)", off.rc);
    check(off.hits == 0 && off.misses == 0 && off.lib_warmed == 0, "no-cache: no archive, no warming");
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

    printf("%d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
