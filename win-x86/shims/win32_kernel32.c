// kernel32: heaps and virtual memory, modules and LoadLibrary, the process and the system,
// time, code pages and locale, environment and the console (the rest: win32_kfile.c files,
// win32_ksync.c handles, threads and synchronisation).
#include <ctype.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "audio/audio.h"
#include "steam/steam32.h"
#include "win32_shims.h"

int w32_host_dll(const char *dll);  // win32_dispatch.c

// --- heaps -------------------------------------------------------------------------------------
// Every heap is one guest allocator: a block has a 16-byte header (requested size, the class
// capacity, a magic) before the address the guest gets. Up to 1 MB the blocks come from
// w32_galloc's power-of-two classes; larger ones are 64 KB-rounded runs kept on a free list.
#define HMAGIC 0x48454150u  // "HEAP"
#define BIG (1u << 20)
typedef struct big { uint32_t g, cap; struct big *next; } big;
static big *big_free;
static pthread_mutex_t big_mu = PTHREAD_MUTEX_INITIALIZER;

static uint32_t cap_for(uint32_t n) {
  if (n <= BIG) {
    uint32_t c = 16;
    while (c < n) c <<= 1;
    return c;
  }
  return (n + 0xffff) & ~0xffffu;
}

uint32_t w32_heap_alloc(uint32_t size) {
  if (size > 0x7fff0000u) return 0;
  uint32_t cap = cap_for(size + 16), g = 0;
  if (cap <= BIG) {
    g = w32_galloc(cap);
  } else {
    pthread_mutex_lock(&big_mu);
    for (big **p = &big_free; *p; p = &(*p)->next)
      if ((*p)->cap >= cap && (*p)->cap <= 2 * cap) {
        big *b = *p;
        *p = b->next;
        g = b->g;
        cap = b->cap;
        free(b);
        break;
      }
    pthread_mutex_unlock(&big_mu);
    if (g) memset(RC32_H(g), 0, cap);
    else g = rc32_alloc(cap);
  }
  if (!g) return 0;
  w32_wr32(g, size);
  w32_wr32(g + 4, cap);
  w32_wr32(g + 8, HMAGIC);
  return g + 16;
}
static int is_block(uint32_t p) { return p >= 0x10010 && w32_rd32(p - 8) == HMAGIC; }
uint32_t w32_heap_size(uint32_t p) { return is_block(p) ? w32_rd32(p - 16) : 0xffffffffu; }
void w32_heap_free(uint32_t p) {
  if (!is_block(p)) return;
  uint32_t g = p - 16, cap = w32_rd32(g + 4);
  w32_wr32(g + 8, 0);
  if (cap <= BIG) {
    w32_gfree(g, cap);
  } else {
    big *b = malloc(sizeof *b);
    *b = (big){g, cap, 0};
    pthread_mutex_lock(&big_mu);
    b->next = big_free;
    big_free = b;
    pthread_mutex_unlock(&big_mu);
  }
}
static uint32_t heap_realloc(uint32_t p, uint32_t n, int in_place) {
  if (!is_block(p) || n > 0x7fff0000u) return 0;  // (n + 16 must not wrap)
  uint32_t old = w32_rd32(p - 16), cap = w32_rd32(p - 12);
  if (n + 16 <= cap) {
    if (n > old) memset(RC32_H(p + old), 0, n - old);
    w32_wr32(p - 16, n);
    return p;
  }
  if (in_place) return 0;
  uint32_t q = w32_heap_alloc(n);
  if (!q) return 0;
  memcpy(RC32_H(q), RC32_H(p), old < n ? old : n);
  w32_heap_free(p);
  return q;
}

#define PROCESS_HEAP 0x00ee0000u
static void k_HeapCreate(rc_ctx *c) { w32_ret(c, PROCESS_HEAP + 0x10000, 3); }
static void k_HeapDestroy(rc_ctx *c) { w32_ret(c, 1, 1); }
static void k_GetProcessHeap(rc_ctx *c) { w32_ret(c, PROCESS_HEAP, 0); }
static void k_HeapAlloc(rc_ctx *c) {
  uint32_t p = w32_heap_alloc(w32_arg(c, 2));
  if (!p) w32_set_error(c, W32_ERROR_NOT_ENOUGH_MEMORY);
  w32_ret(c, p, 3);
}
static void k_HeapFree(rc_ctx *c) { w32_heap_free(w32_arg(c, 2)); w32_ret(c, 1, 3); }
static void k_HeapSize(rc_ctx *c) { w32_ret(c, w32_heap_size(w32_arg(c, 2)), 3); }
static void k_HeapReAlloc(rc_ctx *c) {
  uint32_t p = heap_realloc(w32_arg(c, 2), w32_arg(c, 3), (w32_arg(c, 1) & 0x10) != 0);  // HEAP_REALLOC_IN_PLACE_ONLY
  if (!p) w32_set_error(c, W32_ERROR_NOT_ENOUGH_MEMORY);
  w32_ret(c, p, 4);
}
static void k_HeapValidate(rc_ctx *c) { w32_ret(c, 1, 3); }
static void k_HeapCompact(rc_ctx *c) { w32_ret(c, 0x100000, 2); }
static void k_HeapWalk(rc_ctx *c) { w32_set_error(c, W32_ERROR_NO_MORE_ITEMS); w32_ret(c, 0, 2); }
static void k_HeapSetInformation(rc_ctx *c) { w32_ret(c, 1, 4); }
static void k_HeapQueryInformation(rc_ctx *c) {
  if (w32_arg(c, 2) && w32_arg(c, 3) >= 4) w32_wr32(w32_arg(c, 2), 2);  // the low-fragmentation heap
  if (w32_arg(c, 4)) w32_wr32(w32_arg(c, 4), 4);
  w32_ret(c, 1, 5);
}
// Global / Local memory: GMEM_MOVEABLE handles are the blocks themselves
static void k_GlobalAlloc(rc_ctx *c) { w32_ret(c, w32_heap_alloc(w32_arg(c, 1)), 2); }
static void k_GlobalFree(rc_ctx *c) { w32_heap_free(w32_arg(c, 0)); w32_ret(c, 0, 1); }
static void k_GlobalLock(rc_ctx *c) { w32_ret(c, w32_arg(c, 0), 1); }
static void k_GlobalUnlock(rc_ctx *c) { w32_ret(c, 1, 1); }
static void k_GlobalUnWire(rc_ctx *c) { w32_ret(c, 1, 1); }
static void k_GlobalSize(rc_ctx *c) { uint32_t s = w32_heap_size(w32_arg(c, 0)); w32_ret(c, s == 0xffffffffu ? 0 : s, 1); }
static void k_LocalAlloc(rc_ctx *c) { w32_ret(c, w32_heap_alloc(w32_arg(c, 1)), 2); }
static void k_LocalFree(rc_ctx *c) { w32_heap_free(w32_arg(c, 0)); w32_ret(c, 0, 1); }

// --- virtual memory ----------------------------------------------------------------------------
// Reservations come from the arena (committed at once, as the arena is); MEM_COMMIT inside one
// returns the address, a fixed address outside every reservation is refused.
typedef struct vres { uint32_t g, size; struct vres *next; } vres;
static vres *vlist;
static pthread_mutex_t v_mu = PTHREAD_MUTEX_INITIALIZER;
static vres *vfind(uint32_t a) {
  for (vres *v = vlist; v; v = v->next)
    if (a - v->g < v->size) return v;
  return 0;
}
unsigned long w32_valloc_calls, w32_valloc_bytes, w32_vfree_calls;  // LP_THREAD_CPU's memory line
static void k_VirtualAlloc(rc_ctx *c) {
  uint32_t a = w32_arg(c, 0), n = w32_arg(c, 1), r = 0;
  pthread_mutex_lock(&v_mu);
  if (a) {
    vres *v = vfind(a);
    if (v && a + n - v->g <= v->size) r = a & ~0xfffu;
  } else if (n) {
    uint32_t sz = (n + 0xfff) & ~0xfffu, g = rc32_alloc(sz + 0x10000);
    if (g) {
      r = (g + 0xffff) & ~0xffffu;  // 64 KB granularity
      vres *v = malloc(sizeof *v);
      *v = (vres){r, sz, vlist};
      vlist = v;
      w32_valloc_calls++;
      w32_valloc_bytes += sz;
    }
  }
  pthread_mutex_unlock(&v_mu);
  if (!r) {
    w32_set_error(c, a ? 487 : W32_ERROR_NOT_ENOUGH_MEMORY);  // ERROR_INVALID_ADDRESS
    if (w32_log) fprintf(stderr, "w32: VirtualAlloc(0x%x, 0x%x, 0x%x) refused\n", a, n, w32_arg(c, 2));
  }
  w32_ret(c, r, 4);
}
static void k_VirtualFree(rc_ctx *c) {
  uint32_t a = w32_arg(c, 0), type = w32_arg(c, 2);
  pthread_mutex_lock(&v_mu);
  vres *v = vfind(a);
  if (v && (type & 0x8000)) w32_vfree_calls++;  // MEM_RELEASE
  if (v && (type & 0x4000)) memset(RC32_H(a & ~0xfffu), 0, ((w32_arg(c, 1) + 0xfff) & ~0xfffu));  // MEM_DECOMMIT reads as zero again
  pthread_mutex_unlock(&v_mu);
  w32_ret(c, v != 0, 3);  // released memory is not handed out again (the arena never shrinks)
}
static void k_VirtualProtect(rc_ctx *c) {
  if (w32_arg(c, 3)) w32_wr32(w32_arg(c, 3), 0x04);  // PAGE_READWRITE
  w32_ret(c, 1, 4);
}
static void k_VirtualQuery(rc_ctx *c) {
  uint32_t a = w32_arg(c, 0), b = w32_arg(c, 1);
  if (w32_arg(c, 2) < 28) { w32_ret(c, 0, 3); return; }
  memset(RC32_H(b), 0, 28);
  pthread_mutex_lock(&v_mu);
  vres *v = vfind(a);
  pthread_mutex_unlock(&v_mu);
  uint32_t page = a & ~0xfffu;
  w32_wr32(b, page);
  if (v) {
    w32_wr32(b + 4, v->g);
    w32_wr32(b + 8, 0x04);
    w32_wr32(b + 12, v->g + v->size - page);
    w32_wr32(b + 16, 0x1000);   // MEM_COMMIT
    w32_wr32(b + 20, 0x04);
    w32_wr32(b + 24, 0x20000);  // MEM_PRIVATE
  } else {
    w32_wr32(b + 4, page);
    w32_wr32(b + 8, 0x40);
    w32_wr32(b + 12, 0x1000);
    w32_wr32(b + 16, 0x1000);
    w32_wr32(b + 20, 0x40);     // images and the arena: committed, read/write/execute
    w32_wr32(b + 24, 0x1000000);
  }
  w32_ret(c, 28, 3);
}
static void k_IsBadReadPtr(rc_ctx *c) { w32_ret(c, w32_arg(c, 0) < 0x10000 && w32_arg(c, 1), 2); }
static void k_IsBadWritePtr(rc_ctx *c) { w32_ret(c, w32_arg(c, 0) < 0x10000 && w32_arg(c, 1), 2); }
static void k_IsBadCodePtr(rc_ctx *c) { w32_ret(c, w32_arg(c, 0) < 0x10000, 1); }

// --- modules -----------------------------------------------------------------------------------
// A DLL the host provides gets a handle: a small guest block with MZ and PE signatures.
#define MAXHM 32
static struct { char dll[32]; uint32_t h; } hmods[MAXHM];
static int nhmods;
static pthread_mutex_t hm_mu = PTHREAD_MUTEX_INITIALIZER;

// "C:\x\KERNEL32" -> "kernel32.dll"
static void canon_dll(const char *s, char *out, size_t n) {
  for (const char *p = s; *p; p++)
    if (*p == '\\' || *p == '/') s = p + 1;
  size_t k = 0;
  for (; s[k] && k < n - 5; k++) out[k] = (char)tolower((unsigned char)s[k]);
  out[k] = 0;
  if (!strchr(out, '.')) strcat(out, ".dll");
}

uint32_t w32_host_module(const char *dll) {
  char d[32];
  canon_dll(dll, d, sizeof d);
  if (!w32_host_dll(d)) return 0;
  pthread_mutex_lock(&hm_mu);
  uint32_t h = 0;
  for (int k = 0; k < nhmods; k++)
    if (!strcmp(hmods[k].dll, d)) h = hmods[k].h;
  if (!h && nhmods < MAXHM) {
    uint32_t g = rc32_alloc(0x1000 + 0x10000);
    h = (g + 0xffff) & ~0xffffu;
    memcpy(RC32_H(h), "MZ", 2);
    w32_wr32(h + 0x3c, 0x40);
    memcpy(RC32_H(h + 0x40), "PE\0\0\x4c\x01", 6);
    snprintf(hmods[nhmods].dll, sizeof hmods[nhmods].dll, "%s", d);
    hmods[nhmods++].h = h;
  }
  pthread_mutex_unlock(&hm_mu);
  return h;
}
const char *w32_host_module_name(uint32_t h) {
  for (int k = 0; k < nhmods; k++)
    if (hmods[k].h == h) return hmods[k].dll;
  return 0;
}

static uint32_t module_handle(const char *n) {
  if (!n) return rc32_image_base(0);
  uint32_t b = rc32_image_base(n);
  return b ? b : w32_host_module(n);
}
static void k_GetModuleHandleA(rc_ctx *c) {
  const char *n = w32_cstr(w32_arg(c, 0));
  uint32_t h = module_handle(n);
  if (!h) w32_set_error(c, W32_ERROR_MOD_NOT_FOUND);
  if (w32_log && n) fprintf(stderr, "w32: GetModuleHandle(\"%s\") -> 0x%x\n", n, h);
  w32_ret(c, h, 1);
}
static void k_GetModuleHandleW(rc_ctx *c) {
  char n[260];
  uint32_t p = w32_arg(c, 0), h = module_handle(p ? w32_wstr(p, n, sizeof n) : 0);
  if (!h) w32_set_error(c, W32_ERROR_MOD_NOT_FOUND);
  if (w32_log && p) fprintf(stderr, "w32: GetModuleHandle(\"%s\") -> 0x%x\n", n, h);
  w32_ret(c, h, 1);
}

// Names already logged (LoadLibrary, GetProcAddress): 1 the first time key is seen with value v.
typedef struct seen_name { char *key; uint32_t v; struct seen_name *next; } seen_name;
static seen_name *loaded, *missed;
static pthread_mutex_t seen_mu = PTHREAD_MUTEX_INITIALIZER;
static int log_first(seen_name **list, const char *key, uint32_t v) {
  pthread_mutex_lock(&seen_mu);
  seen_name *e = *list;
  while (e && (e->v != v || strcmp(e->key, key))) e = e->next;
  int first = !e;
  if (first && (e = malloc(sizeof *e))) {
    *e = (seen_name){strdup(key), v, *list};
    *list = e;
  }
  pthread_mutex_unlock(&seen_mu);
  return first;
}

// LoadLibrary: a translated image (started when it has not been), a DLL the host provides, else
// ERROR_MOD_NOT_FOUND: Steam's client and the Ubisoft loaders (steam32_dll_blocked), the
// hardware survey (systemdetection.dll: the game skips it when it does not load), anything else.
static uint32_t load_library(rc_ctx *c, const char *n) {
  uint32_t b = 0;
  const char *why = "unknown";
  char d[64];
  canon_dll(n, d, sizeof d);
  if (steam32_dll_blocked(n)) {
    why = "blocked";
  } else if (!strcmp(d, "systemdetection.dll")) {
    why = "hardware survey, not loaded";
  } else if ((b = rc32_image_base(n))) {
    if (rc32_image_start(c, b)) {
      b = 0;
      why = "DllMain failed";
    }
  } else {
    b = w32_host_module(n);
  }
  if (w32_log || log_first(&loaded, d, b))  // the game loads d3d9.dll again every few frames
    fprintf(stderr, "w32: LoadLibrary(\"%s\") -> 0x%x%s%s\n", n, b, b ? "" : ": ", b ? "" : why);
  if (!b) w32_set_error(c, W32_ERROR_MOD_NOT_FOUND);
  return b;
}
static void k_LoadLibraryA(rc_ctx *c) { w32_ret(c, load_library(c, w32_cstr(w32_arg(c, 0))), 1); }
static void k_LoadLibraryW(rc_ctx *c) { char n[260]; w32_ret(c, load_library(c, w32_wstr(w32_arg(c, 0), n, sizeof n)), 1); }
static void k_LoadLibraryExA(rc_ctx *c) { w32_ret(c, load_library(c, w32_cstr(w32_arg(c, 0))), 3); }
static void k_LoadLibraryExW(rc_ctx *c) { char n[260]; w32_ret(c, load_library(c, w32_wstr(w32_arg(c, 0), n, sizeof n)), 3); }
static void k_FreeLibrary(rc_ctx *c) { w32_ret(c, 1, 1); }
static void k_DisableThreadLibraryCalls(rc_ctx *c) { w32_ret(c, 1, 1); }
static void k_GetProcAddress(rc_ctx *c) {
  uint32_t m = w32_arg(c, 0), np = w32_arg(c, 1), r = 0;
  const char *dll = w32_host_module_name(m);
  char name[256];
  if (np < 0x10000) snprintf(name, sizeof name, "#%u", np);
  else snprintf(name, sizeof name, "%s", (const char *)RC32_H(np));
  if (dll) {
    char imp[300];
    snprintf(imp, sizeof imp, "%s!%s", dll, name);
    rc_fn f = np >= 0x10000 ? w32_import(imp) : 0;
    if (f && !strcmp(dll, "winmm.dll")) r = audio_import(name);  // the audio layer's own cell, if it has one
    if (f && !r) r = w32_proc_cell(imp, f);
  } else if (m) {
    r = rc32_image_export(m, np >= 0x10000 ? name : (const char *)(uintptr_t)np);
  }
  if (!r) w32_set_error(c, W32_ERROR_PROC_NOT_FOUND);
  char key[300];
  snprintf(key, sizeof key, "%s!%s", dll ? dll : "image", name);
  if (w32_log || (!r && dll && log_first(&missed, key, 0)))  // a missing one once (d3d9!DebugSetMute: each frame)
    fprintf(stderr, "w32: GetProcAddress(%s, %s) -> 0x%x\n", dll ? dll : "image", name, r);
  w32_ret(c, r, 2);
}
static void module_path(uint32_t m, char *out, size_t n) {
  const char *dll = w32_host_module_name(m);
  if (!m || m == rc32_image_base(0)) snprintf(out, n, "%s\\Rocksmith2014.exe", w32_game_dir_win());
  else if (dll) snprintf(out, n, "C:\\Windows\\SysWOW64\\%s", dll);
  else snprintf(out, n, "%s\\module_%x.dll", w32_game_dir_win(), m);
}
static void k_GetModuleFileNameA(rc_ctx *c) {
  char p[512];
  uint32_t b = w32_arg(c, 1), cap = w32_arg(c, 2);
  module_path(w32_arg(c, 0), p, sizeof p);
  uint32_t k = (uint32_t)strlen(p);
  if (!cap) { w32_ret(c, 0, 3); return; }
  if (k >= cap) { k = cap - 1; w32_set_error(c, W32_ERROR_INSUFFICIENT_BUFFER); }
  memcpy(RC32_H(b), p, k);
  ((char *)RC32_H(b))[k] = 0;
  w32_ret(c, k, 3);
}
static void k_GetModuleFileNameW(rc_ctx *c) {
  char p[512];
  module_path(w32_arg(c, 0), p, sizeof p);
  w32_ret(c, w32_put_wstr(w32_arg(c, 1), w32_arg(c, 2), p), 3);
}

// --- process and system ------------------------------------------------------------------------
static void k_GetCurrentProcess(rc_ctx *c) { w32_ret(c, 0xffffffffu, 0); }
static void k_GetCurrentProcessId(rc_ctx *c) { w32_ret(c, *w32_teb(c, RC32_TEB_PID), 0); }
// The end of the process with the game's threads still running: _exit, not exit, which would tear the
// host's down under them, so the renderer's shader cache is saved here (exit would have done that).
__attribute__((weak)) int rs_gfx_flush_cache(double max_seconds);  // libRSOpenGL; not in the tests
_Noreturn void w32_host_exit(int code) {
  if (rs_gfx_flush_cache) rs_gfx_flush_cache(2.0);
  fflush(0);
  _exit(code);
}
static void k_ExitProcess(rc_ctx *c) {
  uint32_t code = w32_arg(c, 0);
  fprintf(stderr, "w32: ExitProcess(%u)\n", code);
  rc32_images_fini();  // DLL_PROCESS_DETACH: the CRTs flush their streams
  w32_host_exit((int)code);
}
static void k_TerminateProcess(rc_ctx *c) {
  if (w32_arg(c, 0) == 0xffffffffu) {
    fprintf(stderr, "w32: TerminateProcess(self, %u)\n", w32_arg(c, 1));
    _exit((int)w32_arg(c, 1));
  }
  w32_set_error(c, W32_ERROR_ACCESS_DENIED);
  w32_ret(c, 0, 2);
}
static void k_GetExitCodeProcess(rc_ctx *c) { if (w32_arg(c, 1)) w32_wr32(w32_arg(c, 1), 259); w32_ret(c, 1, 2); }
static void k_CreateProcessA(rc_ctx *c) {  // no child processes (and none to reach the network with)
  fprintf(stderr, "w32: CreateProcess(\"%s\", \"%s\") refused\n", w32_arg(c, 0) ? w32_cstr(w32_arg(c, 0)) : "",
          w32_arg(c, 1) ? w32_cstr(w32_arg(c, 1)) : "");
  w32_set_error(c, W32_ERROR_ACCESS_DENIED);
  w32_ret(c, 0, 10);
}
static void k_CreateProcessW(rc_ctx *c) {
  fprintf(stderr, "w32: CreateProcessW refused\n");
  w32_set_error(c, W32_ERROR_ACCESS_DENIED);
  w32_ret(c, 0, 10);
}
static void k_CreatePipe(rc_ctx *c) { w32_set_error(c, W32_ERROR_ACCESS_DENIED); w32_ret(c, 0, 4); }
static void k_PeekNamedPipe(rc_ctx *c) { w32_set_error(c, W32_ERROR_INVALID_HANDLE); w32_ret(c, 0, 6); }
static void k_GetVersion(rc_ctx *c) { w32_ret(c, 0x1db10106u, 0); }  // 6.1 build 7601: Windows 7 SP1
static void version_info(uint32_t v, int wide) {
  uint32_t sz = w32_rd32(v);
  w32_wr32(v + 4, 6);
  w32_wr32(v + 8, 1);
  w32_wr32(v + 12, 7601);
  w32_wr32(v + 16, 2);  // VER_PLATFORM_WIN32_NT
  if (wide) w32_put_wstr(v + 20, 128, "Service Pack 1");
  else snprintf((char *)RC32_H(v + 20), 128, "Service Pack 1");
  uint32_t ex = wide ? 276 : 148;  // OSVERSIONINFOEX: service pack 1.0, a workstation
  if (sz >= ex + 8) {
    *(uint16_t *)RC32_H(v + ex) = 1;
    *(uint16_t *)RC32_H(v + ex + 2) = 0;
    *(uint16_t *)RC32_H(v + ex + 4) = 0x100;
    *(uint8_t *)RC32_H(v + ex + 6) = 1;  // VER_NT_WORKSTATION
  }
}
static void k_GetVersionExA(rc_ctx *c) { version_info(w32_arg(c, 0), 0); w32_ret(c, 1, 1); }
static void k_GetVersionExW(rc_ctx *c) { version_info(w32_arg(c, 0), 1); w32_ret(c, 1, 1); }
static uint32_t ncpu(void) {
  int n = 4;
  size_t sz = sizeof n;
  sysctlbyname("hw.ncpu", &n, &sz, 0, 0);
  return n < 1 ? 1 : n > 32 ? 32 : (uint32_t)n;
}
static void k_GetSystemInfo(rc_ctx *c) {
  uint32_t s = w32_arg(c, 0), n = ncpu();
  memset(RC32_H(s), 0, 36);
  w32_wr32(s + 4, 4096);
  w32_wr32(s + 8, 0x10000);
  w32_wr32(s + 12, 0xfffeffffu);  // large address aware
  w32_wr32(s + 16, n == 32 ? 0xffffffffu : (1u << n) - 1);
  w32_wr32(s + 20, n);
  w32_wr32(s + 24, 586);
  w32_wr32(s + 28, 0x10000);
  *(uint16_t *)RC32_H(s + 32) = 6;      // wProcessorLevel
  *(uint16_t *)RC32_H(s + 34) = 0x3a09; // wProcessorRevision
  w32_ret(c, 0, 1);
}
static uint64_t physmem(void) {
  uint64_t m = 8ull << 30;
  size_t sz = sizeof m;
  sysctlbyname("hw.memsize", &m, &sz, 0, 0);
  return m;
}
static void k_GlobalMemoryStatus(rc_ctx *c) {  // MEMORYSTATUS: 32-bit fields, clamped
  uint32_t b = w32_arg(c, 0);
  uint64_t m = physmem();
  uint32_t mm = m > 0xffffffffu ? 0xffffffffu : (uint32_t)m;
  w32_wr32(b, 32);
  w32_wr32(b + 4, 30);
  w32_wr32(b + 8, mm);
  w32_wr32(b + 12, mm / 2);
  w32_wr32(b + 16, mm);
  w32_wr32(b + 20, mm / 2);
  w32_wr32(b + 24, 0xfffe0000u);
  w32_wr32(b + 28, 0xc0000000u);
  w32_ret(c, 0, 1);
}
static void k_GlobalMemoryStatusEx(rc_ctx *c) {
  uint32_t b = w32_arg(c, 0);
  uint64_t m = physmem();
  w32_wr32(b + 4, 30);
  w32_wr64(b + 8, m);
  w32_wr64(b + 16, m / 2);
  w32_wr64(b + 24, m);
  w32_wr64(b + 32, m / 2);
  w32_wr64(b + 40, 0xfffe0000ull);
  w32_wr64(b + 48, 0xc0000000ull);
  w32_wr64(b + 56, 0);
  w32_ret(c, 1, 1);
}
static void k_IsProcessorFeaturePresent(rc_ctx *c) {
  uint32_t f = w32_arg(c, 0);  // what the front end's CPUID reports: x87, MMX, SSE, SSE2, SSE3, cmpxchg8b
  w32_ret(c, f == 0 ? 0 : f == 2 || f == 3 || f == 6 || f == 8 || f == 10 || f == 13 || f == 12, 1);
}
static void k_IsDebuggerPresent(rc_ctx *c) { w32_ret(c, 0, 0); }
static void k_CheckRemoteDebuggerPresent(rc_ctx *c) { if (w32_arg(c, 1)) w32_wr32(w32_arg(c, 1), 0); w32_ret(c, 1, 2); }
static void k_GetProcessAffinityMask(rc_ctx *c) {
  uint32_t n = ncpu(), m = n == 32 ? 0xffffffffu : (1u << n) - 1;
  if (w32_arg(c, 1)) w32_wr32(w32_arg(c, 1), m);
  if (w32_arg(c, 2)) w32_wr32(w32_arg(c, 2), m);
  w32_ret(c, 1, 3);
}
static void k_SetProcessAffinityMask(rc_ctx *c) { w32_ret(c, 1, 2); }
static void k_GetPriorityClass(rc_ctx *c) { w32_ret(c, 0x20, 1); }  // NORMAL_PRIORITY_CLASS
static void k_SetPriorityClass(rc_ctx *c) { w32_ret(c, 1, 2); }
static uint32_t error_mode;
static void k_SetErrorMode(rc_ctx *c) { uint32_t o = error_mode; error_mode = w32_arg(c, 0); w32_ret(c, o, 1); }
static void k_SetThreadExecutionState(rc_ctx *c) { w32_ret(c, 0x80000000u, 1); }  // ES_CONTINUOUS
static uint32_t uef;
static void k_SetUnhandledExceptionFilter(rc_ctx *c) { uint32_t o = uef; uef = w32_arg(c, 0); w32_ret(c, o, 1); }
static void k_UnhandledExceptionFilter(rc_ctx *c) {
  uint32_t ep = w32_arg(c, 0), rec = ep ? w32_rd32(ep) : 0;
  fprintf(stderr, "w32: UnhandledExceptionFilter: exception 0x%08x at 0x%x\n", rec ? w32_rd32(rec) : 0,
          rec ? w32_rd32(rec + 12) : 0);
  w32_ret(c, 0, 1);  // EXCEPTION_CONTINUE_SEARCH
}
static void k_OutputDebugStringA(rc_ctx *c) {
  const char *s = w32_cstr(w32_arg(c, 0));
  if (s) fprintf(stderr, "[debug] %s%s", s, s[0] && s[strlen(s) - 1] == '\n' ? "" : "\n");
  w32_ret(c, 0, 1);
}
static void k_OutputDebugStringW(rc_ctx *c) {
  char s[2048];
  w32_wstr(w32_arg(c, 0), s, sizeof s);
  fprintf(stderr, "[debug] %s%s", s, s[0] && s[strlen(s) - 1] == '\n' ? "" : "\n");
  w32_ret(c, 0, 1);
}
static void k_DebugBreak(rc_ctx *c) { fprintf(stderr, "w32: DebugBreak (ignored)\n"); w32_ret(c, 0, 0); }
static void k_Beep(rc_ctx *c) { w32_ret(c, 1, 2); }
static void k_GetLastError(rc_ctx *c) { w32_ret(c, *w32_teb(c, RC32_TEB_LASTERR), 0); }
static void k_SetLastError(rc_ctx *c) { w32_set_error(c, w32_arg(c, 0)); w32_ret(c, 0, 1); }
static void k_EncodePointer(rc_ctx *c) { w32_ret(c, w32_arg(c, 0) ^ 0x5a5a5a5au, 1); }
static void k_DecodePointer(rc_ctx *c) { w32_ret(c, w32_arg(c, 0) ^ 0x5a5a5a5au, 1); }
static void k_FormatMessageA(rc_ctx *c) {
  uint32_t fl = w32_arg(c, 0), id = w32_arg(c, 2), buf = w32_arg(c, 4), n = w32_arg(c, 5);
  char m[64];
  snprintf(m, sizeof m, "Error %u.\r\n", id);
  uint32_t len = (uint32_t)strlen(m);
  if (fl & 0x100) {  // FORMAT_MESSAGE_ALLOCATE_BUFFER: LocalAlloc'd, its address at buf
    uint32_t p = w32_heap_alloc(len + 1);
    memcpy(RC32_H(p), m, len + 1);
    w32_wr32(buf, p);
  } else if (n > len) {
    memcpy(RC32_H(buf), m, len + 1);
  } else {
    w32_set_error(c, W32_ERROR_INSUFFICIENT_BUFFER);
    len = 0;
  }
  w32_ret(c, len, 7);
}
static void k_RaiseException_(rc_ctx *c) {
  void rc32_imp_RaiseException(rc_ctx *);
  rc32_imp_RaiseException(c);
}
static void k_RtlUnwind_(rc_ctx *c) {
  void rc32_imp_RtlUnwind(rc_ctx *);
  rc32_imp_RtlUnwind(c);
}

// --- time --------------------------------------------------------------------------------------
static uint64_t ft_now(void) {
  struct timespec t;
  clock_gettime(CLOCK_REALTIME, &t);
  return ((uint64_t)t.tv_sec + 11644473600ull) * 10000000ull + (uint64_t)t.tv_nsec / 100;
}
static void k_GetTickCount(rc_ctx *c) { w32_ret(c, (uint32_t)(clock_gettime_nsec_np(CLOCK_MONOTONIC) / 1000000), 0); }
static void k_GetTickCount64(rc_ctx *c) {
  uint64_t t = clock_gettime_nsec_np(CLOCK_MONOTONIC) / 1000000;
  c->r[2] = (uint32_t)(t >> 32);
  w32_ret(c, (uint32_t)t, 0);
}
// 10 MHz, as on most Windows machines
static void k_QueryPerformanceCounter(rc_ctx *c) { w32_wr64(w32_arg(c, 0), clock_gettime_nsec_np(CLOCK_MONOTONIC) / 100); w32_ret(c, 1, 1); }
static void k_QueryPerformanceFrequency(rc_ctx *c) { w32_wr64(w32_arg(c, 0), 10000000); w32_ret(c, 1, 1); }
static void k_GetSystemTimeAsFileTime(rc_ctx *c) { w32_wr64(w32_arg(c, 0), ft_now()); w32_ret(c, 0, 1); }
static void put_systemtime(uint32_t st, const struct tm *t, uint32_t ms) {
  uint16_t v[8] = {(uint16_t)(t->tm_year + 1900), (uint16_t)(t->tm_mon + 1), (uint16_t)t->tm_wday, (uint16_t)t->tm_mday,
                   (uint16_t)t->tm_hour, (uint16_t)t->tm_min, (uint16_t)t->tm_sec, (uint16_t)ms};
  memcpy(RC32_H(st), v, 16);
}
static void systime(uint32_t st, int local) {
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  struct tm t;
  if (local) localtime_r(&ts.tv_sec, &t);
  else gmtime_r(&ts.tv_sec, &t);
  put_systemtime(st, &t, (uint32_t)(ts.tv_nsec / 1000000));
}
static void k_GetLocalTime(rc_ctx *c) { systime(w32_arg(c, 0), 1); w32_ret(c, 0, 1); }
static void k_GetSystemTime(rc_ctx *c) { systime(w32_arg(c, 0), 0); w32_ret(c, 0, 1); }
static void k_SetLocalTime(rc_ctx *c) { w32_set_error(c, W32_ERROR_ACCESS_DENIED); w32_ret(c, 0, 1); }
static void k_FileTimeToSystemTime(rc_ctx *c) {
  uint64_t ft;
  memcpy(&ft, RC32_H(w32_arg(c, 0)), 8);
  time_t s = (time_t)(ft / 10000000ull - 11644473600ull);
  struct tm t;
  gmtime_r(&s, &t);
  put_systemtime(w32_arg(c, 1), &t, (uint32_t)(ft % 10000000ull / 10000));
  w32_ret(c, 1, 2);
}
static void k_SystemTimeToFileTime(rc_ctx *c) {
  uint16_t v[8];
  memcpy(v, RC32_H(w32_arg(c, 0)), 16);
  struct tm t = {.tm_year = v[0] - 1900, .tm_mon = v[1] - 1, .tm_mday = v[3], .tm_hour = v[4], .tm_min = v[5], .tm_sec = v[6]};
  time_t s = timegm(&t);
  w32_wr64(w32_arg(c, 1), ((uint64_t)s + 11644473600ull) * 10000000ull + (uint64_t)v[7] * 10000);
  w32_ret(c, 1, 2);
}
static int64_t tz_bias_100ns(void) {  // local - UTC
  time_t now = time(0);
  struct tm t;
  localtime_r(&now, &t);
  return (int64_t)t.tm_gmtoff * 10000000ll;
}
static void k_FileTimeToLocalFileTime(rc_ctx *c) {
  uint64_t ft;
  memcpy(&ft, RC32_H(w32_arg(c, 0)), 8);
  w32_wr64(w32_arg(c, 1), ft + (uint64_t)tz_bias_100ns());
  w32_ret(c, 1, 2);
}
static void k_LocalFileTimeToFileTime(rc_ctx *c) {
  uint64_t ft;
  memcpy(&ft, RC32_H(w32_arg(c, 0)), 8);
  w32_wr64(w32_arg(c, 1), ft - (uint64_t)tz_bias_100ns());
  w32_ret(c, 1, 2);
}
static void k_GetTimeZoneInformation(rc_ctx *c) {  // the host's offset, no daylight rules
  uint32_t b = w32_arg(c, 0);
  memset(RC32_H(b), 0, 172);
  w32_wr32(b, (uint32_t)(int32_t)(-tz_bias_100ns() / 600000000ll));  // Bias: UTC = local + bias (minutes)
  w32_put_wstr(b + 4, 32, "Local Standard Time");
  w32_ret(c, 0, 1);  // TIME_ZONE_ID_UNKNOWN
}

// --- strings and code pages --------------------------------------------------------------------
// The ANSI code page is 1252; CP_UTF8 (65001) is UTF-8; OEM 437 is read as 1252 (ASCII matters).
static const uint16_t cp1252_hi[32] = {
    0x20ac, 0x81, 0x201a, 0x192, 0x201e, 0x2026, 0x2020, 0x2021, 0x2c6, 0x2030, 0x160, 0x2039, 0x152, 0x8d, 0x17d, 0x8f,
    0x90, 0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013, 0x2014, 0x2dc, 0x2122, 0x161, 0x203a, 0x153, 0x9d, 0x17e, 0x178};
static int is_utf8(uint32_t cp) { return cp == 65001; }
// one code unit sequence: decode at s (n bytes left) into *u; bytes used
static int utf8_dec(const uint8_t *s, int n, uint32_t *u) {
  if (s[0] < 0x80) { *u = s[0]; return 1; }
  int k = s[0] >= 0xf0 ? 3 : s[0] >= 0xe0 ? 2 : s[0] >= 0xc0 ? 1 : 0;
  if (!k || k >= n) { *u = 0xfffd; return 1; }
  uint32_t v = s[0] & (0x3f >> k);
  for (int j = 1; j <= k; j++) {
    if ((s[j] & 0xc0) != 0x80) { *u = 0xfffd; return 1; }
    v = v << 6 | (s[j] & 0x3f);
  }
  *u = v;
  return k + 1;
}
static uint32_t mb2wc(uint32_t cp, const uint8_t *s, int n, uint16_t *out, uint32_t cap) {
  uint32_t o = 0;
  for (int i = 0; i < n;) {
    uint32_t u;
    if (is_utf8(cp)) i += utf8_dec(s + i, n - i, &u);
    else { u = s[i] >= 0x80 && s[i] < 0xa0 ? cp1252_hi[s[i] - 0x80] : s[i]; i++; }
    if (u >= 0x10000) {
      if (out && o + 1 < cap) { out[o] = (uint16_t)(0xd800 + ((u - 0x10000) >> 10)); out[o + 1] = (uint16_t)(0xdc00 + (u & 0x3ff)); }
      o += 2;
    } else {
      if (out && o < cap) out[o] = (uint16_t)u;
      o++;
    }
  }
  return o;
}
static uint32_t wc2mb(uint32_t cp, const uint16_t *s, int n, uint8_t *out, uint32_t cap, int *defused) {
  uint32_t o = 0;
  for (int i = 0; i < n; i++) {
    uint32_t u = s[i];
    if (u >= 0xd800 && u < 0xdc00 && i + 1 < n && s[i + 1] >= 0xdc00 && s[i + 1] < 0xe000) {
      u = 0x10000 + ((u - 0xd800) << 10) + (s[i + 1] - 0xdc00);
      i++;
    }
    uint8_t b[4];
    int k = 0;
    if (is_utf8(cp)) {
      if (u < 0x80) b[k++] = (uint8_t)u;
      else if (u < 0x800) { b[k++] = (uint8_t)(0xc0 | u >> 6); b[k++] = (uint8_t)(0x80 | (u & 0x3f)); }
      else if (u < 0x10000) { b[k++] = (uint8_t)(0xe0 | u >> 12); b[k++] = (uint8_t)(0x80 | (u >> 6 & 0x3f)); b[k++] = (uint8_t)(0x80 | (u & 0x3f)); }
      else { b[k++] = (uint8_t)(0xf0 | u >> 18); b[k++] = (uint8_t)(0x80 | (u >> 12 & 0x3f)); b[k++] = (uint8_t)(0x80 | (u >> 6 & 0x3f)); b[k++] = (uint8_t)(0x80 | (u & 0x3f)); }
    } else {
      int ch = -1;
      if (u < 0x80 || (u >= 0xa0 && u < 0x100)) ch = (int)u;
      else for (int j = 0; j < 32; j++) if (cp1252_hi[j] == u) ch = 0x80 + j;
      if (ch < 0) { ch = '?'; if (defused) *defused = 1; }
      b[k++] = (uint8_t)ch;
    }
    for (int j = 0; j < k; j++, o++)
      if (out && o < cap) out[o] = b[j];
  }
  return o;
}
static uint32_t wlen(uint32_t g) { uint32_t n = 0; while (((uint16_t *)RC32_H(g))[n]) n++; return n; }

char *w32_wstr(uint32_t g, char *out, size_t n) {
  if (!g) { out[0] = 0; return out; }
  uint32_t k = wc2mb(65001, (const uint16_t *)RC32_H(g), (int)wlen(g), (uint8_t *)out, (uint32_t)n - 1, 0);
  out[k < n - 1 ? k : n - 1] = 0;
  return out;
}
uint32_t w32_put_wstr(uint32_t g, uint32_t cap, const char *s) {
  int n = (int)strlen(s);
  uint32_t k = mb2wc(65001, (const uint8_t *)s, n, g ? (uint16_t *)RC32_H(g) : 0, cap);
  if (g && cap) ((uint16_t *)RC32_H(g))[k < cap ? k : cap - 1] = 0;
  return g && k >= cap ? (cap ? cap - 1 : 0) : k;
}

static uint32_t real_cp(uint32_t cp) { return cp == 0 || cp == 1 || cp == 3 || cp == 2 ? 1252 : cp; }  // CP_ACP / OEMCP / MACCP / THREAD_ACP
static void k_MultiByteToWideChar(rc_ctx *c) {
  uint32_t cp = real_cp(w32_arg(c, 0)), src = w32_arg(c, 2), dst = w32_arg(c, 4), cap = w32_arg(c, 5);
  int32_t n = (int32_t)w32_arg(c, 3);
  if (n < 0) n = (int32_t)strlen((const char *)RC32_H(src)) + 1;
  uint32_t need = mb2wc(cp, (const uint8_t *)RC32_H(src), n, 0, 0);
  if (cap && need > cap) { w32_set_error(c, W32_ERROR_INSUFFICIENT_BUFFER); w32_ret(c, 0, 6); return; }
  if (cap) mb2wc(cp, (const uint8_t *)RC32_H(src), n, (uint16_t *)RC32_H(dst), cap);
  w32_ret(c, need, 6);
}
static void k_WideCharToMultiByte(rc_ctx *c) {
  uint32_t cp = real_cp(w32_arg(c, 0)), src = w32_arg(c, 2), dst = w32_arg(c, 4), cap = w32_arg(c, 5), used = w32_arg(c, 7);
  int32_t n = (int32_t)w32_arg(c, 3);
  if (n < 0) n = (int32_t)wlen(src) + 1;
  int def = 0;
  uint32_t need = wc2mb(cp, (const uint16_t *)RC32_H(src), n, 0, 0, &def);
  if (cap && need > cap) { w32_set_error(c, W32_ERROR_INSUFFICIENT_BUFFER); w32_ret(c, 0, 8); return; }
  if (cap) wc2mb(cp, (const uint16_t *)RC32_H(src), n, (uint8_t *)RC32_H(dst), cap, 0);
  if (used) w32_wr32(used, (uint32_t)def);
  w32_ret(c, need, 8);
}
static void k_GetACP(rc_ctx *c) { w32_ret(c, 1252, 0); }
static void k_GetOEMCP(rc_ctx *c) { w32_ret(c, 437, 0); }
static void k_IsValidCodePage(rc_ctx *c) { uint32_t cp = w32_arg(c, 0); w32_ret(c, cp == 1252 || cp == 437 || cp == 65001 || cp == 20127, 1); }
static void k_GetCPInfo(rc_ctx *c) {
  uint32_t cp = real_cp(w32_arg(c, 0)), i = w32_arg(c, 1);
  memset(RC32_H(i), 0, 20);
  w32_wr32(i, is_utf8(cp) ? 4 : 1);  // MaxCharSize
  *(uint8_t *)RC32_H(i + 4) = '?';
  w32_ret(c, 1, 2);
}
static void k_IsDBCSLeadByte(rc_ctx *c) { w32_ret(c, 0, 1); }
static uint16_t ctype1(uint16_t ch) {
  uint16_t t = 0;
  if (ch >= 'A' && ch <= 'Z') t |= 0x1 | 0x100;
  if (ch >= 'a' && ch <= 'z') t |= 0x2 | 0x100;
  if (ch >= 0xc0 && ch < 0x100 && ch != 0xd7 && ch != 0xf7) t |= (ch < 0xe0 ? 0x1 : 0x2) | 0x100;
  if (ch >= '0' && ch <= '9') t |= 0x4;
  if (ch == ' ' || (ch >= 9 && ch <= 13) || ch == 0xa0) t |= 0x8;
  if (ch == ' ' || ch == 9) t |= 0x40;
  if (ch > 32 && ch < 127 && !(t & 0x104)) t |= 0x10;
  if (ch >= 0xa1 && ch < 0xc0) t |= 0x10;
  if (ch < 32 || ch == 127) t |= 0x20;
  if ((ch >= '0' && ch <= '9') || ((ch | 0x20) >= 'a' && (ch | 0x20) <= 'f')) t |= 0x80;
  if (ch >= 0x100) t |= 0x100;  // other scripts: letters, roughly
  return t;
}
static void k_GetStringTypeW(rc_ctx *c) {
  uint32_t type = w32_arg(c, 0), src = w32_arg(c, 1), out = w32_arg(c, 3);
  int32_t n = (int32_t)w32_arg(c, 2);
  if (n < 0) n = (int32_t)wlen(src) + 1;
  for (int32_t k = 0; k < n; k++) {
    uint16_t ch = ((uint16_t *)RC32_H(src))[k];
    ((uint16_t *)RC32_H(out))[k] = type == 1 ? ctype1(ch) : type == 2 ? (ch < 0x80 ? 1 : 0) : 0;  // CT_CTYPE2: C2_LEFTTORIGHT
  }
  w32_ret(c, 1, 4);
}
static void k_GetStringTypeA(rc_ctx *c) {  // (LCID, type, src, n, out)
  uint32_t type = w32_arg(c, 1), src = w32_arg(c, 2), out = w32_arg(c, 4);
  int32_t n = (int32_t)w32_arg(c, 3);
  if (n < 0) n = (int32_t)strlen((const char *)RC32_H(src)) + 1;
  for (int32_t k = 0; k < n; k++) {
    uint8_t b = ((uint8_t *)RC32_H(src))[k];
    uint16_t ch = b >= 0x80 && b < 0xa0 ? cp1252_hi[b - 0x80] : b;
    ((uint16_t *)RC32_H(out))[k] = type == 1 ? ctype1(ch) : 0;
  }
  w32_ret(c, 1, 5);
}
static uint16_t lower(uint16_t ch) { return (ch >= 'A' && ch <= 'Z') || (ch >= 0xc0 && ch < 0xdf && ch != 0xd7) ? ch + 0x20 : ch; }
static uint16_t upper(uint16_t ch) { return (ch >= 'a' && ch <= 'z') || (ch >= 0xe0 && ch < 0xff && ch != 0xf7) ? ch - 0x20 : ch; }
static void k_LCMapStringW(rc_ctx *c) {
  uint32_t fl = w32_arg(c, 1), src = w32_arg(c, 2), dst = w32_arg(c, 4), cap = w32_arg(c, 5);
  int32_t n = (int32_t)w32_arg(c, 3);
  if (n < 0) n = (int32_t)wlen(src) + 1;
  if (fl & 0x400) {  // LCMAP_SORTKEY: the upper-cased string as bytes, then a NUL
    uint32_t need = (uint32_t)n * 2 + 1;
    if (cap) {
      if (cap < need) { w32_set_error(c, W32_ERROR_INSUFFICIENT_BUFFER); w32_ret(c, 0, 6); return; }
      for (int32_t k = 0; k < n; k++) {
        uint16_t ch = upper(((uint16_t *)RC32_H(src))[k]);
        ((uint8_t *)RC32_H(dst))[2 * k] = (uint8_t)(ch >> 8);
        ((uint8_t *)RC32_H(dst))[2 * k + 1] = (uint8_t)ch;
      }
      ((uint8_t *)RC32_H(dst))[2 * n] = 0;
    }
    w32_ret(c, need, 6);
    return;
  }
  if (cap && (uint32_t)n > cap) { w32_set_error(c, W32_ERROR_INSUFFICIENT_BUFFER); w32_ret(c, 0, 6); return; }
  if (cap)
    for (int32_t k = 0; k < n; k++) {
      uint16_t ch = ((uint16_t *)RC32_H(src))[k];
      ((uint16_t *)RC32_H(dst))[k] = (fl & 0x100) ? lower(ch) : (fl & 0x200) ? upper(ch) : ch;
    }
  w32_ret(c, (uint32_t)n, 6);
}
static int cmp_units(const uint16_t *a, int na, const uint16_t *b, int nb, int icase) {
  for (int k = 0; k < na && k < nb; k++) {
    uint16_t x = icase ? lower(a[k]) : a[k], y = icase ? lower(b[k]) : b[k];
    if (x != y) return x < y ? 1 : 3;
  }
  return na == nb ? 2 : na < nb ? 1 : 3;
}
static void k_CompareStringW(rc_ctx *c) {
  uint32_t fl = w32_arg(c, 1), a = w32_arg(c, 2), b = w32_arg(c, 4);
  int32_t na = (int32_t)w32_arg(c, 3), nb = (int32_t)w32_arg(c, 5);
  if (na < 0) na = (int32_t)wlen(a);
  if (nb < 0) nb = (int32_t)wlen(b);
  w32_ret(c, (uint32_t)cmp_units((uint16_t *)RC32_H(a), na, (uint16_t *)RC32_H(b), nb, fl & 1), 6);
}
static void k_CompareStringA(rc_ctx *c) {
  uint32_t fl = w32_arg(c, 1), a = w32_arg(c, 2), b = w32_arg(c, 4);
  int32_t na = (int32_t)w32_arg(c, 3), nb = (int32_t)w32_arg(c, 5);
  if (na < 0) na = (int32_t)strlen((char *)RC32_H(a));
  if (nb < 0) nb = (int32_t)strlen((char *)RC32_H(b));
  uint16_t wa[1024], wb[1024];
  if (na > 1024) na = 1024;
  if (nb > 1024) nb = 1024;
  for (int k = 0; k < na; k++) wa[k] = ((uint8_t *)RC32_H(a))[k];
  for (int k = 0; k < nb; k++) wb[k] = ((uint8_t *)RC32_H(b))[k];
  w32_ret(c, (uint32_t)cmp_units(wa, na, wb, nb, fl & 1), 6);
}
static void k_lstrcmpW(rc_ctx *c) {
  int r = cmp_units((uint16_t *)RC32_H(w32_arg(c, 0)), (int)wlen(w32_arg(c, 0)), (uint16_t *)RC32_H(w32_arg(c, 1)), (int)wlen(w32_arg(c, 1)), 0);
  w32_ret(c, (uint32_t)(r - 2), 2);
}
static void k_lstrcmpiA(rc_ctx *c) {
  int r = strcasecmp(w32_cstr(w32_arg(c, 0)), w32_cstr(w32_arg(c, 1)));
  w32_ret(c, (uint32_t)(r < 0 ? -1 : r > 0), 2);
}
static void k_lstrlenA(rc_ctx *c) { uint32_t s = w32_arg(c, 0); w32_ret(c, s ? (uint32_t)strlen(w32_cstr(s)) : 0, 1); }
static void k_lstrlenW(rc_ctx *c) { uint32_t s = w32_arg(c, 0); w32_ret(c, s ? wlen(s) : 0, 1); }

// --- locale: en-US ------------------------------------------------------------------------------
static void k_GetUserDefaultLCID(rc_ctx *c) { w32_ret(c, 0x409, 0); }
static void k_GetSystemDefaultLCID(rc_ctx *c) { w32_ret(c, 0x409, 0); }
static void k_GetThreadLocale(rc_ctx *c) { w32_ret(c, 0x409, 0); }
static void k_GetUserDefaultLangID(rc_ctx *c) { w32_ret(c, 0x409, 0); }
static void k_GetUserDefaultUILanguage(rc_ctx *c) { w32_ret(c, 0x409, 0); }
static void k_GetSystemDefaultUILanguage(rc_ctx *c) { w32_ret(c, 0x409, 0); }
static void k_IsValidLocale(rc_ctx *c) { w32_ret(c, 1, 2); }
static const char *locale_info(uint32_t t) {
  switch (t & 0xffff) {
  case 0x1: return "0409";           // LOCALE_ILANGUAGE
  case 0x2: return "English (United States)";
  case 0x3: return "ENU";            // LOCALE_SABBREVLANGNAME
  case 0x5: return "1";              // LOCALE_ICOUNTRY
  case 0x6: return "United States";
  case 0x7: return "USA";            // LOCALE_SABBREVCTRYNAME
  case 0xb: return "437";            // LOCALE_IDEFAULTCODEPAGE
  case 0xc: return ",";              // LOCALE_SLIST
  case 0xe: return ".";              // LOCALE_SDECIMAL
  case 0xf: return ",";              // LOCALE_STHOUSAND
  case 0x10: return "3;0";           // LOCALE_SGROUPING
  case 0x11: return "2";             // LOCALE_IDIGITS
  case 0x14: return "$";             // LOCALE_SCURRENCY
  case 0x1d: return "/";             // LOCALE_SDATE
  case 0x1e: return ":";             // LOCALE_STIME
  case 0x1f: return "M/d/yyyy";      // LOCALE_SSHORTDATE
  case 0x20: return "dddd, MMMM d, yyyy";
  case 0x1001: return "English";     // LOCALE_SENGLANGUAGE
  case 0x1002: return "United States";
  case 0x1003: return "h:mm:ss tt";  // LOCALE_STIMEFORMAT
  case 0x1004: return "1252";        // LOCALE_IDEFAULTANSICODEPAGE
  case 0x1009: return "1";           // LOCALE_IFIRSTWEEKOFYEAR... (unused)
  case 0x59: return "en";            // LOCALE_SISO639LANGNAME
  case 0x5a: return "US";            // LOCALE_SISO3166CTRYNAME
  case 0x5c: return "en-US";         // LOCALE_SNAME
  case 0x28: return "AM";
  case 0x29: return "PM";
  }
  return 0;
}
static uint32_t locale_get(rc_ctx *c, uint32_t type, uint32_t buf, uint32_t cap, int wide) {
  const char *v = locale_info(type);
  if (!v) {
    if (w32_log) fprintf(stderr, "w32: GetLocaleInfo(0x%x): not known\n", type);
    w32_set_error(c, W32_ERROR_INVALID_PARAMETER);
    return 0;
  }
  if (type & 0x20000000) {  // LOCALE_RETURN_NUMBER: a DWORD
    if (cap && cap < (wide ? 2u : 4u)) { w32_set_error(c, W32_ERROR_INSUFFICIENT_BUFFER); return 0; }
    if (cap) w32_wr32(buf, (uint32_t)strtoul(v, 0, (type & 0xffff) == 1 ? 16 : 10));
    return wide ? 2 : 4;
  }
  uint32_t n = (uint32_t)strlen(v) + 1;
  if (!cap) return n;
  if (cap < n) { w32_set_error(c, W32_ERROR_INSUFFICIENT_BUFFER); return 0; }
  if (wide) w32_put_wstr(buf, cap, v);
  else memcpy(RC32_H(buf), v, n);
  return n;
}
static void k_GetLocaleInfoA(rc_ctx *c) { w32_ret(c, locale_get(c, w32_arg(c, 1), w32_arg(c, 2), w32_arg(c, 3), 0), 4); }
static void k_GetLocaleInfoW(rc_ctx *c) { w32_ret(c, locale_get(c, w32_arg(c, 1), w32_arg(c, 2), w32_arg(c, 3), 1), 4); }
static void k_EnumSystemLocalesA(rc_ctx *c) {
  uint32_t cb = w32_arg(c, 0), s = w32_gstr("00000409");
  w32_guest_call(c, cb, W32_STDCALL, 0, &s, 1, 0);
  w32_ret(c, 1, 2);
}

// --- environment, command line, console --------------------------------------------------------
// The guest's environment: empty but for what the game asks of Windows; LP_ARGS is appended to its
// command line.
static void k_GetCommandLineA(rc_ctx *c) {
  static uint32_t g;
  if (!g) {
    char s[1024];
    const char *a = getenv("LP_ARGS");
    snprintf(s, sizeof s, "\"%s\\Rocksmith2014.exe\"%s%s", w32_game_dir_win(), a && *a ? " " : "", a ? a : "");
    g = w32_gstr(s);
  }
  w32_ret(c, g, 0);
}
static void k_GetCommandLineW(rc_ctx *c) {
  static uint32_t g;
  if (!g) {
    char s[1024];
    const char *a = getenv("LP_ARGS");
    snprintf(s, sizeof s, "\"%s\\Rocksmith2014.exe\"%s%s", w32_game_dir_win(), a && *a ? " " : "", a ? a : "");
    uint32_t n = w32_put_wstr(0, 0, s) + 1;
    g = rc32_alloc(2 * n);
    w32_put_wstr(g, n, s);
  }
  w32_ret(c, g, 0);
}
#define MAXENV 64
static char *envv[MAXENV];
static pthread_mutex_t env_mu = PTHREAD_MUTEX_INITIALIZER;
static void env_init(void) {
  static int done;
  if (done) return;
  done = 1;
  static const char *dflt[] = {"SystemRoot=C:\\Windows", "windir=C:\\Windows", "SystemDrive=C:", "OS=Windows_NT",
                               "USERNAME=Player", "USERPROFILE=C:\\Users\\Player", "HOMEDRIVE=C:", "HOMEPATH=\\Users\\Player",
                               "APPDATA=C:\\Users\\Player\\AppData\\Roaming", "LOCALAPPDATA=C:\\Users\\Player\\AppData\\Local",
                               "TEMP=C:\\Users\\Player\\AppData\\Local\\Temp", "TMP=C:\\Users\\Player\\AppData\\Local\\Temp",
                               "PROGRAMFILES=C:\\Program Files (x86)", "NUMBER_OF_PROCESSORS=4", "PROCESSOR_ARCHITECTURE=x86",
                               "COMPUTERNAME=PC"};
  for (size_t k = 0; k < sizeof dflt / sizeof dflt[0]; k++) envv[k] = strdup(dflt[k]);
}
static const char *env_get(const char *n) {
  env_init();
  size_t l = strlen(n);
  for (int k = 0; k < MAXENV; k++)
    if (envv[k] && !strncasecmp(envv[k], n, l) && envv[k][l] == '=') return envv[k] + l + 1;
  return 0;
}
static void env_set(const char *n, const char *v) {
  pthread_mutex_lock(&env_mu);
  env_init();
  size_t l = strlen(n);
  int free_k = -1;
  for (int k = 0; k < MAXENV; k++) {
    if (envv[k] && !strncasecmp(envv[k], n, l) && envv[k][l] == '=') {
      free(envv[k]);
      envv[k] = 0;
    }
    if (!envv[k] && free_k < 0) free_k = k;
  }
  if (v && free_k >= 0) {
    envv[free_k] = malloc(l + strlen(v) + 2);
    sprintf(envv[free_k], "%s=%s", n, v);
  }
  pthread_mutex_unlock(&env_mu);
}
static void k_GetEnvironmentStringsW(rc_ctx *c) {
  pthread_mutex_lock(&env_mu);
  env_init();
  char blk[8192];
  size_t o = 0;
  for (int k = 0; k < MAXENV; k++)
    if (envv[k] && o + strlen(envv[k]) + 2 < sizeof blk) o += (size_t)snprintf(blk + o, sizeof blk - o, "%s", envv[k]) + 1;
  blk[o++] = 0;
  pthread_mutex_unlock(&env_mu);
  uint32_t g = w32_heap_alloc(2 * (uint32_t)o);
  for (size_t k = 0; k < o; k++) ((uint16_t *)RC32_H(g))[k] = (uint8_t)blk[k];
  w32_ret(c, g, 0);
}
static void k_GetEnvironmentStrings(rc_ctx *c) {
  pthread_mutex_lock(&env_mu);
  env_init();
  char blk[8192];
  size_t o = 0;
  for (int k = 0; k < MAXENV; k++)
    if (envv[k] && o + strlen(envv[k]) + 2 < sizeof blk) o += (size_t)snprintf(blk + o, sizeof blk - o, "%s", envv[k]) + 1;
  blk[o++] = 0;
  pthread_mutex_unlock(&env_mu);
  uint32_t g = w32_heap_alloc((uint32_t)o);
  memcpy(RC32_H(g), blk, o);
  w32_ret(c, g, 0);
}
static void k_FreeEnvironmentStringsW(rc_ctx *c) { w32_heap_free(w32_arg(c, 0)); w32_ret(c, 1, 1); }
static void k_FreeEnvironmentStringsA(rc_ctx *c) { w32_heap_free(w32_arg(c, 0)); w32_ret(c, 1, 1); }
static void k_GetEnvironmentVariableA(rc_ctx *c) {
  const char *v = env_get(w32_cstr(w32_arg(c, 0)));
  uint32_t b = w32_arg(c, 1), cap = w32_arg(c, 2);
  if (!v) { w32_set_error(c, W32_ERROR_ENVVAR_NOT_FOUND); w32_ret(c, 0, 3); return; }
  uint32_t n = (uint32_t)strlen(v);
  if (cap <= n) { w32_ret(c, n + 1, 3); return; }
  memcpy(RC32_H(b), v, n + 1);
  w32_ret(c, n, 3);
}
static void k_GetEnvironmentVariableW(rc_ctx *c) {
  char n[256];
  const char *v = env_get(w32_wstr(w32_arg(c, 0), n, sizeof n));
  uint32_t b = w32_arg(c, 1), cap = w32_arg(c, 2);
  if (!v) { w32_set_error(c, W32_ERROR_ENVVAR_NOT_FOUND); w32_ret(c, 0, 3); return; }
  uint32_t need = w32_put_wstr(0, 0, v);
  if (cap <= need) { w32_ret(c, need + 1, 3); return; }
  w32_ret(c, w32_put_wstr(b, cap, v), 3);
}
static void k_SetEnvironmentVariableA(rc_ctx *c) {
  env_set(w32_cstr(w32_arg(c, 0)), w32_arg(c, 1) ? w32_cstr(w32_arg(c, 1)) : 0);
  w32_ret(c, 1, 2);
}
static void k_SetEnvironmentVariableW(rc_ctx *c) {
  char n[256], v[1024];
  env_set(w32_wstr(w32_arg(c, 0), n, sizeof n), w32_arg(c, 1) ? w32_wstr(w32_arg(c, 1), v, sizeof v) : 0);
  w32_ret(c, 1, 2);
}
static void k_ExpandEnvironmentStringsA(rc_ctx *c) {
  const char *s = w32_cstr(w32_arg(c, 0));
  uint32_t dst = w32_arg(c, 1), cap = w32_arg(c, 2);
  char out[4096];
  size_t o = 0;
  while (*s && o < sizeof out - 1) {
    const char *e = *s == '%' ? strchr(s + 1, '%') : 0;
    if (e) {
      char n[256];
      snprintf(n, sizeof n, "%.*s", (int)(e - s - 1), s + 1);
      const char *v = env_get(n);
      if (v) {
        o += (size_t)snprintf(out + o, sizeof out - o, "%s", v);
        if (o >= sizeof out) o = sizeof out - 1;  // snprintf counts what did not fit
        s = e + 1;
        continue;
      }
    }
    out[o++] = *s++;
  }
  out[o] = 0;
  uint32_t n = (uint32_t)o + 1;
  if (cap >= n) memcpy(RC32_H(dst), out, n);
  w32_ret(c, n, 3);
}
static void k_GetStartupInfoW(rc_ctx *c) { memset(RC32_H(w32_arg(c, 0)), 0, 68); w32_wr32(w32_arg(c, 0), 68); w32_ret(c, 0, 1); }
static void k_GetStartupInfoA(rc_ctx *c) { memset(RC32_H(w32_arg(c, 0)), 0, 68); w32_wr32(w32_arg(c, 0), 68); w32_ret(c, 0, 1); }
static void k_SetHandleCount(rc_ctx *c) { w32_ret(c, w32_arg(c, 0), 1); }
static void k_GetConsoleMode(rc_ctx *c) { w32_set_error(c, W32_ERROR_INVALID_HANDLE); w32_ret(c, 0, 2); }  // not a console: the CRT writes bytes
static void k_SetConsoleMode(rc_ctx *c) { w32_ret(c, 0, 2); }
static void k_GetConsoleCP(rc_ctx *c) { w32_ret(c, 437, 0); }
static void k_GetConsoleOutputCP(rc_ctx *c) { w32_ret(c, 437, 0); }
static void k_SetConsoleCtrlHandler(rc_ctx *c) { w32_ret(c, 1, 2); }
static void k_FlushConsoleInputBuffer(rc_ctx *c) { w32_ret(c, 1, 1); }
static void k_GetNumberOfConsoleInputEvents(rc_ctx *c) { if (w32_arg(c, 1)) w32_wr32(w32_arg(c, 1), 0); w32_ret(c, 1, 2); }
static void k_WriteConsoleW(rc_ctx *c) {
  uint32_t b = w32_arg(c, 1), n = w32_arg(c, 2);
  char s[4096];
  uint32_t k = wc2mb(65001, (uint16_t *)RC32_H(b), (int)n, (uint8_t *)s, sizeof s, 0);
  fwrite(s, 1, k < sizeof s ? k : sizeof s, stderr);
  if (w32_arg(c, 3)) w32_wr32(w32_arg(c, 3), n);
  w32_ret(c, 1, 5);
}

#define E(n) {#n, k_##n}
static const w32_export fns[] = {
    E(HeapCreate), E(HeapDestroy), E(GetProcessHeap), E(HeapAlloc), E(HeapFree), E(HeapSize), E(HeapReAlloc),
    E(HeapValidate), E(HeapCompact), E(HeapWalk), E(HeapSetInformation), E(HeapQueryInformation),
    E(GlobalAlloc), E(GlobalFree), E(GlobalLock), E(GlobalUnlock), E(GlobalUnWire), E(GlobalSize),
    E(LocalAlloc), E(LocalFree), E(VirtualAlloc), E(VirtualFree), E(VirtualProtect), E(VirtualQuery),
    E(IsBadReadPtr), E(IsBadWritePtr), E(IsBadCodePtr), E(GetModuleHandleA), E(GetModuleHandleW),
    E(LoadLibraryA), E(LoadLibraryW), E(LoadLibraryExA), E(LoadLibraryExW), E(FreeLibrary),
    E(DisableThreadLibraryCalls), E(GetProcAddress), E(GetModuleFileNameA), E(GetModuleFileNameW),
    E(GetCurrentProcess), E(GetCurrentProcessId), E(ExitProcess), E(TerminateProcess), E(GetExitCodeProcess),
    E(CreateProcessA), E(CreateProcessW), E(CreatePipe), E(PeekNamedPipe), E(GetVersion), E(GetVersionExA),
    E(GetVersionExW), E(GetSystemInfo), E(GlobalMemoryStatus), E(GlobalMemoryStatusEx),
    E(IsProcessorFeaturePresent), E(IsDebuggerPresent), E(CheckRemoteDebuggerPresent),
    E(GetProcessAffinityMask), E(SetProcessAffinityMask), E(GetPriorityClass), E(SetPriorityClass),
    E(SetErrorMode), E(SetThreadExecutionState), E(SetUnhandledExceptionFilter), E(UnhandledExceptionFilter),
    E(OutputDebugStringA), E(OutputDebugStringW), E(DebugBreak), E(Beep), E(GetLastError), E(SetLastError),
    E(EncodePointer), E(DecodePointer), E(FormatMessageA), {"RaiseException", k_RaiseException_},
    {"RtlUnwind", k_RtlUnwind_}, E(GetTickCount), E(GetTickCount64), E(QueryPerformanceCounter),
    E(QueryPerformanceFrequency), E(GetSystemTimeAsFileTime), E(GetLocalTime), E(GetSystemTime), E(SetLocalTime),
    E(FileTimeToSystemTime), E(SystemTimeToFileTime), E(FileTimeToLocalFileTime), E(LocalFileTimeToFileTime),
    E(GetTimeZoneInformation), E(MultiByteToWideChar), E(WideCharToMultiByte), E(GetACP), E(GetOEMCP),
    E(IsValidCodePage), E(GetCPInfo), E(IsDBCSLeadByte), E(GetStringTypeW), E(GetStringTypeA), E(LCMapStringW),
    E(CompareStringW), E(CompareStringA), E(lstrcmpW), E(lstrcmpiA), E(lstrlenA), E(lstrlenW),
    {"lstrlen", k_lstrlenA}, E(GetUserDefaultLCID), E(GetSystemDefaultLCID), E(GetThreadLocale),
    E(GetUserDefaultLangID), E(GetUserDefaultUILanguage), E(GetSystemDefaultUILanguage), E(IsValidLocale),
    E(GetLocaleInfoA), E(GetLocaleInfoW), E(EnumSystemLocalesA), E(GetCommandLineA), E(GetCommandLineW),
    E(GetEnvironmentStringsW), E(GetEnvironmentStrings), E(FreeEnvironmentStringsW), E(FreeEnvironmentStringsA),
    E(GetEnvironmentVariableA), E(GetEnvironmentVariableW), E(SetEnvironmentVariableA),
    E(SetEnvironmentVariableW), E(ExpandEnvironmentStringsA), E(GetStartupInfoW), E(GetStartupInfoA),
    E(SetHandleCount), E(GetConsoleMode), E(SetConsoleMode), E(GetConsoleCP), E(GetConsoleOutputCP),
    E(SetConsoleCtrlHandler), E(FlushConsoleInputBuffer), E(GetNumberOfConsoleInputEvents), E(WriteConsoleW),
};
extern const w32_dll w32_dll_kernel32_file;
W32_DLL(w32_dll_kernel32, "kernel32.dll", fns, &w32_dll_kernel32_file);

// ntdll: what the DLLs import from it directly
static const w32_export ntdll_fns[] = {{"RtlUnwind", k_RtlUnwind_}};
W32_DLL(w32_dll_ntdll, "ntdll.dll", ntdll_fns, 0);
