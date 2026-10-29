// Metal core of libRSOpenGL (docs/METAL-DESIGN.md "Metal core"): shared types.
//
// One RSMContext per rs_gfx context, current per thread. GL objects are ObjC
// objects held in per-type name tables; GL state is plain C in the context.
// Every rsmtl_<name> here replaces the generated weak "unimplemented" stub of
// the same name when RS_GL_BACKEND=metal.
#pragma once
#define GL_SILENCE_DEPRECATION
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

#include "gl_api.h"
#include "rs_gfx.h"
#include "rsmtl_gfx.h"
#include "msl/msl_xlat.h"

// gl_api.h declares rsmtl_<name> for every table entry (game imports +
// extra-gl-imports.txt) with the SDK's signature.

#ifndef GL_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_TEXTURE_MAX_ANISOTROPY_EXT 0x84FE
#endif
#ifndef GL_PROGRAM_BINARY_RETRIEVABLE_HINT
#define GL_PROGRAM_BINARY_RETRIEVABLE_HINT 0x8257
#endif
#ifndef GL_PROGRAM_BINARY_LENGTH
#define GL_PROGRAM_BINARY_LENGTH 0x8741
#endif
#ifndef GL_NUM_PROGRAM_BINARY_FORMATS
#define GL_NUM_PROGRAM_BINARY_FORMATS 0x87FE
#endif
#ifndef GL_PROGRAM_BINARY_FORMATS
#define GL_PROGRAM_BINARY_FORMATS 0x87FF
#endif
#ifndef GL_NUM_EXTENSIONS
#define GL_NUM_EXTENSIONS 0x821D
#endif
#ifndef GL_MAJOR_VERSION
#define GL_MAJOR_VERSION 0x821B
#define GL_MINOR_VERSION 0x821C
#endif
#ifndef GL_MAX_VERTEX_UNIFORM_VECTORS
#define GL_MAX_VERTEX_UNIFORM_VECTORS 0x8DFB
#define GL_MAX_FRAGMENT_UNIFORM_VECTORS 0x8DFD
#define GL_MAX_VARYING_VECTORS 0x8DFC
#endif
#ifndef GL_VERTEX_ARRAY_BINDING
#define GL_VERTEX_ARRAY_BINDING 0x85B5
#endif
#ifndef GL_DEPTH_STENCIL_ATTACHMENT
#define GL_DEPTH_STENCIL_ATTACHMENT 0x821A
#endif
#ifndef GL_HALF_FLOAT
#define GL_HALF_FLOAT 0x140B
#endif

enum {
    RSM_MAX_ATTRIBS = 16,
    RSM_MAX_UNITS = 16,       // GL_MAX_TEXTURE_IMAGE_UNITS (Apple: 16)
    RSM_VB_BASE = 14,         // vertex buffer index of attribute 0: 14..29 (0 uniforms, 1 alpha ref, 30 probe)
    RSM_FRAMES_IN_FLIGHT = 2,
};

// --- Name tables ----------------------------------------------------------------
// GLuint -> retained ObjC object (CFBridgingRetain). Names come from Gen*; binding
// an ungenerated name creates the object, as GL 2.1 allows.
typedef struct {
    void **objs;
    uint32_t cap;
    uint32_t next;  // lowest name that may be free
} rsm_names;

static inline id rsm_get(const rsm_names *t, GLuint n) {
    return n && n < t->cap ? (__bridge id)t->objs[n] : nil;
}
GLuint rsm_names_alloc(rsm_names *t);
void rsm_names_set(rsm_names *t, GLuint n, id obj);  // nil frees the name
void rsm_names_clear(rsm_names *t);

// --- Byte-keyed hash map: fixed-size keys -> retained objects ---------------------
typedef struct {
    uint32_t key_size, cap, count;
    uint8_t *keys;
    uint64_t *hashes;  // 0 = empty
    void **vals;
} rsm_map;
void rsm_map_init(rsm_map *m, uint32_t key_size);
id rsm_map_get(rsm_map *m, const void *key);
void rsm_map_put(rsm_map *m, const void *key, id val);
void rsm_map_free(rsm_map *m);
uint64_t rsm_hash(const void *p, size_t n);

// --- Objects -------------------------------------------------------------------
@interface RSMBuffer : NSObject {
@public
    id<MTLBuffer> mtl;
    size_t size;
    GLenum usage;
    uint64_t last_use;  // serial of the last command buffer that read it
    // Byte ranges the GPU reads (draws binding the buffer directly): [rd_lo, rd_hi) by command
    // buffer rd_serial, [old_lo, old_hi) by older ones up to old_serial. A write outside both
    // needs no rename even while the buffer is in flight (rsm_buffer_gpu_read).
    uint64_t rd_serial, old_serial;
    size_t rd_lo, rd_hi, old_lo, old_hi;
    bool streamed;      // written while in flight: indexed draws work out the vertex range they read
    bool mapped;
    GLenum map_access;
    bool bound;
}
@end

typedef struct {
    GLenum min_filter, mag_filter, wrap_s, wrap_t, wrap_r;
    GLenum compare_mode, compare_func;
    float max_aniso, min_lod, max_lod;
    GLint base_level, max_level;
} rsm_texparams;

@interface RSMTexture : NSObject {
@public
    GLenum target;     // 0 until first bound
    id<MTLTexture> mtl;   // storage: uploads, render target, reads
    id<MTLTexture> view;  // swizzled view for sampling (luminance, RGB, ...), lazily
    MTLPixelFormat fmt;
    GLenum internal;   // as given to glTexImage2D / glCompressedTexImage2D
    int w, h;          // level 0
    int levels;        // allocated
    uint32_t defined;  // levels with data or storage (bit per level)
    int swz;           // rsm_swizzle_*
    rsm_texparams p;
    id<MTLSamplerState> sampler;  // nil: rebuild from p
    uint64_t last_use;
    bool bound;
}
@end

@interface RSMRenderbuffer : NSObject {
@public
    GLenum internal;
    int w, h;
    id<MTLTexture> mtl;
    bool bound;
}
@end

enum { RSM_ATT_COLOR0, RSM_ATT_DEPTH, RSM_ATT_STENCIL, RSM_ATT_COUNT };
typedef struct {
    GLenum type;  // 0, GL_TEXTURE, GL_RENDERBUFFER
    GLuint name;
    GLint level;
    GLint face;   // cube face / slice
    // The object itself (RSMTexture / RSMRenderbuffer): GL keeps a deleted object alive while a
    // framebuffer that isn't bound still has it attached, and its name may go to a new object.
    __strong id obj;
} rsm_attachment;

@interface RSMFramebuffer : NSObject {
@public
    rsm_attachment att[RSM_ATT_COUNT];
    bool bound;
}
@end

typedef struct {
    bool enabled;
    GLint size;        // 1..4 or GL_BGRA
    GLenum type;
    bool normalized;
    GLsizei stride;    // as given (0 = packed)
    const void *ptr;   // offset into buffer, or client pointer when buffer == 0
    GLuint buffer;
    __strong RSMBuffer *bo;  // the object (a deleted buffer stays alive while a VAO uses it)
    GLuint divisor;
} rsm_attrib;

@interface RSMVertexArray : NSObject {
@public
    rsm_attrib a[RSM_MAX_ATTRIBS];
    GLuint element_buffer;
    RSMBuffer *element_obj;
    bool bound;
}
@end

@interface RSMShader : NSObject {
@public
    GLenum type;
    NSString *source;
    bool compiled;
    bool deleted;
    GLuint name;
    int attached;  // programs it is attached to: a deleted shader keeps its name until 0
}
@end

// A uniform as the program presents it: location range [loc, loc + count).
typedef struct {
    rsmsl_uniform u;
    char *name;
    int loc;
} rsm_uniform;

// A texture binding: MSL slot <- unit stored in the sampler uniform's value.
typedef struct {
    int slot;
    int stages;
    int type;        // RSMSL_SAMPLER_*
    int value_loc;   // location whose int value is the unit
} rsm_texbind;

@class RSMLibEntry;

@interface RSMProgram : NSObject {
@public
    NSMutableArray<RSMShader *> *shaders;
    // glBindAttribLocation calls (name -> index), used at link.
    NSMutableDictionary<NSString *, NSNumber *> *bound_attribs;
    bool linked;
    bool deleted;
    GLuint name;           // a deleted program keeps its name while current
    NSString *log;
    uint64_t uid;          // unique per link: pipeline keys and "program changed"
    // The MSL's library, shared by every program with the same MSL (mtl_cache.m). Linking only
    // queues its compile; the first draw waits for it (rsm_program_ready).
    RSMLibEntry *entry;
    bool ready;            // entry compiled and usable (checked once)
    rsmsl_program *refl;
    // Active attributes: name -> location (glGetAttribLocation) and the mask.
    uint32_t attrib_mask;
    rsm_uniform *uniforms;
    int n_uniforms;
    int n_locations;
    int *loc_uniform;   // location -> index in uniforms
    int *int_values;    // location -> int value (sampler units)
    rsm_texbind *tex;
    int n_tex;
    uint8_t *vs_block, *fs_block;
    int vs_size, fs_size;
    bool vs_dirty, fs_dirty;
    rsm_map pipelines;  // rsm_pkey -> MTLRenderPipelineState
}
@end

// --- Pipeline key --------------------------------------------------------------
enum { RSM_STEP_VERTEX = 0, RSM_STEP_INSTANCE = 1, RSM_STEP_CONSTANT = 2 };
typedef struct {
    uint8_t fmt[RSM_MAX_ATTRIBS];     // MTLVertexFormat, 0 = unused by the program
    uint8_t step[RSM_MAX_ATTRIBS];
    uint8_t offset[RSM_MAX_ATTRIBS];  // attribute offset inside the binding (alignment fix-ups)
    uint16_t stride[RSM_MAX_ATTRIBS];
    uint32_t rate[RSM_MAX_ATTRIBS];
} rsm_vlayout;

typedef struct {
    rsm_vlayout vl;
    uint16_t color_fmt, depth_fmt, stencil_fmt;
    uint8_t blend, color_mask, alpha_func;
    uint8_t src_rgb, dst_rgb, src_a, dst_a, eq_rgb, eq_a;
    uint8_t pad[3];
} rsm_pkey;

// --- Context -----------------------------------------------------------------
enum {
    RSM_DIRTY_PIPELINE = 1u << 0,
    RSM_DIRTY_DEPTH = 1u << 1,
    RSM_DIRTY_VIEWPORT = 1u << 2,
    RSM_DIRTY_SCISSOR = 1u << 3,
    RSM_DIRTY_RASTER = 1u << 4,   // cull, winding, fill mode
    RSM_DIRTY_STENCIL_REF = 1u << 5,
    RSM_DIRTY_BLEND_COLOR = 1u << 6,
    RSM_DIRTY_VERTEX = 1u << 7,   // attribute layout or bindings
    RSM_DIRTY_TEXTURES = 1u << 8,
    RSM_DIRTY_UNIFORMS = 1u << 9,
    RSM_DIRTY_ALPHA_REF = 1u << 10,
    RSM_DIRTY_ALL = 0xFFFFu,
};

typedef struct {
    GLenum func;
    GLint ref;
    GLuint mask;
    GLenum fail, zfail, zpass;
    GLuint writemask;
} rsm_stencil_face;

typedef struct {
    void *buf;        // retained id<MTLBuffer>
    uint64_t serial;  // last command buffer that used it
} rsm_chunk;
#define RSM_CHUNK_BUF(c) ((__bridge id<MTLBuffer>)(c).buf)

@interface RSMContext : NSObject {
@public
    id<MTLDevice> dev;
    id<MTLCommandQueue> queue;
    id<MTLCommandBuffer> cmd;       // being recorded (nil until needed)
    id<MTLCommandBuffer> last_cmd;  // last committed
    id<MTLRenderCommandEncoder> enc;
    uint64_t serial;               // serial of the command buffer being recorded
    _Atomic uint64_t completed;    // highest completed serial
    dispatch_semaphore_t frames;   // frames in flight
    pthread_mutex_t lock;          // CGLLockContext
    MTLResourceOptions buf_opts;   // shared (unified memory) or managed
    MTLStorageMode tex_storage;

    // Default framebuffer.
    CAMetalLayer *layer;
    int w, h;
    id<MTLTexture> def_color, def_ds;

    rsm_names buffers, textures, renderbuffers, framebuffers, vaos, objects /*shaders+programs*/;
    RSMVertexArray *vao0;
    RSMVertexArray *vao;           // current (vao0 when 0 is bound)
    GLuint vao_name;

    // Bindings.
    GLuint array_buffer, pixel_pack_buffer, pixel_unpack_buffer;
    GLint unpack_client_storage;
    GLuint draw_fb, read_fb, renderbuffer;
    GLuint tex2d[RSM_MAX_UNITS], texcube[RSM_MAX_UNITS];
    int active_unit, client_unit;
    RSMProgram *prog;
    GLuint prog_name;

    // Fixed state.
    bool blend, depth_test, stencil_test, scissor_test, cull, alpha_test, dither, poly_offset_fill;
    GLenum blend_src_rgb, blend_dst_rgb, blend_src_a, blend_dst_a, blend_eq_rgb, blend_eq_a;
    float blend_color[4];
    bool color_mask[4];
    bool depth_mask;
    GLenum depth_func;
    rsm_stencil_face stencil[2];   // front, back
    GLint viewport[4], scissor[4];
    float depth_range[2];
    GLenum cull_mode, front_face, polygon_mode[2];
    GLenum alpha_func;
    float alpha_ref;
    float clear_color[4];
    double clear_depth;
    GLint clear_stencil;
    float vattr[RSM_MAX_ATTRIBS][4];  // current generic attribute values
    GLint unpack_alignment, unpack_row_length, unpack_skip_rows, unpack_skip_pixels;
    GLint pack_alignment, pack_row_length, pack_skip_rows, pack_skip_pixels;
    GLenum error;

    // Render pass.
    bool fb_dirty;                 // draw framebuffer changed: next draw opens a new pass
    unsigned pending_clear;        // GL_*_BUFFER_BIT to apply as load actions
    float pending_color[4];
    double pending_depth;
    GLint pending_stencil;
    MTLPixelFormat pass_color_fmt, pass_depth_fmt, pass_stencil_fmt;
    int pass_w, pass_h;            // render target size of the open/next pass
    id<MTLTexture> pass_color, pass_depth, pass_stencil;
    int pass_color_level, pass_color_slice;

    // Draw state cache (valid while `enc` is open).
    unsigned dirty;
    id<MTLRenderPipelineState> cur_pipeline;
    rsm_pkey cur_pkey;
    bool cur_pkey_valid;
    uint64_t cur_prog_uid;         // program whose uniforms the encoder has
    __unsafe_unretained id<MTLBuffer> enc_vb[RSM_MAX_ATTRIBS];
    NSUInteger enc_vb_off[RSM_MAX_ATTRIBS];
    __unsafe_unretained id<MTLTexture> enc_tex[2][RSM_MAX_UNITS];
    __unsafe_unretained id<MTLSamplerState> enc_smp[2][RSM_MAX_UNITS];
    id<MTLDepthStencilState> cur_ds;
    bool skip_raster;              // cull FRONT_AND_BACK

    // Caches.
    rsm_map ds_states;   // uint64 -> MTLDepthStencilState
    rsm_map samplers;    // uint64 -> MTLSamplerState
    id<MTLLibrary> internal_lib;
    rsm_map clear_pipelines;  // formats + mask -> pipeline
    id<MTLRenderPipelineState> present_pipeline;
    id<MTLSamplerState> present_sampler;
    id<MTLTexture> dummy_tex, dummy_cube;
    uint64_t next_uid;

    // Transient memory (client arrays, converted indices, big uniform blocks).
    rsm_chunk cur_chunk;
    size_t chunk_off;
    rsm_chunk *spare;
    int n_spare, cap_spare;
    // Recycled MTLBuffers from renamed GL buffers.
    rsm_chunk *pool;
    int n_pool, cap_pool;
    size_t pool_bytes;
}
@end

extern __thread __unsafe_unretained RSMContext *rsm_tls;
// Unretained: the current context outlives any GL call made on it (no retain/release per call).
#define RSM_CTX(...)                       \
    __unsafe_unretained RSMContext *ctx = rsm_tls; \
    if (__builtin_expect(!ctx, 0))         \
        return __VA_ARGS__;

void rsm_error(RSMContext *ctx, GLenum e);
// GL_INVALID_ENUM for a value the core doesn't handle; logged (first 32) with RS_GL_VERBOSE=1.
void rsm_bad_enum(RSMContext *ctx, const char *fn, GLenum v);
void rsm_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

// Command buffers and passes (mtl_context.m / mtl_draw.m).
id<MTLCommandBuffer> rsm_cmd(RSMContext *ctx);
void rsm_end_pass(RSMContext *ctx);                // resolves pending clears
void rsm_commit(RSMContext *ctx, bool wait);       // ends the pass
static inline bool rsm_in_flight(RSMContext *ctx, uint64_t last_use) {
    return last_use > atomic_load_explicit(&ctx->completed, memory_order_acquire);
}
// *buf stays valid until the command buffer being recorded completes (the context holds it).
void *rsm_transient(RSMContext *ctx, size_t size, size_t align, __unsafe_unretained id<MTLBuffer> *buf,
                    NSUInteger *off);
// The GPU reads [lo, hi) of b in the command buffer being recorded.
void rsm_buffer_gpu_read(RSMContext *ctx, RSMBuffer *b, size_t lo, size_t hi);
id<MTLBuffer> rsm_new_buffer(RSMContext *ctx, size_t size);       // pooled
void rsm_retire_buffer(RSMContext *ctx, id<MTLBuffer> b, uint64_t serial);

// Objects (mtl_objects.m).
RSMBuffer *rsm_buffer(RSMContext *ctx, GLuint name);
// Client pointer of pixel data: an offset into the bound pixel pack/unpack buffer, if any.
// `bytes` is how much the call reads/writes from there; NULL (GL_INVALID_OPERATION recorded)
// when that runs past the buffer's end.
const void *rsm_unpack_ptr(RSMContext *ctx, const void *p, size_t bytes);
void *rsm_pack_ptr(RSMContext *ctx, void *p, size_t bytes);
RSMTexture *rsm_texture(RSMContext *ctx, GLuint name);
RSMRenderbuffer *rsm_renderbuffer(RSMContext *ctx, GLuint name);
void rsm_texture_sampler(RSMContext *ctx, RSMTexture *t);  // build t->sampler
id<MTLTexture> rsm_texture_view(RSMTexture *t);             // what shaders sample
// The attachment's texture / level / slice / pixel format. NO = none.
bool rsm_attachment_texture(RSMContext *ctx, const rsm_attachment *a, id<MTLTexture> *tex,
                            int *level, int *slice);
void rsm_fb_changed(RSMContext *ctx);  // bound draw fb's attachments changed
// Read back a region of a texture as tightly packed rows (row 0 = GL bottom).
// Returns malloc'd bytes in the texture's own format (bytes per pixel in *bpp).
void *rsm_read_texture(RSMContext *ctx, id<MTLTexture> tex, int level, int slice, int x, int y,
                       int w, int h, MTLBlitOption opt, size_t *bpp);
// Pixel conversion to the caller's (format, type) with the pack state.
void rsm_pack_pixels(RSMContext *ctx, const void *src, MTLPixelFormat sfmt, int swz, int w, int h,
                     GLenum format, GLenum type, void *dst);

// Programs (mtl_program.m).
// Wait for the program's library (wait = false: only look). false: the compile failed and the
// program is now unlinked (logged, LINK_STATUS false from here on).
bool rsm_program_ready(RSMContext *ctx, RSMProgram *p, bool wait);

// Shader and pipeline cache (mtl_cache.m): one per device, shared by its contexts. Libraries are
// keyed by their MSL, pipelines by (library, rsm_pkey); on disk an MTLBinaryArchive plus a
// manifest of what was built, prebuilt on a background queue at the next start.
@interface RSMLibEntry : NSObject {
@public
    NSString *msl;
    uint64_t hash;                // rsm_hash of the MSL bytes (manifest)
    id<MTLLibrary> lib;           // nil: compile failed (error)
    id<MTLFunction> vs;           // rs_vs, when present
    id<MTLFunction> fs[9];        // rs_fs per alpha-test variant (GL func - GL_NEVER, 8 = off)
    NSString *error;
    int state;                    // 0 new, 1 being built, 2 done, 3 queued on the compile pool
    bool link_queued;             // a link job for it is on the pool
    int fs_state[9];
    rsm_map pipes;                // rsm_pkey -> RSMPipeSlot
    bool record;                  // the game's program: goes into the manifest
}
@end
// Compile (or find) the library for this MSL. Never nil; ->lib is nil on failure.
// record: list it in the manifest (game programs; not the core's internal shaders).
RSMLibEntry *rsm_cache_library(RSMContext *ctx, const char *msl, bool record);
// The same without waiting: the compile is queued on the compile pool (RS_MTL_COMPILE_THREADS=0:
// compiled here). Read ->lib/->vs/->error only after rsm_cache_library_state != 0 or _wait.
RSMLibEntry *rsm_cache_library_async(RSMContext *ctx, const char *msl, bool record);
int rsm_cache_library_state(RSMLibEntry *e);  // 0 pending, 1 compiled, -1 failed (no waiting)
bool rsm_cache_library_wait(RSMContext *ctx, RSMLibEntry *e);  // compiled (builds it here if queued)
id<MTLRenderPipelineState> rsm_cache_pipeline(RSMContext *ctx, RSMLibEntry *e, const rsm_pkey *k);
void rsm_cache_context_created(RSMContext *ctx);    // first one: open the disk cache, start warming
void rsm_cache_context_destroyed(RSMContext *ctx);  // last one: write the archive
// rs_gfx_prepare: wait for warming (and the corpus warm, mtl_cache.m) on the whole pool (at most
// max_s), then throttle the pool. 0 everything warmed (or nothing to warm), 1 timed out.
int rsm_cache_prepare(RSMContext *ctx, double max_s);
// rs_gfx_prepare_progress: libraries compiled / to compile so far; 1 while rs_gfx_prepare runs.
int rsm_cache_progress(RSMContext *ctx, int *done, int *total);
// The descriptor for a program pipeline (mtl_draw.m).
MTLRenderPipelineDescriptor *rsm_pipeline_desc(id<MTLFunction> vs, id<MTLFunction> fs, const rsm_pkey *k);

// Draws (mtl_draw.m).
void rsm_draw_init(RSMContext *ctx);
void rsm_present_draw(RSMContext *ctx, id<MTLTexture> drawable);

enum { RSM_SWZ_NONE, RSM_SWZ_RGB1, RSM_SWZ_LUM, RSM_SWZ_ALPHA, RSM_SWZ_LUM_ALPHA,
       RSM_SWZ_INTENSITY, RSM_SWZ_RED, RSM_SWZ_RG };
