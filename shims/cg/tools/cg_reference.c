// x86_64 research tool: load each program file through NVIDIA's Cg 3.1 runtime the
// way the game does (CG_OBJECT, arbvp1/arbfp1, "-po MaxLocalParams=128") and dump
// what the runtime reports, as the oracle for the shim's own #var parser.
//
//   clang -arch x86_64 cg_reference.c -framework OpenGL <Cg> -o cg_reference
//   ./cg_reference <out.jsonl> <program files...>
//
// One JSON object per line: {"file", "ok", "error", "params": [...]} where params
// are the leaf parameters in cgGetFirstLeafParameter/Next order for CG_PROGRAM
// then CG_GLOBAL, with every property the game queries plus the resource,
// cgGetNamedParameter round-trip and cgGLGetTextureEnum (needs a GL context).
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/cg_api.h"

const char *cgGetErrorString(int);
int cgGetError(void);
const char *cgGetLastListing(CGcontext);
CGresource cgGetParameterResource(CGparameter);
const char *cgGetResourceString(CGresource);
const char *cgGetTypeString(CGtype);
CGenum cgGetParameterNamespace(CGparameter);
CGprogram cgGetParameterProgram(CGparameter);

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
    fread(b, 1, n, f);
    b[n] = 0;
    fclose(f);
    return b;
}

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: %s out.jsonl files...\n", argv[0]); return 64; }
    CGLPixelFormatAttribute attrs[] = {kCGLPFAAccelerated, 0};
    CGLPixelFormatObj pf;
    GLint npf;
    CGLContextObj glctx;
    if (CGLChoosePixelFormat(attrs, &pf, &npf) || CGLCreateContext(pf, NULL, &glctx) ||
        CGLSetCurrentContext(glctx)) {
        fprintf(stderr, "no GL context\n");
        return 1;
    }
    FILE *out = fopen(argv[1], "w");
    CGcontext ctx = cgCreateContext();
    cgGLSetContextOptimalOptions(ctx, CG_PROFILE_ARBVP1);
    cgGLSetContextOptimalOptions(ctx, CG_PROFILE_ARBFP1);
    const char *args[] = {"-po MaxLocalParams=128", NULL};
    for (int i = 2; i < argc; i++) {
        char *src = slurp(argv[i]);
        if (!src) continue;
        CGprofile prof = strstr(src, "!!ARBfp") || strstr(argv[i], ".fpo") ? CG_PROFILE_ARBFP1
                                                                            : CG_PROFILE_ARBVP1;
        cgGetError();
        CGprogram p = cgCreateProgram(ctx, CG_OBJECT, src, prof, "main", args);
        int err = cgGetError();
        fputs("{\"file\": ", out);
        js(out, strrchr(argv[i], '/') ? strrchr(argv[i], '/') + 1 : argv[i]);
        fprintf(out, ", \"profile\": %d, \"ok\": %d, \"error\": ", prof, p != NULL);
        js(out, err ? cgGetErrorString(err) : NULL);
        if (p) {
            cgGetError();
            cgGLLoadProgram(p);
            int lerr = cgGetError();
            fprintf(out, ", \"load_ok\": %d", lerr == 0);
            const char *compiled = cgGetProgramString(p, CG_COMPILED_PROGRAM);
            fprintf(out, ", \"compiled_same\": %d, \"entry\": ",
                    compiled && !strcmp(compiled, src));
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
                    fprintf(out, ", \"type\": %d, \"type_s\": ", t);
                    js(out, cgGetTypeString(t));
                    fputs(", \"semantic\": ", out);
                    js(out, cgGetParameterSemantic(q));
                    CGresource r = cgGetParameterResource(q);
                    fprintf(out, ", \"dir\": %d, \"var\": %d, \"res\": %d, \"res_s\": ",
                            cgGetParameterDirection(q), cgGetParameterVariability(q), r);
                    js(out, cgGetResourceString(r));
                    fprintf(out, ", \"index\": %lu, \"used\": %d, \"named_ok\": %d",
                            cgGetParameterResourceIndex(q), cgIsParameterUsed(q, p),
                            name && cgGetNamedParameter(p, name) == q);
                    if (t >= CG_SAMPLER1D && t <= CG_SAMPLERCUBE) {
                        cgGetError();
                        GLenum e = cgGLGetTextureEnum(q);
                        fprintf(out, ", \"tex_enum\": %u, \"tex_enum_err\": %d", e, cgGetError());
                    }
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
