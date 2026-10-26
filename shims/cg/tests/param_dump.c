// Dump the shim's parameter table for each program file in the same JSONL
// format as tools/cg_reference.c, so tests/compare_params.py can diff the two.
// Runs without a GL context: cgCreateProgram and the parameter queries never
// touch GL.
//   param_dump <out.jsonl> <program files...>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/cg_api.h"

static void js(FILE *f, const char *s) {
    if (!s) { fputs("null", f); return; }
    fputc('"', f);
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') fprintf(f, "\\%c", c);
        else if (c < 0x20) fprintf(f, "\\u%04x", c);
        else fputc(c, f);
    }
    fputc('"', f);
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
    if (argc < 3) { fprintf(stderr, "usage: %s out.jsonl files...\n", argv[0]); return 64; }
    FILE *out = fopen(argv[1], "w");
    if (!out) return 1;
    CGcontext ctx = cgCreateContext();
    const char *args[] = {"-po MaxLocalParams=128", NULL};
    for (int i = 2; i < argc; i++) {
        char *src = slurp(argv[i]);
        if (!src) continue;
        CGprofile prof = strstr(src, "!!ARBfp") || strstr(argv[i], ".fpo") ? CG_PROFILE_ARBFP1
                                                                            : CG_PROFILE_ARBVP1;
        CGprogram p = cgCreateProgram(ctx, CG_OBJECT, src, prof, "main", args);
        fputs("{\"file\": ", out);
        js(out, strrchr(argv[i], '/') ? strrchr(argv[i], '/') + 1 : argv[i]);
        fprintf(out, ", \"profile\": %d, \"ok\": %d", prof, p != NULL);
        if (p) {
            const char *compiled = cgGetProgramString(p, CG_COMPILED_PROGRAM);
            fprintf(out, ", \"compiled_same\": %d, \"entry\": ", compiled && !strcmp(compiled, src));
            js(out, cgGetProgramString(p, CG_PROGRAM_ENTRY));
            fputs(", \"params\": [", out);
            int first = 1;
            CGenum spaces[] = {CG_PROGRAM, CG_GLOBAL};
            for (int s = 0; s < 2; s++) {
                for (CGparameter q = cgGetFirstLeafParameter(p, spaces[s]); q;
                     q = cgGetNextLeafParameter(q)) {
                    const char *name = cgGetParameterName(q);
                    CGtype t = cgGetParameterType(q);
                    fprintf(out, "%s{\"ns\": %d, \"name\": ", first ? "" : ", ", spaces[s]);
                    first = 0;
                    js(out, name);
                    fprintf(out, ", \"type\": %d, \"semantic\": ", t);
                    js(out, cgGetParameterSemantic(q));
                    fprintf(out, ", \"dir\": %d, \"var\": %d, \"res\": %d, \"index\": %lu, \"used\": %d, "
                                 "\"named_ok\": %d",
                            cgGetParameterDirection(q), cgGetParameterVariability(q),
                            cgGetParameterResource(q), cgGetParameterResourceIndex(q),
                            cgIsParameterUsed(q, p), name && cgGetNamedParameter(p, name) == q);
                    if (t >= CG_SAMPLER1D && t <= CG_SAMPLERCUBE)
                        fprintf(out, ", \"tex_enum\": %u", cgGLGetTextureEnum(q));
                    fputs("}", out);
                }
            }
            fputs("]", out);
            cgDestroyProgram(p);
        }
        fputs("}\n", out);
        free(src);
    }
    fclose(out);
    cgDestroyContext(ctx);
    return 0;
}
