// Metal core: fixed GL state (enables, blend, depth/stencil, rasterizer, pixel
// store) and the glGet* queries answered from it.
#include "mtl_internal.h"

#include <math.h>
#include <string.h>

int rsm_num_extensions(void);

#define GL_ALPHA_TEST_FUNC_ 0x0BC1
#define GL_ALPHA_TEST_REF_ 0x0BC2
#ifndef GL_IMPLEMENTATION_COLOR_READ_FORMAT
#define GL_IMPLEMENTATION_COLOR_READ_FORMAT 0x8B9B
#define GL_IMPLEMENTATION_COLOR_READ_TYPE 0x8B9A
#endif
#include <stdarg.h>

static bool *rsm_cap(RSMContext *ctx, GLenum cap, unsigned *dirty) {
    switch (cap) {
    case GL_BLEND: *dirty = RSM_DIRTY_PIPELINE; return &ctx->blend;
    case GL_DEPTH_TEST: *dirty = RSM_DIRTY_DEPTH; return &ctx->depth_test;
    case GL_STENCIL_TEST: *dirty = RSM_DIRTY_DEPTH | RSM_DIRTY_STENCIL_REF; return &ctx->stencil_test;
    case GL_SCISSOR_TEST: *dirty = RSM_DIRTY_SCISSOR; return &ctx->scissor_test;
    case GL_CULL_FACE: *dirty = RSM_DIRTY_RASTER; return &ctx->cull;
    case GL_ALPHA_TEST: *dirty = RSM_DIRTY_PIPELINE | RSM_DIRTY_ALPHA_REF; return &ctx->alpha_test;
    case GL_DITHER: *dirty = 0; return &ctx->dither;
    case GL_POLYGON_OFFSET_FILL: *dirty = RSM_DIRTY_RASTER; return &ctx->poly_offset_fill;
    }
    return NULL;
}

// Accepted and ignored: fixed-function and multisample switches with no effect
// on a programmable pipeline (GL_TEXTURE_2D, GL_LIGHTING, GL_FOG, ...).
static bool rsm_cap_ignored(GLenum cap) {
    switch (cap) {
    case GL_TEXTURE_2D: case GL_TEXTURE_CUBE_MAP: case GL_LIGHTING: case GL_FOG:
    case GL_NORMALIZE: case GL_RESCALE_NORMAL: case GL_COLOR_MATERIAL: case GL_MULTISAMPLE:
    case GL_SAMPLE_ALPHA_TO_COVERAGE: case GL_SAMPLE_ALPHA_TO_ONE: case GL_SAMPLE_COVERAGE:
    case GL_LINE_SMOOTH: case GL_POLYGON_SMOOTH: case GL_POINT_SMOOTH: case GL_POLYGON_OFFSET_LINE:
    case GL_POLYGON_OFFSET_POINT: case GL_VERTEX_PROGRAM_POINT_SIZE: case GL_POINT_SPRITE:
    case GL_TEXTURE_GEN_S: case GL_TEXTURE_GEN_T: case GL_TEXTURE_GEN_R: case GL_TEXTURE_GEN_Q:
    case GL_LIGHT0: case GL_LIGHT1: case GL_COLOR_LOGIC_OP: case GL_LINE_STIPPLE:
    case GL_TEXTURE_CUBE_MAP_SEAMLESS: case GL_FRAMEBUFFER_SRGB: case GL_DEPTH_CLAMP:
    case GL_CLIP_PLANE0: case GL_CLIP_PLANE1: case GL_CLIP_PLANE2: case GL_CLIP_PLANE3:
    case GL_TEXTURE_1D: case GL_TEXTURE_3D: case GL_TEXTURE_RECTANGLE_ARB:
    case GL_VERTEX_PROGRAM_ARB: case GL_FRAGMENT_PROGRAM_ARB:
        return true;
    }
    return false;
}

static bool g_ignored_caps[0x10000];

static void rsm_enable(GLenum cap, bool on) {
    RSM_CTX()
    unsigned dirty = 0;
    bool *p = rsm_cap(ctx, cap, &dirty);
    if (p) {
        if (*p != on) {
            *p = on;
            ctx->dirty |= dirty;
        }
        return;
    }
    if (rsm_cap_ignored(cap)) {
        g_ignored_caps[cap & 0xFFFF] = on;
        return;
    }
    rsm_error(ctx, GL_INVALID_ENUM);
}

void rsmtl_glEnable(GLenum cap) { rsm_enable(cap, true); }
void rsmtl_glDisable(GLenum cap) { rsm_enable(cap, false); }

GLboolean rsmtl_glIsEnabled(GLenum cap) {
    RSM_CTX(GL_FALSE)
    unsigned d;
    bool *p = rsm_cap(ctx, cap, &d);
    if (p)
        return *p;
    if (rsm_cap_ignored(cap))
        return g_ignored_caps[cap & 0xFFFF];
    rsm_error(ctx, GL_INVALID_ENUM);
    return GL_FALSE;
}

// --- Blend ---------------------------------------------------------------------
static bool rsm_blend_factor_ok(GLenum f) {
    switch (f) {
    case GL_ZERO: case GL_ONE: case GL_SRC_COLOR: case GL_ONE_MINUS_SRC_COLOR: case GL_DST_COLOR:
    case GL_ONE_MINUS_DST_COLOR: case GL_SRC_ALPHA: case GL_ONE_MINUS_SRC_ALPHA: case GL_DST_ALPHA:
    case GL_ONE_MINUS_DST_ALPHA: case GL_CONSTANT_COLOR: case GL_ONE_MINUS_CONSTANT_COLOR:
    case GL_CONSTANT_ALPHA: case GL_ONE_MINUS_CONSTANT_ALPHA: case GL_SRC_ALPHA_SATURATE:
        return true;
    }
    return false;
}

void rsmtl_glBlendFunc(GLenum sfactor, GLenum dfactor) { rsmtl_glBlendFuncSeparate(sfactor, dfactor, sfactor, dfactor); }

void rsmtl_glBlendFuncSeparate(GLenum srgb, GLenum drgb, GLenum sa, GLenum da) {
    RSM_CTX()
    if (!rsm_blend_factor_ok(srgb) || !rsm_blend_factor_ok(drgb) || !rsm_blend_factor_ok(sa) ||
        !rsm_blend_factor_ok(da)) {
        rsm_error(ctx, GL_INVALID_ENUM);
        return;
    }
    if (ctx->blend_src_rgb != srgb || ctx->blend_dst_rgb != drgb || ctx->blend_src_a != sa ||
        ctx->blend_dst_a != da) {
        ctx->blend_src_rgb = srgb;
        ctx->blend_dst_rgb = drgb;
        ctx->blend_src_a = sa;
        ctx->blend_dst_a = da;
        ctx->dirty |= RSM_DIRTY_PIPELINE;
    }
}

static bool rsm_blend_eq_ok(GLenum e) {
    return e == GL_FUNC_ADD || e == GL_FUNC_SUBTRACT || e == GL_FUNC_REVERSE_SUBTRACT || e == GL_MIN ||
           e == GL_MAX;
}

void rsmtl_glBlendEquationSeparate(GLenum rgb, GLenum a) {
    RSM_CTX()
    if (!rsm_blend_eq_ok(rgb) || !rsm_blend_eq_ok(a)) {
        rsm_error(ctx, GL_INVALID_ENUM);
        return;
    }
    if (ctx->blend_eq_rgb != rgb || ctx->blend_eq_a != a) {
        ctx->blend_eq_rgb = rgb;
        ctx->blend_eq_a = a;
        ctx->dirty |= RSM_DIRTY_PIPELINE;
    }
}

void rsmtl_glBlendEquation(GLenum mode) { rsmtl_glBlendEquationSeparate(mode, mode); }

void rsmtl_glBlendColor(GLclampf r, GLclampf g, GLclampf b, GLclampf a) {
    RSM_CTX()
    float c[4] = {r, g, b, a};
    if (memcmp(c, ctx->blend_color, sizeof c)) {
        memcpy(ctx->blend_color, c, sizeof c);
        ctx->dirty |= RSM_DIRTY_BLEND_COLOR;
    }
}

void rsmtl_glColorMask(GLboolean r, GLboolean g, GLboolean b, GLboolean a) {
    RSM_CTX()
    bool m[4] = {r != 0, g != 0, b != 0, a != 0};
    if (memcmp(m, ctx->color_mask, sizeof m)) {
        memcpy(ctx->color_mask, m, sizeof m);
        ctx->dirty |= RSM_DIRTY_PIPELINE;
    }
}

// --- Depth / stencil ------------------------------------------------------------
static bool rsm_func_ok(GLenum f) { return f >= GL_NEVER && f <= GL_ALWAYS; }

void rsmtl_glDepthFunc(GLenum func) {
    RSM_CTX()
    if (!rsm_func_ok(func)) {
        rsm_error(ctx, GL_INVALID_ENUM);
        return;
    }
    if (ctx->depth_func != func) {
        ctx->depth_func = func;
        ctx->dirty |= RSM_DIRTY_DEPTH;
    }
}

void rsmtl_glDepthMask(GLboolean flag) {
    RSM_CTX()
    if (ctx->depth_mask != (flag != 0)) {
        ctx->depth_mask = flag != 0;
        ctx->dirty |= RSM_DIRTY_DEPTH;
    }
}

void rsmtl_glDepthRange(GLclampd n, GLclampd f) {
    RSM_CTX()
    ctx->depth_range[0] = (float)fmin(fmax(n, 0), 1);
    ctx->depth_range[1] = (float)fmin(fmax(f, 0), 1);
    ctx->dirty |= RSM_DIRTY_VIEWPORT;
}

static void rsm_faces(GLenum face, int *lo, int *hi) {
    *lo = face == GL_BACK ? 1 : 0;
    *hi = face == GL_FRONT ? 0 : 1;
}

void rsmtl_glStencilFuncSeparate(GLenum face, GLenum func, GLint ref, GLuint mask) {
    RSM_CTX()
    if ((face != GL_FRONT && face != GL_BACK && face != GL_FRONT_AND_BACK) || !rsm_func_ok(func)) {
        rsm_error(ctx, GL_INVALID_ENUM);
        return;
    }
    int lo, hi;
    rsm_faces(face, &lo, &hi);
    for (int f = lo; f <= hi; f++) {
        ctx->stencil[f].func = func;
        ctx->stencil[f].ref = ref;
        ctx->stencil[f].mask = mask;
    }
    ctx->dirty |= RSM_DIRTY_DEPTH | RSM_DIRTY_STENCIL_REF;
}

void rsmtl_glStencilFunc(GLenum func, GLint ref, GLuint mask) {
    rsmtl_glStencilFuncSeparate(GL_FRONT_AND_BACK, func, ref, mask);
}

static bool rsm_sop_ok(GLenum op) {
    switch (op) {
    case GL_KEEP: case GL_ZERO: case GL_REPLACE: case GL_INCR: case GL_DECR: case GL_INVERT:
    case GL_INCR_WRAP: case GL_DECR_WRAP:
        return true;
    }
    return false;
}

void rsmtl_glStencilOpSeparate(GLenum face, GLenum fail, GLenum zfail, GLenum zpass) {
    RSM_CTX()
    if ((face != GL_FRONT && face != GL_BACK && face != GL_FRONT_AND_BACK) || !rsm_sop_ok(fail) ||
        !rsm_sop_ok(zfail) || !rsm_sop_ok(zpass)) {
        rsm_error(ctx, GL_INVALID_ENUM);
        return;
    }
    int lo, hi;
    rsm_faces(face, &lo, &hi);
    for (int f = lo; f <= hi; f++) {
        ctx->stencil[f].fail = fail;
        ctx->stencil[f].zfail = zfail;
        ctx->stencil[f].zpass = zpass;
    }
    ctx->dirty |= RSM_DIRTY_DEPTH;
}

void rsmtl_glStencilOp(GLenum fail, GLenum zfail, GLenum zpass) {
    rsmtl_glStencilOpSeparate(GL_FRONT_AND_BACK, fail, zfail, zpass);
}

void rsmtl_glStencilMaskSeparate(GLenum face, GLuint mask) {
    RSM_CTX()
    int lo, hi;
    rsm_faces(face, &lo, &hi);
    for (int f = lo; f <= hi; f++)
        ctx->stencil[f].writemask = mask;
    ctx->dirty |= RSM_DIRTY_DEPTH;
}

void rsmtl_glStencilMask(GLuint mask) {
    RSM_CTX()
    if (ctx->stencil[0].writemask == mask && ctx->stencil[1].writemask == mask)
        return;
    rsmtl_glStencilMaskSeparate(GL_FRONT_AND_BACK, mask);
}

// --- Rasterizer ------------------------------------------------------------------
void rsmtl_glViewport(GLint x, GLint y, GLsizei w, GLsizei h) {
    RSM_CTX()
    if (w < 0 || h < 0) {
        rsm_error(ctx, GL_INVALID_VALUE);
        return;
    }
    GLint v[4] = {x, y, w, h};
    if (memcmp(v, ctx->viewport, sizeof v)) {
        memcpy(ctx->viewport, v, sizeof v);
        ctx->dirty |= RSM_DIRTY_VIEWPORT;
    }
}

void rsmtl_glScissor(GLint x, GLint y, GLsizei w, GLsizei h) {
    RSM_CTX()
    if (w < 0 || h < 0) {
        rsm_error(ctx, GL_INVALID_VALUE);
        return;
    }
    GLint v[4] = {x, y, w, h};
    if (memcmp(v, ctx->scissor, sizeof v)) {
        memcpy(ctx->scissor, v, sizeof v);
        ctx->dirty |= RSM_DIRTY_SCISSOR;
    }
}

void rsmtl_glCullFace(GLenum mode) {
    RSM_CTX()
    if (mode != GL_FRONT && mode != GL_BACK && mode != GL_FRONT_AND_BACK) {
        rsm_error(ctx, GL_INVALID_ENUM);
        return;
    }
    if (ctx->cull_mode != mode) {
        ctx->cull_mode = mode;
        ctx->dirty |= RSM_DIRTY_RASTER;
    }
}

void rsmtl_glFrontFace(GLenum mode) {
    RSM_CTX()
    if (mode != GL_CW && mode != GL_CCW) {
        rsm_error(ctx, GL_INVALID_ENUM);
        return;
    }
    if (ctx->front_face != mode) {
        ctx->front_face = mode;
        ctx->dirty |= RSM_DIRTY_RASTER;
    }
}

void rsmtl_glPolygonMode(GLenum face, GLenum mode) {
    RSM_CTX()
    if ((face != GL_FRONT && face != GL_BACK && face != GL_FRONT_AND_BACK) ||
        (mode != GL_POINT && mode != GL_LINE && mode != GL_FILL)) {
        rsm_error(ctx, GL_INVALID_ENUM);
        return;
    }
    int lo, hi;
    rsm_faces(face, &lo, &hi);
    for (int f = lo; f <= hi; f++)
        if (ctx->polygon_mode[f] != mode) {
            ctx->polygon_mode[f] = mode;
            ctx->dirty |= RSM_DIRTY_RASTER;
        }
}

void rsmtl_glPolygonOffset(GLfloat factor, GLfloat units) {
    RSM_CTX()
    (void)factor;
    (void)units;
    ctx->dirty |= RSM_DIRTY_RASTER;
}

void rsmtl_glLineWidth(GLfloat width) { (void)width; }
void rsmtl_glPointSize(GLfloat size) { (void)size; }
void rsmtl_glHint(GLenum target, GLenum mode) { (void)target; (void)mode; }
void rsmtl_glShadeModel(GLenum mode) { (void)mode; }
void rsmtl_glDrawBuffer(GLenum mode) { (void)mode; }
void rsmtl_glReadBuffer(GLenum mode) { (void)mode; }

void rsmtl_glAlphaFunc(GLenum func, GLclampf ref) {
    RSM_CTX()
    if (!rsm_func_ok(func)) {
        rsm_error(ctx, GL_INVALID_ENUM);
        return;
    }
    ref = ref < 0 ? 0 : ref > 1 ? 1 : ref;
    if (ctx->alpha_func != func) {
        ctx->alpha_func = func;
        ctx->dirty |= RSM_DIRTY_PIPELINE;
    }
    if (ctx->alpha_ref != ref) {
        ctx->alpha_ref = ref;
        ctx->dirty |= RSM_DIRTY_ALPHA_REF;
    }
}

// --- Clear values ---------------------------------------------------------------
static float rsm_clampf(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

void rsmtl_glClearColor(GLclampf r, GLclampf g, GLclampf b, GLclampf a) {
    RSM_CTX()
    ctx->clear_color[0] = rsm_clampf(r);
    ctx->clear_color[1] = rsm_clampf(g);
    ctx->clear_color[2] = rsm_clampf(b);
    ctx->clear_color[3] = rsm_clampf(a);
}

void rsmtl_glClearDepth(GLclampd depth) {
    RSM_CTX()
    ctx->clear_depth = depth < 0 ? 0 : depth > 1 ? 1 : depth;
}

void rsmtl_glClearStencil(GLint s) {
    RSM_CTX()
    ctx->clear_stencil = s;
}

// --- Pixel store ------------------------------------------------------------------
void rsmtl_glPixelStorei(GLenum pname, GLint param) {
    RSM_CTX()
    GLint *p = NULL;
    switch (pname) {
    case GL_UNPACK_ALIGNMENT: case GL_PACK_ALIGNMENT:
        if (param != 1 && param != 2 && param != 4 && param != 8) {
            rsm_error(ctx, GL_INVALID_VALUE);
            return;
        }
        p = pname == GL_UNPACK_ALIGNMENT ? &ctx->unpack_alignment : &ctx->pack_alignment;
        break;
    case GL_UNPACK_ROW_LENGTH: p = &ctx->unpack_row_length; break;
    case GL_UNPACK_SKIP_ROWS: p = &ctx->unpack_skip_rows; break;
    case GL_UNPACK_SKIP_PIXELS: p = &ctx->unpack_skip_pixels; break;
    case GL_PACK_ROW_LENGTH: p = &ctx->pack_row_length; break;
    case GL_PACK_SKIP_ROWS: p = &ctx->pack_skip_rows; break;
    case GL_PACK_SKIP_PIXELS: p = &ctx->pack_skip_pixels; break;
    case GL_UNPACK_SWAP_BYTES: case GL_UNPACK_LSB_FIRST: case GL_PACK_SWAP_BYTES:
    case GL_PACK_LSB_FIRST: case GL_UNPACK_IMAGE_HEIGHT: case GL_UNPACK_SKIP_IMAGES:
    case GL_PACK_IMAGE_HEIGHT: case GL_PACK_SKIP_IMAGES:
        return;
    case GL_UNPACK_CLIENT_STORAGE_APPLE:  // uploads are copied anyway
        ctx->unpack_client_storage = param != 0;
        return;
    default:
        rsm_error(ctx, GL_INVALID_ENUM);
        return;
    }
    if (param < 0) {
        rsm_error(ctx, GL_INVALID_VALUE);
        return;
    }
    *p = param;
}

void rsmtl_glPixelStoref(GLenum pname, GLfloat param) { rsmtl_glPixelStorei(pname, (GLint)param); }

// --- Queries -----------------------------------------------------------------------
// One query answered as up to 16 values; `kind` says how glGetIntegerv converts
// floats (colors map [0,1] to the integer range, others round).
enum { Q_INT, Q_FLOAT, Q_COLOR, Q_BOOL };
typedef struct {
    int n, kind;
    double v[16];
} rsm_q;

static void qi(rsm_q *q, int n, ...) {
    va_list ap;
    va_start(ap, n);
    q->n = n;
    q->kind = Q_INT;
    for (int i = 0; i < n; i++)
        q->v[i] = va_arg(ap, int);
    va_end(ap);
}

static void qf(rsm_q *q, int kind, int n, const float *v) {
    q->n = n;
    q->kind = kind;
    for (int i = 0; i < n; i++)
        q->v[i] = v[i];
}

static bool rsm_query(RSMContext *ctx, GLenum pname, rsm_q *q) {
    memset(q, 0, sizeof *q);
    unsigned dummy;
    bool *cap = rsm_cap(ctx, pname, &dummy);
    if (cap) {
        q->n = 1;
        q->kind = Q_BOOL;
        q->v[0] = *cap;
        return true;
    }
    switch (pname) {
    // Limits (Apple's legacy 2.1 context on Apple silicon).
    case GL_MAX_TEXTURE_SIZE: case GL_MAX_CUBE_MAP_TEXTURE_SIZE: case GL_MAX_RENDERBUFFER_SIZE:
        qi(q, 1, 16384); break;
    case GL_MAX_3D_TEXTURE_SIZE: qi(q, 1, 2048); break;
    case GL_MAX_VIEWPORT_DIMS: qi(q, 2, 16384, 16384); break;
    case GL_MAX_VERTEX_ATTRIBS: qi(q, 1, RSM_MAX_ATTRIBS); break;
    case GL_MAX_TEXTURE_IMAGE_UNITS: case GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS:
    case GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS:
        qi(q, 1, RSM_MAX_UNITS); break;
    case GL_MAX_TEXTURE_UNITS: case GL_MAX_TEXTURE_COORDS: qi(q, 1, 8); break;
    case GL_MAX_VERTEX_UNIFORM_COMPONENTS: case GL_MAX_FRAGMENT_UNIFORM_COMPONENTS:
        qi(q, 1, 4096); break;
    case GL_MAX_VARYING_FLOATS: qi(q, 1, 124); break;
    case GL_MAX_COLOR_ATTACHMENTS: case GL_MAX_DRAW_BUFFERS: qi(q, 1, 8); break;
    case GL_MAX_SAMPLES: qi(q, 1, 4); break;
    case GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT: qi(q, 1, 16); break;
    case GL_MAX_TEXTURE_LOD_BIAS: qi(q, 1, 16); break;
    case GL_MAX_ELEMENTS_VERTICES: qi(q, 1, 1048575); break;
    case GL_MAX_ELEMENTS_INDICES: qi(q, 1, 150000); break;
    case GL_ALIASED_LINE_WIDTH_RANGE: qi(q, 2, 1, 16); break;
    case GL_ALIASED_POINT_SIZE_RANGE: case GL_POINT_SIZE_RANGE: qi(q, 2, 1, 64); break;
    case GL_SUBPIXEL_BITS: qi(q, 1, 4); break;
    case GL_NUM_COMPRESSED_TEXTURE_FORMATS: qi(q, 1, 4); break;
    case GL_COMPRESSED_TEXTURE_FORMATS:
        qi(q, 4, GL_COMPRESSED_RGB_S3TC_DXT1_EXT, GL_COMPRESSED_RGBA_S3TC_DXT1_EXT,
           GL_COMPRESSED_RGBA_S3TC_DXT3_EXT, GL_COMPRESSED_RGBA_S3TC_DXT5_EXT);
        break;
    // The game and Scaleform read these; Apple's legacy context has 0 program
    // binary formats (it answers GL_INVALID_ENUM, the Metal core says 0).
    case GL_NUM_PROGRAM_BINARY_FORMATS: qi(q, 1, 0); break;
    case GL_PROGRAM_BINARY_FORMATS: q->n = 0; break;
    case GL_NUM_EXTENSIONS: qi(q, 1, rsm_num_extensions()); break;
    // Framebuffer bits: default framebuffer RGBA8, depth 24, stencil 8.
    case GL_RED_BITS: case GL_GREEN_BITS: case GL_BLUE_BITS: case GL_ALPHA_BITS: qi(q, 1, 8); break;
    case GL_DEPTH_BITS: qi(q, 1, 24); break;
    case GL_STENCIL_BITS: qi(q, 1, 8); break;
    case GL_SAMPLE_BUFFERS: case GL_SAMPLES: qi(q, 1, 0); break;
    case GL_DOUBLEBUFFER: q->n = 1; q->kind = Q_BOOL; q->v[0] = 1; break;
    case GL_IMPLEMENTATION_COLOR_READ_FORMAT: qi(q, 1, GL_BGRA); break;
    case GL_IMPLEMENTATION_COLOR_READ_TYPE: qi(q, 1, GL_UNSIGNED_INT_8_8_8_8_REV); break;
    // Bindings.
    case GL_ARRAY_BUFFER_BINDING: qi(q, 1, (int)ctx->array_buffer); break;
    case GL_PIXEL_PACK_BUFFER_BINDING: qi(q, 1, (int)ctx->pixel_pack_buffer); break;
    case GL_PIXEL_UNPACK_BUFFER_BINDING: qi(q, 1, (int)ctx->pixel_unpack_buffer); break;
    case GL_UNPACK_CLIENT_STORAGE_APPLE: q->n = 1; q->kind = Q_BOOL; q->v[0] = ctx->unpack_client_storage; break;
    case GL_ELEMENT_ARRAY_BUFFER_BINDING: qi(q, 1, (int)ctx->vao->element_buffer); break;
    case GL_VERTEX_ARRAY_BINDING: qi(q, 1, (int)ctx->vao_name); break;
    case GL_TEXTURE_BINDING_2D: qi(q, 1, (int)ctx->tex2d[ctx->active_unit]); break;
    case GL_TEXTURE_BINDING_CUBE_MAP: qi(q, 1, (int)ctx->texcube[ctx->active_unit]); break;
    case GL_ACTIVE_TEXTURE: qi(q, 1, GL_TEXTURE0 + ctx->active_unit); break;
    case GL_CLIENT_ACTIVE_TEXTURE: qi(q, 1, GL_TEXTURE0 + ctx->client_unit); break;
    case GL_CURRENT_PROGRAM: qi(q, 1, (int)ctx->prog_name); break;
    case GL_FRAMEBUFFER_BINDING: qi(q, 1, (int)ctx->draw_fb); break;
    case GL_READ_FRAMEBUFFER_BINDING: qi(q, 1, (int)ctx->read_fb); break;
    case GL_RENDERBUFFER_BINDING: qi(q, 1, (int)ctx->renderbuffer); break;
    // State.
    case GL_VIEWPORT: qi(q, 4, ctx->viewport[0], ctx->viewport[1], ctx->viewport[2], ctx->viewport[3]); break;
    case GL_SCISSOR_BOX: qi(q, 4, ctx->scissor[0], ctx->scissor[1], ctx->scissor[2], ctx->scissor[3]); break;
    case GL_DEPTH_RANGE: qf(q, Q_COLOR, 2, ctx->depth_range); break;
    case GL_COLOR_CLEAR_VALUE: qf(q, Q_COLOR, 4, ctx->clear_color); break;
    case GL_DEPTH_CLEAR_VALUE: q->n = 1; q->kind = Q_COLOR; q->v[0] = ctx->clear_depth; break;
    case GL_STENCIL_CLEAR_VALUE: qi(q, 1, ctx->clear_stencil); break;
    case GL_COLOR_WRITEMASK:
        q->n = 4;
        q->kind = Q_BOOL;
        for (int i = 0; i < 4; i++)
            q->v[i] = ctx->color_mask[i];
        break;
    case GL_DEPTH_WRITEMASK: q->n = 1; q->kind = Q_BOOL; q->v[0] = ctx->depth_mask; break;
    case GL_DEPTH_FUNC: qi(q, 1, ctx->depth_func); break;
    case GL_BLEND_SRC: case GL_BLEND_SRC_RGB: qi(q, 1, ctx->blend_src_rgb); break;
    case GL_BLEND_DST: case GL_BLEND_DST_RGB: qi(q, 1, ctx->blend_dst_rgb); break;
    case GL_BLEND_SRC_ALPHA: qi(q, 1, ctx->blend_src_a); break;
    case GL_BLEND_DST_ALPHA: qi(q, 1, ctx->blend_dst_a); break;
    case GL_BLEND_EQUATION_RGB: qi(q, 1, ctx->blend_eq_rgb); break;
    case GL_BLEND_EQUATION_ALPHA: qi(q, 1, ctx->blend_eq_a); break;
    case GL_BLEND_COLOR: qf(q, Q_COLOR, 4, ctx->blend_color); break;
    case GL_CULL_FACE_MODE: qi(q, 1, ctx->cull_mode); break;
    case GL_FRONT_FACE: qi(q, 1, ctx->front_face); break;
    case GL_POLYGON_MODE: qi(q, 2, ctx->polygon_mode[0], ctx->polygon_mode[1]); break;
    case GL_STENCIL_FUNC: qi(q, 1, ctx->stencil[0].func); break;
    case GL_STENCIL_REF: qi(q, 1, ctx->stencil[0].ref); break;
    case GL_STENCIL_VALUE_MASK: qi(q, 1, (int)ctx->stencil[0].mask); break;
    case GL_STENCIL_WRITEMASK: qi(q, 1, (int)ctx->stencil[0].writemask); break;
    case GL_STENCIL_FAIL: qi(q, 1, ctx->stencil[0].fail); break;
    case GL_STENCIL_PASS_DEPTH_FAIL: qi(q, 1, ctx->stencil[0].zfail); break;
    case GL_STENCIL_PASS_DEPTH_PASS: qi(q, 1, ctx->stencil[0].zpass); break;
    case GL_STENCIL_BACK_FUNC: qi(q, 1, ctx->stencil[1].func); break;
    case GL_STENCIL_BACK_REF: qi(q, 1, ctx->stencil[1].ref); break;
    case GL_STENCIL_BACK_VALUE_MASK: qi(q, 1, (int)ctx->stencil[1].mask); break;
    case GL_STENCIL_BACK_WRITEMASK: qi(q, 1, (int)ctx->stencil[1].writemask); break;
    case GL_STENCIL_BACK_FAIL: qi(q, 1, ctx->stencil[1].fail); break;
    case GL_STENCIL_BACK_PASS_DEPTH_FAIL: qi(q, 1, ctx->stencil[1].zfail); break;
    case GL_STENCIL_BACK_PASS_DEPTH_PASS: qi(q, 1, ctx->stencil[1].zpass); break;
    case GL_ALPHA_TEST_FUNC_: qi(q, 1, ctx->alpha_func); break;
    case GL_ALPHA_TEST_REF_: q->n = 1; q->kind = Q_COLOR; q->v[0] = ctx->alpha_ref; break;
    case GL_UNPACK_ALIGNMENT: qi(q, 1, ctx->unpack_alignment); break;
    case GL_UNPACK_ROW_LENGTH: qi(q, 1, ctx->unpack_row_length); break;
    case GL_UNPACK_SKIP_ROWS: qi(q, 1, ctx->unpack_skip_rows); break;
    case GL_UNPACK_SKIP_PIXELS: qi(q, 1, ctx->unpack_skip_pixels); break;
    case GL_PACK_ALIGNMENT: qi(q, 1, ctx->pack_alignment); break;
    case GL_PACK_ROW_LENGTH: qi(q, 1, ctx->pack_row_length); break;
    case GL_PACK_SKIP_ROWS: qi(q, 1, ctx->pack_skip_rows); break;
    case GL_PACK_SKIP_PIXELS: qi(q, 1, ctx->pack_skip_pixels); break;
    case GL_LINE_WIDTH: case GL_POINT_SIZE: qi(q, 1, 1); break;
    case GL_TEXTURE_2D: case GL_TEXTURE_CUBE_MAP: case GL_LIGHTING: case GL_FOG:
        q->n = 1; q->kind = Q_BOOL; q->v[0] = g_ignored_caps[pname & 0xFFFF]; break;
    // Not in a legacy 2.1 context: Apple answers GL_INVALID_ENUM (the game asks
    // for GL_MAX_VERTEX_UNIFORM_VECTORS once and copes).
    case GL_MAX_VERTEX_UNIFORM_VECTORS: case GL_MAX_FRAGMENT_UNIFORM_VECTORS:
    case GL_MAX_VARYING_VECTORS: case GL_MAJOR_VERSION: case GL_MINOR_VERSION:
    default:
        return false;
    }
    return true;
}

void rsmtl_glGetIntegerv(GLenum pname, GLint *params) {
    RSM_CTX()
    rsm_q q;
    if (!rsm_query(ctx, pname, &q)) {
        rsm_error(ctx, GL_INVALID_ENUM);
        return;
    }
    for (int i = 0; i < q.n; i++) {
        double v = q.v[i];
        if (q.kind == Q_COLOR) {
            double s = v * 2147483647.0;
            params[i] = (GLint)(s >= 2147483647.0 ? 2147483647 : s <= -2147483648.0 ? -2147483647 - 1 : lround(s));
        } else {
            params[i] = (GLint)lround(v);
        }
    }
}

void rsmtl_glGetFloatv(GLenum pname, GLfloat *params) {
    RSM_CTX()
    rsm_q q;
    if (!rsm_query(ctx, pname, &q)) {
        rsm_error(ctx, GL_INVALID_ENUM);
        return;
    }
    for (int i = 0; i < q.n; i++)
        params[i] = (GLfloat)q.v[i];
}

void rsmtl_glGetDoublev(GLenum pname, GLdouble *params) {
    RSM_CTX()
    rsm_q q;
    if (!rsm_query(ctx, pname, &q)) {
        rsm_error(ctx, GL_INVALID_ENUM);
        return;
    }
    for (int i = 0; i < q.n; i++)
        params[i] = q.v[i];
}

void rsmtl_glGetBooleanv(GLenum pname, GLboolean *params) {
    RSM_CTX()
    rsm_q q;
    if (!rsm_query(ctx, pname, &q)) {
        rsm_error(ctx, GL_INVALID_ENUM);
        return;
    }
    for (int i = 0; i < q.n; i++)
        params[i] = q.v[i] != 0;
}
