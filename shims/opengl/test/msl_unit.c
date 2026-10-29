// Unit tests of the GLSL -> MSL translator (src/msl/): reflection, attribute locations,
// unsupported input, the corpus file and its lookup. Rendering is checked by msl_verify.
//   msl_unit <scratch corpus path>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

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
        // Used to loop forever at the end of the input.
        {"#version 120\nvoid main() { gl_Position = vec4(0.0); }\ninvariant gl_Position,", "#version 120\nvoid main() { gl_FragColor = vec4(1.0); }\n"},
        // Sizes: an absurd array, a uniform block over the limit, a constant out of int range.
        {"#version 120\nvoid main() { float a[100000000]; gl_Position = vec4(0.0); }\n", "#version 120\nvoid main() { gl_FragColor = vec4(1.0); }\n"},
        {"#version 120\nuniform mat4 m[40000];\nvoid main() { gl_Position = m[1] * m[39999][0]; }\n", "#version 120\nvoid main() { gl_FragColor = vec4(1.0); }\n"},
        {"#version 120\nconst int K = 99999 * 99999 * 99999;\nvoid main() { float a[K]; gl_Position = vec4(0.0); }\n", "#version 120\nvoid main() { gl_FragColor = vec4(1.0); }\n"},
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
    CHECK(rsmsl_key("", "") == 0x1dd1fba3a0365d2full);  // pinned: stable across runs and builds
    CHECK(rsmsl_key("a", "b") == 0x1d473c4a8ff231fbull);
    // Every length around the 16-byte blocks hashes differently (the tail is length-tagged).
    char buf[40] = {0};
    uint64_t seen[40];
    int distinct = 1;
    for (int n = 0; n < 40; n++) {
        buf[n] = 0;
        seen[n] = rsmsl_key(buf, "x");
        for (int m = 0; m < n; m++) distinct &= seen[m] != seen[n];
        buf[n] = 'a';
    }
    CHECK(distinct);
}

// Deeply nested input fails with a message, within a 512 KB stack (a secondary thread's default).
static const char *g_deep;
static void *deep_thread(void *arg) {
    char *err = NULL;
    rsmsl_program *p = rsmsl_translate_direct("void main() { gl_Position = vec4(0.0); }", g_deep, NULL, 0, &err);
    *(int *)arg = !p && err && strstr(err, "nesting too deep");
    rsmsl_free(p);
    free(err);
    return NULL;
}
static void test_nesting(void) {
    for (int open_ch = 0; open_ch < 2; open_ch++) {
        int n = 100000;
        char *fs = malloc((size_t)n * 8 + 100);
        char *o = fs + sprintf(fs, "void main() { %s", open_ch ? "" : "gl_FragColor = vec4(");
        for (int i = 0; i < n; i++) *o++ = open_ch ? '{' : '(';
        o += sprintf(o, "%s", open_ch ? "" : "1.0");
        for (int i = 0; i < n; i++) o += sprintf(o, "%s", open_ch ? "}" : ")");
        sprintf(o, "%s }", open_ch ? "" : ");");
        g_deep = fs;
        pthread_attr_t at;
        pthread_attr_init(&at);
        pthread_attr_setstacksize(&at, 512 * 1024);
        pthread_t t;
        int ok = 0;
        CHECK(pthread_create(&t, &at, deep_thread, &ok) == 0);
        pthread_join(t, NULL);
        CHECK(ok);
        free(fs);
    }
    // Nesting the game's shaders use (at most ~20 levels) still translates.
    char fs[4096];
    char *o = fs + sprintf(fs, "void main() { float x = 0.5; gl_FragColor = vec4(");
    for (int i = 0; i < 40; i++) *o++ = '(';
    o += sprintf(o, "x");
    for (int i = 0; i < 40; i++) o += sprintf(o, " * 1.0)");
    sprintf(o, "); }");
    char *err = NULL;
    rsmsl_program *p = rsmsl_translate_direct("void main() { gl_Position = vec4(0.0); }", fs, NULL, 0, &err);
    CHECK(p != NULL);
    if (!p) fprintf(stderr, "%s\n", err);
    rsmsl_free(p);
    free(err);
}

static int same_program(const rsmsl_program *a, const rsmsl_program *b) {
    if (strcmp(a->msl, b->msl) || a->n_attribs != b->n_attribs || a->n_uniforms != b->n_uniforms ||
        a->n_samplers != b->n_samplers || a->vs_uniform_size != b->vs_uniform_size || a->fs_uniform_size != b->fs_uniform_size)
        return 0;
    for (int i = 0; i < a->n_attribs; i++)
        if (strcmp(a->attribs[i].name, b->attribs[i].name) || a->attribs[i].location != b->attribs[i].location ||
            a->attribs[i].type != b->attribs[i].type)
            return 0;
    for (int i = 0; i < a->n_uniforms; i++) {
        const rsmsl_uniform *x = &a->uniforms[i], *y = &b->uniforms[i];
        if (strcmp(x->name, y->name) || x->type != y->type || x->array_size != y->array_size || x->stages != y->stages ||
            x->vs_offset != y->vs_offset || x->fs_offset != y->fs_offset || x->stride != y->stride)
            return 0;
    }
    for (int i = 0; i < a->n_samplers; i++)
        if (strcmp(a->samplers[i].name, b->samplers[i].name) || a->samplers[i].index != b->samplers[i].index ||
            a->samplers[i].type != b->samplers[i].type || a->samplers[i].stages != b->samplers[i].stages)
            return 0;
    return 1;
}

// A damaged corpus (truncated, bytes changed, another version) is ignored entry by entry or as a
// whole: every lookup still returns the right program (from the corpus only when intact) and
// never crashes. Each variant runs in a child process (the corpus is opened once per process).
// msls: the MSL of every entry of the intact corpus; an entry read from a damaged one is either
// rejected (NULL) or one of them.
static void test_corpus_damage(const char *path, const rsmsl_program *ref, const rsmsl_attrib *bound, int nb,
                               const char *const *msls, int n_msls) {
    FILE *f = fopen(path, "rb");
    if (!f) { CHECK(0); return; }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *orig = malloc((size_t)size);
    CHECK(fread(orig, 1, (size_t)size, f) == (size_t)size);
    fclose(f);
    char dpath[1024];
    snprintf(dpath, sizeof dpath, "%s.damaged", path);
    int variants = 0, bad = 0, from_corpus = 0;
    unsigned seed = 12345;
    for (int v = 0; v < 600; v++) {
        unsigned char *d = malloc((size_t)size);
        memcpy(d, orig, (size_t)size);
        long n = size;
        if (v < 150) {
            n = (long)((double)size * v / 150);  // truncated
        } else if (v < 150 + 24) {
            d[8 + (v - 150) / 3] ^= (unsigned char)(1u << ((v - 150) % 3 * 3));  // version, counts, pool offset
        } else {
            for (int k = 0; k < 1 + v % 3; k++) {  // random bytes changed anywhere
                seed = seed * 1103515245u + 12345u;
                d[(seed >> 8) % (unsigned)size] ^= (unsigned char)(1 + (seed >> 24) % 255);
            }
        }
        FILE *o = fopen(dpath, "wb");
        fwrite(d, 1, (size_t)n, o);
        fclose(o);
        free(d);
        fflush(stderr);
        pid_t pid = fork();
        if (pid == 0) {
            rsmsl_corpus_override(dpath);
            rsmsl_program *c = rsmsl_translate(VS, FS, bound, nb, NULL);
            int ok = c && same_program(c, ref);
            int fc = c && c->from_corpus;
            rsmsl_free(c);
            rsmsl_corpus_flags(VS, "x");
            int n = rsmsl_corpus_count();
            for (int i = -1; i <= n; i++) {
                char *m = rsmsl_corpus_msl(i);
                int known = 0;
                for (int k = 0; m && k < n_msls; k++)
                    known |= !strcmp(m, msls[k]);
                if (m && !known) ok = 0;
                free(m);
            }
            _exit(ok ? (fc ? 0 : 1) : 2);
        }
        int st = 0;
        waitpid(pid, &st, 0);
        variants++;
        if (!WIFEXITED(st) || WEXITSTATUS(st) > 1) {
            bad++;
            fprintf(stderr, "damaged corpus variant %d: %s %d\n", v, WIFEXITED(st) ? "exit" : "signal",
                    WIFEXITED(st) ? WEXITSTATUS(st) : WTERMSIG(st));
        } else if (WEXITSTATUS(st) == 0) {
            from_corpus++;
        }
    }
    printf("damaged corpus: %d variants, %d from the corpus, the rest translated at run time\n", variants, from_corpus);
    CHECK(bad == 0);
    CHECK(from_corpus > 0 && from_corpus < variants);  // intact variants (n == size, harmless bytes) still hit
    remove(dpath);
    free(orig);
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
    const char *msls[] = {p->msl, q.msl};
    test_corpus_damage(path, p, bound, 1, msls, 2);

    rsmsl_corpus_override(path);
    int n = 0;
    CHECK(rsmsl_corpus_path(&n) && n == 2);
    // Enumeration: every entry's MSL once (index order), nothing out of range.
    CHECK(rsmsl_corpus_count() == 2);
    char *m0 = rsmsl_corpus_msl(0), *m1 = rsmsl_corpus_msl(1);
    CHECK(m0 && m1 && strcmp(m0, m1) && (!strcmp(m0, p->msl) || !strcmp(m0, q.msl)) &&
          (!strcmp(m1, p->msl) || !strcmp(m1, q.msl)));
    free(m0);
    free(m1);
    CHECK(rsmsl_corpus_msl(-1) == NULL && rsmsl_corpus_msl(2) == NULL);
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
    alarm(120);  // a hang is a failure
    test_reflection();
    test_errors();
    test_nesting();
    test_key();
    test_corpus(argc > 1 ? argv[1] : "msl_unit_corpus.bin");
    printf("%d passed, %d failed\n", passed, failed);
    return failed != 0;
}
