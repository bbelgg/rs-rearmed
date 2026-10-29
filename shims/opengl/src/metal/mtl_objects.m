// Metal core: buffers, vertex arrays, textures, samplers, renderbuffers and
// framebuffer objects; pixel conversion for uploads and reads.
#include "mtl_internal.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

// --- Buffers -------------------------------------------------------------------------
RSMBuffer *rsm_buffer(RSMContext *ctx, GLuint name) {
    RSMBuffer *b = rsm_get(&ctx->buffers, name);
    if (!b && name) {
        b = [RSMBuffer new];
        rsm_names_set(&ctx->buffers, name, b);
    }
    return b;
}

// GL_INVALID_OPERATION unless [off, off + bytes) lies inside b's data store.
static bool rsm_pbo_range_ok(RSMContext *ctx, RSMBuffer *b, size_t off, size_t bytes) {
    if (!b || !b->mtl || b->mapped || off > b->size || bytes > b->size - off) {
        rsm_error(ctx, GL_INVALID_OPERATION);
        return false;
    }
    return true;
}

const void *rsm_unpack_ptr(RSMContext *ctx, const void *p, size_t bytes) {
    if (!ctx->pixel_unpack_buffer)
        return p;
    RSMBuffer *b = rsm_get(&ctx->buffers, ctx->pixel_unpack_buffer);
    if (!rsm_pbo_range_ok(ctx, b, (size_t)p, bytes))
        return NULL;
    return (const uint8_t *)b->mtl.contents + (size_t)p;
}

static bool rsm_buffer_gpu_overlap(RSMContext *ctx, RSMBuffer *b, size_t off, size_t len);
static void rsm_buffer_rename(RSMContext *ctx, RSMBuffer *b, size_t skip_off, size_t skip_len);

void *rsm_pack_ptr(RSMContext *ctx, void *p, size_t bytes) {
    if (!ctx->pixel_pack_buffer)
        return p;
    RSMBuffer *b = rsm_get(&ctx->buffers, ctx->pixel_pack_buffer);
    if (!rsm_pbo_range_ok(ctx, b, (size_t)p, bytes))
        return NULL;
    // The GPU may still read the range: the buffer gets new storage first.
    if (rsm_buffer_gpu_overlap(ctx, b, (size_t)p, bytes))
        rsm_buffer_rename(ctx, b, (size_t)p, bytes);
    if (ctx->buf_opts & MTLResourceStorageModeManaged)
        [b->mtl didModifyRange:NSMakeRange((size_t)p, bytes)];
    return (uint8_t *)b->mtl.contents + (size_t)p;
}

// --- GPU read ranges --------------------------------------------------------------------
void rsm_buffer_gpu_read(RSMContext *ctx, RSMBuffer *b, size_t lo, size_t hi) {
    if (hi > b->size)
        hi = b->size;
    if (lo > hi)
        lo = hi;
    if (b->rd_serial != ctx->serial) {
        // A new command buffer: the previous one's reads join the older ones still in flight.
        if (b->rd_hi > b->rd_lo) {
            if (b->old_hi > b->old_lo && rsm_in_flight(ctx, b->old_serial)) {
                b->old_lo = b->rd_lo < b->old_lo ? b->rd_lo : b->old_lo;
                b->old_hi = b->rd_hi > b->old_hi ? b->rd_hi : b->old_hi;
            } else {
                b->old_lo = b->rd_lo;
                b->old_hi = b->rd_hi;
            }
            b->old_serial = b->rd_serial;
        }
        b->rd_serial = ctx->serial;
        b->rd_lo = lo;
        b->rd_hi = hi;
    } else if (lo < hi) {
        if (b->rd_hi <= b->rd_lo) {
            b->rd_lo = lo;
            b->rd_hi = hi;
        } else {
            b->rd_lo = lo < b->rd_lo ? lo : b->rd_lo;
            b->rd_hi = hi > b->rd_hi ? hi : b->rd_hi;
        }
    }
    b->last_use = ctx->serial;
}

// May a GPU command recorded or in flight read [off, off + len)?
static bool rsm_buffer_gpu_overlap(RSMContext *ctx, RSMBuffer *b, size_t off, size_t len) {
    if (!rsm_in_flight(ctx, b->last_use))
        return false;
    b->streamed = true;
    if (ctx->buf_opts & MTLResourceStorageModeManaged)
        return true;  // managed storage syncs whole ranges at commit: no writes under the GPU
    size_t end = off + len;
    if (b->rd_hi > b->rd_lo && off < b->rd_hi && b->rd_lo < end && rsm_in_flight(ctx, b->rd_serial))
        return true;
    if (b->old_hi > b->old_lo && off < b->old_hi && b->old_lo < end && rsm_in_flight(ctx, b->old_serial))
        return true;
    return false;
}

static void rsm_buffer_reset_reads(RSMBuffer *b) {
    b->last_use = b->rd_serial = b->old_serial = 0;
    b->rd_lo = b->rd_hi = b->old_lo = b->old_hi = 0;
}

void rsmtl_glGenBuffers(GLsizei n, GLuint *buffers) {
    RSM_CTX()
    if (n < 0) {
        rsm_error(ctx, GL_INVALID_VALUE);
        return;
    }
    for (GLsizei i = 0; i < n; i++) {
        buffers[i] = rsm_names_alloc(&ctx->buffers);
        rsm_names_set(&ctx->buffers, buffers[i], [RSMBuffer new]);
    }
}

void rsmtl_glDeleteBuffers(GLsizei n, const GLuint *buffers) {
    RSM_CTX()
    for (GLsizei i = 0; i < n; i++) {
        GLuint b = buffers[i];
        if (!b || !rsm_get(&ctx->buffers, b))
            continue;
        if (ctx->array_buffer == b)
            ctx->array_buffer = 0;
        if (ctx->pixel_pack_buffer == b)
            ctx->pixel_pack_buffer = 0;
        if (ctx->pixel_unpack_buffer == b)
            ctx->pixel_unpack_buffer = 0;
        if (ctx->vao->element_buffer == b) {
            ctx->vao->element_buffer = 0;
            ctx->vao->element_obj = nil;
        }
        for (int a = 0; a < RSM_MAX_ATTRIBS; a++)
            if (ctx->vao->a[a].buffer == b) {
                ctx->vao->a[a].buffer = 0;
                ctx->vao->a[a].bo = nil;
            }
        rsm_names_set(&ctx->buffers, b, nil);
    }
    ctx->dirty |= RSM_DIRTY_VERTEX;
}

GLboolean rsmtl_glIsBuffer(GLuint name) {
    RSM_CTX(GL_FALSE)
    RSMBuffer *b = rsm_get(&ctx->buffers, name);
    return b && b->bound;
}

static GLuint *rsm_buffer_binding(RSMContext *ctx, GLenum target) {
    switch (target) {
    case GL_ARRAY_BUFFER: return &ctx->array_buffer;
    case GL_ELEMENT_ARRAY_BUFFER: return &ctx->vao->element_buffer;
    case GL_PIXEL_PACK_BUFFER: return &ctx->pixel_pack_buffer;
    case GL_PIXEL_UNPACK_BUFFER: return &ctx->pixel_unpack_buffer;
    }
    rsm_error(ctx, GL_INVALID_ENUM);
    return NULL;
}

void rsmtl_glBindBuffer(GLenum target, GLuint buffer) {
    RSM_CTX()
    GLuint *slot = rsm_buffer_binding(ctx, target);
    if (!slot)
        return;
    RSMBuffer *b = nil;
    if (buffer) {
        b = rsm_buffer(ctx, buffer);
        b->bound = true;
    }
    *slot = buffer;
    if (target == GL_ELEMENT_ARRAY_BUFFER)
        ctx->vao->element_obj = b;
}

static RSMBuffer *rsm_bound_buffer(RSMContext *ctx, GLenum target) {
    GLuint *slot = rsm_buffer_binding(ctx, target);
    if (!slot)
        return nil;
    RSMBuffer *b = rsm_get(&ctx->buffers, *slot);
    if (!b)
        rsm_error(ctx, GL_INVALID_OPERATION);
    return b;
}

static void rsm_buffer_modified(RSMContext *ctx, RSMBuffer *b, size_t off, size_t len) {
    if (ctx->buf_opts & MTLResourceStorageModeManaged)
        [b->mtl didModifyRange:NSMakeRange(off, len)];
}

// A write into a buffer the GPU may still read: switch to a fresh MTLBuffer with
// the old contents except [keep_off, keep_off + keep_len), which the caller writes.
static void rsm_buffer_rename(RSMContext *ctx, RSMBuffer *b, size_t skip_off, size_t skip_len) {
    id<MTLBuffer> old = b->mtl;
    id<MTLBuffer> nb = rsm_new_buffer(ctx, old.length);
    uint8_t *d = nb.contents;
    const uint8_t *s = old.contents;
    if (skip_off)
        memcpy(d, s, skip_off);
    if (skip_off + skip_len < b->size)
        memcpy(d + skip_off + skip_len, s + skip_off + skip_len, b->size - skip_off - skip_len);
    rsm_retire_buffer(ctx, old, b->last_use);
    b->mtl = nb;
    rsm_buffer_reset_reads(b);
    rsm_buffer_modified(ctx, b, 0, b->size);
}

void rsmtl_glBufferData(GLenum target, GLsizeiptr size, const GLvoid *data, GLenum usage) {
    RSM_CTX()
    if (size < 0) {
        rsm_error(ctx, GL_INVALID_VALUE);
        return;
    }
    RSMBuffer *b = rsm_bound_buffer(ctx, target);
    if (!b)
        return;
    size_t alloc = size ? (size_t)size : 4;
    if (b->mtl && b->mtl.length == alloc && !rsm_in_flight(ctx, b->last_use)) {
        // same storage, reusable
    } else {
        if (b->mtl)
            rsm_retire_buffer(ctx, b->mtl, b->last_use);
        b->mtl = rsm_new_buffer(ctx, alloc);
    }
    rsm_buffer_reset_reads(b);
    b->size = (size_t)size;
    b->usage = usage;
    b->mapped = false;
    if (data && size) {
        memcpy(b->mtl.contents, data, (size_t)size);
        rsm_buffer_modified(ctx, b, 0, (size_t)size);
    }
    ctx->dirty |= RSM_DIRTY_VERTEX;
}

void rsmtl_glBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const GLvoid *data) {
    RSM_CTX()
    RSMBuffer *b = rsm_bound_buffer(ctx, target);
    if (!b)
        return;
    if (offset < 0 || size < 0 || (size_t)(offset + size) > b->size) {
        rsm_error(ctx, GL_INVALID_VALUE);
        return;
    }
    if (b->mapped) {
        rsm_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    if (!size || !data)
        return;
    // Only a range the GPU may still read forces new storage (streaming into one buffer).
    if (rsm_buffer_gpu_overlap(ctx, b, (size_t)offset, (size_t)size))
        rsm_buffer_rename(ctx, b, (size_t)offset, (size_t)size);
    memcpy((uint8_t *)b->mtl.contents + offset, data, (size_t)size);
    rsm_buffer_modified(ctx, b, (size_t)offset, (size_t)size);
}

GLvoid *rsmtl_glMapBuffer(GLenum target, GLenum access) {
    RSM_CTX(NULL)
    RSMBuffer *b = rsm_bound_buffer(ctx, target);
    if (!b)
        return NULL;
    if (access != GL_READ_ONLY && access != GL_WRITE_ONLY && access != GL_READ_WRITE) {
        rsm_error(ctx, GL_INVALID_ENUM);
        return NULL;
    }
    if (b->mapped || !b->mtl) {
        rsm_error(ctx, GL_INVALID_OPERATION);
        return NULL;
    }
    // The GPU never writes buffers here, so reads need no wait.
    if (access != GL_READ_ONLY && rsm_buffer_gpu_overlap(ctx, b, 0, b->size))
        rsm_buffer_rename(ctx, b, 0, 0);
    b->mapped = true;
    b->map_access = access;
    return b->mtl.contents;
}

GLboolean rsmtl_glUnmapBuffer(GLenum target) {
    RSM_CTX(GL_FALSE)
    RSMBuffer *b = rsm_bound_buffer(ctx, target);
    if (!b)
        return GL_FALSE;
    if (!b->mapped) {
        rsm_error(ctx, GL_INVALID_OPERATION);
        return GL_FALSE;
    }
    b->mapped = false;
    if (b->map_access != GL_READ_ONLY)
        rsm_buffer_modified(ctx, b, 0, b->size);
    return GL_TRUE;
}

void rsmtl_glGetBufferParameteriv(GLenum target, GLenum pname, GLint *params) {
    RSM_CTX()
    RSMBuffer *b = rsm_bound_buffer(ctx, target);
    if (!b)
        return;
    switch (pname) {
    case GL_BUFFER_SIZE: *params = (GLint)b->size; break;
    case GL_BUFFER_USAGE: *params = (GLint)b->usage; break;
    case GL_BUFFER_MAPPED: *params = b->mapped; break;
    case GL_BUFFER_ACCESS: *params = (GLint)(b->mapped ? b->map_access : GL_READ_WRITE); break;
    default: rsm_error(ctx, GL_INVALID_ENUM);
    }
}

// --- Vertex arrays -------------------------------------------------------------------
void rsmtl_glGenVertexArrays(GLsizei n, GLuint *arrays) {
    RSM_CTX()
    for (GLsizei i = 0; i < n; i++) {
        arrays[i] = rsm_names_alloc(&ctx->vaos);
        rsm_names_set(&ctx->vaos, arrays[i], [RSMVertexArray new]);
    }
}

void rsmtl_glDeleteVertexArrays(GLsizei n, const GLuint *arrays) {
    RSM_CTX()
    for (GLsizei i = 0; i < n; i++) {
        if (!arrays[i] || !rsm_get(&ctx->vaos, arrays[i]))
            continue;
        if (ctx->vao_name == arrays[i]) {
            ctx->vao_name = 0;
            ctx->vao = ctx->vao0;
            ctx->dirty |= RSM_DIRTY_VERTEX;
        }
        rsm_names_set(&ctx->vaos, arrays[i], nil);
    }
}

void rsmtl_glBindVertexArray(GLuint array) {
    RSM_CTX()
    RSMVertexArray *v = ctx->vao0;
    if (array) {
        v = rsm_get(&ctx->vaos, array);
        if (!v) {
            rsm_error(ctx, GL_INVALID_OPERATION);
            return;
        }
        v->bound = true;
    }
    if (v != ctx->vao) {
        ctx->vao = v;
        ctx->vao_name = array;
        ctx->dirty |= RSM_DIRTY_VERTEX;
    }
}

GLboolean rsmtl_glIsVertexArray(GLuint array) {
    RSM_CTX(GL_FALSE)
    RSMVertexArray *v = rsm_get(&ctx->vaos, array);
    return v && v->bound;
}

static void rsm_attrib_pointer(RSMContext *ctx, GLuint index, GLint size, GLenum type,
                               bool normalized, GLsizei stride, const GLvoid *ptr) {
    if (index >= RSM_MAX_ATTRIBS || stride < 0 || ((size < 1 || size > 4) && size != GL_BGRA)) {
        rsm_error(ctx, GL_INVALID_VALUE);
        return;
    }
    rsm_attrib *a = &ctx->vao->a[index];
    a->size = size;
    a->type = type;
    a->normalized = normalized || size == GL_BGRA;
    a->stride = stride;
    a->ptr = ptr;
    a->buffer = ctx->array_buffer;
    a->bo = ctx->array_buffer ? rsm_get(&ctx->buffers, ctx->array_buffer) : nil;
    ctx->dirty |= RSM_DIRTY_VERTEX;
}

void rsmtl_glVertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized,
                                 GLsizei stride, const GLvoid *pointer) {
    RSM_CTX()
    rsm_attrib_pointer(ctx, index, size, type, normalized != 0, stride, pointer);
}

static void rsm_attrib_enable(RSMContext *ctx, GLuint index, bool on) {
    if (index >= RSM_MAX_ATTRIBS) {
        rsm_error(ctx, GL_INVALID_VALUE);
        return;
    }
    if (ctx->vao->a[index].enabled != on) {
        ctx->vao->a[index].enabled = on;
        ctx->dirty |= RSM_DIRTY_VERTEX;
    }
}

void rsmtl_glEnableVertexAttribArray(GLuint index) {
    RSM_CTX()
    rsm_attrib_enable(ctx, index, true);
}

void rsmtl_glDisableVertexAttribArray(GLuint index) {
    RSM_CTX()
    rsm_attrib_enable(ctx, index, false);
}

void rsmtl_glVertexAttribDivisorARB(GLuint index, GLuint divisor) {
    RSM_CTX()
    if (index >= RSM_MAX_ATTRIBS) {
        rsm_error(ctx, GL_INVALID_VALUE);
        return;
    }
    if (ctx->vao->a[index].divisor != divisor) {
        ctx->vao->a[index].divisor = divisor;
        ctx->dirty |= RSM_DIRTY_VERTEX;
    }
}

void rsmtl_glVertexAttribDivisor(GLuint index, GLuint divisor) { rsmtl_glVertexAttribDivisorARB(index, divisor); }

void rsmtl_glVertexAttrib4f(GLuint index, GLfloat x, GLfloat y, GLfloat z, GLfloat w) {
    RSM_CTX()
    if (index >= RSM_MAX_ATTRIBS) {
        rsm_error(ctx, GL_INVALID_VALUE);
        return;
    }
    float v[4] = {x, y, z, w};
    memcpy(ctx->vattr[index], v, sizeof v);
}

void rsmtl_glVertexAttrib4fv(GLuint index, const GLfloat *v) { rsmtl_glVertexAttrib4f(index, v[0], v[1], v[2], v[3]); }
void rsmtl_glVertexAttrib1f(GLuint index, GLfloat x) { rsmtl_glVertexAttrib4f(index, x, 0, 0, 1); }
void rsmtl_glVertexAttrib2f(GLuint index, GLfloat x, GLfloat y) { rsmtl_glVertexAttrib4f(index, x, y, 0, 1); }
void rsmtl_glVertexAttrib3f(GLuint index, GLfloat x, GLfloat y, GLfloat z) { rsmtl_glVertexAttrib4f(index, x, y, z, 1); }

// Conventional arrays, with NVIDIA's aliasing onto generic attributes
// (docs/METAL-DESIGN.md "Built-in attributes"): what the Cg shim uses.
void rsmtl_glVertexPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer) {
    RSM_CTX()
    rsm_attrib_pointer(ctx, 0, size, type, false, stride, pointer);
}

void rsmtl_glNormalPointer(GLenum type, GLsizei stride, const GLvoid *pointer) {
    RSM_CTX()
    rsm_attrib_pointer(ctx, 2, 3, type, type != GL_FLOAT && type != GL_DOUBLE, stride, pointer);
}

void rsmtl_glColorPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer) {
    RSM_CTX()
    rsm_attrib_pointer(ctx, 3, size, type, type != GL_FLOAT && type != GL_DOUBLE, stride, pointer);
}

void rsmtl_glSecondaryColorPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer) {
    RSM_CTX()
    rsm_attrib_pointer(ctx, 4, size, type, type != GL_FLOAT && type != GL_DOUBLE, stride, pointer);
}

void rsmtl_glFogCoordPointer(GLenum type, GLsizei stride, const GLvoid *pointer) {
    RSM_CTX()
    rsm_attrib_pointer(ctx, 5, 1, type, false, stride, pointer);
}

void rsmtl_glTexCoordPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer) {
    RSM_CTX()
    rsm_attrib_pointer(ctx, 8 + (GLuint)ctx->client_unit, size, type, false, stride, pointer);
}

void rsmtl_glClientActiveTexture(GLenum texture) {
    RSM_CTX()
    if (texture < GL_TEXTURE0 || texture >= GL_TEXTURE0 + 8) {
        rsm_error(ctx, GL_INVALID_ENUM);
        return;
    }
    ctx->client_unit = (int)(texture - GL_TEXTURE0);
}

static int rsm_client_array(RSMContext *ctx, GLenum array) {
    switch (array) {
    case GL_VERTEX_ARRAY: return 0;
    case GL_NORMAL_ARRAY: return 2;
    case GL_COLOR_ARRAY: return 3;
    case GL_SECONDARY_COLOR_ARRAY: return 4;
    case GL_FOG_COORD_ARRAY: return 5;
    case GL_TEXTURE_COORD_ARRAY: return 8 + ctx->client_unit;
    }
    return -1;
}

void rsmtl_glEnableClientState(GLenum array) {
    RSM_CTX()
    int i = rsm_client_array(ctx, array);
    if (i < 0)
        rsm_error(ctx, GL_INVALID_ENUM);
    else
        rsm_attrib_enable(ctx, (GLuint)i, true);
}

void rsmtl_glDisableClientState(GLenum array) {
    RSM_CTX()
    int i = rsm_client_array(ctx, array);
    if (i < 0)
        rsm_error(ctx, GL_INVALID_ENUM);
    else
        rsm_attrib_enable(ctx, (GLuint)i, false);
}

void rsmtl_glGetVertexAttribiv(GLuint index, GLenum pname, GLint *params) {
    RSM_CTX()
    if (index >= RSM_MAX_ATTRIBS) {
        rsm_error(ctx, GL_INVALID_VALUE);
        return;
    }
    rsm_attrib *a = &ctx->vao->a[index];
    switch (pname) {
    case GL_VERTEX_ATTRIB_ARRAY_ENABLED: *params = a->enabled; break;
    case GL_VERTEX_ATTRIB_ARRAY_SIZE: *params = a->size ? a->size : 4; break;
    case GL_VERTEX_ATTRIB_ARRAY_STRIDE: *params = a->stride; break;
    case GL_VERTEX_ATTRIB_ARRAY_TYPE: *params = (GLint)(a->type ? a->type : GL_FLOAT); break;
    case GL_VERTEX_ATTRIB_ARRAY_NORMALIZED: *params = a->normalized; break;
    case GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING: *params = (GLint)a->buffer; break;
    case GL_VERTEX_ATTRIB_ARRAY_DIVISOR_ARB: *params = (GLint)a->divisor; break;
    case GL_CURRENT_VERTEX_ATTRIB:
        for (int i = 0; i < 4; i++)
            params[i] = (GLint)ctx->vattr[index][i];
        break;
    default: rsm_error(ctx, GL_INVALID_ENUM);
    }
}

// --- Pixel formats ---------------------------------------------------------------------
typedef struct {
    MTLPixelFormat fmt;
    int swz;
    bool compressed, depth, stencil;
} rsm_texfmt;

static bool rsm_is_dxt(GLenum internal) {
    return internal == GL_COMPRESSED_RGB_S3TC_DXT1_EXT || internal == GL_COMPRESSED_RGBA_S3TC_DXT1_EXT ||
           internal == GL_COMPRESSED_RGBA_S3TC_DXT3_EXT || internal == GL_COMPRESSED_RGBA_S3TC_DXT5_EXT ||
           internal == GL_COMPRESSED_SRGB_S3TC_DXT1_EXT || internal == GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT ||
           internal == GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT ||
           internal == GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT;
}

static bool rsm_choose_format(GLenum internal, GLenum format, rsm_texfmt *f) {
    memset(f, 0, sizeof *f);
    switch (internal) {
    case GL_RGBA: case GL_RGBA8: case 4: case GL_RGBA4: case GL_RGB5_A1: case GL_RGBA2:
    case GL_RGB10_A2: case GL_RGBA12: case GL_RGBA16:
        f->fmt = format == GL_BGRA ? MTLPixelFormatBGRA8Unorm : MTLPixelFormatRGBA8Unorm;
        return true;
    case GL_RGB: case GL_RGB8: case 3: case GL_RGB5: case GL_RGB4: case GL_R3_G3_B2: case GL_RGB10:
    case GL_RGB12: case GL_RGB16:
        f->fmt = format == GL_BGRA || format == GL_BGR ? MTLPixelFormatBGRA8Unorm : MTLPixelFormatRGBA8Unorm;
        f->swz = RSM_SWZ_RGB1;
        return true;
    case GL_SRGB: case GL_SRGB8:
        f->fmt = MTLPixelFormatRGBA8Unorm_sRGB;
        f->swz = RSM_SWZ_RGB1;
        return true;
    case GL_SRGB_ALPHA: case GL_SRGB8_ALPHA8:
        f->fmt = MTLPixelFormatRGBA8Unorm_sRGB;
        return true;
    case GL_LUMINANCE: case GL_LUMINANCE8: case 1: case GL_LUMINANCE4: case GL_LUMINANCE16:
        f->fmt = MTLPixelFormatR8Unorm; f->swz = RSM_SWZ_LUM; return true;
    case GL_ALPHA: case GL_ALPHA8: case GL_ALPHA4: case GL_ALPHA16:
        f->fmt = MTLPixelFormatR8Unorm; f->swz = RSM_SWZ_ALPHA; return true;
    case GL_INTENSITY: case GL_INTENSITY8: case GL_INTENSITY4: case GL_INTENSITY16:
        f->fmt = MTLPixelFormatR8Unorm; f->swz = RSM_SWZ_INTENSITY; return true;
    case GL_LUMINANCE_ALPHA: case GL_LUMINANCE8_ALPHA8: case 2: case GL_LUMINANCE4_ALPHA4:
    case GL_LUMINANCE16_ALPHA16:
        f->fmt = MTLPixelFormatRG8Unorm; f->swz = RSM_SWZ_LUM_ALPHA; return true;
    case GL_RED: case GL_R8: f->fmt = MTLPixelFormatR8Unorm; f->swz = RSM_SWZ_RED; return true;
    case GL_RG: case GL_RG8: f->fmt = MTLPixelFormatRG8Unorm; f->swz = RSM_SWZ_RG; return true;
    case GL_R16F: f->fmt = MTLPixelFormatR16Float; f->swz = RSM_SWZ_RED; return true;
    case GL_R32F: f->fmt = MTLPixelFormatR32Float; f->swz = RSM_SWZ_RED; return true;
    case GL_RG16F: f->fmt = MTLPixelFormatRG16Float; f->swz = RSM_SWZ_RG; return true;
    case GL_RG32F: f->fmt = MTLPixelFormatRG32Float; f->swz = RSM_SWZ_RG; return true;
    case GL_RGBA16F_ARB: f->fmt = MTLPixelFormatRGBA16Float; return true;
    case GL_RGBA32F_ARB: f->fmt = MTLPixelFormatRGBA32Float; return true;
    case GL_RGB16F_ARB: f->fmt = MTLPixelFormatRGBA16Float; f->swz = RSM_SWZ_RGB1; return true;
    case GL_RGB32F_ARB: f->fmt = MTLPixelFormatRGBA32Float; f->swz = RSM_SWZ_RGB1; return true;
    case GL_R11F_G11F_B10F_EXT: f->fmt = MTLPixelFormatRG11B10Float; f->swz = RSM_SWZ_RGB1; return true;
    case GL_DEPTH_COMPONENT: case GL_DEPTH_COMPONENT16: case GL_DEPTH_COMPONENT24:
    case GL_DEPTH_COMPONENT32: case GL_DEPTH_COMPONENT32F:
        f->fmt = MTLPixelFormatDepth32Float; f->depth = true; return true;
    case GL_DEPTH_STENCIL_EXT: case GL_DEPTH24_STENCIL8_EXT: case GL_DEPTH32F_STENCIL8:
        f->fmt = MTLPixelFormatDepth32Float_Stencil8; f->depth = f->stencil = true; return true;
    case GL_STENCIL_INDEX8: case GL_STENCIL_INDEX:
        f->fmt = MTLPixelFormatStencil8; f->stencil = true; return true;
    case GL_COMPRESSED_RGB_S3TC_DXT1_EXT:
        f->fmt = MTLPixelFormatBC1_RGBA; f->swz = RSM_SWZ_RGB1; f->compressed = true; return true;
    case GL_COMPRESSED_RGBA_S3TC_DXT1_EXT: f->fmt = MTLPixelFormatBC1_RGBA; f->compressed = true; return true;
    case GL_COMPRESSED_RGBA_S3TC_DXT3_EXT: f->fmt = MTLPixelFormatBC2_RGBA; f->compressed = true; return true;
    case GL_COMPRESSED_RGBA_S3TC_DXT5_EXT: f->fmt = MTLPixelFormatBC3_RGBA; f->compressed = true; return true;
    case GL_COMPRESSED_SRGB_S3TC_DXT1_EXT:
        f->fmt = MTLPixelFormatBC1_RGBA_sRGB; f->swz = RSM_SWZ_RGB1; f->compressed = true; return true;
    case GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT: f->fmt = MTLPixelFormatBC1_RGBA_sRGB; f->compressed = true; return true;
    case GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT: f->fmt = MTLPixelFormatBC2_RGBA_sRGB; f->compressed = true; return true;
    case GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT: f->fmt = MTLPixelFormatBC3_RGBA_sRGB; f->compressed = true; return true;
    }
    return false;
}

static size_t rsm_fmt_bytes(MTLPixelFormat f) {
    switch (f) {
    case MTLPixelFormatR8Unorm: case MTLPixelFormatStencil8: return 1;
    case MTLPixelFormatRG8Unorm: case MTLPixelFormatR16Float: return 2;
    case MTLPixelFormatRGBA16Float: case MTLPixelFormatRG32Float: return 8;
    case MTLPixelFormatRGBA32Float: return 16;
    case MTLPixelFormatDepth32Float_Stencil8: return 8;
    case MTLPixelFormatBC1_RGBA: case MTLPixelFormatBC1_RGBA_sRGB: return 8;    // per block
    case MTLPixelFormatBC2_RGBA: case MTLPixelFormatBC2_RGBA_sRGB:
    case MTLPixelFormatBC3_RGBA: case MTLPixelFormatBC3_RGBA_sRGB: return 16;   // per block
    default: return 4;
    }
}

static bool rsm_fmt_compressed(MTLPixelFormat f) {
    return f == MTLPixelFormatBC1_RGBA || f == MTLPixelFormatBC1_RGBA_sRGB || f == MTLPixelFormatBC2_RGBA ||
           f == MTLPixelFormatBC2_RGBA_sRGB || f == MTLPixelFormatBC3_RGBA || f == MTLPixelFormatBC3_RGBA_sRGB;
}

static bool rsm_fmt_depth_stencil(MTLPixelFormat f) {
    return f == MTLPixelFormatDepth32Float || f == MTLPixelFormatDepth32Float_Stencil8 ||
           f == MTLPixelFormatStencil8 || f == MTLPixelFormatDepth16Unorm;
}

static MTLTextureSwizzleChannels rsm_swizzle(int swz) {
    const MTLTextureSwizzle R = MTLTextureSwizzleRed, G = MTLTextureSwizzleGreen,
                            B = MTLTextureSwizzleBlue, A = MTLTextureSwizzleAlpha,
                            Z = MTLTextureSwizzleZero, O = MTLTextureSwizzleOne;
    switch (swz) {
    case RSM_SWZ_RGB1: return MTLTextureSwizzleChannelsMake(R, G, B, O);
    case RSM_SWZ_LUM: return MTLTextureSwizzleChannelsMake(R, R, R, O);
    case RSM_SWZ_ALPHA: return MTLTextureSwizzleChannelsMake(Z, Z, Z, R);
    case RSM_SWZ_LUM_ALPHA: return MTLTextureSwizzleChannelsMake(R, R, R, G);
    case RSM_SWZ_INTENSITY: return MTLTextureSwizzleChannelsMake(R, R, R, R);
    case RSM_SWZ_RED: return MTLTextureSwizzleChannelsMake(R, Z, Z, O);
    case RSM_SWZ_RG: return MTLTextureSwizzleChannelsMake(R, G, Z, O);
    }
    return MTLTextureSwizzleChannelsMake(R, G, B, A);
}

// --- Pixel conversion ----------------------------------------------------------------
static int rsm_format_components(GLenum format) {
    switch (format) {
    case GL_RED: case GL_GREEN: case GL_BLUE: case GL_ALPHA: case GL_LUMINANCE:
    case GL_DEPTH_COMPONENT: case GL_STENCIL_INDEX: case GL_INTENSITY:
    case 0x84F9 /* GL_DEPTH_STENCIL */:
        return 1;
    case GL_LUMINANCE_ALPHA: case GL_RG: return 2;
    case GL_RGB: case GL_BGR: return 3;
    case GL_RGBA: case GL_BGRA: case GL_ABGR_EXT: return 4;
    }
    return 0;
}

static int rsm_packed_components(GLenum type) {  // >0: packed type with that many fields
    switch (type) {
    case GL_UNSIGNED_BYTE_3_3_2: case GL_UNSIGNED_BYTE_2_3_3_REV: case GL_UNSIGNED_SHORT_5_6_5:
    case GL_UNSIGNED_SHORT_5_6_5_REV:
        return 3;
    case GL_UNSIGNED_SHORT_4_4_4_4: case GL_UNSIGNED_SHORT_4_4_4_4_REV: case GL_UNSIGNED_SHORT_5_5_5_1:
    case GL_UNSIGNED_SHORT_1_5_5_5_REV: case GL_UNSIGNED_INT_8_8_8_8: case GL_UNSIGNED_INT_8_8_8_8_REV:
    case GL_UNSIGNED_INT_10_10_10_2: case GL_UNSIGNED_INT_2_10_10_10_REV:
        return 4;
    case 0x84FA /* GL_UNSIGNED_INT_24_8 */:
        return 2;
    }
    return 0;
}

static int rsm_type_size(GLenum type) {
    switch (type) {
    case GL_UNSIGNED_BYTE: case GL_BYTE: case GL_UNSIGNED_BYTE_3_3_2: case GL_UNSIGNED_BYTE_2_3_3_REV:
        return 1;
    case GL_UNSIGNED_SHORT: case GL_SHORT: case GL_HALF_FLOAT: case GL_UNSIGNED_SHORT_5_6_5:
    case GL_UNSIGNED_SHORT_5_6_5_REV: case GL_UNSIGNED_SHORT_4_4_4_4: case GL_UNSIGNED_SHORT_4_4_4_4_REV:
    case GL_UNSIGNED_SHORT_5_5_5_1: case GL_UNSIGNED_SHORT_1_5_5_5_REV:
        return 2;
    case GL_UNSIGNED_INT: case GL_INT: case GL_FLOAT: case GL_UNSIGNED_INT_8_8_8_8:
    case GL_UNSIGNED_INT_8_8_8_8_REV: case GL_UNSIGNED_INT_10_10_10_2: case GL_UNSIGNED_INT_2_10_10_10_REV:
    case 0x84FA /* GL_UNSIGNED_INT_24_8 */:
        return 4;
    }
    return 0;
}

// Bytes per pixel of client data, 0 for an unsupported combination.
static size_t rsm_pixel_size(GLenum format, GLenum type) {
    int n = rsm_format_components(format), s = rsm_type_size(type);
    if (!n || !s)
        return 0;
    return rsm_packed_components(type) ? (size_t)s : (size_t)(n * s);
}

static size_t rsm_row_pitch(GLenum format, GLenum type, int width, int row_length, int alignment) {
    int l = row_length > 0 ? row_length : width;
    size_t s = (size_t)rsm_type_size(type), px = rsm_pixel_size(format, type);
    size_t bytes = px * (size_t)l;
    if (rsm_packed_components(type) || s < (size_t)alignment)
        bytes = (bytes + (size_t)alignment - 1) / (size_t)alignment * (size_t)alignment;
    return bytes;
}

// Bytes of client memory a w x h image spans with this pack/unpack state.
static size_t rsm_image_bytes(GLenum format, GLenum type, int w, int h, int row_length, int alignment,
                              int skip_rows, int skip_pixels) {
    if (w <= 0 || h <= 0)
        return 0;
    size_t sp = rsm_row_pitch(format, type, w, row_length, alignment);
    return sp * ((size_t)skip_rows + (size_t)h - 1) + rsm_pixel_size(format, type) * ((size_t)skip_pixels + (size_t)w);
}

static size_t rsm_unpack_bytes(RSMContext *ctx, GLenum format, GLenum type, int w, int h) {
    return rsm_image_bytes(format, type, w, h, ctx->unpack_row_length, ctx->unpack_alignment,
                           ctx->unpack_skip_rows, ctx->unpack_skip_pixels);
}

static float rsm_half_to_float(uint16_t h) {
    __fp16 f;
    memcpy(&f, &h, 2);
    return (float)f;
}

static uint16_t rsm_float_to_half(float v) {
    __fp16 f = (__fp16)v;
    uint16_t h;
    memcpy(&h, &f, 2);
    return h;
}

// One client pixel -> components in format order (normalized to [0,1] for
// unsigned normalized types, as GL converts them).
static void rsm_decode_px(const uint8_t *p, GLenum format, GLenum type, float out[4]) {
    int n = rsm_format_components(format);
    int pk = rsm_packed_components(type);
    float c[4] = {0, 0, 0, 1};
    if (pk) {
        uint32_t v = type == GL_UNSIGNED_BYTE_3_3_2 || type == GL_UNSIGNED_BYTE_2_3_3_REV ? p[0]
                     : rsm_type_size(type) == 2 ? *(const uint16_t *)p
                                                : *(const uint32_t *)p;
        static const struct { GLenum t; int rev; int bits[4]; } L[] = {
            {GL_UNSIGNED_BYTE_3_3_2, 0, {3, 3, 2, 0}}, {GL_UNSIGNED_BYTE_2_3_3_REV, 1, {3, 3, 2, 0}},
            {GL_UNSIGNED_SHORT_5_6_5, 0, {5, 6, 5, 0}}, {GL_UNSIGNED_SHORT_5_6_5_REV, 1, {5, 6, 5, 0}},
            {GL_UNSIGNED_SHORT_4_4_4_4, 0, {4, 4, 4, 4}}, {GL_UNSIGNED_SHORT_4_4_4_4_REV, 1, {4, 4, 4, 4}},
            {GL_UNSIGNED_SHORT_5_5_5_1, 0, {5, 5, 5, 1}}, {GL_UNSIGNED_SHORT_1_5_5_5_REV, 1, {5, 5, 5, 1}},
            {GL_UNSIGNED_INT_8_8_8_8, 0, {8, 8, 8, 8}}, {GL_UNSIGNED_INT_8_8_8_8_REV, 1, {8, 8, 8, 8}},
            {GL_UNSIGNED_INT_10_10_10_2, 0, {10, 10, 10, 2}}, {GL_UNSIGNED_INT_2_10_10_10_REV, 1, {10, 10, 10, 2}},
        };
        for (unsigned k = 0; k < sizeof L / sizeof L[0]; k++) {
            if (L[k].t != type)
                continue;
            int total = 0;
            for (int i = 0; i < pk; i++)
                total += L[k].bits[i];
            int shift = L[k].rev ? 0 : total;
            for (int i = 0; i < pk; i++) {
                int b = L[k].bits[i];
                if (!L[k].rev)
                    shift -= b;
                c[i] = (float)((v >> shift) & ((1u << b) - 1)) / (float)((1u << b) - 1);
                if (L[k].rev)
                    shift += b;
            }
        }
    } else {
        for (int i = 0; i < n; i++) {
            switch (type) {
            case GL_UNSIGNED_BYTE: c[i] = p[i] / 255.0f; break;
            case GL_BYTE: c[i] = fmaxf(((const int8_t *)p)[i] / 127.0f, -1); break;
            case GL_UNSIGNED_SHORT: c[i] = ((const uint16_t *)p)[i] / 65535.0f; break;
            case GL_SHORT: c[i] = fmaxf(((const int16_t *)p)[i] / 32767.0f, -1); break;
            case GL_UNSIGNED_INT: c[i] = (float)(((const uint32_t *)p)[i] / 4294967295.0); break;
            case GL_INT: c[i] = (float)fmax(((const int32_t *)p)[i] / 2147483647.0, -1); break;
            case GL_FLOAT: c[i] = ((const float *)p)[i]; break;
            case GL_HALF_FLOAT: c[i] = rsm_half_to_float(((const uint16_t *)p)[i]); break;
            }
        }
    }
    // Format order -> RGBA meaning.
    float r = 0, g = 0, b = 0, a = 1;
    switch (format) {
    case GL_RED: case GL_DEPTH_COMPONENT: case GL_STENCIL_INDEX: r = c[0]; break;
    case GL_GREEN: g = c[0]; break;
    case GL_BLUE: b = c[0]; break;
    case GL_ALPHA: a = c[0]; break;
    case GL_LUMINANCE: r = g = b = c[0]; break;
    case GL_INTENSITY: r = g = b = a = c[0]; break;
    case GL_LUMINANCE_ALPHA: r = g = b = c[0]; a = c[1]; break;
    case GL_RG: r = c[0]; g = c[1]; break;
    case GL_RGB: r = c[0]; g = c[1]; b = c[2]; break;
    case GL_BGR: b = c[0]; g = c[1]; r = c[2]; break;
    case GL_RGBA: r = c[0]; g = c[1]; b = c[2]; a = c[3]; break;
    case GL_BGRA: b = c[0]; g = c[1]; r = c[2]; a = c[3]; break;
    case GL_ABGR_EXT: a = c[0]; b = c[1]; g = c[2]; r = c[3]; break;
    }
    out[0] = r; out[1] = g; out[2] = b; out[3] = a;
}

static uint8_t rsm_unorm8(float v) {
    v = v < 0 ? 0 : v > 1 ? 1 : v;
    return (uint8_t)lrintf(v * 255.0f);
}

// RGBA meaning -> one texel of the Metal format chosen for (internal, swizzle).
static void rsm_encode_texel(const float c[4], MTLPixelFormat fmt, int swz, uint8_t *d) {
    float r = c[0], g = c[1], b = c[2], a = c[3];
    float s0 = r, s1 = g;  // single / dual channel stores
    switch (swz) {
    case RSM_SWZ_ALPHA: s0 = a; break;
    case RSM_SWZ_LUM_ALPHA: s1 = a; break;
    case RSM_SWZ_INTENSITY: s0 = r; break;
    }
    switch (fmt) {
    case MTLPixelFormatRGBA8Unorm: case MTLPixelFormatRGBA8Unorm_sRGB:
        d[0] = rsm_unorm8(r); d[1] = rsm_unorm8(g); d[2] = rsm_unorm8(b); d[3] = rsm_unorm8(a); break;
    case MTLPixelFormatBGRA8Unorm:
        d[0] = rsm_unorm8(b); d[1] = rsm_unorm8(g); d[2] = rsm_unorm8(r); d[3] = rsm_unorm8(a); break;
    case MTLPixelFormatR8Unorm: d[0] = rsm_unorm8(s0); break;
    case MTLPixelFormatRG8Unorm: d[0] = rsm_unorm8(s0); d[1] = rsm_unorm8(s1); break;
    case MTLPixelFormatR16Float: ((uint16_t *)d)[0] = rsm_float_to_half(s0); break;
    case MTLPixelFormatRG16Float:
        ((uint16_t *)d)[0] = rsm_float_to_half(s0); ((uint16_t *)d)[1] = rsm_float_to_half(s1); break;
    case MTLPixelFormatRGBA16Float:
        for (int i = 0; i < 4; i++) ((uint16_t *)d)[i] = rsm_float_to_half(c[i]);
        break;
    case MTLPixelFormatR32Float: case MTLPixelFormatDepth32Float: ((float *)d)[0] = s0; break;
    case MTLPixelFormatRG32Float: ((float *)d)[0] = s0; ((float *)d)[1] = s1; break;
    case MTLPixelFormatRGBA32Float: memcpy(d, c, 16); break;
    default: break;
    }
}

// One texel of a Metal format -> RGBA meaning as glGetTexImage / glReadPixels see
// it (luminance reads back as R, GL's rule for luminance -> RGBA packing).
static void rsm_decode_texel(const uint8_t *s, MTLPixelFormat fmt, int swz, float c[4]) {
    float v[4] = {0, 0, 0, 1};
    switch (fmt) {
    case MTLPixelFormatRGBA8Unorm: case MTLPixelFormatRGBA8Unorm_sRGB:
        for (int i = 0; i < 4; i++) v[i] = s[i] / 255.0f;
        break;
    case MTLPixelFormatBGRA8Unorm:
        v[0] = s[2] / 255.0f; v[1] = s[1] / 255.0f; v[2] = s[0] / 255.0f; v[3] = s[3] / 255.0f; break;
    case MTLPixelFormatR8Unorm: case MTLPixelFormatStencil8: v[0] = s[0] / 255.0f; break;
    case MTLPixelFormatRG8Unorm: v[0] = s[0] / 255.0f; v[1] = s[1] / 255.0f; break;
    case MTLPixelFormatR16Float: v[0] = rsm_half_to_float(((const uint16_t *)s)[0]); break;
    case MTLPixelFormatRG16Float:
        v[0] = rsm_half_to_float(((const uint16_t *)s)[0]); v[1] = rsm_half_to_float(((const uint16_t *)s)[1]); break;
    case MTLPixelFormatRGBA16Float:
        for (int i = 0; i < 4; i++) v[i] = rsm_half_to_float(((const uint16_t *)s)[i]);
        break;
    case MTLPixelFormatR32Float: case MTLPixelFormatDepth32Float: v[0] = ((const float *)s)[0]; break;
    case MTLPixelFormatRG32Float: v[0] = ((const float *)s)[0]; v[1] = ((const float *)s)[1]; break;
    case MTLPixelFormatRGBA32Float: memcpy(v, s, 16); break;
    default: break;
    }
    switch (swz) {
    case RSM_SWZ_RGB1: v[3] = 1; break;
    case RSM_SWZ_ALPHA: v[3] = v[0]; v[0] = 0; break;
    case RSM_SWZ_LUM_ALPHA: v[3] = v[1]; v[1] = 0; break;
    case RSM_SWZ_LUM: case RSM_SWZ_RED: v[1] = v[2] = 0; v[3] = 1; break;
    case RSM_SWZ_INTENSITY: v[1] = v[2] = 0; v[3] = 1; break;
    case RSM_SWZ_RG: v[2] = 0; v[3] = 1; break;
    }
    memcpy(c, v, sizeof v);
}

// RGBA meaning -> one client pixel.
static void rsm_encode_px(const float c[4], GLenum format, GLenum type, uint8_t *p) {
    float v[4] = {0};
    int n = rsm_format_components(format);
    switch (format) {
    case GL_RED: case GL_LUMINANCE: case GL_DEPTH_COMPONENT: case GL_STENCIL_INDEX: case GL_INTENSITY:
        v[0] = c[0]; break;
    case GL_GREEN: v[0] = c[1]; break;
    case GL_BLUE: v[0] = c[2]; break;
    case GL_ALPHA: v[0] = c[3]; break;
    case GL_LUMINANCE_ALPHA: v[0] = c[0]; v[1] = c[3]; break;
    case GL_RG: v[0] = c[0]; v[1] = c[1]; break;
    case GL_RGB: v[0] = c[0]; v[1] = c[1]; v[2] = c[2]; break;
    case GL_BGR: v[0] = c[2]; v[1] = c[1]; v[2] = c[0]; break;
    case GL_RGBA: memcpy(v, c, 16); break;
    case GL_BGRA: v[0] = c[2]; v[1] = c[1]; v[2] = c[0]; v[3] = c[3]; break;
    case GL_ABGR_EXT: v[0] = c[3]; v[1] = c[2]; v[2] = c[1]; v[3] = c[0]; break;
    }
    int pk = rsm_packed_components(type);
    if (pk) {
        static const struct { GLenum t; int rev; int bits[4]; } L[] = {
            {GL_UNSIGNED_SHORT_5_6_5, 0, {5, 6, 5, 0}}, {GL_UNSIGNED_SHORT_5_6_5_REV, 1, {5, 6, 5, 0}},
            {GL_UNSIGNED_SHORT_4_4_4_4, 0, {4, 4, 4, 4}}, {GL_UNSIGNED_SHORT_4_4_4_4_REV, 1, {4, 4, 4, 4}},
            {GL_UNSIGNED_SHORT_5_5_5_1, 0, {5, 5, 5, 1}}, {GL_UNSIGNED_SHORT_1_5_5_5_REV, 1, {5, 5, 5, 1}},
            {GL_UNSIGNED_INT_8_8_8_8, 0, {8, 8, 8, 8}}, {GL_UNSIGNED_INT_8_8_8_8_REV, 1, {8, 8, 8, 8}},
            {GL_UNSIGNED_INT_10_10_10_2, 0, {10, 10, 10, 2}}, {GL_UNSIGNED_INT_2_10_10_10_REV, 1, {10, 10, 10, 2}},
        };
        uint32_t out = 0;
        for (unsigned k = 0; k < sizeof L / sizeof L[0]; k++) {
            if (L[k].t != type)
                continue;
            int total = 0;
            for (int i = 0; i < pk; i++)
                total += L[k].bits[i];
            int shift = L[k].rev ? 0 : total;
            for (int i = 0; i < pk; i++) {
                int b = L[k].bits[i];
                if (!L[k].rev)
                    shift -= b;
                float x = v[i] < 0 ? 0 : v[i] > 1 ? 1 : v[i];
                out |= (uint32_t)lrintf(x * (float)((1u << b) - 1)) << shift;
                if (L[k].rev)
                    shift += b;
            }
        }
        if (rsm_type_size(type) == 2)
            *(uint16_t *)p = (uint16_t)out;
        else
            *(uint32_t *)p = out;
        return;
    }
    for (int i = 0; i < n; i++) {
        float x = v[i];
        float u = x < 0 ? 0 : x > 1 ? 1 : x;
        switch (type) {
        case GL_UNSIGNED_BYTE: p[i] = (uint8_t)lrintf(u * 255.0f); break;
        case GL_BYTE: ((int8_t *)p)[i] = (int8_t)lrintf(fmaxf(fminf(x, 1), -1) * 127.0f); break;
        case GL_UNSIGNED_SHORT: ((uint16_t *)p)[i] = (uint16_t)lrintf(u * 65535.0f); break;
        case GL_SHORT: ((int16_t *)p)[i] = (int16_t)lrintf(fmaxf(fminf(x, 1), -1) * 32767.0f); break;
        case GL_UNSIGNED_INT: ((uint32_t *)p)[i] = (uint32_t)llrint(u * 4294967295.0); break;
        case GL_INT: ((int32_t *)p)[i] = (int32_t)llrint(fmax(fmin(x, 1), -1) * 2147483647.0); break;
        case GL_FLOAT: ((float *)p)[i] = x; break;
        case GL_HALF_FLOAT: ((uint16_t *)p)[i] = rsm_float_to_half(x); break;
        }
    }
}

// Client pixels (unpack state) -> tightly packed texels of `fmt`. Returns the
// source itself when no conversion is needed (*pitch then is the client pitch).
static const void *rsm_unpack(RSMContext *ctx, const void *data, int w, int h, GLenum format,
                              GLenum type, MTLPixelFormat fmt, int swz, size_t *pitch, void **owned) {
    *owned = NULL;
    size_t px = rsm_pixel_size(format, type);
    size_t sp = rsm_row_pitch(format, type, w, ctx->unpack_row_length, ctx->unpack_alignment);
    const uint8_t *src = (const uint8_t *)data + (size_t)ctx->unpack_skip_rows * sp +
                         (size_t)ctx->unpack_skip_pixels * px;
    bool rgba8 = (type == GL_UNSIGNED_BYTE || type == GL_UNSIGNED_INT_8_8_8_8_REV);
    bool direct = (fmt == MTLPixelFormatRGBA8Unorm || fmt == MTLPixelFormatRGBA8Unorm_sRGB)
                      ? format == GL_RGBA && rgba8
                  : fmt == MTLPixelFormatBGRA8Unorm ? format == GL_BGRA && rgba8
                  : fmt == MTLPixelFormatR8Unorm
                      ? type == GL_UNSIGNED_BYTE &&
                            (format == GL_LUMINANCE || format == GL_ALPHA || format == GL_RED ||
                             format == GL_INTENSITY)
                  : fmt == MTLPixelFormatRG8Unorm
                      ? type == GL_UNSIGNED_BYTE && (format == GL_LUMINANCE_ALPHA || format == GL_RG)
                  : fmt == MTLPixelFormatRGBA32Float ? format == GL_RGBA && type == GL_FLOAT
                  : fmt == MTLPixelFormatRGBA16Float ? format == GL_RGBA && type == GL_HALF_FLOAT
                  : fmt == MTLPixelFormatR32Float || fmt == MTLPixelFormatDepth32Float
                      ? type == GL_FLOAT && rsm_format_components(format) == 1
                      : false;
    if (direct) {
        *pitch = sp;
        return src;
    }
    size_t tb = rsm_fmt_bytes(fmt);
    uint8_t *out = malloc((size_t)w * (size_t)h * tb + 16);
    for (int y = 0; y < h; y++) {
        const uint8_t *s = src + (size_t)y * sp;
        uint8_t *d = out + (size_t)y * (size_t)w * tb;
        if ((fmt == MTLPixelFormatRGBA8Unorm || fmt == MTLPixelFormatBGRA8Unorm) && type == GL_UNSIGNED_BYTE &&
            (format == GL_RGB || format == GL_BGR || format == GL_RGBA || format == GL_BGRA)) {
            // Fast paths: 8-bit RGB(A)/BGR(A) into RGBA8/BGRA8.
            int n = format == GL_RGB || format == GL_BGR ? 3 : 4;
            bool swap = (format == GL_BGR || format == GL_BGRA) != (fmt == MTLPixelFormatBGRA8Unorm);
            for (int x = 0; x < w; x++, s += n, d += 4) {
                d[0] = swap ? s[2] : s[0];
                d[1] = s[1];
                d[2] = swap ? s[0] : s[2];
                d[3] = n == 4 ? s[3] : 255;
            }
            continue;
        }
        for (int x = 0; x < w; x++, s += px, d += tb) {
            float c[4];
            rsm_decode_px(s, format, type, c);
            rsm_encode_texel(c, fmt, swz, d);
        }
    }
    *owned = out;
    *pitch = (size_t)w * tb;
    return out;
}

void rsm_pack_pixels(RSMContext *ctx, const void *src, MTLPixelFormat sfmt, int swz, int w, int h,
                     GLenum format, GLenum type, void *dst) {
    size_t px = rsm_pixel_size(format, type);
    size_t dp = rsm_row_pitch(format, type, w, ctx->pack_row_length, ctx->pack_alignment);
    uint8_t *base = (uint8_t *)dst + (size_t)ctx->pack_skip_rows * dp + (size_t)ctx->pack_skip_pixels * px;
    size_t tb = rsm_fmt_bytes(sfmt);
    for (int y = 0; y < h; y++) {
        const uint8_t *s = (const uint8_t *)src + (size_t)y * (size_t)w * tb;
        uint8_t *d = base + (size_t)y * dp;
        if (swz == RSM_SWZ_NONE && type == GL_UNSIGNED_BYTE &&
            ((sfmt == MTLPixelFormatRGBA8Unorm && format == GL_RGBA) ||
             (sfmt == MTLPixelFormatBGRA8Unorm && format == GL_BGRA))) {
            memcpy(d, s, (size_t)w * 4);
            continue;
        }
        if (swz == RSM_SWZ_NONE && type == GL_UNSIGNED_BYTE &&
            ((sfmt == MTLPixelFormatRGBA8Unorm && format == GL_BGRA) ||
             (sfmt == MTLPixelFormatBGRA8Unorm && format == GL_RGBA))) {
            for (int x = 0; x < w; x++, s += 4, d += 4) {
                d[0] = s[2]; d[1] = s[1]; d[2] = s[0]; d[3] = s[3];
            }
            continue;
        }
        for (int x = 0; x < w; x++, s += tb, d += px) {
            float c[4];
            rsm_decode_texel(s, sfmt, swz, c);
            rsm_encode_px(c, format, type, d);
        }
    }
}

// --- BC1/2/3 decode (glGetTexImage of a DXT texture) -------------------------------
static void rsm_bc_colors(const uint8_t *b, bool dxt1, uint8_t pal[4][4]) {
    uint16_t c0 = (uint16_t)(b[0] | b[1] << 8), c1 = (uint16_t)(b[2] | b[3] << 8);
    for (int i = 0; i < 2; i++) {
        uint16_t c = i ? c1 : c0;
        pal[i][0] = (uint8_t)(((c >> 11) & 31) * 255 / 31);
        pal[i][1] = (uint8_t)(((c >> 5) & 63) * 255 / 63);
        pal[i][2] = (uint8_t)((c & 31) * 255 / 31);
        pal[i][3] = 255;
    }
    for (int k = 0; k < 3; k++) {
        if (c0 > c1 || !dxt1) {
            pal[2][k] = (uint8_t)((2 * pal[0][k] + pal[1][k]) / 3);
            pal[3][k] = (uint8_t)((pal[0][k] + 2 * pal[1][k]) / 3);
        } else {
            pal[2][k] = (uint8_t)((pal[0][k] + pal[1][k]) / 2);
            pal[3][k] = 0;
        }
    }
    pal[2][3] = 255;
    pal[3][3] = (c0 > c1 || !dxt1) ? 255 : 0;
}

static void rsm_bc_decode(const uint8_t *blk, MTLPixelFormat fmt, uint8_t out[16][4]) {
    bool bc1 = fmt == MTLPixelFormatBC1_RGBA || fmt == MTLPixelFormatBC1_RGBA_sRGB;
    bool bc2 = fmt == MTLPixelFormatBC2_RGBA || fmt == MTLPixelFormatBC2_RGBA_sRGB;
    const uint8_t *cb = bc1 ? blk : blk + 8;
    uint8_t pal[4][4];
    rsm_bc_colors(cb, bc1, pal);
    uint32_t idx = (uint32_t)(cb[4] | cb[5] << 8 | cb[6] << 16 | (uint32_t)cb[7] << 24);
    for (int i = 0; i < 16; i++)
        memcpy(out[i], pal[(idx >> (2 * i)) & 3], 4);
    if (bc2) {
        for (int i = 0; i < 16; i++)
            out[i][3] = (uint8_t)(((blk[i / 2] >> (4 * (i & 1))) & 15) * 17);
    } else if (!bc1) {
        uint8_t a[8] = {blk[0], blk[1]};
        if (a[0] > a[1])
            for (int i = 1; i < 7; i++)
                a[i + 1] = (uint8_t)(((7 - i) * a[0] + i * a[1]) / 7);
        else {
            for (int i = 1; i < 5; i++)
                a[i + 1] = (uint8_t)(((5 - i) * a[0] + i * a[1]) / 5);
            a[6] = 0;
            a[7] = 255;
        }
        uint64_t bits = 0;
        for (int i = 0; i < 6; i++)
            bits |= (uint64_t)blk[2 + i] << (8 * i);
        for (int i = 0; i < 16; i++)
            out[i][3] = a[(bits >> (3 * i)) & 7];
    }
}

// --- Textures ---------------------------------------------------------------------
RSMTexture *rsm_texture(RSMContext *ctx, GLuint name) {
    RSMTexture *t = rsm_get(&ctx->textures, name);
    if (!t && name) {
        t = [RSMTexture new];
        t->p = (rsm_texparams){GL_NEAREST_MIPMAP_LINEAR, GL_LINEAR, GL_REPEAT, GL_REPEAT, GL_REPEAT,
                               GL_NONE, GL_LEQUAL, 1, -1000, 1000, 0, 1000};
        rsm_names_set(&ctx->textures, name, t);
    }
    return t;
}

void rsmtl_glGenTextures(GLsizei n, GLuint *textures) {
    RSM_CTX()
    if (n < 0) {
        rsm_error(ctx, GL_INVALID_VALUE);
        return;
    }
    for (GLsizei i = 0; i < n; i++) {
        textures[i] = rsm_names_alloc(&ctx->textures);
        rsm_texture(ctx, textures[i]);
    }
}

void rsmtl_glDeleteTextures(GLsizei n, const GLuint *textures) {
    RSM_CTX()
    for (GLsizei i = 0; i < n; i++) {
        GLuint t = textures[i];
        if (!t || !rsm_get(&ctx->textures, t))
            continue;
        for (int u = 0; u < RSM_MAX_UNITS; u++) {
            if (ctx->tex2d[u] == t)
                ctx->tex2d[u] = 0;
            if (ctx->texcube[u] == t)
                ctx->texcube[u] = 0;
        }
        // Detached from the bound framebuffers only; others keep the object (GL rules).
        id obj = rsm_get(&ctx->textures, t);
        GLuint fbs[2] = {ctx->draw_fb, ctx->read_fb};
        for (int k = 0; k < 2; k++) {
            RSMFramebuffer *fb = rsm_get(&ctx->framebuffers, fbs[k]);
            for (int a = 0; fb && a < RSM_ATT_COUNT; a++)
                if (fb->att[a].obj == obj) {
                    fb->att[a] = (rsm_attachment){0};
                    if (fbs[k] == ctx->draw_fb)
                        rsm_fb_changed(ctx);
                }
        }
        rsm_names_set(&ctx->textures, t, nil);
    }
    ctx->dirty |= RSM_DIRTY_TEXTURES;
}

GLboolean rsmtl_glIsTexture(GLuint texture) {
    RSM_CTX(GL_FALSE)
    RSMTexture *t = rsm_get(&ctx->textures, texture);
    return t && t->bound;
}

void rsmtl_glActiveTexture(GLenum texture) {
    RSM_CTX()
    if (texture < GL_TEXTURE0 || texture >= GL_TEXTURE0 + RSM_MAX_UNITS) {
        rsm_error(ctx, GL_INVALID_ENUM);
        return;
    }
    ctx->active_unit = (int)(texture - GL_TEXTURE0);
}

void rsmtl_glBindTexture(GLenum target, GLuint texture) {
    RSM_CTX()
    GLuint *slot;
    if (target == GL_TEXTURE_1D)  // a 2D texture of height 1 (the translator's sampler1D)
        target = GL_TEXTURE_2D;
    if (target == GL_TEXTURE_2D)
        slot = &ctx->tex2d[ctx->active_unit];
    else if (target == GL_TEXTURE_CUBE_MAP)
        slot = &ctx->texcube[ctx->active_unit];
    else {
        rsm_error(ctx, GL_INVALID_ENUM);
        return;
    }
    if (texture) {
        RSMTexture *t = rsm_texture(ctx, texture);
        if (t->target && t->target != target) {
            rsm_error(ctx, GL_INVALID_OPERATION);
            return;
        }
        t->target = target;
        t->bound = true;
    }
    if (*slot != texture) {
        *slot = texture;
        ctx->dirty |= RSM_DIRTY_TEXTURES;
    }
}

static RSMTexture *rsm_bound_texture(RSMContext *ctx, GLenum target, int *face) {
    *face = 0;
    if (target == GL_TEXTURE_1D)
        target = GL_TEXTURE_2D;
    GLuint name;
    if (target == GL_TEXTURE_2D)
        name = ctx->tex2d[ctx->active_unit];
    else if (target == GL_TEXTURE_CUBE_MAP)
        name = ctx->texcube[ctx->active_unit];
    else if (target >= GL_TEXTURE_CUBE_MAP_POSITIVE_X && target <= GL_TEXTURE_CUBE_MAP_NEGATIVE_Z) {
        name = ctx->texcube[ctx->active_unit];
        *face = (int)(target - GL_TEXTURE_CUBE_MAP_POSITIVE_X);
    } else {
        rsm_error(ctx, GL_INVALID_ENUM);
        return nil;
    }
    RSMTexture *t = rsm_get(&ctx->textures, name);
    if (!t)
        rsm_error(ctx, GL_INVALID_OPERATION);  // texture object 0: not supported
    return t;
}

// --- Texture parameters / samplers ------------------------------------------------------
static void rsm_tex_param(RSMContext *ctx, GLenum target, GLenum pname, float v) {
    int face;
    if (target != GL_TEXTURE_2D && target != GL_TEXTURE_CUBE_MAP && target != GL_TEXTURE_1D) {
        rsm_error(ctx, GL_INVALID_ENUM);
        return;
    }
    RSMTexture *t = rsm_bound_texture(ctx, target, &face);
    if (!t)
        return;
    rsm_texparams p = t->p;
    GLenum e = (GLenum)v;
    switch (pname) {
    case GL_TEXTURE_MIN_FILTER: p.min_filter = e; break;
    case GL_TEXTURE_MAG_FILTER: p.mag_filter = e; break;
    case GL_TEXTURE_WRAP_S: p.wrap_s = e; break;
    case GL_TEXTURE_WRAP_T: p.wrap_t = e; break;
    case GL_TEXTURE_WRAP_R: p.wrap_r = e; break;
    case GL_TEXTURE_COMPARE_MODE: p.compare_mode = e; break;
    case GL_TEXTURE_COMPARE_FUNC: p.compare_func = e; break;
    case GL_TEXTURE_MAX_ANISOTROPY_EXT: p.max_aniso = v < 1 ? 1 : v > 16 ? 16 : v; break;
    case GL_TEXTURE_MIN_LOD: p.min_lod = v; break;
    case GL_TEXTURE_MAX_LOD: p.max_lod = v; break;
    case GL_TEXTURE_BASE_LEVEL: p.base_level = (GLint)v; break;
    case GL_TEXTURE_MAX_LEVEL: p.max_level = (GLint)v; break;
    case GL_TEXTURE_LOD_BIAS: case GL_GENERATE_MIPMAP: case GL_TEXTURE_PRIORITY:
    case GL_DEPTH_TEXTURE_MODE: case GL_TEXTURE_STORAGE_HINT_APPLE:
        return;  // no Metal equivalent; accepted
    default:
        rsm_error(ctx, GL_INVALID_ENUM);
        return;
    }
    if (memcmp(&p, &t->p, sizeof p)) {
        t->p = p;
        t->sampler = nil;
        ctx->dirty |= RSM_DIRTY_TEXTURES;
    }
}

void rsmtl_glTexParameteri(GLenum target, GLenum pname, GLint param) {
    RSM_CTX()
    rsm_tex_param(ctx, target, pname, (float)param);
}

void rsmtl_glTexParameterf(GLenum target, GLenum pname, GLfloat param) {
    RSM_CTX()
    rsm_tex_param(ctx, target, pname, param);
}

void rsmtl_glTexParameteriv(GLenum target, GLenum pname, const GLint *params) {
    RSM_CTX()
    if (pname != GL_TEXTURE_BORDER_COLOR)
        rsm_tex_param(ctx, target, pname, (float)params[0]);
}

void rsmtl_glTexParameterfv(GLenum target, GLenum pname, const GLfloat *params) {
    RSM_CTX()
    if (pname != GL_TEXTURE_BORDER_COLOR)
        rsm_tex_param(ctx, target, pname, params[0]);
}

void rsmtl_glGetTexParameteriv(GLenum target, GLenum pname, GLint *params) {
    RSM_CTX()
    int face;
    RSMTexture *t = rsm_bound_texture(ctx, target, &face);
    if (!t)
        return;
    switch (pname) {
    case GL_TEXTURE_MIN_FILTER: *params = (GLint)t->p.min_filter; break;
    case GL_TEXTURE_MAG_FILTER: *params = (GLint)t->p.mag_filter; break;
    case GL_TEXTURE_WRAP_S: *params = (GLint)t->p.wrap_s; break;
    case GL_TEXTURE_WRAP_T: *params = (GLint)t->p.wrap_t; break;
    case GL_TEXTURE_WRAP_R: *params = (GLint)t->p.wrap_r; break;
    case GL_TEXTURE_COMPARE_MODE: *params = (GLint)t->p.compare_mode; break;
    case GL_TEXTURE_COMPARE_FUNC: *params = (GLint)t->p.compare_func; break;
    case GL_TEXTURE_MAX_ANISOTROPY_EXT: *params = (GLint)t->p.max_aniso; break;
    case GL_TEXTURE_MIN_LOD: *params = (GLint)t->p.min_lod; break;
    case GL_TEXTURE_MAX_LOD: *params = (GLint)t->p.max_lod; break;
    case GL_TEXTURE_BASE_LEVEL: *params = t->p.base_level; break;
    case GL_TEXTURE_MAX_LEVEL: *params = t->p.max_level; break;
    default: rsm_error(ctx, GL_INVALID_ENUM);
    }
}

void rsmtl_glGetTexParameterfv(GLenum target, GLenum pname, GLfloat *params) {
    RSM_CTX()
    if (pname == GL_TEXTURE_MAX_ANISOTROPY_EXT || pname == GL_TEXTURE_MIN_LOD || pname == GL_TEXTURE_MAX_LOD) {
        int face;
        RSMTexture *t = rsm_bound_texture(ctx, target, &face);
        if (t)
            *params = pname == GL_TEXTURE_MAX_ANISOTROPY_EXT ? t->p.max_aniso
                      : pname == GL_TEXTURE_MIN_LOD          ? t->p.min_lod
                                                             : t->p.max_lod;
        return;
    }
    GLint v = 0;
    rsmtl_glGetTexParameteriv(target, pname, &v);
    *params = (GLfloat)v;
}

static MTLSamplerAddressMode rsm_wrap(GLenum w) {
    switch (w) {
    case GL_CLAMP_TO_EDGE: case GL_CLAMP: return MTLSamplerAddressModeClampToEdge;
    case GL_MIRRORED_REPEAT: return MTLSamplerAddressModeMirrorRepeat;
    case GL_CLAMP_TO_BORDER: return MTLSamplerAddressModeClampToBorderColor;
    }
    return MTLSamplerAddressModeRepeat;
}

void rsm_texture_sampler(RSMContext *ctx, RSMTexture *t) {
    const rsm_texparams *p = &t->p;
    // Levels the sampler may use: GL_TEXTURE_MAX_LEVEL, and level 0 only when the
    // min filter has no mipmaps.
    bool mip = p->min_filter != GL_NEAREST && p->min_filter != GL_LINEAR;
    float max_lod = fminf(p->max_lod, (float)(p->max_level - p->base_level));
    float min_lod = fmaxf(p->min_lod, 0);
    if (!mip)
        max_lod = 0;
    int aniso = (int)p->max_aniso;
    uint64_t key = (uint64_t)(p->min_filter & 0xF) | (uint64_t)(p->mag_filter & 0xF) << 4 |
                   (uint64_t)rsm_wrap(p->wrap_s) << 8 | (uint64_t)rsm_wrap(p->wrap_t) << 12 |
                   (uint64_t)rsm_wrap(p->wrap_r) << 16 | (uint64_t)(aniso & 31) << 20 |
                   (uint64_t)(p->compare_mode == GL_COMPARE_R_TO_TEXTURE ? (p->compare_func & 7) + 1 : 0) << 25 |
                   (uint64_t)((int)fminf(fmaxf(min_lod * 4, 0), 255)) << 32 |
                   (uint64_t)((int)fminf(fmaxf(max_lod * 4, 0), 255)) << 40;
    id<MTLSamplerState> s = rsm_map_get(&ctx->samplers, &key);
    if (!s) {
        MTLSamplerDescriptor *d = [MTLSamplerDescriptor new];
        bool min_linear = p->min_filter == GL_LINEAR || p->min_filter == GL_LINEAR_MIPMAP_NEAREST ||
                          p->min_filter == GL_LINEAR_MIPMAP_LINEAR;
        d.minFilter = min_linear ? MTLSamplerMinMagFilterLinear : MTLSamplerMinMagFilterNearest;
        d.magFilter = p->mag_filter == GL_NEAREST ? MTLSamplerMinMagFilterNearest : MTLSamplerMinMagFilterLinear;
        d.mipFilter = !mip ? MTLSamplerMipFilterNotMipmapped
                      : (p->min_filter == GL_NEAREST_MIPMAP_LINEAR || p->min_filter == GL_LINEAR_MIPMAP_LINEAR)
                          ? MTLSamplerMipFilterLinear
                          : MTLSamplerMipFilterNearest;
        d.sAddressMode = rsm_wrap(p->wrap_s);
        d.tAddressMode = rsm_wrap(p->wrap_t);
        d.rAddressMode = rsm_wrap(p->wrap_r);
        d.borderColor = MTLSamplerBorderColorTransparentBlack;
        d.maxAnisotropy = (NSUInteger)(aniso < 1 ? 1 : aniso);
        d.lodMinClamp = min_lod;
        d.lodMaxClamp = max_lod < min_lod ? min_lod : max_lod;
        if (p->compare_mode == GL_COMPARE_R_TO_TEXTURE)
            d.compareFunction = (MTLCompareFunction)(p->compare_func - GL_NEVER);
        s = [ctx->dev newSamplerStateWithDescriptor:d];
        rsm_map_put(&ctx->samplers, &key, s);
    }
    t->sampler = s;
}

// Swizzle is not allowed on render-target textures: sample through a view.
id<MTLTexture> rsm_texture_view(RSMTexture *t) {
    if (t->swz == RSM_SWZ_NONE || !t->mtl)
        return t->mtl;
    if (!t->view) {
        t->view = [t->mtl newTextureViewWithPixelFormat:t->fmt
                                            textureType:t->mtl.textureType
                                                 levels:NSMakeRange(0, t->mtl.mipmapLevelCount)
                                                 slices:NSMakeRange(0, t->mtl.arrayLength * (t->mtl.textureType == MTLTextureTypeCube ? 6 : 1))
                                                swizzle:rsm_swizzle(t->swz)];
        if (!t->view)
            t->view = t->mtl;
    }
    return t->view;
}

// --- Texture storage ----------------------------------------------------------------
static int rsm_full_levels(int w, int h) {
    int m = w > h ? w : h, n = 1;
    while (m > 1) {
        m >>= 1;
        n++;
    }
    return n;
}

static id<MTLTexture> rsm_tex_new(RSMContext *ctx, RSMTexture *t, MTLPixelFormat fmt, int w, int h,
                                  int levels) {
    MTLTextureDescriptor *d = [MTLTextureDescriptor new];
    d.textureType = t->target == GL_TEXTURE_CUBE_MAP ? MTLTextureTypeCube : MTLTextureType2D;
    d.pixelFormat = fmt;
    d.width = (NSUInteger)w;
    d.height = (NSUInteger)h;
    d.mipmapLevelCount = (NSUInteger)levels;
    bool ds = rsm_fmt_depth_stencil(fmt);
    d.usage = MTLTextureUsageShaderRead | (rsm_fmt_compressed(fmt) ? 0 : MTLTextureUsageRenderTarget);
    d.storageMode = ds ? MTLStorageModePrivate : ctx->tex_storage;
    id<MTLTexture> tex = [ctx->dev newTextureWithDescriptor:d];
    if (!tex)
        rsm_log("can't create a %dx%d texture (format %lu, %d levels)", w, h, (unsigned long)fmt, levels);
    return tex;
}

// Copy what exists of `old` into a texture with `levels` levels.
static void rsm_tex_grow(RSMContext *ctx, RSMTexture *t, int levels) {
    id<MTLTexture> old = t->mtl;
    id<MTLTexture> nt = rsm_tex_new(ctx, t, t->fmt, t->w, t->h, levels);
    if (!nt)
        return;
    rsm_end_pass(ctx);
    id<MTLBlitCommandEncoder> b = [rsm_cmd(ctx) blitCommandEncoder];
    int slices = t->target == GL_TEXTURE_CUBE_MAP ? 6 : 1;
    for (int l = 0; l < t->levels && l < levels; l++) {
        if (!(t->defined & (1u << l)))
            continue;
        for (int s = 0; s < slices; s++)
            [b copyFromTexture:old sourceSlice:(NSUInteger)s sourceLevel:(NSUInteger)l
                     toTexture:nt destinationSlice:(NSUInteger)s destinationLevel:(NSUInteger)l
                    sliceCount:1 levelCount:1];
    }
    [b endEncoding];
    t->mtl = nt;
    t->view = nil;
    t->levels = levels;
    t->last_use = ctx->serial;
    // New storage: a framebuffer with it attached must render into the new texture.
    ctx->fb_dirty = true;
    ctx->dirty |= RSM_DIRTY_TEXTURES;
}

// Make storage for (level, w, h) of `fmt`. Level 0 decides the texture; other
// levels grow the mip chain when needed.
static bool rsm_tex_define(RSMContext *ctx, RSMTexture *t, int level, GLenum internal,
                           const rsm_texfmt *f, int w, int h) {
    if (level == 0) {
        bool same = t->mtl && t->fmt == f->fmt && t->w == w && t->h == h && t->swz == f->swz;
        if (!same) {
            // Keep the mip chain only for the same size (a format change): a smaller level 0
            // can't hold the old level count.
            int levels = t->mtl && t->levels > 1 && t->w == w && t->h == h ? t->levels : 1;
            t->fmt = f->fmt;
            t->swz = f->swz;
            t->w = w;
            t->h = h;
            t->mtl = rsm_tex_new(ctx, t, f->fmt, w, h, levels);
            t->view = nil;
            t->levels = levels;
            t->defined = 0;
            t->last_use = 0;
            ctx->dirty |= RSM_DIRTY_TEXTURES;
            rsm_fb_changed(ctx);  // a render target may have been replaced
        }
        t->internal = internal;
        t->defined |= 1;
        return t->mtl != nil;
    }
    if (!t->mtl) {
        // A mip level before level 0: size the texture from it.
        rsm_texfmt f0 = *f;
        if (!rsm_tex_define(ctx, t, 0, internal, &f0, w << level, h << level))
            return false;
        t->defined = 0;
    }
    int lw = t->w >> level, lh = t->h >> level;
    if ((lw ? lw : 1) != w || (lh ? lh : 1) != h || f->fmt != t->fmt) {
        rsm_log("mip level %d of a %dx%d texture is %dx%d: ignored", level, t->w, t->h, w, h);
        return false;
    }
    if (level >= t->levels)
        rsm_tex_grow(ctx, t, rsm_full_levels(t->w, t->h));
    t->defined |= 1u << level;
    return level < t->levels;
}

// Write a region: directly when the GPU is done with the texture, else through a
// staging buffer and a blit ordered after the commands already recorded.
static void rsm_tex_write(RSMContext *ctx, RSMTexture *t, int level, int slice, int x, int y, int w, int h,
                          const void *bytes, size_t pitch) {
    if (!t->mtl || w <= 0 || h <= 0)
        return;
    bool comp = rsm_fmt_compressed(t->fmt);
    size_t rows = comp ? (size_t)(h + 3) / 4 : (size_t)h;
    MTLRegion r = MTLRegionMake2D((NSUInteger)x, (NSUInteger)y, (NSUInteger)w, (NSUInteger)h);
    if (!rsm_in_flight(ctx, t->last_use) && t->mtl.storageMode != MTLStorageModePrivate) {
        [t->mtl replaceRegion:r mipmapLevel:(NSUInteger)level slice:(NSUInteger)slice withBytes:bytes
                  bytesPerRow:pitch bytesPerImage:pitch * rows];
        return;
    }
    size_t row_bytes = comp ? ((size_t)w + 3) / 4 * rsm_fmt_bytes(t->fmt) : (size_t)w * rsm_fmt_bytes(t->fmt);
    __unsafe_unretained id<MTLBuffer> sb;
    NSUInteger off;
    uint8_t *d = rsm_transient(ctx, row_bytes * rows, 16, &sb, &off);
    for (size_t i = 0; i < rows; i++)
        memcpy(d + i * row_bytes, (const uint8_t *)bytes + i * pitch, row_bytes);
    rsm_end_pass(ctx);
    id<MTLBlitCommandEncoder> b = [rsm_cmd(ctx) blitCommandEncoder];
    [b copyFromBuffer:sb sourceOffset:off sourceBytesPerRow:row_bytes sourceBytesPerImage:row_bytes * rows
           sourceSize:MTLSizeMake((NSUInteger)w, (NSUInteger)h, 1)
            toTexture:t->mtl destinationSlice:(NSUInteger)slice destinationLevel:(NSUInteger)level
    destinationOrigin:MTLOriginMake((NSUInteger)x, (NSUInteger)y, 0)];
    [b endEncoding];
    t->last_use = ctx->serial;
}

void rsmtl_glTexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height,
                        GLint border, GLenum format, GLenum type, const GLvoid *pixels) {
    RSM_CTX()
    int face;
    if (target == GL_TEXTURE_CUBE_MAP) {
        rsm_error(ctx, GL_INVALID_ENUM);
        return;
    }
    RSMTexture *t = rsm_bound_texture(ctx, target, &face);
    if (!t)
        return;
    if (level < 0 || width < 0 || height < 0 || border != 0 || width > 16384 || height > 16384) {
        rsm_error(ctx, GL_INVALID_VALUE);
        return;
    }
    rsm_texfmt f;
    if (!rsm_choose_format((GLenum)internalformat, format, &f) || f.compressed) {
        rsm_bad_enum(ctx, "glTexImage2D internalformat", (GLenum)internalformat);
        return;
    }
    if (pixels && !rsm_pixel_size(format, type)) {
        rsm_bad_enum(ctx, "glTexImage2D format", format);
        rsm_log("glTexImage2D: type 0x%04x", type);
        return;
    }
    if (!width || !height)
        return;
    if (ctx->pixel_unpack_buffer &&
        !(pixels = rsm_unpack_ptr(ctx, pixels, rsm_unpack_bytes(ctx, format, type, width, height))))
        return;
    @autoreleasepool {
        if (!rsm_tex_define(ctx, t, level, (GLenum)internalformat, &f, width, height) || !pixels || f.stencil)
            return;
        size_t pitch;
        void *owned;
        const void *src = rsm_unpack(ctx, pixels, width, height, format, type, t->fmt, t->swz, &pitch, &owned);
        if (f.depth) {
            // Depth textures are private: always through a blit.
            uint64_t keep = t->last_use;
            t->last_use = ctx->serial;
            rsm_tex_write(ctx, t, level, face, 0, 0, width, height, src, pitch);
            if (!keep)
                t->last_use = ctx->serial;
        } else {
            rsm_tex_write(ctx, t, level, face, 0, 0, width, height, src, pitch);
        }
        free(owned);
    }
}

void rsmtl_glTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width,
                           GLsizei height, GLenum format, GLenum type, const GLvoid *pixels) {
    RSM_CTX()
    int face;
    if (target == GL_TEXTURE_CUBE_MAP) {
        rsm_error(ctx, GL_INVALID_ENUM);
        return;
    }
    RSMTexture *t = rsm_bound_texture(ctx, target, &face);
    if (!t)
        return;
    if (!t->mtl || level < 0 || level >= t->levels || !(t->defined & (1u << level))) {
        rsm_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    int lw = t->w >> level, lh = t->h >> level;
    lw = lw ? lw : 1;
    lh = lh ? lh : 1;
    if (xoffset < 0 || yoffset < 0 || width < 0 || height < 0 || xoffset + width > lw || yoffset + height > lh) {
        rsm_error(ctx, GL_INVALID_VALUE);
        return;
    }
    if (rsm_fmt_compressed(t->fmt) || rsm_fmt_depth_stencil(t->fmt) || !rsm_pixel_size(format, type)) {
        rsm_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    if (!width || !height)
        return;
    pixels = rsm_unpack_ptr(ctx, pixels, rsm_unpack_bytes(ctx, format, type, width, height));
    if (!pixels)
        return;
    @autoreleasepool {
        size_t pitch;
        void *owned;
        const void *src = rsm_unpack(ctx, pixels, width, height, format, type, t->fmt, t->swz, &pitch, &owned);
        rsm_tex_write(ctx, t, level, face, xoffset, yoffset, width, height, src, pitch);
        free(owned);
    }
}

void rsmtl_glCompressedTexImage2D(GLenum target, GLint level, GLenum internalformat, GLsizei width,
                                  GLsizei height, GLint border, GLsizei imageSize, const GLvoid *data) {
    RSM_CTX()
    int face;
    if (target == GL_TEXTURE_CUBE_MAP) {
        rsm_error(ctx, GL_INVALID_ENUM);
        return;
    }
    RSMTexture *t = rsm_bound_texture(ctx, target, &face);
    if (!t)
        return;
    rsm_texfmt f;
    if (!rsm_is_dxt(internalformat) || !rsm_choose_format(internalformat, 0, &f)) {
        rsm_error(ctx, GL_INVALID_ENUM);
        return;
    }
    if (level < 0 || width <= 0 || height <= 0 || border != 0) {
        rsm_error(ctx, GL_INVALID_VALUE);
        return;
    }
    size_t pitch = (size_t)(width + 3) / 4 * rsm_fmt_bytes(f.fmt);
    size_t need = pitch * (size_t)((height + 3) / 4);
    if ((size_t)imageSize < need) {
        rsm_error(ctx, GL_INVALID_VALUE);
        return;
    }
    if (ctx->pixel_unpack_buffer && !(data = rsm_unpack_ptr(ctx, data, need)))
        return;
    @autoreleasepool {
        if (!rsm_tex_define(ctx, t, level, internalformat, &f, width, height) || !data)
            return;
        rsm_tex_write(ctx, t, level, face, 0, 0, width, height, data, pitch);
    }
}

void rsmtl_glCompressedTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width,
                                     GLsizei height, GLenum format, GLsizei imageSize, const GLvoid *data) {
    RSM_CTX()
    int face;
    RSMTexture *t = rsm_bound_texture(ctx, target, &face);
    if (!t)
        return;
    if (level < 0 || xoffset < 0 || yoffset < 0 || width < 0 || height < 0 || imageSize < 0) {
        rsm_error(ctx, GL_INVALID_VALUE);
        return;
    }
    if (!t->mtl || !rsm_fmt_compressed(t->fmt) || level >= t->levels || !(t->defined & (1u << level)) ||
        (xoffset & 3) || (yoffset & 3)) {
        rsm_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    // Inside the level, whole blocks except at its right / top edge.
    int lw = t->w >> level ? t->w >> level : 1, lh = t->h >> level ? t->h >> level : 1;
    if (xoffset + width > lw || yoffset + height > lh || ((width & 3) && xoffset + width != lw) ||
        ((height & 3) && yoffset + height != lh)) {
        rsm_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    (void)format;
    size_t pitch = (size_t)(width + 3) / 4 * rsm_fmt_bytes(t->fmt);
    size_t need = pitch * (size_t)((height + 3) / 4);
    if ((size_t)imageSize < need) {
        rsm_error(ctx, GL_INVALID_VALUE);
        return;
    }
    if (!width || !height || !(data = rsm_unpack_ptr(ctx, data, need)))
        return;
    @autoreleasepool {
        rsm_tex_write(ctx, t, level, face, xoffset, yoffset, width, height, data, pitch);
    }
}

void rsmtl_glGenerateMipmap(GLenum target) {
    RSM_CTX()
    int face;
    if (target != GL_TEXTURE_2D && target != GL_TEXTURE_CUBE_MAP) {
        rsm_error(ctx, GL_INVALID_ENUM);
        return;
    }
    RSMTexture *t = rsm_bound_texture(ctx, target, &face);
    if (!t || !t->mtl) {
        rsm_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    @autoreleasepool {
        int full = rsm_full_levels(t->w, t->h);
        if (t->levels < full)
            rsm_tex_grow(ctx, t, full);
        if (full > 1 && !rsm_fmt_compressed(t->fmt) && !rsm_fmt_depth_stencil(t->fmt)) {
            rsm_end_pass(ctx);
            id<MTLBlitCommandEncoder> b = [rsm_cmd(ctx) blitCommandEncoder];
            [b generateMipmapsForTexture:t->mtl];
            [b endEncoding];
            t->last_use = ctx->serial;
        }
        t->defined = (1u << full) - 1;
        ctx->dirty |= RSM_DIRTY_TEXTURES;
    }
}

void rsmtl_glGetTexLevelParameteriv(GLenum target, GLint level, GLenum pname, GLint *params) {
    RSM_CTX()
    int face;
    RSMTexture *t = rsm_bound_texture(ctx, target, &face);
    if (!t)
        return;
    bool def = t->mtl && level >= 0 && level < t->levels && (t->defined & (1u << level));
    int lw = def ? (t->w >> level ? t->w >> level : 1) : 0;
    int lh = def ? (t->h >> level ? t->h >> level : 1) : 0;
    bool comp = def && rsm_fmt_compressed(t->fmt);
    int ch = 0, depth = 0, stencil = 0;
    if (def) {
        switch (t->fmt) {
        case MTLPixelFormatDepth32Float: depth = 32; break;
        case MTLPixelFormatDepth32Float_Stencil8: depth = 32; stencil = 8; break;
        case MTLPixelFormatStencil8: stencil = 8; break;
        case MTLPixelFormatRGBA16Float: case MTLPixelFormatR16Float: case MTLPixelFormatRG16Float: ch = 16; break;
        case MTLPixelFormatRGBA32Float: case MTLPixelFormatR32Float: case MTLPixelFormatRG32Float: ch = 32; break;
        default: ch = 8;
        }
    }
    switch (pname) {
    case GL_TEXTURE_WIDTH: *params = lw; break;
    case GL_TEXTURE_HEIGHT: *params = lh; break;
    case GL_TEXTURE_DEPTH: *params = def ? 1 : 0; break;
    case GL_TEXTURE_BORDER: *params = 0; break;
    case GL_TEXTURE_INTERNAL_FORMAT: *params = def ? (GLint)t->internal : 1; break;
    case GL_TEXTURE_COMPRESSED: *params = comp; break;
    case GL_TEXTURE_COMPRESSED_IMAGE_SIZE:
        if (!comp) {
            rsm_error(ctx, GL_INVALID_OPERATION);
            return;
        }
        *params = (GLint)((size_t)((lw + 3) / 4) * (size_t)((lh + 3) / 4) * rsm_fmt_bytes(t->fmt));
        break;
    case GL_TEXTURE_RED_SIZE: case GL_TEXTURE_GREEN_SIZE: case GL_TEXTURE_BLUE_SIZE:
    case GL_TEXTURE_ALPHA_SIZE: case GL_TEXTURE_LUMINANCE_SIZE: case GL_TEXTURE_INTENSITY_SIZE:
        *params = ch; break;
    case GL_TEXTURE_DEPTH_SIZE: *params = depth; break;
    case GL_TEXTURE_STENCIL_SIZE_EXT: *params = stencil; break;
    default: rsm_error(ctx, GL_INVALID_ENUM);
    }
}

// --- Reads ---------------------------------------------------------------------------
void *rsm_read_texture(RSMContext *ctx, id<MTLTexture> tex, int level, int slice, int x, int y, int w,
                       int h, MTLBlitOption opt, size_t *bpp) {
    size_t tb = opt == MTLBlitOptionDepthFromDepthStencil   ? 4
                : opt == MTLBlitOptionStencilFromDepthStencil ? 1
                                                              : rsm_fmt_bytes(tex.pixelFormat);
    bool comp = rsm_fmt_compressed(tex.pixelFormat);
    size_t row = comp ? (size_t)(w + 3) / 4 * tb : (size_t)w * tb;
    size_t rows = comp ? (size_t)(h + 3) / 4 : (size_t)h;
    id<MTLBuffer> buf = [ctx->dev newBufferWithLength:row * rows options:MTLResourceStorageModeShared];
    rsm_end_pass(ctx);
    id<MTLBlitCommandEncoder> b = [rsm_cmd(ctx) blitCommandEncoder];
    [b copyFromTexture:tex sourceSlice:(NSUInteger)slice sourceLevel:(NSUInteger)level
          sourceOrigin:MTLOriginMake((NSUInteger)x, (NSUInteger)y, 0)
            sourceSize:MTLSizeMake((NSUInteger)w, (NSUInteger)h, 1)
              toBuffer:buf destinationOffset:0 destinationBytesPerRow:row destinationBytesPerImage:row * rows
               options:opt];
    [b endEncoding];
    rsm_commit(ctx, true);
    void *out = malloc(row * rows);
    memcpy(out, buf.contents, row * rows);
    *bpp = tb;
    return out;
}

void rsmtl_glGetTexImage(GLenum target, GLint level, GLenum format, GLenum type, GLvoid *pixels) {
    RSM_CTX()
    int face;
    RSMTexture *t = rsm_bound_texture(ctx, target, &face);
    if (!t)
        return;
    if (!t->mtl || level < 0 || level >= t->levels) {
        rsm_error(ctx, GL_INVALID_VALUE);
        return;
    }
    if (!rsm_pixel_size(format, type)) {
        rsm_error(ctx, GL_INVALID_ENUM);
        return;
    }
    int lw = t->w >> level ? t->w >> level : 1, lh = t->h >> level ? t->h >> level : 1;
    pixels = rsm_pack_ptr(ctx, pixels,
                          rsm_image_bytes(format, type, lw, lh, ctx->pack_row_length, ctx->pack_alignment,
                                          ctx->pack_skip_rows, ctx->pack_skip_pixels));
    if (!pixels)
        return;
    @autoreleasepool {
        size_t bpp;
        MTLBlitOption opt = t->fmt == MTLPixelFormatDepth32Float_Stencil8
                                ? (format == GL_STENCIL_INDEX ? MTLBlitOptionStencilFromDepthStencil
                                                              : MTLBlitOptionDepthFromDepthStencil)
                                : MTLBlitOptionNone;
        uint8_t *raw = rsm_read_texture(ctx, t->mtl, level, face, 0, 0, lw, lh, opt, &bpp);
        if (rsm_fmt_compressed(t->fmt)) {
            uint8_t *rgba = malloc((size_t)lw * (size_t)lh * 4);
            int bw = (lw + 3) / 4, bh = (lh + 3) / 4;
            for (int by = 0; by < bh; by++)
                for (int bx = 0; bx < bw; bx++) {
                    uint8_t px[16][4];
                    rsm_bc_decode(raw + ((size_t)by * (size_t)bw + (size_t)bx) * bpp, t->fmt, px);
                    for (int i = 0; i < 16; i++) {
                        int x = bx * 4 + (i & 3), y = by * 4 + i / 4;
                        if (x < lw && y < lh)
                            memcpy(rgba + ((size_t)y * (size_t)lw + (size_t)x) * 4, px[i], 4);
                    }
                }
            rsm_pack_pixels(ctx, rgba, MTLPixelFormatRGBA8Unorm, t->swz, lw, lh, format, type, pixels);
            free(rgba);
        } else {
            MTLPixelFormat sf = opt == MTLBlitOptionDepthFromDepthStencil     ? MTLPixelFormatDepth32Float
                                : opt == MTLBlitOptionStencilFromDepthStencil ? MTLPixelFormatStencil8
                                                                              : t->fmt;
            rsm_pack_pixels(ctx, raw, sf, t->swz, lw, lh, format, type, pixels);
        }
        free(raw);
    }
}

// --- Renderbuffers -------------------------------------------------------------------
RSMRenderbuffer *rsm_renderbuffer(RSMContext *ctx, GLuint name) {
    RSMRenderbuffer *r = rsm_get(&ctx->renderbuffers, name);
    if (!r && name) {
        r = [RSMRenderbuffer new];
        rsm_names_set(&ctx->renderbuffers, name, r);
    }
    return r;
}

void rsmtl_glGenRenderbuffers(GLsizei n, GLuint *renderbuffers) {
    RSM_CTX()
    for (GLsizei i = 0; i < n; i++) {
        renderbuffers[i] = rsm_names_alloc(&ctx->renderbuffers);
        rsm_renderbuffer(ctx, renderbuffers[i]);
    }
}

void rsmtl_glDeleteRenderbuffers(GLsizei n, const GLuint *renderbuffers) {
    RSM_CTX()
    for (GLsizei i = 0; i < n; i++) {
        GLuint r = renderbuffers[i];
        if (!r || !rsm_get(&ctx->renderbuffers, r))
            continue;
        if (ctx->renderbuffer == r)
            ctx->renderbuffer = 0;
        id obj = rsm_get(&ctx->renderbuffers, r);
        GLuint fbs[2] = {ctx->draw_fb, ctx->read_fb};
        for (int k = 0; k < 2; k++) {
            RSMFramebuffer *fb = rsm_get(&ctx->framebuffers, fbs[k]);
            for (int a = 0; fb && a < RSM_ATT_COUNT; a++)
                if (fb->att[a].obj == obj) {
                    fb->att[a] = (rsm_attachment){0};
                    if (fbs[k] == ctx->draw_fb)
                        rsm_fb_changed(ctx);
                }
        }
        rsm_names_set(&ctx->renderbuffers, r, nil);
    }
}

void rsmtl_glBindRenderbuffer(GLenum target, GLuint renderbuffer) {
    RSM_CTX()
    if (target != GL_RENDERBUFFER) {
        rsm_error(ctx, GL_INVALID_ENUM);
        return;
    }
    if (renderbuffer)
        rsm_renderbuffer(ctx, renderbuffer)->bound = true;
    ctx->renderbuffer = renderbuffer;
}

GLboolean rsmtl_glIsRenderbuffer(GLuint renderbuffer) {
    RSM_CTX(GL_FALSE)
    RSMRenderbuffer *r = rsm_get(&ctx->renderbuffers, renderbuffer);
    return r && r->bound;
}

void rsmtl_glRenderbufferStorage(GLenum target, GLenum internalformat, GLsizei width, GLsizei height) {
    RSM_CTX()
    if (target != GL_RENDERBUFFER) {
        rsm_error(ctx, GL_INVALID_ENUM);
        return;
    }
    RSMRenderbuffer *r = rsm_get(&ctx->renderbuffers, ctx->renderbuffer);
    if (!r) {
        rsm_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    rsm_texfmt f;
    if (!rsm_choose_format(internalformat, GL_RGBA, &f) || f.compressed) {
        rsm_error(ctx, GL_INVALID_ENUM);
        return;
    }
    if (width < 0 || height < 0 || width > 16384 || height > 16384) {
        rsm_error(ctx, GL_INVALID_VALUE);
        return;
    }
    @autoreleasepool {
        r->internal = internalformat;
        r->w = width;
        r->h = height;
        r->mtl = nil;
        if (width && height) {
            MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:f.fmt
                                                                                        width:(NSUInteger)width
                                                                                       height:(NSUInteger)height
                                                                                    mipmapped:NO];
            d.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
            d.storageMode = MTLStorageModePrivate;
            r->mtl = [ctx->dev newTextureWithDescriptor:d];
            r->mtl.label = @"rsgl renderbuffer";
        }
        rsm_fb_changed(ctx);
    }
}

void rsmtl_glGetRenderbufferParameteriv(GLenum target, GLenum pname, GLint *params) {
    RSM_CTX()
    RSMRenderbuffer *r = rsm_get(&ctx->renderbuffers, ctx->renderbuffer);
    if (target != GL_RENDERBUFFER) {
        rsm_error(ctx, GL_INVALID_ENUM);
        return;
    }
    if (!r) {
        rsm_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    MTLPixelFormat f = r->mtl ? r->mtl.pixelFormat : MTLPixelFormatInvalid;
    bool col = r->mtl && !rsm_fmt_depth_stencil(f);
    switch (pname) {
    case GL_RENDERBUFFER_WIDTH: *params = r->w; break;
    case GL_RENDERBUFFER_HEIGHT: *params = r->h; break;
    case GL_RENDERBUFFER_INTERNAL_FORMAT: *params = r->internal ? (GLint)r->internal : GL_RGBA; break;
    case GL_RENDERBUFFER_RED_SIZE: case GL_RENDERBUFFER_GREEN_SIZE: case GL_RENDERBUFFER_BLUE_SIZE:
    case GL_RENDERBUFFER_ALPHA_SIZE:
        *params = col ? 8 : 0; break;
    case GL_RENDERBUFFER_DEPTH_SIZE:
        *params = f == MTLPixelFormatDepth32Float || f == MTLPixelFormatDepth32Float_Stencil8 ? 32 : 0; break;
    case GL_RENDERBUFFER_STENCIL_SIZE:
        *params = f == MTLPixelFormatStencil8 || f == MTLPixelFormatDepth32Float_Stencil8 ? 8 : 0; break;
    case GL_RENDERBUFFER_SAMPLES: *params = 0; break;
    default: rsm_error(ctx, GL_INVALID_ENUM);
    }
}

// --- Framebuffers ---------------------------------------------------------------------
void rsm_fb_changed(RSMContext *ctx) {
    rsm_end_pass(ctx);
    ctx->fb_dirty = true;
}

void rsmtl_glGenFramebuffers(GLsizei n, GLuint *framebuffers) {
    RSM_CTX()
    for (GLsizei i = 0; i < n; i++) {
        framebuffers[i] = rsm_names_alloc(&ctx->framebuffers);
        rsm_names_set(&ctx->framebuffers, framebuffers[i], [RSMFramebuffer new]);
    }
}

void rsmtl_glDeleteFramebuffers(GLsizei n, const GLuint *framebuffers) {
    RSM_CTX()
    for (GLsizei i = 0; i < n; i++) {
        GLuint f = framebuffers[i];
        if (!f || !rsm_get(&ctx->framebuffers, f))
            continue;
        if (ctx->draw_fb == f) {
            rsm_end_pass(ctx);
            ctx->draw_fb = 0;
            ctx->fb_dirty = true;
        }
        if (ctx->read_fb == f)
            ctx->read_fb = 0;
        rsm_names_set(&ctx->framebuffers, f, nil);
    }
}

void rsmtl_glBindFramebuffer(GLenum target, GLuint framebuffer) {
    RSM_CTX()
    if (target != GL_FRAMEBUFFER && target != GL_DRAW_FRAMEBUFFER && target != GL_READ_FRAMEBUFFER) {
        rsm_error(ctx, GL_INVALID_ENUM);
        return;
    }
    if (framebuffer) {
        RSMFramebuffer *fb = rsm_get(&ctx->framebuffers, framebuffer);
        if (!fb) {
            fb = [RSMFramebuffer new];
            rsm_names_set(&ctx->framebuffers, framebuffer, fb);
        }
        fb->bound = true;
    }
    if (target != GL_READ_FRAMEBUFFER && ctx->draw_fb != framebuffer) {
        rsm_end_pass(ctx);
        ctx->draw_fb = framebuffer;
        ctx->fb_dirty = true;
    }
    if (target != GL_DRAW_FRAMEBUFFER)
        ctx->read_fb = framebuffer;
}

GLboolean rsmtl_glIsFramebuffer(GLuint framebuffer) {
    RSM_CTX(GL_FALSE)
    RSMFramebuffer *fb = rsm_get(&ctx->framebuffers, framebuffer);
    return fb && fb->bound;
}

static RSMFramebuffer *rsm_target_fb(RSMContext *ctx, GLenum target, GLuint *name) {
    if (target != GL_FRAMEBUFFER && target != GL_DRAW_FRAMEBUFFER && target != GL_READ_FRAMEBUFFER) {
        rsm_error(ctx, GL_INVALID_ENUM);
        return nil;
    }
    *name = target == GL_READ_FRAMEBUFFER ? ctx->read_fb : ctx->draw_fb;
    RSMFramebuffer *fb = rsm_get(&ctx->framebuffers, *name);
    if (!fb)
        rsm_error(ctx, GL_INVALID_OPERATION);
    return fb;
}

static int rsm_attachment_index(GLenum attachment, int *second) {
    *second = -1;
    switch (attachment) {
    case GL_COLOR_ATTACHMENT0: return RSM_ATT_COLOR0;
    case GL_DEPTH_ATTACHMENT: return RSM_ATT_DEPTH;
    case GL_STENCIL_ATTACHMENT: return RSM_ATT_STENCIL;
    case GL_DEPTH_STENCIL_ATTACHMENT: *second = RSM_ATT_STENCIL; return RSM_ATT_DEPTH;
    }
    if (attachment > GL_COLOR_ATTACHMENT0 && attachment <= GL_COLOR_ATTACHMENT15)
        return -2;  // accepted, never drawn to (no glDrawBuffers)
    return -1;
}

static void rsm_attach(RSMContext *ctx, GLenum target, GLenum attachment, rsm_attachment a) {
    GLuint name;
    RSMFramebuffer *fb = rsm_target_fb(ctx, target, &name);
    if (!fb)
        return;
    int second, i = rsm_attachment_index(attachment, &second);
    if (i == -1) {
        rsm_error(ctx, GL_INVALID_ENUM);
        return;
    }
    if (i == -2)
        return;
    fb->att[i] = a;
    if (second >= 0)
        fb->att[second] = a;
    if (name == ctx->draw_fb)
        rsm_fb_changed(ctx);
}

void rsmtl_glFramebufferTexture2D(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level) {
    RSM_CTX()
    rsm_attachment a = {0};
    if (texture) {
        RSMTexture *t = rsm_get(&ctx->textures, texture);
        if (!t) {
            rsm_error(ctx, GL_INVALID_OPERATION);
            return;
        }
        int face = 0;
        if (textarget >= GL_TEXTURE_CUBE_MAP_POSITIVE_X && textarget <= GL_TEXTURE_CUBE_MAP_NEGATIVE_Z)
            face = (int)(textarget - GL_TEXTURE_CUBE_MAP_POSITIVE_X);
        else if (textarget != GL_TEXTURE_2D) {
            rsm_error(ctx, GL_INVALID_ENUM);
            return;
        }
        if (level < 0 || level > 14) {
            rsm_error(ctx, GL_INVALID_VALUE);
            return;
        }
        a = (rsm_attachment){GL_TEXTURE, texture, level, face, t};
    }
    rsm_attach(ctx, target, attachment, a);
}

void rsmtl_glFramebufferRenderbuffer(GLenum target, GLenum attachment, GLenum renderbuffertarget,
                                     GLuint renderbuffer) {
    RSM_CTX()
    if (renderbuffertarget != GL_RENDERBUFFER) {
        rsm_error(ctx, GL_INVALID_ENUM);
        return;
    }
    rsm_attachment a = {0};
    if (renderbuffer) {
        RSMRenderbuffer *r = rsm_get(&ctx->renderbuffers, renderbuffer);
        if (!r) {
            rsm_error(ctx, GL_INVALID_OPERATION);
            return;
        }
        a = (rsm_attachment){GL_RENDERBUFFER, renderbuffer, 0, 0, r};
    }
    rsm_attach(ctx, target, attachment, a);
}

bool rsm_attachment_texture(RSMContext *ctx, const rsm_attachment *a, id<MTLTexture> *tex, int *level,
                            int *slice) {
    *tex = nil;
    *level = *slice = 0;
    (void)ctx;
    if (a->type == GL_TEXTURE) {
        RSMTexture *t = a->obj;
        if (!t || !t->mtl || a->level < 0 || a->level >= t->levels)
            return false;
        *tex = t->mtl;
        *level = a->level;
        *slice = a->face;
        t->defined |= 1u << a->level;
        return true;
    }
    if (a->type == GL_RENDERBUFFER) {
        RSMRenderbuffer *r = a->obj;
        if (!r || !r->mtl)
            return false;
        *tex = r->mtl;
        return true;
    }
    return false;
}

GLenum rsmtl_glCheckFramebufferStatus(GLenum target) {
    RSM_CTX(0)
    if (target != GL_FRAMEBUFFER && target != GL_DRAW_FRAMEBUFFER && target != GL_READ_FRAMEBUFFER) {
        rsm_error(ctx, GL_INVALID_ENUM);
        return 0;
    }
    GLuint name = target == GL_READ_FRAMEBUFFER ? ctx->read_fb : ctx->draw_fb;
    if (!name)
        return GL_FRAMEBUFFER_COMPLETE;
    RSMFramebuffer *fb = rsm_get(&ctx->framebuffers, name);
    int n = 0;
    for (int i = 0; fb && i < RSM_ATT_COUNT; i++) {
        if (!fb->att[i].type)
            continue;
        id<MTLTexture> t;
        int l, s;
        if (!rsm_attachment_texture(ctx, &fb->att[i], &t, &l, &s))
            return GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
        MTLPixelFormat f = t.pixelFormat;
        bool ds = rsm_fmt_depth_stencil(f);
        if ((i == RSM_ATT_COLOR0 && (ds || rsm_fmt_compressed(f))) ||
            (i == RSM_ATT_DEPTH && f != MTLPixelFormatDepth32Float && f != MTLPixelFormatDepth32Float_Stencil8) ||
            (i == RSM_ATT_STENCIL && f != MTLPixelFormatStencil8 && f != MTLPixelFormatDepth32Float_Stencil8))
            return GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
        n++;
    }
    return n ? GL_FRAMEBUFFER_COMPLETE : GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT;
}

void rsmtl_glGetFramebufferAttachmentParameteriv(GLenum target, GLenum attachment, GLenum pname, GLint *params) {
    RSM_CTX()
    GLuint name = target == GL_READ_FRAMEBUFFER ? ctx->read_fb : ctx->draw_fb;
    if (!name) {
        // Default framebuffer.
        if (pname == GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE)
            *params = GL_FRAMEBUFFER_DEFAULT;
        else
            rsm_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    RSMFramebuffer *fb = rsm_target_fb(ctx, target, &name);
    if (!fb)
        return;
    int second, i = rsm_attachment_index(attachment, &second);
    if (i == -1) {
        rsm_error(ctx, GL_INVALID_ENUM);
        return;
    }
    rsm_attachment a = i >= 0 ? fb->att[i] : (rsm_attachment){0};
    switch (pname) {
    case GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE: *params = a.type ? (GLint)a.type : GL_NONE; break;
    case GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME: *params = (GLint)a.name; break;
    case GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_LEVEL: *params = a.level; break;
    case GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_CUBE_MAP_FACE:
        *params = a.face ? (GLint)(GL_TEXTURE_CUBE_MAP_POSITIVE_X + (GLenum)a.face) : 0; break;
    default:
        if (!a.type)
            rsm_error(ctx, GL_INVALID_ENUM);
        else
            *params = 0;
    }
}

void rsmtl_glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type,
                        GLvoid *pixels) {
    RSM_CTX()
    if (width < 0 || height < 0) {
        rsm_error(ctx, GL_INVALID_VALUE);
        return;
    }
    if (!rsm_pixel_size(format, type)) {
        rsm_error(ctx, GL_INVALID_ENUM);
        return;
    }
    id<MTLTexture> tex;
    int level = 0, slice = 0;
    int ai = format == GL_DEPTH_COMPONENT ? RSM_ATT_DEPTH : format == GL_STENCIL_INDEX ? RSM_ATT_STENCIL
                                                                                       : RSM_ATT_COLOR0;
    if (!ctx->read_fb) {
        tex = ai == RSM_ATT_COLOR0 ? ctx->def_color : ctx->def_ds;
    } else {
        RSMFramebuffer *fb = rsm_get(&ctx->framebuffers, ctx->read_fb);
        if (!fb || !rsm_attachment_texture(ctx, &fb->att[ai], &tex, &level, &slice)) {
            rsm_error(ctx, GL_INVALID_OPERATION);
            return;
        }
    }
    int tw = (int)tex.width >> level, th = (int)tex.height >> level;
    int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
    int x1 = x + width > tw ? tw : x + width, y1 = y + height > th ? th : y + height;
    if (x1 <= x0 || y1 <= y0)
        return;
    pixels = rsm_pack_ptr(ctx, pixels,
                          rsm_image_bytes(format, type, width, height, ctx->pack_row_length, ctx->pack_alignment,
                                          ctx->pack_skip_rows, ctx->pack_skip_pixels));
    if (!pixels)
        return;
    @autoreleasepool {
        MTLPixelFormat f = tex.pixelFormat;
        MTLBlitOption opt = MTLBlitOptionNone;
        if (f == MTLPixelFormatDepth32Float_Stencil8) {
            opt = ai == RSM_ATT_STENCIL ? MTLBlitOptionStencilFromDepthStencil : MTLBlitOptionDepthFromDepthStencil;
            f = ai == RSM_ATT_STENCIL ? MTLPixelFormatStencil8 : MTLPixelFormatDepth32Float;
        }
        size_t bpp;
        void *raw = rsm_read_texture(ctx, tex, level, slice, x0, y0, x1 - x0, y1 - y0, opt, &bpp);
        // Pack into the caller's rectangle at the clipped offset.
        GLint skip_px = ctx->pack_skip_pixels, skip_rows = ctx->pack_skip_rows, rl = ctx->pack_row_length;
        ctx->pack_skip_pixels += x0 - x;
        ctx->pack_skip_rows += y0 - y;
        if (!rl)
            ctx->pack_row_length = width;
        rsm_pack_pixels(ctx, raw, f, RSM_SWZ_NONE, x1 - x0, y1 - y0, format, type, pixels);
        ctx->pack_skip_pixels = skip_px;
        ctx->pack_skip_rows = skip_rows;
        ctx->pack_row_length = rl;
        free(raw);
    }
}
