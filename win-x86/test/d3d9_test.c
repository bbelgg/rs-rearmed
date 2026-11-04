// Host test of the Direct3D 9 layer (win-x86/shims/d3d9): no game and no translated code. Every
// call goes the way a guest COM call does: arguments pushed on a guest stack in the 4 GB
// window, `this` last, a return address, then w32_hostfn_call on the vtable slot's address
// (what rc32_icall does for `call [vtbl + 4 * slot]`); each call checks the callee removed
// exactly its stdcall arguments.
//
//   d3d9_test                 device, textured quad, readback (exact pixels), Present flip,
//                             vs_3_0 / ps_3_0 constants, DrawIndexedPrimitive with a base
//                             vertex, a render-target texture, viewport, fixed-function draws
//                             (pre-transformed, transformed and textured, lit, fogged, and mixed
//                             with shaders); prints "N passed, 0 failed"
//   d3d9_test --corpus DIR    also create every shader blob in DIR (vs_*.bin / ps_*.bin, e.g. the
//                             ones embedded in the game image) and link each in a draw
#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "d3d9_internal.h"
#include "rs_gfx.h"

void shim_Direct3DCreate9(rc_ctx *c);
extern int d3d9_program_failures;
// no rc32_icall here: the test calls w32_hostfn_call itself
void rc32_hostfn_hook_add(rc32_hostfn_hook h) { (void)h; }
int w32_log;  // win32_guestobj.c's log level (win32_ksync.c has it in the game)
// win32_frame.c's LP_THREAD_CPU memory line (win32_kernel32.c and win32_ksync.c have them in the game)
unsigned long w32_valloc_calls, w32_valloc_bytes, w32_vfree_calls, w32_threads_made;
unsigned w32_handles_open(void) { return 0; }
// the layer never calls into the guest here (w32_guest_call)
uint32_t rc32_call(rc_ctx *c, uint32_t fn, const uint32_t *args, int nargs, uint32_t ecx, uint32_t edx,
                   uint32_t *hi) {
    (void)c; (void)args; (void)nargs; (void)ecx; (void)edx; (void)hi;
    fprintf(stderr, "rc32_call(0x%x) in the D3D9 test\n", fn);
    abort();
}

_Noreturn void rc_fail(const char *what, uint64_t addr) {
    fprintf(stderr, "rc_fail: %s at 0x%llx\n", what, (unsigned long long)addr);
    abort();
}

static int passed, failed;
static void check(int ok, const char *fmt, ...) {
    if (ok) { passed++; return; }
    failed++;
    va_list ap;
    va_start(ap, fmt);
    printf("FAIL ");
    vprintf(fmt, ap);
    printf("\n");
    va_end(ap);
}

// --- guest calls -------------------------------------------------------------------------------
static rc_ctx ctx, *c = &ctx;

static void push(uint32_t v) {
    c->r[4] = (uint32_t)(c->r[4] - 4);
    d3d9_wr32((uint32_t)c->r[4], v);
}

// obj->vtbl[slot](obj, args...)
static uint32_t com(uint32_t obj, int slot, int n, ...) {
    uint32_t a[16];
    va_list ap;
    va_start(ap, n);
    for (int k = 0; k < n; k++) a[k] = va_arg(ap, uint32_t);
    va_end(ap);
    uint32_t esp0 = (uint32_t)c->r[4];
    for (int k = n - 1; k >= 0; k--) push(a[k]);
    push(obj);
    push(0x0badc0deu);  // return address
    uint32_t fn = d3d9_rd32(d3d9_rd32(obj) + 4 * (uint32_t)slot);
    int took = w32_hostfn_call(c, fn);
    check(took, "slot %d of 0x%x is a thunk", slot, obj);
    check((uint32_t)c->r[4] == esp0, "slot %d removed its %d arguments (esp off by %d)", slot, n + 1,
          (int)((uint32_t)c->r[4] - esp0));
    check(c->ret_to == 0x0badc0deu, "slot %d return address", slot);
    c->r[4] = esp0;
    return (uint32_t)c->r[0];
}

static uint32_t galloc(uint32_t n) { return rc32_alloc(n); }
static uint32_t gptr(uint32_t g) { return d3d9_rd32(g); }
static uint32_t fbits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }

// --- interface slots used -----------------------------------------------------------------------
enum {
    D3D_Release = 2, D3D_GetAdapterCount = 4, D3D_GetAdapterIdentifier = 5, D3D_CheckDeviceFormat = 10,
    D3D_GetDeviceCaps = 14, D3D_CreateDevice = 16,
};
enum {
    DEV_Release = 2, DEV_Present = 17, DEV_GetBackBuffer = 18, DEV_CreateTexture = 23, DEV_CreateVertexBuffer = 26,
    DEV_CreateIndexBuffer = 27, DEV_GetRenderTargetData = 32, DEV_CreateOffscreenPlainSurface = 36,
    DEV_SetRenderTarget = 37, DEV_BeginScene = 41, DEV_EndScene = 42, DEV_Clear = 43, DEV_SetTransform = 44,
    DEV_GetTransform = 45, DEV_MultiplyTransform = 46, DEV_SetViewport = 47, DEV_SetMaterial = 49, DEV_SetLight = 51,
    DEV_GetLight = 52, DEV_LightEnable = 53, DEV_SetRenderState = 57, DEV_GetRenderState = 58,
    DEV_CreateStateBlock = 59, DEV_SetTexture = 65, DEV_SetTextureStageState = 67, DEV_SetSamplerState = 69,
    DEV_DrawPrimitive = 81, DEV_DrawIndexedPrimitive = 82, DEV_DrawPrimitiveUP = 83,
    DEV_CreateVertexDeclaration = 86, DEV_SetVertexDeclaration = 87, DEV_SetFVF = 89, DEV_CreateVertexShader = 91,
    DEV_SetVertexShader = 92, DEV_SetVertexShaderConstantF = 94, DEV_SetStreamSource = 100, DEV_SetIndices = 104,
    DEV_CreatePixelShader = 106, DEV_SetPixelShader = 107, DEV_SetPixelShaderConstantF = 109, DEV_CreateQuery = 118,
};
enum { RES_Release = 2, TEX_GetSurfaceLevel = 18, TEX_LockRect = 19, TEX_UnlockRect = 20 };
enum { SURF_GetDesc = 12, SURF_LockRect = 13, SURF_UnlockRect = 14 };
enum { BUF_Lock = 11, BUF_Unlock = 12 };
enum { SB_Capture = 4, SB_Apply = 5 };
enum { Q_Issue = 6, Q_GetData = 7 };

// --- shader assembly ----------------------------------------------------------------------------
#define REG(t, n) ((((uint32_t)(t) & 7) << 28) | (((uint32_t)(t) & 0x18) << 8) | (uint32_t)(n))
#define DST(t, n, m) (0x80000000u | REG(t, n) | ((uint32_t)(m) << 16))
#define SRC(t, n) (0x80000000u | REG(t, n) | (0xE4u << 16))
#define INS(op, len) ((uint32_t)(op) | ((uint32_t)(len) << 24))
#define DCL(usage, idx) (0x80000000u | (uint32_t)(usage) | ((uint32_t)(idx) << 16))
#define DCL_2D (0x80000000u | (2u << 27))
enum { T_TEMP = 0, T_INPUT = 1, T_CONST = 2, T_TEX = 3, T_RASTOUT = 4, T_OUT = 6, T_COLOROUT = 8, T_SAMPLER = 10 };

static const uint32_t vs20[] = {
    0xFFFE0200,
    INS(31, 2), DCL(0, 0), DST(T_INPUT, 0, 15),  // dcl_position v0
    INS(31, 2), DCL(5, 0), DST(T_INPUT, 1, 15),  // dcl_texcoord v1
    INS(1, 2), DST(T_RASTOUT, 0, 15), SRC(T_INPUT, 0),  // mov oPos, v0
    INS(1, 2), DST(T_OUT, 0, 15), SRC(T_INPUT, 1),      // mov oT0, v1
    0x0000FFFF,
};
static const uint32_t ps20[] = {
    0xFFFF0200,
    INS(31, 2), 0x80000000u, DST(T_TEX, 0, 15),  // dcl t0
    INS(31, 2), DCL_2D, DST(T_SAMPLER, 0, 15),   // dcl_2d s0
    INS(66, 3), DST(T_TEMP, 0, 15), SRC(T_TEX, 0), SRC(T_SAMPLER, 0),  // texld r0, t0, s0
    INS(1, 2), DST(T_COLOROUT, 0, 15), SRC(T_TEMP, 0),                 // mov oC0, r0
    0x0000FFFF,
};
static const uint32_t vs30[] = {
    0xFFFE0300,
    INS(31, 2), DCL(0, 0), DST(T_INPUT, 0, 15),  // dcl_position v0
    INS(31, 2), DCL(0, 0), DST(T_OUT, 0, 15),    // dcl_position o0
    INS(9, 3), DST(T_OUT, 0, 1), SRC(T_INPUT, 0), SRC(T_CONST, 0),  // dp4 o0.x, v0, c0
    INS(9, 3), DST(T_OUT, 0, 2), SRC(T_INPUT, 0), SRC(T_CONST, 1),  // dp4 o0.y, v0, c1
    INS(9, 3), DST(T_OUT, 0, 4), SRC(T_INPUT, 0), SRC(T_CONST, 2),  // dp4 o0.z, v0, c2
    INS(9, 3), DST(T_OUT, 0, 8), SRC(T_INPUT, 0), SRC(T_CONST, 3),  // dp4 o0.w, v0, c3
    0x0000FFFF,
};
static const uint32_t ps30[] = {
    0xFFFF0300,
    INS(81, 5), DST(T_CONST, 5, 15), 0x3f800000, 0x3f800000, 0x3f800000, 0x3f800000,  // def c5, 1, 1, 1, 1
    INS(5, 3), DST(T_TEMP, 0, 15), SRC(T_CONST, 0), SRC(T_CONST, 5),  // mul r0, c0, c5
    INS(1, 2), DST(T_COLOROUT, 0, 15), SRC(T_TEMP, 0),                // mov oC0, r0
    0x0000FFFF,
};

static uint32_t gcopy(const void *p, uint32_t n) {
    uint32_t g = galloc(n);
    memcpy(RC32_H(g), p, n);
    return g;
}

enum { W = 16, H = 8 };

static uint32_t texel(int x, int y) { return 0xFF000000u | (uint32_t)(0x40 + y * 20) << 16 | (uint32_t)(x * 15) << 8 | (uint32_t)(255 - x * 7 - y); }

static int near(uint32_t a, uint32_t b, int tol) {
    for (int k = 0; k < 32; k += 8) {
        int d = (int)((a >> k) & 255) - (int)((b >> k) & 255);
        if (d < -tol || d > tol) return 0;
    }
    return 1;
}

// Read a render target into memory: an A8R8G8B8 system-memory surface, GetRenderTargetData, LockRect.
static void read_target(uint32_t dev, uint32_t rt, int w, int h, uint32_t *out) {
    uint32_t pp = galloc(4), lr = galloc(8);
    check(com(dev, DEV_CreateOffscreenPlainSurface, 6, w, h, FMT_A8R8G8B8, POOL_SYSTEMMEM, pp, 0) == D3D_OK, "CreateOffscreenPlainSurface");
    uint32_t sys = gptr(pp);
    check(com(dev, DEV_GetRenderTargetData, 2, rt, sys) == D3D_OK, "GetRenderTargetData");
    check(com(sys, SURF_LockRect, 3, lr, 0, LOCK_READONLY) == D3D_OK, "LockRect system surface");
    uint32_t pitch = gptr(lr), bits = gptr(lr + 4);
    for (int y = 0; y < h; y++) memcpy(out + y * w, RC32_H(bits + (uint32_t)y * pitch), 4 * (size_t)w);
    com(sys, SURF_UnlockRect, 0);
    check(com(sys, RES_Release, 0) == 0, "system surface released");
}

static void corpus(uint32_t dev, const char *dir);
static void fixed_function(uint32_t dev, uint32_t bb, uint32_t tex, uint32_t vb, uint32_t vs, uint32_t ps);

// the seam's display size (win32_user32.c in the game): a fixed 1920x1080 desktop here
void w32_display_size(int32_t *w, int32_t *h) {
    *w = 1920;
    *h = 1080;
}

int main(int argc, char **argv) {
    const char *corpus_dir = 0;
    for (int k = 1; k < argc; k++)
        if (!strcmp(argv[k], "--corpus") && k + 1 < argc) corpus_dir = argv[++k];
    printf("backend %s\n", rs_gfx_backend() == RS_GFX_METAL ? "metal" : "gl");
    if (rc32_mem_init()) return 1;
    uint32_t stack = rc32_alloc(1 << 20);
    c->r[4] = stack + (1 << 20) - 64;

    // Direct3DCreate9: an import-style thunk, arguments at esp with no return address
    uint32_t esp0 = (uint32_t)c->r[4];
    push(32);
    shim_Direct3DCreate9(c);
    check((uint32_t)c->r[4] == esp0, "Direct3DCreate9 removed its argument");
    uint32_t d3d = (uint32_t)c->r[0];
    check(d3d != 0, "Direct3DCreate9");
    check(com(d3d, D3D_GetAdapterCount, 0) == 1, "one adapter");
    uint32_t caps = galloc(304), id = galloc(1100);
    check(com(d3d, D3D_GetDeviceCaps, 3, 0, 1, caps) == D3D_OK, "GetDeviceCaps");
    check(gptr(caps + 196) == 0xFFFE0300 && gptr(caps + 204) == 0xFFFF0300, "shader model 3 caps");
    check(com(d3d, D3D_GetAdapterIdentifier, 3, 0, 0, id) == D3D_OK && gptr(id + 1064) == 0x10DE, "adapter identifier");
    check(com(d3d, D3D_CheckDeviceFormat, 6, 0, 1, FMT_X8R8G8B8, 0, RTYPE_TEXTURE, FMT_DXT5) == D3D_OK, "DXT5 textures");
    check(com(d3d, D3D_CheckDeviceFormat, 6, 0, 1, FMT_X8R8G8B8, USAGE_DEPTHSTENCIL, RTYPE_SURFACE, FMT_D24S8) == D3D_OK, "D24S8");
    check(com(d3d, D3D_CheckDeviceFormat, 6, 0, 1, FMT_X8R8G8B8, 0, RTYPE_VOLUMETEXTURE, FMT_A8R8G8B8) != D3D_OK, "no volumes");

    // the device: 16x8 so a swapped axis shows
    uint32_t pp = galloc(56), ppdev = galloc(4);
    uint32_t ppv[14] = {W, H, FMT_X8R8G8B8, 1, 0, 0, 1 /* DISCARD */, 0x10001, 1, 1, FMT_D24S8, 0, 0, 0};
    memcpy(RC32_H(pp), ppv, sizeof ppv);
    check(com(d3d, D3D_CreateDevice, 6, 0, 1, 0x10001, 0x40 /* HW VP */, pp, ppdev) == D3D_OK, "CreateDevice");
    uint32_t dev = gptr(ppdev);
    check(dev != 0, "device");
    if (!dev) goto done;

    // 1. a textured quad covering the target, texels 1:1 with D3D's half-pixel convention
    uint32_t out = galloc(4);
    check(com(dev, DEV_CreateTexture, 8, W, H, 1, 0, FMT_A8R8G8B8, POOL_MANAGED, out, 0) == D3D_OK, "CreateTexture");
    uint32_t tex = gptr(out), lr = galloc(8);
    check(com(tex, TEX_LockRect, 4, 0, lr, 0, 0) == D3D_OK, "texture LockRect");
    uint32_t pitch = gptr(lr), bits = gptr(lr + 4);
    check(pitch >= 4 * W && bits != 0, "locked rect");
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) d3d9_wr32(bits + (uint32_t)y * pitch + 4 * (uint32_t)x, texel(x, y));
    check(com(tex, TEX_UnlockRect, 1, 0) == D3D_OK, "texture UnlockRect");

    // positions in clip space shifted by half a pixel up-left, as D3D9 code does for 1:1 texels
    float hx = 1.0f / W, hy = 1.0f / H;
    float quad[4][6] = {
        {-1 - hx, 1 + hy, 0.5f, 1, 0, 0}, {1 - hx, 1 + hy, 0.5f, 1, 1, 0},
        {-1 - hx, -1 + hy, 0.5f, 1, 0, 1}, {1 - hx, -1 + hy, 0.5f, 1, 1, 1},
    };
    check(com(dev, DEV_CreateVertexBuffer, 6, sizeof quad, USAGE_WRITEONLY, 0, POOL_DEFAULT, out, 0) == D3D_OK, "CreateVertexBuffer");
    uint32_t vb = gptr(out);
    check(com(vb, BUF_Lock, 4, 0, 0, out, 0) == D3D_OK, "VB Lock");
    memcpy(RC32_H(gptr(out)), quad, sizeof quad);
    check(com(vb, BUF_Unlock, 0) == D3D_OK, "VB Unlock");
    uint8_t de[3][8] = {{0, 0, 0, 0, DT_FLOAT4, 0, DU_POSITION, 0}, {0, 0, 16, 0, DT_FLOAT2, 0, DU_TEXCOORD, 0},
                        {0xFF, 0, 0, 0, DT_UNUSED, 0, 0, 0}};
    check(com(dev, DEV_CreateVertexDeclaration, 2, gcopy(de, sizeof de), out) == D3D_OK, "CreateVertexDeclaration");
    uint32_t decl = gptr(out);
    check(com(dev, DEV_CreateVertexShader, 2, gcopy(vs20, sizeof vs20), out) == D3D_OK, "CreateVertexShader vs_2_0");
    uint32_t vs = gptr(out);
    check(com(dev, DEV_CreatePixelShader, 2, gcopy(ps20, sizeof ps20), out) == D3D_OK, "CreatePixelShader ps_2_0");
    uint32_t ps = gptr(out);

    check(com(dev, DEV_Clear, 6, 0, 0, CLEAR_TARGET | CLEAR_ZBUFFER, 0xFFFF0000u, fbits(1.0f), 0) == D3D_OK, "Clear");
    check(com(dev, DEV_BeginScene, 0) == D3D_OK, "BeginScene");
    com(dev, DEV_SetRenderState, 2, RS_ZENABLE, 0);
    com(dev, DEV_SetRenderState, 2, RS_CULLMODE, 1);
    com(dev, DEV_SetVertexDeclaration, 1, decl);
    com(dev, DEV_SetStreamSource, 4, 0, vb, 0, 24);
    com(dev, DEV_SetVertexShader, 1, vs);
    com(dev, DEV_SetPixelShader, 1, ps);
    com(dev, DEV_SetTexture, 2, 0, tex);
    com(dev, DEV_SetSamplerState, 3, 0, SS_MINFILTER, 1);
    com(dev, DEV_SetSamplerState, 3, 0, SS_MAGFILTER, 1);
    check(com(dev, DEV_DrawPrimitive, 3, PT_TRIANGLESTRIP, 0, 2) == D3D_OK, "DrawPrimitive");
    check(com(dev, DEV_EndScene, 0) == D3D_OK, "EndScene");

    check(com(dev, DEV_GetBackBuffer, 4, 0, 0, 0, out) == D3D_OK, "GetBackBuffer");
    uint32_t bb = gptr(out);
    uint32_t px[W * H];
    read_target(dev, bb, W, H, px);
    int bad = 0;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            if (px[y * W + x] != texel(x, y)) {
                if (bad++ < 4) printf("  pixel (%d,%d) = %08x, want %08x\n", x, y, px[y * W + x], texel(x, y));
            }
    check(bad == 0, "textured quad: every pixel is its texel (%d wrong)", bad);

    // 2. Present: framebuffer 0 shows the back buffer upright (GL row 0 = bottom = D3D row H-1)
    check(com(dev, DEV_Present, 4, 0, 0, 0, 0) == D3D_OK, "Present");
    {
        uint8_t fb[W * H * 4];
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, fb);
        int fbad = 0;
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) {
                const uint8_t *p = fb + 4 * (y * W + x);
                uint32_t v = 0xFF000000u | (uint32_t)p[0] << 16 | (uint32_t)p[1] << 8 | p[2];
                if (v != texel(x, H - 1 - y)) fbad++;
            }
        check(fbad == 0, "Present: window framebuffer is the back buffer, upright (%d wrong)", fbad);
    }

    // 3. vs_3_0 / ps_3_0 with constants, DrawIndexedPrimitive with a base vertex, into a
    // render-target texture, half of it through the viewport
    check(com(dev, DEV_CreateTexture, 8, W, H, 1, USAGE_RENDERTARGET, FMT_A8R8G8B8, POOL_DEFAULT, out, 0) == D3D_OK, "render target texture");
    uint32_t rtt = gptr(out);
    check(com(rtt, TEX_GetSurfaceLevel, 2, 0, out) == D3D_OK, "GetSurfaceLevel");
    uint32_t rts = gptr(out);
    check(com(dev, DEV_SetRenderTarget, 2, 0, rts) == D3D_OK, "SetRenderTarget");
    check(com(dev, DEV_Clear, 6, 0, 0, CLEAR_TARGET, 0xFF102030u, fbits(1.0f), 0) == D3D_OK, "Clear render target");
    uint32_t vp = galloc(24);
    uint32_t vpv[6] = {0, 0, W / 2, H, fbits(0.0f), fbits(1.0f)};
    memcpy(RC32_H(vp), vpv, 24);
    com(dev, DEV_SetViewport, 1, vp);
    float verts[8][4] = {{9, 9, 9, 1}, {9, 9, 9, 1}, {9, 9, 9, 1}, {9, 9, 9, 1},
                         {-1, 1, 0.5f, 1}, {1, 1, 0.5f, 1}, {-1, -1, 0.5f, 1}, {1, -1, 0.5f, 1}};
    check(com(dev, DEV_CreateVertexBuffer, 6, sizeof verts, USAGE_DYNAMIC, 0, POOL_DEFAULT, out, 0) == D3D_OK, "VB 2");
    uint32_t vb2 = gptr(out);
    com(vb2, BUF_Lock, 4, 0, 0, out, LOCK_DISCARD);
    memcpy(RC32_H(gptr(out)), verts, sizeof verts);
    com(vb2, BUF_Unlock, 0);
    uint16_t idx[6] = {0, 1, 2, 2, 1, 3};
    check(com(dev, DEV_CreateIndexBuffer, 6, sizeof idx, 0, FMT_INDEX16, POOL_DEFAULT, out, 0) == D3D_OK, "CreateIndexBuffer");
    uint32_t ib = gptr(out);
    com(ib, BUF_Lock, 4, 0, 0, out, 0);
    memcpy(RC32_H(gptr(out)), idx, sizeof idx);
    com(ib, BUF_Unlock, 0);
    uint8_t de2[2][8] = {{0, 0, 0, 0, DT_FLOAT4, 0, DU_POSITION, 0}, {0xFF, 0, 0, 0, DT_UNUSED, 0, 0, 0}};
    com(dev, DEV_CreateVertexDeclaration, 2, gcopy(de2, sizeof de2), out);
    uint32_t decl2 = gptr(out);
    check(com(dev, DEV_CreateVertexShader, 2, gcopy(vs30, sizeof vs30), out) == D3D_OK, "CreateVertexShader vs_3_0");
    uint32_t vs3 = gptr(out);
    check(com(dev, DEV_CreatePixelShader, 2, gcopy(ps30, sizeof ps30), out) == D3D_OK, "CreatePixelShader ps_3_0");
    uint32_t ps3 = gptr(out);
    float ident[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    float color[4] = {0.25f, 0.5f, 0.75f, 1.0f};
    com(dev, DEV_SetVertexShaderConstantF, 3, 0, gcopy(ident, sizeof ident), 4);
    com(dev, DEV_SetPixelShaderConstantF, 3, 0, gcopy(color, sizeof color), 1);
    com(dev, DEV_SetVertexDeclaration, 1, decl2);
    com(dev, DEV_SetStreamSource, 4, 0, vb2, 0, 16);
    com(dev, DEV_SetIndices, 1, ib);
    com(dev, DEV_SetVertexShader, 1, vs3);
    com(dev, DEV_SetPixelShader, 1, ps3);
    com(dev, DEV_BeginScene, 0);
    check(com(dev, DEV_DrawIndexedPrimitive, 6, PT_TRIANGLELIST, 4, 0, 4, 0, 2) == D3D_OK, "DrawIndexedPrimitive");
    com(dev, DEV_EndScene, 0);
    read_target(dev, rts, W, H, px);
    int lbad = 0, rbad = 0;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            uint32_t v = px[y * W + x];
            if (x < W / 2 && !near(v, 0xFF4080BFu, 1)) { if (lbad++ < 2) printf("  left (%d,%d) = %08x\n", x, y, v); }
            if (x >= W / 2 && v != 0xFF102030u) { if (rbad++ < 2) printf("  right (%d,%d) = %08x\n", x, y, v); }
        }
    check(lbad == 0, "vs_3_0/ps_3_0 constants fill the viewport (%d wrong)", lbad);
    check(rbad == 0, "outside the viewport keeps the clear colour (%d wrong)", rbad);

    // 4. sample the render target from the first quad: render-to-texture rows stay top first
    check(com(dev, DEV_SetRenderTarget, 2, 0, bb) == D3D_OK, "SetRenderTarget back buffer");
    com(dev, DEV_SetVertexDeclaration, 1, decl);
    com(dev, DEV_SetStreamSource, 4, 0, vb, 0, 24);
    com(dev, DEV_SetVertexShader, 1, vs);
    com(dev, DEV_SetPixelShader, 1, ps);
    com(dev, DEV_SetTexture, 2, 0, rtt);
    com(dev, DEV_BeginScene, 0);
    com(dev, DEV_DrawPrimitive, 3, PT_TRIANGLESTRIP, 0, 2);
    com(dev, DEV_EndScene, 0);
    uint32_t px2[W * H];
    read_target(dev, bb, W, H, px2);
    int sbad = 0;
    for (int k = 0; k < W * H; k++) if (px2[k] != px[k]) sbad++;
    check(sbad == 0, "render target sampled back unchanged (%d wrong)", sbad);

    // 5. state block, render state round trip, queries, DrawPrimitiveUP
    com(dev, DEV_SetRenderState, 2, RS_ALPHABLENDENABLE, 1);
    check(com(dev, DEV_CreateStateBlock, 2, SBT_ALL, out) == D3D_OK, "CreateStateBlock");
    uint32_t sb = gptr(out);
    com(dev, DEV_SetRenderState, 2, RS_ALPHABLENDENABLE, 0);
    com(sb, SB_Apply, 0);
    com(dev, DEV_GetRenderState, 2, RS_ALPHABLENDENABLE, out);
    check(gptr(out) == 1, "state block restores a render state");
    com(dev, DEV_SetRenderState, 2, RS_ALPHABLENDENABLE, 0);
    check(com(dev, DEV_CreateQuery, 2, QT_EVENT, out) == D3D_OK, "CreateQuery event");
    uint32_t q = gptr(out);
    com(q, Q_Issue, 1, ISSUE_END);
    check(com(q, Q_GetData, 3, out, 4, 1) == D3D_OK && gptr(out) == 1, "event query signalled");
    check(com(dev, DEV_DrawPrimitiveUP, 4, PT_TRIANGLESTRIP, 2, gcopy(quad, sizeof quad), 24) == D3D_OK, "DrawPrimitiveUP");
    read_target(dev, bb, W, H, px);
    bad = 0;
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) if (px[y * W + x] != px2[y * W + x]) bad++;
    check(bad == 0, "DrawPrimitiveUP draws like the vertex buffer (%d wrong)", bad);

    // 6. a dynamic vertex buffer as the game streams into it: every lock of the whole buffer
    // (offset 0, size 0), DISCARD then NOOVERWRITE appends, each drawn before the next lock (indexed
    // with a base vertex, and not indexed), then a plain lock overwriting the first quad. Uploads
    // happen at the draws (d3d9_buffer_sync): each quarter of the target must show its quad.
    {
        check(com(dev, DEV_SetRenderTarget, 2, 0, rts) == D3D_OK, "SetRenderTarget (streaming)");
        uint32_t vpf[6] = {0, 0, W, H, fbits(0.0f), fbits(1.0f)};
        memcpy(RC32_H(vp), vpf, 24);
        com(dev, DEV_SetViewport, 1, vp);
        com(dev, DEV_Clear, 6, 0, 0, CLEAR_TARGET, 0xFF102030u, fbits(1.0f), 0);
        check(com(dev, DEV_CreateVertexBuffer, 6, 4096, USAGE_DYNAMIC | USAGE_WRITEONLY, 0, POOL_DEFAULT, out, 0) == D3D_OK,
              "VB 3 (dynamic)");
        uint32_t vb3 = gptr(out);
        com(dev, DEV_SetVertexDeclaration, 1, decl2);
        com(dev, DEV_SetStreamSource, 4, 0, vb3, 0, 16);
        com(dev, DEV_SetIndices, 1, ib);
        com(dev, DEV_SetVertexShader, 1, vs3);
        com(dev, DEV_SetPixelShader, 1, ps3);
        com(dev, DEV_BeginScene, 0);
        // quarter q of the target, as vertices first..first+3 (a strip and the {0,1,2,2,1,3} list)
        for (int step = 0; step < 4; step++) {
            static const uint32_t flags[4] = {LOCK_DISCARD, LOCK_NOOVERWRITE, LOCK_NOOVERWRITE, 0};
            static const int first[4] = {0, 4, 8, 0}, quarter[4] = {0, 1, 2, 3};
            check(com(vb3, BUF_Lock, 4, 0, 0, out, flags[step]) == D3D_OK, "VB 3 lock %d", step);
            float *v = (float *)RC32_H(gptr(out)) + first[step] * 4;
            float x0 = -1 + 0.5f * (float)quarter[step], x1 = x0 + 0.5f;
            float q[4][4] = {{x0, 1, 0.5f, 1}, {x1, 1, 0.5f, 1}, {x0, -1, 0.5f, 1}, {x1, -1, 0.5f, 1}};
            memcpy(v, q, sizeof q);
            com(vb3, BUF_Unlock, 0);
            if (step == 2) check(com(dev, DEV_DrawPrimitive, 3, PT_TRIANGLESTRIP, first[step], 2) == D3D_OK, "VB 3 draw %d", step);
            else check(com(dev, DEV_DrawIndexedPrimitive, 6, PT_TRIANGLELIST, first[step], 0, 4, 0, 2) == D3D_OK, "VB 3 draw %d", step);
        }
        com(dev, DEV_EndScene, 0);
        read_target(dev, rts, W, H, px);
        int qbad[4] = {0, 0, 0, 0};
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) {
                int qx = x * 4 / W;
                if (!near(px[y * W + x], 0xFF4080BFu, 1) && qbad[qx]++ < 1) printf("  quarter %d (%d,%d) = %08x\n", qx, x, y, px[y * W + x]);
            }
        for (int k = 0; k < 4; k++) check(qbad[k] == 0, "streamed quad %d drawn from its lock (%d wrong)", k, qbad[k]);
        com(vb3, RES_Release, 0);
        check(com(dev, DEV_SetRenderTarget, 2, 0, bb) == D3D_OK, "SetRenderTarget back buffer again");
    }

    fixed_function(dev, bb, tex, vb, vs, ps);
    if (corpus_dir) corpus(dev, corpus_dir);

    // release everything; the device goes last
    uint32_t objs[] = {sb, q, ib, vb2, decl2, vs3, ps3, rtt, vb, decl, vs, ps, tex, bb};
    com(rts, RES_Release, 0);
    for (size_t k = 0; k < sizeof objs / sizeof objs[0]; k++) com(objs[k], RES_Release, 0);
    com(dev, DEV_Release, 0);
    com(d3d, D3D_Release, 0);
done:
    printf("%d passed, %d failed\n", passed, failed);
    return failed != 0;
}

// --- fixed function ------------------------------------------------------------------------------
static uint32_t scale8(uint32_t c, uint32_t f) {  // per channel c * f / 255
    uint32_t o = 0;
    for (int k = 0; k < 32; k += 8) o |= (((c >> k) & 255) * ((f >> k) & 255) + 127) / 255 << k;
    return o;
}

static uint32_t gmat(const float *m) { return gcopy(m, 64); }

// Every pixel of the back buffer against want(x, y); returns the number wrong.
static int check_target(uint32_t dev, uint32_t bb, uint32_t (*want)(int, int, void *), void *arg, int tol, const char *what) {
    uint32_t px[W * H];
    read_target(dev, bb, W, H, px);
    int bad = 0;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            uint32_t w = want(x, y, arg);
            if (!near(px[y * W + x], w, tol) && bad++ < 3) printf("  %s (%d,%d) = %08x, want %08x\n", what, x, y, px[y * W + x], w);
        }
    return bad;
}

static uint32_t want_rhw(int x, int y, void *arg) { (void)y; (void)arg; return x < W / 2 ? 0xFF20C040u : 0xFF0000FFu; }
static uint32_t want_tfactor(int x, int y, void *arg) { return scale8(texel(x, y), *(uint32_t *)arg); }
static uint32_t want_texel(int x, int y, void *arg) { (void)arg; return texel(x, y); }
static uint32_t want_solid(int x, int y, void *arg) { (void)x; (void)y; return *(uint32_t *)arg; }

static void ff_reset_stage0(uint32_t dev) {
    com(dev, DEV_SetTextureStageState, 3, 0, TSS_COLOROP, TOP_MODULATE);
    com(dev, DEV_SetTextureStageState, 3, 0, TSS_COLORARG1, TA_TEXTURE);
    com(dev, DEV_SetTextureStageState, 3, 0, TSS_COLORARG2, TA_CURRENT);
    com(dev, DEV_SetTextureStageState, 3, 0, TSS_ALPHAOP, TOP_SELECTARG1);
    com(dev, DEV_SetTextureStageState, 3, 0, TSS_ALPHAARG1, TA_TEXTURE);
}

static void fixed_function(uint32_t dev, uint32_t bb, uint32_t tex, uint32_t vb, uint32_t vs, uint32_t ps) {
    static const float ident[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    uint32_t out = galloc(128);
    int f0 = d3d9_program_failures;
    com(dev, DEV_SetVertexShader, 1, 0);
    com(dev, DEV_SetPixelShader, 1, 0);
    com(dev, DEV_SetTexture, 2, 0, 0);
    com(dev, DEV_SetRenderState, 2, RS_ZENABLE, 0);
    com(dev, DEV_SetRenderState, 2, RS_CULLMODE, 1);
    com(dev, DEV_SetRenderState, 2, RS_ALPHABLENDENABLE, 0);
    ff_reset_stage0(dev);

    // a. pre-transformed XYZRHW | DIFFUSE over the left half (pixel centres on integers: the
    // quad's edges at -0.5 cover whole pixels), no texture bound: the stage reads white
    float rhw[4][5];
    float qx[4] = {-0.5f, W / 2 - 0.5f, -0.5f, W / 2 - 0.5f}, qy[4] = {-0.5f, -0.5f, H - 0.5f, H - 0.5f};
    for (int k = 0; k < 4; k++) {
        uint32_t col = 0xFF20C040u;
        float v[5] = {qx[k], qy[k], 0.5f, 1.0f, 0};
        memcpy(&v[4], &col, 4);
        memcpy(rhw[k], v, sizeof v);
    }
    com(dev, DEV_Clear, 6, 0, 0, CLEAR_TARGET, 0xFF0000FFu, fbits(1.0f), 0);
    com(dev, DEV_SetFVF, 1, FVF_XYZRHW | FVF_DIFFUSE);
    com(dev, DEV_BeginScene, 0);
    check(com(dev, DEV_DrawPrimitiveUP, 4, PT_TRIANGLESTRIP, 2, gcopy(rhw, sizeof rhw), 20) == D3D_OK, "FF: XYZRHW draw");
    com(dev, DEV_EndScene, 0);
    int bad = check_target(dev, bb, want_rhw, 0, 0, "rhw");
    check(bad == 0, "FF: pre-transformed diffuse quad fills exactly the left half (%d wrong)", bad);

    // b. XYZ | TEX1 through world, view and projection, MODULATE(TEXTURE, TFACTOR); world is
    // scale then translate by MultiplyTransform, with D3D's half-pixel shift
    float hx = 1.0f / W, hy = 1.0f / H;
    float tq[4][5] = {{0, 1, 0, 0, 0}, {1, 1, 0, 1, 0}, {0, 0, 0, 0, 1}, {1, 0, 0, 1, 1}};
    float tr[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -1 - hx, -1 + hy, 0, 1};
    float sc[16] = {2, 0, 0, 0, 0, 2, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    float view[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 5, 1};
    float proj[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0.1f, 0, 0, 0, -0.4f, 1};
    com(dev, DEV_SetTransform, 2, TS_WORLD, gmat(tr));
    com(dev, DEV_MultiplyTransform, 2, TS_WORLD, gmat(sc));
    com(dev, DEV_SetTransform, 2, TS_VIEW, gmat(view));
    com(dev, DEV_SetTransform, 2, TS_PROJECTION, gmat(proj));
    com(dev, DEV_GetTransform, 2, TS_WORLD, out);
    float wm[16];
    memcpy(wm, RC32_H(out), 64);
    check(wm[0] == 2 && wm[5] == 2 && wm[12] == -1 - hx && wm[13] == -1 + hy,
          "FF: MultiplyTransform is the given matrix times the current one");
    uint32_t tf = 0xFF80C0FFu;
    com(dev, DEV_SetRenderState, 2, RS_LIGHTING, 0);
    com(dev, DEV_SetRenderState, 2, RS_TEXTUREFACTOR, tf);
    com(dev, DEV_SetTextureStageState, 3, 0, TSS_COLORARG2, TA_TFACTOR);
    com(dev, DEV_SetTexture, 2, 0, tex);
    com(dev, DEV_SetSamplerState, 3, 0, SS_MINFILTER, 1);
    com(dev, DEV_SetSamplerState, 3, 0, SS_MAGFILTER, 1);
    com(dev, DEV_SetFVF, 1, FVF_XYZ | (1u << FVF_TEXCOUNT_SHIFT));
    com(dev, DEV_BeginScene, 0);
    check(com(dev, DEV_DrawPrimitiveUP, 4, PT_TRIANGLESTRIP, 2, gcopy(tq, sizeof tq), 20) == D3D_OK, "FF: XYZ|TEX1 draw");
    com(dev, DEV_EndScene, 0);
    bad = check_target(dev, bb, want_tfactor, &tf, 1, "tex1");
    check(bad == 0, "FF: transformed textured quad is texel * TFACTOR everywhere (%d wrong)", bad);

    // c. a pixel shader with the fixed-function vertex stage: the same quad, texels exactly
    com(dev, DEV_SetPixelShader, 1, ps);
    com(dev, DEV_BeginScene, 0);
    com(dev, DEV_DrawPrimitiveUP, 4, PT_TRIANGLESTRIP, 2, gcopy(tq, sizeof tq), 20);
    com(dev, DEV_EndScene, 0);
    bad = check_target(dev, bb, want_texel, 0, 0, "ffvs+ps");
    check(bad == 0, "FF: fixed-function vertex stage + ps_2_0 gives every texel (%d wrong)", bad);
    com(dev, DEV_SetPixelShader, 1, 0);

    // d. a vertex shader with the fixed-function pixel stage: vs_2_0 writes oT0 only, so diffuse
    // is white and MODULATE(TEXTURE, CURRENT) is the texel
    ff_reset_stage0(dev);
    com(dev, DEV_SetVertexShader, 1, vs);
    com(dev, DEV_SetFVF, 1, FVF_XYZW | (1u << FVF_TEXCOUNT_SHIFT));
    com(dev, DEV_SetStreamSource, 4, 0, vb, 0, 24);
    com(dev, DEV_Clear, 6, 0, 0, CLEAR_TARGET, 0xFF0000FFu, fbits(1.0f), 0);
    com(dev, DEV_BeginScene, 0);
    com(dev, DEV_DrawPrimitive, 3, PT_TRIANGLESTRIP, 0, 2);
    com(dev, DEV_EndScene, 0);
    bad = check_target(dev, bb, want_texel, 0, 0, "vs+ffps");
    check(bad == 0, "FF: vs_2_0 + fixed-function pixel stage gives every texel (%d wrong)", bad);
    com(dev, DEV_SetVertexShader, 1, 0);
    com(dev, DEV_SetTexture, 2, 0, 0);

    // e. lighting: XYZ | NORMAL facing the camera, one directional light along +z, material
    // diffuse 0.8 and ambient 1, RS_AMBIENT 0x20 grey, light ambient 0.1
    float lq[4][6] = {{-1, 1, 0.5f, 0, 0, -1}, {1, 1, 0.5f, 0, 0, -1}, {-1, -1, 0.5f, 0, 0, -1}, {1, -1, 0.5f, 0, 0, -1}};
    com(dev, DEV_SetTransform, 2, TS_WORLD, gmat(ident));
    com(dev, DEV_SetTransform, 2, TS_VIEW, gmat(ident));
    com(dev, DEV_SetTransform, 2, TS_PROJECTION, gmat(ident));
    float mat[17] = {0.8f, 0.8f, 0.8f, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    com(dev, DEV_SetMaterial, 1, gcopy(mat, sizeof mat));
    float light[26] = {0, 1, 0.5f, 0.25f, 1, 0, 0, 0, 0, 0.1f, 0.1f, 0.1f, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0};
    uint32_t ltype = LIGHT_DIRECTIONAL;
    memcpy(&light[0], &ltype, 4);
    check(com(dev, DEV_SetLight, 2, 3, gcopy(light, sizeof light)) == D3D_OK, "FF: SetLight");
    check(com(dev, DEV_LightEnable, 2, 3, 1) == D3D_OK, "FF: LightEnable");
    check(com(dev, DEV_GetLight, 2, 3, out) == D3D_OK && !memcmp(RC32_H(out), light, 104), "FF: GetLight round trip");
    com(dev, DEV_SetRenderState, 2, RS_LIGHTING, 1);
    com(dev, DEV_SetRenderState, 2, RS_AMBIENT, 0xFF202020u);
    com(dev, DEV_SetTextureStageState, 3, 0, TSS_COLOROP, TOP_SELECTARG1);
    com(dev, DEV_SetTextureStageState, 3, 0, TSS_COLORARG1, TA_DIFFUSE);
    com(dev, DEV_SetTextureStageState, 3, 0, TSS_ALPHAARG1, TA_DIFFUSE);
    com(dev, DEV_SetFVF, 1, FVF_XYZ | FVF_NORMAL);
    com(dev, DEV_BeginScene, 0);
    check(com(dev, DEV_DrawPrimitiveUP, 4, PT_TRIANGLESTRIP, 2, gcopy(lq, sizeof lq), 24) == D3D_OK, "FF: lit draw");
    com(dev, DEV_EndScene, 0);
    // ambient 1 * (0.1255 + 0.1) + diffuse 0.8 * (1, 0.5, 0.25): (1, 0.6255, 0.4255)
    uint32_t lit = 0xFFFFA06Cu;
    bad = check_target(dev, bb, want_solid, &lit, 2, "lit");
    check(bad == 0, "FF: directional light, material and ambient (%d wrong)", bad);
    com(dev, DEV_LightEnable, 2, 3, 0);
    com(dev, DEV_BeginScene, 0);
    com(dev, DEV_DrawPrimitiveUP, 4, PT_TRIANGLESTRIP, 2, gcopy(lq, sizeof lq), 24);
    com(dev, DEV_EndScene, 0);
    uint32_t amb = 0xFF202020u;  // 0.1255: the light's ambient goes with it
    bad = check_target(dev, bb, want_solid, &amb, 2, "unlit");
    check(bad == 0, "FF: with the light disabled only the global ambient is left (%d wrong)", bad);

    // f. linear vertex fog at eye depth 0.5 between 0 and 1: half way to the (black) fog colour
    com(dev, DEV_LightEnable, 2, 3, 1);
    com(dev, DEV_SetRenderState, 2, RS_FOGENABLE, 1);
    com(dev, DEV_SetRenderState, 2, RS_FOGVERTEXMODE, 3);
    com(dev, DEV_SetRenderState, 2, RS_FOGSTART, fbits(0.0f));
    com(dev, DEV_SetRenderState, 2, RS_FOGEND, fbits(1.0f));
    com(dev, DEV_SetRenderState, 2, RS_FOGCOLOR, 0xFF000000u);
    com(dev, DEV_BeginScene, 0);
    com(dev, DEV_DrawPrimitiveUP, 4, PT_TRIANGLESTRIP, 2, gcopy(lq, sizeof lq), 24);
    com(dev, DEV_EndScene, 0);
    uint32_t fogged = 0xFF805036u;
    bad = check_target(dev, bb, want_solid, &fogged, 2, "fog");
    check(bad == 0, "FF: linear vertex fog (%d wrong)", bad);
    com(dev, DEV_SetRenderState, 2, RS_FOGENABLE, 0);

    // g. the generator's other paths compile and link on this backend: every implemented stage
    // operation over two stages, point and spot lights with specular, table fog modes, generated
    // and transformed (projected) coordinates, a cube texture stage
    float tl[4][11];
    for (int k = 0; k < 4; k++) {
        float v[11] = {lq[k][0], lq[k][1], lq[k][2], 0, 0, -1, 0, 0.5f, 0.5f, 1, 0};
        uint32_t col = 0xFF808080u;
        memcpy(&v[6], &col, 4);
        memcpy(tl[k], v, sizeof v);
    }
    com(dev, DEV_SetFVF, 1, FVF_XYZ | FVF_NORMAL | FVF_DIFFUSE | (1u << FVF_TEXCOUNT_SHIFT) | (1u << 16));  // TEX1 of 3
    com(dev, DEV_SetTexture, 2, 0, tex);
    com(dev, DEV_SetTexture, 2, 1, tex);
    com(dev, DEV_SetTextureStageState, 3, 1, TSS_COLORARG1, TA_TEXTURE | TA_COMPLEMENT);
    com(dev, DEV_SetTextureStageState, 3, 1, TSS_COLORARG2, TA_CURRENT | TA_ALPHAREPLICATE);
    com(dev, DEV_SetTextureStageState, 3, 1, TSS_COLORARG0, TA_SPECULAR);
    com(dev, DEV_SetTextureStageState, 3, 1, TSS_ALPHAARG0, TA_TFACTOR);
    com(dev, DEV_SetTextureStageState, 3, 1, TSS_RESULTARG, TA_TEMP);
    static const int ops[] = {TOP_SELECTARG2, TOP_MODULATE2X, TOP_MODULATE4X, TOP_ADD, TOP_ADDSIGNED, TOP_ADDSIGNED2X,
                              TOP_SUBTRACT, TOP_ADDSMOOTH, TOP_BLENDDIFFUSEALPHA, TOP_BLENDTEXTUREALPHA,
                              TOP_BLENDFACTORALPHA, TOP_BLENDTEXTUREALPHAPM, TOP_BLENDCURRENTALPHA,
                              TOP_MODULATEALPHA_ADDCOLOR, TOP_MODULATECOLOR_ADDALPHA, TOP_MODULATEINVALPHA_ADDCOLOR,
                              TOP_MODULATEINVCOLOR_ADDALPHA, TOP_DOTPRODUCT3, TOP_MULTIPLYADD, TOP_LERP};
    for (size_t k = 0; k < sizeof ops / sizeof ops[0]; k++) {
        com(dev, DEV_SetTextureStageState, 3, 1, TSS_COLOROP, ops[k]);
        com(dev, DEV_SetTextureStageState, 3, 1, TSS_ALPHAOP, ops[k]);
        com(dev, DEV_DrawPrimitiveUP, 4, PT_TRIANGLESTRIP, 2, gcopy(tl, sizeof tl), 44);
    }
    float pl[26] = {0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, -1, 0, 0, 1, 10, 1, 1, 0.1f, 0.01f, 0.5f, 1.0f};
    uint32_t lt = LIGHT_POINT;
    memcpy(&pl[0], &lt, 4);
    com(dev, DEV_SetLight, 2, 0, gcopy(pl, sizeof pl));
    lt = LIGHT_SPOT;
    memcpy(&pl[0], &lt, 4);
    com(dev, DEV_SetLight, 2, 1, gcopy(pl, sizeof pl));
    com(dev, DEV_LightEnable, 2, 0, 1);
    com(dev, DEV_LightEnable, 2, 1, 1);
    com(dev, DEV_SetRenderState, 2, RS_SPECULARENABLE, 1);
    com(dev, DEV_SetRenderState, 2, RS_NORMALIZENORMALS, 1);
    com(dev, DEV_SetRenderState, 2, RS_DIFFUSEMATERIALSOURCE, 1);
    com(dev, DEV_SetRenderState, 2, RS_FOGENABLE, 1);
    com(dev, DEV_SetRenderState, 2, RS_RANGEFOGENABLE, 1);
    for (uint32_t fm = 1; fm <= 3; fm++) {
        com(dev, DEV_SetRenderState, 2, RS_FOGTABLEMODE, fm);
        com(dev, DEV_SetTextureStageState, 3, 0, TSS_TEXCOORDINDEX, fm << 16);  // camera-space normal, position, reflection
        com(dev, DEV_SetTextureStageState, 3, 0, TSS_TEXTURETRANSFORMFLAGS, (fm + 1) | (fm == 2 ? 256u : 0));
        com(dev, DEV_DrawPrimitiveUP, 4, PT_TRIANGLESTRIP, 2, gcopy(tl, sizeof tl), 44);
    }
    com(dev, DEV_SetRenderState, 2, RS_FOGTABLEMODE, 0);
    for (uint32_t fm = 1; fm <= 2; fm++) {
        com(dev, DEV_SetRenderState, 2, RS_FOGVERTEXMODE, fm);
        com(dev, DEV_DrawPrimitiveUP, 4, PT_TRIANGLESTRIP, 2, gcopy(tl, sizeof tl), 44);
    }
    uint32_t cube = 0;
    if (com(dev, 25 /* CreateCubeTexture */, 7, 4, 1, 0, FMT_A8R8G8B8, POOL_MANAGED, out, 0) == D3D_OK) {
        cube = gptr(out);
        com(dev, DEV_SetTexture, 2, 0, cube);
        com(dev, DEV_DrawPrimitiveUP, 4, PT_TRIANGLESTRIP, 2, gcopy(tl, sizeof tl), 44);
        com(dev, DEV_SetTexture, 2, 0, 0);
        com(cube, RES_Release, 0);
    }
    check(cube != 0, "FF: CreateCubeTexture");
    com(dev, DEV_SetTextureStageState, 3, 0, TSS_TEXCOORDINDEX, 0);
    com(dev, DEV_SetTextureStageState, 3, 0, TSS_TEXTURETRANSFORMFLAGS, 0);
    com(dev, DEV_SetTextureStageState, 3, 1, TSS_COLOROP, TOP_DISABLE);
    com(dev, DEV_SetTextureStageState, 3, 1, TSS_ALPHAOP, TOP_DISABLE);
    com(dev, DEV_SetTextureStageState, 3, 1, TSS_RESULTARG, TA_CURRENT);
    com(dev, DEV_SetTexture, 2, 1, 0);
    com(dev, DEV_SetRenderState, 2, RS_FOGENABLE, 0);
    com(dev, DEV_SetRenderState, 2, RS_SPECULARENABLE, 0);
    com(dev, DEV_LightEnable, 2, 0, 0);
    com(dev, DEV_LightEnable, 2, 1, 0);
    com(dev, DEV_LightEnable, 2, 3, 0);
    com(dev, DEV_SetTexture, 2, 0, 0);
    ff_reset_stage0(dev);
    com(dev, DEV_SetFVF, 1, 0);
    check(d3d9_program_failures == f0, "FF: every generated program links (%d failed)", d3d9_program_failures - f0);
}

// --- the shader corpus ---------------------------------------------------------------------------
typedef struct {
    char name[64];
    uint32_t obj;
    const d3d9_sm_info *info;
    d3d9_sm *sm;
} blob;

static void corpus(uint32_t dev, const char *dir) {
    DIR *dd = opendir(dir);
    if (!dd) { check(0, "corpus directory %s", dir); return; }
    static blob vs[1024], ps[1024];
    int nvs = 0, nps = 0, bad = 0;
    struct dirent *e;
    uint32_t out = galloc(4);
    while ((e = readdir(dd))) {
        int is_vs = !strncmp(e->d_name, "vs_", 3), is_ps = !strncmp(e->d_name, "ps_", 3);
        if ((!is_vs && !is_ps) || !strstr(e->d_name, ".bin")) continue;
        char p[1024];
        snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
        FILE *f = fopen(p, "rb");
        if (!f) continue;
        static uint8_t buf[1 << 18];
        size_t n = fread(buf, 1, sizeof buf, f);
        fclose(f);
        uint32_t g = gcopy(buf, (uint32_t)n);
        blob *b = is_vs ? &vs[nvs] : &ps[nps];
        if ((is_vs ? nvs : nps) >= 1024) continue;
        snprintf(b->name, sizeof b->name, "%s", e->d_name);
        if (com(dev, is_vs ? DEV_CreateVertexShader : DEV_CreatePixelShader, 2, g, out) != D3D_OK) {
            check(0, "create %s", e->d_name);
            continue;
        }
        b->obj = gptr(out);
        d3d9_shader *s = (d3d9_shader *)d3d9_obj_get(b->obj, -1);
        if (s->failed) bad++;
        b->sm = s->sm;
        b->info = d3d9_sm_get_info(s->sm);
        if (is_vs) nvs++;
        else nps++;
    }
    closedir(dd);
    check(bad == 0, "corpus: every shader translates (%d failed)", bad);
    // a declaration with one float4 element per usage/index the vertex shaders read, one stream
    uint8_t de[17][8];
    int n = 0;
    for (int u = 0; u < 14 && n < 16; u++)
        for (int i = 0; i < 4 && n < 16; i++) {
            int used = 0;
            for (int k = 0; k < nvs && !used; k++)
                for (int r = 0; r < 16; r++)
                    if (vs[k].info->input[r].used && vs[k].info->input[r].usage == u && vs[k].info->input[r].index == i) used = 1;
            if (!used) continue;
            uint8_t el[8] = {0, 0, (uint8_t)(16 * n), 0, DT_FLOAT4, 0, (uint8_t)u, (uint8_t)i};
            memcpy(de[n++], el, 8);
        }
    uint8_t end[8] = {0xFF, 0, 0, 0, DT_UNUSED, 0, 0, 0};
    memcpy(de[n], end, 8);
    com(dev, DEV_CreateVertexDeclaration, 2, gcopy(de, (uint32_t)(n + 1) * 8), out);
    uint32_t decl = gptr(out);
    com(dev, DEV_CreateVertexBuffer, 6, (uint32_t)(3 * 16 * n), 0, 0, POOL_DEFAULT, out, 0);
    uint32_t vb = gptr(out);
    com(dev, DEV_SetVertexDeclaration, 1, decl);
    com(dev, DEV_SetStreamSource, 4, 0, vb, 0, (uint32_t)(16 * n));
    int f0 = d3d9_program_failures, links = 0;
    // every pixel shader with a vertex shader of its model that writes what it reads (else any
    // of its model), then every vertex shader not used yet with the first pixel shader of its model
    static int vs_used[1024];
    for (int k = 0; k < nps; k++) {
        int best = -1;
        for (int j = 0; j < nvs && best < 0; j++) {
            if (vs[j].info->major != ps[k].info->major) continue;
            best = j;
        }
        if (best < 0) continue;
        vs_used[best] = 1;
        com(dev, DEV_SetVertexShader, 1, vs[best].obj);
        com(dev, DEV_SetPixelShader, 1, ps[k].obj);
        com(dev, DEV_DrawPrimitive, 3, PT_TRIANGLELIST, 0, 1);
        links++;
    }
    for (int j = 0; j < nvs; j++) {
        if (vs_used[j]) continue;
        for (int k = 0; k < nps; k++)
            if (ps[k].info->major == vs[j].info->major) {
                com(dev, DEV_SetVertexShader, 1, vs[j].obj);
                com(dev, DEV_SetPixelShader, 1, ps[k].obj);
                com(dev, DEV_DrawPrimitive, 3, PT_TRIANGLELIST, 0, 1);
                links++;
                break;
            }
    }
    printf("corpus: %d vertex + %d pixel shaders, %d programs linked\n", nvs, nps, links);
    check(d3d9_program_failures == f0, "corpus: every program links (%d failed)", d3d9_program_failures - f0);
    com(dev, DEV_SetVertexShader, 1, 0);
    com(dev, DEV_SetPixelShader, 1, 0);
    com(dev, DEV_SetVertexDeclaration, 1, 0);
    com(dev, DEV_SetStreamSource, 4, 0, 0, 0, 0);
    for (int k = 0; k < nvs; k++) com(vs[k].obj, RES_Release, 0);
    for (int k = 0; k < nps; k++) com(ps[k].obj, RES_Release, 0);
    com(decl, RES_Release, 0);
    com(vb, RES_Release, 0);
}
