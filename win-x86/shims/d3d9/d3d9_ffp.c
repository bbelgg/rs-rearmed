// The Direct3D 9 fixed-function pipeline on GLSL (docs/windows/DESIGN.md "Direct3D 9").
//
// Transforms, the material, lights, clip planes and the texture stage states are device state
// (d3d9_state, captured by state blocks). A draw with no vertex shader, no pixel shader or
// neither gets a program generated from that state: the missing stage becomes GLSL like the
// translator's (d3d9_shader.h: attributes rs_vN, varyings by semantic, the rs_fix epilogue), so
// it links with a translated shader for the other stage. Programs are cached by a key of
// everything the generated code depends on; the values (matrices, colours, light parameters)
// are uniforms recomputed per draw and uploaded when they change:
//   rs_ffv[FFV_COUNT]   vertex stage (layout FFV_* below)
//   rs_ffp[FFP_COUNT]   pixel stage (FFP_*)
//
// Implemented: XYZ / XYZW / XYZRHW positions, world (0) / view / projection, per-vertex lighting
// with directional, point and spot lights and the material sources, texture coordinate index and
// generation (camera-space normal / position / reflection vector), texture transforms (count and
// projected), texture stages with every operation but the bump-map and premodulate ones,
// specular add, and fog (vertex and table modes, linear / exp / exp2; table fog uses eye depth
// per vertex). Not implemented, logged once: vertex blending, sphere-map generation, the
// bump-map operations, user clip planes, point sprites.
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "d3d9_internal.h"

static inline float f32(uint32_t u) { return d3d9_f32(u); }

// --- vertex uniform layout (vec4s) -----------------------------------------------------------
enum {
    FFV_WVP = 0,        // columns of world * view * projection
    FFV_WV = 4,         // columns of world * view (eye-space position)
    FFV_NRM = 8,        // rows of (world * view)^-1, 3x3: eye-space normal (inverse transpose)
    FFV_VIEWPORT = 11,  // X Y W H (pre-transformed positions)
    FFV_MAT = 12,       // material diffuse, ambient, specular, emissive, (power)
    FFV_AMBIENT = 17,   // RS_AMBIENT
    FFV_LIGHT = 18,     // 8 x: diffuse, specular, ambient, eye position (w range), eye direction
                        // towards the light, attenuation (a0 a1 a2 falloff), spot (cos theta/2, cos phi/2)
    FFV_FOG = 74,       // start, end, density, 1 / (end - start)
    FFV_TEXM = 75,      // 8 x columns of the texture matrix
    FFV_COUNT = 107,
};
enum { FFP_TFACTOR = 0, FFP_FOGCOLOR = 1, FFP_FOG = 2, FFP_CONST = 3, FFP_COUNT = 11 };
enum { LIGHT_VEC4S = 7 };

// varyings (need / avail masks)
enum { VY_COLOR0 = 1, VY_COLOR1 = 2, VY_TEX0 = 4, VY_FOG = 1 << 10 };

// --- the state key ---------------------------------------------------------------------------
typedef struct {
    uint8_t tci, gen, count, proj;  // texture coordinate set, generation, transform count, projected
} ffp_tc;

typedef struct {
    uint8_t cop, ca0, ca1, ca2, aop, aa0, aa1, aa2, result, tex;  // tex: 0 none, 2 2D, 3 cube
} ffp_stage;

typedef struct {
    uint32_t vs_id, ps_id;  // the bound shaders (0: that stage is fixed function)
    // fixed-function vertex stage
    uint8_t pos_t;          // pre-transformed (XYZRHW)
    uint8_t normal, color0, color1;
    uint8_t tc_dims[8];     // texture coordinate sets in the declaration: components, 0 absent
    uint8_t lighting, nlights, light_type[8];
    uint8_t mat_src[4];     // diffuse, ambient, specular, emissive: 0 material, 1 colour 0, 2 colour 1
    uint8_t specular, localviewer, normalize;
    uint8_t range_fog;
    ffp_tc tc[8];           // per output texture coordinate
    uint16_t need;          // varyings the pixel stage reads (VY_*)
    // fog: 0 none, 1 factor in rs_fog0.x (vertex fog, or specular alpha), 2..4 table linear / exp /
    // exp2 from the distance in rs_fog0.x
    uint8_t fog;
    uint8_t vfog;           // vertex fog mode for the factor (1 exp, 2 exp2, 3 linear; 0: specular alpha)
    // fixed-function pixel stage
    uint8_t nstages;
    ffp_stage st[8];
} ffp_key;

typedef struct ffp_entry {
    ffp_key key;
    d3d9_program p;
    struct ffp_entry *next;
} ffp_entry;

// --- matrices (row-major, D3D's row vectors) ---------------------------------------------------
static void mat_mul(float *o, const float *a, const float *b) {
    float t[16];
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            t[4 * r + c] = a[4 * r] * b[c] + a[4 * r + 1] * b[4 + c] + a[4 * r + 2] * b[8 + c] + a[4 * r + 3] * b[12 + c];
    memcpy(o, t, sizeof t);
}
static void mat_columns(float *dst, const float *m) {
    for (int c = 0; c < 4; c++)
        for (int r = 0; r < 4; r++) dst[4 * c + r] = m[4 * r + c];
}
static const float kIdentity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

void d3d9_ffp_defaults(d3d9_state *s) {
    for (int k = 0; k < D3D9_XF_COUNT; k++) memcpy(s->xf[k], kIdentity, sizeof kIdentity);
    memset(s->mat, 0, sizeof s->mat);
    memset(s->light, 0, sizeof s->light);
    memset(s->light_set, 0, sizeof s->light_set);
    memset(s->light_on, 0, sizeof s->light_on);
    memset(s->clip, 0, sizeof s->clip);
}

// --- methods ----------------------------------------------------------------------------------
static d3d9_device *lock_dev(uint32_t g) {
    d3d9_device *d = (d3d9_device *)d3d9_obj_get(g, D3D9K_DEVICE);
    if (d) d3d9_dev_lock(d);
    return d;
}
static d3d9_state *wstate(d3d9_device *d) { return d->recording ? &d->recording->s : &d->st; }
static d3d9_state_mask *wmask(d3d9_device *d) { return d->recording ? &d->recording->m : 0; }

static int xf_index(uint32_t ts) {
    if (ts == TS_VIEW) return D3D9_XF_VIEW;
    if (ts == TS_PROJECTION) return D3D9_XF_PROJ;
    if (ts >= TS_TEXTURE0 && ts < TS_TEXTURE0 + 8) return D3D9_XF_TEX0 + (int)(ts - TS_TEXTURE0);
    if (ts >= TS_WORLD && ts < TS_WORLD + 4) return D3D9_XF_WORLD + (int)(ts - TS_WORLD);
    d3d9_log_once("d3d9: transform state %u not supported\n", ts);
    return -1;
}

uint64_t d3d9_ffp_set_transform(rc_ctx *c, uint32_t self, const uint32_t *a) {
    (void)c;
    d3d9_device *d = lock_dev(a[0]);
    if (!d) return D3DERR_INVALIDCALL;
    int k = xf_index(a[1]);
    uint32_t hr = D3D_OK;
    if (!a[2]) hr = D3DERR_INVALIDCALL;
    else if (k >= 0) {
        memcpy(wstate(d)->xf[k], RC32_H(a[2]), 64);
        if (wmask(d)) wmask(d)->xf[k] = 1;
    }
    d3d9_dev_unlock(d);
    return hr;
}
uint64_t d3d9_ffp_get_transform(rc_ctx *c, uint32_t self, const uint32_t *a) {
    (void)c;
    d3d9_device *d = lock_dev(a[0]);
    if (!d) return D3DERR_INVALIDCALL;
    int k = xf_index(a[1]);
    uint32_t hr = D3D_OK;
    if (!a[2]) hr = D3DERR_INVALIDCALL;
    else memcpy(RC32_H(a[2]), k >= 0 ? d->st.xf[k] : kIdentity, 64);
    d3d9_dev_unlock(d);
    return hr;
}
uint64_t d3d9_ffp_multiply_transform(rc_ctx *c, uint32_t self, const uint32_t *a) {
    (void)c;
    d3d9_device *d = lock_dev(a[0]);
    if (!d) return D3DERR_INVALIDCALL;
    int k = xf_index(a[1]);
    uint32_t hr = D3D_OK;
    if (!a[2]) hr = D3DERR_INVALIDCALL;
    else if (k >= 0) {
        // the given matrix times the device's current one, then set like SetTransform (recorded)
        float m[16];
        memcpy(m, RC32_H(a[2]), 64);
        mat_mul(wstate(d)->xf[k], m, d->st.xf[k]);
        if (wmask(d)) wmask(d)->xf[k] = 1;
    }
    d3d9_dev_unlock(d);
    return hr;
}
uint64_t d3d9_ffp_set_material(rc_ctx *c, uint32_t self, const uint32_t *a) {
    (void)c;
    d3d9_device *d = lock_dev(a[0]);
    if (!d) return D3DERR_INVALIDCALL;
    uint32_t hr = D3D_OK;
    if (!a[1]) hr = D3DERR_INVALIDCALL;
    else {
        memcpy(wstate(d)->mat, RC32_H(a[1]), 68);
        if (wmask(d)) wmask(d)->mat = 1;
    }
    d3d9_dev_unlock(d);
    return hr;
}
uint64_t d3d9_ffp_get_material(rc_ctx *c, uint32_t self, const uint32_t *a) {
    (void)c;
    d3d9_device *d = lock_dev(a[0]);
    if (!d) return D3DERR_INVALIDCALL;
    uint32_t hr = D3D_OK;
    if (!a[1]) hr = D3DERR_INVALIDCALL;
    else memcpy(RC32_H(a[1]), d->st.mat, 68);
    d3d9_dev_unlock(d);
    return hr;
}
static int light_index(uint32_t i) {
    if (i < D3D9_MAX_LIGHTS) return (int)i;
    d3d9_log_once("d3d9: light index %u above %d not supported\n", i, D3D9_MAX_LIGHTS - 1);
    return -1;
}
uint64_t d3d9_ffp_set_light(rc_ctx *c, uint32_t self, const uint32_t *a) {
    (void)c;
    d3d9_device *d = lock_dev(a[0]);
    if (!d) return D3DERR_INVALIDCALL;
    int k = light_index(a[1]);
    uint32_t hr = D3D_OK;
    if (!a[2]) hr = D3DERR_INVALIDCALL;
    else if (k >= 0) {
        d3d9_state *s = wstate(d);
        memcpy(s->light[k], RC32_H(a[2]), 104);
        s->light_set[k] = 1;
        if (wmask(d)) wmask(d)->light[k] = 1;
    }
    d3d9_dev_unlock(d);
    return hr;
}
uint64_t d3d9_ffp_get_light(rc_ctx *c, uint32_t self, const uint32_t *a) {
    (void)c;
    d3d9_device *d = lock_dev(a[0]);
    if (!d) return D3DERR_INVALIDCALL;
    int k = a[1] < D3D9_MAX_LIGHTS ? (int)a[1] : -1;
    uint32_t hr = D3D_OK;
    if (!a[2] || k < 0 || !d->st.light_set[k]) hr = D3DERR_INVALIDCALL;
    else memcpy(RC32_H(a[2]), d->st.light[k], 104);
    d3d9_dev_unlock(d);
    return hr;
}
uint64_t d3d9_ffp_light_enable(rc_ctx *c, uint32_t self, const uint32_t *a) {
    (void)c;
    d3d9_device *d = lock_dev(a[0]);
    if (!d) return D3DERR_INVALIDCALL;
    int k = light_index(a[1]);
    if (k >= 0) {
        d3d9_state *s = wstate(d);
        if (!s->light_set[k]) {
            // enabling a light never set gives the default one: white, directional, along +z
            memset(s->light[k], 0, sizeof s->light[k]);
            float def[] = {1, 1, 1, 0};
            s->light[k][0] = LIGHT_DIRECTIONAL;
            memcpy(&s->light[k][1], def, 16);
            float dir = 1.0f;
            memcpy(&s->light[k][18], &dir, 4);
            s->light_set[k] = 1;
        }
        s->light_on[k] = a[2] != 0;
        if (wmask(d)) wmask(d)->light[k] = 1;
    }
    d3d9_dev_unlock(d);
    return D3D_OK;
}
uint64_t d3d9_ffp_get_light_enable(rc_ctx *c, uint32_t self, const uint32_t *a) {
    (void)c;
    d3d9_device *d = lock_dev(a[0]);
    if (!d) return D3DERR_INVALIDCALL;
    int k = a[1] < D3D9_MAX_LIGHTS ? (int)a[1] : -1;
    uint32_t hr = D3D_OK;
    if (!a[2] || k < 0 || !d->st.light_set[k]) hr = D3DERR_INVALIDCALL;
    else d3d9_wr32(a[2], d->st.light_on[k] ? 128 : 0);  // D3D9 answers 128 for TRUE
    d3d9_dev_unlock(d);
    return hr;
}
uint64_t d3d9_ffp_set_clip_plane(rc_ctx *c, uint32_t self, const uint32_t *a) {
    (void)c;
    d3d9_device *d = lock_dev(a[0]);
    if (!d) return D3DERR_INVALIDCALL;
    uint32_t hr = D3D_OK;
    if (!a[2] || a[1] >= 6) hr = D3DERR_INVALIDCALL;
    else {
        memcpy(wstate(d)->clip[a[1]], RC32_H(a[2]), 16);
        if (wmask(d)) wmask(d)->clip[a[1]] = 1;
    }
    d3d9_dev_unlock(d);
    return hr;
}
uint64_t d3d9_ffp_get_clip_plane(rc_ctx *c, uint32_t self, const uint32_t *a) {
    (void)c;
    d3d9_device *d = lock_dev(a[0]);
    if (!d) return D3DERR_INVALIDCALL;
    uint32_t hr = D3D_OK;
    if (!a[2] || a[1] >= 6) hr = D3DERR_INVALIDCALL;
    else memcpy(RC32_H(a[2]), d->st.clip[a[1]], 16);
    d3d9_dev_unlock(d);
    return hr;
}

// --- key ---------------------------------------------------------------------------------------
static int velem_dims(uint8_t type) {
    switch (type) {
    case DT_FLOAT1: return 1;
    case DT_FLOAT2: case DT_SHORT2: case DT_SHORT2N: case DT_USHORT2N: case DT_FLOAT16_2: return 2;
    case DT_FLOAT3: case DT_UDEC3: case DT_DEC3N: return 3;
    }
    return 4;
}

static int stage_uses(const uint32_t *t, int arg) {
    int c = t[TSS_COLOROP], al = t[TSS_ALPHAOP];
    int three_c = c == TOP_MULTIPLYADD || c == TOP_LERP, three_a = al == TOP_MULTIPLYADD || al == TOP_LERP;
    if ((t[TSS_COLORARG1] & 15) == (uint32_t)arg || (t[TSS_COLORARG2] & 15) == (uint32_t)arg) return 1;
    if (three_c && (t[TSS_COLORARG0] & 15) == (uint32_t)arg) return 1;
    if (al != TOP_DISABLE) {
        if ((t[TSS_ALPHAARG1] & 15) == (uint32_t)arg || (t[TSS_ALPHAARG2] & 15) == (uint32_t)arg) return 1;
        if (three_a && (t[TSS_ALPHAARG0] & 15) == (uint32_t)arg) return 1;
    }
    if (arg == TA_TEXTURE) {
        int ops[] = {TOP_BLENDTEXTUREALPHA, TOP_BLENDTEXTUREALPHAPM};
        for (int k = 0; k < 2; k++) if (c == ops[k] || al == ops[k]) return 1;
    }
    if (arg == TA_DIFFUSE && (c == TOP_BLENDDIFFUSEALPHA || al == TOP_BLENDDIFFUSEALPHA)) return 1;
    return 0;
}

static int vy_of(int usage, int index) {
    if (usage == DU_COLOR && index < 2) return index ? VY_COLOR1 : VY_COLOR0;
    if (usage == DU_TEXCOORD && index < 8) return VY_TEX0 << index;
    if (usage == DU_FOG && index == 0) return VY_FOG;
    return 0;
}

static void build_key(d3d9_device *d, d3d9_decl *decl, ffp_key *k) {
    const d3d9_state *s = &d->st;
    const uint32_t *r = s->rs;
    memset(k, 0, sizeof *k);
    k->vs_id = s->vs ? s->vs->id : 0;
    k->ps_id = s->ps ? s->ps->id : 0;
    int fogon = r[RS_FOGENABLE] != 0;
    // the pixel stage: what it reads
    if (s->ps) {
        d3d9_sm_input in[32];
        int n = d3d9_sm_ps_semantics(s->ps->sm, in, 32);
        for (int j = 0; j < n; j++) k->need |= (uint16_t)vy_of(in[j].usage, in[j].index);
    } else {
        k->need |= VY_COLOR0;
        int spec = r[RS_SPECULARENABLE] != 0;
        for (int st = 0; st < 8; st++) {
            const uint32_t *t = s->tss[st];
            if (t[TSS_COLOROP] == TOP_DISABLE) break;
            ffp_stage *e = &k->st[st];
            e->cop = (uint8_t)t[TSS_COLOROP];
            e->ca1 = (uint8_t)t[TSS_COLORARG1];
            e->ca2 = (uint8_t)t[TSS_COLORARG2];
            e->ca0 = (uint8_t)t[TSS_COLORARG0];
            e->aop = (uint8_t)t[TSS_ALPHAOP];
            e->aa1 = (uint8_t)t[TSS_ALPHAARG1];
            e->aa2 = (uint8_t)t[TSS_ALPHAARG2];
            e->aa0 = (uint8_t)t[TSS_ALPHAARG0];
            e->result = (uint8_t)t[TSS_RESULTARG];
            if (stage_uses(t, TA_TEXTURE)) {
                const d3d9_obj *o = s->tex[st];
                if (o && o->kind == D3D9K_TEXTURE) e->tex = 2;
                else if (o && o->kind == D3D9K_CUBE) e->tex = 3;
                if (e->tex) k->need |= (uint16_t)(VY_TEX0 << st);
            }
            if (stage_uses(t, TA_SPECULAR)) spec = 1;
            k->nstages = (uint8_t)(st + 1);
        }
        if (spec) k->need |= VY_COLOR1;
        k->specular = r[RS_SPECULARENABLE] != 0;
        if (fogon) k->need |= VY_FOG;
    }
    // the vertex stage
    if (!s->vs) {
        for (int j = 0; j < decl->n; j++) {
            const d3d9_velem *e = &decl->e[j];
            if (e->usage == DU_POSITIONT && e->index == 0) k->pos_t = 1;
            else if (e->usage == DU_NORMAL && e->index == 0) k->normal = 1;
            else if (e->usage == DU_COLOR && e->index == 0) k->color0 = 1;
            else if (e->usage == DU_COLOR && e->index == 1) k->color1 = 1;
            else if (e->usage == DU_TEXCOORD && e->index < 8) k->tc_dims[e->index] = (uint8_t)velem_dims(e->type);
            else if (e->usage == DU_BLENDWEIGHT && r[RS_VERTEXBLEND])
                d3d9_log_once("d3d9: fixed-function vertex blending is not supported (world 0 used)\n");
        }
        if (!k->pos_t && r[RS_LIGHTING]) {
            k->lighting = 1;
            for (int j = 0; j < D3D9_MAX_LIGHTS && k->nlights < 8; j++)
                if (s->light_on[j]) k->light_type[k->nlights++] = (uint8_t)s->light[j][0];
            int cv = r[RS_COLORVERTEX] != 0;
            uint32_t src[4] = {r[RS_DIFFUSEMATERIALSOURCE], r[RS_AMBIENTMATERIALSOURCE], r[RS_SPECULARMATERIALSOURCE],
                               r[RS_EMISSIVEMATERIALSOURCE]};
            for (int j = 0; j < 4; j++) {
                uint8_t m = cv && src[j] <= 2 ? (uint8_t)src[j] : 0;
                if ((m == 1 && !k->color0) || (m == 2 && !k->color1)) m = 0;
                k->mat_src[j] = m;
            }
            k->specular = r[RS_SPECULARENABLE] != 0;
            k->localviewer = r[RS_LOCALVIEWER] != 0;
            k->normalize = r[RS_NORMALIZENORMALS] != 0;
        }
        for (int st = 0; st < 8; st++) {
            if (!(k->need & (VY_TEX0 << st))) continue;
            const uint32_t *t = s->tss[st];
            ffp_tc *tc = &k->tc[st];
            tc->tci = (uint8_t)(t[TSS_TEXCOORDINDEX] & 7);
            tc->gen = (uint8_t)((t[TSS_TEXCOORDINDEX] >> 16) & 15);
            uint32_t ttff = t[TSS_TEXTURETRANSFORMFLAGS];
            tc->count = (uint8_t)(ttff & 7);
            tc->proj = (ttff & 256) != 0 && !s->ps;
            if (tc->gen >= 4) {
                d3d9_log_once("d3d9: texture coordinate generation %u not supported\n", tc->gen);
                tc->gen = 0;
            }
        }
    }
    if (!s->ps && fogon) {
        uint32_t table = r[RS_FOGTABLEMODE], vmode = r[RS_FOGVERTEXMODE];
        if (table >= 1 && table <= 3) {
            if (s->vs) d3d9_log_once("d3d9: table fog with a vertex shader is not supported (no fog)\n");
            else k->fog = (uint8_t)(table == 3 ? 2 : table == 1 ? 3 : 4);
        } else {
            k->fog = 1;
            if (!s->vs) k->vfog = (uint8_t)(vmode <= 3 ? vmode : 0);
        }
        if (!s->vs) k->range_fog = r[RS_RANGEFOGENABLE] != 0;
    }
    if (r[RS_CLIPPLANEENABLE]) d3d9_log_once("d3d9: user clip planes are not supported\n");
}

// --- GLSL ---------------------------------------------------------------------------------------
typedef struct {
    char *s;
    size_t n, cap;
} sbuf;

static void sb_add(sbuf *b, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void sb_add(sbuf *b, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char tmp[1024];
    int n = vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if ((size_t)n >= sizeof tmp) n = sizeof tmp - 1;
    if (b->n + (size_t)n + 1 > b->cap) {
        b->cap = (b->n + (size_t)n + 1) * 2;
        b->s = realloc(b->s, b->cap);
    }
    memcpy(b->s + b->n, tmp, (size_t)n + 1);
    b->n += (size_t)n;
}

// FF vertex attributes: the register each input semantic is bound to
enum { A_POS = 0, A_NORMAL = 1, A_COLOR0 = 2, A_COLOR1 = 3, A_TEX0 = 4 };

static void emit_epilogue(sbuf *o) {
    sb_add(o, "  gl_Position = pos;\n");
    sb_add(o, "  gl_Position.x = pos.x + rs_fix.x * pos.w;\n");
    sb_add(o, "  gl_Position.y = rs_fix.y * pos.w - pos.y;\n");
    sb_add(o, "  gl_Position.z = pos.z * rs_fix.z + rs_fix.w * pos.w;\n");
}

// The fixed-function vertex stage writing the varyings in `pin` (semantics) / k->need.
static char *ffp_vs(const ffp_key *k, const d3d9_sm_input *pin, int npin) {
    sbuf o = {0};
    char nm[48];
    sb_add(&o, "#version 120\nuniform vec4 rs_fix;\nuniform vec4 rs_ffv[%d];\n", FFV_COUNT);
    sb_add(&o, "attribute vec4 rs_v%d;\n", A_POS);
    if (k->normal) sb_add(&o, "attribute vec4 rs_v%d;\n", A_NORMAL);
    if (k->color0) sb_add(&o, "attribute vec4 rs_v%d;\n", A_COLOR0);
    if (k->color1) sb_add(&o, "attribute vec4 rs_v%d;\n", A_COLOR1);
    int used_set[8] = {0};
    for (int st = 0; st < 8; st++)
        if ((k->need & (VY_TEX0 << st)) && !k->tc[st].gen && k->tc_dims[k->tc[st].tci]) used_set[k->tc[st].tci] = 1;
    for (int j = 0; j < 8; j++) if (used_set[j]) sb_add(&o, "attribute vec4 rs_v%d;\n", A_TEX0 + j);
    for (int j = 0; j < npin; j++) sb_add(&o, "varying vec4 %s;\n", d3d9_sm_varying(pin[j].usage, pin[j].index, nm, sizeof nm));
    sb_add(&o, "void main() {\n");
    sb_add(&o, "  vec4 ipos = rs_v%d;\n  vec4 pos;\n  vec3 ep = vec3(0.0);\n", A_POS);
    if (k->pos_t) {
        // screen space: x, y in pixels, z the depth, w = 1 / rhw
        sb_add(&o, "  float w = ipos.w != 0.0 ? 1.0 / ipos.w : 1.0;\n");
        sb_add(&o, "  vec4 vp = rs_ffv[%d];\n", FFV_VIEWPORT);
        sb_add(&o, "  float nx = (ipos.x - vp.x) / vp.z * 2.0 - 1.0;\n");
        sb_add(&o, "  float ny = 1.0 - (ipos.y - vp.y) / vp.w * 2.0;\n");
        // the epilogue maps z with the viewport depth range: undo it, the depth is final
        sb_add(&o, "  float nz = rs_fix.z != 0.0 ? ((2.0 * ipos.z - 1.0) - rs_fix.w) / rs_fix.z : ipos.z;\n");
        sb_add(&o, "  pos = vec4(nx * w, ny * w, nz * w, w);\n");
        sb_add(&o, "  ep = vec3(0.0, 0.0, w);\n");
    } else {
        sb_add(&o, "  pos = vec4(dot(ipos, rs_ffv[%d]), dot(ipos, rs_ffv[%d]), dot(ipos, rs_ffv[%d]), dot(ipos, rs_ffv[%d]));\n",
               FFV_WVP, FFV_WVP + 1, FFV_WVP + 2, FFV_WVP + 3);
        sb_add(&o, "  ep = vec3(dot(ipos, rs_ffv[%d]), dot(ipos, rs_ffv[%d]), dot(ipos, rs_ffv[%d]));\n", FFV_WV, FFV_WV + 1,
               FFV_WV + 2);
    }
    int need_n = k->lighting;
    for (int st = 0; st < 8; st++)
        if ((k->need & (VY_TEX0 << st)) && (k->tc[st].gen == 1 || k->tc[st].gen == 3)) need_n = 1;
    if (need_n) {
        if (k->normal && !k->pos_t) {
            sb_add(&o, "  vec3 n = vec3(dot(rs_v%d.xyz, rs_ffv[%d].xyz), dot(rs_v%d.xyz, rs_ffv[%d].xyz), dot(rs_v%d.xyz, rs_ffv[%d].xyz));\n",
                   A_NORMAL, FFV_NRM, A_NORMAL, FFV_NRM + 1, A_NORMAL, FFV_NRM + 2);
            if (k->normalize) sb_add(&o, "  if (dot(n, n) > 0.0) n = normalize(n);\n");
        } else {
            sb_add(&o, "  vec3 n = vec3(0.0);\n");
        }
    }
    sb_add(&o, "  vec4 vdiff = %s;\n", k->color0 ? "rs_v2" : "vec4(1.0)");
    sb_add(&o, "  vec4 vspec = %s;\n", k->color1 ? "rs_v3" : "vec4(0.0)");
    if (k->lighting) {
        static const char *mcol[3] = {0, "vdiff", "vspec"};
        char mat[4][32];
        for (int j = 0; j < 4; j++) {
            if (k->mat_src[j]) snprintf(mat[j], sizeof mat[j], "%s", mcol[k->mat_src[j]]);
            else snprintf(mat[j], sizeof mat[j], "rs_ffv[%d]", FFV_MAT + j);
        }
        sb_add(&o, "  vec4 md = %s, ma = %s, ms = %s, me = %s;\n", mat[0], mat[1], mat[2], mat[3]);
        sb_add(&o, "  vec3 dsum = vec3(0.0), asum = vec3(0.0), ssum = vec3(0.0);\n");
        sb_add(&o, "  vec3 L, H;\n  float att, nl;\n");
        if (k->specular)
            sb_add(&o, "  vec3 V = %s;\n", k->localviewer ? "(dot(ep, ep) > 0.0 ? -normalize(ep) : vec3(0.0, 0.0, -1.0))"
                                                           : "vec3(0.0, 0.0, -1.0)");
        for (int j = 0; j < k->nlights; j++) {
            int b = FFV_LIGHT + LIGHT_VEC4S * j;
            int t = k->light_type[j];
            if (t == LIGHT_DIRECTIONAL) {
                sb_add(&o, "  L = rs_ffv[%d].xyz;\n  att = 1.0;\n", b + 4);
            } else {
                sb_add(&o, "  L = rs_ffv[%d].xyz - ep;\n", b + 3);
                sb_add(&o, "  {\n    float dl = length(L);\n    L = dl > 0.0 ? L / dl : vec3(0.0);\n");
                sb_add(&o, "    vec4 at = rs_ffv[%d];\n", b + 5);
                sb_add(&o, "    att = dl > rs_ffv[%d].w ? 0.0 : 1.0 / max(at.x + at.y * dl + at.z * dl * dl, 1e-20);\n", b + 3);
                if (t == LIGHT_SPOT) {
                    sb_add(&o, "    float rho = dot(L, rs_ffv[%d].xyz);\n    vec4 sp = rs_ffv[%d];\n", b + 4, b + 6);
                    sb_add(&o, "    float spot = rho > sp.x ? 1.0 : (rho <= sp.y ? 0.0 : pow(max((rho - sp.y) / max(sp.x - sp.y, 1e-6), 1e-6), at.w));\n");
                    sb_add(&o, "    att *= spot;\n");
                }
                sb_add(&o, "  }\n");
            }
            sb_add(&o, "  nl = max(dot(n, L), 0.0);\n");
            sb_add(&o, "  asum += rs_ffv[%d].rgb * att;\n", b + 2);
            sb_add(&o, "  dsum += rs_ffv[%d].rgb * (nl * att);\n", b);
            if (k->specular) {
                sb_add(&o, "  H = normalize(L + V);\n");
                sb_add(&o, "  if (nl > 0.0) ssum += rs_ffv[%d].rgb * (pow(max(dot(n, H), 1e-6), rs_ffv[%d].x) * att);\n", b + 1,
                       FFV_MAT + 4);
            }
        }
        sb_add(&o, "  vec4 col0 = clamp(vec4(me.rgb + ma.rgb * (rs_ffv[%d].rgb + asum) + md.rgb * dsum, md.a), 0.0, 1.0);\n",
               FFV_AMBIENT);
        sb_add(&o, "  vec4 col1 = clamp(vec4(ms.rgb * ssum, ms.a), 0.0, 1.0);\n");
    } else {
        sb_add(&o, "  vec4 col0 = vdiff;\n  vec4 col1 = vspec;\n");
    }
    // fog: the factor, or the distance for table fog
    if (k->need & VY_FOG) {
        if (k->fog >= 2) {
            sb_add(&o, "  float fogv = %s;\n", k->pos_t ? "w" : k->range_fog ? "length(ep)" : "abs(ep.z)");
        } else if (k->vfog && !k->pos_t) {
            sb_add(&o, "  float fd = %s;\n", k->range_fog ? "length(ep)" : "abs(ep.z)");
            sb_add(&o, "  vec4 fp = rs_ffv[%d];\n", FFV_FOG);
            if (k->vfog == 1) sb_add(&o, "  float fogv = exp(-fp.z * fd);\n");
            else if (k->vfog == 2) sb_add(&o, "  float fogv = exp(-(fp.z * fd) * (fp.z * fd));\n");
            else sb_add(&o, "  float fogv = (fp.y - fd) * fp.w;\n");
            sb_add(&o, "  fogv = clamp(fogv, 0.0, 1.0);\n");
        } else {
            sb_add(&o, "  float fogv = col1.a;\n");
        }
    }
    // texture coordinates by stage
    for (int st = 0; st < 8; st++) {
        if (!(k->need & (VY_TEX0 << st))) continue;
        const ffp_tc *tc = &k->tc[st];
        sb_add(&o, "  vec4 tc%d = ", st);
        if (tc->gen == 1) sb_add(&o, "vec4(n, 1.0);\n");
        else if (tc->gen == 2) sb_add(&o, "vec4(ep, 1.0);\n");
        else if (tc->gen == 3) sb_add(&o, "vec4(reflect(dot(ep, ep) > 0.0 ? normalize(ep) : vec3(0.0, 0.0, 1.0), n), 1.0);\n");
        else {
            int dims = k->tc_dims[tc->tci];
            // D3D extends a set to (.., 1, 0): the component after the last is 1
            static const char *ext[5] = {"vec4(0.0, 0.0, 0.0, 1.0)", "vec4(rs_v%d.x, 1.0, 0.0, 0.0)",
                                         "vec4(rs_v%d.xy, 1.0, 0.0)", "vec4(rs_v%d.xyz, 1.0)", "rs_v%d"};
            sb_add(&o, ext[dims], A_TEX0 + tc->tci);
            sb_add(&o, ";\n");
        }
        if (tc->count) {
            int b = FFV_TEXM + 4 * st;
            sb_add(&o, "  tc%d = vec4(dot(tc%d, rs_ffv[%d]), dot(tc%d, rs_ffv[%d]), dot(tc%d, rs_ffv[%d]), dot(tc%d, rs_ffv[%d]));\n",
                   st, st, b, st, b + 1, st, b + 2, st, b + 3);
        }
    }
    emit_epilogue(&o);
    for (int j = 0; j < npin; j++) {
        const char *v = d3d9_sm_varying(pin[j].usage, pin[j].index, nm, sizeof nm);
        int vy = vy_of(pin[j].usage, pin[j].index);
        if (vy == VY_COLOR0) sb_add(&o, "  %s = col0;\n", v);
        else if (vy == VY_COLOR1) sb_add(&o, "  %s = col1;\n", v);
        else if (vy == VY_FOG) sb_add(&o, "  %s = vec4(fogv);\n", v);
        else if (vy) sb_add(&o, "  %s = tc%d;\n", v, pin[j].index);
        else sb_add(&o, "  %s = vec4(0.0);\n", v);
    }
    sb_add(&o, "}\n");
    return o.s;
}

// A texture stage argument as a vec4 expression.
static void stage_arg(char *o, size_t n, int st, uint8_t a, int stage0) {
    const char *b;
    char cb[24];
    switch (a & 15) {
    case TA_DIFFUSE: b = "diff"; break;
    case TA_CURRENT: b = stage0 ? "diff" : "cur"; break;
    case TA_TEXTURE: b = "tex"; break;
    case TA_TFACTOR: snprintf(cb, sizeof cb, "rs_ffp[%d]", FFP_TFACTOR); b = cb; break;
    case TA_SPECULAR: b = "spec"; break;
    case TA_TEMP: b = "tmp"; break;
    case TA_CONSTANT: snprintf(cb, sizeof cb, "rs_ffp[%d]", FFP_CONST + st); b = cb; break;
    default:
        d3d9_log_once("d3d9: texture stage argument 0x%x not supported\n", a);
        b = "cur";
    }
    char t[40];
    if (a & TA_ALPHAREPLICATE) snprintf(t, sizeof t, "%s.aaaa", b);
    else snprintf(t, sizeof t, "%s", b);
    if (a & TA_COMPLEMENT) snprintf(o, n, "(vec4(1.0) - %s)", t);
    else snprintf(o, n, "%s", t);
}

// One operation on the components `sw` (".rgb" or ".a") of the arguments.
static void stage_op(sbuf *o, int op, const char *x1, const char *x2, const char *x0, const char *sw, const char *ty,
                     int stage0) {
    const char *cur = stage0 ? "diff" : "cur";
    switch (op) {
    case TOP_SELECTARG1: sb_add(o, "%s%s", x1, sw); return;
    case TOP_SELECTARG2: sb_add(o, "%s%s", x2, sw); return;
    case TOP_MODULATE: sb_add(o, "%s%s * %s%s", x1, sw, x2, sw); return;
    case TOP_MODULATE2X: sb_add(o, "%s%s * %s%s * 2.0", x1, sw, x2, sw); return;
    case TOP_MODULATE4X: sb_add(o, "%s%s * %s%s * 4.0", x1, sw, x2, sw); return;
    case TOP_ADD: sb_add(o, "%s%s + %s%s", x1, sw, x2, sw); return;
    case TOP_ADDSIGNED: sb_add(o, "%s%s + %s%s - %s(0.5)", x1, sw, x2, sw, ty); return;
    case TOP_ADDSIGNED2X: sb_add(o, "(%s%s + %s%s - %s(0.5)) * 2.0", x1, sw, x2, sw, ty); return;
    case TOP_SUBTRACT: sb_add(o, "%s%s - %s%s", x1, sw, x2, sw); return;
    case TOP_ADDSMOOTH: sb_add(o, "%s%s + %s%s - %s%s * %s%s", x1, sw, x2, sw, x1, sw, x2, sw); return;
    case TOP_BLENDDIFFUSEALPHA: sb_add(o, "mix(%s%s, %s%s, diff.a)", x2, sw, x1, sw); return;
    case TOP_BLENDTEXTUREALPHA: sb_add(o, "mix(%s%s, %s%s, tex.a)", x2, sw, x1, sw); return;
    case TOP_BLENDFACTORALPHA: sb_add(o, "mix(%s%s, %s%s, rs_ffp[%d].a)", x2, sw, x1, sw, FFP_TFACTOR); return;
    case TOP_BLENDCURRENTALPHA: sb_add(o, "mix(%s%s, %s%s, %s.a)", x2, sw, x1, sw, cur); return;
    case TOP_BLENDTEXTUREALPHAPM: sb_add(o, "%s%s + %s%s * (1.0 - tex.a)", x1, sw, x2, sw); return;
    case TOP_MODULATEALPHA_ADDCOLOR: sb_add(o, "%s%s + %s.a * %s%s", x1, sw, x1, x2, sw); return;
    case TOP_MODULATECOLOR_ADDALPHA: sb_add(o, "%s%s * %s%s + %s(%s.a)", x1, sw, x2, sw, ty, x1); return;
    case TOP_MODULATEINVALPHA_ADDCOLOR: sb_add(o, "%s%s + (1.0 - %s.a) * %s%s", x1, sw, x1, x2, sw); return;
    case TOP_MODULATEINVCOLOR_ADDALPHA: sb_add(o, "(%s(1.0) - %s%s) * %s%s + %s(%s.a)", ty, x1, sw, x2, sw, ty, x1); return;
    case TOP_DOTPRODUCT3:
        sb_add(o, "%s(clamp(4.0 * dot(%s.rgb - vec3(0.5), %s.rgb - vec3(0.5)), 0.0, 1.0))", ty, x1, x2);
        return;
    case TOP_MULTIPLYADD: sb_add(o, "%s%s + %s%s * %s%s", x0, sw, x1, sw, x2, sw); return;
    case TOP_LERP: sb_add(o, "mix(%s%s, %s%s, %s%s)", x2, sw, x1, sw, x0, sw); return;
    }
    d3d9_log_once("d3d9: texture stage operation %d not supported (first argument used)\n", op);
    sb_add(o, "%s%s", x1, sw);
}

// The fixed-function pixel stage. avail: the varyings the vertex stage writes (VY_*).
static char *ffp_ps(const ffp_key *k, int avail) {
    sbuf o = {0};
    sb_add(&o, "#version 120\nuniform vec4 rs_ffp[%d];\n", FFP_COUNT);
    for (int st = 0; st < k->nstages; st++)
        if (k->st[st].tex) sb_add(&o, "uniform %s s%d;\n", k->st[st].tex == 3 ? "samplerCube" : "sampler2D", st);
    int have = k->need & avail;
    if (have & VY_COLOR0) sb_add(&o, "varying vec4 rs_color0;\n");
    if (have & VY_COLOR1) sb_add(&o, "varying vec4 rs_color1;\n");
    for (int st = 0; st < 8; st++) if (have & (VY_TEX0 << st)) sb_add(&o, "varying vec4 rs_texcoord%d;\n", st);
    if (have & VY_FOG) sb_add(&o, "varying vec4 rs_fog0;\n");
    sb_add(&o, "void main() {\n");
    sb_add(&o, "  vec4 diff = %s;\n", have & VY_COLOR0 ? "rs_color0" : "vec4(1.0)");
    sb_add(&o, "  vec4 spec = %s;\n", have & VY_COLOR1 ? "rs_color1" : "vec4(0.0)");
    sb_add(&o, "  vec4 cur = diff, tmp = vec4(0.0), tex = vec4(1.0), res;\n");
    for (int st = 0; st < k->nstages; st++) {
        const ffp_stage *e = &k->st[st];
        int s0 = st == 0;
        if (e->tex) {
            const ffp_tc *tc = &k->tc[st];
            char coord[64];
            if (!(have & (VY_TEX0 << st))) snprintf(coord, sizeof coord, "vec4(0.0)");
            else snprintf(coord, sizeof coord, "rs_texcoord%d", st);
            if (e->tex == 3) {
                sb_add(&o, "  tex = textureCube(s%d, %s.xyz);\n", st, coord);
            } else if (tc->proj && tc->count >= 2) {
                static const char *last[5] = {"", "", "y", "z", "w"};
                sb_add(&o, "  tex = texture2D(s%d, %s.xy / %s.%s);\n", st, coord, coord, last[tc->count]);
            } else {
                sb_add(&o, "  tex = texture2D(s%d, %s.xy);\n", st, coord);
            }
        } else {
            sb_add(&o, "  tex = vec4(1.0);\n");  // no texture bound: the stage reads white
        }
        char c1[48], c2[48], c0[48], a1[48], a2[48], a0[48];
        stage_arg(c1, sizeof c1, st, e->ca1, s0);
        stage_arg(c2, sizeof c2, st, e->ca2, s0);
        stage_arg(c0, sizeof c0, st, e->ca0, s0);
        sb_add(&o, "  res.rgb = clamp(");
        stage_op(&o, e->cop, c1, c2, c0, ".rgb", "vec3", s0);
        sb_add(&o, ", 0.0, 1.0);\n");
        if (e->cop == TOP_DOTPRODUCT3) {
            sb_add(&o, "  res.a = res.r;\n");
        } else if (e->aop == TOP_DISABLE) {
            sb_add(&o, "  res.a = %s.a;\n", s0 ? "diff" : "cur");
        } else {
            stage_arg(a1, sizeof a1, st, e->aa1, s0);
            stage_arg(a2, sizeof a2, st, e->aa2, s0);
            stage_arg(a0, sizeof a0, st, e->aa0, s0);
            sb_add(&o, "  res.a = clamp(");
            stage_op(&o, e->aop, a1, a2, a0, ".a", "float", s0);
            sb_add(&o, ", 0.0, 1.0);\n");
        }
        sb_add(&o, "  %s = res;\n", e->result == TA_TEMP ? "tmp" : "cur");
    }
    if (k->specular) sb_add(&o, "  cur.rgb = clamp(cur.rgb + spec.rgb, 0.0, 1.0);\n");
    if (k->fog && (have & VY_FOG)) {
        sb_add(&o, "  vec4 fp = rs_ffp[%d];\n  float f = rs_fog0.x;\n", FFP_FOG);
        if (k->fog == 2) sb_add(&o, "  f = (fp.y - f) * fp.w;\n");
        else if (k->fog == 3) sb_add(&o, "  f = exp(-fp.z * f);\n");
        else if (k->fog == 4) sb_add(&o, "  f = exp(-(fp.z * f) * (fp.z * f));\n");
        sb_add(&o, "  cur.rgb = mix(rs_ffp[%d].rgb, cur.rgb, clamp(f, 0.0, 1.0));\n", FFP_FOGCOLOR);
    }
    sb_add(&o, "  gl_FragColor = cur;\n}\n");
    return o.s;
}

// --- programs ------------------------------------------------------------------------------
static void bind_ffp(GLuint p, void *arg) {
    const ffp_key *k = arg;
    glBindAttribLocation(p, A_POS, "rs_v0");
    char n[16];
    for (int r = 1; r < 12; r++) {
        snprintf(n, sizeof n, "rs_v%d", r);
        if ((r == A_NORMAL && k->normal) || (r == A_COLOR0 && k->color0) || (r == A_COLOR1 && k->color1) ||
            (r >= A_TEX0 && k->tc_dims[r - A_TEX0]))
            glBindAttribLocation(p, (GLuint)r, n);
    }
}

static void bind_vs(GLuint p, void *arg) {
    const d3d9_sm_info *in = d3d9_sm_get_info(((d3d9_shader *)arg)->sm);
    char n[16];
    for (int k = 0; k < 16; k++)
        if (in->input[k].used) {
            snprintf(n, sizeof n, "rs_v%d", k);
            glBindAttribLocation(p, (GLuint)k, n);
        }
}

// Which varyings translated vertex shader GLSL writes (its varying declarations).
static int vs_avail(const char *src) {
    int m = 0;
    char nm[48], decl[64];
    for (int j = 0; j < 11; j++) {
        int usage = j < 2 ? DU_COLOR : j < 10 ? DU_TEXCOORD : DU_FOG;
        int index = j < 2 ? j : j < 10 ? j - 2 : 0;
        snprintf(decl, sizeof decl, "varying vec4 %s;", d3d9_sm_varying(usage, index, nm, sizeof nm));
        if (strstr(src, decl)) m |= vy_of(usage, index);
    }
    return m;
}

static uint32_t key_hash(const ffp_key *k) {
    const uint8_t *b = (const uint8_t *)k;
    uint32_t h = 2166136261u;
    for (size_t j = 0; j < sizeof *k; j++) h = (h ^ b[j]) * 16777619u;
    return h;
}

static void make_program(d3d9_device *d, const ffp_key *k, d3d9_program *p) {
    const d3d9_state *s = &d->st;
    char err[512] = "", log[2048] = "";
    char *vsrc = 0, *fsrc = 0;
    int avail;
    if (s->vs) {
        vsrc = d3d9_sm_glsl(s->vs->sm, 0, err, sizeof err);
        if (!vsrc) {
            fprintf(stderr, "d3d9: vertex shader %u: %s\n", s->vs->id, err);
            goto fail;
        }
        avail = vs_avail(vsrc);
    } else {
        d3d9_sm_input pin[32];
        int npin = 0;
        if (s->ps) {
            npin = d3d9_sm_ps_semantics(s->ps->sm, pin, 32);
        } else {
            if (k->need & VY_COLOR0) pin[npin++] = (d3d9_sm_input){1, DU_COLOR, 0};
            if (k->need & VY_COLOR1) pin[npin++] = (d3d9_sm_input){1, DU_COLOR, 1};
            for (int st = 0; st < 8; st++)
                if (k->need & (VY_TEX0 << st)) pin[npin++] = (d3d9_sm_input){1, DU_TEXCOORD, st};
            if (k->need & VY_FOG) pin[npin++] = (d3d9_sm_input){1, DU_FOG, 0};
        }
        vsrc = ffp_vs(k, pin, npin);
        avail = k->need;
    }
    if (s->ps) {
        if (!s->ps->glsl) goto fail;
        fsrc = strdup(s->ps->glsl);
    } else {
        fsrc = ffp_ps(k, avail);
    }
    p->prog = d3d9_gl_link(vsrc, fsrc, s->vs ? bind_vs : bind_ffp, s->vs ? (void *)s->vs : (void *)k, log, sizeof log);
    if (!p->prog) {
        fprintf(stderr, "d3d9: fixed-function program (vs %u, ps %u) failed: %s\n--- vs\n%s--- ps\n%s", k->vs_id, k->ps_id,
                log, vsrc, fsrc);
        goto fail;
    }
    // attributes
    if (s->vs) {
        const d3d9_sm_info *vi = d3d9_sm_get_info(s->vs->sm);
        p->nvc = vi->nconst;
        for (int r = 0; r < 16; r++) {
            p->attr_used[r] = vi->input[r].used;
            p->attr[r] = vi->input[r];
        }
    } else {
        p->attr_used[A_POS] = 1;
        p->attr[A_POS] = (d3d9_sm_input){1, k->pos_t ? DU_POSITIONT : DU_POSITION, 0};
        if (k->normal) p->attr_used[A_NORMAL] = 1, p->attr[A_NORMAL] = (d3d9_sm_input){1, DU_NORMAL, 0};
        if (k->color0) p->attr_used[A_COLOR0] = 1, p->attr[A_COLOR0] = (d3d9_sm_input){1, DU_COLOR, 0};
        if (k->color1) p->attr_used[A_COLOR1] = 1, p->attr[A_COLOR1] = (d3d9_sm_input){1, DU_COLOR, 1};
        for (int st = 0; st < 8; st++) {
            if (!(k->need & (VY_TEX0 << st)) || k->tc[st].gen) continue;
            int set = k->tc[st].tci;
            if (!k->tc_dims[set]) continue;
            p->attr_used[A_TEX0 + set] = 1;
            p->attr[A_TEX0 + set] = (d3d9_sm_input){1, DU_TEXCOORD, set};
        }
    }
    p->loc_vc = d3d9_gl_uloc(p->prog, "vc");
    p->loc_pc = d3d9_gl_uloc(p->prog, "pc");
    p->loc_fix = glGetUniformLocation(p->prog, "rs_fix");
    p->loc_vi = d3d9_gl_uloc(p->prog, "rs_vi");
    p->loc_pi = d3d9_gl_uloc(p->prog, "rs_pi");
    p->loc_vb = d3d9_gl_uloc(p->prog, "rs_vb");
    p->loc_pb = d3d9_gl_uloc(p->prog, "rs_pb");
    p->loc_ffv = s->vs ? -1 : d3d9_gl_uloc(p->prog, "rs_ffv");
    p->loc_ffp = s->ps ? -1 : d3d9_gl_uloc(p->prog, "rs_ffp");
    glUseProgram(p->prog);
    d->cur_prog = p;
    if (s->ps) {
        const d3d9_sm_info *pi = d3d9_sm_get_info(s->ps->sm);
        p->npc = pi->nconst;
        for (int j = 0; j < 16; j++) p->sampler_type[j] = pi->sampler_type[j];
    } else {
        for (int st = 0; st < k->nstages; st++) p->sampler_type[st] = k->st[st].tex;
    }
    for (int j = 0; j < 16; j++)
        if (p->sampler_type[j]) {
            char n[8];
            snprintf(n, sizeof n, "s%d", j);
            GLint l = glGetUniformLocation(p->prog, n);
            if (l >= 0) glUniform1i(l, j);
        }
    p->ff_last = calloc(FFV_COUNT + FFP_COUNT, 16);
    p->ff_last[0] = p->ff_last[4 * FFV_COUNT] = NAN;  // nothing uploaded yet: never equal
    p->fix[0] = NAN;
    free(vsrc);
    free(fsrc);
    return;
fail:
    free(vsrc);
    free(fsrc);
    p->failed = 1;
    d3d9_program_failures++;
}

d3d9_program *d3d9_ffp_program(d3d9_device *d, d3d9_decl *decl) {
    ffp_key k;
    build_key(d, decl, &k);
    uint32_t h = key_hash(&k) & 63;
    for (ffp_entry *e = d->ffp_programs[h]; e; e = e->next)
        if (!memcmp(&e->key, &k, sizeof k)) return &e->p;
    ffp_entry *e = calloc(1, sizeof *e);
    e->key = k;
    e->next = d->ffp_programs[h];
    d->ffp_programs[h] = e;
    d3d9_program *p = &e->p;
    p->vs_id = k.vs_id;
    p->ps_id = k.ps_id;
    p->ffp = 1;
    p->vc_gen = p->pc_gen = p->ic_gen = ~0ull;
    make_program(d, &k, p);
    return p;
}

// --- uniforms -----------------------------------------------------------------------------------
static void color4(float *o, uint32_t c) {
    o[0] = ((c >> 16) & 255) / 255.0f;
    o[1] = ((c >> 8) & 255) / 255.0f;
    o[2] = (c & 255) / 255.0f;
    o[3] = (c >> 24) / 255.0f;
}

static void vec_mat(float *o, const float *v, const float *m, float w) {  // (v, w) * m, xyz
    for (int c = 0; c < 3; c++) o[c] = v[0] * m[c] + v[1] * m[4 + c] + v[2] * m[8 + c] + w * m[12 + c];
}

static void build_ffv(const d3d9_state *s, float u[FFV_COUNT][4]) {
    memset(u, 0, sizeof(float) * 4 * FFV_COUNT);
    float wv[16], wvp[16];
    mat_mul(wv, s->xf[D3D9_XF_WORLD], s->xf[D3D9_XF_VIEW]);
    mat_mul(wvp, wv, s->xf[D3D9_XF_PROJ]);
    mat_columns(u[FFV_WVP], wvp);
    mat_columns(u[FFV_WV], wv);
    // rows of the inverse of the upper 3x3 of world * view
    const float *a = wv;
    float c00 = a[5] * a[10] - a[6] * a[9], c01 = a[6] * a[8] - a[4] * a[10], c02 = a[4] * a[9] - a[5] * a[8];
    float det = a[0] * c00 + a[1] * c01 + a[2] * c02;
    float id = fabsf(det) > 1e-30f ? 1.0f / det : 0.0f;
    float inv[9] = {
        c00 * id, (a[2] * a[9] - a[1] * a[10]) * id, (a[1] * a[6] - a[2] * a[5]) * id,
        c01 * id, (a[0] * a[10] - a[2] * a[8]) * id, (a[2] * a[4] - a[0] * a[6]) * id,
        c02 * id, (a[1] * a[8] - a[0] * a[9]) * id, (a[0] * a[5] - a[1] * a[4]) * id,
    };
    for (int r = 0; r < 3; r++) memcpy(u[FFV_NRM + r], inv + 3 * r, 12);
    u[FFV_VIEWPORT][0] = (float)s->viewport[0];
    u[FFV_VIEWPORT][1] = (float)s->viewport[1];
    u[FFV_VIEWPORT][2] = s->viewport[2] ? (float)s->viewport[2] : 1.0f;
    u[FFV_VIEWPORT][3] = s->viewport[3] ? (float)s->viewport[3] : 1.0f;
    for (int j = 0; j < 4; j++) memcpy(u[FFV_MAT + j], s->mat + 4 * j, 16);
    u[FFV_MAT + 4][0] = s->mat[16];
    color4(u[FFV_AMBIENT], s->rs[RS_AMBIENT]);
    int n = 0;
    const float *view = s->xf[D3D9_XF_VIEW];
    for (int j = 0; j < D3D9_MAX_LIGHTS && n < 8; j++) {
        if (!s->light_on[j]) continue;
        const uint32_t *l = s->light[j];
        float (*o)[4] = &u[FFV_LIGHT + LIGHT_VEC4S * n++];
        float f[26];
        memcpy(f, l, sizeof f);
        memcpy(o[0], f + 1, 16);
        memcpy(o[1], f + 5, 16);
        memcpy(o[2], f + 9, 16);
        vec_mat(o[3], f + 13, view, 1.0f);
        o[3][3] = f[19];
        float dir[3];
        vec_mat(dir, f + 16, view, 0.0f);
        float len = sqrtf(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
        for (int c = 0; c < 3; c++) o[4][c] = len > 0 ? -dir[c] / len : 0.0f;
        o[5][0] = f[21];
        o[5][1] = f[22];
        o[5][2] = f[23];
        if (o[5][0] == 0 && o[5][1] == 0 && o[5][2] == 0) o[5][0] = 1.0f;
        o[5][3] = f[20];
        o[6][0] = cosf(f[24] * 0.5f);
        o[6][1] = cosf(f[25] * 0.5f);
    }
    float fs = f32(s->rs[RS_FOGSTART]), fe = f32(s->rs[RS_FOGEND]);
    u[FFV_FOG][0] = fs;
    u[FFV_FOG][1] = fe;
    u[FFV_FOG][2] = f32(s->rs[RS_FOGDENSITY]);
    u[FFV_FOG][3] = fe != fs ? 1.0f / (fe - fs) : 0.0f;
    for (int st = 0; st < 8; st++) mat_columns(u[FFV_TEXM + 4 * st], s->xf[D3D9_XF_TEX0 + st]);
}

static void build_ffp(const d3d9_state *s, float u[FFP_COUNT][4]) {
    memset(u, 0, sizeof(float) * 4 * FFP_COUNT);
    color4(u[FFP_TFACTOR], s->rs[RS_TEXTUREFACTOR]);
    color4(u[FFP_FOGCOLOR], s->rs[RS_FOGCOLOR]);
    float fs = f32(s->rs[RS_FOGSTART]), fe = f32(s->rs[RS_FOGEND]);
    u[FFP_FOG][0] = fs;
    u[FFP_FOG][1] = fe;
    u[FFP_FOG][2] = f32(s->rs[RS_FOGDENSITY]);
    u[FFP_FOG][3] = fe != fs ? 1.0f / (fe - fs) : 0.0f;
    for (int st = 0; st < 8; st++) color4(u[FFP_CONST + st], s->tss[st][TSS_CONSTANT]);
}

void d3d9_ffp_upload(d3d9_device *d, d3d9_program *p) {
    float u[FFV_COUNT + FFP_COUNT][4];
    if (p->loc_ffv >= 0) {
        build_ffv(&d->st, u);
        if (memcmp(u, p->ff_last, sizeof(float) * 4 * FFV_COUNT)) {
            glUniform4fv(p->loc_ffv, FFV_COUNT, &u[0][0]);
            memcpy(p->ff_last, u, sizeof(float) * 4 * FFV_COUNT);
        }
    }
    if (p->loc_ffp >= 0) {
        build_ffp(&d->st, u + FFV_COUNT);
        float *last = p->ff_last + 4 * FFV_COUNT;
        if (memcmp(u + FFV_COUNT, last, sizeof(float) * 4 * FFP_COUNT)) {
            glUniform4fv(p->loc_ffp, FFP_COUNT, &u[FFV_COUNT][0]);
            memcpy(last, u + FFV_COUNT, sizeof(float) * 4 * FFP_COUNT);
        }
    }
}
