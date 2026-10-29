// GLSL 1.20 -> Metal Shading Language (msl_xlat.h, docs/METAL-DESIGN.md "Shader translation").
//
// A direct, typed source-to-source translator for the GLSL the game feeds GL: Scaleform GFx's
// shaders (built from fragments at run time) and the Cg shim's ARB -> GLSL output. Both are small,
// regular GLSL 1.20 (no preprocessor beyond #version, ~20 built-in functions), so a parser with full
// GLSL 1.20 typing is far smaller and faster than glslang + SPIRV-Cross and keeps the output
// readable. Everything in the GLSL 1.20 language is parsed and type checked; what has no MSL
// mapping here (shadow samplers, uniform structs, matrix attributes, varying arrays, most
// fixed-function built-in uniforms) fails with a message instead of producing wrong code.
//
// Shape of the output (one library per program):
//
//   helpers                          rs_mod, rs_proj*, matrix helpers (templates)
//   struct rs_vs_in / rs_vs_out      [[stage_in]] attributes, varyings as [[user(v_name)]]
//   struct rs_vs_u                   vertex uniforms ([[buffer(0)]]), offsets in the reflection
//   struct rs_vs_ctx {               the GLSL globals: references to in / uniforms / out, textures,
//     ...; void v_main() {...} }     global variables; GLSL functions become member functions, so
//                                    their bodies use globals by name as in GLSL
//   vertex rs_vs_out rs_vs(...)      runs v_main, then the epilogue (y = -y, z = (z + w) / 2)
//   ... the same for the fragment stage, plus the alpha test (function constant 0, buffer(1))
//
// Every GLSL identifier is renamed v_<name> (so no GLSL name can collide with MSL keywords,
// functions or the translator's own rs_* names; a local that shadows a visible name becomes
// rs_l<n>_<name>, since C++ puts a variable in scope in its own initialiser); swizzles are written
// as xyzw. GLSL's implicit int -> float conversions are made explicit, scalar arguments of genType
// built-ins are broadcast, and integer-to-float literal conversions are folded. out / inout
// arguments are copied in and out (user_call); matrix arithmetic MSL lacks (unary minus, + - /
// with a scalar, / with a matrix) goes through helpers; array assignment through rs_acopy.
// Uniform initialisers are ignored (the core starts every uniform at 0; neither shader family
// uses them).
//
// Conventions the Metal core relies on (in addition to msl_xlat.h):
//   - sampler1D is a texture2d (the core creates GL_TEXTURE_1D as a 2D texture of height 1),
//     sampler2DRect a texture2d sampled with normalised coordinates (coordinates are divided by
//     the texture size in the shader, so its sampler state stays normalised).
//   - sampler arrays take consecutive slots; rsmsl_sampler lists one entry per element,
//     named "tex[0]", "tex[1]", ... (the uniform itself is listed once with array_size N).
//   - uniform storage: bool and bvecN are stored as int / intN (as glUniform1i writes them);
//     matrices are column-major with each column padded to its MSL alignment (float2 columns
//     take 8 bytes, float3 and float4 columns 16); `stride` is the size of one array element.
//   - attributes are always assigned a location (bound by name, built-in aliasing, or the lowest
//     free location in declaration order, like GL's linker); never -1.
#include "msl_internal.h"

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace {

struct XErr {
    std::string msg;
};

[[noreturn]] void fail(int line, const char *fmt, ...) {
    char b[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    throw XErr{line > 0 ? "line " + std::to_string(line) + ": " + b : std::string(b)};
}

// --- Types ------------------------------------------------------------------------------------

enum Base { B_VOID, B_FLOAT, B_INT, B_BOOL, B_SAMPLER, B_STRUCT };
enum Samp { S_NONE, S_1D, S_2D, S_3D, S_CUBE, S_RECT };

struct StructDef;

struct Type {
    Base base = B_VOID;
    int rows = 1, cols = 1;  // vector: rows = size, cols = 1; matrix: cols x rows
    int samp = S_NONE;
    int array = 0;           // 0: not an array
    const StructDef *st = nullptr;

    bool is_array() const { return array > 0; }
    bool num() const { return !array && (base == B_FLOAT || base == B_INT); }
    bool scalar() const { return !array && rows == 1 && cols == 1 && (base == B_FLOAT || base == B_INT || base == B_BOOL); }
    bool vec() const { return !array && cols == 1 && rows > 1; }
    bool mat() const { return !array && cols > 1; }
    int comps() const { return rows * cols; }
    Type elem() const { Type t = *this; t.array = 0; return t; }
    bool operator==(const Type &o) const {
        return base == o.base && rows == o.rows && cols == o.cols && samp == o.samp && array == o.array && st == o.st;
    }
    bool operator!=(const Type &o) const { return !(*this == o); }
};

struct StructDef {
    std::string name;
    std::vector<std::pair<std::string, Type>> fields;
};

Type mk(Base b, int rows = 1, int cols = 1) {
    Type t;
    t.base = b;
    t.rows = rows;
    t.cols = cols;
    return t;
}
Type with_base(Type t, Base b) { t.base = b; return t; }
const Type T_FLOAT = mk(B_FLOAT), T_INT = mk(B_INT), T_BOOL = mk(B_BOOL), T_VOID = mk(B_VOID);

std::string glsl_name(const Type &t) {
    std::string s;
    switch (t.base) {
    case B_VOID: s = "void"; break;
    case B_STRUCT: s = t.st ? t.st->name : "struct"; break;
    case B_SAMPLER: {
        static const char *n[] = {"?", "sampler1D", "sampler2D", "sampler3D", "samplerCube", "sampler2DRect"};
        s = n[t.samp];
        break;
    }
    default:
        if (t.cols > 1) s = t.cols == t.rows ? "mat" + std::to_string(t.cols)
                                             : "mat" + std::to_string(t.cols) + "x" + std::to_string(t.rows);
        else if (t.rows > 1) s = std::string(t.base == B_FLOAT ? "vec" : t.base == B_INT ? "ivec" : "bvec") + std::to_string(t.rows);
        else s = t.base == B_FLOAT ? "float" : t.base == B_INT ? "int" : "bool";
    }
    if (t.array) s += "[" + std::to_string(t.array) + "]";
    return s;
}

// Element type in MSL (no array suffix).
std::string msl_type(const Type &t) {
    switch (t.base) {
    case B_VOID: return "void";
    case B_STRUCT: return "v_" + t.st->name;
    case B_SAMPLER:
        return t.samp == S_3D ? "texture3d<float>" : t.samp == S_CUBE ? "texturecube<float>" : "texture2d<float>";
    default: break;
    }
    if (t.cols > 1) return "float" + std::to_string(t.cols) + "x" + std::to_string(t.rows);
    const char *b = t.base == B_FLOAT ? "float" : t.base == B_INT ? "int" : "bool";
    return t.rows > 1 ? b + std::to_string(t.rows) : std::string(b);
}

int gl_type_enum(const Type &t) {
    if (t.base == B_SAMPLER) {
        static const int e[] = {0, 0x8B5D, 0x8B5E, 0x8B5F, 0x8B60, 0x8B63};
        return e[t.samp];
    }
    if (t.cols > 1) {
        static const int sq[] = {0, 0, 0x8B5A, 0x8B5B, 0x8B5C};
        if (t.cols == t.rows) return sq[t.cols];
        // mat2x3 .. mat4x3
        static const int ns[5][5] = {{0}, {0}, {0, 0, 0, 0x8B65, 0x8B66}, {0, 0, 0x8B67, 0, 0x8B68}, {0, 0, 0x8B69, 0x8B6A, 0}};
        return ns[t.cols][t.rows];
    }
    int n = t.rows - 1;
    if (t.base == B_FLOAT) return n ? 0x8B50 + n - 1 : 0x1406;
    if (t.base == B_INT) return n ? 0x8B53 + n - 1 : 0x1404;
    return n ? 0x8B57 + n - 1 : 0x8B56;
}

// MSL size / alignment of one (non-array) uniform element.
void msl_layout(const Type &t, int *size, int *align) {
    int r = t.rows;
    int vs = r == 1 ? 4 : r == 2 ? 8 : 16;
    if (t.cols > 1) { *size = vs * t.cols; *align = vs; return; }
    *size = vs;
    *align = vs;
}

// --- Tokens -----------------------------------------------------------------------------------

enum TokKind { K_END, K_IDENT, K_INT, K_FLOAT, K_PUNCT };
struct Tok {
    TokKind k;
    std::string s;
    int line;
};

// Length of the operator at p: <<= >>= ++ -- <= >= == != && || ^^ += -= *= /= %= &= |= ^= << >> or 1.
size_t punct_len(const char *p) {
    char c = p[0], d = p[1];
    if ((c == '<' || c == '>') && d == c) return p[2] == '=' ? 3 : 2;
    if ((c == '+' || c == '-' || c == '&' || c == '|' || c == '^') && d == c) return 2;
    if (d == '=' && strchr("<>=!+-*/%&|^", c)) return 2;
    return 1;
}

std::vector<Tok> tokenize(const char *src) {
    std::vector<Tok> v;
    v.reserve(strlen(src) / 3 + 16);
    int line = 1;
    bool line_start = true;
    const char *p = src;
    while (*p) {
        char c = *p;
        if (c == '\n') { line++; line_start = true; p++; continue; }
        if (isspace((unsigned char)c)) { p++; continue; }
        if (c == '/' && p[1] == '/') { while (*p && *p != '\n') p++; continue; }
        if (c == '/' && p[1] == '*') {
            p += 2;
            while (*p && !(p[0] == '*' && p[1] == '/')) { if (*p == '\n') line++; p++; }
            if (*p) p += 2;
            continue;
        }
        if (c == '#' && line_start) {
            std::string d;
            while (*p && *p != '\n') {
                if (p[0] == '\\' && p[1] == '\n') { p += 2; line++; continue; }
                d += *p++;
            }
            size_t i = 1;
            while (i < d.size() && isspace((unsigned char)d[i])) i++;
            size_t j = i;
            while (j < d.size() && isalpha((unsigned char)d[j])) j++;
            std::string dir = d.substr(i, j - i);
            if (dir.empty() || dir == "version" || dir == "extension" || dir == "pragma" || dir == "line") continue;
            fail(line, "preprocessor directive #%s is not supported", dir.c_str());
        }
        line_start = false;
        if (isalpha((unsigned char)c) || c == '_') {
            const char *s = p;
            while (isalnum((unsigned char)*p) || *p == '_') p++;
            v.push_back({K_IDENT, std::string(s, p), line});
            continue;
        }
        if (isdigit((unsigned char)c) || (c == '.' && isdigit((unsigned char)p[1]))) {
            const char *s = p;
            bool flt = false;
            if (c == '0' && (p[1] == 'x' || p[1] == 'X')) {
                p += 2;
                while (isxdigit((unsigned char)*p)) p++;
            } else {
                while (isdigit((unsigned char)*p)) p++;
                if (*p == '.') { flt = true; p++; while (isdigit((unsigned char)*p)) p++; }
                if ((*p == 'e' || *p == 'E') &&
                    (isdigit((unsigned char)p[1]) || ((p[1] == '+' || p[1] == '-') && isdigit((unsigned char)p[2])))) {
                    flt = true;
                    p += 2;
                    while (isdigit((unsigned char)*p)) p++;
                }
                if (*p == 'f' || *p == 'F') { flt = true; p++; }
            }
            v.push_back({flt ? K_FLOAT : K_INT, std::string(s, p), line});
            continue;
        }
        size_t n = punct_len(p);
        v.push_back({K_PUNCT, std::string(p, n), line});
        p += n;
    }
    v.push_back({K_END, "", line});
    return v;
}

// --- Symbols ----------------------------------------------------------------------------------

enum VarKind { V_LOCAL, V_PARAM, V_GLOBAL, V_UNIFORM, V_ATTRIB, V_VARYING, V_BUILTIN, V_CONST };

struct Var {
    std::string name;   // GLSL name
    std::string mname;  // member / variable name in MSL
    std::string msl;    // expression that reads it
    Type t;
    VarKind kind = V_LOCAL;
    bool lvalue = true;
    bool used = false;
    bool written = false;  // assigned or passed as out / inout (array parameters)
    int decl_order = 0;
    int loc = -1;        // attributes: fixed (built-in) location
    bool has_cv = false;
    double cv = 0;
};
using VarP = std::shared_ptr<Var>;

struct Param {
    std::string name;
    Type t;
    int qual = 0;  // 0 in, 1 out, 2 inout
};

struct Func {
    std::string name;
    Type ret;
    std::vector<Param> params;
    bool defined = false;
};

// Typed expression: MSL text plus what the parent needs to convert or fold it.
struct E {
    Type t;
    std::string s;       // MSL text (for samplers: the texture)
    std::string smp;     // for samplers: the sampler state
    bool lvalue = false;
    bool cst = false;    // compile-time scalar constant
    double cv = 0;
    bool lit = false;    // a bare literal (int literals convert to float literals)
    Var *var = nullptr;   // the variable this expression names (for gl_TexCoord / gl_FragData indexing)
    Var *root = nullptr;  // l-values: the variable at the root of the access chain (v in v.xy, v[i].a)
};

// A folded constant as a GLSL int (saturated: float -> int conversion of out-of-range values is
// undefined in C++, and such constants only come from nonsense input).
long long ival(double v) {
    if (!(v == v)) return 0;
    if (v >= 2147483647.0) return 2147483647;
    if (v <= -2147483648.0) return -2147483648LL;
    return (long long)v;
}

std::string fmt_num(double v, bool flt) {
    if (!flt) return std::to_string(ival(v));
    char b[64];
    snprintf(b, sizeof b, "%.9g", v);
    std::string s = b;
    if (s.find_first_of(".en") == std::string::npos) s += ".0";
    return s + "f";
}

// --- Per-stage parser / emitter -----------------------------------------------------------------

// Nesting limit (counted in assign / unary / statement): one level takes ~1.8 KB of stack in an
// optimised build. The translator runs on the GL thread, which may be a secondary thread with a
// 512 KB stack, so deep input must fail with a message instead of overflowing it: 128 levels are
// ~230 KB. The game's shaders nest at most 18.
const int kMaxNest = 128;
// Arrays and uniform blocks: far beyond any GL implementation's limits (Apple: 4096 uniform
// components per stage), small enough that sizes and offsets can't overflow.
const int kMaxArray = 1 << 16;
const int kMaxBlock = 1 << 20;

struct Stage;
struct Nest {
    int *d;
    Nest(Stage *s, int ln);
    ~Nest() { --*d; }
};

struct Stage {
    int stage;  // RSMSL_STAGE_VS / _FS
    std::vector<Tok> toks;
    size_t pos = 0;

    std::vector<std::map<std::string, VarP>> scopes;  // [0] = globals
    std::vector<VarP> globals;                         // declaration order (uniforms, attributes, varyings, ...)
    std::map<std::string, VarP> builtins;
    std::map<std::string, std::vector<std::shared_ptr<Func>>> funcs;
    std::map<std::string, std::shared_ptr<StructDef>> structs;
    std::vector<std::string> struct_order;
    std::string members;  // ctx member declarations of globals (with initialisers)
    std::string structs_text, funcs_text;
    const Func *cur_func = nullptr;
    int order = 0;
    int uniq = 0;   // temporaries / renamed locals
    int depth = 0;  // expression / statement nesting (bounded: deep input must fail, not overflow the stack)

    // Built-ins seen.
    bool uses_fragcoord = false, uses_frontfacing = false, uses_pointcoord = false;
    bool uses_fragcolor = false, uses_fragdata = false, writes_depth = false, uses_pointsize = false;
    int fragdata_max = 0;           // highest gl_FragData index + 1
    unsigned texcoord_mask = 0;     // gl_TexCoord elements used
    bool uses_texcoord = false;
    bool ff_color = false, ff_seccolor = false, ff_fog = false;  // gl_FrontColor / gl_Color, ...
    bool uses_clipvertex = false, uses_backcolor = false;
    bool uses_instanceid = false, uses_vertexid = false;

    explicit Stage(int st, const char *src) : stage(st) {
        toks = tokenize(src ? src : "");
        scopes.emplace_back();
    }

    bool vs() const { return stage == RSMSL_STAGE_VS; }
    const Tok &peek(int o = 0) const { return toks[std::min(pos + o, toks.size() - 1)]; }
    const Tok &next() { const Tok &t = toks[pos]; if (pos < toks.size() - 1) pos++; return t; }
    bool is(const char *s, int o = 0) const {
        const Tok &t = peek(o);
        if (t.k != K_PUNCT && t.k != K_IDENT) return false;
        const char *a = t.s.c_str();
        while (*a && *a == *s) a++, s++;
        return *a == *s;
    }
    bool accept(const char *s) { if (is(s)) { next(); return true; } return false; }
    void expect(const char *s) {
        if (!accept(s)) fail(peek().line, "expected '%s' before '%s'", s, peek().s.c_str());
    }
    int line() const { return peek().line; }

    // --- type names
    bool type_at(int o, Type *out) const {
        const Tok &t = peek(o);
        if (t.k != K_IDENT) return false;
        const std::string &s = t.s;
        if (!strchr("vfibms", s[0]) && structs.empty()) return false;  // quick reject (most identifiers)
        Type r;
        if (s == "void") r = T_VOID;
        else if (s == "float") r = T_FLOAT;
        else if (s == "int") r = T_INT;
        else if (s == "bool") r = T_BOOL;
        else if (s.size() == 4 && !s.compare(0, 3, "vec") && s[3] >= '2' && s[3] <= '4') r = mk(B_FLOAT, s[3] - '0');
        else if (s.size() == 5 && !s.compare(0, 4, "ivec") && s[4] >= '2' && s[4] <= '4') r = mk(B_INT, s[4] - '0');
        else if (s.size() == 5 && !s.compare(0, 4, "bvec") && s[4] >= '2' && s[4] <= '4') r = mk(B_BOOL, s[4] - '0');
        else if (!s.compare(0, 3, "mat") && (s.size() == 4 || s.size() == 6)) {
            int c = s[3] - '0', rr = s.size() == 6 ? s[5] - '0' : c;
            if (c < 2 || c > 4 || rr < 2 || rr > 4 || (s.size() == 6 && s[4] != 'x')) return false;
            r = mk(B_FLOAT, rr, c);
        } else if (s == "sampler1D") { r = mk(B_SAMPLER); r.samp = S_1D; }
        else if (s == "sampler2D") { r = mk(B_SAMPLER); r.samp = S_2D; }
        else if (s == "sampler3D") { r = mk(B_SAMPLER); r.samp = S_3D; }
        else if (s == "samplerCube") { r = mk(B_SAMPLER); r.samp = S_CUBE; }
        else if (s == "sampler2DRect") { r = mk(B_SAMPLER); r.samp = S_RECT; }
        else if (s == "sampler1DShadow" || s == "sampler2DShadow" || s == "sampler2DRectShadow")
            fail(t.line, "%s is not supported", s.c_str());
        else {
            auto it = structs.find(s);
            if (it == structs.end()) return false;
            r = mk(B_STRUCT);
            r.st = it->second.get();
        }
        if (out) *out = r;
        return true;
    }
    static bool precision_kw(const std::string &s) { return s == "highp" || s == "mediump" || s == "lowp"; }
    void skip_precision() { while (peek().k == K_IDENT && precision_kw(peek().s)) next(); }
    Type parse_type() {
        skip_precision();
        Type t;
        if (!type_at(0, &t)) fail(line(), "expected a type, got '%s'", peek().s.c_str());
        next();
        return t;
    }

    // --- constants
    int const_int(const E &e, int ln) {
        if (!e.cst || e.t.base != B_INT || !e.t.scalar()) fail(ln, "expected an integer constant expression");
        return (int)ival(e.cv);
    }
    int array_size() {
        int ln = line();
        E e = assign();
        int n = const_int(e, ln);
        if (n <= 0 || n > kMaxArray) fail(ln, "array size must be 1..%d", kMaxArray);
        expect("]");
        return n;
    }

    // --- variables
    VarP lookup(const std::string &n) {
        for (size_t i = scopes.size(); i-- > 0;) {
            auto it = scopes[i].find(n);
            if (it != scopes[i].end()) return it->second;
        }
        return builtin(n);
    }
    void declare(VarP v, int ln) {
        auto &sc = scopes.back();
        if (sc.count(v->name)) fail(ln, "'%s' redeclared", v->name.c_str());
        if (!v->name.compare(0, 3, "gl_")) fail(ln, "'%s': the gl_ prefix is reserved", v->name.c_str());
        v->decl_order = order++;
        sc[v->name] = v;
    }

    VarP mkbuiltin(const std::string &n, Type t, VarKind k, const std::string &msl, bool lv) {
        auto v = std::make_shared<Var>();
        v->name = n;
        v->t = t;
        v->kind = k;
        v->msl = msl;
        v->mname = msl;
        v->lvalue = lv;
        v->decl_order = order++;
        builtins[n] = v;
        return v;
    }
    VarP builtin(const std::string &n) {
        auto it = builtins.find(n);
        if (it != builtins.end()) return it->second;
        if (n.compare(0, 3, "gl_")) return nullptr;
        Type v4 = mk(B_FLOAT, 4), v4a = v4;
        v4a.array = 8;
        // Constants (Apple's GL values where they matter).
        static const std::pair<const char *, int> consts[] = {
            {"gl_MaxLights", 8}, {"gl_MaxClipPlanes", 8}, {"gl_MaxTextureUnits", 8}, {"gl_MaxTextureCoords", 8},
            {"gl_MaxVertexAttribs", 16}, {"gl_MaxVertexUniformComponents", 4096}, {"gl_MaxVaryingFloats", 64},
            {"gl_MaxVertexTextureImageUnits", 16}, {"gl_MaxCombinedTextureImageUnits", 32},
            {"gl_MaxTextureImageUnits", 16}, {"gl_MaxFragmentUniformComponents", 4096}, {"gl_MaxDrawBuffers", 8}};
        for (auto &c : consts)
            if (n == c.first) {
                VarP v = mkbuiltin(n, T_INT, V_CONST, std::to_string(c.second), false);
                v->has_cv = true;
                v->cv = c.second;
                return v;
            }
        // Matrix state, as uniforms the core fills (reported with their gl_ names).
        static const char *mats[] = {"gl_ModelViewMatrix", "gl_ProjectionMatrix", "gl_ModelViewProjectionMatrix", "gl_TextureMatrix"};
        for (const char *m : mats)
            for (const char *suf : {"", "Inverse", "Transpose", "InverseTranspose"}) {
                std::string full = std::string(m) + suf;
                if (n != full) continue;
                Type t = mk(B_FLOAT, 4, 4);
                if (!strcmp(m, "gl_TextureMatrix")) t.array = 8;
                VarP v = mkbuiltin(n, t, V_UNIFORM, "", false);
                v->mname = "rs_" + n;
                v->msl = "rs_u." + v->mname;
                globals.push_back(v);
                return v;
            }
        if (n == "gl_NormalMatrix" || n == "gl_NormalScale") {
            VarP v = mkbuiltin(n, n == "gl_NormalMatrix" ? mk(B_FLOAT, 3, 3) : T_FLOAT, V_UNIFORM, "", false);
            v->mname = "rs_" + n;
            v->msl = "rs_u." + v->mname;
            globals.push_back(v);
            return v;
        }
        if (vs()) {
            // GL_ARB_draw_instanced / GLSL 1.30+ names; Metal's ids include baseInstance / vertexStart,
            // like GL's (first is counted in gl_VertexID).
            if (n == "gl_InstanceIDARB" || n == "gl_InstanceID") { uses_instanceid = true; return mkbuiltin(n, T_INT, V_BUILTIN, "rs_InstanceID", false); }
            if (n == "gl_VertexID") { uses_vertexid = true; return mkbuiltin(n, T_INT, V_BUILTIN, "rs_VertexID", false); }
            if (n == "gl_Position") return mkbuiltin(n, v4, V_BUILTIN, "rs_out.rs_Position", true);
            if (n == "gl_PointSize") { uses_pointsize = true; return mkbuiltin(n, T_FLOAT, V_BUILTIN, "rs_out.rs_PointSize", true); }
            if (n == "gl_ClipVertex") { uses_clipvertex = true; return mkbuiltin(n, v4, V_BUILTIN, "rs_ClipVertex", true); }
            if (n == "gl_FrontColor") { ff_color = true; return mkbuiltin(n, v4, V_BUILTIN, "rs_FrontColor", true); }
            if (n == "gl_FrontSecondaryColor") { ff_seccolor = true; return mkbuiltin(n, v4, V_BUILTIN, "rs_FrontSecondaryColor", true); }
            if (n == "gl_BackColor" || n == "gl_BackSecondaryColor") {
                uses_backcolor = true;
                return mkbuiltin(n, v4, V_BUILTIN, n == "gl_BackColor" ? "rs_BackColor" : "rs_BackSecondaryColor", true);
            }
            if (n == "gl_TexCoord") { uses_texcoord = true; return mkbuiltin(n, v4a, V_BUILTIN, "rs_TexCoord", true); }
            if (n == "gl_FogFragCoord") { ff_fog = true; return mkbuiltin(n, T_FLOAT, V_BUILTIN, "rs_FogFragCoord", true); }
            static const std::pair<const char *, int> attrs[] = {{"gl_Vertex", 0}, {"gl_Normal", 2}, {"gl_Color", 3},
                                                                 {"gl_SecondaryColor", 4}, {"gl_FogCoord", 5}};
            for (auto &a : attrs)
                if (n == a.first) {
                    Type t = a.second == 2 ? mk(B_FLOAT, 3) : a.second == 5 ? T_FLOAT : v4;
                    VarP v = mkbuiltin(n, t, V_ATTRIB, "", false);
                    v->mname = "rs_" + n;
                    v->msl = "rs_in." + v->mname;
                    v->loc = a.second;
                    globals.push_back(v);
                    return v;
                }
            if (!n.compare(0, 16, "gl_MultiTexCoord") && n.size() == 17 && n[16] >= '0' && n[16] <= '7') {
                VarP v = mkbuiltin(n, v4, V_ATTRIB, "", false);
                v->mname = "rs_" + n;
                v->msl = "rs_in." + v->mname;
                v->loc = 8 + (n[16] - '0');
                globals.push_back(v);
                return v;
            }
        } else {
            if (n == "gl_FragCoord") { uses_fragcoord = true; return mkbuiltin(n, v4, V_BUILTIN, "rs_FragCoord", false); }
            if (n == "gl_FrontFacing") { uses_frontfacing = true; return mkbuiltin(n, T_BOOL, V_BUILTIN, "rs_FrontFacing", false); }
            if (n == "gl_PointCoord") { uses_pointcoord = true; return mkbuiltin(n, mk(B_FLOAT, 2), V_BUILTIN, "rs_PointCoord", false); }
            if (n == "gl_FragColor") { uses_fragcolor = true; return mkbuiltin(n, v4, V_BUILTIN, "rs_FragColor", true); }
            if (n == "gl_FragData") { uses_fragdata = true; return mkbuiltin(n, v4a, V_BUILTIN, "rs_FragData", true); }
            if (n == "gl_FragDepth") { writes_depth = true; uses_fragcoord = true; return mkbuiltin(n, T_FLOAT, V_BUILTIN, "rs_FragDepth", true); }
            if (n == "gl_Color") { ff_color = true; return mkbuiltin(n, v4, V_BUILTIN, "rs_in.rs_Color", false); }
            if (n == "gl_SecondaryColor") { ff_seccolor = true; return mkbuiltin(n, v4, V_BUILTIN, "rs_in.rs_SecondaryColor", false); }
            if (n == "gl_TexCoord") { uses_texcoord = true; return mkbuiltin(n, v4a, V_BUILTIN, "rs_TexCoord", false); }
            if (n == "gl_FogFragCoord") { ff_fog = true; return mkbuiltin(n, T_FLOAT, V_BUILTIN, "rs_in.rs_FogFragCoord", false); }
        }
        fail(line(), "built-in '%s' is not supported", n.c_str());
    }

    // --- conversions
    std::string conv(const E &e, const Type &to, int ln, const char *what = "value") {
        if (e.t == to) return e.s;
        if (e.t.base == B_INT && to.base == B_FLOAT && !e.t.array && !to.array && e.t.rows == to.rows && e.t.cols == to.cols) {
            if (e.lit) return fmt_num(e.cv, true);
            return msl_type(to) + "(" + e.s + ")";
        }
        fail(ln, "cannot convert %s from %s to %s", what, glsl_name(e.t).c_str(), glsl_name(to).c_str());
    }
    bool convertible(const Type &from, const Type &to) const {
        return from == to || (from.base == B_INT && to.base == B_FLOAT && !from.array && !to.array &&
                              from.rows == to.rows && from.cols == to.cols);
    }
    // To float of the same shape (genType arguments).
    std::string to_float(const E &e, int ln) {
        if (!e.t.num() || e.t.mat()) fail(ln, "expected a float scalar or vector, got %s", glsl_name(e.t).c_str());
        return conv(e, with_base(e.t, B_FLOAT), ln);
    }
    // Float of shape `shape` (broadcasting a scalar).
    std::string bcast(const E &e, const Type &shape, int ln) {
        std::string s = to_float(e, ln);
        if (e.t.scalar() && shape.vec()) return msl_type(shape) + "(" + s + ")";
        if (e.t.rows != shape.rows) fail(ln, "argument size mismatch (%s vs %s)", glsl_name(e.t).c_str(), glsl_name(shape).c_str());
        return s;
    }

    // --- expressions
    E expr() {
        E e = assign();
        while (is(",")) {
            next();
            E r = assign();
            E c;
            c.t = r.t;
            c.s = "(" + e.s + ", " + r.s + ")";
            e = c;
        }
        return e;
    }

    E assign() {
        int ln = line();
        Nest nest(this, ln);
        E lhs = cond();
        // Assignment operators: the punctuators ending in '=' except the comparisons.
        const Tok &t = peek();
        if (t.k == K_PUNCT && t.s.back() == '=' && t.s != "==" && t.s != "!=" && t.s != "<=" && t.s != ">=") {
            const std::string op_s = next().s;
            const char *op = op_s.c_str();
            E rhs = assign();
            if (!lhs.lvalue) fail(ln, "assignment to a read-only value");
            if (lhs.root) lhs.root->written = true;
            E r;
            r.t = lhs.t;
            if (!strcmp(op, "=")) {
                if (lhs.t.base == B_SAMPLER) fail(ln, "samplers cannot be assigned");
                if (lhs.t.is_array()) {  // C arrays don't assign; the value of the expression is void
                    if (rhs.t != lhs.t) fail(ln, "cannot assign %s to %s", glsl_name(rhs.t).c_str(), glsl_name(lhs.t).c_str());
                    r.t = T_VOID;
                    r.s = "rs_acopy(" + lhs.s + ", " + rhs.s + ")";
                    return r;
                }
                r.s = "(" + lhs.s + " = " + conv(rhs, lhs.t, ln) + ")";
                return r;
            }
            std::string bop(op, strlen(op) - 1);
            E res = binary(lhs, bop, rhs, ln);
            if (res.t != lhs.t) fail(ln, "'%s': result type %s does not match %s", op, glsl_name(res.t).c_str(), glsl_name(lhs.t).c_str());
            if (lhs.t.mat() && (rhs.t.scalar() ? bop != "*" : bop == "/"))
                r.s = "(" + lhs.s + " = " + res.s + ")";  // component-wise matrix ops: helpers, no MSL operator
            else
                r.s = "(" + lhs.s + " " + op + " " + conv(rhs, with_base(rhs.t, rhs.t.base == B_INT && lhs.t.base == B_FLOAT ? B_FLOAT : rhs.t.base), ln) + ")";
            return r;
        }
        return lhs;
    }

    E cond() {
        int ln = line();
        E c = binop_level(0);
        if (!is("?")) return c;
        next();
        if (c.t != T_BOOL) fail(ln, "?: condition must be bool");
        E a = expr();
        expect(":");
        E b = assign();
        Type t = unify(a.t, b.t, ln);
        E r;
        r.t = t;
        r.s = "(" + c.s + " ? " + conv(a, t, ln) + " : " + conv(b, t, ln) + ")";
        return r;
    }

    Type unify(const Type &a, const Type &b, int ln) {
        if (a == b) return a;
        if (convertible(a, b)) return b;
        if (convertible(b, a)) return a;
        fail(ln, "type mismatch: %s and %s", glsl_name(a).c_str(), glsl_name(b).c_str());
    }

    // Binary operator precedence (higher binds tighter), -1 for anything else.
    static int prec(const std::string &op) {
        char a = op[0], b = op.size() > 1 ? op[1] : 0;
        if (op.size() > 2) return -1;
        switch (a) {
        case '|': return b == '|' ? 1 : b ? -1 : 4;
        case '^': return b == '^' ? 2 : b ? -1 : 5;
        case '&': return b == '&' ? 3 : b ? -1 : 6;
        case '=': return b == '=' ? 7 : -1;
        case '!': return b == '=' ? 7 : -1;
        case '<': case '>': return !b || b == '=' ? 8 : b == a ? 9 : -1;
        case '+': case '-': return b ? -1 : 10;
        case '*': case '/': case '%': return b ? -1 : 11;
        default: return -1;
        }
    }

    E binop_level(int minp) {
        E lhs = unary();
        for (;;) {
            const Tok &t = peek();
            if (t.k != K_PUNCT) break;
            int p = prec(t.s);
            if (p < 0 || p < minp) break;
            std::string op = t.s;
            int ln = t.line;
            next();
            E rhs = binop_level(p + 1);
            lhs = binary(lhs, op, rhs, ln);
        }
        return lhs;
    }

    static double fold(double a, const std::string &op, double b, bool isint) {
        if (op == "+") return a + b;
        if (op == "-") return a - b;
        if (op == "*") return a * b;
        if (op == "/") return isint ? (b != 0 ? (double)(ival(a) / ival(b)) : 0) : a / b;
        return 0;
    }

    E binary(const E &a0, const std::string &op, const E &b0, int ln) {
        E a = a0, b = b0, r;
        if (a.t.is_array() || b.t.is_array() || a.t.base == B_SAMPLER || b.t.base == B_SAMPLER)
            fail(ln, "'%s' does not apply to %s", op.c_str(), glsl_name(a.t.is_array() ? a.t : b.t).c_str());
        if (op == "&&" || op == "||" || op == "^^") {
            if (a.t != T_BOOL || b.t != T_BOOL) fail(ln, "'%s' needs bool operands", op.c_str());
            r.t = T_BOOL;
            r.s = "(" + a.s + (op == "^^" ? " != " : " " + op + " ") + b.s + ")";
            return r;
        }
        if (op == "==" || op == "!=") {
            if (a.t.base == B_STRUCT || b.t.base == B_STRUCT) fail(ln, "struct comparison is not supported");
            Type t = unify(a.t, b.t, ln);
            std::string x = conv(a, t, ln), y = conv(b, t, ln);
            r.t = T_BOOL;
            if (t.mat()) r.s = std::string("(") + (op == "!=" ? "!" : "") + "rs_meq(" + x + ", " + y + "))";
            else if (t.vec()) r.s = (op == "==" ? "all(" : "any(") + x + " " + op + " " + y + ")";
            else r.s = "(" + x + " " + op + " " + y + ")";
            return r;
        }
        if (op == "<" || op == ">" || op == "<=" || op == ">=") {
            if (!a.t.scalar() || !b.t.scalar() || a.t.base == B_BOOL || b.t.base == B_BOOL)
                fail(ln, "'%s' needs scalar int or float operands", op.c_str());
            Type t = unify(a.t, b.t, ln);
            r.t = T_BOOL;
            r.s = "(" + conv(a, t, ln) + " " + op + " " + conv(b, t, ln) + ")";
            return r;
        }
        if (op == "%" || op == "&" || op == "|" || op == "^" || op == "<<" || op == ">>") {
            if (a.t.base != B_INT || b.t.base != B_INT) fail(ln, "'%s' needs int operands", op.c_str());
            r.t = a.t.scalar() ? b.t : a.t;
            r.s = "(" + a.s + " " + op + " " + b.s + ")";
            return r;
        }
        // + - * /
        if (!a.t.num() || !b.t.num()) fail(ln, "'%s' needs numeric operands, got %s and %s", op.c_str(), glsl_name(a.t).c_str(), glsl_name(b.t).c_str());
        Base base = a.t.base == B_FLOAT || b.t.base == B_FLOAT ? B_FLOAT : B_INT;
        std::string x = conv(a, with_base(a.t, base), ln), y = conv(b, with_base(b.t, base), ln);
        Type ta = with_base(a.t, base), tb = with_base(b.t, base), rt;
        if (ta.scalar() && tb.scalar()) rt = ta;
        else if (ta.scalar() || tb.scalar()) {
            rt = ta.scalar() ? tb : ta;
            if (rt.mat() && op != "*") {  // component-wise; MSL has no operator for these
                r.t = rt;
                r.s = std::string(op == "+" ? "rs_madd(" : op == "-" ? "rs_msub(" : "rs_mdiv(") + x + ", " + y + ")";
                return r;
            }
        } else if (ta.vec() && tb.vec()) {
            if (ta.rows != tb.rows) fail(ln, "vector size mismatch in '%s'", op.c_str());
            rt = ta;
        } else if (op == "*" && ta.mat() && tb.vec()) {
            if (ta.cols != tb.rows) fail(ln, "matrix * vector size mismatch");
            rt = mk(B_FLOAT, ta.rows);
        } else if (op == "*" && ta.vec() && tb.mat()) {
            if (ta.rows != tb.rows) fail(ln, "vector * matrix size mismatch");
            rt = mk(B_FLOAT, tb.cols);
        } else if (op == "*" && ta.mat() && tb.mat()) {
            if (ta.cols != tb.rows) fail(ln, "matrix * matrix size mismatch");
            rt = mk(B_FLOAT, ta.rows, tb.cols);
        } else if (ta.mat() && tb.mat() && (op == "+" || op == "-" || op == "/")) {
            if (ta != tb) fail(ln, "matrix size mismatch in '%s'", op.c_str());
            if (op == "/") {  // component-wise
                r.t = ta;
                r.s = "rs_mdiv(" + x + ", " + y + ")";
                return r;
            }
            rt = ta;
        } else {
            fail(ln, "'%s' is not supported between %s and %s", op.c_str(), glsl_name(ta).c_str(), glsl_name(tb).c_str());
        }
        r.t = rt;
        r.s = "(" + x + " " + op + " " + y + ")";
        if (a.cst && b.cst && rt.scalar()) {
            r.cst = true;
            r.cv = fold(a.cv, op, b.cv, base == B_INT);
        }
        return r;
    }

    E unary() {
        int ln = line();
        Nest nest(this, ln);
        if (is("+") || is("-") || is("!") || is("~")) {
            std::string op = next().s;
            E e = unary();
            if (op == "!") {
                if (e.t != T_BOOL) fail(ln, "'!' needs a bool");
                E r; r.t = T_BOOL; r.s = "(!" + e.s + ")";
                return r;
            }
            if (op == "~") {
                if (e.t.base != B_INT || e.t.array) fail(ln, "'~' needs an int");
                E r; r.t = e.t; r.s = "(~" + e.s + ")";
                return r;
            }
            if (!e.t.num()) fail(ln, "unary '%s' needs a number", op.c_str());
            if (op == "+") return e;
            E r = e;
            r.lvalue = false;
            r.var = r.root = nullptr;
            r.s = e.t.mat() ? "(" + e.s + " * -1.0f)" : "(-" + e.s + ")";  // MSL matrices have no unary minus
            if (e.cst) { r.cv = -e.cv; r.cst = true; }
            if (e.lit) { r.lit = true; r.s = "(-" + e.s + ")"; }
            return r;
        }
        if (is("++") || is("--")) {
            std::string op = next().s;
            E e = unary();
            if (!e.lvalue || !e.t.num()) fail(ln, "'%s' needs a numeric l-value", op.c_str());
            if (e.root) e.root->written = true;
            E r; r.t = e.t;
            r.s = e.t.scalar() ? "(" + op + e.s + ")"
                : e.t.mat() ? "(" + e.s + " = " + (op == "++" ? "rs_madd(" : "rs_msub(") + e.s + ", 1.0f))"
                            : "(" + e.s + (op == "++" ? " += " : " -= ") + "1)";
            return r;
        }
        return postfix(primary());
    }

    E postfix(E e) {
        for (;;) {
            int ln = line();
            if (accept("[")) {
                E idx = expr();
                expect("]");
                if (idx.t != T_INT) fail(ln, "index must be an int, got %s", glsl_name(idx.t).c_str());
                E r;
                r.lvalue = e.lvalue;
                r.root = e.root;
                if (e.t.is_array()) {
                    r.t = e.t.elem();
                    if (idx.cst && (idx.cv < 0 || idx.cv >= e.t.array)) fail(ln, "index %d out of range", (int)ival(idx.cv));
                    if (e.var && e.var->name == "gl_TexCoord") texcoord_mask |= idx.cst ? 1u << (int)ival(idx.cv) : 0xFFu;
                    if (e.var && e.var->name == "gl_FragData") fragdata_max = std::max(fragdata_max, idx.cst ? (int)ival(idx.cv) + 1 : 8);
                    if (r.t.base == B_SAMPLER) r.smp = e.smp + "[" + idx.s + "]";
                } else if (e.t.vec()) {
                    r.t = mk(e.t.base);
                } else if (e.t.mat()) {
                    r.t = mk(B_FLOAT, e.t.rows);
                } else {
                    fail(ln, "%s cannot be indexed", glsl_name(e.t).c_str());
                }
                r.s = e.s + "[" + idx.s + "]";
                e = r;
                continue;
            }
            if (accept(".")) {
                const Tok &f = next();
                if (f.k != K_IDENT) fail(ln, "expected a field name");
                if (e.t.is_array()) {
                    if (f.s != "length") fail(ln, "arrays only have length()");
                    expect("(");
                    expect(")");
                    E r; r.t = T_INT; r.cst = true; r.cv = e.t.array; r.lit = true; r.s = std::to_string(e.t.array);
                    e = r;
                    continue;
                }
                if (e.t.base == B_STRUCT) {
                    const Type *ft = nullptr;
                    for (auto &fd : e.t.st->fields)
                        if (fd.first == f.s) ft = &fd.second;
                    if (!ft) fail(ln, "%s has no field '%s'", e.t.st->name.c_str(), f.s.c_str());
                    E r; r.t = *ft; r.lvalue = e.lvalue; r.root = e.root; r.s = e.s + ".v_" + f.s;
                    e = r;
                    continue;
                }
                if (!e.t.vec() && !e.t.scalar()) fail(ln, "'.%s' on %s", f.s.c_str(), glsl_name(e.t).c_str());
                if (f.s.size() > 4) fail(ln, "bad swizzle '%s'", f.s.c_str());
                std::string sw;
                bool rep = false;
                for (char ch : f.s) {
                    const char *sets[] = {"xyzw", "rgba", "stpq"};
                    int k = -1;
                    for (const char *set : sets)
                        if (const char *q = strchr(set, ch)) k = (int)(q - set);
                    if (k < 0 || k >= e.t.rows) fail(ln, "bad swizzle '%s' on %s", f.s.c_str(), glsl_name(e.t).c_str());
                    char m = "xyzw"[k];
                    if (sw.find(m) != std::string::npos) rep = true;
                    sw += m;
                }
                E r;
                r.t = mk(e.t.base, (int)sw.size());
                r.lvalue = e.lvalue && !rep;
                r.root = e.root;
                r.s = e.t.scalar() ? msl_type(mk(e.t.base, 4)) + "(" + e.s + ")." + sw : e.s + "." + sw;
                if (e.t.scalar() && sw.size() == 1) r.s = e.s;
                e = r;
                continue;
            }
            if (is("++") || is("--")) {
                std::string op = next().s;
                if (!e.lvalue || !e.t.num()) fail(ln, "'%s' needs a numeric l-value", op.c_str());
                if (e.root) e.root->written = true;
                E r; r.t = e.t;
                if (e.t.scalar()) {
                    r.s = "(" + e.s + op + ")";
                } else {  // a statement expression: swizzles can't bind to a reference
                    std::string t = "rs_o" + std::to_string(uniq++);
                    std::string upd = e.t.mat() ? e.s + " = " + (op == "++" ? "rs_madd(" : "rs_msub(") + t + ", 1.0f)"
                                                : e.s + " = " + t + (op == "++" ? " + 1" : " - 1");
                    r.s = "({ " + msl_type(e.t) + " " + t + " = " + e.s + "; " + upd + "; " + t + "; })";
                }
                e = r;
                continue;
            }
            return e;
        }
    }

    E primary() {
        int ln = line();
        const Tok &t = peek();
        if (accept("(")) {
            E e = expr();
            expect(")");
            E r = e;
            if (!e.lit) r.s = "(" + e.s + ")";
            return r;
        }
        if (t.k == K_INT) {
            next();
            E e;
            e.t = T_INT;
            e.cst = e.lit = true;
            e.cv = (double)strtoll(t.s.c_str(), nullptr, 0);
            e.s = t.s;
            return e;
        }
        if (t.k == K_FLOAT) {
            next();
            E e;
            e.t = T_FLOAT;
            e.cst = true;
            std::string s = t.s;
            if (s.back() == 'f' || s.back() == 'F') s.pop_back();
            e.cv = strtod(s.c_str(), nullptr);
            e.s = s + "f";
            return e;
        }
        if (t.k != K_IDENT) fail(ln, "unexpected '%s'", t.s.c_str());
        if (t.s == "true" || t.s == "false") {
            next();
            E e; e.t = T_BOOL; e.s = t.s; e.cst = true; e.cv = t.s == "true";
            return e;
        }
        Type ct;
        if (type_at(0, &ct)) {
            next();
            if (is("[")) fail(ln, "array constructors are not supported");
            expect("(");
            std::vector<E> args = call_args();
            return construct(ct, args, ln);
        }
        std::string name = next().s;
        if (is("(")) {
            next();
            std::vector<E> args = call_args();
            return call(name, args, ln);
        }
        VarP v = lookup(name);
        if (!v) fail(ln, "'%s' is not declared", name.c_str());
        v->used = true;
        E e;
        e.t = v->t;
        e.s = v->msl;
        e.lvalue = v->lvalue;
        e.var = e.root = v.get();
        if (v->t.base == B_SAMPLER) e.smp = "rs_s_" + v->mname;
        if (v->has_cv) { e.cst = true; e.cv = v->cv; }
        if (v->kind == V_UNIFORM && v->t.base == B_BOOL && !v->t.is_array())
            e.s = msl_type(v->t) + "(" + v->msl + ")";  // stored as int
        if (v->kind == V_UNIFORM && v->t.base == B_BOOL && v->t.is_array())
            fail(ln, "bool uniform arrays are not supported");
        return e;
    }

    std::vector<E> call_args() {
        std::vector<E> a;
        if (accept(")")) return a;
        if (is("void") && is(")", 1)) { next(); next(); return a; }
        for (;;) {
            a.push_back(assign());
            if (accept(")")) return a;
            expect(",");
        }
    }

    // --- constructors
    // The components of `args` in order (a matrix argument column by column), cut into `groups`
    // groups of `size` components of type `base` (vector constructor: 1 group; matrix: its columns).
    // Components left over in the last argument are dropped, as GLSL does; an argument that isn't
    // reached is an error.
    std::vector<std::vector<std::string>> gather(const std::vector<E> &args, int groups, int size, Base base,
                                                 const Type &t, int ln) {
        std::vector<std::vector<std::string>> g(groups);
        int gi = 0, gneed = size;
        for (size_t i = 0; i < args.size(); i++) {
            const E &a = args[i];
            if (gi == groups) fail(ln, "too many arguments to %s", glsl_name(t).c_str());
            if (a.t.base == B_STRUCT) fail(ln, "%s constructor from %s is not supported", glsl_name(t).c_str(), glsl_name(a.t).c_str());
            int npieces = a.t.mat() ? a.t.cols : 1, pn = a.t.rows;
            Base pb = a.t.mat() ? B_FLOAT : a.t.base;
            for (int k = 0; k < npieces && gi < groups; k++) {
                std::string ps = a.t.mat() ? a.s + "[" + std::to_string(k) + "]" : a.s;
                for (int off = 0; off < pn && gi < groups;) {
                    int take = std::min(pn - off, gneed);
                    std::string x = off == 0 && take == pn ? ps : ps + "." + std::string("xyzw").substr(off, take);
                    if (pb != base) {
                        if (a.lit && base == B_FLOAT && pn == 1) x = fmt_num(a.cv, true);
                        else x = msl_type(mk(base, take)) + "(" + x + ")";
                    }
                    g[gi].push_back(x);
                    off += take;
                    if ((gneed -= take) == 0) gi++, gneed = size;
                }
            }
        }
        if (gi < groups) fail(ln, "not enough arguments to %s", glsl_name(t).c_str());
        return g;
    }

    E construct(const Type &t, std::vector<E> &args, int ln) {
        E r;
        r.t = t;
        if (args.empty()) fail(ln, "%s constructor needs arguments", glsl_name(t).c_str());
        for (auto &a : args)
            if (a.t.is_array() || a.t.base == B_SAMPLER || a.t.base == B_VOID) fail(ln, "bad constructor argument");
        if (t.base == B_STRUCT) {
            if (args.size() != t.st->fields.size()) fail(ln, "%s constructor needs %zu arguments", t.st->name.c_str(), t.st->fields.size());
            r.s = msl_type(t) + "{";
            for (size_t i = 0; i < args.size(); i++) r.s += (i ? ", " : "") + conv(args[i], t.st->fields[i].second, ln);
            r.s += "}";
            return r;
        }
        if (t.base == B_SAMPLER || t.base == B_VOID) fail(ln, "cannot construct %s", glsl_name(t).c_str());
        if (t.scalar()) {
            const E &a = args[0];
            if (a.t.base == B_STRUCT) fail(ln, "bad constructor argument");
            std::string s = a.t.scalar() ? a.s : a.t.mat() ? a.s + "[0][0]" : a.s + ".x";
            Base ab = a.t.base;
            if (ab == t.base) r.s = s;
            else if (a.lit && t.base == B_FLOAT) r.s = fmt_num(a.cv, true);
            else r.s = msl_type(t) + "(" + s + ")";
            if (a.cst && a.t.scalar()) {
                r.cst = true;
                r.cv = t.base == B_INT ? (double)ival(a.cv) : t.base == B_BOOL ? (a.cv != 0) : a.cv;
                if (t.base == B_INT && a.lit) r.lit = true, r.s = fmt_num(r.cv, false);
            }
            return r;
        }
        if (t.vec()) {
            std::string tn = msl_type(t);
            if (args.size() == 1 && args[0].t.scalar()) {
                const E &a = args[0];
                std::string s = a.t.base == t.base ? a.s : (a.lit && t.base == B_FLOAT) ? fmt_num(a.cv, true)
                                                                                       : msl_type(mk(t.base)) + "(" + a.s + ")";
                r.s = tn + "(" + s + ")";
                return r;
            }
            std::vector<std::vector<std::string>> g = gather(args, 1, t.rows, t.base, t, ln);
            std::vector<std::string> &parts = g[0];
            r.s = tn + "(";
            for (size_t i = 0; i < parts.size(); i++) r.s += (i ? ", " : "") + parts[i];
            r.s += ")";
            return r;
        }
        // matrices
        std::string tn = msl_type(t);
        std::string dims = std::to_string(t.cols) + ", " + std::to_string(t.rows);
        if (args.size() == 1 && args[0].t.scalar()) {
            r.s = "rs_mdiag<" + dims + ">(" + conv(args[0], T_FLOAT, ln) + ")";
            return r;
        }
        if (args.size() == 1 && args[0].t.mat()) {
            r.s = args[0].t == t ? args[0].s : "rs_mconv<" + dims + ">(" + args[0].s + ")";
            return r;
        }
        bool cols_ok = (int)args.size() == t.cols, scal_ok = (int)args.size() == t.cols * t.rows;
        for (auto &a : args) {
            if (!(a.t.vec() && a.t.rows == t.rows && a.t.base != B_BOOL)) cols_ok = false;
            if (!(a.t.scalar() && a.t.base != B_BOOL)) scal_ok = false;
        }
        if (!cols_ok && !scal_ok) {  // components in column order, from any mix of scalars and vectors
            std::vector<std::vector<std::string>> g = gather(args, t.cols, t.rows, B_FLOAT, t, ln);
            r.s = tn + "(";
            for (size_t c = 0; c < g.size(); c++) {
                r.s += (c ? ", " : "") + msl_type(mk(B_FLOAT, t.rows)) + "(";
                for (size_t i = 0; i < g[c].size(); i++) r.s += (i ? ", " : "") + g[c][i];
                r.s += ")";
            }
            r.s += ")";
            return r;
        }
        r.s = tn + "(";
        for (size_t i = 0; i < args.size(); i++)
            r.s += (i ? ", " : "") + conv(args[i], with_base(args[i].t, B_FLOAT), ln);
        r.s += ")";
        return r;
    }

    // --- calls
    E call(const std::string &name, std::vector<E> &args, int ln) {
        auto it = funcs.find(name);
        if (it != funcs.end()) {
            const Func *best = nullptr;
            int best_cost = 1 << 30;
            for (auto &f : it->second) {
                if (f->params.size() != args.size()) continue;
                int cost = 0;
                bool ok = true;
                for (size_t i = 0; i < args.size() && ok; i++) {
                    const Param &p = f->params[i];
                    if (args[i].t == p.t) continue;
                    if (p.qual == 0 && convertible(args[i].t, p.t)) cost++;
                    else ok = false;
                }
                if (ok && cost < best_cost) { best = f.get(); best_cost = cost; }
            }
            if (best) return user_call(*best, args, ln);
        }
        return builtin_call(name, args, ln);
    }

    // GLSL passes out / inout arguments by copy-in / copy-out; the MSL function takes references.
    // A whole local variable or parameter binds directly (nothing else can reach it during the
    // call) unless it is passed to two out / inout parameters; anything else (a swizzle or
    // component, which can't bind to a reference, or a global the callee may also access) goes
    // through a temporary in a statement expression, copied back left to right after the call.
    // (The l-value of such an argument is evaluated twice, before and after the call.) Arrays from
    // the uniform block (constant address space) are copied to a thread temporary for in
    // parameters too.
    E user_call(const Func &f, std::vector<E> &args, int ln) {
        std::string pre, post, call = "v_" + f.name + "(";
        for (size_t i = 0; i < args.size(); i++) {
            const Param &p = f.params[i];
            E &a = args[i];
            std::string s;
            if (p.t.base == B_SAMPLER) {
                s = a.s + ", " + a.smp;
            } else if (p.qual) {
                if (!a.lvalue) fail(ln, "argument %zu of %s must be an l-value", i + 1, f.name.c_str());
                if (a.root) a.root->written = true;
                bool direct = a.var && (a.var->kind == V_LOCAL || a.var->kind == V_PARAM);
                for (size_t j = 0; direct && j < args.size(); j++)
                    if (j != i && f.params[j].qual && args[j].root == a.var) direct = false;
                if (direct) {
                    s = a.s;
                } else {
                    s = "rs_a" + std::to_string(uniq++);
                    if (p.t.is_array()) {
                        pre += msl_type(p.t.elem()) + " " + s + "[" + std::to_string(p.t.array) + "]; rs_acopy(" + s + ", " + a.s + "); ";
                        post += "rs_acopy(" + a.s + ", " + s + "); ";
                    } else {
                        pre += msl_type(p.t) + " " + s + " = " + a.s + "; ";
                        post += a.s + " = " + s + "; ";
                    }
                }
            } else if (p.t.is_array() && a.root && a.root->kind == V_UNIFORM) {
                s = "rs_a" + std::to_string(uniq++);
                pre += msl_type(p.t.elem()) + " " + s + "[" + std::to_string(p.t.array) + "]; rs_acopy(" + s + ", " + a.s + "); ";
            } else {
                s = conv(a, p.t, ln);
            }
            call += (i ? ", " : "") + s;
        }
        call += ")";
        E r;
        r.t = f.ret;
        if (pre.empty() && post.empty()) {
            r.s = call;
        } else if (f.ret == T_VOID) {
            r.s = "({ " + pre + call + "; " + post + "})";
        } else {
            std::string t = "rs_r" + std::to_string(uniq++);
            r.s = "({ " + pre + msl_type(f.ret) + " " + t + " = " + call + "; " + post + t + "; })";
        }
        return r;
    }

    static bool genf(const Type &t) { return t.num() && !t.mat(); }

    E builtin_call(const std::string &n, std::vector<E> &a, int ln) {
        auto argc = [&](size_t k) {
            if (a.size() != k) fail(ln, "%s expects %zu arguments", n.c_str(), k);
            for (auto &x : a)
                if (x.t.base == B_SAMPLER && n.compare(0, 7, "texture")) fail(ln, "bad argument to %s", n.c_str());
        };
        auto gentype = [&](size_t i) {
            if (!genf(a[i].t)) fail(ln, "%s: argument %zu must be a float scalar or vector, got %s", n.c_str(), i + 1, glsl_name(a[i].t).c_str());
            return with_base(a[i].t, B_FLOAT);
        };
        E r;
        // genType f(genType)
        static const std::map<std::string, std::string> unary = {
            {"radians", "rs_radians"}, {"degrees", "rs_degrees"}, {"sin", "sin"}, {"cos", "cos"}, {"tan", "tan"},
            {"asin", "asin"}, {"acos", "acos"}, {"exp", "exp"}, {"log", "log"}, {"exp2", "exp2"}, {"log2", "log2"},
            {"sqrt", "sqrt"}, {"inversesqrt", "rsqrt"}, {"abs", "abs"}, {"sign", "sign"}, {"floor", "floor"},
            {"ceil", "ceil"}, {"fract", "fract"}, {"normalize", "normalize"}, {"dFdx", "dfdx"}, {"dFdy", "dfdy"},
            {"fwidth", "fwidth"}};
        auto u = unary.find(n);
        if (u != unary.end() || (n == "atan" && a.size() == 1)) {
            argc(1);
            r.t = gentype(0);
            r.s = (n == "atan" ? std::string("atan") : u->second) + "(" + to_float(a[0], ln) + ")";
            return r;
        }
        if (n == "atan" || n == "pow" || n == "reflect") {
            argc(2);
            Type t = gentype(0);
            r.t = t;
            r.s = std::string(n == "atan" ? "atan2" : n == "pow" ? "pow" : "reflect") + "(" + bcast(a[0], t, ln) + ", " + bcast(a[1], t, ln) + ")";
            if (gentype(1) != t) fail(ln, "%s: argument types differ", n.c_str());
            return r;
        }
        if (n == "mod" || n == "min" || n == "max") {
            argc(2);
            Type t = gentype(0);
            Type t1 = gentype(1);
            if (t1 != t && !t1.scalar()) fail(ln, "%s: argument types differ", n.c_str());
            r.t = t;
            r.s = (n == "mod" ? std::string("rs_mod") : n) + "(" + to_float(a[0], ln) + ", " + bcast(a[1], t, ln) + ")";
            return r;
        }
        if (n == "step") {
            argc(2);
            Type t = gentype(1);
            Type t0 = gentype(0);
            if (t0 != t && !t0.scalar()) fail(ln, "step: argument types differ");
            r.t = t;
            r.s = "step(" + bcast(a[0], t, ln) + ", " + to_float(a[1], ln) + ")";
            return r;
        }
        if (n == "clamp" || n == "mix" || n == "smoothstep" || n == "faceforward") {
            argc(3);
            Type t = n == "smoothstep" ? gentype(2) : gentype(0);
            for (int i = 0; i < 3; i++) {
                Type ti = gentype(i);
                bool scal_ok = (n == "clamp" && i > 0) || (n == "mix" && i == 2) || (n == "smoothstep" && i < 2);
                if (ti != t && !(scal_ok && ti.scalar())) fail(ln, "%s: argument types differ", n.c_str());
            }
            r.t = t;
            r.s = n + "(" + bcast(a[0], t, ln) + ", " + bcast(a[1], t, ln) + ", " + bcast(a[2], t, ln) + ")";
            return r;
        }
        if (n == "refract") {
            argc(3);
            Type t = gentype(0);
            if (gentype(1) != t || !gentype(2).scalar()) fail(ln, "refract: bad arguments");
            r.t = t;
            r.s = "refract(" + to_float(a[0], ln) + ", " + to_float(a[1], ln) + ", " + to_float(a[2], ln) + ")";
            return r;
        }
        if (n == "length" || n == "distance" || n == "dot") {
            argc(n == "length" ? 1 : 2);
            Type t = gentype(0);
            if (a.size() == 2 && gentype(1) != t) fail(ln, "%s: argument types differ", n.c_str());
            r.t = T_FLOAT;
            if (t.scalar()) {  // MSL has no scalar overloads
                r.s = n == "length" ? "abs(" + to_float(a[0], ln) + ")"
                    : n == "distance" ? "abs(" + to_float(a[0], ln) + " - " + to_float(a[1], ln) + ")"
                                      : "(" + to_float(a[0], ln) + " * " + to_float(a[1], ln) + ")";
                return r;
            }
            r.s = n + "(" + to_float(a[0], ln) + (a.size() == 2 ? ", " + to_float(a[1], ln) : "") + ")";
            return r;
        }
        if (n == "cross") {
            argc(2);
            if (gentype(0) != mk(B_FLOAT, 3) || gentype(1) != mk(B_FLOAT, 3)) fail(ln, "cross needs vec3 arguments");
            r.t = mk(B_FLOAT, 3);
            r.s = "cross(" + to_float(a[0], ln) + ", " + to_float(a[1], ln) + ")";
            return r;
        }
        if (n == "matrixCompMult") {
            argc(2);
            if (!a[0].t.mat() || a[0].t != a[1].t) fail(ln, "matrixCompMult needs two matrices of one type");
            r.t = a[0].t;
            r.s = "rs_mcm(" + a[0].s + ", " + a[1].s + ")";
            return r;
        }
        if (n == "transpose") {
            argc(1);
            if (!a[0].t.mat()) fail(ln, "transpose needs a matrix");
            r.t = mk(B_FLOAT, a[0].t.cols, a[0].t.rows);
            r.s = "transpose(" + a[0].s + ")";
            return r;
        }
        if (n == "outerProduct") {
            argc(2);
            Type c = gentype(0), rr = gentype(1);
            if (!c.vec() || !rr.vec()) fail(ln, "outerProduct needs vectors");
            r.t = mk(B_FLOAT, c.rows, rr.rows);
            r.s = "rs_outer(" + to_float(a[0], ln) + ", " + to_float(a[1], ln) + ")";
            return r;
        }
        if (n == "lessThan" || n == "lessThanEqual" || n == "greaterThan" || n == "greaterThanEqual" ||
            n == "equal" || n == "notEqual") {
            argc(2);
            bool eq = n == "equal" || n == "notEqual";
            if (!a[0].t.vec() || a[0].t != a[1].t || (!eq && a[0].t.base == B_BOOL))
                fail(ln, "%s needs two vectors of one type", n.c_str());
            static const std::map<std::string, std::string> ops = {{"lessThan", "<"}, {"lessThanEqual", "<="}, {"greaterThan", ">"},
                                                                   {"greaterThanEqual", ">="}, {"equal", "=="}, {"notEqual", "!="}};
            r.t = mk(B_BOOL, a[0].t.rows);
            r.s = "(" + a[0].s + " " + ops.at(n) + " " + a[1].s + ")";
            return r;
        }
        if (n == "any" || n == "all" || n == "not") {
            argc(1);
            if (!a[0].t.vec() || a[0].t.base != B_BOOL) fail(ln, "%s needs a bvec", n.c_str());
            r.t = n == "not" ? a[0].t : T_BOOL;
            r.s = n == "not" ? "(!" + a[0].s + ")" : n + "(" + a[0].s + ")";
            return r;
        }
        if (n == "ftransform") {
            if (!vs()) fail(ln, "ftransform is vertex-only");
            argc(0);
            E m = ident_expr("gl_ModelViewProjectionMatrix"), v = ident_expr("gl_Vertex");
            r.t = mk(B_FLOAT, 4);
            r.s = "(" + m.s + " * " + v.s + ")";
            return r;
        }
        if (!n.compare(0, 7, "texture") || !n.compare(0, 6, "shadow")) return texture_call(n, a, ln);
        fail(ln, "function '%s' is not declared", n.c_str());
    }

    E ident_expr(const char *n) {
        VarP v = lookup(n);
        v->used = true;
        E e;
        e.t = v->t;
        e.s = v->msl;
        return e;
    }

    E texture_call(const std::string &n, std::vector<E> &a, int ln) {
        struct TF { const char *name; int samp; int proj; bool lod; };
        static const TF tf[] = {
            {"texture1D", S_1D, 0, false}, {"texture1DProj", S_1D, 1, false}, {"texture1DLod", S_1D, 0, true},
            {"texture1DProjLod", S_1D, 1, true}, {"texture2D", S_2D, 0, false}, {"texture2DProj", S_2D, 1, false},
            {"texture2DLod", S_2D, 0, true}, {"texture2DProjLod", S_2D, 1, true}, {"texture3D", S_3D, 0, false},
            {"texture3DProj", S_3D, 1, false}, {"texture3DLod", S_3D, 0, true}, {"texture3DProjLod", S_3D, 1, true},
            {"textureCube", S_CUBE, 0, false}, {"textureCubeLod", S_CUBE, 0, true},
            {"texture2DRect", S_RECT, 0, false}, {"texture2DRectProj", S_RECT, 1, false}};
        const TF *f = nullptr;
        for (const TF &x : tf)
            if (n == x.name) f = &x;
        if (!f) fail(ln, "function '%s' is not supported", n.c_str());
        if (a.size() < 2 || a.size() > 3) fail(ln, "%s: wrong number of arguments", n.c_str());
        if (a[0].t.base != B_SAMPLER || a[0].t.is_array() || a[0].t.samp != f->samp)
            fail(ln, "%s: first argument must be a %s", n.c_str(), glsl_name([&] { Type t = mk(B_SAMPLER); t.samp = f->samp; return t; }()).c_str());
        if (f->lod && a.size() != 3) fail(ln, "%s needs a lod argument", n.c_str());
        if (f->lod && !vs()) fail(ln, "%s is vertex-only in GLSL 1.20", n.c_str());
        if (f->samp == S_RECT && a.size() != 2) fail(ln, "%s takes no bias", n.c_str());
        int dim = f->samp == S_1D ? 1 : f->samp == S_2D || f->samp == S_RECT ? 2 : 3;
        Type ct = gentype_arg(a[1], ln);
        std::string c = to_float(a[1], ln);
        if (f->proj) {
            bool ok = ct.rows == 4 || ct.rows == dim + 1;
            if (!ok) fail(ln, "%s: bad coordinate type %s", n.c_str(), glsl_name(ct).c_str());
            c = std::string(dim == 1 ? "rs_proj1(" : dim == 2 ? "rs_proj2(" : "rs_proj3(") + c + ")";
        } else if (ct.rows != dim) {
            fail(ln, "%s: bad coordinate type %s", n.c_str(), glsl_name(ct).c_str());
        }
        if (f->samp == S_1D) c = "float2(" + c + ", 0.5f)";
        if (f->samp == S_RECT) c = "(" + c + " / float2(" + a[0].s + ".get_width(), " + a[0].s + ".get_height()))";
        std::string opt;
        if (a.size() == 3) {
            if (!a[2].t.scalar() || a[2].t.base == B_BOOL) fail(ln, "%s: bias/lod must be a float", n.c_str());
            opt = std::string(f->lod ? ", level(" : ", bias(") + conv(a[2], T_FLOAT, ln) + ")";
        } else if (vs()) {
            opt = ", level(0.0f)";  // no derivatives in a vertex shader: base level
        }
        E r;
        r.t = mk(B_FLOAT, 4);
        r.s = a[0].s + ".sample(" + a[0].smp + ", " + c + opt + ")";
        return r;
    }
    Type gentype_arg(const E &e, int ln) {
        if (!genf(e.t)) fail(ln, "texture coordinate must be a float vector, got %s", glsl_name(e.t).c_str());
        return with_base(e.t, B_FLOAT);
    }

    // --- statements
    static std::string ind(int d) { return std::string(4 * d, ' '); }

    bool decl_start() {
        int o = 0;
        while (peek(o).k == K_IDENT && (peek(o).s == "const" || precision_kw(peek(o).s))) o++;
        if (o > 0) return true;
        Type t;
        return type_at(0, &t) && peek(1).k == K_IDENT;
    }

    std::string local_decl(int d, bool in_for = false) {
        int ln = line();
        bool is_const = false;
        for (;;) {
            if (accept("const")) is_const = true;
            else if (peek().k == K_IDENT && precision_kw(peek().s)) next();
            else break;
        }
        if (is("struct")) fail(ln, "local struct declarations are not supported");
        Type base = parse_type();
        if (base.base == B_VOID || base.base == B_SAMPLER) fail(ln, "bad local variable type %s", glsl_name(base).c_str());
        std::string out, ty = (is_const ? "const " : "") + msl_type(base);
        for (;;) {
            const Tok &id = next();
            if (id.k != K_IDENT) fail(ln, "expected a variable name");
            Type t = base;
            if (accept("[")) t.array = array_size();
            auto v = std::make_shared<Var>();
            v->name = id.s;
            v->mname = "v_" + id.s;
            // A name visible from an outer scope gets a fresh MSL name: in C++ the new variable is in
            // scope in its own initialiser (`float x = x + 1.0;` would read itself), in GLSL it isn't.
            for (auto &sc : scopes)
                if (sc.count(id.s)) { v->mname = "rs_l" + std::to_string(uniq++) + "_" + id.s; break; }  // rs_: no GLSL name maps there
            v->msl = v->mname;
            v->t = t;
            v->kind = V_LOCAL;
            v->lvalue = !is_const;
            std::string init;
            if (accept("=")) {
                if (t.is_array()) fail(ln, "array initialisers are not supported");
                E e = assign();
                init = conv(e, t, ln);
                if (is_const && e.cst && t.scalar()) { v->has_cv = true; v->cv = t.base == B_INT ? (double)ival(e.cv) : e.cv; }
            } else if (is_const) {
                fail(ln, "const '%s' needs an initialiser", id.s.c_str());
            }
            declare(v, ln);  // after the initialiser: `float x = x;` reads the outer x, as in GLSL
            std::string dcl = v->mname + (t.is_array() ? "[" + std::to_string(t.array) + "]" : "") + " = " + (init.empty() ? "{}" : init);
            // A for-init keeps its declarators in one declaration; elsewhere one per line.
            if (in_for) out += (out.empty() ? ty + " " : ", ") + dcl;
            else out += ind(d) + ty + " " + dcl + ";\n";
            if (accept(",")) continue;
            expect(";");
            break;
        }
        return out;
    }

    std::string statement(int d) {
        int ln = line();
        Nest nest(this, ln);
        if (accept("{")) {
            scopes.emplace_back();
            std::string s = ind(d) + "{\n";
            while (!is("}")) {
                if (peek().k == K_END) fail(ln, "unterminated block");
                s += statement(d + 1);
            }
            next();
            scopes.pop_back();
            return s + ind(d) + "}\n";
        }
        if (accept(";")) return "";
        if (accept("if")) {
            expect("(");
            E c = expr();
            expect(")");
            if (c.t != T_BOOL) fail(ln, "if condition must be bool");
            std::string s = ind(d) + "if (" + c.s + ")\n" + sub_statement(d);
            if (accept("else")) s += ind(d) + "else\n" + sub_statement(d);
            return s;
        }
        if (accept("for")) {
            expect("(");
            scopes.emplace_back();
            std::string init;
            if (accept(";")) init = "";
            else if (decl_start()) {
                init = local_decl(0, true);
            } else {
                init = expr().s;
                expect(";");
            }
            std::string c;
            if (!is(";")) {
                E ce = expr();
                if (ce.t != T_BOOL) fail(ln, "for condition must be bool");
                c = ce.s;
            }
            expect(";");
            std::string inc = is(")") ? "" : expr().s;
            expect(")");
            std::string body = sub_statement(d);
            scopes.pop_back();
            return ind(d) + "for (" + init + "; " + c + "; " + inc + ")\n" + body;
        }
        if (accept("while")) {
            expect("(");
            E c = expr();
            expect(")");
            if (c.t != T_BOOL) fail(ln, "while condition must be bool");
            return ind(d) + "while (" + c.s + ")\n" + sub_statement(d);
        }
        if (accept("do")) {
            std::string body = sub_statement(d);
            expect("while");
            expect("(");
            E c = expr();
            expect(")");
            expect(";");
            if (c.t != T_BOOL) fail(ln, "do-while condition must be bool");
            return ind(d) + "do\n" + body + ind(d) + "while (" + c.s + ");\n";
        }
        if (accept("return")) {
            if (accept(";")) {
                if (cur_func->ret != T_VOID) fail(ln, "return needs a value");
                return ind(d) + "return;\n";
            }
            E e = expr();
            expect(";");
            return ind(d) + "return " + conv(e, cur_func->ret, ln, "return value") + ";\n";
        }
        if (accept("break")) { expect(";"); return ind(d) + "break;\n"; }
        if (accept("continue")) { expect(";"); return ind(d) + "continue;\n"; }
        if (accept("discard")) {
            if (vs()) fail(ln, "discard in a vertex shader");
            expect(";");
            return ind(d) + "discard_fragment();\n";
        }
        if (decl_start()) return local_decl(d);
        E e = expr();
        expect(";");
        return ind(d) + e.s + ";\n";
    }
    // Body of if/for/while: its own scope even without braces.
    std::string sub_statement(int d) {
        if (is("{")) return statement(d);
        scopes.emplace_back();
        std::string s = statement(d + 1);
        scopes.pop_back();
        return s.empty() ? ind(d + 1) + ";\n" : s;
    }

    // --- top level
    void parse() {
        while (peek().k != K_END) top_level();
        auto m = funcs.find("main");
        if (m == funcs.end() || m->second.empty() || !m->second[0]->defined || !m->second[0]->params.empty() ||
            m->second[0]->ret != T_VOID)
            fail(0, "no void main()");
    }

    Type struct_def() {
        int ln = line();
        expect("struct");
        const Tok &nm = next();
        if (nm.k != K_IDENT) fail(ln, "expected a struct name");
        if (structs.count(nm.s)) fail(ln, "struct %s redefined", nm.s.c_str());
        auto sd = std::make_shared<StructDef>();
        sd->name = nm.s;
        expect("{");
        std::string body;
        while (!accept("}")) {
            Type t = parse_type();
            if (t.base == B_SAMPLER || t.base == B_VOID) fail(ln, "bad struct member type");
            for (;;) {
                const Tok &f = next();
                if (f.k != K_IDENT) fail(ln, "expected a member name");
                Type ft = t;
                if (accept("[")) ft.array = array_size();
                sd->fields.push_back({f.s, ft});
                body += "        " + msl_type(ft) + " v_" + f.s + (ft.array ? "[" + std::to_string(ft.array) + "]" : "") + ";\n";
                if (!accept(",")) break;
            }
            expect(";");
        }
        structs[nm.s] = sd;
        structs_text += "    struct v_" + nm.s + " {\n" + body + "    };\n";
        Type t = mk(B_STRUCT);
        t.st = sd.get();
        return t;
    }

    void top_level() {
        int ln = line();
        if (accept(";")) return;
        if (accept("precision")) {
            while (!accept(";")) { if (peek().k == K_END) fail(ln, "bad precision statement"); next(); }
            return;
        }
        if (is("invariant") && peek(1).k == K_IDENT && (is(";", 2) || is(",", 2))) {
            // `invariant gl_Position;` — no-op here
            while (!accept(";")) {
                if (peek().k == K_END) fail(ln, "bad invariant declaration");
                next();
            }
            return;
        }
        VarKind kind = V_GLOBAL;
        bool is_const = false;
        for (;;) {
            if (accept("uniform")) kind = V_UNIFORM;
            else if (accept("attribute")) kind = V_ATTRIB;
            else if (accept("varying")) kind = V_VARYING;
            else if (accept("const")) is_const = true;
            else if (accept("invariant") || accept("centroid")) {}
            else break;
        }
        Type base;
        if (is("struct")) {
            base = struct_def();
            if (accept(";")) return;
            if (kind == V_UNIFORM) fail(ln, "uniform structs are not supported");
        } else {
            base = parse_type();
        }
        if (peek().k == K_IDENT && is("(", 1)) {
            if (kind != V_GLOBAL || is_const) fail(ln, "bad function qualifiers");
            function(base);
            return;
        }
        if (kind == V_ATTRIB && !vs()) fail(ln, "attribute in a fragment shader");
        if (kind == V_ATTRIB && (base.base != B_FLOAT || base.mat())) fail(ln, "attributes of type %s are not supported", glsl_name(base).c_str());
        if (kind == V_VARYING && (base.base != B_FLOAT || base.mat())) fail(ln, "varyings of type %s are not supported", glsl_name(base).c_str());
        if (kind == V_UNIFORM && base.base == B_STRUCT) fail(ln, "uniform structs are not supported");
        if (base.base == B_SAMPLER && kind != V_UNIFORM) fail(ln, "samplers must be uniforms");
        if (base.base == B_VOID) fail(ln, "void variable");
        for (;;) {
            const Tok &id = next();
            if (id.k != K_IDENT) fail(ln, "expected a variable name");
            Type t = base;
            if (accept("[")) t.array = array_size();
            if ((kind == V_ATTRIB || kind == V_VARYING) && t.array) fail(ln, "%s arrays are not supported", kind == V_ATTRIB ? "attribute" : "varying");
            auto v = std::make_shared<Var>();
            v->name = id.s;
            v->mname = "v_" + id.s;
            v->t = t;
            v->kind = kind;
            v->lvalue = kind == V_GLOBAL ? !is_const : kind == V_VARYING ? vs() : false;
            switch (kind) {
            case V_UNIFORM: v->msl = t.base == B_SAMPLER ? v->mname : "rs_u." + v->mname; break;
            case V_ATTRIB: v->msl = "rs_in." + v->mname; break;
            case V_VARYING: v->msl = (vs() ? "rs_out." : "rs_in.") + v->mname; break;
            default: v->msl = v->mname; break;
            }
            std::string init;
            if (accept("=")) {
                E e = assign();
                if (kind != V_UNIFORM) init = conv(e, t, ln);  // uniform initialisers: ignored (GL default 0)
                if (is_const && e.cst && t.scalar()) { v->has_cv = true; v->cv = t.base == B_INT ? (double)ival(e.cv) : e.cv; }
            } else if (is_const) {
                fail(ln, "const '%s' needs an initialiser", id.s.c_str());
            }
            declare(v, ln);
            globals.push_back(v);
            if (kind == V_GLOBAL)
                members += "    " + std::string(is_const ? "const " : "") + msl_type(t) + " " + v->mname +
                           (t.array ? "[" + std::to_string(t.array) + "]" : "") + " = " + (init.empty() ? "{}" : init) + ";\n";
            if (accept(",")) continue;
            expect(";");
            break;
        }
    }

    // An `in` array parameter is a const reference; if the function writes it, the reference gets
    // another name and the body starts with a copy (GLSL passes by value).
    std::string param_text(const Param &p, const Var &v, std::string *prologue) {
        std::string nm = v.mname;
        if (p.t.base == B_SAMPLER) {
            if (p.t.array) fail(0, "sampler array parameters are not supported");
            return msl_type(p.t) + " " + nm + ", sampler rs_s_" + nm;
        }
        std::string ty = msl_type(p.t);
        if (p.t.array) {
            std::string dim = "[" + std::to_string(p.t.array) + "]";
            if (p.qual) return "thread " + ty + " (&" + nm + ")" + dim;
            if (!v.written) return "const thread " + ty + " (&" + nm + ")" + dim;
            *prologue += ind(2) + ty + " " + nm + dim + "; rs_acopy(" + nm + ", rs_p_" + nm + ");\n";
            return "const thread " + ty + " (&rs_p_" + nm + ")" + dim;
        }
        return p.qual ? "thread " + ty + " &" + nm : ty + " " + nm;
    }

    void function(const Type &ret) {
        int ln = line();
        std::string name = next().s;
        if (!name.compare(0, 3, "gl_")) fail(ln, "'%s': the gl_ prefix is reserved", name.c_str());
        expect("(");
        auto f = std::make_shared<Func>();
        f->name = name;
        f->ret = ret;
        if (ret.is_array()) fail(ln, "array return types are not supported");
        if (!(is("void") && is(")", 1)) && !is(")")) {
            for (;;) {
                Param p;
                for (;;) {
                    if (accept("const")) {}
                    else if (accept("in")) p.qual = 0;
                    else if (accept("out")) p.qual = 1;
                    else if (accept("inout")) p.qual = 2;
                    else if (peek().k == K_IDENT && precision_kw(peek().s)) next();
                    else break;
                }
                p.t = parse_type();
                if (peek().k == K_IDENT) p.name = next().s;
                else p.name = "rs_unnamed" + std::to_string(f->params.size());
                if (accept("[")) p.t.array = array_size();
                if (p.t.base == B_SAMPLER && p.qual) fail(ln, "out samplers");
                f->params.push_back(p);
                if (accept(")")) break;
                expect(",");
            }
        } else {
            accept("void");
            expect(")");
        }
        // Match an earlier prototype.
        std::shared_ptr<Func> existing;
        for (auto &g : funcs[name]) {
            if (g->params.size() != f->params.size()) continue;
            bool same = true;
            for (size_t i = 0; i < f->params.size(); i++)
                if (g->params[i].t != f->params[i].t) same = false;
            if (same) existing = g;
        }
        if (existing && existing->ret != ret) fail(ln, "%s redeclared with another return type", name.c_str());
        if (!existing) funcs[name].push_back(f), existing = f;
        if (accept(";")) return;
        if (existing->defined) fail(ln, "%s redefined", name.c_str());
        existing->defined = true;
        existing->params = f->params;  // names from the definition
        cur_func = existing.get();
        scopes.emplace_back();
        std::vector<VarP> pvars;
        for (size_t i = 0; i < f->params.size(); i++) {
            const Param &p = f->params[i];
            auto v = std::make_shared<Var>();
            pvars.push_back(v);
            v->name = p.name;
            v->mname = "v_" + p.name;
            v->msl = v->mname;
            v->t = p.t;
            v->kind = V_PARAM;
            v->lvalue = p.t.base != B_SAMPLER;
            declare(v, ln);
        }
        if (!is("{")) fail(ln, "expected a function body");
        std::string body = statement(1);
        scopes.pop_back();
        cur_func = nullptr;
        std::string sig = msl_type(ret) + " v_" + name + "(", prologue;
        for (size_t i = 0; i < f->params.size(); i++) sig += (i ? ", " : "") + param_text(f->params[i], *pvars[i], &prologue);
        sig += ")";
        if (!prologue.empty()) body.insert(body.find('\n') + 1, prologue);
        funcs_text += "    " + sig + "\n" + body;
    }
};

Nest::Nest(Stage *s, int ln) : d(&s->depth) {
    if (++*d > kMaxNest) fail(ln, "nesting too deep");
}

// --- Link: interface, reflection, entry points ------------------------------------------------

struct Uni {
    std::string name;
    Type t;
    int stages = 0;
    int off[3] = {-1, -1, -1};  // by stage bit
    int stride = 0;
    int slot = -1;  // samplers
    const Var *var[3] = {nullptr, nullptr, nullptr};
};

const char *kPrelude =
    "#include <metal_stdlib>\n"
    "using namespace metal;\n"
    "// IEEE semantics as GL (NaN / inf from the Cg shim's ARB rules, precise transcendentals); before\n"
    "// MSL 3.2 the core compiles with fastMathEnabled = NO instead.\n"
    "#if __METAL_VERSION__ >= 320\n"
    "#pragma METAL fp math_mode(safe)\n"
    "#endif\n"
    "\n"
    "// GLSL built-ins without a direct MSL equivalent.\n"
    "template<typename T, typename U> inline T rs_mod(T x, U y) { return x - y * floor(x / y); }\n"
    "template<typename T> inline T rs_radians(T x) { return x * 0.0174532925199432958f; }\n"
    "template<typename T> inline T rs_degrees(T x) { return x * 57.295779513082321f; }\n"
    "template<typename D, typename S, size_t N> inline void rs_acopy(thread D (&d)[N], const thread S (&s)[N]) {\n"
    "    for (size_t i = 0; i < N; i++) d[i] = s[i];\n"
    "}\n"
    "template<typename D, typename S, size_t N> inline void rs_acopy(thread D (&d)[N], const constant S (&s)[N]) {\n"
    "    for (size_t i = 0; i < N; i++) d[i] = s[i];\n"
    "}\n"
    "inline float rs_proj1(float2 c) { return c.x / c.y; }\n"
    "inline float rs_proj1(float4 c) { return c.x / c.w; }\n"
    "inline float2 rs_proj2(float3 c) { return c.xy / c.z; }\n"
    "inline float2 rs_proj2(float4 c) { return c.xy / c.w; }\n"
    "inline float3 rs_proj3(float4 c) { return c.xyz / c.w; }\n"
    "template<int C, int R> inline matrix<float, C, R> rs_mdiag(float s) {\n"
    "    matrix<float, C, R> m;\n"
    "    for (int i = 0; i < C; i++) for (int j = 0; j < R; j++) m[i][j] = i == j ? s : 0.0f;\n"
    "    return m;\n"
    "}\n"
    "template<int C, int R, int C2, int R2> inline matrix<float, C, R> rs_mconv(matrix<float, C2, R2> a) {\n"
    "    matrix<float, C, R> m;\n"
    "    for (int i = 0; i < C; i++) for (int j = 0; j < R; j++)\n"
    "        m[i][j] = i < C2 && j < R2 ? a[i][j] : (i == j ? 1.0f : 0.0f);\n"
    "    return m;\n"
    "}\n"
    "template<int C, int R> inline matrix<float, C, R> rs_mcm(matrix<float, C, R> a, matrix<float, C, R> b) {\n"
    "    for (int i = 0; i < C; i++) a[i] *= b[i];\n"
    "    return a;\n"
    "}\n"
    "// Component-wise matrix arithmetic (GLSL + - / with a scalar, / with a matrix).\n"
    "template<int C, int R> inline matrix<float, C, R> rs_madd(matrix<float, C, R> a, float s) { for (int i = 0; i < C; i++) a[i] += s; return a; }\n"
    "template<int C, int R> inline matrix<float, C, R> rs_madd(float s, matrix<float, C, R> a) { for (int i = 0; i < C; i++) a[i] = s + a[i]; return a; }\n"
    "template<int C, int R> inline matrix<float, C, R> rs_msub(matrix<float, C, R> a, float s) { for (int i = 0; i < C; i++) a[i] -= s; return a; }\n"
    "template<int C, int R> inline matrix<float, C, R> rs_msub(float s, matrix<float, C, R> a) { for (int i = 0; i < C; i++) a[i] = s - a[i]; return a; }\n"
    "template<int C, int R> inline matrix<float, C, R> rs_mdiv(matrix<float, C, R> a, float s) { for (int i = 0; i < C; i++) a[i] /= s; return a; }\n"
    "template<int C, int R> inline matrix<float, C, R> rs_mdiv(float s, matrix<float, C, R> a) { for (int i = 0; i < C; i++) a[i] = s / a[i]; return a; }\n"
    "template<int C, int R> inline matrix<float, C, R> rs_mdiv(matrix<float, C, R> a, matrix<float, C, R> b) { for (int i = 0; i < C; i++) a[i] /= b[i]; return a; }\n"
    "template<int C, int R> inline bool rs_meq(matrix<float, C, R> a, matrix<float, C, R> b) {\n"
    "    for (int i = 0; i < C; i++) if (any(a[i] != b[i])) return false;\n"
    "    return true;\n"
    "}\n"
    "template<int C, int R> inline matrix<float, C, R> rs_outer(vec<float, R> c, vec<float, C> r) {\n"
    "    matrix<float, C, R> m;\n"
    "    for (int i = 0; i < C; i++) m[i] = c * r[i];\n"
    "    return m;\n"
    "}\n"
    "\n"
    "// Alpha test (GL func - GL_NEVER; 8 = off), reference at fragment buffer(1).\n"
    "constant uint rs_alpha_func [[function_constant(0)]];\n"
    "constant uint rs_af = is_function_constant_defined(rs_alpha_func) ? rs_alpha_func : 8u;\n";

std::string decl_member(const Type &t, const std::string &name) {
    return msl_type(t) + " " + name + (t.array ? "[" + std::to_string(t.array) + "]" : "");
}

struct Linked {
    std::string msl;
    std::vector<rsmsl_attrib> attribs;
    std::vector<std::string> attrib_names;
    std::vector<Uni> unis;
    std::vector<std::pair<std::string, Uni>> samplers;  // element name, sampler
    int size[3] = {0, 0, 0};
};

Linked link(Stage &v, Stage &f, const rsmsl_attrib *bound, int n_bound, unsigned flags) {
    Linked L;
    // Attributes.
    std::vector<Var *> attrs;
    for (auto &g : v.globals)
        if (g->kind == V_ATTRIB && g->used) attrs.push_back(g.get());
    std::map<const Var *, int> loc;
    unsigned taken = 0;
    for (Var *a : attrs) {
        int l = a->loc;
        if (l < 0)
            for (int i = 0; i < n_bound; i++)
                if (bound[i].name && a->name == bound[i].name) l = bound[i].location;
        if (l >= 0) {
            if (l >= 16) fail(0, "attribute %s: location %d out of range", a->name.c_str(), l);
            if (taken & (1u << l)) fail(0, "attribute %s: location %d is already used", a->name.c_str(), l);
            taken |= 1u << l;
            loc[a] = l;
        }
    }
    for (Var *a : attrs) {
        if (loc.count(a)) continue;
        int l = 0;
        while (l < 16 && (taken & (1u << l))) l++;
        if (l == 16) fail(0, "too many attributes");
        taken |= 1u << l;
        loc[a] = l;
    }

    // Varyings.
    std::vector<Var *> vs_vary, fs_vary;
    for (auto &g : v.globals)
        if (g->kind == V_VARYING) vs_vary.push_back(g.get());
    for (auto &g : f.globals)
        if (g->kind == V_VARYING && g->used) {
            const Var *m = nullptr;
            for (Var *x : vs_vary)
                if (x->name == g->name) m = x;
            if (!m) fail(0, "varying %s is not declared in the vertex shader", g->name.c_str());
            if (m->t != g->t) fail(0, "varying %s has different types in the two stages", g->name.c_str());
            fs_vary.push_back(g.get());
        }
    unsigned tc_mask = f.uses_texcoord ? f.texcoord_mask : 0;

    // Uniforms and samplers, VS declaration order first.
    std::map<std::string, size_t> uidx;
    for (Stage *s : {&v, &f}) {
        int sb = s->stage;
        for (auto &g : s->globals) {
            if (g->kind != V_UNIFORM || !g->used) continue;
            auto it = uidx.find(g->name);
            if (it == uidx.end()) {
                uidx[g->name] = L.unis.size();
                Uni u;
                u.name = g->name;
                u.t = g->t;
                L.unis.push_back(u);
                it = uidx.find(g->name);
            }
            Uni &u = L.unis[it->second];
            if (u.t != g->t) fail(0, "uniform %s has different types in the two stages", g->name.c_str());
            u.stages |= sb;
            u.var[sb] = g.get();
        }
    }
    int slot = 0;
    for (Uni &u : L.unis) {
        if (u.t.base != B_SAMPLER) continue;
        u.slot = slot;
        int n = u.t.array ? u.t.array : 1;
        for (int i = 0; i < n; i++)
            L.samplers.push_back({u.t.array ? u.name + "[" + std::to_string(i) + "]" : u.name, u});
        slot += n;
    }
    if (slot > 16) fail(0, "too many samplers (%d, Metal allows 16 per stage)", slot);
    // Block layout per stage, in declaration order.
    std::string ustruct[3];
    for (Stage *s : {&v, &f}) {
        int sb = s->stage, off = 0, maxal = 16;
        std::vector<Var *> order;
        for (auto &g : s->globals)
            if (g->kind == V_UNIFORM && g->used && g->t.base != B_SAMPLER) order.push_back(g.get());
        for (Var *g : order) {
            Uni &u = L.unis[uidx[g->name]];
            Type st = g->t.base == B_BOOL ? with_base(g->t, B_INT) : g->t;
            int sz, al;
            msl_layout(st.elem(), &sz, &al);
            off = (off + al - 1) / al * al;
            u.off[sb] = off;
            u.stride = sz;
            off += sz * (st.array ? st.array : 1);
            if (off > kMaxBlock) fail(0, "%s uniforms take more than %d bytes", sb == RSMSL_STAGE_VS ? "vertex" : "fragment", kMaxBlock);
            ustruct[sb] += "    " + decl_member(st, g->mname) + ";  // offset " + std::to_string(u.off[sb]) + "\n";
        }
        L.size[sb] = (off + maxal - 1) / maxal * maxal;
        if (L.size[sb] > kMaxBlock) fail(0, "%s uniforms take %d bytes (at most %d)", sb == RSMSL_STAGE_VS ? "vertex" : "fragment", L.size[sb], kMaxBlock);
    }

    // --- MSL
    std::string m = kPrelude;
    auto tex_params = [&](int sb, std::string &params, std::vector<std::string> &init, std::string &members) {
        for (const Uni &u : L.unis) {
            if (u.t.base != B_SAMPLER || !(u.stages & sb)) continue;
            const Var *g = u.var[sb];
            std::string tt = msl_type(u.t.elem());
            std::string ty = u.t.array ? "array<" + tt + ", " + std::to_string(u.t.array) + ">" : tt;
            std::string sty = u.t.array ? "array<sampler, " + std::to_string(u.t.array) + ">" : "sampler";
            params += ",\n        " + ty + " rs_t_" + g->mname + " [[texture(" + std::to_string(u.slot) + ")]]";
            params += ",\n        " + sty + " rs_ss_" + g->mname + " [[sampler(" + std::to_string(u.slot) + ")]]";
            members += "    " + ty + " " + g->mname + ";\n    " + sty + " rs_s_" + g->mname + ";\n";
            init.push_back("rs_t_" + g->mname);
            init.push_back("rs_ss_" + g->mname);
        }
    };
    auto join = [](const std::vector<std::string> &v) {
        std::string s;
        for (size_t i = 0; i < v.size(); i++) s += (i ? ", " : "") + v[i];
        return s;
    };

    // Vertex stage.
    {
        std::string in_s, out_s;
        for (Var *a : attrs) in_s += "    " + decl_member(a->t, a->mname) + " [[attribute(" + std::to_string(loc[a]) + ")]];\n";
        out_s += "    float4 rs_Position [[position]];\n";
        if (v.uses_pointsize) out_s += "    float rs_PointSize [[point_size]];\n";
        for (Var *x : vs_vary) out_s += "    " + decl_member(x->t, x->mname) + " [[user(" + x->mname + ")]];\n";
        if (f.ff_color) out_s += "    float4 rs_Color [[user(rs_Color)]];\n";
        if (f.ff_seccolor) out_s += "    float4 rs_SecondaryColor [[user(rs_SecondaryColor)]];\n";
        if (f.ff_fog) out_s += "    float rs_FogFragCoord [[user(rs_FogFragCoord)]];\n";
        for (int i = 0; i < 8; i++)
            if (tc_mask & (1u << i)) out_s += "    float4 rs_TexCoord" + std::to_string(i) + " [[user(rs_TexCoord" + std::to_string(i) + ")]];\n";
        if (!attrs.empty()) m += "\nstruct rs_vs_in {\n" + in_s + "};\n";
        m += "\nstruct rs_vs_out {\n" + out_s + "};\n";
        if (L.size[RSMSL_STAGE_VS]) m += "\nstruct rs_vs_u {\n" + ustruct[RSMSL_STAGE_VS] + "};\n";
        std::string params, members;
        std::vector<std::string> init;
        if (!attrs.empty()) { params += "\n        rs_vs_in rs_in [[stage_in]]"; members += "    thread const rs_vs_in &rs_in;\n"; init.push_back("rs_in"); }
        if (L.size[RSMSL_STAGE_VS]) {
            params += std::string(params.empty() ? "\n" : ",\n") + "        constant rs_vs_u &rs_u [[buffer(0)]]";
            members += "    constant rs_vs_u &rs_u;\n";
            init.push_back("rs_u");
        }
        members += "    thread rs_vs_out &rs_out;\n";
        init.push_back("rs_out");
        if (v.uses_instanceid) {
            params += std::string(params.empty() ? "\n" : ",\n") + "        uint rs_instance_id [[instance_id]]";
            members += "    int rs_InstanceID;\n";
            init.push_back("int(rs_instance_id)");
        }
        if (v.uses_vertexid) {
            params += std::string(params.empty() ? "\n" : ",\n") + "        uint rs_vertex_id [[vertex_id]]";
            members += "    int rs_VertexID;\n";
            init.push_back("int(rs_vertex_id)");
        }
        std::string tp;
        tex_params(RSMSL_STAGE_VS, tp, init, members);
        if (params.empty() && !tp.empty()) tp = tp.substr(1);
        params += tp;
        if (v.uses_clipvertex) members += "    float4 rs_ClipVertex = {};\n";
        if (v.ff_color) members += "    float4 rs_FrontColor = {};\n";
        if (v.ff_seccolor) members += "    float4 rs_FrontSecondaryColor = {};\n";
        if (v.uses_backcolor) members += "    float4 rs_BackColor = {};\n    float4 rs_BackSecondaryColor = {};\n";
        if (v.uses_texcoord) members += "    float4 rs_TexCoord[8] = {};\n";
        if (v.ff_fog) members += "    float rs_FogFragCoord = {};\n";
        members += v.members;
        m += "\nstruct rs_vs_ctx {\n" + v.structs_text + members + v.funcs_text + "};\n";
        // The body is a plain function so a probe entry (msl_verify) can call it too.
        std::vector<std::string> pdecl, pplain, pname;
        for (size_t a = 0, b; a < params.size(); a = b + 2) {
            b = params.find(",\n", a);
            if (b == std::string::npos) b = params.size();
            std::string item = params.substr(a, b - a);
            size_t st = item.find_first_not_of(" \n");
            item = st == std::string::npos ? "" : item.substr(st);
            if (item.empty()) continue;
            size_t at = item.find(" [[");
            std::string plain = item.substr(0, at);
            pdecl.push_back(item);
            pplain.push_back(plain);
            pname.push_back(plain.substr(plain.find_last_of(" &") + 1));
        }
        std::string sig_plain, sig_decl, args;
        for (size_t i = 0; i < pdecl.size(); i++) {
            sig_plain += (i ? ", " : "") + pplain[i];
            sig_decl += std::string(i ? "," : "") + "\n        " + pdecl[i];
            args += (i ? ", " : "") + pname[i];
        }
        m += "\nstatic inline rs_vs_out rs_vs_main(" + sig_plain + ")\n{\n";
        m += "    rs_vs_out rs_out = {};\n";
        m += "    rs_vs_ctx rs_c{" + join(init) + "};\n";
        m += "    rs_c.v_main();\n";
        if (f.ff_color && v.ff_color) m += "    rs_out.rs_Color = saturate(rs_c.rs_FrontColor);\n";
        if (f.ff_seccolor && v.ff_seccolor) m += "    rs_out.rs_SecondaryColor = saturate(rs_c.rs_FrontSecondaryColor);\n";
        if (f.ff_fog && v.ff_fog) m += "    rs_out.rs_FogFragCoord = rs_c.rs_FogFragCoord;\n";
        for (int i = 0; i < 8; i++)
            if ((tc_mask & (1u << i)) && v.uses_texcoord)
                m += "    rs_out.rs_TexCoord" + std::to_string(i) + " = rs_c.rs_TexCoord[" + std::to_string(i) + "];\n";
        m += "    // GL clip space -> Metal: row 0 of the target is GL's bottom row; depth -1..1 -> 0..1\n";
        m += "    rs_out.rs_Position.y = -rs_out.rs_Position.y;\n";
        m += "    rs_out.rs_Position.z = (rs_out.rs_Position.z + rs_out.rs_Position.w) * 0.5f;\n";
        m += "    return rs_out;\n}\n";
        m += "\nvertex rs_vs_out rs_vs(" + sig_decl + ")\n{\n    return rs_vs_main(" + args + ");\n}\n";
        if (flags & RSMSL_XLAT_PROBE) {
            // Every output of every vertex as float4s at buffer(30): position, then the
            // varyings in rs_vs_out order (names listed in the comment for the harness).
            std::vector<std::pair<std::string, int>> outs;  // member, components
            outs.push_back({"rs_Position", 4});
            if (v.uses_pointsize) outs.push_back({"rs_PointSize", 1});
            for (Var *x : vs_vary) outs.push_back({x->mname, x->t.rows});
            if (f.ff_color) outs.push_back({"rs_Color", 4});
            if (f.ff_seccolor) outs.push_back({"rs_SecondaryColor", 4});
            if (f.ff_fog) outs.push_back({"rs_FogFragCoord", 1});
            for (int i = 0; i < 8; i++)
                if (tc_mask & (1u << i)) outs.push_back({"rs_TexCoord" + std::to_string(i), 4});
            m += "\n// rs_probe:";
            for (auto &o : outs) m += " " + o.first + ":" + std::to_string(o.second);
            m += "\nvertex void rs_vs_probe(" + sig_decl + (sig_decl.empty() ? "\n        " : ",\n        ") +
                 "device float4 *rs_probe [[buffer(30)]]" + (v.uses_vertexid ? std::string("") : ",\n        uint rs_vertex_id [[vertex_id]]") + ")\n{\n";
            m += "    rs_vs_out o = rs_vs_main(" + args + ");\n";
            for (size_t i = 0; i < outs.size(); i++) {
                int n = outs[i].second;
                std::string e = "o." + outs[i].first;
                std::string v4 = n == 4 ? e : n == 3 ? "float4(" + e + ", 0.0f)" : n == 2 ? "float4(" + e + ", 0.0f, 0.0f)" : "float4(" + e + ", 0.0f, 0.0f, 0.0f)";
                m += "    rs_probe[rs_vertex_id * " + std::to_string(outs.size()) + " + " + std::to_string(i) + "] = " + v4 + ";\n";
            }
            m += "}\n";
        }
    }

    // Fragment stage.
    {
        std::string in_s;
        for (Var *x : fs_vary) in_s += "    " + decl_member(x->t, x->mname) + " [[user(" + x->mname + ")]];\n";
        if (f.ff_color) in_s += "    float4 rs_Color [[user(rs_Color)]];\n";
        if (f.ff_seccolor) in_s += "    float4 rs_SecondaryColor [[user(rs_SecondaryColor)]];\n";
        if (f.ff_fog) in_s += "    float rs_FogFragCoord [[user(rs_FogFragCoord)]];\n";
        for (int i = 0; i < 8; i++)
            if (tc_mask & (1u << i)) in_s += "    float4 rs_TexCoord" + std::to_string(i) + " [[user(rs_TexCoord" + std::to_string(i) + ")]];\n";
        int ncolor = f.uses_fragdata ? std::max(1, f.fragdata_max) : 1;
        std::string out_s;
        for (int i = 0; i < ncolor; i++) out_s += "    float4 rs_c" + std::to_string(i) + " [[color(" + std::to_string(i) + ")]];\n";
        if (f.writes_depth) out_s += "    float rs_depth [[depth(any)]];\n";
        if (!in_s.empty()) m += "\nstruct rs_fs_in {\n" + in_s + "};\n";
        m += "\nstruct rs_fs_out {\n" + out_s + "};\n";
        if (L.size[RSMSL_STAGE_FS]) m += "\nstruct rs_fs_u {\n" + ustruct[RSMSL_STAGE_FS] + "};\n";
        std::vector<std::string> params, init;
        std::string members;
        if (!in_s.empty()) { params.push_back("rs_fs_in rs_in [[stage_in]]"); members += "    thread const rs_fs_in &rs_in;\n"; init.push_back("rs_in"); }
        if (L.size[RSMSL_STAGE_FS]) { params.push_back("constant rs_fs_u &rs_u [[buffer(0)]]"); members += "    constant rs_fs_u &rs_u;\n"; init.push_back("rs_u"); }
        params.push_back("constant float &rs_alpha_ref [[buffer(1)]]");
        if (f.uses_fragcoord) { params.push_back("float4 rs_FragCoord [[position]]"); members += "    float4 rs_FragCoord;\n"; init.push_back("rs_FragCoord"); }
        if (f.uses_frontfacing) { params.push_back("bool rs_FrontFacing [[front_facing]]"); members += "    bool rs_FrontFacing;\n"; init.push_back("rs_FrontFacing"); }
        if (f.uses_pointcoord) {  // Metal's origin is the top of the target, i.e. GL's bottom (y flip): GL's upper left is 1 - y
            params.push_back("float2 rs_point_coord [[point_coord]]");
            members += "    float2 rs_PointCoord;\n";
            init.push_back("float2(rs_point_coord.x, 1.0f - rs_point_coord.y)");
        }
        std::string tp;
        tex_params(RSMSL_STAGE_FS, tp, init, members);
        members += "    float4 rs_FragColor = {};\n";
        if (f.uses_fragdata) members += "    float4 rs_FragData[8] = {};\n";
        if (f.writes_depth) members += "    float rs_FragDepth = rs_FragCoord.z;\n";
        if (f.uses_texcoord) {
            members += "    float4 rs_TexCoord[8] = {";
            for (int i = 0; i < 8; i++)
                members += std::string(i ? ", " : "") + ((tc_mask & (1u << i)) ? "rs_in.rs_TexCoord" + std::to_string(i) : "float4(0.0f)");
            members += "};\n";
        }
        members += f.members;
        m += "\nstruct rs_fs_ctx {\n" + f.structs_text + members + f.funcs_text + "};\n";
        std::string ps;
        for (size_t i = 0; i < params.size(); i++) ps += (i ? ",\n        " : "\n        ") + params[i];
        ps += tp;
        m += "\nfragment rs_fs_out rs_fs(" + ps + ")\n{\n";
        m += "    rs_fs_ctx rs_c{" + join(init) + "};\n";
        m += "    rs_c.v_main();\n";
        m += "    rs_fs_out rs_o;\n";
        if (f.uses_fragdata)
            for (int i = 0; i < ncolor; i++) m += "    rs_o.rs_c" + std::to_string(i) + " = rs_c.rs_FragData[" + std::to_string(i) + "];\n";
        else
            m += "    rs_o.rs_c0 = rs_c.rs_FragColor;\n";
        if (f.writes_depth) m += "    rs_o.rs_depth = rs_c.rs_FragDepth;\n";
        m += "    if (rs_af != 8u) {  // alpha test on the clamped colour, as GL does for fixed-point targets\n"
             "        float a = saturate(rs_o.rs_c0.a), r = saturate(rs_alpha_ref);\n"
             "        bool pass = rs_af == 1u ? a < r : rs_af == 2u ? a == r : rs_af == 3u ? a <= r : rs_af == 4u ? a > r\n"
             "                  : rs_af == 5u ? a != r : rs_af == 6u ? a >= r : rs_af == 7u;\n"
             "        if (!pass) discard_fragment();\n"
             "    }\n";
        m += "    return rs_o;\n}\n";
    }
    L.msl = m;

    for (Var *a : attrs) {
        rsmsl_attrib r;
        r.name = nullptr;
        r.location = loc[a];
        r.type = gl_type_enum(a->t);
        L.attribs.push_back(r);
        L.attrib_names.push_back(a->name);
    }
    return L;
}

char *dupstr(const std::string &s) {
    char *p = (char *)malloc(s.size() + 1);
    if (p) memcpy(p, s.c_str(), s.size() + 1);
    return p;
}

}  // namespace

extern "C" rsmsl_program *rsmsl_translate_direct(const char *vs_glsl, const char *fs_glsl,
                                                 const rsmsl_attrib *bound, int n_bound, char **error) {
    return rsmsl_translate_ex(vs_glsl, fs_glsl, bound, n_bound, 0, error);
}

extern "C" rsmsl_program *rsmsl_translate_ex(const char *vs_glsl, const char *fs_glsl, const rsmsl_attrib *bound,
                                             int n_bound, unsigned flags, char **error) {
    if (error) *error = nullptr;
    std::string stage_name = "vertex";
    try {
        Stage v(RSMSL_STAGE_VS, vs_glsl);
        v.parse();
        stage_name = "fragment";
        Stage f(RSMSL_STAGE_FS, fs_glsl);
        f.parse();
        stage_name = "link";
        Linked L = link(v, f, bound, n_bound, flags);

        rsmsl_program *p = (rsmsl_program *)calloc(1, sizeof *p);
        p->msl = dupstr(L.msl);
        p->n_attribs = (int)L.attribs.size();
        p->attribs = (rsmsl_attrib *)calloc(p->n_attribs ? p->n_attribs : 1, sizeof(rsmsl_attrib));
        for (int i = 0; i < p->n_attribs; i++) {
            p->attribs[i] = L.attribs[i];
            p->attribs[i].name = dupstr(L.attrib_names[i]);
        }
        p->n_uniforms = (int)L.unis.size();
        p->uniforms = (rsmsl_uniform *)calloc(p->n_uniforms ? p->n_uniforms : 1, sizeof(rsmsl_uniform));
        for (int i = 0; i < p->n_uniforms; i++) {
            const Uni &u = L.unis[i];
            rsmsl_uniform &r = p->uniforms[i];
            r.name = dupstr(u.name);
            r.type = gl_type_enum(u.t);
            r.array_size = u.t.array ? u.t.array : 1;
            r.stages = u.stages;
            r.vs_offset = u.off[RSMSL_STAGE_VS];
            r.fs_offset = u.off[RSMSL_STAGE_FS];
            r.stride = u.t.base == B_SAMPLER ? 0 : u.stride;
        }
        p->n_samplers = (int)L.samplers.size();
        p->samplers = (rsmsl_sampler *)calloc(p->n_samplers ? p->n_samplers : 1, sizeof(rsmsl_sampler));
        int k = 0;
        for (auto &s : L.samplers) {
            rsmsl_sampler &r = p->samplers[k];
            r.name = dupstr(s.first);
            r.type = gl_type_enum(s.second.t.elem());
            r.stages = s.second.stages;
            // element i of an array: slot + i (the entries of one array are consecutive)
            int elem = 0;
            size_t br = s.first.find('[');
            if (br != std::string::npos) elem = atoi(s.first.c_str() + br + 1);
            r.index = s.second.slot + elem;
            k++;
        }
        p->vs_uniform_size = L.size[RSMSL_STAGE_VS];
        p->fs_uniform_size = L.size[RSMSL_STAGE_FS];
        return p;
    } catch (const XErr &e) {
        if (error) *error = dupstr(stage_name + " shader: " + e.msg);
        return nullptr;
    } catch (const std::exception &e) {  // out of memory (or a translator bug): fail, don't abort the game
        if (error) *error = dupstr(stage_name + " shader: internal error: " + e.what());
        return nullptr;
    }
}

extern "C" void rsmsl_free(rsmsl_program *p) {
    if (!p) return;
    free(p->msl);
    for (int i = 0; i < p->n_attribs; i++) free((void *)p->attribs[i].name);
    for (int i = 0; i < p->n_uniforms; i++) free((void *)p->uniforms[i].name);
    for (int i = 0; i < p->n_samplers; i++) free((void *)p->samplers[i].name);
    free(p->attribs);
    free(p->uniforms);
    free(p->samplers);
    free(p);
}

extern "C" uint64_t rsmsl_key(const char *vs, const char *fs) {
    // FNV-1a 64 over both sources, separated by a 0 byte.
    uint64_t h = 0xcbf29ce484222325ull;
    for (const char *s : {vs ? vs : "", fs ? fs : ""}) {
        for (const unsigned char *p = (const unsigned char *)s; *p; p++) h = (h ^ *p) * 0x100000001b3ull;
        h = (h ^ 0) * 0x100000001b3ull;
    }
    return h;
}
