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
// File format (little-endian):
//   char magic[8] "RSMSLC2\0"; u32 translator version (RSMSL_VERSION); u32 count;
//   u32 pool_count; u32 pool_offset
//   count x { u64 key (rsmsl_key); u32 vs_len; u32 fs_len; u32 offset; u32 size; u32 flags; u32 0 }
//            sorted by key
//   entries at `offset`: u32 n; n x u32 pool index        the MSL, as concatenated pool strings
//            (prelude / vertex part / fragment part, shared between programs);
//            u32 n; n x { str name; i32 location; i32 type }                       attributes
//            u32 n; n x { str name; i32 type, array_size, stages, vs_offset, fs_offset, stride }
//            u32 n; n x { str name; i32 type, stages, index }                       samplers
//            i32 vs_uniform_size, fs_uniform_size
//   str = u32 length + bytes (no terminator)
// A corpus from another translator version is ignored.
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

const char kMagic[8] = {'R', 'S', 'M', 'S', 'L', 'C', '2', 0};
const size_t kHeader = 24;

struct IndexEntry {
    uint64_t key;
    uint32_t vs_len, fs_len, offset, size, flags, pad;
};
static_assert(sizeof(IndexEntry) == 32, "corpus index layout");

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
Corpus g;

bool log_on() {
    static int on = -1;
    if (on < 0) {
        const char *e = getenv("RS_MSL_LOG");
        on = e && *e && strcmp(e, "0") ? 1 : 0;
    }
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
    if (fstat(fd, &st) || st.st_size < (off_t)kHeader) { close(fd); return false; }
    void *m = mmap(nullptr, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (m == MAP_FAILED) return false;
    const unsigned char *d = (const unsigned char *)m;
    uint32_t ver, count, pool_count, pool_off;
    memcpy(&ver, d + 8, 4);
    memcpy(&count, d + 12, 4);
    memcpy(&pool_count, d + 16, 4);
    memcpy(&pool_off, d + 20, 4);
    bool ok = !memcmp(d, kMagic, 8) && ver == RSMSL_VERSION && kHeader + (uint64_t)count * sizeof(IndexEntry) <= (uint64_t)st.st_size;
    std::vector<std::pair<uint32_t, uint32_t>> pool;
    for (uint64_t o = pool_off, i = 0; ok && i < pool_count; i++) {
        uint32_t n;
        if (o + 4 > (uint64_t)st.st_size) { ok = false; break; }
        memcpy(&n, d + o, 4);
        if (o + 4 + n > (uint64_t)st.st_size) { ok = false; break; }
        pool.push_back({(uint32_t)(o + 4), n});
        o += 4 + n;
    }
    if (!ok) {
        logf("ignoring corpus %s (%s)", path.c_str(), memcmp(d, kMagic, 8) ? "not a corpus" : "other translator version");
        munmap(m, (size_t)st.st_size);
        return false;
    }
    g.path = path;
    g.data = d;
    g.size = (size_t)st.st_size;
    g.index = (const IndexEntry *)(d + kHeader);
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

const IndexEntry *find(const char *vs, const char *fs) {
    init();
    if (!g.count) return nullptr;
    uint64_t key = rsmsl_key(vs, fs);
    const IndexEntry *lo = g.index, *hi = g.index + g.count;
    const IndexEntry *it = std::lower_bound(lo, hi, key, [](const IndexEntry &e, uint64_t k) { return e.key < k; });
    if (it == hi || it->key != key) return nullptr;
    if (it->vs_len != strlen(vs ? vs : "") || it->fs_len != strlen(fs ? fs : "")) return nullptr;
    if ((uint64_t)it->offset + it->size > g.size) return nullptr;
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
    char *str() {
        uint32_t n = u32();
        if (!ok || (size_t)(end - p) < n) { ok = false; return strdup(""); }
        char *s = (char *)malloc(n + 1);
        memcpy(s, p, n);
        s[n] = 0;
        p += n;
        return s;
    }
};

rsmsl_program *decode(const IndexEntry *e) {
    Reader r{g.data + e->offset, g.data + e->offset + e->size};
    rsmsl_program *p = (rsmsl_program *)calloc(1, sizeof *p);
    uint32_t n = r.u32();
    std::string msl;
    for (uint32_t i = 0; r.ok && i < n; i++) {
        uint32_t k = r.u32();
        if (k >= g.pool.size()) { r.ok = false; break; }
        msl.append((const char *)g.data + g.pool[k].first, g.pool[k].second);
    }
    p->msl = strdup(msl.c_str());
    n = r.u32();
    if (n > 64) r.ok = false;
    p->attribs = (rsmsl_attrib *)calloc(r.ok && n ? n : 1, sizeof(rsmsl_attrib));
    for (uint32_t i = 0; r.ok && i < n; i++, p->n_attribs++) {
        p->attribs[i].name = r.str();
        p->attribs[i].location = r.i32();
        p->attribs[i].type = r.i32();
    }
    n = r.u32();
    if (n > 4096) r.ok = false;
    p->uniforms = (rsmsl_uniform *)calloc(r.ok && n ? n : 1, sizeof(rsmsl_uniform));
    for (uint32_t i = 0; r.ok && i < n; i++, p->n_uniforms++) {
        rsmsl_uniform &u = p->uniforms[i];
        u.name = r.str();
        u.type = r.i32();
        u.array_size = r.i32();
        u.stages = r.i32();
        u.vs_offset = r.i32();
        u.fs_offset = r.i32();
        u.stride = r.i32();
    }
    n = r.u32();
    if (n > 256) r.ok = false;
    p->samplers = (rsmsl_sampler *)calloc(r.ok && n ? n : 1, sizeof(rsmsl_sampler));
    for (uint32_t i = 0; r.ok && i < n; i++, p->n_samplers++) {
        rsmsl_sampler &s = p->samplers[i];
        s.name = r.str();
        s.type = r.i32();
        s.stages = r.i32();
        s.index = r.i32();
    }
    p->vs_uniform_size = r.i32();
    p->fs_uniform_size = r.i32();
    if (!r.ok) {
        logf("corrupt corpus entry %016llx", (unsigned long long)e->key);
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
        for (int j = 0; j < n_bound; j++)
            if (want[i] < 0 && bound[j].name && !strcmp(bound[j].name, a.name)) want[i] = bound[j].location;
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

struct WEntry {
    uint64_t key;
    uint32_t vs_len, fs_len, flags;
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
};

extern "C" rsmsl_program *rsmsl_translate(const char *vs_glsl, const char *fs_glsl,
                                          const rsmsl_attrib *bound_attribs, int n_bound, char **error) {
    if (error) *error = nullptr;
    uint64_t key = rsmsl_key(vs_glsl, fs_glsl);
    if (const IndexEntry *e = find(vs_glsl, fs_glsl)) {
        rsmsl_program *p = decode(e);
        if (p && bindings_match(p, bound_attribs, n_bound)) return p;
        if (p) logf("corpus program %016llx has other attribute bindings: translating again", (unsigned long long)key);
        rsmsl_free(p);
    }
    rsmsl_program *p = rsmsl_translate_direct(vs_glsl, fs_glsl, bound_attribs, n_bound, error);
    if (p) logf("translated program %016llx at run time (not in the corpus)", (unsigned long long)key);
    else logf("translation of program %016llx failed: %s", (unsigned long long)key, error && *error ? *error : "?");
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

extern "C" int rsmsl_corpus_flags(const char *vs, const char *fs) {
    const IndexEntry *e = find(vs, fs);
    return e ? (int)e->flags : -1;
}

extern "C" rsmsl_corpus_writer *rsmsl_corpus_begin(void) { return new rsmsl_corpus_writer; }

extern "C" void rsmsl_corpus_add(rsmsl_corpus_writer *w, const char *vs, const char *fs,
                                 const rsmsl_program *p, unsigned flags) {
    WEntry e;
    e.key = rsmsl_key(vs, fs);
    e.vs_len = (uint32_t)strlen(vs ? vs : "");
    e.fs_len = (uint32_t)strlen(fs ? fs : "");
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
    for (auto &x : w->entries)
        if (x.key == e.key) { x = std::move(e); return; }
    w->entries.push_back(std::move(e));
}

extern "C" int rsmsl_corpus_finish(rsmsl_corpus_writer *w, const char *path) {
    std::sort(w->entries.begin(), w->entries.end(), [](const WEntry &a, const WEntry &b) { return a.key < b.key; });
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
        IndexEntry ie = {e.key, e.vs_len, e.fs_len, off + (uint32_t)blobs.size(), (uint32_t)b.size(), e.flags, 0};
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
