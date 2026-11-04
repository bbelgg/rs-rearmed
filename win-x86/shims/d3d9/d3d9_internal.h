// Direct3D 9 on libRSOpenGL for the 32-bit Windows guest: shared internals.
//
// Layout of the layer (docs/windows/DESIGN.md "Direct3D 9"):
//   d3d9_thunk.c     guest-callable COM vtables, object table, freeable guest memory
//   d3d9_main.c      Direct3DCreate9, IDirect3D9 (one adapter, caps, formats, modes)
//   d3d9_device.c    IDirect3DDevice9: state, draws, render targets, Present
//   d3d9_resource.c  textures, surfaces, vertex/index buffers, declarations, shaders,
//                    queries, state blocks, the swap chain
//   d3d9_shader.c    SM2/SM3 bytecode -> GLSL 1.20 (libRSOpenGL then takes it to MSL)
//   d3d9_ffp.c       the fixed-function pipeline: transforms, lights, material, texture stages
//                    and fog as state, and GLSL generated from that state for draws without a
//                    vertex or pixel shader (docs/windows/DESIGN.md "Direct3D 9")
//
// Conventions:
//   - Every render target keeps D3D's row order: GL row 0 is the D3D top row. The vertex
//     epilogue flips y (d3d9_shader.h), so textures, render-to-texture and readbacks need no
//     flipping; only Present flips, drawing the back buffer onto the window's framebuffer 0.
//   - The device's GL context is made current on whatever thread calls in, under the device
//     lock (D3DCREATE_MULTITHREADED behaviour for every device).
//   - D3D state is recorded on the host and applied to GL at draw / clear time (dirty bits),
//     so the layer's own draws (Present, StretchRect) can clobber GL freely.
#ifndef D3D9_INTERNAL_H
#define D3D9_INTERNAL_H

#include <pthread.h>
#include <stdint.h>

#include "d3d9_defs.h"
#include "d3d9_gl.h"
#include "d3d9_shader.h"
#include "d3d9_thunk.h"

typedef struct rs_gfx_context rs_gfx_context;

enum {
    D3D9K_D3D = 1, D3D9K_DEVICE, D3D9K_TEXTURE, D3D9K_CUBE, D3D9K_SURFACE, D3D9K_VB, D3D9K_IB,
    D3D9K_DECL, D3D9K_VS, D3D9K_PS, D3D9K_QUERY, D3D9K_STATEBLOCK, D3D9K_SWAPCHAIN,
};

typedef struct d3d9_device d3d9_device;
typedef struct d3d9_texture d3d9_texture;

// A surface: a level (and face) of a texture, a render target, a depth stencil, an offscreen
// plain surface or the back buffer.
typedef struct {
    d3d9_obj o;
    d3d9_device *dev;
    uint32_t format, usage, pool, width, height, ms;
    d3d9_texture *container;  // 0: standalone
    int level, face;
    GLuint tex;               // colour: texture backing (render targets, back buffer)
    GLuint rb;                // depth stencil: renderbuffer
    uint32_t mem;             // guest memory for locks (lockable / system memory), 0 until locked
    uint32_t mem_size, pitch;
    int locked, lock_ro;
    uint32_t lock_rect[4];
} d3d9_surface;

struct d3d9_texture {
    d3d9_obj o;
    d3d9_device *dev;
    int cube;
    uint32_t format, usage, pool, width, height, levels;
    GLuint tex;
    GLenum target;
    d3d9_surface *surf[6][16];  // [face][level], created on demand
    // GL sampler state last applied to this texture (D3D keeps it per sampler, GL per texture)
    GLint s_wrap_s, s_wrap_t, s_min, s_mag, s_maxlevel;
    float s_aniso, s_bias;
    int dirty_mips;   // AUTOGENMIPMAP: regenerate before sampling
    uint32_t lod;
};

typedef struct {
    d3d9_obj o;
    d3d9_device *dev;
    int is_index;
    uint32_t size, usage, pool, fvf, format;
    GLuint buf;
    uint32_t mem;            // guest copy of the contents (Lock hands out pointers into it)
    int locks;
    uint32_t dirty_lo, dirty_hi;
    int discard;             // a lock since the last upload had D3DLOCK_DISCARD
    // dynamic buffers upload at the draw, only what it reads (d3d9_buffer_sync): bytes written
    // since they were last uploaded lie in [pend_lo, pend_hi); the ranges uploaded since the last
    // lock are clean[] (a ring)
    uint32_t pend_lo, pend_hi;
    uint32_t clean_lo[8], clean_hi[8];
    int nclean;
} d3d9_buffer;

// A draw is about to read bytes [lo, hi) of b: upload what of them the guest wrote since (under
// the device lock; dynamic buffers only, a no-op for the others and with LP_D3D9_EAGER=1).
void d3d9_buffer_sync(d3d9_buffer *b, uint64_t lo, uint64_t hi);
static inline int d3d9_buffer_pending(const d3d9_buffer *b) { return b && b->pend_hi > b->pend_lo; }

// Fixed-function state indices: D3DTS_VIEW, _PROJECTION, _TEXTURE0..7, _WORLD.._WORLD3.
enum { D3D9_XF_VIEW, D3D9_XF_PROJ, D3D9_XF_TEX0, D3D9_XF_WORLD = D3D9_XF_TEX0 + 8, D3D9_XF_COUNT = D3D9_XF_WORLD + 4 };
#define D3D9_MAX_LIGHTS 16  // light indices the device keeps (D3D allows any DWORD)

typedef struct {
    uint16_t stream, offset;
    uint8_t type, method, usage, index;
} d3d9_velem;

typedef struct {
    d3d9_obj o;
    d3d9_device *dev;
    int n;
    d3d9_velem e[64];
    uint32_t fvf;  // nonzero: made from an FVF
} d3d9_decl;

typedef struct {
    d3d9_obj o;
    d3d9_device *dev;
    d3d9_sm *sm;
    char *glsl;      // pixel shaders: translated once; vertex shaders depend on the pixel shader
    int failed;
    uint32_t id;
} d3d9_shader;

typedef struct {
    d3d9_obj o;
    d3d9_device *dev;
    uint32_t type;
    int issued;
} d3d9_query;

// Everything a state block can capture.
typedef struct {
    uint32_t rs[D3D9_RS_COUNT];
    uint32_t ss[D3D9_MAX_SAMPLERS][D3D9_SS_COUNT];
    d3d9_obj *tex[D3D9_MAX_SAMPLERS];   // texture or cube texture
    d3d9_shader *vs, *ps;
    d3d9_decl *decl;
    uint32_t fvf;
    d3d9_buffer *vb[D3D9_MAX_STREAMS];
    uint32_t vb_off[D3D9_MAX_STREAMS], vb_stride[D3D9_MAX_STREAMS], vb_freq[D3D9_MAX_STREAMS];
    d3d9_buffer *ib;
    float vsc[256][4], psc[224][4];
    int vsi[16][4], psi[16][4];
    int vsb[16], psb[16];
    uint32_t viewport[6];  // X Y W H MinZ MaxZ (floats as bits for the last two)
    int32_t scissor[4];
    uint32_t tss[8][33];   // texture stage states (D3DTSS_*)
    // fixed function (d3d9_ffp.c)
    float xf[D3D9_XF_COUNT][16];        // D3D9_XF_* matrices, row-major as D3D gives them
    float mat[17];                      // D3DMATERIAL9: diffuse, ambient, specular, emissive, power
    uint32_t light[D3D9_MAX_LIGHTS][26];  // D3DLIGHT9 as given
    uint8_t light_set[D3D9_MAX_LIGHTS], light_on[D3D9_MAX_LIGHTS];
    float clip[6][4];
} d3d9_state;

// Which parts a state block holds.
typedef struct {
    uint8_t rs[D3D9_RS_COUNT];
    uint8_t ss[D3D9_MAX_SAMPLERS][D3D9_SS_COUNT];
    uint8_t tex[D3D9_MAX_SAMPLERS];
    uint8_t vs, ps, decl, fvf, ib, viewport, scissor;
    uint8_t vb[D3D9_MAX_STREAMS];
    uint8_t vsc[256], psc[224], vsi[16], psi[16], vsb[16], psb[16];
    uint8_t tss[8][33], xf[D3D9_XF_COUNT], mat, light[D3D9_MAX_LIGHTS], clip[6];
} d3d9_state_mask;

typedef struct {
    d3d9_obj o;
    d3d9_device *dev;
    d3d9_state s;
    d3d9_state_mask m;
} d3d9_stateblock;

// A linked GL program for a (vertex shader, pixel shader) pair, or one generated from
// fixed-function state for a missing stage (d3d9_ffp.c).
typedef struct d3d9_program {
    uint32_t vs_id, ps_id;
    GLuint prog;
    int failed;
    GLint loc_vc, loc_pc, loc_fix, loc_vi, loc_pi, loc_vb, loc_pb;
    int nvc, npc;
    uint64_t vc_gen, pc_gen, ic_gen;  // the constant generations last uploaded
    float fix[4];
    int attr_used[16];
    d3d9_sm_input attr[16];
    int sampler_type[16];             // the units the pixel stage samples: 0 none, 2 2D, 3 cube
    int ffp;                          // a stage is fixed function: rs_ffv / rs_ffp uniforms
    GLint loc_ffv, loc_ffp;
    float *ff_last;                   // the fixed-function uniforms last uploaded (FFV + FFP vec4s)
    struct d3d9_program *next;
} d3d9_program;

struct d3d9_device {
    d3d9_obj o;
    pthread_mutex_t mu;
    rs_gfx_context *ctx;
    uint32_t d3d;              // the IDirect3D9 guest object that made it
    uint32_t adapter, devtype, focus, behavior;
    uint32_t pp[14];           // D3DPRESENT_PARAMETERS as last applied
    d3d9_surface *backbuffer, *auto_ds;
    d3d9_obj *swapchain;
    d3d9_surface *rt[4], *ds;
    d3d9_state st;
    d3d9_stateblock *recording;
    int in_scene;
    // GL objects
    GLuint fbo, up_buf, up_ibuf;
    GLuint blit_prog;
    GLint blit_loc_tex, blit_loc_rect;
    int fbo_dirty;
    // dirty groups (D3D9D_*)
    uint32_t dirty;
    uint64_t vc_gen, pc_gen, ic_gen;
    d3d9_program *programs[256];
    void *ffp_programs[64];    // d3d9_ffp.c's cache, by state key
    d3d9_program *cur_prog;
    uint32_t next_shader_id;
    int attr_enabled[16];
    int attr_divisor[16];
    int width, height;         // back buffer
};

enum {
    D3D9D_BLEND = 1, D3D9D_DEPTH = 2, D3D9D_STENCIL = 4, D3D9D_RASTER = 8, D3D9D_VIEWPORT = 16,
    D3D9D_SCISSOR = 32, D3D9D_ALPHATEST = 64, D3D9D_COLORMASK = 128, D3D9D_ALL = 0xff,
};

// d3d9_main.c
uint32_t d3d9_create(uint32_t sdk);
void d3d9_fill_caps(uint8_t *caps);  // a D3DCAPS9 (304 bytes)
int d3d9_format_bpp(uint32_t fmt);  // bits per pixel (block formats: per pixel too), 0 unknown
int d3d9_format_is_block(uint32_t fmt);

// d3d9_device.c
uint32_t d3d9_device_create(uint32_t d3d, const uint32_t *a, uint32_t pp_guest, uint32_t *out);
void d3d9_dev_lock(d3d9_device *d);    // takes the lock and makes the context current
void d3d9_dev_unlock(d3d9_device *d);
void d3d9_dev_unbind(d3d9_device *d, d3d9_obj *o);  // drop every binding of a resource
// draw a texture into the current framebuffer: rect = dst in NDC (x0 y0 x1 y1), src texcoords
void d3d9_dev_blit(d3d9_device *d, GLuint tex, const float dst[4], const float src[4], int linear);
void d3d9_dev_bind_target(d3d9_device *d, d3d9_surface *color, d3d9_surface *depth);
void d3d9_state_defaults(d3d9_device *d, d3d9_state *s);
GLuint d3d9_gl_link(const char *vs, const char *fs, void (*bind)(GLuint, void *), void *arg, char *log, size_t loglen);
GLint d3d9_gl_uloc(GLuint p, const char *name);  // name, else name[0]
extern int d3d9_program_failures;

// d3d9_ffp.c
// The program for a draw with no vertex shader or no pixel shader (or neither), from the current
// fixed-function state (the bound shader, if any, for the other stage); failed set when it cannot
// be made.
d3d9_program *d3d9_ffp_program(d3d9_device *d, d3d9_decl *decl);
void d3d9_ffp_upload(d3d9_device *d, d3d9_program *p);  // its uniforms, before the draw
void d3d9_ffp_defaults(d3d9_state *s);
uint64_t d3d9_ffp_set_transform(rc_ctx *c, uint32_t self, const uint32_t *a);
uint64_t d3d9_ffp_get_transform(rc_ctx *c, uint32_t self, const uint32_t *a);
uint64_t d3d9_ffp_multiply_transform(rc_ctx *c, uint32_t self, const uint32_t *a);
uint64_t d3d9_ffp_set_material(rc_ctx *c, uint32_t self, const uint32_t *a);
uint64_t d3d9_ffp_get_material(rc_ctx *c, uint32_t self, const uint32_t *a);
uint64_t d3d9_ffp_set_light(rc_ctx *c, uint32_t self, const uint32_t *a);
uint64_t d3d9_ffp_get_light(rc_ctx *c, uint32_t self, const uint32_t *a);
uint64_t d3d9_ffp_light_enable(rc_ctx *c, uint32_t self, const uint32_t *a);
uint64_t d3d9_ffp_get_light_enable(rc_ctx *c, uint32_t self, const uint32_t *a);
uint64_t d3d9_ffp_set_clip_plane(rc_ctx *c, uint32_t self, const uint32_t *a);
uint64_t d3d9_ffp_get_clip_plane(rc_ctx *c, uint32_t self, const uint32_t *a);

// d3d9_resource.c
d3d9_surface *d3d9_surface_new(d3d9_device *d, uint32_t w, uint32_t h, uint32_t fmt, uint32_t usage,
                               uint32_t pool, uint32_t ms);
uint32_t d3d9_create_texture(d3d9_device *d, const uint32_t *a, int cube, uint32_t *out);
uint32_t d3d9_create_buffer(d3d9_device *d, int is_index, uint32_t len, uint32_t usage, uint32_t fmt_or_fvf,
                            uint32_t pool, uint32_t *out);
uint32_t d3d9_create_decl(d3d9_device *d, uint32_t elems, uint32_t *out);
d3d9_decl *d3d9_decl_from_fvf(d3d9_device *d, uint32_t fvf);
uint32_t d3d9_create_shader(d3d9_device *d, int ps, uint32_t func, uint32_t *out);
uint32_t d3d9_create_query(d3d9_device *d, uint32_t type, uint32_t *out);
uint32_t d3d9_create_stateblock(d3d9_device *d, uint32_t type, uint32_t *out);
d3d9_stateblock *d3d9_stateblock_new(d3d9_device *d);
uint32_t d3d9_swapchain_get(d3d9_device *d);
void d3d9_texture_apply_sampler(d3d9_texture *t, const uint32_t *ss);
void d3d9_surface_read(d3d9_surface *s, uint8_t *dst, uint32_t pitch);  // GPU -> D3D format rows
void d3d9_surface_upload_mem(d3d9_surface *s);                          // guest copy -> GPU
uint32_t d3d9_surface_ensure_mem(d3d9_surface *s);
void d3d9_tex_format(uint32_t fmt, GLint *internal, GLenum *format, GLenum *type, int *convert);
// rows of fmt in D3D layout -> what d3d9_tex_format asked GL for (0: none needed)
void *d3d9_convert_rows(uint32_t fmt, const uint8_t *src, uint32_t pitch, uint32_t w, uint32_t h);
uint32_t d3d9_pitch(uint32_t fmt, uint32_t w);
uint32_t d3d9_level_size(uint32_t fmt, uint32_t w, uint32_t h);

// The window seam: a context presenting to the guest window `hwnd` (a CAMetalLayer-backed one
// from the platform shell). Weak default: an offscreen context (headless, tests).
rs_gfx_context *d3d9_platform_context(uint32_t hwnd, int w, int h);
void d3d9_platform_resize(uint32_t hwnd, int w, int h);
void d3d9_platform_windowed(int windowed);

#endif
