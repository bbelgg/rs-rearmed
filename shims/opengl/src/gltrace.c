// Per-frame GL call tracer for libRSOpenGL / libglspy. Diagnostic only: it
// observes calls (and reads state back), it never changes what is drawn.
//
//   RS_GL_TRACE_DIR=<dir>      enable; one <dir>/frame_NNNNNN.gltrace (+ .png) per traced frame
//   RS_GL_TRACE_START=<secs>   start this long after the first presented frame
//   RS_GL_TRACE_KEY=1|<path>   start when <dir>/go (or <path>) exists; the file is consumed
//   RS_GL_TRACE_FRAMES=<n>     number of consecutive frames to record (default 30)
//   (neither START nor KEY: start at the second frame)
//
// Shadow state (bound program/textures/buffers/FBO, arrays, ARB parameters,
// texture sizes, ...) is kept from process start while the tracer is armed, so
// a trace window can start at any time. The spy build (libglspy) additionally
// interposes the ARB program / client-array entry points that Cg.framework
// calls and the game does not import; the shim build only sees what the game
// itself imports.
#include "gl_api.h"
#include "gltrace.h"

#include <dlfcn.h>
#include <math.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#ifdef RS_GL_SPY
#include "../../include/interpose.h"
#endif

int rs_tr_on;

#define GL_TRUE_ 1
#define E_ARRAY_BUFFER 0x8892
#define E_ELEMENT_ARRAY_BUFFER 0x8893
#define E_TEXTURE_2D 0x0DE1
#define E_CUBE_MAP 0x8513
#define E_TEXTURE_3D 0x806F
#define E_TEXTURE_RECT 0x84F5
#define E_FRAMEBUFFER 0x8D40
#define E_READ_FRAMEBUFFER 0x8CA8
#define E_DRAW_FRAMEBUFFER 0x8CA9
#define E_VERTEX_PROGRAM 0x8620
#define E_FRAGMENT_PROGRAM 0x8804
#define E_FLOAT 0x1406
#define E_DOUBLE 0x140A

static void *const *rs_r;  // real functions by RS_GL_IDX_*
static const char *const *rs_nm;
#define REAL(name) ((typeof(&name))rs_r[RS_GL_IDX_##name])

// --- Enum names -----------------------------------------------------------------
static const struct { unsigned v; const char *n; } kEnums[] = {
#include "gl_enums.inc"
};

static const char *enum_name(unsigned v) {
    size_t lo = 0, hi = sizeof kEnums / sizeof kEnums[0];
    while (lo < hi) {
        size_t m = (lo + hi) / 2;
        if (kEnums[m].v < v) lo = m + 1; else hi = m;
    }
    return lo < sizeof kEnums / sizeof kEnums[0] && kEnums[lo].v == v ? kEnums[lo].n : NULL;
}

static const char *mode_name(unsigned m) {
    static const char *const n[] = {"GL_POINTS", "GL_LINES", "GL_LINE_LOOP", "GL_LINE_STRIP",
                                    "GL_TRIANGLES", "GL_TRIANGLE_STRIP", "GL_TRIANGLE_FAN",
                                    "GL_QUADS", "GL_QUAD_STRIP", "GL_POLYGON"};
    return m < 10 ? n[m] : enum_name(m);
}

static const char *ename(unsigned v, char *buf) {
    const char *n = v >= 0x100 ? enum_name(v) : NULL;
    if (n) return n + 3;  // drop "GL_"
    snprintf(buf, 16, "0x%x", v);
    return buf;
}

static const char *bfname(unsigned f, char *buf) {  // blend factor
    if (f == 0) return "ZERO";
    if (f == 1) return "ONE";
    return ename(f, buf);
}

// --- Parameter metadata (parsed from the wrapper's parameter list) ----------------
enum { K_SINT, K_UINT, K_ENUM, K_BOOL, K_BITF, K_PTR, K_FLT };
struct meta { int n; uint8_t kind[12]; int done; };
static struct meta rs_meta[RS_GL_COUNT];

static void parse_params(int idx, const char *params) {
    struct meta *m = &rs_meta[idx];
    char buf[512];
    snprintf(buf, sizeof buf, "%s", params);
    char *s = buf;
    while (*s == '(' || *s == ' ') s++;
    char *e = s + strlen(s);
    while (e > s && (e[-1] == ')' || e[-1] == ' ')) *--e = 0;
    m->n = 0;
    if (strcmp(s, "void") != 0 && *s) {
        for (char *p = strtok(s, ","); p && m->n < 12; p = strtok(NULL, ",")) {
            while (*p == ' ') p++;
            int k = K_SINT;
            if (strchr(p, '*')) k = K_PTR;
            else if (!strncmp(p, "GLenum", 6)) k = K_ENUM;
            else if (!strncmp(p, "GLboolean", 9)) k = K_BOOL;
            else if (!strncmp(p, "GLbitfield", 10)) k = K_BITF;
            else if (!strncmp(p, "GLfloat", 7) || !strncmp(p, "GLclampf", 8) ||
                     !strncmp(p, "GLdouble", 8) || !strncmp(p, "GLclampd", 8)) k = K_FLT;
            else if (!strncmp(p, "GLuint", 6) || !strncmp(p, "GLushort", 8) ||
                     !strncmp(p, "GLubyte", 7)) k = K_UINT;
            m->kind[m->n++] = (uint8_t)k;
        }
    }
    m->done = 1;
}

static double argf(const struct rs_targ *a) {
    if (a->sz == 4) { float f; memcpy(&f, &a->u, 4); return f; }
    double d; memcpy(&d, &a->u, 8); return d;
}
static int64_t argi(const struct rs_targ *a) {
    switch (a->sz) {
    case 1: return (int8_t)a->u;
    case 2: return (int16_t)a->u;
    case 4: return (int32_t)a->u;
    default: return (int64_t)a->u;
    }
}
#define AU(i) ((unsigned)a[i].u)
#define AI(i) ((int)argi(&a[i]))
#define AF(i) ((float)argf(&a[i]))
#define AP(i) ((const void *)(uintptr_t)a[i].u)

// --- Output ---------------------------------------------------------------------------
static FILE *tr_fp;
static int tr_rec;
static pthread_mutex_t tr_mu = PTHREAD_MUTEX_INITIALIZER;
static const char *tr_dir;
static unsigned long tr_frame;
static long long tr_t0us;

static long long now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

static void out(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void out(const char *fmt, ...) {
    if (!tr_fp) return;
    va_list ap;
    va_start(ap, fmt);
    pthread_mutex_lock(&tr_mu);
    vfprintf(tr_fp, fmt, ap);
    pthread_mutex_unlock(&tr_mu);
    va_end(ap);
}

static uint64_t fnv(const void *p, size_t n, uint64_t h) {
    const unsigned char *b = p;
    for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 0x100000001b3ULL; }
    return h;
}
#define FNV0 0xcbf29ce484222325ULL

// --- Shadow state -----------------------------------------------------------------------
#define NID 65536
#define NSLOT 32
struct arr { uint8_t en, norm; int size; unsigned type; int stride; const void *ptr; unsigned buf, div; };
struct vao { struct arr a[NSLOT]; unsigned elem; };
static struct vao *vaos[4096];
static unsigned cur_vao, buf_array, client_tex;

static struct vao *vao_get(unsigned id) {
    if (id >= 4096) id = 0;
    if (!vaos[id]) vaos[id] = calloc(1, sizeof(struct vao));
    return vaos[id];
}

struct cap { unsigned e; uint8_t v; };
static struct cap caps[96];
static int ncaps;
static int cap_get(unsigned e) {
    for (int i = 0; i < ncaps; i++) if (caps[i].e == e) return caps[i].v;
    return 0;
}
static void cap_set(unsigned e, int v) {
    for (int i = 0; i < ncaps; i++) if (caps[i].e == e) { caps[i].v = (uint8_t)v; return; }
    if (ncaps < 96) caps[ncaps++] = (struct cap){e, (uint8_t)v};
}

static struct {
    unsigned depth_func, cull, front, alpha_func, poly_mode, blend_eq;
    int depth_mask, cmask[4];
    unsigned bsrc, bdst, bsrca, bdsta;
    float alpha_ref, clear[4];
    int vp[4], sc[4];
    unsigned prog, fbo_draw, fbo_read, rbo, active_tex;
} st = {.depth_func = 0x201, .cull = 0x405, .front = 0x901, .alpha_func = 0x207, .poly_mode = 0x1B02,
        .blend_eq = 0x8006, .depth_mask = 1, .cmask = {1, 1, 1, 1}, .bsrc = 1, .bdst = 0,
        .bsrca = 1, .bdsta = 0, .active_tex = 0x84C0};

struct tinfo { int w, h; unsigned fmt; };
static struct tinfo tex_info[NID], rb_info[NID];
static unsigned tex_bound[32][4];  // [unit][2D, cube, 3D, rect]
static unsigned tex_slot(unsigned t) {
    switch (t) {
    case E_TEXTURE_2D: return 0;
    case E_CUBE_MAP: return 1;
    case E_TEXTURE_3D: return 2;
    case E_TEXTURE_RECT: return 3;
    default: return (t >= 0x8515 && t <= 0x851A) ? 1 : 0xff;
    }
}

struct att { uint8_t kind; unsigned id; };  // kind 1 texture, 2 renderbuffer
static struct att fbo_att[1024][11];
static int att_slot(unsigned a) {
    if (a >= 0x8CE0 && a < 0x8CE8) return (int)(a - 0x8CE0);
    if (a == 0x8D00) return 8;
    if (a == 0x8D20) return 9;
    if (a == 0x821A) return 10;
    return -1;
}

static uint32_t mapped_bits[(1 << 20) / 32];
static unsigned map_target_buf(unsigned target) {
    return target == E_ELEMENT_ARRAY_BUFFER ? vao_get(cur_vao)->elem : buf_array;
}
static void map_set(unsigned id, int v) {
    if (id >= (1u << 20)) return;
    if (v) mapped_bits[id / 32] |= 1u << (id % 32); else mapped_bits[id / 32] &= ~(1u << (id % 32));
}
static int map_get(unsigned id) {
    return id < (1u << 20) && (mapped_bits[id / 32] >> (id % 32) & 1);
}

// GLSL programs: hash of the attached shaders' sources.
static uint64_t sh_hash[NID], prog_hash[NID];
static unsigned prog_sh[NID][6];
static int prog_nsh[NID];

// ARB programs
struct arbprog {
    float loc[256][4];
    uint64_t set[4];
    uint64_t hash;
    int nlocal;  // highest program.local index + 1 in the program text
    float last[256][4];  // last values written to the trace
    unsigned long dumped_frame;
};
static struct arbprog *arb[2][NID];  // [0] vertex, [1] fragment
static unsigned arb_bound[2];
static float arb_env[2][256][4];
static uint64_t arb_env_set[2][4];

static struct arbprog *arb_get(int t, unsigned id) {
    if (id >= NID) return NULL;
    if (!arb[t][id]) arb[t][id] = calloc(1, sizeof(struct arbprog));
    return arb[t][id];
}
static int arb_t(unsigned target) { return target == E_FRAGMENT_PROGRAM; }

// --- Real GL helpers ---------------------------------------------------------------------
static void (*r_GetBufferSubData)(unsigned, long, long, void *);
static unsigned char (*r_IsEnabled)(unsigned);
static void (*r_BindBuffer)(unsigned, unsigned);
static unsigned (*r_GetError)(void);
static void (*r_GetLocalParam)(unsigned, unsigned, float *);

static void err_begin(void) {  // keep the game's pending error out of our queries
    unsigned e = r_GetError();
    if (e) rs_tr_stash_error(e);
}
static void err_end(void) { r_GetError(); }

// Read `len` bytes of buffer `buf` (0 = client memory at `off`). Returns 0 on failure.
static int read_buf(unsigned target, unsigned buf, uintptr_t off, size_t len, void *dst) {
    if (!buf) {
        if (!off) return 0;
        memcpy(dst, (const void *)off, len);
        return 1;
    }
    if (map_get(buf) || !r_GetBufferSubData) return 0;
    unsigned prev = target == E_ELEMENT_ARRAY_BUFFER ? vao_get(cur_vao)->elem : buf_array;
    err_begin();
    r_BindBuffer(target, buf);
    memset(dst, 0, len);
    r_GetBufferSubData(target, (long)off, (long)len, dst);
    r_BindBuffer(target, prev);
    unsigned e = r_GetError();
    return e == 0;
}

// --- Value formatting ------------------------------------------------------------------------
static size_t type_size(unsigned t) {
    switch (t) {
    case 0x1400: case 0x1401: return 1;
    case 0x1402: case 0x1403: case 0x140B: return 2;
    case 0x1404: case 0x1405: case E_FLOAT: return 4;
    case E_DOUBLE: return 8;
    default: return 4;
    }
}

static double comp(const unsigned char *p, unsigned t) {
    switch (t) {
    case 0x1400: return (int8_t)p[0];
    case 0x1401: return p[0];
    case 0x1402: { int16_t v; memcpy(&v, p, 2); return v; }
    case 0x1403: { uint16_t v; memcpy(&v, p, 2); return v; }
    case 0x1404: { int32_t v; memcpy(&v, p, 4); return v; }
    case 0x1405: { uint32_t v; memcpy(&v, p, 4); return v; }
    case E_FLOAT: { float v; memcpy(&v, p, 4); return v; }
    case E_DOUBLE: { double v; memcpy(&v, p, 8); return v; }
    case 0x140B: { uint16_t h; memcpy(&h, p, 2);
        int s = h >> 15, e = h >> 10 & 31, m = h & 1023;
        double v = e == 0 ? ldexp(m, -24) : e == 31 ? (m ? NAN : INFINITY) : ldexp(1024 + m, e - 25);
        return s ? -v : v; }
    default: return 0;
    }
}

static void fmt_vals(char *dst, size_t cap, const float *v, int n) {
    size_t o = 0;
    for (int i = 0; i < n && o + 24 < cap; i++)
        o += (size_t)snprintf(dst + o, cap - o, "%s%.9g", i ? "," : "", v[i]);
    if (!n && cap) dst[0] = 0;
}

// --- Call logging --------------------------------------------------------------------------
static void log_call(int idx, const struct rs_targ *a, uint64_t ret) {
    struct meta *m = &rs_meta[idx];
    char b[1400], nb[16];
    size_t o = 0;
    o += (size_t)snprintf(b + o, sizeof b - o, "@%lld %s(", now_us() - tr_t0us, rs_nm[idx]);
    for (int i = 0; i < m->n && o < sizeof b - 64; i++) {
        const struct rs_targ *x = &a[i + 1];
        const char *sep = i ? ", " : "";
        int k = m->kind[i];
        if (x->kind == 8 || k == K_FLT)
            o += (size_t)snprintf(b + o, sizeof b - o, "%s%.9g", sep, argf(x));
        else if (k == K_PTR || x->kind == 5)
            o += (size_t)snprintf(b + o, sizeof b - o, x->u ? "%s%#llx" : "%s%.0lld", sep, (unsigned long long)x->u);
        else if (k == K_ENUM) {
            unsigned v = (unsigned)x->u;
            const char *n;
            if (i == 0 && (idx == RS_GL_IDX_glDrawArrays || idx == RS_GL_IDX_glDrawElements ||
                           idx == RS_GL_IDX_glDrawArraysInstancedARB ||
                           idx == RS_GL_IDX_glDrawElementsInstanced ||
                           idx == RS_GL_IDX_glDrawElementsInstancedARB))
                n = mode_name(v) ? mode_name(v) + 3 : ename(v, nb);
            else if (idx == RS_GL_IDX_glBlendFunc || idx == RS_GL_IDX_glBlendFuncSeparate)
                n = bfname(v, nb);
            else
                n = ename(v, nb);
            o += (size_t)snprintf(b + o, sizeof b - o, "%s%s", sep, n);
        } else if (k == K_BOOL)
            o += (size_t)snprintf(b + o, sizeof b - o, "%s%s", sep, (x->u & 0xff) ? "true" : "false");
        else if (k == K_BITF)
            o += (size_t)snprintf(b + o, sizeof b - o, "%s%#x", sep, (unsigned)x->u);
        else if (k == K_UINT)
            o += (size_t)snprintf(b + o, sizeof b - o, "%s%llu", sep, (unsigned long long)(x->sz == 4 ? (uint32_t)x->u : x->u));
        else
            o += (size_t)snprintf(b + o, sizeof b - o, "%s%lld", sep, (long long)argi(x));
    }
    o += (size_t)snprintf(b + o, sizeof b - o, ")");
    if (ret || idx == RS_GL_IDX_glGetError || idx == RS_GL_IDX_glCreateProgram ||
        idx == RS_GL_IDX_glCreateShader || idx == RS_GL_IDX_glMapBuffer ||
        idx == RS_GL_IDX_glGetUniformLocation || idx == RS_GL_IDX_glCheckFramebufferStatus)
        o += (size_t)snprintf(b + o, sizeof b - o, " = %#llx", (unsigned long long)ret);

    // Argument-specific payloads.
    float f[16];
    char vb[400];
    switch (idx) {
    case RS_GL_IDX_glUniform1fv: case RS_GL_IDX_glUniform2fv:
    case RS_GL_IDX_glUniform3fv: case RS_GL_IDX_glUniform4fv: {
        int comps = idx == RS_GL_IDX_glUniform1fv ? 1 : idx == RS_GL_IDX_glUniform2fv ? 2 : idx == RS_GL_IDX_glUniform3fv ? 3 : 4;
        int n = AI(2) * comps;
        if (n > 16) n = 16;
        if (AP(3) && n > 0) { memcpy(f, AP(3), (size_t)n * 4); fmt_vals(vb, sizeof vb, f, n);
            o += (size_t)snprintf(b + o, sizeof b - o, " v=[%s]", vb); }
        break; }
    case RS_GL_IDX_glUniform1iv: {
        const int *p = AP(3);
        int n = AI(2) > 8 ? 8 : AI(2);
        o += (size_t)snprintf(b + o, sizeof b - o, " v=[");
        for (int i = 0; p && i < n; i++) o += (size_t)snprintf(b + o, sizeof b - o, "%s%d", i ? "," : "", p[i]);
        o += (size_t)snprintf(b + o, sizeof b - o, "]");
        break; }
    case RS_GL_IDX_glUniformMatrix4fv:
        if (AP(4) && AI(2) > 0) { memcpy(f, AP(4), 64); fmt_vals(vb, sizeof vb, f, 16);
            o += (size_t)snprintf(b + o, sizeof b - o, " v=[%s]", vb); }
        break;
    case RS_GL_IDX_glBufferData: case RS_GL_IDX_glBufferSubData: {
        const void *d = idx == RS_GL_IDX_glBufferData ? AP(3) : AP(4);
        long sz = idx == RS_GL_IDX_glBufferData ? AI(2) : (long)argi(&a[3]);
        unsigned bufid = map_target_buf(AU(1));
        o += (size_t)snprintf(b + o, sizeof b - o, " buf=%u", bufid);
        if (d && sz > 0) {
            size_t n = (size_t)sz > (1u << 20) ? (1u << 20) : (size_t)sz;
            long nan = 0, big = 0;
            if (AU(1) == E_ARRAY_BUFFER) {
                const float *fl = d;
                for (size_t i = 0; i < n / 4; i++) {
                    float v = fl[i];
                    if (!isfinite(v)) nan++; else if (fabsf(v) > 1e7f) big++;
                }
            }
            o += (size_t)snprintf(b + o, sizeof b - o, " hash=%016llx", (unsigned long long)fnv(d, n, FNV0));
            if (AU(1) == E_ARRAY_BUFFER) {
                o += (size_t)snprintf(b + o, sizeof b - o, " nonfinite_f32=%ld big_f32=%ld", nan, big);
                int nf = n >= 32 ? 8 : (int)(n / 4);
                memcpy(f, d, (size_t)nf * 4);
                fmt_vals(vb, sizeof vb, f, nf);
                o += (size_t)snprintf(b + o, sizeof b - o, " f32=[%s]", vb);
            }
        }
        break; }
    case RS_GL_IDX_glGenBuffers: case RS_GL_IDX_glGenTextures: case RS_GL_IDX_glGenFramebuffers:
    case RS_GL_IDX_glGenRenderbuffers: case RS_GL_IDX_glGenVertexArrays: {
        const unsigned *p = AP(2);
        o += (size_t)snprintf(b + o, sizeof b - o, " ->");
        for (int i = 0; p && i < AI(1) && i < 8; i++) o += (size_t)snprintf(b + o, sizeof b - o, " %u", p[i]);
        break; }
    default: break;
    }
    out("%s\n", b);
}

// --- Draw dump ------------------------------------------------------------------------------------
static const char *const kSlotName[NSLOT] = {
    "pos", "normal", "color", "color2", "fogc", "s5", "s6", "s7",
    "tc0", "tc1", "tc2", "tc3", "tc4", "tc5", "tc6", "tc7",
    "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7", "a8", "a9", "a10", "a11", "a12", "a13", "a14", "a15"};

#define CAP_BYTES (4u << 20)

static void dump_arrays(unsigned first, unsigned last, unsigned nvert_inst) {
    struct vao *v = vao_get(cur_vao);
    for (int s = 0; s < NSLOT; s++) {
        struct arr *r = &v->a[s];
        if (!r->en || r->size <= 0) continue;
        size_t es = (size_t)r->size * type_size(r->type);
        size_t stride = r->stride ? (size_t)r->stride : es;
        unsigned lo = first, cnt = last >= first ? last - first + 1 : 0;
        if (r->div) { lo = 0; cnt = (nvert_inst + r->div - 1) / r->div; if (cnt > 4096) cnt = 4096; }
        if (cnt == 0) continue;
        int trunc = 0;
        if ((cnt - 1) * stride + es > CAP_BYTES) { cnt = (unsigned)((CAP_BYTES - es) / stride) + 1; trunc = 1; }
        size_t len = (cnt - 1) * stride + es;
        uintptr_t off = (uintptr_t)r->ptr + (uintptr_t)lo * stride;
        unsigned char *data = malloc(len);
        char line[900];
        size_t o = (size_t)snprintf(line, sizeof line,
            "  A %s size=%d type=%s%s stride=%d buf=%u off=%#lx div=%u n=%u", kSlotName[s], r->size,
            ename(r->type, line + 800), r->norm ? "(norm)" : "", r->stride, r->buf,
            (unsigned long)(uintptr_t)r->ptr, r->div, cnt);
        if (!data || !read_buf(E_ARRAY_BUFFER, r->buf, off, len, data)) {
            o += (size_t)snprintf(line + o, sizeof line - o, " READFAIL%s", r->buf && map_get(r->buf) ? "(mapped)" : "");
            out("%s\n", line);
            free(data);
            continue;
        }
        uint64_t h = fnv(data, len, FNV0);
        long nan = 0, inf = 0, big = 0;
        double mn[4] = {1e300, 1e300, 1e300, 1e300}, mx[4] = {-1e300, -1e300, -1e300, -1e300};
        int isf = r->type == E_FLOAT || r->type == E_DOUBLE || r->type == 0x140B;
        if (isf)
            for (unsigned j = 0; j < cnt; j++)
                for (int c = 0; c < r->size; c++) {
                    double x = comp(data + (size_t)j * stride + (size_t)c * type_size(r->type), r->type);
                    if (isnan(x)) nan++;
                    else if (isinf(x)) inf++;
                    else {
                        if (fabs(x) > 1e7) big++;
                        if (c < 4) { if (x < mn[c]) mn[c] = x; if (x > mx[c]) mx[c] = x; }
                    }
                }
        o += (size_t)snprintf(line + o, sizeof line - o, " hash=%016llx%s", (unsigned long long)h, trunc ? "(trunc)" : "");
        if (isf) {
            o += (size_t)snprintf(line + o, sizeof line - o, " nan=%ld inf=%ld big=%ld min=[", nan, inf, big);
            for (int c = 0; c < r->size && c < 4; c++)
                o += (size_t)snprintf(line + o, sizeof line - o, "%s%.6g", c ? "," : "", mn[c] > mx[c] ? 0.0 : mn[c]);
            o += (size_t)snprintf(line + o, sizeof line - o, "] max=[");
            for (int c = 0; c < r->size && c < 4; c++)
                o += (size_t)snprintf(line + o, sizeof line - o, "%s%.6g", c ? "," : "", mn[c] > mx[c] ? 0.0 : mx[c]);
            o += (size_t)snprintf(line + o, sizeof line - o, "]");
        }
        for (unsigned j = 0; j < cnt && j < 4; j++) {
            o += (size_t)snprintf(line + o, sizeof line - o, " v%u=(", j);
            for (int c = 0; c < r->size && c < 4; c++)
                o += (size_t)snprintf(line + o, sizeof line - o, "%s%.6g", c ? "," : "",
                                      comp(data + (size_t)j * stride + (size_t)c * type_size(r->type), r->type));
            o += (size_t)snprintf(line + o, sizeof line - o, ")");
        }
        out("%s\n", line);
        free(data);
    }
}

// Cg (NVIDIA's runtime, notably for matrices) can reach GL through paths the
// interposer doesn't see, so ask GL for the real values of the bound program.
static void refresh_local(int t) {
    struct arbprog *p = arb[t][arb_bound[t]];
    if (!p || !arb_bound[t] || !r_GetLocalParam) return;
    for (int i = 0; i < p->nlocal && i < 256; i++) {
        float v[4] = {0, 0, 0, 0};
        r_GetLocalParam(t ? E_FRAGMENT_PROGRAM : E_VERTEX_PROGRAM, (unsigned)i, v);
        if (memcmp(v, p->loc[i], 16) != 0 || !((p->set[i / 64] >> (i % 64)) & 1)) {
            if (!((p->set[i / 64] >> (i % 64)) & 1) && v[0] == 0 && v[1] == 0 && v[2] == 0 && v[3] == 0) continue;
            memcpy(p->loc[i], v, 16);
            p->set[i / 64] |= 1ULL << (i % 64);
        }
    }
}

static void dump_arb_updates(int t) {
    struct arbprog *p = arb[t][arb_bound[t]];
    if (!p || !arb_bound[t]) return;
    char line[4096];
    size_t o = (size_t)snprintf(line, sizeof line, "  PU %s %u", t ? "fp" : "vp", arb_bound[t]);
    int any = 0;
    for (int i = 0; i < 256; i++)
        if (((p->set[i / 64] >> (i % 64)) & 1) && memcmp(p->last[i], p->loc[i], 16) != 0 && o + 120 < sizeof line) {
            memcpy(p->last[i], p->loc[i], 16);
            any = 1;
            o += (size_t)snprintf(line + o, sizeof line - o, " %d:%.9g,%.9g,%.9g,%.9g", i, p->loc[i][0], p->loc[i][1], p->loc[i][2], p->loc[i][3]);
        }
    if (any) out("%s\n", line);
}

static void dump_arb_tables(int t) {
    struct arbprog *p = arb[t][arb_bound[t]];
    if (!p || !arb_bound[t]) return;
    refresh_local(t);
    if (p->dumped_frame == tr_frame + 1) { dump_arb_updates(t); return; }
    p->dumped_frame = tr_frame + 1;
    memcpy(p->last, p->loc, sizeof p->last);
    char line[8192];
    size_t o = (size_t)snprintf(line, sizeof line, "  PT %s %u hash=%016llx", t ? "fp" : "vp", arb_bound[t],
                                (unsigned long long)p->hash);
    for (int i = 0; i < 256; i++)
        if ((p->set[i / 64] >> (i % 64)) & 1 && o + 120 < sizeof line)
            o += (size_t)snprintf(line + o, sizeof line - o, " %d:%.9g,%.9g,%.9g,%.9g", i, p->loc[i][0],
                                  p->loc[i][1], p->loc[i][2], p->loc[i][3]);
    out("%s\n", line);
}

static void dump_env_tables(void) {
    static unsigned long done_frame = ~0ul;
    if (done_frame == tr_frame) return;
    done_frame = tr_frame;
    for (int t = 0; t < 2; t++) {
        char line[8192];
        size_t o = (size_t)snprintf(line, sizeof line, "  ET %s", t ? "fp" : "vp");
        int any = 0;
        for (int i = 0; i < 256; i++)
            if ((arb_env_set[t][i / 64] >> (i % 64)) & 1 && o + 120 < sizeof line) {
                any = 1;
                o += (size_t)snprintf(line + o, sizeof line - o, " %d:%.9g,%.9g,%.9g,%.9g", i, arb_env[t][i][0],
                                      arb_env[t][i][1], arb_env[t][i][2], arb_env[t][i][3]);
            }
        if (any) out("%s\n", line);
    }
}

static void dump_draw(int idx, const struct rs_targ *a) {
    char nb[16], nb2[16], nb3[16], nb4[16];
    err_begin();
    out("  S glprog=%u gh=%016llx vp=%u vph=%016llx fp=%u fph=%016llx fbo=%u\n", st.prog,
        (unsigned long long)(st.prog < NID ? prog_hash[st.prog] : 0), arb_bound[0],
        (unsigned long long)(arb[0][arb_bound[0] % NID] ? arb[0][arb_bound[0] % NID]->hash : 0), arb_bound[1],
        (unsigned long long)(arb[1][arb_bound[1] % NID] ? arb[1][arb_bound[1] % NID]->hash : 0), st.fbo_draw);
    char cl[400];
    size_t o = 0;
    static const unsigned kCaps[] = {0x0B71, 0x0BE2, 0x0B44, 0x0BC0, 0x0C11, 0x0B90, E_VERTEX_PROGRAM,
                                     E_FRAGMENT_PROGRAM, 0x8037, 0x0DE1, 0x0B50};
    for (size_t i = 0; i < sizeof kCaps / sizeof kCaps[0]; i++)
        if (cap_get(kCaps[i])) o += (size_t)snprintf(cl + o, sizeof cl - o, "%s%s", o ? "," : "", enum_name(kCaps[i]) + 3);
    cl[o] = 0;
    out("  R vp=%d,%d,%d,%d sc=%d,%d,%d,%d caps=%s depth=%s/mask%d blend=%s,%s,%s,%s/%s cull=%s/%s cmask=%d%d%d%d alpha=%s,%g poly=%s\n",
        st.vp[0], st.vp[1], st.vp[2], st.vp[3], st.sc[0], st.sc[1], st.sc[2], st.sc[3], cl,
        ename(st.depth_func, nb), st.depth_mask, bfname(st.bsrc, nb2), bfname(st.bdst, nb3),
        bfname(st.bsrca, nb4), bfname(st.bdsta, nb), ename(st.blend_eq, nb2), ename(st.cull, nb3),
        ename(st.front, nb4), st.cmask[0], st.cmask[1], st.cmask[2], st.cmask[3],
        ename(st.alpha_func, nb), st.alpha_ref, ename(st.poly_mode, nb2));
    char tl[1200];
    o = 0;
    for (int u = 0; u < 32; u++)
        for (int s = 0; s < 4; s++)
            if (tex_bound[u][s] && o + 80 < sizeof tl) {
                unsigned id = tex_bound[u][s];
                struct tinfo *ti = id < NID ? &tex_info[id] : &tex_info[0];
                o += (size_t)snprintf(tl + o, sizeof tl - o, "%su%d:%s:%u(%dx%d,%s)", o ? " " : "", u,
                                      s == 0 ? "2D" : s == 1 ? "cube" : s == 2 ? "3D" : "rect", id, ti->w, ti->h,
                                      ti->fmt ? ename(ti->fmt, nb) : "?");
            }
    tl[o] = 0;
    out("  T %s\n", tl);
    if (st.fbo_draw && st.fbo_draw < 1024) {
        char fl[600];
        o = 0;
        static const char *const an[11] = {"c0", "c1", "c2", "c3", "c4", "c5", "c6", "c7", "depth", "stencil", "ds"};
        for (int s = 0; s < 11; s++) {
            struct att *at = &fbo_att[st.fbo_draw][s];
            if (!at->kind || o + 80 >= sizeof fl) continue;
            struct tinfo *ti = at->id < NID ? (at->kind == 1 ? &tex_info[at->id] : &rb_info[at->id]) : &tex_info[0];
            o += (size_t)snprintf(fl + o, sizeof fl - o, "%s%s=%s%u(%dx%d)", o ? " " : "", an[s],
                                  at->kind == 1 ? "tex" : "rb", at->id, ti->w, ti->h);
        }
        fl[o] = 0;
        out("  FB %u %s\n", st.fbo_draw, fl);
    }
    if (cap_get(E_VERTEX_PROGRAM)) dump_arb_tables(0);
    if (cap_get(E_FRAGMENT_PROGRAM)) dump_arb_tables(1);
    dump_env_tables();

    // Vertex range.
    unsigned first = 0, last = 0, count = 0, inst = 1;
    int elems = idx == RS_GL_IDX_glDrawElements || idx == RS_GL_IDX_glDrawElementsInstanced ||
                idx == RS_GL_IDX_glDrawElementsInstancedARB;
    if (!elems) {
        first = AU(2); count = AU(3);
        last = count ? first + count - 1 : first;
        if (idx == RS_GL_IDX_glDrawArraysInstancedARB) inst = AU(4);
    } else {
        count = AU(2);
        unsigned type = AU(3);
        if (idx != RS_GL_IDX_glDrawElements) inst = AU(5);
        size_t isz = type == 0x1401 ? 1 : type == 0x1403 ? 2 : 4;
        unsigned n = count > 1000000 ? 1000000 : count;
        unsigned char *ib = n ? malloc((size_t)n * isz) : NULL;
        unsigned eb = vao_get(cur_vao)->elem;
        if (ib && read_buf(E_ELEMENT_ARRAY_BUFFER, eb, (uintptr_t)AP(4), (size_t)n * isz, ib)) {
            unsigned mn = 0xffffffffu, mx = 0;
            long degenerate = 0;
            for (unsigned i = 0; i < n; i++) {
                unsigned x = isz == 1 ? ib[i] : isz == 2 ? ((uint16_t *)ib)[i] : ((uint32_t *)ib)[i];
                if (x < mn) mn = x;
                if (x > mx) mx = x;
                if (i >= 2) {
                    unsigned p = isz == 1 ? ib[i - 1] : isz == 2 ? ((uint16_t *)ib)[i - 1] : ((uint32_t *)ib)[i - 1];
                    unsigned q = isz == 1 ? ib[i - 2] : isz == 2 ? ((uint16_t *)ib)[i - 2] : ((uint32_t *)ib)[i - 2];
                    if (x == p || p == q || x == q) degenerate++;
                }
            }
            first = mn; last = mx;
            char il[300];
            size_t io = (size_t)snprintf(il, sizeof il, "  I buf=%u off=%#lx n=%u min=%u max=%u degenerate_windows=%ld hash=%016llx first=[", eb,
                                         (unsigned long)(uintptr_t)AP(4), count, mn, mx, degenerate,
                                         (unsigned long long)fnv(ib, (size_t)n * isz, FNV0));
            for (unsigned i = 0; i < n && i < 12; i++)
                io += (size_t)snprintf(il + io, sizeof il - io, "%s%u", i ? "," : "",
                                       isz == 1 ? ib[i] : isz == 2 ? ((uint16_t *)ib)[i] : ((uint32_t *)ib)[i]);
            snprintf(il + io, sizeof il - io, "]");
            out("%s\n", il);
        } else {
            out("  I READFAIL buf=%u off=%#lx n=%u\n", eb, (unsigned long)(uintptr_t)AP(4), count);
            first = 0; last = 0; count = 0;
        }
        free(ib);
    }
    if (count) dump_arrays(first, last, inst);
    err_end();
}

// --- Shadow update + dispatch -------------------------------------------------------------------------------
static void set_arr(int slot, int size, unsigned type, int norm, int stride, const void *ptr) {
    if (slot < 0 || slot >= NSLOT) return;
    struct arr *r = &vao_get(cur_vao)->a[slot];
    r->size = size; r->type = type; r->norm = (uint8_t)norm; r->stride = stride; r->ptr = ptr;
    r->buf = buf_array;
}
static int client_slot(unsigned cap) {
    switch (cap) {
    case 0x8074: return 0;
    case 0x8075: return 1;
    case 0x8076: return 2;
    case 0x845E: return 3;
    case 0x8457: return 4;
    case 0x8078: return 8 + (int)(client_tex & 7);
    default: return -1;
    }
}

static int is_draw(int idx) {
    return idx == RS_GL_IDX_glDrawArrays || idx == RS_GL_IDX_glDrawElements ||
           idx == RS_GL_IDX_glDrawArraysInstancedARB || idx == RS_GL_IDX_glDrawElementsInstanced ||
           idx == RS_GL_IDX_glDrawElementsInstancedARB;
}

void rs_tr_call(int idx, const char *params, const struct rs_targ *a, uint64_t ret) {
    if (!rs_meta[idx].done) parse_params(idx, params);
    switch (idx) {
    case RS_GL_IDX_glEnable: cap_set(AU(1), 1); break;
    case RS_GL_IDX_glDisable: cap_set(AU(1), 0); break;
    case RS_GL_IDX_glDepthFunc: st.depth_func = AU(1); break;
    case RS_GL_IDX_glDepthMask: st.depth_mask = a[1].u & 0xff; break;
    case RS_GL_IDX_glBlendFunc: st.bsrc = st.bsrca = AU(1); st.bdst = st.bdsta = AU(2); break;
    case RS_GL_IDX_glBlendFuncSeparate: st.bsrc = AU(1); st.bdst = AU(2); st.bsrca = AU(3); st.bdsta = AU(4); break;
    case RS_GL_IDX_glBlendEquation: st.blend_eq = AU(1); break;
    case RS_GL_IDX_glColorMask: for (int i = 0; i < 4; i++) st.cmask[i] = a[i + 1].u & 0xff ? 1 : 0; break;
    case RS_GL_IDX_glCullFace: st.cull = AU(1); break;
    case RS_GL_IDX_glFrontFace: st.front = AU(1); break;
    case RS_GL_IDX_glAlphaFunc: st.alpha_func = AU(1); st.alpha_ref = AF(2); break;
    case RS_GL_IDX_glPolygonMode: st.poly_mode = AU(2); break;
    case RS_GL_IDX_glViewport: for (int i = 0; i < 4; i++) st.vp[i] = AI(i + 1); break;
    case RS_GL_IDX_glScissor: for (int i = 0; i < 4; i++) st.sc[i] = AI(i + 1); break;
    case RS_GL_IDX_glUseProgram: st.prog = AU(1); break;
    case RS_GL_IDX_glBindFramebuffer:
        if (AU(1) == E_FRAMEBUFFER || AU(1) == E_DRAW_FRAMEBUFFER) st.fbo_draw = AU(2);
        if (AU(1) == E_FRAMEBUFFER || AU(1) == E_READ_FRAMEBUFFER) st.fbo_read = AU(2);
        break;
    case RS_GL_IDX_glFramebufferTexture2D: {
        int s = att_slot(AU(2));
        unsigned fb = AU(1) == E_READ_FRAMEBUFFER ? st.fbo_read : st.fbo_draw;
        if (s >= 0 && fb < 1024) fbo_att[fb][s] = (struct att){AU(4) ? 1 : 0, AU(4)};
        break; }
    case RS_GL_IDX_glFramebufferRenderbuffer: {
        int s = att_slot(AU(2));
        unsigned fb = AU(1) == E_READ_FRAMEBUFFER ? st.fbo_read : st.fbo_draw;
        if (s >= 0 && fb < 1024) fbo_att[fb][s] = (struct att){AU(4) ? 2 : 0, AU(4)};
        break; }
    case RS_GL_IDX_glBindRenderbuffer: st.rbo = AU(2); break;
    case RS_GL_IDX_glRenderbufferStorage:
        if (st.rbo < NID) rb_info[st.rbo] = (struct tinfo){AI(3), AI(4), AU(2)};
        break;
    case RS_GL_IDX_glActiveTexture: st.active_tex = AU(1); break;
    case RS_GL_IDX_glBindTexture: {
        unsigned s = tex_slot(AU(1)), u = (st.active_tex - 0x84C0) & 31;
        if (s != 0xff) tex_bound[u][s] = AU(2);
        break; }
    case RS_GL_IDX_glTexImage2D:
    case RS_GL_IDX_glCompressedTexImage2D: {
        unsigned s = tex_slot(AU(1)), u = (st.active_tex - 0x84C0) & 31;
        if (s != 0xff && AI(2) == 0) {
            unsigned id = tex_bound[u][s];
            if (id < NID) tex_info[id] = (struct tinfo){AI(4), AI(5), AU(3)};
        }
        break; }
    case RS_GL_IDX_glBindBuffer:
        if (AU(1) == E_ARRAY_BUFFER) buf_array = AU(2);
        else if (AU(1) == E_ELEMENT_ARRAY_BUFFER) vao_get(cur_vao)->elem = AU(2);
        break;
    case RS_GL_IDX_glDeleteBuffers: {
        const unsigned *ids = AP(2);
        for (int i = 0; ids && i < AI(1); i++) {
            if (ids[i] == buf_array) buf_array = 0;
            for (int v = 0; v < 4096; v++) {
                if (!vaos[v]) continue;
                if (vaos[v]->elem == ids[i]) vaos[v]->elem = 0;
                for (int k = 0; k < NSLOT; k++) if (vaos[v]->a[k].buf == ids[i]) vaos[v]->a[k].buf = 0;
            }
            map_set(ids[i], 0);
        }
        break; }
    case RS_GL_IDX_glDeleteTextures: {
        const unsigned *ids = AP(2);
        for (int i = 0; ids && i < AI(1); i++) {
            for (int u = 0; u < 32; u++) for (int k = 0; k < 4; k++) if (tex_bound[u][k] == ids[i]) tex_bound[u][k] = 0;
            if (ids[i] < NID) tex_info[ids[i]] = (struct tinfo){0, 0, 0};
        }
        break; }
    case RS_GL_IDX_glBindVertexArray: cur_vao = AU(1) < 4096 ? AU(1) : 0; break;
    case RS_GL_IDX_glVertexAttribPointer:
        if (AU(1) < 16) set_arr(16 + (int)AU(1), AI(2), AU(3), a[4].u & 0xff, AI(5), AP(6));
        break;
    case RS_GL_IDX_glEnableVertexAttribArray:
        if (AU(1) < 16) vao_get(cur_vao)->a[16 + AU(1)].en = 1;
        break;
    case RS_GL_IDX_glDisableVertexAttribArray:
        if (AU(1) < 16) vao_get(cur_vao)->a[16 + AU(1)].en = 0;
        break;
    case RS_GL_IDX_glVertexAttribDivisorARB:
        if (AU(1) < 16) vao_get(cur_vao)->a[16 + AU(1)].div = AU(2);
        break;
    case RS_GL_IDX_glMapBuffer: map_set(map_target_buf(AU(1)), 1); break;
    case RS_GL_IDX_glUnmapBuffer: map_set(map_target_buf(AU(1)), 0); break;
    case RS_GL_IDX_glShaderSource: {
        const char *const *s = AP(3);
        const int *len = AP(4);
        uint64_t h = FNV0;
        for (int i = 0; s && i < AI(2); i++)
            if (s[i]) h = fnv(s[i], len && len[i] >= 0 ? (size_t)len[i] : strlen(s[i]), h);
        if (AU(1) < NID) sh_hash[AU(1)] = h;
        break; }
    case RS_GL_IDX_glAttachShader:
        if (AU(1) < NID && prog_nsh[AU(1)] < 6) prog_sh[AU(1)][prog_nsh[AU(1)]++] = AU(2);
        break;
    case RS_GL_IDX_glLinkProgram:
        if (AU(1) < NID) {
            uint64_t h = 0;
            for (int i = 0; i < prog_nsh[AU(1)]; i++) h += sh_hash[prog_sh[AU(1)][i] % NID] * 0x9E3779B97F4A7C15ULL;
            prog_hash[AU(1)] = h;
        }
        break;
    case RS_GL_IDX_glDeleteProgram:
        if (AU(1) < NID) { prog_nsh[AU(1)] = 0; prog_hash[AU(1)] = 0; }
        break;
    default: break;
    }
    if (!tr_rec) return;
    log_call(idx, a, ret);
    if (is_draw(idx)) dump_draw(idx, a);
}

// --- Spy-only entry points (Cg.framework's calls) --------------------------------------------------------------
#ifdef RS_GL_SPY
typedef unsigned GLenum_;
extern void glBindProgramARB(GLenum, GLuint);
extern void glProgramStringARB(GLenum, GLenum, GLsizei, const void *);
extern void glProgramLocalParameter4fARB(GLenum, GLuint, GLfloat, GLfloat, GLfloat, GLfloat);
extern void glProgramLocalParameter4fvARB(GLenum, GLuint, const GLfloat *);
extern void glProgramEnvParameter4fARB(GLenum, GLuint, GLfloat, GLfloat, GLfloat, GLfloat);
extern void glProgramEnvParameter4fvARB(GLenum, GLuint, const GLfloat *);
extern void glProgramLocalParameter4dARB(GLenum, GLuint, GLdouble, GLdouble, GLdouble, GLdouble);
extern void glProgramLocalParameter4dvARB(GLenum, GLuint, const GLdouble *);
extern void glProgramEnvParameter4dARB(GLenum, GLuint, GLdouble, GLdouble, GLdouble, GLdouble);
extern void glProgramEnvParameter4dvARB(GLenum, GLuint, const GLdouble *);
extern void glEnableClientState(GLenum);
extern void glDisableClientState(GLenum);
extern void glClientActiveTexture(GLenum);
extern void glVertexPointer(GLint, GLenum, GLsizei, const GLvoid *);
extern void glNormalPointer(GLenum, GLsizei, const GLvoid *);
extern void glColorPointer(GLint, GLenum, GLsizei, const GLvoid *);
extern void glSecondaryColorPointer(GLint, GLenum, GLsizei, const GLvoid *);
extern void glTexCoordPointer(GLint, GLenum, GLsizei, const GLvoid *);
extern void glFogCoordPointer(GLenum, GLsizei, const GLvoid *);

static void xlog(const char *fn, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void xlog(const char *fn, const char *fmt, ...) {
    if (!tr_rec) return;
    char b[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    out("@%lld %s(%s)\n", now_us() - tr_t0us, fn, b);
}

static void x_set_local(unsigned target, unsigned idx, const float *v) {
    int t = arb_t(target);
    struct arbprog *p = arb_get(t, arb_bound[t]);
    if (!p || idx >= 256) return;
    memcpy(p->loc[idx], v, 16);
    p->set[idx / 64] |= 1ULL << (idx % 64);
}
static void x_set_env(unsigned target, unsigned idx, const float *v) {
    int t = arb_t(target);
    if (idx >= 256) return;
    memcpy(arb_env[t][idx], v, 16);
    arb_env_set[t][idx / 64] |= 1ULL << (idx % 64);
}

static void rs_x_glBindProgramARB(GLenum target, GLuint id) {
    glBindProgramARB(target, id);
    if (!rs_tr_on) return;
    arb_bound[arb_t(target)] = id;
    if (id) arb_get(arb_t(target), id);
    xlog("glBindProgramARB", "%s, %u", target == E_FRAGMENT_PROGRAM ? "FRAGMENT_PROGRAM_ARB" : "VERTEX_PROGRAM_ARB", id);
}
static void rs_x_glProgramStringARB(GLenum target, GLenum fmt, GLsizei len, const void *s) {
    glProgramStringARB(target, fmt, len, s);
    if (!rs_tr_on || !s) return;
    int t = arb_t(target);
    struct arbprog *p = arb_get(t, arb_bound[t]);
    uint64_t h = fnv(s, (size_t)len, FNV0);
    if (p) {
        p->hash = h; memset(p->set, 0, sizeof p->set);
        p->nlocal = 0;
        const char *str = s, *end = (const char *)s + len;
        for (const char *q = str; q && q < end; q++) {
            if (*q != 'p' || end - q < 14 || strncmp(q, "program.local[", 14) != 0) continue;
            int a = atoi(q + 14), b = a;
            const char *dots = strstr(q + 14, "..");
            if (dots && dots < q + 24) b = atoi(dots + 2);
            if (b + 1 > p->nlocal) p->nlocal = b + 1;
        }
        if (p->nlocal > 256) p->nlocal = 256;
    }
    char path[1100];
    snprintf(path, sizeof path, "%s/arb", tr_dir);
    mkdir(path, 0755);
    snprintf(path, sizeof path, "%s/arb/%s_%016llx.txt", tr_dir, t ? "fp" : "vp", (unsigned long long)h);
    if (access(path, F_OK) != 0) {
        FILE *f = fopen(path, "w");
        if (f) { fwrite(s, 1, (size_t)len, f); fclose(f); }
    }
    xlog("glProgramStringARB", "%s, id=%u, len=%d hash=%016llx", t ? "FRAGMENT" : "VERTEX", arb_bound[t], len, (unsigned long long)h);
}
static void rs_x_glProgramLocalParameter4fARB(GLenum t, GLuint i, GLfloat x, GLfloat y, GLfloat z, GLfloat w) {
    glProgramLocalParameter4fARB(t, i, x, y, z, w);
    if (!rs_tr_on) return;
    float v[4] = {x, y, z, w};
    x_set_local(t, i, v);
    xlog("glProgramLocalParameter4fARB", "%s, prog=%u, %u, %.9g,%.9g,%.9g,%.9g", t == E_FRAGMENT_PROGRAM ? "FP" : "VP", arb_bound[arb_t(t)], i, x, y, z, w);
}
static void rs_x_glProgramLocalParameter4fvARB(GLenum t, GLuint i, const GLfloat *p) {
    glProgramLocalParameter4fvARB(t, i, p);
    if (!rs_tr_on || !p) return;
    x_set_local(t, i, p);
    xlog("glProgramLocalParameter4fvARB", "%s, prog=%u, %u, %.9g,%.9g,%.9g,%.9g", t == E_FRAGMENT_PROGRAM ? "FP" : "VP", arb_bound[arb_t(t)], i, p[0], p[1], p[2], p[3]);
}
static void rs_x_glProgramEnvParameter4fARB(GLenum t, GLuint i, GLfloat x, GLfloat y, GLfloat z, GLfloat w) {
    glProgramEnvParameter4fARB(t, i, x, y, z, w);
    if (!rs_tr_on) return;
    float v[4] = {x, y, z, w};
    x_set_env(t, i, v);
    xlog("glProgramEnvParameter4fARB", "%s, %u, %.9g,%.9g,%.9g,%.9g", t == E_FRAGMENT_PROGRAM ? "FP" : "VP", i, x, y, z, w);
}
static void rs_x_glProgramEnvParameter4fvARB(GLenum t, GLuint i, const GLfloat *p) {
    glProgramEnvParameter4fvARB(t, i, p);
    if (!rs_tr_on || !p) return;
    x_set_env(t, i, p);
    xlog("glProgramEnvParameter4fvARB", "%s, %u, %.9g,%.9g,%.9g,%.9g", t == E_FRAGMENT_PROGRAM ? "FP" : "VP", i, p[0], p[1], p[2], p[3]);
}
// NVIDIA's Cg runtime (the Rosetta reference) uploads matrices through the double entry points.
static void rs_x_glProgramLocalParameter4dARB(GLenum t, GLuint i, GLdouble x, GLdouble y, GLdouble z, GLdouble w) {
    glProgramLocalParameter4dARB(t, i, x, y, z, w);
    if (!rs_tr_on) return;
    float v[4] = {(float)x, (float)y, (float)z, (float)w};
    x_set_local(t, i, v);
    xlog("glProgramLocalParameter4dARB", "%s, prog=%u, %u, %.9g,%.9g,%.9g,%.9g", t == E_FRAGMENT_PROGRAM ? "FP" : "VP", arb_bound[arb_t(t)], i, x, y, z, w);
}
static void rs_x_glProgramLocalParameter4dvARB(GLenum t, GLuint i, const GLdouble *p) {
    glProgramLocalParameter4dvARB(t, i, p);
    if (!rs_tr_on || !p) return;
    float v[4] = {(float)p[0], (float)p[1], (float)p[2], (float)p[3]};
    x_set_local(t, i, v);
    xlog("glProgramLocalParameter4dvARB", "%s, prog=%u, %u, %.9g,%.9g,%.9g,%.9g", t == E_FRAGMENT_PROGRAM ? "FP" : "VP", arb_bound[arb_t(t)], i, p[0], p[1], p[2], p[3]);
}
static void rs_x_glProgramEnvParameter4dARB(GLenum t, GLuint i, GLdouble x, GLdouble y, GLdouble z, GLdouble w) {
    glProgramEnvParameter4dARB(t, i, x, y, z, w);
    if (!rs_tr_on) return;
    float v[4] = {(float)x, (float)y, (float)z, (float)w};
    x_set_env(t, i, v);
    xlog("glProgramEnvParameter4dARB", "%s, %u, %.9g,%.9g,%.9g,%.9g", t == E_FRAGMENT_PROGRAM ? "FP" : "VP", i, x, y, z, w);
}
static void rs_x_glProgramEnvParameter4dvARB(GLenum t, GLuint i, const GLdouble *p) {
    glProgramEnvParameter4dvARB(t, i, p);
    if (!rs_tr_on || !p) return;
    float v[4] = {(float)p[0], (float)p[1], (float)p[2], (float)p[3]};
    x_set_env(t, i, v);
    xlog("glProgramEnvParameter4dvARB", "%s, %u, %.9g,%.9g,%.9g,%.9g", t == E_FRAGMENT_PROGRAM ? "FP" : "VP", i, p[0], p[1], p[2], p[3]);
}
static void rs_x_glEnableClientState(GLenum cap) {
    glEnableClientState(cap);
    if (!rs_tr_on) return;
    int s = client_slot(cap);
    if (s >= 0) vao_get(cur_vao)->a[s].en = 1;
    xlog("glEnableClientState", "%#x", cap);
}
static void rs_x_glDisableClientState(GLenum cap) {
    glDisableClientState(cap);
    if (!rs_tr_on) return;
    int s = client_slot(cap);
    if (s >= 0) vao_get(cur_vao)->a[s].en = 0;
    xlog("glDisableClientState", "%#x", cap);
}
static void rs_x_glClientActiveTexture(GLenum t) {
    glClientActiveTexture(t);
    if (rs_tr_on) client_tex = t - 0x84C0;
}
static void rs_x_glVertexPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *p) {
    glVertexPointer(size, type, stride, p);
    if (rs_tr_on) { set_arr(0, size, type, 0, stride, p); xlog("glVertexPointer", "%d, %#x, %d, %p buf=%u", size, type, stride, p, buf_array); }
}
static void rs_x_glNormalPointer(GLenum type, GLsizei stride, const GLvoid *p) {
    glNormalPointer(type, stride, p);
    if (rs_tr_on) set_arr(1, 3, type, 1, stride, p);
}
static void rs_x_glColorPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *p) {
    glColorPointer(size, type, stride, p);
    if (rs_tr_on) set_arr(2, size, type, 1, stride, p);
}
static void rs_x_glSecondaryColorPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *p) {
    glSecondaryColorPointer(size, type, stride, p);
    if (rs_tr_on) set_arr(3, size, type, 1, stride, p);
}
static void rs_x_glTexCoordPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *p) {
    glTexCoordPointer(size, type, stride, p);
    if (rs_tr_on) set_arr(8 + (int)(client_tex & 7), size, type, 0, stride, p);
}
static void rs_x_glFogCoordPointer(GLenum type, GLsizei stride, const GLvoid *p) {
    glFogCoordPointer(type, stride, p);
    if (rs_tr_on) set_arr(4, 1, type, 0, stride, p);
}

RS_INTERPOSE(rs_x_glBindProgramARB, glBindProgramARB);
RS_INTERPOSE(rs_x_glProgramStringARB, glProgramStringARB);
RS_INTERPOSE(rs_x_glProgramLocalParameter4fARB, glProgramLocalParameter4fARB);
RS_INTERPOSE(rs_x_glProgramLocalParameter4fvARB, glProgramLocalParameter4fvARB);
RS_INTERPOSE(rs_x_glProgramEnvParameter4fARB, glProgramEnvParameter4fARB);
RS_INTERPOSE(rs_x_glProgramEnvParameter4fvARB, glProgramEnvParameter4fvARB);
RS_INTERPOSE(rs_x_glProgramLocalParameter4dARB, glProgramLocalParameter4dARB);
RS_INTERPOSE(rs_x_glProgramLocalParameter4dvARB, glProgramLocalParameter4dvARB);
RS_INTERPOSE(rs_x_glProgramEnvParameter4dARB, glProgramEnvParameter4dARB);
RS_INTERPOSE(rs_x_glProgramEnvParameter4dvARB, glProgramEnvParameter4dvARB);
RS_INTERPOSE(rs_x_glEnableClientState, glEnableClientState);
RS_INTERPOSE(rs_x_glDisableClientState, glDisableClientState);
RS_INTERPOSE(rs_x_glClientActiveTexture, glClientActiveTexture);
RS_INTERPOSE(rs_x_glVertexPointer, glVertexPointer);
RS_INTERPOSE(rs_x_glNormalPointer, glNormalPointer);
RS_INTERPOSE(rs_x_glColorPointer, glColorPointer);
RS_INTERPOSE(rs_x_glSecondaryColorPointer, glSecondaryColorPointer);
RS_INTERPOSE(rs_x_glTexCoordPointer, glTexCoordPointer);
RS_INTERPOSE(rs_x_glFogCoordPointer, glFogCoordPointer);
#endif

// --- Frame control ----------------------------------------------------------------------------------------------
static double tr_start = -1;
static const char *tr_key;
static int tr_frames = 30, tr_left, tr_started;
static long long tr_first_us;
static char tr_keypath[1100];

static void trace_open(unsigned long frame);
static void tr_atexit(void) {
    if (tr_fp) { fprintf(tr_fp, "# EXIT (trace cut short)\n"); fflush(tr_fp); }
}
void rs_tr_init(void *const *real, const char *const *names) {
    rs_r = real;
    rs_nm = names;
    tr_dir = getenv("RS_GL_TRACE_DIR");
    if (!tr_dir || !*tr_dir) { tr_dir = NULL; return; }
    mkdir(tr_dir, 0755);
    const char *e;
    if ((e = getenv("RS_GL_TRACE_START"))) tr_start = atof(e);
    if ((e = getenv("RS_GL_TRACE_FRAMES")) && atoi(e) > 0) tr_frames = atoi(e);
    tr_key = getenv("RS_GL_TRACE_KEY");
    if (tr_key && *tr_key) {
        if (strchr(tr_key, '/')) snprintf(tr_keypath, sizeof tr_keypath, "%s", tr_key);
        else snprintf(tr_keypath, sizeof tr_keypath, "%s/go", tr_dir);
    } else tr_key = NULL;
    if (tr_start < 0 && !tr_key) tr_start = 0;
    void *sys = dlopen("/System/Library/Frameworks/OpenGL.framework/Versions/A/OpenGL", RTLD_NOW | RTLD_LOCAL);
    *(void **)&r_GetBufferSubData = sys ? dlsym(sys, "glGetBufferSubData") : NULL;
    *(void **)&r_IsEnabled = sys ? dlsym(sys, "glIsEnabled") : NULL;
    *(void **)&r_GetLocalParam = sys ? dlsym(sys, "glGetProgramLocalParameterfvARB") : NULL;
    r_BindBuffer = (void (*)(unsigned, unsigned))real[RS_GL_IDX_glBindBuffer];
    r_GetError = (unsigned (*)(void))real[RS_GL_IDX_glGetError];
    rs_tr_on = 1;
    atexit(tr_atexit);
    if ((e = getenv("RS_GL_TRACE_NOW")) && *e == '1') {  // record from load until the first flush (tests)
        trace_open(0);
        tr_left = 1;
        tr_started = 1;
    }
    fprintf(stderr, "[rs-gl] tracing armed: dir=%s start=%g key=%s frames=%d\n", tr_dir, tr_start,
            tr_key ? tr_keypath : "-", tr_frames);
}

static void trace_open(unsigned long frame) {
    char path[1100];
    snprintf(path, sizeof path, "%s/frame_%06lu.gltrace", tr_dir, frame);
    tr_fp = fopen(path, "w");
    if (!tr_fp) return;
    setvbuf(tr_fp, NULL, _IOFBF, 1 << 20);
    tr_frame = frame;
    tr_t0us = now_us();
    fprintf(tr_fp, "# rs-gltrace v1 frame=%lu arch=%s\n", frame,
#if defined(__arm64__)
            "arm64"
#else
            "x86_64"
#endif
    );
    tr_rec = 1;
}

void rs_tr_frame_end(void *ctx, unsigned long frame, void (*snap)(void *, const char *)) {
    if (!rs_tr_on) return;
    long long now = now_us();
    if (tr_rec) {
        tr_rec = 0;
        fprintf(tr_fp, "# END frame=%lu dur_us=%lld\n", frame, now - tr_t0us);
        fclose(tr_fp);
        tr_fp = NULL;
        if (snap) {
            char path[1100];
            snprintf(path, sizeof path, "%s/frame_%06lu.png", tr_dir, frame);
            err_begin();
            snap(ctx, path);
            err_end();
        }
        if (--tr_left <= 0) {
            char path[1100];
            snprintf(path, sizeof path, "%s/done", tr_dir);
            FILE *f = fopen(path, "w");
            if (f) { fprintf(f, "frames traced, last=%lu\n", frame); fclose(f); }
            fprintf(stderr, "[rs-gl] trace complete (%d frames) in %s\n", tr_frames, tr_dir);
        }
    }
    if (!tr_first_us) tr_first_us = now;
    if (!tr_started) {
        int go = 0;
        if (tr_key) {
            struct stat sb;
            if (stat(tr_keypath, &sb) == 0) {
                char used[1200];
                snprintf(used, sizeof used, "%s.used", tr_keypath);
                rename(tr_keypath, used);
                go = 1;
            }
        } else if (tr_start >= 0 && (double)(now - tr_first_us) / 1e6 >= tr_start && frame >= 1)
            go = 1;
        if (go) { tr_started = 1; tr_left = tr_frames; }
    }
    if (tr_started && tr_left > 0) trace_open(frame + 1);
}
