// d3d9.dll: Direct3DCreate9 and the D3DPERF_* markers. The device itself is shims/d3d9/
// (Direct3D 9 on libRSOpenGL, so on the Metal core).
//
// The game loads D3D9.DLL itself (LoadLibrary + GetProcAddress "Direct3DCreate9"), so these are
// reached through GetProcAddress cells as well as through the import table; each is an ordinary
// stdcall shim (arguments at guest esp, removed here).
#include <stdio.h>

#include "d3d9/d3d9_internal.h"
#include "win32_shims.h"

// IDirect3D9 *WINAPI Direct3DCreate9(UINT SDKVersion)
void shim_Direct3DCreate9(rc_ctx *c) {  // also the D3D9 layer's test entry (test/d3d9_test.c)
  uint32_t sdk = w32_arg(c, 0);
  uint32_t d3d = d3d9_create(sdk);
  fprintf(stderr, "w32: Direct3DCreate9(SDK=%u) -> 0x%x\n", sdk, d3d);
  w32_ret(c, d3d, 1);
}
static void d_Direct3DCreate9Ex(rc_ctx *c) {  // no 9Ex device: the caller falls back to Direct3DCreate9
  if (w32_arg(c, 1)) w32_wr32(w32_arg(c, 1), 0);
  w32_ret(c, 0x8876086cu, 2);  // D3DERR_NOTAVAILABLE
}
// int WINAPI D3DPERF_BeginEvent(D3DCOLOR, LPCWSTR): the nesting level; markers are dropped
static int perf_depth;
static void d_D3DPERF_BeginEvent(rc_ctx *c) { w32_ret(c, (uint32_t)perf_depth++, 2); }
static void d_D3DPERF_EndEvent(rc_ctx *c) { w32_ret(c, (uint32_t)(perf_depth > 0 ? --perf_depth : -1), 0); }
// DWORD WINAPI D3DPERF_GetStatus(void): 0 = no profiler attached
static void d_D3DPERF_GetStatus(rc_ctx *c) { w32_ret(c, 0, 0); }
static void d_D3DPERF_SetMarker(rc_ctx *c) { w32_ret(c, 0, 2); }
static void d_D3DPERF_SetRegion(rc_ctx *c) { w32_ret(c, 0, 2); }
static void d_D3DPERF_SetOptions(rc_ctx *c) { w32_ret(c, 0, 1); }
static void d_D3DPERF_QueryRepeatFrame(rc_ctx *c) { w32_ret(c, 0, 0); }

#define E(n) {#n, d_##n}
static const w32_export fns[] = {
    {"Direct3DCreate9", shim_Direct3DCreate9}, E(Direct3DCreate9Ex), E(D3DPERF_BeginEvent), E(D3DPERF_EndEvent), E(D3DPERF_GetStatus),
    E(D3DPERF_SetMarker), E(D3DPERF_SetRegion), E(D3DPERF_SetOptions), E(D3DPERF_QueryRepeatFrame),
};
W32_DLL(w32_dll_d3d9, "d3d9.dll", fns, 0);
