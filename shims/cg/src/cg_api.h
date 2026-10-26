// The subset of NVIDIA Cg 3.1 / CgGL that Rocksmith 2014 imports.
//
// Numeric values were read from the game's own Cg.framework (3.1.0.13), not
// copied from headers. Handles are opaque pointers in the real runtime; the
// game never dereferences them.
#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _CGcontext *CGcontext;
typedef struct _CGprogram *CGprogram;
typedef struct _CGparameter *CGparameter;
typedef void *CGhandle;
typedef int CGbool;

typedef unsigned int CGGLenum_t;   // GLenum without pulling in GL headers
typedef unsigned int CGGLuint_t;

typedef enum {
    CG_UNKNOWN = 4096,
    CG_IN,
    CG_OUT,
    CG_INOUT,
    CG_MIXED,
    CG_VARYING,
    CG_UNIFORM,
    CG_CONSTANT,
    CG_PROGRAM_SOURCE,
    CG_PROGRAM_ENTRY,
    CG_COMPILED_PROGRAM,
    CG_PROGRAM_PROFILE,
    CG_GLOBAL,
    CG_PROGRAM,
    CG_DEFAULT,
    CG_ERROR,
    CG_SOURCE,
    CG_OBJECT,
    CG_COMPILE_MANUAL,
    CG_COMPILE_IMMEDIATE,
    CG_COMPILE_LAZY,
    CG_CURRENT,
    CG_LITERAL,
    CG_VERSION,
    CG_ROW_MAJOR,
    CG_COLUMN_MAJOR,
} CGenum;

typedef enum {
    CG_PROFILE_UNKNOWN = 6145,
    CG_PROFILE_VP30 = 6148,
    CG_PROFILE_FP30 = 6149,
    CG_PROFILE_ARBVP1 = 6150,
    CG_PROFILE_FP40 = 6151,
    CG_PROFILE_ARBFP1 = 7000,
    CG_PROFILE_VP40 = 7001,
    CG_PROFILE_GLSLV = 7007,
    CG_PROFILE_GLSLF = 7008,
} CGprofile;

typedef enum {
    CG_UNKNOWN_TYPE = 0,
    CG_STRUCT = 1,
    CG_ARRAY = 2,
    CG_HALF = 1025,
    CG_HALF4x4 = 1044,
    CG_FLOAT = 1045,
    CG_FLOAT2 = 1046,
    CG_FLOAT3 = 1047,
    CG_FLOAT4 = 1048,
    CG_FLOAT2x2 = 1054,
    CG_FLOAT3x3 = 1059,
    CG_FLOAT3x4 = 1060,
    CG_FLOAT4x2 = 1062,
    CG_FLOAT4x3 = 1063,
    CG_FLOAT4x4 = 1064,
    CG_SAMPLER1D = 1065,
    CG_SAMPLER2D = 1066,
    CG_SAMPLER3D = 1067,
    CG_SAMPLERRECT = 1068,
    CG_SAMPLERCUBE = 1069,
    CG_FIXED = 1070,
    CG_FLOAT1 = 1091,
    CG_INT = 1093,
    CG_BOOL = 1114,
    CG_TEXTURE = 1137,
    CG_SAMPLER = 1143,
} CGtype;

typedef int CGresource;
#define CG_UNDEFINED_RESOURCE 3256

// --- core runtime -----------------------------------------------------------
CGcontext cgCreateContext(void);
void cgDestroyContext(CGcontext ctx);
CGprogram cgCreateProgram(CGcontext ctx, CGenum program_type, const char *program,
                          CGprofile profile, const char *entry, const char **args);
void cgDestroyProgram(CGprogram prog);
const char *cgGetProgramString(CGprogram prog, CGenum pname);

CGparameter cgGetNamedParameter(CGprogram prog, const char *name);
CGparameter cgGetFirstLeafParameter(CGprogram prog, CGenum name_space);
CGparameter cgGetNextLeafParameter(CGparameter param);
const char *cgGetParameterName(CGparameter param);
CGtype cgGetParameterType(CGparameter param);
const char *cgGetParameterSemantic(CGparameter param);
CGenum cgGetParameterDirection(CGparameter param);
CGenum cgGetParameterVariability(CGparameter param);
unsigned long cgGetParameterResourceIndex(CGparameter param);
CGbool cgIsParameterUsed(CGparameter param, CGhandle handle);

// --- CgGL -------------------------------------------------------------------
void cgGLBindProgram(CGprogram prog);
void cgGLLoadProgram(CGprogram prog);
void cgGLEnableProfile(CGprofile profile);
void cgGLSetContextOptimalOptions(CGcontext ctx, CGprofile profile);
void cgGLSetParameter4fv(CGparameter param, const float *v);
void cgGLSetMatrixParameterfr(CGparameter param, const float *matrix);
void cgGLSetParameterPointer(CGparameter param, int fsize, CGGLenum_t type, int stride,
                             const void *pointer);
void cgGLEnableClientState(CGparameter param);
void cgGLDisableClientState(CGparameter param);
void cgGLSetTextureParameter(CGparameter param, CGGLuint_t texobj);
void cgGLEnableTextureParameter(CGparameter param);
void cgGLDisableTextureParameter(CGparameter param);
CGGLenum_t cgGLGetTextureEnum(CGparameter param);
void cgGLSetupSampler(CGparameter param, CGGLuint_t texobj);

// --- extras the game doesn't import, exported for tests and diagnostics -----
int cgGetError(void);
const char *cgGetErrorString(int error);
const char *cgGetLastListing(CGcontext ctx);
CGresource cgGetParameterResource(CGparameter param);
void cgGLDisableProfile(CGprofile profile);
void cgGLUnbindProgram(CGprofile profile);

#ifdef __cplusplus
}
#endif
