// 32-bit guests: the 4 GB guest window (rc32.h).
//
// The whole window is reserved PROT_NONE at start and committed in pieces: the image at its
// preferred base, and an arena for everything else the guest may point to (stacks, TEBs, heap
// blocks, the import area). The first 64 KB stay unmapped, so a null pointer still faults.
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>

#include "rc32.h"

#define WINDOW (1ull << 32)
// The arena: above the usual image bases (0x400000, DLLs at 0x10000000..0x3fffffff) and below
// the top of the space a 32-bit large-address-aware process would see.
#define ARENA_LO 0x50000000u
#define ARENA_HI 0xf0000000u

uint8_t *rc_mem;
static uint32_t arena_next = ARENA_LO, arena_committed = ARENA_LO;
static pthread_mutex_t arena_mu = PTHREAD_MUTEX_INITIALIZER;

int rc32_mem_init(void) {
  if (rc_mem) return 0;
  void *p = mmap(0, WINDOW, PROT_NONE, MAP_PRIVATE | MAP_ANON | MAP_NORESERVE, -1, 0);
  if (p == MAP_FAILED) {
    fprintf(stderr, "rc32: cannot reserve the 4 GB guest window (errno %d)\n", errno);
    return -1;
  }
  rc_mem = p;
  return 0;
}

int rc32_mem_commit(uint32_t g, uint32_t size) {
  if (rc32_mem_init()) return -1;
  uint64_t pg = (uint64_t)getpagesize();
  uint64_t lo = (uint64_t)g & ~(pg - 1), hi = ((uint64_t)g + size + pg - 1) & ~(pg - 1);
  if (lo < 0x10000 || hi > WINDOW) return -1;
  return mprotect(rc_mem + lo, hi - lo, PROT_READ | PROT_WRITE);
}

uint32_t rc32_alloc(uint32_t size) {
  if (rc32_mem_init()) return 0;
  pthread_mutex_lock(&arena_mu);
  uint32_t a = (arena_next + 15) & ~15u, r = 0;
  if ((uint64_t)a + size <= ARENA_HI) {
    arena_next = a + size;
    if (arena_next > arena_committed) {
      uint32_t want = (arena_next - arena_committed + 0xfffff) & ~0xfffffu;  // 1 MB steps
      if (rc32_mem_commit(arena_committed, want) == 0) {
        arena_committed += want;
        r = a;
      }
    } else {
      r = a;
    }
  }
  if (r) memset(rc_mem + r, 0, size);
  pthread_mutex_unlock(&arena_mu);
  return r;
}
