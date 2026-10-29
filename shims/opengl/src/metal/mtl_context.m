// Metal core: context, command buffers, transient memory, rs_gfx, CGL, strings
// and errors (docs/METAL-DESIGN.md "Metal core").
#include "mtl_internal.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

__thread __unsafe_unretained RSMContext *rsm_tls;

void rsm_log(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fputs("[rs-mtl] ", stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}

void rsm_error(RSMContext *ctx, GLenum e) {
    if (ctx && ctx->error == GL_NO_ERROR)
        ctx->error = e;
}

// --- Name tables ---------------------------------------------------------------
GLuint rsm_names_alloc(rsm_names *t) {
    GLuint n = t->next ? t->next : 1;
    while (n < t->cap && t->objs[n])
        n++;
    t->next = n + 1;
    return n;
}

void rsm_names_set(rsm_names *t, GLuint n, id obj) {
    if (!n)
        return;
    if (n >= t->cap) {
        if (!obj)
            return;
        uint32_t cap = t->cap ? t->cap : 64;
        while (cap <= n)
            cap *= 2;
        t->objs = realloc(t->objs, cap * sizeof(void *));
        memset(t->objs + t->cap, 0, (cap - t->cap) * sizeof(void *));
        t->cap = cap;
    }
    void *old = t->objs[n];
    t->objs[n] = obj ? (void *)CFBridgingRetain(obj) : NULL;
    if (old)
        CFRelease(old);
    if (!obj && n < t->next)
        t->next = n;
}

void rsm_names_clear(rsm_names *t) {
    for (uint32_t i = 0; i < t->cap; i++)
        if (t->objs[i])
            CFRelease(t->objs[i]);
    free(t->objs);
    memset(t, 0, sizeof *t);
}

// --- Hash map ---------------------------------------------------------------------
uint64_t rsm_hash(const void *p, size_t n) {
    const uint8_t *b = p;
    uint64_t h = 1469598103934665603ull;
    size_t i = 0;
    for (; i + 8 <= n; i += 8) {
        uint64_t v;
        memcpy(&v, b + i, 8);
        h = (h ^ v) * 1099511628211ull;
        h ^= h >> 29;
    }
    for (; i < n; i++)
        h = (h ^ b[i]) * 1099511628211ull;
    h ^= h >> 32;
    return h ? h : 1;
}

void rsm_map_init(rsm_map *m, uint32_t key_size) {
    memset(m, 0, sizeof *m);
    m->key_size = key_size;
}

static void rsm_map_grow(rsm_map *m) {
    rsm_map n = *m;
    n.cap = m->cap ? m->cap * 2 : 64;
    n.count = 0;
    n.keys = calloc(n.cap, m->key_size);
    n.hashes = calloc(n.cap, sizeof(uint64_t));
    n.vals = calloc(n.cap, sizeof(void *));
    for (uint32_t i = 0; i < m->cap; i++) {
        if (!m->hashes[i])
            continue;
        uint32_t j = (uint32_t)m->hashes[i] & (n.cap - 1);
        while (n.hashes[j])
            j = (j + 1) & (n.cap - 1);
        n.hashes[j] = m->hashes[i];
        memcpy(n.keys + (size_t)j * m->key_size, m->keys + (size_t)i * m->key_size, m->key_size);
        n.vals[j] = m->vals[i];
        n.count++;
    }
    free(m->keys);
    free(m->hashes);
    free(m->vals);
    *m = n;
}

id rsm_map_get(rsm_map *m, const void *key) {
    if (!m->cap)
        return nil;
    uint64_t h = rsm_hash(key, m->key_size);
    for (uint32_t j = (uint32_t)h & (m->cap - 1);; j = (j + 1) & (m->cap - 1)) {
        if (!m->hashes[j])
            return nil;
        if (m->hashes[j] == h && !memcmp(m->keys + (size_t)j * m->key_size, key, m->key_size))
            return (__bridge id)m->vals[j];
    }
}

void rsm_map_put(rsm_map *m, const void *key, id val) {
    if ((m->count + 1) * 4 >= m->cap * 3)
        rsm_map_grow(m);
    uint64_t h = rsm_hash(key, m->key_size);
    uint32_t j = (uint32_t)h & (m->cap - 1);
    for (; m->hashes[j]; j = (j + 1) & (m->cap - 1)) {
        if (m->hashes[j] == h && !memcmp(m->keys + (size_t)j * m->key_size, key, m->key_size)) {
            CFRelease(m->vals[j]);
            m->vals[j] = (void *)CFBridgingRetain(val);
            return;
        }
    }
    m->hashes[j] = h;
    memcpy(m->keys + (size_t)j * m->key_size, key, m->key_size);
    m->vals[j] = (void *)CFBridgingRetain(val);
    m->count++;
}

void rsm_map_free(rsm_map *m) {
    for (uint32_t i = 0; i < m->cap; i++)
        if (m->hashes[i])
            CFRelease(m->vals[i]);
    free(m->keys);
    free(m->hashes);
    free(m->vals);
    uint32_t ks = m->key_size;
    memset(m, 0, sizeof *m);
    m->key_size = ks;
}

// --- Object classes ----------------------------------------------------------------
@implementation RSMBuffer
@end
@implementation RSMTexture
@end
@implementation RSMRenderbuffer
@end
@implementation RSMFramebuffer
@end
@implementation RSMVertexArray
@end
@implementation RSMShader
@end

@implementation RSMContext
@end

// --- Command buffers ------------------------------------------------------------
id<MTLCommandBuffer> rsm_cmd(RSMContext *ctx) {
    if (!ctx->cmd) {
        ctx->cmd = [ctx->queue commandBuffer];
        ctx->cmd.label = [NSString stringWithFormat:@"rsgl %llu", (unsigned long long)ctx->serial];
    }
    return ctx->cmd;
}

static void rsm_note_completed(RSMContext *ctx, uint64_t s) {
    uint64_t cur = atomic_load(&ctx->completed);
    while (cur < s && !atomic_compare_exchange_weak(&ctx->completed, &cur, s)) {
    }
}

static void rsm_commit_ex(RSMContext *ctx, bool wait, bool present) {
    rsm_end_pass(ctx);
    if (ctx->cmd) {
        uint64_t s = ctx->serial;
        id<MTLCommandBuffer> cb = ctx->cmd;
        dispatch_semaphore_t sem = present ? ctx->frames : NULL;
        __weak RSMContext *wctx = ctx;
        [cb addCompletedHandler:^(id<MTLCommandBuffer> b) {
          (void)b;
          RSMContext *c = wctx;
          if (c)
              rsm_note_completed(c, s);
          if (sem)
              dispatch_semaphore_signal(sem);
        }];
        if (present)
            dispatch_semaphore_wait(ctx->frames, DISPATCH_TIME_FOREVER);
        [cb commit];
        ctx->last_cmd = cb;
        ctx->cmd = nil;
        ctx->serial++;
        if (wait) {
            [cb waitUntilCompleted];
            rsm_note_completed(ctx, s);
            if (cb.status == MTLCommandBufferStatusError)
                rsm_log("command buffer failed: %s", cb.error.localizedDescription.UTF8String);
        }
    } else if (wait && ctx->last_cmd) {
        [ctx->last_cmd waitUntilCompleted];
        rsm_note_completed(ctx, ctx->serial - 1);
    }
}

void rsm_commit(RSMContext *ctx, bool wait) { rsm_commit_ex(ctx, wait, false); }

// --- Transient memory -------------------------------------------------------------
enum { RSM_CHUNK = 4 << 20 };

void *rsm_transient(RSMContext *ctx, size_t size, size_t align, id<MTLBuffer> *buf,
                    NSUInteger *off) {
    size_t o = (ctx->chunk_off + align - 1) & ~(align - 1);
    if (!ctx->cur_chunk.buf || o + size > RSM_CHUNK_BUF(ctx->cur_chunk).length) {
        if (ctx->cur_chunk.buf) {
            if (ctx->n_spare == ctx->cap_spare) {
                ctx->cap_spare = ctx->cap_spare ? ctx->cap_spare * 2 : 8;
                ctx->spare = realloc(ctx->spare, ctx->cap_spare * sizeof(rsm_chunk));
            }
            ctx->spare[ctx->n_spare++] = ctx->cur_chunk;
            ctx->cur_chunk.buf = NULL;
        }
        uint64_t done = atomic_load(&ctx->completed);
        for (int i = 0; i < ctx->n_spare; i++) {
            if (ctx->spare[i].serial <= done && RSM_CHUNK_BUF(ctx->spare[i]).length >= size) {
                ctx->cur_chunk = ctx->spare[i];
                ctx->spare[i] = ctx->spare[--ctx->n_spare];
                break;
            }
        }
        if (!ctx->cur_chunk.buf) {
            id<MTLBuffer> b = [ctx->dev newBufferWithLength:size > RSM_CHUNK ? size : RSM_CHUNK
                                                    options:MTLResourceStorageModeShared |
                                                            MTLResourceCPUCacheModeWriteCombined];
            b.label = @"rsgl transient";
            ctx->cur_chunk.buf = (void *)CFBridgingRetain(b);
        }
        o = 0;
    }
    ctx->cur_chunk.serial = ctx->serial;
    ctx->chunk_off = o + size;
    id<MTLBuffer> b = RSM_CHUNK_BUF(ctx->cur_chunk);
    *buf = b;
    *off = o;
    return (uint8_t *)b.contents + o;
}

// Retired buffers wait in a FIFO in serial order; the front ones are the first to be
// free again, so the search stops at the first one still in flight.
enum { RSM_POOL_MAX = 4096 };
static const size_t RSM_POOL_BYTES = 256u << 20;

id<MTLBuffer> rsm_new_buffer(RSMContext *ctx, size_t size) {
    if (!size)
        size = 4;
    uint64_t done = atomic_load(&ctx->completed);
    for (int i = 0; i < ctx->n_pool && ctx->pool[i].serial <= done; i++) {
        if (RSM_CHUNK_BUF(ctx->pool[i]).length == size) {
            id<MTLBuffer> b = CFBridgingRelease(ctx->pool[i].buf);
            ctx->pool_bytes -= size;
            memmove(ctx->pool + i, ctx->pool + i + 1, (size_t)(ctx->n_pool - i - 1) * sizeof(rsm_chunk));
            ctx->n_pool--;
            return b;
        }
    }
    return [ctx->dev newBufferWithLength:size options:ctx->buf_opts];
}

void rsm_retire_buffer(RSMContext *ctx, id<MTLBuffer> b, uint64_t serial) {
    if (!b)
        return;
    while (ctx->n_pool && (ctx->n_pool >= RSM_POOL_MAX || ctx->pool_bytes + b.length > RSM_POOL_BYTES)) {
        // drop the oldest
        ctx->pool_bytes -= RSM_CHUNK_BUF(ctx->pool[0]).length;
        CFRelease(ctx->pool[0].buf);
        memmove(ctx->pool, ctx->pool + 1, (size_t)(ctx->n_pool - 1) * sizeof(rsm_chunk));
        ctx->n_pool--;
    }
    if (ctx->n_pool == ctx->cap_pool) {
        ctx->cap_pool = ctx->cap_pool ? ctx->cap_pool * 2 : 16;
        ctx->pool = realloc(ctx->pool, (size_t)ctx->cap_pool * sizeof(rsm_chunk));
    }
    // Keep the FIFO sorted by serial (a buffer unused for a while may retire with an old one).
    int i = ctx->n_pool;
    while (i > 0 && ctx->pool[i - 1].serial > serial)
        i--;
    memmove(ctx->pool + i + 1, ctx->pool + i, (size_t)(ctx->n_pool - i) * sizeof(rsm_chunk));
    ctx->pool[i] = (rsm_chunk){(void *)CFBridgingRetain(b), serial};
    ctx->n_pool++;
    ctx->pool_bytes += b.length;
}

// --- Contexts -------------------------------------------------------------------
static pthread_mutex_t g_ctx_lock = PTHREAD_MUTEX_INITIALIZER;
static RSMContext *__unsafe_unretained g_ctxs[64];

static bool rsm_known(void *p) {
    bool ok = false;
    pthread_mutex_lock(&g_ctx_lock);
    for (int i = 0; i < 64; i++)
        ok |= (__bridge void *)g_ctxs[i] == p && p;
    pthread_mutex_unlock(&g_ctx_lock);
    return ok;
}

static void rsm_default_fb(RSMContext *ctx) {
    MTLTextureDescriptor *d =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                           width:(NSUInteger)ctx->w
                                                          height:(NSUInteger)ctx->h
                                                       mipmapped:NO];
    d.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    d.storageMode = MTLStorageModePrivate;
    ctx->def_color = [ctx->dev newTextureWithDescriptor:d];
    ctx->def_color.label = @"rsgl default color";
    d.pixelFormat = MTLPixelFormatDepth32Float_Stencil8;
    d.usage = MTLTextureUsageRenderTarget;
    ctx->def_ds = [ctx->dev newTextureWithDescriptor:d];
    ctx->def_ds.label = @"rsgl default depth/stencil";
}

static void rsm_init_state(RSMContext *ctx) {
    ctx->blend_src_rgb = ctx->blend_src_a = GL_ONE;
    ctx->blend_dst_rgb = ctx->blend_dst_a = GL_ZERO;
    ctx->blend_eq_rgb = ctx->blend_eq_a = GL_FUNC_ADD;
    for (int i = 0; i < 4; i++)
        ctx->color_mask[i] = true;
    ctx->depth_mask = true;
    ctx->depth_func = GL_LESS;
    for (int f = 0; f < 2; f++)
        ctx->stencil[f] = (rsm_stencil_face){GL_ALWAYS, 0, ~0u, GL_KEEP, GL_KEEP, GL_KEEP, ~0u};
    ctx->viewport[2] = ctx->scissor[2] = ctx->w;
    ctx->viewport[3] = ctx->scissor[3] = ctx->h;
    ctx->depth_range[1] = 1;
    ctx->cull_mode = GL_BACK;
    ctx->front_face = GL_CCW;
    ctx->polygon_mode[0] = ctx->polygon_mode[1] = GL_FILL;
    ctx->alpha_func = GL_ALWAYS;
    ctx->clear_depth = 1;
    ctx->dither = true;
    for (int i = 0; i < RSM_MAX_ATTRIBS; i++)
        ctx->vattr[i][3] = 1;
    ctx->unpack_alignment = ctx->pack_alignment = 4;
    ctx->fb_dirty = true;
    ctx->dirty = RSM_DIRTY_ALL;
}

static RSMContext *rsm_create(int w, int h, CAMetalLayer *layer) {
    id<MTLDevice> dev = layer && layer.device ? layer.device : MTLCreateSystemDefaultDevice();
    if (!dev) {
        rsm_log("no Metal device");
        return nil;
    }
    RSMContext *ctx = [RSMContext new];
    ctx->dev = dev;
    ctx->queue = [dev newCommandQueueWithMaxCommandBufferCount:64];
    ctx->serial = 1;
    ctx->frames = dispatch_semaphore_create(RSM_FRAMES_IN_FLIGHT);
    pthread_mutexattr_t a;
    pthread_mutexattr_init(&a);
    pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&ctx->lock, &a);
    pthread_mutexattr_destroy(&a);
    bool unified = dev.hasUnifiedMemory;
    ctx->buf_opts = unified ? MTLResourceStorageModeShared : MTLResourceStorageModeManaged;
    ctx->tex_storage = unified ? MTLStorageModeShared : MTLStorageModeManaged;
    ctx->w = w > 0 ? w : 1;
    ctx->h = h > 0 ? h : 1;
    ctx->layer = layer;
    if (layer) {
        layer.device = dev;
        layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
        layer.framebufferOnly = YES;
        layer.drawableSize = CGSizeMake(ctx->w, ctx->h);
    }
    rsm_default_fb(ctx);
    ctx->vao0 = [RSMVertexArray new];
    ctx->vao = ctx->vao0;
    rsm_map_init(&ctx->ds_states, sizeof(uint64_t));
    rsm_map_init(&ctx->samplers, sizeof(uint64_t));
    rsm_map_init(&ctx->clear_pipelines, sizeof(uint64_t));
    rsm_init_state(ctx);
    rsm_cache_context_created(ctx);
    rsm_draw_init(ctx);
    pthread_mutex_lock(&g_ctx_lock);
    for (int i = 0; i < 64; i++)
        if (!g_ctxs[i]) {
            g_ctxs[i] = ctx;
            break;
        }
    pthread_mutex_unlock(&g_ctx_lock);
    return ctx;
}

rs_gfx_context *rsmtl_gfx_create_offscreen(int w, int h) {
    @autoreleasepool {
        RSMContext *ctx = rsm_create(w, h, nil);
        return ctx ? (rs_gfx_context *)CFBridgingRetain(ctx) : NULL;
    }
}

rs_gfx_context *rsmtl_gfx_create_layer(void *layer, int w, int h) {
    @autoreleasepool {
        RSMContext *ctx = rsm_create(w, h, (__bridge CAMetalLayer *)layer);
        return ctx ? (rs_gfx_context *)CFBridgingRetain(ctx) : NULL;
    }
}

void rsmtl_gfx_destroy(rs_gfx_context *c) {
    if (!c)
        return;
    @autoreleasepool {
        RSMContext *ctx = (__bridge RSMContext *)c;
        RSMContext *prev = rsm_tls;
        rsm_tls = ctx;
        rsm_commit(ctx, true);
        rsm_tls = prev == ctx ? nil : prev;
        pthread_mutex_lock(&g_ctx_lock);
        for (int i = 0; i < 64; i++)
            if (g_ctxs[i] == ctx)
                g_ctxs[i] = nil;
        pthread_mutex_unlock(&g_ctx_lock);
        rsm_names_clear(&ctx->buffers);
        rsm_names_clear(&ctx->textures);
        rsm_names_clear(&ctx->renderbuffers);
        rsm_names_clear(&ctx->framebuffers);
        rsm_names_clear(&ctx->vaos);
        rsm_names_clear(&ctx->objects);
        rsm_map_free(&ctx->ds_states);
        rsm_map_free(&ctx->samplers);
        rsm_map_free(&ctx->clear_pipelines);
        if (ctx->cur_chunk.buf)
            CFRelease(ctx->cur_chunk.buf);
        for (int i = 0; i < ctx->n_spare; i++)
            CFRelease(ctx->spare[i].buf);
        for (int i = 0; i < ctx->n_pool; i++)
            CFRelease(ctx->pool[i].buf);
        free(ctx->spare);
        free(ctx->pool);
        ctx->prog = nil;
        rsm_cache_context_destroyed(ctx);
        CFRelease(c);
    }
}

void rsmtl_gfx_make_current(rs_gfx_context *c) { rsm_tls = (__bridge RSMContext *)c; }

rs_gfx_context *rsmtl_gfx_current(void) { return (__bridge rs_gfx_context *)rsm_tls; }

void rsmtl_gfx_resize(rs_gfx_context *c, int w, int h) {
    RSMContext *ctx = (__bridge RSMContext *)c;
    if (!ctx || w <= 0 || h <= 0 || (w == ctx->w && h == ctx->h))
        return;
    @autoreleasepool {
        RSMContext *prev = rsm_tls;
        rsm_tls = ctx;
        rsm_end_pass(ctx);
        rsm_tls = prev;
        ctx->w = w;
        ctx->h = h;
        rsm_default_fb(ctx);
        if (ctx->layer)
            ctx->layer.drawableSize = CGSizeMake(w, h);
        if (!ctx->draw_fb)
            ctx->fb_dirty = true;
    }
}

void rsmtl_gfx_present(rs_gfx_context *c) {
    RSMContext *ctx = (__bridge RSMContext *)c;
    if (!ctx)
        return;
    @autoreleasepool {
        RSMContext *prev = rsm_tls;
        rsm_tls = ctx;
        rsm_end_pass(ctx);
        if (ctx->layer) {
            id<CAMetalDrawable> d = [ctx->layer nextDrawable];
            if (d) {
                rsm_present_draw(ctx, d.texture);
                [rsm_cmd(ctx) presentDrawable:d];
            }
        }
        rsm_commit_ex(ctx, false, true);
        // The next frame starts with a fresh pass on the default framebuffer.
        ctx->fb_dirty = true;
        rsm_tls = prev;
    }
}

void rsmtl_gfx_lock(rs_gfx_context *c) {
    if (c)
        pthread_mutex_lock(&((__bridge RSMContext *)c)->lock);
}

void rsmtl_gfx_unlock(rs_gfx_context *c) {
    if (c)
        pthread_mutex_unlock(&((__bridge RSMContext *)c)->lock);
}

CGLContextObj rsmtl_gfx_cgl(rs_gfx_context *c) { return (CGLContextObj)c; }

// --- CGL ---------------------------------------------------------------------------
CGLError rsmtl_CGLLockContext(CGLContextObj c) {
    void *p = rsm_known(c) ? (void *)c : (__bridge void *)rsm_tls;
    if (!p)
        return kCGLBadContext;
    pthread_mutex_lock(&((__bridge RSMContext *)p)->lock);
    return kCGLNoError;
}

CGLError rsmtl_CGLUnlockContext(CGLContextObj c) {
    void *p = rsm_known(c) ? (void *)c : (__bridge void *)rsm_tls;
    if (!p)
        return kCGLBadContext;
    pthread_mutex_unlock(&((__bridge RSMContext *)p)->lock);
    return kCGLNoError;
}

CGLContextObj rsmtl_CGLGetCurrentContext(void) { return (CGLContextObj)(__bridge void *)rsm_tls; }

CGLError rsmtl_CGLSetCurrentContext(CGLContextObj c) {
    if (c && !rsm_known(c))
        return kCGLBadContext;
    rsm_tls = (__bridge RSMContext *)(void *)c;
    return kCGLNoError;
}

CGLError rsmtl_CGLFlushDrawable(CGLContextObj c) {
    void *p = rsm_known(c) ? (void *)c : (__bridge void *)rsm_tls;
    if (!p)
        return kCGLBadContext;
    rsmtl_gfx_present((rs_gfx_context *)p);
    return kCGLNoError;
}

// --- Errors, strings, sync ----------------------------------------------------------
GLenum rsmtl_glGetError(void) {
    RSM_CTX(GL_NO_ERROR)
    GLenum e = ctx->error;
    ctx->error = GL_NO_ERROR;
    return e;
}

// Apple's legacy 2.1 list (Apple M2, macOS 26), less what the Metal core doesn't
// provide (ARB assembly programs, fixed-function extras, geometry shaders, ...).
// The game checks EXT_bgra and EXT_texture_compression_s3tc; Scaleform checks
// GL_ARB_get_program_binary, which Apple doesn't list either.
static const char *const kExtensions[] = {
    "GL_ARB_color_buffer_float", "GL_ARB_depth_buffer_float", "GL_ARB_depth_texture",
    "GL_ARB_draw_elements_base_vertex", "GL_ARB_draw_instanced", "GL_ARB_fragment_shader",
    "GL_ARB_framebuffer_object", "GL_ARB_half_float_pixel", "GL_ARB_half_float_vertex",
    "GL_ARB_instanced_arrays", "GL_ARB_multitexture", "GL_ARB_shader_objects",
    "GL_ARB_shading_language_100", "GL_ARB_texture_border_clamp", "GL_ARB_texture_compression",
    "GL_ARB_texture_cube_map", "GL_ARB_texture_float", "GL_ARB_texture_mirrored_repeat",
    "GL_ARB_texture_non_power_of_two", "GL_ARB_texture_rg", "GL_ARB_vertex_array_bgra",
    "GL_ARB_vertex_buffer_object", "GL_ARB_vertex_shader", "GL_EXT_abgr", "GL_EXT_bgra",
    "GL_EXT_blend_color", "GL_EXT_blend_equation_separate", "GL_EXT_blend_func_separate",
    "GL_EXT_blend_minmax", "GL_EXT_blend_subtract", "GL_EXT_framebuffer_object",
    "GL_EXT_packed_depth_stencil", "GL_EXT_stencil_two_side", "GL_EXT_stencil_wrap",
    "GL_EXT_texture_compression_dxt1", "GL_EXT_texture_compression_s3tc",
    "GL_EXT_texture_filter_anisotropic", "GL_EXT_vertex_array_bgra", "GL_APPLE_packed_pixels",
    "GL_APPLE_vertex_array_object", "GL_ATI_separate_stencil", "GL_SGIS_generate_mipmap",
    "GL_SGIS_texture_edge_clamp", "GL_SGIS_texture_lod",
};
enum { RSM_N_EXT = sizeof kExtensions / sizeof kExtensions[0] };

int rsm_num_extensions(void) { return RSM_N_EXT; }

const GLubyte *rsmtl_glGetString(GLenum name) {
    RSM_CTX(NULL)
    static char renderer[128], extensions[4096];
    static dispatch_once_t once;
    dispatch_once(&once, ^{
      snprintf(renderer, sizeof renderer, "%s", ctx->dev.name.UTF8String);
      size_t o = 0;
      for (int i = 0; i < RSM_N_EXT; i++)
          o += (size_t)snprintf(extensions + o, sizeof extensions - o, "%s ", kExtensions[i]);
    });
    switch (name) {
    case GL_VENDOR: return (const GLubyte *)"Apple";
    case GL_RENDERER: return (const GLubyte *)renderer;
    case GL_VERSION: return (const GLubyte *)"2.1 Metal - RS Re-ARMed";
    case GL_SHADING_LANGUAGE_VERSION: return (const GLubyte *)"1.20";
    case GL_EXTENSIONS: return (const GLubyte *)extensions;
    }
    rsm_error(ctx, GL_INVALID_ENUM);
    return NULL;
}

const GLubyte *rsmtl_glGetStringi(GLenum name, GLuint index) {
    RSM_CTX(NULL)
    if (name != GL_EXTENSIONS) {
        rsm_error(ctx, GL_INVALID_ENUM);
        return NULL;
    }
    if (index >= RSM_N_EXT) {
        rsm_error(ctx, GL_INVALID_VALUE);
        return NULL;
    }
    return (const GLubyte *)kExtensions[index];
}

void rsmtl_glFlush(void) {
    RSM_CTX()
    @autoreleasepool {
        rsm_commit(ctx, false);
    }
}

void rsmtl_glFinish(void) {
    RSM_CTX()
    @autoreleasepool {
        rsm_commit(ctx, true);
    }
}
