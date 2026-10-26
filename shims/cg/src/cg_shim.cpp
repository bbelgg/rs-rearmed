// arm64 replacement for Cg.framework (NVIDIA Cg 3.1) as used by Rocksmith 2014.
//
// The game only ever creates programs from precompiled ARB objects (CG_OBJECT,
// arbvp1/arbfp1), so no Cg compiler is needed: the shim parses the object's
// #var header for the parameter table and drives GL with one of two backends:
//
//   arb (default)   hand the object to GL_ARB_{vertex,fragment}_program, as
//                   NVIDIA's runtime does. Works on Apple's legacy GL.
//   glsl            translate to GLSL (arb_glsl.cpp), link each bound
//                   vertex/fragment pair into a GL program, keep program.local
//                   in a uniform array. Works on GL contexts without
//                   ARB_vertex_program, e.g. ANGLE. In the game it sometimes
//                   never leaves the loading screen.
//
// Environment:
//   RS_CG_BACKEND=arb|glsl      backend (default arb)
//   RS_CG_GLSL_DIALECT=120|100es
//   RS_CG_LOG=1                 log errors and program creation to stderr
//   RS_CG_DUMP_DIR=<dir>        write every translated shader there
#define GL_SILENCE_DEPRECATION
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "arb_program.h"
#include "cg_api.h"
#include "cg_key.h"

#define CG_EXPORT extern "C" __attribute__((visibility("default")))

namespace rscg {
namespace {

// Cg error codes (cg_errors.h) the shim can raise.
enum {
    CG_NO_ERROR = 0,
    CG_COMPILER_ERROR = 1,
    CG_INVALID_PARAMETER_ERROR = 2,
    CG_INVALID_PROFILE_ERROR = 3,
    CG_PROGRAM_LOAD_ERROR = 4,
    CG_INVALID_CONTEXT_HANDLE_ERROR = 16,
    CG_INVALID_PROGRAM_HANDLE_ERROR = 17,
    CG_INVALID_PARAM_HANDLE_ERROR = 18,
    CG_INVALID_ENUMERANT_ERROR = 22,
    CG_PROGRAM_NOT_LOADED_ERROR = 34,
};

struct Config {
    bool glsl = false;
    std::string dialect = "120";
    bool log = false;
    std::string dump_dir;
    Config() {
        const char *b = getenv("RS_CG_BACKEND");
        if (b && !strcmp(b, "glsl")) glsl = true;
        if (const char *d = getenv("RS_CG_GLSL_DIALECT")) dialect = d;
        log = getenv("RS_CG_LOG") && *getenv("RS_CG_LOG") && strcmp(getenv("RS_CG_LOG"), "0");
        if (const char *d = getenv("RS_CG_DUMP_DIR")) dump_dir = d;
    }
};

Config &cfg() {
    static Config c;
    return c;
}

void logf(const char *fmt, ...) {
    if (!cfg().log) return;
    va_list ap;
    va_start(ap, fmt);
    fputs("[cg-shim] ", stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}

int g_error = CG_NO_ERROR;
std::string g_listing;

void set_error(int e, const char *what) {
    g_error = e;
    logf("error %d: %s", e, what);
}

struct Context;
struct Program;

struct Param {
    ParamInfo info;
    Program *program;
    GLuint texture = 0;
};

struct Program {
    Context *ctx;
    CGprofile profile;
    Stage stage;
    std::string source, entry;
    ArbProgram arb;
    bool parsed = false;
    std::string parse_error;
    std::vector<std::unique_ptr<Param>> params;

    // program.local / c[] values, 4 floats per slot, and per-slot "last set"
    // stamps compared against what each GL program has seen.
    std::vector<float> slots;
    std::vector<uint64_t> slot_stamp;
    uint64_t stamp = 0;

    bool loaded = false;
    GLuint arb_id = 0;               // arb backend
    uint64_t arb_uploaded = 0;
    GLuint fs = 0;                   // glsl backend: fragment shader object
    std::map<unsigned, GLuint> vs;   // glsl backend: vertex shader per FS input set
};

struct Context {
    std::vector<Program *> programs;
};

std::set<void *> g_live;   // every valid handle, so stale/garbage handles are rejected

// Direct-mapped cache of handles found in g_live: the game passes the same few
// hundred parameters every frame, and a set lookup per cgGLSet* call shows in
// profiles. An entry is cleared when its handle leaves g_live.
void *g_live_cache[1024];
inline void *&live_cache_slot(void *h) { return g_live_cache[((uintptr_t)h >> 4) & 1023]; }

void live_insert(void *h) { g_live.insert(h); }
void live_erase(void *h) {
    g_live.erase(h);
    if (live_cache_slot(h) == h) live_cache_slot(h) = nullptr;
}

template <class T> T *check(void *h, int err) {
    void *&c = live_cache_slot(h);
    if (h && c == h) return static_cast<T *>(h);
    if (!h || !g_live.count(h)) {
        set_error(err, "invalid handle");
        return nullptr;
    }
    c = h;
    return static_cast<T *>(h);
}

uint64_t g_clock = 1;   // monotonically increasing stamp for slot writes

// --- GLSL backend -------------------------------------------------------------

struct Link {
    GLuint program = 0;
    bool ok = false;
    Program *vp = nullptr, *fp = nullptr;
    std::vector<GLint> vp_loc, fp_loc;   // per-slot uniform locations
    uint64_t vp_seen = 0, fp_seen = 0;
};

std::map<std::pair<Program *, Program *>, std::unique_ptr<Link>> g_links;
Program *g_bound_vp = nullptr, *g_bound_fp = nullptr;
bool g_vp_enabled = false, g_fp_enabled = false;   // cgGLEnableProfile state
Link *g_current = nullptr;

// The game also drives GL itself (its own GLSL programs, glUseProgram(0) for
// fixed function), so whether our program is still in use is asked of GL.
bool is_current(const Link *l) {
    GLint cur = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &cur);
    return l && (GLuint)cur == l->program;
}

void dump(const std::string &name, const std::string &text) {
    if (cfg().dump_dir.empty()) return;
    std::ofstream(cfg().dump_dir + "/" + name) << text;
}

std::string program_tag(const Program *p) {
    char b[32];
    snprintf(b, sizeof b, "%016llx",
             (unsigned long long)cg_program_key(p->source.c_str(), p->profile, p->entry.c_str()));
    return b;
}

GLuint compile(GLenum kind, const std::string &src, const std::string &tag) {
    GLuint s = glCreateShader(kind);
    const char *t = src.c_str();
    glShaderSource(s, 1, &t, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    dump(tag, src);
    if (!ok) {
        char log[4096] = {0};
        glGetShaderInfoLog(s, sizeof log, nullptr, log);
        g_listing = log;
        logf("GLSL compile failed for %s:\n%s", tag.c_str(), log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

// Shaders used when only one stage has a Cg program bound.
const ArbProgram &passthrough(Stage s) {
    static ArbProgram vp, fp;
    static bool init = false;
    if (!init) {
        std::string e;
        parse_arb_program("!!ARBvp1.0\nMOV result.position, vertex.position;\n"
                          "MOV result.color, vertex.color;\nMOV result.texcoord[0], vertex.texcoord[0];\nEND\n",
                          &vp, &e);
        parse_arb_program("!!ARBfp1.0\nMOV result.color, fragment.color;\nEND\n", &fp, &e);
        init = true;
    }
    return s == Stage::Vertex ? vp : fp;
}

GLuint vertex_shader(Program *vp, unsigned fs_inputs) {
    if (vp) {
        auto it = vp->vs.find(fs_inputs);
        if (it != vp->vs.end()) return it->second;
    }
    const ArbProgram &a = vp ? vp->arb : passthrough(Stage::Vertex);
    char tag[64];
    snprintf(tag, sizeof tag, "%s.%03x.vert", vp ? program_tag(vp).c_str() : "passthrough", fs_inputs);
    GLuint s = compile(GL_VERTEX_SHADER, emit_glsl(a, cfg().dialect, fs_inputs), tag);
    if (vp) vp->vs[fs_inputs] = s;
    return s;
}

GLuint fragment_shader(Program *fp) {
    if (fp && fp->fs) return fp->fs;
    static GLuint pass = 0;
    if (!fp && pass) return pass;
    const ArbProgram &a = fp ? fp->arb : passthrough(Stage::Fragment);
    std::string tag = (fp ? program_tag(fp) : std::string("passthrough")) + ".frag";
    GLuint s = compile(GL_FRAGMENT_SHADER, emit_glsl(a, cfg().dialect, 0), tag);
    (fp ? fp->fs : pass) = s;
    return s;
}

std::vector<GLint> slot_locations(GLuint prog, const char *array, int n) {
    std::vector<GLint> loc(n, -1);
    for (int i = 0; i < n; i++) {
        char name[64];
        snprintf(name, sizeof name, "%s[%d]", array, i);
        loc[i] = glGetUniformLocation(prog, name);
    }
    return loc;
}

Link *get_link(Program *vp, Program *fp) {
    auto key = std::make_pair(vp, fp);
    auto it = g_links.find(key);
    if (it != g_links.end()) return it->second.get();
    auto l = std::make_unique<Link>();
    l->vp = vp;
    l->fp = fp;
    unsigned fs_inputs = (fp ? fp->arb : passthrough(Stage::Fragment)).outputs;
    GLuint v = vertex_shader(vp, fs_inputs), f = fragment_shader(fp);
    if (v && f) {
        l->program = glCreateProgram();
        glAttachShader(l->program, v);
        glAttachShader(l->program, f);
        for (int loc = 0; loc < 16; loc++) {
            char name[16];
            snprintf(name, sizeof name, "rs_attr%d", loc);
            glBindAttribLocation(l->program, loc, name);
        }
        glLinkProgram(l->program);
        GLint ok = 0;
        glGetProgramiv(l->program, GL_LINK_STATUS, &ok);
        if (!ok) {
            char log[4096] = {0};
            glGetProgramInfoLog(l->program, sizeof log, nullptr, log);
            g_listing = log;
            logf("GLSL link failed (%s + %s):\n%s", vp ? program_tag(vp).c_str() : "-",
                 fp ? program_tag(fp).c_str() : "-", log);
        } else {
            // Left current: get_link is only called to activate this pair.
            l->ok = true;
            glUseProgram(l->program);
            for (unsigned unit = 0; unit < 16; unit++)
                for (const char *t : {"1D", "2D", "3D", "CUBE", "RECT"}) {
                    char name[32];
                    snprintf(name, sizeof name, "rs_tex%u_%s", unit, t);
                    GLint loc = glGetUniformLocation(l->program, name);
                    if (loc >= 0) glUniform1i(loc, unit);
                }
            if (vp) l->vp_loc = slot_locations(l->program, "rs_vp_local", vp->arb.uniform_slots);
            if (fp) l->fp_loc = slot_locations(l->program, "rs_fp_local", fp->arb.uniform_slots);
        }
    }
    Link *raw = l.get();
    g_links[key] = std::move(l);
    return raw;
}

// Upload slots of `p` written after `*seen` into the current GL program.
void upload_slots(Program *p, const std::vector<GLint> &loc, uint64_t *seen) {
    if (!p || *seen >= p->stamp) return;
    int n = (int)loc.size();
    for (int i = 0; i < n; i++) {
        if (p->slot_stamp[i] <= *seen || loc[i] < 0) continue;
        // Coalesce a run whose uniform locations are consecutive.
        int j = i + 1;
        while (j < n && p->slot_stamp[j] > *seen && loc[j] == loc[i] + (j - i)) j++;
        glUniform4fv(loc[i], j - i, &p->slots[4 * i]);
        i = j - 1;
    }
    *seen = p->stamp;
}

// Use the program for the bound pair, counting a stage only while its profile is
// enabled (as GL_{VERTEX,FRAGMENT}_PROGRAM_ARB do for the arb backend). With
// neither stage active, fixed function (program 0) is back, unless the game has
// made a program of its own current since.
void glsl_activate() {
    Program *vp = g_vp_enabled ? g_bound_vp : nullptr;
    Program *fp = g_fp_enabled ? g_bound_fp : nullptr;
    if (!vp && !fp) {
        if (is_current(g_current)) glUseProgram(0);
        g_current = nullptr;
        return;
    }
    Link *l = get_link(vp, fp);
    g_current = l->ok ? l : nullptr;
    if (!g_current) return;
    glUseProgram(l->program);
    upload_slots(l->vp, l->vp_loc, &l->vp_seen);
    upload_slots(l->fp, l->fp_loc, &l->fp_seen);
}

void forget_program_links(Program *p) {
    for (auto it = g_links.begin(); it != g_links.end();) {
        if (it->first.first == p || it->first.second == p) {
            if (it->second.get() == g_current) g_current = nullptr;
            if (it->second->program) glDeleteProgram(it->second->program);
            it = g_links.erase(it);
        } else {
            ++it;
        }
    }
}

// --- ARB backend ----------------------------------------------------------------

GLenum arb_target(const Program *p) {
    return p->stage == Stage::Vertex ? GL_VERTEX_PROGRAM_ARB : GL_FRAGMENT_PROGRAM_ARB;
}

// GL_EXT_gpu_program_parameters sets a run of program.local in one call; Apple's
// GL has it. Looked up at the first program load.
bool g_arb_multi = false;

void arb_set_locals(Program *p, int first, int count) {
    if (g_arb_multi) {
        glProgramLocalParameters4fvEXT(arb_target(p), first, count, &p->slots[4 * first]);
        return;
    }
    for (int i = first; i < first + count; i++)
        glProgramLocalParameter4fvARB(arb_target(p), i, &p->slots[4 * i]);
}

void arb_upload(Program *p) {
    if (p->arb_uploaded >= p->stamp) return;
    int n = std::min(p->arb.max_local + 1, (int)p->slot_stamp.size());
    const uint64_t *st = p->slot_stamp.data(), seen = p->arb_uploaded;
    for (int i = 0; i < n; i++) {
        if (st[i] <= seen) continue;
        int j = i + 1;
        while (j < n && st[j] > seen) j++;
        arb_set_locals(p, i, j - i);
        i = j - 1;
    }
    p->arb_uploaded = p->stamp;
}

bool arb_is_bound(const Program *p) {
    return p == (p->stage == Stage::Vertex ? g_bound_vp : g_bound_fp);
}

// What the shim last set for GL_{VERTEX,FRAGMENT}_PROGRAM_ARB (enable and
// binding) in `ctx`, so the per-draw cgGLEnableProfile/cgGLBindProgram the game
// issues don't repeat it to the driver. The game itself never touches these
// (it imports no ARB program calls); state of another context starts unknown.
struct ArbState {
    CGLContextObj ctx = nullptr;
    int enabled[2] = {-1, -1};
    GLuint bound[2] = {~0u, ~0u};
};
ArbState g_arb;

ArbState &arb_state() {
    CGLContextObj c = CGLGetCurrentContext();
    if (c != g_arb.ctx) g_arb = ArbState(), g_arb.ctx = c;
    return g_arb;
}

void arb_bind(GLenum target, GLuint id) {
    ArbState &s = arb_state();
    GLuint &b = s.bound[target == GL_FRAGMENT_PROGRAM_ARB];
    if (b == id) return;
    glBindProgramARB(target, id);
    b = id;
}

// --- shared -----------------------------------------------------------------------

void write_slots(Param *q, int first_row, const float *rows, int nrows) {
    if (q->info.reg_base < 0) return;   // not allocated: unused parameter
    Program *p = q->program;
    int base = q->info.reg_base + first_row;
    int n = nrows;
    if (n > q->info.reg_count - first_row) n = q->info.reg_count - first_row;
    if (n > (int)p->slot_stamp.size() - base) n = (int)p->slot_stamp.size() - base;
    // Rows that already hold these values are already in (or on their way to)
    // every GL program: only the changed span [lo, hi) is stamped and uploaded.
    // A row never written counts as changed: Apple's GL doesn't zero
    // program.local of a recycled program name.
    float *dst = &p->slots[4 * base];
    const uint64_t *st = &p->slot_stamp[base];
    int lo = 0;
    while (lo < n && st[lo] && !memcmp(dst + 4 * lo, rows + 4 * lo, 16)) lo++;
    if (lo >= n) return;
    int hi = n;
    while (st[hi - 1] && !memcmp(dst + 4 * (hi - 1), rows + 4 * (hi - 1), 16)) hi--;
    memcpy(dst + 4 * lo, rows + 4 * lo, 16 * (hi - lo));
    uint64_t t = ++g_clock, prev = p->stamp;
    for (int r = base + lo; r < base + hi; r++) p->slot_stamp[r] = t;
    p->stamp = t;
    if (cfg().glsl) {
        // glUniform goes to whatever program is current: if that isn't ours any
        // more, the values wait for the pair's next activation.
        if (g_current && (g_current->vp == p || g_current->fp == p) && is_current(g_current)) {
            if (g_current->vp == p) upload_slots(p, g_current->vp_loc, &g_current->vp_seen);
            if (g_current->fp == p) upload_slots(p, g_current->fp_loc, &g_current->fp_seen);
        }
    } else if (p->loaded && arb_is_bound(p)) {
        if (p->arb_uploaded < prev) {
            arb_upload(p);   // older writes still pending
        } else {
            int end = std::min(base + hi, p->arb.max_local + 1);
            if (end > base + lo) arb_set_locals(p, base + lo, end - base - lo);
            p->arb_uploaded = t;
        }
    }
}

bool load_program(Program *p) {
    if (p->loaded) return true;
    // The arb backend only needs the parameter table; GL judges the body.
    if (!p->parsed && cfg().glsl) {
        g_listing = p->parse_error;
        set_error(CG_PROGRAM_LOAD_ERROR, p->parse_error.c_str());
        return false;
    }
    if (cfg().glsl) {
        // Vertex shaders are compiled per pairing; compile the common variant now
        // so translation errors surface at load time as they do with NVIDIA's runtime.
        bool ok = p->stage == Stage::Fragment ? fragment_shader(p) != 0
                                              : vertex_shader(p, passthrough(Stage::Fragment).outputs) != 0;
        if (!ok) {
            set_error(CG_PROGRAM_LOAD_ERROR, "GLSL compilation failed");
            return false;
        }
    } else {
        static bool ext_checked = false;
        if (!ext_checked) {
            const char *e = (const char *)glGetString(GL_EXTENSIONS);
            g_arb_multi = e && strstr(e, "GL_EXT_gpu_program_parameters");
            ext_checked = true;
        }
        GLenum target = arb_target(p);
        glGenProgramsARB(1, &p->arb_id);
        GLint prev = 0;
        glGetProgramivARB(target, GL_PROGRAM_BINDING_ARB, &prev);
        glBindProgramARB(target, p->arb_id);
        glProgramStringARB(target, GL_PROGRAM_FORMAT_ASCII_ARB, (GLsizei)p->source.size(), p->source.c_str());
        GLint errpos = -1;
        glGetIntegerv(GL_PROGRAM_ERROR_POSITION_ARB, &errpos);
        glBindProgramARB(target, prev);
        if (errpos != -1) {
            const char *msg = (const char *)glGetString(GL_PROGRAM_ERROR_STRING_ARB);
            g_listing = msg ? msg : "";
            logf("ARB program %s rejected at %d: %s", program_tag(p).c_str(), errpos, g_listing.c_str());
            glDeleteProgramsARB(1, &p->arb_id);
            p->arb_id = 0;
            set_error(CG_PROGRAM_LOAD_ERROR, "glProgramStringARB failed");
            return false;
        }
    }
    p->loaded = true;
    return true;
}

bool is_vertex_profile(CGprofile p) {
    return p == CG_PROFILE_ARBVP1 || p == CG_PROFILE_VP30 || p == CG_PROFILE_VP40 || p == CG_PROFILE_GLSLV;
}
bool is_fragment_profile(CGprofile p) {
    return p == CG_PROFILE_ARBFP1 || p == CG_PROFILE_FP30 || p == CG_PROFILE_FP40 || p == CG_PROFILE_GLSLF;
}

}  // namespace
}  // namespace rscg

using namespace rscg;

// --- context / program lifecycle ---------------------------------------------------

CG_EXPORT CGcontext cgCreateContext(void) {
    Context *c = new Context;
    live_insert(c);
    return (CGcontext)c;
}

CG_EXPORT void cgDestroyProgram(CGprogram handle);

CG_EXPORT void cgDestroyContext(CGcontext handle) {
    Context *c = check<Context>(handle, CG_INVALID_CONTEXT_HANDLE_ERROR);
    if (!c) return;
    std::vector<Program *> progs = c->programs;
    for (Program *p : progs) cgDestroyProgram((CGprogram)p);
    live_erase(c);
    delete c;
}

CG_EXPORT CGprogram cgCreateProgram(CGcontext handle, CGenum type, const char *text, CGprofile profile,
                                    const char *entry, const char **args) {
    (void)args;   // "-po MaxLocalParams=128": compile options of an already compiled object
    Context *c = check<Context>(handle, CG_INVALID_CONTEXT_HANDLE_ERROR);
    if (!c) return nullptr;
    if (!text) { set_error(CG_INVALID_PARAMETER_ERROR, "null program text"); return nullptr; }
    if (!is_vertex_profile(profile) && !is_fragment_profile(profile)) {
        set_error(CG_INVALID_PROFILE_ERROR, "unsupported profile");
        return nullptr;
    }
    std::string object;
    if (type == CG_OBJECT) {
        object = text;
    } else if (type == CG_SOURCE) {
        // There is no Cg compiler in the shim; the game only passes objects.
        uint64_t key = cg_program_key(text, profile, entry);
        fprintf(stderr, "[cg-shim] Cg source program %016llx rejected: only CG_OBJECT is supported\n",
                (unsigned long long)key);
        set_error(CG_COMPILER_ERROR, "CG_SOURCE is not supported");
        return nullptr;
    } else {
        set_error(CG_INVALID_ENUMERANT_ERROR, "program type");
        return nullptr;
    }

    Program *p = new Program;
    p->ctx = c;
    p->profile = is_vertex_profile(profile) ? CG_PROFILE_ARBVP1 : CG_PROFILE_ARBFP1;
    p->stage = is_vertex_profile(profile) ? Stage::Vertex : Stage::Fragment;
    p->source = object;
    p->entry = entry ? entry : "main";
    p->arb.stage = p->stage;
    p->parsed = parse_arb_program(object, &p->arb, &p->parse_error);
    if (p->parsed && p->arb.stage != p->stage) {
        p->parsed = false;
        p->parse_error = "object stage does not match profile";
    }
    if (!p->parsed) logf("program %s will fail to load: %s", program_tag(p).c_str(), p->parse_error.c_str());
    p->slots.assign(4 * std::max(p->arb.uniform_slots, p->arb.max_local + 1), 0.0f);
    p->slot_stamp.assign(p->slots.size() / 4, 0);
    // Constant registers are part of the GLSL uniform array; they go up with the
    // first upload to each linked program.
    for (auto &k : p->arb.uniform_consts) {
        memcpy(&p->slots[4 * k.first], k.second.data(), 16);
        p->slot_stamp[k.first] = 1;
    }
    p->stamp = 1;
    for (const ParamInfo &info : p->arb.params) {
        auto q = std::make_unique<Param>();
        q->info = info;
        q->program = p;
        live_insert(q.get());
        p->params.push_back(std::move(q));
    }
    c->programs.push_back(p);
    live_insert(p);
    logf("created %s program %s (%zu params)", p->stage == Stage::Vertex ? "vertex" : "fragment",
         program_tag(p).c_str(), p->params.size());
    return (CGprogram)p;
}

CG_EXPORT void cgDestroyProgram(CGprogram handle) {
    Program *p = check<Program>(handle, CG_INVALID_PROGRAM_HANDLE_ERROR);
    if (!p) return;
    if (g_bound_vp == p) g_bound_vp = nullptr;
    if (g_bound_fp == p) g_bound_fp = nullptr;
    forget_program_links(p);
    if (p->fs) glDeleteShader(p->fs);
    for (auto &v : p->vs)
        if (v.second) glDeleteShader(v.second);
    if (p->arb_id) {
        glDeleteProgramsARB(1, &p->arb_id);   // unbinds it if bound; the name may come back
        for (GLuint &b : g_arb.bound)
            if (b == p->arb_id) b = ~0u;
    }
    for (auto &q : p->params) live_erase(q.get());
    auto &v = p->ctx->programs;
    v.erase(std::remove(v.begin(), v.end(), p), v.end());
    live_erase(p);
    delete p;
}

CG_EXPORT const char *cgGetProgramString(CGprogram handle, CGenum pname) {
    Program *p = check<Program>(handle, CG_INVALID_PROGRAM_HANDLE_ERROR);
    if (!p) return nullptr;
    switch (pname) {
    case CG_PROGRAM_SOURCE:
    case CG_COMPILED_PROGRAM: return p->source.c_str();
    case CG_PROGRAM_ENTRY: return p->entry.c_str();
    case CG_PROGRAM_PROFILE: return p->profile == CG_PROFILE_ARBVP1 ? "arbvp1" : "arbfp1";
    default: set_error(CG_INVALID_ENUMERANT_ERROR, "cgGetProgramString pname"); return nullptr;
    }
}

// --- parameters ------------------------------------------------------------------

CG_EXPORT CGparameter cgGetNamedParameter(CGprogram handle, const char *name) {
    Program *p = check<Program>(handle, CG_INVALID_PROGRAM_HANDLE_ERROR);
    if (!p || !name) return nullptr;
    for (auto &q : p->params)
        if (q->info.name == name) return (CGparameter)q.get();
    return nullptr;
}

static CGparameter next_in_namespace(Program *p, size_t from, CGenum ns) {
    for (size_t i = from; i < p->params.size(); i++)
        if (p->params[i]->info.name_space == ns) return (CGparameter)p->params[i].get();
    return nullptr;
}

CG_EXPORT CGparameter cgGetFirstLeafParameter(CGprogram handle, CGenum name_space) {
    Program *p = check<Program>(handle, CG_INVALID_PROGRAM_HANDLE_ERROR);
    if (!p) return nullptr;
    if (name_space != CG_PROGRAM && name_space != CG_GLOBAL) {
        set_error(CG_INVALID_ENUMERANT_ERROR, "namespace");
        return nullptr;
    }
    return next_in_namespace(p, 0, name_space);
}

CG_EXPORT CGparameter cgGetNextLeafParameter(CGparameter handle) {
    Param *q = check<Param>(handle, CG_INVALID_PARAM_HANDLE_ERROR);
    if (!q) return nullptr;
    Program *p = q->program;
    for (size_t i = 0; i < p->params.size(); i++)
        if (p->params[i].get() == q) return next_in_namespace(p, i + 1, q->info.name_space);
    return nullptr;
}

#define PARAM_GETTER(ret, fn, expr, fail)                                      \
    CG_EXPORT ret fn(CGparameter handle) {                                     \
        Param *q = check<Param>(handle, CG_INVALID_PARAM_HANDLE_ERROR);        \
        if (!q) return fail;                                                   \
        return expr;                                                           \
    }

PARAM_GETTER(const char *, cgGetParameterName, q->info.name.c_str(), nullptr)
PARAM_GETTER(CGtype, cgGetParameterType, q->info.type, CG_UNKNOWN_TYPE)
PARAM_GETTER(const char *, cgGetParameterSemantic, q->info.semantic.c_str(), nullptr)
PARAM_GETTER(CGenum, cgGetParameterDirection, q->info.direction, CG_ERROR)
PARAM_GETTER(CGenum, cgGetParameterVariability, q->info.variability, CG_ERROR)
PARAM_GETTER(unsigned long, cgGetParameterResourceIndex, q->info.resource_index, 0)
PARAM_GETTER(CGresource, cgGetParameterResource, q->info.resource, CG_UNDEFINED_RESOURCE)

CG_EXPORT CGbool cgIsParameterUsed(CGparameter handle, CGhandle container) {
    Param *q = check<Param>(handle, CG_INVALID_PARAM_HANDLE_ERROR);
    if (!q) return 0;
    if (container && container != (CGhandle)q->program && container != (CGhandle)q) return 0;
    return q->info.used;
}

// --- CgGL ----------------------------------------------------------------------

CG_EXPORT void cgGLSetContextOptimalOptions(CGcontext, CGprofile) {}

static void set_profile_enabled(CGprofile profile, bool on, const char *what) {
    bool vertex = is_vertex_profile(profile);
    if (!vertex && !is_fragment_profile(profile)) {
        set_error(CG_INVALID_PROFILE_ERROR, what);
        return;
    }
    if (cfg().glsl) {
        bool &enabled = vertex ? g_vp_enabled : g_fp_enabled;
        if (enabled == on) return;
        enabled = on;
        // A program the game made current wins over ARB programs, and a profile
        // change doesn't replace it; the next cgGLBindProgram activates ours.
        GLint cur = 0;
        glGetIntegerv(GL_CURRENT_PROGRAM, &cur);
        if (cur && !(g_current && (GLuint)cur == g_current->program)) {
            g_current = nullptr;
            return;
        }
        glsl_activate();
    } else {
        GLenum cap = vertex ? GL_VERTEX_PROGRAM_ARB : GL_FRAGMENT_PROGRAM_ARB;
        int &e = arb_state().enabled[!vertex];
        if (e == (int)on) return;
        on ? glEnable(cap) : glDisable(cap);
        e = on;
    }
}

CG_EXPORT void cgGLEnableProfile(CGprofile profile) {
    set_profile_enabled(profile, true, "cgGLEnableProfile");
}

CG_EXPORT void cgGLDisableProfile(CGprofile profile) {
    set_profile_enabled(profile, false, "cgGLDisableProfile");
}

CG_EXPORT void cgGLLoadProgram(CGprogram handle) {
    Program *p = check<Program>(handle, CG_INVALID_PROGRAM_HANDLE_ERROR);
    if (p) load_program(p);
}

CG_EXPORT void cgGLBindProgram(CGprogram handle) {
    Program *p = check<Program>(handle, CG_INVALID_PROGRAM_HANDLE_ERROR);
    if (!p) return;
    if (!p->loaded) {
        set_error(CG_PROGRAM_NOT_LOADED_ERROR, "cgGLBindProgram before cgGLLoadProgram");
        return;
    }
    (p->stage == Stage::Vertex ? g_bound_vp : g_bound_fp) = p;
    if (cfg().glsl) {
        glsl_activate();
    } else {
        arb_bind(arb_target(p), p->arb_id);
        arb_upload(p);
    }
}

CG_EXPORT void cgGLUnbindProgram(CGprofile profile) {
    if (is_vertex_profile(profile)) g_bound_vp = nullptr;
    else g_bound_fp = nullptr;
    if (cfg().glsl) glsl_activate();
    else arb_bind(is_vertex_profile(profile) ? GL_VERTEX_PROGRAM_ARB : GL_FRAGMENT_PROGRAM_ARB, 0);
}

CG_EXPORT void cgGLSetParameter4fv(CGparameter handle, const float *v) {
    Param *q = check<Param>(handle, CG_INVALID_PARAM_HANDLE_ERROR);
    if (!q || !v) return;
    write_slots(q, 0, v, 1);
}

CG_EXPORT void cgGLSetMatrixParameterfr(CGparameter handle, const float *m) {
    Param *q = check<Param>(handle, CG_INVALID_PARAM_HANDLE_ERROR);
    if (!q || !m) return;
    int rows = q->info.rows, cols = q->info.cols;
    if (rows < 2) {   // not a matrix type
        set_error(CG_INVALID_PARAMETER_ERROR, "cgGLSetMatrixParameterfr on non-matrix");
        return;
    }
    float r[16] = {0};
    for (int i = 0; i < rows && i < 4; i++)
        for (int j = 0; j < cols && j < 4; j++) r[4 * i + j] = m[i * cols + j];
    write_slots(q, 0, r, rows);
}

// Vertex inputs. The glsl backend binds every input to the generic attribute
// NVIDIA aliasing assigns it; the arb backend uses the conventional arrays for
// conventional inputs, as NVIDIA's runtime does.
// Inputs bound to ATTRn (and BLENDWEIGHT, which has no conventional array) are
// generic attributes even for the arb backend.
static bool is_generic(const Param *q) {
    return cfg().glsl || (q->info.resource >= 2113 && q->info.resource <= 2128) || q->info.attrib == 1;
}

static bool normalizes(const Param *q, GLenum type) {
    if (type == GL_FLOAT || type == GL_DOUBLE || type == GL_HALF_FLOAT_ARB) return false;
    int loc = q->info.attrib;
    return loc == 2 || loc == 3 || loc == 4;   // normal, primary/secondary color
}

static void client_state(Param *q, bool enable) {
    int loc = q->info.attrib;
    if (loc < 0) {
        set_error(CG_INVALID_PARAMETER_ERROR, "not a vertex input");
        return;
    }
    if (is_generic(q)) {
        enable ? glEnableVertexAttribArray(loc) : glDisableVertexAttribArray(loc);
        return;
    }
    GLenum array;
    switch (loc) {
    case 0: array = GL_VERTEX_ARRAY; break;
    case 2: array = GL_NORMAL_ARRAY; break;
    case 3: array = GL_COLOR_ARRAY; break;
    case 4: array = GL_SECONDARY_COLOR_ARRAY; break;
    case 5: array = GL_FOG_COORDINATE_ARRAY; break;
    default:
        glClientActiveTexture(GL_TEXTURE0 + (loc - 8));
        enable ? glEnableClientState(GL_TEXTURE_COORD_ARRAY) : glDisableClientState(GL_TEXTURE_COORD_ARRAY);
        glClientActiveTexture(GL_TEXTURE0);
        return;
    }
    enable ? glEnableClientState(array) : glDisableClientState(array);
}

CG_EXPORT void cgGLEnableClientState(CGparameter handle) {
    if (Param *q = check<Param>(handle, CG_INVALID_PARAM_HANDLE_ERROR)) client_state(q, true);
}

CG_EXPORT void cgGLDisableClientState(CGparameter handle) {
    if (Param *q = check<Param>(handle, CG_INVALID_PARAM_HANDLE_ERROR)) client_state(q, false);
}

CG_EXPORT void cgGLSetParameterPointer(CGparameter handle, int fsize, CGGLenum_t type, int stride,
                                       const void *pointer) {
    Param *q = check<Param>(handle, CG_INVALID_PARAM_HANDLE_ERROR);
    if (!q) return;
    int loc = q->info.attrib;
    if (loc < 0) {
        set_error(CG_INVALID_PARAMETER_ERROR, "not a vertex input");
        return;
    }
    if (is_generic(q)) {
        // Conventional normal/fog arrays have 3/1 components whatever fsize says
        // (the shader then reads w = 1); keep that for their generic stand-ins.
        int size = fsize;
        if (q->info.resource == 3092 /* NORMAL0 */ && size > 3) size = 3;
        if (loc == 5 && !(q->info.resource >= 2113 && q->info.resource <= 2128)) size = 1;
        if (stride == 0 && size != fsize) {
            int bytes = type == GL_FLOAT || type == GL_INT || type == GL_UNSIGNED_INT ? 4
                        : type == GL_DOUBLE ? 8 : type == GL_SHORT || type == GL_UNSIGNED_SHORT ? 2 : 1;
            stride = fsize * bytes;
        }
        glVertexAttribPointer(loc, size, type, normalizes(q, type), stride, pointer);
        return;
    }
    // glNormalPointer/glFogCoordPointer have no size: a 0 stride would mean
    // "packed 3 (1) components" even when fsize says otherwise.
    if (stride == 0 && (loc == 2 || loc == 5)) {
        int bytes = type == GL_FLOAT || type == GL_INT || type == GL_UNSIGNED_INT ? 4
                    : type == GL_DOUBLE ? 8 : type == GL_SHORT || type == GL_UNSIGNED_SHORT ? 2 : 1;
        stride = fsize * bytes;
    }
    switch (loc) {
    case 0: glVertexPointer(fsize, type, stride, pointer); break;
    case 2: glNormalPointer(type, stride, pointer); break;
    case 3: glColorPointer(fsize, type, stride, pointer); break;
    case 4: glSecondaryColorPointer(fsize, type, stride, pointer); break;
    case 5: glFogCoordPointer(type, stride, pointer); break;
    default:
        glClientActiveTexture(GL_TEXTURE0 + (loc - 8));
        glTexCoordPointer(fsize, type, stride, pointer);
        glClientActiveTexture(GL_TEXTURE0);
    }
}

// Textures.
static Param *sampler_param(CGparameter handle) {
    Param *q = check<Param>(handle, CG_INVALID_PARAM_HANDLE_ERROR);
    if (q && !q->info.tex_target) {
        set_error(CG_INVALID_PARAMETER_ERROR, "not a sampler");
        return nullptr;
    }
    return q;
}

CG_EXPORT void cgGLSetTextureParameter(CGparameter handle, CGGLuint_t texobj) {
    if (Param *q = sampler_param(handle)) q->texture = texobj;
}

CG_EXPORT void cgGLSetupSampler(CGparameter handle, CGGLuint_t texobj) {
    // For parameters without CgFX sampler state this is cgGLSetTextureParameter.
    cgGLSetTextureParameter(handle, texobj);
}

CG_EXPORT void cgGLEnableTextureParameter(CGparameter handle) {
    Param *q = sampler_param(handle);
    if (!q || q->info.texunit < 0) return;   // unused sampler: no unit assigned
    // The game sets the active unit itself, so it is asked of GL (client-side
    // state, no round trip) and only switched when it differs.
    GLint prev = GL_TEXTURE0;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &prev);
    GLenum unit = GL_TEXTURE0 + q->info.texunit;
    if ((GLenum)prev != unit) glActiveTexture(unit);
    glBindTexture(q->info.tex_target, q->texture);
    if ((GLenum)prev != unit) glActiveTexture(prev);
}

CG_EXPORT void cgGLDisableTextureParameter(CGparameter handle) {
    // Programmable pipelines sample whatever is bound; nothing to undo.
    sampler_param(handle);
}

CG_EXPORT CGGLenum_t cgGLGetTextureEnum(CGparameter handle) {
    Param *q = sampler_param(handle);
    if (!q) return GL_INVALID_OPERATION;
    if (q->info.texunit < 0) {   // unused sampler: NVIDIA's runtime fails the same way
        set_error(CG_INVALID_PARAMETER_ERROR, "sampler has no texture unit");
        return GL_INVALID_OPERATION;
    }
    return GL_TEXTURE0 + q->info.texunit;
}

// --- errors ---------------------------------------------------------------------

CG_EXPORT int cgGetError(void) {
    int e = g_error;
    g_error = CG_NO_ERROR;
    return e;
}

CG_EXPORT const char *cgGetErrorString(int error) {
    switch (error) {
    case CG_NO_ERROR: return "CG_NO_ERROR";
    case CG_COMPILER_ERROR: return "The compile returned an error.";
    case CG_INVALID_PARAMETER_ERROR: return "The parameter used is invalid.";
    case CG_INVALID_PROFILE_ERROR: return "The profile is not supported.";
    case CG_PROGRAM_LOAD_ERROR: return "The program could not load.";
    case CG_INVALID_CONTEXT_HANDLE_ERROR: return "Invalid context handle.";
    case CG_INVALID_PROGRAM_HANDLE_ERROR: return "Invalid program handle.";
    case CG_INVALID_PARAM_HANDLE_ERROR: return "Invalid parameter handle.";
    case CG_INVALID_ENUMERANT_ERROR: return "Invalid enumerant parameter.";
    case CG_PROGRAM_NOT_LOADED_ERROR: return "The program must be loaded before this operation may be used.";
    default: return "Unknown error.";
    }
}

CG_EXPORT const char *cgGetLastListing(CGcontext) { return g_listing.c_str(); }
