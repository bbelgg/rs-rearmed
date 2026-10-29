// The app's warm-only mode (rsp_mtl_warm_only, --warm-shaders: the installer's shader warm-up) on a
// test corpus, headless: no window. Each run is a child process (as the installer runs the app), with
// its own disk cache directory and a per-test salt (RS_MTL_SALT_COMPILE), so the system's Metal cache
// starts cold:
//   first    empty cache: exit 0, monotonic "progress F Preparing shaders… N of M" lines ending at
//            F = 1 with every distinct corpus library, the summary, the corpus-warmed record written;
//   again    the record is there and (RS_METAL_CORPUS_MIN_MANIFEST=0) the manifest counts as big
//            enough: the corpus is skipped, "nothing to compile", exit 0, quick;
//   forced   RS_METAL_CORPUS_WARM=1 compiles it again (system cache hits: quick);
//   timeout  RS_METAL_WARM_MAX_SECONDS-like limit (here max_seconds 0.001): exit 1, still F = 1 last;
//   gl       not the Metal backend: exit 2, no progress.
#import <Foundation/Foundation.h>
#include <dirent.h>
#include <mach/mach_time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../src/rsp_present.h"
#include "msl_internal.h"  // the corpus writer (rsmsl, linked into this test)

static int g_pass, g_fail;
#define CHECK(cond, ...)                          \
  do {                                            \
    if (cond) g_pass++;                           \
    else {                                        \
      g_fail++;                                   \
      printf("FAIL %s:%d: ", __FILE__, __LINE__); \
      printf(__VA_ARGS__);                        \
      printf("\n");                               \
    }                                             \
  } while (0)

enum { NPROG = 48 };  // distinct MSL texts in the test corpus (plus one duplicate entry)

static const char *kVS =
    "attribute vec2 pos;\n"
    "attribute vec4 col;\n"
    "varying vec4 v_col;\n"
    "void main() { v_col = col; gl_Position = vec4(pos, 0.0, 1.0); }\n";

static void fs_text(char *fs, size_t n, int i, int nonce) {
  snprintf(fs, n,
           "varying vec4 v_col;\n"
           "uniform vec4 tint;\n"
           "void main() {\n"
           "    vec4 c = v_col * tint;\n"
           "    c.r = %d.0 / 255.0;\n"
           "    c.b = float(%d) * 1.0e-12;\n"
           "    gl_FragColor = c;\n"
           "}\n",
           i * 8, nonce);
}

static int write_corpus(const char *path, int nonce) {
  rsmsl_attrib bound[] = {{"pos", 0, 0}, {"col", 1, 0}};
  rsmsl_corpus_writer *w = rsmsl_corpus_begin();
  rsmsl_program *p0 = NULL;
  for (int i = 0; i < NPROG; i++) {
    char fs[512];
    fs_text(fs, sizeof fs, i, nonce);
    char *err = NULL;
    rsmsl_program *p = rsmsl_translate_direct(kVS, fs, bound, 2, &err);
    if (!p) {
      printf("translation %d failed: %s\n", i, err ? err : "?");
      return -1;
    }
    rsmsl_corpus_add(w, kVS, fs, p, 0);
    if (i) rsmsl_free(p);
    else p0 = p;
  }
  char fs[512];
  fs_text(fs, sizeof fs, 0, nonce);
  char vs2[512];
  snprintf(vs2, sizeof vs2, "%s// the same MSL again\n", kVS);
  rsmsl_corpus_add(w, vs2, fs, p0, 0);
  rsmsl_free(p0);
  return rsmsl_corpus_finish(w, path);
}

static double now_s(void) {
  static mach_timebase_info_data_t tb;
  if (!tb.denom) mach_timebase_info(&tb);
  return (double)mach_absolute_time() * tb.numer / tb.denom / 1e9;
}

typedef struct {
  int rc, lines, bad, backwards, last_done, last_total, ready, nothing;
  double last_f, secs;
  char log[1 << 16];
} result;

static char g_self[1024], g_dir[1024], g_corpus[1100];

// Runs this test as a warm-only child: `--child <max seconds>` with the given environment.
static result run(const char *name, const char *env, const char *max_s) {
  result r;
  memset(&r, 0, sizeof r);
  r.last_f = -1;
  r.last_done = r.last_total = -1;
  char cmd[6144];
  // The shell drops DYLD_* (where ctest finds libRSOpenGL): handed on explicitly.
  const char *dyld = getenv("DYLD_LIBRARY_PATH");
  snprintf(cmd, sizeof cmd,
           "env DYLD_LIBRARY_PATH='%s' RS_MSL_CORPUS='%s' RS_METAL_CACHE_DIR='%s' RS_METAL_CACHE_STATS=1 %s '%s' "
           "--child %s 2>&1",
           dyld ? dyld : "", g_corpus, g_dir, env, g_self, max_s);
  double t0 = now_s();
  FILE *p = popen(cmd, "r");
  if (!p) {
    r.rc = -1;
    return r;
  }
  char line[1024];
  size_t used = 0;
  double prev = -1;
  int prev_done = -1;
  while (fgets(line, sizeof line, p)) {
    size_t n = strlen(line);
    if (used + n < sizeof r.log) {
      memcpy(r.log + used, line, n + 1);
      used += n;
    }
    double f;
    int done, total;
    if (!strncmp(line, "progress ", 9)) {
      r.lines++;
      char what[64] = "";
      if (sscanf(line, "progress %lf Preparing shaders… %d of %d", &f, &done, &total) == 3) {
        if (f < prev || done < prev_done || done > total) r.backwards++;
        prev = f;
        prev_done = done;
        r.last_done = done;
        r.last_total = total;
      } else if (sscanf(line, "progress %lf Preparing shaders… %63[a-z ]", &f, what) == 2 &&
                 strstr(what, "nothing to compile")) {
        r.nothing = 1;
        if (f < prev) r.backwards++;
        prev = f;
      } else {
        r.bad++;
      }
      r.last_f = prev;
    }
    if (strstr(line, "shader libraries ready")) r.ready = 1;
  }
  int st = pclose(p);
  r.rc = WIFEXITED(st) ? WEXITSTATUS(st) : -1;
  r.secs = now_s() - t0;
  printf("%-8s rc %d, %d progress lines, last %.3f (%d of %d), %.2f s\n", name, r.rc, r.lines, r.last_f, r.last_done,
         r.last_total, r.secs);
  return r;
}

static long num(const char *log, const char *key) {
  const char *p = strstr(log, key);
  return p ? strtol(p + strlen(key), NULL, 10) : -1;
}

// The cache's one subdirectory (rsm<version>-k<conventions>-<os>-<gpu>) and its corpus-warmed record.
static long long record_size(void) {
  DIR *d = opendir(g_dir);
  if (!d) return -1;
  long long sz = -1;
  for (struct dirent *e; (e = readdir(d));) {
    if (strncmp(e->d_name, "rsm", 3)) continue;
    char path[2200];
    snprintf(path, sizeof path, "%s/%s/corpus-warmed", g_dir, e->d_name);
    struct stat sb;
    if (!stat(path, &sb)) sz = sb.st_size;
  }
  closedir(d);
  return sz;
}

int main(int argc, char **argv) {
  setvbuf(stdout, NULL, _IOLBF, 0);
  if (argc >= 3 && !strcmp(argv[1], "--child")) return rsp_mtl_warm_only(atof(argv[2]), 0.02, stdout);
  if (argc < 2) {
    fprintf(stderr, "usage: %s <scratch dir>\n", argv[0]);
    return 2;
  }
  if (!realpath(argv[0], g_self)) snprintf(g_self, sizeof g_self, "%s", argv[0]);
  int nonce = (int)((getpid() * 2654435761u ^ (unsigned)time(NULL)) & 0x3fffffff);
  snprintf(g_dir, sizeof g_dir, "%s/cache", argv[1]);
  snprintf(g_corpus, sizeof g_corpus, "%s/corpus.bin", argv[1]);
  char sh[2400];
  snprintf(sh, sizeof sh, "rm -rf '%s' && mkdir -p '%s'", argv[1], argv[1]);
  if (system(sh)) return 2;
  CHECK(write_corpus(g_corpus, nonce) == 0, "test corpus written");
  char salt[160];
  snprintf(salt, sizeof salt, "RS_GL_BACKEND=metal RS_MTL_SALT_COMPILE=warm%d", nonce);

  // The line format.
  char line[160];
  double f = rsp_mtl_warm_line(line, sizeof line, 3, 12, 0);
  CHECK(f == 0.25 && !strcmp(line, "progress 0.250 Preparing shaders… 3 of 12"), "line: %s", line);
  f = rsp_mtl_warm_line(line, sizeof line, 3, 24, 0.25);  // the total grew: F stays
  CHECK(f == 0.25 && !strcmp(line, "progress 0.250 Preparing shaders… 3 of 24"), "line after growth: %s", line);
  f = rsp_mtl_warm_line(line, sizeof line, 0, 0, 0);
  CHECK(f == 1.0 && strstr(line, "nothing to compile"), "line with nothing: %s", line);

  char first_env[300];  // two compile threads: long enough for several progress lines
  snprintf(first_env, sizeof first_env, "%s RS_MTL_COMPILE_THREADS=2", salt);
  result first = run("first", first_env, "60");
  CHECK(first.rc == 0 && first.ready, "first: exit 0 and ready (rc %d): %.400s", first.rc, first.log);
  CHECK(first.lines >= 2 && first.bad == 0 && first.backwards == 0, "first: %d lines, %d bad, %d backwards",
        first.lines, first.bad, first.backwards);
  CHECK(first.last_f == 1.0 && first.last_done == NPROG && first.last_total == NPROG,
        "first: ends at 1.0 with %d of %d (want %d)", first.last_done, first.last_total, NPROG);
  CHECK(num(first.log, "corpus_warmed=") == NPROG, "first: %ld corpus libraries compiled",
        num(first.log, "corpus_warmed="));
  CHECK(record_size() == 17, "first: corpus-warmed record written (%lld bytes)", record_size());

  // The record makes the next prepare skip the corpus (the manifest counts as big enough here: the
  // corpus libraries are not in it, and a real install's manifest fills up as the game runs).
  char again_env[300];
  snprintf(again_env, sizeof again_env, "%s RS_METAL_CORPUS_MIN_MANIFEST=0", salt);
  result again = run("again", again_env, "60");
  CHECK(again.rc == 0 && again.ready && again.nothing && again.last_f == 1.0 && again.backwards == 0,
        "again: nothing to compile, exit 0 (rc %d): %.400s", again.rc, again.log);
  CHECK(num(again.log, "corpus_warmed=") == 0 && strstr(again.log, "compiled here before"),
        "again: corpus skipped (%ld)", num(again.log, "corpus_warmed="));
  // Without that: a small manifest warms the corpus again, all system-cache hits now.
  result small = run("small", salt, "60");
  CHECK(small.rc == 0 && small.last_f == 1.0 && num(small.log, "corpus_warmed=") == NPROG &&
            strstr(small.log, "manifest is small"),
        "small manifest: corpus compiled again (%ld)", num(small.log, "corpus_warmed="));
  CHECK(small.secs < first.secs, "small manifest: quicker than the cold first run (%.2f s vs %.2f s)", small.secs,
        first.secs);

  // Timeout: exit 1, the bar still ends at 1, no record for this corpus (a new salt: another identity).
  snprintf(sh, sizeof sh, "rm -rf '%s'", g_dir);
  if (system(sh)) return 2;
  char to_env[300];
  snprintf(to_env, sizeof to_env, "RS_GL_BACKEND=metal RS_MTL_SALT_COMPILE=warmto%d RS_MTL_COMPILE_THREADS=1", nonce);
  result to = run("timeout", to_env, "0.001");
  CHECK(to.rc == 1 && to.last_f == 1.0 && !to.ready && strstr(to.log, "timed out"), "timeout: exit 1 (rc %d): %.400s",
        to.rc, to.log);
  CHECK(record_size() < 0, "timeout: no record (%lld)", record_size());

  // Not the Metal backend: nothing happens.
  result gl = run("gl", "RS_GL_BACKEND=gl", "60");
  CHECK(gl.rc == 2 && gl.lines == 0, "gl: exit 2 without progress (rc %d, %d lines)", gl.rc, gl.lines);

  printf("%d passed, %d failed\n", g_pass, g_fail);
  return g_fail ? 1 : 0;
}
