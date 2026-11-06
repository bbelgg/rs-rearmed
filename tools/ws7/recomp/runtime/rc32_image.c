// 32-bit guests: the loaded images (the game and the DLLs translated with it), mapping each PE
// into the window, binding the imports between them, DLL start-up and shutdown, and dispatch of
// indirect calls (rc32.h).
#include <mach/mach_time.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>

#include "rc32.h"

#define MAX_IMAGES 16
#define FWD_RET 0xfffffff4u  // return address of a call forwarded to another image (never run)

struct rc32_image {
  rc32_module *mod;
  char dll[64];          // lower case, from the export directory ("msvcr100.dll"); the file name else
  uint32_t base, size, entry;
  char alias[64];        // another name the image answers to ("msvcrt.dll"), "" for none
  int is_dll, started, deferred;
  uint32_t exp_dir, exp_size;  // the export directory (guest address, size), 0 when none
  uint32_t imp_area, imp_count;
  rc_fn *fwd;            // per import: the translated export it is bound to (a direct call)
  uint8_t *bound;        // per import: bound to another image's export
  int novr;              // overrides of its exports (rc32_override_add) bound at its addresses
};

static rc32_image images[MAX_IMAGES];
static int nimages;
uint32_t rc32_imp_area;
void (*rc32_host_import)(rc_ctx *c, const rc32_module *m, int k);
static rc32_tls_image tls[MAX_IMAGES];
static int ntls;

// Native overrides of exports (rc32_override_add): by DLL and export name; resolved to guest
// addresses by rc32_images_bind, sorted by address there (ovr_at).
#define MAX_OVERRIDES 128
static struct { char dll[64]; const char *name; rc_fn fn; uint32_t addr; } ovr[MAX_OVERRIDES];
static int novr;

static uint32_t rd32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static uint16_t rd16(const uint8_t *p) { uint16_t v; memcpy(&v, p, 2); return v; }
static uint32_t g32(uint32_t g) { return rd32(rc_mem + g); }

int rc32_tls_images(const rc32_tls_image **out) {
  *out = tls;
  return ntls;
}

rc32_image *rc32_image_add(rc32_module *m, const char *path) {
  if (rc32_mem_init()) return 0;
  if (nimages == MAX_IMAGES) { fprintf(stderr, "rc32: too many images\n"); return 0; }
  FILE *f = fopen(path, "rb");
  if (!f) { perror(path); return 0; }
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  uint8_t *d = malloc((size_t)n);
  if (!d || fread(d, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(d); return 0; }
  fclose(f);
  rc32_image *im = 0;
  if (n < 0x40 || d[0] != 'M' || d[1] != 'Z') goto out;
  uint32_t o = rd32(d + 0x3c);
  if ((long)o + 0xf8 > n || memcmp(d + o, "PE\0\0", 4) || rd16(d + o + 4) != 0x14c) goto out;
  uint16_t nsect = rd16(d + o + 6), optsz = rd16(d + o + 20), chars = rd16(d + o + 22);
  const uint8_t *opt = d + o + 24;
  uint32_t base = rd32(opt + 28), size = rd32(opt + 56), hdrs = rd32(opt + 60), ndirs = rd32(opt + 92);
  if (rc32_mem_reserve(base, size)) {
    fprintf(stderr, "rc32: %s at 0x%x..0x%x overlaps another image or guest memory in use\n", path, base, base + size);
    goto out;
  }
  if (rc32_mem_commit(base, size)) { fprintf(stderr, "rc32: cannot map %s at 0x%x\n", path, base); goto out; }
  memcpy(rc_mem + base, d, hdrs < (uint32_t)n ? hdrs : (uint32_t)n);
  const uint8_t *s = opt + optsz;
  for (int k = 0; k < nsect; k++, s += 40) {
    uint32_t vs = rd32(s + 8), va = rd32(s + 12), rs = rd32(s + 16), ro = rd32(s + 20);
    uint32_t cp = rs < vs || !vs ? rs : vs;
    if ((uint64_t)ro + cp > (uint64_t)n) goto out;
    memcpy(rc_mem + base + va, d + ro, cp);
  }
  im = &images[nimages];
  memset(im, 0, sizeof *im);
  im->mod = m;
  im->base = base;
  im->size = size;
  im->is_dll = (chars & 0x2000) != 0;  // IMAGE_FILE_DLL
  im->entry = rd32(opt + 16) ? base + rd32(opt + 16) : 0;
  if (!im->is_dll) {
    rc32_stack_default(rd32(opt + 72));  // SizeOfStackReserve: what Windows gives threads
    rc_img_lo = (uint64_t)(uintptr_t)(rc_mem + base);
    rc_img_hi = rc_img_lo + size;
  }
  if (ndirs > 0 && rd32(opt + 96)) {
    im->exp_dir = base + rd32(opt + 96);
    im->exp_size = rd32(opt + 100);
  }
  // the DLL name: the export directory's, else the file's
  const char *nm = im->exp_dir && g32(im->exp_dir + 12) ? (const char *)rc_mem + base + g32(im->exp_dir + 12) : 0;
  if (!nm) {
    nm = strrchr(path, '/');
    nm = nm ? nm + 1 : path;
  }
  size_t k = 0;
  for (; nm[k] && k < sizeof im->dll - 1; k++) im->dll[k] = (char)(nm[k] >= 'A' && nm[k] <= 'Z' ? nm[k] | 0x20 : nm[k]);
  im->dll[k] = 0;
  if (ndirs > 9 && rd32(opt + 96 + 8 * 9)) {
    const uint8_t *t = rc_mem + base + rd32(opt + 96 + 8 * 9);
    tls[ntls] = (rc32_tls_image){rd32(t), rd32(t + 4), rd32(t + 16)};
    uint32_t index = (uint32_t)ntls;
    if (rd32(t + 8)) memcpy(rc_mem + rd32(t + 8), &index, 4);  // the image's TLS index
    ntls++;
  }
  im->imp_count = (uint32_t)*m->imports_len;
  m->image = im;
  nimages++;
out:
  free(d);
  return im;
}

static rc32_image *image_at(uint32_t g) {
  for (int k = 0; k < nimages; k++)
    if (g - images[k].base < images[k].size) return &images[k];
  return 0;
}

// s[0..n) names d: in full, or without d's extension ("msvcr100" for "msvcr100.dll")
static int names(const char *d, const char *s, size_t n) {
  return d[0] && !strncasecmp(d, s, n) && (!d[n] || (d[n] == '.' && !memchr(s, '.', n)));
}

static int name_match(const rc32_image *im, const char *s, size_t n) {
  return names(im->dll, s, n) || names(im->alias, s, n);
}

// the image a DLL name stands for: a path's directories are ignored ("C:\x\d3dx9_42.dll")
static rc32_image *image_named(const char *dll) {
  for (const char *p = dll; *p; p++)
    if (*p == '\\' || *p == '/') dll = p + 1;
  size_t n = strlen(dll);
  for (int k = 0; k < nimages; k++)
    if (name_match(&images[k], dll, n)) return &images[k];
  return 0;
}

int rc32_image_alias(const char *dll, const char *alias) {
  rc32_image *im = image_named(dll);
  if (!im || strlen(alias) >= sizeof im->alias) return -1;
  for (size_t k = 0; alias[k]; k++) im->alias[k] = (char)(alias[k] >= 'A' && alias[k] <= 'Z' ? alias[k] | 0x20 : alias[k]);
  return 0;
}

void rc32_image_defer(rc32_image *im) { im->deferred = 1; }

uint32_t rc32_image_base(const char *dll) {
  if (!dll) {
    for (int k = 0; k < nimages; k++)
      if (!images[k].mod->name[0]) return images[k].base;
    return 0;
  }
  rc32_image *im = image_named(dll);
  return im ? im->base : 0;
}

// the export `name` (or ordinal, when name < 0x10000 as a value) of im; forwarders followed
static uint32_t export_of(rc32_image *im, const char *name, int depth) {
  if (!im || !im->exp_dir || depth > 8) return 0;
  uint32_t ed = im->exp_dir, b = im->base;
  uint32_t obase = g32(ed + 16), nfun = g32(ed + 20), nname = g32(ed + 24);
  uint32_t afun = b + g32(ed + 28), aname = b + g32(ed + 32), aord = b + g32(ed + 36);
  uint32_t idx = 0xffffffffu;
  if ((uintptr_t)name < 0x10000) {
    idx = (uint32_t)(uintptr_t)name - obase;
  } else {
    int lo = 0, hi = (int)nname - 1;  // the name table is sorted
    while (lo <= hi) {
      int mid = (lo + hi) / 2;
      int c = strcmp((const char *)rc_mem + b + g32(aname + 4 * (uint32_t)mid), name);
      if (!c) { idx = rd16(rc_mem + aord + 2 * (uint32_t)mid); break; }
      if (c < 0) lo = mid + 1;
      else hi = mid - 1;
    }
  }
  if (idx >= nfun) return 0;
  uint32_t rva = g32(afun + 4 * idx);
  if (!rva) return 0;
  if (rva - (ed - b) < im->exp_size) {  // a forwarder: "DLL.name" or "DLL.#ordinal"
    char fw[256];
    snprintf(fw, sizeof fw, "%s", (const char *)rc_mem + b + rva);
    char *dot = strrchr(fw, '.');
    if (!dot) return 0;
    *dot = 0;
    rc32_image *to = image_named(fw);
    if (dot[1] == '#') return export_of(to, (const char *)(uintptr_t)strtoul(dot + 2, 0, 10), depth + 1);
    return export_of(to, dot + 1, depth + 1);
  }
  return b + rva;
}

uint32_t rc32_image_export(uint32_t base, const char *name) { return export_of(image_at(base), name, 0); }

// the export an import name ("msvcr100.dll!_iob", "msvcr100.dll!#12") stands for, or 0; the
// first few misses of an image are logged (*misses counts them)
static uint32_t resolve(const char *imp, rc32_image *self, int *misses) {
  const char *bang = strchr(imp, '!');
  if (!bang || (size_t)(bang - imp) >= 64) return 0;
  char dll[64];
  memcpy(dll, imp, (size_t)(bang - imp));
  dll[bang - imp] = 0;
  rc32_image *im = image_named(dll);
  if (!im || im == self) return 0;
  const char *name = bang + 1;
  if (name[0] == '#') name = (const char *)(uintptr_t)strtoul(name + 1, 0, 10);
  uint32_t a = export_of(im, name, 0);
  if (!a && (*misses)++ < 3) fprintf(stderr, "rc32: %s: %s does not export it\n", imp, im->dll);
  return a;
}

int rc32_override_add(const char *dll, const char *name, rc_fn fn) {
  if (strlen(dll) >= sizeof ovr[0].dll || !name || !fn) return -1;
  for (int k = 0; k < novr; k++)
    if (!strcasecmp(ovr[k].dll, dll) && !strcmp(ovr[k].name, name)) {
      ovr[k].fn = fn;  // the last one wins
      return 0;
    }
  if (novr == MAX_OVERRIDES) { fprintf(stderr, "rc32: too many overrides\n"); return -1; }
  snprintf(ovr[novr].dll, sizeof ovr[novr].dll, "%s", dll);
  ovr[novr].name = name;
  ovr[novr].fn = fn;
  ovr[novr].addr = 0;
  novr++;
  return 0;
}

// the override bound at guest address g of image im, or 0
static rc_fn ovr_at(const rc32_image *im, uint32_t g) {
  if (!im->novr) return 0;
  int lo = 0, hi = novr - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    uint32_t a = ovr[mid].addr;
    if (a == g) return ovr[mid].fn;
    if (a < g) lo = mid + 1;
    else hi = mid - 1;
  }
  return 0;
}

// resolve every override to its export's address (0: the DLL is not loaded, or has no such
// export) and sort them by address for ovr_at
static void overrides_bind(void) {
  for (int i = 0; i < nimages; i++) images[i].novr = 0;
  for (int k = 0; k < novr; k++) {
    rc32_image *im = image_named(ovr[k].dll);
    ovr[k].addr = im ? export_of(im, ovr[k].name, 0) : 0;
    rc32_image *at = ovr[k].addr ? image_at(ovr[k].addr) : 0;  // a forwarder may land elsewhere
    if (at) at->novr++;
    else if (im) fprintf(stderr, "rc32: override %s!%s: no such export\n", ovr[k].dll, ovr[k].name);
  }
  for (int a = 1; a < novr; a++)  // insertion sort: a few dozen entries, once
    for (int b = a; b > 0 && ovr[b - 1].addr > ovr[b].addr; b--) {
      __typeof__(ovr[0]) t = ovr[b];
      ovr[b] = ovr[b - 1];
      ovr[b - 1] = t;
    }
}

int rc32_images_bind(void) {
  overrides_bind();
  for (int i = 0; i < nimages; i++) {
    rc32_image *im = &images[i];
    const rc32_import *imps = im->mod->imports;
    uint32_t n = im->imp_count;
    if (!im->imp_area) {
      im->imp_area = rc32_alloc(4 * (n ? n : 1));
      im->fwd = calloc(n ? n : 1, sizeof *im->fwd);
      im->bound = calloc(n ? n : 1, 1);
      if (!im->imp_area || !im->fwd || !im->bound) return -1;
      memset(rc_mem + im->imp_area, 0xc3, 4 * n);  // `ret` bytes: never run
      if (!im->mod->name[0]) rc32_imp_area = im->imp_area;
    }
    int misses = 0;
    for (uint32_t k = 0; k < n; k++) {
      uint32_t a = resolve(imps[k].name, im, &misses), v = a ? a : im->imp_area + 4 * k;
      im->bound[k] = a != 0;
      __atomic_store_n(&im->fwd[k], a ? rc32_lookup(a) : 0, __ATOMIC_RELEASE);
      memcpy(rc_mem + imps[k].slot, &v, 4);
    }
    if (misses > 3)
      fprintf(stderr, "rc32: %s: %d more imports of loaded images not exported (left to the host)\n",
              im->mod->name[0] ? im->mod->name : "the main image", misses - 3);
  }
  return 0;
}

void rc32_import_thunk(rc_ctx *c, rc32_module *m, int k) {
  rc32_image *im = m->image;
  rc_fn f = im && im->fwd ? __atomic_load_n(&im->fwd[k], __ATOMIC_ACQUIRE) : 0;
  if (f) {
    // as the guest's `call [slot]` would: a return address (the thunk has none), the callee's
    // own `ret N` removes it and its arguments; ecx/edx and the x87 state pass through c
    uint32_t saved = c->ret_to, ra = FWD_RET;
    c->r[4] = (uint32_t)(c->r[4] - 4);
    memcpy(rc_mem + (uint32_t)c->r[4], &ra, 4);
    f(c);
    if (c->ret_to != FWD_RET) rc_fail("rc32: an export returned to another address than its caller", c->ret_to);
    c->ret_to = saved;  // a `jmp [slot]` site's own return address (RC32_RET_SAVE)
    return;
  }
  if (im && im->bound && im->bound[k]) {
    fprintf(stderr, "rc32: call of %s, an export with no translated function\n", m->imports[k].name);
    rc_fail("call of a data export", m->imports[k].slot);
  }
  if (m->host) {
    m->host(c, k);
  } else if (rc32_host_import) {
    rc32_host_import(c, m, k);
  } else {
    fprintf(stderr, "rc32: import not implemented: %s (%s)\n", m->imports[k].name, m->name);
    rc_fail("import not implemented", m->imports[k].slot);
  }
}

// --- DLL start-up and shutdown -----------------------------------------------------------------
static int order[MAX_IMAGES], norder;  // DLLs in the order they were started
static pthread_mutex_t start_mu = PTHREAD_RECURSIVE_MUTEX_INITIALIZER;  // fini may run from exit() inside a DllMain

// does a import from b?
static int imports_from(rc32_image *a, rc32_image *b) {
  for (uint32_t k = 0; k < a->imp_count; k++) {
    const char *nm = a->mod->imports[k].name, *bang = strchr(nm, '!');
    if (bang && name_match(b, nm, (size_t)(bang - nm))) return 1;
  }
  return 0;
}

static int start(rc_ctx *c, rc32_image *im, int depth) {
  if (im->started || !im->is_dll || depth > MAX_IMAGES) return 0;
  im->started = 1;  // also marks it while its dependencies start (a cycle stops here)
  for (int k = 0; k < nimages; k++)
    if (&images[k] != im && imports_from(im, &images[k]) && start(c, &images[k], depth + 1)) return -1;
  if (im->entry) {
    uint32_t a[3] = {im->base, 1, 0};  // DllMain(hinst, DLL_PROCESS_ATTACH, 0): stdcall
    uint32_t ok = rc32_call(c, im->entry, a, 3, 0, 0, 0);
    if (!ok) {
      fprintf(stderr, "rc32: DllMain of %s (PROCESS_ATTACH) returned FALSE\n", im->dll);
      return -1;
    }
  }
  order[norder++] = (int)(im - images);
  return 0;
}

int rc32_images_init(rc_ctx *c) {
  pthread_mutex_lock(&start_mu);
  static int once;
  if (!once) {
    once = 1;
    atexit(rc32_images_fini);
  }
  int rc = 0;
  for (int k = 0; k < nimages && !rc; k++)
    if (!images[k].deferred) rc = start(c, &images[k], 0);
  pthread_mutex_unlock(&start_mu);
  return rc;
}

int rc32_image_start(rc_ctx *c, uint32_t base) {
  rc32_image *im = image_at(base);
  if (!im) return -1;
  pthread_mutex_lock(&start_mu);
  int rc = start(c, im, 0);
  pthread_mutex_unlock(&start_mu);
  return rc;
}

void rc32_images_fini(void) {
  pthread_mutex_lock(&start_mu);
  int n = norder;
  norder = 0;
  pthread_mutex_unlock(&start_mu);
  if (!n) return;
  rc_ctx *c = rc32_thread_ctx();
  for (int k = n - 1; k >= 0; k--) {
    rc32_image *im = &images[order[k]];
    if (!im->entry) continue;
    // DLL_PROCESS_DETACH with lpReserved set, as at a process exit: the game's other threads are
    // still running here, and with 0 (FreeLibrary's form) the CRT frees its locks and TLS under them
    uint32_t a[3] = {im->base, 0, 1};
    rc32_call(c, im->entry, a, 3, 0, 0, 0);
  }
}

// --- dispatch ----------------------------------------------------------------------------------
static rc_fn table_lookup(const rc32_module *m, uint32_t g) {
  const rc32_entry *t = m->table;
  int lo = 0, hi = *m->table_len - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    uint32_t a = t[mid].addr;
    if (a == g) return t[mid].fn;
    if (a < g) lo = mid + 1;
    else hi = mid - 1;
  }
  return 0;
}

static rc_fn lookup_slow(uint32_t g);
void (*rc32_note)(int what, uint64_t ticks);
void (*rc32_icall_trace)(rc_ctx *c, uint32_t g);

// Indirect calls (vtables, callbacks) look the same few thousand addresses up all the time: a direct-mapped
// cache in front of the binary search, shared by all threads, an entry one 64-bit word: the guest address, and
// the function as an offset from this code (0x7fffffff: no translated function there, a host function's cell
// or an import cell: the COM objects of the seams are called through such cells many times a frame).
// Entries only ever hold what the search answered, and the answer for an address does not change once the
// images are bound, so a race costs nothing: a thread reads one whole word or another.
// The generated code reads it itself (RC32_ICALL, rc32.h). While rc32_icall_trace is set nothing is added, so every
// call reaches rc32_icall.
uint64_t rc32_lookup_cache[1u << RC32_LOOKUP_BITS];

rc_fn rc32_lookup(uint32_t g) {
  uint32_t s = (g * 2654435761u) >> (32 - RC32_LOOKUP_BITS);
  uint64_t e = __atomic_load_n(&rc32_lookup_cache[s], __ATOMIC_RELAXED);
  const char *base = (const char *)rc32_lookup;
  if ((uint32_t)(e >> 32) == g && e) {
    int32_t o = (int32_t)(uint32_t)e;
    return (uint32_t)o == RC32_LOOKUP_NONE ? 0 : (rc_fn)(uintptr_t)(base + o);
  }
  uint64_t t0 = rc32_note ? mach_absolute_time() : 0;
  rc_fn f = lookup_slow(g);
  if (t0) rc32_note(RC32_NOTE_LOOKUP_MISS, mach_absolute_time() - t0);
  int64_t d = f ? (const char *)(uintptr_t)f - base : RC32_LOOKUP_NONE;
  if (d >= INT32_MIN && d <= RC32_LOOKUP_NONE && !rc32_icall_trace)  // another library's may be too far off: not cached
    __atomic_store_n(&rc32_lookup_cache[s], (uint64_t)g << 32 | (uint32_t)(int32_t)d, __ATOMIC_RELAXED);
  return f;
}

static rc_fn lookup_slow(uint32_t g) {
  rc32_image *im = image_at(g);
  if (im) {
    rc_fn f = ovr_at(im, g);
    return f ? f : table_lookup(im->mod, g);
  }
  for (int k = 0; k < nimages; k++) {  // code outside every image's range (none expected)
    rc_fn f = table_lookup(images[k].mod, g);
    if (f) return f;
  }
  return 0;
}

static rc32_hostfn_hook hooks[8];
static int nhooks;
static pthread_mutex_t hook_mu = PTHREAD_MUTEX_INITIALIZER;

void rc32_hostfn_hook_add(rc32_hostfn_hook h) {
  pthread_mutex_lock(&hook_mu);
  int have = 0;
  for (int k = 0; k < nhooks; k++) have |= hooks[k] == h;
  if (!have && nhooks < 8) {
    hooks[nhooks] = h;
    __atomic_store_n(&nhooks, nhooks + 1, __ATOMIC_RELEASE);
  } else if (!have) {
    fprintf(stderr, "rc32: too many host function hooks\n");
  }
  pthread_mutex_unlock(&hook_mu);
}

// rc32_seh.c: a resume point of a live generated frame (does not return when it is one)
void rc32_resume_try(rc_ctx *c, uint32_t g);

void rc32_icall(rc_ctx *c, uint32_t g) {
  if (__builtin_expect(rc32_icall_trace != 0, 0)) rc32_icall_trace(c, g);
  rc_fn f = rc32_lookup(g);
  if (f) {
    f(c);
    return;
  }
  for (int i = 0; i < nimages; i++) {
    rc32_image *im = &images[i];
    uint32_t k = (g - im->imp_area) / 4;
    if (im->imp_area && g >= im->imp_area && k < im->imp_count && !(g & 3)) {
      memcpy(&c->ret_to, rc_mem + (uint32_t)c->r[4], 4);
      c->r[4] = (uint32_t)(c->r[4] + 4);  // the thunk wants the arguments at esp
      im->mod->imports[k].fn(c);
      return;
    }
  }
  int n = __atomic_load_n(&nhooks, __ATOMIC_ACQUIRE);
  for (int k = 0; k < n; k++)
    if (hooks[k](c, g)) return;
  rc32_resume_try(c, g);
  rc_fail("call of an address that is no translated function", g);
}

__attribute__((weak)) uint64_t rc32_rdtsc(void) { return mach_absolute_time(); }

// The 32-bit runtime's rc_fail (rc_image.c has the 64-bit one; a build links one of them). A
// test runner may define its own instead and leave this file's copy out with RC32_NO_FAIL.
#ifndef RC32_NO_FAIL
void (*rc_fail_hook)(const char *why);
_Noreturn void rc_fail(const char *what, uint64_t addr) {
  if (rc_fail_hook) rc_fail_hook(what);
  fprintf(stderr, "rc_fail: %s at 0x%llx\n", what, (unsigned long long)addr);
  abort();
}
#endif
