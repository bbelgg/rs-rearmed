// Metal core: render passes, glClear, pipeline / depth-stencil state caches,
// per-draw state, vertex fetch, draws and present.
#include "mtl_internal.h"

#include <stdlib.h>
#include <string.h>

static NSString *const kInternalMSL =
    @"#include <metal_stdlib>\n"
     "using namespace metal;\n"
     "struct rsm_clear_u { float4 color; float depth; };\n"
     "struct rsm_pos { float4 pos [[position]]; };\n"
     "vertex rsm_pos rsm_clear_vs(uint vid [[vertex_id]], constant rsm_clear_u &u [[buffer(0)]]) {\n"
     "  float2 p = float2((vid << 1) & 2, vid & 2) * 2.0 - 1.0;\n"
     "  rsm_pos o; o.pos = float4(p, u.depth, 1.0); return o;\n"
     "}\n"
     "fragment float4 rsm_clear_fs(constant rsm_clear_u &u [[buffer(0)]]) { return u.color; }\n"
     "fragment void rsm_clear_fs_none() {}\n"
     "struct rsm_blit_v { float4 pos [[position]]; float2 uv; };\n"
     "vertex rsm_blit_v rsm_blit_vs(uint vid [[vertex_id]]) {\n"
     "  float2 p = float2((vid << 1) & 2, vid & 2) * 2.0 - 1.0;\n"
     "  rsm_blit_v o; o.pos = float4(p, 0.0, 1.0); o.uv = p * 0.5 + 0.5; return o;\n"
     "}\n"
     // Row 0 of the colour texture is GL's bottom row: uv.y = 1 at the top of the drawable.
     "fragment float4 rsm_blit_fs(rsm_blit_v in [[stage_in]], texture2d<float> t [[texture(0)]],\n"
     "                            sampler s [[sampler(0)]]) {\n"
     "  return float4(t.sample(s, in.uv).rgb, 1.0);\n"
     "}\n";

static id<MTLTexture> rsm_dummy(RSMContext *ctx, bool cube) {
    MTLTextureDescriptor *d = [MTLTextureDescriptor new];
    d.textureType = cube ? MTLTextureTypeCube : MTLTextureType2D;
    d.pixelFormat = MTLPixelFormatRGBA8Unorm;
    d.width = d.height = 1;
    d.storageMode = MTLStorageModeShared;
    d.usage = MTLTextureUsageShaderRead;
    id<MTLTexture> t = [ctx->dev newTextureWithDescriptor:d];
    static const uint8_t black[4] = {0, 0, 0, 255};  // an incomplete GL texture samples (0,0,0,1)
    for (int s = 0; s < (cube ? 6 : 1); s++)
        [t replaceRegion:MTLRegionMake2D(0, 0, 1, 1) mipmapLevel:0 slice:(NSUInteger)s withBytes:black
             bytesPerRow:4 bytesPerImage:4];
    return t;
}

void rsm_draw_init(RSMContext *ctx) {
    RSMLibEntry *le = rsm_cache_library(ctx, kInternalMSL.UTF8String, false);
    ctx->internal_lib = le->lib;
    if (!ctx->internal_lib)
        rsm_log("internal shaders: %s", le->error.UTF8String);
    ctx->dummy_tex = rsm_dummy(ctx, false);
    ctx->dummy_cube = rsm_dummy(ctx, true);
    MTLSamplerDescriptor *sd = [MTLSamplerDescriptor new];
    ctx->present_sampler = [ctx->dev newSamplerStateWithDescriptor:sd];
}

// --- Render pass ------------------------------------------------------------------------
static RSMTexture *rsm_att_object(RSMContext *ctx, const rsm_attachment *a) {
    return a->type == GL_TEXTURE ? rsm_get(&ctx->textures, a->name) : nil;
}

// Resolve the draw framebuffer's attachments into ctx->pass_*.
static void rsm_pass_targets(RSMContext *ctx) {
    ctx->pass_color = ctx->pass_depth = ctx->pass_stencil = nil;
    ctx->pass_color_level = ctx->pass_color_slice = 0;
    if (!ctx->draw_fb) {
        ctx->pass_color = ctx->def_color;
        ctx->pass_depth = ctx->pass_stencil = ctx->def_ds;
    } else {
        RSMFramebuffer *fb = rsm_get(&ctx->framebuffers, ctx->draw_fb);
        id<MTLTexture> t;
        int l, s;
        if (fb && rsm_attachment_texture(ctx, &fb->att[RSM_ATT_COLOR0], &t, &l, &s)) {
            ctx->pass_color = t;
            ctx->pass_color_level = l;
            ctx->pass_color_slice = s;
        }
        if (fb && rsm_attachment_texture(ctx, &fb->att[RSM_ATT_DEPTH], &t, &l, &s) &&
            (t.pixelFormat == MTLPixelFormatDepth32Float || t.pixelFormat == MTLPixelFormatDepth32Float_Stencil8))
            ctx->pass_depth = t;
        if (fb && rsm_attachment_texture(ctx, &fb->att[RSM_ATT_STENCIL], &t, &l, &s) &&
            (t.pixelFormat == MTLPixelFormatStencil8 || t.pixelFormat == MTLPixelFormatDepth32Float_Stencil8))
            ctx->pass_stencil = t;
        if (ctx->pass_color && (ctx->pass_color.pixelFormat == MTLPixelFormatDepth32Float ||
                                ctx->pass_color.pixelFormat == MTLPixelFormatDepth32Float_Stencil8))
            ctx->pass_color = nil;
    }
    ctx->pass_color_fmt = ctx->pass_color ? ctx->pass_color.pixelFormat : MTLPixelFormatInvalid;
    ctx->pass_depth_fmt = ctx->pass_depth ? ctx->pass_depth.pixelFormat : MTLPixelFormatInvalid;
    ctx->pass_stencil_fmt = ctx->pass_stencil ? ctx->pass_stencil.pixelFormat : MTLPixelFormatInvalid;
    int w = 1 << 30, h = 1 << 30;
    if (ctx->pass_color) {
        int cw = (int)ctx->pass_color.width >> ctx->pass_color_level;
        int ch = (int)ctx->pass_color.height >> ctx->pass_color_level;
        w = cw > 0 ? cw : 1;
        h = ch > 0 ? ch : 1;
    }
    id<MTLTexture> ds[2] = {ctx->pass_depth, ctx->pass_stencil};
    for (int i = 0; i < 2; i++)
        if (ds[i]) {
            w = (int)ds[i].width < w ? (int)ds[i].width : w;
            h = (int)ds[i].height < h ? (int)ds[i].height : h;
        }
    if (w == 1 << 30)
        w = h = 0;
    ctx->pass_w = w;
    ctx->pass_h = h;
    ctx->fb_dirty = false;
}

static void rsm_mark_targets(RSMContext *ctx) {
    if (!ctx->draw_fb)
        return;
    RSMFramebuffer *fb = rsm_get(&ctx->framebuffers, ctx->draw_fb);
    for (int i = 0; fb && i < RSM_ATT_COUNT; i++) {
        RSMTexture *t = rsm_att_object(ctx, &fb->att[i]);
        if (t)
            t->last_use = ctx->serial;
    }
}

static bool rsm_begin_pass(RSMContext *ctx) {
    if (ctx->enc)
        return true;
    if (ctx->fb_dirty)
        rsm_pass_targets(ctx);
    if (!ctx->pass_color && !ctx->pass_depth && !ctx->pass_stencil) {
        ctx->pending_clear = 0;
        return false;
    }
    MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
    unsigned pc = ctx->pending_clear;
    if (ctx->pass_color) {
        MTLRenderPassColorAttachmentDescriptor *c = rp.colorAttachments[0];
        c.texture = ctx->pass_color;
        c.level = (NSUInteger)ctx->pass_color_level;
        c.slice = (NSUInteger)ctx->pass_color_slice;
        c.loadAction = pc & GL_COLOR_BUFFER_BIT ? MTLLoadActionClear : MTLLoadActionLoad;
        c.storeAction = MTLStoreActionStore;
        c.clearColor = MTLClearColorMake(ctx->pending_color[0], ctx->pending_color[1], ctx->pending_color[2],
                                         ctx->pending_color[3]);
    }
    if (ctx->pass_depth) {
        rp.depthAttachment.texture = ctx->pass_depth;
        rp.depthAttachment.loadAction = pc & GL_DEPTH_BUFFER_BIT ? MTLLoadActionClear : MTLLoadActionLoad;
        rp.depthAttachment.storeAction = MTLStoreActionStore;
        rp.depthAttachment.clearDepth = ctx->pending_depth;
    }
    if (ctx->pass_stencil) {
        rp.stencilAttachment.texture = ctx->pass_stencil;
        rp.stencilAttachment.loadAction = pc & GL_STENCIL_BUFFER_BIT ? MTLLoadActionClear : MTLLoadActionLoad;
        rp.stencilAttachment.storeAction = MTLStoreActionStore;
        rp.stencilAttachment.clearStencil = (uint32_t)ctx->pending_stencil & 0xFF;
    }
    if (!ctx->pass_color)
        rp.renderTargetWidth = (NSUInteger)ctx->pass_w, rp.renderTargetHeight = (NSUInteger)ctx->pass_h;
    ctx->enc = [rsm_cmd(ctx) renderCommandEncoderWithDescriptor:rp];
    ctx->pending_clear = 0;
    rsm_mark_targets(ctx);
    ctx->dirty = RSM_DIRTY_ALL;
    ctx->cur_pipeline = nil;
    ctx->cur_pkey_valid = false;
    ctx->cur_ds = nil;
    ctx->cur_prog_uid = 0;
    memset(ctx->enc_vb, 0, sizeof ctx->enc_vb);
    memset(ctx->enc_vb_off, 0, sizeof ctx->enc_vb_off);
    memset(ctx->enc_tex, 0, sizeof ctx->enc_tex);
    memset(ctx->enc_smp, 0, sizeof ctx->enc_smp);
    return ctx->enc != nil;
}

void rsm_end_pass(RSMContext *ctx) {
    if (!ctx->enc && ctx->pending_clear)
        rsm_begin_pass(ctx);  // clears with no draw after them still happen
    if (ctx->enc) {
        [ctx->enc endEncoding];
        ctx->enc = nil;
    }
}

// --- Depth / stencil state ---------------------------------------------------------------
static unsigned rsm_sop(GLenum op) {
    switch (op) {
    case GL_KEEP: return MTLStencilOperationKeep;
    case GL_ZERO: return MTLStencilOperationZero;
    case GL_REPLACE: return MTLStencilOperationReplace;
    case GL_INCR: return MTLStencilOperationIncrementClamp;
    case GL_DECR: return MTLStencilOperationDecrementClamp;
    case GL_INVERT: return MTLStencilOperationInvert;
    case GL_INCR_WRAP: return MTLStencilOperationIncrementWrap;
    case GL_DECR_WRAP: return MTLStencilOperationDecrementWrap;
    }
    return MTLStencilOperationKeep;
}

static id<MTLDepthStencilState> rsm_ds_state(RSMContext *ctx, uint64_t key) {
    id<MTLDepthStencilState> s = rsm_map_get(&ctx->ds_states, &key);
    if (s)
        return s;
    MTLDepthStencilDescriptor *d = [MTLDepthStencilDescriptor new];
    d.depthCompareFunction = (MTLCompareFunction)(key & 7);
    d.depthWriteEnabled = (key >> 3) & 1;
    if ((key >> 4) & 1) {
        for (int f = 0; f < 2; f++) {
            uint64_t k = key >> (5 + 28 * f);
            MTLStencilDescriptor *sd = [MTLStencilDescriptor new];
            sd.stencilCompareFunction = (MTLCompareFunction)(k & 7);
            sd.stencilFailureOperation = (MTLStencilOperation)((k >> 3) & 7);
            sd.depthFailureOperation = (MTLStencilOperation)((k >> 6) & 7);
            sd.depthStencilPassOperation = (MTLStencilOperation)((k >> 9) & 7);
            sd.readMask = (uint32_t)((k >> 12) & 0xFF);
            sd.writeMask = (uint32_t)((k >> 20) & 0xFF);
            if (f)
                d.backFaceStencil = sd;
            else
                d.frontFaceStencil = sd;
        }
    }
    s = [ctx->dev newDepthStencilStateWithDescriptor:d];
    rsm_map_put(&ctx->ds_states, &key, s);
    return s;
}

static uint64_t rsm_ds_key(RSMContext *ctx) {
    bool depth = ctx->depth_test && ctx->pass_depth;
    uint64_t key = depth ? (uint64_t)(ctx->depth_func - GL_NEVER) : MTLCompareFunctionAlways;
    key |= (uint64_t)(depth && ctx->depth_mask) << 3;
    if (ctx->stencil_test && ctx->pass_stencil) {
        key |= 1u << 4;
        for (int f = 0; f < 2; f++) {
            const rsm_stencil_face *s = &ctx->stencil[f];
            uint64_t k = (uint64_t)(s->func - GL_NEVER) | (uint64_t)rsm_sop(s->fail) << 3 |
                         (uint64_t)rsm_sop(s->zfail) << 6 | (uint64_t)rsm_sop(s->zpass) << 9 |
                         (uint64_t)(s->mask & 0xFF) << 12 | (uint64_t)(s->writemask & 0xFF) << 20;
            key |= k << (5 + 28 * f);
        }
    }
    return key;
}

// --- Dynamic state ------------------------------------------------------------------------
static bool rsm_apply_dynamic(RSMContext *ctx) {
    id<MTLRenderCommandEncoder> e = ctx->enc;
    unsigned d = ctx->dirty;
    if (d & RSM_DIRTY_VIEWPORT) {
        MTLViewport v = {ctx->viewport[0], ctx->viewport[1], ctx->viewport[2], ctx->viewport[3],
                         ctx->depth_range[0], ctx->depth_range[1]};
        [e setViewport:v];
    }
    if (d & (RSM_DIRTY_SCISSOR | RSM_DIRTY_VIEWPORT)) {
        int x0 = 0, y0 = 0, x1 = ctx->pass_w, y1 = ctx->pass_h;
        if (ctx->scissor_test) {
            x0 = ctx->scissor[0] > 0 ? ctx->scissor[0] : 0;
            y0 = ctx->scissor[1] > 0 ? ctx->scissor[1] : 0;
            int sx1 = ctx->scissor[0] + ctx->scissor[2], sy1 = ctx->scissor[1] + ctx->scissor[3];
            x1 = sx1 < x1 ? sx1 : x1;
            y1 = sy1 < y1 ? sy1 : y1;
        }
        ctx->skip_raster = x1 <= x0 || y1 <= y0;
        if (!ctx->skip_raster)
            [e setScissorRect:(MTLScissorRect){(NSUInteger)x0, (NSUInteger)y0, (NSUInteger)(x1 - x0),
                                               (NSUInteger)(y1 - y0)}];
    }
    if (d & RSM_DIRTY_RASTER) {
        [e setCullMode:!ctx->cull || ctx->cull_mode == GL_FRONT_AND_BACK ? MTLCullModeNone
                       : ctx->cull_mode == GL_FRONT                     ? MTLCullModeFront
                                                                        : MTLCullModeBack];
        // The vertex epilogue's y flip reverses the winding.
        [e setFrontFacingWinding:ctx->front_face == GL_CCW ? MTLWindingClockwise : MTLWindingCounterClockwise];
        [e setTriangleFillMode:ctx->polygon_mode[0] == GL_FILL ? MTLTriangleFillModeFill : MTLTriangleFillModeLines];
    }
    if (d & RSM_DIRTY_STENCIL_REF)
        [e setStencilFrontReferenceValue:(uint32_t)ctx->stencil[0].ref & 0xFF
                      backReferenceValue:(uint32_t)ctx->stencil[1].ref & 0xFF];
    if (d & RSM_DIRTY_BLEND_COLOR)
        [e setBlendColorRed:ctx->blend_color[0] green:ctx->blend_color[1] blue:ctx->blend_color[2]
                      alpha:ctx->blend_color[3]];
    if (d & RSM_DIRTY_DEPTH) {
        id<MTLDepthStencilState> s = rsm_ds_state(ctx, rsm_ds_key(ctx));
        if (s != ctx->cur_ds) {
            [e setDepthStencilState:s];
            ctx->cur_ds = s;
        }
    }
    ctx->dirty &= ~(unsigned)(RSM_DIRTY_VIEWPORT | RSM_DIRTY_SCISSOR | RSM_DIRTY_RASTER | RSM_DIRTY_STENCIL_REF |
                              RSM_DIRTY_BLEND_COLOR | RSM_DIRTY_DEPTH);
    return !ctx->skip_raster;
}

// --- glClear ---------------------------------------------------------------------------------
static id<MTLRenderPipelineState> rsm_clear_pipeline(RSMContext *ctx, bool color, unsigned mask) {
    uint64_t key = (uint64_t)ctx->pass_color_fmt | (uint64_t)ctx->pass_depth_fmt << 16 |
                   (uint64_t)ctx->pass_stencil_fmt << 32 | (uint64_t)mask << 48 | (uint64_t)color << 52;
    id<MTLRenderPipelineState> ps = rsm_map_get(&ctx->clear_pipelines, &key);
    if (ps)
        return ps;
    MTLRenderPipelineDescriptor *d = [MTLRenderPipelineDescriptor new];
    d.vertexFunction = [ctx->internal_lib newFunctionWithName:@"rsm_clear_vs"];
    d.fragmentFunction = [ctx->internal_lib newFunctionWithName:color ? @"rsm_clear_fs" : @"rsm_clear_fs_none"];
    if (color) {
        d.colorAttachments[0].pixelFormat = ctx->pass_color_fmt;
        d.colorAttachments[0].writeMask = mask;
    }
    d.depthAttachmentPixelFormat = ctx->pass_depth_fmt;
    d.stencilAttachmentPixelFormat = ctx->pass_stencil_fmt;
    NSError *e = nil;
    ps = [ctx->dev newRenderPipelineStateWithDescriptor:d error:&e];
    if (!ps) {
        rsm_log("clear pipeline: %s", e.localizedDescription.UTF8String);
        return nil;
    }
    rsm_map_put(&ctx->clear_pipelines, &key, ps);
    return ps;
}

// Clear with a full-screen triangle: honours the scissor and the write masks.
static void rsm_clear_quad(RSMContext *ctx, unsigned mask) {
    if (!rsm_begin_pass(ctx))
        return;
    id<MTLRenderCommandEncoder> e = ctx->enc;
    unsigned cm = 0;
    if (mask & GL_COLOR_BUFFER_BIT)
        cm = (ctx->color_mask[0] ? MTLColorWriteMaskRed : 0) | (ctx->color_mask[1] ? MTLColorWriteMaskGreen : 0) |
             (ctx->color_mask[2] ? MTLColorWriteMaskBlue : 0) | (ctx->color_mask[3] ? MTLColorWriteMaskAlpha : 0);
    bool color = ctx->pass_color != nil;
    id<MTLRenderPipelineState> ps = rsm_clear_pipeline(ctx, color, cm);
    if (!ps)
        return;
    bool depth = (mask & GL_DEPTH_BUFFER_BIT) && ctx->depth_mask && ctx->pass_depth;
    bool stencil = (mask & GL_STENCIL_BUFFER_BIT) && ctx->pass_stencil;
    uint64_t key = MTLCompareFunctionAlways | (uint64_t)depth << 3;
    if (stencil) {
        uint64_t k = MTLCompareFunctionAlways | (uint64_t)MTLStencilOperationReplace << 3 |
                     (uint64_t)MTLStencilOperationReplace << 6 | (uint64_t)MTLStencilOperationReplace << 9 |
                     (uint64_t)0xFF << 12;
        key |= 1u << 4 | (k | (uint64_t)(ctx->stencil[0].writemask & 0xFF) << 20) << 5 |
               (k | (uint64_t)(ctx->stencil[1].writemask & 0xFF) << 20) << 33;
    }
    [e setRenderPipelineState:ps];
    [e setDepthStencilState:rsm_ds_state(ctx, key)];
    [e setStencilReferenceValue:(uint32_t)ctx->clear_stencil & 0xFF];
    [e setViewport:(MTLViewport){0, 0, ctx->pass_w, ctx->pass_h, 0, 1}];
    [e setCullMode:MTLCullModeNone];
    [e setTriangleFillMode:MTLTriangleFillModeFill];
    ctx->dirty = RSM_DIRTY_SCISSOR;
    rsm_apply_dynamic(ctx);  // scissor only (the rest is re-applied before the next draw)
    ctx->dirty = RSM_DIRTY_ALL;
    ctx->cur_pipeline = nil;
    ctx->cur_pkey_valid = false;
    ctx->cur_ds = nil;
    if (ctx->skip_raster)
        return;
    struct {
        float color[4];
        float depth;
        float pad[3];
    } u = {{ctx->clear_color[0], ctx->clear_color[1], ctx->clear_color[2], ctx->clear_color[3]},
           (float)ctx->clear_depth, {0}};
    [e setVertexBytes:&u length:sizeof u atIndex:0];
    [e setFragmentBytes:&u length:sizeof u atIndex:0];
    [e drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
}

void rsmtl_glClear(GLbitfield mask) {
    RSM_CTX()
    if (mask & ~(GLbitfield)(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT)) {
        rsm_error(ctx, GL_INVALID_VALUE);
        return;
    }
    @autoreleasepool {
        if (!ctx->enc && ctx->fb_dirty)
            rsm_pass_targets(ctx);
        if (!ctx->pass_color)
            mask &= ~(GLbitfield)GL_COLOR_BUFFER_BIT;
        if (!ctx->pass_depth)
            mask &= ~(GLbitfield)GL_DEPTH_BUFFER_BIT;
        if (!ctx->pass_stencil)
            mask &= ~(GLbitfield)GL_STENCIL_BUFFER_BIT;
        if (!mask)
            return;
        bool full = !ctx->scissor_test ||
                    (ctx->scissor[0] <= 0 && ctx->scissor[1] <= 0 && ctx->scissor[0] + ctx->scissor[2] >= ctx->pass_w &&
                     ctx->scissor[1] + ctx->scissor[3] >= ctx->pass_h);
        unsigned quad = mask;
        if (!ctx->enc && full) {
            // Before the first draw of the pass: load actions.
            if ((mask & GL_COLOR_BUFFER_BIT) && ctx->color_mask[0] && ctx->color_mask[1] && ctx->color_mask[2] &&
                ctx->color_mask[3]) {
                ctx->pending_clear |= GL_COLOR_BUFFER_BIT;
                memcpy(ctx->pending_color, ctx->clear_color, sizeof ctx->pending_color);
                quad &= ~(unsigned)GL_COLOR_BUFFER_BIT;
            }
            if ((mask & GL_DEPTH_BUFFER_BIT) && ctx->depth_mask) {
                ctx->pending_clear |= GL_DEPTH_BUFFER_BIT;
                ctx->pending_depth = ctx->clear_depth;
                quad &= ~(unsigned)GL_DEPTH_BUFFER_BIT;
            } else if (mask & GL_DEPTH_BUFFER_BIT) {
                quad &= ~(unsigned)GL_DEPTH_BUFFER_BIT;  // masked: nothing to write
            }
            if ((mask & GL_STENCIL_BUFFER_BIT) && (ctx->stencil[0].writemask & 0xFF) == 0xFF &&
                (ctx->stencil[1].writemask & 0xFF) == 0xFF) {
                ctx->pending_clear |= GL_STENCIL_BUFFER_BIT;
                ctx->pending_stencil = ctx->clear_stencil;
                quad &= ~(unsigned)GL_STENCIL_BUFFER_BIT;
            }
        }
        if (quad)
            rsm_clear_quad(ctx, quad);
    }
}

// --- Vertex formats ----------------------------------------------------------------------------
static MTLVertexFormat rsm_vertex_format(GLint size, GLenum type, bool norm) {
    if (size == GL_BGRA)
        return type == GL_UNSIGNED_BYTE ? MTLVertexFormatUChar4Normalized_BGRA : MTLVertexFormatInvalid;
    static const MTLVertexFormat F[] = {MTLVertexFormatFloat, MTLVertexFormatFloat2, MTLVertexFormatFloat3, MTLVertexFormatFloat4};
    static const MTLVertexFormat H[] = {MTLVertexFormatHalf, MTLVertexFormatHalf2, MTLVertexFormatHalf3, MTLVertexFormatHalf4};
    static const MTLVertexFormat UB[] = {MTLVertexFormatUChar, MTLVertexFormatUChar2, MTLVertexFormatUChar3, MTLVertexFormatUChar4};
    static const MTLVertexFormat UBN[] = {MTLVertexFormatUCharNormalized, MTLVertexFormatUChar2Normalized,
                                          MTLVertexFormatUChar3Normalized, MTLVertexFormatUChar4Normalized};
    static const MTLVertexFormat B[] = {MTLVertexFormatChar, MTLVertexFormatChar2, MTLVertexFormatChar3, MTLVertexFormatChar4};
    static const MTLVertexFormat BN[] = {MTLVertexFormatCharNormalized, MTLVertexFormatChar2Normalized,
                                         MTLVertexFormatChar3Normalized, MTLVertexFormatChar4Normalized};
    static const MTLVertexFormat US[] = {MTLVertexFormatUShort, MTLVertexFormatUShort2, MTLVertexFormatUShort3, MTLVertexFormatUShort4};
    static const MTLVertexFormat USN[] = {MTLVertexFormatUShortNormalized, MTLVertexFormatUShort2Normalized,
                                          MTLVertexFormatUShort3Normalized, MTLVertexFormatUShort4Normalized};
    static const MTLVertexFormat S[] = {MTLVertexFormatShort, MTLVertexFormatShort2, MTLVertexFormatShort3, MTLVertexFormatShort4};
    static const MTLVertexFormat SN[] = {MTLVertexFormatShortNormalized, MTLVertexFormatShort2Normalized,
                                         MTLVertexFormatShort3Normalized, MTLVertexFormatShort4Normalized};
    static const MTLVertexFormat I[] = {MTLVertexFormatInt, MTLVertexFormatInt2, MTLVertexFormatInt3, MTLVertexFormatInt4};
    static const MTLVertexFormat UI[] = {MTLVertexFormatUInt, MTLVertexFormatUInt2, MTLVertexFormatUInt3, MTLVertexFormatUInt4};
    if (size < 1 || size > 4)
        return MTLVertexFormatInvalid;
    int i = size - 1;
    switch (type) {
    case GL_FLOAT: return F[i];
    case GL_HALF_FLOAT: return H[i];
    case GL_UNSIGNED_BYTE: return norm ? UBN[i] : UB[i];
    case GL_BYTE: return norm ? BN[i] : B[i];
    case GL_UNSIGNED_SHORT: return norm ? USN[i] : US[i];
    case GL_SHORT: return norm ? SN[i] : S[i];
    case GL_INT: return I[i];
    case GL_UNSIGNED_INT: return UI[i];
    }
    return MTLVertexFormatInvalid;
}

static size_t rsm_attrib_bytes(const rsm_attrib *a) {
    if (a->size == GL_BGRA)
        return 4;
    size_t t = a->type == GL_FLOAT || a->type == GL_INT || a->type == GL_UNSIGNED_INT ? 4
               : a->type == GL_HALF_FLOAT || a->type == GL_SHORT || a->type == GL_UNSIGNED_SHORT ? 2
                                                                                                    : 1;
    return t * (size_t)a->size;
}

// --- Pipelines -------------------------------------------------------------------------------
static uint8_t rsm_blend_factor(GLenum f) {
    switch (f) {
    case GL_ZERO: return MTLBlendFactorZero;
    case GL_ONE: return MTLBlendFactorOne;
    case GL_SRC_COLOR: return MTLBlendFactorSourceColor;
    case GL_ONE_MINUS_SRC_COLOR: return MTLBlendFactorOneMinusSourceColor;
    case GL_DST_COLOR: return MTLBlendFactorDestinationColor;
    case GL_ONE_MINUS_DST_COLOR: return MTLBlendFactorOneMinusDestinationColor;
    case GL_SRC_ALPHA: return MTLBlendFactorSourceAlpha;
    case GL_ONE_MINUS_SRC_ALPHA: return MTLBlendFactorOneMinusSourceAlpha;
    case GL_DST_ALPHA: return MTLBlendFactorDestinationAlpha;
    case GL_ONE_MINUS_DST_ALPHA: return MTLBlendFactorOneMinusDestinationAlpha;
    case GL_CONSTANT_COLOR: return MTLBlendFactorBlendColor;
    case GL_ONE_MINUS_CONSTANT_COLOR: return MTLBlendFactorOneMinusBlendColor;
    case GL_CONSTANT_ALPHA: return MTLBlendFactorBlendAlpha;
    case GL_ONE_MINUS_CONSTANT_ALPHA: return MTLBlendFactorOneMinusBlendAlpha;
    case GL_SRC_ALPHA_SATURATE: return MTLBlendFactorSourceAlphaSaturated;
    }
    return MTLBlendFactorOne;
}

static uint8_t rsm_blend_op(GLenum e) {
    switch (e) {
    case GL_FUNC_SUBTRACT: return MTLBlendOperationSubtract;
    case GL_FUNC_REVERSE_SUBTRACT: return MTLBlendOperationReverseSubtract;
    case GL_MIN: return MTLBlendOperationMin;
    case GL_MAX: return MTLBlendOperationMax;
    }
    return MTLBlendOperationAdd;
}

MTLRenderPipelineDescriptor *rsm_pipeline_desc(id<MTLFunction> vs, id<MTLFunction> fs, const rsm_pkey *k) {
    MTLRenderPipelineDescriptor *d = [MTLRenderPipelineDescriptor new];
    d.vertexFunction = vs;
    d.fragmentFunction = fs;
    MTLVertexDescriptor *vd = [MTLVertexDescriptor vertexDescriptor];
    for (int i = 0; i < RSM_MAX_ATTRIBS; i++) {
        if (!k->vl.fmt[i])
            continue;
        vd.attributes[i].format = (MTLVertexFormat)k->vl.fmt[i];
        vd.attributes[i].offset = k->vl.offset[i];
        vd.attributes[i].bufferIndex = (NSUInteger)(RSM_VB_BASE + i);
        MTLVertexBufferLayoutDescriptor *l = vd.layouts[RSM_VB_BASE + i];
        l.stride = k->vl.stride[i];
        l.stepFunction = k->vl.step[i] == RSM_STEP_CONSTANT   ? MTLVertexStepFunctionConstant
                         : k->vl.step[i] == RSM_STEP_INSTANCE ? MTLVertexStepFunctionPerInstance
                                                              : MTLVertexStepFunctionPerVertex;
        l.stepRate = k->vl.step[i] == RSM_STEP_CONSTANT ? 0 : k->vl.rate[i];
    }
    d.vertexDescriptor = vd;
    if (k->color_fmt) {
        MTLRenderPipelineColorAttachmentDescriptor *c = d.colorAttachments[0];
        c.pixelFormat = k->color_fmt;
        c.writeMask = k->color_mask;
        c.blendingEnabled = k->blend;
        c.sourceRGBBlendFactor = k->src_rgb;
        c.destinationRGBBlendFactor = k->dst_rgb;
        c.sourceAlphaBlendFactor = k->src_a;
        c.destinationAlphaBlendFactor = k->dst_a;
        c.rgbBlendOperation = k->eq_rgb;
        c.alphaBlendOperation = k->eq_a;
    }
    d.depthAttachmentPixelFormat = k->depth_fmt;
    d.stencilAttachmentPixelFormat = k->stencil_fmt;
    return d;
}

// Per program first (no lock), then the shared cache (built once per MSL and key).
static id<MTLRenderPipelineState> rsm_pipeline(RSMContext *ctx, RSMProgram *p, const rsm_pkey *k) {
    id<MTLRenderPipelineState> ps = rsm_map_get(&p->pipelines, k);
    if (ps)
        return ps;
    ps = rsm_cache_pipeline(ctx, p->entry, k);
    if (ps)
        rsm_map_put(&p->pipelines, k, ps);
    return ps;
}

// --- Draws ----------------------------------------------------------------------------------
typedef struct {
    MTLPrimitiveType prim;
    bool convert;  // needs generated indices (fan, loop, quads, polygon)
    bool tri;
} rsm_prim;

static bool rsm_primitive(GLenum mode, rsm_prim *p) {
    p->convert = false;
    p->tri = true;
    switch (mode) {
    case GL_POINTS: p->prim = MTLPrimitiveTypePoint; p->tri = false; return true;
    case GL_LINES: p->prim = MTLPrimitiveTypeLine; p->tri = false; return true;
    case GL_LINE_STRIP: p->prim = MTLPrimitiveTypeLineStrip; p->tri = false; return true;
    case GL_LINE_LOOP: p->prim = MTLPrimitiveTypeLineStrip; p->tri = false; p->convert = true; return true;
    case GL_TRIANGLES: p->prim = MTLPrimitiveTypeTriangle; return true;
    case GL_TRIANGLE_STRIP: case GL_QUAD_STRIP: p->prim = MTLPrimitiveTypeTriangleStrip; return true;
    case GL_TRIANGLE_FAN: case GL_QUADS: case GL_POLYGON:
        p->prim = MTLPrimitiveTypeTriangle; p->convert = true; return true;
    }
    return false;
}

static inline uint32_t rsm_index(const void *src, GLenum type, size_t i) {
    switch (type) {
    case GL_UNSIGNED_BYTE: return ((const uint8_t *)src)[i];
    case GL_UNSIGNED_SHORT: return ((const uint16_t *)src)[i];
    default: return ((const uint32_t *)src)[i];
    }
}

static size_t rsm_index_size(GLenum type) {
    return type == GL_UNSIGNED_BYTE ? 1 : type == GL_UNSIGNED_SHORT ? 2 : 4;
}

static void rsm_draw(RSMContext *ctx, GLenum mode, GLint first, GLsizei count, GLenum itype, const void *indices,
                     GLsizei instances, bool indexed) {
    rsm_prim pr;
    if (!rsm_primitive(mode, &pr) ||
        (indexed && itype != GL_UNSIGNED_BYTE && itype != GL_UNSIGNED_SHORT && itype != GL_UNSIGNED_INT)) {
        rsm_error(ctx, GL_INVALID_ENUM);
        return;
    }
    if (count < 0 || instances < 0 || first < 0) {
        rsm_error(ctx, GL_INVALID_VALUE);
        return;
    }
    RSMProgram *p = ctx->prog;
    if (!count || !instances || !p || !p->linked)
        return;
    if (mode == GL_QUAD_STRIP)
        count &= ~1;
    if (pr.tri && ctx->cull && ctx->cull_mode == GL_FRONT_AND_BACK)
        return;
    @autoreleasepool {
        // Index source.
        const uint8_t *isrc = NULL;
        id<MTLBuffer> ibuf = nil;
        NSUInteger ioff = 0;
        RSMBuffer *eb = nil;
        if (indexed) {
            eb = rsm_get(&ctx->buffers, ctx->vao->element_buffer);
            if (eb) {
                if (!eb->mtl || (size_t)indices + (size_t)count * rsm_index_size(itype) > eb->size) {
                    rsm_error(ctx, GL_INVALID_OPERATION);
                    return;
                }
                isrc = (const uint8_t *)eb->mtl.contents + (size_t)indices;
            } else {
                if (!indices)
                    return;
                isrc = indices;
            }
        }
        // Does any attribute need its data copied (client arrays, misaligned)?
        uint32_t used = p->attrib_mask;
        bool copy = false;
        for (int i = 0; i < RSM_MAX_ATTRIBS; i++) {
            if (!(used & (1u << i)))
                continue;
            const rsm_attrib *a = &ctx->vao->a[i];
            if (!a->enabled)
                continue;
            size_t es = rsm_attrib_bytes(a), stride = a->stride ? (size_t)a->stride : es;
            if (!a->buffer || ((uintptr_t)a->ptr & 3) || (stride & 3))
                copy = true;
        }
        // Vertex range (only needed for copies).
        uint32_t vmin = (uint32_t)first, vmax = (uint32_t)first + (uint32_t)count - 1;
        if (indexed && copy) {
            vmin = UINT32_MAX;
            vmax = 0;
            for (GLsizei i = 0; i < count; i++) {
                uint32_t v = rsm_index(isrc, itype, (size_t)i);
                vmin = v < vmin ? v : vmin;
                vmax = v > vmax ? v : vmax;
            }
        }
        // Indices for Metal.
        MTLIndexType mit = MTLIndexTypeUInt16;
        NSUInteger icount = (NSUInteger)count;
        bool use_index = indexed || pr.convert;
        if (pr.convert) {
            size_t n = (size_t)count, out_n = 0;
            if (mode == GL_LINE_LOOP)
                out_n = n + 1;
            else if (mode == GL_QUADS)
                out_n = n / 4 * 6;
            else
                out_n = n >= 3 ? (n - 2) * 3 : 0;
            if (!out_n)
                return;
            uint32_t *d = rsm_transient(ctx, out_n * 4, 4, &ibuf, &ioff);
            size_t o = 0;
#define IDX(k) (indexed ? rsm_index(isrc, itype, (k)) : (uint32_t)first + (uint32_t)(k))
            if (mode == GL_LINE_LOOP) {
                for (size_t k = 0; k < n; k++)
                    d[o++] = IDX(k);
                d[o++] = IDX(0);
            } else if (mode == GL_QUADS) {
                for (size_t q = 0; q + 3 < n; q += 4) {
                    uint32_t a = IDX(q), b = IDX(q + 1), c = IDX(q + 2), e = IDX(q + 3);
                    d[o++] = a; d[o++] = b; d[o++] = c;
                    d[o++] = a; d[o++] = c; d[o++] = e;
                }
            } else {
                uint32_t a = IDX(0);
                for (size_t k = 1; k + 1 < n; k++) {
                    d[o++] = a;
                    d[o++] = IDX(k);
                    d[o++] = IDX(k + 1);
                }
            }
#undef IDX
            mit = MTLIndexTypeUInt32;
            icount = o;
        } else if (indexed) {
            if (itype == GL_UNSIGNED_BYTE || !eb || ((size_t)indices & 3)) {
                size_t is = itype == GL_UNSIGNED_INT ? 4 : 2;
                uint8_t *d = rsm_transient(ctx, (size_t)count * is, 4, &ibuf, &ioff);
                if (itype == GL_UNSIGNED_BYTE)
                    for (GLsizei k = 0; k < count; k++)
                        ((uint16_t *)d)[k] = isrc[k];
                else
                    memcpy(d, isrc, (size_t)count * is);
                mit = is == 4 ? MTLIndexTypeUInt32 : MTLIndexTypeUInt16;
            } else {
                ibuf = eb->mtl;
                ioff = (NSUInteger)(size_t)indices;
                mit = itype == GL_UNSIGNED_INT ? MTLIndexTypeUInt32 : MTLIndexTypeUInt16;
                eb->last_use = ctx->serial;
            }
        }

        if (!rsm_begin_pass(ctx))
            return;
        id<MTLRenderCommandEncoder> e = ctx->enc;

        // Pipeline key.
        rsm_pkey k;
        memset(&k, 0, sizeof k);
        for (int i = 0; i < RSM_MAX_ATTRIBS; i++) {
            if (!(used & (1u << i)))
                continue;
            const rsm_attrib *a = &ctx->vao->a[i];
            MTLVertexFormat f = a->enabled ? rsm_vertex_format(a->size, a->type, a->normalized) : MTLVertexFormatInvalid;
            if (!a->enabled || f == MTLVertexFormatInvalid) {
                k.vl.fmt[i] = MTLVertexFormatFloat4;
                k.vl.step[i] = RSM_STEP_CONSTANT;
                k.vl.stride[i] = 16;
                continue;
            }
            size_t es = rsm_attrib_bytes(a), stride = a->stride ? (size_t)a->stride : es;
            k.vl.fmt[i] = (uint8_t)f;
            k.vl.step[i] = a->divisor ? RSM_STEP_INSTANCE : RSM_STEP_VERTEX;
            k.vl.rate[i] = a->divisor ? a->divisor : 1;
            bool direct = a->buffer && !((uintptr_t)a->ptr & 3) && !(stride & 3) && (a->divisor || !copy);
            k.vl.stride[i] = (uint16_t)(direct ? stride : (es + 3) & ~(size_t)3);
        }
        k.color_fmt = (uint16_t)ctx->pass_color_fmt;
        k.depth_fmt = (uint16_t)ctx->pass_depth_fmt;
        k.stencil_fmt = (uint16_t)ctx->pass_stencil_fmt;
        k.blend = ctx->blend;
        k.color_mask = (ctx->color_mask[0] ? MTLColorWriteMaskRed : 0) | (ctx->color_mask[1] ? MTLColorWriteMaskGreen : 0) |
                       (ctx->color_mask[2] ? MTLColorWriteMaskBlue : 0) | (ctx->color_mask[3] ? MTLColorWriteMaskAlpha : 0);
        k.alpha_func = ctx->alpha_test && ctx->alpha_func != GL_ALWAYS ? (uint8_t)(ctx->alpha_func - GL_NEVER) : 8;
        if (ctx->blend) {
            k.src_rgb = rsm_blend_factor(ctx->blend_src_rgb);
            k.dst_rgb = rsm_blend_factor(ctx->blend_dst_rgb);
            k.src_a = rsm_blend_factor(ctx->blend_src_a);
            k.dst_a = rsm_blend_factor(ctx->blend_dst_a);
            k.eq_rgb = rsm_blend_op(ctx->blend_eq_rgb);
            k.eq_a = rsm_blend_op(ctx->blend_eq_a);
        }
        if (!ctx->cur_pkey_valid || ctx->cur_prog_uid != p->uid || memcmp(&k, &ctx->cur_pkey, sizeof k)) {
            id<MTLRenderPipelineState> ps = rsm_pipeline(ctx, p, &k);
            if (!ps)
                return;
            if (ps != ctx->cur_pipeline) {
                [e setRenderPipelineState:ps];
                ctx->cur_pipeline = ps;
            }
            ctx->cur_pkey = k;
            ctx->cur_pkey_valid = true;
            ctx->dirty |= RSM_DIRTY_ALPHA_REF;
        }
        if (!rsm_apply_dynamic(ctx))
            return;

        // Vertex buffers.
        for (int i = 0; i < RSM_MAX_ATTRIBS; i++) {
            if (!(used & (1u << i)))
                continue;
            int slot = RSM_VB_BASE + i;
            if (slot > 30)
                continue;
            const rsm_attrib *a = &ctx->vao->a[i];
            if (k.vl.step[i] == RSM_STEP_CONSTANT) {
                [e setVertexBytes:ctx->vattr[i] length:16 atIndex:(NSUInteger)slot];
                ctx->enc_vb[i] = nil;
                continue;
            }
            size_t es = rsm_attrib_bytes(a), stride = a->stride ? (size_t)a->stride : es;
            RSMBuffer *b = a->buffer ? rsm_get(&ctx->buffers, a->buffer) : nil;
            if (a->buffer && (!b || !b->mtl)) {
                [e setVertexBytes:ctx->vattr[i] length:16 atIndex:(NSUInteger)slot];
                ctx->enc_vb[i] = nil;
                continue;
            }
            const uint8_t *src = b ? (const uint8_t *)b->mtl.contents + (size_t)a->ptr : a->ptr;
            bool inst = a->divisor != 0;
            bool aligned = b && !((uintptr_t)a->ptr & 3) && !(stride & 3);
            if (aligned && (inst || !copy)) {
                NSUInteger off = (NSUInteger)(size_t)a->ptr;
                if (ctx->enc_vb[i] != b->mtl) {
                    [e setVertexBuffer:b->mtl offset:off atIndex:(NSUInteger)slot];
                    ctx->enc_vb[i] = b->mtl;
                    ctx->enc_vb_off[i] = off;
                } else if (ctx->enc_vb_off[i] != off) {
                    [e setVertexBufferOffset:off atIndex:(NSUInteger)slot];
                    ctx->enc_vb_off[i] = off;
                }
                b->last_use = ctx->serial;
                continue;
            }
            // Copy the elements the draw reads into transient memory, packed.
            size_t ns = k.vl.stride[i];
            uint32_t lo = inst ? 0 : vmin;
            uint32_t n = inst ? ((uint32_t)instances + a->divisor - 1) / a->divisor : vmax - vmin + 1;
            if (b && (size_t)a->ptr + (size_t)(lo + n - 1) * stride + es > b->size) {
                n = 0;  // out of range: GL would read garbage; draw nothing from it
            }
            id<MTLBuffer> tb;
            NSUInteger toff;
            uint8_t *d = rsm_transient(ctx, (size_t)(n ? n : 1) * ns, 4, &tb, &toff);
            for (uint32_t v = 0; v < n; v++)
                memcpy(d + (size_t)v * ns, src + (size_t)(lo + v) * stride, es);
            [e setVertexBuffer:tb offset:toff atIndex:(NSUInteger)slot];
            ctx->enc_vb[i] = nil;
            if (b)
                b->last_use = ctx->serial;
        }

        // Uniforms.
        if (ctx->cur_prog_uid != p->uid || p->vs_dirty || p->fs_dirty) {
            bool all = ctx->cur_prog_uid != p->uid;
            for (int st = 0; st < 2; st++) {
                if (!all && !(st ? p->fs_dirty : p->vs_dirty))
                    continue;
                const uint8_t *blk = st ? p->fs_block : p->vs_block;
                int size = st ? p->fs_size : p->vs_size;
                size_t n = size > 16 ? (size_t)size : 16;
                if (n <= 4096) {
                    if (st)
                        [e setFragmentBytes:blk length:n atIndex:0];
                    else
                        [e setVertexBytes:blk length:n atIndex:0];
                } else {
                    id<MTLBuffer> ub;
                    NSUInteger uo;
                    memcpy(rsm_transient(ctx, n, 256, &ub, &uo), blk, n);
                    if (st)
                        [e setFragmentBuffer:ub offset:uo atIndex:0];
                    else
                        [e setVertexBuffer:ub offset:uo atIndex:0];
                }
            }
            p->vs_dirty = p->fs_dirty = false;
            ctx->cur_prog_uid = p->uid;
        }
        // rs_fs always declares buffer(1), alpha test or not: always bound.
        if (ctx->dirty & RSM_DIRTY_ALPHA_REF) {
            float ref = ctx->alpha_ref;
            [e setFragmentBytes:&ref length:sizeof ref atIndex:1];
            ctx->dirty &= ~(unsigned)RSM_DIRTY_ALPHA_REF;
        }

        // Textures.
        for (int i = 0; i < p->n_tex; i++) {
            const rsm_texbind *tb = &p->tex[i];
            int unit = tb->value_loc >= 0 ? p->int_values[tb->value_loc] : 0;
            if (unit < 0 || unit >= RSM_MAX_UNITS || tb->slot < 0 || tb->slot >= RSM_MAX_UNITS)
                continue;
            bool cube = tb->type == RSMSL_SAMPLER_CUBE;
            RSMTexture *t = rsm_get(&ctx->textures, cube ? ctx->texcube[unit] : ctx->tex2d[unit]);
            id<MTLTexture> mt = t && t->mtl ? rsm_texture_view(t) : cube ? ctx->dummy_cube : ctx->dummy_tex;
            id<MTLSamplerState> ms = ctx->present_sampler;
            if (t && t->mtl) {
                if (!t->sampler)
                    rsm_texture_sampler(ctx, t);
                ms = t->sampler;
                t->last_use = ctx->serial;
            }
            for (int st = 0; st < 2; st++) {
                if (!(tb->stages & (st ? RSMSL_STAGE_FS : RSMSL_STAGE_VS)))
                    continue;
                NSUInteger s = (NSUInteger)tb->slot;
                if (ctx->enc_tex[st][s] != mt) {
                    if (st)
                        [e setFragmentTexture:mt atIndex:s];
                    else
                        [e setVertexTexture:mt atIndex:s];
                    ctx->enc_tex[st][s] = mt;
                }
                if (ctx->enc_smp[st][s] != ms) {
                    if (st)
                        [e setFragmentSamplerState:ms atIndex:s];
                    else
                        [e setVertexSamplerState:ms atIndex:s];
                    ctx->enc_smp[st][s] = ms;
                }
            }
        }

        // Draw.
        if (use_index) {
            NSInteger base = copy ? -(NSInteger)vmin : 0;
            [e drawIndexedPrimitives:pr.prim indexCount:icount indexType:mit indexBuffer:ibuf indexBufferOffset:ioff
                       instanceCount:(NSUInteger)instances baseVertex:base baseInstance:0];
        } else {
            NSUInteger start = copy ? 0 : (NSUInteger)first;
            [e drawPrimitives:pr.prim vertexStart:start vertexCount:(NSUInteger)count
                instanceCount:(NSUInteger)instances];
        }
    }
}

void rsmtl_glDrawArrays(GLenum mode, GLint first, GLsizei count) {
    RSM_CTX()
    rsm_draw(ctx, mode, first, count, 0, NULL, 1, false);
}

void rsmtl_glDrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices) {
    RSM_CTX()
    rsm_draw(ctx, mode, 0, count, type, indices, 1, true);
}

void rsmtl_glDrawArraysInstancedARB(GLenum mode, GLint first, GLsizei count, GLsizei primcount) {
    RSM_CTX()
    rsm_draw(ctx, mode, first, count, 0, NULL, primcount, false);
}

void rsmtl_glDrawElementsInstanced(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices,
                                   GLsizei instancecount) {
    RSM_CTX()
    rsm_draw(ctx, mode, 0, count, type, indices, instancecount, true);
}

void rsmtl_glDrawElementsInstancedARB(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices,
                                      GLsizei primcount) {
    RSM_CTX()
    rsm_draw(ctx, mode, 0, count, type, indices, primcount, true);
}

// --- Present -------------------------------------------------------------------------------
void rsm_present_draw(RSMContext *ctx, id<MTLTexture> target) {
    if (!ctx->present_pipeline || ctx->present_pipeline.label.integerValue != (NSInteger)target.pixelFormat) {
        MTLRenderPipelineDescriptor *d = [MTLRenderPipelineDescriptor new];
        d.vertexFunction = [ctx->internal_lib newFunctionWithName:@"rsm_blit_vs"];
        d.fragmentFunction = [ctx->internal_lib newFunctionWithName:@"rsm_blit_fs"];
        d.colorAttachments[0].pixelFormat = target.pixelFormat;
        d.label = [NSString stringWithFormat:@"%lu", (unsigned long)target.pixelFormat];
        NSError *e = nil;
        ctx->present_pipeline = [ctx->dev newRenderPipelineStateWithDescriptor:d error:&e];
        if (!ctx->present_pipeline) {
            rsm_log("present pipeline: %s", e.localizedDescription.UTF8String);
            return;
        }
    }
    MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
    rp.colorAttachments[0].texture = target;
    rp.colorAttachments[0].loadAction = MTLLoadActionDontCare;
    rp.colorAttachments[0].storeAction = MTLStoreActionStore;
    id<MTLRenderCommandEncoder> e = [rsm_cmd(ctx) renderCommandEncoderWithDescriptor:rp];
    [e setRenderPipelineState:ctx->present_pipeline];
    [e setFragmentTexture:ctx->def_color atIndex:0];
    [e setFragmentSamplerState:ctx->present_sampler atIndex:0];
    [e drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
    [e endEncoding];
}
