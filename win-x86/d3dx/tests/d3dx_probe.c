// Runs the translated d3dx9_42.dll (with D3DCompiler_42.dll and msvcr100.dll, also translated)
// without the game: maps the three images, binds their imports to each other and to a minimal
// Win32 (probe_win32.c), runs their DllMains and calls D3DX the way game code does:
//
//   matrix   D3DXMatrixMultiply / Transpose / Inverse, D3DXVec4Transform against host math
//   buffer   D3DXCreateBuffer
//   asm      D3DXAssembleShader on a vs_2_0 snippet: the expected tokens, D3DXGetShaderVersion,
//            D3DXGetShaderInputSemantics, and the D3D9 layer's bytecode -> GLSL translator on it
//   hlsl     D3DXCompileShader on an HLSL ps_2_0 shader, D3DXGetShaderConstantTable on the
//            bytecode: the constants, their registers and sampler index; GLSL from it
//   image    D3DXGetImageInfoFromFileInMemory on a DDS
//   effect   (PROBE_D3D9 builds) D3DXCreateEffect on a small .fx against the D3D9 layer's device:
//            description, technique, a parameter round trip, Begin / BeginPass / End, and
//            D3DXCreateTextureFromFileInMemory
//
// usage: d3dx_probe <build dir> [-v] [step ...]   (the DLLs are build inputs; never committed)
// Built by build.sh beside this file.
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "d3d9_shader.h"
#include "probe.h"

static rc_ctx *C;
static int fails, checks;
#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); fails++; } else if (probe_verbose) fprintf(stderr, "ok   %s\n", #x); } while (0)

static uint32_t gbuf(uint32_t n) { return w32_galloc(n); }
static uint32_t gcopy(const void *p, uint32_t n) { uint32_t g = gbuf(n); memcpy(RC32_H(g), p, n); return g; }
static float gf(uint32_t g) { float f; memcpy(&f, RC32_H(g), 4); return f; }

// a stdcall export of d3dx9_42
static uint32_t dx(const char *name, int n, ...) {
  uint32_t a[16];
  va_list ap;
  va_start(ap, n);
  for (int k = 0; k < n; k++) a[k] = va_arg(ap, uint32_t);
  va_end(ap);
  uint32_t f = probe_export(&probe_img[IMG_D3DX], name);
  if (!f) { fprintf(stderr, "no export %s\n", name); exit(2); }
  return w32_guest_call(C, f, W32_STDCALL, 0, a, n, 0);
}
// obj->vtbl[slot](obj, args...): a guest COM object (translated methods) or a host one (cells)
static uint32_t com(uint32_t obj, int slot, int n, ...) {
  uint32_t a[16];
  va_list ap;
  va_start(ap, n);
  a[0] = obj;
  for (int k = 0; k < n; k++) a[k + 1] = va_arg(ap, uint32_t);
  va_end(ap);
  uint32_t fn = w32_rd32(w32_rd32(obj) + 4 * (uint32_t)slot);
  if (rc32_lookup(fn)) return w32_guest_call(C, fn, W32_STDCALL, 0, a, n + 1, 0);
  uint32_t esp = (uint32_t)C->r[4], saved = C->ret_to;
  for (int k = n; k >= 0; k--) { C->r[4] = (uint32_t)(C->r[4] - 4); w32_wr32((uint32_t)C->r[4], a[k]); }
  C->r[4] = (uint32_t)(C->r[4] - 4);
  w32_wr32((uint32_t)C->r[4], 0x0badc0deu);
  rc32_icall(C, fn);
  CHECK((uint32_t)C->r[4] == esp);
  C->r[4] = esp;
  C->ret_to = saved;
  return (uint32_t)C->r[0];
}
enum { IU_Release = 2, BUF_GetBufferPointer = 3, BUF_GetBufferSize = 4 };
static uint32_t buf_ptr(uint32_t b) { return com(b, BUF_GetBufferPointer, 0); }
static uint32_t buf_size(uint32_t b) { return com(b, BUF_GetBufferSize, 0); }
static void show_errors(const char *what, uint32_t err) {
  if (err) fprintf(stderr, "%s: %.*s\n", what, (int)buf_size(err), (const char *)RC32_H(buf_ptr(err)));
}

// --- matrix ---------------------------------------------------------------------------------
static void mat_mul(const float *a, const float *b, float *o) {
  for (int i = 0; i < 4; i++)
    for (int j = 0; j < 4; j++) {
      double s = 0;
      for (int k = 0; k < 4; k++) s += (double)a[4 * i + k] * b[4 * k + j];
      o[4 * i + j] = (float)s;
    }
}
static int near(float a, float b, float tol) { return fabsf(a - b) <= tol * (1 + fabsf(b)); }

static void step_matrix(void) {
  const float A[16] = {2, 0.5f, -1, 0, 0.25f, 3, 0, 1, 1, -2, 4, 0.5f, 0, 1, 0.75f, 1};
  const float B[16] = {1, 2, 3, 4, -1, 0.5f, 2, 0, 0, 1, -3, 2, 0.5f, 0, 1, 1};
  uint32_t ga = gcopy(A, 64), gb = gcopy(B, 64), go = gbuf(64);
  float ref[16], got[16];

  CHECK(dx("D3DXMatrixMultiply", 3, go, ga, gb) == go);
  mat_mul(A, B, ref);
  memcpy(got, RC32_H(go), 64);
  int ok = 1;
  for (int k = 0; k < 16; k++) ok &= near(got[k], ref[k], 1e-6f);
  CHECK(ok);

  CHECK(dx("D3DXMatrixTranspose", 2, go, ga) == go);
  memcpy(got, RC32_H(go), 64);
  ok = 1;
  for (int i = 0; i < 4; i++)
    for (int j = 0; j < 4; j++) ok &= got[4 * i + j] == A[4 * j + i];
  CHECK(ok);

  uint32_t gdet = gbuf(4);
  CHECK(dx("D3DXMatrixInverse", 3, go, gdet, ga) == go);
  memcpy(got, RC32_H(go), 64);
  mat_mul(got, A, ref);
  ok = 1;
  for (int i = 0; i < 4; i++)
    for (int j = 0; j < 4; j++) ok &= fabsf(ref[4 * i + j] - (i == j)) < 1e-5f;
  CHECK(ok);
  // det(A) by cofactors, in double
  double m[4][4], det = 1;
  for (int k = 0; k < 16; k++) m[k / 4][k % 4] = A[k];
  for (int c = 0; c < 4; c++) {
    int p = c;
    for (int r = c + 1; r < 4; r++) if (fabs(m[r][c]) > fabs(m[p][c])) p = r;
    if (p != c) { for (int k = 0; k < 4; k++) { double t = m[c][k]; m[c][k] = m[p][k]; m[p][k] = t; } det = -det; }
    det *= m[c][c];
    for (int r = c + 1; r < 4; r++) { double f = m[r][c] / m[c][c]; for (int k = c; k < 4; k++) m[r][k] -= f * m[c][k]; }
  }
  fprintf(stderr, "matrix: det %g (host %g)\n", gf(gdet), det);
  CHECK(near(gf(gdet), (float)det, 1e-5f));
  const float S[16] = {1, 2, 3, 4, 2, 4, 6, 8, 0, 0, 1, 0, 0, 0, 0, 1};  // singular
  CHECK(dx("D3DXMatrixInverse", 3, go, 0, gcopy(S, 64)) == 0);

  const float V[4] = {1, -2, 0.5f, 1};
  uint32_t gv = gcopy(V, 16), gvo = gbuf(16);
  CHECK(dx("D3DXVec4Transform", 3, gvo, gv, ga) == gvo);
  ok = 1;
  for (int j = 0; j < 4; j++) {
    double s = 0;
    for (int i = 0; i < 4; i++) s += (double)V[i] * A[4 * i + j];
    ok &= near(gf(gvo + 4 * j), (float)s, 1e-6f);
  }
  CHECK(ok);
}

static void step_buffer(void) {
  uint32_t pp = gbuf(4);
  CHECK(dx("D3DXCreateBuffer", 2, 100, pp) == 0);
  uint32_t b = w32_rd32(pp);
  CHECK(b && buf_size(b) == 100 && buf_ptr(b) >= 0x10000);
  CHECK(com(b, IU_Release, 0) == 0);
}

// --- shaders --------------------------------------------------------------------------------
static void glsl_of(const char *what, uint32_t tok, uint32_t bytes) {
  char err[256];
  d3d9_sm *sm = d3d9_sm_parse((const uint32_t *)RC32_H(tok), bytes / 4, err, sizeof err);
  CHECK(sm != 0);
  if (!sm) { fprintf(stderr, "%s: %s\n", what, err); return; }
  char *g = d3d9_sm_glsl(sm, 0, err, sizeof err);
  CHECK(g != 0);
  if (!g) fprintf(stderr, "%s: %s\n", what, err);
  else if (probe_verbose) fprintf(stderr, "--- %s as GLSL:\n%s", what, g);
  free(g);
  d3d9_sm_free(sm);
}

static void step_asm(void) {
  static const char src[] =
      "vs_2_0\n"
      "dcl_position v0\n"
      "dcl_texcoord v1\n"
      "m4x4 oPos, v0, c0\n"
      "mov oT0, v1\n";
  uint32_t pp = gbuf(4), pe = gbuf(4);
  uint32_t hr = dx("D3DXAssembleShader", 7, gcopy(src, sizeof src - 1), (uint32_t)sizeof src - 1, 0, 0, 0, pp, pe);
  fprintf(stderr, "D3DXAssembleShader -> 0x%x\n", hr);
  show_errors("asm", w32_rd32(pe));
  CHECK(hr == 0);
  uint32_t b = w32_rd32(pp);
  if (hr || !b) return;
  uint32_t t = buf_ptr(b), n = buf_size(b) / 4;
  const uint32_t *w = (const uint32_t *)RC32_H(t);
  fprintf(stderr, "asm: %u tokens:", n);
  for (uint32_t k = 0; k < n; k++) fprintf(stderr, " %08x", w[k]);
  fprintf(stderr, "\n");
  // dcl_position v0; dcl_texcoord v1; m4x4 oPos, v0, c0; mov oT0, v1 (vs_2_0 tokens carry their
  // length in bits 24-27)
  static const uint32_t want[] = {
      0xfffe0200, 0x0200001f, 0x80000000, 0x900f0000, 0x0200001f, 0x80000005, 0x900f0001,
      0x03000014, 0xc00f0000, 0x90e40000, 0xa0e40000, 0x02000001, 0xe00f0000, 0x90e40001, 0x0000ffff};
  uint32_t skip = 1;  // a comment block (the assembler's creator string) may follow the version
  if (n > 1 && (w[1] & 0xffff) == 0xfffe) skip = 2 + (w[1] >> 16);
  CHECK(n == sizeof want / 4 + skip - 1 && w[0] == want[0] && !memcmp(w + skip, want + 1, sizeof want - 4));
  CHECK(dx("D3DXGetShaderVersion", 1, t) == 0xfffe0200);
  uint32_t sem = gbuf(8 * 16), cnt = gbuf(4);
  CHECK(dx("D3DXGetShaderInputSemantics", 3, t, sem, cnt) == 0);
  CHECK(w32_rd32(cnt) == 2 && w32_rd32(sem) == 0 && w32_rd32(sem + 8) == 5 && w32_rd32(sem + 12) == 0);
  glsl_of("vs_2_0 (assembled)", t, n * 4);
  com(b, IU_Release, 0);
}

// D3DXCONSTANT_DESC: Name, RegisterSet, RegisterIndex, RegisterCount, Class, Type, Rows, Columns,
// Elements, StructMembers, Bytes, DefaultValue
enum { CT_GetDesc = 5, CT_GetConstantDesc = 6, CT_GetSamplerIndex = 7, CT_GetConstantByName = 9 };
enum { RS_BOOL, RS_INT4, RS_FLOAT4, RS_SAMPLER };

static void step_hlsl(void) {
  static const char src[] =
      "sampler2D tex : register(s0);\n"
      "float4 tint;\n"
      "float4 scale_bias;\n"
      "float4 main(float2 uv : TEXCOORD0) : COLOR\n"
      "{\n"
      "    return tex2D(tex, uv) * tint * scale_bias.x + scale_bias.y;\n"
      "}\n";
  uint32_t pp = gbuf(4), pe = gbuf(4), pct = gbuf(4);
  uint32_t hr = dx("D3DXCompileShader", 10, gcopy(src, sizeof src - 1), (uint32_t)sizeof src - 1, 0, 0,
                   w32_gstr("main"), w32_gstr("ps_2_0"), 0, pp, pe, pct);
  fprintf(stderr, "D3DXCompileShader -> 0x%x\n", hr);
  show_errors("hlsl", w32_rd32(pe));
  CHECK(hr == 0);
  uint32_t b = w32_rd32(pp);
  if (hr || !b) return;
  uint32_t t = buf_ptr(b), n = buf_size(b);
  fprintf(stderr, "hlsl: %u bytes of ps_2_0 bytecode, version 0x%08x\n", n, w32_rd32(t));
  CHECK(w32_rd32(t) == 0xffff0200 && w32_rd32(t + n - 4) == 0x0000ffff);
  CHECK(w32_rd32(pct) != 0);  // the table D3DXCompileShader handed back
  if (w32_rd32(pct)) com(w32_rd32(pct), IU_Release, 0);

  uint32_t ppt = gbuf(4);
  CHECK(dx("D3DXGetShaderConstantTable", 2, t, ppt) == 0);
  uint32_t ct = w32_rd32(ppt);
  if (!ct) return;
  uint32_t desc = gbuf(12);
  CHECK(com(ct, CT_GetDesc, 1, desc) == 0);
  fprintf(stderr, "constant table: creator \"%s\", version 0x%x, %u constants\n",
          (const char *)RC32_H(w32_rd32(desc)), w32_rd32(desc + 4), w32_rd32(desc + 8));
  CHECK(w32_rd32(desc + 4) == 0xffff0200 && w32_rd32(desc + 8) == 3);
  static const struct { const char *name; uint32_t set, cls, rows, cols; } want[] = {
      {"tex", RS_SAMPLER, 4 /* D3DXPC_OBJECT */, 1, 1},
      {"tint", RS_FLOAT4, 1 /* D3DXPC_VECTOR */, 1, 4},
      {"scale_bias", RS_FLOAT4, 1, 1, 4},
  };
  uint32_t regs = 0;
  for (int k = 0; k < 3; k++) {
    uint32_t h = com(ct, CT_GetConstantByName, 2, 0, w32_gstr(want[k].name));
    CHECK(h != 0);
    if (!h) continue;
    uint32_t cd = gbuf(48), cnt = gbuf(4);
    w32_wr32(cnt, 1);
    CHECK(com(ct, CT_GetConstantDesc, 3, h, cd, cnt) == 0);
    fprintf(stderr, "  %-10s set %u reg %u x%u class %u type %u %ux%u\n", (const char *)RC32_H(w32_rd32(cd)),
            w32_rd32(cd + 4), w32_rd32(cd + 8), w32_rd32(cd + 12), w32_rd32(cd + 16), w32_rd32(cd + 20),
            w32_rd32(cd + 24), w32_rd32(cd + 28));
    CHECK(!strcmp((const char *)RC32_H(w32_rd32(cd)), want[k].name) && w32_rd32(cd + 4) == want[k].set &&
          w32_rd32(cd + 16) == want[k].cls && w32_rd32(cd + 24) == want[k].rows && w32_rd32(cd + 28) == want[k].cols);
    if (want[k].set == RS_FLOAT4) regs |= 1u << w32_rd32(cd + 8);
    if (want[k].set == RS_SAMPLER) CHECK(com(ct, CT_GetSamplerIndex, 1, h) == 0);
  }
  CHECK(regs == 3);  // tint and scale_bias in c0 and c1
  glsl_of("ps_2_0 (compiled)", t, n);
  com(ct, IU_Release, 0);
  com(b, IU_Release, 0);
}

// a 4x4 A8R8G8B8 DDS with two mip levels
static uint32_t dds_4x4(uint32_t *len) {
  uint8_t d[128 + 64 + 16] = {0};
  uint32_t h[32] = {0x20534444, 124, 0x1 | 0x2 | 0x4 | 0x1000 | 0x20000 | 0x8, 4, 4, 16, 0, 2};
  h[19] = 32; h[20] = 0x41; h[22] = 32;  // pixel format: RGB | ALPHAPIXELS, 32 bits
  h[23] = 0xff0000; h[24] = 0xff00; h[25] = 0xff; h[26] = 0xff000000;
  h[27] = 0x1000 | 0x400000 | 0x8;       // TEXTURE | MIPMAP | COMPLEX
  memcpy(d, h, sizeof h);
  for (int k = 0; k < 20; k++) memcpy(d + 128 + 4 * k, &(uint32_t){0xff000000u | (uint32_t)k * 0x0a0b0c}, 4);
  *len = sizeof d;
  return gcopy(d, sizeof d);
}

static void step_image(void) {
  uint32_t n, dds = dds_4x4(&n), info = gbuf(28);
  uint32_t hr = dx("D3DXGetImageInfoFromFileInMemory", 3, dds, n, info);
  fprintf(stderr, "D3DXGetImageInfoFromFileInMemory -> 0x%x: %ux%ux%u, %u mips, format %u, type %u, file %u\n", hr,
          w32_rd32(info), w32_rd32(info + 4), w32_rd32(info + 8), w32_rd32(info + 12), w32_rd32(info + 16),
          w32_rd32(info + 20), w32_rd32(info + 24));
  CHECK(hr == 0 && w32_rd32(info) == 4 && w32_rd32(info + 4) == 4 && w32_rd32(info + 12) == 2 &&
        w32_rd32(info + 16) == 21 /* A8R8G8B8 */ && w32_rd32(info + 20) == 3 /* RTYPE_TEXTURE */ &&
        w32_rd32(info + 24) == 4 /* D3DXIFF_DDS */);
}

#ifdef PROBE_D3D9
#include "d3d9_internal.h"
static uint32_t device(void) {
  uint32_t d3d = d3d9_create(32);
  CHECK(d3d != 0);
  uint32_t pp = gbuf(56), ppdev = gbuf(4);
  uint32_t ppv[14] = {64, 64, FMT_X8R8G8B8, 1, 0, 0, 1 /* DISCARD */, 0x10001, 1, 1, FMT_D24S8, 0, 0, 0};
  memcpy(RC32_H(pp), ppv, sizeof ppv);
  CHECK(com(d3d, 16 /* CreateDevice */, 6, 0, 1, 0x10001, 0x40 /* HW VP */, pp, ppdev) == 0);
  return w32_rd32(ppdev);
}

// ID3DXEffect (ID3DXBaseEffect first)
enum {
  FX_GetDesc = 3, FX_GetParameterByName = 9, FX_GetTechniqueByName = 13, FX_SetVector = 34, FX_GetVector = 35,
  FX_SetMatrix = 38, FX_SetTechnique = 58, FX_Begin = 63, FX_BeginPass = 64, FX_EndPass = 66, FX_End = 67,
};

static void step_effect(void) {
  uint32_t dev = device();
  if (!dev) return;
  uint32_t pv = gbuf(4);
  CHECK(dx("D3DXGetVertexShaderProfile", 1, dev) != 0);
  fprintf(stderr, "profiles: %s / %s\n", (const char *)RC32_H(dx("D3DXGetVertexShaderProfile", 1, dev)),
          (const char *)RC32_H(dx("D3DXGetPixelShaderProfile", 1, dev)));
  (void)pv;
  static const char fx[] =
      "float4x4 wvp;\n"
      "float4 color = {1, 0, 0, 1};\n"
      "float4 vs(float4 p : POSITION) : POSITION { return mul(p, wvp); }\n"
      "float4 ps() : COLOR { return color; }\n"
      "technique T\n"
      "{\n"
      "    pass P0\n"
      "    {\n"
      "        ZEnable = false;\n"
      "        VertexShader = compile vs_2_0 vs();\n"
      "        PixelShader = compile ps_2_0 ps();\n"
      "    }\n"
      "}\n";
  uint32_t ppfx = gbuf(4), pe = gbuf(4);
  uint32_t hr = dx("D3DXCreateEffect", 9, dev, gcopy(fx, sizeof fx - 1), (uint32_t)sizeof fx - 1, 0, 0, 0, 0, ppfx, pe);
  fprintf(stderr, "D3DXCreateEffect -> 0x%x\n", hr);
  show_errors("effect", w32_rd32(pe));
  CHECK(hr == 0);
  uint32_t e = w32_rd32(ppfx);
  if (!hr && e) {
    uint32_t d = gbuf(16);
    CHECK(com(e, FX_GetDesc, 1, d) == 0);
    fprintf(stderr, "effect: %u parameters, %u techniques, %u functions\n", w32_rd32(d + 4), w32_rd32(d + 8), w32_rd32(d + 12));
    CHECK(w32_rd32(d + 4) == 2 && w32_rd32(d + 8) == 1);
    uint32_t t = com(e, FX_GetTechniqueByName, 1, w32_gstr("T"));
    CHECK(t != 0 && com(e, FX_SetTechnique, 1, t) == 0);
    uint32_t col = com(e, FX_GetParameterByName, 2, 0, w32_gstr("color")), v = gbuf(16);
    CHECK(col != 0 && com(e, FX_GetVector, 2, col, v) == 0 && gf(v) == 1.0f && gf(v + 4) == 0.0f);
    float green[4] = {0, 1, 0, 1};
    CHECK(com(e, FX_SetVector, 2, col, gcopy(green, 16)) == 0 && com(e, FX_GetVector, 2, col, v) == 0 && gf(v + 4) == 1.0f);
    float id[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    CHECK(com(e, FX_SetMatrix, 2, com(e, FX_GetParameterByName, 2, 0, w32_gstr("wvp")), gcopy(id, 64)) == 0);
    uint32_t np = gbuf(4);
    CHECK(com(e, FX_Begin, 2, np, 0) == 0 && w32_rd32(np) == 1);
    CHECK(com(e, FX_BeginPass, 1, 0) == 0);
    CHECK(com(e, FX_EndPass, 0) == 0);
    CHECK(com(e, FX_End, 0) == 0);
    com(e, IU_Release, 0);
  }
  uint32_t n, dds = dds_4x4(&n), pt = gbuf(4);
  hr = dx("D3DXCreateTextureFromFileInMemory", 4, dev, dds, n, pt);
  fprintf(stderr, "D3DXCreateTextureFromFileInMemory -> 0x%x\n", hr);
  CHECK(hr == 0 && w32_rd32(pt) != 0);
}
#endif

int main(int argc, char **argv) {
  if (argc < 2) { fprintf(stderr, "usage: d3dx_probe <build dir> [-v] [step ...]\n"); return 2; }
  const char *want[16];
  int nwant = 0;
  for (int k = 2; k < argc; k++) {
    if (!strcmp(argv[k], "-v")) probe_verbose = 1;
    else if (nwant < 16) want[nwant++] = argv[k];
  }
  if (rc32_mem_init()) return 2;
  for (int k = 0; k < IMG_N; k++) {
    char p[1024];
    snprintf(p, sizeof p, "%s/in/%s", argv[1], probe_img[k].name);
    if (probe_image_map(&probe_img[k], p)) return 2;
    fprintf(stderr, "%s at 0x%x, entry 0x%x, %d functions\n", probe_img[k].name, probe_img[k].base,
            probe_img[k].entry, *probe_img[k].mod->table_len);
  }
  probe_bind();
  C = rc32_thread_ctx();
  // DllMains in load order (D3DCompiler_42 when d3dx loads it)
  if (!probe_attach(&probe_img[IMG_CRT]) || !probe_attach(&probe_img[IMG_D3DX])) return 1;

  static const struct { const char *name; void (*fn)(void); } steps[] = {
      {"matrix", step_matrix}, {"buffer", step_buffer}, {"asm", step_asm}, {"hlsl", step_hlsl},
      {"image", step_image},
#ifdef PROBE_D3D9
      {"effect", step_effect},
#endif
  };
  for (size_t s = 0; s < sizeof steps / sizeof steps[0]; s++) {
    int run = !nwant;
    for (int k = 0; k < nwant; k++) run |= !strcmp(want[k], steps[s].name);
    if (!run) continue;
    int f0 = fails;
    fprintf(stderr, "== %s\n", steps[s].name);
    steps[s].fn();
    fprintf(stderr, "== %s: %s\n", steps[s].name, fails == f0 ? "ok" : "FAILED");
  }
  fprintf(stderr, "%d checks, %d failed\n%s\n", checks, fails, fails ? "d3dx_probe: FAILURES" : "d3dx_probe: OK");
  return fails != 0;
}
