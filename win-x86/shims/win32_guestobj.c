// Host functions callable from the guest, guest vtables and objects, native -> guest calls from a
// shim, freeable guest memory and interned guest strings (win32_guestobj.h).
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "win32_guestobj.h"

extern int w32_log __attribute__((weak));  // win32_dispatch.c (LP_LOG); absent in seam-less tests
void (*w32_call_timed)(const char *name, uint64_t ns);

// --- thunk cells ---------------------------------------------------------------------------
#define CELLS 8192
#define MAXWORDS 32
typedef struct {
  const char *name;
  uint8_t conv, words, sret;
  int warned;
  w32_hostfn fn;
  uint64_t dflt;
} cell;
static cell cells[CELLS];
static uint32_t cell_area, ncells;
static pthread_mutex_t cell_mu = PTHREAD_MUTEX_INITIALIZER;

static uint32_t cell_new(const char *name, int conv, int words, int sret, w32_hostfn fn, uint64_t dflt) {
  if (words > MAXWORDS) return 0;
  pthread_mutex_lock(&cell_mu);
  uint32_t g = 0;
  if (!cell_area) {
    cell_area = rc32_alloc(4 * CELLS);
    if (cell_area) memset(RC32_H(cell_area), 0xcc, 4 * CELLS);  // int3: never run
    rc32_hostfn_hook_add(w32_hostfn_call);  // rc32_icall offers the cells' addresses here
  }
  if (cell_area && ncells < CELLS) {
    cells[ncells] = (cell){name, (uint8_t)conv, (uint8_t)words, (uint8_t)sret, 0, fn, dflt};
    g = cell_area + 4 * ncells;
    __atomic_store_n(&ncells, ncells + 1, __ATOMIC_RELEASE);
  }
  pthread_mutex_unlock(&cell_mu);
  if (!g) fprintf(stderr, "w32: no thunk cell for %s\n", name);
  return g;
}

uint32_t w32_hostfn_new(const char *name, int conv, int words, w32_hostfn fn, uint64_t dflt) {
  return cell_new(name, conv, words, 0, fn, dflt);
}

int w32_hostfn_call(rc_ctx *c, uint32_t g) {
  uint32_t n = __atomic_load_n(&ncells, __ATOMIC_ACQUIRE);
  uint32_t k = (g - cell_area) / 4;
  if (!cell_area || g < cell_area || k >= n || (g & 3)) return 0;
  cell *e = &cells[k];
  uint32_t esp = (uint32_t)c->r[4], ret_to;
  memcpy(&ret_to, RC32_H(esp), 4);
  esp += 4;  // the return address
  uint32_t a[MAXWORDS];
  memcpy(a, RC32_H(esp), 4 * (size_t)e->words);
  uint32_t self = e->conv == W32_THISCALL ? (uint32_t)c->r[1] : 0;
  uint64_t r;
  void (*timed)(const char *, uint64_t) = w32_call_timed;
  if (e->fn && timed) {
    uint64_t t0 = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
    r = e->fn(c, self, a);
    timed(e->name, clock_gettime_nsec_np(CLOCK_UPTIME_RAW) - t0);
  } else if (e->fn) {
    r = e->fn(c, self, a);
  } else {
    if (!__atomic_exchange_n(&e->warned, 1, __ATOMIC_RELAXED))
      fprintf(stderr, "w32: %s not implemented (returns 0x%llx)\n", e->name, (unsigned long long)e->dflt);
    r = e->dflt;
    if (e->sret && e->words) {
      w32_wr64(a[0], 0);
      r = a[0];
    }
  }
  if (&w32_log && w32_log > 2) fprintf(stderr, "w32: %s -> 0x%llx\n", e->name, (unsigned long long)r);  // LP_LOG=3
  c->r[0] = (uint32_t)r;
  c->r[2] = (uint32_t)(r >> 32);
  c->r[4] = e->conv == W32_CDECL ? esp : esp + 4 * (uint32_t)e->words;
  c->ret_to = ret_to;
  return 1;
}

typedef struct vt_cache {
  const w32_slot *slots;
  uint32_t g;
  struct vt_cache *next;
} vt_cache;
static vt_cache *vts;

static pthread_mutex_t vt_mu = PTHREAD_MUTEX_INITIALIZER;  // one vtable per slot table, made once

uint32_t w32_vtable(const w32_slot *slots, int n, int conv, uint64_t dflt) {
  pthread_mutex_lock(&vt_mu);
  vt_cache *v = vts;
  while (v && v->slots != slots) v = v->next;
  uint32_t g = v ? v->g : rc32_alloc(4 * (uint32_t)n);
  if (!v && g && (v = malloc(sizeof *v))) {
    for (int k = 0; k < n; k++)
      w32_wr32(g + 4 * k, cell_new(slots[k].name, conv, slots[k].words, slots[k].sret, slots[k].fn, dflt));
    *v = (vt_cache){slots, g, vts};
    vts = v;
  } else if (!v) {
    g = 0;
  }
  pthread_mutex_unlock(&vt_mu);
  return g;
}

uint32_t w32_object(uint32_t vtable, uint32_t size) {
  uint32_t g = rc32_alloc(size < 4 ? 4 : size);
  if (g) w32_wr32(g, vtable);
  return g;
}

// --- native -> guest -----------------------------------------------------------------------
uint32_t w32_guest_call(rc_ctx *c, uint32_t fn, int conv, uint32_t self, const uint32_t *args,
                        int nargs, uint32_t *hi) {
  uint64_t r[8];
  memcpy(r, c->r, sizeof r);
  uint32_t ret_to = c->ret_to, lo;
  // a host function's cell (an interface the seam made, called back through its vtable): run it
  // on a frame as the guest's call would leave one
  uint32_t esp = (uint32_t)c->r[4], sp = esp - 4 * (uint32_t)(nargs + 1);
  memcpy(RC32_H(sp + 4), args, 4 * (size_t)nargs);
  w32_wr32(sp, 0);
  c->r[4] = sp;
  if (conv == W32_THISCALL) c->r[1] = self;
  if (w32_hostfn_call(c, fn)) {
    lo = (uint32_t)c->r[0];
    if (hi) *hi = (uint32_t)c->r[2];
  } else {
    c->r[4] = esp;
    lo = rc32_call(c, fn, args, nargs, conv == W32_THISCALL ? self : (uint32_t)c->r[1], (uint32_t)c->r[2], hi);
  }
  memcpy(c->r, r, sizeof r);
  c->ret_to = ret_to;
  return lo;
}

// --- freeable guest memory -----------------------------------------------------------------
// Classes 2^4 .. 2^31; a freed block goes on its class's list (its first word links it).
static uint32_t gfree_head[32];
static pthread_mutex_t gmem_mu = PTHREAD_MUTEX_INITIALIZER;

static int gclass(uint32_t size) {
  int k = 4;
  while (k < 31 && (1u << k) < size) k++;
  return k;
}

uint32_t w32_galloc(uint32_t size) {
  if (!size) size = 1;
  if (size > 0x80000000u) return 0;
  int k = gclass(size);
  pthread_mutex_lock(&gmem_mu);
  uint32_t g = gfree_head[k];
  if (g) gfree_head[k] = w32_rd32(g);
  pthread_mutex_unlock(&gmem_mu);
  if (g) {
    memset(RC32_H(g), 0, (size_t)1 << k);
    return g;
  }
  return rc32_alloc(1u << k);
}

void w32_gfree(uint32_t g, uint32_t size) {
  if (!g) return;
  int k = gclass(size ? size : 1);
  pthread_mutex_lock(&gmem_mu);
  w32_wr32(g, gfree_head[k]);
  gfree_head[k] = g;
  pthread_mutex_unlock(&gmem_mu);
}

// --- interned strings ----------------------------------------------------------------------
typedef struct gstr_ent {
  char *s;
  uint32_t g;
  struct gstr_ent *next;
} gstr_ent;
static gstr_ent *gstr_tab[256];
static pthread_mutex_t gstr_mu = PTHREAD_MUTEX_INITIALIZER;

uint32_t w32_gstr(const char *s) {
  if (!s) return 0;
  uint32_t h = 2166136261u;
  for (const char *p = s; *p; p++) h = (h ^ (uint8_t)*p) * 16777619u;
  pthread_mutex_lock(&gstr_mu);
  gstr_ent *e;
  for (e = gstr_tab[h & 255]; e; e = e->next)
    if (!strcmp(e->s, s)) break;
  if (!e) {
    size_t n = strlen(s) + 1;
    uint32_t g = rc32_alloc((uint32_t)n);
    if (g && (e = malloc(sizeof *e))) {
      memcpy(RC32_H(g), s, n);
      *e = (gstr_ent){strdup(s), g, gstr_tab[h & 255]};
      gstr_tab[h & 255] = e;
    }
  }
  uint32_t g = e ? e->g : 0;
  pthread_mutex_unlock(&gstr_mu);
  return g;
}
