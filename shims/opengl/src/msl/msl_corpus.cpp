// Offline shader corpus and the public rsmsl_translate (msl_xlat.h).
//
// The corpus is MSL + reflection for every GLSL pair seen in the game, translated and checked
// against GL at build time (tools/metal/build-msl-corpus.sh). rsmsl_translate looks there first and
// translates at run time otherwise (logged with RS_MSL_LOG=1).
//
// Where the corpus is found (first hit wins):
//   RS_MSL_CORPUS=<file>     (set but empty: no corpus)
//   <dir of libRSOpenGL>/rsmsl-corpus.bin
//   <dir of libRSOpenGL>/../Resources/rsmsl-corpus.bin   (app bundle: Contents/MacOS -> Contents/Resources)
//
// File format (little-endian; the translator only builds for little-endian hosts):
//   char magic[8] "RSMSLC3\0"; u32 translator version (RSMSL_VERSION); u32 count;
//   u32 pool_count; u32 pool_offset
//   count x { u64 key (rsmsl_key); u64 key2 (second hash of the pair); u32 vs_len; u32 fs_len;
//             u32 offset; u32 size; u32 flags; u32 check }                sorted by key
//   entries at `offset`: u32 n; n x u32 pool index        the MSL, as concatenated pool strings
//            (prelude / vertex part / fragment part, shared between programs);
//            u32 n; n x { str name; i32 location; i32 type }                       attributes
//            u32 n; n x { str name; i32 type, array_size, stages, vs_offset, fs_offset, stride }
//            u32 n; n x { str name; i32 type, stages, index }                       samplers
//            i32 vs_uniform_size, fs_uniform_size
//   at pool_offset: pool_count x str
//   str = u32 length + bytes (no terminator)
//   check = low 32 bits of the hash of the entry's bytes after the pool indices, then its MSL
//            (so merging corpora, which renumbers the pool, keeps it)
// A lookup hashes the two sources once (key, key2 and both lengths must match: 128 bits), binary
// searches the sorted index in place (mmap), and decodes the entry after checking `check` and
// every offset, count and reflection value. Anything that doesn't check out (another version, a
// truncated or corrupt file or entry) is ignored: the program is translated at run time.
#include "msl_internal.h"

#include <dlfcn.h>
#include <stdarg.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace {

const char kMagic[8] = {'R', 'S', 'M', 'S', 'L', 'C', '3', 0};
const size_t kHeader = 24;

struct IndexEntry {
    uint64_t key, key2;
    uint32_t vs_len, fs_len, offset, size, flags, check;
};
static_assert(sizeof(IndexEntry) == 40, "corpus index layout");

// --- Hash (rsmsl_key, the corpus index and entry checks) -------------------------------------------
// MurmurHash3 x64-128's block and finalisation steps, 16 bytes at a time: ~1 us for a typical
// pair (FNV-1a, byte at a time, took 6). Each string is followed by its length, so the split
// between the two sources counts. Deterministic: no seed, no pointers; little-endian loads.
struct Hasher {
    uint64_t h1 = 0x9e3779b97f4a7c15ull, h2 = 0x6a09e667f3bcc909ull, total = 0;
    static uint64_t rotl(uint64_t x, int r) { return (x << r) | (x >> (64 - r)); }
    static uint64_t fmix(uint64_t k) {
        k ^= k >> 33; k *= 0xff51afd7ed558ccdull;
        k ^= k >> 33; k *= 0xc4ceb9fe1a85ec53ull;
        return k ^ (k >> 33);
    }
    void block(uint64_t k1, uint64_t k2) {
        const uint64_t c1 = 0x87c37b91114253d5ull, c2 = 0x4cf5ad432745937full;
        k1 *= c1; k1 = rotl(k1, 31); k1 *= c2; h1 ^= k1;
        h1 = rotl(h1, 27); h1 += h2; h1 = h1 * 5 + 0x52dce729;
        k2 *= c2; k2 = rotl(k2, 33); k2 *= c1; h2 ^= k2;
        h2 = rotl(h2, 31); h2 += h1; h2 = h2 * 5 + 0x38495ab5;
    }
    void add(const void *data, size_t n) {
        const unsigned char *p = (const unsigned char *)data;
        total += n;
        for (; n >= 16; p += 16, n -= 16) {
            uint64_t a, b;
            memcpy(&a, p, 8);
            memcpy(&b, p + 8, 8);
            block(a, b);
        }
        if (n) {
            uint64_t t[2] = {0, 0};
            memcpy(t, p, n);
            block(t[0], t[1] ^ 0x8000000000000000ull);
        }
    }
    void end_string(size_t n) { block(n, 0xa5a5a5a5a5a5a5a5ull); }
    void finish(uint64_t *a, uint64_t *b) {
        uint64_t x = h1 ^ total, y = h2 ^ total;
        x += y; y += x;
        x = fmix(x); y = fmix(y);
        x += y; y += x;
        *a = x;
        if (b) *b = y;
    }
};

struct PairKey {
    uint64_t key, key2;
    size_t vs_len, fs_len;
};
PairKey pair_key(const char *vs, const char *fs) {
    PairKey k;
    Hasher h;
    k.vs_len = strlen(vs ? vs : "");
    k.fs_len = strlen(fs ? fs : "");
    h.add(vs ? vs : "", k.vs_len);
    h.end_string(k.vs_len);
    h.add(fs ? fs : "", k.fs_len);
    h.end_string(k.fs_len);
    h.finish(&k.key, &k.key2);
    return k;
}

// --- Reading -------------------------------------------------------------------------------------

struct Corpus {
    std::once_flag once;
    bool overridden = false;
    std::string override_path;
    std::string path;
    const unsigned char *data = nullptr;
    size_t size = 0;
    const IndexEntry *index = nullptr;
    uint32_t count = 0;
    std::vector<std::pair<uint32_t, uint32_t>> pool;  // offset, length
};
Corpus g;  // written once (init, under call_once), then read-only: lookups need no lock

bool log_on() {
    static const bool on = [] {
        const char *e = getenv("RS_MSL_LOG");
        return e && *e && strcmp(e, "0");
    }();
    return on;
}
void logf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void logf(const char *fmt, ...) {
    if (!log_on()) return;
    va_list ap;
    va_start(ap, fmt);
    fputs("[rs-msl] ", stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}

bool try_open(const std::string &path) {
    int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0) return false;
    struct stat st;
    if (fstat(fd, &st) || st.st_size < (off_t)kHeader || st.st_size > (off_t)UINT32_MAX) {
        close(fd);
        logf("ignoring corpus %s (size)", path.c_str());
        return false;
    }
    size_t size = (size_t)st.st_size;
    void *m = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (m == MAP_FAILED) return false;
    const unsigned char *d = (const unsigned char *)m;
    uint32_t ver, count, pool_count, pool_off;
    memcpy(&ver, d + 8, 4);
    memcpy(&count, d + 12, 4);
    memcpy(&pool_count, d + 16, 4);
    memcpy(&pool_off, d + 20, 4);
    const char *why = nullptr;
    if (memcmp(d, kMagic, 8) || ver != RSMSL_VERSION) why = "another format or translator version";
    else if (kHeader + (uint64_t)count * sizeof(IndexEntry) > size || pool_off > size || pool_count > size / 4) why = "truncated";
    std::vector<std::pair<uint32_t, uint32_t>> pool;
    for (uint64_t o = pool_off, i = 0; !why && i < pool_count; i++) {
        uint32_t n;
        if (o + 4 > size) { why = "truncated pool"; break; }
        memcpy(&n, d + o, 4);
        if (o + 4 + n > size) { why = "truncated pool"; break; }
        pool.push_back({(uint32_t)(o + 4), n});
        o += 4 + n;
    }
    const IndexEntry *index = (const IndexEntry *)(d + kHeader);
    for (uint32_t i = 0; !why && i < count; i++) {
        if (i && index[i].key <= index[i - 1].key) why = "index not sorted";
        else if ((uint64_t)index[i].offset + index[i].size > size) why = "entry out of range";
    }
    if (why) {
        logf("ignoring corpus %s (%s)", path.c_str(), why);
        munmap(m, size);
        return false;
    }
    g.path = path;
    g.data = d;
    g.size = size;
    g.index = index;
    g.count = count;
    g.pool = std::move(pool);
    logf("corpus %s: %u programs", path.c_str(), count);
    return true;
}

void init() {
    std::call_once(g.once, [] {
        if (g.overridden) {
            if (!g.override_path.empty()) try_open(g.override_path);
            return;
        }
        if (const char *e = getenv("RS_MSL_CORPUS")) {
            if (*e) try_open(e);
            return;
        }
        Dl_info info;
        if (dladdr((const void *)&rsmsl_translate, &info) && info.dli_fname) {
            std::string dir = info.dli_fname;
            size_t sl = dir.rfind('/');
            dir = sl == std::string::npos ? "." : dir.substr(0, sl);
            if (try_open(dir + "/rsmsl-corpus.bin")) return;
            if (try_open(dir + "/../Resources/rsmsl-corpus.bin")) return;
        }
        logf("no shader corpus found: every program is translated at run time");
    });
}

const IndexEntry *find(const PairKey &k) {
    init();
    if (!g.count) return nullptr;
    const IndexEntry *lo = g.index, *hi = g.index + g.count;
    const IndexEntry *it = std::lower_bound(lo, hi, k.key, [](const IndexEntry &e, uint64_t x) { return e.key < x; });
    if (it == hi || it->key != k.key || it->key2 != k.key2 || it->vs_len != k.vs_len || it->fs_len != k.fs_len) return nullptr;
    return it;
}

struct Reader {
    const unsigned char *p, *end;
    bool ok = true;
    uint32_t u32() {
        if (end - p < 4) { ok = false; return 0; }
        uint32_t v;
        memcpy(&v, p, 4);
        p += 4;
        return v;
    }
    int32_t i32() { return (int32_t)u32(); }
    char *str() {  // NULL (and !ok) if it doesn't fit, is empty, too long or holds a 0 byte
        uint32_t n = u32();
        if (!ok || (size_t)(end - p) < n || n == 0 || n > 1024 || memchr(p, 0, n)) { ok = false; return nullptr; }
        char *s = (char *)malloc(n + 1);
        if (!s) { ok = false; return nullptr; }
        memcpy(s, p, n);
        s[n] = 0;
        p += n;
        return s;
    }
};

bool is_one_of(int v, std::initializer_list<int> l) {
    for (int x : l)
        if (v == x) return true;
    return false;
}
bool sampler_type(int t) { return is_one_of(t, {0x8B5D, RSMSL_SAMPLER_2D, 0x8B5F, RSMSL_SAMPLER_CUBE, 0x8B63}); }
bool uniform_type(int t) {
    return sampler_type(t) || is_one_of(t, {RSMSL_FLOAT, RSMSL_VEC2, RSMSL_VEC3, RSMSL_VEC4, RSMSL_INT, RSMSL_IVEC2, RSMSL_IVEC3,
                                            RSMSL_IVEC4, RSMSL_BOOL, 0x8B57, 0x8B58, 0x8B59, RSMSL_MAT2, RSMSL_MAT3, RSMSL_MAT4,
                                            0x8B65, 0x8B66, 0x8B67, 0x8B68, 0x8B69, 0x8B6A});
}

// The reflection must be something the translator could have produced: the core indexes its
// uniform blocks, vertex buffers and texture slots with these values.
bool valid(const rsmsl_program *p) {
    const int kMaxBlock = 1 << 20;
    for (int s : {p->vs_uniform_size, p->fs_uniform_size})
        if (s < 0 || s > kMaxBlock || s % 16) return false;
    unsigned locs = 0;
    for (int i = 0; i < p->n_attribs; i++) {
        const rsmsl_attrib &a = p->attribs[i];
        if (a.location < 0 || a.location >= 16 || (locs & (1u << a.location))) return false;
        locs |= 1u << a.location;
        if (!is_one_of(a.type, {RSMSL_FLOAT, RSMSL_VEC2, RSMSL_VEC3, RSMSL_VEC4})) return false;
    }
    for (int i = 0; i < p->n_uniforms; i++) {
        const rsmsl_uniform &u = p->uniforms[i];
        if (!uniform_type(u.type) || u.array_size < 1 || u.array_size > (1 << 16) || u.stages < 1 || u.stages > 3) return false;
        if (sampler_type(u.type)) {
            if (u.vs_offset != -1 || u.fs_offset != -1 || u.stride != 0) return false;
            continue;
        }
        if (u.stride < 4 || u.stride > 64) return false;
        int offs[2] = {u.vs_offset, u.fs_offset}, sizes[2] = {p->vs_uniform_size, p->fs_uniform_size};
        for (int st = 0; st < 2; st++) {
            if (!(u.stages & (1 << st))) {
                if (offs[st] != -1) return false;
            } else if (offs[st] < 0 || (int64_t)offs[st] + (int64_t)u.stride * u.array_size > sizes[st]) {
                return false;
            }
        }
    }
    unsigned slots = 0;
    for (int i = 0; i < p->n_samplers; i++) {
        const rsmsl_sampler &s = p->samplers[i];
        if (!sampler_type(s.type) || s.stages < 1 || s.stages > 3 || s.index < 0 || s.index >= 16 || (slots & (1u << s.index)))
            return false;
        slots |= 1u << s.index;
    }
    return true;
}

rsmsl_program *decode(const IndexEntry *e) {
    const unsigned char *blob = g.data + e->offset;
    Reader r{blob, blob + e->size};
    rsmsl_program *p = (rsmsl_program *)calloc(1, sizeof *p);
    if (!p) return nullptr;
    // MSL: at most a few pool pieces; checked against `check` with the entry's bytes.
    uint32_t n = r.u32();
    uint32_t piece[16];
    uint64_t len = 0;
    if (n > 16) r.ok = false;
    for (uint32_t i = 0; r.ok && i < n; i++) {
        piece[i] = r.u32();
        if (piece[i] >= g.pool.size()) r.ok = false;
        else len += g.pool[piece[i]].second;
    }
    if (r.ok) {
        Hasher h;
        h.add(r.p, (size_t)(r.end - r.p));
        for (uint32_t i = 0; i < n; i++) h.add(g.data + g.pool[piece[i]].first, g.pool[piece[i]].second);
        uint64_t c;
        h.finish(&c, nullptr);
        if ((uint32_t)c != e->check) r.ok = false;
    }
    if (r.ok && (p->msl = (char *)malloc(len + 1))) {
        char *o = p->msl;
        for (uint32_t i = 0; i < n; i++) {
            memcpy(o, g.data + g.pool[piece[i]].first, g.pool[piece[i]].second);
            o += g.pool[piece[i]].second;
        }
        *o = 0;
        if (strlen(p->msl) != len) r.ok = false;
    } else {
        r.ok = false;
    }
    n = r.u32();
    if (n > 16) r.ok = false;
    if (r.ok) p->attribs = (rsmsl_attrib *)calloc(n ? n : 1, sizeof(rsmsl_attrib));
    for (uint32_t i = 0; r.ok && p->attribs && i < n; i++) {
        rsmsl_attrib &a = p->attribs[p->n_attribs];
        if (!(a.name = r.str())) break;
        p->n_attribs++;
        a.location = r.i32();
        a.type = r.i32();
    }
    n = r.u32();
    if (n > 4096) r.ok = false;
    if (r.ok) p->uniforms = (rsmsl_uniform *)calloc(n ? n : 1, sizeof(rsmsl_uniform));
    for (uint32_t i = 0; r.ok && p->uniforms && i < n; i++) {
        rsmsl_uniform &u = p->uniforms[p->n_uniforms];
        if (!(u.name = r.str())) break;
        p->n_uniforms++;
        u.type = r.i32();
        u.array_size = r.i32();
        u.stages = r.i32();
        u.vs_offset = r.i32();
        u.fs_offset = r.i32();
        u.stride = r.i32();
    }
    n = r.u32();
    if (n > 16) r.ok = false;
    if (r.ok) p->samplers = (rsmsl_sampler *)calloc(n ? n : 1, sizeof(rsmsl_sampler));
    for (uint32_t i = 0; r.ok && p->samplers && i < n; i++) {
        rsmsl_sampler &s = p->samplers[p->n_samplers];
        if (!(s.name = r.str())) break;
        p->n_samplers++;
        s.type = r.i32();
        s.stages = r.i32();
        s.index = r.i32();
    }
    p->vs_uniform_size = r.i32();
    p->fs_uniform_size = r.i32();
    if (!r.ok || !p->attribs || !p->uniforms || !p->samplers || r.p != r.end || !valid(p)) {
        logf("corrupt corpus entry %016llx: translating at run time", (unsigned long long)e->key);
        rsmsl_free(p);
        return nullptr;
    }
    p->from_corpus = 1;
    return p;
}

// The corpus entry's attribute locations are what the translator would assign with these
// bindings (bound by name, built-ins fixed, the rest lowest free in declaration order).
bool bindings_match(const rsmsl_program *p, const rsmsl_attrib *bound, int n_bound) {
    unsigned taken = 0;
    std::vector<int> want(p->n_attribs, -1);
    for (int i = 0; i < p->n_attribs; i++) {
        const rsmsl_attrib &a = p->attribs[i];
        if (!strncmp(a.name, "gl_", 3)) want[i] = a.location;
        else
            for (int j = 0; j < n_bound; j++)  // the last binding of a name wins, as in the translator
                if (bound[j].name && !strcmp(bound[j].name, a.name)) want[i] = bound[j].location;
        if (want[i] >= 0) {
            if (want[i] >= 16 || (taken & (1u << want[i]))) return false;
            taken |= 1u << want[i];
        }
    }
    for (int i = 0; i < p->n_attribs; i++) {
        if (want[i] < 0) {
            int l = 0;
            while (l < 16 && (taken & (1u << l))) l++;
            if (l == 16) return false;
            taken |= 1u << l;
            want[i] = l;
        }
        if (want[i] != p->attribs[i].location) return false;
    }
    return true;
}

// --- Writing -------------------------------------------------------------------------------------

struct WEntry {
    PairKey k;
    uint32_t flags;
    std::vector<std::string> segs;  // MSL pieces, pooled at write time
    std::string blob;               // reflection
};

// Prelude / vertex part / fragment part: identical across many programs.
std::vector<std::string> split_msl(const std::string &m) {
    std::vector<std::string> v;
    size_t a = m.find("\nstruct rs_vs_"), b = a == std::string::npos ? a : m.find("\nstruct rs_fs_", a);
    if (a == std::string::npos || b == std::string::npos) return {m};
    return {m.substr(0, a), m.substr(a, b - a), m.substr(b)};
}

void put_u32(std::string &b, uint32_t v) { b.append((const char *)&v, 4); }
void put_str(std::string &b, const char *s) {
    uint32_t n = (uint32_t)strlen(s ? s : "");
    put_u32(b, n);
    b.append(s ? s : "", n);
}

}  // namespace

struct rsmsl_corpus_writer {
    std::vector<WEntry> entries;
    std::map<std::pair<uint64_t, uint64_t>, size_t> by_key;
};

extern "C" uint64_t rsmsl_key(const char *vs, const char *fs) { return pair_key(vs, fs).key; }

extern "C" rsmsl_program *rsmsl_translate(const char *vs_glsl, const char *fs_glsl,
                                          const rsmsl_attrib *bound_attribs, int n_bound, char **error) {
    if (error) *error = nullptr;
    PairKey k = pair_key(vs_glsl, fs_glsl);
    if (const IndexEntry *e = find(k)) {
        rsmsl_program *p = decode(e);
        if (p && bindings_match(p, bound_attribs, n_bound)) return p;
        if (p) logf("corpus program %016llx has other attribute bindings: translating again", (unsigned long long)k.key);
        rsmsl_free(p);
    }
    rsmsl_program *p = rsmsl_translate_direct(vs_glsl, fs_glsl, bound_attribs, n_bound, error);
    if (p) logf("translated program %016llx at run time (not in the corpus)", (unsigned long long)k.key);
    else logf("translation of program %016llx failed: %s", (unsigned long long)k.key, error && *error ? *error : "?");
    return p;
}

extern "C" void rsmsl_corpus_override(const char *path) {
    g.overridden = true;
    g.override_path = path ? path : "";
}

extern "C" const char *rsmsl_corpus_path(int *n_entries) {
    init();
    if (n_entries) *n_entries = (int)g.count;
    return g.count ? g.path.c_str() : nullptr;
}

extern "C" int rsmsl_corpus_count(void) {
    init();
    return (int)g.count;
}

extern "C" char *rsmsl_corpus_msl(int i) {
    init();
    if (i < 0 || (uint32_t)i >= g.count) return nullptr;
    // The whole entry is checked (its hash covers the reflection too), then only the MSL is kept.
    rsmsl_program *p = decode(&g.index[i]);
    if (!p) return nullptr;
    char *msl = p->msl;
    p->msl = nullptr;
    rsmsl_free(p);
    return msl;
}

extern "C" int rsmsl_corpus_flags(const char *vs, const char *fs) {
    const IndexEntry *e = find(pair_key(vs, fs));
    return e ? (int)e->flags : -1;
}

extern "C" rsmsl_corpus_writer *rsmsl_corpus_begin(void) { return new rsmsl_corpus_writer; }

extern "C" void rsmsl_corpus_add(rsmsl_corpus_writer *w, const char *vs, const char *fs,
                                 const rsmsl_program *p, unsigned flags) {
    WEntry e;
    e.k = pair_key(vs, fs);
    e.flags = flags;
    std::string &b = e.blob;
    e.segs = split_msl(p->msl);
    put_u32(b, (uint32_t)p->n_attribs);
    for (int i = 0; i < p->n_attribs; i++) {
        put_str(b, p->attribs[i].name);
        put_u32(b, (uint32_t)p->attribs[i].location);
        put_u32(b, (uint32_t)p->attribs[i].type);
    }
    put_u32(b, (uint32_t)p->n_uniforms);
    for (int i = 0; i < p->n_uniforms; i++) {
        const rsmsl_uniform &u = p->uniforms[i];
        put_str(b, u.name);
        for (int v : {u.type, u.array_size, u.stages, u.vs_offset, u.fs_offset, u.stride}) put_u32(b, (uint32_t)v);
    }
    put_u32(b, (uint32_t)p->n_samplers);
    for (int i = 0; i < p->n_samplers; i++) {
        const rsmsl_sampler &s = p->samplers[i];
        put_str(b, s.name);
        for (int v : {s.type, s.stages, s.index}) put_u32(b, (uint32_t)v);
    }
    put_u32(b, (uint32_t)p->vs_uniform_size);
    put_u32(b, (uint32_t)p->fs_uniform_size);
    auto it = w->by_key.find({e.k.key, e.k.key2});
    if (it != w->by_key.end()) {
        w->entries[it->second] = std::move(e);
        return;
    }
    w->by_key[{e.k.key, e.k.key2}] = w->entries.size();
    w->entries.push_back(std::move(e));
}

extern "C" int rsmsl_corpus_finish(rsmsl_corpus_writer *w, const char *path) {
    std::sort(w->entries.begin(), w->entries.end(), [](const WEntry &a, const WEntry &b) { return a.k.key < b.k.key; });
    for (size_t i = 1; i < w->entries.size(); i++)
        if (w->entries[i].k.key == w->entries[i - 1].k.key) {  // a 64-bit collision: the index needs unique keys
            fprintf(stderr, "rsmsl_corpus_finish: two programs with key %016llx\n", (unsigned long long)w->entries[i].k.key);
            delete w;
            return -1;
        }
    std::map<std::string, uint32_t> pool_idx;
    std::string pool;
    uint32_t off = (uint32_t)(kHeader + w->entries.size() * sizeof(IndexEntry));
    std::string index, blobs;
    for (auto &e : w->entries) {
        std::string b;
        put_u32(b, (uint32_t)e.segs.size());
        for (auto &sg : e.segs) {
            auto it = pool_idx.find(sg);
            if (it == pool_idx.end()) {
                it = pool_idx.emplace(sg, (uint32_t)pool_idx.size()).first;
                put_str(pool, sg.c_str());
            }
            put_u32(b, it->second);
        }
        b += e.blob;
        Hasher h;
        h.add(e.blob.data(), e.blob.size());
        for (auto &sg : e.segs) h.add(sg.data(), sg.size());
        uint64_t check;
        h.finish(&check, nullptr);
        IndexEntry ie = {e.k.key, e.k.key2, (uint32_t)e.k.vs_len, (uint32_t)e.k.fs_len, off + (uint32_t)blobs.size(),
                         (uint32_t)b.size(), e.flags, (uint32_t)check};
        index.append((const char *)&ie, sizeof ie);
        blobs += b;
    }
    std::string head(kMagic, 8);
    put_u32(head, RSMSL_VERSION);
    put_u32(head, (uint32_t)w->entries.size());
    put_u32(head, (uint32_t)pool_idx.size());
    put_u32(head, off + (uint32_t)blobs.size());
    blobs += pool;
    delete w;
    std::string tmp = std::string(path) + ".tmp";
    FILE *f = fopen(tmp.c_str(), "wb");
    if (!f) return -1;
    bool ok = fwrite(head.data(), 1, head.size(), f) == head.size() && fwrite(index.data(), 1, index.size(), f) == index.size() &&
              fwrite(blobs.data(), 1, blobs.size(), f) == blobs.size();
    ok = fclose(f) == 0 && ok;
    if (!ok || rename(tmp.c_str(), path)) { unlink(tmp.c_str()); return -1; }
    return 0;
}
