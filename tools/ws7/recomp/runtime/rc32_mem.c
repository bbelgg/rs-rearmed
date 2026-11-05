// 32-bit guests: the 4 GB guest window (rc32.h).
//
// The whole window is reserved PROT_NONE at start and committed in pieces: each image at its
// preferred base, and an arena for everything else the guest may point to (stacks, TEBs, heap
// blocks, the import areas). The first 64 KB stay unmapped, so a null pointer still faults. The
// arena steps over the images inside its range (Microsoft's DLLs prefer 0x78000000 and up).
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
uint64_t rc_slide;  // the same base under the name a mod knows (RC_A)
static uint32_t arena_next = ARENA_LO, arena_committed = ARENA_LO;
static pthread_mutex_t arena_mu = PTHREAD_MUTEX_INITIALIZER;
// the images' ranges [lo, hi)
#define MAX_RESERVED 16
static struct { uint64_t lo, hi; } reserved[MAX_RESERVED];
static int nreserved;

int rc32_mem_init(void) {
  if (rc_mem) return 0;
  void *p = mmap(0, WINDOW, PROT_NONE, MAP_PRIVATE | MAP_ANON | MAP_NORESERVE, -1, 0);
  if (p == MAP_FAILED) {
    fprintf(stderr, "rc32: cannot reserve the 4 GB guest window (errno %d)\n", errno);
    return -1;
  }
  rc_mem = p;
  rc_slide = (uint64_t)(uintptr_t)p;
  return 0;
}

int rc32_mem_commit(uint32_t g, uint32_t size) {
  if (rc32_mem_init()) return -1;
  uint64_t pg = (uint64_t)getpagesize();
  uint64_t lo = (uint64_t)g & ~(pg - 1), hi = ((uint64_t)g + size + pg - 1) & ~(pg - 1);
  if (lo < 0x10000 || hi > WINDOW) return -1;
  return mprotect(rc_mem + lo, hi - lo, PROT_READ | PROT_WRITE);
}

void rc32_mem_guard(uint32_t g, uint32_t size) { mprotect(rc_mem + g, size, PROT_NONE); }

int rc32_mem_reserve(uint32_t g, uint32_t size) {
  uint64_t lo = g, hi = (uint64_t)g + size;
  int rc = -1;
  pthread_mutex_lock(&arena_mu);
  if (nreserved < MAX_RESERVED && !(lo < arena_next && hi > ARENA_LO)) {
    rc = 0;
    for (int k = 0; k < nreserved; k++)
      if (lo < reserved[k].hi && hi > reserved[k].lo) rc = -1;
    if (!rc) {
      reserved[nreserved].lo = lo;
      reserved[nreserved].hi = hi;
      nreserved++;
    }
  }
  pthread_mutex_unlock(&arena_mu);
  return rc;
}

uint32_t rc32_arena_used(void) { return arena_next - ARENA_LO; }

uint32_t rc32_alloc(uint32_t size) {
  if (rc32_mem_init()) return 0;
  pthread_mutex_lock(&arena_mu);
  uint64_t a = (arena_next + 15) & ~15u;
  uint32_t r = 0;
  for (int k = 0; k < nreserved; k++)  // step over the images (restart after each step)
    if (a < reserved[k].hi && a + size > reserved[k].lo) {
      a = (reserved[k].hi + 15) & ~15ull;
      k = -1;
    }
  if (a > arena_committed) arena_committed = (uint32_t)(a & ~0xfffull);  // a page boundary
  if (a + size <= ARENA_HI) {
    arena_next = (uint32_t)(a + size);
    if (arena_next > arena_committed) {
      uint32_t want = (arena_next - arena_committed + 0xfffff) & ~0xfffffu;  // 1 MB steps
      if (rc32_mem_commit(arena_committed, want) == 0) {
        arena_committed += want;
        r = (uint32_t)a;
      }
    } else {
      r = (uint32_t)a;
    }
  }
  if (r) memset(rc_mem + r, 0, size);
  pthread_mutex_unlock(&arena_mu);
  return r;
}
