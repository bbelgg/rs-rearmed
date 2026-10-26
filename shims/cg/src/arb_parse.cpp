// Parser for Cg-compiled ARB programs: the "#var/#const" header Cg's runtime uses
// to build the parameter table, and the ARB_vertex_program/ARB_fragment_program
// body the GLSL emitter translates.
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>

#include "arb_program.h"

namespace rscg {
namespace {

constexpr CGresource RES_C = 2178;
constexpr CGresource RES_TEXUNIT0 = 2048;
constexpr CGresource RES_ATTR0 = 2113;
constexpr CGresource RES_TEXCOORD0 = 3220;
constexpr CGresource RES_HPOS = 2243;
constexpr CGresource RES_COL0 = 2245;
constexpr CGresource RES_COL1 = 2246;
constexpr CGresource RES_POSITION0 = 2437;
constexpr CGresource RES_NORMAL0 = 3092;
constexpr CGresource RES_COLOR0 = 2757;
constexpr CGresource RES_BLENDWEIGHT0 = 3028;
// TEX0..TEX7 are not contiguous in Cg's enum.
constexpr CGresource RES_TEX[8] = {2179, 2180, 2181, 2192, 2193, 2194, 2195, 2196};

constexpr unsigned GL_TEXTURE_1D_ = 0x0DE0, GL_TEXTURE_2D_ = 0x0DE1, GL_TEXTURE_3D_ = 0x806F,
                   GL_TEXTURE_RECT_ = 0x84F5, GL_TEXTURE_CUBE_ = 0x8513;

std::string trim(const std::string &s) {
    size_t a = 0, b = s.size();
    while (a < b && isspace((unsigned char)s[a])) a++;
    while (b > a && isspace((unsigned char)s[b - 1])) b--;
    return s.substr(a, b - a);
}

bool starts_with(const std::string &s, const char *p) { return s.compare(0, strlen(p), p) == 0; }

// Split "a : b : c" on " : " boundaries. Fields may be empty.
std::vector<std::string> split_colon(const std::string &s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == ':') { out.push_back(trim(cur)); cur.clear(); }
        else cur += c;
    }
    out.push_back(trim(cur));
    return out;
}

CGtype type_from_string(const std::string &t, int *rows, int *cols) {
    static const std::map<std::string, int> types = {
        {"float", CG_FLOAT}, {"float1", CG_FLOAT1}, {"float2", CG_FLOAT2}, {"float3", CG_FLOAT3},
        {"float4", CG_FLOAT4}, {"float2x2", CG_FLOAT2x2}, {"float3x3", CG_FLOAT3x3},
        {"float3x4", CG_FLOAT3x4}, {"float4x2", CG_FLOAT4x2}, {"float4x3", CG_FLOAT4x3},
        {"float4x4", CG_FLOAT4x4}, {"half", CG_HALF}, {"half2", 1026}, {"half3", 1027},
        {"half4", 1028}, {"half4x4", CG_HALF4x4}, {"int", CG_INT}, {"bool", CG_BOOL},
        {"sampler1D", CG_SAMPLER1D}, {"sampler2D", CG_SAMPLER2D}, {"sampler3D", CG_SAMPLER3D},
        {"samplerRECT", CG_SAMPLERRECT}, {"samplerCUBE", CG_SAMPLERCUBE}, {"sampler", CG_SAMPLER},
        {"texture", CG_TEXTURE}, {"fixed", CG_FIXED}, {"fixed4", 1073},
    };
    *rows = 1;
    *cols = 4;
    size_t x = t.find('x');
    if (x != std::string::npos && x > 0 && isdigit((unsigned char)t[x - 1])) {
        *rows = t[x - 1] - '0';
        *cols = t[x + 1] - '0';
    } else if (!t.empty() && isdigit((unsigned char)t.back())) {
        *cols = t.back() - '0';
    } else if (t == "float" || t == "half" || t == "int" || t == "bool" || t == "fixed") {
        *cols = 1;
    }
    auto it = types.find(t);
    return it == types.end() ? CG_UNKNOWN_TYPE : (CGtype)it->second;
}

// Number following `prefix` in `s` (e.g. "TEX3" with prefix "TEX" -> 3), or -1.
int suffix_number(const std::string &s, const char *prefix) {
    size_t n = strlen(prefix);
    if (s.compare(0, n, prefix) || s.size() == n) return -1;
    for (size_t i = n; i < s.size(); i++)
        if (!isdigit((unsigned char)s[i])) return -1;
    return atoi(s.c_str() + n);
}

// Resource field of a varying: Cg's resource enum and index.
bool varying_resource(const std::string &r, CGresource *res, unsigned long *index) {
    int n;
    *index = 0;
    if ((n = suffix_number(r, "TEXCOORD")) >= 0) { *res = RES_TEXCOORD0 + n; *index = n; return true; }
    if ((n = suffix_number(r, "TEX")) >= 0 && n < 8) { *res = RES_TEX[n]; *index = n; return true; }
    if ((n = suffix_number(r, "ATTR")) >= 0) { *res = RES_ATTR0 + n; *index = n; return true; }
    if ((n = suffix_number(r, "COLOR")) >= 0) { *res = RES_COLOR0 + n; *index = n; return true; }
    if (r == "POSITION") { *res = RES_POSITION0; return true; }
    if (r == "NORMAL") { *res = RES_NORMAL0; return true; }
    if (r == "WEIGHT") { *res = RES_BLENDWEIGHT0; return true; }
    if (r == "HPOS") { *res = RES_HPOS; return true; }
    if (r == "COL" || r == "COL0") { *res = RES_COL0; return true; }
    if (r == "COL1") { *res = RES_COL1; *index = 1; return true; }
    return false;
}

void parse_var_line(const std::string &line, ArbProgram *p) {
    std::vector<std::string> f = split_colon(line.substr(4));
    if (f.size() < 5) return;
    std::string decl = f[0];
    size_t sp = decl.find(' ');
    if (sp == std::string::npos) return;
    std::string type_s = decl.substr(0, sp), name = trim(decl.substr(sp + 1));
    const std::string &sem = f[1], &res = f[2];
    int pindex = atoi(f[3].c_str());
    if (res == "$kill" || type_s[0] == '<') return;   // KIL bookkeeping, not a parameter

    ParamInfo q;
    q.name = name;
    q.type = type_from_string(type_s, &q.rows, &q.cols);
    q.used = f[4] == "1";
    q.name_space = pindex >= 0 ? CG_PROGRAM : CG_GLOBAL;
    switch (q.type) {
    case CG_SAMPLER1D: q.tex_target = GL_TEXTURE_1D_; break;
    case CG_SAMPLER2D: case CG_SAMPLER: q.tex_target = GL_TEXTURE_2D_; break;
    case CG_SAMPLER3D: q.tex_target = GL_TEXTURE_3D_; break;
    case CG_SAMPLERRECT: q.tex_target = GL_TEXTURE_RECT_; break;
    case CG_SAMPLERCUBE: q.tex_target = GL_TEXTURE_CUBE_; break;
    default: break;
    }

    bool vin = starts_with(sem, "$vin."), vout = starts_with(sem, "$vout.");
    if (vin || vout) {
        q.semantic = sem.substr(vin ? 5 : 6);
        q.direction = vin ? CG_IN : CG_OUT;
        q.variability = CG_VARYING;
        if (!varying_resource(res, &q.resource, &q.resource_index)) {
            // NVIDIA's runtime reports varyings that are unbound or bound to
            // resources it has no enum for (FOGC, DEPR) as uniform.
            q.resource = CG_UNDEFINED_RESOURCE;
            q.variability = CG_UNIFORM;
        }
        if (vin && p->stage == Stage::Vertex && q.resource != CG_UNDEFINED_RESOURCE) {
            std::string base;
            int idx = -1, n;
            if ((n = suffix_number(res, "TEXCOORD")) >= 0) { base = "vertex.texcoord"; idx = n; }
            else if ((n = suffix_number(res, "ATTR")) >= 0) { base = "vertex.attrib"; idx = n; }
            else if ((n = suffix_number(res, "COLOR")) >= 0) base = n ? "vertex.color.secondary" : "vertex.color";
            else if (res == "COL0" || res == "COL") base = "vertex.color";
            else if (res == "COL1") base = "vertex.color.secondary";
            else if (res == "POSITION") base = "vertex.position";
            else if (res == "NORMAL") base = "vertex.normal";
            else if (res == "WEIGHT") base = "vertex.weight";
            q.attrib = attrib_location_for(base, idx);
        }
    } else {
        q.semantic = sem;   // reported upper-cased, like every Cg semantic
        for (char &ch : q.semantic) ch = (char)toupper((unsigned char)ch);
        q.variability = CG_UNIFORM;
        q.direction = CG_IN;
        int n;
        if (starts_with(res, "c[")) {
            q.resource = RES_C;
            q.reg_base = atoi(res.c_str() + 2);
            q.resource_index = q.reg_base;
            size_t comma = res.find(',');
            q.reg_count = comma == std::string::npos ? 1 : atoi(res.c_str() + comma + 1);
        } else if (starts_with(res, "texunit ")) {
            q.texunit = atoi(res.c_str() + 8);
            q.resource = RES_TEXUNIT0 + q.texunit;
            q.resource_index = q.texunit;
        } else if ((n = suffix_number(res, "texunit")) >= 0) {
            q.texunit = n;
            q.resource = RES_TEXUNIT0 + n;
            q.resource_index = n;
        }
    }
    p->params.push_back(q);
}

void parse_const_line(const std::string &line, std::vector<ParamInfo> *consts) {
    // "#const c[2] = 0.0009765625 0.5"
    size_t lb = line.find("c[");
    if (lb == std::string::npos) return;
    ParamInfo q;
    q.reg_base = atoi(line.c_str() + lb + 2);
    q.name = "$C_" + std::to_string(q.reg_base) + "$";
    q.type = CG_FLOAT4;
    q.variability = CG_CONSTANT;
    q.resource = RES_C;
    q.resource_index = q.reg_base;
    q.reg_count = 1;
    q.used = true;
    consts->push_back(q);
}

// --- body tokenizer ---------------------------------------------------------

struct Lexer {
    const std::string &s;
    size_t i = 0;
    explicit Lexer(const std::string &str) : s(str) {}
    void ws() { while (i < s.size() && isspace((unsigned char)s[i])) i++; }
    bool eof() { ws(); return i >= s.size(); }
    char peek() { ws(); return i < s.size() ? s[i] : 0; }
    bool accept(char c) { if (peek() == c) { i++; return true; } return false; }
    std::string ident() {   // identifier, no dots
        ws();
        size_t a = i;
        while (i < s.size() && (isalnum((unsigned char)s[i]) || s[i] == '_' || s[i] == '$')) i++;
        return s.substr(a, i - a);
    }
    bool number(float *out) {
        ws();
        const char *b = s.c_str() + i;
        char *e;
        float v = strtof(b, &e);
        if (e == b) return false;
        i += e - b;
        *out = v;
        return true;
    }
    int integer() {   // not strtof: "0..8" must stop before the range dots
        ws();
        const char *b = s.c_str() + i;
        char *e;
        long v = strtol(b, &e, 10);
        i += e - b;
        return (int)v;
    }
};

const std::set<std::string> &known_bindings() {
    static const std::set<std::string> b = {
        "vertex.position", "vertex.normal", "vertex.color", "vertex.color.primary",
        "vertex.color.secondary", "vertex.fogcoord", "vertex.weight", "vertex.texcoord",
        "vertex.attrib", "fragment.color", "fragment.color.primary", "fragment.color.secondary",
        "fragment.fogcoord", "fragment.position", "fragment.texcoord", "result.position",
        "result.color", "result.color.primary", "result.color.secondary", "result.fogcoord",
        "result.pointsize", "result.depth", "result.texcoord", "program.local", "program.env",
    };
    return b;
}

bool is_swizzle(const std::string &w) {
    if (w.empty() || w.size() > 4) return false;
    for (char c : w)
        if (!strchr("xyzwrgba", c)) return false;
    return true;
}

std::string normalize_swizzle(std::string w) {
    for (char &c : w) {
        const char *rgba = "rgba", *p = strchr(rgba, c);
        if (p) c = "xyzw"[p - rgba];
    }
    return w;
}

bool parse_operand(Lexer &lx, Operand *op, std::string *err) {
    op->negate = lx.accept('-');
    if (lx.accept('+')) {}
    if (lx.peek() == '{') {
        lx.accept('{');
        int n = 0;
        float v;
        while (n < 4 && lx.number(&v)) {
            op->imm[n++] = v;
            if (!lx.accept(',')) break;
        }
        if (!lx.accept('}')) { *err = "bad inline constant"; return false; }
        // Missing components: y,z = 0, w = 1 (ARB_vertex_program 2.14.3.2), but a
        // single scalar in braces is replicated by the instruction's swizzle.
        for (int k = n; k < 4; k++) op->imm[k] = k == 3 ? 1.0f : 0.0f;
        op->has_imm = true;
        op->base = "{";
    } else {
        std::string path = lx.ident();
        if (path.empty()) { *err = "expected operand"; return false; }
        // Extend with dotted segments while the result is still a binding prefix.
        while (lx.i < lx.s.size() && lx.s[lx.i] == '.') {
            size_t save = lx.i;
            lx.i++;
            std::string seg = lx.ident();
            std::string cand = path + "." + seg;
            bool prefix = false;
            for (const std::string &k : known_bindings())
                if (k == cand || starts_with(k, (cand + ".").c_str())) prefix = true;
            if (prefix) { path = cand; continue; }
            lx.i = save;
            break;
        }
        op->base = path;
    }
    if (op->base != "{" && lx.peek() == '[') {
        lx.accept('[');
        lx.ws();
        if (isalpha((unsigned char)lx.peek())) {
            op->relative = true;
            op->rel_reg = lx.ident();
            if (lx.accept('.')) {
                std::string c = lx.ident();
                op->rel_comp = c.empty() ? 'x' : normalize_swizzle(c)[0];
            }
            op->index = 0;
            if (lx.accept('+')) op->index = lx.integer();
            else if (lx.accept('-')) op->index = -lx.integer();
        } else {
            op->index = lx.integer();
        }
        if (!lx.accept(']')) { *err = "expected ]"; return false; }
    }
    if (lx.i < lx.s.size() && lx.s[lx.i] == '.') {
        lx.i++;
        std::string w = lx.ident();
        if (!is_swizzle(w)) { *err = "bad swizzle ." + w; return false; }
        op->swizzle = normalize_swizzle(w);
    }
    return true;
}

// "program.local[0..3]" or "{1, 0}" or "program.local[5]" element lists.
bool parse_param_elems(Lexer &lx, ParamDecl *d, std::string *err) {
    auto one = [&](void) -> bool {
        if (lx.peek() == '{') {
            lx.accept('{');
            ParamBinding b;
            int n = 0;
            float v;
            while (n < 4 && lx.number(&v)) {
                b.value[n++] = v;
                if (!lx.accept(',')) break;
            }
            for (int k = n; k < 4; k++) b.value[k] = k == 3 ? 1.0f : 0.0f;
            if (!lx.accept('}')) { *err = "bad PARAM constant"; return false; }
            d->elems.push_back(b);
            return true;
        }
        float scalar;
        size_t save = lx.i;
        if (lx.peek() == '-' || isdigit((unsigned char)lx.peek()) || lx.peek() == '.') {
            if (lx.number(&scalar)) {   // scalar constant: replicated
                ParamBinding b;
                for (float &c : b.value) c = scalar;
                d->elems.push_back(b);
                return true;
            }
            lx.i = save;
        }
        std::string a = lx.ident();
        if (a != "program" || !lx.accept('.')) { *err = "unsupported PARAM binding " + a; return false; }
        std::string kind = lx.ident();
        if (kind != "local") { *err = "unsupported PARAM binding program." + kind; return false; }
        if (!lx.accept('[')) { *err = "expected ["; return false; }
        int lo = lx.integer(), hi = lo;
        lx.ws();
        if (lx.s.compare(lx.i, 2, "..") == 0) {
            lx.i += 2;
            hi = lx.integer();
        }
        if (!lx.accept(']')) { *err = "expected ]"; return false; }
        for (int k = lo; k <= hi; k++) {
            ParamBinding b;
            b.is_local = true;
            b.local_index = k;
            d->elems.push_back(b);
        }
        return true;
    };
    if (lx.accept('{')) {
        do {
            if (!one()) return false;
        } while (lx.accept(','));
        if (!lx.accept('}')) { *err = "expected } in PARAM"; return false; }
        return true;
    }
    return one();
}

bool parse_statement(const std::string &stmt, int line, ArbProgram *p, std::string *err) {
    Lexer lx(stmt);
    std::string kw = lx.ident();
    if (kw.empty()) return true;
    if (kw == "OPTION") return true;   // precision hints only
    if (kw == "TEMP" || kw == "ADDRESS") {
        do {
            std::string n = lx.ident();
            if (n.empty()) { *err = "empty " + kw; return false; }
            (kw == "TEMP" ? p->temps : p->addresses).push_back(n);
        } while (lx.accept(','));
        return true;
    }
    if (kw == "PARAM") {
        ParamDecl d;
        d.name = lx.ident();
        if (lx.accept('[')) {
            d.is_array = true;
            lx.ws();
            if (lx.peek() != ']') lx.integer();
            lx.accept(']');
        }
        if (!lx.accept('=')) { *err = "expected = in PARAM"; return false; }
        if (!parse_param_elems(lx, &d, err)) return false;
        for (const ParamBinding &b : d.elems)
            if (b.is_local && b.local_index > p->max_local) p->max_local = b.local_index;
        p->param_decls.push_back(d);
        return true;
    }
    if (kw == "ATTRIB" || kw == "OUTPUT" || kw == "ALIAS") {
        *err = kw + " declarations are not used by Cg output and not supported";
        return false;
    }

    Instruction ins;
    ins.line = line;
    ins.op = kw;
    if (ins.op.size() > 4 && ins.op.compare(ins.op.size() - 4, 4, "_SAT") == 0) {
        ins.saturate = true;
        ins.op.resize(ins.op.size() - 4);
    }
    static const std::set<std::string> ops = {
        "ABS", "ADD", "ARL", "CMP", "COS", "DP3", "DP4", "DPH", "DST", "EX2", "EXP", "FLR",
        "FRC", "KIL", "LG2", "LIT", "LOG", "LRP", "MAD", "MAX", "MIN", "MOV", "MUL", "POW",
        "RCP", "RSQ", "SCS", "SGE", "SIN", "SLT", "SUB", "SWZ", "TEX", "TXB", "TXP", "XPD",
    };
    if (!ops.count(ins.op)) { *err = "unsupported instruction " + kw; return false; }

    if (ins.op != "KIL") {
        if (!parse_operand(lx, &ins.dst, err)) return false;
        if (!lx.accept(',')) { *err = "expected , after destination"; return false; }
    }
    if (ins.op == "SWZ") {
        Operand s;
        if (!parse_operand(lx, &s, err)) return false;
        ins.src.push_back(s);
        while (lx.accept(',')) {
            std::string c;
            if (lx.accept('-')) c = "-";
            lx.ws();
            c += lx.s[lx.i++];
            ins.swz_ext.push_back(normalize_swizzle(c));
        }
        if (ins.swz_ext.size() != 4) { *err = "SWZ needs 4 components"; return false; }
        return p->code.push_back(ins), true;
    }
    do {
        lx.ws();
        if (starts_with(stmt.substr(lx.i), "texture")) {
            lx.ident();
            ins.tex_unit = 0;
            if (lx.accept('[')) {
                ins.tex_unit = lx.integer();
                lx.accept(']');
            }
            if (!lx.accept(',')) { *err = "expected texture target"; return false; }
            ins.tex_target = lx.ident();
            p->tex_units_used |= 1u << ins.tex_unit;
            break;
        }
        Operand s;
        if (!parse_operand(lx, &s, err)) return false;
        ins.src.push_back(s);
    } while (lx.accept(','));
    if (!lx.eof()) { *err = "trailing text in instruction"; return false; }
    p->code.push_back(ins);
    return true;
}

}  // namespace

int attrib_location_for(const std::string &base, int index) {
    // NVIDIA's conventional/generic aliasing, which Cg's arbvp1 profile assumes.
    if (base == "vertex.position") return 0;
    if (base == "vertex.weight") return 1;
    if (base == "vertex.normal") return 2;
    if (base == "vertex.color" || base == "vertex.color.primary") return 3;
    if (base == "vertex.color.secondary") return 4;
    if (base == "vertex.fogcoord") return 5;
    if (base == "vertex.texcoord") return 8 + (index < 0 ? 0 : index);
    if (base == "vertex.attrib") return index;
    return -1;
}

bool parse_arb_program(const std::string &raw, ArbProgram *p, std::string *error) {
    std::string text;
    text.reserve(raw.size());
    for (char c : raw)
        if (c != '\r') text += c;

    size_t start = text.find("!!ARB");
    if (start == std::string::npos) { *error = "not an ARB program object"; return false; }
    if (starts_with(text.substr(start), "!!ARBfp1.0")) p->stage = Stage::Fragment;
    else if (starts_with(text.substr(start), "!!ARBvp1.0")) p->stage = Stage::Vertex;
    else { *error = "unsupported ARB program version"; return false; }

    // Header pass: parameter table.
    std::vector<ParamInfo> consts;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t eol = text.find('\n', pos);
        if (eol == std::string::npos) eol = text.size();
        std::string line = trim(text.substr(pos, eol - pos));
        pos = eol + 1;
        if (starts_with(line, "#var ")) parse_var_line(line, p);
        else if (starts_with(line, "#const ")) parse_const_line(line, &consts);
        else if (starts_with(line, "#program ")) p->entry = trim(line.substr(9));
    }
    // NVIDIA's order: entry-function parameters, then the constant registers, then
    // globals (#var order within each group).
    std::vector<ParamInfo> ordered;
    for (const ParamInfo &q : p->params)
        if (q.name_space == CG_PROGRAM) ordered.push_back(q);
    ordered.insert(ordered.end(), consts.begin(), consts.end());
    for (const ParamInfo &q : p->params)
        if (q.name_space == CG_GLOBAL) ordered.push_back(q);
    p->params.swap(ordered);

    // Body pass: statements between the header line and END.
    pos = text.find('\n', start);
    int line_no = 1;
    std::string cur;
    int cur_line = 0;
    bool ended = false;
    std::vector<std::pair<std::string, int>> stmts;
    while (pos != std::string::npos && pos < text.size() && !ended) {
        size_t eol = text.find('\n', pos + 1);
        std::string line = text.substr(pos + 1, (eol == std::string::npos ? text.size() : eol) - pos - 1);
        pos = eol;
        line_no++;
        size_t hash = line.find('#');
        if (hash != std::string::npos) line.resize(hash);
        for (char c : line) {
            if (cur.empty() && isspace((unsigned char)c)) continue;
            if (cur.empty()) cur_line = line_no;
            if (c == ';') {
                stmts.emplace_back(trim(cur), cur_line);
                cur.clear();
            } else {
                cur += c;
            }
        }
        cur += ' ';
        if (trim(cur) == "END") ended = true;
    }
    if (!ended) { *error = "missing END"; return false; }

    for (auto &st : stmts)
        if (!parse_statement(st.first, st.second, p, error)) {
            *error = "line " + std::to_string(st.second) + ": " + *error + " in `" + st.first + "`";
            return false;
        }

    // Record what the body touches.
    std::set<std::string> params_named;
    for (const ParamDecl &d : p->param_decls) params_named.insert(d.name);
    for (const Instruction &ins : p->code) {
        auto visit = [&](const Operand &o, bool is_dst) {
            if (o.base == "program.local" && o.index > p->max_local) p->max_local = o.index;
            if (p->stage == Stage::Vertex && !is_dst && starts_with(o.base, "vertex.")) {
                int loc = attrib_location_for(o.base, o.index);
                if (loc >= 0) p->attribs_read |= 1u << loc;
            }
            const std::string &b = o.base;
            unsigned bit = 0;
            if (b == "result.texcoord" || b == "fragment.texcoord") bit = 1u << (VAR_TEX0 + (o.index < 0 ? 0 : o.index));
            else if (b == "result.color.secondary" || b == "fragment.color.secondary") bit = 1u << VAR_COL1;
            else if (starts_with(b, "result.color") || starts_with(b, "fragment.color")) bit = 1u << VAR_COL0;
            else if (b == "result.fogcoord" || b == "fragment.fogcoord") bit = 1u << VAR_FOG;
            if (p->stage == Stage::Vertex ? is_dst : !is_dst) p->outputs |= bit;
            if (b == "result.depth") p->writes_depth = true;
        };
        if (ins.op != "KIL") visit(ins.dst, true);
        for (const Operand &s : ins.src) visit(s, false);
        if (ins.op == "KIL") p->kills = true;
    }

    p->uniform_slots = p->max_local + 1;
    for (const ParamDecl &d : p->param_decls) {
        if (!d.is_array) continue;
        bool direct = true;
        for (size_t k = 0; k < d.elems.size(); k++)
            if (d.elems[k].is_local && d.elems[k].local_index != (int)k) direct = false;
        if (!direct) continue;
        p->direct_params.insert(d.name);
        for (size_t k = 0; k < d.elems.size(); k++)
            if (!d.elems[k].is_local) {
                std::array<float, 4> v;
                std::copy(d.elems[k].value, d.elems[k].value + 4, v.begin());
                p->uniform_consts.emplace_back((int)k, v);
            }
        if ((int)d.elems.size() > p->uniform_slots) p->uniform_slots = (int)d.elems.size();
    }
    // Two direct arrays would both claim the low slots for their constants.
    std::set<int> seen;
    for (auto &c : p->uniform_consts)
        if (!seen.insert(c.first).second) {
            *error = "overlapping PARAM arrays";
            return false;
        }
    return true;
}

}  // namespace rscg
