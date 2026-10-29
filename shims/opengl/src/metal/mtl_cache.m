// Metal core: shader library and pipeline cache (docs/METAL-PLAN.md step 7).
//
// In memory, per device (shared by its contexts): MSL text -> library + rs_vs + rs_fs variants,
// and per library rsm_pkey -> MTLRenderPipelineState. Each item is built once; a thread that
// needs an item another thread is building waits for it instead of compiling it again.
//
// Library compiles (newLibraryWithSource, 0.1-0.5 s each when the system's Metal cache is cold)
// run on a pool of worker threads ("Compile pool" below): glLinkProgram queues the compile and
// returns; the first pipeline needed for that library waits for it, or compiles it on the calling
// thread if no worker has started it yet. Warming (below) uses the same pool at a lower priority.
// rs_gfx_prepare (the shell's safe load, before the game starts loading) waits for warming on the
// whole pool and then throttles the pool for the rest of the run ("Safe load" below); on a first
// run it also compiles the offline shader corpus's libraries ("Corpus warm" below).
//
// On disk (one directory per cache format, core conventions, OS build and GPU; not per build of
// libRSOpenGL: rebuilding the app keeps the cache, and libraries are keyed by their MSL text):
//   archive-<seq>.bin  MTLBinaryArchives with the GPU code of the pipelines built so far. New
//                  pipeline descriptors look there first (MTLPipelineOptionFailOnBinaryArchiveMiss
//                  tells a hit from a miss); a miss is compiled as usual and added to a fresh
//                  archive afterwards, on a background queue. See "Archives" below for why there
//                  are several files.
//   manifest.bin   append-only records: the MSL of each library and the rsm_pkey of each pipeline,
//                  in first-use order, plus a mark per run and per library the game linked in it.
//                  At the next start the compile pool compiles those libraries and prebuilds the
//                  pipelines, so the game's first use finds them done ("warming"). Libraries the
//                  game has not linked in the last RSM_KEEP_RUNS runs (an older translator's MSL)
//                  are dropped then (the manifest is rewritten). newLibraryWithSource has no binary
//                  form we can keep (only MTLDynamicLibrary serialises, and it cannot hold
//                  vertex/fragment functions), so warming is what hides the MSL front-end compile;
//                  the system's Metal cache makes repeated compiles of the same source cheap too,
//                  but may be evicted.
//   corpus-warmed  the identity of the last shader corpus compiled completely by a safe load's
//                  corpus warm here (see "Corpus warm").
//
// Archive access: adds and serialisation run on one serial queue (concurrent add + serialise
// crashes in Metal); lookups from pipeline creation run on any thread (concurrent with both, as
// checked under stress). The fresh archive is written every RSM_SERIALIZE_EVERY new pipelines, a
// few seconds after the last new one, when the last context goes and at exit; to a temporary file
// renamed into place, so a crash leaves the previous files.
//
// Corrupt or foreign files are discarded (bad manifest records: the file is cut at the first one;
// an archive Metal rejects: deleted). Disk use: the directories of other formats, conventions, OS
// versions and GPUs are deleted at start (only our own files in them); the manifest stops growing
// at RSM_MAX_MANIFEST bytes, the archives at RSM_MAX_ARCHIVE (over it at start: deleted), and more
// than RSM_MAX_ARCHIVE_FILES archive files are compacted into one during warming.
//
// Environment:
//   RS_METAL_CACHE=0          no disk cache (and no warming); 1: on with the default directory
//                             even outside an app bundle
//   RS_METAL_CACHE_DIR=<dir>  cache directory (default ~/Library/Caches/rs-rearmed/metal when the
//                             process runs from an .app bundle; off otherwise, so tools and tests
//                             never write there unless asked)
//   RS_METAL_CACHE_STATS=1    print counters (hits, misses, compile times) when the last context
//                             goes and at exit; 2: also log every archive hit and miss
//   RS_METAL_CACHE_WARM_SYNC=1  wait for warming before the first context returns (tests)
//   RS_MTL_COMPILE_THREADS=<n>  compile pool size (default: CPUs - 2, 2..8); 0: every library is
//                             compiled synchronously in glLinkProgram (the old behaviour; benchmarks).
//                             Also the throttled pool's size after rs_gfx_prepare (default 2)
//   RS_MTL_SALT=<text>        diagnostics only: append "// salt <text>" to every MSL, so the system's
//                             Metal cache cannot help (cold-compile measurements)
//   RS_MTL_SALT_COMPILE=<text>  diagnostics only: the same, but only in the source handed to the
//                             compiler (cache keys and the manifest keep the plain MSL), so a run
//                             can meet a known manifest with a cold system Metal cache
//   RS_METAL_CORPUS_LIMIT=<n>  tests only: warm at most the first n corpus entries
//   RS_METAL_CORPUS_WARM=0|1  safe load's corpus warm: never / always (default: by the rule in
//                             "Corpus warm")
//   RS_MTL_TEST_FAIL=<text>   tests only: MSL containing <text> gets an #error (compile failure path)
//   RS_METAL_CACHE_KEEP_RUNS=<n>  tests only: RSM_KEEP_RUNS
//   RS_METAL_CORPUS_MIN_MANIFEST=<n>  tests only: RSM_CORPUS_MIN_MANIFEST
#include "mtl_internal.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread/qos.h>
#include <mach/mach_time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <unistd.h>

enum {
    RSM_CACHE_VERSION = 3,       // bump when the manifest format or the rsm_pkey layout changes
    // Bump when the core's pipeline or shader conventions change in a way the cache cannot see
    // (rsm_pipeline_desc maps an rsm_pkey differently, bindings move, ...): the new value starts
    // a new cache directory. MSL changes need no bump: libraries are keyed by their text.
    RSM_CORE_CONVENTIONS = 1,
    RSM_KEEP_RUNS = 8,           // manifest: drop libraries not linked in this many runs
    RSM_SERIALIZE_EVERY = 32,    // new pipelines between archive writes
    RSM_SERIALIZE_DELAY_MS = 3000,
    RSM_MAX_THREADS = 8,
    RSM_MAX_BOOST_THREADS = 16,  // the pool while rs_gfx_prepare waits (nothing else runs then)
    // Safe load: the corpus is warmed when the manifest lists fewer libraries than this (a run
    // that reaches the game's menus links about 90) or no complete corpus warm is recorded.
    RSM_CORPUS_MIN_MANIFEST = 32,
    RSM_MAX_ARCHIVE_FILES = 8,
    RSM_MAX_LIBS = 8192,
    RSM_MAX_PIPES = 65536,
};
#define RSM_MAX_MANIFEST (64ull << 20)
#define RSM_MAX_ARCHIVE (1024ull << 20)

static const char kManifestMagic[8] = {'R', 'S', 'M', 'C', 'M', 'A', 'N', '1'};
// REC_RUN (8 zero bytes) starts each run's records; REC_SEEN (lib hash): the game linked that
// already-recorded library in this run (REC_LIB counts as seen too).
enum { REC_LIB = 1, REC_PIPE = 2, REC_RUN = 3, REC_SEEN = 4 };

typedef struct {
    char magic[8];
    uint32_t version;
    uint32_t pkey_size;
} manifest_header;

typedef struct {
    uint32_t type;
    uint32_t len;     // payload bytes
    uint64_t check;   // rsm_hash(payload) ^ type
} record_header;

typedef struct {
    uint64_t lib;     // library MSL hash
    rsm_pkey k;
} pipe_key;

// --- Counters ----------------------------------------------------------------------------
static struct {
    _Atomic int lib_compiled, lib_warmed, lib_reused, lib_waited, lib_failed, lib_async, lib_stolen;
    _Atomic int pipe_hits, pipe_misses, pipe_built, pipe_warmed, pipe_reused, pipe_waited;
    _Atomic uint64_t lib_ns, pipe_ns, wait_ns, warm_ns, serialize_ns;
    _Atomic int serialized, discarded, compacted;
    // Compile pool: peak of jobs running at once and the highest QoS class a job ran at, both
    // since the throttle started (rs_gfx_prepare); time spent in rs_gfx_prepare.
    int pool_peak, pool_qos;
    _Atomic uint64_t prepare_ns;
    _Atomic int corpus_warmed, corpus_failed;  // corpus warm: libraries compiled, failed
    _Atomic uint64_t corpus_ns;
    bool verbose;  // RS_METAL_CACHE_STATS=2: every hit and miss
} st;

static uint64_t rsm_now_ns(void) {
    static mach_timebase_info_data_t tb;
    if (!tb.denom)
        mach_timebase_info(&tb);
    return mach_absolute_time() * tb.numer / tb.denom;
}

@interface RSMPipeSlot : NSObject {
@public
    id<MTLRenderPipelineState> ps;
    int state;
}
@end
@implementation RSMPipeSlot
@end

@implementation RSMLibEntry
- (void)dealloc {
    rsm_map_free(&pipes);
}
@end

@interface RSMShaderCache : NSObject {
@public
    id<MTLDevice> dev;
    NSMutableDictionary<NSString *, RSMLibEntry *> *libs;
    pthread_mutex_t m;
    pthread_cond_t cv;

    // Disk (persist only).
    bool persist;
    NSString *dir;
    NSMutableArray<id<MTLBinaryArchive>> *archives;  // read-only: loaded from files or written once
    id<MTLBinaryArchive> fresh;    // receives new pipelines; nil when the size limit is reached
    NSArray<id<MTLBinaryArchive>> *lookup;  // archives + fresh, for pipeline descriptors (under m)
    NSMutableArray<NSString *> *files;
    int next_seq;
    uint64_t archive_bytes;
    bool compact;                  // too many files: compact during warming
    _Atomic bool compacting;
    dispatch_queue_t io;           // archive adds/serialisation, manifest appends
    int manifest_fd;
    uint64_t manifest_bytes;
    int n_libs_rec, n_pipes_rec;
    rsm_map recorded;              // pipe_key -> @YES (in the manifest)
    NSMutableSet<NSNumber *> *recorded_libs;
    bool run_marked;               // this run's REC_RUN is written
    NSMutableSet<NSNumber *> *linked_libs;  // hashes the game linked in this run (under m)
    int unsaved;                   // pipelines added to the archive since the last write
    uint64_t save_gen;

    // Warming.
    dispatch_group_t warm;
    _Atomic int warm_total;        // libraries queued for warming
    _Atomic int warm_done;
    _Atomic bool closing;

    // Safe load (rs_gfx_prepare): the corpus warm and the progress rs_gfx_prepare_progress reports.
    dispatch_group_t corpus;
    _Atomic int corpus_total, corpus_done;
    _Atomic bool preparing;
}
@end
@implementation RSMShaderCache
@end

static pthread_mutex_t g_caches_lock = PTHREAD_MUTEX_INITIALIZER;
static NSMutableArray<RSMShaderCache *> *g_caches;
static bool g_persist_claimed;
static int g_live_contexts;

static bool rsm_env_on(const char *name) {
    const char *e = getenv(name);
    return e && *e && strcmp(e, "0") != 0;
}

// --- Cache identity ------------------------------------------------------------------------
static void rsm_sanitize(const char *in, char *out, size_t n) {
    size_t j = 0;
    for (; *in && j + 1 < n; in++)
        out[j++] = ((*in >= 'a' && *in <= 'z') || (*in >= 'A' && *in <= 'Z') || (*in >= '0' && *in <= '9'))
                       ? *in
                       : '_';
    out[j] = 0;
}

// "rsm<version>-k<conventions>-<os build>-" (+ GPU name): directories with another prefix are stale.
static NSString *rsm_cache_prefix(void) {
    char os[64] = "unknown", oss[64];
    size_t n = sizeof os;
    if (sysctlbyname("kern.osversion", os, &n, NULL, 0))
        strcpy(os, "unknown");
    rsm_sanitize(os, oss, sizeof oss);
    return [NSString stringWithFormat:@"rsm%d-k%d-%s-", RSM_CACHE_VERSION, RSM_CORE_CONVENTIONS, oss];
}

static NSString *rsm_cache_base(void) {
    const char *e = getenv("RS_METAL_CACHE");
    if (e && !strcmp(e, "0"))
        return nil;
    const char *d = getenv("RS_METAL_CACHE_DIR");
    if (d && *d)
        return @(d);
    bool in_app = [NSBundle.mainBundle.bundlePath hasSuffix:@".app"];
    if (!in_app && !(e && !strcmp(e, "1")))
        return nil;
    NSArray *c = NSSearchPathForDirectoriesInDomains(NSCachesDirectory, NSUserDomainMask, YES);
    if (!c.count)
        return nil;
    return [c[0] stringByAppendingPathComponent:@"rs-rearmed/metal"];
}

// Remove a cache directory of ours: only our own files, then the directory (left if anything else is in it).
static void rsm_remove_cache_dir(NSString *path) {
    DIR *d = opendir(path.fileSystemRepresentation);
    if (!d)
        return;
    struct dirent *de;
    while ((de = readdir(d))) {
        if (!strncmp(de->d_name, "manifest.bin", 12) || !strncmp(de->d_name, "archive-", 8) ||
            !strcmp(de->d_name, "corpus-warmed"))
            unlink([path stringByAppendingPathComponent:@(de->d_name)].fileSystemRepresentation);
    }
    closedir(d);
    rmdir(path.fileSystemRepresentation);
}

// Every cache directory of ours but `keep` (other formats, conventions, OS builds, GPUs) goes.
static void rsm_clean_base(NSString *base, NSString *keep) {
    DIR *d = opendir(base.fileSystemRepresentation);
    if (!d)
        return;
    struct dirent *de;
    NSMutableArray *stale = [NSMutableArray new];
    while ((de = readdir(d)))
        if (de->d_type == DT_DIR && !strncmp(de->d_name, "rsm", 3) && de->d_name[3] >= '0' &&
            de->d_name[3] <= '9' && ![@(de->d_name) isEqualToString:keep])
            [stale addObject:@(de->d_name)];
    closedir(d);
    for (NSString *s in stale) {
        rsm_remove_cache_dir([base stringByAppendingPathComponent:s]);
        st.discarded++;
    }
}

// --- pkey sanity (manifest records are checksummed; this guards against a bug or a hand edit) ---
static bool rsm_color_fmt_ok(unsigned f) {
    switch (f) {
    case MTLPixelFormatInvalid: case MTLPixelFormatR8Unorm: case MTLPixelFormatRG8Unorm:
    case MTLPixelFormatRGBA8Unorm: case MTLPixelFormatRGBA8Unorm_sRGB: case MTLPixelFormatBGRA8Unorm:
    case MTLPixelFormatBGRA8Unorm_sRGB: case MTLPixelFormatR16Float: case MTLPixelFormatRG16Float:
    case MTLPixelFormatRGBA16Float: case MTLPixelFormatR32Float: case MTLPixelFormatRG32Float:
    case MTLPixelFormatRGBA32Float: case MTLPixelFormatRG11B10Float: case MTLPixelFormatRGB10A2Unorm:
        return true;
    }
    return false;
}

static bool rsm_pkey_ok(const rsm_pkey *k) {
    for (int i = 0; i < RSM_MAX_ATTRIBS; i++)
        if (k->vl.fmt[i] > 55 /* MTLVertexFormatFloatRGB9E5 */ || k->vl.step[i] > RSM_STEP_CONSTANT)
            return false;
    if (!rsm_color_fmt_ok(k->color_fmt))
        return false;
    if (k->depth_fmt != MTLPixelFormatInvalid && k->depth_fmt != MTLPixelFormatDepth32Float &&
        k->depth_fmt != MTLPixelFormatDepth16Unorm && k->depth_fmt != MTLPixelFormatDepth32Float_Stencil8)
        return false;
    if (k->stencil_fmt != MTLPixelFormatInvalid && k->stencil_fmt != MTLPixelFormatStencil8 &&
        k->stencil_fmt != MTLPixelFormatDepth32Float_Stencil8)
        return false;
    return k->blend <= 1 && k->color_mask <= 15 && k->alpha_func <= 8 &&
           k->src_rgb <= MTLBlendFactorOneMinusSource1Alpha && k->dst_rgb <= MTLBlendFactorOneMinusSource1Alpha &&
           k->src_a <= MTLBlendFactorOneMinusSource1Alpha && k->dst_a <= MTLBlendFactorOneMinusSource1Alpha &&
           k->eq_rgb <= MTLBlendOperationMax && k->eq_a <= MTLBlendOperationMax;
}

// --- Manifest ------------------------------------------------------------------------------
typedef struct {
    NSMutableArray<RSMLibEntry *> *libs;                      // in file order
    NSMutableDictionary<NSNumber *, NSMutableArray<NSData *> *> *pipes;  // lib hash -> pkeys
} warm_list;

static bool rsm_write_all(int fd, const void *p, size_t n) {
    const uint8_t *b = p;
    while (n) {
        ssize_t w = write(fd, b, n);
        if (w < 0 && errno == EINTR)
            continue;
        if (w <= 0)
            return false;
        b += w;
        n -= (size_t)w;
    }
    return true;
}

static void rsm_record(uint8_t **buf, size_t *len, size_t *cap, uint32_t type, const void *a, size_t na,
                       const void *b, size_t nb) {
    size_t n = sizeof(record_header) + na + nb;
    if (*len + n > *cap) {
        *cap = (*len + n) * 2;
        *buf = realloc(*buf, *cap);
    }
    uint8_t *r = *buf + *len;
    record_header *h = (record_header *)r;
    memcpy(r + sizeof *h, a, na);
    if (nb)
        memcpy(r + sizeof *h + na, b, nb);
    h->type = type;
    h->len = (uint32_t)(na + nb);
    h->check = rsm_hash(r + sizeof *h, na + nb) ^ type;
    *len += n;
}

// Called on c->io.
static void rsm_manifest_append(RSMShaderCache *c, uint32_t type, const void *a, size_t na, const void *b,
                                size_t nb) {
    if (c->manifest_fd < 0)
        return;
    uint8_t *buf = NULL;
    size_t n = 0, cap = 0;
    static const uint64_t zero = 0;
    if (!c->run_marked)
        rsm_record(&buf, &n, &cap, REC_RUN, &zero, 8, NULL, 0);
    rsm_record(&buf, &n, &cap, type, a, na, b, nb);
    // One write per record (O_APPEND): a crash leaves at most one torn record at the end.
    if (c->manifest_bytes + n <= RSM_MAX_MANIFEST && rsm_write_all(c->manifest_fd, buf, n)) {
        c->manifest_bytes += n;
        c->run_marked = true;
    }
    free(buf);
}

// On c->io: the game linked this library in this run.
static void rsm_note_seen(RSMShaderCache *c, uint64_t hash) {
    if ([c->recorded_libs containsObject:@(hash)])  // else REC_LIB comes with its first pipeline
        rsm_manifest_append(c, REC_SEEN, &hash, 8, NULL, 0);
}

typedef struct {
    RSMLibEntry *e;
    int last_run;  // index of the last run that linked it
} manifest_lib;

static void rsm_manifest_open(RSMShaderCache *c, warm_list *wl) {
    NSString *path = [c->dir stringByAppendingPathComponent:@"manifest.bin"];
    const char *fp = path.fileSystemRepresentation;
    int fd = open(fp, O_RDWR | O_CREAT, 0644);
    if (fd < 0) {
        rsm_log("cache: cannot open %s: %s", fp, strerror(errno));
        c->manifest_fd = -1;
        return;
    }
    struct stat sb;
    uint8_t *data = NULL;
    size_t size = 0, good = 0;
    if (!fstat(fd, &sb) && sb.st_size > 0 && (uint64_t)sb.st_size <= RSM_MAX_MANIFEST) {
        size = (size_t)sb.st_size;
        data = malloc(size);
        ssize_t n = pread(fd, data, size, 0);
        if (n < 0) {
            // Unreadable now: run without the manifest rather than truncate it (rewritten below when empty).
            rsm_log("cache: cannot read %s: %s", fp, strerror(errno));
            free(data);
            close(fd);
            c->manifest_fd = -1;
            return;
        }
        size = (size_t)n;  // a short read (another process truncating it): what was read
    }
    manifest_header want;
    memcpy(want.magic, kManifestMagic, 8);
    want.version = RSM_CACHE_VERSION;
    want.pkey_size = sizeof(rsm_pkey);
    NSMutableArray<RSMLibEntry *> *libs = [NSMutableArray new];       // file order
    NSMutableDictionary<NSNumber *, NSNumber *> *last = [NSMutableDictionary new];  // hash -> last run
    NSMutableDictionary<NSNumber *, NSMutableArray<NSData *> *> *pipes = [NSMutableDictionary new];
    int runs = 0, n_seen = 0, n_pipes = 0;
    if (size >= sizeof want && !memcmp(data, &want, sizeof want)) {
        size_t off = sizeof want;
        NSMutableDictionary<NSNumber *, RSMLibEntry *> *byhash = [NSMutableDictionary new];
        rsm_map seen_pk;
        rsm_map_init(&seen_pk, sizeof(pipe_key));
        good = off;
        while (off + sizeof(record_header) <= size) {
            record_header h;
            memcpy(&h, data + off, sizeof h);
            if (h.len > size - off - sizeof h)
                break;
            const uint8_t *pl = data + off + sizeof h;
            if ((rsm_hash(pl, h.len) ^ h.type) != h.check)
                break;
            if (h.type == REC_LIB && h.len > 8) {
                uint64_t hash;
                memcpy(&hash, pl, 8);
                NSString *msl = [[NSString alloc] initWithBytes:pl + 8 length:h.len - 8 encoding:NSUTF8StringEncoding];
                if (!msl || rsm_hash(pl + 8, h.len - 8) != hash)
                    break;
                if (!byhash[@(hash)] && libs.count < RSM_MAX_LIBS) {
                    RSMLibEntry *e = [RSMLibEntry new];
                    e->msl = msl;
                    e->hash = hash;
                    e->record = true;
                    byhash[@(hash)] = e;
                    [libs addObject:e];
                }
                last[@(hash)] = @(runs);
            } else if (h.type == REC_PIPE && h.len == sizeof(pipe_key)) {
                pipe_key pk;
                memcpy(&pk, pl, sizeof pk);
                if (!rsm_pkey_ok(&pk.k) || !byhash[@(pk.lib)])
                    break;
                if (!rsm_map_get(&seen_pk, &pk) && n_pipes < RSM_MAX_PIPES) {
                    rsm_map_put(&seen_pk, &pk, @YES);
                    NSMutableArray *a = pipes[@(pk.lib)];
                    if (!a)
                        pipes[@(pk.lib)] = a = [NSMutableArray new];
                    [a addObject:[NSData dataWithBytes:&pk.k length:sizeof pk.k]];
                    n_pipes++;
                }
            } else if (h.type == REC_RUN && h.len == 8) {
                runs++;
            } else if (h.type == REC_SEEN && h.len == 8) {
                uint64_t hash;
                memcpy(&hash, pl, 8);
                if (byhash[@(hash)])
                    last[@(hash)] = @(runs);
                n_seen++;
            } else {
                break;
            }
            off += sizeof h + h.len;
            good = off;
        }
        rsm_map_free(&seen_pk);
        if (good < size) {
            rsm_log("cache: manifest damaged at byte %zu of %zu: dropping the rest", good, size);
            st.discarded++;
        }
    } else {
        if (size > 0) {
            rsm_log("cache: manifest is from another format or damaged: starting a new one");
            st.discarded++;
        }
        good = 0;
    }
    free(data);
    // Keep the libraries linked in the last RSM_KEEP_RUNS runs.
    const char *ke = getenv("RS_METAL_CACHE_KEEP_RUNS");
    int keep_runs = ke && atoi(ke) > 0 ? atoi(ke) : RSM_KEEP_RUNS;
    NSMutableArray<RSMLibEntry *> *keep = [NSMutableArray new];
    for (RSMLibEntry *e in libs)
        if (runs - last[@(e->hash)].intValue < keep_runs)
            [keep addObject:e];
    // Rewrite without the others, or when the per-run marks outgrow the rest.
    bool rewrite = good && (keep.count < libs.count || n_seen > 4 * (int)libs.count + 1024);
    if (rewrite) {
        rsm_log("cache: manifest: keeping %lu of %lu libraries (%d runs)", (unsigned long)keep.count,
                (unsigned long)libs.count, runs);
        uint8_t *buf = NULL;
        size_t len = 0, cap = 0;
        static const uint64_t zero = 0;
        rsm_record(&buf, &len, &cap, REC_RUN, &zero, 8, NULL, 0);
        for (RSMLibEntry *e in keep) {
            const char *msl = e->msl.UTF8String;
            rsm_record(&buf, &len, &cap, REC_LIB, &e->hash, 8, msl, strlen(msl));
            for (NSData *kd in pipes[@(e->hash)]) {
                pipe_key pk;
                memset(&pk, 0, sizeof pk);
                pk.lib = e->hash;
                memcpy(&pk.k, kd.bytes, sizeof pk.k);
                rsm_record(&buf, &len, &cap, REC_PIPE, &pk, sizeof pk, NULL, 0);
            }
        }
        NSString *tmp = [path stringByAppendingFormat:@".tmp.%d", (int)getpid()];
        int tfd = open(tmp.fileSystemRepresentation, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        bool ok = tfd >= 0 && rsm_write_all(tfd, &want, sizeof want) && rsm_write_all(tfd, buf, len);
        if (tfd >= 0)
            close(tfd);
        free(buf);
        if (ok && !rename(tmp.fileSystemRepresentation, fp)) {
            close(fd);
            fd = open(fp, O_RDWR);
            good = sizeof want + len;
            size = good;
            if (fd < 0) {
                c->manifest_fd = -1;
                return;
            }
        } else {
            unlink(tmp.fileSystemRepresentation);
            keep = libs;  // could not rewrite: keep everything
        }
    }
    for (RSMLibEntry *e in keep) {
        [wl->libs addObject:e];
        [c->recorded_libs addObject:@(e->hash)];
        c->n_libs_rec++;
        NSArray<NSData *> *a = pipes[@(e->hash)];
        if (a)
            wl->pipes[@(e->hash)] = (NSMutableArray *)a;
        for (NSData *kd in a) {
            pipe_key pk;
            memset(&pk, 0, sizeof pk);
            pk.lib = e->hash;
            memcpy(&pk.k, kd.bytes, sizeof pk.k);
            rsm_map_put(&c->recorded, &pk, @YES);
            c->n_pipes_rec++;
        }
    }
    if (!good) {
        if (ftruncate(fd, 0) || !rsm_write_all(fd, &want, sizeof want)) {
            close(fd);
            c->manifest_fd = -1;
            return;
        }
        good = sizeof want;
    } else if (good < size && ftruncate(fd, (off_t)good)) {
        close(fd);
        c->manifest_fd = -1;
        return;
    }
    close(fd);
    // Appends from here on.
    c->manifest_fd = open(fp, O_WRONLY | O_APPEND);
    c->manifest_bytes = good;
}

// --- Archives ------------------------------------------------------------------------------
// Metal serialises a fresh (in-memory) MTLBinaryArchive reliably once; serialising it again after
// more adds, or serialising an archive opened from a file after adding to it, fails or silently
// drops pipelines ("expecting 'fragment' stage in pipeline no. N"). So each archive object is
// written exactly once: archive-<seq>.bin files are read-only, new pipelines go to a fresh archive
// that becomes the next file. Too many files at start: warming adds everything it builds to the
// fresh archive, which then replaces them all (compaction).
static int rsm_archive_seq(const char *name) {
    int seq = -1, n = 0;
    if (sscanf(name, "archive-%d.bin%n", &seq, &n) == 1 && name[n] == 0 && seq >= 0)
        return seq;
    return -1;
}

static void rsm_set_lookup(RSMShaderCache *c) {
    NSMutableArray *l = [c->archives mutableCopy];
    if (c->fresh)
        [l addObject:c->fresh];
    pthread_mutex_lock(&c->m);
    c->lookup = l.count ? [l copy] : nil;
    pthread_mutex_unlock(&c->m);
}

static id<MTLBinaryArchive> rsm_new_archive(RSMShaderCache *c) {
    NSError *e = nil;
    id<MTLBinaryArchive> a = [c->dev newBinaryArchiveWithDescriptor:[MTLBinaryArchiveDescriptor new] error:&e];
    if (!a)
        rsm_log("cache: no binary archive (%s)", e.localizedDescription.UTF8String);
    return a;
}

static void rsm_archive_open(RSMShaderCache *c) {
    c->archives = [NSMutableArray new];
    c->files = [NSMutableArray new];
    NSMutableArray<NSString *> *names = [NSMutableArray new];
    DIR *d = opendir(c->dir.fileSystemRepresentation);
    if (d) {
        struct dirent *de;
        while ((de = readdir(d)))
            if (rsm_archive_seq(de->d_name) >= 0)
                [names addObject:@(de->d_name)];
        closedir(d);
    }
    [names sortUsingComparator:^NSComparisonResult(NSString *x, NSString *y) {
        int a = rsm_archive_seq(x.UTF8String), b = rsm_archive_seq(y.UTF8String);
        return a < b ? NSOrderedAscending : a > b ? NSOrderedDescending : NSOrderedSame;
    }];
    uint64_t total = 0;
    for (NSString *n in names) {
        struct stat sb;
        NSString *path = [c->dir stringByAppendingPathComponent:n];
        if (!stat(path.fileSystemRepresentation, &sb))
            total += (uint64_t)sb.st_size;
        int seq = rsm_archive_seq(n.UTF8String);
        if (seq >= c->next_seq)
            c->next_seq = seq + 1;
    }
    if (total > RSM_MAX_ARCHIVE) {
        rsm_log("cache: archives over %llu MB: starting again", RSM_MAX_ARCHIVE >> 20);
        for (NSString *n in names)
            unlink([c->dir stringByAppendingPathComponent:n].fileSystemRepresentation);
        st.discarded++;
        [names removeAllObjects];
        total = 0;
    }
    for (NSString *n in names) {
        NSString *path = [c->dir stringByAppendingPathComponent:n];
        MTLBinaryArchiveDescriptor *ad = [MTLBinaryArchiveDescriptor new];
        ad.url = [NSURL fileURLWithPath:path];
        NSError *e = nil;
        id<MTLBinaryArchive> a = [c->dev newBinaryArchiveWithDescriptor:ad error:&e];
        if (!a) {
            rsm_log("cache: discarding %s (%s)", n.UTF8String, e.localizedDescription.UTF8String);
            unlink(path.fileSystemRepresentation);
            st.discarded++;
            continue;
        }
        [c->archives addObject:a];
        [c->files addObject:path];
    }
    c->archive_bytes = total;
    c->compact = c->files.count > RSM_MAX_ARCHIVE_FILES;
    c->fresh = rsm_new_archive(c);
    rsm_set_lookup(c);
}

// On c->io: write the fresh archive as the next file and start another.
static void rsm_archive_save(RSMShaderCache *c) {
    if (!c->fresh || !c->unsaved)
        return;
    uint64_t t0 = rsm_now_ns();
    NSString *tmp = [c->dir stringByAppendingPathComponent:[NSString stringWithFormat:@"archive-new.tmp.%d", (int)getpid()]];
    NSString *path = nil;
    NSError *e = nil;
    bool ok = [c->fresh serializeToURL:[NSURL fileURLWithPath:tmp] error:&e];
    // link(), not rename(): another game instance on the same cache may have written this sequence
    // number since we looked; take the next free one instead of replacing its file.
    for (int tries = 0; ok && tries < 64; tries++) {
        path = [c->dir stringByAppendingPathComponent:[NSString stringWithFormat:@"archive-%06d.bin", c->next_seq++]];
        if (!link(tmp.fileSystemRepresentation, path.fileSystemRepresentation))
            break;
        if (errno != EEXIST)
            ok = false;
        path = nil;
    }
    ok = ok && path;
    unlink(tmp.fileSystemRepresentation);
    if (ok) {
        st.serialized++;
        struct stat sb;
        if (!stat(path.fileSystemRepresentation, &sb))
            c->archive_bytes += (uint64_t)sb.st_size;
        [c->files addObject:path];
    } else {
        rsm_log("cache: writing an archive failed: %s", e ? e.localizedDescription.UTF8String : strerror(errno));
    }
    // Written (or not): the object stays for lookups, never gets more adds.
    [c->archives addObject:c->fresh];
    c->fresh = c->archive_bytes < RSM_MAX_ARCHIVE ? rsm_new_archive(c) : nil;
    rsm_set_lookup(c);
    c->unsaved = 0;
    st.serialize_ns += rsm_now_ns() - t0;
}

// On c->io, after warming in compaction mode: the fresh archive holds everything; drop the old files.
static void rsm_archive_compact(RSMShaderCache *c, NSArray<NSString *> *old) {
    NSUInteger before = c->files.count;
    rsm_archive_save(c);
    if (c->files.count == before)
        return;  // nothing written: keep the old files
    NSString *keep = c->files.lastObject;
    uint64_t freed = 0;
    for (NSString *p in old) {
        struct stat sb;
        if (!stat(p.fileSystemRepresentation, &sb))
            freed += (uint64_t)sb.st_size;
        unlink(p.fileSystemRepresentation);  // still mapped by the archives using them
    }
    c->archive_bytes = c->archive_bytes > freed ? c->archive_bytes - freed : 0;
    [c->files removeAllObjects];
    [c->files addObject:keep];
    st.compacted++;
}

// On c->io: a pipeline was built on a miss (d != nil: add it to the archive) or found; note it in the manifest.
static void rsm_note_pipeline(RSMShaderCache *c, RSMLibEntry *e, const rsm_pkey *k, MTLRenderPipelineDescriptor *d) {
    if (d && c->fresh) {
        NSError *err = nil;
        if ([c->fresh addRenderPipelineFunctionsWithDescriptor:d error:&err]) {
            // Compaction: one file at the end of warming, not a file per RSM_SERIALIZE_EVERY.
            if (++c->unsaved >= RSM_SERIALIZE_EVERY && !c->compacting) {
                rsm_archive_save(c);
            } else {
                uint64_t gen = ++c->save_gen;
                dispatch_after(dispatch_time(DISPATCH_TIME_NOW, RSM_SERIALIZE_DELAY_MS * NSEC_PER_MSEC), c->io, ^{
                    if (gen == c->save_gen && !c->compacting)
                        rsm_archive_save(c);
                });
            }
        } else {
            rsm_log("cache: archive add failed: %s", err.localizedDescription.UTF8String);
        }
    }
    if (!e->record)
        return;
    if (![c->recorded_libs containsObject:@(e->hash)]) {
        if (c->n_libs_rec >= RSM_MAX_LIBS)
            return;
        const char *msl = e->msl.UTF8String;
        rsm_manifest_append(c, REC_LIB, &e->hash, 8, msl, strlen(msl));
        [c->recorded_libs addObject:@(e->hash)];
        c->n_libs_rec++;
    }
    pipe_key pk;
    memset(&pk, 0, sizeof pk);
    pk.lib = e->hash;
    pk.k = *k;
    if (rsm_map_get(&c->recorded, &pk) || c->n_pipes_rec >= RSM_MAX_PIPES)
        return;
    rsm_map_put(&c->recorded, &pk, @YES);
    rsm_manifest_append(c, REC_PIPE, &pk, sizeof pk, NULL, 0);
    c->n_pipes_rec++;
}

// --- Once-only builds ------------------------------------------------------------------------
// Library states (RSMLibEntry.state); pipeline slots and fragment variants use the first three.
enum { RSM_NEW = 0, RSM_BUILDING = 1, RSM_DONE = 2, RSM_QUEUED = 3 };

// Under c->m: true = the caller builds (state set to BUILDING); false = done (waited for if needed).
static bool rsm_claim(RSMShaderCache *c, int *state, bool render, _Atomic int *waited) {
    if (*state == RSM_BUILDING) {
        uint64_t t0 = render ? rsm_now_ns() : 0;
        while (*state == RSM_BUILDING)
            pthread_cond_wait(&c->cv, &c->m);
        if (render) {
            (*waited)++;
            st.wait_ns += rsm_now_ns() - t0;
        }
    }
    // NEW again after the wait: a warming compile failed and left it to be retried (rsm_publish_lib).
    if (*state == RSM_NEW || *state == RSM_QUEUED) {
        *state = RSM_BUILDING;
        return true;
    }
    return false;
}

static void rsm_publish_state(RSMShaderCache *c, int *state, int to) {
    pthread_mutex_lock(&c->m);
    __atomic_store_n(state, to, __ATOMIC_RELEASE);  // rsm_cache_library_state reads it without the lock
    pthread_cond_broadcast(&c->cv);
    pthread_mutex_unlock(&c->m);
}

static void rsm_publish(RSMShaderCache *c, int *state) { rsm_publish_state(c, state, RSM_DONE); }

// A library that failed to compile while warming stays NEW, so the game's own use compiles it again: a
// failure there may be the compiler service's (busy with the warm-up's parallel compiles), not the MSL's.
static void rsm_publish_lib(RSMShaderCache *c, RSMLibEntry *e, bool render) {
    rsm_publish_state(c, &e->state, e->lib || render ? RSM_DONE : RSM_NEW);
}

static RSMShaderCache *rsm_cache_for(id<MTLDevice> dev);

// No fast math: with it 44 of 372 Cg programs differ from GL.
static MTLCompileOptions *rsm_compile_options(void) {
    MTLCompileOptions *o = [MTLCompileOptions new];
    if (@available(macOS 15.0, *))
        o.mathMode = MTLMathModeSafe;
    else
        o.fastMathEnabled = NO;
    return o;
}

// RS_MTL_DUMP_MSL=<dir>: every library's MSL as <dir>/<hash>.metal before it is compiled
// (a compile that never returns leaves its source behind).
static void rsm_dump_msl(RSMLibEntry *e) {
    static const char *dir;
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        dir = getenv("RS_MTL_DUMP_MSL");
    });
    if (!dir)
        return;
    char path[1024];
    snprintf(path, sizeof path, "%s/%016llx.metal", dir, (unsigned long long)e->hash);
    FILE *f = fopen(path, "w");
    if (f) {
        fputs(e->msl.UTF8String, f);
        fclose(f);
    }
}

// RS_MTL_SALT_COMPILE's line (nil when unset).
static NSString *rsm_compile_salt(void) {
    static NSString *csalt;
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        const char *s = getenv("RS_MTL_SALT_COMPILE");
        if (s && *s && !strpbrk(s, "\r\n"))
            csalt = [NSString stringWithFormat:@"\n// salt %s\n", s];
    });
    return csalt;
}

static id<MTLLibrary> rsm_compile(RSMShaderCache *c, NSString *msl, NSError **err) {
    NSString *csalt = rsm_compile_salt();
    NSString *src = csalt ? [msl stringByAppendingString:csalt] : msl;
    return [c->dev newLibraryWithSource:src options:rsm_compile_options() error:err];
}

// render: compiled for the game (on its thread or by the pool for a link); else warming.
static void rsm_build_library(RSMShaderCache *c, RSMLibEntry *e, bool render) {
    rsm_dump_msl(e);
    uint64_t t0 = rsm_now_ns();
    NSError *err = nil;
    id<MTLLibrary> lib = rsm_compile(c, e->msl, &err);
    if (lib) {
        e->vs = [lib newFunctionWithName:@"rs_vs"];
        e->error = nil;
    } else {
        e->error = err.localizedDescription ?: @"unknown error";
        st.lib_failed++;
    }
    e->lib = lib;
    uint64_t dt = rsm_now_ns() - t0;
    if (dt > 1000000000ull)
        rsm_log("slow library compile: %.1f s (%016llx)", dt / 1e9, (unsigned long long)e->hash);
    if (render) {
        st.lib_compiled++;
        st.lib_ns += dt;
    } else {
        st.lib_warmed++;
        st.warm_ns += dt;
    }
}

// Under c->m: the entry for this MSL (created RSM_NEW if there is none).
static RSMLibEntry *rsm_entry(RSMShaderCache *c, NSString *msl, RSMLibEntry *seed) {
    RSMLibEntry *e = c->libs[msl];
    if (!e) {
        e = seed ?: [RSMLibEntry new];
        e->msl = msl;
        if (!seed) {
            const char *u = msl.UTF8String;
            e->hash = rsm_hash(u, strlen(u));
        }
        rsm_map_init(&e->pipes, sizeof(rsm_pkey));
        c->libs[msl] = e;
    }
    return e;
}

// The entry for this MSL, built (by this thread or another) when it returns.
static RSMLibEntry *rsm_library(RSMShaderCache *c, NSString *msl, bool record, bool render, RSMLibEntry *seed) {
    pthread_mutex_lock(&c->m);
    RSMLibEntry *e = rsm_entry(c, msl, seed);
    e->record |= record;
    bool mine = rsm_claim(c, &e->state, render, &st.lib_waited);
    pthread_mutex_unlock(&c->m);
    if (mine) {
        rsm_build_library(c, e, render);
        rsm_publish_lib(c, e, render);
    } else if (render) {
        st.lib_reused++;
    }
    return e;
}

// Wait until e is built; if no worker has started it, build it on this thread.
static void rsm_library_wait(RSMShaderCache *c, RSMLibEntry *e, bool render) {
    if (__atomic_load_n(&e->state, __ATOMIC_ACQUIRE) == RSM_DONE)
        return;
    pthread_mutex_lock(&c->m);
    bool mine = rsm_claim(c, &e->state, render, &st.lib_waited);
    pthread_mutex_unlock(&c->m);
    if (mine) {
        if (render)
            st.lib_stolen++;
        rsm_build_library(c, e, render);
        rsm_publish_lib(c, e, render);
    }
}

static id<MTLRenderPipelineState> rsm_pipeline_for(RSMShaderCache *c, RSMLibEntry *e, const rsm_pkey *k, bool render);

// --- Compile pool ----------------------------------------------------------------------------
// Up to g_pool_max worker threads (created on demand, never exit) take jobs from two FIFOs: the
// game's links first, then warming. A job only builds its library if it is still RSM_QUEUED (a
// waiting thread may have taken it, or the other FIFO's job for the same entry); a warming job
// then prebuilds that library's pipelines from the manifest. Workers run link jobs at
// user-initiated QoS and warming at utility (the compile itself runs in Metal's compiler
// service, which inherits the requester's QoS). After rs_gfx_prepare at most g_pool_limit jobs
// run at once, all at utility ("Safe load" below). Corpus jobs (also in the warming FIFO) compile
// an MSL text that has no entry and drop the library ("Corpus warm" below).
@interface RSMJob : NSObject {
@public
    RSMShaderCache *c;
    RSMLibEntry *e;
    NSArray<NSData *> *keys;  // warming: pipelines to prebuild
    bool warm;                // manifest or corpus warming (the second FIFO)
    NSString *corpus_msl;     // a corpus job: compile this, keep nothing
}
@end
@implementation RSMJob
@end

static pthread_mutex_t g_pool_m = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_pool_cv = PTHREAD_COND_INITIALIZER;
static NSMutableArray<RSMJob *> *g_jobs[2];  // [0] links, [1] warming
static int g_pool_threads, g_pool_idle;      // idle: waiting for a job or for a free slot
static int g_pool_running;                   // jobs running now
// Safe load (rs_gfx_prepare): warming at user-initiated QoS while it waits; afterwards at most
// g_pool_limit jobs run at once, all at utility QoS.
static _Atomic bool g_warm_boost;
static bool g_throttled;
static int g_pool_limit = 1 << 30;

static bool rsm_pool_explicit(void) {
    const char *e = getenv("RS_MTL_COMPILE_THREADS");
    return e && *e;
}

static int rsm_pool_max(void) {
    static int n = -1;
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        const char *e = getenv("RS_MTL_COMPILE_THREADS");
        if (e && *e) {
            n = atoi(e);
        } else {
            int cpus = 0;
            size_t len = sizeof cpus;
            if (sysctlbyname("hw.activecpu", &cpus, &len, NULL, 0))
                cpus = 4;
            n = cpus - 2;  // the game's render and main threads keep a core each
        }
        n = n < 0 ? 0 : n > RSM_MAX_THREADS ? RSM_MAX_THREADS : n;
        if (e && *e)
            return;
        n = n < 2 ? 2 : n;
    });
    return n;
}

// A corpus job: only the system's Metal cache keeps the result, so the game's own compile of
// this MSL later is a cache hit (well under a millisecond) instead of 0.1-0.5 s of CPU. Keeping
// the libraries instead would hold some 60 KB each, for thousands the game mostly never links.
static void rsm_run_corpus_job(RSMJob *j) {
    RSMShaderCache *c = j->c;
    uint64_t t0 = rsm_now_ns();
    @autoreleasepool {
        NSError *err = nil;
        id<MTLLibrary> lib = rsm_compile(c, j->corpus_msl, &err);
        if (!lib)
            st.corpus_failed++;
        lib = nil;
    }
    st.corpus_ns += rsm_now_ns() - t0;
    st.corpus_warmed++;
}

static void rsm_job_done(RSMJob *j) {
    if (j->corpus_msl) {
        j->c->corpus_done++;
        dispatch_group_leave(j->c->corpus);
    } else if (j->warm) {
        j->c->warm_done++;
        dispatch_group_leave(j->c->warm);
    }
}

static void rsm_run_job(RSMJob *j) {
    RSMShaderCache *c = j->c;
    RSMLibEntry *e = j->e;
    if (j->corpus_msl) {
        if (!c->closing) {
            pthread_set_qos_class_self_np(g_warm_boost && !g_throttled ? QOS_CLASS_USER_INITIATED : QOS_CLASS_UTILITY, 0);
            rsm_run_corpus_job(j);
        }
    } else if (!(j->warm && c->closing)) {
        // Read without the lock: a job that starts as the mode changes runs at either QoS.
        bool throttled = __atomic_load_n(&g_throttled, __ATOMIC_RELAXED);
        qos_class_t q = throttled || (j->warm && !g_warm_boost) ? QOS_CLASS_UTILITY : QOS_CLASS_USER_INITIATED;
        pthread_set_qos_class_self_np(q, 0);
        if (throttled) {
            int now = (int)qos_class_self();
            pthread_mutex_lock(&g_pool_m);
            if (now > st.pool_qos)
                st.pool_qos = now;
            pthread_mutex_unlock(&g_pool_m);
        }
        pthread_mutex_lock(&c->m);
        bool mine = e->state == RSM_QUEUED || e->state == RSM_NEW;
        if (mine)
            e->state = RSM_BUILDING;
        pthread_mutex_unlock(&c->m);
        if (mine) {
            if (!j->warm)
                st.lib_async++;
            rsm_build_library(c, e, !j->warm);
            rsm_publish_lib(c, e, !j->warm);
        }
        for (NSData *kd in j->keys) {
            if (c->closing)
                break;
            rsm_pipeline_for(c, e, kd.bytes, false);
        }
    }
    rsm_job_done(j);
}

static void *rsm_worker(void *arg) {
    (void)arg;
    pthread_setname_np("rs.metal.compile");
    for (;;) {
        RSMJob *j;
        pthread_mutex_lock(&g_pool_m);
        while ((!g_jobs[0].count && !g_jobs[1].count) || g_pool_running >= g_pool_limit) {
            g_pool_idle++;
            pthread_cond_wait(&g_pool_cv, &g_pool_m);
            g_pool_idle--;
        }
        NSMutableArray *q = g_jobs[0].count ? g_jobs[0] : g_jobs[1];
        j = q[0];
        [q removeObjectAtIndex:0];
        if (++g_pool_running > st.pool_peak && g_throttled)
            st.pool_peak = g_pool_running;
        pthread_mutex_unlock(&g_pool_m);
        @autoreleasepool {
            rsm_run_job(j);
            j = nil;
        }
        pthread_mutex_lock(&g_pool_m);
        g_pool_running--;
        if (g_jobs[0].count || g_jobs[1].count)
            pthread_cond_signal(&g_pool_cv);  // a worker held back by the limit
        pthread_mutex_unlock(&g_pool_m);
    }
    return NULL;
}

// While rs_gfx_prepare waits the game has not started: the pool may use every CPU (unless
// RS_MTL_COMPILE_THREADS is set).
static int rsm_pool_boost_max(void) {
    int cpus = 0;
    size_t len = sizeof cpus;
    if (sysctlbyname("hw.activecpu", &cpus, &len, NULL, 0))
        cpus = 4;
    return cpus < 2 ? 2 : cpus > RSM_MAX_BOOST_THREADS ? RSM_MAX_BOOST_THREADS : cpus;
}

static void rsm_pool_push(NSArray<RSMJob *> *jobs, bool warm) {
    pthread_mutex_lock(&g_pool_m);
    if (!g_jobs[0]) {
        g_jobs[0] = [NSMutableArray new];
        g_jobs[1] = [NSMutableArray new];
    }
    [g_jobs[warm] addObjectsFromArray:jobs];
    int want = (int)(g_jobs[0].count + g_jobs[1].count);
    int max = rsm_pool_max() ?: (g_jobs[1].count ? 2 : 0);  // 0: warming still runs in the background
    if (g_warm_boost && !g_throttled && !rsm_pool_explicit())
        max = rsm_pool_boost_max();
    if (g_throttled && max > g_pool_limit)
        max = g_pool_limit;
    while (g_pool_idle < want && g_pool_threads < max) {
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        pthread_attr_set_qos_class_np(&attr, QOS_CLASS_USER_INITIATED, 0);
        pthread_t t;
        int rc = pthread_create(&t, &attr, rsm_worker, NULL);
        pthread_attr_destroy(&attr);
        if (rc)
            break;
        g_pool_threads++;
        want--;
    }
    pthread_cond_broadcast(&g_pool_cv);
    pthread_mutex_unlock(&g_pool_m);
}

// At exit: queued jobs are dropped (running ones finish or are cut off by the exit).
static void rsm_pool_drain(void) {
    pthread_mutex_lock(&g_pool_m);
    NSArray *warm = [g_jobs[1] copy];
    [g_jobs[0] removeAllObjects];
    [g_jobs[1] removeAllObjects];
    pthread_mutex_unlock(&g_pool_m);
    for (RSMJob *j in warm)
        rsm_job_done(j);
}

// rs_gfx_prepare timed out: the corpus jobs not started yet are dropped (speculative; the game's
// own links still compile what it needs). Returns how many.
static int rsm_pool_drop_corpus(RSMShaderCache *c) {
    pthread_mutex_lock(&g_pool_m);
    NSMutableArray<RSMJob *> *drop = [NSMutableArray new], *keep = [NSMutableArray new];
    for (RSMJob *j in g_jobs[1])
        [(j->corpus_msl && j->c == c ? drop : keep) addObject:j];
    [g_jobs[1] setArray:keep];
    pthread_mutex_unlock(&g_pool_m);
    for (RSMJob *j in drop)
        dispatch_group_leave(c->corpus);
    return (int)drop.count;
}

// The throttled pool's size: RS_MTL_COMPILE_THREADS when set (at least 1), else 2.
static int rsm_pool_throttle_limit(void) {
    if (rsm_pool_explicit())
        return rsm_pool_max() > 0 ? rsm_pool_max() : 1;
    return 2;
}

static void rsm_pool_throttle(void) {
    pthread_mutex_lock(&g_pool_m);
    g_throttled = true;
    g_pool_limit = rsm_pool_throttle_limit();
    st.pool_peak = g_pool_running < g_pool_limit ? g_pool_running : g_pool_limit;
    pthread_mutex_unlock(&g_pool_m);
}

static NSString *rsm_msl_string(const char *msl) {
    NSString *s = [[NSString alloc] initWithUTF8String:msl]
                      ?: [[NSString alloc] initWithCString:msl encoding:NSISOLatin1StringEncoding];
    static NSString *salt, *fail;
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        const char *e = getenv("RS_MTL_SALT");
        if (e && *e && !strpbrk(e, "\r\n"))
            salt = [NSString stringWithFormat:@"\n// salt %s\n", e];
        e = getenv("RS_MTL_TEST_FAIL");
        if (e && *e)
            fail = @(e);
    });
    if (salt)
        s = [s stringByAppendingString:salt];
    if (fail && [s containsString:fail])
        s = [s stringByAppendingString:@"\n#error RS_MTL_TEST_FAIL\n"];
    return s;
}

RSMLibEntry *rsm_cache_library(RSMContext *ctx, const char *msl, bool record) {
    RSMShaderCache *c = rsm_cache_for(ctx->dev);
    return rsm_library(c, rsm_msl_string(msl), record, true, nil);
}

RSMLibEntry *rsm_cache_library_async(RSMContext *ctx, const char *msl, bool record) {
    RSMShaderCache *c = rsm_cache_for(ctx->dev);
    NSString *s = rsm_msl_string(msl);
    bool async = rsm_pool_max() > 0;
    pthread_mutex_lock(&c->m);
    RSMLibEntry *e = rsm_entry(c, s, nil);
    e->record |= record;
    // New, or queued for warming only: queue it as a link (ahead of warming).
    bool push = async && (e->state == RSM_NEW || e->state == RSM_QUEUED) && !e->link_queued;
    if (push) {
        e->state = RSM_QUEUED;
        e->link_queued = true;
    } else if (async && e->state == RSM_DONE) {
        st.lib_reused++;
    }
    bool seen = record && c->persist && ![c->linked_libs containsObject:@(e->hash)];
    if (seen)
        [c->linked_libs addObject:@(e->hash)];
    pthread_mutex_unlock(&c->m);
    if (seen) {
        uint64_t h = e->hash;
        dispatch_async(c->io, ^{
            rsm_note_seen(c, h);
        });
    }
    if (!async)
        return rsm_library(c, s, record, true, nil);
    if (push) {
        RSMJob *j = [RSMJob new];
        j->c = c;
        j->e = e;
        rsm_pool_push(@[ j ], false);
    }
    return e;
}

int rsm_cache_library_state(RSMLibEntry *e) {
    if (__atomic_load_n(&e->state, __ATOMIC_ACQUIRE) != RSM_DONE)
        return 0;
    return e->lib ? 1 : -1;
}

bool rsm_cache_library_wait(RSMContext *ctx, RSMLibEntry *e) {
    rsm_library_wait(rsm_cache_for(ctx->dev), e, true);
    return e->lib != nil;
}

static id<MTLFunction> rsm_fragment(RSMShaderCache *c, RSMLibEntry *e, int alpha) {
    if (alpha < 0 || alpha > 8 || !e->lib)
        return nil;
    pthread_mutex_lock(&c->m);
    bool mine = rsm_claim(c, &e->fs_state[alpha], false, NULL);
    pthread_mutex_unlock(&c->m);
    if (mine) {
        MTLFunctionConstantValues *cv = [MTLFunctionConstantValues new];
        uint32_t v = (uint32_t)alpha;
        [cv setConstantValue:&v type:MTLDataTypeUInt atIndex:0];
        NSError *err = nil;
        id<MTLFunction> f = [e->lib newFunctionWithName:@"rs_fs" constantValues:cv error:&err];
        if (!f) {
            f = [e->lib newFunctionWithName:@"rs_fs"];
            if (!f)
                rsm_log("fragment function: %s", err.localizedDescription.UTF8String);
        }
        e->fs[alpha] = f;
        rsm_publish(c, &e->fs_state[alpha]);
    }
    return e->fs[alpha];
}

static id<MTLRenderPipelineState> rsm_build_pipeline(RSMShaderCache *c, RSMLibEntry *e, const rsm_pkey *k, bool render) {
    id<MTLFunction> fs = rsm_fragment(c, e, k->alpha_func);
    if (!e->vs || !fs)
        return nil;
    uint64_t t0 = rsm_now_ns();
    MTLRenderPipelineDescriptor *d = rsm_pipeline_desc(e->vs, fs, k);
    NSError *err = nil;
    id<MTLRenderPipelineState> ps = nil;
    pthread_mutex_lock(&c->m);
    NSArray<id<MTLBinaryArchive>> *a = c->lookup;  // replaced by rsm_archive_save
    pthread_mutex_unlock(&c->m);
    if (a) {
        d.binaryArchives = a;
        ps = [c->dev newRenderPipelineStateWithDescriptor:d options:MTLPipelineOptionFailOnBinaryArchiveMiss
                                               reflection:nil error:&err];
        d.binaryArchives = nil;
        if (ps)
            st.pipe_hits++;
        else
            st.pipe_misses++;
        if (st.verbose)
            rsm_log("cache: %s lib %016llx alpha %d blend %d attr1 fmt %d stride %d", ps ? "hit" : "miss",
                    (unsigned long long)e->hash, k->alpha_func, k->blend, k->vl.fmt[1], k->vl.stride[1]);
    }
    bool miss = !ps;
    if (!ps) {
        err = nil;
        ps = [c->dev newRenderPipelineStateWithDescriptor:d error:&err];
    }
    uint64_t dt = rsm_now_ns() - t0;
    if (render) {
        st.pipe_built++;
        st.pipe_ns += dt;
    } else {
        st.pipe_warmed++;
        st.warm_ns += dt;
    }
    if (!ps) {
        static _Atomic int told;
        if (told++ < 16)
            rsm_log("pipeline: %s", err.localizedDescription.UTF8String);
        return nil;
    }
    if (c->persist) {
        // Compaction: hits go into the fresh archive too (cheap: the code is at hand), the game's own
        // as well, since warming then finds their slots done and the old files are deleted.
        MTLRenderPipelineDescriptor *add = miss || c->compacting ? d : nil;
        rsm_pkey kk = *k;
        dispatch_async(c->io, ^{
            rsm_note_pipeline(c, e, &kk, add);
        });
    }
    return ps;
}

static id<MTLRenderPipelineState> rsm_pipeline_for(RSMShaderCache *c, RSMLibEntry *e, const rsm_pkey *k, bool render) {
    if (!e)
        return nil;
    rsm_library_wait(c, e, render);
    if (!e->lib)
        return nil;
    pthread_mutex_lock(&c->m);
    RSMPipeSlot *s = rsm_map_get(&e->pipes, k);
    if (!s) {
        s = [RSMPipeSlot new];
        rsm_map_put(&e->pipes, k, s);
    }
    bool mine = rsm_claim(c, &s->state, render, &st.pipe_waited);
    pthread_mutex_unlock(&c->m);
    if (mine) {
        s->ps = rsm_build_pipeline(c, e, k, render);
        rsm_publish(c, &s->state);
    } else if (render) {
        st.pipe_reused++;
    }
    return s->ps;
}

id<MTLRenderPipelineState> rsm_cache_pipeline(RSMContext *ctx, RSMLibEntry *e, const rsm_pkey *k) {
    return rsm_pipeline_for(rsm_cache_for(ctx->dev), e, k, true);
}

// --- Warming -------------------------------------------------------------------------------
static void rsm_warm_start(RSMShaderCache *c, warm_list *wl) {
    if (!wl->libs.count)
        return;
    c->warm = dispatch_group_create();
    c->compacting = c->compact;
    // Libraries in first-use order, each one's pipelines right after it, on the compile pool
    // behind the game's own links.
    NSMutableArray<RSMJob *> *jobs = [NSMutableArray new];
    pthread_mutex_lock(&c->m);
    for (RSMLibEntry *seed in wl->libs) {
        RSMLibEntry *e = rsm_entry(c, seed->msl, seed);
        e->record = true;
        if (e->state == RSM_NEW)
            e->state = RSM_QUEUED;
        RSMJob *j = [RSMJob new];
        j->c = c;
        j->e = e;
        j->keys = wl->pipes[@(seed->hash)];
        j->warm = true;
        [jobs addObject:j];
        dispatch_group_enter(c->warm);
    }
    c->warm_total = (int)jobs.count;
    pthread_mutex_unlock(&c->m);
    rsm_pool_push(jobs, true);
    // After the blocks above (a notify on an empty group would run at once).
    if (c->compact) {
        NSArray *old = [c->files copy];
        dispatch_group_notify(c->warm, c->io, ^{
            if (!c->closing)
                rsm_archive_compact(c, old);
            c->compacting = false;
        });
    }
}

// --- Safe load -------------------------------------------------------------------------------
// The shell calls rs_gfx_prepare before the game starts loading when the game's loading is known
// to stall while the CPU is busy (a modded cache.psarc). Warming, started when the first context
// was made, gets the whole pool at user-initiated QoS and is waited for (at most max_s): on a warm
// system Metal cache that is milliseconds, after a cold start (new OS, new translator) the
// compile happens here, behind a black window, instead of during the game's loading. Then the
// pool is throttled for the rest of the run: libraries the manifest did not know (a first run)
// compile on at most rsm_pool_throttle_limit() workers at utility QoS; a draw that needs one
// right away still compiles it on its own thread (rsm_library_wait).
static const char *rsm_qos_name(int q) {
    switch (q) {
    case QOS_CLASS_USER_INTERACTIVE: return "user-interactive";
    case QOS_CLASS_USER_INITIATED: return "user-initiated";
    case QOS_CLASS_DEFAULT: return "default";
    case QOS_CLASS_UTILITY: return "utility";
    case QOS_CLASS_BACKGROUND: return "background";
    default: return "none";
    }
}

// --- Corpus warm ---------------------------------------------------------------------------
// The manifest only knows what earlier runs linked: on the very first run (or after an OS update,
// a new GPU or translator: a new cache directory or new MSL) it is empty and every library the
// game needs would compile, cold, while it loads. So rs_gfx_prepare also compiles the MSL of
// every program in the offline shader corpus (rsmsl_corpus_*; distinct texts only, minus the
// manifest's) on the whole pool, before the game starts. The libraries are not kept (see
// rsm_run_corpus_job): the system's Metal cache makes the game's own compiles of them cheap. Done
// when the manifest lists fewer than RSM_CORPUS_MIN_MANIFEST libraries, or when the cache
// directory has no record (corpus-warmed) that this corpus was compiled completely here before;
// the record names the corpus's MSL set (and RS_MTL_SALT_COMPILE), so a new corpus or translator
// warms again. A corpus warm cut short by the timeout leaves no record: the next start goes on
// where the system's cache stopped. Pipelines are not warmed from the corpus: they need draw
// state the corpus does not have (the manifest has it), and cost far less than the libraries.
#ifdef RSM_MSL_DYNAMIC
extern int rsmsl_corpus_count(void) __attribute__((weak_import));
extern char *rsmsl_corpus_msl(int) __attribute__((weak_import));
#endif

static NSString *rsm_corpus_record_path(RSMShaderCache *c) {
    return [c->dir stringByAppendingPathComponent:@"corpus-warmed"];
}

static uint64_t rsm_corpus_record(RSMShaderCache *c) {
    char buf[64] = "";
    FILE *f = fopen(rsm_corpus_record_path(c).fileSystemRepresentation, "r");
    if (!f)
        return 0;
    if (!fgets(buf, sizeof buf, f))
        buf[0] = 0;
    fclose(f);
    return strtoull(buf, NULL, 16);
}

static void rsm_corpus_record_write(RSMShaderCache *c, uint64_t id) {
    NSString *path = rsm_corpus_record_path(c);
    NSString *tmp = [path stringByAppendingFormat:@".tmp.%d", (int)getpid()];
    FILE *f = fopen(tmp.fileSystemRepresentation, "w");
    if (!f)
        return;
    bool ok = fprintf(f, "%016llx\n", (unsigned long long)id) > 0;
    ok = fclose(f) == 0 && ok;
    if (!ok || rename(tmp.fileSystemRepresentation, path.fileSystemRepresentation))
        unlink(tmp.fileSystemRepresentation);
}

// The corpus's distinct MSL texts (as the game would compile them: rsm_msl_string), without the
// ones already known (the manifest's, being warmed), and the identity of the whole set.
static NSArray<NSString *> *rsm_corpus_list(RSMShaderCache *c, uint64_t *id) {
    *id = 0;
#ifdef RSM_MSL_DYNAMIC
    if (!rsmsl_corpus_count || !rsmsl_corpus_msl)
        return @[];
#endif
    int n = rsmsl_corpus_count();
    if (n <= 0)
        return @[];
    // RS_METAL_CORPUS_LIMIT=<n>: tests only, warm at most the first n corpus entries (a full cold
    // corpus takes minutes). The corpus identity then covers only those, so a later run without
    // the limit warms the rest.
    const char *lim = getenv("RS_METAL_CORPUS_LIMIT");
    if (lim && atoi(lim) > 0 && atoi(lim) < n)
        n = atoi(lim);
    NSMutableArray<NSString *> *out = [NSMutableArray new];
    NSMutableSet<NSString *> *seen = [NSMutableSet new];
    uint64_t h = 0xcbf29ce484222325ull;
    for (int i = 0; i < n; i++) {
        @autoreleasepool {
            char *m = rsmsl_corpus_msl(i);
            if (!m)
                continue;
            NSString *s = rsm_msl_string(m);
            uint64_t mh = rsm_hash(m, strlen(m));
            free(m);
            if (!s || [seen containsObject:s])
                continue;
            [seen addObject:s];
            h = (h ^ mh) * 0x100000001b3ull;
            pthread_mutex_lock(&c->m);
            bool known = c->libs[s] != nil;
            pthread_mutex_unlock(&c->m);
            if (!known)
                [out addObject:s];
        }
    }
    NSString *csalt = rsm_compile_salt();
    if (csalt) {
        const char *u = csalt.UTF8String;
        h = (h ^ rsm_hash(u, strlen(u))) * 0x100000001b3ull;
    }
    *id = h ? h : 1;
    return out;
}

// Whether this safe load warms the corpus; if so, the jobs are queued (c->corpus) and *id is the
// record to write once they are all done.
static bool rsm_corpus_start(RSMShaderCache *c, uint64_t *id) {
    *id = 0;
    const char *e = getenv("RS_METAL_CORPUS_WARM");
    bool force = e && *e && atoi(e) != 0;
    if ((e && *e && !force) || !c->persist || c->corpus)
        return false;
    uint64_t t0 = rsm_now_ns();
    uint64_t cid;
    NSArray<NSString *> *list = rsm_corpus_list(c, &cid);
    if (!cid)
        return false;  // no corpus
    const char *me = getenv("RS_METAL_CORPUS_MIN_MANIFEST");
    int min_manifest = me && *me ? atoi(me) : RSM_CORPUS_MIN_MANIFEST;
    uint64_t rec = rsm_corpus_record(c);
    bool small = c->warm_total < min_manifest;
    if (!force && !small && rec == cid) {
        rsm_log("safe load: shader corpus compiled here before, the manifest's %d libraries are enough",
                c->warm_total);
        return false;
    }
    rsm_log("safe load: compiling the shader corpus too (%lu distinct libraries not in the manifest; %s; "
            "listed in %.0f ms)",
            (unsigned long)list.count,
            force ? "RS_METAL_CORPUS_WARM=1"
            : rec != cid ? (rec ? "another corpus than last time" : "first time with this corpus here")
                         : "the manifest is small",
            (rsm_now_ns() - t0) / 1e6);
    *id = cid;
    if (!list.count)
        return true;
    c->corpus = dispatch_group_create();
    NSMutableArray<RSMJob *> *jobs = [NSMutableArray new];
    for (NSString *msl in list) {
        RSMJob *j = [RSMJob new];
        j->c = c;
        j->warm = true;
        j->corpus_msl = msl;
        dispatch_group_enter(c->corpus);
        [jobs addObject:j];
    }
    c->corpus_total = (int)jobs.count;
    rsm_pool_push(jobs, true);
    return true;
}

int rsm_cache_prepare(RSMContext *ctx, double max_s) {
    RSMShaderCache *c = rsm_cache_for(ctx->dev);
    uint64_t t0 = rsm_now_ns();
    int rc = 0;
    c->preparing = true;
    g_warm_boost = true;
    rsm_pool_push(@[], true);  // the whole machine for what warming has queued
    uint64_t corpus_id = 0;
    bool corpus = rsm_corpus_start(c, &corpus_id);
    int total = c->warm_total + c->corpus_total;
    if (total) {
        rsm_log("safe load: compiling %d libraries (cache manifest %d, shader corpus %d) before the game loads "
                "(%d threads, at most %g s)",
                total, c->warm_total, (int)c->corpus_total, g_pool_threads, max_s);
        double next_log = 2.0;
        for (;;) {
            double el = (rsm_now_ns() - t0) / 1e9, left = max_s - el;
            if (left <= 0) {
                rc = 1;
                break;
            }
            double step = left < 0.25 ? left : 0.25;
            dispatch_time_t until = dispatch_time(DISPATCH_TIME_NOW, (int64_t)(step * 1e9));
            bool done = (!c->warm || !dispatch_group_wait(c->warm, until)) &&
                        (!c->corpus || !dispatch_group_wait(c->corpus, until));
            if (done)
                break;
            if (el >= next_log) {
                rsm_log("safe load: %d of %d libraries (%.0f s)", (int)(c->warm_done + c->corpus_done), total, el);
                next_log += el < 30 ? 2.0 : 10.0;
            }
        }
        if (rc) {
            int dropped = c->corpus ? rsm_pool_drop_corpus(c) : 0;
            rsm_log("safe load: timed out after %g s with %d of %d libraries; the manifest's rest compiles while "
                    "the game loads, %d corpus libraries are skipped",
                    max_s, (int)(c->warm_done + c->corpus_done), total, dropped);
        }
    }
    g_warm_boost = false;
    if (corpus && !rc && !c->closing) {
        rsm_corpus_record_write(c, corpus_id);
        if (st.corpus_failed)
            rsm_log("safe load: %d corpus libraries failed to compile", (int)st.corpus_failed);
    }
    rsm_pool_throttle();
    uint64_t dt = rsm_now_ns() - t0;
    st.prepare_ns += dt;
    rsm_log("safe load: %d libraries ready in %.1f ms; compile pool now %d at a time, utility QoS",
            (int)(c->warm_done + c->corpus_done), dt / 1e6, g_pool_limit);
    c->preparing = false;
    return rc;
}

int rsm_cache_progress(RSMContext *ctx, int *done, int *total) {
    RSMShaderCache *c = rsm_cache_for(ctx->dev);
    // done first: a reader never sees done > total (both only grow).
    int d = c->warm_done + c->corpus_done;
    int t = c->warm_total + c->corpus_total;
    if (done)
        *done = d < t ? d : t;
    if (total)
        *total = t;
    return c->preparing ? 1 : 0;
}

// --- Setup ---------------------------------------------------------------------------------
static void rsm_print_stats(const char *when) {
    if (!rsm_env_on("RS_METAL_CACHE_STATS"))
        return;
    fprintf(stderr,
            "[rs-mtl] cache stats (%s): lib_compiled=%d lib_ms=%.1f lib_async=%d lib_stolen=%d lib_reused=%d "
            "lib_waited=%d lib_warmed=%d lib_failed=%d pipe_built=%d pipe_ms=%.1f pipe_reused=%d pipe_waited=%d pipe_warmed=%d "
            "archive_hits=%d archive_misses=%d wait_ms=%.1f warm_ms=%.1f archive_writes=%d write_ms=%.1f "
            "discarded=%d compacted=%d pool_threads=%d pool_limit=%d pool_peak=%d pool_qos=%s prepare_ms=%.1f "
            "corpus_warmed=%d corpus_failed=%d corpus_ms=%.1f\n",
            when, st.lib_compiled, st.lib_ns / 1e6, st.lib_async, st.lib_stolen, st.lib_reused, st.lib_waited, st.lib_warmed, st.lib_failed,
            st.pipe_built, st.pipe_ns / 1e6, st.pipe_reused, st.pipe_waited, st.pipe_warmed, st.pipe_hits,
            st.pipe_misses, st.wait_ns / 1e6, st.warm_ns / 1e6, st.serialized, st.serialize_ns / 1e6, st.discarded,
            st.compacted, g_pool_threads, g_throttled ? g_pool_limit : rsm_pool_max(), st.pool_peak,
            rsm_qos_name(st.pool_qos), st.prepare_ns / 1e6, (int)st.corpus_warmed, (int)st.corpus_failed,
            st.corpus_ns / 1e6);
}

static void rsm_flush(RSMShaderCache *c) {
    if (!c->persist)
        return;
    dispatch_sync(c->io, ^{
        rsm_archive_save(c);
        if (c->manifest_fd >= 0)
            fsync(c->manifest_fd);
    });
}

static void rsm_cache_atexit(void) {
    pthread_mutex_lock(&g_caches_lock);
    NSArray *caches = [g_caches copy];
    pthread_mutex_unlock(&g_caches_lock);
    for (RSMShaderCache *c in caches)
        c->closing = true;
    rsm_pool_drain();
    for (RSMShaderCache *c in caches)
        rsm_flush(c);
    rsm_print_stats("exit");
}

static void rsm_open_disk(RSMShaderCache *c, warm_list *wl) {
    NSString *base = rsm_cache_base();
    if (!base)
        return;
    NSString *prefix = rsm_cache_prefix();
    char dn[64];
    rsm_sanitize(c->dev.name.UTF8String ?: "gpu", dn, sizeof dn);
    NSString *dir = [base stringByAppendingPathComponent:[prefix stringByAppendingString:@(dn)]];
    NSError *e = nil;
    if (![NSFileManager.defaultManager createDirectoryAtPath:dir withIntermediateDirectories:YES attributes:nil
                                                       error:&e]) {
        rsm_log("cache: cannot create %s: %s", dir.fileSystemRepresentation, e.localizedDescription.UTF8String);
        return;
    }
    rsm_clean_base(base, dir.lastPathComponent);
    // Leftovers of interrupted archive and manifest writes (".tmp.<pid>"), unless that process still runs: a
    // second game instance must not delete the first one's file while it is being written.
    DIR *d = opendir(dir.fileSystemRepresentation);
    if (d) {
        struct dirent *de;
        while ((de = readdir(d))) {
            const char *t = strstr(de->d_name, ".tmp");
            if ((strncmp(de->d_name, "archive-", 8) && strncmp(de->d_name, "manifest.bin", 12)) || !t)
                continue;
            int pid = t[4] == '.' ? atoi(t + 5) : 0;
            if (pid > 0 && pid != getpid() && (kill(pid, 0) == 0 || errno == EPERM))
                continue;
            unlink([dir stringByAppendingPathComponent:@(de->d_name)].fileSystemRepresentation);
        }
        closedir(d);
    }
    c->dir = dir;
    c->persist = true;
    c->io = dispatch_queue_create("rs.metal.cache.io",
                                  dispatch_queue_attr_make_with_qos_class(DISPATCH_QUEUE_SERIAL, QOS_CLASS_UTILITY, 0));
    rsm_map_init(&c->recorded, sizeof(pipe_key));
    c->recorded_libs = [NSMutableSet new];
    c->linked_libs = [NSMutableSet new];
    rsm_manifest_open(c, wl);
    rsm_archive_open(c);
    atexit(rsm_cache_atexit);
}

static RSMShaderCache *rsm_cache_for(id<MTLDevice> dev) {
    pthread_mutex_lock(&g_caches_lock);
    if (!g_caches)
        g_caches = [NSMutableArray new];
    for (RSMShaderCache *c in g_caches)
        if (c->dev == dev) {
            pthread_mutex_unlock(&g_caches_lock);
            return c;
        }
    const char *sv = getenv("RS_METAL_CACHE_STATS");
    st.verbose = sv && !strcmp(sv, "2");
    RSMShaderCache *c = [RSMShaderCache new];
    c->dev = dev;
    c->libs = [NSMutableDictionary new];
    c->manifest_fd = -1;
    pthread_mutex_init(&c->m, NULL);
    pthread_cond_init(&c->cv, NULL);
    // The disk cache belongs to the first device used (the game has one).
    warm_list wl = {[NSMutableArray new], [NSMutableDictionary new]};
    if (!g_persist_claimed) {
        g_persist_claimed = true;
        @autoreleasepool {
            rsm_open_disk(c, &wl);
        }
    }
    [g_caches addObject:c];
    pthread_mutex_unlock(&g_caches_lock);
    if (c->persist) {
        rsm_warm_start(c, &wl);
        if (c->warm && rsm_env_on("RS_METAL_CACHE_WARM_SYNC"))
            dispatch_group_wait(c->warm, DISPATCH_TIME_FOREVER);
    }
    return c;
}

void rsm_cache_context_created(RSMContext *ctx) {
    rsm_cache_for(ctx->dev);
    pthread_mutex_lock(&g_caches_lock);
    g_live_contexts++;
    pthread_mutex_unlock(&g_caches_lock);
}

void rsm_cache_context_destroyed(RSMContext *ctx) {
    pthread_mutex_lock(&g_caches_lock);
    bool last = --g_live_contexts == 0;
    pthread_mutex_unlock(&g_caches_lock);
    if (!last)
        return;
    rsm_flush(rsm_cache_for(ctx->dev));
    rsm_print_stats("last context");
}
