// The quit's teardown of the Mac game's two JSON tables (overrides/mods/quit_tables.c), without a game: a table in
// the game's layout that is empty at the quit is freed as the game frees it (every node, the buckets, the table, its
// mutex, and both globals cleared); one a cut-short load left entries in is kept whole, where the game would assert;
// and no table at all is nothing to free.
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint64_t rs_quit_table_free(uint64_t *slot, uint64_t *lock_slot, void (*del)(void *), int (*mutex_destroy)(void *));

static int g_fail;
#define CHECK(c) do { if (!(c)) { g_fail++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

// what was freed, and the mutexes destroyed
static void *g_freed[64];
static int g_nfreed, g_destroyed;
static void del(void *p) {
  if (g_nfreed < 64) g_freed[g_nfreed] = p;
  g_nfreed++;
  free(p);
}
static int was_freed(const void *p) {
  for (int i = 0; i < g_nfreed && i < 64; i++)
    if (g_freed[i] == p) return 1;
  return 0;
}
static int destroy(void *m) {
  g_destroyed++;
  return pthread_mutex_destroy(m);
}

// A table of nb buckets and the game's layout (+0x08 buckets, +0x10 their end, +0x18 capacity end, +0x20 count),
// with n nodes {next, key, value} in chains: node i in bucket i % nb.
static uint64_t *make_table(uint64_t nb, int n, uint64_t **nodes) {
  uint64_t *t = calloc(5, 8), *bk = calloc(nb, 8);
  t[1] = (uint64_t)(uintptr_t)bk, t[2] = t[3] = (uint64_t)(uintptr_t)(bk + nb);
  for (int i = 0; i < n; i++) {
    uint64_t *node = calloc(3, 8);
    node[0] = bk[i % nb], node[1] = (uint64_t)i;
    bk[i % nb] = (uint64_t)(uintptr_t)node;
    t[4]++;
    if (nodes) nodes[i] = node;
  }
  return t;
}
static uint64_t *make_lock(void) {
  pthread_mutex_t *m = malloc(sizeof *m);
  pthread_mutexattr_t a;
  pthread_mutexattr_init(&a);
  pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE);
  pthread_mutex_init(m, &a);
  return (uint64_t *)m;
}

int main(void) {
  // emptied by the documents' releases, as at a quit that comes after the loads: freed, globals cleared
  {
    uint64_t *t = make_table(7, 0, NULL), *bk = (uint64_t *)(uintptr_t)t[1], *m = make_lock();
    uint64_t slot = (uint64_t)(uintptr_t)t, lock = (uint64_t)(uintptr_t)m;
    g_nfreed = g_destroyed = 0;
    CHECK(rs_quit_table_free(&slot, &lock, del, destroy) == 0);
    CHECK(slot == 0 && lock == 0);
    CHECK(g_nfreed == 3 && was_freed(t) && was_freed(bk) && was_freed(m) && g_destroyed == 1);
  }
  // nodes still linked with a count of 0 (the game frees whatever its chains hold): every node freed too
  {
    uint64_t *nodes[5];
    uint64_t *t = make_table(3, 5, nodes), *m = make_lock();
    t[4] = 0;
    uint64_t slot = (uint64_t)(uintptr_t)t, lock = (uint64_t)(uintptr_t)m;
    g_nfreed = g_destroyed = 0;
    CHECK(rs_quit_table_free(&slot, &lock, del, destroy) == 0);
    CHECK(slot == 0 && lock == 0 && g_nfreed == 5 + 3 && g_destroyed == 1);
    for (int i = 0; i < 5; i++) CHECK(was_freed(nodes[i]));
  }
  // a quit that cut a load short: entries left, the table and its mutex kept as they were (the game asserts here)
  {
    uint64_t *nodes[4];
    uint64_t *t = make_table(5, 4, nodes), *m = make_lock();
    uint64_t before[5];
    memcpy(before, t, sizeof before);
    uint64_t slot = (uint64_t)(uintptr_t)t, lock = (uint64_t)(uintptr_t)m;
    g_nfreed = g_destroyed = 0;
    CHECK(rs_quit_table_free(&slot, &lock, del, destroy) == 4);
    CHECK(slot == (uint64_t)(uintptr_t)t && lock == (uint64_t)(uintptr_t)m && g_nfreed == 0 && g_destroyed == 0);
    CHECK(!memcmp(before, t, sizeof before) && nodes[0][1] == 0 && nodes[3][1] == 3);
    // the mutex still works: a document released later locks it to remove its entry
    CHECK(pthread_mutex_lock((pthread_mutex_t *)m) == 0 && pthread_mutex_unlock((pthread_mutex_t *)m) == 0);
  }
  // no table (never made), a mutex all the same: the mutex freed, where the game asserts on the table first
  {
    uint64_t *m = make_lock();
    uint64_t slot = 0, lock = (uint64_t)(uintptr_t)m;
    g_nfreed = g_destroyed = 0;
    CHECK(rs_quit_table_free(&slot, &lock, del, destroy) == 0);
    CHECK(slot == 0 && lock == 0 && g_nfreed == 1 && g_destroyed == 1);
    slot = lock = 0;  // and neither: nothing done
    g_nfreed = g_destroyed = 0;
    CHECK(rs_quit_table_free(&slot, &lock, del, destroy) == 0 && g_nfreed == 0 && g_destroyed == 0);
  }
  printf(g_fail ? "test_quit_tables: %d failure(s)\n" : "test_quit_tables: ok\n", g_fail);
  return g_fail != 0;
}
