// IDirect3DDevice9 on libRSOpenGL (d3d9_internal.h has the conventions).
#include <dlfcn.h>
#include <math.h>
#include <mach/mach_time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "d3d9_internal.h"
#include "rs_gfx.h"
#include "win32_shims.h"

static const uint8_t IID_IUnknown[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0xC0, 0, 0, 0, 0, 0, 0, 0x46};
// {D0223B96-BF7A-43fd-92BD-A43B0D82B9EB}
static const uint8_t IID_IDirect3DDevice9[16] = {0x96, 0x3B, 0x22, 0xD0, 0x7A, 0xBF, 0xfd, 0x43,
                                                 0x92, 0xBD, 0xA4, 0x3B, 0x0D, 0x82, 0xB9, 0xEB};

d3d9_surface *d3d9_texture_level(d3d9_texture *t, int face, int level);

__attribute__((weak)) rs_gfx_context *d3d9_platform_context(uint32_t hwnd, int w, int h) {
    (void)hwnd;
    return rs_gfx_create_offscreen(w, h);
}

// A Reset to a new back buffer size: the window seam's hook (win-x86/app/lp_window.m).
__attribute__((weak)) void d3d9_platform_resize(uint32_t hwnd, int w, int h) {
    (void)hwnd, (void)w, (void)h;
}
// Whether the next d3d9_platform_context is for a windowed device (else full screen).
__attribute__((weak)) void d3d9_platform_windowed(int windowed) { (void)windowed; }

static d3d9_device *dev_of(uint32_t g) { return (d3d9_device *)d3d9_obj_get(g, D3D9K_DEVICE); }

void d3d9_dev_lock(d3d9_device *d) {
    pthread_mutex_lock(&d->mu);
    if (rs_gfx_current() != d->ctx) rs_gfx_make_current(d->ctx);
}
void d3d9_dev_unlock(d3d9_device *d) { pthread_mutex_unlock(&d->mu); }

// Every method body runs locked, with the device's context current.
#define DEV(a)                              \
    d3d9_device *d = dev_of((a)[0]);        \
    if (!d) return D3DERR_INVALIDCALL;      \
    d3d9_dev_lock(d);                       \
    uint32_t hr_ = D3D_OK;                  \
    (void)c
#define DONE(v) do { hr_ = (v); goto out_; } while (0)
#define END out_: __attribute__((unused)); d3d9_dev_unlock(d); return hr_

static inline float f32(uint32_t u) { return d3d9_f32(u); }
static inline uint32_t u32f(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }

// --- state defaults ------------------------------------------------------------------------
void d3d9_state_defaults(d3d9_device *d, d3d9_state *s) {
    memset(s->rs, 0, sizeof s->rs);
    uint32_t *r = s->rs;
    r[RS_ZENABLE] = d->auto_ds ? 1 : 0;
    r[RS_FILLMODE] = 3;
    r[RS_SHADEMODE] = 2;
    r[RS_ZWRITEENABLE] = 1;
    r[RS_LASTPIXEL] = 1;
    r[RS_SRCBLEND] = 2;
    r[RS_DESTBLEND] = 1;
    r[RS_CULLMODE] = 3;
    r[RS_ZFUNC] = 4;
    r[RS_ALPHAFUNC] = 8;
    r[RS_FOGTABLEMODE] = 0;
    r[RS_FOGEND] = u32f(1.0f);
    r[RS_FOGDENSITY] = u32f(1.0f);
    r[RS_STENCILFAIL] = r[RS_STENCILZFAIL] = r[RS_STENCILPASS] = 1;
    r[RS_STENCILFUNC] = 8;
    r[RS_STENCILMASK] = r[RS_STENCILWRITEMASK] = 0xFFFFFFFFu;
    r[RS_TEXTUREFACTOR] = 0xFFFFFFFFu;
    r[RS_CLIPPING] = 1;
    r[RS_LIGHTING] = 1;
    r[RS_COLORVERTEX] = 1;
    r[RS_LOCALVIEWER] = 1;
    r[RS_DIFFUSEMATERIALSOURCE] = 1;
    r[RS_SPECULARMATERIALSOURCE] = 2;
    r[RS_POINTSIZE] = u32f(1.0f);
    r[RS_POINTSIZE_MIN] = u32f(1.0f);
    r[RS_POINTSCALE_A] = u32f(1.0f);
    r[RS_MULTISAMPLEANTIALIAS] = 1;
    r[RS_MULTISAMPLEMASK] = 0xFFFFFFFFu;
    r[RS_POINTSIZE_MAX] = u32f(64.0f);
    r[RS_COLORWRITEENABLE] = r[RS_COLORWRITEENABLE1] = r[RS_COLORWRITEENABLE2] = r[RS_COLORWRITEENABLE3] = 0xF;
    r[RS_TWEENFACTOR] = 0;
    r[RS_BLENDOP] = 1;
    r[RS_POSITIONDEGREE] = 3;
    r[RS_NORMALDEGREE] = 1;
    r[RS_MINTESSELLATIONLEVEL] = r[RS_MAXTESSELLATIONLEVEL] = u32f(1.0f);
    r[RS_ADAPTIVETESS_W] = u32f(1.0f);
    r[RS_CCW_STENCILFAIL] = r[RS_CCW_STENCILZFAIL] = r[RS_CCW_STENCILPASS] = 1;
    r[RS_CCW_STENCILFUNC] = 8;
    r[RS_BLENDFACTOR] = 0xFFFFFFFFu;
    r[RS_SRCBLENDALPHA] = 2;
    r[RS_DESTBLENDALPHA] = 1;
    r[RS_BLENDOPALPHA] = 1;
    for (int k = 0; k < D3D9_MAX_SAMPLERS; k++) {
        uint32_t *ss = s->ss[k];
        memset(ss, 0, sizeof s->ss[k]);
        ss[SS_ADDRESSU] = ss[SS_ADDRESSV] = ss[SS_ADDRESSW] = 1;
        ss[SS_MAGFILTER] = ss[SS_MINFILTER] = 1;
        ss[SS_MAXANISOTROPY] = 1;
    }
    for (int k = 0; k < 8; k++) {
        memset(s->tss[k], 0, sizeof s->tss[k]);
        s->tss[k][1] = k ? 1 : 4;  // COLOROP: DISABLE / MODULATE
        s->tss[k][2] = 2;          // COLORARG1 TEXTURE
        s->tss[k][3] = 1;          // COLORARG2 CURRENT
        s->tss[k][4] = k ? 1 : 2;  // ALPHAOP
        s->tss[k][5] = 2;
        s->tss[k][6] = 1;
        s->tss[k][11] = (uint32_t)k;  // TEXCOORDINDEX
    }
    for (int k = 0; k < D3D9_MAX_STREAMS; k++) s->vb_freq[k] = 1;
    uint32_t w = d->rt[0] ? d->rt[0]->width : (uint32_t)d->width, h = d->rt[0] ? d->rt[0]->height : (uint32_t)d->height;
    s->viewport[0] = s->viewport[1] = 0;
    s->viewport[2] = w;
    s->viewport[3] = h;
    s->viewport[4] = u32f(0.0f);
    s->viewport[5] = u32f(1.0f);
    s->scissor[0] = s->scissor[1] = 0;
    s->scissor[2] = (int32_t)w;
    s->scissor[3] = (int32_t)h;
    d3d9_ffp_defaults(s);
}

// Swap a held object reference.
#define SETREF(slot, val) do { d3d9_obj *n_ = (d3d9_obj *)(val); d3d9_obj_ihold(n_); d3d9_obj_idrop((d3d9_obj *)(slot)); (slot) = (void *)n_; } while (0)

// The state a Set* call writes: the recording state block or the device.
static d3d9_state *wstate(d3d9_device *d) { return d->recording ? &d->recording->s : &d->st; }
static d3d9_state_mask *wmask(d3d9_device *d) { return d->recording ? &d->recording->m : 0; }

void d3d9_dev_unbind(d3d9_device *d, d3d9_obj *o) {
    d3d9_state *s = &d->st;
    for (int k = 0; k < D3D9_MAX_SAMPLERS; k++) if (s->tex[k] == o) SETREF(s->tex[k], 0);
    for (int k = 0; k < D3D9_MAX_STREAMS; k++) if ((d3d9_obj *)s->vb[k] == o) SETREF(s->vb[k], 0);
    if ((d3d9_obj *)s->ib == o) SETREF(s->ib, 0);
    if ((d3d9_obj *)s->vs == o) SETREF(s->vs, 0);
    if ((d3d9_obj *)s->ps == o) SETREF(s->ps, 0);
    if ((d3d9_obj *)s->decl == o) SETREF(s->decl, 0);
}

// --- GL helpers ------------------------------------------------------------------------------
static GLenum gl_cmp(uint32_t f) { return f >= 1 && f <= 8 ? GL_NEVER + (f - 1) : 0x0207; }
static GLenum gl_blend(uint32_t b) {
    switch (b) {
    case 1: return GL_ZERO;
    case 2: return GL_ONE;
    case 3: return GL_SRC_COLOR;
    case 4: return GL_ONE_MINUS_SRC_COLOR;
    case 5: return GL_SRC_ALPHA;
    case 6: return GL_ONE_MINUS_SRC_ALPHA;
    case 7: return GL_DST_ALPHA;
    case 8: return GL_ONE_MINUS_DST_ALPHA;
    case 9: return GL_DST_COLOR;
    case 10: return GL_ONE_MINUS_DST_COLOR;
    case 11: return GL_SRC_ALPHA_SATURATE;
    case 14: return GL_CONSTANT_COLOR;
    case 15: return GL_ONE_MINUS_CONSTANT_COLOR;
    }
    d3d9_log_once("d3d9: blend factor %u not supported\n", b);
    return GL_ONE;
}
static GLenum gl_blendop(uint32_t o) {
    switch (o) {
    case 2: return GL_FUNC_SUBTRACT;
    case 3: return GL_FUNC_REVERSE_SUBTRACT;
    case 4: return GL_MIN;
    case 5: return GL_MAX;
    }
    return GL_FUNC_ADD;
}
static GLenum gl_stencilop(uint32_t o) {
    switch (o) {
    case 2: return GL_ZERO;
    case 3: return GL_REPLACE;
    case 4: return GL_INCR;
    case 5: return GL_DECR;
    case 6: return GL_INVERT;
    case 7: return GL_INCR_WRAP;
    case 8: return GL_DECR_WRAP;
    }
    return GL_KEEP;
}

static void glset(GLenum cap, int on) { if (on) glEnable(cap); else glDisable(cap); }

static void blend_pair(uint32_t s, uint32_t dd, GLenum *gs, GLenum *gd) {
    if (s == 12) { *gs = GL_SRC_ALPHA; *gd = GL_ONE_MINUS_SRC_ALPHA; return; }  // BOTHSRCALPHA
    if (s == 13) { *gs = GL_ONE_MINUS_SRC_ALPHA; *gd = GL_SRC_ALPHA; return; }  // BOTHINVSRCALPHA
    *gs = gl_blend(s);
    *gd = gl_blend(dd);
}

static void apply_state(d3d9_device *d) {
    const uint32_t *r = d->st.rs;
    uint32_t dirty = d->dirty;
    d->dirty = 0;
    if (dirty & D3D9D_BLEND) {
        glset(GL_BLEND, r[RS_ALPHABLENDENABLE] != 0);
        GLenum s, dd, sa, da;
        blend_pair(r[RS_SRCBLEND], r[RS_DESTBLEND], &s, &dd);
        if (r[RS_SEPARATEALPHABLENDENABLE]) blend_pair(r[RS_SRCBLENDALPHA], r[RS_DESTBLENDALPHA], &sa, &da);
        else sa = s, da = dd;
        glBlendFuncSeparate(s, dd, sa, da);
        glBlendEquation(gl_blendop(r[RS_BLENDOP]));
        if (r[RS_SEPARATEALPHABLENDENABLE] && r[RS_BLENDOPALPHA] != r[RS_BLENDOP])
            d3d9_log_once("d3d9: a separate alpha blend op is not supported (colour op used)\n");
        uint32_t f = r[RS_BLENDFACTOR];
        glBlendColor(((f >> 16) & 255) / 255.0f, ((f >> 8) & 255) / 255.0f, (f & 255) / 255.0f, (f >> 24) / 255.0f);
    }
    if (dirty & D3D9D_DEPTH) {
        glset(GL_DEPTH_TEST, r[RS_ZENABLE] != 0 && d->ds != 0);
        glDepthFunc(gl_cmp(r[RS_ZFUNC]));
        glDepthMask(r[RS_ZWRITEENABLE] != 0 && d->ds != 0);
    }
    if (dirty & D3D9D_STENCIL) {
        glset(GL_STENCIL_TEST, r[RS_STENCILENABLE] != 0 && d->ds != 0);
        uint32_t ref = r[RS_STENCILREF], mask = r[RS_STENCILMASK];
        // D3D's clockwise faces are GL's front faces here (the flip), so the CCW_* states are
        // GL's back faces
        glStencilFuncSeparate(GL_FRONT, gl_cmp(r[RS_STENCILFUNC]), (GLint)ref, mask);
        glStencilFuncSeparate(GL_BACK, gl_cmp(r[RS_TWOSIDEDSTENCILMODE] ? r[RS_CCW_STENCILFUNC] : r[RS_STENCILFUNC]),
                              (GLint)ref, mask);
        glStencilOp(gl_stencilop(r[RS_STENCILFAIL]), gl_stencilop(r[RS_STENCILZFAIL]), gl_stencilop(r[RS_STENCILPASS]));
        if (r[RS_TWOSIDEDSTENCILMODE] && (r[RS_CCW_STENCILFAIL] != r[RS_STENCILFAIL] ||
                                          r[RS_CCW_STENCILZFAIL] != r[RS_STENCILZFAIL] ||
                                          r[RS_CCW_STENCILPASS] != r[RS_STENCILPASS]))
            d3d9_log_once("d3d9: two-sided stencil with different CCW operations is not supported\n");
        glStencilMask(r[RS_STENCILWRITEMASK]);
    }
    if (dirty & D3D9D_RASTER) {
        glFrontFace(GL_CCW);
        uint32_t cull = r[RS_CULLMODE];
        glset(GL_CULL_FACE, cull == 2 || cull == 3);
        if (cull == 2) glCullFace(GL_FRONT);       // D3DCULL_CW: D3D clockwise = GL counter-clockwise
        else if (cull == 3) glCullFace(GL_BACK);   // D3DCULL_CCW
        uint32_t fill = r[RS_FILLMODE];
        glPolygonMode(GL_FRONT_AND_BACK, fill == 1 ? GL_POINT : fill == 2 ? GL_LINE : GL_FILL);
        if (r[RS_DEPTHBIAS] || r[RS_SLOPESCALEDEPTHBIAS]) d3d9_log_once("d3d9: depth bias is not supported\n");
    }
    if (dirty & D3D9D_VIEWPORT) {
        const uint32_t *v = d->st.viewport;
        glViewport((GLint)v[0], (GLint)v[1], (GLsizei)v[2], (GLsizei)v[3]);
    }
    if (dirty & D3D9D_SCISSOR) {
        glset(GL_SCISSOR_TEST, r[RS_SCISSORTESTENABLE] != 0);
        const int32_t *s = d->st.scissor;
        int w = s[2] - s[0], h = s[3] - s[1];
        glScissor(s[0], s[1], w > 0 ? w : 0, h > 0 ? h : 0);
    }
    if (dirty & D3D9D_ALPHATEST) {
        glset(GL_ALPHA_TEST, r[RS_ALPHATESTENABLE] != 0);
        glAlphaFunc(gl_cmp(r[RS_ALPHAFUNC]), (float)(r[RS_ALPHAREF] & 255) / 255.0f);
    }
    if (dirty & D3D9D_COLORMASK) {
        uint32_t m = r[RS_COLORWRITEENABLE];
        glColorMask((m & 1) != 0, (m & 2) != 0, (m & 4) != 0, (m & 8) != 0);
    }
}

static uint32_t rs_dirty_group(uint32_t st) {
    switch (st) {
    case RS_ALPHABLENDENABLE: case RS_SRCBLEND: case RS_DESTBLEND: case RS_BLENDOP: case RS_SEPARATEALPHABLENDENABLE:
    case RS_SRCBLENDALPHA: case RS_DESTBLENDALPHA: case RS_BLENDOPALPHA: case RS_BLENDFACTOR:
        return D3D9D_BLEND;
    case RS_ZENABLE: case RS_ZFUNC: case RS_ZWRITEENABLE:
        return D3D9D_DEPTH;
    case RS_STENCILENABLE: case RS_STENCILFAIL: case RS_STENCILZFAIL: case RS_STENCILPASS: case RS_STENCILFUNC:
    case RS_STENCILREF: case RS_STENCILMASK: case RS_STENCILWRITEMASK: case RS_TWOSIDEDSTENCILMODE:
    case RS_CCW_STENCILFAIL: case RS_CCW_STENCILZFAIL: case RS_CCW_STENCILPASS: case RS_CCW_STENCILFUNC:
        return D3D9D_STENCIL;
    case RS_CULLMODE: case RS_FILLMODE: case RS_DEPTHBIAS: case RS_SLOPESCALEDEPTHBIAS:
        return D3D9D_RASTER;
    case RS_SCISSORTESTENABLE: return D3D9D_SCISSOR;
    case RS_ALPHATESTENABLE: case RS_ALPHAFUNC: case RS_ALPHAREF: return D3D9D_ALPHATEST;
    case RS_COLORWRITEENABLE: return D3D9D_COLORMASK;
    }
    return 0;
}

// --- render targets --------------------------------------------------------------------------
void d3d9_dev_bind_target(d3d9_device *d, d3d9_surface *color, d3d9_surface *depth) {
    glBindFramebuffer(GL_FRAMEBUFFER, d->fbo);
    GLuint tex = 0;
    GLint level = 0;
    GLenum target = GL_TEXTURE_2D;
    if (color) {
        if (color->container) {
            tex = color->container->tex;
            level = color->level;
            if (color->container->cube) target = GL_TEXTURE_CUBE_MAP_POSITIVE_X + (GLenum)color->face;
        } else {
            tex = color->tex;
        }
    }
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, target, tex, level);
    GLuint rb = depth ? depth->rb : 0;
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, rb);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, rb);
    GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (st != GL_FRAMEBUFFER_COMPLETE) d3d9_log_once("d3d9: render target incomplete (0x%x)\n", st);
}

static void ensure_target(d3d9_device *d) {
    if (!d->fbo_dirty) return;
    d->fbo_dirty = 0;
    d3d9_dev_bind_target(d, d->rt[0], d->ds);
}

// --- the blit program (Present, StretchRect) -------------------------------------------------
static const char kBlitVS[] =
    "#version 120\n"
    "attribute vec2 rs_p;\n"
    "uniform vec4 rs_b[2];\n"
    "varying vec2 rs_uv;\n"
    "void main() {\n"
    "  rs_uv = mix(rs_b[1].xy, rs_b[1].zw, rs_p);\n"
    "  gl_Position = vec4(mix(rs_b[0].xy, rs_b[0].zw, rs_p), 0.0, 1.0);\n"
    "}\n";
static const char kBlitFS[] =
    "#version 120\n"
    "uniform sampler2D rs_tex;\n"
    "varying vec2 rs_uv;\n"
    "void main() { gl_FragColor = texture2D(rs_tex, rs_uv); }\n";

static GLuint compile(GLenum type, const char *src, char *log, size_t loglen) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, 0);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        if (log) glGetShaderInfoLog(s, (GLsizei)loglen, 0, log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

GLuint d3d9_gl_link(const char *vs, const char *fs, void (*bind)(GLuint, void *), void *arg, char *log, size_t loglen) {
    GLuint v = compile(GL_VERTEX_SHADER, vs, log, loglen);
    if (!v) return 0;
    GLuint f = compile(GL_FRAGMENT_SHADER, fs, log, loglen);
    if (!f) { glDeleteShader(v); return 0; }
    GLuint p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    if (bind) bind(p, arg);
    glLinkProgram(p);
    glDeleteShader(v);
    glDeleteShader(f);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        if (log) glGetProgramInfoLog(p, (GLsizei)loglen, 0, log);
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

static void bind_blit(GLuint p, void *arg) { (void)arg; glBindAttribLocation(p, 0, "rs_p"); }

static GLuint blit_quad_buf;

void d3d9_dev_blit(d3d9_device *d, GLuint tex, const float dst[4], const float src[4], int linear) {
    if (!d->blit_prog) {
        char log[1024] = "";
        d->blit_prog = d3d9_gl_link(kBlitVS, kBlitFS, bind_blit, 0, log, sizeof log);
        if (!d->blit_prog) { fprintf(stderr, "d3d9: blit program: %s\n", log); return; }
        d->blit_loc_rect = glGetUniformLocation(d->blit_prog, "rs_b");
        d->blit_loc_tex = glGetUniformLocation(d->blit_prog, "rs_tex");
    }
    if (!blit_quad_buf) {
        static const float q[8] = {0, 0, 1, 0, 0, 1, 1, 1};
        glGenBuffers(1, &blit_quad_buf);
        glBindBuffer(GL_ARRAY_BUFFER, blit_quad_buf);
        glBufferData(GL_ARRAY_BUFFER, sizeof q, q, GL_STATIC_DRAW);
    }
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_ALPHA_TEST);
    glColorMask(1, 1, 1, 1);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glUseProgram(d->blit_prog);
    d->cur_prog = 0;
    float b[8] = {dst[0], dst[1], dst[2], dst[3], src[0], src[1], src[2], src[3]};
    glUniform4fv(d->blit_loc_rect, 2, b);
    glUniform1i(d->blit_loc_tex, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    GLint f = linear ? GL_LINEAR : GL_NEAREST;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, f);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, f);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    for (int k = 0; k < 16; k++)
        if (d->attr_enabled[k]) { glDisableVertexAttribArray((GLuint)k); d->attr_enabled[k] = 0; }
    glBindBuffer(GL_ARRAY_BUFFER, blit_quad_buf);
    glVertexAttribPointer(0, 2, GL_FLOAT, 0, 0, 0);
    glEnableVertexAttribArray(0);
    if (d->attr_divisor[0]) { glVertexAttribDivisorARB(0, 0); d->attr_divisor[0] = 0; }
    d->attr_enabled[0] = 1;
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    d->dirty = D3D9D_ALL;
    d->fbo_dirty = 1;
    // the texture's sampler state was overwritten
    for (int k = 0; k < D3D9_MAX_SAMPLERS; k++) {
        d3d9_obj *o = d->st.tex[k];
        if (o && o->kind == D3D9K_TEXTURE && ((d3d9_texture *)o)->tex == tex) ((d3d9_texture *)o)->s_min = -1;
    }
}

// --- programs ----------------------------------------------------------------------------------
static void bind_attrs(GLuint p, void *arg) {
    d3d9_shader *vs = arg;
    const d3d9_sm_info *in = d3d9_sm_get_info(vs->sm);
    char n[16];
    for (int k = 0; k < 16; k++)
        if (in->input[k].used) {
            snprintf(n, sizeof n, "rs_v%d", k);
            glBindAttribLocation(p, (GLuint)k, n);
        }
}

GLint d3d9_gl_uloc(GLuint p, const char *name) {
    GLint l = glGetUniformLocation(p, name);
    if (l < 0) {
        char b[32];
        snprintf(b, sizeof b, "%s[0]", name);
        l = glGetUniformLocation(p, b);
    }
    return l;
}

int d3d9_program_failures;  // programs that did not translate or link (tests read it)
unsigned long d3d9_programs_made;  // LP_THREAD_CPU's memory line

// Both shaders bound (d3d9_ffp.c makes the programs with a fixed-function stage).
static d3d9_program *get_program(d3d9_device *d, d3d9_shader *vs, d3d9_shader *ps) {
    uint32_t vid = vs->id, pid = ps->id;
    uint32_t h = (vid * 31u + pid) & 255;
    for (d3d9_program *p = d->programs[h]; p; p = p->next)
        if (p->vs_id == vid && p->ps_id == pid) return p;
    d3d9_program *p = calloc(1, sizeof *p);
    d3d9_programs_made++;
    p->vs_id = vid;
    p->ps_id = pid;
    p->next = d->programs[h];
    d->programs[h] = p;
    p->vc_gen = p->pc_gen = p->ic_gen = ~0ull;
    char err[512] = "";
    char *vsrc = d3d9_sm_glsl(vs->sm, ps->sm, err, sizeof err);
    if (!vsrc) {
        fprintf(stderr, "d3d9: vertex shader %u: %s\n", vid, err);
        p->failed = 1;
        d3d9_program_failures++;
        return p;
    }
    if (!ps->glsl) {
        p->failed = 1;
        d3d9_program_failures++;
        free(vsrc);
        return p;
    }
    char log[2048] = "";
    uint64_t t0 = LP_FRAME_ON() ? lp_frame_now() : 0;
    p->prog = d3d9_gl_link(vsrc, ps->glsl, bind_attrs, vs, log, sizeof log);
    if (t0) lp_frame_event(LPF_PROGRAM, lp_frame_now() - t0, 0);
    if (!p->prog) {
        fprintf(stderr, "d3d9: program (vs %u, ps %u) failed: %s\n--- vs\n%s--- ps\n%s", vid, pid, log, vsrc,
                ps->glsl);
        p->failed = 1;
        d3d9_program_failures++;
        free(vsrc);
        return p;
    }
    free(vsrc);
    const d3d9_sm_info *vi = d3d9_sm_get_info(vs->sm);
    p->nvc = vi->nconst;
    p->npc = d3d9_sm_get_info(ps->sm)->nconst;
    p->loc_vc = d3d9_gl_uloc(p->prog, "vc");
    p->loc_pc = d3d9_gl_uloc(p->prog, "pc");
    p->loc_fix = glGetUniformLocation(p->prog, "rs_fix");
    p->loc_vi = d3d9_gl_uloc(p->prog, "rs_vi");
    p->loc_pi = d3d9_gl_uloc(p->prog, "rs_pi");
    p->loc_vb = d3d9_gl_uloc(p->prog, "rs_vb");
    p->loc_pb = d3d9_gl_uloc(p->prog, "rs_pb");
    for (int k = 0; k < 16; k++) {
        p->attr_used[k] = vi->input[k].used;
        p->attr[k] = vi->input[k];
    }
    glUseProgram(p->prog);
    const d3d9_sm_info *pi = d3d9_sm_get_info(ps->sm);
    for (int k = 0; k < 16; k++)
        if (pi->sampler_type[k]) {
            p->sampler_type[k] = pi->sampler_type[k];
            char n[8];
            snprintf(n, sizeof n, "s%d", k);
            GLint l = glGetUniformLocation(p->prog, n);
            if (l >= 0) glUniform1i(l, k);
        }
    d->cur_prog = p;
    p->fix[0] = NAN;
    return p;
}

// --- draws ----------------------------------------------------------------------------------
static GLenum gl_prim(uint32_t t, uint32_t n, GLsizei *count) {
    switch (t) {
    case PT_POINTLIST: *count = (GLsizei)n; return GL_POINTS;
    case PT_LINELIST: *count = (GLsizei)(2 * n); return GL_LINES;
    case PT_LINESTRIP: *count = (GLsizei)(n + 1); return GL_LINE_STRIP;
    case PT_TRIANGLELIST: *count = (GLsizei)(3 * n); return GL_TRIANGLES;
    case PT_TRIANGLESTRIP: *count = (GLsizei)(n + 2); return GL_TRIANGLE_STRIP;
    case PT_TRIANGLEFAN: *count = (GLsizei)(n + 2); return GL_TRIANGLE_FAN;
    }
    *count = 0;
    return GL_POINTS;
}

static int velem_gl(uint8_t type, GLint *size, GLenum *gtype, GLboolean *norm) {
    *norm = 0;
    switch (type) {
    case DT_FLOAT1: case DT_FLOAT2: case DT_FLOAT3: case DT_FLOAT4: *size = type + 1; *gtype = GL_FLOAT; return 1;
    case DT_D3DCOLOR: *size = GL_BGRA; *gtype = GL_UNSIGNED_BYTE; *norm = 1; return 1;
    case DT_UBYTE4: *size = 4; *gtype = GL_UNSIGNED_BYTE; return 1;
    case DT_UBYTE4N: *size = 4; *gtype = GL_UNSIGNED_BYTE; *norm = 1; return 1;
    case DT_SHORT2: *size = 2; *gtype = GL_SHORT; return 1;
    case DT_SHORT4: *size = 4; *gtype = GL_SHORT; return 1;
    case DT_SHORT2N: *size = 2; *gtype = GL_SHORT; *norm = 1; return 1;
    case DT_SHORT4N: *size = 4; *gtype = GL_SHORT; *norm = 1; return 1;
    case DT_USHORT2N: *size = 2; *gtype = GL_UNSIGNED_SHORT; *norm = 1; return 1;
    case DT_USHORT4N: *size = 4; *gtype = GL_UNSIGNED_SHORT; *norm = 1; return 1;
    case DT_FLOAT16_2: *size = 2; *gtype = GL_HALF_FLOAT; return 1;
    case DT_FLOAT16_4: *size = 4; *gtype = GL_HALF_FLOAT; return 1;
    }
    d3d9_log_once("d3d9: vertex element type %u not supported\n", type);
    return 0;
}

// The vertices [lo, hi) of a draw (base vertex included), for d3d9_buffer_sync; all = 1 when they
// are not known (then a stream's whole pending range is uploaded).
typedef struct { int64_t lo, hi; int all; } vrange;

// The vertex range an indexed draw reads: the smallest and largest index of its n indices from
// startIndex, read from the guest copy of the index buffer, plus the base vertex. Only computed
// when a stream of the draw has bytes pending upload.
static vrange index_range(d3d9_buffer *ib, uint32_t start, uint32_t n, int32_t base) {
    vrange r = {0, 0, 1};
    uint32_t isz = ib->format == FMT_INDEX32 ? 4 : 2;
    if ((uint64_t)start * isz + (uint64_t)n * isz > ib->size || !n) return r;
    uint32_t mn = UINT32_MAX, mx = 0;
    if (isz == 2) {
        const uint16_t *p = RC32_H(ib->mem + start * 2);
        for (uint32_t k = 0; k < n; k++) {
            uint16_t v;
            memcpy(&v, p + k, 2);
            mn = v < mn ? v : mn;
            mx = v > mx ? v : mx;
        }
    } else {
        const uint32_t *p = RC32_H(ib->mem + start * 4);
        for (uint32_t k = 0; k < n; k++) {
            uint32_t v;
            memcpy(&v, p + k, 4);
            mn = v < mn ? v : mn;
            mx = v > mx ? v : mx;
        }
    }
    r.lo = (int64_t)base + mn;
    r.hi = (int64_t)base + mx + 1;
    r.all = 0;
    return r;
}

// Set up program, constants, textures and attributes for a draw. base: base vertex index;
// up_stride: nonzero for the *UP draws (stream 0 is d->up_buf); vr: the vertices it reads (for
// the dynamic buffers' upload). Returns 0 to skip the draw.
static int draw_prep(d3d9_device *d, int32_t base, uint32_t up_stride, int *instances, vrange vr) {
    d3d9_state *s = &d->st;
    *instances = 0;
    d3d9_decl *decl = s->decl;
    if (!decl && s->fvf) decl = s->decl = d3d9_decl_from_fvf(d, s->fvf), d3d9_obj_ihold(&decl->o);
    if (!decl) {
        d3d9_log_once("d3d9: draw without a vertex declaration skipped\n");
        return 0;
    }
    if ((s->vs && s->vs->failed) || (s->ps && s->ps->failed)) return 0;
    // a missing stage is generated from the fixed-function state
    d3d9_program *p = s->vs && s->ps ? get_program(d, s->vs, s->ps) : d3d9_ffp_program(d, decl);
    if (p->failed) return 0;
    ensure_target(d);
    apply_state(d);
    if (d->cur_prog != p) {
        glUseProgram(p->prog);
        d->cur_prog = p;
    }
    // constants
    if (p->nvc && p->loc_vc >= 0 && p->vc_gen != d->vc_gen) {
        glUniform4fv(p->loc_vc, p->nvc, &s->vsc[0][0]);
        p->vc_gen = d->vc_gen;
    }
    if (p->npc && p->loc_pc >= 0 && p->pc_gen != d->pc_gen) {
        glUniform4fv(p->loc_pc, p->npc, &s->psc[0][0]);
        p->pc_gen = d->pc_gen;
    }
    if (p->ic_gen != d->ic_gen) {
        GLint v[16];
        if (p->loc_vi >= 0) { for (int k = 0; k < 16; k++) v[k] = s->vsi[k][0]; glUniform1iv(p->loc_vi, 16, v); }
        if (p->loc_pi >= 0) { for (int k = 0; k < 16; k++) v[k] = s->psi[k][0]; glUniform1iv(p->loc_pi, 16, v); }
        if (p->loc_vb >= 0) glUniform1iv(p->loc_vb, 16, s->vsb);
        if (p->loc_pb >= 0) glUniform1iv(p->loc_pb, 16, s->psb);
        p->ic_gen = d->ic_gen;
    }
    float vw = (float)s->viewport[2], vh = (float)s->viewport[3];
    float mn = f32(s->viewport[4]), mx = f32(s->viewport[5]);
    float fix[4] = {vw > 0 ? 1.0f / vw : 0, vh > 0 ? 1.0f / vh : 0, 2.0f * (mx - mn), 2.0f * mn - 1.0f};
    if (p->loc_fix >= 0 && memcmp(fix, p->fix, sizeof fix)) {
        glUniform4fv(p->loc_fix, 1, fix);
        memcpy(p->fix, fix, sizeof fix);
    }
    if (p->ffp) d3d9_ffp_upload(d, p);
    // textures
    int any_tex = 0;
    for (int k = 0; k < 16; k++) {
        if (!p->sampler_type[k]) continue;
        any_tex = 1;
        glActiveTexture(GL_TEXTURE0 + (GLenum)k);
        d3d9_obj *o = s->tex[k];
        d3d9_texture *t = o && (o->kind == D3D9K_TEXTURE || o->kind == D3D9K_CUBE) ? (d3d9_texture *)o : 0;
        GLenum want = p->sampler_type[k] == 3 ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D;
        if (t && t->target == want) {
            glBindTexture(want, t->tex);
            d3d9_texture_apply_sampler(t, s->ss[k]);
        } else {
            glBindTexture(want, 0);
        }
    }
    if (any_tex) glActiveTexture(GL_TEXTURE0);
    // attributes: each vertex shader input from the declaration element with its semantic
    uint32_t inst_count = 0;
    if (!up_stride && (s->vb_freq[0] & STREAMSOURCE_INDEXEDDATA)) inst_count = s->vb_freq[0] & 0x3fffffffu;
    for (int r = 0; r < 16; r++) {
        int on = 0;
        if (p->attr_used[r]) {
            const d3d9_velem *e = 0;
            for (int k = 0; k < decl->n; k++)
                if (decl->e[k].usage == p->attr[r].usage && decl->e[k].index == p->attr[r].index) { e = &decl->e[k]; break; }
            GLint size;
            GLenum type;
            GLboolean norm;
            if (e && velem_gl(e->type, &size, &type, &norm)) {
                GLuint buf;
                uintptr_t off;
                uint32_t stride, divisor = 0;
                if (up_stride) {
                    if (e->stream != 0) goto no_data;
                    buf = d->up_buf;
                    stride = up_stride;
                    off = e->offset + (uintptr_t)((int64_t)base * stride);
                } else {
                    d3d9_buffer *vb = s->vb[e->stream];
                    if (!vb) goto no_data;
                    buf = vb->buf;
                    stride = s->vb_stride[e->stream];
                    uint32_t fq = s->vb_freq[e->stream];
                    if (inst_count && (fq & STREAMSOURCE_INSTANCEDATA)) {
                        divisor = fq & 0x3fffffffu;
                        off = s->vb_off[e->stream] + e->offset;
                    } else {
                        off = s->vb_off[e->stream] + e->offset + (uintptr_t)((int64_t)base * stride);
                    }
                    if (d3d9_buffer_pending(vb)) {
                        // to the end of the last element read (an element may reach past the stride)
                        uint64_t so = s->vb_off[e->stream], tail = e->offset + 16u;
                        if (vr.all || !stride) {
                            d3d9_buffer_sync(vb, 0, vb->size);
                        } else if (divisor) {  // per instance: ceil(instances / divisor) elements
                            d3d9_buffer_sync(vb, so, so + (uint64_t)((inst_count + divisor - 1) / divisor) * stride + tail);
                        } else {
                            int64_t lo = vr.lo < 0 ? 0 : vr.lo, hi = vr.hi < lo ? lo : vr.hi;
                            d3d9_buffer_sync(vb, so + (uint64_t)lo * stride, so + (uint64_t)hi * stride + tail);
                        }
                    }
                }
                glBindBuffer(GL_ARRAY_BUFFER, buf);
                glVertexAttribPointer((GLuint)r, size, type, norm, (GLsizei)stride, (const void *)off);
                if (!d->attr_enabled[r]) glEnableVertexAttribArray((GLuint)r);
                if ((uint32_t)d->attr_divisor[r] != divisor) {
                    glVertexAttribDivisorARB((GLuint)r, divisor);
                    d->attr_divisor[r] = (int)divisor;
                }
                on = 1;
            } else if (!e) {
                d3d9_log_once("d3d9: vertex shader input usage %d index %d has no declaration element\n",
                              p->attr[r].usage, p->attr[r].index);
            }
        }
    no_data:  // an input with no buffer reads the current value, not the last draw's pointer
        if (!on && d->attr_enabled[r]) glDisableVertexAttribArray((GLuint)r);
        d->attr_enabled[r] = on;
    }
    *instances = (int)inst_count;
    return 1;
}

// --- methods -------------------------------------------------------------------------------
static uint64_t m_qi(rc_ctx *c, uint32_t self, const uint32_t *a) {
    (void)c;
    if (!a[2]) return D3DERR_INVALIDCALL;
    const uint8_t *iid = RC32_H(a[1]);
    d3d9_device *d = dev_of(a[0]);
    if (d && (!memcmp(iid, IID_IUnknown, 16) || !memcmp(iid, IID_IDirect3DDevice9, 16))) {
        d3d9_obj_addref(&d->o);
        d3d9_wr32(a[2], a[0]);
        return D3D_OK;
    }
    d3d9_wr32(a[2], 0);
    return E_NOINTERFACE_;
}
static uint64_t m_addref(rc_ctx *c, uint32_t self, const uint32_t *a) { (void)c; d3d9_device *d = dev_of(a[0]); return d ? d3d9_obj_addref(&d->o) : 0; }
static uint64_t m_release(rc_ctx *c, uint32_t self, const uint32_t *a) { (void)c; d3d9_device *d = dev_of(a[0]); return d ? d3d9_obj_release(&d->o) : 0; }

static uint64_t m_test_coop(rc_ctx *c, uint32_t self, const uint32_t *a) { (void)c; return dev_of(a[0]) ? D3D_OK : D3DERR_INVALIDCALL; }
static uint64_t m_avail_mem(rc_ctx *c, uint32_t self, const uint32_t *a) { (void)c; (void)a; return 0x7FF00000u; }
static uint64_t m_ok(rc_ctx *c, uint32_t self, const uint32_t *a) { (void)c; (void)a; return D3D_OK; }

static uint64_t m_get_d3d(rc_ctx *c, uint32_t self, const uint32_t *a) {
    DEV(a);
    if (!a[1]) DONE(D3DERR_INVALIDCALL);
    d3d9_obj *o = d3d9_obj_get(d->d3d, D3D9K_D3D);
    if (o) d3d9_obj_addref(o);
    d3d9_wr32(a[1], o ? d->d3d : 0);
    END;
}
static uint64_t m_caps(rc_ctx *c, uint32_t self, const uint32_t *a) {
    (void)c;
    if (!a[1]) return D3DERR_INVALIDCALL;
    d3d9_fill_caps(RC32_H(a[1]));
    return D3D_OK;
}
static uint64_t m_display_mode(rc_ctx *c, uint32_t self, const uint32_t *a) {
    DEV(a);
    if (!a[2]) DONE(D3DERR_INVALIDCALL);
    uint32_t m[4] = {(uint32_t)d->width, (uint32_t)d->height, 60, FMT_X8R8G8B8};
    memcpy(RC32_H(a[2]), m, 16);
    END;
}
static uint64_t m_creation(rc_ctx *c, uint32_t self, const uint32_t *a) {
    DEV(a);
    if (!a[1]) DONE(D3DERR_INVALIDCALL);
    uint32_t p[4] = {d->adapter, d->devtype, d->focus, d->behavior};
    memcpy(RC32_H(a[1]), p, 16);
    END;
}
static uint64_t m_show_cursor(rc_ctx *c, uint32_t self, const uint32_t *a) {
    (void)c;
    static int shown;
    int prev = shown;
    shown = a[1] != 0;
    return (uint32_t)prev;
}
static uint64_t m_get_swapchain(rc_ctx *c, uint32_t self, const uint32_t *a) {
    DEV(a);
    if (!a[2] || a[1] != 0) DONE(D3DERR_INVALIDCALL);
    d3d9_wr32(a[2], d3d9_swapchain_get(d));
    END;
}
static uint64_t m_num_swapchains(rc_ctx *c, uint32_t self, const uint32_t *a) { (void)c; (void)a; return 1; }

static void create_backbuffer(d3d9_device *d) {
    uint32_t *pp = d->pp;
    d->width = (int)pp[PP_WIDTH];
    d->height = (int)pp[PP_HEIGHT];
    uint32_t fmt = pp[PP_FORMAT] == FMT_UNKNOWN ? FMT_X8R8G8B8 : pp[PP_FORMAT];
    d->backbuffer = d3d9_surface_new(d, (uint32_t)d->width, (uint32_t)d->height, fmt, USAGE_RENDERTARGET, POOL_DEFAULT, 0);
    d->auto_ds = 0;
    if (pp[PP_AUTODS])
        d->auto_ds = d3d9_surface_new(d, (uint32_t)d->width, (uint32_t)d->height, pp[PP_DSFORMAT], USAGE_DEPTHSTENCIL, POOL_DEFAULT, 0);
    SETREF(d->rt[0], d->backbuffer);
    for (int k = 1; k < 4; k++) SETREF(d->rt[k], 0);
    SETREF(d->ds, d->auto_ds);
    d->fbo_dirty = 1;
}

static void read_pp(d3d9_device *d, uint32_t g) {
    memcpy(d->pp, RC32_H(g), sizeof d->pp);
    uint32_t *pp = d->pp;
    if (!pp[PP_WIDTH] || !pp[PP_HEIGHT]) {
        // windowed with a zero size: the window's client area; without a window seam, 1280x720
        pp[PP_WIDTH] = 1280;
        pp[PP_HEIGHT] = 720;
    }
    if (pp[PP_FORMAT] == FMT_UNKNOWN) pp[PP_FORMAT] = FMT_X8R8G8B8;
    if (!pp[PP_COUNT]) pp[PP_COUNT] = 1;
    if (pp[PP_MSTYPE]) {
        d3d9_log_once("d3d9: multisampled back buffer (type %u) created without multisampling\n", pp[PP_MSTYPE]);
        pp[PP_MSTYPE] = 0;
    }
    memcpy(RC32_H(g), pp, sizeof d->pp);  // D3D writes the resolved values back
}

static uint64_t m_reset(rc_ctx *c, uint32_t self, const uint32_t *a) {
    DEV(a);
    if (!a[1]) DONE(D3DERR_INVALIDCALL);
    if (d->backbuffer) { SETREF(d->rt[0], 0); d3d9_obj_release(&d->backbuffer->o); d->backbuffer = 0; }
    if (d->auto_ds) { SETREF(d->ds, 0); d3d9_obj_release(&d->auto_ds->o); d->auto_ds = 0; }
    read_pp(d, a[1]);
    fprintf(stderr, "d3d9: Reset %ux%u, %s\n", d->pp[PP_WIDTH], d->pp[PP_HEIGHT], d->pp[PP_WINDOWED] ? "windowed" : "fullscreen");
    rs_gfx_resize(d->ctx, (int)d->pp[PP_WIDTH], (int)d->pp[PP_HEIGHT]);
    d3d9_platform_resize(d->pp[PP_HWND] ? d->pp[PP_HWND] : d->focus, (int)d->pp[PP_WIDTH], (int)d->pp[PP_HEIGHT]);
    create_backbuffer(d);
    // Reset puts every state back to its default and unbinds everything
    d3d9_state *s = &d->st;
    for (int k = 0; k < D3D9_MAX_SAMPLERS; k++) SETREF(s->tex[k], 0);
    for (int k = 0; k < D3D9_MAX_STREAMS; k++) SETREF(s->vb[k], 0);
    SETREF(s->ib, 0);
    SETREF(s->vs, 0);
    SETREF(s->ps, 0);
    SETREF(s->decl, 0);
    s->fvf = 0;
    d3d9_state_defaults(d, s);
    d->dirty = D3D9D_ALL;
    d->vc_gen++;
    d->pc_gen++;
    d->ic_gen++;
    END;
}

// RS_GL_FRAMETIMES=<file>: an 'F' event per Present in libRSOpenGL's frame-time log
// (shims/opengl/src/nsgl_hook.m), as the Mac build's shell writes on Metal, so the same tools read
// both. Otherwise one load and branch per frame.
typedef void (*d3d9_ft_mark_t)(int kind, uint64_t t0, uint64_t t1, uint64_t x, unsigned flags);
static d3d9_ft_mark_t d3d9_ft_mark(void) {
    static d3d9_ft_mark_t fn;
    static int state;  // only the presenting thread asks
    if (!state) {
        const char *e = getenv("RS_GL_FRAMETIMES");
        if (e && *e) fn = (d3d9_ft_mark_t)dlsym(RTLD_DEFAULT, "rs_gl_frametime_mark");
        state = 1;
    }
    return fn;
}

uint32_t d3d9_dev_present(d3d9_device *d) {
    d3d9_ft_mark_t ft = d3d9_ft_mark();
    uint64_t ft0 = ft ? mach_absolute_time() : 0;
    lp_frame_present();
    uint64_t t0 = LP_FRAME_ON() ? lp_frame_now() : 0;
    d3d9_dev_lock(d);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, d->width, d->height);
    // back buffer row 0 is the top: draw it flipped onto framebuffer 0 (row 0 at the bottom)
    static const float dst[4] = {-1, -1, 1, 1}, src[4] = {0, 1, 1, 0};
    if (d->backbuffer) d3d9_dev_blit(d, d->backbuffer->tex, dst, src, 0);
    rs_gfx_present(d->ctx);
    d->fbo_dirty = 1;
    d->dirty = D3D9D_ALL;
    d3d9_dev_unlock(d);
    if (t0) lp_frame_event(LPF_PRESENT, lp_frame_now() - t0, 0);
    if (ft) ft('F', ft0, mach_absolute_time(), 0, 0);
    return D3D_OK;
}

static uint64_t m_present(rc_ctx *c, uint32_t self, const uint32_t *a) {
    (void)c;
    // pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion: the whole back buffer always
    d3d9_device *d = dev_of(a[0]);
    if (!d) return D3DERR_INVALIDCALL;
    if (a[1] || a[2] || a[3]) d3d9_log_once("d3d9: Present with rectangles / a window override: whole back buffer presented\n");
    return d3d9_dev_present(d);
}

static uint64_t m_get_backbuffer(rc_ctx *c, uint32_t self, const uint32_t *a) {
    DEV(a);
    // iSwapChain, iBackBuffer, Type, ppBackBuffer
    if (!a[4] || a[1] != 0 || a[2] != 0 || !d->backbuffer) DONE(D3DERR_INVALIDCALL);
    d3d9_obj_addref(&d->backbuffer->o);
    d3d9_wr32(a[4], d->backbuffer->o.guest);
    END;
}
static uint64_t m_raster_status(rc_ctx *c, uint32_t self, const uint32_t *a) {
    (void)c;
    if (!a[2]) return D3DERR_INVALIDCALL;
    uint32_t r[2] = {0, 0};
    memcpy(RC32_H(a[2]), r, 8);
    return D3D_OK;
}
static uint64_t m_set_gamma(rc_ctx *c, uint32_t self, const uint32_t *a) {
    (void)c;
    (void)a;
    d3d9_log_once("d3d9: SetGammaRamp ignored\n");
    return D3D_OK;
}
static uint64_t m_get_gamma(rc_ctx *c, uint32_t self, const uint32_t *a) {
    (void)c;
    if (!a[2]) return D3D_OK;
    uint16_t *r = RC32_H(a[2]);
    for (int ch = 0; ch < 3; ch++)
        for (int k = 0; k < 256; k++) r[ch * 256 + k] = (uint16_t)(k * 257);
    return D3D_OK;
}

static uint64_t m_create_texture(rc_ctx *c, uint32_t self, const uint32_t *a) {
    DEV(a);
    hr_ = d3d9_create_texture(d, a, 0, 0);
    END;
}
static uint64_t m_create_cube(rc_ctx *c, uint32_t self, const uint32_t *a) {
    DEV(a);
    hr_ = d3d9_create_texture(d, a, 1, 0);
    END;
}
static uint64_t m_create_vb(rc_ctx *c, uint32_t self, const uint32_t *a) {
    DEV(a);
    // Length, Usage, FVF, Pool, ppVertexBuffer, pSharedHandle
    if (!a[5]) DONE(D3DERR_INVALIDCALL);
    uint32_t g = 0;
    hr_ = d3d9_create_buffer(d, 0, a[1], a[2], a[3], a[4], &g);
    d3d9_wr32(a[5], g);
    END;
}
static uint64_t m_create_ib(rc_ctx *c, uint32_t self, const uint32_t *a) {
    DEV(a);
    // Length, Usage, Format, Pool, ppIndexBuffer, pSharedHandle
    if (!a[5]) DONE(D3DERR_INVALIDCALL);
    uint32_t g = 0;
    hr_ = d3d9_create_buffer(d, 1, a[1], a[2], a[3], a[4], &g);
    d3d9_wr32(a[5], g);
    END;
}
static uint64_t m_create_rt(rc_ctx *c, uint32_t self, const uint32_t *a) {
    DEV(a);
    // Width, Height, Format, MultiSample, MultisampleQuality, Lockable, ppSurface, pSharedHandle
    if (!a[7]) DONE(D3DERR_INVALIDCALL);
    if (a[4]) d3d9_log_once("d3d9: multisampled render target created without multisampling\n");
    d3d9_surface *s = d3d9_surface_new(d, a[1], a[2], a[3], USAGE_RENDERTARGET, POOL_DEFAULT, 0);
    d3d9_wr32(a[7], s ? s->o.guest : 0);
    hr_ = s ? D3D_OK : D3DERR_OUTOFVIDEOMEMORY;
    END;
}
static uint64_t m_create_ds(rc_ctx *c, uint32_t self, const uint32_t *a) {
    DEV(a);
    // Width, Height, Format, MultiSample, MultisampleQuality, Discard, ppSurface, pSharedHandle
    if (!a[7]) DONE(D3DERR_INVALIDCALL);
    d3d9_surface *s = d3d9_surface_new(d, a[1], a[2], a[3], USAGE_DEPTHSTENCIL, POOL_DEFAULT, 0);
    d3d9_wr32(a[7], s ? s->o.guest : 0);
    hr_ = s ? D3D_OK : D3DERR_OUTOFVIDEOMEMORY;
    END;
}
static uint64_t m_create_offscreen(rc_ctx *c, uint32_t self, const uint32_t *a) {
    DEV(a);
    // Width, Height, Format, Pool, ppSurface, pSharedHandle
    if (!a[5]) DONE(D3DERR_INVALIDCALL);
    d3d9_surface *s = d3d9_surface_new(d, a[1], a[2], a[3], 0, a[4], 0);
    d3d9_wr32(a[5], s ? s->o.guest : 0);
    hr_ = s ? D3D_OK : D3DERR_OUTOFVIDEOMEMORY;
    END;
}

static d3d9_surface *surf_of(uint32_t g) { return (d3d9_surface *)d3d9_obj_get(g, D3D9K_SURFACE); }

static uint64_t m_update_surface(rc_ctx *c, uint32_t self, const uint32_t *a) {
    DEV(a);
    // pSourceSurface, pSourceRect, pDestinationSurface, pDestPoint
    d3d9_surface *src = surf_of(a[1]), *dst = surf_of(a[3]);
    if (!src || !dst || src->format != dst->format) DONE(D3DERR_INVALIDCALL);
    uint32_t r[4] = {0, 0, src->width, src->height}, pt[2] = {0, 0};
    if (a[2]) memcpy(r, RC32_H(a[2]), 16);
    if (a[4]) memcpy(pt, RC32_H(a[4]), 8);
    uint32_t sm = d3d9_surface_ensure_mem(src), dm = d3d9_surface_ensure_mem(dst);
    if (!sm || !dm) DONE(E_OUTOFMEMORY_);
    int block = d3d9_format_is_block(src->format);
    uint32_t bpp = (uint32_t)d3d9_format_bpp(src->format);
    if (block) {
        // whole blocks: rows of 4
        uint32_t bs = bpp * 2;  // bytes per 4x4 block
        for (uint32_t y = r[1] / 4; y < (r[3] + 3) / 4; y++)
            memcpy(RC32_H(dm + (pt[1] / 4 + y - r[1] / 4) * dst->pitch + pt[0] / 4 * bs),
                   RC32_H(sm + y * src->pitch + r[0] / 4 * bs), (r[2] - r[0] + 3) / 4 * bs);
    } else {
        uint32_t bp = bpp / 8;
        for (uint32_t y = r[1]; y < r[3]; y++)
            memcpy(RC32_H(dm + (pt[1] + y - r[1]) * dst->pitch + pt[0] * bp), RC32_H(sm + y * src->pitch + r[0] * bp),
                   (r[2] - r[0]) * bp);
    }
    d3d9_surface_upload_mem(dst);
    END;
}

static uint64_t m_update_texture(rc_ctx *c, uint32_t self, const uint32_t *a) {
    DEV(a);
    d3d9_obj *so = d3d9_obj_get(a[1], -1), *dobj = d3d9_obj_get(a[2], -1);
    if (!so || !dobj || so->kind != dobj->kind || (so->kind != D3D9K_TEXTURE && so->kind != D3D9K_CUBE)) DONE(D3DERR_INVALIDCALL);
    d3d9_texture *st = (d3d9_texture *)so, *dt = (d3d9_texture *)dobj;
    if (st->format != dt->format) DONE(D3DERR_INVALIDCALL);
    // levels match from the top when the sizes match; skip extra source levels otherwise
    uint32_t skip = 0;
    while (skip < st->levels && (st->width >> skip) > dt->width) skip++;
    int faces = st->cube ? 6 : 1;
    for (int f = 0; f < faces; f++)
        for (uint32_t l = 0; l < dt->levels && l + skip < st->levels; l++) {
            d3d9_surface *ss = d3d9_texture_level(st, f, (int)(l + skip)), *ds = d3d9_texture_level(dt, f, (int)l);
            if (!ss || !ds) continue;
            uint32_t sm = d3d9_surface_ensure_mem(ss), dm = d3d9_surface_ensure_mem(ds);
            if (!sm || !dm) continue;
            memcpy(RC32_H(dm), RC32_H(sm), ss->mem_size < ds->mem_size ? ss->mem_size : ds->mem_size);
            d3d9_surface_upload_mem(ds);
        }
    END;
}

static uint64_t m_get_rt_data(rc_ctx *c, uint32_t self, const uint32_t *a) {
    DEV(a);
    // pRenderTarget, pDestSurface
    d3d9_surface *src = surf_of(a[1]), *dst = surf_of(a[2]);
    if (!src || !dst || src->width != dst->width || src->height != dst->height) DONE(D3DERR_INVALIDCALL);
    uint32_t m = d3d9_surface_ensure_mem(dst);
    if (!m) DONE(E_OUTOFMEMORY_);
    if (dst->format != src->format && !((dst->format == FMT_A8R8G8B8 || dst->format == FMT_X8R8G8B8) &&
                                        (src->format == FMT_A8R8G8B8 || src->format == FMT_X8R8G8B8)))
        DONE(D3DERR_INVALIDCALL);
    d3d9_surface_read(src, RC32_H(m), dst->pitch);
    END;
}
static uint64_t m_get_front_data(rc_ctx *c, uint32_t self, const uint32_t *a) {
    DEV(a);
    // iSwapChain, pDestSurface (A8R8G8B8 of the display size); the back buffer stands in
    d3d9_surface *dst = surf_of(a[2]);
    if (!dst || !d->backbuffer) DONE(D3DERR_INVALIDCALL);
    uint32_t m = d3d9_surface_ensure_mem(dst);
    if (m && dst->width >= d->backbuffer->width && dst->height >= d->backbuffer->height)
        d3d9_surface_read(d->backbuffer, RC32_H(m), dst->pitch);
    END;
}

static void rect_or_full(uint32_t g, const d3d9_surface *s, int32_t r[4]) {
    if (g) memcpy(r, RC32_H(g), 16);
    else { r[0] = 0; r[1] = 0; r[2] = (int32_t)s->width; r[3] = (int32_t)s->height; }
}

static GLuint surface_texture(d3d9_surface *s) {
    if (s->container) return s->level == 0 && !s->container->cube ? s->container->tex : 0;
    return s->tex;
}

static uint64_t m_stretch_rect(rc_ctx *c, uint32_t self, const uint32_t *a) {
    DEV(a);
    // pSourceSurface, pSourceRect, pDestSurface, pDestRect, Filter
    d3d9_surface *src = surf_of(a[1]), *dst = surf_of(a[3]);
    if (!src || !dst) DONE(D3DERR_INVALIDCALL);
    if (src->rb || dst->rb) {
        d3d9_log_once("d3d9: StretchRect of depth stencil surfaces is not supported\n");
        DONE(D3D_OK);
    }
    GLuint tex = surface_texture(src);
    if (!tex) {
        d3d9_log_once("d3d9: StretchRect from a mip level / cube face / system memory surface is not supported\n");
        DONE(D3D_OK);
    }
    int32_t sr[4], dr[4];
    rect_or_full(a[2], src, sr);
    rect_or_full(a[4], dst, dr);
    d3d9_dev_bind_target(d, dst, 0);
    glViewport(0, 0, (GLsizei)dst->width, (GLsizei)dst->height);
    // both rows top first: no flip
    float dn[4] = {2.0f * dr[0] / dst->width - 1, 2.0f * dr[1] / dst->height - 1, 2.0f * dr[2] / dst->width - 1,
                   2.0f * dr[3] / dst->height - 1};
    float sn[4] = {(float)sr[0] / src->width, (float)sr[1] / src->height, (float)sr[2] / src->width,
                   (float)sr[3] / src->height};
    d3d9_dev_blit(d, tex, dn, sn, a[5] == 2 || a[5] == 3);
    END;
}

static void clear_rects(d3d9_device *d, d3d9_surface *target, uint32_t count, uint32_t rects, uint32_t flags,
                        uint32_t color, float z, uint32_t stencil, int use_state) {
    GLbitfield bits = 0;
    if (flags & CLEAR_TARGET) {
        bits |= GL_COLOR_BUFFER_BIT;
        glClearColor(((color >> 16) & 255) / 255.0f, ((color >> 8) & 255) / 255.0f, (color & 255) / 255.0f,
                     (color >> 24) / 255.0f);
        glColorMask(1, 1, 1, 1);
    }
    if ((flags & CLEAR_ZBUFFER) && d->ds) {
        bits |= GL_DEPTH_BUFFER_BIT;
        glClearDepth(z);
        glDepthMask(1);
    }
    if ((flags & CLEAR_STENCIL) && d->ds) {
        bits |= GL_STENCIL_BUFFER_BIT;
        glClearStencil((GLint)stencil);
        glStencilMask(0xFFFFFFFFu);
    }
    if (!bits) return;
    int32_t base[4] = {0, 0, (int32_t)target->width, (int32_t)target->height};
    if (use_state) {
        const uint32_t *v = d->st.viewport;
        int32_t vr[4] = {(int32_t)v[0], (int32_t)v[1], (int32_t)(v[0] + v[2]), (int32_t)(v[1] + v[3])};
        for (int k = 0; k < 2; k++) { if (vr[k] > base[k]) base[k] = vr[k]; if (vr[k + 2] < base[k + 2]) base[k + 2] = vr[k + 2]; }
        if (d->st.rs[RS_SCISSORTESTENABLE]) {
            const int32_t *s = d->st.scissor;
            for (int k = 0; k < 2; k++) { if (s[k] > base[k]) base[k] = s[k]; if (s[k + 2] < base[k + 2]) base[k + 2] = s[k + 2]; }
        }
    }
    glEnable(GL_SCISSOR_TEST);
    uint32_t n = rects && count ? count : 1;
    for (uint32_t k = 0; k < n; k++) {
        int32_t r[4] = {base[0], base[1], base[2], base[3]};
        if (rects && count) {
            int32_t q[4];
            memcpy(q, RC32_H(rects + 16 * k), 16);
            for (int j = 0; j < 2; j++) { if (q[j] > r[j]) r[j] = q[j]; if (q[j + 2] < r[j + 2]) r[j + 2] = q[j + 2]; }
        }
        if (r[2] <= r[0] || r[3] <= r[1]) continue;
        glScissor(r[0], r[1], r[2] - r[0], r[3] - r[1]);
        glClear(bits);
    }
    d->dirty |= D3D9D_SCISSOR | D3D9D_COLORMASK | D3D9D_DEPTH | D3D9D_STENCIL;
}

static uint64_t m_color_fill(rc_ctx *c, uint32_t self, const uint32_t *a) {
    DEV(a);
    // pSurface, pRect, color
    d3d9_surface *s = surf_of(a[1]);
    if (!s || s->rb) DONE(D3DERR_INVALIDCALL);
    if (!surface_texture(s) && !s->container) {
        d3d9_log_once("d3d9: ColorFill of a system memory surface is not supported\n");
        DONE(D3D_OK);
    }
    d3d9_dev_bind_target(d, s, 0);
    d3d9_surface *saved = d->ds;
    d->ds = 0;
    clear_rects(d, s, a[2] ? 1 : 0, a[2], CLEAR_TARGET, a[3], 0, 0, 0);
    d->ds = saved;
    d->fbo_dirty = 1;
    END;
}

static uint64_t m_set_rt(rc_ctx *c, uint32_t self, const uint32_t *a) {
    DEV(a);
    // RenderTargetIndex, pRenderTarget
    uint32_t i = a[1];
    d3d9_surface *s = a[2] ? surf_of(a[2]) : 0;
    if (i >= 4 || (i == 0 && !s) || (a[2] && !s) || (s && !(s->usage & USAGE_RENDERTARGET))) DONE(D3DERR_INVALIDCALL);
    if (i > 0) {
        if (s) d3d9_log_once("d3d9: multiple render targets are not supported (index %u ignored)\n", i);
        SETREF(d->rt[i], s);
        DONE(D3D_OK);
    }
    SETREF(d->rt[0], s);
    d->fbo_dirty = 1;
    // setting render target 0 resets the viewport and the scissor rectangle to the whole target
    d3d9_state *st = &d->st;
    st->viewport[0] = st->viewport[1] = 0;
    st->viewport[2] = s->width;
    st->viewport[3] = s->height;
    st->viewport[4] = u32f(0.0f);
    st->viewport[5] = u32f(1.0f);
    st->scissor[0] = st->scissor[1] = 0;
    st->scissor[2] = (int32_t)s->width;
    st->scissor[3] = (int32_t)s->height;
    d->dirty |= D3D9D_VIEWPORT | D3D9D_SCISSOR;
    END;
}
static uint64_t m_get_rt(rc_ctx *c, uint32_t self, const uint32_t *a) {
    DEV(a);
    if (!a[2] || a[1] >= 4) DONE(D3DERR_INVALIDCALL);
    d3d9_surface *s = d->rt[a[1]];
    d3d9_wr32(a[2], s ? s->o.guest : 0);
    if (!s) DONE(D3DERR_NOTFOUND);
    d3d9_obj_addref(&s->o);
    END;
}
static uint64_t m_set_ds(rc_ctx *c, uint32_t self, const uint32_t *a) {
    DEV(a);
    d3d9_surface *s = a[1] ? surf_of(a[1]) : 0;
    if (a[1] && (!s || !s->rb)) DONE(D3DERR_INVALIDCALL);
    SETREF(d->ds, s);
    d->fbo_dirty = 1;
    d->dirty |= D3D9D_DEPTH | D3D9D_STENCIL;
    END;
}
static uint64_t m_get_ds(rc_ctx *c, uint32_t self, const uint32_t *a) {
    DEV(a);
    if (!a[1]) DONE(D3DERR_INVALIDCALL);
    d3d9_wr32(a[1], d->ds ? d->ds->o.guest : 0);
    if (!d->ds) DONE(D3DERR_NOTFOUND);
    d3d9_obj_addref(&d->ds->o);
    END;
}

static uint64_t m_begin_scene(rc_ctx *c, uint32_t self, const uint32_t *a) {
    DEV(a);
    if (d->in_scene) DONE(D3DERR_INVALIDCALL);
    d->in_scene = 1;
    END;
}
static uint64_t m_end_scene(rc_ctx *c, uint32_t self, const uint32_t *a) {
    DEV(a);
    if (!d->in_scene) DONE(D3DERR_INVALIDCALL);
    d->in_scene = 0;
    END;
}

static uint64_t m_clear(rc_ctx *c, uint32_t self, const uint32_t *a) {
    DEV(a);
    // Count, pRects, Flags, Color, Z, Stencil
    if (!d->rt[0]) DONE(D3DERR_INVALIDCALL);
    ensure_target(d);
    clear_rects(d, d->rt[0], a[1], a[2], a[3], a[4], f32(a[5]), a[6], 1);
    END;
}

static uint32_t m_get_zero(uint32_t g, uint32_t size) {
    if (g) memset(RC32_H(g), 0, size);
    return D3D_OK;
}
static uint64_t m_get_clip_status(rc_ctx *c, uint32_t self, const uint32_t *a) { (void)c; return m_get_zero(a[1], 8); }

static uint64_t m_set_viewport(rc_ctx *c, uint32_t self, const uint32_t *a) {
    DEV(a);
    if (!a[1]) DONE(D3DERR_INVALIDCALL);
    d3d9_state *s = wstate(d);
    memcpy(s->viewport, RC32_H(a[1]), 24);
    if (wmask(d)) wmask(d)->viewport = 1;
    else d->dirty |= D3D9D_VIEWPORT;
    END;
}
static uint64_t m_get_viewport(rc_ctx *c, uint32_t self, const uint32_t *a) {
    DEV(a);
    if (!a[1]) DONE(D3DERR_INVALIDCALL);
    memcpy(RC32_H(a[1]), d->st.viewport, 24);
    END;
}

static uint64_t m_set_rs(rc_ctx *c, uint32_t self, const uint32_t *a) {
    DEV(a);
    uint32_t st = a[1];
    if (st >= D3D9_RS_COUNT) DONE(D3D_OK);
    wstate(d)->rs[st] = a[2];
    if (wmask(d)) wmask(d)->rs[st] = 1;
    else d->dirty |= rs_dirty_group(st);
    if (st == RS_SRGBWRITEENABLE && a[2]) d3d9_log_once("d3d9: sRGB writes are not supported\n");
    if (st == RS_CLIPPLANEENABLE && a[2]) d3d9_log_once("d3d9: user clip planes are not supported\n");
    END;
}
static uint64_t m_get_rs(rc_ctx *c, uint32_t self, const uint32_t *a) {
    DEV(a);
    if (!a[2] || a[1] >= D3D9_RS_COUNT) DONE(D3DERR_INVALIDCALL);
    d3d9_wr32(a[2], d->st.rs[a[1]]);
    END;
}

static uint64_t m_create_sb(rc_ctx *c, uint32_t self, const uint32_t *a) {
    DEV(a);
    if (!a[2]) DONE(D3DERR_INVALIDCALL);
    uint32_t g = 0;
    hr_ = d3d9_create_stateblock(d, a[1], &g);
    d3d9_wr32(a[2], g);
    END;
}
static uint64_t m_begin_sb(rc_ctx *c, uint32_t self, const uint32_t *a) {
    DEV(a);
    if (d->recording) DONE(D3DERR_INVALIDCALL);
    d->recording = d3d9_stateblock_new(d);
    END;
}
static uint64_t m_end_sb(rc_ctx *c, uint32_t self, const uint32_t *a) {
    DEV(a);
    if (!d->recording || !a[1]) DONE(D3DERR_INVALIDCALL);
    d3d9_wr32(a[1], d->recording->o.guest);
    d->recording = 0;
    END;
}

static int sampler_index(uint32_t s) {
    if (s < 16) return (int)s;
    if (s >= 257 && s <= 260) return (int)(16 + s - 257);
    return -1;
}

static uint64_t m_get_texture(rc_ctx *c, uint32_t self, const uint32_t *a) {
    DEV(a);
    int k = sampler_index(a[1]);
    if (k < 0 || !a[2]) DONE(D3DERR_INVALIDCALL);
    d3d9_obj *o = d->st.tex[k];
    if (o) d3d9_obj_addref(o);
    d3d9_wr32(a[2], o ? o->guest : 0);
    END;
}
static uint64_t m_set_texture(rc_ctx *c, uint32_t self, const uint32_t *a) {
    DEV(a);
    int k = sampler_index(a[1]);
    if (k < 0) DONE(D3DERR_INVALIDCALL);
    d3d9_obj *o = a[2] ? d3d9_obj_get(a[2], -1) : 0;
    if (a[2] && (!o || (o->kind != D3D9K_TEXTURE && o->kind != D3D9K_CUBE))) {
        d3d9_log_once("d3d9: SetTexture with an unsupported texture object\n");
        o = 0;
    }
    SETREF(wstate(d)->tex[k], o);
    if (wmask(d)) wmask(d)->tex[k] = 1;
    END;
}
static uint64_t m_get_tss(rc_ctx *c, uint32_t self, const uint32_t *a) {
    DEV(a);
    if (!a[3] || a[1] >= 8 || a[2] >= 33) DONE(D3DERR_INVALIDCALL);
    d3d9_wr32(a[3], d->st.tss[a[1]][a[2]]);
    END;
}
static uint64_t m_set_tss(rc_ctx *c, uint32_t self, const uint32_t *a) {
    DEV(a);
    if (a[1] < 8 && a[2] < 33) {
        wstate(d)->tss[a[1]][a[2]] = a[3];
        if (wmask(d)) wmask(d)->tss[a[1]][a[2]] = 1;
    }
    END;
}
static uint64_t m_get_ss(rc_ctx *c, uint32_t self, const uint32_t *a) {
    DEV(a);
    int k = sampler_index(a[1]);
    if (k < 0 || a[2] >= D3D9_SS_COUNT || !a[3]) DONE(D3DERR_INVALIDCALL);
    d3d9_wr32(a[3], d->st.ss[k][a[2]]);
    END;
}
static uint64_t m_set_ss(rc_ctx *c, uint32_t self, const uint32_t *a) {
    DEV(a);
    int k = sampler_index(a[1]);
    if (k < 0 || a[2] >= D3D9_SS_COUNT) DONE(D3D_OK);
    wstate(d)->ss[k][a[2]] = a[3];
    if (wmask(d)) wmask(d)->ss[k][a[2]] = 1;
    END;
}
static uint64_t m_validate(rc_ctx *c, uint32_t self, const uint32_t *a) {
    (void)c;
    if (a[1]) d3d9_wr32(a[1], 1);
    return D3D_OK;
}
static uint64_t m_get_palette(rc_ctx *c, uint32_t self, const uint32_t *a) { (void)c; return m_get_zero(a[2], 1024); }
static uint64_t m_get_cur_palette(rc_ctx *c, uint32_t self, const uint32_t *a) { (void)c; if (a[1]) d3d9_wr32(a[1], 0xFFFF); return D3D_OK; }

static uint64_t m_set_scissor(rc_ctx *c, uint32_t self, const uint32_t *a) {
    DEV(a);
    if (!a[1]) DONE(D3DERR_INVALIDCALL);
    memcpy(wstate(d)->scissor, RC32_H(a[1]), 16);
    if (wmask(d)) wmask(d)->scissor = 1;
    else d->dirty |= D3D9D_SCISSOR;
    END;
}
static uint64_t m_get_scissor(rc_ctx *c, uint32_t self, const uint32_t *a) {
    DEV(a);
    if (!a[1]) DONE(D3DERR_INVALIDCALL);
    memcpy(RC32_H(a[1]), d->st.scissor, 16);
    END;
}
static uint64_t m_zero(rc_ctx *c, uint32_t self, const uint32_t *a) { (void)c; (void)a; return 0; }

static uint64_t m_draw(rc_ctx *c, uint32_t self, const uint32_t *a) {
    DEV(a);
    // PrimitiveType, StartVertex, PrimitiveCount
    GLsizei n;
    GLenum mode = gl_prim(a[1], a[3], &n);
    int inst;
    vrange vr = {(int64_t)a[2], (int64_t)a[2] + n, 0};
    if (!n || !draw_prep(d, 0, 0, &inst, vr)) DONE(D3D_OK);
    if (inst > 1) glDrawArraysInstancedARB(mode, (GLint)a[2], n, inst);
    else glDrawArrays(mode, (GLint)a[2], n);
    END;
}

static uint64_t m_draw_indexed(rc_ctx *c, uint32_t self, const uint32_t *a) {
    DEV(a);
    // PrimitiveType, BaseVertexIndex, MinVertexIndex, NumVertices, startIndex, primCount
    GLsizei n;
    GLenum mode = gl_prim(a[1], a[6], &n);
    d3d9_buffer *ib = d->st.ib;
    if (!ib) {
        d3d9_log_once("d3d9: DrawIndexedPrimitive without an index buffer skipped\n");
        DONE(D3D_OK);
    }
    int inst;
    uint32_t isz = ib->format == FMT_INDEX32 ? 4 : 2;
    // the vertices it reads, from the indices, only when a stream has bytes to upload
    vrange vr = {0, 0, 1};
    for (int k = 0; k < D3D9_MAX_STREAMS; k++)
        if (d3d9_buffer_pending(d->st.vb[k])) {
            vr = index_range(ib, a[5], (uint32_t)n, (int32_t)a[2]);
            break;
        }
    if (!n || !draw_prep(d, (int32_t)a[2], 0, &inst, vr)) DONE(D3D_OK);
    if (d3d9_buffer_pending(ib)) d3d9_buffer_sync(ib, (uint64_t)a[5] * isz, ((uint64_t)a[5] + (uint32_t)n) * isz);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ib->buf);
    const void *off = (const void *)(uintptr_t)(a[5] * isz);
    GLenum it = isz == 4 ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT;
    if (inst > 1) glDrawElementsInstancedARB(mode, n, it, off, inst);
    else glDrawElements(mode, n, it, off);
    END;
}

static uint64_t m_draw_up(rc_ctx *c, uint32_t self, const uint32_t *a) {
    DEV(a);
    // PrimitiveType, PrimitiveCount, pVertexStreamZeroData, VertexStreamZeroStride
    GLsizei n;
    GLenum mode = gl_prim(a[1], a[2], &n);
    if (!a[3] || !a[4]) DONE(D3DERR_INVALIDCALL);
    // D3D: stream 0 is unset afterwards
    SETREF(d->st.vb[0], 0);
    d->st.vb_off[0] = d->st.vb_stride[0] = 0;
    if (!n) DONE(D3D_OK);
    glBindBuffer(GL_ARRAY_BUFFER, d->up_buf);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)n * a[4], RC32_H(a[3]), GL_STREAM_DRAW);
    int inst;
    if (!draw_prep(d, 0, a[4], &inst, (vrange){0, 0, 1})) DONE(D3D_OK);
    glDrawArrays(mode, 0, n);
    END;
}

static uint64_t m_draw_indexed_up(rc_ctx *c, uint32_t self, const uint32_t *a) {
    DEV(a);
    // PrimitiveType, MinVertexIndex, NumVertices, PrimitiveCount, pIndexData, IndexDataFormat,
    // pVertexStreamZeroData, VertexStreamZeroStride
    GLsizei n;
    GLenum mode = gl_prim(a[1], a[4], &n);
    if (!a[5] || !a[7] || !a[8]) DONE(D3DERR_INVALIDCALL);
    SETREF(d->st.vb[0], 0);
    d->st.vb_off[0] = d->st.vb_stride[0] = 0;
    SETREF(d->st.ib, 0);
    if (!n) DONE(D3D_OK);
    uint32_t isz = a[6] == FMT_INDEX32 ? 4 : 2;
    glBindBuffer(GL_ARRAY_BUFFER, d->up_buf);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(a[2] + a[3]) * a[8], RC32_H(a[7]), GL_STREAM_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, d->up_ibuf);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)n * isz, RC32_H(a[5]), GL_STREAM_DRAW);
    int inst;
    if (!draw_prep(d, 0, a[8], &inst, (vrange){0, 0, 1})) DONE(D3D_OK);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, d->up_ibuf);
    glDrawElements(mode, n, isz == 4 ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT, 0);
    END;
}

static uint64_t m_create_decl(rc_ctx *c, uint32_t self, const uint32_t *a) {
    DEV(a);
    if (!a[1] || !a[2]) DONE(D3DERR_INVALIDCALL);
    uint32_t g = 0;
    hr_ = d3d9_create_decl(d, a[1], &g);
    d3d9_wr32(a[2], g);
    END;
}
static uint64_t m_set_decl(rc_ctx *c, uint32_t self, const uint32_t *a) {
    DEV(a);
    d3d9_obj *o = a[1] ? d3d9_obj_get(a[1], D3D9K_DECL) : 0;
    if (a[1] && !o) DONE(D3DERR_INVALIDCALL);
    d3d9_state *s = wstate(d);
    SETREF(s->decl, o);
    if (o) s->fvf = ((d3d9_decl *)o)->fvf;
    if (wmask(d)) wmask(d)->decl = wmask(d)->fvf = 1;
    END;
}
static uint64_t m_get_decl(rc_ctx *c, uint32_t self, const uint32_t *a) {
    DEV(a);
    if (!a[1]) DONE(D3DERR_INVALIDCALL);
    d3d9_decl *o = d->st.decl;
    if (o) d3d9_obj_addref(&o->o);
    d3d9_wr32(a[1], o ? o->o.guest : 0);
    END;
}
static uint64_t m_set_fvf(rc_ctx *c, uint32_t self, const uint32_t *a) {
    DEV(a);
    d3d9_state *s = wstate(d);
    s->fvf = a[1];
    d3d9_decl *dc = a[1] ? d3d9_decl_from_fvf(d, a[1]) : 0;
    SETREF(s->decl, dc);  // FVF declarations are cached (d3d9_decl_from_fvf)
    if (wmask(d)) wmask(d)->decl = wmask(d)->fvf = 1;
    END;
}
static uint64_t m_get_fvf(rc_ctx *c, uint32_t self, const uint32_t *a) {
    DEV(a);
    if (!a[1]) DONE(D3DERR_INVALIDCALL);
    d3d9_wr32(a[1], d->st.fvf);
    END;
}

static uint64_t m_create_vs(rc_ctx *c, uint32_t self, const uint32_t *a) {
    DEV(a);
    if (!a[1] || !a[2]) DONE(D3DERR_INVALIDCALL);
    uint32_t g = 0;
    hr_ = d3d9_create_shader(d, 0, a[1], &g);
    d3d9_wr32(a[2], g);
    END;
}
static uint64_t m_create_ps(rc_ctx *c, uint32_t self, const uint32_t *a) {
    DEV(a);
    if (!a[1] || !a[2]) DONE(D3DERR_INVALIDCALL);
    uint32_t g = 0;
    hr_ = d3d9_create_shader(d, 1, a[1], &g);
    d3d9_wr32(a[2], g);
    END;
}
static uint64_t m_set_vs(rc_ctx *c, uint32_t self, const uint32_t *a) {
    DEV(a);
    d3d9_obj *o = a[1] ? d3d9_obj_get(a[1], D3D9K_VS) : 0;
    if (a[1] && !o) DONE(D3DERR_INVALIDCALL);
    SETREF(wstate(d)->vs, o);
    if (wmask(d)) wmask(d)->vs = 1;
    END;
}
static uint64_t m_set_ps(rc_ctx *c, uint32_t self, const uint32_t *a) {
    DEV(a);
    d3d9_obj *o = a[1] ? d3d9_obj_get(a[1], D3D9K_PS) : 0;
    if (a[1] && !o) DONE(D3DERR_INVALIDCALL);
    SETREF(wstate(d)->ps, o);
    if (wmask(d)) wmask(d)->ps = 1;
    END;
}
static uint32_t get_shader(d3d9_device *d, d3d9_shader *s, uint32_t out) {
    (void)d;
    if (!out) return D3DERR_INVALIDCALL;
    if (s) d3d9_obj_addref(&s->o);
    d3d9_wr32(out, s ? s->o.guest : 0);
    return D3D_OK;
}
static uint64_t m_get_vs(rc_ctx *c, uint32_t self, const uint32_t *a) { DEV(a); hr_ = get_shader(d, d->st.vs, a[1]); END; }
static uint64_t m_get_ps(rc_ctx *c, uint32_t self, const uint32_t *a) { DEV(a); hr_ = get_shader(d, d->st.ps, a[1]); END; }

// Set*ShaderConstant{F,I,B}(StartRegister, pConstantData, Count)
static uint32_t set_constf(d3d9_device *d, int ps, const uint32_t *a) {
    uint32_t start = a[1], n = a[3], max = ps ? 224 : 256;
    if (!a[2] || start > max || n > max - start) return D3DERR_INVALIDCALL;
    d3d9_state *s = wstate(d);
    memcpy(ps ? &s->psc[start][0] : &s->vsc[start][0], RC32_H(a[2]), 16 * (size_t)n);
    if (wmask(d)) memset((ps ? wmask(d)->psc : wmask(d)->vsc) + start, 1, n);
    else if (ps) d->pc_gen++;
    else d->vc_gen++;
    return D3D_OK;
}
static uint32_t get_constf(d3d9_device *d, int ps, const uint32_t *a) {
    uint32_t start = a[1], n = a[3], max = ps ? 224 : 256;
    if (!a[2] || start > max || n > max - start) return D3DERR_INVALIDCALL;
    memcpy(RC32_H(a[2]), ps ? &d->st.psc[start][0] : &d->st.vsc[start][0], 16 * (size_t)n);
    return D3D_OK;
}
static uint32_t set_consti(d3d9_device *d, int ps, const uint32_t *a) {
    uint32_t start = a[1], n = a[3];
    if (!a[2] || start > 16 || n > 16 - start) return D3DERR_INVALIDCALL;
    d3d9_state *s = wstate(d);
    memcpy(ps ? &s->psi[start][0] : &s->vsi[start][0], RC32_H(a[2]), 16 * (size_t)n);
    if (wmask(d)) memset((ps ? wmask(d)->psi : wmask(d)->vsi) + start, 1, n);
    else d->ic_gen++;
    return D3D_OK;
}
static uint32_t get_consti(d3d9_device *d, int ps, const uint32_t *a) {
    uint32_t start = a[1], n = a[3];
    if (!a[2] || start > 16 || n > 16 - start) return D3DERR_INVALIDCALL;
    memcpy(RC32_H(a[2]), ps ? &d->st.psi[start][0] : &d->st.vsi[start][0], 16 * (size_t)n);
    return D3D_OK;
}
static uint32_t set_constb(d3d9_device *d, int ps, const uint32_t *a) {
    uint32_t start = a[1], n = a[3];
    if (!a[2] || start > 16 || n > 16 - start) return D3DERR_INVALIDCALL;
    d3d9_state *s = wstate(d);
    memcpy(ps ? &s->psb[start] : &s->vsb[start], RC32_H(a[2]), 4 * (size_t)n);
    if (wmask(d)) memset((ps ? wmask(d)->psb : wmask(d)->vsb) + start, 1, n);
    else d->ic_gen++;
    return D3D_OK;
}
static uint32_t get_constb(d3d9_device *d, int ps, const uint32_t *a) {
    uint32_t start = a[1], n = a[3];
    if (!a[2] || start > 16 || n > 16 - start) return D3DERR_INVALIDCALL;
    memcpy(RC32_H(a[2]), ps ? &d->st.psb[start] : &d->st.vsb[start], 4 * (size_t)n);
    return D3D_OK;
}
static uint64_t m_set_vsf(rc_ctx *c, uint32_t self, const uint32_t *a) { DEV(a); hr_ = set_constf(d, 0, a); END; }
static uint64_t m_get_vsf(rc_ctx *c, uint32_t self, const uint32_t *a) { DEV(a); hr_ = get_constf(d, 0, a); END; }
static uint64_t m_set_vsi(rc_ctx *c, uint32_t self, const uint32_t *a) { DEV(a); hr_ = set_consti(d, 0, a); END; }
static uint64_t m_get_vsi(rc_ctx *c, uint32_t self, const uint32_t *a) { DEV(a); hr_ = get_consti(d, 0, a); END; }
static uint64_t m_set_vsb(rc_ctx *c, uint32_t self, const uint32_t *a) { DEV(a); hr_ = set_constb(d, 0, a); END; }
static uint64_t m_get_vsb(rc_ctx *c, uint32_t self, const uint32_t *a) { DEV(a); hr_ = get_constb(d, 0, a); END; }
static uint64_t m_set_psf(rc_ctx *c, uint32_t self, const uint32_t *a) { DEV(a); hr_ = set_constf(d, 1, a); END; }
static uint64_t m_get_psf(rc_ctx *c, uint32_t self, const uint32_t *a) { DEV(a); hr_ = get_constf(d, 1, a); END; }
static uint64_t m_set_psi(rc_ctx *c, uint32_t self, const uint32_t *a) { DEV(a); hr_ = set_consti(d, 1, a); END; }
static uint64_t m_get_psi(rc_ctx *c, uint32_t self, const uint32_t *a) { DEV(a); hr_ = get_consti(d, 1, a); END; }
static uint64_t m_set_psb(rc_ctx *c, uint32_t self, const uint32_t *a) { DEV(a); hr_ = set_constb(d, 1, a); END; }
static uint64_t m_get_psb(rc_ctx *c, uint32_t self, const uint32_t *a) { DEV(a); hr_ = get_constb(d, 1, a); END; }

static uint64_t m_set_stream(rc_ctx *c, uint32_t self, const uint32_t *a) {
    DEV(a);
    // StreamNumber, pStreamData, OffsetInBytes, Stride
    if (a[1] >= D3D9_MAX_STREAMS) DONE(D3DERR_INVALIDCALL);
    d3d9_obj *o = a[2] ? d3d9_obj_get(a[2], D3D9K_VB) : 0;
    if (a[2] && !o) DONE(D3DERR_INVALIDCALL);
    d3d9_state *s = wstate(d);
    SETREF(s->vb[a[1]], o);
    s->vb_off[a[1]] = a[3];
    s->vb_stride[a[1]] = a[4];
    if (wmask(d)) wmask(d)->vb[a[1]] = 1;
    END;
}
static uint64_t m_get_stream(rc_ctx *c, uint32_t self, const uint32_t *a) {
    DEV(a);
    if (a[1] >= D3D9_MAX_STREAMS || !a[2]) DONE(D3DERR_INVALIDCALL);
    d3d9_buffer *b = d->st.vb[a[1]];
    if (b) d3d9_obj_addref(&b->o);
    d3d9_wr32(a[2], b ? b->o.guest : 0);
    if (a[3]) d3d9_wr32(a[3], d->st.vb_off[a[1]]);
    if (a[4]) d3d9_wr32(a[4], d->st.vb_stride[a[1]]);
    END;
}
static uint64_t m_set_stream_freq(rc_ctx *c, uint32_t self, const uint32_t *a) {
    DEV(a);
    if (a[1] >= D3D9_MAX_STREAMS) DONE(D3DERR_INVALIDCALL);
    wstate(d)->vb_freq[a[1]] = a[2] ? a[2] : 1;
    if (wmask(d)) wmask(d)->vb[a[1]] = 1;
    END;
}
static uint64_t m_get_stream_freq(rc_ctx *c, uint32_t self, const uint32_t *a) {
    DEV(a);
    if (a[1] >= D3D9_MAX_STREAMS || !a[2]) DONE(D3DERR_INVALIDCALL);
    d3d9_wr32(a[2], d->st.vb_freq[a[1]]);
    END;
}
static uint64_t m_set_indices(rc_ctx *c, uint32_t self, const uint32_t *a) {
    DEV(a);
    d3d9_obj *o = a[1] ? d3d9_obj_get(a[1], D3D9K_IB) : 0;
    if (a[1] && !o) DONE(D3DERR_INVALIDCALL);
    SETREF(wstate(d)->ib, o);
    if (wmask(d)) wmask(d)->ib = 1;
    END;
}
static uint64_t m_get_indices(rc_ctx *c, uint32_t self, const uint32_t *a) {
    DEV(a);
    if (!a[1]) DONE(D3DERR_INVALIDCALL);
    d3d9_buffer *b = d->st.ib;
    if (b) d3d9_obj_addref(&b->o);
    d3d9_wr32(a[1], b ? b->o.guest : 0);
    END;
}
static uint64_t m_create_query(rc_ctx *c, uint32_t self, const uint32_t *a) {
    DEV(a);
    uint32_t g = 0;
    hr_ = d3d9_create_query(d, a[1], a[2] ? &g : 0);
    if (a[2]) d3d9_wr32(a[2], g);
    END;
}

static const d3d9_slot dev_slots[119] = {
    {"IDirect3DDevice9::QueryInterface", 3, 0, m_qi},
    {"IDirect3DDevice9::AddRef", 1, 0, m_addref},
    {"IDirect3DDevice9::Release", 1, 0, m_release},
    {"IDirect3DDevice9::TestCooperativeLevel", 1, 0, m_test_coop},
    {"IDirect3DDevice9::GetAvailableTextureMem", 1, 0, m_avail_mem},
    {"IDirect3DDevice9::EvictManagedResources", 1, 0, m_ok},
    {"IDirect3DDevice9::GetDirect3D", 2, 0, m_get_d3d},
    {"IDirect3DDevice9::GetDeviceCaps", 2, 0, m_caps},
    {"IDirect3DDevice9::GetDisplayMode", 3, 0, m_display_mode},
    {"IDirect3DDevice9::GetCreationParameters", 2, 0, m_creation},
    {"IDirect3DDevice9::SetCursorProperties", 4, 0, m_ok},
    {"IDirect3DDevice9::SetCursorPosition", 4, 0, m_zero},
    {"IDirect3DDevice9::ShowCursor", 2, 0, m_show_cursor},
    {"IDirect3DDevice9::CreateAdditionalSwapChain", 3, 0},
    {"IDirect3DDevice9::GetSwapChain", 3, 0, m_get_swapchain},
    {"IDirect3DDevice9::GetNumberOfSwapChains", 1, 0, m_num_swapchains},
    {"IDirect3DDevice9::Reset", 2, 0, m_reset},
    {"IDirect3DDevice9::Present", 5, 0, m_present},
    {"IDirect3DDevice9::GetBackBuffer", 5, 0, m_get_backbuffer},
    {"IDirect3DDevice9::GetRasterStatus", 3, 0, m_raster_status},
    {"IDirect3DDevice9::SetDialogBoxMode", 2, 0, m_ok},
    {"IDirect3DDevice9::SetGammaRamp", 4, 0, m_set_gamma},
    {"IDirect3DDevice9::GetGammaRamp", 3, 0, m_get_gamma},
    {"IDirect3DDevice9::CreateTexture", 9, 0, m_create_texture},
    {"IDirect3DDevice9::CreateVolumeTexture", 10, 0},
    {"IDirect3DDevice9::CreateCubeTexture", 8, 0, m_create_cube},
    {"IDirect3DDevice9::CreateVertexBuffer", 7, 0, m_create_vb},
    {"IDirect3DDevice9::CreateIndexBuffer", 7, 0, m_create_ib},
    {"IDirect3DDevice9::CreateRenderTarget", 9, 0, m_create_rt},
    {"IDirect3DDevice9::CreateDepthStencilSurface", 9, 0, m_create_ds},
    {"IDirect3DDevice9::UpdateSurface", 5, 0, m_update_surface},
    {"IDirect3DDevice9::UpdateTexture", 3, 0, m_update_texture},
    {"IDirect3DDevice9::GetRenderTargetData", 3, 0, m_get_rt_data},
    {"IDirect3DDevice9::GetFrontBufferData", 3, 0, m_get_front_data},
    {"IDirect3DDevice9::StretchRect", 6, 0, m_stretch_rect},
    {"IDirect3DDevice9::ColorFill", 4, 0, m_color_fill},
    {"IDirect3DDevice9::CreateOffscreenPlainSurface", 7, 0, m_create_offscreen},
    {"IDirect3DDevice9::SetRenderTarget", 3, 0, m_set_rt},
    {"IDirect3DDevice9::GetRenderTarget", 3, 0, m_get_rt},
    {"IDirect3DDevice9::SetDepthStencilSurface", 2, 0, m_set_ds},
    {"IDirect3DDevice9::GetDepthStencilSurface", 2, 0, m_get_ds},
    {"IDirect3DDevice9::BeginScene", 1, 0, m_begin_scene},
    {"IDirect3DDevice9::EndScene", 1, 0, m_end_scene},
    {"IDirect3DDevice9::Clear", 7, 0, m_clear},
    {"IDirect3DDevice9::SetTransform", 3, 0, d3d9_ffp_set_transform},
    {"IDirect3DDevice9::GetTransform", 3, 0, d3d9_ffp_get_transform},
    {"IDirect3DDevice9::MultiplyTransform", 3, 0, d3d9_ffp_multiply_transform},
    {"IDirect3DDevice9::SetViewport", 2, 0, m_set_viewport},
    {"IDirect3DDevice9::GetViewport", 2, 0, m_get_viewport},
    {"IDirect3DDevice9::SetMaterial", 2, 0, d3d9_ffp_set_material},
    {"IDirect3DDevice9::GetMaterial", 2, 0, d3d9_ffp_get_material},
    {"IDirect3DDevice9::SetLight", 3, 0, d3d9_ffp_set_light},
    {"IDirect3DDevice9::GetLight", 3, 0, d3d9_ffp_get_light},
    {"IDirect3DDevice9::LightEnable", 3, 0, d3d9_ffp_light_enable},
    {"IDirect3DDevice9::GetLightEnable", 3, 0, d3d9_ffp_get_light_enable},
    {"IDirect3DDevice9::SetClipPlane", 3, 0, d3d9_ffp_set_clip_plane},
    {"IDirect3DDevice9::GetClipPlane", 3, 0, d3d9_ffp_get_clip_plane},
    {"IDirect3DDevice9::SetRenderState", 3, 0, m_set_rs},
    {"IDirect3DDevice9::GetRenderState", 3, 0, m_get_rs},
    {"IDirect3DDevice9::CreateStateBlock", 3, 0, m_create_sb},
    {"IDirect3DDevice9::BeginStateBlock", 1, 0, m_begin_sb},
    {"IDirect3DDevice9::EndStateBlock", 2, 0, m_end_sb},
    {"IDirect3DDevice9::SetClipStatus", 2, 0, m_ok},
    {"IDirect3DDevice9::GetClipStatus", 2, 0, m_get_clip_status},
    {"IDirect3DDevice9::GetTexture", 3, 0, m_get_texture},
    {"IDirect3DDevice9::SetTexture", 3, 0, m_set_texture},
    {"IDirect3DDevice9::GetTextureStageState", 4, 0, m_get_tss},
    {"IDirect3DDevice9::SetTextureStageState", 4, 0, m_set_tss},
    {"IDirect3DDevice9::GetSamplerState", 4, 0, m_get_ss},
    {"IDirect3DDevice9::SetSamplerState", 4, 0, m_set_ss},
    {"IDirect3DDevice9::ValidateDevice", 2, 0, m_validate},
    {"IDirect3DDevice9::SetPaletteEntries", 3, 0, m_ok},
    {"IDirect3DDevice9::GetPaletteEntries", 3, 0, m_get_palette},
    {"IDirect3DDevice9::SetCurrentTexturePalette", 2, 0, m_ok},
    {"IDirect3DDevice9::GetCurrentTexturePalette", 2, 0, m_get_cur_palette},
    {"IDirect3DDevice9::SetScissorRect", 2, 0, m_set_scissor},
    {"IDirect3DDevice9::GetScissorRect", 2, 0, m_get_scissor},
    {"IDirect3DDevice9::SetSoftwareVertexProcessing", 2, 0, m_ok},
    {"IDirect3DDevice9::GetSoftwareVertexProcessing", 1, 0, m_zero},
    {"IDirect3DDevice9::SetNPatchMode", 2, 0, m_ok},
    {"IDirect3DDevice9::GetNPatchMode", 1, 0, m_zero},
    {"IDirect3DDevice9::DrawPrimitive", 4, 0, m_draw},
    {"IDirect3DDevice9::DrawIndexedPrimitive", 7, 0, m_draw_indexed},
    {"IDirect3DDevice9::DrawPrimitiveUP", 5, 0, m_draw_up},
    {"IDirect3DDevice9::DrawIndexedPrimitiveUP", 9, 0, m_draw_indexed_up},
    {"IDirect3DDevice9::ProcessVertices", 7, 0},
    {"IDirect3DDevice9::CreateVertexDeclaration", 3, 0, m_create_decl},
    {"IDirect3DDevice9::SetVertexDeclaration", 2, 0, m_set_decl},
    {"IDirect3DDevice9::GetVertexDeclaration", 2, 0, m_get_decl},
    {"IDirect3DDevice9::SetFVF", 2, 0, m_set_fvf},
    {"IDirect3DDevice9::GetFVF", 2, 0, m_get_fvf},
    {"IDirect3DDevice9::CreateVertexShader", 3, 0, m_create_vs},
    {"IDirect3DDevice9::SetVertexShader", 2, 0, m_set_vs},
    {"IDirect3DDevice9::GetVertexShader", 2, 0, m_get_vs},
    {"IDirect3DDevice9::SetVertexShaderConstantF", 4, 0, m_set_vsf},
    {"IDirect3DDevice9::GetVertexShaderConstantF", 4, 0, m_get_vsf},
    {"IDirect3DDevice9::SetVertexShaderConstantI", 4, 0, m_set_vsi},
    {"IDirect3DDevice9::GetVertexShaderConstantI", 4, 0, m_get_vsi},
    {"IDirect3DDevice9::SetVertexShaderConstantB", 4, 0, m_set_vsb},
    {"IDirect3DDevice9::GetVertexShaderConstantB", 4, 0, m_get_vsb},
    {"IDirect3DDevice9::SetStreamSource", 5, 0, m_set_stream},
    {"IDirect3DDevice9::GetStreamSource", 5, 0, m_get_stream},
    {"IDirect3DDevice9::SetStreamSourceFreq", 3, 0, m_set_stream_freq},
    {"IDirect3DDevice9::GetStreamSourceFreq", 3, 0, m_get_stream_freq},
    {"IDirect3DDevice9::SetIndices", 2, 0, m_set_indices},
    {"IDirect3DDevice9::GetIndices", 2, 0, m_get_indices},
    {"IDirect3DDevice9::CreatePixelShader", 3, 0, m_create_ps},
    {"IDirect3DDevice9::SetPixelShader", 2, 0, m_set_ps},
    {"IDirect3DDevice9::GetPixelShader", 2, 0, m_get_ps},
    {"IDirect3DDevice9::SetPixelShaderConstantF", 4, 0, m_set_psf},
    {"IDirect3DDevice9::GetPixelShaderConstantF", 4, 0, m_get_psf},
    {"IDirect3DDevice9::SetPixelShaderConstantI", 4, 0, m_set_psi},
    {"IDirect3DDevice9::GetPixelShaderConstantI", 4, 0, m_get_psi},
    {"IDirect3DDevice9::SetPixelShaderConstantB", 4, 0, m_set_psb},
    {"IDirect3DDevice9::GetPixelShaderConstantB", 4, 0, m_get_psb},
    {"IDirect3DDevice9::DrawRectPatch", 4, 0},
    {"IDirect3DDevice9::DrawTriPatch", 4, 0},
    {"IDirect3DDevice9::DeletePatch", 2, 0},
    {"IDirect3DDevice9::CreateQuery", 3, 0, m_create_query},
};

static void dev_destroy(d3d9_obj *o) {
    d3d9_device *d = (d3d9_device *)o;
    // resources outlive the device in D3D only as dead objects; the GL context stays (cheap,
    // and objects created on it may still be released later)
    d3d9_obj *d3d = d3d9_obj_get(d->d3d, D3D9K_D3D);
    if (d3d) d3d9_obj_release(d3d);
    fprintf(stderr, "d3d9: device released\n");
}

uint32_t d3d9_device_create(uint32_t d3d, const uint32_t *a, uint32_t pp_guest, uint32_t *out) {
    d3d9_device *d = calloc(1, sizeof *d);
    d->o.kind = D3D9K_DEVICE;
    d->o.destroy = dev_destroy;
    pthread_mutexattr_t at;
    pthread_mutexattr_init(&at);
    pthread_mutexattr_settype(&at, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&d->mu, &at);
    pthread_mutexattr_destroy(&at);
    d->d3d = d3d;
    d->adapter = a[1];
    d->devtype = a[2];
    d->focus = a[3];
    d->behavior = a[4];
    read_pp(d, pp_guest);
    uint32_t hwnd = d->pp[PP_HWND] ? d->pp[PP_HWND] : d->focus;
    d3d9_platform_windowed(d->pp[PP_WINDOWED] != 0);
    d->ctx = d3d9_platform_context(hwnd, (int)d->pp[PP_WIDTH], (int)d->pp[PP_HEIGHT]);
    if (!d->ctx) {
        fprintf(stderr, "d3d9: CreateDevice: no GL context\n");
        free(d);
        return D3DERR_NOTAVAILABLE;
    }
    uint32_t vt = d3d9_vtable(dev_slots, 119);
    if (!vt || !d3d9_obj_publish(&d->o, vt)) {
        free(d);
        return E_OUTOFMEMORY_;
    }
    d3d9_obj *d3o = d3d9_obj_get(d3d, D3D9K_D3D);
    if (d3o) d3d9_obj_addref(d3o);
    d3d9_dev_lock(d);
    glGetError();
    glGenFramebuffers(1, &d->fbo);
    glGenBuffers(1, &d->up_buf);
    glGenBuffers(1, &d->up_ibuf);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    create_backbuffer(d);
    d3d9_state_defaults(d, &d->st);
    d->dirty = D3D9D_ALL;
    d->fbo_dirty = 1;
    d->next_shader_id = 1;
    d3d9_dev_unlock(d);
    fprintf(stderr, "d3d9: device %ux%u format %u%s, window 0x%x, %s\n", d->pp[PP_WIDTH], d->pp[PP_HEIGHT], d->pp[PP_FORMAT],
            d->pp[PP_AUTODS] ? " + depth stencil" : "", hwnd, d->pp[PP_WINDOWED] ? "windowed" : "fullscreen");
    *out = d->o.guest;
    return D3D_OK;
}

// --- state blocks: copy the masked parts between states (holding references) --------------
void d3d9_state_copy(d3d9_state *dst, const d3d9_state *src, const d3d9_state_mask *m) {
    for (int k = 0; k < D3D9_RS_COUNT; k++) if (m->rs[k]) dst->rs[k] = src->rs[k];
    for (int s = 0; s < D3D9_MAX_SAMPLERS; s++) {
        for (int k = 0; k < D3D9_SS_COUNT; k++) if (m->ss[s][k]) dst->ss[s][k] = src->ss[s][k];
        if (m->tex[s]) SETREF(dst->tex[s], src->tex[s]);
    }
    if (m->vs) SETREF(dst->vs, src->vs);
    if (m->ps) SETREF(dst->ps, src->ps);
    if (m->decl) SETREF(dst->decl, src->decl);
    if (m->fvf) dst->fvf = src->fvf;
    if (m->ib) SETREF(dst->ib, src->ib);
    for (int k = 0; k < D3D9_MAX_STREAMS; k++)
        if (m->vb[k]) {
            SETREF(dst->vb[k], src->vb[k]);
            dst->vb_off[k] = src->vb_off[k];
            dst->vb_stride[k] = src->vb_stride[k];
            dst->vb_freq[k] = src->vb_freq[k];
        }
    for (int k = 0; k < 256; k++) if (m->vsc[k]) memcpy(dst->vsc[k], src->vsc[k], 16);
    for (int k = 0; k < 224; k++) if (m->psc[k]) memcpy(dst->psc[k], src->psc[k], 16);
    for (int k = 0; k < 16; k++) {
        if (m->vsi[k]) memcpy(dst->vsi[k], src->vsi[k], 16);
        if (m->psi[k]) memcpy(dst->psi[k], src->psi[k], 16);
        if (m->vsb[k]) dst->vsb[k] = src->vsb[k];
        if (m->psb[k]) dst->psb[k] = src->psb[k];
    }
    if (m->viewport) memcpy(dst->viewport, src->viewport, sizeof dst->viewport);
    if (m->scissor) memcpy(dst->scissor, src->scissor, sizeof dst->scissor);
    for (int s = 0; s < 8; s++)
        for (int k = 0; k < 33; k++) if (m->tss[s][k]) dst->tss[s][k] = src->tss[s][k];
    for (int k = 0; k < D3D9_XF_COUNT; k++) if (m->xf[k]) memcpy(dst->xf[k], src->xf[k], sizeof dst->xf[k]);
    if (m->mat) memcpy(dst->mat, src->mat, sizeof dst->mat);
    for (int k = 0; k < D3D9_MAX_LIGHTS; k++)
        if (m->light[k]) {
            memcpy(dst->light[k], src->light[k], sizeof dst->light[k]);
            dst->light_set[k] = src->light_set[k];
            dst->light_on[k] = src->light_on[k];
        }
    for (int k = 0; k < 6; k++) if (m->clip[k]) memcpy(dst->clip[k], src->clip[k], sizeof dst->clip[k]);
}

void d3d9_state_release(d3d9_state *s) {
    for (int k = 0; k < D3D9_MAX_SAMPLERS; k++) SETREF(s->tex[k], 0);
    for (int k = 0; k < D3D9_MAX_STREAMS; k++) SETREF(s->vb[k], 0);
    SETREF(s->ib, 0);
    SETREF(s->vs, 0);
    SETREF(s->ps, 0);
    SETREF(s->decl, 0);
}

void d3d9_dev_state_changed(d3d9_device *d) {
    d->dirty = D3D9D_ALL;
    d->vc_gen++;
    d->pc_gen++;
    d->ic_gen++;
}
