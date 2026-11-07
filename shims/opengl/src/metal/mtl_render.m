// Metal core: the rules of the render API (shims/include/rs_render.h, docs/mac/METAL.md). An upload of level 0 finds its
// rule by size and hash and keeps it on the texture object; a draw that binds such a texture asks the rule what to do.
// A texture without a rule costs a draw one test, and with no rules made an upload costs one test too. A mod gives pixels
// from any thread: they wait here for the next draw, on the thread that has the device.
#include "mtl_internal.h"
#include "rsmtl_gfx.h"

#include <os/lock.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_RULES 64
#define MAX_SIDE 4096

struct rsm_rule {
    int w, h;  // 0: any size
    uint64_t hash;
    _Atomic bool hide;
    bool found;  // logged the first match
    // Under `lock`
    bool pending;     // new pixels, or back to the game's texture (rgba NULL)
    void *rgba;       // pending pixels
    int new_w, new_h;
    id<MTLTexture> tex;  // nil: the game's texture
};

static struct rsm_rule rules[MAX_RULES];
static os_unfair_lock lock = OS_UNFAIR_LOCK_INIT;
_Atomic int rsm_render_rules;

rs_tex_rule *rsmtl_render_match(int w, int h, uint64_t hash) {
    if (w < 0 || h < 0)
        return NULL;
    struct rsm_rule *r = NULL;
    os_unfair_lock_lock(&lock);
    int n = atomic_load(&rsm_render_rules);
    for (int i = 0; i < n && !r; i++)
        if (rules[i].w == w && rules[i].h == h && rules[i].hash == hash)
            r = &rules[i];
    if (!r && n < MAX_RULES) {
        r = &rules[n];
        r->w = w;
        r->h = h;
        r->hash = hash;
        atomic_store(&rsm_render_rules, n + 1);  // the rule is complete: an upload may read it
    }
    os_unfair_lock_unlock(&lock);
    return (rs_tex_rule *)r;
}

int rsmtl_render_replace(rs_tex_rule *rule, int w, int h, const void *rgba) {
    struct rsm_rule *r = (struct rsm_rule *)rule;
    if (!r)
        return -1;
    void *copy = NULL;
    if (rgba) {
        if (w < 1 || h < 1 || w > MAX_SIDE || h > MAX_SIDE)
            return -1;
        size_t n = (size_t)w * (size_t)h * 4;
        if (!(copy = malloc(n)))
            return -1;
        memcpy(copy, rgba, n);
    }
    os_unfair_lock_lock(&lock);
    void *old = r->rgba;
    r->rgba = copy;
    r->new_w = w;
    r->new_h = h;
    r->pending = true;
    os_unfair_lock_unlock(&lock);
    free(old);
    return 0;
}

int rsmtl_render_hide(rs_tex_rule *rule, int on) {
    struct rsm_rule *r = (struct rsm_rule *)rule;
    if (!r)
        return -1;
    atomic_store(&r->hide, on != 0);
    return 0;
}

uint64_t rsmtl_render_hash(const void *data, size_t bytes) { return rsm_hash(data, bytes); }

// The rule of an upload of level 0
static struct rsm_rule *rule_of(int w, int h, const void *data, size_t bytes) {
    int n = atomic_load(&rsm_render_rules);
    uint64_t hash = 0;
    bool hashed = false;
    for (int i = 0; i < n; i++) {
        struct rsm_rule *r = &rules[i];
        if ((r->w || r->h) && (r->w != w || r->h != h))
            continue;
        if (!hashed) {
            hash = rsm_hash(data, bytes);
            hashed = true;
        }
        if (r->hash == hash) {
            if (!r->found) {
                r->found = true;
                fprintf(stderr, "[rs-gl] render: a rule found its texture, %dx%d, hash %016llx\n", w, h,
                        (unsigned long long)hash);
            }
            return r;
        }
    }
    return NULL;
}

void rsm_tex_uploaded(RSMTexture *t, GLenum internal, GLenum format, GLenum type, int w, int h, const void *data,
                      size_t bytes) {
    if (rsm_texlog_on)
        rsm_texlog_upload(t, internal, format, type, w, h, data, bytes);
    t->rule = t->target == GL_TEXTURE_2D ? rule_of(w, h, data, bytes) : NULL;
}

// What a draw binds instead of a texture with a rule (nil: the texture), and whether the draw is hidden.
id<MTLTexture> rsm_rule_texture(RSMContext *ctx, struct rsm_rule *r, bool *hide) {
    if (atomic_load(&r->hide))
        *hide = true;
    os_unfair_lock_lock(&lock);
    void *rgba = NULL;
    int w = 0, h = 0;
    bool pending = r->pending;
    if (pending) {
        rgba = r->rgba;
        w = r->new_w;
        h = r->new_h;
        r->rgba = NULL;
        r->pending = false;
    }
    id<MTLTexture> tex = r->tex;
    os_unfair_lock_unlock(&lock);
    if (!pending)
        return tex;
    // The old texture stays alive as long as a command buffer that bound it.
    id<MTLTexture> made = nil;
    if (rgba) {
        MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                                     width:(NSUInteger)w
                                                                                    height:(NSUInteger)h
                                                                                 mipmapped:NO];
        d.storageMode = MTLStorageModeShared;
        d.usage = MTLTextureUsageShaderRead;
        made = [ctx->dev newTextureWithDescriptor:d];
        [made replaceRegion:MTLRegionMake2D(0, 0, (NSUInteger)w, (NSUInteger)h) mipmapLevel:0 withBytes:rgba
                bytesPerRow:(NSUInteger)w * 4];
        free(rgba);
    }
    os_unfair_lock_lock(&lock);
    r->tex = made;
    os_unfair_lock_unlock(&lock);
    return made;
}
