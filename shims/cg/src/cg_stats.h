// GL calls made by the Cg shim, counted by kind. Exported for tests and
// benchmarks (tests/stress_test.c); not part of NVIDIA's Cg API.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

enum {
    ST_UseProgram,      // glUseProgram
    ST_Uniform,         // glUniform4fv, glUniform1i
    ST_Get,             // glGetIntegerv
    ST_Compile,         // glCompileShader
    ST_Links,           // glLinkProgram
    ST_BindProgramARB,  // glBindProgramARB
    ST_ProgramLocal,    // glProgramLocalParameter(s)4fv*
    ST_Enable,          // glEnable/glDisable
    ST_Texture,         // glActiveTexture, glBindTexture
    ST_Attrib,          // glVertexAttribPointer, glEnable/DisableVertexAttribArray
    ST_CurProgram,      // GL_CURRENT_PROGRAM answered by libRSOpenGL's tracking (no GL call)
    ST_Warmed,          // links made at program load for pairs from the pair cache
    ST_CompileUs,       // microseconds in shader compiles (translation included)
    ST_LinkUs,          // microseconds in links (locations and sampler units included)
    ST_LinkMaxUs,       // the longest link
    RS_CG_STAT_COUNT
};

static const char *const rs_cg_stat_names[RS_CG_STAT_COUNT] = {
    "UseProgram", "Uniform", "GetIntegerv", "CompileShader", "LinkProgram",
    "BindProgramARB", "ProgramLocal", "Enable/Disable", "Texture", "Attrib",
    "CurrentProgram", "WarmedLinks", "CompileUs", "LinkUs", "LinkMaxUs",
};

// Counters that are GL calls (the rest are times and events).
#define RS_CG_STAT_IS_CALL(s) ((s) <= ST_Attrib)

// Copies the running totals into out[RS_CG_STAT_COUNT].
void rs_cg_gl_stats(unsigned long long *out);

#ifdef __cplusplus
}
#endif
