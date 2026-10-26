// Overrides: the rsmacfixes and RSBypass patches as native `F_<addr>` replacements.
// Credit: rs-mac-fixes by glebb (github.com/glebb/rs-mac-fixes, MIT) and RSBypass by aik002.
//
// The mods patch x86 bytes at fixed addresses of the Steam x86_64 build. In the recompiled app no x86
// code executes, so each patch becomes a native function that replaces the translated function containing
// it (mods.skip lists them). Everything works on guest memory directly (guest pointers are
// host pointers) and calls translated code with OV_GUEST / OV_VCALL.
//
// Behaviour is the mod's, function for function; where the mod's patch sits in the middle of a function,
// the whole function is replaced by a native one (the three number-table functions, the install-step
// caller) whose other effects are the game's.
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/sysctl.h>
#include <time.h>
#include <unistd.h>

#include "mods.h"

// libc++'s operator new / delete, as the translated code calls them (nodes and bucket arrays are freed by
// translated code with operator delete)
extern void *rs_op_new(size_t) __asm__("__Znwm");
extern void rs_op_delete(void *) __asm__("__ZdlPv");

// ------------------------------------------------------------------------------------------------
// Configuration
// ------------------------------------------------------------------------------------------------
static rs_mods_config g_cfg = {1, 1, 0.050};
static int g_log;
static atomic_ulong g_path_hits, g_path_misses, g_installed, g_frames, g_bypassed;
#define COUNT(x) atomic_fetch_add_explicit(&(x), 1, memory_order_relaxed)

static void logf_(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void logf_(const char *fmt, ...) {
  if (!g_log) return;
  va_list ap;
  va_start(ap, fmt);
  fputs("[rs-mods] ", stderr);
  vfprintf(stderr, fmt, ap);
  fputc('\n', stderr);
  va_end(ap);
}

static int env_on(const char *name, int dflt) {
  const char *v = getenv(name);
  if (!v || !*v) return dflt;
  return !(v[0] == '0' || v[0] == 'n' || v[0] == 'N' || v[0] == 'f' || v[0] == 'F');
}

void rs_mods_reload_config(void) {
  g_log = env_on("RS_MODS_LOG", 0);
  g_cfg.bypass = env_on("RS_MODS_BYPASS", 1);
  g_cfg.fixes = env_on("RS_MODS_FIXES", 1);
  g_cfg.budget_s = 0.050;
  const char *b = getenv("RS_MODS_BUDGET_MS");
  if (b && *b) {
    char *end;
    double ms = strtod(b, &end);
    if (end != b && ms >= 0 && ms <= 1000) g_cfg.budget_s = ms / 1000;
  }
}
const rs_mods_config *rs_mods_config_get(void) { return &g_cfg; }

rs_mods_stats rs_mods_stats_get(void) {
  return (rs_mods_stats){g_path_hits, g_path_misses, g_installed, g_frames, g_bypassed};
}
void rs_mods_stats_reset(void) {
  g_path_hits = g_path_misses = g_installed = g_frames = g_bypassed = 0;
}

static double elapsed_s(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (double)t.tv_sec + (double)t.tv_nsec * 1e-9;
}

// ------------------------------------------------------------------------------------------------
// OS-level fix: open-file limit (rsmacfixes fix 4). The arm64 runtime does it itself, once, at startup.
// ------------------------------------------------------------------------------------------------
uint64_t rs_mods_raise_nofile(void) {
  struct rlimit rl;
  if (getrlimit(RLIMIT_NOFILE, &rl) != 0) return 0;
  rlim_t old = rl.rlim_cur, want = 10240;
  if (rl.rlim_max != RLIM_INFINITY && rl.rlim_max < want) want = rl.rlim_max;
  int maxproc = 0;
  size_t n = sizeof maxproc;
  if (sysctlbyname("kern.maxfilesperproc", &maxproc, &n, NULL, 0) == 0 && maxproc > 0 && (rlim_t)maxproc < want)
    want = (rlim_t)maxproc;
  if (want > old) {
    rl.rlim_cur = want;
    int r = setrlimit(RLIMIT_NOFILE, &rl);
    getrlimit(RLIMIT_NOFILE, &rl);
    logf_("open-file limit %llu -> %llu%s", (unsigned long long)old, (unsigned long long)rl.rlim_cur,
          r ? " (setrlimit failed)" : "");
  }
  return rl.rlim_cur;
}

// ------------------------------------------------------------------------------------------------
// End key in menus (rsmacfixes keys()): the game fills the macOS-keycode -> Scaleform-key table at
// 0x101909f20 at startup; kVK_End (0x77) is missing. Added once the game has filled the table (a known
// entry is set), and again should the table be rebuilt. Runs on a poller thread (as in the mod) because
// nothing the translated code calls fills that table at a known point.
// ------------------------------------------------------------------------------------------------
#define KEYTAB 0x101909f20ull
#define KEYTAB_END_INDEX 0x77
#define KEYTAB_END_VALUE 0x23
#define KEYTAB_CHECK_INDEX 0x73
#define KEYTAB_CHECK_VALUE 0x24

int rs_mods_keys_poll_once(void) {
  if (!g_cfg.fixes || !rc_img_lo) return 0;
  volatile uint32_t *t = (volatile uint32_t *)(uintptr_t)RC_A(KEYTAB);
  if (t[KEYTAB_CHECK_INDEX] != KEYTAB_CHECK_VALUE || t[KEYTAB_END_INDEX] == KEYTAB_END_VALUE) return 0;
  if (t[KEYTAB_END_INDEX] != 0) return 0;  // something else is there: leave it
  t[KEYTAB_END_INDEX] = KEYTAB_END_VALUE;
  logf_("keys: End key (menus) added");
  return 1;
}

static void *keys_thread(void *arg) {
  (void)arg;
  for (;;) {
    rs_mods_keys_poll_once();
    usleep(100000);
  }
  return NULL;
}

__attribute__((constructor)) static void mods_init(void) {
  rs_mods_reload_config();
  if (g_cfg.fixes) rs_mods_raise_nofile();
  if (g_cfg.fixes && !env_on("RS_MODS_NO_THREAD", 0)) {
    pthread_t t;
    if (pthread_create(&t, NULL, keys_thread, NULL) == 0) pthread_detach(t);
  }
  logf_("fixes %s, bypass %s, budget %.0f ms", g_cfg.fixes ? "on" : "off", g_cfg.bypass ? "on" : "off",
        g_cfg.budget_s * 1000);
}

// ------------------------------------------------------------------------------------------------
// Game keyboard (rsmacfixes set_key): F_101135520 / F_101135560 = key down / up (called only from
// F_100ba1190). In the game: characters 1..255 -> KEY_* index through CHAR_TO_KEY, state array entry =
// 1 / 0. The mod also maps the keys CHAR_TO_KEY lacks, from the caller's full character, a local of
// F_100ba1190 at frame offset -0x28; the override is entered with the caller's frame pointer in c->r[5],
// so it reads the same slot.
// ------------------------------------------------------------------------------------------------
#define CHAR_TO_KEY 0x101921c60ull
#define KEY_STATE 0x101922060ull

static const struct {
  uint32_t ch;  // full character as Cocoa reports it
  uint8_t key;  // game KEY_*
} EXTRA_KEYS[] = {
    {' ', 0x39},                                                       // KEY_SPACE
    {0x0d, 0x1c}, {0x03, 0x1c},                                        // Return, keypad Enter: KEY_RETURN
    {0x1b, 0x01},                                                      // KEY_ESCAPE
    {0x09, 0x0f}, {0x19, 0x0f},                                        // Tab, Shift+Tab: KEY_TAB
    {0x7f, 0x0e}, {0x08, 0x0e},                                        // Delete (backspace): KEY_BACK
    {0xF700, 0x79}, {0xF701, 0x7e}, {0xF702, 0x7b}, {0xF703, 0x7c},    // KEY_UP, DOWN, LEFT, RIGHT
    {0xF704, 0x3b}, {0xF705, 0x3c}, {0xF706, 0x3d}, {0xF707, 0x3e}, {0xF708, 0x3f},  // KEY_F1..F5
    {0xF709, 0x40}, {0xF70A, 0x41}, {0xF70B, 0x42}, {0xF70C, 0x43}, {0xF70D, 0x44},  // KEY_F6..F10
    {0xF70E, 0x55}, {0xF70F, 0x56},                                                  // KEY_F11, F12
    {0xF728, 0x81}, {0xF729, 0x78}, {0xF72B, 0x7d},                    // forward delete, Home, End
    {0xF72C, 0x7a}, {0xF72D, 0x7f},                                    // Page Up (KEY_PRIOR), Page Down (KEY_NEXT)
    {'-', 0x0c}, {'=', 0x0d}, {'[', 0x1a}, {']', 0x1b}, {';', 0x27}, {'\'', 0x28},
    {'`', 0x29}, {'\\', 0x2b}, {',', 0x33}, {'.', 0x34}, {'/', 0x35},
};
#define NEXTRAKEYS (sizeof EXTRA_KEYS / sizeof EXTRA_KEYS[0])

// The game's mapping: characters 1..255 through CHAR_TO_KEY, a character without a key sets entry 0 (KEY_NOKEY).
static void set_key(rc_ctx *c, int ch, uint32_t down) {
  (void)c;
  const int32_t *map = (const int32_t *)(uintptr_t)RC_A(CHAR_TO_KEY);
  uint32_t *state = (uint32_t *)(uintptr_t)RC_A(KEY_STATE);
  if (ch < 1 || ch > 255) return;
  int64_t key = map[ch];
  if (key >= 0 && key <= 0xff) state[key] = down;
}
// The mod also maps characters 0 and above 255 (arrows arrive as 0: Cocoa's 0xF700 truncated to a byte).
static void set_key_fixed(rc_ctx *c, int ch, uint32_t down) {
  int64_t key = -1;
  const int32_t *map = (const int32_t *)(uintptr_t)RC_A(CHAR_TO_KEY);
  uint32_t *state = (uint32_t *)(uintptr_t)RC_A(KEY_STATE);
  if (ch >= 1 && ch <= 255) key = map[ch];
  if (key <= 0) {
    uint32_t full = RC_LD(uint32_t, c->r[5] - 0x28);
    for (size_t i = 0; i < NEXTRAKEYS; i++)
      if (EXTRA_KEYS[i].ch == full) key = EXTRA_KEYS[i].key;
  }
  if (key >= 0 && key <= 0xff) state[key] = down;
}

static void key_event(rc_ctx *c, uint32_t down) {
  OV_ARGS(a, c);
  (void)OV_U64(a);  // this
  int ch = OV_I32(a);
  if (g_cfg.fixes) set_key_fixed(c, ch, down);
  else set_key(c, ch, down);
  ov_ret_void(c);
}
void F_101135520(rc_ctx *c) { key_event(c, 1); }
void F_101135560(rc_ctx *c) { key_event(c, 0); }

// ------------------------------------------------------------------------------------------------
// Thread priority (rsmacfixes my_setschedparam): F_101313170 = the engine thread class's SetPriority(this,
// index). It passes a value of the table at 0x10142d3f0 (3071 3069 2302 1535 768 1 0) to
// pthread_setschedparam; macOS rejects the first five (EINVAL above 63), accepts 1 and 0, the bottom of the
// scheduler, which hitches Floodgate job threads by 20-130 ms. Requests below the default (31) are
// raised to it. Replaces the whole function (the mod interposes the call it makes).
// ------------------------------------------------------------------------------------------------
#define PRIO_TABLE 0x10142d3f0ull
#define DEFAULT_PRIO 31

void F_101313170(rc_ctx *c) {
  OV_ARGS(a, c);
  uint8_t *self = OV_PTR(a, uint8_t);
  int32_t idx = OV_I32(a);
  uint32_t ok = 1;
  if (*(int32_t *)(self + 0x20) != idx) {
    if ((uint32_t)idx > 6) {
      ok = 0;
    } else {
      int32_t want = RC_LD(int32_t, RC_A(PRIO_TABLE) + 4ull * (uint32_t)idx);
      pthread_t th = RC_LD(pthread_t, self + 0x30);
      int policy = 0;
      struct sched_param sp;
      memset(&sp, 0, sizeof sp);
      pthread_getschedparam(th, &policy, &sp);
      sp.sched_priority = want;
      if (g_cfg.fixes && want < DEFAULT_PRIO) sp.sched_priority = DEFAULT_PRIO;
      pthread_setschedparam(th, policy, &sp);
      *(int32_t *)(self + 0x20) = idx;
    }
  }
  ov_ret_u32(c, ok);
}

// ------------------------------------------------------------------------------------------------
// Path MD5 cache (rsmacfixes hook_path_md5): F_100158b70(void **out, const char *path) = `*out =
// intern(path, -1)` (F_100158030: hash the path, return the interned refcounted path object with a
// reference for the caller). Every mounted archive's file-exists check recomputes the MD5 of the same path:
// quadratic in the number of archives. The cache hands out the same object with its reference count raised
// under the game's recursive path-table lock. The game's lock protocol (checked in F_100158030/F_100158840):
// pthread_mutex_lock; owner (pthread_self) at +0x40; depth ++ at +0x48 ... depth --; unlock.
// ------------------------------------------------------------------------------------------------
#define PATH_LOCK_PTR 0x1018e6c10ull   // pointer to the recursive mutex of the interned path table
#define PATH_TABLE_PTR 0x1018e6c00ull  // the interned path table itself
#define FN_INTERN 0x100158030ull       // void *(const char *s, int len /* -1 = strlen */)
#define FN_RELEASE 0x100158840ull      // int (void *obj): drops a reference
#define OBJ_REFS 0x28                  // int refcount in the interned object

#define SLOTS 1024  // direct-mapped
static struct {
  char *s;
  size_t n;
  void *obj;
} g_cache[SLOTS];
static void *g_cache_table;  // the path table the cached objects belong to

static pthread_mutex_t *path_lock(void) {
  pthread_mutex_t *m = (pthread_mutex_t *)(uintptr_t)RC_LD(uint64_t, RC_A(PATH_LOCK_PTR));
  pthread_mutex_lock(m);
  *(pthread_t *)((char *)m + 0x40) = pthread_self();
  (*(int *)((char *)m + 0x48))++;
  void *table = (void *)(uintptr_t)RC_LD(uint64_t, RC_A(PATH_TABLE_PTR));
  if (table != g_cache_table) {  // a new path table: the old objects went with the old table
    for (unsigned i = 0; i < SLOTS; i++) {
      free(g_cache[i].s);
      g_cache[i].s = NULL;
      g_cache[i].obj = NULL;
    }
    if (g_cache_table) logf_("path table recreated; MD5 cache dropped");
    g_cache_table = table;
  }
  return m;
}
static void path_unlock(pthread_mutex_t *m) {
  (*(int *)((char *)m + 0x48))--;
  pthread_mutex_unlock(m);
}

static void *intern_path(const char *s) {
  return (void *)(uintptr_t)OV_GUEST(FN_INTERN, (uint64_t)(uintptr_t)s, 0xffffffffull);
}

void F_100158b70(rc_ctx *c) {
  OV_ARGS(a, c);
  void **out = OV_PTR(a, void *);
  const char *s = OV_PTR(a, const char);
  void *obj;
  if (!g_cfg.fixes || !RC_LD(uint64_t, RC_A(PATH_LOCK_PTR))) {  // the game's path; the lock is created later than the hook
    obj = intern_path(s);
    *out = obj;
    OV_RET_PTR(c, obj);
    return;
  }
  size_t n = strlen(s);
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < n; i++) h = (h ^ (uint8_t)s[i]) * 16777619u;
  unsigned k = h % SLOTS;
  pthread_mutex_t *m = path_lock();
  if (g_cache[k].obj && g_cache[k].n == n && !memcmp(g_cache[k].s, s, n)) {
    obj = g_cache[k].obj;
    (*(int *)((char *)obj + OBJ_REFS))++;
    path_unlock(m);
    COUNT(g_path_hits);
    *out = obj;
    OV_RET_PTR(c, obj);
    return;
  }
  path_unlock(m);
  COUNT(g_path_misses);
  obj = intern_path(s);  // +1 reference, for the caller
  char *copy = malloc(n + 1);
  void *evicted = NULL;
  if (obj && copy) {
    memcpy(copy, s, n + 1);
    m = path_lock();
    evicted = g_cache[k].obj;
    free(g_cache[k].s);
    g_cache[k].s = copy;
    g_cache[k].n = n;
    g_cache[k].obj = obj;
    (*(int *)((char *)obj + OBJ_REFS))++;  // the cache's own reference
    path_unlock(m);
  } else {
    free(copy);
  }
  if (evicted) OV_GUEST(FN_RELEASE, (uint64_t)(uintptr_t)evicted);
  *out = obj;
  OV_RET_PTR(c, obj);
}

// ------------------------------------------------------------------------------------------------
// Batched DLC mounting (rsmacfixes batch_install_step): F_100a0d730 is the per-frame update of the DLC
// coordinator. It calls the install step F_100a0d7e0 (mounts one pending archive, or clears flag +0xa and
// marks enumeration done when the list is empty) once per frame; the mod redirected that call to a loop
// bounded by a frame budget. The step stays translated; the caller is replaced by this native version.
// ------------------------------------------------------------------------------------------------
#define INSTALLING_FLAG 0xa

void F_100a0d730(rc_ctx *c) {
  OV_ARGS(a, c);
  uint8_t *self = OV_PTR(a, uint8_t);
  uint64_t o = (uint64_t)(uintptr_t)self;
  OV_GUEST(0x100a0cc40, o);
  // --- the patched call (0x100a0d742) ---
  if (!g_cfg.fixes || g_cfg.budget_s <= 0 || !*(volatile uint8_t *)(self + INSTALLING_FLAG)) {
    OV_GUEST(0x100a0d7e0, o);
  } else {
    COUNT(g_frames);
    double t0 = elapsed_s();
    do {
      OV_GUEST(0x100a0d7e0, o);
      // the step that finds the list empty clears the flag and installs nothing
      if (*(volatile uint8_t *)(self + INSTALLING_FLAG)) COUNT(g_installed);
    } while (*(volatile uint8_t *)(self + INSTALLING_FLAG) && elapsed_s() - t0 < g_cfg.budget_s);
  }
  if (!self[0x1a0]) self[0x1a0] = 1;
  if (self[0x9] && self[0x8]) {
    if ((uint8_t)OV_GUEST(0x100a0d9c0, o)) {
      self[0x8] = 0;
      self[0x1a1] = 0;
    }
  }
  if (!self[0x1a1]) OV_GUEST(0x100a0dcd0, o);
  if (self[0xc8]) {
    if (!self[0xa] && *(uint64_t *)(self + 0x68)) {  // clear the std::map at +0x48
      OV_GUEST(0x10008b010, o + 0x48, *(uint64_t *)(self + 0x50));
      *(uint64_t *)(self + 0x58) = o + 0x48;
      *(uint64_t *)(self + 0x50) = 0;
      *(uint64_t *)(self + 0x60) = o + 0x48;
      *(uint64_t *)(self + 0x68) = 0;
    }
    self[0xc8] = 0;
  }
  ov_ret_void(c);
}

// ------------------------------------------------------------------------------------------------
// JSON-number hash table (rsmacfixes SITES): the game's global table of number values hashes a double by
// its integer part ((uint64)v with the 2^63 fixup), so every value in [0, 1) lands in bucket 0. The mod
// replaces the hash by the value's bit pattern, with -0 made +0 so equal numbers hash equally, at the three
// places that compute it, which sit in the middle of three functions: the number object's destructor
// F_1000f8d20 (remove), the interning constructor F_1000f8f10 (insert/find) and the table's rehash
// F_1000f9920. All three are replaced whole.
// Table (*0x101949588): +0x08 buckets begin, +0x10 buckets end, +0x18 capacity end, +0x20 count; nodes
// {next, double key, number object*}; its recursive mutex is *0x101949590.
// ------------------------------------------------------------------------------------------------
#define TABLE_PTR 0x101949588ull
#define TABLE_LOCK_PTR 0x101949590ull
#define NUM_VTABLE 0x1017f5c70ull
#define HASH_CONST 0x1013eaa50ull  // 2^63 as a double
#define PRIME_TABLE 0x1013eaa70ull // 28 sizes (uint64), lower_bound'ed by rehash
#define NPRIMES 28
#define FN_EFD_ALLOC 0x1012efc90ull
#define FN_OBJ_CTOR 0x100105620ull
#define FN_OBJ_DTOR_BASE 0x100105640ull

static inline uint64_t *table(void) { return (uint64_t *)(uintptr_t)RC_LD(uint64_t, RC_A(TABLE_PTR)); }

// The game's hash. `insert` selects the compare of F_1000f8f10 (v >= 2^63), the other two use
// 2^63 <= v or unordered; the NaN case differs between them (never reached: JSON numbers).
static uint64_t hash_orig(double v, int insert) {
  double k = RC_LD(double, RC_A(HASH_CONST));
  int big = insert ? (v >= k) : (k <= v || v != v);
  return big ? (uint64_t)rc_cvtt_f64_i64(v - k) ^ 0x8000000000000000ull : (uint64_t)rc_cvtt_f64_i64(v);
}
static uint64_t hash_num(double v, int insert) {
  if (!g_cfg.fixes) return hash_orig(v, insert);
  double z = 0.0 + v;  // -0 -> +0
  uint64_t u;
  memcpy(&u, &z, 8);
  return u;
}

// F_1000f9920(table, min_buckets): grow to the next table size >= min_buckets if that is larger than the
// current bucket count, relinking every node by the (new) hash.
static void num_rehash(uint64_t *T, uint64_t want) {
  uint64_t nb = (T[2] - T[1]) / 8;
  if (nb >= want) return;
  const uint64_t *P = (const uint64_t *)(uintptr_t)RC_A(PRIME_TABLE);
  const uint64_t *p = P;
  uint64_t cnt = NPRIMES;
  while (cnt > 0) {  // std::lower_bound
    uint64_t half = cnt >> 1;
    if (p[half] < want) {
      p += half + 1;
      cnt -= half + 1;
    } else {
      cnt = half;
    }
  }
  uint64_t new_nb = (p == P + NPRIMES) ? 0xfffffffbull : *p;
  if (new_nb <= nb) return;
  uint64_t *nbk = rs_op_new(new_nb * 8);
  memset(nbk, 0, new_nb * 8);
  uint64_t *old = (uint64_t *)(uintptr_t)T[1];
  for (uint64_t i = 0; i < nb; i++) {
    uint64_t *node;
    while ((node = (uint64_t *)(uintptr_t)old[i]) != NULL) {
      double key;
      memcpy(&key, &node[1], 8);
      uint64_t idx = hash_num(key, 0) % new_nb;
      old[i] = node[0];
      node[0] = nbk[idx];
      nbk[idx] = (uint64_t)(uintptr_t)node;
    }
  }
  T[1] = (uint64_t)(uintptr_t)nbk;
  T[2] = (uint64_t)(uintptr_t)(nbk + new_nb);
  T[3] = (uint64_t)(uintptr_t)(nbk + new_nb);
  if (old) rs_op_delete(old);
}

void F_1000f9920(rc_ctx *c) {
  OV_ARGS(a, c);
  uint64_t *T = OV_PTR(a, uint64_t);
  uint64_t want = OV_U64(a);
  num_rehash(T, want);
  ov_ret_void(c);
}

// F_1000f8f10(out, double v): *out = the number object for v, from the table or newly created and
// inserted. The reference-count/virtual calls (slots 0 and 1 of the object: AddRef/Release inlined
// smart-pointer traffic) are made in the same order as the game's.
void F_1000f8f10(rc_ctx *c) {
  OV_ARGS(a, c);
  uint64_t *out = OV_PTR(a, uint64_t);
  double v = OV_F64(a);
  pthread_mutex_t *m = (pthread_mutex_t *)(uintptr_t)RC_LD(uint64_t, RC_A(TABLE_LOCK_PTR));
  pthread_mutex_lock(m);
  *(pthread_t *)((char *)m + 0x40) = pthread_self();
  (*(int *)((char *)m + 0x48))++;
  *out = 0;
  uint64_t *T = table();
  uint64_t nb = (T[2] - T[1]) / 8;
  uint64_t h = hash_num(v, 1);
  uint64_t *node = nb ? (uint64_t *)(uintptr_t)((uint64_t *)(uintptr_t)T[1])[h % nb] : NULL;
  for (; node; node = (uint64_t *)(uintptr_t)node[0]) {
    double k;
    memcpy(&k, &node[1], 8);
    if (k == v) break;
  }
  if (node) {  // found
    uint64_t obj = node[2];
    if (obj) {
      OV_VCALL(obj, 0);
      *out = obj;
      OV_VCALL(obj, 0);
      OV_VCALL(obj, 1);
    }
  } else {
    uint64_t obj = OV_GUEST(FN_EFD_ALLOC, 0x18, 0);
    OV_GUEST(FN_OBJ_CTOR, obj);
    int flag = 0;
    if (obj) {
      RC_ST(uint64_t, obj, RC_A(NUM_VTABLE));
      RC_ST(double, obj + 0x10, v);
      atomic_fetch_add_explicit((_Atomic int32_t *)(uintptr_t)(obj + 8), 1, memory_order_seq_cst);  // OSAtomicAdd32
      *out = obj;
      OV_VCALL(obj, 0);
      OV_VCALL(obj, 1);
      T = table();
      OV_VCALL(obj, 0);
      OV_VCALL(obj, 0);
    } else {
      T = table();
      flag = 1;
    }
    num_rehash(T, T[4] + 1);
    nb = (T[2] - T[1]) / 8;
    uint64_t idx = h % nb;
    uint64_t *bk = (uint64_t *)(uintptr_t)T[1];
    uint64_t head = bk[idx];
    uint64_t *n2;
    for (n2 = (uint64_t *)(uintptr_t)head; n2; n2 = (uint64_t *)(uintptr_t)n2[0]) {
      double k;
      memcpy(&k, &n2[1], 8);
      if (k == v) break;
    }
    if (!n2) {
      uint64_t *nn = rs_op_new(0x18);
      memcpy(&nn[1], &v, 8);
      nn[2] = obj;
      nn[0] = head;
      bk[idx] = (uint64_t)(uintptr_t)nn;
      T[4]++;
    }
    if (obj) OV_VCALL(obj, 1);
    if (!flag) OV_VCALL(obj, 1);
  }
  m = (pthread_mutex_t *)(uintptr_t)RC_LD(uint64_t, RC_A(TABLE_LOCK_PTR));
  (*(int *)((char *)m + 0x48))--;
  pthread_mutex_unlock(m);
  OV_RET_PTR(c, out);
}

// F_1000f8d20(this): the number object's destructor: set the vtable, remove its node(s) from the table, run
// the base destructor (a tail call).
void F_1000f8d20(rc_ctx *c) {
  OV_ARGS(a, c);
  uint8_t *self = OV_PTR(a, uint8_t);
  RC_ST(uint64_t, self, RC_A(NUM_VTABLE));
  uint64_t *T = table();
  double v;
  memcpy(&v, self + 0x10, 8);
  uint64_t nb = T ? (T[2] - T[1]) / 8 : 0;
  if (nb) {
    uint64_t idx = hash_num(v, 0) % nb;
    uint64_t *bk = (uint64_t *)(uintptr_t)T[1];
    uint64_t *first = (uint64_t *)(uintptr_t)bk[idx];
    if (first) {
      uint64_t *prev = first;
      uint64_t *cur = (uint64_t *)(uintptr_t)first[0];
      while (cur) {  // nodes after the first: unlink every one with this key
        double k;
        memcpy(&k, &cur[1], 8);
        if (k == v) {
          prev[0] = cur[0];
          rs_op_delete(cur);
          T[4]--;
          cur = (uint64_t *)(uintptr_t)prev[0];
          memcpy(&v, self + 0x10, 8);
        } else {
          prev = cur;
          cur = (uint64_t *)(uintptr_t)cur[0];
        }
      }
      double k0;
      memcpy(&k0, &first[1], 8);
      if (k0 == v) {  // the first node
        ((uint64_t *)(uintptr_t)T[1])[idx] = first[0];
        rs_op_delete(first);
        T[4]--;
      }
    }
  }
  uint64_t r = OV_GUEST(FN_OBJ_DTOR_BASE, (uint64_t)(uintptr_t)self);
  ov_ret_u64(c, r);
}

// ------------------------------------------------------------------------------------------------
// RSBypass: F_1001eb6a0 (SNG loader) calls the DSA verifier F_10137683d(data, len, keyHex) at 0x1001eb763
// (return address 0x1001eb768) and, if the result is false, abandons the file; the mod NOPs that
// `test r14b,r14b; je` (9 bytes at 0x1001eb784) so unsigned custom songs load. The verifier has other
// callers (package config 0x1002a7550), so the bypass is made only for the SNG loader's call, found by the
// return address the translated caller pushed. With RS_MODS_BYPASS=0 the real verifier runs. The verifier
// itself is the crypto module's adapter, built under another name (build.sh: -DF_10137683d=...).
// ------------------------------------------------------------------------------------------------
#define SNG_VERIFY_RET 0x1001eb768ull
extern void rs_crypto_F_10137683d(rc_ctx *c);

void F_10137683d(rc_ctx *c) {
  if (g_cfg.bypass && RC_LD(uint64_t, c->r[4]) == RC_A(SNG_VERIFY_RET)) {
    COUNT(g_bypassed);
    ov_ret_u32(c, 1);
    return;
  }
  rs_crypto_F_10137683d(c);
}

const ov_entry ov_mods_entries[] = {
    {0x1000f8d20ull, "num_dtor", F_1000f8d20},
    {0x1000f8f10ull, "num_intern", F_1000f8f10},
    {0x1000f9920ull, "num_rehash", F_1000f9920},
    {0x100158b70ull, "path_md5", F_100158b70},
    {0x100a0d730ull, "dlc_update", F_100a0d730},
    {0x101135520ull, "key_down", F_101135520},
    {0x101135560ull, "key_up", F_101135560},
    {0x101313170ull, "thread_set_priority", F_101313170},
    {0x10137683dull, "sng_verify", F_10137683d},
};
const int OV_MODS_COUNT = (int)(sizeof ov_mods_entries / sizeof ov_mods_entries[0]);
