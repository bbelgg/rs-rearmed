// Metal core: shader library and pipeline cache (docs/METAL-PLAN.md step 7).
//
// In memory, per device (shared by its contexts): MSL text -> library + rs_vs + rs_fs variants,
// and per library rsm_pkey -> MTLRenderPipelineState. Each item is built once; a thread that
// needs an item another thread is building waits for it instead of compiling it again.
//
// On disk (one directory per libRSOpenGL build (LC_UUID), OS build, cache format and GPU):
//   archive-<seq>.bin  MTLBinaryArchives with the GPU code of the pipelines built so far. New
//                  pipeline descriptors look there first (MTLPipelineOptionFailOnBinaryArchiveMiss
//                  tells a hit from a miss); a miss is compiled as usual and added to a fresh
//                  archive afterwards, on a background queue. See "Archives" below for why there
//                  are several files.
//   manifest.bin   append-only records: the MSL of each library and the rsm_pkey of each pipeline,
//                  in first-use order. At the next start a background queue (utility QoS) compiles
//                  those libraries and prebuilds the pipelines, so the game's first use finds them
//                  done ("warming"). newLibraryWithSource has no binary form we can keep (only
//                  MTLDynamicLibrary serialises, and it cannot hold vertex/fragment functions), so
//                  warming is what hides the MSL front-end compile; the system's Metal cache makes
//                  repeated compiles of the same source cheap too, but may be evicted.
//
// Archive access: adds and serialisation run on one serial queue (concurrent add + serialise
// crashes in Metal); lookups from pipeline creation run on any thread (concurrent with both, as
// checked under stress). The fresh archive is written every RSM_SERIALIZE_EVERY new pipelines, a
// few seconds after the last new one, when the last context goes and at exit; to a temporary file
// renamed into place, so a crash leaves the previous files.
//
// Corrupt or foreign files are discarded (bad manifest records: the file is cut at the first one;
// an archive Metal rejects: deleted). Disk use: the directories of other builds and OS versions
// are deleted at start (only our own files in them); the manifest stops growing at
// RSM_MAX_MANIFEST bytes, the archives at RSM_MAX_ARCHIVE (over it at start: deleted), and more
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
#include "mtl_internal.h"

#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <mach-o/loader.h>
#include <mach/mach_time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <unistd.h>

enum {
    RSM_CACHE_VERSION = 2,       // bump when the manifest or rsm_pkey meaning changes
    RSM_SERIALIZE_EVERY = 32,    // new pipelines between archive writes
    RSM_SERIALIZE_DELAY_MS = 3000,
    RSM_WARM_QUEUES = 2,
    RSM_MAX_ARCHIVE_FILES = 8,
    RSM_MAX_LIBS = 8192,
    RSM_MAX_PIPES = 65536,
};
#define RSM_MAX_MANIFEST (64ull << 20)
#define RSM_MAX_ARCHIVE (1024ull << 20)

static const char kManifestMagic[8] = {'R', 'S', 'M', 'C', 'M', 'A', 'N', '1'};
enum { REC_LIB = 1, REC_PIPE = 2 };

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
    _Atomic int lib_compiled, lib_warmed, lib_reused, lib_waited, lib_failed;
    _Atomic int pipe_hits, pipe_misses, pipe_built, pipe_warmed, pipe_reused, pipe_waited;
    _Atomic uint64_t lib_ns, pipe_ns, wait_ns, warm_ns, serialize_ns;
    _Atomic int serialized, discarded, compacted;
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
    int unsaved;                   // pipelines added to the archive since the last write
    uint64_t save_gen;

    // Warming.
    dispatch_group_t warm;
    _Atomic bool closing;
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

// --- Build-time identity -------------------------------------------------------------------
static void rsm_build_id(char out[33]) {
    strcpy(out, "nouuid");
    Dl_info di;
    if (!dladdr((const void *)&rsm_build_id, &di) || !di.dli_fbase)
        return;
    const struct mach_header_64 *mh = di.dli_fbase;
    if (mh->magic != MH_MAGIC_64)
        return;
    const uint8_t *p = (const uint8_t *)(mh + 1);
    for (uint32_t i = 0; i < mh->ncmds; i++) {
        const struct load_command *lc = (const struct load_command *)p;
        if (lc->cmd == LC_UUID) {
            const struct uuid_command *u = (const struct uuid_command *)lc;
            for (int j = 0; j < 16; j++)
                snprintf(out + j * 2, 3, "%02x", u->uuid[j]);
            return;
        }
        p += lc->cmdsize;
    }
}

static void rsm_sanitize(const char *in, char *out, size_t n) {
    size_t j = 0;
    for (; *in && j + 1 < n; in++)
        out[j++] = ((*in >= 'a' && *in <= 'z') || (*in >= 'A' && *in <= 'Z') || (*in >= '0' && *in <= '9'))
                       ? *in
                       : '_';
    out[j] = 0;
}

// "rsm<version>-<build uuid>-<os build>-": directories with another prefix are stale.
static NSString *rsm_cache_prefix(void) {
    char uuid[33], os[64] = "unknown", oss[64];
    rsm_build_id(uuid);
    size_t n = sizeof os;
    if (sysctlbyname("kern.osversion", os, &n, NULL, 0))
        strcpy(os, "unknown");
    rsm_sanitize(os, oss, sizeof oss);
    return [NSString stringWithFormat:@"rsm%d-%s-%s-", RSM_CACHE_VERSION, uuid, oss];
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
        if (!strcmp(de->d_name, "manifest.bin") || !strncmp(de->d_name, "archive-", 8))
            unlink([path stringByAppendingPathComponent:@(de->d_name)].fileSystemRepresentation);
    }
    closedir(d);
    rmdir(path.fileSystemRepresentation);
}

static void rsm_clean_base(NSString *base, NSString *prefix) {
    DIR *d = opendir(base.fileSystemRepresentation);
    if (!d)
        return;
    struct dirent *de;
    NSMutableArray *stale = [NSMutableArray new];
    while ((de = readdir(d)))
        if (de->d_type == DT_DIR && !strncmp(de->d_name, "rsm", 3) && de->d_name[3] >= '0' &&
            de->d_name[3] <= '9' && ![@(de->d_name) hasPrefix:prefix])
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

// Called on c->io.
static void rsm_manifest_append(RSMShaderCache *c, uint32_t type, const void *a, size_t na, const void *b,
                                size_t nb) {
    if (c->manifest_fd < 0 || c->manifest_bytes + sizeof(record_header) + na + nb > RSM_MAX_MANIFEST)
        return;
    size_t n = sizeof(record_header) + na + nb;
    uint8_t *buf = malloc(n);
    record_header *h = (record_header *)buf;
    memcpy(buf + sizeof *h, a, na);
    if (nb)
        memcpy(buf + sizeof *h + na, b, nb);
    h->type = type;
    h->len = (uint32_t)(na + nb);
    h->check = rsm_hash(buf + sizeof *h, na + nb) ^ type;
    // One write per record (O_APPEND): a crash leaves at most one torn record at the end.
    if (rsm_write_all(c->manifest_fd, buf, n))
        c->manifest_bytes += n;
    free(buf);
}

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
        if (pread(fd, data, size, 0) != (ssize_t)size)
            size = 0;
    }
    manifest_header want;
    memcpy(want.magic, kManifestMagic, 8);
    want.version = RSM_CACHE_VERSION;
    want.pkey_size = sizeof(rsm_pkey);
    bool bad = false;
    if (size >= sizeof want && !memcmp(data, &want, sizeof want)) {
        size_t off = sizeof want;
        NSMutableDictionary<NSNumber *, RSMLibEntry *> *byhash = [NSMutableDictionary new];
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
                if (!byhash[@(hash)] && wl->libs.count < RSM_MAX_LIBS) {
                    RSMLibEntry *e = [RSMLibEntry new];
                    e->msl = msl;
                    e->hash = hash;
                    e->record = true;
                    byhash[@(hash)] = e;
                    [wl->libs addObject:e];
                    [c->recorded_libs addObject:@(hash)];
                    c->n_libs_rec++;
                }
            } else if (h.type == REC_PIPE && h.len == sizeof(pipe_key)) {
                pipe_key pk;
                memcpy(&pk, pl, sizeof pk);
                if (!rsm_pkey_ok(&pk.k) || !byhash[@(pk.lib)])
                    break;
                if (!rsm_map_get(&c->recorded, &pk) && c->n_pipes_rec < RSM_MAX_PIPES) {
                    rsm_map_put(&c->recorded, &pk, @YES);
                    NSMutableArray *a = wl->pipes[@(pk.lib)];
                    if (!a)
                        wl->pipes[@(pk.lib)] = a = [NSMutableArray new];
                    [a addObject:[NSData dataWithBytes:&pk.k length:sizeof pk.k]];
                    c->n_pipes_rec++;
                }
            } else {
                break;
            }
            off += sizeof h + h.len;
            good = off;
        }
        if (good < size) {
            rsm_log("cache: manifest damaged at byte %zu of %zu: dropping the rest", good, size);
            st.discarded++;
        }
    } else {
        bad = size > 0;
        if (bad) {
            rsm_log("cache: manifest is from another format or damaged: starting a new one");
            st.discarded++;
        }
        good = 0;
    }
    free(data);
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
    NSString *path = [c->dir stringByAppendingPathComponent:[NSString stringWithFormat:@"archive-%06d.bin", c->next_seq++]];
    NSString *tmp = [path stringByAppendingFormat:@".tmp.%d", (int)getpid()];
    NSError *e = nil;
    if ([c->fresh serializeToURL:[NSURL fileURLWithPath:tmp] error:&e] &&
        !rename(tmp.fileSystemRepresentation, path.fileSystemRepresentation)) {
        st.serialized++;
        struct stat sb;
        if (!stat(path.fileSystemRepresentation, &sb))
            c->archive_bytes += (uint64_t)sb.st_size;
        [c->files addObject:path];
    } else {
        rsm_log("cache: writing %s failed: %s", path.lastPathComponent.UTF8String,
                e ? e.localizedDescription.UTF8String : strerror(errno));
        unlink(tmp.fileSystemRepresentation);
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
// Under c->m: true = the caller builds (state set to 1); false = done (waited for if needed).
static bool rsm_claim(RSMShaderCache *c, int *state, bool render, _Atomic int *waited) {
    if (*state == 0) {
        *state = 1;
        return true;
    }
    if (*state == 1) {
        uint64_t t0 = render ? rsm_now_ns() : 0;
        while (*state == 1)
            pthread_cond_wait(&c->cv, &c->m);
        if (render) {
            (*waited)++;
            st.wait_ns += rsm_now_ns() - t0;
        }
    }
    return false;
}

static void rsm_publish(RSMShaderCache *c, int *state) {
    pthread_mutex_lock(&c->m);
    __atomic_store_n(state, 2, __ATOMIC_RELEASE);  // rsm_cache_fragment reads it without the lock
    pthread_cond_broadcast(&c->cv);
    pthread_mutex_unlock(&c->m);
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

static void rsm_build_library(RSMShaderCache *c, RSMLibEntry *e, bool render) {
    uint64_t t0 = rsm_now_ns();
    NSError *err = nil;
    e->lib = [c->dev newLibraryWithSource:e->msl options:rsm_compile_options() error:&err];
    if (e->lib) {
        e->vs = [e->lib newFunctionWithName:@"rs_vs"];
    } else {
        e->error = err.localizedDescription ?: @"unknown error";
        st.lib_failed++;
    }
    uint64_t dt = rsm_now_ns() - t0;
    if (render) {
        st.lib_compiled++;
        st.lib_ns += dt;
    } else {
        st.lib_warmed++;
        st.warm_ns += dt;
    }
}

// The entry for this MSL, built (by this thread or another) when it returns.
static RSMLibEntry *rsm_library(RSMShaderCache *c, NSString *msl, bool record, bool render, RSMLibEntry *seed) {
    pthread_mutex_lock(&c->m);
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
    e->record |= record;
    bool mine = rsm_claim(c, &e->state, render, &st.lib_waited);
    pthread_mutex_unlock(&c->m);
    if (mine) {
        rsm_build_library(c, e, render);
        rsm_publish(c, &e->state);
    } else if (render) {
        st.lib_reused++;
    }
    return e;
}

RSMLibEntry *rsm_cache_library(RSMContext *ctx, const char *msl, bool record) {
    RSMShaderCache *c = rsm_cache_for(ctx->dev);
    NSString *s = [[NSString alloc] initWithUTF8String:msl]
                      ?: [[NSString alloc] initWithCString:msl encoding:NSISOLatin1StringEncoding];
    return rsm_library(c, s, record, true, nil);
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

id<MTLFunction> rsm_cache_fragment(RSMLibEntry *e, int alpha) {
    if (!e || !e->lib)
        return nil;
    if (alpha >= 0 && alpha <= 8 && __atomic_load_n(&e->fs_state[alpha], __ATOMIC_ACQUIRE) == 2)
        return e->fs[alpha];
    return rsm_fragment(rsm_cache_for(e->lib.device), e, alpha);
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
        // Compaction: warming copies hits into the fresh archive too (cheap: the code is at hand).
        MTLRenderPipelineDescriptor *add = miss || (c->compacting && !render) ? d : nil;
        rsm_pkey kk = *k;
        dispatch_async(c->io, ^{
            rsm_note_pipeline(c, e, &kk, add);
        });
    }
    return ps;
}

static id<MTLRenderPipelineState> rsm_pipeline_for(RSMShaderCache *c, RSMLibEntry *e, const rsm_pkey *k, bool render) {
    if (!e || !e->lib)
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
    dispatch_queue_attr_t attr =
        dispatch_queue_attr_make_with_qos_class(DISPATCH_QUEUE_SERIAL, QOS_CLASS_UTILITY, 0);
    dispatch_queue_t q[RSM_WARM_QUEUES];
    for (int i = 0; i < RSM_WARM_QUEUES; i++)
        q[i] = dispatch_queue_create("rs.metal.cache.warm", attr);
    NSArray<RSMLibEntry *> *libs = wl->libs;
    NSDictionary *pipes = wl->pipes;
    // Libraries in first-use order, interleaved over the queues; each one's pipelines right after it.
    for (NSUInteger i = 0; i < libs.count; i++) {
        RSMLibEntry *seed = libs[i];
        NSArray<NSData *> *keys = pipes[@(seed->hash)];
        dispatch_group_async(c->warm, q[i % RSM_WARM_QUEUES], ^{
            if (c->closing)
                return;
            @autoreleasepool {
                RSMLibEntry *e = rsm_library(c, seed->msl, true, false, seed);
                for (NSData *kd in keys) {
                    if (c->closing)
                        break;
                    rsm_pipeline_for(c, e, kd.bytes, false);
                }
            }
        });
    }
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

// --- Setup ---------------------------------------------------------------------------------
static void rsm_print_stats(const char *when) {
    if (!rsm_env_on("RS_METAL_CACHE_STATS"))
        return;
    fprintf(stderr,
            "[rs-mtl] cache stats (%s): lib_compiled=%d lib_ms=%.1f lib_reused=%d lib_waited=%d lib_warmed=%d "
            "lib_failed=%d pipe_built=%d pipe_ms=%.1f pipe_reused=%d pipe_waited=%d pipe_warmed=%d "
            "archive_hits=%d archive_misses=%d wait_ms=%.1f warm_ms=%.1f archive_writes=%d write_ms=%.1f "
            "discarded=%d compacted=%d\n",
            when, st.lib_compiled, st.lib_ns / 1e6, st.lib_reused, st.lib_waited, st.lib_warmed, st.lib_failed,
            st.pipe_built, st.pipe_ns / 1e6, st.pipe_reused, st.pipe_waited, st.pipe_warmed, st.pipe_hits,
            st.pipe_misses, st.wait_ns / 1e6, st.warm_ns / 1e6, st.serialized, st.serialize_ns / 1e6, st.discarded,
            st.compacted);
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
    for (RSMShaderCache *c in caches) {
        c->closing = true;
        rsm_flush(c);
    }
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
    rsm_clean_base(base, prefix);
    // Leftovers of interrupted archive writes.
    DIR *d = opendir(dir.fileSystemRepresentation);
    if (d) {
        struct dirent *de;
        while ((de = readdir(d)))
            if (!strncmp(de->d_name, "archive-", 8) && strstr(de->d_name, ".tmp"))
                unlink([dir stringByAppendingPathComponent:@(de->d_name)].fileSystemRepresentation);
        closedir(d);
    }
    c->dir = dir;
    c->persist = true;
    c->io = dispatch_queue_create("rs.metal.cache.io",
                                  dispatch_queue_attr_make_with_qos_class(DISPATCH_QUEUE_SERIAL, QOS_CLASS_UTILITY, 0));
    rsm_map_init(&c->recorded, sizeof(pipe_key));
    c->recorded_libs = [NSMutableSet new];
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
