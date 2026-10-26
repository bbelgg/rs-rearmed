// ARB_vertex_program / ARB_fragment_program -> GLSL.
//
// Every ARB register becomes a vec4 local; each instruction computes a vec4 into
// `rs_t` and then assigns the written components, so source/destination aliasing
// and write masks behave as in ARB. Instruction semantics follow the ARB specs,
// and, where the specs leave results undefined, with what Apple's GL does when it
// runs the same ARB object (tests/diff_render.c): RSQ uses |x|, LG2 and POW of a
// negative base give NaN, POW of 0 is 0, unwritten outputs are (0,0,0,1).
//
// Interface:
//   vertex   attribute vec4 rs_attr<N>     generic location N (see attrib_location_for)
//            uniform vec4 rs_vp_local[K]   program.local[0..K-1]
//   fragment uniform vec4 rs_fp_local[K]
//            uniform sampler<T> rs_tex<N>_<T>   bound to texture image unit N
//   varyings rs_v_tex<N>, rs_v_col0, rs_v_col1, rs_v_fog
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <sstream>

#include "arb_program.h"

namespace rscg {
namespace {

const char *varying_name(unsigned v) {
    static const char *names[VAR_COUNT] = {"rs_v_tex0", "rs_v_tex1", "rs_v_tex2", "rs_v_tex3",
                                           "rs_v_tex4", "rs_v_tex5", "rs_v_tex6", "rs_v_tex7",
                                           "rs_v_col0", "rs_v_col1", "rs_v_fog"};
    return names[v];
}

std::string fmt_float(float f) {
    if (std::isnan(f)) return "(0.0/0.0)";
    if (std::isinf(f)) return f > 0 ? "(1.0/0.0)" : "(-1.0/0.0)";
    char b[48];
    snprintf(b, sizeof b, "%.9g", f);
    std::string s = b;
    if (s.find_first_of(".e") == std::string::npos) s += ".0";
    else if (s.find('.') == std::string::npos) {   // "1e-05" -> "1.0e-05"
        size_t e = s.find('e');
        s.insert(e, ".0");
    }
    return s;
}

std::string vec4_lit(const float v[4]) {
    return "vec4(" + fmt_float(v[0]) + ", " + fmt_float(v[1]) + ", " + fmt_float(v[2]) + ", " +
           fmt_float(v[3]) + ")";
}

struct Emitter {
    const ArbProgram &p;
    bool es;
    bool vs;
    unsigned fs_inputs;
    std::set<std::string> temps, addrs;
    std::map<std::string, const ParamDecl *> params;
    std::set<std::string> samplers;   // "rs_tex0_2D" ...
    std::ostringstream body;
    std::string error;

    Emitter(const ArbProgram &prog, bool es_, unsigned fsin)
        : p(prog), es(es_), vs(prog.stage == Stage::Vertex), fs_inputs(fsin) {
        temps.insert(p.temps.begin(), p.temps.end());
        addrs.insert(p.addresses.begin(), p.addresses.end());
        for (const ParamDecl &d : p.param_decls) params[d.name] = &d;
    }

    const char *local_array() const { return vs ? "rs_vp_local" : "rs_fp_local"; }

    // Register reference without swizzle/negation.
    std::string reg(const Operand &o, bool is_dst) {
        const std::string &b = o.base;
        if (o.has_imm) return vec4_lit(o.imm);
        if (temps.count(b)) return "t_" + b;
        if (addrs.count(b)) return "a_" + b;
        if (p.direct_params.count(b)) {
            if (o.relative)
                return std::string(local_array()) + "[a_" + o.rel_reg + "." + o.rel_comp + " + " +
                       std::to_string(o.index) + "]";
            return std::string(local_array()) + "[" + std::to_string(o.index < 0 ? 0 : o.index) + "]";
        }
        if (params.count(b)) {
            const ParamDecl *d = params[b];
            if (!d->is_array) return "p_" + b;
            if (o.relative)
                return "p_" + b + "[a_" + o.rel_reg + "." + o.rel_comp + " + " + std::to_string(o.index) + "]";
            return "p_" + b + "[" + std::to_string(o.index < 0 ? 0 : o.index) + "]";
        }
        if (b == "program.local") return std::string(local_array()) + "[" + std::to_string(o.index) + "]";
        if (vs) {
            if (!is_dst) {
                int loc = attrib_location_for(b, o.index);
                if (loc >= 0) return "rs_attr" + std::to_string(loc);
            }
            if (b == "result.position") return "o_pos";
            if (b == "result.color" || b == "result.color.primary") return "o_var[" + std::to_string(VAR_COL0) + "]";
            if (b == "result.color.secondary") return "o_var[" + std::to_string(VAR_COL1) + "]";
            if (b == "result.fogcoord") return "o_var[" + std::to_string(VAR_FOG) + "]";
            if (b == "result.texcoord") return "o_var[" + std::to_string(VAR_TEX0 + (o.index < 0 ? 0 : o.index)) + "]";
            if (b == "result.pointsize") return "o_psize";
        } else {
            if (b == "fragment.texcoord") return varying_name(VAR_TEX0 + (o.index < 0 ? 0 : o.index));
            if (b == "fragment.color" || b == "fragment.color.primary") return varying_name(VAR_COL0);
            if (b == "fragment.color.secondary") return varying_name(VAR_COL1);
            if (b == "fragment.fogcoord") return varying_name(VAR_FOG);
            if (b == "fragment.position") return "gl_FragCoord";
            if (b == "result.color" || b == "result.color.primary") return "o_color";
            if (b == "result.depth") return "o_depth";
        }
        error = "unknown register " + b;
        return "vec4(0.0)";
    }

    // Source operand as a vec4 expression.
    std::string src(const Operand &o) {
        std::string r = reg(o, false);
        std::string sw = o.swizzle;
        if (sw.size() == 1) sw = std::string(4, sw[0]);
        else if (!sw.empty() && sw.size() < 4) sw += std::string(4 - sw.size(), sw.back());
        if (addrs.count(o.base)) r = "vec4(" + r + ")";
        std::string e = sw.empty() || sw == "xyzw" ? r : "(" + r + ")." + sw;
        return o.negate ? "(-" + e + ")" : e;
    }

    std::string scalar(const Operand &o) { return "(" + src(o) + ").x"; }

    void instruction(const Instruction &ins) {
        const std::string &op = ins.op;
        std::vector<std::string> s;
        for (const Operand &o : ins.src) s.push_back(src(o));
        auto need = [&](size_t n) {
            if (s.size() != n) error = op + " expects " + std::to_string(n) + " sources";
            while (s.size() < n) s.push_back("vec4(0.0)");
        };
        std::string e;
        if (op == "ABS") { need(1); e = "abs(" + s[0] + ")"; }
        else if (op == "ADD") { need(2); e = s[0] + " + " + s[1]; }
        else if (op == "SUB") { need(2); e = s[0] + " - " + s[1]; }
        else if (op == "MUL") { need(2); e = s[0] + " * " + s[1]; }
        else if (op == "MAD") { need(3); e = s[0] + " * " + s[1] + " + " + s[2]; }
        else if (op == "MOV") { need(1); e = s[0]; }
        else if (op == "MAX") { need(2); e = "max(" + s[0] + ", " + s[1] + ")"; }
        else if (op == "MIN") { need(2); e = "min(" + s[0] + ", " + s[1] + ")"; }
        else if (op == "FLR") { need(1); e = "floor(" + s[0] + ")"; }
        else if (op == "FRC") { need(1); e = "fract(" + s[0] + ")"; }
        else if (op == "DP3") { need(2); e = "vec4(dot((" + s[0] + ").xyz, (" + s[1] + ").xyz))"; }
        else if (op == "DP4") { need(2); e = "vec4(dot(" + s[0] + ", " + s[1] + "))"; }
        else if (op == "DPH") { need(2); e = "vec4(dot((" + s[0] + ").xyz, (" + s[1] + ").xyz) + (" + s[1] + ").w)"; }
        else if (op == "DST") { need(2); e = "rs_dst(" + s[0] + ", " + s[1] + ")"; }
        else if (op == "XPD") { need(2); e = "vec4(cross((" + s[0] + ").xyz, (" + s[1] + ").xyz), 1.0)"; }
        else if (op == "SGE") { need(2); e = "rs_sge(" + s[0] + ", " + s[1] + ")"; }
        else if (op == "SLT") { need(2); e = "rs_slt(" + s[0] + ", " + s[1] + ")"; }
        else if (op == "CMP") { need(3); e = "rs_cmp(" + s[0] + ", " + s[1] + ", " + s[2] + ")"; }
        else if (op == "LRP") { need(3); e = s[0] + " * " + s[1] + " + (vec4(1.0) - " + s[0] + ") * " + s[2]; }
        else if (op == "RCP") { need(1); e = "vec4(1.0 / " + scalar(ins.src[0]) + ")"; }
        else if (op == "RSQ") { need(1); e = "vec4(inversesqrt(abs(" + scalar(ins.src[0]) + ")))"; }
        else if (op == "EX2") { need(1); e = "vec4(exp2(" + scalar(ins.src[0]) + "))"; }
        else if (op == "LG2") { need(1); e = "vec4(log2(" + scalar(ins.src[0]) + "))"; }
        else if (op == "POW") { need(2); e = "vec4(rs_pow(" + scalar(ins.src[0]) + ", " + scalar(ins.src[1]) + "))"; }
        else if (op == "SIN") { need(1); e = "vec4(sin(" + scalar(ins.src[0]) + "))"; }
        else if (op == "COS") { need(1); e = "vec4(cos(" + scalar(ins.src[0]) + "))"; }
        else if (op == "SCS") { need(1); e = "rs_scs(" + scalar(ins.src[0]) + ")"; }
        else if (op == "EXP") { need(1); e = "rs_exp(" + scalar(ins.src[0]) + ")"; }
        else if (op == "LOG") { need(1); e = "rs_log(" + scalar(ins.src[0]) + ")"; }
        else if (op == "LIT") { need(1); e = "rs_lit(" + s[0] + ")"; }
        else if (op == "ARL") {
            need(1);
            body << "  " << reg(ins.dst, true) << ".x = int(floor(" << scalar(ins.src[0]) << "));\n";
            return;
        } else if (op == "KIL") {
            need(1);
            body << "  if (any(lessThan(" << s[0] << ", vec4(0.0)))) discard;\n";
            return;
        } else if (op == "SWZ") {
            need(1);
            std::string r = "vec4(";
            for (int k = 0; k < 4; k++) {
                const std::string &c = ins.swz_ext[k];
                bool neg = c[0] == '-';
                char ch = c.back();
                std::string v = ch == '0' ? "0.0" : ch == '1' ? "1.0" : "(" + s[0] + ")." + ch;
                r += (k ? ", " : "") + std::string(neg ? "-" : "") + v;
            }
            e = r + ")";
        } else if (op == "TEX" || op == "TXP" || op == "TXB") {
            need(1);
            e = texture(ins, s[0]);
        } else {
            error = "unhandled op " + op;
            return;
        }
        if (ins.saturate) e = "clamp(" + e + ", 0.0, 1.0)";
        std::string d = reg(ins.dst, true);
        std::string mask = ins.dst.swizzle.empty() ? "xyzw" : ins.dst.swizzle;
        if (addrs.count(ins.dst.base)) { error = "non-ARL write to address register"; return; }
        if (mask == "xyzw") body << "  " << d << " = " << e << ";\n";
        else body << "  rs_t = " << e << ";\n  " << d << "." << mask << " = rs_t." << mask << ";\n";
    }

    std::string texture(const Instruction &ins, const std::string &coord) {
        const std::string &t = ins.tex_target;
        std::string sampler = "rs_tex" + std::to_string(ins.tex_unit) + "_" + t;
        samplers.insert(sampler);
        bool proj = ins.op == "TXP", bias = ins.op == "TXB";
        std::string c = "(" + coord + ")";
        std::string fn, arg;
        if (t == "2D") { fn = es ? "texture2D" : "texture2D"; arg = proj ? c : c + ".xy"; if (proj) fn += "Proj"; }
        else if (t == "CUBE") { fn = "textureCube"; arg = c + ".xyz"; }
        else if (t == "1D") { fn = proj ? "texture1DProj" : "texture1D"; arg = proj ? c : c + ".x"; }
        else if (t == "3D") { fn = proj ? "texture3DProj" : "texture3D"; arg = proj ? c : c + ".xyz"; }
        else if (t == "RECT") { fn = proj ? "texture2DRectProj" : "texture2DRect"; arg = proj ? c : c + ".xy"; }
        else { error = "unsupported texture target " + t; return "vec4(0.0)"; }
        if (es && (t == "1D" || t == "3D" || t == "RECT")) error = "texture target " + t + " has no GLSL ES 1.00 equivalent";
        if (bias) return fn + "(" + sampler + ", " + arg + ", " + c + ".w)";
        return fn + "(" + sampler + ", " + arg + ")";
    }

    static std::string sampler_type(const std::string &name) {
        std::string t = name.substr(name.rfind('_') + 1);
        if (t == "2D") return "sampler2D";
        if (t == "CUBE") return "samplerCube";
        if (t == "1D") return "sampler1D";
        if (t == "3D") return "sampler3D";
        return "sampler2DRect";
    }

    std::string run() {
        // Locals: PARAM arrays are built from the uniform array and constants.
        std::ostringstream decl;
        for (const ParamDecl &d : p.param_decls) {
            if (p.direct_params.count(d.name)) continue;
            size_t n = d.elems.size();
            if (d.is_array) decl << "  vec4 p_" << d.name << "[" << n << "];\n";
            else decl << "  vec4 p_" << d.name << ";\n";
            for (size_t k = 0; k < n; k++) {
                const ParamBinding &b = d.elems[k];
                std::string lhs = "p_" + d.name + (d.is_array ? "[" + std::to_string(k) + "]" : "");
                std::string rhs = b.is_local ? std::string(local_array()) + "[" + std::to_string(b.local_index) + "]"
                                             : vec4_lit(b.value);
                decl << "  " << lhs << " = " << rhs << ";\n";
            }
        }
        for (const std::string &t : p.temps) decl << "  vec4 t_" << t << " = vec4(0.0);\n";
        for (const std::string &a : p.addresses) decl << "  ivec4 a_" << a << " = ivec4(0);\n";
        decl << "  vec4 rs_t;\n";
        if (vs) {
            decl << "  vec4 o_pos = vec4(0.0, 0.0, 0.0, 1.0);\n  float o_psize = 1.0;\n";
            decl << "  vec4 o_var[" << VAR_COUNT << "];\n";
            for (unsigned v = 0; v < VAR_COUNT; v++)
                decl << "  o_var[" << v << "] = vec4(0.0, 0.0, 0.0, 1.0);\n";
        } else {
            decl << "  vec4 o_color = vec4(0.0);\n  vec4 o_depth = vec4(gl_FragCoord.z);\n";
        }

        for (const Instruction &ins : p.code) {
            instruction(ins);
            if (!error.empty()) return "#error " + error + " (line " + std::to_string(ins.line) + ")\n";
        }

        std::ostringstream out;
        if (es) out << "#version 100\nprecision highp float;\nprecision highp int;\n";
        else out << "#version 120\n";
        out << "// Translated from Cg " << (vs ? "arbvp1" : "arbfp1") << " object (entry "
            << (p.entry.empty() ? "main" : p.entry) << ") by the RS Re-ARMed Cg shim\n";
        int nlocal = p.uniform_slots;
        if (nlocal > 0) out << "uniform vec4 " << local_array() << "[" << nlocal << "];\n";
        const char *vary = "varying";
        if (vs) {
            for (unsigned loc = 0; loc < 16; loc++)
                if (p.attribs_read & (1u << loc)) out << "attribute vec4 rs_attr" << loc << ";\n";
            for (unsigned v = 0; v < VAR_COUNT; v++)
                if (fs_inputs & (1u << v)) out << vary << " vec4 " << varying_name(v) << ";\n";
        } else {
            for (unsigned v = 0; v < VAR_COUNT; v++)
                if (p.outputs & (1u << v)) out << vary << " vec4 " << varying_name(v) << ";\n";
            for (const std::string &s : samplers) out << "uniform " << sampler_type(s) << " " << s << ";\n";
        }
        out << helpers();
        out << "void main() {\n" << decl.str() << body.str();
        if (vs) {
            out << "  gl_Position = o_pos;\n";
            // Fixed-function rules between the stages: vertex colors are clamped
            // (GL_CLAMP_VERTEX_COLOR defaults to TRUE) and only fog's x survives.
            for (unsigned v = 0; v < VAR_COUNT; v++) {
                if (!(fs_inputs & (1u << v))) continue;
                std::string o = "o_var[" + std::to_string(v) + "]";
                if (v == VAR_COL0 || v == VAR_COL1) o = "clamp(" + o + ", 0.0, 1.0)";
                if (v == VAR_FOG) o = "vec4(" + o + ".x, 0.0, 0.0, 1.0)";
                out << "  " << varying_name(v) << " = " << o << ";\n";
            }
        } else {
            out << "  gl_FragColor = o_color;\n";
            if (p.writes_depth && !es) out << "  gl_FragDepth = o_depth.z;\n";
        }
        out << "}\n";
        return out.str();
    }

    std::string helpers() const {
        // Componentwise selects written out so inf/NaN in the unselected operand
        // can't leak through (mix() would compute inf*0).
        return "vec4 rs_cmp(vec4 a, vec4 b, vec4 c) {\n"
               "  return vec4(a.x < 0.0 ? b.x : c.x, a.y < 0.0 ? b.y : c.y,\n"
               "              a.z < 0.0 ? b.z : c.z, a.w < 0.0 ? b.w : c.w);\n}\n"
               // Apple's ARB POW: 0 for a zero base whatever the exponent, NaN for a
               // negative one. Spelled out so constant folding can't differ.
               "float rs_pow(float a, float b) { return a == 0.0 ? 0.0 : exp2(b * log2(a)); }\n"
               "vec4 rs_sge(vec4 a, vec4 b) { return vec4(greaterThanEqual(a, b)); }\n"
               "vec4 rs_slt(vec4 a, vec4 b) { return vec4(lessThan(a, b)); }\n"
               "vec4 rs_dst(vec4 a, vec4 b) { return vec4(1.0, a.y * b.y, a.z, b.w); }\n"
               "vec4 rs_scs(float a) { return vec4(cos(a), sin(a), 0.0, 1.0); }\n"
               "vec4 rs_exp(float a) { float f = floor(a); return vec4(exp2(f), a - f, exp2(a), 1.0); }\n"
               "vec4 rs_log(float a) {\n"
               "  float l = log2(abs(a)), f = floor(l);\n"
               "  return vec4(f, abs(a) / exp2(f), l, 1.0);\n}\n"
               "vec4 rs_lit(vec4 a) {\n"
               "  float d = max(a.x, 0.0), s = max(a.y, 0.0), e = clamp(a.w, -128.0, 128.0);\n"
               "  return vec4(1.0, d, a.x > 0.0 ? (s == 0.0 && e == 0.0 ? 1.0 : pow(s, e)) : 0.0, 1.0);\n}\n";
    }
};

}  // namespace

std::string emit_glsl(const ArbProgram &p, const std::string &dialect, unsigned fs_inputs) {
    Emitter e(p, dialect == "100es", fs_inputs);
    return e.run();
}

}  // namespace rscg
