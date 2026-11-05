// The shader sources of Learn & Play's offline corpus, made on the user's Mac from their own game files
// (docs/windows/PERFORMANCE.md "Shader preparation"; tools/metal/make-corpus.sh --learn-play runs this).
//
//   lp_shaders PROGRAMS OUTDIR FILE...
//
// FILEs are searched for Direct3D 9 shader bytecode: the game image (its Scaleform shaders), and the
// .vso/.pso/.fxo/.cache files of static.psarc. PROGRAMS (win-x86/shaders/programs.tsv) lists the programs
// the game was seen linking: which vertex and pixel shader, by a hash of their bytecode, and for a
// fixed-function stage the state key. No game content is in it. To those come the pairs of an effect: the vertex and
// pixel shaders of one .fxo, or a .vso and .pso of one name, when the pixel shader's inputs are among the vertex
// shader's outputs (every such program in the list is one of these, and they are few). For each program whose shaders
// were found this writes what the D3D9 layer gives the GL layer for it, by the same code (d3d9_sm_glsl,
// d3d9_ffp_sources):
//   OUTDIR/<hash>.vert, OUTDIR/<hash>.frag     the GLSL, by a hash of the text
//   OUTDIR/pairs.tsv                           <vertex file> TAB <fragment file> TAB <attribute bindings>,
//                                              as tools/metal/pair_shaders.py writes it
// and on stdout a summary. Exit 0, also with programs missing: they are translated when the game links
// them, as with no corpus at all. Everything in OUTDIR comes from the game: keep it on this Mac.
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "d3d9_ffp_gen.h"

void d3d9_log_once(const char *fmt, ...) { (void)fmt; }


static uint64_t fnv64(const void *p, size_t n) {
    const uint8_t *b = p;
    uint64_t h = 0xcbf29ce484222325ull;
    while (n--) h = (h ^ *b++) * 0x100000001b3ull;
    return h;
}

// --- the shaders found, by hash (open addressing) -------------------------------------------------
typedef struct {
    uint64_t hash;
    d3d9_sm *sm;
    char *ps_glsl;
} found;

static found *table;
static size_t table_cap, table_n;

static found *table_slot(uint64_t h) {
    size_t k = h & (table_cap - 1);
    while (table[k].sm && table[k].hash != h) k = (k + 1) & (table_cap - 1);
    return &table[k];
}

static void table_add(uint64_t h, d3d9_sm *sm) {
    if ((table_n + 1) * 2 > table_cap) {
        found *old = table;
        size_t n = table_cap;
        table_cap = n ? n * 2 : 1024;
        table = calloc(table_cap, sizeof *table);
        for (size_t k = 0; k < n; k++)
            if (old[k].sm) *table_slot(old[k].hash) = old[k];
        free(old);
    }
    found *e = table_slot(h);
    if (e->sm) {
        d3d9_sm_free(sm);
        return;
    }
    e->hash = h;
    e->sm = sm;
    table_n++;
}

// The shaders of each effect file, by the file's name without its extension.
typedef struct {
    char name[64];
    uint64_t hash;
    int is_ps;
} member;

static member *members;
static int nmembers;

static const char *effect_name(const char *path, char *o, size_t n) {
    const char *b = strrchr(path, '/');
    b = b ? b + 1 : path;
    const char *e = strrchr(b, '.');
    if (!e || (strcmp(e, ".vso") && strcmp(e, ".pso") && strcmp(e, ".fxo"))) return 0;
    snprintf(o, n, "%.*s", (int)(e - b), b);
    return o;
}

// Every place in the file where a shader parses to its end token. The files hold bytecode at any
// byte offset (a .cache's entries are packed), so each offset with a version token is tried.
static int scan_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "lp_shaders: can't read %s\n", path);
        return 0;
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *d = malloc((size_t)n + 4);
    size_t got = fread(d, 1, (size_t)n, f);
    fclose(f);
    enum { MAX_TOKENS = 1 << 18 };
    uint32_t *tok = malloc(4 * MAX_TOKENS);
    char effect[64];
    const char *in_effect = effect_name(path, effect, sizeof effect);
    int count = 0;
    for (long o = 0; o + 8 <= (long)got; o++) {
        if (d[o + 3] != 0xff || (d[o + 2] != 0xff && d[o + 2] != 0xfe)) continue;
        if ((d[o + 1] != 2 && d[o + 1] != 3) || d[o] > 1) continue;
        long left = ((long)got - o) / 4;
        if (left > MAX_TOKENS) left = MAX_TOKENS;
        memcpy(tok, d + o, 4 * (size_t)left);
        char err[256];
        d3d9_sm *sm = d3d9_sm_parse(tok, (uint32_t)left, err, sizeof err);
        if (!sm) continue;
        uint32_t nt = d3d9_sm_get_info(sm)->ntokens;
        if (nt < 4) {
            d3d9_sm_free(sm);
            continue;
        }
        uint64_t h = fnv64(d3d9_sm_tokens(sm), 4 * (size_t)nt);
        if (in_effect) {
            members = realloc(members, sizeof *members * (size_t)(nmembers + 1));
            member *m = &members[nmembers++];
            snprintf(m->name, sizeof m->name, "%s", effect);
            m->hash = h;
            m->is_ps = d3d9_sm_get_info(sm)->is_ps;
        }
        table_add(h, sm);
        o += 4 * (long)nt - 1;
        count++;
    }
    free(tok);
    free(d);
    return count;
}

static found *find(uint64_t h) {
    if (!table_cap) return 0;
    found *e = table_slot(h);
    return e->sm ? e : 0;
}

// --- output ---------------------------------------------------------------------------------------
static const char *outdir;
static FILE *pairs;
static int written;

static void write_text(const char *ext, const char *text, char *path, size_t n) {
    snprintf(path, n, "%s/%016llx.%s", outdir, (unsigned long long)fnv64(text, strlen(text)), ext);
    FILE *f = fopen(path, "wb");
    if (!f || fputs(text, f) < 0) fprintf(stderr, "lp_shaders: can't write %s\n", path);
    if (f) fclose(f);
}

static void emit(const char *vs, const char *fs) {
    // different lines can come to the same text (shaders that differ in their comments, key fields the text
    // does not use): a program once
    static uint64_t *seen;
    static int nseen;
    uint64_t id = fnv64(vs, strlen(vs)) * 31 + fnv64(fs, strlen(fs));
    for (int k = 0; k < nseen; k++)
        if (seen[k] == id) return;
    seen = realloc(seen, sizeof *seen * (size_t)(nseen + 1));
    seen[nseen++] = id;
    char v[1200], f[1200], bind[256] = "";
    write_text("vert", vs, v, sizeof v);
    write_text("frag", fs, f, sizeof f);
    for (int r = 0; r < 16; r++) {  // as the D3D9 layer binds them: register N at location N
        size_t l = strlen(bind);
        snprintf(bind + l, sizeof bind - l, "%srs_v%d=%d", r ? "," : "", r, r);
    }
    fprintf(pairs, "%s\t%s\t%s\n", v, f, bind);
    written++;
}

static const char *ps_glsl(found *p) {
    if (!p->ps_glsl) {
        char err[256];
        p->ps_glsl = d3d9_sm_glsl(p->sm, 0, err, sizeof err);
    }
    return p->ps_glsl;
}

static int hex_key(const char *s, ffp_key *k) {
    if (strlen(s) != 2 * sizeof *k) return 0;
    uint8_t *b = (uint8_t *)k;
    for (size_t j = 0; j < sizeof *k; j++) {
        unsigned v;
        if (sscanf(s + 2 * j, "%2x", &v) != 1) return 0;
        b[j] = (uint8_t)v;
    }
    return 1;
}

// One line of PROGRAMS: "pair VS PS" or "ffp VS PS KEY" (hashes in hex, 0: that stage is fixed function).
// Returns 1 when its program was written, 0 when a shader is missing or a line is not understood.
static int program(char *line) {
    char kind[8], key[1024] = "";
    unsigned long long vh, ph;
    int n = sscanf(line, "%7s %llx %llx %1023s", kind, &vh, &ph, key);
    if (n < 3) return 0;
    found *v = vh ? find(vh) : 0, *p = ph ? find(ph) : 0;
    if ((vh && !v) || (ph && !p)) return 0;
    char err[512] = "", *vsrc = 0, *fsrc = 0;
    int ok = 0;
    if (!strcmp(kind, "pair") && v && p) {
        vsrc = d3d9_sm_glsl(v->sm, p->sm, err, sizeof err);
        const char *g = ps_glsl(p);
        if (vsrc && g) emit(vsrc, g), ok = 1;
    } else if (!strcmp(kind, "ffp")) {
        ffp_key k;
        if (hex_key(key, &k) && d3d9_ffp_sources(&k, v ? v->sm : 0, p ? p->sm : 0, p ? ps_glsl(p) : 0, &vsrc, &fsrc, err,
                                                 sizeof err)) {
            emit(vsrc, fsrc);
            ok = 1;
        }
    }
    free(vsrc);
    free(fsrc);
    return ok;
}

// Whether a vertex shader (its GLSL generated for no pixel shader in particular: every output) writes what ps reads.
static int feeds(const char *vs_glsl, const d3d9_sm *ps) {
    d3d9_sm_input in[32];
    int n = d3d9_sm_ps_semantics(ps, in, 32);
    for (int k = 0; k < n; k++) {
        char name[48], decl[64];
        snprintf(decl, sizeof decl, "varying vec4 %s;", d3d9_sm_varying(in[k].usage, in[k].index, name, sizeof name));
        if (!strstr(vs_glsl, decl)) return 0;
    }
    return 1;
}

static void effect_pairs(void) {
    for (int a = 0; a < nmembers; a++) {
        if (members[a].is_ps) continue;
        found *v = find(members[a].hash);
        char err[512] = "", *all = d3d9_sm_glsl(v->sm, 0, err, sizeof err);
        if (!all) continue;
        for (int b = 0; b < nmembers; b++) {
            if (!members[b].is_ps || strcmp(members[a].name, members[b].name)) continue;
            found *p = find(members[b].hash);
            const char *g = ps_glsl(p);
            if (!g || !feeds(all, p->sm)) continue;
            char *vsrc = d3d9_sm_glsl(v->sm, p->sm, err, sizeof err);
            if (vsrc) emit(vsrc, g);
            free(vsrc);
        }
        free(all);
    }
}

int main(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr, "usage: lp_shaders PROGRAMS OUTDIR FILE...\n");
        return 2;
    }
    outdir = argv[2];
    mkdir(outdir, 0755);
    char path[1024];
    snprintf(path, sizeof path, "%s/pairs.tsv", outdir);
    pairs = fopen(path, "w");
    FILE *m = fopen(argv[1], "r");
    if (!pairs || !m) {
        fprintf(stderr, "lp_shaders: can't open %s\n", pairs ? argv[1] : path);
        return 1;
    }
    int shaders = 0;
    for (int a = 3; a < argc; a++) shaders += scan_file(argv[a]);
    emit(d3d9_blit_vs, d3d9_blit_fs);
    int total = 0, missing = 0;
    char line[2048];
    while (fgets(line, sizeof line, m)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        total++;
        if (!program(line)) missing++;
    }
    fclose(m);
    effect_pairs();
    fclose(pairs);
    printf("lp_shaders: %d shaders (%zu distinct) in %d files; %d of %d listed programs made, %d not; %d programs\n", shaders,
           table_n, argc - 3, total - missing, total, missing, written);
    return 0;
}
