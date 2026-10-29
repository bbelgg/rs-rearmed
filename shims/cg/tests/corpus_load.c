// Load every program file through the shim on a real GL context and bind it,
// so each translation is compiled and linked by the driver.
//   corpus_load <program files...>     (RS_CG_BACKEND selects glsl/arb)
//   corpus_load --dump-glsl <dir> <program files...>
//       glsl backend; every generated shader goes to <dir> (RS_CG_DUMP_GLSL):
//       <key>.<varyings>.vert, <key>.frag and passthrough.* for the other stage,
//       with index.tsv listing file name, stage and key of every program.
// Exits 1 if any program that NVIDIA's runtime can load fails in the shim.
// Files that are not ARB objects (Cg source shipped under a .fpo name) are
// expected to fail, as they do with NVIDIA's runtime.
#define GL_SILENCE_DEPRECATION
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../src/cg_api.h"
#include "../src/cg_key.h"

// mkdir -p, then remove the shaders of an earlier run.
static int prepare_dir(const char *dir) {
    char path[4096];
    snprintf(path, sizeof path, "%s", dir);
    for (char *p = path + 1; *p; p++)
        if (*p == '/') { *p = 0; mkdir(path, 0755); *p = '/'; }
    mkdir(path, 0755);
    DIR *d = opendir(dir);
    if (!d) return 0;
    for (struct dirent *e; (e = readdir(d));) {
        size_t n = strlen(e->d_name);
        if ((n > 5 && (!strcmp(e->d_name + n - 5, ".vert") || !strcmp(e->d_name + n - 5, ".frag"))) ||
            !strcmp(e->d_name, "index.tsv")) {
            snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
            unlink(path);
        }
    }
    closedir(d);
    return 1;
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

int main(int argc, char **argv) {
    FILE *index = NULL;
    if (argc >= 3 && !strcmp(argv[1], "--dump-glsl")) {
        if (!prepare_dir(argv[2])) {
            fprintf(stderr, "can't use %s\n", argv[2]);
            return 1;
        }
        // Read by the shim at its first call.
        setenv("RS_CG_BACKEND", "glsl", 1);
        setenv("RS_CG_DUMP_GLSL", argv[2], 1);
        char path[4096];
        snprintf(path, sizeof path, "%s/index.tsv", argv[2]);
        index = fopen(path, "w");
        if (index) fprintf(index, "# file\tstage\tkey (shaders: <key>.<varying bits, hex>.vert, <key>.frag)\n");
        argv += 2;
        argc -= 2;
    }
    CGLPixelFormatAttribute attrs[] = {kCGLPFAAccelerated, 0};
    CGLPixelFormatObj pf;
    GLint npf;
    CGLContextObj glctx;
    if (CGLChoosePixelFormat(attrs, &pf, &npf) || CGLCreateContext(pf, NULL, &glctx) ||
        CGLSetCurrentContext(glctx)) {
        fprintf(stderr, "no GL context\n");
        return 1;
    }
    CGcontext ctx = cgCreateContext();
    int loaded = 0, expected_fail = 0, failed = 0;
    for (int i = 1; i < argc; i++) {
        char *src = slurp(argv[i]);
        if (!src) continue;
        int is_object = strncmp(src, "!!ARB", 5) == 0;
        CGprofile prof = strstr(src, "!!ARBfp") || strstr(argv[i], ".fpo") ? CG_PROFILE_ARBFP1
                                                                            : CG_PROFILE_ARBVP1;
        cgGetError();
        while (glGetError() != GL_NO_ERROR) {}
        CGprogram p = cgCreateProgram(ctx, CG_OBJECT, src, prof, "main", NULL);
        cgGLLoadProgram(p);
        int err = cgGetError();
        if (!err) {
            cgGLEnableProfile(prof);
            cgGLBindProgram(p);
            err = cgGetError();
        }
        GLenum glerr = glGetError();
        if (!err && glerr == GL_NO_ERROR) {
            loaded++;
            if (index) {
                const char *base = strrchr(argv[i], '/');
                fprintf(index, "%s\t%s\t%016llx\n", base ? base + 1 : argv[i],
                        prof == CG_PROFILE_ARBFP1 ? "fragment" : "vertex",
                        (unsigned long long)cg_program_key(src, prof, "main"));
            }
        } else if (!is_object) {
            expected_fail++;
        } else {
            failed++;
            printf("FAIL %s: %s (gl 0x%x)\n%s\n", argv[i], cgGetErrorString(err), glerr,
                   cgGetLastListing(ctx));
        }
        cgDestroyProgram(p);
        free(src);
    }
    if (index) fclose(index);
    printf("%d loaded, %d non-objects rejected as expected, %d failed\n", loaded, expected_fail, failed);
    return failed ? 1 : 0;
}
