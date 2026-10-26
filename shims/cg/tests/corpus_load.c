// Load every program file through the shim on a real GL context and bind it,
// so each translation is compiled and linked by the driver.
//   corpus_load <program files...>     (RS_CG_BACKEND selects glsl/arb)
// Exits 1 if any program that NVIDIA's runtime can load fails in the shim.
// Files that are not ARB objects (Cg source shipped under a .fpo name) are
// expected to fail, as they do with NVIDIA's runtime.
#define GL_SILENCE_DEPRECATION
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/cg_api.h"

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
    printf("%d loaded, %d non-objects rejected as expected, %d failed\n", loaded, expected_fail, failed);
    return failed ? 1 : 0;
}
