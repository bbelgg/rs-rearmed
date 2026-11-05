// The fixed-function pipeline's generated GLSL (d3d9_ffp_gen.h).
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "d3d9_defs.h"
#include "d3d9_ffp_gen.h"

// The layer's own blit (d3d9_dev_blit).
const char d3d9_blit_vs[] =
    "#version 120\n"
    "attribute vec2 rs_p;\n"
    "uniform vec4 rs_b[2];\n"
    "varying vec2 rs_uv;\n"
    "void main() {\n"
    "  rs_uv = mix(rs_b[1].xy, rs_b[1].zw, rs_p);\n"
    "  gl_Position = vec4(mix(rs_b[0].xy, rs_b[0].zw, rs_p), 0.0, 1.0);\n"
    "}\n";
const char d3d9_blit_fs[] =
    "#version 120\n"
    "uniform sampler2D rs_tex;\n"
    "varying vec2 rs_uv;\n"
    "void main() { gl_FragColor = texture2D(rs_tex, rs_uv); }\n";

int d3d9_ffp_vy_of(int usage, int index) {
    if (usage == DU_COLOR && index < 2) return index ? VY_COLOR1 : VY_COLOR0;
    if (usage == DU_TEXCOORD && index < 8) return VY_TEX0 << index;
    if (usage == DU_FOG && index == 0) return VY_FOG;
    return 0;
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
        int vy = d3d9_ffp_vy_of(pin[j].usage, pin[j].index);
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

// Which varyings translated vertex shader GLSL writes (its varying declarations).
static int vs_avail(const char *src) {
    int m = 0;
    char nm[48], decl[64];
    for (int j = 0; j < 11; j++) {
        int usage = j < 2 ? DU_COLOR : j < 10 ? DU_TEXCOORD : DU_FOG;
        int index = j < 2 ? j : j < 10 ? j - 2 : 0;
        snprintf(decl, sizeof decl, "varying vec4 %s;", d3d9_sm_varying(usage, index, nm, sizeof nm));
        if (strstr(src, decl)) m |= d3d9_ffp_vy_of(usage, index);
    }
    return m;
}

int d3d9_ffp_sources(const ffp_key *k, const d3d9_sm *vs, const d3d9_sm *ps, const char *ps_glsl, char **vsrc, char **fsrc,
                     char *err, size_t errlen) {
    int avail;
    *vsrc = *fsrc = 0;
    if (vs) {
        *vsrc = d3d9_sm_glsl(vs, 0, err, errlen);
        if (!*vsrc) return 0;
        avail = vs_avail(*vsrc);
    } else {
        d3d9_sm_input pin[32];
        int npin = 0;
        if (ps) {
            npin = d3d9_sm_ps_semantics(ps, pin, 32);
        } else {
            if (k->need & VY_COLOR0) pin[npin++] = (d3d9_sm_input){1, DU_COLOR, 0};
            if (k->need & VY_COLOR1) pin[npin++] = (d3d9_sm_input){1, DU_COLOR, 1};
            for (int st = 0; st < 8; st++)
                if (k->need & (VY_TEX0 << st)) pin[npin++] = (d3d9_sm_input){1, DU_TEXCOORD, st};
            if (k->need & VY_FOG) pin[npin++] = (d3d9_sm_input){1, DU_FOG, 0};
        }
        *vsrc = ffp_vs(k, pin, npin);
        avail = k->need;
    }
    if (ps) {
        if (!ps_glsl) {
            snprintf(err, errlen, "the pixel shader did not translate");
            free(*vsrc);
            *vsrc = 0;
            return 0;
        }
        *fsrc = strdup(ps_glsl);
    } else {
        *fsrc = ffp_ps(k, avail);
    }
    return 1;
}
