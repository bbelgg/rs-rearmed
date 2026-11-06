// The host side of the game's and the DLLs' imports, by name (win32_shims.h): the main image's
// rc32_import_call, the --module images' rc32_host_import, and the cells GetProcAddress hands out
// for host DLLs.
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "audio/audio.h"
#include "steam/steam32.h"
#include "win32_shims.h"

extern const w32_dll w32_dll_kernel32, w32_dll_ntdll, w32_dll_user32, w32_dll_gdi32, w32_dll_advapi32,
    w32_dll_ole32, w32_dll_oleaut32, w32_dll_shell32, w32_dll_winmm, w32_dll_ws2_32, w32_dll_iphlpapi,
    w32_dll_wininet, w32_dll_setupapi, w32_dll_rpcrt4, w32_dll_dinput8, w32_dll_xinput1_3, w32_dll_d3d9,
    w32_dll_bink2w32, w32_dll_avrt;
static const w32_dll *const dlls[] = {
    &w32_dll_kernel32, &w32_dll_ntdll, &w32_dll_user32, &w32_dll_gdi32, &w32_dll_advapi32, &w32_dll_ole32,
    &w32_dll_oleaut32, &w32_dll_shell32, &w32_dll_winmm, &w32_dll_ws2_32, &w32_dll_iphlpapi,
    &w32_dll_wininet, &w32_dll_setupapi, &w32_dll_rpcrt4, &w32_dll_dinput8, &w32_dll_xinput1_3,
    &w32_dll_d3d9, &w32_dll_bink2w32, &w32_dll_avrt,
};
#define NDLLS (int)(sizeof dlls / sizeof dlls[0])


// "kernel32.dll" / "KERNEL32" / "c:\windows\system32\kernel32.dll" against a table's name
static int dll_is(const char *tab, const char *s, size_t n) {
  for (size_t k = 0; k < n; k++)
    if (s[k] == '\\' || s[k] == '/') return dll_is(tab, s + k + 1, n - k - 1);
  size_t t = strlen(tab);
  if (n == t) return !strncasecmp(tab, s, n);
  return n == t - 4 && !strncasecmp(tab, s, n);  // without ".dll"
}

static const w32_dll *dll_named(const char *s, size_t n) {
  for (int k = 0; k < NDLLS; k++)
    if (dll_is(dlls[k]->dll, s, n)) return dlls[k];
  return 0;
}

// the winmm functions the audio layer serves (audio_dispatch, by name): run() calls it for this
static void audio_marker(rc_ctx *c) { (void)c; }

rc_fn w32_import(const char *imp) {
  const char *bang = strchr(imp, '!');
  if (!bang) return 0;
  if (dll_is("steam_api.dll", imp, (size_t)(bang - imp))) return steam32_import(imp);
  if (dll_is("winmm.dll", imp, (size_t)(bang - imp)) && audio_import(bang + 1)) return audio_marker;
  const w32_dll *d = dll_named(imp, (size_t)(bang - imp));
  if (!d) return 0;
  for (; d; d = d->more)
    for (int k = 0; k < d->n; k++)
      if (!strcmp(d->fns[k].name, bang + 1)) return d->fns[k].fn;
  return 0;
}

// 1 when the host provides a DLL of this name (LoadLibrary / GetModuleHandle answer for it)
int w32_host_dll(const char *dll) {
  return dll_named(dll, strlen(dll)) != 0 || dll_is("steam_api.dll", dll, strlen(dll));
}

// --- LP_PROF_IMPORTS: which imports are called, from where, how often ---------------------------
// Every LP_PROF_IMPORTS seconds, the import calls of that interval by name, thread, the result
// (eax, 0x200 for any value past the codes) and the host return addresses of the call (symbolize them with atos), most called first.
#define PROF_N 65536
#define PROF_FR 6
typedef struct {
  const char *name;
  uint32_t tid, eax;
  void *fr[PROF_FR];
  uint64_t n, last;
} prof_ent;
static prof_ent *prof;
static pthread_mutex_t prof_mu = PTHREAD_MUTEX_INITIALIZER;
static double prof_secs;

// indirect calls (rc32_icall_trace) by target, and a name for the targets that a {name, function}
// table of the game names (the Lua bindings)
#define ICALL_N 65536
static struct { uint32_t g; uint32_t n, last; } icalls[ICALL_N];
static struct { uint32_t g; const char *name; } *fnames;
static int nfnames;

static volatile int icall_new_log;  // LP_PROF_NEW: from that round on, log each target seen for the first time
static const char *fname_of(uint32_t g);
static void icall_note(rc_ctx *c, uint32_t g) {
  for (uint32_t i = 0, s = (g * 2654435761u) % ICALL_N; i < 64; i++, s = (s + 1) % ICALL_N) {
    uint32_t have = __atomic_load_n(&icalls[s].g, __ATOMIC_RELAXED);
    if (!have && __atomic_compare_exchange_n(&icalls[s].g, &have, g, 0, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
      have = g;
      if (icall_new_log) {
        uint32_t esp = (uint32_t)c->r[4];
        fprintf(stderr, "w32: prof new icall 0x%x %s tid %x ecx %x edx %x stack %x %x %x %x %x %x\n", g, fname_of(g),
                *w32_teb(c, RC32_TEB_TID), (uint32_t)c->r[1], (uint32_t)c->r[2], w32_rd32(esp), w32_rd32(esp + 4),
                w32_rd32(esp + 8), w32_rd32(esp + 12), w32_rd32(esp + 16), w32_rd32(esp + 20));
      }
    }
    if (have == g) {
      __atomic_fetch_add(&icalls[s].n, 1, __ATOMIC_RELAXED);
      return;
    }
  }
}
static int ident(uint32_t s) {
  const uint8_t *p = RC32_H(s);
  int k = 0;
  if (!((p[0] >= 'A' && p[0] <= 'Z') || (p[0] >= 'a' && p[0] <= 'z') || p[0] == '_')) return 0;
  for (; p[k] && k < 64; k++)
    if (!((p[k] >= 'A' && p[k] <= 'Z') || (p[k] >= 'a' && p[k] <= 'z') || (p[k] >= '0' && p[k] <= '9') || p[k] == '_'))
      return 0;
  return !p[k] && k >= 3;
}
static int fname_cmp(const void *a, const void *b) {
  uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
  return x < y ? -1 : x > y;
}
static void fnames_scan(void) {
  uint32_t base = rc32_image_base(0);
  if (!base) return;
  uint32_t pe = w32_rd32(base + 0x3c), size = w32_rd32(base + pe + 0x50), end = base + size;
  int cap = 0;
  for (uint32_t a = base; a + 8 <= end; a += 4) {
    uint32_t s = w32_rd32(a), f = w32_rd32(a + 4);
    if (s < base || s >= end - 4 || f < base || f >= end || !rc32_lookup(f) || !ident(s)) continue;
    if (nfnames == cap) {
      cap = cap ? 2 * cap : 4096;
      fnames = realloc(fnames, (size_t)cap * sizeof *fnames);
    }
    fnames[nfnames].g = f;
    fnames[nfnames++].name = (const char *)RC32_H(s);
  }
  qsort(fnames, (size_t)nfnames, sizeof *fnames, fname_cmp);
}
static const char *fname_of(uint32_t g) {
  int lo = 0, hi = nfnames - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    if (fnames[mid].g == g) {
      while (mid > 0 && fnames[mid - 1].g == g) mid--;
      return fnames[mid].name;
    }
    if (fnames[mid].g < g) lo = mid + 1;
    else hi = mid - 1;
  }
  return "";
}
static int icall_cmp(const void *a, const void *b) {
  uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
  uint32_t dx = icalls[x].n - icalls[x].last, dy = icalls[y].n - icalls[y].last;
  return dx < dy ? 1 : dx > dy ? -1 : 0;
}
// the named targets of this interval (all of them: they are few), then the busiest others
static void icall_dump(void) {
  static uint32_t v[ICALL_N];
  int m = 0;
  for (uint32_t k = 0; k < ICALL_N; k++)
    if (icalls[k].g && icalls[k].n != icalls[k].last) v[m++] = k;
  qsort(v, (size_t)m, sizeof *v, icall_cmp);
  int shown = 0;
  for (int k = 0; k < m; k++) {
    const char *nm = fname_of(icalls[v[k]].g);
    if (*nm || shown < 30) {
      fprintf(stderr, "w32: prof icall %7u 0x%x %s\n", icalls[v[k]].n - icalls[v[k]].last, icalls[v[k]].g, nm);
      if (!*nm) shown++;
    }
  }
  for (int k = 0; k < m; k++) icalls[v[k]].last = icalls[v[k]].n;
}

static int prof_cmp(const void *a, const void *b) {
  const prof_ent *x = *(prof_ent *const *)a, *y = *(prof_ent *const *)b;
  uint64_t dx = x->n - x->last, dy = y->n - y->last;
  return dx < dy ? 1 : dx > dy ? -1 : 0;
}
static void *prof_dump(void *arg) {
  (void)arg;
  static prof_ent *v[PROF_N];
  for (int round = 1;; round++) {
    usleep((useconds_t)(prof_secs * 1e6));
    pthread_mutex_lock(&prof_mu);
    int m = 0;
    for (int k = 0; k < PROF_N; k++)
      if (prof[k].name && prof[k].n != prof[k].last) v[m++] = &prof[k];
    qsort(v, (size_t)m, sizeof *v, prof_cmp);
    fprintf(stderr, "w32: prof %d (%.0f s): %d call sites\n", round, round * prof_secs, m);
    for (int k = 0; k < m && k < 60; k++) {
      fprintf(stderr, "w32: prof   %7llu %-28s tid %x eax %x:", (unsigned long long)(v[k]->n - v[k]->last),
              v[k]->name, v[k]->tid, v[k]->eax);
      for (int f = 0; f < PROF_FR; f++) fprintf(stderr, " %p", v[k]->fr[f]);
      fprintf(stderr, "\n");
    }
    for (int k = 0; k < m; k++) v[k]->last = v[k]->n;
    pthread_mutex_unlock(&prof_mu);
    if (round == 1) fnames_scan();
    if (round == 1) {
      rc32_icall_trace = icall_note;
      memset(rc32_lookup_cache, 0, sizeof rc32_lookup_cache);   // from now on every call reaches rc32_icall
    }
    else icall_dump();
    const char *nw = getenv("LP_PROF_NEW");
    if (nw && round >= atoi(nw)) icall_new_log = 1;
  }
  return 0;
}
static void prof_note(rc_ctx *c, const char *name) {
  void *fr[PROF_FR] = {0};
  void **fp = __builtin_frame_address(0);
  for (int k = 0; k < PROF_FR && fp; k++) {  // the frame-pointer chain
    fr[k] = fp[1];
    void **up = fp[0];
    if (up <= fp) break;
    fp = up;
  }
  uint32_t tid = *w32_teb(c, RC32_TEB_TID), eax = (uint32_t)c->r[0];
  if (eax >= 0x200 && eax != 0xffffffffu) eax = 0x200;  // results that are not codes: one class
  uintptr_t hsh = (uintptr_t)name ^ tid * 31u ^ eax * 131u;
  for (int k = 0; k < PROF_FR; k++) hsh = hsh * 1000003u ^ (uintptr_t)fr[k];
  pthread_mutex_lock(&prof_mu);
  for (uint32_t i = 0, s = (uint32_t)(hsh % PROF_N); i < 64; i++, s = (s + 1) % PROF_N) {
    prof_ent *e = &prof[s];
    if (!e->name) {
      e->name = name;
      e->tid = tid;
      e->eax = eax;
      memcpy(e->fr, fr, sizeof fr);
    } else if (e->name != name || e->tid != tid || e->eax != eax || memcmp(e->fr, fr, sizeof fr)) {
      continue;
    }
    e->n++;
    break;
  }
  pthread_mutex_unlock(&prof_mu);
}

// --- calls --------------------------------------------------------------------------------------
static void run(rc_ctx *c, const char *name, rc_fn f, uint8_t *seen) {
  uint64_t t0 = LP_FRAME_ON() ? lp_frame_now() : 0;
  int first = seen && !*seen;
  if (w32_log > 1 || (w32_log && seen && !*seen)) {
    if (seen) *seen = 1;
    fprintf(stderr, "w32: %s%s (esp 0x%x)\n", w32_log > 1 ? "" : "first call: ", name, (uint32_t)c->r[4]);
  }
  if (f == audio_marker) {
    if (!audio_dispatch(c, name)) rc_fail("w32: audio_dispatch refused an import it named", 0);
  } else {
    f(c);
  }
  if (prof) prof_note(c, name);
  if (t0) {
    uint64_t dt = lp_frame_now() - t0;
    lp_frame_call(name, dt);
    if (first) lp_frame_event(LPF_IMPORT, dt, 0);
  }
}

static _Noreturn void missing(const char *name, uint32_t slot) {
  fprintf(stderr, "w32: import not implemented: %s\n", name);
  rc_fail("import not implemented", slot);
}

// the main image (the game): k indexes rc32_imports
void rc32_import_call(rc_ctx *c, int k) {
  static rc_fn cache[4096];
  static uint8_t seen[4096];
  rc_fn f = k < 4096 ? __atomic_load_n(&cache[k], __ATOMIC_ACQUIRE) : 0;
  if (!f) {
    f = w32_import(rc32_imports[k].name);
    if (!f) missing(rc32_imports[k].name, rc32_imports[k].slot);
    if (k < 4096) __atomic_store_n(&cache[k], f, __ATOMIC_RELEASE);
  }
  run(c, rc32_imports[k].name, f, k < 4096 ? &seen[k] : 0);
}

// a --module image (msvcr100, d3dx9_42, ...): its imports no other image exports
#define MAXMOD 8
static struct { const rc32_module *m; rc_fn *fns; uint8_t *seen; } mods[MAXMOD];
static pthread_mutex_t mod_mu = PTHREAD_MUTEX_INITIALIZER;
static void host_import(rc_ctx *c, const rc32_module *m, int k) {
  int i;
  for (i = 0; i < MAXMOD && __atomic_load_n(&mods[i].m, __ATOMIC_ACQUIRE) != m; i++)
    if (!__atomic_load_n(&mods[i].m, __ATOMIC_ACQUIRE)) break;
  if (i == MAXMOD || mods[i].m != m) {
    pthread_mutex_lock(&mod_mu);
    for (i = 0; i < MAXMOD && mods[i].m && mods[i].m != m; i++) {}
    if (i < MAXMOD && !mods[i].m) {
      mods[i].fns = calloc((size_t)*m->imports_len + 1, sizeof(rc_fn));
      mods[i].seen = calloc((size_t)*m->imports_len + 1, 1);
      __atomic_store_n(&mods[i].m, m, __ATOMIC_RELEASE);
    }
    pthread_mutex_unlock(&mod_mu);
    if (i == MAXMOD) rc_fail("w32: too many modules", 0);
  }
  rc_fn f = __atomic_load_n(&mods[i].fns[k], __ATOMIC_ACQUIRE);
  if (!f) {
    f = w32_import(m->imports[k].name);
    if (!f) missing(m->imports[k].name, m->imports[k].slot);
    __atomic_store_n(&mods[i].fns[k], f, __ATOMIC_RELEASE);
  }
  run(c, m->imports[k].name, f, &mods[i].seen[k]);
}

// --- GetProcAddress cells ------------------------------------------------------------------------
// One word per function in a reserved block (int3 bytes, never run), as the import area has one
// per import; the call pops its return address and runs the shim with the arguments at esp.
#define PCELLS 1024
static struct { const char *name; rc_fn fn; } pcell[PCELLS];
static uint32_t pcell_area, npcell;
static pthread_mutex_t pcell_mu = PTHREAD_MUTEX_INITIALIZER;

static int pcell_call(rc_ctx *c, uint32_t g) {
  uint32_t n = __atomic_load_n(&npcell, __ATOMIC_ACQUIRE), k = (g - pcell_area) / 4;
  if (!pcell_area || g < pcell_area || k >= n || (g & 3)) return 0;
  memcpy(&c->ret_to, RC32_H((uint32_t)c->r[4]), 4);
  c->r[4] = (uint32_t)(c->r[4] + 4);
  run(c, pcell[k].name, pcell[k].fn, 0);
  return 1;
}

uint32_t w32_proc_cell(const char *name, rc_fn fn) {
  pthread_mutex_lock(&pcell_mu);
  uint32_t g = 0;
  for (uint32_t k = 0; k < npcell; k++)
    if (pcell[k].fn == fn) g = pcell_area + 4 * k;
  if (!g) {
    if (!pcell_area) {
      pcell_area = rc32_alloc(4 * PCELLS);
      memset(RC32_H(pcell_area), 0xcc, 4 * PCELLS);
      rc32_hostfn_hook_add(pcell_call);
    }
    if (npcell < PCELLS) {
      pcell[npcell].name = strdup(name);
      pcell[npcell].fn = fn;
      g = pcell_area + 4 * npcell;
      __atomic_store_n(&npcell, npcell + 1, __ATOMIC_RELEASE);
    }
  }
  pthread_mutex_unlock(&pcell_mu);
  return g;
}

void w32_init(void) {
  const char *l = getenv("LP_LOG");
  w32_log = l ? atoi(l) : 0;
  rc32_host_import = host_import;
  const char *p = getenv("LP_PROF_IMPORTS");
  if (p && atof(p) > 0) {
    prof_secs = atof(p);
    prof = calloc(PROF_N, sizeof *prof);
    pthread_t th;
    if (prof && !pthread_create(&th, 0, prof_dump, 0)) pthread_detach(th);
  }
}
