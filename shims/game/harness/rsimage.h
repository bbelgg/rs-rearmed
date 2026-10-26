// rsimage: run original x86_64 Rocksmith 2014 functions in-process.
//
// rsimage_load() maps every segment of the decrypted game executable
// (analysis/cg/Rocksmith2014_x64_plain) at its preferred address, binds its
// imports against libSystem, libstdc++ and CoreFoundation in this process, and
// points every other import at a PROT_NONE trap page so a call into OpenGL,
// Steam, Bink etc. is reported instead of crashing at random.
//
// No static initializer runs unless asked for (rsimage_run_inits), and no ObjC
// classes are registered. Without initializers this suits leaf and
// library-level functions (hashes, codecs, parsers, math). Stateful engine code
// needs the initializers of its component (class-info tables, static
// categories, mutexes) and usually the component's own startup call; see
// harness/efd_env.h for Gamebryo's efd.
//
// Rosetta keeps its own runtime at 0x100000000, so the image is loaded at
// rsimage_slide (image base 0x400000000) and rebased; the game is PIE, so its
// code is position independent. The host binary must be x86_64.
#ifndef RSIMAGE_H
#define RSIMAGE_H

#include <setjmp.h>
#include <stdint.h>

typedef struct {
  uint64_t text_lo, text_hi;
  int resolved, unresolved;  // bound import slots
  int ninits;                // __mod_init_func entries
  int unwind;                // 1: the image's __unwind_info/__eh_frame are registered
                             // with libunwind, so C++ exceptions thrown and caught
                             // inside original code work
} rsimage_info;

int rsimage_load(const char *path, rsimage_info *info);

// While set, a call into an unresolved import siglongjmps here instead of
// exiting; rsimage_last_trap() names the import.
void rsimage_set_trap_jmp(sigjmp_buf *jb);
const char *rsimage_last_trap(void);

extern uint64_t rsimage_slide;

// Static initializers (__DATA,__mod_init_func), in image order. Each runs at
// most once. A trapped import inside one is reported and makes the call return
// -1 (the remaining ones are not run); imports outside libSystem, libstdc++
// and CoreFoundation stay trapped. All 3,487 run without trapping.
int rsimage_init_count(void);
uint64_t rsimage_init_addr(int i);  // unslid
// Run the initializers whose (unslid) address is in [lo, hi), e.g. one
// component's range. Returns how many ran.
int rsimage_run_inits(uint64_t lo, uint64_t hi);
// Run the initializers at these unslid addresses, in the order given.
int rsimage_run_init_list(const uint64_t *addrs, int n);

// Component ranges (analysis/ws7/components.md) for rsimage_run_inits.
#define RS_RANGE_ENGINE_EFD 0x10127a980ULL, 0x101319bd0ULL
#define RS_RANGE_ENGINE_EGF 0x1011c1c10ULL, 0x101260190ULL
#define RS_RANGE_ENGINE_NIMAIN 0x100e34310ULL, 0x101146090ULL
#define RS_RANGE_ALL 0ULL, ~0ULL

// Original function by its unslid address, e.g. RS_FN(uint32_t (*)(const char *), 0x100ab08b0).
#define RS_FN(type, addr) ((type)(uintptr_t)((uint64_t)(addr) + rsimage_slide))

#endif
