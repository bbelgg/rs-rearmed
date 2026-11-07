// Metal core: the texture log, a diagnostic for finding which game textures a mod should act on
// (docs/mac/METAL.md). Off unless a switch is set, and then a draw costs one more test:
//
//   RS_GL_TEX_LOG=<file>    a line for each new texture content (level 0): its hash, the checksum RSMods
//                           tags textures with (the first 20 bytes), size and format. <file>.draws is
//                           rewritten every 300 frames: each kind of draw (program, mode, count, vertex
//                           stride, the textures of units 0-3) with how many frames it was in
//   RS_GL_TEX_DUMP=<dir>    each new content as <hash>.dds (DXT) or <hash>.raw, up to 1 GB. Game content: keep
//                           it out of the repository
//   RS_GL_TEX_MARK=<hash,…> textures with these hashes draw as solid magenta, by a rule of the render API
//                           (mtl_render.m): what changes on screen is what uses them
#include "mtl_internal.h"
#include "rsmtl_gfx.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DRAW_SLOTS (1u << 16)
#define SEEN_SLOTS (1u << 18)
#define DUMP_LIMIT (1ull << 30)

typedef struct {
    uint64_t prog, ids[RSM_TEXLOG_UNITS];
    uint32_t mode, count, stride, instances;
    uint64_t draws, frames, first, last;
    bool used;
} draw_entry;

bool rsm_texlog_on;

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static FILE *log_file;
static char draws_path[1024];
static const char *dump_dir;
static uint64_t dump_bytes;
static draw_entry *draws;
static uint64_t *seen;  // hashes logged so far, open addressing
static uint64_t frame, updates;

__attribute__((constructor)) static void texlog_init(void) {
    const char *path = getenv("RS_GL_TEX_LOG");
    dump_dir = getenv("RS_GL_TEX_DUMP");
    if (dump_dir && !*dump_dir)
        dump_dir = NULL;
    static const uint8_t magenta[4] = {255, 0, 255, 255};
    for (const char *m = getenv("RS_GL_TEX_MARK"); m && *m;) {
        char *end;
        uint64_t v = strtoull(m, &end, 16);
        if (end == m)
            break;
        rsmtl_render_replace(rsmtl_render_match(0, 0, v), 1, 1, magenta);
        m = *end ? end + 1 : end;
    }
    if (path && *path) {
        log_file = fopen(path, "w");
        snprintf(draws_path, sizeof draws_path, "%s.draws", path);
        draws = calloc(DRAW_SLOTS, sizeof *draws);
    }
    if (log_file || dump_dir)
        seen = calloc(SEEN_SLOTS, sizeof *seen);
    rsm_texlog_on = log_file || dump_dir;
}

// 1 the first time a hash is seen
static bool first_sight(uint64_t h) {
    if (!seen)
        return false;
    for (uint32_t i = (uint32_t)h & (SEEN_SLOTS - 1), n = 0; n < SEEN_SLOTS; i = (i + 1) & (SEEN_SLOTS - 1), n++) {
        if (seen[i] == h)
            return false;
        if (!seen[i]) {
            seen[i] = h;
            return true;
        }
    }
    return false;
}

// RSMods' QuickCheckSum(data, 20): five words
static uint32_t rsmods_tag(const void *data, size_t bytes) {
    uint32_t w[5] = {0};
    memcpy(w, data, bytes < sizeof w ? bytes : sizeof w);
    uint32_t sum = w[0];
    for (int i = 1; i < 5; i++) {
        uint32_t t = (sum >> 29) + w[i];
        t = (sum >> 17) + t;
        sum = (sum << 3) ^ t;
    }
    return sum;
}

static void dump(uint64_t hash, GLenum internal, int w, int h, const void *data, size_t bytes) {
    if (dump_bytes + bytes > DUMP_LIMIT)
        return;
    const char *four = internal == 0x83F0 || internal == 0x83F1 ? "DXT1"
                       : internal == 0x83F2                     ? "DXT3"
                       : internal == 0x83F3                     ? "DXT5"
                                                                : NULL;
    char path[1200];
    snprintf(path, sizeof path, "%s/%016llx.%s", dump_dir, (unsigned long long)hash, four ? "dds" : "raw");
    FILE *f = fopen(path, "wb");
    if (!f)
        return;
    if (four) {
        uint32_t hd[32] = {0};
        memcpy(hd, "DDS ", 4);
        hd[1] = 124;
        hd[2] = 0x81007;  // caps, height, width, pixel format, linear size
        hd[3] = (uint32_t)h;
        hd[4] = (uint32_t)w;
        hd[5] = (uint32_t)bytes;
        hd[19] = 32;
        hd[20] = 4;  // a four-character code
        memcpy(&hd[21], four, 4);
        hd[27] = 0x1000;  // texture
        fwrite(hd, sizeof hd, 1, f);
    }
    fwrite(data, 1, bytes, f);
    fclose(f);
    dump_bytes += bytes;
}

void rsm_texlog_upload(RSMTexture *t, GLenum internal, GLenum format, GLenum type, int w, int h, const void *data,
                       size_t bytes) {
    uint64_t hash = rsm_hash(data, bytes);
    t->log_id = hash;
    pthread_mutex_lock(&lock);
    if (first_sight(hash)) {
        if (log_file) {
            fprintf(log_file, "upload frame=%llu hash=%016llx tag=%08x size=%dx%d internal=0x%04x format=0x%04x type=0x%04x bytes=%zu\n",
                    (unsigned long long)frame, (unsigned long long)hash, rsmods_tag(data, bytes), w, h, internal, format,
                    type, bytes);
            fflush(log_file);
        }
        if (dump_dir)
            dump(hash, internal, w, h, data, bytes);
    }
    pthread_mutex_unlock(&lock);
}

void rsm_texlog_update(RSMTexture *t) {
    pthread_mutex_lock(&lock);
    updates++;
    if (log_file && t->log_id && first_sight(t->log_id ^ 0x5555555555555555ull)) {
        fprintf(log_file, "update frame=%llu hash=%016llx\n", (unsigned long long)frame, (unsigned long long)t->log_id);
        fflush(log_file);
    }
    pthread_mutex_unlock(&lock);
}

void rsm_texlog_draw(uint64_t prog, GLenum mode, int count, int stride, int instances, const uint64_t *ids) {
    if (!draws)
        return;
    draw_entry key = {.prog = prog, .mode = mode, .count = (uint32_t)count, .stride = (uint32_t)stride, .instances = (uint32_t)instances};
    memcpy(key.ids, ids, sizeof key.ids);
    uint64_t h = rsm_hash(ids, sizeof key.ids) ^ (prog * 31 + mode) ^ ((uint64_t)count << 20) ^ ((uint64_t)stride << 44);
    pthread_mutex_lock(&lock);
    for (uint32_t i = (uint32_t)(h ^ (h >> 32)) & (DRAW_SLOTS - 1), n = 0; n < DRAW_SLOTS; i = (i + 1) & (DRAW_SLOTS - 1), n++) {
        draw_entry *e = &draws[i];
        if (!e->used) {
            *e = key;
            e->used = true;
            e->first = frame;
            e->last = frame - 1;
        } else if (e->prog != key.prog || e->mode != key.mode || e->count != key.count || e->stride != key.stride ||
                   e->instances != key.instances || memcmp(e->ids, key.ids, sizeof key.ids)) {
            continue;
        }
        e->draws++;
        if (e->last != frame)
            e->frames++;
        e->last = frame;
        break;
    }
    pthread_mutex_unlock(&lock);
}

void rsm_texlog_frame(void) {
    if (!rsm_texlog_on)
        return;
    pthread_mutex_lock(&lock);
    frame++;
    if (draws && frame % 300 == 0) {
        char tmp[1100];
        snprintf(tmp, sizeof tmp, "%s.tmp", draws_path);
        FILE *f = fopen(tmp, "w");
        if (f) {
            fprintf(f, "# frame %llu, %llu sub-image updates\n# frames draws first last prog mode count stride instances tex0 tex1 tex2 tex3\n",
                    (unsigned long long)frame, (unsigned long long)updates);
            for (uint32_t i = 0; i < DRAW_SLOTS; i++) {
                const draw_entry *e = &draws[i];
                if (!e->used)
                    continue;
                fprintf(f, "%llu %llu %llu %llu %llu 0x%x %u %u %u", (unsigned long long)e->frames,
                        (unsigned long long)e->draws, (unsigned long long)e->first, (unsigned long long)e->last,
                        (unsigned long long)e->prog, e->mode, e->count, e->stride, e->instances);
                for (int u = 0; u < RSM_TEXLOG_UNITS; u++)
                    fprintf(f, " %016llx", (unsigned long long)e->ids[u]);
                fputc('\n', f);
            }
            fclose(f);
            rename(tmp, draws_path);
        }
    }
    pthread_mutex_unlock(&lock);
}
