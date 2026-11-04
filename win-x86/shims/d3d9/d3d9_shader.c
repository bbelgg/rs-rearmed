// D3D9 shader bytecode -> GLSL 1.20 (d3d9_shader.h).
//
// Token formats (the D3D9 DDK, "Direct3D Shader Codes"):
//   version      0xFFFE (vs) / 0xFFFF (ps) << 16 | major << 8 | minor
//   instruction  opcode [15:0], control [23:16], length in tokens [27:24] (2_0 and up),
//                predicated [28]
//   destination  register number [10:0], type [30:28] | [12:11] << 3, write mask [19:16],
//                result modifier [23:20] (1 saturate, 2 partial precision, 4 centroid)
//   source       number and type as above, relative [13], swizzle [23:16], modifier [27:24];
//                a relative source is followed by a token naming the address register
//   comment      0xFFFE | length [30:16]; end 0x0000FFFF
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "d3d9_shader.h"

enum {
    OP_NOP = 0, OP_MOV, OP_ADD, OP_SUB, OP_MAD, OP_MUL, OP_RCP, OP_RSQ, OP_DP3, OP_DP4, OP_MIN, OP_MAX,
    OP_SLT, OP_SGE, OP_EXP, OP_LOG, OP_LIT, OP_DST, OP_LRP, OP_FRC, OP_M4x4, OP_M4x3, OP_M3x4, OP_M3x3,
    OP_M3x2, OP_CALL, OP_CALLNZ, OP_LOOP, OP_RET, OP_ENDLOOP, OP_LABEL, OP_DCL, OP_POW, OP_CRS, OP_SGN,
    OP_ABS, OP_NRM, OP_SINCOS, OP_REP, OP_ENDREP, OP_IF, OP_IFC, OP_ELSE, OP_ENDIF, OP_BREAK, OP_BREAKC,
    OP_MOVA, OP_DEFB, OP_DEFI,
    OP_TEXKILL = 65, OP_TEX = 66, OP_EXPP = 78, OP_LOGP = 79, OP_CND = 80, OP_DEF = 81, OP_CMP = 88,
    OP_DP2ADD = 90, OP_DSX = 91, OP_DSY = 92, OP_TEXLDD = 93, OP_SETP = 94, OP_TEXLDL = 95, OP_BREAKP = 96,
    OP_PHASE = 0xFFFD, OP_COMMENT = 0xFFFE, OP_END = 0xFFFF,
};

enum {
    RT_TEMP = 0, RT_INPUT = 1, RT_CONST = 2, RT_ADDR = 3, RT_TEXTURE = 3, RT_RASTOUT = 4, RT_ATTROUT = 5,
    RT_OUTPUT = 6, RT_CONSTINT = 7, RT_COLOROUT = 8, RT_DEPTHOUT = 9, RT_SAMPLER = 10, RT_CONST2 = 11,
    RT_CONST3 = 12, RT_CONST4 = 13, RT_CONSTBOOL = 14, RT_LOOP = 15, RT_MISC = 17, RT_LABEL = 18,
    RT_PREDICATE = 19,
};

enum { U_POSITION = 0, U_PSIZE = 4, U_TEXCOORD = 5, U_COLOR = 10, U_FOG = 11 };
static const char *usage_name[] = {"position", "blendweight", "blendindices", "normal", "psize",
                                   "texcoord", "tangent", "binormal", "tessfactor", "positiont",
                                   "color", "fog", "depth", "sample"};

typedef struct {
    uint32_t tok;
    uint32_t rel;  // the relative-address token (when tok bit 13)
} param;

typedef struct {
    uint16_t op;
    uint8_t ctrl;
    uint8_t np;
    param p[5];
    uint32_t imm[4];  // def / defi / defb values
} ins;

typedef struct {
    int regtype, num, usage, index, mask;
} decl;

struct d3d9_sm {
    d3d9_sm_info info;
    uint32_t *tokens;
    ins *code;
    int ncode;
    decl in[32], out[16];
    int nin, nout;
    // def'd constants: float c, int i, bool b
    int has_def[256];
    float defv[256][4];
    int has_defi[16];
    int defi[16][4];
    int has_defb[16];
    int defb[16];
};

static int rtype(uint32_t t) { return (int)(((t >> 28) & 7) | ((t >> 8) & 0x18)); }
static int rnum(uint32_t t) { return (int)(t & 0x7ff); }

static void setf(char *err, size_t n, const char *fmt, ...) {
    if (!err || !n) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(err, n, fmt, ap);
    va_end(ap);
}

// Parameter count of an instruction, for 2_0+ the length field; for opcodes without one
// (never in 2_0+ bytecode) we refuse the shader.
d3d9_sm *d3d9_sm_parse(const uint32_t *tok, uint32_t maxtok, char *err, size_t errlen) {
    if (!tok || maxtok < 2) { setf(err, errlen, "no bytecode"); return 0; }
    uint32_t ver = tok[0];
    int is_ps = (ver >> 16) == 0xFFFF;
    if (!is_ps && (ver >> 16) != 0xFFFE) { setf(err, errlen, "not a shader (version token 0x%08x)", ver); return 0; }
    int major = (ver >> 8) & 0xff, minor = ver & 0xff;
    if (major < 2 || major > 3) { setf(err, errlen, "%s_%d_%d: only shader models 2 and 3 are supported", is_ps ? "ps" : "vs", major, minor); return 0; }
    d3d9_sm *sm = calloc(1, sizeof *sm);
    sm->info.is_ps = is_ps;
    sm->info.major = major;
    sm->info.minor = minor;
    int cap = 64;
    sm->code = malloc(cap * sizeof *sm->code);
    uint32_t p = 1;
    int ended = 0;
    while (p < maxtok) {
        uint32_t t = tok[p];
        uint32_t op = t & 0xffff;
        if (t == 0x0000FFFF) { p++; ended = 1; break; }
        if (op == OP_COMMENT) { p += 1 + ((t >> 16) & 0x7fff); continue; }
        uint32_t len = (t >> 24) & 0xf;
        if (p + 1 + len > maxtok) break;
        if (op == OP_PHASE) { setf(err, errlen, "phase (ps_1_4) is not supported"); goto fail; }
        ins in = {0};
        in.op = (uint16_t)op;
        in.ctrl = (uint8_t)((t >> 16) & 0xff);
        if (t & (1u << 28)) { setf(err, errlen, "predicated instructions are not supported (opcode %u)", op); goto fail; }
        const uint32_t *a = tok + p + 1;
        if (op == OP_DCL) {
            if (len != 2) { setf(err, errlen, "dcl of length %u", len); goto fail; }
            uint32_t d = a[0], r = a[1];
            int rt = rtype(r), n = rnum(r);
            if (rt == RT_SAMPLER) {
                if (n < 16) sm->info.sampler_type[n] = (int)((d >> 27) & 0xf);
            } else if (rt == RT_MISC) {
                // vPos / vFace: no declaration needed
            } else {
                decl dc = {rt, n, (int)(d & 0x1f), (int)((d >> 16) & 0xf), (int)((r >> 16) & 0xf)};
                int is_out = !is_ps && rt == RT_OUTPUT;
                if (is_out) {
                    if (sm->nout < 16) sm->out[sm->nout++] = dc;
                } else if (sm->nin < 32) {
                    sm->in[sm->nin++] = dc;
                    if (!is_ps && rt == RT_INPUT && n < 16)
                        sm->info.input[n] = (d3d9_sm_input){1, dc.usage, dc.index};
                }
            }
            p += 1 + len;
            continue;
        }
        if (op == OP_DEF || op == OP_DEFI) {
            if (len != 5) { setf(err, errlen, "def of length %u", len); goto fail; }
            int n = rnum(a[0]);
            if (op == OP_DEF && n < 256) {
                sm->has_def[n] = 1;
                memcpy(sm->defv[n], a + 1, 16);
            } else if (op == OP_DEFI && n < 16) {
                sm->has_defi[n] = 1;
                memcpy(sm->defi[n], a + 1, 16);
            }
            p += 1 + len;
            continue;
        }
        if (op == OP_DEFB) {
            int n = rnum(a[0]);
            if (n < 16) { sm->has_defb[n] = 1; sm->defb[n] = (int)a[1]; }
            p += 1 + len;
            continue;
        }
        // parameters: each is one token, plus one when relative (2_0+ encoding)
        uint32_t k = 0;
        while (k < len) {
            if (in.np >= 5) { setf(err, errlen, "too many parameters (opcode %u)", op); goto fail; }
            param pa = {a[k++], 0};
            // a destination's bit 13 is relative addressing too (vs_3_0 o[aL]); sources likewise
            if ((pa.tok & (1u << 13)) && k < len) pa.rel = a[k++];
            in.p[in.np++] = pa;
        }
        if (sm->ncode == cap) sm->code = realloc(sm->code, (cap *= 2) * sizeof *sm->code);
        sm->code[sm->ncode++] = in;
        p += 1 + len;
    }
    if (!ended) { setf(err, errlen, "no end token within %u tokens", maxtok); goto fail; }
    sm->info.ntokens = p;
    sm->tokens = malloc(4 * (size_t)p);
    memcpy(sm->tokens, tok, 4 * (size_t)p);
    return sm;
fail:
    d3d9_sm_free(sm);
    return 0;
}

void d3d9_sm_free(d3d9_sm *sm) {
    if (!sm) return;
    free(sm->code);
    free(sm->tokens);
    free(sm);
}
const d3d9_sm_info *d3d9_sm_get_info(const d3d9_sm *sm) { return &sm->info; }
const uint32_t *d3d9_sm_tokens(const d3d9_sm *sm) { return sm->tokens; }

// --- code generation ---------------------------------------------------------------------------
typedef struct {
    char *s;
    size_t n, cap;
} sbuf;

static void sb_add(sbuf *b, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void sb_add(sbuf *b, const char *fmt, ...) {
    va_list ap;
    for (;;) {
        size_t room = b->cap - b->n;
        va_start(ap, fmt);
        int w = vsnprintf(b->s ? b->s + b->n : 0, b->s ? room : 0, fmt, ap);
        va_end(ap);
        if (w < 0) return;
        if (b->s && (size_t)w < room) { b->n += (size_t)w; return; }
        b->cap = b->cap * 2 + (size_t)w + 256;
        b->s = realloc(b->s, b->cap);
    }
}

typedef struct {
    const d3d9_sm *sm;
    int ps, major;
    char *err;
    size_t errlen;
    int failed;
    sbuf body;
    int depth;        // indentation / loop nesting
    int loopvar;      // counter for loop variable names
    // usage
    int max_temp;
    int used_input[32], used_tex[16], used_out[16], used_oc[4], used_od[2], used_ot[8];
    int used_opos, used_ofog, used_opts, used_odepth, used_vpos, used_vface;
    int used_a0, used_al, used_p0;
    int max_const, const_rel, used_int, used_bool;
    int used_def[256];
} gen;

static void gfail(gen *g, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void gfail(gen *g, const char *fmt, ...) {
    if (g->failed) return;
    g->failed = 1;
    va_list ap;
    va_start(ap, fmt);
    if (g->err && g->errlen) vsnprintf(g->err, g->errlen, fmt, ap);
    va_end(ap);
}

static void fmt_float(char *o, size_t n, float f) {
    if (isnan(f)) f = 0;
    if (isinf(f)) f = f > 0 ? 3.402823e38f : -3.402823e38f;
    snprintf(o, n, "%.9g", (double)f);
    if (!strpbrk(o, ".en")) strncat(o, ".0", n - strlen(o) - 1);
}

static const char *SW = "xyzw";

// The GLSL name of a register (no swizzle). `off` is added to the register number (m4x4 rows).
static void reg_name(gen *g, const param *pa, int off, int write, char *o, size_t n) {
    int rt = rtype(pa->tok), num = rnum(pa->tok) + off;
    int rel = (pa->tok >> 13) & 1;
    char relx[32] = "";
    if (rel) {
        int rrt = rtype(pa->rel);
        int comp = (pa->rel >> 16) & 3;
        if (rrt == RT_ADDR && !g->ps) { snprintf(relx, sizeof relx, "a0.%c", SW[comp]); g->used_a0 = 1; }
        else if (rrt == RT_LOOP) { snprintf(relx, sizeof relx, "aL"); g->used_al = 1; }
        else { gfail(g, "relative addressing through register type %d", rrt); }
    }
    switch (rt) {
    case RT_TEMP:
        if (num > g->max_temp) g->max_temp = num;
        snprintf(o, n, "r%d", num);
        return;
    case RT_INPUT:
        if (rel) { gfail(g, "relative input register"); break; }
        if (num < 32) g->used_input[num] = 1;
        snprintf(o, n, g->ps ? "v%d" : "rs_v%d", num);
        return;
    case RT_CONST:
        if (write) break;
        if (!rel && num < 256 && g->sm->has_def[num]) {
            g->used_def[num] = 1;
            snprintf(o, n, "k%d", num);
            return;
        }
        if (rel) {
            g->const_rel = 1;
            snprintf(o, n, "%s[%s + %d]", g->ps ? "pc" : "vc", relx, num);
        } else {
            if (num + 1 > g->max_const) g->max_const = num + 1;
            snprintf(o, n, "%s[%d]", g->ps ? "pc" : "vc", num);
        }
        return;
    case RT_ADDR:  // == RT_TEXTURE
        if (g->ps) {
            if (num < 16) g->used_tex[num] = 1;
            snprintf(o, n, "t%d", num);
        } else {
            g->used_a0 = 1;
            snprintf(o, n, write ? "a0" : "vec4(a0)");
        }
        return;
    case RT_RASTOUT:
        if (num == 0) { g->used_opos = 1; snprintf(o, n, "o_pos"); return; }
        if (num == 1) { g->used_ofog = 1; snprintf(o, n, "o_fog"); return; }
        if (num == 2) { g->used_opts = 1; snprintf(o, n, "o_pts"); return; }
        break;
    case RT_ATTROUT:
        if (num < 2) { g->used_od[num] = 1; snprintf(o, n, "o_d%d", num); return; }
        break;
    case RT_OUTPUT:
        if (rel) { gfail(g, "relative output register"); break; }
        if (g->major >= 3) { if (num < 16) g->used_out[num] = 1; snprintf(o, n, "o%d", num); }
        else { if (num < 8) g->used_ot[num] = 1; snprintf(o, n, "o_t%d", num); }
        return;
    case RT_COLOROUT:
        if (num < 4) { g->used_oc[num] = 1; snprintf(o, n, "oC%d", num); return; }
        break;
    case RT_DEPTHOUT:
        g->used_odepth = 1;
        snprintf(o, n, "oDepth");
        return;
    case RT_MISC:
        if (num == 0) { g->used_vpos = 1; snprintf(o, n, "vPos"); return; }
        if (num == 1) { g->used_vface = 1; snprintf(o, n, "vFace"); return; }
        break;
    case RT_LOOP:
        g->used_al = 1;
        snprintf(o, n, "vec4(float(aL))");
        return;
    }
    gfail(g, "register type %d number %d not supported%s", rt, num, write ? " as a destination" : "");
    snprintf(o, n, "vec4(0.0)");
}

// A source operand as a vec4 expression: register, swizzle, modifier.
static void src_expr(gen *g, const param *pa, int off, char *o, size_t n) {
    char base[96];
    reg_name(g, pa, off, 0, base, sizeof base);
    uint32_t sw = (pa->tok >> 16) & 0xff;
    char swz[8] = "";
    if (sw != 0xE4) snprintf(swz, sizeof swz, ".%c%c%c%c", SW[sw & 3], SW[(sw >> 2) & 3], SW[(sw >> 4) & 3], SW[(sw >> 6) & 3]);
    char v[160];
    snprintf(v, sizeof v, "%s%s", base, swz);
    int mod = (pa->tok >> 24) & 0xf;
    switch (mod) {
    case 0: snprintf(o, n, "%s", v); break;
    case 1: snprintf(o, n, "(-%s)", v); break;
    case 2: snprintf(o, n, "(%s - 0.5)", v); break;
    case 3: snprintf(o, n, "(0.5 - %s)", v); break;
    case 4: snprintf(o, n, "(2.0 * %s - 1.0)", v); break;
    case 5: snprintf(o, n, "(1.0 - 2.0 * %s)", v); break;
    case 6: snprintf(o, n, "(1.0 - %s)", v); break;
    case 7: snprintf(o, n, "(2.0 * %s)", v); break;
    case 8: snprintf(o, n, "(-2.0 * %s)", v); break;
    case 11: snprintf(o, n, "abs(%s)", v); break;
    case 12: snprintf(o, n, "(-abs(%s))", v); break;
    default:
        gfail(g, "source modifier %d not supported", mod);
        snprintf(o, n, "%s", v);
    }
}

static const char *cmp_op(int c) {
    static const char *ops[] = {0, ">", "==", ">=", "<", "!=", "<="};
    return c >= 1 && c <= 6 ? ops[c] : 0;
}

static void indent(gen *g) {
    for (int k = 0; k < g->depth + 1; k++) sb_add(&g->body, "  ");
}

// dst = expr (a vec4 expression), under the write mask and the saturate modifier.
static void emit_assign(gen *g, const param *d, const char *expr) {
    char dn[96];
    reg_name(g, d, 0, 1, dn, sizeof dn);
    int mask = (d->tok >> 16) & 0xf;
    int sat = (d->tok >> 20) & 1;
    if (!mask) return;
    char m[6] = "";
    int nm = 0;
    if (mask != 0xf) {
        m[nm++] = '.';
        for (int k = 0; k < 4; k++)
            if (mask & (1 << k)) m[nm++] = SW[k];
        m[nm] = 0;
    }
    int is_a0 = !g->ps && rtype(d->tok) == RT_ADDR;
    indent(g);
    if (is_a0) {
        sb_add(&g->body, "%s%s = ivec4(floor(%s + 0.5))%s;\n", dn, m, expr, m);
    } else if (sat) {
        sb_add(&g->body, "%s%s = clamp(%s, 0.0, 1.0)%s;\n", dn, m, expr, m);
    } else {
        sb_add(&g->body, "%s%s = (%s)%s;\n", dn, m, expr, m);
    }
}

static void texld(gen *g, const ins *in, int kind) {
    // kind 0 plain, 1 proj, 2 bias, 3 lod, 4 grad
    char s0[256], sname[64];
    src_expr(g, &in->p[1], 0, s0, sizeof s0);
    const param *sp = &in->p[2];
    if (rtype(sp->tok) != RT_SAMPLER) { gfail(g, "texld without a sampler"); return; }
    int sn = rnum(sp->tok);
    snprintf(sname, sizeof sname, "s%d", sn);
    int ty = sn < 16 ? g->sm->info.sampler_type[sn] : 0;
    if (!ty) ty = 2;  // ps_2_0 may leave a sampler undeclared? treat as 2D
    char e[600];
    // GLSL 1.20 (and so the GLSL -> MSL path) has no explicit-LOD lookup in fragment shaders.
    // Scaleform's ps_3_0 filter shaders use texldl with LOD 0 on render targets that have one
    // level, where an implicit lookup is the same; anything else is approximate.
    if (kind == 3 && g->ps) kind = 0;
    if (ty == 2) {
        switch (kind) {
        case 0: snprintf(e, sizeof e, "texture2D(%s, (%s).xy)", sname, s0); break;
        case 1: snprintf(e, sizeof e, "texture2DProj(%s, %s)", sname, s0); break;
        case 2: snprintf(e, sizeof e, "texture2D(%s, (%s).xy, (%s).w)", sname, s0, s0); break;
        case 3: snprintf(e, sizeof e, "texture2DLod(%s, (%s).xy, (%s).w)", sname, s0, s0); break;
        default: gfail(g, "texldd not supported"); return;
        }
    } else if (ty == 3) {
        switch (kind) {
        case 0: snprintf(e, sizeof e, "textureCube(%s, (%s).xyz)", sname, s0); break;
        case 2: snprintf(e, sizeof e, "textureCube(%s, (%s).xyz, (%s).w)", sname, s0, s0); break;
        case 3: snprintf(e, sizeof e, "textureCubeLod(%s, (%s).xyz, (%s).w)", sname, s0, s0); break;
        default: gfail(g, "projected / gradient cube lookup not supported"); return;
        }
    } else {
        gfail(g, "sampler type %d (volume) not supported", ty);
        return;
    }
    uint32_t sw = (sp->tok >> 16) & 0xff;
    if (sw != 0xE4) {
        char t[700];
        snprintf(t, sizeof t, "%s.%c%c%c%c", e, SW[sw & 3], SW[(sw >> 2) & 3], SW[(sw >> 4) & 3], SW[(sw >> 6) & 3]);
        emit_assign(g, &in->p[0], t);
    } else {
        emit_assign(g, &in->p[0], e);
    }
}

// The integer constant iN's components as GLSL expressions.
static void int_const(gen *g, int n, int comp, char *o, size_t len) {
    if (n < 16 && g->sm->has_defi[n]) {
        snprintf(o, len, "%d", g->sm->defi[n][comp]);
        return;
    }
    if (comp != 0) {
        gfail(g, "loop needs the y/z of integer constant i%d, which the shader does not define", n);
        snprintf(o, len, "0");
        return;
    }
    g->used_int = 1;
    snprintf(o, len, "%s[%d]", g->ps ? "rs_pi" : "rs_vi", n);
}

static void gen_ins(gen *g, const ins *in) {
    char s[4][256], e[1400];
    int np = in->np;
    // sources start at p[1] for most instructions
#define SRC(k) (src_expr(g, &in->p[(k) + 1], 0, s[k], sizeof s[k]), s[k])
    switch (in->op) {
    case OP_NOP: return;
    case OP_MOV: SRC(0); emit_assign(g, &in->p[0], s[0]); return;
    case OP_MOVA: SRC(0); emit_assign(g, &in->p[0], s[0]); return;
    case OP_ADD: SRC(0); SRC(1); snprintf(e, sizeof e, "%s + %s", s[0], s[1]); break;
    case OP_SUB: SRC(0); SRC(1); snprintf(e, sizeof e, "%s - %s", s[0], s[1]); break;
    case OP_MUL: SRC(0); SRC(1); snprintf(e, sizeof e, "%s * %s", s[0], s[1]); break;
    case OP_MAD: SRC(0); SRC(1); SRC(2); snprintf(e, sizeof e, "%s * %s + %s", s[0], s[1], s[2]); break;
    case OP_MIN: SRC(0); SRC(1); snprintf(e, sizeof e, "min(%s, %s)", s[0], s[1]); break;
    case OP_MAX: SRC(0); SRC(1); snprintf(e, sizeof e, "max(%s, %s)", s[0], s[1]); break;
    case OP_SLT: SRC(0); SRC(1); snprintf(e, sizeof e, "vec4(1.0) - step(%s, %s)", s[1], s[0]); break;
    case OP_SGE: SRC(0); SRC(1); snprintf(e, sizeof e, "step(%s, %s)", s[1], s[0]); break;
    case OP_RCP: SRC(0); snprintf(e, sizeof e, "vec4(1.0 / (%s).x)", s[0]); break;
    case OP_RSQ: SRC(0); snprintf(e, sizeof e, "vec4(inversesqrt(abs((%s).x)))", s[0]); break;
    case OP_EXP: case OP_EXPP: SRC(0); snprintf(e, sizeof e, "vec4(exp2((%s).x))", s[0]); break;
    case OP_LOG: case OP_LOGP: SRC(0); snprintf(e, sizeof e, "vec4(log2(abs((%s).x)))", s[0]); break;
    case OP_POW: SRC(0); SRC(1); snprintf(e, sizeof e, "vec4(pow(abs((%s).x), (%s).x))", s[0], s[1]); break;
    case OP_DP3: SRC(0); SRC(1); snprintf(e, sizeof e, "vec4(dot((%s).xyz, (%s).xyz))", s[0], s[1]); break;
    case OP_DP4: SRC(0); SRC(1); snprintf(e, sizeof e, "vec4(dot(%s, %s))", s[0], s[1]); break;
    case OP_DP2ADD: SRC(0); SRC(1); SRC(2); snprintf(e, sizeof e, "vec4(dot((%s).xy, (%s).xy) + (%s).x)", s[0], s[1], s[2]); break;
    case OP_FRC: SRC(0); snprintf(e, sizeof e, "fract(%s)", s[0]); break;
    case OP_ABS: SRC(0); snprintf(e, sizeof e, "abs(%s)", s[0]); break;
    case OP_SGN: SRC(0); snprintf(e, sizeof e, "sign(%s)", s[0]); break;
    case OP_LRP: SRC(0); SRC(1); SRC(2); snprintf(e, sizeof e, "mix(%s, %s, %s)", s[2], s[1], s[0]); break;
    case OP_CMP: SRC(0); SRC(1); SRC(2); snprintf(e, sizeof e, "mix(%s, %s, step(0.0, %s))", s[2], s[1], s[0]); break;
    case OP_CND: SRC(0); SRC(1); SRC(2); snprintf(e, sizeof e, "mix(%s, %s, step(0.5000001, %s))", s[2], s[1], s[0]); break;
    case OP_NRM: SRC(0); snprintf(e, sizeof e, "(%s * inversesqrt(dot((%s).xyz, (%s).xyz)))", s[0], s[0], s[0]); break;
    case OP_CRS: SRC(0); SRC(1); snprintf(e, sizeof e, "vec4(cross((%s).xyz, (%s).xyz), 0.0)", s[0], s[1]); break;
    case OP_SINCOS: SRC(0); snprintf(e, sizeof e, "vec4(cos((%s).x), sin((%s).x), 0.0, 0.0)", s[0], s[0]); break;
    case OP_DSX: SRC(0); snprintf(e, sizeof e, "dFdx(%s)", s[0]); break;
    case OP_DSY: SRC(0); snprintf(e, sizeof e, "dFdy(%s)", s[0]); break;
    case OP_LIT:
        SRC(0);
        snprintf(e, sizeof e,
                 "vec4(1.0, max((%s).x, 0.0), ((%s).x > 0.0 && (%s).y > 0.0) ? pow((%s).y, clamp((%s).w, -128.0, 128.0)) : 0.0, 1.0)",
                 s[0], s[0], s[0], s[0], s[0]);
        break;
    case OP_DST: SRC(0); SRC(1); snprintf(e, sizeof e, "vec4(1.0, (%s).y * (%s).y, (%s).z, (%s).w)", s[0], s[1], s[0], s[1]); break;
    case OP_M4x4: case OP_M4x3: case OP_M3x4: case OP_M3x3: case OP_M3x2: {
        int rows = in->op == OP_M4x4 || in->op == OP_M3x4 ? 4 : in->op == OP_M3x2 ? 2 : 3;
        int three = in->op >= OP_M3x4;
        SRC(0);
        char r[4][300];
        for (int k = 0; k < 4; k++) {
            if (k < rows) {
                char m[256];
                src_expr(g, &in->p[2], k, m, sizeof m);
                if (three) snprintf(r[k], sizeof r[k], "dot((%s).xyz, (%s).xyz)", s[0], m);
                else snprintf(r[k], sizeof r[k], "dot(%s, %s)", s[0], m);
            } else {
                snprintf(r[k], sizeof r[k], "0.0");
            }
        }
        snprintf(e, sizeof e, "vec4(%s, %s, %s, %s)", r[0], r[1], r[2], r[3]);
        break;
    }
    case OP_TEX:
        if (g->ps && g->major >= 2) texld(g, in, (in->ctrl & 1) ? 1 : (in->ctrl & 2) ? 2 : 0);
        else gfail(g, "tex outside pixel shader model 2+");
        return;
    case OP_TEXLDL: texld(g, in, 3); return;
    case OP_TEXLDD: texld(g, in, 4); return;
    case OP_TEXKILL: {
        char dn[96];
        reg_name(g, &in->p[0], 0, 0, dn, sizeof dn);
        int mask = (in->p[0].tok >> 16) & 0xf;
        indent(g);
        if (!mask || mask == 0xf) {
            sb_add(&g->body, "if (any(lessThan(%s, vec4(0.0)))) discard;\n", dn);
        } else {
            sb_add(&g->body, "if (false");
            for (int k = 0; k < 4; k++)
                if (mask & (1 << k)) sb_add(&g->body, " || %s.%c < 0.0", dn, SW[k]);
            sb_add(&g->body, ") discard;\n");
        }
        return;
    }
    case OP_REP: {
        int n = rnum(in->p[0].tok);
        char cnt[64];
        int_const(g, n, 0, cnt, sizeof cnt);
        indent(g);
        int v = g->loopvar++;
        sb_add(&g->body, "for (int rs_k%d = 0; rs_k%d < %s; rs_k%d++) {\n", v, v, cnt, v);
        g->depth++;
        return;
    }
    case OP_LOOP: {
        // loop aL, iN: aL = iN.y; iN.x iterations; aL += iN.z
        int n = rnum(in->p[1].tok);
        char cnt[64], start[64], step[64];
        int_const(g, n, 0, cnt, sizeof cnt);
        int_const(g, n, 1, start, sizeof start);
        int_const(g, n, 2, step, sizeof step);
        g->used_al = 1;
        indent(g);
        int v = g->loopvar++;
        sb_add(&g->body, "aL = %s;\n", start);
        indent(g);
        sb_add(&g->body, "for (int rs_k%d = 0; rs_k%d < %s; rs_k%d++, aL += %s) {\n", v, v, cnt, v, step);
        g->depth++;
        return;
    }
    case OP_ENDREP: case OP_ENDLOOP: case OP_ENDIF:
        if (g->depth <= 0) { gfail(g, "unbalanced end of block"); return; }
        g->depth--;
        indent(g);
        sb_add(&g->body, "}\n");
        return;
    case OP_ELSE:
        if (g->depth <= 0) { gfail(g, "else outside if"); return; }
        g->depth--;
        indent(g);
        sb_add(&g->body, "} else {\n");
        g->depth++;
        return;
    case OP_BREAK:
        indent(g);
        sb_add(&g->body, "break;\n");
        return;
    case OP_BREAKC: case OP_IFC: {
        const char *op = cmp_op(in->ctrl & 7);
        if (!op) { gfail(g, "comparison %d", in->ctrl & 7); return; }
        src_expr(g, &in->p[0], 0, s[0], sizeof s[0]);
        src_expr(g, &in->p[1], 0, s[1], sizeof s[1]);
        indent(g);
        if (in->op == OP_BREAKC) {
            sb_add(&g->body, "if ((%s).x %s (%s).x) break;\n", s[0], op, s[1]);
        } else {
            sb_add(&g->body, "if ((%s).x %s (%s).x) {\n", s[0], op, s[1]);
            g->depth++;
        }
        return;
    }
    case OP_IF: {
        const param *b = &in->p[0];
        if (rtype(b->tok) != RT_CONSTBOOL) { gfail(g, "if on a predicate is not supported"); return; }
        int n = rnum(b->tok);
        indent(g);
        if (n < 16 && g->sm->has_defb[n]) {
            sb_add(&g->body, "if (%s) {\n", g->sm->defb[n] ? "true" : "false");
        } else {
            g->used_bool = 1;
            sb_add(&g->body, "if (%s[%d] != 0) {\n", g->ps ? "rs_pb" : "rs_vb", n);
        }
        g->depth++;
        return;
    }
    case OP_RET:
        if (g->depth == 0) return;  // the end of main
        gfail(g, "ret inside a block (subroutines are not supported)");
        return;
    case OP_CALL: case OP_CALLNZ: case OP_LABEL:
        gfail(g, "subroutines (call / label) are not supported");
        return;
    case OP_SETP: case OP_BREAKP:
        gfail(g, "predicates (setp / breakp) are not supported");
        return;
    default:
        gfail(g, "opcode %u not supported", in->op);
        return;
    }
    (void)np;
    emit_assign(g, &in->p[0], e);
#undef SRC
}

static const char *sem_name(int usage, int index, char *o, size_t n) {
    snprintf(o, n, "rs_%s%d", usage >= 0 && usage < 14 ? usage_name[usage] : "unknown", index);
    return o;
}

// The pixel shader's inputs as semantics: (usage, index) per input, in a list.
typedef struct {
    int usage, index;
    char local[16];  // the PS register it feeds ("v0", "t1")
} psin;

static int ps_inputs(const d3d9_sm *ps, psin *o, int max) {
    int n = 0;
    int ps3 = ps->info.major >= 3;
    for (int k = 0; k < ps->nin && n < max; k++) {
        const decl *d = &ps->in[k];
        if (ps3) {
            if (d->regtype != RT_INPUT) continue;
            o[n].usage = d->usage;
            o[n].index = d->index;
            snprintf(o[n].local, sizeof o[n].local, "v%d", d->num);
        } else if (d->regtype == RT_INPUT) {
            o[n].usage = U_COLOR;
            o[n].index = d->num;
            snprintf(o[n].local, sizeof o[n].local, "v%d", d->num);
        } else if (d->regtype == RT_TEXTURE) {
            o[n].usage = U_TEXCOORD;
            o[n].index = d->num;
            snprintf(o[n].local, sizeof o[n].local, "t%d", d->num);
        } else {
            continue;
        }
        n++;
    }
    return n;
}

int d3d9_sm_ps_semantics(const d3d9_sm *ps, d3d9_sm_input *out, int max) {
    psin pin[32];
    int n = ps_inputs(ps, pin, max < 32 ? max : 32);
    for (int k = 0; k < n; k++) out[k] = (d3d9_sm_input){1, pin[k].usage, pin[k].index};
    return n;
}

const char *d3d9_sm_varying(int usage, int index, char *o, size_t n) { return sem_name(usage, index, o, n); }

// The vertex shader register that carries (usage, index), or 0.
static const char *vs_output_for(gen *g, int usage, int index, char *o, size_t n) {
    const d3d9_sm *vs = g->sm;
    if (vs->info.major >= 3) {
        for (int k = 0; k < vs->nout; k++)
            if (vs->out[k].usage == usage && vs->out[k].index == index && vs->out[k].num < 16 && g->used_out[vs->out[k].num]) {
                snprintf(o, n, "o%d", vs->out[k].num);
                return o;
            }
        return 0;
    }
    if (usage == U_POSITION && index == 0 && g->used_opos) return snprintf(o, n, "o_pos"), o;
    if (usage == U_COLOR && index < 2 && g->used_od[index]) return snprintf(o, n, "o_d%d", index), o;
    if (usage == U_TEXCOORD && index < 8 && g->used_ot[index]) return snprintf(o, n, "o_t%d", index), o;
    if (usage == U_FOG && index == 0 && g->used_ofog) return snprintf(o, n, "o_fog"), o;
    if (usage == U_PSIZE && index == 0 && g->used_opts) return snprintf(o, n, "o_pts"), o;
    return 0;
}

char *d3d9_sm_glsl(const d3d9_sm *sm, const d3d9_sm *ps, char *err, size_t errlen) {
    gen g = {0};
    g.sm = sm;
    g.ps = sm->info.is_ps;
    g.major = sm->info.major;
    g.err = err;
    g.errlen = errlen;
    g.max_temp = -1;
    if (ps && !ps->info.is_ps) ps = 0;
    for (int k = 0; k < sm->ncode && !g.failed; k++) gen_ins(&g, &sm->code[k]);
    if (!g.failed && g.depth != 0) gfail(&g, "unterminated block");
    if (g.failed) {
        free(g.body.s);
        return 0;
    }
    sbuf o = {0};
    sb_add(&o, "#version 120\n");
    // float constants: the whole bank when indexed, else up to the highest one used
    int nconst = g.const_rel ? (g.ps ? (g.major >= 3 ? 224 : 32) : 256) : g.max_const;
    if (nconst) sb_add(&o, "uniform vec4 %s[%d];\n", g.ps ? "pc" : "vc", nconst);
    if (g.used_int) sb_add(&o, "uniform int %s[16];\n", g.ps ? "rs_pi" : "rs_vi");
    if (g.used_bool) sb_add(&o, "uniform int %s[16];\n", g.ps ? "rs_pb" : "rs_vb");
    ((d3d9_sm *)sm)->info.nconst = nconst;
    ((d3d9_sm *)sm)->info.uses_int = g.used_int;
    ((d3d9_sm *)sm)->info.uses_bool = g.used_bool;
    char nm[48], src[48];
    if (!g.ps) {
        sb_add(&o, "uniform vec4 rs_fix;\n");
        for (int k = 0; k < 16; k++)
            if (sm->info.input[k].used || g.used_input[k]) sb_add(&o, "attribute vec4 rs_v%d;\n", k);
        // varyings: the pixel shader's inputs, or everything written
        psin pin[32];
        int npin = 0;
        if (ps) {
            npin = ps_inputs(ps, pin, 32);
        } else if (g.major >= 3) {
            for (int k = 0; k < sm->nout; k++)
                if (sm->out[k].usage != U_POSITION || sm->out[k].index != 0)
                    pin[npin++] = (psin){sm->out[k].usage, sm->out[k].index, ""};
        } else {
            for (int k = 0; k < 2; k++) if (g.used_od[k]) pin[npin++] = (psin){U_COLOR, k, ""};
            for (int k = 0; k < 8; k++) if (g.used_ot[k]) pin[npin++] = (psin){U_TEXCOORD, k, ""};
            if (g.used_ofog) pin[npin++] = (psin){U_FOG, 0, ""};
        }
        for (int k = 0; k < npin; k++) sb_add(&o, "varying vec4 %s;\n", sem_name(pin[k].usage, pin[k].index, nm, sizeof nm));
        sb_add(&o, "void main() {\n");
        for (int k = 0; k <= g.max_temp; k++) sb_add(&o, "  vec4 r%d = vec4(0.0);\n", k);
        if (g.used_a0) sb_add(&o, "  ivec4 a0 = ivec4(0);\n");
        if (g.used_al) sb_add(&o, "  int aL = 0;\n");
        // the position: o_pos in 2_0, the POSITION0 output in 3_0
        const char *pos = "o_pos";
        if (g.major >= 3) {
            pos = 0;
            for (int k = 0; k < sm->nout; k++)
                if (sm->out[k].usage == U_POSITION && sm->out[k].index == 0) {
                    snprintf(src, sizeof src, "o%d", sm->out[k].num);
                    pos = src;
                    g.used_out[sm->out[k].num] = 1;
                }
            for (int k = 0; k < 16; k++)
                if (g.used_out[k]) sb_add(&o, "  vec4 o%d = vec4(0.0, 0.0, 0.0, 1.0);\n", k);
            if (!pos) {
                free(g.body.s);
                free(o.s);
                setf(err, errlen, "vs_3_0 without a POSITION0 output");
                return 0;
            }
        } else {
            sb_add(&o, "  vec4 o_pos = vec4(0.0, 0.0, 0.0, 1.0);\n");
            for (int k = 0; k < 2; k++) if (g.used_od[k]) sb_add(&o, "  vec4 o_d%d = vec4(0.0);\n", k);
            for (int k = 0; k < 8; k++) if (g.used_ot[k]) sb_add(&o, "  vec4 o_t%d = vec4(0.0);\n", k);
            if (g.used_ofog) sb_add(&o, "  vec4 o_fog = vec4(0.0);\n");
            if (g.used_opts) sb_add(&o, "  vec4 o_pts = vec4(0.0);\n");
        }
        char pv[48];
        snprintf(pv, sizeof pv, "%s", pos);
        for (int k = 0; k < 256; k++)
            if (g.used_def[k]) {
                char f[4][32];
                for (int j = 0; j < 4; j++) fmt_float(f[j], sizeof f[j], sm->defv[k][j]);
                sb_add(&o, "  const vec4 k%d = vec4(%s, %s, %s, %s);\n", k, f[0], f[1], f[2], f[3]);
            }
        sb_add(&o, "%s", g.body.s ? g.body.s : "");
        sb_add(&o, "  gl_Position = %s;\n", pv);
        sb_add(&o, "  gl_Position.x = %s.x + rs_fix.x * %s.w;\n", pv, pv);
        sb_add(&o, "  gl_Position.y = rs_fix.y * %s.w - %s.y;\n", pv, pv);
        sb_add(&o, "  gl_Position.z = %s.z * rs_fix.z + rs_fix.w * %s.w;\n", pv, pv);
        for (int k = 0; k < npin; k++) {
            const char *from = vs_output_for(&g, pin[k].usage, pin[k].index, src, sizeof src);
            sb_add(&o, "  %s = %s;\n", sem_name(pin[k].usage, pin[k].index, nm, sizeof nm), from ? from : "vec4(0.0)");
        }
        sb_add(&o, "}\n");
    } else {
        for (int k = 0; k < 16; k++) {
            int ty = sm->info.sampler_type[k];
            if (ty == 2) sb_add(&o, "uniform sampler2D s%d;\n", k);
            else if (ty == 3) sb_add(&o, "uniform samplerCube s%d;\n", k);
        }
        // ps_2_0 may sample a sampler it did not declare: declare it 2D
        psin pin[32];
        int npin = ps_inputs(sm, pin, 32);
        for (int k = 0; k < npin; k++) sb_add(&o, "varying vec4 %s;\n", sem_name(pin[k].usage, pin[k].index, nm, sizeof nm));
        sb_add(&o, "void main() {\n");
        for (int k = 0; k <= g.max_temp; k++) sb_add(&o, "  vec4 r%d = vec4(0.0);\n", k);
        if (g.used_al) sb_add(&o, "  int aL = 0;\n");
        int have[2][32] = {{0}};
        for (int k = 0; k < npin; k++) {
            sb_add(&o, "  vec4 %s = %s;\n", pin[k].local, sem_name(pin[k].usage, pin[k].index, nm, sizeof nm));
            int isv = pin[k].local[0] == 'v';
            int n = atoi(pin[k].local + 1);
            if (n < 32) have[isv][n] = 1;
        }
        for (int k = 0; k < 32; k++) if (g.used_input[k] && !have[1][k]) sb_add(&o, "  vec4 v%d = vec4(0.0);\n", k);
        for (int k = 0; k < 16; k++) if (g.used_tex[k] && !have[0][k]) sb_add(&o, "  vec4 t%d = vec4(0.0);\n", k);
        if (g.used_vpos) sb_add(&o, "  vec4 vPos = vec4(gl_FragCoord.xy - 0.5, 0.0, 0.0);\n");
        if (g.used_vface) sb_add(&o, "  vec4 vFace = vec4(gl_FrontFacing ? 1.0 : -1.0);\n");
        int maxoc = -1;
        for (int k = 0; k < 4; k++)
            if (g.used_oc[k]) {
                sb_add(&o, "  vec4 oC%d = vec4(0.0);\n", k);
                maxoc = k;
            }
        if (g.used_odepth) sb_add(&o, "  vec4 oDepth = vec4(gl_FragCoord.z);\n");
        for (int k = 0; k < 256; k++)
            if (g.used_def[k]) {
                char f[4][32];
                for (int j = 0; j < 4; j++) fmt_float(f[j], sizeof f[j], sm->defv[k][j]);
                sb_add(&o, "  const vec4 k%d = vec4(%s, %s, %s, %s);\n", k, f[0], f[1], f[2], f[3]);
            }
        sb_add(&o, "%s", g.body.s ? g.body.s : "");
        if (maxoc <= 0) {
            sb_add(&o, "  gl_FragColor = %s;\n", maxoc == 0 ? "oC0" : "vec4(0.0)");
        } else {
            for (int k = 0; k <= maxoc; k++) {
                if (g.used_oc[k]) sb_add(&o, "  gl_FragData[%d] = oC%d;\n", k, k);
                else sb_add(&o, "  gl_FragData[%d] = vec4(0.0);\n", k);
            }
        }
        if (g.used_odepth) sb_add(&o, "  gl_FragDepth = oDepth.x;\n");
        sb_add(&o, "}\n");
    }
    free(g.body.s);
    return o.s;
}
