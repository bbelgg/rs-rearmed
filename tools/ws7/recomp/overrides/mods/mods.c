// Overrides: the rsmacfixes and RSBypass patches as native `F_<addr>` replacements.
// Credit: rs-mac-fixes by glebb (github.com/glebb/rs-mac-fixes, MIT) and RSBypass by aik002.
//
// The mods patch x86 bytes at fixed addresses of the Steam x86_64 build. In the recompiled app no x86
// code executes, so each patch becomes a native function that replaces the translated function containing
// it (tools/targets/names/overrides/mods.list names them). Everything works on guest memory directly (guest pointers are
// host pointers) and calls translated code with OV_GUEST / OV_VCALL.
//
// Behaviour is the mod's, function for function; where the mod's patch sits in the middle of a function,
// the whole function is replaced by a native one (the three number-table functions, the install-step
// caller) whose other effects are the game's.
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

#include "mods.h"
#include "rc_hook.h"
#include "rc_mid.h"

// libc++'s operator new / delete, as the translated code calls them (nodes and bucket arrays are freed by
// translated code with operator delete)
extern void *rs_op_new(size_t) __asm__("__Znwm");
extern void rs_op_delete(void *) __asm__("__ZdlPv");

// ------------------------------------------------------------------------------------------------
// The switches (rs_mods_cfg), the log (rs_mods_logf) and the start are mods_shared.c's, one source with Learn & Play.
// ------------------------------------------------------------------------------------------------
static atomic_ulong g_path_hits, g_path_misses, g_installed, g_frames, g_bypassed, g_overlaid;
#define COUNT(x) atomic_fetch_add_explicit(&(x), 1, memory_order_relaxed)

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
// End key in menus (rsmacfixes keys()): the game fills the macOS-keycode -> Scaleform-key table at
// 0x101909f20 at startup; kVK_End (0x77) is missing. Added once the game has filled the table (a known
// entry is set), and again should the table be rebuilt. Runs on a poller thread (as in the mod) because
// nothing the translated code calls fills that table at a known point.
// ------------------------------------------------------------------------------------------------
#define KEYTAB_END_INDEX 0x77
#define KEYTAB_END_VALUE 0x23
#define KEYTAB_CHECK_INDEX 0x73
#define KEYTAB_CHECK_VALUE 0x24

int rs_mods_keys_poll_once(void) {
  if (!rs_mods_cfg.fixes || !rc_img_lo) return 0;
  volatile uint32_t *t = (volatile uint32_t *)(uintptr_t)RC_GA(scaleform_keycode_table);
  if (t[KEYTAB_CHECK_INDEX] != KEYTAB_CHECK_VALUE || t[KEYTAB_END_INDEX] == KEYTAB_END_VALUE) return 0;
  if (t[KEYTAB_END_INDEX] != 0) return 0;  // something else is there: leave it
  t[KEYTAB_END_INDEX] = KEYTAB_END_VALUE;
  rs_mods_logf("keys: End key (menus) added");
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

static int before_open_file(rc_ctx *c, void *user);
static int before_autosave_warning(rc_ctx *c, void *user);
static void mods_manifest_points(void);
static int before_is_cable(rc_ctx *c, void *user);

// The launcher has set the environment by now, so the switches are read here (Learn & Play reads its
// settings.env in main, and starts from there: lp_mods_init).
__attribute__((constructor)) static void mods_init(void) {
  rs_mods_start();  // mods_shared.c: the switches, the open-file limit, the fixes both editions share
  if (rc_hook_add_name("frontend_open_file", before_open_file, NULL, NULL))
    rs_mods_logf("no FrontEnd_OpenFile hook point");
  if (rc_hook_add_name("autosave_warning", before_autosave_warning, NULL, NULL))
    rs_mods_logf("no Auto Save warning hook point");
  if (rc_hook_add_name("input_mode_is_cable", before_is_cable, NULL, NULL))
    rs_mods_logf("no hook point for Player 2's input mode");
  mods_manifest_points();
  if (rs_mods_cfg.fixes && !rs_mods_env_on("RS_MODS_NO_THREAD", 0)) {
    pthread_t t;
    if (pthread_create(&t, NULL, keys_thread, NULL) == 0) pthread_detach(t);
  }
}

// ------------------------------------------------------------------------------------------------
// Game keyboard (rsmacfixes set_key): F_101135520 / F_101135560 = key down / up (called only from
// F_100ba1190). In the game: characters 1..255 -> KEY_* index through CHAR_TO_KEY, state array entry =
// 1 / 0. The mod also maps the keys CHAR_TO_KEY lacks, from the caller's full character, a local of
// F_100ba1190 at frame offset -0x28; the override is entered with the caller's frame pointer in c->r[5],
// so it reads the same slot.
// ------------------------------------------------------------------------------------------------

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
  const int32_t *map = (const int32_t *)(uintptr_t)RC_GA(game_char_to_key);
  uint32_t *state = (uint32_t *)(uintptr_t)RC_GA(game_key_state);
  if (ch < 1 || ch > 255) return;
  int64_t key = map[ch];
  if (key >= 0 && key <= 0xff) state[key] = down;
}
// The mod also maps characters 0 and above 255 (arrows arrive as 0: Cocoa's 0xF700 truncated to a byte).
static void set_key_fixed(rc_ctx *c, int ch, uint32_t down) {
  int64_t key = -1;
  const int32_t *map = (const int32_t *)(uintptr_t)RC_GA(game_char_to_key);
  uint32_t *state = (uint32_t *)(uintptr_t)RC_GA(game_key_state);
  if (ch >= 1 && ch <= 255) key = map[ch];
  if (key <= 0) {
    uint32_t full = RC_LD(uint32_t, RC_FRAME_PTR(c) + RC_OFF(input_sink_full_char));
    for (size_t i = 0; i < NEXTRAKEYS; i++)
      if (EXTRA_KEYS[i].ch == full) key = EXTRA_KEYS[i].key;
  }
  if (key >= 0 && key <= 0xff) state[key] = down;
}

static void key_event(rc_ctx *c, uint32_t down) {
  OV_ARGS(a, c);
  (void)OV_U64(a);  // this
  int ch = OV_I32(a);
  if (rs_mods_cfg.fixes) set_key_fixed(c, ch, down);
  else set_key(c, ch, down);
  ov_ret_void(c);
}
void RC_FN(key_down)(rc_ctx *c) { key_event(c, 1); }
void RC_FN(key_up)(rc_ctx *c) { key_event(c, 0); }

// ------------------------------------------------------------------------------------------------
// Thread priority (rsmacfixes my_setschedparam): F_101313170 = the engine thread class's SetPriority(this,
// index). It passes a value of the table at 0x10142d3f0 (3071 3069 2302 1535 768 1 0) to
// pthread_setschedparam; macOS rejects the first five (EINVAL above 63), accepts 1 and 0, the bottom of the
// scheduler, which hitches Floodgate job threads by 20-130 ms. Requests below the default (31) are
// raised to it. Replaces the whole function (the mod interposes the call it makes).
// ------------------------------------------------------------------------------------------------
#define DEFAULT_PRIO 31

void RC_FN(thread_set_priority)(rc_ctx *c) {
  OV_ARGS_OF(thread_set_priority, a, c);
  uint8_t *self = OV_PTR(a, uint8_t);
  int32_t idx = OV_I32(a);
  uint32_t ok = 1;
  if (*(int32_t *)(self + RC_OFF(thread_priority_index)) != idx) {
    if ((uint32_t)idx > 6) {
      ok = 0;
    } else {
      int32_t want = RC_LD(int32_t, RC_GA(thread_priority_table) + 4ull * (uint32_t)idx);
      pthread_t th = RC_LD(pthread_t, self + RC_OFF(thread_pthread));
      int policy = 0;
      struct sched_param sp;
      memset(&sp, 0, sizeof sp);
      pthread_getschedparam(th, &policy, &sp);
      sp.sched_priority = want;
      if (rs_mods_cfg.fixes && want < DEFAULT_PRIO) sp.sched_priority = DEFAULT_PRIO;
      pthread_setschedparam(th, policy, &sp);
      *(int32_t *)(self + RC_OFF(thread_priority_index)) = idx;
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
#define OBJ_REFS RC_OFF(path_obj_refs)  // int refcount in the interned object

#define SLOTS 1024  // direct-mapped
static struct {
  char *s;
  size_t n;
  void *obj;
} g_cache[SLOTS];
static void *g_cache_table;  // the path table the cached objects belong to

static pthread_mutex_t *path_lock(void) {
  pthread_mutex_t *m = (pthread_mutex_t *)(uintptr_t)RC_LD(uint64_t, RC_GA(path_table_lock));
  pthread_mutex_lock(m);
  *(pthread_t *)((char *)m + RC_OFF(rmutex_owner)) = pthread_self();
  (*(int *)((char *)m + RC_OFF(rmutex_depth)))++;
  void *table = (void *)(uintptr_t)RC_LD(uint64_t, RC_GA(path_table));
  if (table != g_cache_table) {  // a new path table: the old objects went with the old table
    for (unsigned i = 0; i < SLOTS; i++) {
      free(g_cache[i].s);
      g_cache[i].s = NULL;
      g_cache[i].obj = NULL;
    }
    if (g_cache_table) rs_mods_logf("path table recreated; MD5 cache dropped");
    g_cache_table = table;
  }
  return m;
}
static void path_unlock(pthread_mutex_t *m) {
  (*(int *)((char *)m + RC_OFF(rmutex_depth)))--;
  pthread_mutex_unlock(m);
}

static void *intern_path(const char *s) {
  return (void *)(uintptr_t)OV_GUEST(path_intern, (uint64_t)(uintptr_t)s, 0xffffffffull);
}

void RC_FN(path_md5)(rc_ctx *c) {
  OV_ARGS_OF(path_md5, a, c);
  void **out = OV_PTR(a, void *);
  const char *s = OV_PTR(a, const char);
  void *obj;
  // the game's path with the fixes off, and until its lock is made: later than the hook
  if (!rs_mods_cfg.fixes || !RC_LD(uint64_t, RC_GA(path_table_lock))) {
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
  if (evicted) OV_GUEST(path_release, (uint64_t)(uintptr_t)evicted);
  *out = obj;
  OV_RET_PTR(c, obj);
}

// ------------------------------------------------------------------------------------------------
// Batched DLC mounting (rsmacfixes batch_install_step): F_100a0d730 is the per-frame update of the DLC
// coordinator. It calls the install step F_100a0d7e0 (mounts one pending archive, or clears flag +0xa and
// marks enumeration done when the list is empty) once per frame; the mod redirected that call to a loop
// bounded by a frame budget. The step stays translated; the caller is replaced by this native version.
//
// The flag is what a player sees of all this: the menu shows "Enumerating downloadable content..." for as long as
// it is set (0x1000e07d4), and a song list on screen fills itself in every three seconds while it is, and once more
// when it clears (0x100345b00). But a step is a third of what it takes for an archive's songs to be in the lists:
//
//   1. The step opens the archive, asks for its song manifest (an AssetLoadRequest for its .hsan file,
//      0x10055e740) and puts its package in a queue (dlc_package_queue).
//   2. The update's head, which the game calls once a frame, takes one package off the queue (dlc_package) and
//      ends it when its files are in.
//   3. The asset loader thread reads the manifest, and the ManifestManager's handler (0x10055bc20) takes the
//      answer on the game's thread: the songs are in the lists from then. The thread's loop is a pass and a pause
//      of 10 ms, and a pass ends at the first file it finishes: 85 manifests a second at the most.
//
// Steps alone for a frame budget emptied the list of archives in a few frames and cleared the flag with the other
// two still to come, the lists short and the menus slow to answer meanwhile: 147 archives were open for 2.7 s
// more, and with 2,326 the manifests alone were 15 s behind the last package. So:
//
//   - The loop takes a step only when no package is left to load, and calls the head until then: a package is
//     loaded before the next archive is opened, in the original's order, several a frame.
//   - While archives are mounted the loader thread's pause is LOADER_BUSY_US (asset_loader_thread below), so the
//     manifests come in as fast as they are read.
//   - The step that finds the list empty ends the enumeration for the game (the profile's loading waits for that,
//     0x10035a6ff). If manifests asked for have not been answered by then, the flag is set again and held until
//     they have: the text stays, and a song list fills in, to the last song.
//
// And a step itself grew with the library. Mounting an archive, the game asks its file system for the hash file of
// each shader of its shader cache, "data/Shaders/Generated/NAME.sha1" (ten of them, 0x100088f96), should the
// archive bring a newer one; the file system asks every archive mounted there in turn, and a song archive never
// has one. So a step took 2 ms with a hundred archives mounted and 19 ms with 2,300: most of a 27 s mounting.
// Within one mounting, a question the step before had "no" for is asked of the archives this step mounted alone
// (fs_asked below): the others have not changed.
//
// Learn & Play's loop waits for each mount as the first of these does, for its own reason; the rest is open there
// (win-x86/shims/win32_overrides.c, docs/windows/PLAN.md).
// ------------------------------------------------------------------------------------------------
#define INSTALLING_FLAG RC_OFF(dlc_installing)
#define LOAD_STALL_S 5.0     // nothing loaded or answered for this long: the mounting goes on without waiting
#define LOADER_BUSY_US 200   // the loader thread's pause between passes while archives are mounted
#define LOADER_IDLE_US 10000 // and the game's own

// During the install steps an archive in dlc/ counts as owned, whatever AppID it carries: mods_shared.c's hook on
// steam_dlc_owned, one source with Learn & Play, which rs_mods_mounting tells when the steps run.

// RS_MODS_LOG=1 says what a mounting did once its list is empty, its packages are loaded and its manifests are in,
// in Learn & Play's words: the archives its steps took, the frames and the time. Learn & Play's steps dropped
// archives when several ran in a frame, until its loop waited for each mount to be finished
// (win-x86/shims/win32_overrides.c). This game's do not: it lists the same songs after a mounting with the budget
// as after one at RS_MODS_BUDGET_MS=0, a step a frame as in the original (docs/mac/TESTING.md has the check).
static struct {
  unsigned steps, frames;
  double t0;
  uint64_t left;        // the packages left to load when that last changed
  double left_since;    // and when that was
  unsigned asked0, answered0;  // the manifest counts when the mounting began
  unsigned answered;    // the answers when that last changed
  double answered_since;
  double t_mounted;     // when the list was empty and the last package loaded
  unsigned frames_mounted;  // and the frames until then
  int holding;          // since then: the flag is held for the manifests
  int stalled;          // something did not load: this mounting no longer waits
} g_mount;  // the game thread's

// The manifests asked for and answered since the game started, counted at the two mid points. A mount asks for its
// manifests by path, with no name; the handler is also given the answers to what the game loads by name (a
// profile's guitar and gear, urn:database:json-db:...), which are not counted. -1: a point is missing, and nothing
// waits for manifests.
static atomic_uint g_manifests_asked, g_manifests_answered;
static int g_manifest_points = -1, g_manifest_response;
static atomic_int g_loader_busy;  // archives are being mounted: read by the loader thread

static void on_manifest_request(rc_mid_frame *f, void *user) {
  (void)f, (void)user;
  COUNT(g_manifests_asked);
}
static void on_manifest_response(rc_mid_frame *f, void *user) {
  (void)user;
  const uint8_t *name = (const uint8_t *)(uintptr_t)rc_mid_get(f, g_manifest_response);
  if (!name) return;
  name += RC_OFF(asset_load_name);  // the game's string: its text in place, or behind a pointer at its start
  int in_place = RC_LD(uint64_t, name + RC_OFF(efd_str_inline)) == (uint64_t)(uintptr_t)(name + RC_OFF(efd_str_size));
  const char *text = in_place ? (const char *)name : (const char *)(uintptr_t)RC_LD(uint64_t, name);
  if (text && !*text) COUNT(g_manifests_answered);
}

// The asset loader thread's loop (the game's: a pass, 10 ms, until its stop byte is set). A pass of the
// AssetFactoryManager takes the requests that have come and goes through the loads it has in hand until one is
// finished, so with the game's pause a hundred files a second is the most it reads, however small. While archives
// are mounted the pause is LOADER_BUSY_US; at any other time the loop is the game's.
void RC_FN(asset_loader_thread)(rc_ctx *c) {
  OV_ARGS_OF(asset_loader_thread, a, c);
  uint8_t *self = OV_PTR(a, uint8_t);
  while (!*(volatile uint8_t *)(self + RC_OFF(asset_loader_stop))) {
    OV_VCALL(RC_LD(uint64_t, self + RC_OFF(asset_loader_manager)), RC_SLOT(asset_loader_pass));
    usleep(atomic_load_explicit(&g_loader_busy, memory_order_relaxed) ? LOADER_BUSY_US : LOADER_IDLE_US);
  }
  OV_VCALL((uint64_t)(uintptr_t)self, RC_SLOT(asset_loader_end));
  ov_ret_u64(c, 0);
}

// The file questions of a step (the fs_exists and fs_mount points). A step mounts its archive (fs_mount: where, and
// the archive's file system object) and then asks the whole file system its questions (fs_exists), the same ones
// in every step. One the step before had "no" for, with every archive mounted then, can only be "yes" now through
// what this step mounted: that is asked, with the game's own call on the archive (the whole file system's answer
// is that call on each archive in turn, 0x10015aa0d), and a "yes" goes to the whole file system after all, so the
// game goes on from the same answer either way. Nothing is taken as known across a mount that was not a step's, in
// a step that mounted at a place this does not know how to take a path from, or with an archive that does not
// answer for the appid.appid every archive of dlc/ has. When the mounting ends the whole file system is asked each
// question once more: an answer other than the last one given switches this off for the run, with a line in the
// log.
#define FS_QUESTIONS 16    // room for the questions a step asks (ten)
#define FS_PATH 160
#define FS_STEP_MOUNTS 4   // and for the archives it mounts (one)
#define FS_AT 32
static struct {
  int point, fs, path, found, answered;  // fs_exists: the point, its values and its continuation; point -1: none
  int m_path, m_fs;                      // fs_mount's values
  int off;                               // an answer was not the file system's: not in this run
  unsigned step;                         // the step of this mounting, from 1
  unsigned last;                         // and the last one that asked something
  uint64_t whole;                        // the whole file system's object
  struct {
    char path[FS_PATH];
    unsigned step;  // the step it was last "no" in; 0: free
  } no[FS_QUESTIONS];
  struct {
    uint64_t fs;
    char at[FS_AT];
  } mounted[FS_STEP_MOUNTS];  // what this step mounted, and where (lower case)
  int nmounted;               // -1: something this cannot answer for
  unsigned asked, alone;      // the questions of this mounting, and those the step's own archives answered
} g_fs = {.point = -1};  // the game thread's, in a step
static _Thread_local int t_in_step;  // this thread is in an install step
static atomic_int g_fs_stale;        // something was mounted outside a step

static void fs_forget(void) {
  for (int i = 0; i < FS_QUESTIONS; i++) g_fs.no[i].step = 0;
}

static void on_fs_mount(rc_mid_frame *f, void *user) {
  (void)user;
  if (!t_in_step) {
    atomic_store(&g_fs_stale, 1);
    return;
  }
  const char *at = (const char *)(uintptr_t)rc_mid_get(f, g_fs.m_path);
  const uint64_t *fsp = (const uint64_t *)(uintptr_t)rc_mid_get(f, g_fs.m_fs);
  uint64_t fs = fsp ? RC_LD(uint64_t, fsp) : 0;
  int n = g_fs.nmounted;
  size_t len = at ? strlen(at) : 0;
  if (n < 0 || n == FS_STEP_MOUNTS || !fs || !len || len >= FS_AT) {
    g_fs.nmounted = -1;
    return;
  }
  for (size_t i = 0; i < len; i++) {  // one name of the plain kind, as "data": a path is taken from it below
    char ch = at[i] >= 'A' && at[i] <= 'Z' ? (char)(at[i] + 32) : at[i];
    if (!((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_')) {
      g_fs.nmounted = -1;
      return;
    }
    g_fs.mounted[n].at[i] = ch;
  }
  g_fs.mounted[n].at[len] = 0;
  if (!((uint8_t)OV_VCALL(fs, RC_SLOT(fs_exists), (uint64_t)(uintptr_t) "appid.appid") & 1)) {
    g_fs.nmounted = -1;
    return;
  }
  g_fs.mounted[n].fs = fs;
  g_fs.nmounted = n + 1;
}

// 1 when an archive this step mounted has the file, by the path from where it is mounted
static int fs_in_step_mounts(const char *path) {
  for (int i = 0; i < g_fs.nmounted; i++) {
    size_t len = strlen(g_fs.mounted[i].at);
    if (strncmp(path, g_fs.mounted[i].at, len) || path[len] != '/' || !path[len + 1]) continue;  // not under it
    if ((uint8_t)OV_VCALL(g_fs.mounted[i].fs, RC_SLOT(fs_exists), (uint64_t)(uintptr_t)(path + len + 1)) & 1) return 1;
  }
  return 0;
}

static void on_fs_exists(rc_mid_frame *f, void *user) {
  (void)user;
  if (!t_in_step || g_fs.off) return;  // the game's own call
  const char *path = (const char *)(uintptr_t)rc_mid_get(f, g_fs.path);
  uint64_t whole = rc_mid_get(f, g_fs.fs);
  if (!path || !whole || strlen(path) >= FS_PATH) return;
  if (atomic_exchange(&g_fs_stale, 0)) fs_forget();
  g_fs.whole = whole;
  g_fs.last = g_fs.step;
  g_fs.asked++;
  int slot = -1, free_slot = -1;
  for (int i = 0; i < FS_QUESTIONS; i++) {
    if (!g_fs.no[i].step) free_slot = free_slot < 0 ? i : free_slot;
    else if (!strcmp(g_fs.no[i].path, path)) slot = i;
  }
  int found;
  if (slot >= 0 && g_fs.no[slot].step == g_fs.step - 1 && g_fs.nmounted > 0 && !fs_in_step_mounts(path)) {
    found = 0;
    g_fs.alone++;
  } else {
    found = (uint8_t)OV_VCALL(whole, RC_SLOT(fs_exists), (uint64_t)(uintptr_t)path) & 1;
  }
  if (found) {
    if (slot >= 0) g_fs.no[slot].step = 0;
  } else if (slot >= 0 || free_slot >= 0) {
    if (slot < 0) strcpy(g_fs.no[slot = free_slot].path, path);
    g_fs.no[slot].step = g_fs.step;
  }
  rc_mid_set(f, g_fs.found, (uint64_t)found);
  f->go = g_fs.answered;
}

// The mounting's end: every question last answered "no" to the whole file system once more.
static void fs_check(void) {
  if (g_fs.off || !g_fs.whole || atomic_load(&g_fs_stale)) return;
  for (int i = 0; i < FS_QUESTIONS; i++) {
    if (g_fs.no[i].step != g_fs.last) continue;
    if (!((uint8_t)OV_VCALL(g_fs.whole, RC_SLOT(fs_exists), (uint64_t)(uintptr_t)g_fs.no[i].path) & 1)) continue;
    g_fs.off = 1;
    rs_mods_logf("DLC: %s is there, and a step was told it was not: file questions go to the whole file system "
                 "from here on, and a restart mounts the archives with them",
                 g_fs.no[i].path);
  }
}

// The mid points of the mounting: the manifests' and the file system's.
static void mods_manifest_points(void) {
  int asked = rc_mid_add_name("manifest_request", on_manifest_request, NULL);
  int answered = rc_mid_add_name("manifest_response", on_manifest_response, NULL);
  g_manifest_response = answered < 0 ? -1 : rc_mid_value(answered, "response");
  g_manifest_points = asked >= 0 && g_manifest_response >= 0 ? 0 : -1;
  if (g_manifest_points < 0) rs_mods_logf("no points for the song manifests: the mounting does not wait for them");
  int mount = rc_mid_add_name("fs_mount", on_fs_mount, NULL);
  int exists = mount < 0 ? -1 : rc_mid_add_name("fs_exists", on_fs_exists, NULL);
  if (exists >= 0) {
    g_fs.m_path = rc_mid_value(mount, "path"), g_fs.m_fs = rc_mid_value(mount, "fs");
    g_fs.fs = rc_mid_value(exists, "fs"), g_fs.path = rc_mid_value(exists, "path");
    g_fs.found = rc_mid_value(exists, "found"), g_fs.answered = rc_mid_cont(exists, "answered");
    if (g_fs.m_path >= 0 && g_fs.m_fs >= 0 && g_fs.fs >= 0 && g_fs.path >= 0 && g_fs.found >= 0 && g_fs.answered > 0)
      g_fs.point = exists;
  }
  if (g_fs.point < 0) {
    g_fs.off = 1;
    rs_mods_logf("no points for the file system: a step asks all of it");
  }
}

// What is left to load of the archives the steps took: the packages in the queue, and the one the head has. 0 when
// there is none, and otherwise a number that changes whenever one is done.
static uint64_t mount_left(const uint8_t *self) {
  uint64_t now = RC_LD(uint64_t, self + RC_OFF(dlc_package));
  const uint64_t *queue = RC_HOST(RC_LD(uint64_t, RC_GA(dlc_package_queue)));  // begin, end
  uint64_t waiting = queue ? (RC_LD(uint64_t, queue + 1) - RC_LD(uint64_t, queue)) / 8 : 0;
  return now || waiting ? (waiting << 1 | (now != 0)) + 1 : 0;
}

static void mount_stall(const char *what) {
  g_mount.stalled = 1;
  rs_mods_logf("DLC: %s in %.0f s, the mounting goes on without waiting", what, LOAD_STALL_S);
}

// 1 while the steps have to wait for a package to be loaded. One that makes no progress for LOAD_STALL_S is taken
// to be none of this mounting's, and the rest of the steps go on as they did before the loop waited.
static int mount_loading(const uint8_t *self) {
  if (g_mount.stalled) return 0;
  uint64_t left = mount_left(self);
  double t = elapsed_s();
  if (left != g_mount.left) g_mount.left = left, g_mount.left_since = t;
  if (!left) return 0;
  if (t - g_mount.left_since < LOAD_STALL_S) return 1;
  mount_stall("a package did not load");
  return 0;
}

// 1 while manifests this mounting asked for have not been answered, with the same patience.
static int manifests_waiting(void) {
  if (g_mount.stalled || g_manifest_points < 0) return 0;
  unsigned answered = atomic_load(&g_manifests_answered);
  int left = (int)(atomic_load(&g_manifests_asked) - g_mount.asked0) - (int)(answered - g_mount.answered0);
  double t = elapsed_s();
  if (answered != g_mount.answered) g_mount.answered = answered, g_mount.answered_since = t;
  if (left <= 0) return 0;
  if (t - g_mount.answered_since < LOAD_STALL_S) return 1;
  mount_stall("a song manifest was not answered");
  return 0;
}

// One install step. 1 when it took an archive: the step that finds the list empty clears the flag and installs
// nothing.
static int mount_step(uint8_t *self, uint64_t o) {
  rs_mods_mounting(1);
  g_fs.step++;
  g_fs.nmounted = 0;
  t_in_step = rs_mods_cfg.fixes;
  OV_GUEST(dlc_install_step, o);
  t_in_step = 0;
  rs_mods_mounting(0);
  if (!*(volatile uint8_t *)(self + INSTALLING_FLAG)) return 0;
  g_mount.steps++;
  return 1;
}

void RC_FN(dlc_update)(rc_ctx *c) {
  OV_ARGS_OF(dlc_update, a, c);
  uint8_t *self = OV_PTR(a, uint8_t);
  uint64_t o = (uint64_t)(uintptr_t)self;
  int installing = *(volatile uint8_t *)(self + INSTALLING_FLAG);
  int batched = installing && rs_mods_cfg.fixes && rs_mods_cfg.budget_s > 0;
  if (installing && !g_mount.frames++) {
    g_mount.t0 = elapsed_s();
    g_mount.asked0 = atomic_load(&g_manifests_asked);
    g_mount.answered0 = atomic_load(&g_manifests_answered);
    fs_forget();
    g_fs.step = g_fs.last = g_fs.asked = g_fs.alone = 0;
  }
  atomic_store_explicit(&g_loader_busy, batched, memory_order_relaxed);
  OV_GUEST(dlc_update_head, o);
  // --- the patched call (0x100a0d742) ---
  if (!batched) {
    mount_step(self, o);
  } else if (g_mount.holding) {
    if (!manifests_waiting()) self[INSTALLING_FLAG] = 0;  // the end the game's step made, now for the menus too
  } else {
    COUNT(g_frames);
    double t0 = elapsed_s();
    do {
      // The head again for the package of the step before, until it is loaded (a call or two each, as run). One
      // that is not by the end of the budget is waited for in the next frame, without a step.
      while (mount_loading(self) && elapsed_s() - t0 < rs_mods_cfg.budget_s) {
        uint64_t left = mount_left(self);
        OV_GUEST(dlc_update_head, o);
        if (mount_left(self) == left) usleep(50);
      }
      if (mount_loading(self)) break;
      if (mount_step(self, o)) COUNT(g_installed);
    } while (*(volatile uint8_t *)(self + INSTALLING_FLAG) && elapsed_s() - t0 < rs_mods_cfg.budget_s);
    if (!*(volatile uint8_t *)(self + INSTALLING_FLAG)) {
      fs_check();
      g_mount.t_mounted = g_mount.answered_since = elapsed_s();
      g_mount.frames_mounted = g_mount.frames;
      if (manifests_waiting()) self[INSTALLING_FLAG] = g_mount.holding = 1;
    }
  }
  if (installing && !*(volatile uint8_t *)(self + INSTALLING_FLAG)) {
    double t = elapsed_s();
    if (g_mount.holding)
      rs_mods_logf("DLC: %u archives mounted in %u frames, %.2f s, their songs in the lists %.2f s later",
                   g_mount.steps, g_mount.frames_mounted, g_mount.t_mounted - g_mount.t0, t - g_mount.t_mounted);
    else
      rs_mods_logf("DLC: %u archives mounted in %u frames, %.2f s", g_mount.steps, g_mount.frames, t - g_mount.t0);
    if (g_fs.asked)
      rs_mods_logf("DLC: %u of the steps' %u file questions asked of the step's archive alone", g_fs.alone, g_fs.asked);
    memset(&g_mount, 0, sizeof g_mount);
  }
  if (!self[RC_OFF(dlc_first_done)]) self[RC_OFF(dlc_first_done)] = 1;
  if (self[RC_OFF(dlc_check_on)] && self[RC_OFF(dlc_pending)]) {
    if ((uint8_t)OV_GUEST(dlc_enum_check, o)) {
      self[RC_OFF(dlc_pending)] = 0;
      self[RC_OFF(dlc_tail_done)] = 0;
    }
  }
  if (!self[RC_OFF(dlc_tail_done)]) OV_GUEST(dlc_update_tail, o);
  if (self[RC_OFF(dlc_clear_map)]) {
    if (!self[INSTALLING_FLAG] && *(uint64_t *)(self + RC_OFF(dlc_map_size))) {  // clear the std::map
      OV_GUEST(std_tree_destroy, o + RC_OFF(dlc_map), *(uint64_t *)(self + RC_OFF(dlc_map_root)));
      *(uint64_t *)(self + RC_OFF(dlc_map_begin)) = o + RC_OFF(dlc_map);
      *(uint64_t *)(self + RC_OFF(dlc_map_root)) = 0;
      *(uint64_t *)(self + RC_OFF(dlc_map_end)) = o + RC_OFF(dlc_map);
      *(uint64_t *)(self + RC_OFF(dlc_map_size)) = 0;
    }
    self[RC_OFF(dlc_clear_map)] = 0;
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
#define NPRIMES 28
#define ADDREF RC_SLOT(json_number_add_ref)
#define RELEASE RC_SLOT(json_number_release)

static inline uint64_t *table(void) { return (uint64_t *)(uintptr_t)RC_LD(uint64_t, RC_GA(json_number_table)); }

// The game's hash. `insert` selects the compare of F_1000f8f10 (v >= 2^63), the other two use
// 2^63 <= v or unordered; the NaN case differs between them (never reached: JSON numbers).
static uint64_t hash_orig(double v, int insert) {
  double k = RC_LD(double, RC_GA(const_two_pow_63));
  int big = insert ? (v >= k) : (k <= v || v != v);
  return big ? (uint64_t)rc_cvtt_f64_i64(v - k) ^ 0x8000000000000000ull : (uint64_t)rc_cvtt_f64_i64(v);
}
static uint64_t hash_num(double v, int insert) {
  if (!rs_mods_cfg.fixes) return hash_orig(v, insert);
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
  const uint64_t *P = (const uint64_t *)(uintptr_t)RC_GA(hashtable_prime_sizes);
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

void RC_FN(num_rehash)(rc_ctx *c) {
  OV_ARGS_OF(num_rehash, a, c);
  uint64_t *T = OV_PTR(a, uint64_t);
  uint64_t want = OV_U64(a);
  num_rehash(T, want);
  ov_ret_void(c);
}

// F_1000f8f10(out, double v): *out = the number object for v, from the table or newly created and
// inserted. The reference-count/virtual calls (slots 0 and 1 of the object: AddRef/Release inlined
// smart-pointer traffic) are made in the same order as the game's.
void RC_FN(num_intern)(rc_ctx *c) {
  OV_ARGS_OF(num_intern, a, c);
  uint64_t *out = OV_PTR(a, uint64_t);
  double v = OV_F64(a);
  pthread_mutex_t *m = (pthread_mutex_t *)(uintptr_t)RC_LD(uint64_t, RC_GA(json_number_table_lock));
  pthread_mutex_lock(m);
  *(pthread_t *)((char *)m + RC_OFF(rmutex_owner)) = pthread_self();
  (*(int *)((char *)m + RC_OFF(rmutex_depth)))++;
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
      OV_VCALL(obj, ADDREF);
      *out = obj;
      OV_VCALL(obj, ADDREF);
      OV_VCALL(obj, RELEASE);
    }
  } else {
    uint64_t obj = OV_GUEST(efd_alloc, 0x18, 0);
    OV_GUEST(json_number_base_ctor, obj);
    int flag = 0;
    if (obj) {
      RC_ST(uint64_t, obj, RC_GA(json_number_vtable));
      RC_ST(double, obj + RC_OFF(json_number_value), v);
      atomic_fetch_add_explicit((_Atomic int32_t *)(uintptr_t)(obj + RC_OFF(json_number_refs)), 1, memory_order_seq_cst);  // OSAtomicAdd32
      *out = obj;
      OV_VCALL(obj, ADDREF);
      OV_VCALL(obj, RELEASE);
      T = table();
      OV_VCALL(obj, ADDREF);
      OV_VCALL(obj, ADDREF);
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
    if (obj) OV_VCALL(obj, RELEASE);
    if (!flag) OV_VCALL(obj, RELEASE);
  }
  m = (pthread_mutex_t *)(uintptr_t)RC_LD(uint64_t, RC_GA(json_number_table_lock));
  (*(int *)((char *)m + RC_OFF(rmutex_depth)))--;
  pthread_mutex_unlock(m);
  OV_RET_PTR(c, out);
}

// F_1000f8d20(this): the number object's destructor: set the vtable, remove its node(s) from the table, run
// the base destructor (a tail call).
void RC_FN(num_dtor)(rc_ctx *c) {
  OV_ARGS_OF(num_dtor, a, c);
  uint8_t *self = OV_PTR(a, uint8_t);
  RC_ST(uint64_t, self, RC_GA(json_number_vtable));
  uint64_t *T = table();
  double v;
  memcpy(&v, self + RC_OFF(json_number_value), 8);
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
          memcpy(&v, self + RC_OFF(json_number_value), 8);
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
  uint64_t r = OV_GUEST(json_number_base_dtor, (uint64_t)(uintptr_t)self);
  ov_ret_u64(c, r);
}

// ------------------------------------------------------------------------------------------------
// RSBypass: F_1001eb6a0 (SNG loader) calls the DSA verifier F_10137683d(data, len, keyHex) at 0x1001eb763
// (return address 0x1001eb768) and, if the result is false, abandons the file; the mod NOPs that
// `test r14b,r14b; je` (9 bytes at 0x1001eb784) so unsigned custom songs load. The verifier has other
// callers (package config 0x1002a7550), so the bypass is made only for the SNG loader's call, found by the
// return address the translated caller pushed. With RS_MODS_BYPASS=0 the real verifier runs. The verifier
// itself is the crypto module's adapter, built under another name (build.sh renames it to rs_crypto_dsa_verify).
// ------------------------------------------------------------------------------------------------
extern void rs_crypto_dsa_verify(rc_ctx *c);  // the crypto module's adapter (build.sh renames it)

void RC_FN(sng_verify)(rc_ctx *c) {
  if (rs_mods_cfg.bypass && RC_RET_ADDR(c) == RC_GA(sng_load_verify_ret)) {
    COUNT(g_bypassed);
    ov_ret_u32(c, 1);
    return;
  }
  rs_crypto_dsa_verify(c);
}

// ------------------------------------------------------------------------------------------------
// Auto Save warning: a pre hook on the UI event handler (autosave_warning), which takes
// (self, category, event). With category "UI" and event "OnSignInFlowComplete" the handler asks the
// sign-in state (autosave_signin_query, predicate slot 0x1f): when that is false it builds the
// RS2AutosaveWarning dialog (string setup plus the navigate call 0x100306ac0 at 0x1005e7f92), stores
// the menu at self+0x418 and writes 4.0 to the state-machine timer at self+0x428. When the predicate
// is true it tail-calls the MP_Options controller 0x1005e7c60; other events return or tail-call the
// compare for "OnSignInFlowError".
//
// Before the dialog the same branch calls input_mode_restore: it puts back the player's cached input mode
// (CachedInputMode_<user id>), gives a finished profile with no InputMode the first one, and otherwise
// applies the InputMode to the input system. Nothing else on the way to the main menu does, so the hook
// calls it too.
//
// The x64 patches this ports NOP the navigate call (so the dialog is never constructed) and change the
// timer from 4.0 to 0.0 (so the state machine advances at once instead of waiting). The hook does the
// same, except for what it leaves in the created-dialog slot: the handler (0x1005e48b0) only runs the
// advance past the warning while that slot is non-NULL and the timer is at most zero (it dismisses the
// dialog by name, drops the slot's reference, calling the object's release only if the count reaches
// zero, and continues). A NULL slot, which the skipped call's never-written out-parameter would leave,
// stalls the state machine on the profile screen, so the hook stores a token object instead: guest heap
// memory whose reference count starts high, so the handler's release never fires. The dialog string
// itself is skipped with the call: it only feeds the skipped call, allocated and freed within the
// function. Returning nonzero skips the original; every other case returns zero, so a true predicate
// still reaches the MP_Options controller and other events behave as the game has them.
// With RS_MODS_AUTOSAVE=0 the hook returns zero at once.
// ------------------------------------------------------------------------------------------------
// Token for the created-dialog slot (see above): guest memory with a high reference count at +0x8, so the
// handler's release never calls through it. Reused for the life of the process.
static void *g_autosave_token;
static int autosave_token_ready(void) {
  if (g_autosave_token) return 1;
  uint64_t tok = OV_GUEST(efd_alloc, 0x18, 0);
  if (!tok) return 0;
  RC_ST(uint64_t, tok, 0);
  RC_ST(int32_t, tok + 8, 1000000);
  g_autosave_token = (void *)(uintptr_t)tok;
  return 1;
}

static int before_autosave_warning(rc_ctx *c, void *user) {
  (void)user;
  if (!rs_mods_cfg.autosave) return 0;
  OV_ARGS(a, c);
  uint8_t *self = OV_PTR(a, uint8_t);
  const char *cat = OV_PTR(a, const char);
  const char *ev = OV_PTR(a, const char);
  if (strncasecmp(cat, "UI", 3) != 0 || strncasecmp(ev, "OnSignInFlowComplete", 21) != 0) return 0;
  uint64_t svc = OV_GUESTV(autosave_signin_query, NULL, 0);
  if ((uint8_t)OV_VCALL(svc, RC_SLOT(autosave_query),
                        (uint64_t)RC_LD(uint32_t, RC_GA(autosave_query_arg))) != 0)
    // the MP_Options branch: the original takes it (evaluating the predicate again, a state read)
    return 0;
  void **slot = (void **)(self + RC_OFF(autosave_menu));
  if (*slot == NULL && !autosave_token_ready()) return 0;  // out of memory: the original shows the dialog
  OV_GUEST(input_mode_restore, OV_GUESTV(service_manager_holder, NULL, 0));
  // A foreign object already in the slot is left alone: the zeroed timer below hurries it the same way.
  if (*slot == NULL) *slot = g_autosave_token;
  RC_ST(double, self + RC_OFF(autosave_timer), 0.0);
  rs_mods_logf("auto save warning skipped");
  ov_ret_void(c);
  return 1;
}

const ov_entry ov_mods_entries[] = {
    {RC_ADDR(num_dtor), "num_dtor", RC_FN(num_dtor)},
    {RC_ADDR(num_intern), "num_intern", RC_FN(num_intern)},
    {RC_ADDR(num_rehash), "num_rehash", RC_FN(num_rehash)},
    {RC_ADDR(path_md5), "path_md5", RC_FN(path_md5)},
    {RC_ADDR(dlc_update), "dlc_update", RC_FN(dlc_update)},
    {RC_ADDR(key_down), "key_down", RC_FN(key_down)},
    {RC_ADDR(key_up), "key_up", RC_FN(key_up)},
    {RC_ADDR(thread_set_priority), "thread_set_priority", RC_FN(thread_set_priority)},
    {RC_ADDR(sng_verify), "sng_verify", RC_FN(sng_verify)},
};
const int OV_MODS_COUNT = (int)(sizeof ov_mods_entries / sizeof ov_mods_entries[0]);

// ------------------------------------------------------------------------------------------------
// Cache overlay (the Direct Connect choice). The game opens the archives inside cache.psarc (cache0.7z ...
// cache8.7z; the menu data is in one of them) by name through FrontEnd_OpenFile (0x1003c7810), which finds
// them in cache.psarc, mounted at the root of the VFS (0x1003c9682). The installer's overlay
// (tools/play-native/cache_overlay.py) is <game>/Update/patcharchives.psarc, the patch container the engine
// mounts at PatchUpdate/ itself when the file is there (0x1003c8cd1, "%spatcharchives.psarc"). It holds new
// copies of the cacheN.7z archives whose menu files it changes, made from the user's own cache.psarc. The
// engine's patch layer takes only its own .new/.delta archives from there, so for a cacheN.7z this opens
// PatchUpdate/<name> first, and the name itself when the overlay has no such archive (or there is no
// overlay). cache.psarc is never changed. The pre hook calls the hook point again, which passes the patch
// path through unchanged.
// ------------------------------------------------------------------------------------------------

static int is_cache_7z(const char *p) {
  if (strncasecmp(p, "cache", 5) != 0) return 0;
  p += 5;
  if (*p < '0' || *p > '9') return 0;
  while (*p >= '0' && *p <= '9') p++;
  return strcasecmp(p, ".7z") == 0;
}

// Two players on any input. A player's input mode and input live in their profile, but the Multiplayer screens
// ask whether Player 2 is in Real Tone mode (input_mode_is_cable: its only callers) and, when not, set them to it, so
// the game's rule is two cables. And a player's saved input is restored for Player 1 alone, after the profile screen;
// Player 2 takes the first slot free. So here a profile on Direct Connect or the microphone is taken to be in the
// right mode. The other half, Player 2's saved input found before the game looks for a free slot, is one source
// with Learn & Play (before_input_bind, mods_shared.c); this half is not, as Learn & Play has the check inlined
// into a function of its own (win32_overrides.c). RS_MODS_MULTIPLAYER=0 is the original.
static int before_is_cable(rc_ctx *c, void *user) {
  (void)user;
  OV_ARGS(a, c);
  if (!rs_mods_cfg.multiplayer || OV_U32(a) != RS_MODS_PLAYER_2 ||
      !rs_mods_any_input((uint8_t)OV_GUEST(input_mode, RS_MODS_PLAYER_2)))
    return 0;
  ov_ret_u32(c, 1);
  return 1;
}

static int before_open_file(rc_ctx *c, void *user) {
  (void)user;
  OV_ARGS(a, c);
  uint64_t out = OV_U64(a);
  const char *path = OV_PTR(a, const char);
  if (!rs_mods_cfg.overlay || !out || !path || !is_cache_7z(path)) return 0;
  char alt[64];
  if (snprintf(alt, sizeof alt, "patchupdate/%s", path) >= (int)sizeof alt) return 0;
  OV_GUEST(frontend_open_file, out, (uint64_t)(uintptr_t)alt);
  if (!*(uint64_t *)(uintptr_t)out) return 0;  // not in the overlay: the original opens it from cache.psarc
  COUNT(g_overlaid);
  rs_mods_logf("cache overlay: %s from Update/patcharchives.psarc", path);
  ov_ret_u64(c, out);
  return 1;
}
