// Parsed form of a Cg-compiled ARB_vertex_program / ARB_fragment_program object
// (the .vpo/.fpo text the game passes to cgCreateProgram as CG_OBJECT).
#pragma once

#include <array>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "cg_api.h"

namespace rscg {

enum class Stage { Vertex, Fragment };

// One leaf parameter, in the order and with the values NVIDIA's runtime reports
// for the same object (checked against tools/cg_reference.c output).
struct ParamInfo {
    std::string name;
    CGtype type = CG_UNKNOWN_TYPE;
    std::string semantic;
    CGenum name_space = CG_PROGRAM;   // CG_PROGRAM or CG_GLOBAL
    CGenum direction = CG_IN;
    CGenum variability = CG_UNIFORM;
    CGresource resource = CG_UNDEFINED_RESOURCE;
    unsigned long resource_index = 0;
    bool used = false;

    // What the shim needs to drive GL:
    int reg_base = -1;    // uniform: first program.local / c[] register
    int reg_count = 0;    //          registers occupied (matrix rows)
    int rows = 1, cols = 4;
    int texunit = -1;     // sampler: texture image unit
    CGGLenum_t tex_target = 0;   // GL_TEXTURE_2D, ... from the sampler type
    int attrib = -1;      // vertex input: generic attribute location (NVIDIA aliasing)
};

// A source/destination register reference after parsing, kept as text pieces
// for the GLSL emitter.
struct Operand {
    bool negate = false;
    std::string base;      // "R0", "c", "vertex.position", "fragment.texcoord", "{...}"
    int index = -1;        // [n] for arrays / texcoord sets, -1 if none
    bool relative = false; // c[A0.x + n]
    std::string rel_reg;   // "A0"
    char rel_comp = 'x';
    std::string swizzle;   // "", "x", "xyzw", "xyzz", ...
    float imm[4] = {0, 0, 0, 0};   // inline constant when base == "{"
    bool has_imm = false;
};

struct Instruction {
    std::string op;        // "MAD", "TEX", ... without _SAT
    bool saturate = false;
    Operand dst;
    std::vector<Operand> src;
    int tex_unit = -1;     // TEX/TXP/TXB: texture[n]
    std::string tex_target;// "1D", "2D", "3D", "CUBE", "RECT"
    std::vector<std::string> swz_ext;   // SWZ: extended swizzle components
    int line = 0;
};

// PARAM name[n] = { ... };  each element is either program.local[i] or a constant.
struct ParamBinding {
    bool is_local = false;
    int local_index = 0;
    float value[4] = {0, 0, 0, 1};
};
struct ParamDecl {
    std::string name;
    bool is_array = false;
    std::vector<ParamBinding> elems;
};

struct ArbProgram {
    Stage stage = Stage::Vertex;
    std::string entry;       // #program
    std::vector<ParamInfo> params;
    std::vector<std::string> temps, addresses;
    std::vector<ParamDecl> param_decls;
    std::vector<Instruction> code;
    int max_local = -1;      // highest program.local index referenced
    // The GLSL uniform array rs_vp_local/rs_fp_local stands for program.local and
    // for every PARAM array whose element k is program.local[k] or a constant
    // ("direct" arrays, which is how Cg lays out c[]). uniform_slots is its size
    // and uniform_consts the constant slots the shim fills in once.
    int uniform_slots = 0;
    std::vector<std::pair<int, std::array<float, 4>>> uniform_consts;
    std::set<std::string> direct_params;
    unsigned tex_units_used = 0;   // bitmask (fragment)
    unsigned attribs_read = 0;     // bitmask of generic locations (vertex)
    unsigned outputs = 0;          // Varying bits written (vertex) / read (fragment)
    bool writes_depth = false;
    bool kills = false;
};

// Varying slots shared between the stages.
enum Varying : unsigned {
    VAR_TEX0 = 0,   // ..VAR_TEX0+7
    VAR_COL0 = 8,
    VAR_COL1 = 9,
    VAR_FOG = 10,
    VAR_COUNT = 11,
};

// Parse an ARB object. Returns false (with *error) when it is not a program the
// shim understands. The parameter table is filled even for programs whose body
// fails to parse, matching NVIDIA's runtime, which accepts any text at create
// time and only fails in cgGLLoadProgram.
bool parse_arb_program(const std::string &text, ArbProgram *out, std::string *error);

// Emit a GLSL vertex or fragment shader for the program. `dialect` is "120"
// (desktop compatibility, Apple's legacy GL) or "100es" (GLSL ES 1.00, ANGLE).
// For a vertex shader, `fs_inputs` is the set of Varying bits the paired
// fragment shader reads: every one of them is declared and written, everything
// else is dropped.
std::string emit_glsl(const ArbProgram &p, const std::string &dialect, unsigned fs_inputs);

// Generic attribute location Cg's NVIDIA aliasing gives a vertex input.
int attrib_location_for(const std::string &base, int index);

}  // namespace rscg
