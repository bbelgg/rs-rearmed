// Unit tests of the GLSL -> MSL translator (src/msl/): reflection, attribute locations,
// unsupported input, the corpus file and its lookup. Rendering is checked by msl_verify.
//   msl_unit <scratch corpus path>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "msl_internal.h"

static int passed, failed;
#define CHECK(c)                                                                \
    do {                                                                        \
        if (c) passed++;                                                        \
        else { failed++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } \
    } while (0)

static const rsmsl_uniform *uni(const rsmsl_program *p, const char *n) {
    for (int i = 0; i < p->n_uniforms; i++)
        if (!strcmp(p->uniforms[i].name, n)) return &p->uniforms[i];
    return NULL;
}
static const rsmsl_attrib *attr(const rsmsl_program *p, const char *n) {
    for (int i = 0; i < p->n_attribs; i++)
        if (!strcmp(p->attribs[i].name, n)) return &p->attribs[i];
    return NULL;
}
static const rsmsl_sampler *smp(const rsmsl_program *p, const char *n) {
    for (int i = 0; i < p->n_samplers; i++)
        if (!strcmp(p->samplers[i].name, n)) return &p->samplers[i];
    return NULL;
}

static const char *VS =
    "#version 120\n"
    "uniform float f; uniform vec3 v3; uniform vec2 v2; uniform mat3 m3; uniform vec4 arr[3];\n"
    "uniform bool b; uniform mat4 shared_m;\n"
    "attribute vec4 pos; attribute vec2 uv; attribute float unused_a; attribute vec3 nrm;\n"
    "varying vec2 tc;\n"
    "void main() { gl_Position = shared_m * pos + vec4(m3 * nrm * f + v3, 0.0) + arr[2] + vec4(v2, b ? 1.0 : 0.0, 0.0);"
    " tc = uv + gl_MultiTexCoord1.xy; }\n";
static const char *FS =
    "#version 120\n"
    "uniform sampler2D tex[2]; uniform samplerCube env; uniform mat4 shared_m; uniform vec4 tint;\n"
    "varying vec2 tc;\n"
    "void main() { gl_FragColor = texture2D(tex[1], tc) + textureCube(env, vec3(tc, 1.0)) + shared_m[0] * tint; }\n";

static void test_reflection(void) {
    rsmsl_attrib bound[] = {{"nrm", 5, 0}, {"gl_Vertex", 9, 0}};
    char *err = NULL;
    rsmsl_program *p = rsmsl_translate_direct(VS, FS, bound, 2, &err);
    CHECK(p && !err);
    if (!p) { fprintf(stderr, "%s\n", err); return; }
    // VS block: f 0, v3 16, v2 32, m3 48 (3 x 16), arr 96 (3 x 16), b 144, shared_m 160.
    CHECK(uni(p, "f") && uni(p, "f")->vs_offset == 0 && uni(p, "f")->type == RSMSL_FLOAT);
    CHECK(uni(p, "v3") && uni(p, "v3")->vs_offset == 16 && uni(p, "v3")->stride == 16);
    CHECK(uni(p, "v2") && uni(p, "v2")->vs_offset == 32);
    CHECK(uni(p, "m3") && uni(p, "m3")->vs_offset == 48 && uni(p, "m3")->stride == 48);
    CHECK(uni(p, "arr") && uni(p, "arr")->vs_offset == 96 && uni(p, "arr")->array_size == 3 && uni(p, "arr")->stride == 16);
    CHECK(uni(p, "b") && uni(p, "b")->vs_offset == 144 && uni(p, "b")->type == RSMSL_BOOL);
    CHECK(uni(p, "shared_m") && uni(p, "shared_m")->vs_offset == 160 && uni(p, "shared_m")->fs_offset == 0 &&
          uni(p, "shared_m")->stages == (RSMSL_STAGE_VS | RSMSL_STAGE_FS));
    CHECK(uni(p, "tint") && uni(p, "tint")->fs_offset == 64 && uni(p, "tint")->vs_offset == -1);
    CHECK(p->vs_uniform_size == 224 && p->fs_uniform_size == 80);
    // Samplers: tex[0], tex[1] at 0, 1; env at 2; uniform entries with no storage.
    CHECK(p->n_samplers == 3);
    CHECK(smp(p, "tex[0]") && smp(p, "tex[0]")->index == 0 && smp(p, "tex[1]") && smp(p, "tex[1]")->index == 1);
    CHECK(smp(p, "env") && smp(p, "env")->index == 2 && smp(p, "env")->type == RSMSL_SAMPLER_CUBE);
    CHECK(uni(p, "tex") && uni(p, "tex")->array_size == 2 && uni(p, "tex")->type == RSMSL_SAMPLER_2D &&
          uni(p, "tex")->vs_offset == -1 && uni(p, "tex")->fs_offset == -1);
    // Attributes: inactive ones dropped; bound by name; built-ins fixed (binding ignored);
    // the rest lowest free in declaration order.
    CHECK(!attr(p, "unused_a"));
    CHECK(attr(p, "nrm") && attr(p, "nrm")->location == 5 && attr(p, "nrm")->type == RSMSL_VEC3);
    CHECK(attr(p, "gl_MultiTexCoord1") && attr(p, "gl_MultiTexCoord1")->location == 9);
    CHECK(attr(p, "pos") && attr(p, "pos")->location == 0);
    CHECK(attr(p, "uv") && attr(p, "uv")->location == 1);
    CHECK(strstr(p->msl, "[[function_constant(0)]]") && strstr(p->msl, "[[buffer(1)]]") &&
          strstr(p->msl, "rs_Position.y = -rs_out.rs_Position.y"));
    rsmsl_free(p);
}

static void test_errors(void) {
    const char *bad[][2] = {
        {"#version 120\nvoid main() { gl_Position = vec4(0.0); }\n", "#version 120\nuniform sampler2DShadow s;\nvoid main() { gl_FragColor = vec4(1.0); }\n"},
        {"#version 120\nvoid main() { gl_Position = vec4(0.0) }\n", "#version 120\nvoid main() { gl_FragColor = vec4(1.0); }\n"},
        {"#version 120\nvarying vec2 a;\nvoid main() { gl_Position = vec4(0.0); }\n", "#version 120\nvarying vec3 a;\nvoid main() { gl_FragColor = vec4(a, 1.0); }\n"},
        {"#version 120\nvoid main() { gl_Position = vec4(0.0); }\n", "#version 120\nvarying vec3 missing;\nvoid main() { gl_FragColor = vec4(missing, 1.0); }\n"},
        {"#version 120\nvoid main() { gl_Position = vec4(0.0); }\n", "#version 120\nvoid main() { gl_FragColor = vec3(1.0); }\n"},
        {"#version 120\n#define X 1\nvoid main() { gl_Position = vec4(0.0); }\n", "#version 120\nvoid main() { gl_FragColor = vec4(1.0); }\n"},
        {"#version 120\nvoid foo() {}\n", "#version 120\nvoid main() { gl_FragColor = vec4(1.0); }\n"},
        {"#version 120\nvoid main() { gl_Position = gl_LightSource[0].position; }\n", "#version 120\nvoid main() { gl_FragColor = vec4(1.0); }\n"},
    };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        char *err = NULL;
        rsmsl_program *p = rsmsl_translate_direct(bad[i][0], bad[i][1], NULL, 0, &err);
        CHECK(!p && err && *err);
        if (p) { fprintf(stderr, "case %zu translated\n", i); rsmsl_free(p); }
        free(err);
    }
}

static void test_key(void) {
    CHECK(rsmsl_key("a", "b") == rsmsl_key("a", "b"));
    CHECK(rsmsl_key("ab", "") != rsmsl_key("a", "b"));
    CHECK(rsmsl_key(NULL, NULL) == rsmsl_key("", ""));
    CHECK(rsmsl_key("", "") == 0x08328807b4eb6fedull);  // FNV-1a 64 of two 0 bytes: stable across builds
}

static void test_corpus(const char *path) {
    rsmsl_attrib bound[] = {{"nrm", 5, 0}};
    rsmsl_program *p = rsmsl_translate_direct(VS, FS, bound, 1, NULL);
    CHECK(p != NULL);
    if (!p) return;
    rsmsl_corpus_writer *w = rsmsl_corpus_begin();
    rsmsl_corpus_add(w, VS, FS, p, RSMSL_CORPUS_VERIFIED);
    // A second entry with a marker in the MSL, to see the lookup really comes from the file.
    rsmsl_program q = *p;
    q.msl = "// from the corpus\n";
    const char *FS2 = "#version 120\nvarying vec2 tc;\nvoid main() { gl_FragColor = vec4(tc, 0.0, 1.0); }\n";
    rsmsl_corpus_add(w, VS, FS2, &q, 0);
    CHECK(rsmsl_corpus_finish(w, path) == 0);

    rsmsl_corpus_override(path);
    int n = 0;
    CHECK(rsmsl_corpus_path(&n) && n == 2);
    CHECK(rsmsl_corpus_flags(VS, FS) == RSMSL_CORPUS_VERIFIED && rsmsl_corpus_flags(VS, FS2) == 0);
    CHECK(rsmsl_corpus_flags(VS, "x") == -1);

    char *err = NULL;
    rsmsl_program *c = rsmsl_translate(VS, FS, bound, 1, &err);
    CHECK(c && c->from_corpus && !strcmp(c->msl, p->msl) && c->n_uniforms == p->n_uniforms &&
          c->n_samplers == p->n_samplers && c->vs_uniform_size == p->vs_uniform_size);
    if (c) {
        int same = c->n_attribs == p->n_attribs;
        for (int i = 0; same && i < p->n_attribs; i++)
            same = !strcmp(c->attribs[i].name, p->attribs[i].name) && c->attribs[i].location == p->attribs[i].location;
        for (int i = 0; same && i < p->n_uniforms; i++)
            same = !strcmp(c->uniforms[i].name, p->uniforms[i].name) && c->uniforms[i].fs_offset == p->uniforms[i].fs_offset;
        CHECK(same);
    }
    rsmsl_free(c);
    c = rsmsl_translate(VS, FS2, bound, 1, NULL);
    CHECK(c && c->from_corpus && !strcmp(c->msl, "// from the corpus\n"));
    rsmsl_free(c);
    // Other bindings: the corpus entry can't be used, translated again.
    rsmsl_attrib other[] = {{"nrm", 6, 0}};
    c = rsmsl_translate(VS, FS2, other, 1, NULL);
    CHECK(c && !c->from_corpus && attr(c, "nrm") && attr(c, "nrm")->location == 6);
    rsmsl_free(c);
    // Not in the corpus: runtime translation.
    c = rsmsl_translate(VS, "#version 120\nvarying vec2 tc;\nvoid main() { gl_FragColor = vec4(tc.y); }\n", bound, 1, NULL);
    CHECK(c && !c->from_corpus);
    rsmsl_free(c);
    rsmsl_free(p);
    remove(path);
}

int main(int argc, char **argv) {
    test_reflection();
    test_errors();
    test_key();
    test_corpus(argc > 1 ? argv[1] : "msl_unit_corpus.bin");
    printf("%d passed, %d failed\n", passed, failed);
    return failed != 0;
}
