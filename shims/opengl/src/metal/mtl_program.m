// Metal core: shaders, programs (GLSL -> MSL via rsmsl_translate at link),
// uniforms in CPU shadow blocks.
#include "mtl_internal.h"

#include <stdlib.h>
#include <string.h>

#ifdef RSM_MSL_DYNAMIC
// No translator linked into this build: it may come from the process (a test).
extern rsmsl_program *rsmsl_translate(const char *, const char *, const rsmsl_attrib *, int, char **)
    __attribute__((weak_import));
extern void rsmsl_free(rsmsl_program *) __attribute__((weak_import));
extern uint64_t rsmsl_key(const char *, const char *) __attribute__((weak_import));
#endif

@implementation RSMProgram
- (void)dealloc {
    if (refl)
        rsmsl_free(refl);
    for (int i = 0; i < n_uniforms; i++)
        free(uniforms[i].name);
    free(uniforms);
    free(loc_uniform);
    free(int_values);
    free(tex);
    free(vs_block);
    free(fs_block);
    rsm_map_free(&pipelines);
}
@end

// --- Shaders ------------------------------------------------------------------------
GLuint rsmtl_glCreateShader(GLenum type) {
    RSM_CTX(0)
    if (type != GL_VERTEX_SHADER && type != GL_FRAGMENT_SHADER) {
        rsm_error(ctx, GL_INVALID_ENUM);
        return 0;
    }
    RSMShader *s = [RSMShader new];
    s->type = type;
    s->source = @"";
    GLuint n = rsm_names_alloc(&ctx->objects);
    rsm_names_set(&ctx->objects, n, s);
    return n;
}

static RSMShader *rsm_shader(RSMContext *ctx, GLuint name) {
    id o = rsm_get(&ctx->objects, name);
    if (![o isKindOfClass:[RSMShader class]]) {
        rsm_error(ctx, o ? GL_INVALID_OPERATION : GL_INVALID_VALUE);
        return nil;
    }
    return o;
}

static RSMProgram *rsm_program(RSMContext *ctx, GLuint name) {
    id o = rsm_get(&ctx->objects, name);
    if (![o isKindOfClass:[RSMProgram class]]) {
        rsm_error(ctx, o ? GL_INVALID_OPERATION : GL_INVALID_VALUE);
        return nil;
    }
    return o;
}

void rsmtl_glShaderSource(GLuint shader, GLsizei count, const GLchar *const *string, const GLint *length) {
    RSM_CTX()
    RSMShader *s = rsm_shader(ctx, shader);
    if (!s)
        return;
    if (count < 0) {
        rsm_error(ctx, GL_INVALID_VALUE);
        return;
    }
    NSMutableString *src = [NSMutableString new];
    for (GLsizei i = 0; i < count; i++) {
        if (!string[i])
            continue;
        size_t n = length && length[i] >= 0 ? (size_t)length[i] : strlen(string[i]);
        NSString *part = [[NSString alloc] initWithBytes:string[i] length:n encoding:NSUTF8StringEncoding];
        if (!part)
            part = [[NSString alloc] initWithBytes:string[i] length:n encoding:NSISOLatin1StringEncoding];
        if (part)
            [src appendString:part];
    }
    s->source = src;
    s->compiled = false;
}

void rsmtl_glCompileShader(GLuint shader) {
    RSM_CTX()
    // GLSL is checked when the program links (rsmsl_translate needs both stages).
    RSMShader *s = rsm_shader(ctx, shader);
    if (s)
        s->compiled = true;
}

void rsmtl_glDeleteShader(GLuint shader) {
    RSM_CTX()
    if (!shader)
        return;
    RSMShader *s = rsm_shader(ctx, shader);
    if (!s)
        return;
    s->deleted = true;
    rsm_names_set(&ctx->objects, shader, nil);  // attached programs keep the object
}

GLboolean rsmtl_glIsShader(GLuint shader) {
    RSM_CTX(GL_FALSE)
    return [rsm_get(&ctx->objects, shader) isKindOfClass:[RSMShader class]];
}

void rsmtl_glGetShaderiv(GLuint shader, GLenum pname, GLint *params) {
    RSM_CTX()
    RSMShader *s = rsm_shader(ctx, shader);
    if (!s)
        return;
    switch (pname) {
    case GL_SHADER_TYPE: *params = (GLint)s->type; break;
    case GL_COMPILE_STATUS: *params = s->compiled; break;
    case GL_DELETE_STATUS: *params = s->deleted; break;
    case GL_INFO_LOG_LENGTH: *params = 0; break;
    case GL_SHADER_SOURCE_LENGTH: *params = (GLint)strlen(s->source.UTF8String) + 1; break;
    default: rsm_error(ctx, GL_INVALID_ENUM);
    }
}

static void rsm_copy_log(const char *log, GLsizei bufSize, GLsizei *length, GLchar *out) {
    size_t n = log ? strlen(log) : 0;
    if (bufSize <= 0) {
        if (length)
            *length = 0;
        return;
    }
    if (n > (size_t)bufSize - 1)
        n = (size_t)bufSize - 1;
    if (n)
        memcpy(out, log, n);
    out[n] = 0;
    if (length)
        *length = (GLsizei)n;
}

void rsmtl_glGetShaderInfoLog(GLuint shader, GLsizei bufSize, GLsizei *length, GLchar *infoLog) {
    RSM_CTX()
    if (rsm_shader(ctx, shader))
        rsm_copy_log("", bufSize, length, infoLog);
}

// --- Programs -----------------------------------------------------------------------
GLuint rsmtl_glCreateProgram(void) {
    RSM_CTX(0)
    RSMProgram *p = [RSMProgram new];
    p->shaders = [NSMutableArray new];
    p->bound_attribs = [NSMutableDictionary new];
    p->log = @"";
    rsm_map_init(&p->pipelines, sizeof(rsm_pkey));
    GLuint n = rsm_names_alloc(&ctx->objects);
    rsm_names_set(&ctx->objects, n, p);
    return n;
}

void rsmtl_glDeleteProgram(GLuint program) {
    RSM_CTX()
    if (!program)
        return;
    RSMProgram *p = rsm_program(ctx, program);
    if (!p)
        return;
    p->deleted = true;
    rsm_names_set(&ctx->objects, program, nil);  // the current program stays alive in ctx->prog
}

GLboolean rsmtl_glIsProgram(GLuint program) {
    RSM_CTX(GL_FALSE)
    return [rsm_get(&ctx->objects, program) isKindOfClass:[RSMProgram class]];
}

void rsmtl_glAttachShader(GLuint program, GLuint shader) {
    RSM_CTX()
    RSMProgram *p = rsm_program(ctx, program);
    RSMShader *s = p ? rsm_shader(ctx, shader) : nil;
    if (!s)
        return;
    if ([p->shaders containsObject:s]) {
        rsm_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    [p->shaders addObject:s];
}

void rsmtl_glDetachShader(GLuint program, GLuint shader) {
    RSM_CTX()
    RSMProgram *p = rsm_program(ctx, program);
    RSMShader *s = p ? rsm_shader(ctx, shader) : nil;
    if (s)
        [p->shaders removeObject:s];
}

void rsmtl_glBindAttribLocation(GLuint program, GLuint index, const GLchar *name) {
    RSM_CTX()
    RSMProgram *p = rsm_program(ctx, program);
    if (!p)
        return;
    if (index >= RSM_MAX_ATTRIBS) {
        rsm_error(ctx, GL_INVALID_VALUE);
        return;
    }
    if (!strncmp(name, "gl_", 3)) {
        rsm_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    p->bound_attribs[@(name)] = @(index);
}

void rsmtl_glBindFragDataLocation(GLuint program, GLuint color, const GLchar *name) {
    RSM_CTX()
    (void)color;
    (void)name;
    rsm_program(ctx, program);  // only colour 0 is drawn to
}

void rsmtl_glProgramParameteri(GLuint program, GLenum pname, GLint value) {
    RSM_CTX()
    (void)value;
    if (!rsm_program(ctx, program))
        return;
    if (pname != GL_PROGRAM_BINARY_RETRIEVABLE_HINT)
        rsm_error(ctx, GL_INVALID_ENUM);
}

static int rsm_uniform_components(int type) {
    switch (type) {
    case RSMSL_FLOAT: case RSMSL_INT: case RSMSL_BOOL: return 1;
    case RSMSL_VEC2: case RSMSL_IVEC2: return 2;
    case RSMSL_VEC3: case RSMSL_IVEC3: return 3;
    case RSMSL_VEC4: case RSMSL_IVEC4: case RSMSL_MAT2: return 4;
    case RSMSL_MAT3: return 9;
    case RSMSL_MAT4: return 16;
    }
    return 1;
}

static bool rsm_uniform_is_int(int type) {
    return type == RSMSL_INT || type == RSMSL_IVEC2 || type == RSMSL_IVEC3 || type == RSMSL_IVEC4 ||
           type == RSMSL_BOOL || type == RSMSL_SAMPLER_2D || type == RSMSL_SAMPLER_CUBE;
}

// "name", "name[0]" -> base location; "name[i]" -> base + i. -1 if none.
static int rsm_find_uniform(RSMProgram *p, const char *name) {
    size_t n = strlen(name);
    int idx = 0;
    const char *br = strchr(name, '[');
    if (br && name[n - 1] == ']') {
        char *end;
        long v = strtol(br + 1, &end, 10);
        if (end != name + n - 1 || v < 0)
            return -1;
        idx = (int)v;
        n = (size_t)(br - name);
    }
    for (int i = 0; i < p->n_uniforms; i++) {
        const rsm_uniform *u = &p->uniforms[i];
        if (strlen(u->name) == n && !strncmp(u->name, name, n)) {
            if (idx >= u->u.array_size)
                return -1;
            return u->loc + idx;
        }
    }
    return -1;
}

void rsmtl_glLinkProgram(GLuint program) {
    RSM_CTX()
    RSMProgram *p = rsm_program(ctx, program);
    if (!p)
        return;
    @autoreleasepool {
        NSMutableString *vs = [NSMutableString new], *fs = [NSMutableString new];
        for (RSMShader *s in p->shaders)
            [s->type == GL_VERTEX_SHADER ? vs : fs appendString:s->source];
        // Drop the previous executable.
        if (p->refl)
            rsmsl_free(p->refl);
        p->refl = NULL;
        for (int i = 0; i < p->n_uniforms; i++)
            free(p->uniforms[i].name);
        free(p->uniforms);
        free(p->loc_uniform);
        free(p->int_values);
        free(p->tex);
        free(p->vs_block);
        free(p->fs_block);
        p->uniforms = NULL;
        p->loc_uniform = p->int_values = NULL;
        p->tex = NULL;
        p->vs_block = p->fs_block = NULL;
        p->n_uniforms = p->n_locations = p->n_tex = 0;
        p->lib = nil;
        p->vs = nil;
        for (int i = 0; i < 9; i++)
            p->fs[i] = nil;
        rsm_map_free(&p->pipelines);
        p->linked = false;
        p->attrib_mask = 0;

        int nb = (int)p->bound_attribs.count, k = 0;
        rsmsl_attrib *bound = calloc((size_t)nb + 1, sizeof *bound);
        for (NSString *name in p->bound_attribs)
            bound[k++] = (rsmsl_attrib){name.UTF8String, p->bound_attribs[name].intValue, 0};
        char *err = NULL;
        if (!rsmsl_translate) {
            free(bound);
            p->log = @"no GLSL -> MSL translator in this build";
            rsm_log("link: %s", p->log.UTF8String);
            return;
        }
        rsmsl_program *r = vs.length && fs.length
                               ? rsmsl_translate(vs.UTF8String, fs.UTF8String, bound, nb, &err)
                               : NULL;
        free(bound);
        if (!r) {
            p->log = [NSString stringWithFormat:@"%s", err ? err : "a vertex and a fragment shader are needed"];
            if (err)
                rsm_log("link: %s", err);
            free(err);
            return;
        }
        uint64_t key = rsmsl_key(vs.UTF8String, fs.UTF8String);
        id<MTLLibrary> lib = ctx->libraries[@(key)];
        if (!lib) {
            NSError *e = nil;
            MTLCompileOptions *o = [MTLCompileOptions new];
            lib = [ctx->dev newLibraryWithSource:@(r->msl) options:o error:&e];
            if (!lib) {
                p->log = [NSString stringWithFormat:@"MSL compile failed: %@", e.localizedDescription];
                rsm_log("%s\n--- MSL ---\n%s", p->log.UTF8String, r->msl);
                rsmsl_free(r);
                return;
            }
            ctx->libraries[@(key)] = lib;
        }
        p->lib = lib;
        p->vs = [lib newFunctionWithName:@"rs_vs"];
        if (!p->vs) {
            p->log = @"MSL has no rs_vs";
            rsmsl_free(r);
            return;
        }
        p->refl = r;
        for (int i = 0; i < r->n_attribs; i++)
            if (r->attribs[i].location >= 0 && r->attribs[i].location < RSM_MAX_ATTRIBS)
                p->attrib_mask |= 1u << r->attribs[i].location;
        // Uniform locations: each uniform takes array_size consecutive locations.
        p->n_uniforms = r->n_uniforms;
        p->uniforms = calloc((size_t)r->n_uniforms + 1, sizeof *p->uniforms);
        int loc = 0;
        for (int i = 0; i < r->n_uniforms; i++) {
            p->uniforms[i].u = r->uniforms[i];
            if (p->uniforms[i].u.array_size < 1)
                p->uniforms[i].u.array_size = 1;
            p->uniforms[i].name = strdup(r->uniforms[i].name);
            p->uniforms[i].loc = loc;
            loc += p->uniforms[i].u.array_size;
        }
        p->n_locations = loc;
        p->loc_uniform = calloc((size_t)loc + 1, sizeof(int));
        p->int_values = calloc((size_t)loc + 1, sizeof(int));
        for (int i = 0; i < r->n_uniforms; i++)
            for (int j = 0; j < p->uniforms[i].u.array_size; j++)
                p->loc_uniform[p->uniforms[i].loc + j] = i;
        p->n_tex = r->n_samplers;
        p->tex = calloc((size_t)r->n_samplers + 1, sizeof *p->tex);
        for (int i = 0; i < r->n_samplers; i++) {
            int vl = rsm_find_uniform(p, r->samplers[i].name);
            p->tex[i] = (rsm_texbind){r->samplers[i].index, r->samplers[i].stages, r->samplers[i].type,
                                      vl};
        }
        p->vs_size = r->vs_uniform_size;
        p->fs_size = r->fs_uniform_size;
        p->vs_block = calloc((size_t)p->vs_size + 16, 1);
        p->fs_block = calloc((size_t)p->fs_size + 16, 1);
        p->vs_dirty = p->fs_dirty = true;
        p->uid = ++ctx->next_uid;
        p->linked = true;
        p->log = @"";
        if (ctx->prog == p)
            ctx->dirty |= RSM_DIRTY_PIPELINE | RSM_DIRTY_VERTEX | RSM_DIRTY_TEXTURES | RSM_DIRTY_UNIFORMS;
    }
}

id<MTLFunction> rsm_fragment_variant(RSMContext *ctx, RSMProgram *p, int alpha) {
    (void)ctx;
    if (p->fs[alpha])
        return p->fs[alpha];
    MTLFunctionConstantValues *cv = [MTLFunctionConstantValues new];
    uint32_t v = (uint32_t)alpha;
    [cv setConstantValue:&v type:MTLDataTypeUInt atIndex:0];
    NSError *e = nil;
    id<MTLFunction> f = [p->lib newFunctionWithName:@"rs_fs" constantValues:cv error:&e];
    if (!f) {
        f = [p->lib newFunctionWithName:@"rs_fs"];
        if (!f)
            rsm_log("fragment function: %s", e.localizedDescription.UTF8String);
    }
    p->fs[alpha] = f;
    return f;
}

void rsmtl_glUseProgram(GLuint program) {
    RSM_CTX()
    RSMProgram *p = nil;
    if (program) {
        p = rsm_program(ctx, program);
        if (!p)
            return;
        if (!p->linked) {
            rsm_error(ctx, GL_INVALID_OPERATION);
            return;
        }
    }
    ctx->prog_name = program;
    if (ctx->prog != p) {
        ctx->prog = p;
        ctx->dirty |= RSM_DIRTY_PIPELINE | RSM_DIRTY_VERTEX | RSM_DIRTY_TEXTURES | RSM_DIRTY_UNIFORMS;
    }
}

void rsmtl_glValidateProgram(GLuint program) {
    RSM_CTX()
    rsm_program(ctx, program);
}

void rsmtl_glGetProgramiv(GLuint program, GLenum pname, GLint *params) {
    RSM_CTX()
    RSMProgram *p = rsm_program(ctx, program);
    if (!p)
        return;
    switch (pname) {
    case GL_LINK_STATUS: *params = p->linked; break;
    case GL_VALIDATE_STATUS: *params = p->linked; break;
    case GL_DELETE_STATUS: *params = p->deleted; break;
    case GL_INFO_LOG_LENGTH: *params = p->log.length ? (GLint)strlen(p->log.UTF8String) + 1 : 0; break;
    case GL_ATTACHED_SHADERS: *params = (GLint)p->shaders.count; break;
    case GL_ACTIVE_ATTRIBUTES: *params = p->refl ? p->refl->n_attribs : 0; break;
    case GL_ACTIVE_UNIFORMS: *params = p->n_uniforms; break;
    case GL_ACTIVE_UNIFORM_MAX_LENGTH: {
        GLint m = 0;
        for (int i = 0; i < p->n_uniforms; i++) {
            GLint l = (GLint)strlen(p->uniforms[i].name) + 1 + (p->uniforms[i].u.array_size > 1 ? 3 : 0);
            m = l > m ? l : m;
        }
        *params = m;
        break;
    }
    case GL_ACTIVE_ATTRIBUTE_MAX_LENGTH: {
        GLint m = 0;
        for (int i = 0; p->refl && i < p->refl->n_attribs; i++) {
            GLint l = (GLint)strlen(p->refl->attribs[i].name) + 1;
            m = l > m ? l : m;
        }
        *params = m;
        break;
    }
    case GL_PROGRAM_BINARY_LENGTH: *params = 0; break;
    case GL_PROGRAM_BINARY_RETRIEVABLE_HINT: *params = 0; break;
    default: rsm_error(ctx, GL_INVALID_ENUM);
    }
}

void rsmtl_glGetProgramInfoLog(GLuint program, GLsizei bufSize, GLsizei *length, GLchar *infoLog) {
    RSM_CTX()
    RSMProgram *p = rsm_program(ctx, program);
    if (p)
        rsm_copy_log(p->log.UTF8String, bufSize, length, infoLog);
}

// No program binary formats (GL_NUM_PROGRAM_BINARY_FORMATS = 0): the game then
// compiles from source, as it does on Apple's GL.
void rsmtl_glGetProgramBinary(GLuint program, GLsizei bufSize, GLsizei *length, GLenum *binaryFormat,
                              GLvoid *binary) {
    RSM_CTX()
    (void)bufSize;
    (void)binaryFormat;
    (void)binary;
    if (length)
        *length = 0;
    if (rsm_program(ctx, program))
        rsm_error(ctx, GL_INVALID_OPERATION);
}

void rsmtl_glProgramBinary(GLuint program, GLenum binaryFormat, const GLvoid *binary, GLint length) {
    RSM_CTX()
    (void)binaryFormat;
    (void)binary;
    (void)length;
    RSMProgram *p = rsm_program(ctx, program);
    if (!p)
        return;
    p->linked = false;
    rsm_error(ctx, GL_INVALID_ENUM);
}

GLint rsmtl_glGetUniformLocation(GLuint program, const GLchar *name) {
    RSM_CTX(-1)
    RSMProgram *p = rsm_program(ctx, program);
    if (!p)
        return -1;
    if (!p->linked) {
        rsm_error(ctx, GL_INVALID_OPERATION);
        return -1;
    }
    return rsm_find_uniform(p, name);
}

GLint rsmtl_glGetAttribLocation(GLuint program, const GLchar *name) {
    RSM_CTX(-1)
    RSMProgram *p = rsm_program(ctx, program);
    if (!p)
        return -1;
    if (!p->linked) {
        rsm_error(ctx, GL_INVALID_OPERATION);
        return -1;
    }
    for (int i = 0; i < p->refl->n_attribs; i++)
        if (!strcmp(p->refl->attribs[i].name, name))
            return p->refl->attribs[i].location;
    return -1;
}

// --- Uniforms -----------------------------------------------------------------------
enum { U_FLOAT, U_INT, U_MAT };

// Write `count` elements of `comps` components starting at location `loc`.
static void rsm_set_uniform(GLint loc, GLsizei count, int kind, int comps, const void *data, bool transpose) {
    RSM_CTX()
    RSMProgram *p = ctx->prog;
    if (!p) {
        rsm_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    if (loc == -1)
        return;
    if (loc < 0 || loc >= p->n_locations || count < 0) {
        rsm_error(ctx, loc < 0 || loc >= p->n_locations ? GL_INVALID_OPERATION : GL_INVALID_VALUE);
        return;
    }
    rsm_uniform *u = &p->uniforms[p->loc_uniform[loc]];
    int type = u->u.type;
    bool is_int = rsm_uniform_is_int(type);
    bool is_sampler = type == RSMSL_SAMPLER_2D || type == RSMSL_SAMPLER_CUBE;
    bool is_mat = type == RSMSL_MAT2 || type == RSMSL_MAT3 || type == RSMSL_MAT4;
    int want = rsm_uniform_components(type);
    // GL: the call's type must match (bool accepts both).
    bool ok = comps == want && (kind == U_MAT) == is_mat &&
              (type == RSMSL_BOOL || (kind == U_INT) == is_int || is_mat);
    int elem = loc - u->loc;
    if (!ok || (count > 1 && u->u.array_size == 1)) {
        rsm_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    int n = count < u->u.array_size - elem ? count : u->u.array_size - elem;
    if (is_sampler || is_int) {
        const GLint *iv = data;
        for (int i = 0; i < n && comps == 1; i++)
            p->int_values[loc + i] = kind == U_INT ? iv[i] : (int)((const GLfloat *)data)[i];
        if (is_sampler) {
            ctx->dirty |= RSM_DIRTY_TEXTURES;
            return;
        }
    }
    for (int st = 0; st < 2; st++) {
        int off = st ? u->u.fs_offset : u->u.vs_offset;
        if (off < 0)
            continue;
        uint8_t *block = st ? p->fs_block : p->vs_block;
        int size = st ? p->fs_size : p->vs_size;
        for (int i = 0; i < n; i++) {
            int o = off + (elem + i) * u->u.stride;
            if (is_mat) {
                int dim = type == RSMSL_MAT2 ? 2 : type == RSMSL_MAT3 ? 3 : 4;
                int col = dim == 2 ? 8 : 16;
                const GLfloat *m = (const GLfloat *)data + (size_t)i * (size_t)(dim * dim);
                if (o + dim * col > size)
                    break;
                for (int c = 0; c < dim; c++) {
                    float v[4] = {0};
                    for (int r = 0; r < dim; r++)
                        v[r] = transpose ? m[r * dim + c] : m[c * dim + r];
                    memcpy(block + o + c * col, v, (size_t)dim * 4);
                }
            } else {
                if (o + comps * 4 > size)
                    break;
                const uint8_t *src = (const uint8_t *)data + (size_t)i * (size_t)comps * 4;
                if (type == RSMSL_BOOL || ((type == RSMSL_INT || type == RSMSL_IVEC2 || type == RSMSL_IVEC3 ||
                                            type == RSMSL_IVEC4) && kind == U_FLOAT)) {
                    int32_t iv[4];
                    for (int c = 0; c < comps; c++)
                        iv[c] = kind == U_INT ? ((const GLint *)src)[c] : ((const GLfloat *)src)[c] != 0;
                    memcpy(block + o, iv, (size_t)comps * 4);
                } else {
                    memcpy(block + o, src, (size_t)comps * 4);
                }
            }
        }
        if (st)
            p->fs_dirty = true;
        else
            p->vs_dirty = true;
    }
    ctx->dirty |= RSM_DIRTY_UNIFORMS;
}

void rsmtl_glUniform1f(GLint location, GLfloat v0) { rsm_set_uniform(location, 1, U_FLOAT, 1, &v0, false); }
void rsmtl_glUniform2f(GLint location, GLfloat v0, GLfloat v1) {
    GLfloat v[2] = {v0, v1};
    rsm_set_uniform(location, 1, U_FLOAT, 2, v, false);
}
void rsmtl_glUniform3f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2) {
    GLfloat v[3] = {v0, v1, v2};
    rsm_set_uniform(location, 1, U_FLOAT, 3, v, false);
}
void rsmtl_glUniform4f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3) {
    GLfloat v[4] = {v0, v1, v2, v3};
    rsm_set_uniform(location, 1, U_FLOAT, 4, v, false);
}
void rsmtl_glUniform1fv(GLint l, GLsizei c, const GLfloat *v) { rsm_set_uniform(l, c, U_FLOAT, 1, v, false); }
void rsmtl_glUniform2fv(GLint l, GLsizei c, const GLfloat *v) { rsm_set_uniform(l, c, U_FLOAT, 2, v, false); }
void rsmtl_glUniform3fv(GLint l, GLsizei c, const GLfloat *v) { rsm_set_uniform(l, c, U_FLOAT, 3, v, false); }
void rsmtl_glUniform4fv(GLint l, GLsizei c, const GLfloat *v) { rsm_set_uniform(l, c, U_FLOAT, 4, v, false); }
void rsmtl_glUniform1i(GLint location, GLint v0) { rsm_set_uniform(location, 1, U_INT, 1, &v0, false); }
void rsmtl_glUniform1iv(GLint l, GLsizei c, const GLint *v) { rsm_set_uniform(l, c, U_INT, 1, v, false); }
void rsmtl_glUniform2iv(GLint l, GLsizei c, const GLint *v) { rsm_set_uniform(l, c, U_INT, 2, v, false); }
void rsmtl_glUniform3iv(GLint l, GLsizei c, const GLint *v) { rsm_set_uniform(l, c, U_INT, 3, v, false); }
void rsmtl_glUniform4iv(GLint l, GLsizei c, const GLint *v) { rsm_set_uniform(l, c, U_INT, 4, v, false); }
void rsmtl_glUniformMatrix2fv(GLint l, GLsizei c, GLboolean t, const GLfloat *v) { rsm_set_uniform(l, c, U_MAT, 4, v, t); }
void rsmtl_glUniformMatrix3fv(GLint l, GLsizei c, GLboolean t, const GLfloat *v) { rsm_set_uniform(l, c, U_MAT, 9, v, t); }
void rsmtl_glUniformMatrix4fv(GLint l, GLsizei c, GLboolean t, const GLfloat *v) { rsm_set_uniform(l, c, U_MAT, 16, v, t); }

void rsmtl_glGetUniformfv(GLuint program, GLint location, GLfloat *params) {
    RSM_CTX()
    RSMProgram *p = rsm_program(ctx, program);
    if (!p)
        return;
    if (!p->linked || location < 0 || location >= p->n_locations) {
        rsm_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    rsm_uniform *u = &p->uniforms[p->loc_uniform[location]];
    int elem = location - u->loc, type = u->u.type;
    int comps = rsm_uniform_components(type);
    if (type == RSMSL_SAMPLER_2D || type == RSMSL_SAMPLER_CUBE) {
        params[0] = (GLfloat)p->int_values[location];
        return;
    }
    bool vs = u->u.vs_offset >= 0;
    const uint8_t *src = (vs ? p->vs_block : p->fs_block) + (vs ? u->u.vs_offset : u->u.fs_offset) +
                         elem * u->u.stride;
    bool is_int = rsm_uniform_is_int(type);
    if (type == RSMSL_MAT2 || type == RSMSL_MAT3 || type == RSMSL_MAT4) {
        int dim = type == RSMSL_MAT2 ? 2 : type == RSMSL_MAT3 ? 3 : 4, col = dim == 2 ? 8 : 16;
        for (int c = 0; c < dim; c++)
            memcpy(params + c * dim, src + c * col, (size_t)dim * 4);
        return;
    }
    for (int i = 0; i < comps; i++)
        params[i] = is_int ? (GLfloat)((const int32_t *)src)[i] : ((const float *)src)[i];
}
