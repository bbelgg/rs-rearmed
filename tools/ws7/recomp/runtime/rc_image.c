// Recompiler runtime: map the original image's bytes into a native arm64
// process (data only: __TEXT is never executed), rebase it, bind its data
// imports natively, and dispatch indirect calls to translated functions.
//
// Differences from shims/game/harness/rsimage.c (x86_64 under Rosetta):
//   * arm64 pages are 16 KB and the image's __DATA (0x1017e7000) is only
//     4 KB aligned, so the whole image is one RW allocation at any address;
//   * the slide is whatever the kernel gives us; translated code adds rc_slide.
#include <dlfcn.h>
#include <fcntl.h>
#include <pthread.h>
#include <mach-o/loader.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "rc_rt.h"

uint64_t rc_slide;
rc_image_stats rc_stats;

typedef struct {
  uint64_t vmaddr;
  rc_fn fn;
} rc_entry;
extern const rc_entry rc_table[];
extern const int rc_table_len;

// Optional hook run before rc_fail aborts (e.g. a tracer dumping its entry ring).
void (*rc_fail_hook)(const char *why);

_Noreturn void rc_fail(const char *what, uint64_t addr) {
  fprintf(stderr, "[rc] %s at 0x%llx\n", what, (unsigned long long)addr);
  if (rc_fail_hook) rc_fail_hook(what);
  abort();
}

// Guest address -> translated function. Every indirect call and vtable dispatch goes through here.
// Two read-only indexes over the sorted rc_table, built by rc_dispatch_init at image load (before any
// guest code runs, so no locking or fences):
//  * rc_disp (rc.h): direct-mapped, one 32-bit entry per 16 bytes of guest code, read inline by
//    rc_icall at every call site. An entry holds the low 4 bits of the one function starting in its
//    16 bytes (tag 16 | a & 15 in bits 27..31) and that function's host address (bits 0..26, in
//    4-byte units from rc_disp.fbase); 0 if no function, or more than one, starts there (110 of 62622
//    in the game) or the host address does not fit;
//  * an open-addressing hash with every function, for those and for lookups before the image is loaded.
rc_disp_t rc_disp;

static struct ht_ent {
  uint64_t vmaddr;
  rc_fn fn;
} *ht;
static uint64_t ht_mask;
static pthread_once_t ht_once = PTHREAD_ONCE_INIT;

static inline uint64_t ht_hash(uint64_t a) { return (a * 0x9E3779B97F4A7C15ull) >> 20; }

static void ht_build(void) {
  uint64_t n = 1;
  while (n < 2ull * (uint64_t)rc_table_len) n <<= 1;
  struct ht_ent *t = calloc(n, sizeof *t);
  if (!t) rc_fail("out of memory", 0);
  for (int i = 0; i < rc_table_len; i++) {
    uint64_t h = ht_hash(rc_table[i].vmaddr) & (n - 1);
    while (t[h].fn) h = (h + 1) & (n - 1);
    t[h].vmaddr = rc_table[i].vmaddr;
    t[h].fn = rc_table[i].fn;
  }
  ht_mask = n - 1;
  __atomic_store_n(&ht, t, __ATOMIC_RELEASE);  // other threads read ht_mask and the entries after seeing ht
}

static rc_fn ht_lookup(uint64_t a) {
  struct ht_ent *t = __atomic_load_n(&ht, __ATOMIC_ACQUIRE);
  if (__builtin_expect(!t, 0)) {
    pthread_once(&ht_once, ht_build);
    t = ht;
  }
  for (uint64_t h = ht_hash(a) & ht_mask;; h = (h + 1) & ht_mask) {
    if (t[h].vmaddr == a) return t[h].fn;
    if (!t[h].fn) return 0;
  }
}

rc_fn rc_lookup(uint64_t host) {
  rc_fn f = rc_lookup_fast(host);
  return f ? f : ht_lookup(host - rc_slide);
}

// After rc_slide is known. rc_table is sorted by address.
static void rc_dispatch_init(void) {
  pthread_once(&ht_once, ht_build);
  if (rc_table_len <= 0) return;
  uint64_t lo = rc_table[0].vmaddr & ~15ull, hi = rc_table[rc_table_len - 1].vmaddr;
  uint64_t n = ((hi - lo) >> 4) + 1, fbase = ~0ull;
  for (int i = 0; i < rc_table_len; i++)
    if ((uint64_t)rc_table[i].fn < fbase) fbase = (uint64_t)rc_table[i].fn;
  uint32_t *tab = calloc(n, sizeof *tab);
  if (!tab) rc_fail("out of memory", 0);
  for (int i = 0; i < rc_table_len; i++) {
    uint64_t a = rc_table[i].vmaddr, off = (uint64_t)rc_table[i].fn - fbase, k = (a - lo) >> 4;
    int dup = (i > 0 && (rc_table[i - 1].vmaddr - lo) >> 4 == k) ||
              (i + 1 < rc_table_len && (rc_table[i + 1].vmaddr - lo) >> 4 == k);
    if (dup || (off & 3) || (off >> 2) >= (1u << 27)) continue;  // left to the hash
    tab[k] = (uint32_t)(16 | (a & 15)) << 27 | (uint32_t)(off >> 2);
  }
  rc_disp.tab = tab;
  rc_disp.fbase = fbase;
  rc_disp.lo = rc_slide + lo;
  rc_disp.n = n;
}

uint64_t rc_img_lo, rc_img_hi;  // host range of the mapped image

// Default rc_icall_native (integer/pointer arguments only), used until the
// real one (runtime/rc_native.c) is linked: a strong definition overrides it.
__attribute__((weak)) void rc_icall_native(rc_ctx *c, uint64_t host) {
  typedef uint64_t (*n6)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
  c->r[0] = ((n6)host)(c->r[7], c->r[6], c->r[2], c->r[1], c->r[8], c->r[9]);
  c->r[4] += 8;  // the callee's ret pops the return address the caller pushed
}

// rc_icall when the inline lookup missed: a native target, a function rc_disp does not hold, or none.
void rc_icall_slow(rc_ctx *c, uint64_t host) {
  if (host < rc_img_lo || host >= rc_img_hi) {
    // a native function: an import stored as a pointer, or a host callback
    rc_icall_native(c, host);
    return;
  }
  rc_fn f = ht_lookup(host - rc_slide);
  if (!f) rc_fail("indirect call to untranslated code", host - rc_slide);
  f(c);
}

// Bound values of __la_symbol_ptr (rc.h, rc_la_bound).
uint64_t rc_la_bound[RC_LA_MAX];

static uint64_t uleb(const uint8_t **p) {
  uint64_t r = 0;
  int s = 0;
  uint8_t b;
  do {
    b = *(*p)++;
    r |= (uint64_t)(b & 0x7f) << s;
    s += 7;
  } while (b & 0x80);
  return r;
}
static int64_t sleb(const uint8_t **p) {
  int64_t r = 0;
  int s = 0;
  uint8_t b;
  do {
    b = *(*p)++;
    r |= (int64_t)(b & 0x7f) << s;
    s += 7;
  } while (b & 0x80);
  if (s < 64 && (b & 0x40)) r |= -((int64_t)1 << s);
  return r;
}

// --- symbol resolution ---------------------------------------------------
static const uint8_t *exp_trie;
static size_t exp_size;
static uint64_t text_vmaddr;
static const char *dylibs[64];  // dylibs[ordinal - 1]: load command path
static void *dl_handle[64];
static int ndylibs;

// Export trie lookup; returns the offset from the mach header, or -1.
static int64_t export_find(const char *name) {
  if (!exp_trie || !exp_size) return -1;
  const uint8_t *n = exp_trie;
  const char *s = name;
  for (;;) {
    const uint8_t *p = n;
    uint64_t tsize = uleb(&p);
    if (!*s && tsize) {
      uint64_t flags = uleb(&p);
      if (flags & (EXPORT_SYMBOL_FLAGS_REEXPORT | EXPORT_SYMBOL_FLAGS_STUB_AND_RESOLVER)) return -1;
      if ((flags & EXPORT_SYMBOL_FLAGS_KIND_MASK) != EXPORT_SYMBOL_FLAGS_KIND_REGULAR) return -1;
      return (int64_t)uleb(&p);
    }
    p += tsize;
    int nc = *p++, hit = 0;
    for (int i = 0; i < nc; i++) {
      const char *edge = (const char *)p;
      size_t el = strlen(edge);
      p += el + 1;
      uint64_t child = uleb(&p);
      if (el && !strncmp(s, edge, el)) {
        s += el;
        n = exp_trie + child;
        hit = 1;
        break;
      }
    }
    if (!hit) return -1;
  }
}

static const char **unres;
static int nunres, capunres;
static int log_unresolved;

static void note_unresolved(const char *name, int64_t ord, int mode) {
  for (int i = 0; i < nunres; i++)
    if (!strcmp(unres[i], name)) return;
  if (nunres == capunres) unres = realloc(unres, sizeof *unres * (capunres = capunres ? capunres * 2 : 256));
  unres[nunres++] = strdup(name);
  rc_stats.unresolved_syms++;
  if (log_unresolved)
    fprintf(stderr, "[rc] unresolved %s bind: %s  (ordinal %lld: %s)\n", mode == 2 ? "weak" : mode == 1 ? "lazy" : "regular",
            name, (long long)ord, ord > 0 && ord <= ndylibs ? dylibs[ord - 1] : ord == 0 ? "self" : ord == -1 ? "main executable" : "flat lookup");
}

static void *handle_for(int64_t ord) {
  if (ord < 1 || ord > ndylibs) return RTLD_DEFAULT;
  if (!dl_handle[ord - 1]) {
    const char *p = dylibs[ord - 1];
    void *h = 0;
    // OpenGL goes to libRSOpenGL (shims/opengl) when the app links it: it wraps every GL/CGL
    // function the game imports, picks the backend and re-exports the rest of the framework
    if (!strcmp(p, "/System/Library/Frameworks/OpenGL.framework/Versions/A/OpenGL"))
      h = dlopen("@executable_path/libRSOpenGL.dylib", RTLD_LAZY | RTLD_NOLOAD);
    // only system libraries are opened; the game's own @executable_path shims are not
    if (!h && (!strncmp(p, "/System/", 8) || !strncmp(p, "/usr/lib/", 9))) h = dlopen(p, RTLD_LAZY | RTLD_LOCAL);
    dl_handle[ord - 1] = h ? h : RTLD_DEFAULT;
  }
  return dl_handle[ord - 1];
}

// mode: 0 regular, 1 lazy, 2 weak. Returns 1 and sets *v if resolved.
static int resolve(const char *name, int64_t ord, int mode, uint64_t *v) {
  if (ord <= 0 || mode == 2) {  // self, main executable, flat lookup, weak: own exports first
    int64_t off = export_find(name);
    if (off >= 0) {
      *v = rc_slide + text_vmaddr + off;
      rc_stats.bound_self++;
      return 1;
    }
  }
  const char *sym = name[0] == '_' ? name + 1 : name;
  void *p = 0;
  if (ord > 0) p = dlsym(handle_for(ord), sym);
  if (!p) p = dlsym(RTLD_DEFAULT, sym);
  if (!p && strchr(sym, '$') && strncmp(sym, "OBJC_", 5)) {
    // x86 symbol-version variants (stat$INODE64, select$1050, ...): arm64 has only the plain name
    char plain[256];
    strlcpy(plain, sym, sizeof plain);
    *strchr(plain, '$') = 0;
    p = dlsym(RTLD_DEFAULT, plain);
  }
  if (p) {
    *v = (uint64_t)p;
    rc_stats.bound_dl++;
    return 1;
  }
  rc_stats.unresolved++;
  note_unresolved(name, ord, mode);
  return 0;
}

static void binds(const uint8_t *p, const uint8_t *end, const uint64_t *segs, int mode) {
  const char *name = 0;
  uint64_t addr = 0;
  int64_t addend = 0, ord = 0;
  uint64_t *cnt = mode == 2 ? &rc_stats.weak_binds : mode == 1 ? &rc_stats.lazy_binds : &rc_stats.binds;
  // weak binds that find nothing keep the (rebased) address already in the slot
#define DOBIND(a) do { uint64_t v_; if (resolve(name, ord, mode, &v_)) *(uint64_t *)(a) = v_ + addend; \
    else if (mode != 2) *(uint64_t *)(a) = 0; (*cnt)++; } while (0)
  while (p < end) {
    uint8_t op = *p & BIND_OPCODE_MASK, imm = *p & BIND_IMMEDIATE_MASK;
    p++;
    switch (op) {
      case BIND_OPCODE_DONE: if (mode != 1) return; break;
      case BIND_OPCODE_SET_DYLIB_ORDINAL_IMM: ord = imm; break;
      case BIND_OPCODE_SET_DYLIB_ORDINAL_ULEB: ord = (int64_t)uleb(&p); break;
      case BIND_OPCODE_SET_DYLIB_SPECIAL_IMM: ord = imm ? (int64_t)(int8_t)(BIND_OPCODE_MASK | imm) : 0; break;
      case BIND_OPCODE_SET_SYMBOL_TRAILING_FLAGS_IMM: name = (const char *)p; p += strlen(name) + 1; break;
      case BIND_OPCODE_SET_ADDEND_SLEB: addend = sleb(&p); break;
      case BIND_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB: addr = segs[imm] + uleb(&p); break;
      case BIND_OPCODE_ADD_ADDR_ULEB: addr += uleb(&p); break;
      case BIND_OPCODE_DO_BIND: DOBIND(addr); addr += 8; break;
      case BIND_OPCODE_DO_BIND_ADD_ADDR_ULEB: DOBIND(addr); addr += 8 + uleb(&p); break;
      case BIND_OPCODE_DO_BIND_ADD_ADDR_IMM_SCALED: DOBIND(addr); addr += 8 + imm * 8ull; break;
      case BIND_OPCODE_DO_BIND_ULEB_TIMES_SKIPPING_ULEB: {
        uint64_t n = uleb(&p), skip = uleb(&p);
        for (uint64_t i = 0; i < n; i++, addr += 8 + skip) DOBIND(addr);
        break;
      }
      default: break;  // type
    }
  }
#undef DOBIND
}

static void rebases(const uint8_t *p, const uint8_t *end, const uint64_t *segs) {
  uint64_t addr = 0;
  while (p < end) {
    uint8_t op = *p & REBASE_OPCODE_MASK, imm = *p & REBASE_IMMEDIATE_MASK;
    p++;
    switch (op) {
      case REBASE_OPCODE_DONE: return;
      case REBASE_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB: addr = segs[imm] + uleb(&p); break;
      case REBASE_OPCODE_ADD_ADDR_ULEB: addr += uleb(&p); break;
      case REBASE_OPCODE_ADD_ADDR_IMM_SCALED: addr += imm * 8ull; break;
      case REBASE_OPCODE_DO_REBASE_IMM_TIMES:
        for (int i = 0; i < imm; i++, addr += 8) *(uint64_t *)addr += rc_slide;
        break;
      case REBASE_OPCODE_DO_REBASE_ULEB_TIMES:
        for (uint64_t i = 0, n = uleb(&p); i < n; i++, addr += 8) *(uint64_t *)addr += rc_slide;
        break;
      case REBASE_OPCODE_DO_REBASE_ADD_ADDR_ULEB: *(uint64_t *)addr += rc_slide; addr += 8 + uleb(&p); break;
      case REBASE_OPCODE_DO_REBASE_ULEB_TIMES_SKIPPING_ULEB: {
        uint64_t n = uleb(&p), skip = uleb(&p);
        for (uint64_t i = 0; i < n; i++, addr += 8 + skip) *(uint64_t *)addr += rc_slide;
        break;
      }
      default: break;
    }
  }
}

// section names are 16 bytes, not necessarily NUL-terminated
#define SECT_IS(sc, n) (!strncmp((sc)->sectname, n, 16))
static const uint64_t *init_list, *term_list;
static int ninit, nterm;

int rc_image_inits(const uint64_t **list) { *list = init_list; return ninit; }
int rc_image_terms(const uint64_t **list) { *list = term_list; return nterm; }

// ObjC and static-initializer fix-ups over the mapped sections (after rebase and bind).
static void fixup_section(const struct section_64 *sc) {
  uint64_t *e = (uint64_t *)(sc->addr + rc_slide);
  size_t n = sc->size / 8;
  uint32_t type = sc->flags & SECTION_TYPE;
  if (type == S_MOD_INIT_FUNC_POINTERS) { init_list = e; ninit = (int)n; rc_stats.mod_inits = n; return; }
  if (type == S_MOD_TERM_FUNC_POINTERS) { term_list = e; nterm = (int)n; rc_stats.mod_terms = n; return; }
  if (SECT_IS(sc, "__la_symbol_ptr")) {
    // what rc_import_thunk maps to the slot's thunk: a bound native address (rc.h, rc_la_bound)
    for (size_t i = 0; i < RC_LA_MAX; i++)
      rc_la_bound[i] = i < n && e[i] && !rc_in_image(e[i]) ? e[i] : ~0ull;
  } else if (SECT_IS(sc, "__objc_selrefs")) {
    // each entry points at the selector's C string (SEL is a pointer to a uniqued string)
    typedef const void *(*regfn)(const char *);
    regfn reg = (regfn)dlsym(RTLD_DEFAULT, "sel_registerName");
    if (!reg) rc_fail("sel_registerName not found (libobjc not loaded)", 0);
    for (size_t i = 0; i < n; i++, rc_stats.selrefs++) e[i] = (uint64_t)reg((const char *)e[i]);
  } else if (SECT_IS(sc, "__objc_classrefs") || SECT_IS(sc, "__objc_superrefs")) {
    int sup = sc->sectname[7] == 's';
    uint64_t *tot = sup ? &rc_stats.superrefs : &rc_stats.classrefs;
    uint64_t *in = sup ? &rc_stats.superrefs_internal : &rc_stats.classrefs_internal;
    uint64_t *ex = sup ? &rc_stats.superrefs_external : &rc_stats.classrefs_external;
    uint64_t *nl = sup ? &rc_stats.superrefs_null : &rc_stats.classrefs_null;
    for (size_t i = 0; i < n; i++, (*tot)++) {
      if (!e[i]) (*nl)++;
      else if (rc_in_image(e[i])) (*in)++;
      else (*ex)++;
    }
  } else if (SECT_IS(sc, "__cfstring")) {
    // { isa (bound to ___CFConstantStringClassReference), flags, chars, length }
    for (size_t i = 0; i + 4 <= n; i += 4, rc_stats.cfstrings++)
      if (e[i]) rc_stats.cfstring_isa_bound++;
  }
}

int rc_image_load(const char *path) {
  const char *lg = getenv("RC_LOG_UNRESOLVED");
  log_unresolved = lg && *lg && *lg != '0';
  int fd = open(path, O_RDONLY);
  if (fd < 0) return -1;
  struct stat st;
  fstat(fd, &st);
  uint8_t *file = mmap(0, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
  close(fd);
  const struct mach_header_64 *mh = (const void *)file;
  if (mh->magic != MH_MAGIC_64 || mh->cputype != CPU_TYPE_X86_64) return -1;
  uint64_t lo = ~0ull, hi = 0;
  const struct dyld_info_command *dyld = 0;
  const uint8_t *lc = file + sizeof *mh;
  for (uint32_t i = 0; i < mh->ncmds; i++, lc += ((const struct load_command *)lc)->cmdsize) {
    const struct load_command *c = (const void *)lc;
    if (c->cmd == LC_DYLD_INFO_ONLY || c->cmd == LC_DYLD_INFO) dyld = (const void *)c;
    if (c->cmd == LC_LOAD_DYLIB || c->cmd == LC_LOAD_WEAK_DYLIB || c->cmd == LC_REEXPORT_DYLIB ||
        c->cmd == LC_LOAD_UPWARD_DYLIB) {
      const struct dylib_command *d = (const void *)c;
      if (ndylibs < 64) dylibs[ndylibs++] = (const char *)d + d->dylib.name.offset;
    }
    if (c->cmd != LC_SEGMENT_64) continue;
    const struct segment_command_64 *sg = (const void *)c;
    if (!sg->vmsize || !strcmp(sg->segname, SEG_PAGEZERO)) continue;
    if (sg->vmaddr < lo) lo = sg->vmaddr;
    if (sg->vmaddr + sg->vmsize > hi) hi = sg->vmaddr + sg->vmsize;
  }
  if (!dyld) return -1;
  uint8_t *base = mmap(0, hi - lo, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
  if (base == MAP_FAILED) return -1;
  rc_slide = (uint64_t)base - lo;
  rc_img_lo = (uint64_t)base;
  rc_img_hi = (uint64_t)base + (hi - lo);
  uint64_t segs[16] = {0};
  int nsegs = 0;
  lc = file + sizeof *mh;
  for (uint32_t i = 0; i < mh->ncmds; i++, lc += ((const struct load_command *)lc)->cmdsize) {
    const struct load_command *c = (const void *)lc;
    if (c->cmd != LC_SEGMENT_64) continue;
    const struct segment_command_64 *sg = (const void *)c;
    if (nsegs < 16) segs[nsegs++] = sg->vmaddr + rc_slide;
    if (!sg->vmsize || !strcmp(sg->segname, SEG_PAGEZERO)) continue;
    if (sg->fileoff == 0 && sg->filesize) text_vmaddr = sg->vmaddr;
    memcpy((void *)(sg->vmaddr + rc_slide), file + sg->fileoff, sg->filesize);
  }
  // keep the export trie (needed for self/weak binds) beyond the unmap
  if (dyld->export_size) {
    uint8_t *t = malloc(dyld->export_size);
    memcpy(t, file + dyld->export_off, dyld->export_size);
    exp_trie = t;
    exp_size = dyld->export_size;
  }
  // dylib names point into the file mapping: copy them
  for (int i = 0; i < ndylibs; i++) dylibs[i] = strdup(dylibs[i]);
  rebases(file + dyld->rebase_off, file + dyld->rebase_off + dyld->rebase_size, segs);
  binds(file + dyld->bind_off, file + dyld->bind_off + dyld->bind_size, segs, 0);
  binds(file + dyld->lazy_bind_off, file + dyld->lazy_bind_off + dyld->lazy_bind_size, segs, 1);
  if (dyld->weak_bind_size)
    binds(file + dyld->weak_bind_off, file + dyld->weak_bind_off + dyld->weak_bind_size, segs, 2);
  lc = file + sizeof *mh;
  for (uint32_t i = 0; i < mh->ncmds; i++, lc += ((const struct load_command *)lc)->cmdsize) {
    const struct load_command *c = (const void *)lc;
    if (c->cmd != LC_SEGMENT_64) continue;
    const struct segment_command_64 *sg = (const void *)c;
    const struct section_64 *sc = (const void *)(sg + 1);
    for (uint32_t k = 0; k < sg->nsects; k++) fixup_section(&sc[k]);
  }
  rc_dispatch_init();
  if (log_unresolved)
    fprintf(stderr, "[rc] image: %llu binds, %llu lazy, %llu weak; %llu self, %llu dlsym, %llu unresolved slots (%llu symbols); "
            "%llu selrefs, %llu classrefs (%llu internal, %llu external, %llu null), %llu superrefs, %llu cfstrings, %llu inits\n",
            (unsigned long long)rc_stats.binds, (unsigned long long)rc_stats.lazy_binds, (unsigned long long)rc_stats.weak_binds,
            (unsigned long long)rc_stats.bound_self, (unsigned long long)rc_stats.bound_dl, (unsigned long long)rc_stats.unresolved,
            (unsigned long long)rc_stats.unresolved_syms, (unsigned long long)rc_stats.selrefs, (unsigned long long)rc_stats.classrefs,
            (unsigned long long)rc_stats.classrefs_internal, (unsigned long long)rc_stats.classrefs_external,
            (unsigned long long)rc_stats.classrefs_null, (unsigned long long)rc_stats.superrefs,
            (unsigned long long)rc_stats.cfstrings, (unsigned long long)rc_stats.mod_inits);
  munmap(file, st.st_size);
  return 0;
}
