// The Win32 that d3dx9_42, D3DCompiler_42 and msvcr100 import, as far as d3dx_probe needs it: a
// minimal kernel32 (after win-x86/crt/tests/crt_probe.c, with modules answered from the mapped
// images), and gdi32 / advapi32 stubs. An import the probe lacks stops the run with its name
// (probe_images.c), so a run lists what the D3DX paths it exercises need.
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "probe.h"

static uint32_t arg(rc_ctx *c, int k) { return w32_rd32((uint32_t)(c->r[4] + 4 * k)); }
static void ret(rc_ctx *c, uint32_t v, int nargs) { c->r[0] = v; c->r[4] = (uint32_t)(c->r[4] + 4 * nargs); }
static uint32_t *teb32(rc_ctx *c, uint32_t off) { return (uint32_t *)RC32_H(c->fs + off); }
#define K(name, n) static void k_##name(rc_ctx *c)

// --- heap: blocks in the window with a 16-byte header (size) ---------------------------------
static uint32_t heap_alloc(uint32_t size) {
  uint32_t g = w32_galloc(size + 16);  // zero-filled
  if (!g) return 0;
  w32_wr32(g, size);
  return g + 16;
}
static uint32_t heap_size(uint32_t p) { return p ? w32_rd32(p - 16) : 0; }
static void heap_free(uint32_t p) { if (p) w32_gfree(p - 16, heap_size(p) + 16); }

K(HeapCreate, 3) { ret(c, 0x00ee0000u, 3); }
K(GetProcessHeap, 0) { ret(c, 0x00ee0000u, 0); }
K(HeapDestroy, 1) { ret(c, 1, 1); }
K(HeapAlloc, 3) { ret(c, heap_alloc(arg(c, 2)), 3); }
K(HeapFree, 3) { heap_free(arg(c, 2)); ret(c, 1, 3); }
K(HeapSize, 3) { ret(c, heap_size(arg(c, 2)), 3); }
K(HeapReAlloc, 4) {
  uint32_t old = arg(c, 2), n = arg(c, 3), p = heap_alloc(n), o = heap_size(old);
  if (p && old) memcpy(RC32_H(p), RC32_H(old), o < n ? o : n);
  heap_free(old);
  ret(c, p, 4);
}
K(HeapValidate, 3) { ret(c, 1, 3); }
K(HeapQueryInformation, 5) { ret(c, 0, 5); }
K(HeapSetInformation, 4) { ret(c, 1, 4); }
K(VirtualAlloc, 4) { ret(c, rc32_alloc(arg(c, 1)), 4); }
K(VirtualFree, 3) { ret(c, 1, 3); }
K(LocalAlloc, 2) { ret(c, heap_alloc(arg(c, 1)), 2); }
K(LocalFree, 1) { heap_free(arg(c, 0)); ret(c, 0, 1); }

// --- pointers, locks, TLS/FLS -----------------------------------------------------------------
K(EncodePointer, 1) { ret(c, arg(c, 0) ^ 0x5a5a5a5au, 1); }
K(DecodePointer, 1) { ret(c, arg(c, 0) ^ 0x5a5a5a5au, 1); }
// CRITICAL_SECTION (24 bytes): word 0 holds a host mutex index
static pthread_mutex_t cs_pool[4096];
static uint32_t cs_n;
static void cs_init(uint32_t cs) {
  memset(RC32_H(cs), 0, 24);
  uint32_t k = __atomic_fetch_add(&cs_n, 1, __ATOMIC_RELAXED);
  pthread_mutexattr_t a;
  pthread_mutexattr_init(&a);
  pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE);
  pthread_mutex_init(&cs_pool[k], &a);
  w32_wr32(cs, k + 1);
}
K(InitializeCriticalSectionAndSpinCount, 2) { cs_init(arg(c, 0)); ret(c, 1, 2); }
K(InitializeCriticalSection, 1) { cs_init(arg(c, 0)); ret(c, 0, 1); }
K(DeleteCriticalSection, 1) { ret(c, 0, 1); }
K(EnterCriticalSection, 1) { pthread_mutex_lock(&cs_pool[w32_rd32(arg(c, 0)) - 1]); ret(c, 0, 1); }
K(LeaveCriticalSection, 1) { pthread_mutex_unlock(&cs_pool[w32_rd32(arg(c, 0)) - 1]); ret(c, 0, 1); }
K(TryEnterCriticalSection, 1) { ret(c, pthread_mutex_trylock(&cs_pool[w32_rd32(arg(c, 0)) - 1]) == 0, 1); }
K(InterlockedIncrement, 1) { ret(c, __atomic_add_fetch((uint32_t *)RC32_H(arg(c, 0)), 1, __ATOMIC_SEQ_CST), 1); }
K(InterlockedDecrement, 1) { ret(c, __atomic_sub_fetch((uint32_t *)RC32_H(arg(c, 0)), 1, __ATOMIC_SEQ_CST), 1); }
K(InterlockedExchange, 2) { ret(c, __atomic_exchange_n((uint32_t *)RC32_H(arg(c, 0)), arg(c, 1), __ATOMIC_SEQ_CST), 2); }
K(InterlockedCompareExchange, 3) {
  uint32_t want = arg(c, 2);
  __atomic_compare_exchange_n((uint32_t *)RC32_H(arg(c, 0)), &want, arg(c, 1), 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
  ret(c, want, 3);
}
static uint64_t tls_used;
static uint32_t tls_new(void) {
  for (int k = 0; k < 64; k++)
    if (!(__atomic_fetch_or(&tls_used, 1ull << k, __ATOMIC_SEQ_CST) & (1ull << k))) return (uint32_t)k;
  return 0xffffffffu;
}
K(TlsAlloc, 0) { ret(c, tls_new(), 0); }
K(TlsFree, 1) { ret(c, 1, 1); }
K(TlsGetValue, 1) { uint32_t k = arg(c, 0); *teb32(c, RC32_TEB_LASTERR) = 0; ret(c, k < 64 ? *teb32(c, RC32_TEB_TLSSLOTS + 4 * k) : 0, 1); }
K(TlsSetValue, 2) { uint32_t k = arg(c, 0); if (k < 64) *teb32(c, RC32_TEB_TLSSLOTS + 4 * k) = arg(c, 1); ret(c, k < 64, 2); }
K(GetLastError, 0) { ret(c, *teb32(c, RC32_TEB_LASTERR), 0); }
K(SetLastError, 1) { *teb32(c, RC32_TEB_LASTERR) = arg(c, 0); ret(c, 0, 1); }
// FLS through GetProcAddress (stdcall host functions in thunk cells), on the TLS slots
static uint64_t f_FlsAlloc(rc_ctx *c, uint32_t s, const uint32_t *a) { (void)c; (void)s; (void)a; return tls_new(); }
static uint64_t f_FlsGetValue(rc_ctx *c, uint32_t s, const uint32_t *a) { (void)s; return a[0] < 64 ? *teb32(c, RC32_TEB_TLSSLOTS + 4 * a[0]) : 0; }
static uint64_t f_FlsSetValue(rc_ctx *c, uint32_t s, const uint32_t *a) { (void)s; if (a[0] < 64) *teb32(c, RC32_TEB_TLSSLOTS + 4 * a[0]) = a[1]; return a[0] < 64; }
static uint64_t f_FlsFree(rc_ctx *c, uint32_t s, const uint32_t *a) { (void)c; (void)s; (void)a; return 1; }

// --- modules: the mapped images answer to their names ----------------------------------------
#define HMOD_KERNEL32 0x7b000000u
static void narrow(uint32_t w, char *a, size_t n) {
  size_t k = 0;
  for (; k < n - 1 && *(uint16_t *)RC32_H(w + 2 * k); k++) a[k] = (char)*(uint16_t *)RC32_H(w + 2 * k);
  a[k] = 0;
}
static uint32_t module(const char *n, int load) {
  if (!n) return probe_img[IMG_D3DX].base;  // "the process": the probe has no exe
  const char *s = strrchr(n, '\\');
  s = s ? s + 1 : n;
  if (!strncasecmp(s, "kernel32", 8)) return HMOD_KERNEL32;
  probe_image *m = probe_image_named(s);
  if (m && load) probe_attach(m);
  if (probe_verbose || (load && !m)) fprintf(stderr, "  %s(%s) -> 0x%x\n", load ? "LoadLibrary" : "GetModuleHandle", s, m ? m->base : 0);
  return m ? m->base : 0;
}
K(GetModuleHandleA, 1) { ret(c, module(arg(c, 0) ? (const char *)RC32_H(arg(c, 0)) : 0, 0), 1); }
K(GetModuleHandleW, 1) {
  char a[260];
  if (arg(c, 0)) narrow(arg(c, 0), a, sizeof a);
  ret(c, module(arg(c, 0) ? a : 0, 0), 1);
}
K(LoadLibraryA, 1) { ret(c, module((const char *)RC32_H(arg(c, 0)), 1), 1); }
K(LoadLibraryW, 1) {
  char a[260];
  narrow(arg(c, 0), a, sizeof a);
  ret(c, module(a, 1), 1);
}
K(LoadLibraryExA, 3) { ret(c, module((const char *)RC32_H(arg(c, 0)), 1), 3); }
K(FreeLibrary, 1) { ret(c, 1, 1); }
K(GetProcAddress, 2) {
  uint32_t mod = arg(c, 0), np = arg(c, 1);
  const char *n = np >= 0x10000 ? (const char *)RC32_H(np) : "#ordinal";
  static const struct { const char *n; int words; w32_hostfn fn; } fns[] = {
      {"FlsAlloc", 1, f_FlsAlloc}, {"FlsGetValue", 1, f_FlsGetValue},
      {"FlsSetValue", 2, f_FlsSetValue}, {"FlsFree", 1, f_FlsFree}};
  uint32_t r = 0;
  probe_image *m = probe_image_at(mod);
  if (mod == HMOD_KERNEL32) {
    for (size_t k = 0; k < sizeof fns / sizeof fns[0]; k++)
      if (!strcmp(fns[k].n, n)) r = w32_hostfn_new(fns[k].n, W32_STDCALL, fns[k].words, fns[k].fn, 0);
  } else if (m) {
    r = np >= 0x10000 ? probe_export(m, n) : 0;
  }
  if (probe_verbose || (m && !r)) fprintf(stderr, "  GetProcAddress(0x%x, %s) -> 0x%x\n", mod, n, r);
  ret(c, r, 2);
}
static const char exe_path[] = "C:\\Rocksmith2014\\Rocksmith2014.exe";
K(GetModuleFileNameW, 3) {
  probe_image *m = probe_image_at(arg(c, 0));
  char p[300];
  snprintf(p, sizeof p, m ? "C:\\Windows\\SysWOW64\\%s" : "%s", m ? m->name : exe_path);
  uint32_t b = arg(c, 1), n = arg(c, 2), k = 0;
  for (; k < n - 1 && p[k]; k++) ((uint16_t *)RC32_H(b))[k] = (uint8_t)p[k];
  ((uint16_t *)RC32_H(b))[k] = 0;
  ret(c, k, 3);
}
K(GetModuleFileNameA, 3) {
  probe_image *m = probe_image_at(arg(c, 0));
  char p[300];
  snprintf(p, sizeof p, m ? "C:\\Windows\\SysWOW64\\%s" : "%s", m ? m->name : exe_path);
  uint32_t b = arg(c, 1), n = arg(c, 2), k = (uint32_t)strlen(p) < n - 1 ? (uint32_t)strlen(p) : n - 1;
  memcpy(RC32_H(b), p, k);
  ((char *)RC32_H(b))[k] = 0;
  ret(c, k, 3);
}
K(DisableThreadLibraryCalls, 1) { ret(c, 1, 1); }

// --- process / environment ----------------------------------------------------------------
static uint32_t wstr(const char *s) {
  size_t n = strlen(s) + 1;
  uint32_t g = rc32_alloc(2 * (uint32_t)n);
  for (size_t k = 0; k < n; k++) ((uint16_t *)RC32_H(g))[k] = (uint8_t)s[k];
  return g;
}
K(GetCommandLineA, 0) { static uint32_t g; if (!g) g = w32_gstr("Rocksmith2014.exe -probe"); ret(c, g, 0); }
K(GetCommandLineW, 0) { static uint32_t g; if (!g) g = wstr("Rocksmith2014.exe -probe"); ret(c, g, 0); }
K(GetEnvironmentStringsW, 0) {  // "A=1\0B=2\0\0"
  static const char e[] = "LANG=C\0PROBE=1\0";
  uint32_t g = rc32_alloc(2 * sizeof e);
  for (size_t k = 0; k < sizeof e; k++) ((uint16_t *)RC32_H(g))[k] = (uint8_t)e[k];
  ret(c, g, 0);
}
K(FreeEnvironmentStringsW, 1) { ret(c, 1, 1); }
K(GetEnvironmentVariableA, 3) { *teb32(c, RC32_TEB_LASTERR) = 203; ret(c, 0, 3); }  // ERROR_ENVVAR_NOT_FOUND
K(GetStartupInfoW, 1) { memset(RC32_H(arg(c, 0)), 0, 68); w32_wr32(arg(c, 0), 68); ret(c, 0, 1); }
K(GetStdHandle, 1) { uint32_t id = arg(c, 0); ret(c, id == 0xfffffff6u ? 0x100 : id == 0xfffffff5u ? 0x101 : id == 0xfffffff4u ? 0x102 : 0xffffffffu, 1); }
K(GetFileType, 1) { ret(c, 2, 1); }  // FILE_TYPE_CHAR
K(SetHandleCount, 1) { ret(c, arg(c, 0), 1); }
K(GetACP, 0) { ret(c, 1252, 0); }
K(GetOEMCP, 0) { ret(c, 437, 0); }
K(IsValidCodePage, 1) { ret(c, 1, 1); }
K(GetCPInfo, 2) {  // single-byte code page: MaxCharSize 1, default char '?', no lead bytes
  uint32_t i = arg(c, 1);
  memset(RC32_H(i), 0, 20);
  w32_wr32(i, 1);
  *(uint8_t *)RC32_H(i + 4) = '?';
  ret(c, 1, 2);
}
static void os_version(uint32_t v) {  // OSVERSIONINFO(A|W): Windows 7 SP1
  w32_wr32(v + 4, 6); w32_wr32(v + 8, 1); w32_wr32(v + 12, 7601); w32_wr32(v + 16, 2);
}
K(GetVersionExW, 1) { os_version(arg(c, 0)); ret(c, 1, 1); }
K(GetVersionExA, 1) { os_version(arg(c, 0)); ret(c, 1, 1); }
K(GetVersion, 0) { ret(c, 0x1db10106u, 0); }
K(IsProcessorFeaturePresent, 1) { uint32_t f = arg(c, 0); ret(c, f == 6 || f == 10 || f == 3, 1); }  // SSE, SSE2, ...
K(IsDebuggerPresent, 0) { ret(c, 0, 0); }
K(OutputDebugStringA, 1) { if (probe_verbose) fprintf(stderr, "  debug: %s", (const char *)RC32_H(arg(c, 0))); ret(c, 0, 1); }
K(GetCurrentProcessId, 0) { ret(c, *teb32(c, RC32_TEB_PID), 0); }
K(GetCurrentThreadId, 0) { ret(c, *teb32(c, RC32_TEB_TID), 0); }
K(GetCurrentProcess, 0) { ret(c, 0xffffffffu, 0); }
K(GetCurrentThread, 0) { ret(c, 0xfffffffeu, 0); }
K(GetTickCount, 0) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); ret(c, (uint32_t)(t.tv_sec * 1000 + t.tv_nsec / 1000000), 0); }
K(QueryPerformanceCounter, 1) { w32_wr64(arg(c, 0), clock_gettime_nsec_np(CLOCK_MONOTONIC) / 100); ret(c, 1, 1); }
K(QueryPerformanceFrequency, 1) { w32_wr64(arg(c, 0), 10000000); ret(c, 1, 1); }
K(GetSystemTimeAsFileTime, 1) {
  struct timeval tv;
  gettimeofday(&tv, 0);
  w32_wr64(arg(c, 0), ((uint64_t)tv.tv_sec + 11644473600ull) * 10000000ull + (uint64_t)tv.tv_usec * 10);
  ret(c, 0, 1);
}
K(SetUnhandledExceptionFilter, 1) { ret(c, 0, 1); }
K(UnhandledExceptionFilter, 1) { ret(c, 0, 1); }
K(Sleep, 1) { usleep(arg(c, 0) * 1000); ret(c, 0, 1); }
K(InitializeSListHead, 1) { w32_wr64(arg(c, 0), 0); ret(c, 0, 1); }
K(GetSystemInfo, 1) {
  uint32_t s = arg(c, 0);
  memset(RC32_H(s), 0, 36);
  w32_wr32(s + 4, 4096); w32_wr32(s + 8, 0x10000); w32_wr32(s + 12, 0x7ffeffff);
  w32_wr32(s + 16, 0xff); w32_wr32(s + 20, 8); w32_wr32(s + 24, 586); w32_wr32(s + 28, 0x10000);
  ret(c, 0, 1);
}
K(GetThreadTimes, 5) {  // creation, exit, kernel, user (FILETIMEs): user time only
  struct timespec t;
  clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t);
  for (int k = 1; k < 5; k++) w32_wr64(arg(c, k), 0);
  w32_wr64(arg(c, 4), (uint64_t)t.tv_sec * 10000000ull + (uint64_t)t.tv_nsec / 100);
  ret(c, 1, 5);
}
K(GetProcessAffinityMask, 3) { w32_wr32(arg(c, 1), 0xff); w32_wr32(arg(c, 2), 0xff); ret(c, 1, 3); }
// the console: only writes, to the host's stdout / stderr
K(WriteFile, 5) {
  uint32_t h = arg(c, 0), b = arg(c, 1), n = arg(c, 2);
  ssize_t w = write(h == 0x102 ? 2 : 1, RC32_H(b), n);
  if (arg(c, 3)) w32_wr32(arg(c, 3), w < 0 ? 0 : (uint32_t)w);
  ret(c, w >= 0, 5);
}
K(GetConsoleMode, 2) { ret(c, 0, 2); }  // not a console: the CRT writes bytes
K(GetConsoleCP, 0) { ret(c, 1252, 0); }
K(FlushFileBuffers, 1) { ret(c, 1, 1); }
K(SetFilePointer, 4) { ret(c, 0xffffffffu, 4); }
K(CloseHandle, 1) { ret(c, 1, 1); }
K(GetFullPathNameA, 4) {  // relative names under C:\ (the compiler names its in-memory source)
  const char *n = (const char *)RC32_H(arg(c, 0));
  char p[600];
  snprintf(p, sizeof p, n[0] && n[1] == ':' ? "%s" : "C:\\%s", n);
  uint32_t len = (uint32_t)strlen(p), cap = arg(c, 1), b = arg(c, 2);
  if (len + 1 > cap) { ret(c, len + 1, 4); return; }
  memcpy(RC32_H(b), p, len + 1);
  if (arg(c, 3)) { const char *s = strrchr(p, '\\'); w32_wr32(arg(c, 3), b + (uint32_t)(s ? s + 1 - p : 0)); }
  ret(c, len, 4);
}
// code pages: Windows-1252 as Latin-1, enough for ASCII
K(MultiByteToWideChar, 6) {
  uint32_t src = arg(c, 2), dst = arg(c, 4);
  int32_t n = (int32_t)arg(c, 3), cap = (int32_t)arg(c, 5);
  if (n < 0) n = (int32_t)strlen((const char *)RC32_H(src)) + 1;
  if (cap && dst)
    for (int32_t k = 0; k < n && k < cap; k++) ((uint16_t *)RC32_H(dst))[k] = ((uint8_t *)RC32_H(src))[k];
  ret(c, (uint32_t)(cap && cap < n ? cap : n), 6);
}
K(WideCharToMultiByte, 8) {
  uint32_t src = arg(c, 2), dst = arg(c, 4);
  int32_t n = (int32_t)arg(c, 3), cap = (int32_t)arg(c, 5);
  if (n < 0) { n = 0; while (((uint16_t *)RC32_H(src))[n]) n++; n++; }
  if (cap && dst)
    for (int32_t k = 0; k < n && k < cap; k++) {
      uint16_t w = ((uint16_t *)RC32_H(src))[k];
      ((uint8_t *)RC32_H(dst))[k] = w < 256 ? (uint8_t)w : '?';
    }
  ret(c, (uint32_t)(cap && cap < n ? cap : n), 8);
}
K(GetStringTypeW, 4) {  // C1_* for ASCII
  uint32_t src = arg(c, 1), out = arg(c, 3);
  int32_t n = (int32_t)arg(c, 2);
  if (n < 0) { n = 0; while (((uint16_t *)RC32_H(src))[n]) n++; n++; }
  for (int32_t k = 0; k < n; k++) {
    uint16_t ch = ((uint16_t *)RC32_H(src))[k], t = 0;
    if (ch < 128) {
      if (ch >= 'A' && ch <= 'Z') t |= 0x1 | 0x100;       // C1_UPPER | C1_ALPHA
      if (ch >= 'a' && ch <= 'z') t |= 0x2 | 0x100;       // C1_LOWER | C1_ALPHA
      if (ch >= '0' && ch <= '9') t |= 0x4;               // C1_DIGIT
      if (ch == ' ' || (ch >= 9 && ch <= 13)) t |= 0x8;   // C1_SPACE
      if (ch == ' ' || ch == 9) t |= 0x40;                // C1_BLANK
      if (ch > 32 && ch < 127 && !(t & 0x104)) t |= 0x10; // C1_PUNCT
      if (ch < 32 || ch == 127) t |= 0x20;                // C1_CNTRL
      if ((ch >= '0' && ch <= '9') || ((ch | 0x20) >= 'a' && (ch | 0x20) <= 'f')) t |= 0x80;  // C1_XDIGIT
    }
    ((uint16_t *)RC32_H(out))[k] = t;
  }
  ret(c, 1, 4);
}
K(LCMapStringW, 6) {  // LCMAP_LOWERCASE 0x100 / UPPERCASE 0x200, ASCII
  uint32_t fl = arg(c, 1), src = arg(c, 2), dst = arg(c, 4);
  int32_t n = (int32_t)arg(c, 3), cap = (int32_t)arg(c, 5);
  if (n < 0) { n = 0; while (((uint16_t *)RC32_H(src))[n]) n++; n++; }
  if (cap && dst)
    for (int32_t k = 0; k < n && k < cap; k++) {
      uint16_t ch = ((uint16_t *)RC32_H(src))[k];
      if ((fl & 0x100) && ch >= 'A' && ch <= 'Z') ch |= 0x20;
      if ((fl & 0x200) && ch >= 'a' && ch <= 'z') ch &= ~0x20;
      ((uint16_t *)RC32_H(dst))[k] = ch;
    }
  ret(c, (uint32_t)n, 6);
}

// --- advapi32: no registry (d3dx reads its debug settings there) ---------------------------
K(RegOpenKeyA, 3) { ret(c, 2, 3); }  // ERROR_FILE_NOT_FOUND
K(RegOpenKeyExA, 5) { ret(c, 2, 5); }
K(RegQueryValueExA, 6) { ret(c, 2, 6); }
K(RegCloseKey, 1) { ret(c, 0, 1); }

void rc32_imp_RaiseException(rc_ctx *c);
void rc32_imp_RtlUnwind(rc_ctx *c);

#define E(dll, name) {dll "!" #name, k_##name}
#define K32(name) E("kernel32.dll", name)
static const struct { const char *name; rc_fn fn; } fns[] = {
    K32(HeapCreate), K32(GetProcessHeap), K32(HeapDestroy), K32(HeapAlloc), K32(HeapFree), K32(HeapSize),
    K32(HeapReAlloc), K32(HeapValidate), K32(HeapQueryInformation), K32(HeapSetInformation),
    K32(VirtualAlloc), K32(VirtualFree), K32(LocalAlloc), K32(LocalFree),
    K32(EncodePointer), K32(DecodePointer), K32(InitializeCriticalSectionAndSpinCount),
    K32(InitializeCriticalSection), K32(DeleteCriticalSection), K32(EnterCriticalSection),
    K32(LeaveCriticalSection), K32(TryEnterCriticalSection), K32(InterlockedIncrement),
    K32(InterlockedDecrement), K32(InterlockedExchange), K32(InterlockedCompareExchange), K32(TlsAlloc),
    K32(TlsFree), K32(TlsGetValue), K32(TlsSetValue), K32(GetLastError), K32(SetLastError),
    K32(GetModuleHandleW), K32(GetModuleHandleA), K32(GetProcAddress), K32(GetModuleFileNameW),
    K32(GetModuleFileNameA), K32(LoadLibraryW), K32(LoadLibraryA), K32(LoadLibraryExA), K32(FreeLibrary),
    K32(DisableThreadLibraryCalls), K32(GetCommandLineA),
    K32(GetCommandLineW), K32(GetEnvironmentStringsW), K32(FreeEnvironmentStringsW),
    K32(GetEnvironmentVariableA), K32(GetStartupInfoW),
    K32(GetStdHandle), K32(GetFileType), K32(SetHandleCount), K32(GetACP), K32(GetOEMCP), K32(IsValidCodePage),
    K32(GetCPInfo), K32(GetVersionExW), K32(GetVersionExA), K32(GetVersion), K32(IsProcessorFeaturePresent),
    K32(IsDebuggerPresent), K32(OutputDebugStringA),
    K32(GetCurrentProcessId), K32(GetCurrentThreadId), K32(GetCurrentProcess), K32(GetCurrentThread),
    K32(GetTickCount), K32(QueryPerformanceCounter), K32(QueryPerformanceFrequency), K32(GetSystemTimeAsFileTime),
    K32(SetUnhandledExceptionFilter), K32(UnhandledExceptionFilter), K32(Sleep), K32(InitializeSListHead),
    K32(GetSystemInfo), K32(GetThreadTimes), K32(GetProcessAffinityMask), K32(WriteFile), K32(GetConsoleMode),
    K32(GetConsoleCP), K32(FlushFileBuffers), K32(SetFilePointer), K32(CloseHandle), K32(GetFullPathNameA),
    K32(MultiByteToWideChar), K32(WideCharToMultiByte), K32(GetStringTypeW), K32(LCMapStringW),
    E("advapi32.dll", RegOpenKeyA), E("advapi32.dll", RegOpenKeyExA), E("advapi32.dll", RegQueryValueExA),
    E("advapi32.dll", RegCloseKey),
    {"kernel32.dll!RaiseException", rc32_imp_RaiseException},
    {"kernel32.dll!RtlUnwind", rc32_imp_RtlUnwind},
};

rc_fn probe_win32(const char *name) {
  static uint8_t seen[sizeof fns / sizeof fns[0]];
  for (size_t j = 0; j < sizeof fns / sizeof fns[0]; j++)
    if (!strcasecmp(fns[j].name, name)) {
      if (!seen[j]) {
        seen[j] = 1;
        if (probe_verbose) fprintf(stderr, "  first call: %s\n", name);
      }
      return fns[j].fn;
    }
  return 0;
}
