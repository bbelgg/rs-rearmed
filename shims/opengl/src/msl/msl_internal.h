// Internals of the GLSL -> MSL translator shared by the library, its tests and the corpus tool
// (tools/metal/, shims/opengl/test/msl_verify.m). Not used by the Metal core.
#pragma once
#include "msl_xlat.h"

#ifdef __cplusplus
extern "C" {
#endif

// Bumped whenever the generated MSL or the reflection changes; a corpus written by another
// version is ignored.
#define RSMSL_VERSION 2

// rsmsl_translate without the corpus lookup.
rsmsl_program *rsmsl_translate_direct(const char *vs_glsl, const char *fs_glsl,
                                      const rsmsl_attrib *bound_attribs, int n_bound, char **error);

// With flags: RSMSL_XLAT_PROBE adds `vertex void rs_vs_probe(...)`, which writes every vertex
// output (position after the epilogue, then the varyings) as float4s to buffer(30); a comment
// "// rs_probe: <member>:<components> ..." lists them. For msl_verify only.
enum { RSMSL_XLAT_PROBE = 1 };
rsmsl_program *rsmsl_translate_ex(const char *vs_glsl, const char *fs_glsl, const rsmsl_attrib *bound_attribs,
                                  int n_bound, unsigned flags, char **error);

// Corpus file (see msl_corpus.cpp for the format).
enum { RSMSL_CORPUS_VERIFIED = 1 };  // rendered like GL by msl_verify
typedef struct rsmsl_corpus_writer rsmsl_corpus_writer;
rsmsl_corpus_writer *rsmsl_corpus_begin(void);
void rsmsl_corpus_add(rsmsl_corpus_writer *w, const char *vs_glsl, const char *fs_glsl,
                      const rsmsl_program *p, unsigned flags);
int rsmsl_corpus_finish(rsmsl_corpus_writer *w, const char *path);  // 0 on success; frees w

// Use this corpus file instead of the default search (NULL: none). For tests and tools; call
// before the first rsmsl_translate.
void rsmsl_corpus_override(const char *path);
// Path of the corpus in use and its entry count (NULL / 0 when none was found).
const char *rsmsl_corpus_path(int *n_entries);
// Flags of the corpus entry for this pair, or -1 if there is none.
int rsmsl_corpus_flags(const char *vs_glsl, const char *fs_glsl);

#ifdef __cplusplus
}
#endif
