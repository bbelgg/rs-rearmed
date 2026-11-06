// Runs the translated msvcr100.dll (and msvcp100.dll) without the game, through the runtime's
// image registry as the game will: maps each DLL at its preferred base (rc32_image_add), binds
// the imports between them (msvcp100's 127 msvcr100 imports to msvcr100's exports,
// rc32_images_bind), runs their DllMains in dependency order (DLL_PROCESS_ATTACH: the CRT's own
// start-up: heap, locks, TLS/FLS, locale, stdio, environment, command line;
// rc32_images_init), then calls exports the way game code would, comparing the results with the
// host's, and ends with DLL_PROCESS_DETACH (rc32_images_fini, which flushes stdio). Every
// kernel32 import the DLLs make goes through the minimal kernel32 below (rc32_host_import); an
// import it does not have stops the run with its name, so a run lists the kernel32 the
// runtime needs, in the order it needs it.
//
// usage: crt_probe msvcr100.dll [msvcp100.dll] [-v] [-bench] [-eh eh.dll]   (the DLLs are build
// inputs; never committed). The native math overrides are bound as in the game
// (w32_crtmath_register) and compared with the translated exports (math_probe.c); -bench also
// times both. eh.dll: hand-written exception code (build.sh makes it), run on the real CRT.
// Built against the DLLs' translations (gen_image_pe.py --module msvcr100 / msvcp100): build.sh
// beside this file.
#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "rc32.h"
#include "win32_guestobj.h"
#include "win32_shims.h"

int w32_log;  // win32_guestobj.c's log level (win32_ksync.c has it in the game)
// math_probe.c: the native math overrides (win32_crtmath.c) against the translated exports
int crt_math_probe(rc_ctx *c, const rc32_module *crt, const rc32_module *cpp, int verbose, int bench);
void crt_math_probe_setup(void);

static uint32_t arg(rc_ctx *c, int k) { return w32_rd32((uint32_t)(c->r[4] + 4 * k)); }
static void ret(rc_ctx *c, uint32_t v, int nargs) { c->r[0] = v; c->r[4] = (uint32_t)(c->r[4] + 4 * nargs); }
static uint32_t *teb32(rc_ctx *c, uint32_t off) { return (uint32_t *)RC32_H(c->fs + off); }
static int verbose;
#define K(name, n) static void k_##name(rc_ctx *c)

// --- heap: blocks in the window with a 16-byte header (size, class) ---------------------------
static uint32_t heap_alloc(uint32_t size, int zero) {
  uint32_t g = w32_galloc(size + 16);
  if (!g) return 0;
  w32_wr32(g, size);
  (void)zero;  // w32_galloc zero-fills
  return g + 16;
}
static uint32_t heap_size(uint32_t p) { return p ? w32_rd32(p - 16) : 0; }
static void heap_free(uint32_t p) { if (p) w32_gfree(p - 16, heap_size(p) + 16); }

K(HeapCreate, 3) { ret(c, 0x00ee0000u, 3); }
K(GetProcessHeap, 0) { ret(c, 0x00ee0000u, 0); }
K(HeapDestroy, 1) { ret(c, 1, 1); }
K(HeapAlloc, 3) { ret(c, heap_alloc(arg(c, 2), 1), 3); }
K(HeapFree, 3) { heap_free(arg(c, 2)); ret(c, 1, 3); }
K(HeapSize, 3) { ret(c, heap_size(arg(c, 2)), 3); }
K(HeapReAlloc, 4) {
  uint32_t old = arg(c, 2), n = arg(c, 3), p = heap_alloc(n, 1), o = heap_size(old);
  if (p && old) memcpy(RC32_H(p), RC32_H(old), o < n ? o : n);
  heap_free(old);
  ret(c, p, 4);
}
K(HeapValidate, 3) { ret(c, 1, 3); }
K(HeapQueryInformation, 5) { ret(c, 0, 5); }
K(VirtualAlloc, 4) { ret(c, rc32_alloc(arg(c, 1)), 4); }
K(VirtualFree, 3) { ret(c, 1, 3); }

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
static void tls_alloc(rc_ctx *c, int n) {
  for (int k = 0; k < 64; k++)
    if (!(__atomic_fetch_or(&tls_used, 1ull << k, __ATOMIC_SEQ_CST) & (1ull << k))) { ret(c, k, n); return; }
  ret(c, 0xffffffffu, n);
}
K(TlsAlloc, 0) { tls_alloc(c, 0); }
K(TlsFree, 1) { ret(c, 1, 1); }
K(TlsGetValue, 1) { uint32_t k = arg(c, 0); *teb32(c, RC32_TEB_LASTERR) = 0; ret(c, k < 64 ? *teb32(c, RC32_TEB_TLSSLOTS + 4 * k) : 0, 1); }
K(TlsSetValue, 2) { uint32_t k = arg(c, 0); if (k < 64) *teb32(c, RC32_TEB_TLSSLOTS + 4 * k) = arg(c, 1); ret(c, k < 64, 2); }
K(GetLastError, 0) { ret(c, *teb32(c, RC32_TEB_LASTERR), 0); }
K(SetLastError, 1) { *teb32(c, RC32_TEB_LASTERR) = arg(c, 0); ret(c, 0, 1); }
// FLS through GetProcAddress (stdcall host functions in thunk cells), on the TLS slots
static uint64_t f_FlsAlloc(rc_ctx *c, uint32_t s, const uint32_t *a) {
  (void)s; (void)a;
  for (int k = 0; k < 64; k++)
    if (!(__atomic_fetch_or(&tls_used, 1ull << k, __ATOMIC_SEQ_CST) & (1ull << k))) return k;
  return 0xffffffffu;
}
static uint64_t f_FlsGetValue(rc_ctx *c, uint32_t s, const uint32_t *a) { (void)s; return a[0] < 64 ? *teb32(c, RC32_TEB_TLSSLOTS + 4 * a[0]) : 0; }
static uint64_t f_FlsSetValue(rc_ctx *c, uint32_t s, const uint32_t *a) { (void)s; if (a[0] < 64) *teb32(c, RC32_TEB_TLSSLOTS + 4 * a[0]) = a[1]; return a[0] < 64; }
static uint64_t f_FlsFree(rc_ctx *c, uint32_t s, const uint32_t *a) { (void)c; (void)s; (void)a; return 1; }

// --- modules ----------------------------------------------------------------------------------
#define HMOD_KERNEL32 0x7b000000u
static uint32_t crt_base, cpp_base;
// a module handle is the image's base (rc32_image_base); no main image here: 0 is msvcr100
static uint32_t module_handle(const char *n) {
  if (!n) return rc32_image_base(0) ? rc32_image_base(0) : crt_base;
  if (!strncasecmp(n, "kernel32", 8)) return HMOD_KERNEL32;
  return rc32_image_base(n);
}
K(GetModuleHandleW, 1) {
  uint32_t n = arg(c, 0);
  char a[64] = {0};
  for (int k = 0; n && k < 63 && *(uint16_t *)RC32_H(n + 2 * k); k++) a[k] = (char)*(uint16_t *)RC32_H(n + 2 * k);
  uint32_t r = module_handle(n ? a : 0);
  if (verbose) fprintf(stderr, "  GetModuleHandleW(%s) -> 0x%x\n", n ? a : "0", r);
  ret(c, r, 1);
}
K(GetModuleHandleA, 1) { ret(c, module_handle(arg(c, 0) ? (const char *)RC32_H(arg(c, 0)) : 0), 1); }
K(GetProcAddress, 2) {
  uint32_t mod = arg(c, 0), np = arg(c, 1);
  const char *n = np >= 0x10000 ? (const char *)RC32_H(np) : "#ordinal";
  static const struct { const char *n; int words; w32_hostfn fn; } fns[] = {
      {"FlsAlloc", 1, f_FlsAlloc}, {"FlsGetValue", 1, f_FlsGetValue},
      {"FlsSetValue", 2, f_FlsSetValue}, {"FlsFree", 1, f_FlsFree}};
  uint32_t r = 0;
  if (mod == HMOD_KERNEL32)
    for (size_t k = 0; k < sizeof fns / sizeof fns[0]; k++)
      if (!strcmp(fns[k].n, n)) r = w32_hostfn_new(fns[k].n, W32_STDCALL, fns[k].words, fns[k].fn, 0);
  if (mod != HMOD_KERNEL32 && mod)  // a translated image: its export (by name or ordinal)
    r = rc32_image_export(mod, np >= 0x10000 ? n : (const char *)(uintptr_t)np);
  if (verbose) fprintf(stderr, "  GetProcAddress(0x%x, %s) -> 0x%x\n", mod, n, r);
  ret(c, r, 2);
}
K(GetModuleFileNameW, 3) {
  static const char p[] = "C:\\Rocksmith2014\\Rocksmith2014.exe";
  uint32_t b = arg(c, 1), n = arg(c, 2), k = 0;
  for (; k < n - 1 && p[k]; k++) ((uint16_t *)RC32_H(b))[k] = (uint8_t)p[k];
  ((uint16_t *)RC32_H(b))[k] = 0;
  ret(c, k, 3);
}
K(GetModuleFileNameA, 3) {
  static const char p[] = "C:\\Rocksmith2014\\Rocksmith2014.exe";
  uint32_t b = arg(c, 1), n = arg(c, 2), k = (uint32_t)strlen(p) < n - 1 ? (uint32_t)strlen(p) : n - 1;
  memcpy(RC32_H(b), p, k);
  ((char *)RC32_H(b))[k] = 0;
  ret(c, k, 3);
}
// a translated image answers (and starts, if it has not), anything else is refused
static uint32_t load_library(rc_ctx *c, const char *n) {
  uint32_t b = rc32_image_base(n);
  return b && !rc32_image_start(c, b) ? b : 0;
}
K(LoadLibraryW, 1) {
  uint32_t n = arg(c, 0);
  char a[260] = {0};
  for (int k = 0; n && k < 259 && *(uint16_t *)RC32_H(n + 2 * k); k++) a[k] = (char)*(uint16_t *)RC32_H(n + 2 * k);
  ret(c, load_library(c, a), 1);
}
K(LoadLibraryA, 1) { ret(c, arg(c, 0) ? load_library(c, (const char *)RC32_H(arg(c, 0))) : 0, 1); }
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
K(GetVersionExW, 1) {  // OSVERSIONINFOW: Windows 7 SP1
  uint32_t v = arg(c, 0);
  w32_wr32(v + 4, 6); w32_wr32(v + 8, 1); w32_wr32(v + 12, 7601); w32_wr32(v + 16, 2);
  ret(c, 1, 1);
}
K(IsProcessorFeaturePresent, 1) { uint32_t f = arg(c, 0); ret(c, f == 6 || f == 10 || f == 3, 1); }  // SSE, SSE2, ...
K(IsDebuggerPresent, 0) { ret(c, 0, 0); }
K(GetCurrentProcessId, 0) { ret(c, *teb32(c, RC32_TEB_PID), 0); }
K(GetCurrentThreadId, 0) { ret(c, *teb32(c, RC32_TEB_TID), 0); }
K(GetCurrentProcess, 0) { ret(c, 0xffffffffu, 0); }
K(GetCurrentThread, 0) { ret(c, 0xfffffffeu, 0); }
K(GetTickCount, 0) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); ret(c, (uint32_t)(t.tv_sec * 1000 + t.tv_nsec / 1000000), 0); }
K(QueryPerformanceCounter, 1) { w32_wr64(arg(c, 0), clock_gettime_nsec_np(CLOCK_MONOTONIC) / 100); ret(c, 1, 1); }
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
// the console: only writes, to the host's stdout / stderr; while `capture` is set, stdout goes
// there instead
static char capture[256];
static int capturing;
K(WriteFile, 5) {
  uint32_t h = arg(c, 0), b = arg(c, 1), n = arg(c, 2);
  ssize_t w;
  if (capturing && h == 0x101) {
    size_t l = strlen(capture), m = n < sizeof capture - 1 - l ? n : sizeof capture - 1 - l;
    memcpy(capture + l, RC32_H(b), m);
    capture[l + m] = 0;
    w = (ssize_t)n;
  } else {
    w = write(h == 0x102 ? 2 : 1, RC32_H(b), n);
  }
  if (arg(c, 3)) w32_wr32(arg(c, 3), w < 0 ? 0 : (uint32_t)w);
  ret(c, w >= 0, 5);
}
K(GetConsoleMode, 2) { ret(c, 0, 2); }  // not a console: the CRT writes bytes
K(GetConsoleCP, 0) { ret(c, 1252, 0); }
K(FlushFileBuffers, 1) { ret(c, 1, 1); }
K(SetFilePointer, 4) { ret(c, 0xffffffffu, 4); }
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
K(GetStringTypeW, 4) {  // C1_* for Latin-1
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

void rc32_imp_RaiseException(rc_ctx *c);
void rc32_imp_RtlUnwind(rc_ctx *c);

#define E(name) {"kernel32.dll!" #name, k_##name}
static const struct { const char *name; rc_fn fn; } k32[] = {
    E(HeapCreate), E(GetProcessHeap), E(HeapDestroy), E(HeapAlloc), E(HeapFree), E(HeapSize),
    E(HeapReAlloc), E(HeapValidate), E(HeapQueryInformation), E(VirtualAlloc), E(VirtualFree),
    E(EncodePointer), E(DecodePointer), E(InitializeCriticalSectionAndSpinCount),
    E(InitializeCriticalSection), E(DeleteCriticalSection), E(EnterCriticalSection),
    E(LeaveCriticalSection), E(TryEnterCriticalSection), E(InterlockedIncrement),
    E(InterlockedDecrement), E(InterlockedExchange), E(InterlockedCompareExchange), E(TlsAlloc),
    E(TlsFree), E(TlsGetValue), E(TlsSetValue), E(GetLastError), E(SetLastError),
    E(GetModuleHandleW), E(GetModuleHandleA), E(GetProcAddress), E(GetModuleFileNameW),
    E(GetModuleFileNameA), E(LoadLibraryW), E(LoadLibraryA), E(DisableThreadLibraryCalls), E(GetCommandLineA),
    E(GetCommandLineW), E(GetEnvironmentStringsW), E(FreeEnvironmentStringsW), E(GetStartupInfoW),
    E(GetStdHandle), E(GetFileType), E(SetHandleCount), E(GetACP), E(GetOEMCP), E(IsValidCodePage),
    E(GetCPInfo), E(GetVersionExW), E(IsProcessorFeaturePresent), E(IsDebuggerPresent),
    E(GetCurrentProcessId), E(GetCurrentThreadId), E(GetCurrentProcess), E(GetCurrentThread),
    E(GetTickCount), E(QueryPerformanceCounter), E(GetSystemTimeAsFileTime),
    E(SetUnhandledExceptionFilter), E(UnhandledExceptionFilter), E(Sleep), E(InitializeSListHead),
    E(GetSystemInfo), E(GetThreadTimes), E(GetProcessAffinityMask), E(WriteFile), E(GetConsoleMode), E(GetConsoleCP),
    E(FlushFileBuffers), E(SetFilePointer), E(MultiByteToWideChar), E(WideCharToMultiByte),
    E(GetStringTypeW), E(LCMapStringW),
    {"kernel32.dll!RaiseException", rc32_imp_RaiseException},
    {"kernel32.dll!RtlUnwind", rc32_imp_RtlUnwind},
};

// The DLLs' imports that no loaded image exports (kernel32) come here (rc32_host_import).
static uint8_t used[2][4096];
static void host_import(rc_ctx *c, const rc32_module *m, int k) {
  const char *n = m->imports[k].name;
  int mi = !strcmp(m->name, "msvcp100");
  for (size_t j = 0; j < sizeof k32 / sizeof k32[0]; j++)
    if (!strcasecmp(k32[j].name, n)) {
      if (k < 4096 && !used[mi][k]) {
        used[mi][k] = 1;
        if (verbose) fprintf(stderr, "  first call: %s (%s)\n", n, m->name);
      }
      k32[j].fn(c);
      return;
    }
  fprintf(stderr, "crt_probe: kernel32 import missing: %s (%s)\n", n, m->name);
  exit(3);
}

// --- driving the DLL --------------------------------------------------------------------------
static rc_ctx *C;
static uint32_t export_in(uint32_t base, const char *name) {
  uint32_t a = rc32_image_export(base, name);
  if (!a) {
    fprintf(stderr, "no export %s\n", name);
    exit(2);
  }
  return a;
}
static uint32_t export_va(const char *name) { return export_in(crt_base, name); }
static uint32_t call(const char *name, int n, ...) {  // cdecl
  uint32_t a[16];
  va_list ap;
  va_start(ap, n);
  for (int k = 0; k < n; k++) a[k] = va_arg(ap, uint32_t);
  va_end(ap);
  return w32_guest_call(C, export_va(name), W32_CDECL, 0, a, n, 0);
}
static double call_d(const char *name, int n, ...) {  // cdecl returning double in st(0)
  uint32_t a[16];
  va_list ap;
  va_start(ap, n);
  for (int k = 0; k < n; k++) a[k] = va_arg(ap, uint32_t);
  va_end(ap);
  w32_guest_call(C, export_va(name), W32_CDECL, 0, a, n, 0);
  return C->fpu.st[C->fpu.top & 7];
}
static uint32_t gbuf(uint32_t n) { return w32_galloc(n); }
static int fails;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); fails++; } else if (verbose) fprintf(stderr, "ok   %s\n", #x); } while (0)

// the translated trees (gen_image_pe.py --module)
extern rc32_module msvcr100_rc32_mod;
#ifdef WITH_MSVCP100
extern rc32_module msvcp100_rc32_mod;
#endif

#ifdef WITH_MSVCP100
static void probe_msvcp100(void);
#endif
#ifdef WITH_EH
extern rc32_module eh_rc32_mod;
static void probe_eh(void);
#endif

int main(int argc, char **argv) {
  const char *dll[2] = {0, 0}, *eh = 0;
  int ndll = 0, bench = 0;
  for (int k = 1; k < argc; k++) {
    if (!strcmp(argv[k], "-v")) verbose = 1;
    else if (!strcmp(argv[k], "-bench")) bench = 1;
    else if (!strcmp(argv[k], "-eh") && k + 1 < argc) eh = argv[++k];
    else if (ndll < 2) dll[ndll++] = argv[k];
  }
  if (!ndll) { fprintf(stderr, "usage: crt_probe msvcr100.dll [msvcp100.dll] [-v]\n"); return 2; }
#ifndef WITH_MSVCP100
  if (ndll > 1) { fprintf(stderr, "crt_probe: built without msvcp100 (build.sh)\n"); return 2; }
#endif
  rc32_host_import = host_import;
  if (!rc32_image_add(&msvcr100_rc32_mod, dll[0])) { fprintf(stderr, "cannot map %s\n", dll[0]); return 2; }
#ifdef WITH_MSVCP100
  if (ndll > 1 && !rc32_image_add(&msvcp100_rc32_mod, dll[1])) { fprintf(stderr, "cannot map %s\n", dll[1]); return 2; }
#endif
#ifdef WITH_EH
  if (eh && !rc32_image_add(&eh_rc32_mod, eh)) { fprintf(stderr, "cannot map %s\n", eh); return 2; }
#endif
  rc32_image_alias("msvcr100.dll", "msvcrt.dll");  // as d3dx9_42 imports it
  fprintf(stderr, "%d native math overrides\n", w32_crtmath_register());  // as lp_main does
  fprintf(stderr, "%d native memory overrides\n", w32_crtmem_register());
  crt_math_probe_setup();
  if (rc32_images_bind()) { fprintf(stderr, "cannot bind the imports\n"); return 2; }
  crt_base = rc32_image_base("msvcr100.dll");
  cpp_base = rc32_image_base("msvcp100.dll");
  fprintf(stderr, "msvcr100 at 0x%x, %d functions\n", crt_base, *msvcr100_rc32_mod.table_len);
#ifdef WITH_MSVCP100
  if (cpp_base) {
    // msvcp100's imports from msvcr100 hold msvcr100's exports, data ones included
    int n = 0, bound = 0;
    for (const rc32_import *i = msvcp100_rc32_mod.imports; i->name; i++)
      if (!strncmp(i->name, "msvcr100.dll!", 13)) {
        n++;
        bound += w32_rd32(i->slot) == rc32_image_export(crt_base, i->name + 13);
      }
    fprintf(stderr, "msvcp100 at 0x%x, %d functions; %d/%d msvcr100 imports bound\n", cpp_base,
            *msvcp100_rc32_mod.table_len, bound, n);
    CHECK(n > 0 && bound == n);
  }
#endif
  C = rc32_thread_ctx();
  int st = rc32_images_init(C);  // DllMain(PROCESS_ATTACH): msvcr100, then msvcp100
  fprintf(stderr, "DllMain(PROCESS_ATTACH) of %d image(s) -> %s\n", ndll, st ? "FAILED" : "ok");
  if (st) return 1;

  // heap, strings, memory
  uint32_t p = call("malloc", 1, 100);
  CHECK(p >= 0x10000);
  uint32_t s = w32_gstr("Rocksmith 2014");
  CHECK(call("strlen", 1, s) == 14);
  call("strcpy", 2, p, s);
  CHECK(!strcmp((const char *)RC32_H(p), "Rocksmith 2014"));
  CHECK(call("strcmp", 2, p, s) == 0);
  CHECK(call("_stricmp", 2, w32_gstr("ABC"), w32_gstr("abc")) == 0);
  CHECK(call("strchr", 2, s, ' ') == s + 9);
  {  // the native exports (w32_crtmem_register): the CRT's contracts, overlap and the errors of memcpy_s
    uint32_t b = gbuf(64);
    char *h = (char *)RC32_H(b);
    CHECK(call("memset", 3, b, 0x141, 64) == b && h[0] == 'A' && h[63] == 'A');
    CHECK(call("memcpy", 3, b, s, 9) == b && !memcmp(h, "Rocksmith", 9));
    CHECK(call("memcpy", 3, b + 2, b, 9) == b + 2 && !memcmp(h + 2, "Rocksmith", 9));   // the CRT's copes with overlap
    CHECK(call("memmove", 3, b, b + 2, 9) == b && !memcmp(h, "Rocksmith", 9));
    CHECK(call("memcpy_s", 4, b, 64, s, 14) == 0 && !memcmp(h, "Rocksmith 2014", 14));
    CHECK(call("memcpy_s", 4, 0, 0, 0, 0) == 0);
    CHECK(call("memcpy_s", 4, b, 4, s, 14) == 34 && h[0] == 0 && h[3] == 0 && h[4] == 's');
    CHECK(call("memcpy_s", 4, b, 4, 0, 1) == 22);
  }
  uint32_t q = call("realloc", 2, p, 4000);
  CHECK(q && !strcmp((const char *)RC32_H(q), "Rocksmith 2014"));
  call("memset", 3, q, 'x', 3000);
  CHECK(*(uint8_t *)RC32_H(q + 2999) == 'x');
  call("free", 1, q);
  CHECK(call("atoi", 1, w32_gstr("-1234")) == (uint32_t)-1234);
  CHECK(call("strtoul", 3, w32_gstr("ff"), 0, 16) == 255);
  uint32_t ca = call("calloc", 2, 10, 10);
  CHECK(ca && *(uint8_t *)RC32_H(ca + 99) == 0);
  call("free", 1, ca);

  // formatting and parsing, x87 on the way
  uint32_t b = gbuf(256);
  double pi = 3.14159265358979;
  uint32_t pl, ph;
  memcpy(&pl, &pi, 4);
  memcpy(&ph, (char *)&pi + 4, 4);
  call("sprintf", 7, b, w32_gstr("%d|%5s|%x|%.3f"), 42, w32_gstr("abc"), 0xbeef, pl, ph);
  fprintf(stderr, "sprintf -> \"%s\"\n", (const char *)RC32_H(b));
  CHECK(!strcmp((const char *)RC32_H(b), "42|  abc|beef|3.142"));
  call("_snprintf", 5, b, 8, w32_gstr("%s-%s"), w32_gstr("abcd"), w32_gstr("efgh"));
  CHECK(!memcmp(RC32_H(b), "abcd-efg", 8));
  double d = call_d("atof", 1, w32_gstr("2.5e3"));
  CHECK(d == 2500.0);
  double sq = call_d("sqrt", 2, 0, 0x40310000u);  // sqrt(17.0)
  CHECK(fabs(sq - sqrt(17.0)) < 1e-12);
  uint32_t n1 = gbuf(8), n2 = gbuf(8);
  CHECK(call("sscanf", 4, w32_gstr("12 34"), w32_gstr("%d %d"), n1, n2) == 2 && w32_rd32(n1) == 12 && w32_rd32(n2) == 34);
  CHECK(call("toupper", 1, 'q') == 'Q');
  CHECK(call("isdigit", 1, '7') != 0 && call("isalpha", 1, '7') == 0);

  // errno is per thread
  call("strtol", 3, w32_gstr("99999999999"), 0, 10);
  CHECK(w32_rd32(call("_errno", 0)) == 34);  // ERANGE

  // stdio to the console; the _iob data export is the array __iob_func returns
  call("printf", 2, w32_gstr("printf from the translated msvcr100: %s\n"), w32_gstr("ok"));
  uint32_t iob = call("__iob_func", 0), out = iob + 32;  // stdout: _iob[1], 32 bytes per FILE
  call("fflush", 1, out);
  CHECK(iob == export_va("_iob"));
  // a function pointer read from the export table runs through rc32_icall like any other
  CHECK(rc32_lookup(export_va("strlen")) != 0);
  // LoadLibrary / GetModuleHandle of a translated image: by alias, with a path, no extension
  CHECK(load_library(C, "C:\\Windows\\System32\\MSVCRT.dll") == crt_base);
  CHECK(rc32_image_base("msvcr100") == crt_base && rc32_image_base("kernel32.dll") == 0);

#ifdef WITH_MSVCP100
  if (cpp_base) probe_msvcp100();
  fails += crt_math_probe(C, &msvcr100_rc32_mod, cpp_base ? &msvcp100_rc32_mod : 0, verbose, bench);
#else
  fails += crt_math_probe(C, &msvcr100_rc32_mod, 0, verbose, bench);
#endif
#ifdef WITH_EH
  if (eh) probe_eh();
#endif
  rc32_images_fini();  // DllMain(PROCESS_DETACH), msvcp100 then msvcr100
  fprintf(stderr, "%s\n", fails ? "crt_probe: FAILURES" : "crt_probe: OK");
  return fails != 0;
}

#ifdef WITH_EH
// --- exceptions through the real CRT: tools/ws7/recomp/test/x86/ehcases.py -----------------------
static void probe_eh(void) {
  static const struct { const char *name; uint32_t ret, marks[8]; } cases[] = {
      {"eh_basic", 0x1234, {1, 42, 3}},             // a catch only its continuation leaves
      {"eh_nested", 0x2345, {1, 7, 5, 1}},          // from below a frame with a destructor to run
      {"eh_incatch", 0x3456, {1, 2, 3, 4, 5}},      // a try inside a catch
      {"seh_outer", 0x4567, {1, 0, 6, 1}},          // __except, an inner frame on the chain
      {"sj_basic", 4, {0, 1, 2, 3, 0, 0, 0, 3}},    // setjmp, longjmp three times, unwinding a frame
      {"sj_thunked", 11, {8, 1, 11}},               // through a thunk; an inner setjmp, then the outer
  };
  uint32_t base = rc32_image_base("hand.dll"), out = gbuf(64), seh = w32_rd32(C->fs + RC32_TEB_SEH);
  CHECK(base != 0);
  for (unsigned k = 0; k < sizeof cases / sizeof cases[0]; k++) {
    for (int round = 0; round < 2; round++) {  // a second time: nothing of the first is left behind
      memset(RC32_H(out), 0, 64);
      uint32_t r = w32_guest_call(C, rc32_image_export(base, cases[k].name), W32_CDECL, 0, &out, 1, 0);
      fprintf(stderr, "%s -> 0x%x\n", cases[k].name, r);
      CHECK(r == cases[k].ret);
      for (int j = 0; j < 8; j++) {
        if (w32_rd32(out + 4 * j) != cases[k].marks[j]) fprintf(stderr, "  mark %d: %u\n", j, w32_rd32(out + 4 * j));
        CHECK(w32_rd32(out + 4 * j) == cases[k].marks[j]);
      }
      if (w32_rd32(C->fs + RC32_TEB_SEH) != seh) fprintf(stderr, "  FS:[0] 0x%x, was 0x%x\n", w32_rd32(C->fs + RC32_TEB_SEH), seh);
      CHECK(w32_rd32(C->fs + RC32_TEB_SEH) == seh);
    }
  }
}
#endif

#ifdef WITH_MSVCP100
// --- msvcp100: iostreams over msvcr100 -------------------------------------------------------
// MSVC decorated names of the std:: members used here (char instantiations)
#define SB "?$basic_streambuf@DU?$char_traits@D@std@@@std@@"
#define OS "?$basic_ostream@DU?$char_traits@D@std@@@std@@"
static uint32_t cpp(const char *name) { return export_in(cpp_base, name); }
static uint32_t method(const char *name, uint32_t self, int n, ...) {  // thiscall
  uint32_t a[8];
  va_list ap;
  va_start(ap, n);
  for (int k = 0; k < n; k++) a[k] = va_arg(ap, uint32_t);
  va_end(ap);
  return w32_guest_call(C, cpp(name), W32_THISCALL, self, a, n, 0);
}

static void probe_msvcp100(void) {
  // std::cout, a data export: operator<<(int) on it and std::endl reach msvcr100's stdout (the
  // console's WriteFile, captured here)
  uint32_t cout = cpp("?cout@std@@3V?$basic_ostream@DU?$char_traits@D@std@@@1@A");
  capturing = 1;
  capture[0] = 0;
  uint32_t r = method("??6" OS "QAEAAV01@H@Z", cout, 1, 42);
  CHECK(r == cout);
  method("?flush@" OS "QAEAAV12@XZ", cout, 0);
  capturing = 0;
  fprintf(stderr, "std::cout << 42 -> \"%s\"\n", capture);
  CHECK(!strcmp(capture, "42"));

  // an ostringstream by hand (basic_stringbuf is a header template, not exported): a
  // basic_streambuf whose put area is a guest buffer, a basic_ostream on it, << 42 and << 2.5
  uint32_t buf = gbuf(64), sb = gbuf(128), os = gbuf(256);
  method("??0" SB "IAE@XZ", sb, 0);                      // basic_streambuf() (protected)
  method("?setp@" SB "IAEXPAD0@Z", sb, 2, buf, buf + 63);
  // basic_ostream(streambuf *, bool isstd), plus MSVC's hidden "construct the virtual base" flag
  method("??0" OS "QAE@PAV?$basic_streambuf@DU?$char_traits@D@std@@@1@_N@Z", os, 3, sb, 0, 1);
  method("??6" OS "QAEAAV01@H@Z", os, 1, 42);
  double v = 2.5;
  uint32_t lo, hi;
  memcpy(&lo, &v, 4);
  memcpy(&hi, (char *)&v + 4, 4);
  method("??6" OS "QAEAAV01@N@Z", os, 2, lo, hi);
  method("??6" OS "QAEAAV01@H@Z", os, 1, -7);
  fprintf(stderr, "ostream << 42 << 2.5 << -7 -> \"%s\"\n", (const char *)RC32_H(buf));
  CHECK(!strcmp((const char *)RC32_H(buf), "422.5-7"));
}
#endif
