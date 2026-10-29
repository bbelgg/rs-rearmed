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
//                   ARB_vertex_program: ANGLE, and the Metal backend of
//                   libRSOpenGL, which translates its GLSL to MSL.
//
// Environment:
//   RS_CG_BACKEND=arb|glsl      backend (default arb; always glsl when
//                               RS_GL_BACKEND=metal, which has no ARB programs)
//   RS_CG_GLSL_DIALECT=120|100es
//   RS_CG_GLSL_EAGER=1          glsl: link a pair at bind time, not at first use
//   RS_CG_PAIR_CACHE=<file>|0   glsl: pairs linked in earlier runs, linked again as
//                               soon as both programs are loaded (see "Pair cache";
//                               default ~/Library/Caches/rs-rearmed/cg-pairs.txt when
//                               running from an .app bundle, off otherwise)
//   RS_CG_LOG=1                 log errors, program creation, compiles and links
//                               (with their times) to stderr
//   RS_CG_DUMP_GLSL=<dir>       write every generated GLSL shader there
//                               (RS_CG_DUMP_DIR is the old name)
#define GL_SILENCE_DEPRECATION
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>

#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <mach/mach_time.h>
#include <sys/stat.h>

#include <algorithm>
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <pthread.h>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "arb_program.h"
#include "cg_api.h"
#include "cg_key.h"
#include "cg_stats.h"

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

bool env_on(const char *name) {
    const char *v = getenv(name);
    return v && *v && strcmp(v, "0");
}

struct Config {
    bool glsl = false;
    bool glsl_eager = false;
    std::string dialect = "120";
    bool log = false;
    std::string dump_dir;
    std::string pair_cache;   // empty: off
    Config() {
        const char *b = getenv("RS_CG_BACKEND");
        if (b && !strcmp(b, "glsl")) glsl = true;
        // libRSOpenGL's Metal core implements no ARB programs.
        const char *gl = getenv("RS_GL_BACKEND");
        if (gl && !strcmp(gl, "metal")) glsl = true;
        if (const char *d = getenv("RS_CG_GLSL_DIALECT")) dialect = d;
        glsl_eager = env_on("RS_CG_GLSL_EAGER");
        log = env_on("RS_CG_LOG");
        if (const char *d = getenv("RS_CG_DUMP_DIR")) dump_dir = d;
        if (const char *d = getenv("RS_CG_DUMP_GLSL")) dump_dir = d;
        const char *pc = getenv("RS_CG_PAIR_CACHE");
        if (pc && *pc) {
            if (strcmp(pc, "0")) pair_cache = pc;
        } else {
            // Like libRSOpenGL's Metal cache: only the app writes there by
            // default, never tools and tests.
            char exe[4096];
            uint32_t n = sizeof exe;
            const char *home = getenv("HOME");
            if (home && *home && !_NSGetExecutablePath(exe, &n) && strstr(exe, ".app/Contents/MacOS/"))
                pair_cache = std::string(home) + "/Library/Caches/rs-rearmed/cg-pairs.txt";
        }
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

double now_us() {
    static mach_timebase_info_data_t tb;
    if (!tb.denom) mach_timebase_info(&tb);
    return (double)mach_absolute_time() * tb.numer / tb.denom / 1000.0;
}

// GL calls the shim makes, by kind (rs_cg_gl_stats, cg_stats.h). Every GL call
// below goes through one of these macros.
unsigned long long g_stats[RS_CG_STAT_COUNT];
#define RS_GL_COUNTED(name, stat) \
    template <class... A> inline void name##_c(A... a) { g_stats[stat]++; ::name(a...); }
RS_GL_COUNTED(glUseProgram, ST_UseProgram)
RS_GL_COUNTED(glUniform4fv, ST_Uniform)
RS_GL_COUNTED(glUniform1i, ST_Uniform)
RS_GL_COUNTED(glGetIntegerv, ST_Get)
RS_GL_COUNTED(glCompileShader, ST_Compile)
RS_GL_COUNTED(glBindProgramARB, ST_BindProgramARB)
RS_GL_COUNTED(glProgramLocalParameters4fvEXT, ST_ProgramLocal)
RS_GL_COUNTED(glProgramLocalParameter4fvARB, ST_ProgramLocal)
RS_GL_COUNTED(glEnable, ST_Enable)
RS_GL_COUNTED(glDisable, ST_Enable)
RS_GL_COUNTED(glActiveTexture, ST_Texture)
RS_GL_COUNTED(glBindTexture, ST_Texture)
RS_GL_COUNTED(glVertexAttribPointer, ST_Attrib)
RS_GL_COUNTED(glEnableVertexAttribArray, ST_Attrib)
RS_GL_COUNTED(glDisableVertexAttribArray, ST_Attrib)
#undef RS_GL_COUNTED
#define glUseProgram glUseProgram_c
#define glUniform4fv glUniform4fv_c
#define glUniform1i glUniform1i_c
#define glGetIntegerv glGetIntegerv_c
#define glCompileShader glCompileShader_c
#define glBindProgramARB glBindProgramARB_c
#define glProgramLocalParameters4fvEXT glProgramLocalParameters4fvEXT_c
#define glProgramLocalParameter4fvARB glProgramLocalParameter4fvARB_c
#define glEnable glEnable_c
#define glDisable glDisable_c
#define glActiveTexture glActiveTexture_c
#define glBindTexture glBindTexture_c
#define glVertexAttribPointer glVertexAttribPointer_c
#define glEnableVertexAttribArray glEnableVertexAttribArray_c
#define glDisableVertexAttribArray glDisableVertexAttribArray_c

// GL_CURRENT_PROGRAM. libRSOpenGL tracks it in its glUseProgram wrapper and
// answers without a GL call (rs_gl_current_program, -1 when it doesn't know);
// looked up at run time, so the shim runs on a libRSOpenGL without it too.
// RS_CG_TRACKED_PROGRAM=0 always asks GL (for comparisons).
typedef int (*CurrentProgramFn)(void);
CurrentProgramFn g_current_program_fn;
bool g_current_program_init;

bool tracked_program_available() {
    if (!g_current_program_init) {
        g_current_program_init = true;
        const char *e = getenv("RS_CG_TRACKED_PROGRAM");
        if (!(e && !strcmp(e, "0")))
            g_current_program_fn = (CurrentProgramFn)dlsym(RTLD_DEFAULT, "rs_gl_current_program");
    }
    return g_current_program_fn != nullptr;
}

// The tracked current program, -1 when there is none (no GL call either way).
int tracked_program() {
    if (!tracked_program_available()) return -1;
    int p = g_current_program_fn();
    if (p >= 0) g_stats[ST_CurProgram]++;
    return p;
}

GLint current_program() {
    int p = tracked_program();
    if (p >= 0) return p;
    GLint cur = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &cur);
    return cur;
}

// Share group of `c`. libRSOpenGL's Metal contexts aren't CGL contexts and
// CGLGetShareGroup answers NULL for them: each such context is its own group.
CGLShareGroupObj group_of(CGLContextObj c) {
    if (!c) return nullptr;
    CGLShareGroupObj g = CGLGetShareGroup(c);
    return g ? g : (CGLShareGroupObj)c;
}

// The shim keeps no locks: the game makes every Cg call on its render thread.
// Log (once) if that ever stops being true.
void check_thread(const char *fn) {
    static pthread_t first = pthread_self();
    static bool warned = false;
    if (!warned && !pthread_equal(first, pthread_self())) {
        warned = true;
        fprintf(stderr, "[cg-shim] %s called from a second thread; the shim is not thread-safe\n", fn);
    }
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
    uint64_t key = 0;                // cg_program_key: names the program in logs and the pair cache
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
    uint64_t arb_uploaded = 0;       // (glsl backend: shaders and links in Gpu)
};

struct Context {
    std::vector<Program *> programs;
};

std::set<void *> g_live;   // every valid handle, so stale/garbage handles are rejected

// Direct-mapped cache of handles found in g_live: the game passes the same few
// hundred parameters every frame, and a set lookup per cgGLSet* call shows in
// profiles. An entry is cleared when its handle leaves g_live.
// Hashed rather than indexed by address bits: parameters are heap objects whose
// low bits repeat, so a plain index left most of them missing in gameplay.
void *g_live_cache[16384];
inline void *&live_cache_slot(void *h) {
    return g_live_cache[(((uintptr_t)h >> 4) * 0x9E3779B97F4A7C15ull) >> 50];
}

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
//
// The game binds a Cg program only when it differs from the one it bound last
// for that stage (it keeps that in two globals for the whole run) and never
// disables a profile or unbinds. Between its Cg draws Scaleform makes its own
// GLSL programs current and ends every frame with glUseProgram(0). With ARB
// programs that is harmless (program 0 brings the ARB bindings back); with one
// GLSL program per pair the shim has to take GL back itself: every Cg call that
// prepares a draw (bind, a changed parameter, cgGLEnableClientState, which the
// game issues for every vertex stream of every draw) checks GL_CURRENT_PROGRAM
// and makes the pair's program current again unless a program of the game's own
// is current (that one wins, as it does over ARB programs).
//
// The check reads GL_CURRENT_PROGRAM per changed parameter (its upload needs
// our program current) and once per run of cgGLEnableClientState calls (the
// game's stream setup loop issues nothing else in between). libRSOpenGL tracks
// the current program (rs_gl_current_program), so that is no GL call when it
// knows; then every cgGLEnableClientState checks.
//
// Pairs are linked lazily: binding a vertex program whose new pair has no GL
// program yet only marks it pending, and the next Cg call (the fragment bind, a
// parameter, a stream, a texture) links it. The game binds the vertex program
// and then the fragment program, so linking at the first bind would build a
// pair (new vertex, previous fragment) that is never drawn.
// RS_CG_GLSL_EAGER=1 links at bind time instead.

struct Link {
    GLuint program = 0;
    bool ok = false;
    Program *vp = nullptr, *fp = nullptr;
    std::vector<GLint> vp_loc, fp_loc;   // per-slot uniform locations
    uint64_t vp_seen = 0, fp_seen = 0;
};

struct Shaders {
    GLuint fs = 0;
    bool fs_done = false;                // compiled (or failed) once
    std::map<unsigned, GLuint> vs;       // per varying set (see vs_key)
};

// GL objects live in a share group; everything the backend created for one is
// kept here. The game has a single context, tests use several.
struct Gpu {
    CGLShareGroupObj group = nullptr;
    std::map<std::pair<Program *, Program *>, std::unique_ptr<Link>> links;
    std::map<const Program *, Shaders> shaders;   // nullptr: the passthrough shaders
    std::set<GLuint> ours;                        // GL program names of `links`
    uint64_t pair_serial = 0;                     // g_pair_serial `active` is for
    Link *active = nullptr;                       // linked program of the bound pair
    bool pending = false;                         // bound pair not linked yet
};

std::vector<std::unique_ptr<Gpu>> g_gpus;
CGLContextObj g_gpu_ctx = nullptr;
Gpu *g_gpu = nullptr;

Gpu &gpu() {
    CGLContextObj c = CGLGetCurrentContext();
    if (g_gpu && c == g_gpu_ctx) return *g_gpu;
    CGLShareGroupObj sg = group_of(c);
    g_gpu_ctx = c;
    for (auto &g : g_gpus)
        if (g->group == sg) return *(g_gpu = g.get());
    g_gpus.push_back(std::make_unique<Gpu>());
    g_gpu = g_gpus.back().get();
    g_gpu->group = sg;
    return *g_gpu;
}

Program *g_bound_vp = nullptr, *g_bound_fp = nullptr;
bool g_vp_enabled = false, g_fp_enabled = false;   // cgGLEnableProfile state
uint64_t g_pair_serial = 1;                        // bumped when the bound pair changes

Program *active_vp() { return g_vp_enabled ? g_bound_vp : nullptr; }
Program *active_fp() { return g_fp_enabled ? g_bound_fp : nullptr; }

void dump(const std::string &name, const std::string &text) {
    if (cfg().dump_dir.empty()) return;
    std::ofstream(cfg().dump_dir + "/" + name) << text;
}

std::string program_tag(const Program *p) {
    if (!p) return "passthrough";
    char b[32];
    snprintf(b, sizeof b, "%016llx", (unsigned long long)p->key);
    return b;
}

// `t0`: when the work for this shader started (translation included).
GLuint compile(GLenum kind, const std::string &src, const std::string &tag, double t0) {
    GLuint s = glCreateShader(kind);
    const char *t = src.c_str();
    glShaderSource(s, 1, &t, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    double us = now_us() - t0;
    g_stats[ST_CompileUs] += (unsigned long long)us;
    logf("compiled %s in %.2f ms", tag.c_str(), us / 1000);
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

// A vertex shader declares and writes every varying it has a value for plus
// those the fragment shader reads (the rest at their (0,0,0,1) default), so one
// shader per vertex program serves every fragment program whose inputs it
// writes: varyings the fragment shader doesn't read are allowed.
unsigned vs_key(const Program *vp, unsigned fs_inputs) {
    return (vp ? vp->arb.outputs : passthrough(Stage::Vertex).outputs) | fs_inputs;
}

GLuint vertex_shader(Gpu &g, Program *vp, unsigned fs_inputs) {
    unsigned key = vs_key(vp, fs_inputs);
    Shaders &sh = g.shaders[vp];
    auto it = sh.vs.find(key);
    if (it != sh.vs.end()) return it->second;
    double t0 = now_us();
    const ArbProgram &a = vp ? vp->arb : passthrough(Stage::Vertex);
    char tag[64];
    snprintf(tag, sizeof tag, "%s.%03x.vert", program_tag(vp).c_str(), key);
    GLuint s = compile(GL_VERTEX_SHADER, emit_glsl(a, cfg().dialect, key), tag, t0);
    sh.vs[key] = s;
    return s;
}

GLuint fragment_shader(Gpu &g, Program *fp) {
    Shaders &sh = g.shaders[fp];
    if (sh.fs_done) return sh.fs;
    double t0 = now_us();
    const ArbProgram &a = fp ? fp->arb : passthrough(Stage::Fragment);
    std::string tag = program_tag(fp) + ".frag";
    sh.fs = compile(GL_FRAGMENT_SHADER, emit_glsl(a, cfg().dialect, 0), tag, t0);
    sh.fs_done = true;
    return sh.fs;
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

// --- pair cache ---------------------------------------------------------------
//
// A pair's first link is a GLSL compile in the driver (on the Metal backend also
// an MSL translation and a Metal compile), and linked lazily it happens at the
// pair's first draw, in the middle of a song. Every pair linked is recorded in a
// file by program keys; in later runs a program's load links the recorded pairs
// whose other program is loaded already, so those links happen while the game
// loads. File: a header line, then "<vertex key> <fragment key>\n" in hex per
// pair (0: no program for that stage). Bad lines are skipped, a file with another
// header starts over, at most kMaxPairs are kept.

struct PairCache {
    bool opened = false;
    FILE *out = nullptr;
    std::set<std::pair<uint64_t, uint64_t>> known;
    std::unordered_map<uint64_t, std::vector<uint64_t>> partners;   // key -> keys paired with it
};
PairCache g_pairs;
std::unordered_map<uint64_t, std::vector<Program *>> g_loaded;   // glsl: loaded programs by key
const char kPairHeader[] = "rs-cg-pairs 1\n";
enum { kMaxPairs = 65536 };

bool pair_add(uint64_t v, uint64_t f) {
    if (g_pairs.known.size() >= kMaxPairs || !g_pairs.known.insert({v, f}).second) return false;
    g_pairs.partners[v].push_back(f);
    g_pairs.partners[f].push_back(v);
    return true;
}

void pair_cache_open() {
    if (g_pairs.opened) return;
    g_pairs.opened = true;
    const std::string &path = cfg().pair_cache;
    if (path.empty()) return;
    for (size_t i = 1; i < path.size(); i++)   // parent directories
        if (path[i] == '/') mkdir(path.substr(0, i).c_str(), 0755);
    bool keep = false, newline = true;
    if (FILE *in = fopen(path.c_str(), "r")) {
        char line[128];
        if (fgets(line, sizeof line, in) && !strcmp(line, kPairHeader)) {
            keep = true;
            while (fgets(line, sizeof line, in)) {
                size_t n = strlen(line);
                newline = n && line[n - 1] == '\n';
                unsigned long long v, f;
                char end = 0;
                if (sscanf(line, "%16llx %16llx%c", &v, &f, &end) == 3 && end == '\n') pair_add(v, f);
            }
        }
        fclose(in);
    }
    g_pairs.out = fopen(path.c_str(), keep ? "a" : "w");
    if (!g_pairs.out) {
        logf("pair cache %s: can't write (%s)", path.c_str(), strerror(errno));
        return;
    }
    if (!keep) fputs(kPairHeader, g_pairs.out);
    else if (!newline) fputc('\n', g_pairs.out);   // a cut last line stays on its own
    fflush(g_pairs.out);
    logf("pair cache %s: %zu pairs", path.c_str(), g_pairs.known.size());
}

void pair_record(const Program *vp, const Program *fp) {
    pair_cache_open();
    uint64_t v = vp ? vp->key : 0, f = fp ? fp->key : 0;
    if (!g_pairs.out || !pair_add(v, f)) return;
    fprintf(g_pairs.out, "%016llx %016llx\n", (unsigned long long)v, (unsigned long long)f);
    fflush(g_pairs.out);
}

Link *get_link(Gpu &g, Program *vp, Program *fp);

// Link the recorded pairs of `p` (just loaded) whose other program is loaded.
void pair_warm(Program *p) {
    pair_cache_open();
    auto it = g_pairs.partners.find(p->key);
    if (it == g_pairs.partners.end()) return;
    std::vector<uint64_t> keys = it->second;
    Gpu &g = gpu();
    double t0 = now_us();
    int n = 0;
    for (uint64_t k : keys) {
        std::vector<Program *> others{nullptr};   // key 0: the passthrough shader
        if (k) {
            auto l = g_loaded.find(k);
            others = l != g_loaded.end() ? l->second : std::vector<Program *>();
        }
        for (Program *q : others) {
            if (q && q->stage == p->stage) continue;
            Program *vp = p->stage == Stage::Vertex ? p : q, *fp = p->stage == Stage::Vertex ? q : p;
            if (g.links.count({vp, fp})) continue;
            get_link(g, vp, fp);
            n++;
        }
    }
    g_stats[ST_Warmed] += n;
    if (n) logf("warmed %d pairs of %s in %.1f ms", n, program_tag(p).c_str(), (now_us() - t0) / 1000);
}

// Link the pair (cached, failures included). Leaves GL's current program as it
// found it.
Link *get_link(Gpu &g, Program *vp, Program *fp) {
    auto key = std::make_pair(vp, fp);
    auto it = g.links.find(key);
    if (it != g.links.end()) return it->second.get();
    double t0 = now_us();
    unsigned long long compile_us = g_stats[ST_CompileUs];
    auto l = std::make_unique<Link>();
    l->vp = vp;
    l->fp = fp;
    unsigned fs_inputs = (fp ? fp->arb : passthrough(Stage::Fragment)).outputs;
    GLuint v = vertex_shader(g, vp, fs_inputs), f = fragment_shader(g, fp);
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
        g.ours.insert(l->program);
        g_stats[ST_Links]++;
        if (!ok) {
            char log[4096] = {0};
            glGetProgramInfoLog(l->program, sizeof log, nullptr, log);
            g_listing = log;
            logf("GLSL link failed (%s + %s):\n%s", program_tag(vp).c_str(), program_tag(fp).c_str(), log);
        } else {
            l->ok = true;
            GLint prev = current_program();
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
            if ((GLuint)prev != l->program) glUseProgram(prev);
        }
    }
    double us = now_us() - t0;
    g_stats[ST_LinkUs] += (unsigned long long)us;
    if (us > g_stats[ST_LinkMaxUs]) g_stats[ST_LinkMaxUs] = (unsigned long long)us;
    if (cfg().log)
        logf("linked %s + %s in %.2f ms (%.2f ms compiling), link %llu", program_tag(vp).c_str(),
             program_tag(fp).c_str(), us / 1000, (g_stats[ST_CompileUs] - compile_us) / 1000.0,
             g_stats[ST_Links]);
    if (l->ok) pair_record(vp, fp);
    Link *raw = l.get();
    g.links[key] = std::move(l);
    return raw;
}

// Upload the slots of `p` written after `*seen` into the current GL program,
// one glUniform4fv per run of changed slots. A uniform array loads `count`
// elements from the one `loc[i]` names whatever the other locations are; a run
// stops at an element the linker dropped (location -1: past the highest one used).
void upload_slots(Program *p, const std::vector<GLint> &loc, uint64_t *seen) {
    if (!p || *seen >= p->stamp) return;
    int n = (int)loc.size();
    const uint64_t *st = p->slot_stamp.data(), s = *seen;
    for (int i = 0; i < n; i++) {
        if (st[i] <= s || loc[i] < 0) continue;
        int j = i + 1;
        while (j < n && st[j] > s && loc[j] >= 0) j++;
        glUniform4fv(loc[i], j - i, &p->slots[4 * i]);
        i = j - 1;
    }
    *seen = p->stamp;
}

// The bound pair's linked program in `g`. With `defer`, a pair that has no GL
// program yet is left pending instead of being linked.
Link *bound_link(Gpu &g, bool defer) {
    if (g.pair_serial == g_pair_serial && !(g.pending && !defer)) return g.active;
    g.pair_serial = g_pair_serial;
    g.pending = false;
    g.active = nullptr;
    Program *vp = active_vp(), *fp = active_fp();
    if (!vp && !fp) return nullptr;
    if (defer && !g.links.count(std::make_pair(vp, fp))) {
        g.pending = true;
        return nullptr;
    }
    Link *l = get_link(g, vp, fp);
    g.active = l->ok ? l : nullptr;
    return g.active;
}

// Make the bound pair's program current and bring its uniforms up to date (see
// the top of this section). Returns the link when its program is current.
Link *glsl_sync(bool defer = false) {
    Gpu &g = gpu();
    Link *l = bound_link(g, defer);
    if (g.pending) return nullptr;
    GLint cur = current_program();
    if (!l) {
        // No pair: fixed function, unless the game has a program of its own
        // current. A pair that failed to link leaves GL as it is.
        if (!active_vp() && !active_fp() && cur && g.ours.count((GLuint)cur)) glUseProgram(0);
        return nullptr;
    }
    if ((GLuint)cur != l->program) {
        if (cur && !g.ours.count((GLuint)cur)) return nullptr;   // the game's own program wins
        glUseProgram(l->program);
    }
    upload_slots(l->vp, l->vp_loc, &l->vp_seen);
    upload_slots(l->fp, l->fp_loc, &l->fp_seen);
    return l;
}

bool g_streams_synced = false;   // a cgGLEnableClientState synced, no other Cg call since

void glsl_pair_changed(bool may_defer) {
    ++g_pair_serial;
    g_streams_synced = false;
    glsl_sync(may_defer && !cfg().glsl_eager);
}

// A pending link is made at the next Cg call that can precede a draw.
void glsl_resolve_pending() {
    g_streams_synced = false;
    if (gpu().pending) glsl_sync();
}

// Drop everything built from `p`. GL objects of other share groups can't be
// deleted from here (their context isn't current); they are only forgotten.
void glsl_forget_program(Program *p) {
    if (g_gpus.empty()) return;
    CGLContextObj c = CGLGetCurrentContext();
    CGLShareGroupObj cur = group_of(c);
    GLint cur_prog = c ? current_program() : 0;
    for (auto &gp : g_gpus) {
        Gpu &g = *gp;
        bool live = g.group == cur;
        // A deleted program stays current until replaced, and then its name is
        // gone: leave it first, as glDeleteProgramsARB unbinds for the arb backend.
        if (live && cur_prog)
            for (auto &l : g.links)
                if ((l.first.first == p || l.first.second == p) && l.second->program == (GLuint)cur_prog) {
                    glUseProgram(0);
                    cur_prog = 0;
                    break;
                }
        for (auto it = g.links.begin(); it != g.links.end();) {
            if (it->first.first == p || it->first.second == p) {
                if (it->second.get() == g.active) g.active = nullptr;
                if (it->second->program) {
                    g.ours.erase(it->second->program);
                    if (live) glDeleteProgram(it->second->program);
                }
                it = g.links.erase(it);
            } else {
                ++it;
            }
        }
        auto s = g.shaders.find(p);
        if (s != g.shaders.end()) {
            if (live) {
                if (s->second.fs) glDeleteShader(s->second.fs);
                for (auto &v : s->second.vs)
                    if (v.second) glDeleteShader(v.second);
            }
            g.shaders.erase(s);
        }
        g.pair_serial = 0;   // recompute the active link
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
    if (lo >= n) {
        if (cfg().glsl && (p == active_vp() || p == active_fp())) glsl_resolve_pending();
        return;
    }
    int hi = n;
    while (st[hi - 1] && !memcmp(dst + 4 * (hi - 1), rows + 4 * (hi - 1), 16)) hi--;
    memcpy(dst + 4 * lo, rows + 4 * lo, 16 * (hi - lo));
    uint64_t t = ++g_clock, prev = p->stamp;
    for (int r = base + lo; r < base + hi; r++) p->slot_stamp[r] = t;
    p->stamp = t;
    if (cfg().glsl) {
        // Uploaded now if `p` is part of the bound pair and GL has its program
        // current (or can be given it back); otherwise at the pair's next sync.
        g_streams_synced = false;
        if (p == active_vp() || p == active_fp()) glsl_sync();
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
        Gpu &g = gpu();
        bool ok = p->stage == Stage::Fragment ? fragment_shader(g, p) != 0 : vertex_shader(g, p, 0) != 0;
        if (!ok) {
            set_error(CG_PROGRAM_LOAD_ERROR, "GLSL compilation failed");
            return false;
        }
        p->loaded = true;
        g_loaded[p->key].push_back(p);
        pair_warm(p);
        return true;
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
    p->key = cg_program_key(p->source.c_str(), p->profile, p->entry.c_str());
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
    if (g_bound_vp == p || g_bound_fp == p) ++g_pair_serial;
    if (g_bound_vp == p) g_bound_vp = nullptr;
    if (g_bound_fp == p) g_bound_fp = nullptr;
    glsl_forget_program(p);
    auto lk = g_loaded.find(p->key);
    if (lk != g_loaded.end()) {
        auto &v = lk->second;
        v.erase(std::remove(v.begin(), v.end(), p), v.end());
        if (v.empty()) g_loaded.erase(lk);
    }
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
        glsl_pair_changed(on);   // the game enables a profile right before binding
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
    check_thread("cgGLLoadProgram");
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
    check_thread("cgGLBindProgram");
    if (cfg().glsl) {
        Program *&b = p->stage == Stage::Vertex ? g_bound_vp : g_bound_fp;
        if (b != p) {
            b = p;
            glsl_pair_changed(p->stage == Stage::Vertex);
        } else {
            g_streams_synced = false;
            glsl_sync();
        }
    } else {
        (p->stage == Stage::Vertex ? g_bound_vp : g_bound_fp) = p;
        arb_bind(arb_target(p), p->arb_id);
        arb_upload(p);
    }
}

CG_EXPORT void cgGLUnbindProgram(CGprofile profile) {
    Program *&b = is_vertex_profile(profile) ? g_bound_vp : g_bound_fp;
    if (cfg().glsl) {
        if (b) {
            b = nullptr;
            glsl_pair_changed(false);
        }
        return;
    }
    b = nullptr;
    arb_bind(is_vertex_profile(profile) ? GL_VERTEX_PROGRAM_ARB : GL_FRAGMENT_PROGRAM_ARB, 0);
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
    Param *q = check<Param>(handle, CG_INVALID_PARAM_HANDLE_ERROR);
    if (!q) return;
    // The game enables every vertex stream before each draw: the point where the
    // bound pair has to be current (see the GLSL backend section). Asking GL
    // once per run of these calls is enough when the game disables its streams
    // after each draw; the tracked current program is cheap enough to check on
    // every one, which also covers a draw that leaves them enabled.
    if (cfg().glsl) {
        if (!g_streams_synced) {
            glsl_sync();
            g_streams_synced = true;
        } else if (int t = tracked_program(); t >= 0) {
            Gpu &g = gpu();
            if (g.active && (GLuint)t != g.active->program) glsl_sync();
        }
    }
    client_state(q, true);
}

CG_EXPORT void cgGLDisableClientState(CGparameter handle) {
    g_streams_synced = false;
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
    if (!q) return;
    if (cfg().glsl) glsl_resolve_pending();
    if (q->info.texunit < 0) return;   // unused sampler: no unit assigned
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

CG_EXPORT void rs_cg_gl_stats(unsigned long long *out) {
    memcpy(out, g_stats, sizeof g_stats);
}
