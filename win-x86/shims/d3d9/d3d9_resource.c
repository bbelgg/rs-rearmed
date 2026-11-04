// Textures, surfaces, vertex / index buffers, declarations, shaders, queries, state blocks and
// the swap chain (d3d9_internal.h).
#include <mach/mach_time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "d3d9_internal.h"

void d3d9_state_copy(d3d9_state *dst, const d3d9_state *src, const d3d9_state_mask *m);
void d3d9_state_release(d3d9_state *s);
void d3d9_dev_state_changed(d3d9_device *d);
uint32_t d3d9_dev_present(d3d9_device *d);
d3d9_surface *d3d9_texture_level(d3d9_texture *t, int face, int level);

static const uint8_t IID_IUnknown[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0xC0, 0, 0, 0, 0, 0, 0, 0x46};
static const uint8_t IID_Resource[16] = {0x5D, 0xC0, 0xEE, 0x05, 0x7D, 0x8F, 0x62, 0x43, 0xB9, 0x99, 0xD1, 0xBA, 0xF3, 0x57, 0xC7, 0x04};
static const uint8_t IID_BaseTexture[16] = {0x7E, 0xA8, 0x0C, 0x58, 0x3C, 0x1D, 0x54, 0x4d, 0x99, 0x1D, 0xB7, 0xD3, 0xE3, 0xC2, 0x98, 0xCE};
static const uint8_t IID_Texture[16] = {0x27, 0x12, 0xC3, 0x85, 0xE5, 0x3D, 0x00, 0x4f, 0x9B, 0x3A, 0xF1, 0x1A, 0xC3, 0x8C, 0x18, 0xB5};
static const uint8_t IID_CubeTexture[16] = {0x81, 0x2F, 0xF3, 0xFF, 0x53, 0xD9, 0x3a, 0x47, 0x92, 0x23, 0x93, 0xD6, 0x52, 0xAB, 0xA9, 0x3F};
static const uint8_t IID_Surface[16] = {0x3A, 0xAF, 0xFB, 0x0C, 0xE5, 0x9F, 0x9a, 0x42, 0x99, 0xB3, 0xA2, 0x79, 0x6A, 0xF8, 0xB8, 0x9B};
static const uint8_t IID_VertexBuffer[16] = {0xB5, 0xB1, 0x4B, 0xB6, 0x70, 0xFD, 0xf6, 0x4d, 0xBF, 0x91, 0x19, 0xD0, 0xA1, 0x24, 0x55, 0xE3};
static const uint8_t IID_IndexBuffer[16] = {0x5E, 0xD6, 0x9D, 0x7C, 0xDF, 0xD3, 0x29, 0x45, 0xAC, 0xEE, 0x78, 0x58, 0x30, 0xAC, 0xDE, 0x35};

// Every resource struct starts with d3d9_obj then the device pointer.
typedef struct {
    d3d9_obj o;
    d3d9_device *dev;
} res;

static res *res_of(uint32_t g) {
    d3d9_obj *o = d3d9_obj_get(g, -1);
    return o && o->kind >= D3D9K_TEXTURE ? (res *)o : 0;
}

// --- shared IUnknown / IDirect3DResource9 methods ------------------------------------------
static int iid_is(uint32_t g, const uint8_t *iid) { return !memcmp(RC32_H(g), iid, 16); }

static uint64_t r_qi(rc_ctx *c, uint32_t self, const uint32_t *a) {
    (void)c;
    res *r = res_of(a[0]);
    if (!r || !a[2]) return D3DERR_INVALIDCALL;
    int ok = iid_is(a[1], IID_IUnknown);
    switch (r->o.kind) {
    case D3D9K_TEXTURE: ok |= iid_is(a[1], IID_Resource) | iid_is(a[1], IID_BaseTexture) | iid_is(a[1], IID_Texture); break;
    case D3D9K_CUBE: ok |= iid_is(a[1], IID_Resource) | iid_is(a[1], IID_BaseTexture) | iid_is(a[1], IID_CubeTexture); break;
    case D3D9K_SURFACE: ok |= iid_is(a[1], IID_Resource) | iid_is(a[1], IID_Surface); break;
    case D3D9K_VB: ok |= iid_is(a[1], IID_Resource) | iid_is(a[1], IID_VertexBuffer); break;
    case D3D9K_IB: ok |= iid_is(a[1], IID_Resource) | iid_is(a[1], IID_IndexBuffer); break;
    default: break;  // IUnknown only for the rest
    }
    if (!ok) {
        d3d9_wr32(a[2], 0);
        return E_NOINTERFACE_;
    }
    d3d9_obj_addref(&r->o);
    d3d9_wr32(a[2], a[0]);
    return D3D_OK;
}
static uint64_t r_addref(rc_ctx *c, uint32_t self, const uint32_t *a) {
    (void)c;
    res *r = res_of(a[0]);
    return r ? d3d9_obj_addref(&r->o) : 0;
}
static uint64_t r_release(rc_ctx *c, uint32_t self, const uint32_t *a) {
    (void)c;
    res *r = res_of(a[0]);
    if (!r) return 0;
    d3d9_device *d = r->dev;
    d3d9_dev_lock(d);  // destroy deletes GL objects
    uint32_t n = d3d9_obj_release(&r->o);
    d3d9_dev_unlock(d);
    return n;
}
static uint64_t r_get_device(rc_ctx *c, uint32_t self, const uint32_t *a) {
    (void)c;
    res *r = res_of(a[0]);
    if (!r || !a[1]) return D3DERR_INVALIDCALL;
    d3d9_obj_addref(&r->dev->o);
    d3d9_wr32(a[1], r->dev->o.guest);
    return D3D_OK;
}
static uint64_t r_set_private(rc_ctx *c, uint32_t self, const uint32_t *a) { (void)c; (void)a; d3d9_log_once("d3d9: SetPrivateData ignored\n"); return D3D_OK; }
static uint64_t r_get_private(rc_ctx *c, uint32_t self, const uint32_t *a) { (void)c; (void)a; return D3DERR_NOTFOUND; }
static uint64_t r_ok(rc_ctx *c, uint32_t self, const uint32_t *a) { (void)c; (void)a; return D3D_OK; }
static uint64_t r_zero(rc_ctx *c, uint32_t self, const uint32_t *a) { (void)c; (void)a; return 0; }
static uint64_t r_type(rc_ctx *c, uint32_t self, const uint32_t *a) {
    (void)c;
    res *r = res_of(a[0]);
    if (!r) return 0;
    switch (r->o.kind) {
    case D3D9K_SURFACE: return RTYPE_SURFACE;
    case D3D9K_TEXTURE: return RTYPE_TEXTURE;
    case D3D9K_CUBE: return RTYPE_CUBETEXTURE;
    case D3D9K_VB: return RTYPE_VERTEXBUFFER;
    case D3D9K_IB: return RTYPE_INDEXBUFFER;
    }
    return 0;
}

#define RESOURCE_SLOTS(I)                                     \
    {I "::QueryInterface", 3, 0, r_qi},                          \
    {I "::AddRef", 1, 0, r_addref},                              \
    {I "::Release", 1, 0, r_release},                            \
    {I "::GetDevice", 2, 0, r_get_device},                       \
    {I "::SetPrivateData", 5, 0, r_set_private},                 \
    {I "::GetPrivateData", 4, 0, r_get_private},                 \
    {I "::FreePrivateData", 2, 0, r_ok},                         \
    {I "::SetPriority", 2, 0, r_zero},                           \
    {I "::GetPriority", 1, 0, r_zero},                           \
    {I "::PreLoad", 1, 0, r_ok},                                 \
    {I "::GetType", 1, 0, r_type}

// --- formats --------------------------------------------------------------------------------
void d3d9_tex_format(uint32_t fmt, GLint *internal, GLenum *format, GLenum *type, int *convert) {
    *convert = 0;
    *type = GL_UNSIGNED_BYTE;
    switch (fmt) {
    case FMT_A8R8G8B8: *internal = GL_RGBA8; *format = GL_BGRA; return;
    case FMT_X8R8G8B8: *internal = GL_RGBA8; *format = GL_BGRA; *convert = 1; return;
    case FMT_A8B8G8R8: *internal = GL_RGBA8; *format = GL_RGBA; return;
    case FMT_X8B8G8R8: *internal = GL_RGBA8; *format = GL_RGBA; *convert = 1; return;
    case FMT_R5G6B5: case FMT_X1R5G5B5: case FMT_A1R5G5B5: case FMT_A4R4G4B4: case FMT_X4R4G4B4:
        *internal = GL_RGBA8; *format = GL_RGBA; *convert = 1; return;
    case FMT_A8: *internal = GL_ALPHA; *format = GL_ALPHA; return;
    case FMT_L8: *internal = GL_LUMINANCE; *format = GL_LUMINANCE; return;
    case FMT_A8L8: *internal = GL_LUMINANCE_ALPHA; *format = GL_LUMINANCE_ALPHA; return;
    case FMT_A16B16G16R16F: *internal = GL_RGBA16F; *format = GL_RGBA; *type = GL_HALF_FLOAT; return;
    case FMT_R32F: *internal = GL_R32F; *format = GL_RED; *type = GL_FLOAT; return;
    case FMT_DXT1: *internal = GL_COMPRESSED_RGBA_S3TC_DXT1_EXT; *format = 0; return;
    case FMT_DXT2: case FMT_DXT3: *internal = GL_COMPRESSED_RGBA_S3TC_DXT3_EXT; *format = 0; return;
    case FMT_DXT4: case FMT_DXT5: *internal = GL_COMPRESSED_RGBA_S3TC_DXT5_EXT; *format = 0; return;
    }
    *internal = 0;
    *format = 0;
}

uint32_t d3d9_pitch(uint32_t fmt, uint32_t w) {
    if (d3d9_format_is_block(fmt)) return ((w + 3) / 4) * (fmt == FMT_DXT1 ? 8 : 16);
    uint32_t bpp = (uint32_t)d3d9_format_bpp(fmt);
    if (!bpp) bpp = 32;
    return (w * bpp / 8 + 3) & ~3u;
}
uint32_t d3d9_level_size(uint32_t fmt, uint32_t w, uint32_t h) {
    uint32_t rows = d3d9_format_is_block(fmt) ? (h + 3) / 4 : h;
    return d3d9_pitch(fmt, w) * (rows ? rows : 1);
}

static uint8_t expand(uint32_t v, int bits) { return (uint8_t)(bits ? (v * 255 + ((1u << bits) - 1) / 2) / ((1u << bits) - 1) : 255); }

void *d3d9_convert_rows(uint32_t fmt, const uint8_t *src, uint32_t pitch, uint32_t w, uint32_t h) {
    uint8_t *out = malloc((size_t)w * h * 4 + 4);
    for (uint32_t y = 0; y < h; y++) {
        const uint8_t *s = src + (size_t)y * pitch;
        uint8_t *o = out + (size_t)y * w * 4;
        for (uint32_t x = 0; x < w; x++, o += 4) {
            uint16_t p;
            switch (fmt) {
            case FMT_X8R8G8B8: case FMT_X8B8G8R8:
                memcpy(o, s + 4 * x, 3);
                o[3] = 255;
                break;
            case FMT_R5G6B5:
                memcpy(&p, s + 2 * x, 2);
                o[0] = expand(p >> 11, 5); o[1] = expand((p >> 5) & 63, 6); o[2] = expand(p & 31, 5); o[3] = 255;
                break;
            case FMT_X1R5G5B5: case FMT_A1R5G5B5:
                memcpy(&p, s + 2 * x, 2);
                o[0] = expand((p >> 10) & 31, 5); o[1] = expand((p >> 5) & 31, 5); o[2] = expand(p & 31, 5);
                o[3] = fmt == FMT_X1R5G5B5 || (p >> 15) ? 255 : 0;
                break;
            case FMT_A4R4G4B4: case FMT_X4R4G4B4:
                memcpy(&p, s + 2 * x, 2);
                o[0] = expand((p >> 8) & 15, 4); o[1] = expand((p >> 4) & 15, 4); o[2] = expand(p & 15, 4);
                o[3] = fmt == FMT_X4R4G4B4 ? 255 : expand(p >> 12, 4);
                break;
            }
        }
    }
    return out;
}

// --- surfaces --------------------------------------------------------------------------------
uint32_t d3d9_surface_ensure_mem(d3d9_surface *s) {
    if (!s->mem) {
        s->pitch = d3d9_pitch(s->format, s->width);
        s->mem_size = d3d9_level_size(s->format, s->width, s->height);
        s->mem = w32_galloc(s->mem_size);
    }
    return s->mem;
}

static GLenum surf_target(d3d9_surface *s) {
    return s->container && s->container->cube ? GL_TEXTURE_CUBE_MAP_POSITIVE_X + (GLenum)s->face : GL_TEXTURE_2D;
}

// Upload rows (D3D layout of s->format) into the GL texture behind s.
static void upload_rows(d3d9_surface *s, const uint8_t *src, uint32_t pitch) {
    GLuint tex = s->container ? s->container->tex : s->tex;
    if (!tex) return;
    GLint internal;
    GLenum format, type;
    int convert;
    d3d9_tex_format(s->format, &internal, &format, &type, &convert);
    if (!internal) {
        d3d9_log_once("d3d9: texture format 0x%x not supported (upload skipped)\n", s->format);
        return;
    }
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(s->container && s->container->cube ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D, tex);
    GLenum target = surf_target(s);
    if (!format) {
        glCompressedTexImage2D(target, s->level, (GLenum)internal, (GLsizei)s->width, (GLsizei)s->height, 0,
                               (GLsizei)d3d9_level_size(s->format, s->width, s->height), src);
        return;
    }
    void *tmp = 0;
    if (convert) {
        tmp = d3d9_convert_rows(s->format, src, pitch, s->width, s->height);
        src = tmp;
    }
    glTexSubImage2D(target, s->level, 0, 0, (GLsizei)s->width, (GLsizei)s->height, format, type, src);
    free(tmp);
    if (s->container && (s->container->usage & USAGE_AUTOGENMIPMAP)) s->container->dirty_mips = 1;
}

void d3d9_surface_upload_mem(d3d9_surface *s) {
    if (s->mem) upload_rows(s, RC32_H(s->mem), s->pitch);
}

// GPU -> rows in the surface's D3D format (8-bit RGBA family only).
void d3d9_surface_read(d3d9_surface *s, uint8_t *dst, uint32_t pitch) {
    d3d9_device *d = s->dev;
    if (!(s->format == FMT_A8R8G8B8 || s->format == FMT_X8R8G8B8 || s->format == FMT_A8B8G8R8 || s->format == FMT_X8B8G8R8)) {
        d3d9_log_once("d3d9: reading back format 0x%x is not supported\n", s->format);
        return;
    }
    d3d9_dev_bind_target(d, s, 0);
    uint8_t *tmp = malloc((size_t)s->width * s->height * 4);
    glReadPixels(0, 0, (GLsizei)s->width, (GLsizei)s->height, GL_RGBA, GL_UNSIGNED_BYTE, tmp);
    int bgr = s->format == FMT_A8R8G8B8 || s->format == FMT_X8R8G8B8;
    for (uint32_t y = 0; y < s->height; y++) {  // GL row y is D3D row y (d3d9_internal.h)
        const uint8_t *i = tmp + (size_t)y * s->width * 4;
        uint8_t *o = dst + (size_t)y * pitch;
        for (uint32_t x = 0; x < s->width; x++, i += 4, o += 4) {
            o[0] = bgr ? i[2] : i[0];
            o[1] = i[1];
            o[2] = bgr ? i[0] : i[2];
            o[3] = i[3];
        }
    }
    free(tmp);
    d->fbo_dirty = 1;
}

static d3d9_surface *surf_of(uint32_t g) { return (d3d9_surface *)d3d9_obj_get(g, D3D9K_SURFACE); }

static uint64_t s_get_container(rc_ctx *c, uint32_t self, const uint32_t *a) {
    (void)c;
    d3d9_surface *s = surf_of(a[0]);
    if (!s || !a[2]) return D3DERR_INVALIDCALL;
    if (s->container) {
        uint32_t qa[3] = {s->container->o.guest, a[1], a[2]};
        return r_qi(c, 0, qa);
    }
    // a standalone surface's container is the device
    d3d9_obj_addref(&s->dev->o);
    d3d9_wr32(a[2], s->dev->o.guest);
    return D3D_OK;
}

static void surface_desc(d3d9_surface *s, uint32_t g) {
    uint32_t desc[8] = {s->format, RTYPE_SURFACE, s->usage, s->pool, s->ms, 0, s->width, s->height};
    memcpy(RC32_H(g), desc, 32);
}

static uint64_t s_get_desc(rc_ctx *c, uint32_t self, const uint32_t *a) {
    (void)c;
    d3d9_surface *s = surf_of(a[0]);
    if (!s || !a[1]) return D3DERR_INVALIDCALL;
    surface_desc(s, a[1]);
    return D3D_OK;
}

static uint32_t surface_lock(d3d9_surface *s, uint32_t locked, uint32_t rect, uint32_t flags) {
    if (!locked) return D3DERR_INVALIDCALL;
    if (s->rb) {
        d3d9_log_once("d3d9: locking a depth stencil surface is not supported\n");
        return D3DERR_INVALIDCALL;
    }
    if (s->locked) return D3DERR_INVALIDCALL;
    d3d9_dev_lock(s->dev);
    uint32_t m = d3d9_surface_ensure_mem(s);
    if (!m) {
        d3d9_dev_unlock(s->dev);
        return E_OUTOFMEMORY_;
    }
    // a render target's contents live on the GPU
    if (s->tex && !s->container && !(flags & LOCK_DISCARD)) d3d9_surface_read(s, RC32_H(m), s->pitch);
    uint32_t r[4] = {0, 0, s->width, s->height};
    if (rect) memcpy(r, RC32_H(rect), 16);
    memcpy(s->lock_rect, r, 16);
    uint32_t off;
    if (d3d9_format_is_block(s->format)) off = r[1] / 4 * s->pitch + r[0] / 4 * (s->format == FMT_DXT1 ? 8 : 16);
    else off = r[1] * s->pitch + r[0] * (uint32_t)d3d9_format_bpp(s->format) / 8;
    d3d9_wr32(locked, s->pitch);
    d3d9_wr32(locked + 4, m + off);
    s->locked = 1;
    s->lock_ro = (flags & LOCK_READONLY) != 0;
    d3d9_dev_unlock(s->dev);
    return D3D_OK;
}

static uint32_t surface_unlock(d3d9_surface *s) {
    if (!s->locked) return D3DERR_INVALIDCALL;
    s->locked = 0;
    if (s->lock_ro) return D3D_OK;
    if (s->tex || s->container) {
        d3d9_dev_lock(s->dev);
        d3d9_surface_upload_mem(s);
        d3d9_dev_unlock(s->dev);
    }
    return D3D_OK;
}

static uint64_t s_lock(rc_ctx *c, uint32_t self, const uint32_t *a) {
    (void)c;
    d3d9_surface *s = surf_of(a[0]);
    return s ? surface_lock(s, a[1], a[2], a[3]) : D3DERR_INVALIDCALL;
}
static uint64_t s_unlock(rc_ctx *c, uint32_t self, const uint32_t *a) {
    (void)c;
    d3d9_surface *s = surf_of(a[0]);
    return s ? surface_unlock(s) : D3DERR_INVALIDCALL;
}

static const d3d9_slot surface_slots[] = {
    RESOURCE_SLOTS("IDirect3DSurface9"),
    {"IDirect3DSurface9::GetContainer", 3, 0, s_get_container},
    {"IDirect3DSurface9::GetDesc", 2, 0, s_get_desc},
    {"IDirect3DSurface9::LockRect", 4, 0, s_lock},
    {"IDirect3DSurface9::UnlockRect", 1, 0, s_unlock},
    {"IDirect3DSurface9::GetDC", 2, 0},
    {"IDirect3DSurface9::ReleaseDC", 2, 0},
};

static void surface_free(d3d9_surface *s) {
    if (s->tex) glDeleteTextures(1, &s->tex);
    if (s->rb) glDeleteRenderbuffers(1, &s->rb);
    if (s->mem) w32_gfree(s->mem, s->mem_size);
}
static void surface_destroy(d3d9_obj *o) {
    surface_free((d3d9_surface *)o);
    free(o);
}

d3d9_surface *d3d9_surface_new(d3d9_device *d, uint32_t w, uint32_t h, uint32_t fmt, uint32_t usage, uint32_t pool,
                               uint32_t ms) {
    if (!w || !h) return 0;
    d3d9_surface *s = calloc(1, sizeof *s);
    s->o.kind = D3D9K_SURFACE;
    s->o.destroy = surface_destroy;
    s->dev = d;
    s->format = fmt;
    s->usage = usage;
    s->pool = pool;
    s->width = w;
    s->height = h;
    s->ms = ms;
    if (usage & USAGE_DEPTHSTENCIL) {
        glGenRenderbuffers(1, &s->rb);
        glBindRenderbuffer(GL_RENDERBUFFER, s->rb);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, (GLsizei)w, (GLsizei)h);
    } else if (usage & USAGE_RENDERTARGET) {
        GLint internal;
        GLenum format, type;
        int convert;
        d3d9_tex_format(fmt, &internal, &format, &type, &convert);
        if (!internal || !format) {
            d3d9_log_once("d3d9: render target format 0x%x not supported (RGBA8 used)\n", fmt);
            internal = GL_RGBA8, format = GL_RGBA, type = GL_UNSIGNED_BYTE;
        }
        glGenTextures(1, &s->tex);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, s->tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
        glTexImage2D(GL_TEXTURE_2D, 0, internal, (GLsizei)w, (GLsizei)h, 0, format, type, 0);
    }
    uint32_t vt = d3d9_vtable(surface_slots, (int)(sizeof surface_slots / sizeof surface_slots[0]));
    if (!d3d9_obj_publish(&s->o, vt)) {
        surface_free(s);
        free(s);
        return 0;
    }
    return s;
}

// --- textures --------------------------------------------------------------------------------
static d3d9_texture *tex_of(uint32_t g) {
    d3d9_obj *o = d3d9_obj_get(g, -1);
    return o && (o->kind == D3D9K_TEXTURE || o->kind == D3D9K_CUBE) ? (d3d9_texture *)o : 0;
}

d3d9_surface *d3d9_texture_level(d3d9_texture *t, int face, int level) {
    if (face < 0 || face >= (t->cube ? 6 : 1) || level < 0 || (uint32_t)level >= t->levels) return 0;
    d3d9_surface *s = t->surf[face][level];
    if (s) return s;
    s = calloc(1, sizeof *s);
    s->o.kind = D3D9K_SURFACE;
    s->dev = t->dev;
    s->format = t->format;
    s->usage = t->usage;
    s->pool = t->pool;
    s->width = t->width >> level ? t->width >> level : 1;
    s->height = t->height >> level ? t->height >> level : 1;
    s->container = t;
    s->level = level;
    s->face = face;
    uint32_t vt = d3d9_vtable(surface_slots, (int)(sizeof surface_slots / sizeof surface_slots[0]));
    if (!d3d9_obj_publish(&s->o, vt)) {
        free(s);
        return 0;
    }
    s->o.ref = 0;
    s->o.fwd = &t->o;  // a level shares its texture's reference count
    t->surf[face][level] = s;
    return s;
}

void d3d9_texture_apply_sampler(d3d9_texture *t, const uint32_t *ss) {
    GLenum tg = t->target;
    if (t->dirty_mips) {
        glGenerateMipmap(tg);
        t->dirty_mips = 0;
    }
    static const GLint wrap[] = {GL_REPEAT, GL_REPEAT, GL_MIRRORED_REPEAT, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_BORDER, GL_MIRRORED_REPEAT};
    GLint ws = wrap[ss[SS_ADDRESSU] <= 5 ? ss[SS_ADDRESSU] : 1], wt = wrap[ss[SS_ADDRESSV] <= 5 ? ss[SS_ADDRESSV] : 1];
    uint32_t mag = ss[SS_MAGFILTER], min = ss[SS_MINFILTER], mip = ss[SS_MIPFILTER];
    int has_mips = t->levels > 1 || (t->usage & USAGE_AUTOGENMIPMAP);
    GLint gmag = mag == 1 ? GL_NEAREST : GL_LINEAR;
    int lin = min != 1;
    GLint gmin;
    if (!has_mips || mip == 0) gmin = lin ? GL_LINEAR : GL_NEAREST;
    else if (mip == 1) gmin = lin ? GL_LINEAR_MIPMAP_NEAREST : GL_NEAREST_MIPMAP_NEAREST;
    else gmin = lin ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_LINEAR;
    float aniso = (min == 3 || mag == 3) ? (float)(ss[SS_MAXANISOTROPY] ? ss[SS_MAXANISOTROPY] : 1) : 1.0f;
    float bias = d3d9_f32(ss[SS_MIPMAPLODBIAS]);
    if (ws != t->s_wrap_s) { glTexParameteri(tg, GL_TEXTURE_WRAP_S, ws); t->s_wrap_s = ws; }
    if (wt != t->s_wrap_t) { glTexParameteri(tg, GL_TEXTURE_WRAP_T, wt); t->s_wrap_t = wt; }
    if (gmin != t->s_min) { glTexParameteri(tg, GL_TEXTURE_MIN_FILTER, gmin); t->s_min = gmin; }
    if (gmag != t->s_mag) { glTexParameteri(tg, GL_TEXTURE_MAG_FILTER, gmag); t->s_mag = gmag; }
    if (aniso != t->s_aniso) { glTexParameterf(tg, GL_TEXTURE_MAX_ANISOTROPY_EXT, aniso); t->s_aniso = aniso; }
    if (bias != t->s_bias) { glTexParameterf(tg, GL_TEXTURE_LOD_BIAS, bias); t->s_bias = bias; }
    if (ss[SS_MAXMIPLEVEL]) d3d9_log_once("d3d9: D3DSAMP_MAXMIPLEVEL is not supported\n");
}

static uint64_t t_get_lod(rc_ctx *c, uint32_t self, const uint32_t *a) { (void)c; d3d9_texture *t = tex_of(a[0]); return t ? t->lod : 0; }
static uint64_t t_set_lod(rc_ctx *c, uint32_t self, const uint32_t *a) {
    (void)c;
    d3d9_texture *t = tex_of(a[0]);
    if (!t) return 0;
    uint32_t old = t->lod;
    t->lod = a[1];
    if (a[1]) d3d9_log_once("d3d9: SetLOD is not supported\n");
    return old;
}
static uint64_t t_level_count(rc_ctx *c, uint32_t self, const uint32_t *a) {
    (void)c;
    d3d9_texture *t = tex_of(a[0]);
    return t ? (t->usage & USAGE_AUTOGENMIPMAP ? 1 : t->levels) : 0;
}
static uint64_t t_get_filter(rc_ctx *c, uint32_t self, const uint32_t *a) { (void)c; (void)a; return 2; }  // D3DTEXF_LINEAR
static uint64_t t_gen_mips(rc_ctx *c, uint32_t self, const uint32_t *a) {
    (void)c;
    d3d9_texture *t = tex_of(a[0]);
    if (t && (t->usage & USAGE_AUTOGENMIPMAP)) t->dirty_mips = 1;
    return D3D_OK;
}
static uint64_t t_level_desc(rc_ctx *c, uint32_t self, const uint32_t *a) {
    (void)c;
    d3d9_texture *t = tex_of(a[0]);
    d3d9_surface *s = t ? d3d9_texture_level(t, 0, (int)a[1]) : 0;
    if (!s || !a[2]) return D3DERR_INVALIDCALL;
    surface_desc(s, a[2]);
    return D3D_OK;
}
static uint64_t t_get_surface(rc_ctx *c, uint32_t self, const uint32_t *a) {
    (void)c;
    // texture: Level, ppSurfaceLevel; cube: FaceType, Level, ppCubeMapSurface
    d3d9_texture *t = tex_of(a[0]);
    if (!t) return D3DERR_INVALIDCALL;
    int face = t->cube ? (int)a[1] : 0, level = (int)a[t->cube ? 2 : 1];
    uint32_t out = a[t->cube ? 3 : 2];
    d3d9_surface *s = d3d9_texture_level(t, face, level);
    if (!s || !out) return D3DERR_INVALIDCALL;
    d3d9_obj_addref(&s->o);
    d3d9_wr32(out, s->o.guest);
    return D3D_OK;
}
static uint64_t t_lock(rc_ctx *c, uint32_t self, const uint32_t *a) {
    (void)c;
    // texture: Level, pLockedRect, pRect, Flags; cube: FaceType, Level, pLockedRect, pRect, Flags
    d3d9_texture *t = tex_of(a[0]);
    if (!t) return D3DERR_INVALIDCALL;
    int k = t->cube ? 2 : 1;
    d3d9_surface *s = d3d9_texture_level(t, t->cube ? (int)a[1] : 0, (int)a[k]);
    return s ? surface_lock(s, a[k + 1], a[k + 2], a[k + 3]) : D3DERR_INVALIDCALL;
}
static uint64_t t_unlock(rc_ctx *c, uint32_t self, const uint32_t *a) {
    (void)c;
    d3d9_texture *t = tex_of(a[0]);
    if (!t) return D3DERR_INVALIDCALL;
    d3d9_surface *s = d3d9_texture_level(t, t->cube ? (int)a[1] : 0, (int)a[t->cube ? 2 : 1]);
    return s ? surface_unlock(s) : D3DERR_INVALIDCALL;
}

#define TEXTURE_SLOTS(I)                                       \
    RESOURCE_SLOTS(I),                                         \
    {I "::SetLOD", 2, 0, t_set_lod},                              \
    {I "::GetLOD", 1, 0, t_get_lod},                              \
    {I "::GetLevelCount", 1, 0, t_level_count},                   \
    {I "::SetAutoGenFilterType", 2, 0, r_ok},                     \
    {I "::GetAutoGenFilterType", 1, 0, t_get_filter},             \
    {I "::GenerateMipSubLevels", 1, 0, t_gen_mips},               \
    {I "::GetLevelDesc", 3, 0, t_level_desc}

static const d3d9_slot texture_slots[] = {
    TEXTURE_SLOTS("IDirect3DTexture9"),
    {"IDirect3DTexture9::GetSurfaceLevel", 3, 0, t_get_surface},
    {"IDirect3DTexture9::LockRect", 5, 0, t_lock},
    {"IDirect3DTexture9::UnlockRect", 2, 0, t_unlock},
    {"IDirect3DTexture9::AddDirtyRect", 2, 0, r_ok},
};
static const d3d9_slot cube_slots[] = {
    TEXTURE_SLOTS("IDirect3DCubeTexture9"),
    {"IDirect3DCubeTexture9::GetCubeMapSurface", 4, 0, t_get_surface},
    {"IDirect3DCubeTexture9::LockRect", 6, 0, t_lock},
    {"IDirect3DCubeTexture9::UnlockRect", 3, 0, t_unlock},
    {"IDirect3DCubeTexture9::AddDirtyRect", 3, 0, r_ok},
};

static void texture_destroy(d3d9_obj *o) {
    d3d9_texture *t = (d3d9_texture *)o;
    for (int f = 0; f < 6; f++)
        for (int l = 0; l < 16; l++) {
            d3d9_surface *s = t->surf[f][l];
            if (!s) continue;
            d3d9_obj_kill(&s->o);
            if (s->mem) w32_gfree(s->mem, s->mem_size);
            free(s);
        }
    if (t->tex) glDeleteTextures(1, &t->tex);
    free(t);
}

uint32_t d3d9_create_texture(d3d9_device *d, const uint32_t *a, int cube, uint32_t *unused) {
    (void)unused;
    // texture: Width, Height, Levels, Usage, Format, Pool, ppTexture, pSharedHandle
    // cube:    EdgeLength, Levels, Usage, Format, Pool, ppCubeTexture, pSharedHandle
    uint32_t w, h, levels, usage, fmt, pool, out;
    if (cube) { w = h = a[1]; levels = a[2]; usage = a[3]; fmt = a[4]; pool = a[5]; out = a[6]; }
    else { w = a[1]; h = a[2]; levels = a[3]; usage = a[4]; fmt = a[5]; pool = a[6]; out = a[7]; }
    if (!out || !w || !h) return D3DERR_INVALIDCALL;
    d3d9_wr32(out, 0);
    GLint internal;
    GLenum format, type;
    int convert;
    d3d9_tex_format(fmt, &internal, &format, &type, &convert);
    if (!internal) {
        fprintf(stderr, "d3d9: CreateTexture: format 0x%x not supported\n", fmt);
        return D3DERR_INVALIDCALL;
    }
    uint32_t full = 1;
    while ((w >> full) || (h >> full)) full++;
    if (!levels || levels > full) levels = full;
    if (usage & USAGE_AUTOGENMIPMAP) levels = full;
    if (levels > 16) levels = 16;
    d3d9_texture *t = calloc(1, sizeof *t);
    t->o.kind = cube ? D3D9K_CUBE : D3D9K_TEXTURE;
    t->o.destroy = texture_destroy;
    t->dev = d;
    t->cube = cube;
    t->format = fmt;
    t->usage = usage;
    t->pool = pool;
    t->width = w;
    t->height = h;
    t->levels = levels;
    t->target = cube ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D;
    t->s_wrap_s = t->s_wrap_t = t->s_min = t->s_mag = -1;
    t->s_aniso = t->s_bias = -1;
    glGenTextures(1, &t->tex);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(t->target, t->tex);
    glTexParameteri(t->target, GL_TEXTURE_MAX_LEVEL, (GLint)levels - 1);
    for (int f = 0; f < (cube ? 6 : 1); f++) {
        GLenum tg = cube ? GL_TEXTURE_CUBE_MAP_POSITIVE_X + (GLenum)f : GL_TEXTURE_2D;
        for (uint32_t l = 0; l < levels; l++) {
            uint32_t lw = w >> l ? w >> l : 1, lh = h >> l ? h >> l : 1;
            if (format) {
                glTexImage2D(tg, (GLint)l, internal, (GLsizei)lw, (GLsizei)lh, 0, format, type, 0);
            } else {
                uint32_t n = d3d9_level_size(fmt, lw, lh);
                void *z = calloc(1, n);
                glCompressedTexImage2D(tg, (GLint)l, (GLenum)internal, (GLsizei)lw, (GLsizei)lh, 0, (GLsizei)n, z);
                free(z);
            }
        }
    }
    const d3d9_slot *slots = cube ? cube_slots : texture_slots;
    int n = cube ? (int)(sizeof cube_slots / sizeof cube_slots[0]) : (int)(sizeof texture_slots / sizeof texture_slots[0]);
    if (!d3d9_obj_publish(&t->o, d3d9_vtable(slots, n))) {
        glDeleteTextures(1, &t->tex);
        free(t);
        return E_OUTOFMEMORY_;
    }
    d3d9_wr32(out, t->o.guest);
    return D3D_OK;
}

// --- vertex and index buffers ----------------------------------------------------------------
static d3d9_buffer *buf_of(uint32_t g) {
    d3d9_obj *o = d3d9_obj_get(g, -1);
    return o && (o->kind == D3D9K_VB || o->kind == D3D9K_IB) ? (d3d9_buffer *)o : 0;
}

static uint64_t b_lock(rc_ctx *c, uint32_t self, const uint32_t *a) {
    (void)c;
    // OffsetToLock, SizeToLock, ppbData, Flags
    d3d9_buffer *b = buf_of(a[0]);
    if (!b || !a[3] || a[1] > b->size) return D3DERR_INVALIDCALL;
    uint32_t off = a[1], size = a[2] ? a[2] : b->size - off;
    if (size > b->size - off) size = b->size - off;
    d3d9_dev_lock(b->dev);
    if (!b->locks || off < b->dirty_lo) b->dirty_lo = off;
    if (!b->locks || off + size > b->dirty_hi) b->dirty_hi = off + size;
    if (a[4] & LOCK_READONLY) {
        if (!b->locks) b->dirty_lo = b->dirty_hi = 0;
    }
    if ((a[4] & LOCK_DISCARD) && (b->usage & USAGE_DYNAMIC)) b->discard = 1;
    b->locks++;
    d3d9_dev_unlock(b->dev);
    d3d9_wr32(a[3], b->mem + off);
    return D3D_OK;
}

// Dynamic buffers upload at the draw. The game locks its dynamic vertex buffer (1 MB) whole
// (offset 0, size 0) about 2,900 times a second in a song, with NOOVERWRITE, and appends a few
// vertices each time: uploading the locked range at each Unlock copied 2.7 GB/s, a fifth of the
// game thread's time. Unlock now only notes the range as pending; a draw uploads the part of it
// that it reads (d3d9_buffer_sync: the vertex range of the draw, from the index data for an
// indexed one). The guest copy is always complete, so what a draw sees is what the buffer held
// when it was issued, as in D3D9. LP_D3D9_EAGER=1 uploads at Unlock, as before.
static int lazy_on(void) {
    static int on = -1;
    if (on < 0) {
        const char *e = getenv("LP_D3D9_EAGER");
        on = !(e && *e == '1');
    }
    return on;
}

void d3d9_buffer_sync(d3d9_buffer *b, uint64_t lo, uint64_t hi) {
    if (hi > b->pend_hi) hi = b->pend_hi;
    if (lo < b->pend_lo) lo = b->pend_lo;
    if (lo >= hi) return;
    for (int k = 0; k < b->nclean && k < 8; k++)
        if (lo >= b->clean_lo[k] && hi <= b->clean_hi[k]) return;
    GLenum tg = b->is_index ? GL_ELEMENT_ARRAY_BUFFER : GL_ARRAY_BUFFER;
    glBindBuffer(tg, b->buf);
    glBufferSubData(tg, (GLintptr)lo, (GLsizeiptr)(hi - lo), RC32_H(b->mem + (uint32_t)lo));
    if (lo == b->pend_lo && hi == b->pend_hi) b->pend_lo = b->pend_hi = 0;
    else if (lo == b->pend_lo) b->pend_lo = (uint32_t)hi;
    else if (hi == b->pend_hi) b->pend_hi = (uint32_t)lo;
    else {  // from the middle: pending stays, the range is clean until the next lock
        int k = b->nclean++ & 7;
        b->clean_lo[k] = (uint32_t)lo;
        b->clean_hi[k] = (uint32_t)hi;
    }
}

static uint64_t b_unlock(rc_ctx *c, uint32_t self, const uint32_t *a) {
    (void)c;
    d3d9_buffer *b = buf_of(a[0]);
    if (!b) return D3DERR_INVALIDCALL;
    d3d9_dev_lock(b->dev);
    if (b->locks > 0 && --b->locks == 0 && b->dirty_hi > b->dirty_lo && (b->usage & USAGE_DYNAMIC) && lazy_on()) {
        if (b->discard) {  // new storage; the old contents are undefined
            GLenum tg = b->is_index ? GL_ELEMENT_ARRAY_BUFFER : GL_ARRAY_BUFFER;
            glBindBuffer(tg, b->buf);
            glBufferData(tg, (GLsizeiptr)b->size, 0, GL_DYNAMIC_DRAW);
            b->pend_lo = 0;
            b->pend_hi = b->size;
        } else if (b->pend_hi > b->pend_lo) {
            if (b->dirty_lo < b->pend_lo) b->pend_lo = b->dirty_lo;
            if (b->dirty_hi > b->pend_hi) b->pend_hi = b->dirty_hi;
        } else {
            b->pend_lo = b->dirty_lo;
            b->pend_hi = b->dirty_hi;
        }
        b->nclean = 0;
        b->dirty_lo = b->dirty_hi = 0;
        b->discard = 0;
    } else if (b->locks == 0 && b->dirty_hi > b->dirty_lo) {
        GLenum tg = b->is_index ? GL_ELEMENT_ARRAY_BUFFER : GL_ARRAY_BUFFER;
        glBindBuffer(tg, b->buf);
        // DISCARD: the rest of the buffer is undefined, so the GL buffer may take new storage
        // without copying the old (a sub-range write into storage the GPU still reads copies the
        // whole buffer first)
        if (b->discard) glBufferData(tg, (GLsizeiptr)b->size, 0, GL_DYNAMIC_DRAW);
        glBufferSubData(tg, (GLintptr)b->dirty_lo, (GLsizeiptr)(b->dirty_hi - b->dirty_lo), RC32_H(b->mem + b->dirty_lo));
        b->dirty_lo = b->dirty_hi = 0;
        b->discard = 0;
    }
    d3d9_dev_unlock(b->dev);
    return D3D_OK;
}

static uint64_t b_desc(rc_ctx *c, uint32_t self, const uint32_t *a) {
    (void)c;
    d3d9_buffer *b = buf_of(a[0]);
    if (!b || !a[1]) return D3DERR_INVALIDCALL;
    if (b->is_index) {
        uint32_t desc[5] = {b->format, RTYPE_INDEXBUFFER, b->usage, b->pool, b->size};
        memcpy(RC32_H(a[1]), desc, 20);
    } else {
        uint32_t desc[6] = {0x64 /* D3DFMT_VERTEXDATA */, RTYPE_VERTEXBUFFER, b->usage, b->pool, b->size, b->fvf};
        memcpy(RC32_H(a[1]), desc, 24);
    }
    return D3D_OK;
}

static const d3d9_slot vb_slots[] = {
    RESOURCE_SLOTS("IDirect3DVertexBuffer9"),
    {"IDirect3DVertexBuffer9::Lock", 5, 0, b_lock},
    {"IDirect3DVertexBuffer9::Unlock", 1, 0, b_unlock},
    {"IDirect3DVertexBuffer9::GetDesc", 2, 0, b_desc},
};
static const d3d9_slot ib_slots[] = {
    RESOURCE_SLOTS("IDirect3DIndexBuffer9"),
    {"IDirect3DIndexBuffer9::Lock", 5, 0, b_lock},
    {"IDirect3DIndexBuffer9::Unlock", 1, 0, b_unlock},
    {"IDirect3DIndexBuffer9::GetDesc", 2, 0, b_desc},
};

static void buffer_destroy(d3d9_obj *o) {
    d3d9_buffer *b = (d3d9_buffer *)o;
    if (b->buf) glDeleteBuffers(1, &b->buf);
    w32_gfree(b->mem, b->size);
    free(b);
}

uint32_t d3d9_create_buffer(d3d9_device *d, int is_index, uint32_t len, uint32_t usage, uint32_t fmt_or_fvf,
                            uint32_t pool, uint32_t *out) {
    *out = 0;
    if (!len) return D3DERR_INVALIDCALL;
    d3d9_buffer *b = calloc(1, sizeof *b);
    b->o.kind = is_index ? D3D9K_IB : D3D9K_VB;
    b->o.destroy = buffer_destroy;
    b->dev = d;
    b->is_index = is_index;
    b->size = len;
    b->usage = usage;
    b->pool = pool;
    if (is_index) b->format = fmt_or_fvf == FMT_INDEX32 ? FMT_INDEX32 : FMT_INDEX16;
    else b->fvf = fmt_or_fvf;
    b->mem = w32_galloc(len);
    if (!b->mem) {
        free(b);
        return E_OUTOFMEMORY_;
    }
    GLenum tg = is_index ? GL_ELEMENT_ARRAY_BUFFER : GL_ARRAY_BUFFER;
    glGenBuffers(1, &b->buf);
    glBindBuffer(tg, b->buf);
    glBufferData(tg, (GLsizeiptr)len, 0, (usage & USAGE_DYNAMIC) ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW);
    const d3d9_slot *slots = is_index ? ib_slots : vb_slots;
    if (!d3d9_obj_publish(&b->o, d3d9_vtable(slots, (int)(sizeof vb_slots / sizeof vb_slots[0])))) {
        buffer_destroy(&b->o);
        return E_OUTOFMEMORY_;
    }
    *out = b->o.guest;
    return D3D_OK;
}

// --- vertex declarations ---------------------------------------------------------------------
static uint64_t decl_get(rc_ctx *c, uint32_t self, const uint32_t *a) {
    (void)c;
    // pElement, pNumElements
    d3d9_decl *dc = (d3d9_decl *)d3d9_obj_get(a[0], D3D9K_DECL);
    if (!dc || !a[2]) return D3DERR_INVALIDCALL;
    if (a[1]) {
        for (int k = 0; k < dc->n; k++) memcpy(RC32_H(a[1] + 8 * k), &dc->e[k], 8);
        static const uint8_t end[8] = {0xFF, 0, 0, 0, DT_UNUSED, 0, 0, 0};
        memcpy(RC32_H(a[1] + 8 * dc->n), end, 8);
    }
    d3d9_wr32(a[2], (uint32_t)dc->n + 1);
    return D3D_OK;
}
static uint64_t obj_get_device(rc_ctx *c, uint32_t self, const uint32_t *a) { return r_get_device(c, self, a); }

static const d3d9_slot decl_slots[] = {
    {"IDirect3DVertexDeclaration9::QueryInterface", 3, 0, r_qi},
    {"IDirect3DVertexDeclaration9::AddRef", 1, 0, r_addref},
    {"IDirect3DVertexDeclaration9::Release", 1, 0, r_release},
    {"IDirect3DVertexDeclaration9::GetDevice", 2, 0, obj_get_device},
    {"IDirect3DVertexDeclaration9::GetDeclaration", 3, 0, decl_get},
};

static d3d9_decl *decl_new(d3d9_device *d) {
    d3d9_decl *dc = calloc(1, sizeof *dc);
    dc->o.kind = D3D9K_DECL;
    dc->o.destroy = (void (*)(d3d9_obj *))free;
    dc->dev = d;
    if (!d3d9_obj_publish(&dc->o, d3d9_vtable(decl_slots, (int)(sizeof decl_slots / sizeof decl_slots[0])))) {
        free(dc);
        return 0;
    }
    return dc;
}

uint32_t d3d9_create_decl(d3d9_device *d, uint32_t elems, uint32_t *out) {
    *out = 0;
    d3d9_decl *dc = decl_new(d);
    if (!dc) return E_OUTOFMEMORY_;
    for (int k = 0; k < 64; k++) {
        d3d9_velem e;
        memcpy(&e, RC32_H(elems + 8 * (uint32_t)k), 8);
        if (e.stream == 0xFF || e.type == DT_UNUSED) break;
        if (e.method != 0) d3d9_log_once("d3d9: vertex element method %u (tessellation) ignored\n", e.method);
        dc->e[dc->n++] = e;
    }
    *out = dc->o.guest;
    return D3D_OK;
}

// FVF declarations, cached per FVF for the device's lifetime.
static struct { uint32_t fvf; d3d9_decl *dc; } fvf_cache[64];
static int nfvf;

d3d9_decl *d3d9_decl_from_fvf(d3d9_device *d, uint32_t fvf) {
    for (int k = 0; k < nfvf; k++)
        if (fvf_cache[k].fvf == fvf && fvf_cache[k].dc->dev == d) return fvf_cache[k].dc;
    d3d9_decl *dc = decl_new(d);
    if (!dc) return 0;
    dc->fvf = fvf;
    uint16_t off = 0;
#define ADD(t, u, i, sz) do { dc->e[dc->n++] = (d3d9_velem){0, off, (t), 0, (u), (i)}; off += (sz); } while (0)
    uint32_t pos = fvf & FVF_POSITION_MASK;
    if (pos == FVF_XYZ) ADD(DT_FLOAT3, DU_POSITION, 0, 12);
    else if (pos == FVF_XYZRHW) ADD(DT_FLOAT4, DU_POSITIONT, 0, 16);
    else if (pos == FVF_XYZW) ADD(DT_FLOAT4, DU_POSITION, 0, 16);
    else if (pos >= FVF_XYZB1 && pos <= 0x00e) {
        int nb = (int)(pos - 0x004) / 2;  // XYZB1 = 6 .. XYZB5 = 14
        ADD(DT_FLOAT3, DU_POSITION, 0, 12);
        int weights = (fvf & 0x1000) ? nb - 1 : nb;  // LASTBETA_UBYTE4 / _D3DCOLOR
        if (weights > 0) ADD((uint8_t)(DT_FLOAT1 + weights - 1), DU_BLENDWEIGHT, 0, (uint16_t)(4 * weights));
        if (fvf & 0x1000) ADD(DT_UBYTE4, DU_BLENDINDICES, 0, 4);
    }
    if (fvf & FVF_NORMAL) ADD(DT_FLOAT3, DU_NORMAL, 0, 12);
    if (fvf & FVF_PSIZE) ADD(DT_FLOAT1, DU_PSIZE, 0, 4);
    if (fvf & FVF_DIFFUSE) ADD(DT_D3DCOLOR, DU_COLOR, 0, 4);
    if (fvf & FVF_SPECULAR) ADD(DT_D3DCOLOR, DU_COLOR, 1, 4);
    int ntex = (int)((fvf >> FVF_TEXCOUNT_SHIFT) & 15);
    for (int k = 0; k < ntex && k < 8; k++) {
        int code = (int)((fvf >> (16 + 2 * k)) & 3);
        int n = code == 0 ? 2 : code == 1 ? 3 : code == 2 ? 4 : 1;
        ADD((uint8_t)(DT_FLOAT1 + n - 1), DU_TEXCOORD, (uint8_t)k, (uint16_t)(4 * n));
    }
#undef ADD
    if (nfvf < 64) fvf_cache[nfvf++] = (typeof(fvf_cache[0])){fvf, dc};
    return dc;
}

// --- shaders --------------------------------------------------------------------------------
static uint64_t sh_get_function(rc_ctx *c, uint32_t self, const uint32_t *a) {
    (void)c;
    d3d9_obj *o = d3d9_obj_get(a[0], -1);
    if (!o || (o->kind != D3D9K_VS && o->kind != D3D9K_PS) || !a[2]) return D3DERR_INVALIDCALL;
    d3d9_shader *s = (d3d9_shader *)o;
    uint32_t bytes = 4 * d3d9_sm_get_info(s->sm)->ntokens;
    if (a[1]) {
        if (d3d9_rd32(a[2]) < bytes) return D3DERR_INVALIDCALL;
        memcpy(RC32_H(a[1]), d3d9_sm_tokens(s->sm), bytes);
    }
    d3d9_wr32(a[2], bytes);
    return D3D_OK;
}

static const d3d9_slot vs_slots[] = {
    {"IDirect3DVertexShader9::QueryInterface", 3, 0, r_qi},
    {"IDirect3DVertexShader9::AddRef", 1, 0, r_addref},
    {"IDirect3DVertexShader9::Release", 1, 0, r_release},
    {"IDirect3DVertexShader9::GetDevice", 2, 0, obj_get_device},
    {"IDirect3DVertexShader9::GetFunction", 3, 0, sh_get_function},
};
static const d3d9_slot ps_slots[] = {
    {"IDirect3DPixelShader9::QueryInterface", 3, 0, r_qi},
    {"IDirect3DPixelShader9::AddRef", 1, 0, r_addref},
    {"IDirect3DPixelShader9::Release", 1, 0, r_release},
    {"IDirect3DPixelShader9::GetDevice", 2, 0, obj_get_device},
    {"IDirect3DPixelShader9::GetFunction", 3, 0, sh_get_function},
};

static void shader_destroy(d3d9_obj *o) {
    d3d9_shader *s = (d3d9_shader *)o;
    d3d9_sm_free(s->sm);
    free(s->glsl);
    free(s);
}

uint32_t d3d9_create_shader(d3d9_device *d, int ps, uint32_t func, uint32_t *out) {
    *out = 0;
    char err[256] = "";
    // the bytecode ends with its end token; never read past the window
    uint32_t maxtok = (uint32_t)((0x100000000ull - func) / 4);
    if (maxtok > (1u << 20)) maxtok = 1u << 20;
    d3d9_sm *sm = d3d9_sm_parse(RC32_H(func), maxtok, err, sizeof err);
    if (!sm) {
        fprintf(stderr, "d3d9: Create%sShader: %s\n", ps ? "Pixel" : "Vertex", err);
        return D3DERR_INVALIDCALL;
    }
    if (d3d9_sm_get_info(sm)->is_ps != ps) {
        d3d9_sm_free(sm);
        return D3DERR_INVALIDCALL;
    }
    d3d9_shader *s = calloc(1, sizeof *s);
    s->o.kind = ps ? D3D9K_PS : D3D9K_VS;
    s->o.destroy = shader_destroy;
    s->dev = d;
    s->sm = sm;
    s->id = d->next_shader_id++;
    // translate now: the pixel shader's GLSL is final; a vertex shader's is checked here and
    // generated again per pixel shader it is linked with
    char *g = d3d9_sm_glsl(sm, 0, err, sizeof err);
    if (!g) {
        const d3d9_sm_info *in = d3d9_sm_get_info(sm);
        fprintf(stderr, "d3d9: %s_%d_%d shader %u not translated (its draws are skipped): %s\n", ps ? "ps" : "vs",
                in->major, in->minor, s->id, err);
        s->failed = 1;
    }
    if (ps) s->glsl = g;
    else free(g);
    if (!d3d9_obj_publish(&s->o, ps ? d3d9_vtable(ps_slots, 5) : d3d9_vtable(vs_slots, 5))) {
        shader_destroy(&s->o);
        return E_OUTOFMEMORY_;
    }
    *out = s->o.guest;
    return D3D_OK;
}

// --- queries ---------------------------------------------------------------------------------
static d3d9_query *query_of(uint32_t g) { return (d3d9_query *)d3d9_obj_get(g, D3D9K_QUERY); }

static uint32_t query_size(uint32_t t) {
    switch (t) {
    case QT_EVENT: case QT_OCCLUSION: case QT_TIMESTAMPDISJOINT: return 4;
    case QT_TIMESTAMP: case QT_TIMESTAMPFREQ: return 8;
    }
    return 0;
}
static uint64_t q_type(rc_ctx *c, uint32_t self, const uint32_t *a) { (void)c; d3d9_query *q = query_of(a[0]); return q ? q->type : 0; }
static uint64_t q_size(rc_ctx *c, uint32_t self, const uint32_t *a) { (void)c; d3d9_query *q = query_of(a[0]); return q ? query_size(q->type) : 0; }
static uint64_t q_issue(rc_ctx *c, uint32_t self, const uint32_t *a) {
    (void)c;
    d3d9_query *q = query_of(a[0]);
    if (!q) return D3DERR_INVALIDCALL;
    q->issued = 1;
    return D3D_OK;
}
static uint64_t q_get_data(rc_ctx *c, uint32_t self, const uint32_t *a) {
    (void)c;
    // pData, dwSize, dwGetDataFlags. GL keeps the order of commands and buffer updates, so an
    // event is always reached; the counters are plausible constants.
    d3d9_query *q = query_of(a[0]);
    if (!q) return D3DERR_INVALIDCALL;
    uint32_t n = query_size(q->type);
    if (!a[1] || !a[2]) return D3D_OK;
    if (a[2] < n) return D3DERR_INVALIDCALL;
    uint64_t v = 0;
    switch (q->type) {
    case QT_EVENT: v = 1; break;
    case QT_OCCLUSION: v = 1; d3d9_log_once("d3d9: occlusion queries always report 1 pixel\n"); break;
    case QT_TIMESTAMP: {
        static mach_timebase_info_data_t tb;
        if (!tb.denom) mach_timebase_info(&tb);
        v = mach_absolute_time() * tb.numer / tb.denom;
        break;
    }
    case QT_TIMESTAMPFREQ: v = 1000000000ull; break;
    case QT_TIMESTAMPDISJOINT: v = 0; break;
    }
    memcpy(RC32_H(a[1]), &v, n);
    return D3D_OK;
}

static const d3d9_slot query_slots[] = {
    {"IDirect3DQuery9::QueryInterface", 3, 0, r_qi},
    {"IDirect3DQuery9::AddRef", 1, 0, r_addref},
    {"IDirect3DQuery9::Release", 1, 0, r_release},
    {"IDirect3DQuery9::GetDevice", 2, 0, obj_get_device},
    {"IDirect3DQuery9::GetType", 1, 0, q_type},
    {"IDirect3DQuery9::GetDataSize", 1, 0, q_size},
    {"IDirect3DQuery9::Issue", 2, 0, q_issue},
    {"IDirect3DQuery9::GetData", 4, 0, q_get_data},
};

uint32_t d3d9_create_query(d3d9_device *d, uint32_t type, uint32_t *out) {
    if (!query_size(type)) {
        d3d9_log_once("d3d9: query type %u not supported\n", type);
        return D3DERR_NOTAVAILABLE;
    }
    if (!out) return D3D_OK;  // a support check
    *out = 0;
    d3d9_query *q = calloc(1, sizeof *q);
    q->o.kind = D3D9K_QUERY;
    q->o.destroy = (void (*)(d3d9_obj *))free;
    q->dev = d;
    q->type = type;
    if (!d3d9_obj_publish(&q->o, d3d9_vtable(query_slots, 8))) {
        free(q);
        return E_OUTOFMEMORY_;
    }
    *out = q->o.guest;
    return D3D_OK;
}

// --- state blocks ----------------------------------------------------------------------------
static d3d9_stateblock *sb_of(uint32_t g) { return (d3d9_stateblock *)d3d9_obj_get(g, D3D9K_STATEBLOCK); }

static uint64_t sb_capture(rc_ctx *c, uint32_t self, const uint32_t *a) {
    (void)c;
    d3d9_stateblock *sb = sb_of(a[0]);
    if (!sb) return D3DERR_INVALIDCALL;
    d3d9_dev_lock(sb->dev);
    d3d9_state_copy(&sb->s, &sb->dev->st, &sb->m);
    d3d9_dev_unlock(sb->dev);
    return D3D_OK;
}
static uint64_t sb_apply(rc_ctx *c, uint32_t self, const uint32_t *a) {
    (void)c;
    d3d9_stateblock *sb = sb_of(a[0]);
    if (!sb) return D3DERR_INVALIDCALL;
    d3d9_device *d = sb->dev;
    d3d9_dev_lock(d);
    d3d9_state_copy(&d->st, &sb->s, &sb->m);
    d3d9_dev_state_changed(d);
    d3d9_dev_unlock(d);
    return D3D_OK;
}

static const d3d9_slot sb_slots[] = {
    {"IDirect3DStateBlock9::QueryInterface", 3, 0, r_qi},
    {"IDirect3DStateBlock9::AddRef", 1, 0, r_addref},
    {"IDirect3DStateBlock9::Release", 1, 0, r_release},
    {"IDirect3DStateBlock9::GetDevice", 2, 0, obj_get_device},
    {"IDirect3DStateBlock9::Capture", 1, 0, sb_capture},
    {"IDirect3DStateBlock9::Apply", 1, 0, sb_apply},
};

static void sb_destroy(d3d9_obj *o) {
    d3d9_stateblock *sb = (d3d9_stateblock *)o;
    d3d9_state_release(&sb->s);
    free(sb);
}

d3d9_stateblock *d3d9_stateblock_new(d3d9_device *d) {
    d3d9_stateblock *sb = calloc(1, sizeof *sb);
    sb->o.kind = D3D9K_STATEBLOCK;
    sb->o.destroy = sb_destroy;
    sb->dev = d;
    for (int k = 0; k < D3D9_MAX_STREAMS; k++) sb->s.vb_freq[k] = 1;
    if (!d3d9_obj_publish(&sb->o, d3d9_vtable(sb_slots, 6))) {
        free(sb);
        return 0;
    }
    return sb;
}

uint32_t d3d9_create_stateblock(d3d9_device *d, uint32_t type, uint32_t *out) {
    *out = 0;
    if (type < SBT_ALL || type > SBT_VERTEXSTATE) return D3DERR_INVALIDCALL;
    d3d9_stateblock *sb = d3d9_stateblock_new(d);
    if (!sb) return E_OUTOFMEMORY_;
    d3d9_state_mask *m = &sb->m;
    // ALL: everything; PIXELSTATE / VERTEXSTATE: render and sampler states plus that stage's
    // shader and constants (D3D's lists are finer; this is a superset of each)
    memset(m->rs, 1, sizeof m->rs);
    memset(m->ss, 1, sizeof m->ss);
    if (type != SBT_VERTEXSTATE) {
        m->ps = 1;
        memset(m->psc, 1, sizeof m->psc);
        memset(m->psi, 1, sizeof m->psi);
        memset(m->psb, 1, sizeof m->psb);
    }
    if (type != SBT_PIXELSTATE) {
        m->vs = m->decl = m->fvf = 1;
        memset(m->vsc, 1, sizeof m->vsc);
        memset(m->vsi, 1, sizeof m->vsi);
        memset(m->vsb, 1, sizeof m->vsb);
    }
    // fixed function: texture stages, transforms, material, lights and clip planes in every type
    memset(m->tss, 1, sizeof m->tss);
    memset(m->xf, 1, sizeof m->xf);
    m->mat = 1;
    memset(m->light, 1, sizeof m->light);
    memset(m->clip, 1, sizeof m->clip);
    if (type == SBT_ALL) {
        memset(m->tex, 1, sizeof m->tex);
        memset(m->vb, 1, sizeof m->vb);
        m->ib = m->viewport = m->scissor = 1;
    }
    d3d9_state_copy(&sb->s, &d->st, m);
    *out = sb->o.guest;
    return D3D_OK;
}

// --- the swap chain --------------------------------------------------------------------------
typedef struct {
    d3d9_obj o;
    d3d9_device *dev;
} d3d9_swapchain;

static d3d9_swapchain *sc_of(uint32_t g) { return (d3d9_swapchain *)d3d9_obj_get(g, D3D9K_SWAPCHAIN); }

static uint64_t sc_present(rc_ctx *c, uint32_t self, const uint32_t *a) {
    (void)c;
    d3d9_swapchain *s = sc_of(a[0]);
    return s ? d3d9_dev_present(s->dev) : D3DERR_INVALIDCALL;
}
static uint64_t sc_get_backbuffer(rc_ctx *c, uint32_t self, const uint32_t *a) {
    (void)c;
    // iBackBuffer, Type, ppBackBuffer
    d3d9_swapchain *s = sc_of(a[0]);
    if (!s || !a[3] || a[1] != 0 || !s->dev->backbuffer) return D3DERR_INVALIDCALL;
    d3d9_obj_addref(&s->dev->backbuffer->o);
    d3d9_wr32(a[3], s->dev->backbuffer->o.guest);
    return D3D_OK;
}
static uint64_t sc_raster_status(rc_ctx *c, uint32_t self, const uint32_t *a) {
    (void)c;
    if (!a[1]) return D3DERR_INVALIDCALL;
    memset(RC32_H(a[1]), 0, 8);
    return D3D_OK;
}
static uint64_t sc_display_mode(rc_ctx *c, uint32_t self, const uint32_t *a) {
    (void)c;
    d3d9_swapchain *s = sc_of(a[0]);
    if (!s || !a[1]) return D3DERR_INVALIDCALL;
    uint32_t m[4] = {(uint32_t)s->dev->width, (uint32_t)s->dev->height, 60, FMT_X8R8G8B8};
    memcpy(RC32_H(a[1]), m, 16);
    return D3D_OK;
}
static uint64_t sc_pp(rc_ctx *c, uint32_t self, const uint32_t *a) {
    (void)c;
    d3d9_swapchain *s = sc_of(a[0]);
    if (!s || !a[1]) return D3DERR_INVALIDCALL;
    memcpy(RC32_H(a[1]), s->dev->pp, sizeof s->dev->pp);
    return D3D_OK;
}

static const d3d9_slot sc_slots[] = {
    {"IDirect3DSwapChain9::QueryInterface", 3, 0, r_qi},
    {"IDirect3DSwapChain9::AddRef", 1, 0, r_addref},
    {"IDirect3DSwapChain9::Release", 1, 0, r_release},
    {"IDirect3DSwapChain9::Present", 6, 0, sc_present},
    {"IDirect3DSwapChain9::GetFrontBufferData", 2, 0},
    {"IDirect3DSwapChain9::GetBackBuffer", 4, 0, sc_get_backbuffer},
    {"IDirect3DSwapChain9::GetRasterStatus", 2, 0, sc_raster_status},
    {"IDirect3DSwapChain9::GetDisplayMode", 2, 0, sc_display_mode},
    {"IDirect3DSwapChain9::GetDevice", 2, 0, obj_get_device},
    {"IDirect3DSwapChain9::GetPresentParameters", 2, 0, sc_pp},
};

uint32_t d3d9_swapchain_get(d3d9_device *d) {
    if (!d->swapchain) {
        d3d9_swapchain *s = calloc(1, sizeof *s);
        s->o.kind = D3D9K_SWAPCHAIN;
        s->o.destroy = (void (*)(d3d9_obj *))free;
        s->dev = d;
        if (!d3d9_obj_publish(&s->o, d3d9_vtable(sc_slots, 10))) {
            free(s);
            return 0;
        }
        s->o.ref = 0;
        s->o.fwd = &d->o;  // the implicit swap chain lives as long as the device
        d->swapchain = &s->o;
    }
    d3d9_obj_addref(d->swapchain);
    return d->swapchain->guest;
}
