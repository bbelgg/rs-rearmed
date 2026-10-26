// Map the decrypted Rocksmith 2014 executable into this (x86_64) process at its
// preferred address so individual original functions can be called directly.
// See rsimage.h.
#include "rsimage.h"

#include <dlfcn.h>
#include <fcntl.h>
#include <mach-o/loader.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define TRAP_BASE 0x7e0000000000ULL  // unresolved imports point here (PROT_NONE)
#define TRAP_STRIDE 16
#define MAX_TRAPS 4096

uint64_t rsimage_slide = 0x300000000ULL;  // image lands at 0x400000000

static const char *g_trap_names[MAX_TRAPS];
static uint64_t *g_inits;  // __mod_init_func, slid, in image order
static uint8_t *g_init_done;
static int g_ninits;
static int g_ntraps;
static int g_resolved, g_unresolved;
static sigjmp_buf *g_trap_jmp;
static const char *g_trap_hit;

static const char *const k_libs[] = {
    "/usr/lib/libSystem.B.dylib",
    "/usr/lib/libstdc++.6.dylib",
    "/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation",
};

static uint64_t uleb(const uint8_t **p) {
  uint64_t r = 0;
  int shift = 0;
  uint8_t b;
  do {
    b = *(*p)++;
    r |= (uint64_t)(b & 0x7f) << shift;
    shift += 7;
  } while (b & 0x80);
  return r;
}

static int64_t sleb(const uint8_t **p) {
  int64_t r = 0;
  int shift = 0;
  uint8_t b;
  do {
    b = *(*p)++;
    r |= (int64_t)(b & 0x7f) << shift;
    shift += 7;
  } while (b & 0x80);
  if (shift < 64 && (b & 0x40)) r |= -((int64_t)1 << shift);
  return r;
}

static uint64_t trap_for(const char *name) {
  for (int i = 0; i < g_ntraps; i++)
    if (strcmp(g_trap_names[i], name) == 0) return TRAP_BASE + (uint64_t)i * TRAP_STRIDE;
  if (g_ntraps == MAX_TRAPS) return TRAP_BASE;
  g_trap_names[g_ntraps] = strdup(name);
  return TRAP_BASE + (uint64_t)g_ntraps++ * TRAP_STRIDE;
}

static uint64_t resolve(const char *name) {
  // Mach-O symbol names carry a leading underscore that dlsym does not want.
  void *p = dlsym(RTLD_DEFAULT, name[0] == '_' ? name + 1 : name);
  if (p) {
    g_resolved++;
    return (uint64_t)p;
  }
  g_unresolved++;
  return trap_for(name);
}

// Run dyld bind opcodes, writing resolved pointers. segs[] are segment vmaddrs.
static void run_binds(const uint8_t *p, const uint8_t *end, const uint64_t *segs, int nsegs,
                      int lazy) {
  const char *name = NULL;
  uint64_t addr = 0;
  int64_t addend = 0;
  while (p < end) {
    uint8_t op = *p & BIND_OPCODE_MASK, imm = *p & BIND_IMMEDIATE_MASK;
    p++;
    switch (op) {
      case BIND_OPCODE_DONE:
        if (!lazy) return;
        break;  // lazy binds are a sequence of DONE-terminated records
      case BIND_OPCODE_SET_DYLIB_ORDINAL_IMM:
      case BIND_OPCODE_SET_DYLIB_SPECIAL_IMM:
        break;
      case BIND_OPCODE_SET_DYLIB_ORDINAL_ULEB:
        uleb(&p);
        break;
      case BIND_OPCODE_SET_SYMBOL_TRAILING_FLAGS_IMM:
        name = (const char *)p;
        p += strlen(name) + 1;
        break;
      case BIND_OPCODE_SET_TYPE_IMM:
        break;
      case BIND_OPCODE_SET_ADDEND_SLEB:
        addend = sleb(&p);
        break;
      case BIND_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB:
        addr = (imm < nsegs ? segs[imm] : 0) + uleb(&p);
        break;
      case BIND_OPCODE_ADD_ADDR_ULEB:
        addr += uleb(&p);
        break;
      case BIND_OPCODE_DO_BIND:
        *(uint64_t *)addr = resolve(name) + addend;
        addr += 8;
        break;
      case BIND_OPCODE_DO_BIND_ADD_ADDR_ULEB:
        *(uint64_t *)addr = resolve(name) + addend;
        addr += 8 + uleb(&p);
        break;
      case BIND_OPCODE_DO_BIND_ADD_ADDR_IMM_SCALED:
        *(uint64_t *)addr = resolve(name) + addend;
        addr += 8 + (uint64_t)imm * 8;
        break;
      case BIND_OPCODE_DO_BIND_ULEB_TIMES_SKIPPING_ULEB: {
        uint64_t count = uleb(&p), skip = uleb(&p);
        uint64_t v = resolve(name) + addend;
        for (uint64_t i = 0; i < count; i++) {
          *(uint64_t *)addr = v;
          addr += 8 + skip;
        }
        break;
      }
      default:
        fprintf(stderr, "[rsimage] unknown bind opcode 0x%x\n", op);
        return;
    }
  }
}

// Add the slide to every pointer the rebase opcodes name. segs[] are slid vmaddrs.
static void run_rebases(const uint8_t *p, const uint8_t *end, const uint64_t *segs, int nsegs) {
  uint64_t addr = 0;
  while (p < end) {
    uint8_t op = *p & REBASE_OPCODE_MASK, imm = *p & REBASE_IMMEDIATE_MASK;
    p++;
    switch (op) {
      case REBASE_OPCODE_DONE:
        return;
      case REBASE_OPCODE_SET_TYPE_IMM:
        break;
      case REBASE_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB:
        addr = (imm < nsegs ? segs[imm] : 0) + uleb(&p);
        break;
      case REBASE_OPCODE_ADD_ADDR_ULEB:
        addr += uleb(&p);
        break;
      case REBASE_OPCODE_ADD_ADDR_IMM_SCALED:
        addr += (uint64_t)imm * 8;
        break;
      case REBASE_OPCODE_DO_REBASE_IMM_TIMES:
        for (int i = 0; i < imm; i++, addr += 8) *(uint64_t *)addr += rsimage_slide;
        break;
      case REBASE_OPCODE_DO_REBASE_ULEB_TIMES:
        for (uint64_t i = 0, n = uleb(&p); i < n; i++, addr += 8) *(uint64_t *)addr += rsimage_slide;
        break;
      case REBASE_OPCODE_DO_REBASE_ADD_ADDR_ULEB:
        *(uint64_t *)addr += rsimage_slide;
        addr += 8 + uleb(&p);
        break;
      case REBASE_OPCODE_DO_REBASE_ULEB_TIMES_SKIPPING_ULEB: {
        uint64_t n = uleb(&p), skip = uleb(&p);
        for (uint64_t i = 0; i < n; i++, addr += 8 + skip) *(uint64_t *)addr += rsimage_slide;
        break;
      }
      default:
        fprintf(stderr, "[rsimage] unknown rebase opcode 0x%x\n", op);
        return;
    }
  }
}

// C++ exceptions inside the image. The system unwinder only knows images dyld
// loaded, so a throw in original code (Crypto++ throws on malformed keys and
// catches at its wrapper boundary) would call std::terminate. libunwind on
// macOS 13+ takes a callback that hands it the unwind sections for an address
// (LLVM libunwind's __unw_add_find_dynamic_unwind_sections; not in the SDK
// headers, so declared here with LLVM's layout).
struct rs_unw_dynamic_unwind_sections {
  uintptr_t dso_base;
  uintptr_t dwarf_section;
  size_t dwarf_section_length;
  uintptr_t compact_unwind_section;
  size_t compact_unwind_section_length;
};
static struct rs_unw_dynamic_unwind_sections g_unwind;
static uint64_t g_unwind_lo, g_unwind_hi;

static int find_unwind_sections(uintptr_t addr, struct rs_unw_dynamic_unwind_sections *info) {
  if (addr < g_unwind_lo || addr >= g_unwind_hi) return 0;
  *info = g_unwind;
  return 1;
}

static int register_unwind(void) {
  typedef int (*add_fn)(int (*)(uintptr_t, struct rs_unw_dynamic_unwind_sections *));
  add_fn add = (add_fn)dlsym(RTLD_DEFAULT, "__unw_add_find_dynamic_unwind_sections");
  if (!add || !g_unwind.compact_unwind_section) return 0;
  return add(find_unwind_sections) == 0;
}

static void on_fault(int sig, siginfo_t *si, void *uc) {
  (void)uc;
  uint64_t a = (uint64_t)si->si_addr;
  if (a >= TRAP_BASE && a < TRAP_BASE + MAX_TRAPS * TRAP_STRIDE) {
    g_trap_hit = g_trap_names[(a - TRAP_BASE) / TRAP_STRIDE];
    if (g_trap_jmp) siglongjmp(*g_trap_jmp, 1);
    fprintf(stderr, "[rsimage] original code called unresolved import %s\n", g_trap_hit);
    _exit(3);
  }
  signal(sig, SIG_DFL);
  raise(sig);
}

void rsimage_set_trap_jmp(sigjmp_buf *jb) { g_trap_jmp = jb; }
const char *rsimage_last_trap(void) { return g_trap_hit; }

int rsimage_load(const char *path, rsimage_info *info) {
  memset(info, 0, sizeof *info);
  for (size_t i = 0; i < sizeof k_libs / sizeof *k_libs; i++) dlopen(k_libs[i], RTLD_NOW | RTLD_GLOBAL);

  int fd = open(path, O_RDONLY);
  if (fd < 0) {
    perror(path);
    return -1;
  }
  struct stat st;
  fstat(fd, &st);
  uint8_t *file = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
  close(fd);
  if (file == MAP_FAILED) return -1;

  const struct mach_header_64 *mh = (const void *)file;
  if (mh->magic != MH_MAGIC_64 || mh->cputype != CPU_TYPE_X86_64) {
    fprintf(stderr, "[rsimage] %s: not a thin x86_64 Mach-O\n", path);
    return -1;
  }
  uint64_t segs[16], init_addr = 0;
  int nsegs = 0;
  const struct dyld_info_command *dyld = NULL;
  const uint8_t *lc = file + sizeof *mh;
  for (uint32_t i = 0; i < mh->ncmds; i++, lc += ((const struct load_command *)lc)->cmdsize) {
    const struct load_command *c = (const void *)lc;
    if (c->cmd == LC_DYLD_INFO || c->cmd == LC_DYLD_INFO_ONLY) dyld = (const void *)c;
    if (c->cmd != LC_SEGMENT_64) continue;
    const struct segment_command_64 *sg = (const void *)c;
    if (nsegs < 16) segs[nsegs++] = sg->vmaddr + rsimage_slide;
    const struct section_64 *sect = (const void *)(sg + 1);
    for (uint32_t k = 0; k < sg->nsects; k++) {
      if ((sect[k].flags & SECTION_TYPE) == S_MOD_INIT_FUNC_POINTERS) {
        init_addr = sect[k].addr + rsimage_slide;
        g_ninits = (int)(sect[k].size / 8);
      }
      if (strncmp(sect[k].segname, SEG_TEXT, 16) != 0) continue;
      if (strncmp(sect[k].sectname, "__unwind_info", 16) == 0) {
        g_unwind.compact_unwind_section = sect[k].addr + rsimage_slide;
        g_unwind.compact_unwind_section_length = sect[k].size;
      } else if (strncmp(sect[k].sectname, "__eh_frame", 16) == 0) {
        g_unwind.dwarf_section = sect[k].addr + rsimage_slide;
        g_unwind.dwarf_section_length = sect[k].size;
      }
    }
    if (sg->vmsize == 0 || strcmp(sg->segname, SEG_PAGEZERO) == 0) continue;
    mach_vm_address_t at = sg->vmaddr + rsimage_slide;
    kern_return_t kr = mach_vm_allocate(mach_task_self(), &at, sg->vmsize, VM_FLAGS_FIXED);
    if (kr != KERN_SUCCESS) {
      fprintf(stderr, "[rsimage] cannot map %s at 0x%llx (%s)\n", sg->segname, (unsigned long long)at,
              mach_error_string(kr));
      return -1;
    }
    memcpy((void *)at, file + sg->fileoff, sg->filesize);
    if (strcmp(sg->segname, SEG_TEXT) == 0) {
      info->text_lo = at;
      info->text_hi = at + sg->vmsize;
      g_unwind.dso_base = at;  // the Mach-O header starts __TEXT
    }
  }
  if (!dyld) {
    fprintf(stderr, "[rsimage] no LC_DYLD_INFO\n");
    return -1;
  }
  run_rebases(file + dyld->rebase_off, file + dyld->rebase_off + dyld->rebase_size, segs, nsegs);
  run_binds(file + dyld->bind_off, file + dyld->bind_off + dyld->bind_size, segs, nsegs, 0);
  run_binds(file + dyld->lazy_bind_off, file + dyld->lazy_bind_off + dyld->lazy_bind_size, segs,
            nsegs, 1);
  // Weak binds are skipped: coalesced C++ definitions already point into the image.

  // __TEXT read+exec, everything else stays read/write.
  mprotect((void *)info->text_lo, info->text_hi - info->text_lo, PROT_READ | PROT_EXEC);
  mach_vm_address_t trap = TRAP_BASE;
  mach_vm_allocate(mach_task_self(), &trap, MAX_TRAPS * TRAP_STRIDE, VM_FLAGS_FIXED);
  mprotect((void *)TRAP_BASE, MAX_TRAPS * TRAP_STRIDE, PROT_NONE);
  struct sigaction sa = {0};
  sa.sa_sigaction = on_fault;
  sa.sa_flags = SA_SIGINFO;
  sigaction(SIGSEGV, &sa, NULL);
  sigaction(SIGBUS, &sa, NULL);

  // The initializer table is read after rebasing, so it holds slid addresses.
  g_inits = calloc((size_t)g_ninits + 1, sizeof *g_inits);
  g_init_done = calloc((size_t)g_ninits + 1, 1);
  if (g_ninits) memcpy(g_inits, (void *)init_addr, (size_t)g_ninits * 8);
  info->ninits = g_ninits;
  info->resolved = g_resolved;
  info->unresolved = g_unresolved;
  g_unwind_lo = info->text_lo;
  g_unwind_hi = info->text_hi;
  info->unwind = register_unwind();
  munmap(file, st.st_size);
  return 0;
}

int rsimage_init_count(void) { return g_ninits; }
uint64_t rsimage_init_addr(int i) { return i >= 0 && i < g_ninits ? g_inits[i] - rsimage_slide : 0; }

// Run initializer i under a trap guard. Returns 1 if it ran, 0 if already run, -1 if it trapped.
static int run_init(int i) {
  if (g_init_done[i]) return 0;
  g_init_done[i] = 1;  // also when it traps: never run half an initializer twice
  sigjmp_buf jb, *saved = g_trap_jmp;
  g_trap_jmp = &jb;
  if (sigsetjmp(jb, 1)) {
    g_trap_jmp = saved;
    fprintf(stderr, "[rsimage] initializer 0x%llx called unresolved import %s\n",
            (unsigned long long)(g_inits[i] - rsimage_slide), g_trap_hit);
    return -1;
  }
  ((void (*)(void))(uintptr_t)g_inits[i])();
  g_trap_jmp = saved;
  return 1;
}

int rsimage_run_inits(uint64_t lo, uint64_t hi) {
  int n = 0;
  for (int i = 0; i < g_ninits; i++) {
    uint64_t a = g_inits[i] - rsimage_slide;
    if (a < lo || a >= hi) continue;
    int r = run_init(i);
    if (r < 0) return -1;
    n += r;
  }
  return n;
}

int rsimage_run_init_list(const uint64_t *addrs, int count) {
  int n = 0;
  for (int k = 0; k < count; k++) {
    int found = 0;
    for (int i = 0; i < g_ninits; i++) {
      if (g_inits[i] - rsimage_slide != addrs[k]) continue;
      found = 1;
      int r = run_init(i);
      if (r < 0) return -1;
      n += r;
    }
    if (!found) {
      fprintf(stderr, "[rsimage] 0x%llx is not a static initializer\n", (unsigned long long)addrs[k]);
      return -1;
    }
  }
  return n;
}
