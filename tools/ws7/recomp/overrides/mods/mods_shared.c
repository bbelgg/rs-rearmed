// The built-in fixes' part that is one source for both editions (mods_shared.h): the switches, the log, the start, and
// what the two batched DLC mountings share.
// A fix that needs nothing of one game alone is here too, written as a mod is (rc_mod.h): it finds the game's
// functions and fields by name when it runs, so the same object is right for either game. Built into both apps
// (overrides/mods/build.sh, win-x86/build/build_shims.sh).
#include <ctype.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include "rc_mod.h"
#include "rc_nofile.h"

#include "framerate.h"
#include "mods_shared.h"

// ------------------------------------------------------------------------------------------------
// Configuration
// ------------------------------------------------------------------------------------------------
rs_mods_config rs_mods_cfg = {.bypass = 1, .fixes = 1, .autosave = 1, .multiplayer = 1, .budget_s = 0.050,
                              .intro = 1, .overlay = 1, .guitarcade_skip = 1, .menu_retry = 1, .arrangements = 1};

int rs_mods_env_on(const char *name, int dflt) {
  const char *v = getenv(name);
  if (!v || !*v) return dflt;
  return !(v[0] == '0' || v[0] == 'n' || v[0] == 'N' || v[0] == 'f' || v[0] == 'F');
}

void rs_mods_reload_config(void) {
  rs_mods_cfg.log = rs_mods_env_on("RS_MODS_LOG", 0);
  rs_mods_cfg.bypass = rs_mods_env_on("RS_MODS_BYPASS", 1);
  rs_mods_cfg.fixes = rs_mods_env_on("RS_MODS_FIXES", 1);
  rs_mods_cfg.intro = rs_mods_env_on("RS_MODS_INTRO", 1);
  rs_mods_cfg.overlay = rs_mods_env_on("RS_MODS_OVERLAY", 1);
  rs_mods_cfg.autosave = rs_mods_env_on("RS_MODS_AUTOSAVE", 1);
  rs_mods_cfg.multiplayer = rs_mods_env_on("RS_MODS_MULTIPLAYER", 1);
  rs_mods_cfg.guitarcade_skip = rs_mods_env_on("RS_MODS_GUITARCADE_SKIP", 1);
  rs_mods_cfg.menu_retry = rs_mods_env_on("RS_MODS_MENU_RETRY", 1);
  rs_mods_cfg.arrangements = rs_mods_env_on("RS_MODS_ARRANGEMENTS", 1);
  rs_mods_cfg.budget_s = 0.050;
  const char *b = getenv("RS_MODS_BUDGET_MS");
  if (b && *b) {
    char *end;
    double ms = strtod(b, &end);
    if (end != b && ms >= 0 && ms <= 1000) rs_mods_cfg.budget_s = ms / 1000;
  }
}
const rs_mods_config *rs_mods_config_get(void) { return &rs_mods_cfg; }

// The line is written whole: the game's threads and the mods log to the same stream, and a line of theirs between
// the prefix and the text would hide both from whoever reads the log by its line starts.
void rs_mods_logf(const char *fmt, ...) {
  if (!rs_mods_cfg.log) return;
  va_list ap;
  va_start(ap, fmt);
  flockfile(stderr);
  fputs("[rs-mods] ", stderr);
  vfprintf(stderr, fmt, ap);
  fputc('\n', stderr);
  funlockfile(stderr);
  va_end(ap);
}

// ------------------------------------------------------------------------------------------------
// The cache overlay's names (mods_shared.h). Each edition's hook on FrontEnd_OpenFile opens the name first and the
// path itself when the overlay has no such file, so a menu movie the overlay doesn't hold opens as before, through a
// mod's later hook too (rearmed-mods serves three movies of its own). A path in the overlay already is no one's: the
// Mac's hook opens the name through the hook point again.
// ------------------------------------------------------------------------------------------------
static int is_cache_7z(const char *p) {
  if (strncasecmp(p, "cache", 5) != 0) return 0;
  p += 5;
  if (*p < '0' || *p > '9') return 0;
  while (*p >= '0' && *p <= '9') p++;
  return strcasecmp(p, ".7z") == 0;
}

static int is_sep(char c) { return c == '/' || c == '\\'; }

// The menu movie's name in path, from "gfxassets": ...gfxassets\views\<name>.gfx, <name> without a separator; NULL
static const char *views_movie(const char *path) {
  for (const char *p = path; *p; p++) {
    if ((p != path && !is_sep(p[-1])) || strncasecmp(p, "gfxassets", 9) || !is_sep(p[9]) ||
        strncasecmp(p + 10, "views", 5) || !is_sep(p[15]))
      continue;
    const char *name = p + 16;
    size_t k = strlen(name);
    if (k <= 4 || strcasecmp(name + k - 4, ".gfx") || strpbrk(name, "/\\")) return NULL;
    return p;
  }
  return NULL;
}

int rs_mods_overlay_name(const char *path, char *alt, size_t n) {
  static const char pre[] = "patchupdate/";
  const size_t pn = sizeof pre - 1;
  if (!path || !alt || !strncasecmp(path, pre, pn)) return 0;
  if (is_cache_7z(path)) return snprintf(alt, n, "%s%s", pre, path) < (int)n;
  const char *m = views_movie(path);
  size_t k = m ? strlen(m) : 0;
  if (!m || pn + k + 1 > n) return 0;
  memcpy(alt, pre, pn);
  for (size_t i = 0; i <= k; i++) alt[pn + i] = m[i] == '\\' ? '/' : (char)tolower((unsigned char)m[i]);
  return 1;
}

// ------------------------------------------------------------------------------------------------
// OS-level fix: open-file limit (rsmacfixes fix 4), which the batched DLC mounting needs: a large library has
// more archives open than the 256 descriptors of an app started from Finder.
// ------------------------------------------------------------------------------------------------
uint64_t rs_mods_raise_nofile(void) {
  uint64_t old;
  int failed;
  uint64_t now = rc_raise_nofile(&old, &failed);
  if (now != old || failed)
    rs_mods_logf("open-file limit %llu -> %llu%s", (unsigned long long)old, (unsigned long long)now,
                 failed ? " (setrlimit failed)" : "");
  return now;
}

// ------------------------------------------------------------------------------------------------
// No intro screens. RSBootService (RSBootService.cpp) boots in phases (u32 at boot_phase): 0 and 1 start the
// services and the preload groups; 2 ("OnInit_Intro") waits for the shader, database, GE, UI, reactive-effects
// and fork services; 3 runs boot_phase3_steps steps (u32 counter at boot_step: the patch and other archives
// mounted, the venues) and then loads the start-up sound bank and, once the intro is over, pushes the title
// screen. Phases 2 and 3 first tick the intro (boot_intro_update: the Scaleform intro movie with the logos,
// legal screens and intro video) and wait while it returns true. Its state (u32 at boot_intro_state): 0 load
// the movie's model, 1 wait for it, 2 wait for the movie to start (busy), 3 play (not busy: the boot goes on
// meanwhile), 4 end (release the model, state 5, start the boot service's 3-second tail timer), 5 done.
//
// The game's own skip (in phase 2, just before the tick: a key press once the movie is on its last screen) sets
// state 4 and clears the playing flag (boot_intro_playing). The skip here takes the same transition, without
// the movie ever playing: the model is loaded as usual (states 0 and 1); then, instead of starting the movie,
// the tick reports "not busy" (as it does while the movie plays) until phase 3 has done its steps, and then
// ends the intro. Both conditions are needed, measured on the Mac build: without the model loaded, or with
// the intro ended before phase 3's steps, the title screen never gets its menu movie and the game sits on an
// empty screen (the stall that RSMods' short "fast load" intros also run into, see its QualityOfLife.cpp).
// The screen is white, then black for a few seconds while the title loads. Only during boot (phases 2 and 3).
// With RS_MODS_INTRO=0 the intro plays.
//
// Both games have the tick as a function of its own with the same states and the same fields (each one's
// offsets.map says where they were read), so this is a pre hook on it for both: Learn & Play's is a stdcall
// function, and the hook sees the Mac's call (rc_hook.h).
// ------------------------------------------------------------------------------------------------
static int before_intro_update(rc_ctx *c, void *user) {
  (void)user;
  if (!rs_mods_cfg.intro) return 0;
  OV_ARGS(a, c);
  uint8_t *boot = OV_PTR(a, uint8_t);
  uint32_t phase = RC_LD(uint32_t, boot + RC_OFF(boot_phase));
  uint32_t state = RC_LD(uint32_t, boot + RC_OFF(boot_intro_state));
  if ((phase != 2 && phase != 3) || state < 2 || state >= 4) return 0;  // loading the model: the original
  if (phase == 2 || RC_LD(uint32_t, boot + RC_OFF(boot_step)) < (uint32_t)RC_OFF(boot_phase3_steps)) {
    ov_ret_u32(c, 0);  // not busy, the movie not started
    return 1;
  }
  rs_mods_logf("intro: skipped");
  RC_ST(uint32_t, boot + RC_OFF(boot_intro_state), 4);  // the game's skip: the original ends the intro (state 5)
  boot[RC_OFF(boot_intro_playing)] = 0;                 // and starts the tail timer
  return 0;
}

// ------------------------------------------------------------------------------------------------
// Song ownership during a mount. Mounting an archive asks whether its AppID is in the DLC list the game made
// once at start (steam_dlc_owned), so no answer of the Steam layer can cover a song added while the game runs
// (shims/steam/src/enumerate.h). During the install step of the DLC coordinator's update an archive in dlc/
// counts as owned, whatever AppID it carries; every other caller gets the game's answer, and RS_MODS_BYPASS=0
// asks the list during a mount too. The update is each edition's own (their batched mounting loops differ:
// mods.c, win-x86/shims/win32_overrides.c), and says here when it is in a step (rs_mods_mounting, below).
// ------------------------------------------------------------------------------------------------
static _Thread_local int t_mounting;  // this thread is in an install step: another thread's question is the game's

static int before_dlc_owned(rc_ctx *c, void *user) {
  (void)user;
  if (!t_mounting || !rs_mods_cfg.bypass) return 0;
  ov_ret_u32(c, 1);
  return 1;
}

// ------------------------------------------------------------------------------------------------
// Batched DLC mounting (rsmacfixes batch_install_step), the part that is one source. The DLC coordinator's
// per-frame update calls its install step once: the step mounts one pending archive, or, with the list empty, clears
// the coordinator's installing flag and marks the enumeration done. Each edition replaces the update by a native one
// that runs the step in a loop bounded by a frame budget (mods.c, win-x86/shims/win32_overrides.c: the loops differ in
// how they wait for a mount to be finished) and calls what is here around it.
//
// The flag is what a player sees of all this: the menu shows "Enumerating downloadable content..." for as long as
// it is set, and a song list on screen fills itself in every three seconds while it is, and once more when it
// clears. But a step is a third of what it takes for an archive's songs to be in the lists:
//
//   1. The step opens the archive, asks for its song manifest (an AssetLoadRequest for its .hsan file) and leaves
//      the rest of the mount to the update's head.
//   2. The update's head, which the game calls once a frame, finishes one mount a call.
//   3. The asset loader thread reads the manifest, and the ManifestManager's handler takes the answer on the game's
//      thread: the songs are in the lists from then. The thread's loop is a pass and a pause of 10 ms, and a pass
//      ends at the first file it finishes: 85 manifests a second at the most.
//
// Steps alone for a frame budget emptied the list of archives and cleared the flag with the manifests still to come,
// the lists short meanwhile: with 2,326 archives the Mac's manifests were 15 s behind its last package. So:
//
//   - Each edition's loop has the head finish a mount before the next step.
//   - While archives are mounted the loader thread's pause is LOADER_BUSY_US (rs_mods_loader_pause_us, which each
//     edition's replacement of the thread's loop asks), so the manifests come in as fast as they are read.
//   - The step that finds the list empty ends the enumeration for the game (the profile's loading waits for that).
//     If manifests asked for have not been answered by then, the flag is set again and held until they have: the
//     text stays, and a song list fills in, to the last song.
//
// And a step itself grew with the library. Mounting an archive, the game asks its file system for the hash file of
// each shader of its shader cache, "data/Shaders/Generated/NAME.sha1" (ten of them), should the archive bring a
// newer one; the file system asks every archive mounted there in turn, and a song archive never has one. So a step
// of the Mac game took 2 ms with a hundred archives mounted and 19 ms with 2,300: most of a 27 s mounting. Within
// one mounting, a question the step before had "no" for is asked of the archives this step mounted alone (fs_asked
// below): the others have not changed.
//
// RS_MODS_LOG=1 says what a mounting did once its list is empty, its mounts are finished and its manifests are in:
// the archives its steps took, the frames and the time.
// ------------------------------------------------------------------------------------------------
#define LOAD_STALL_S 5.0     // nothing loaded or answered for this long: the mounting goes on without waiting
#define LOADER_BUSY_US 200   // the loader thread's pause between passes while archives are mounted
#define LOADER_IDLE_US 10000 // and the game's own
#define COUNT(x) atomic_fetch_add_explicit(&(x), 1, memory_order_relaxed)

static double now_s(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (double)t.tv_sec + (double)t.tv_nsec * 1e-9;
}

static struct {
  unsigned steps, frames;
  double t0;
  uint64_t left;        // what an edition's loop has left to load, when that last changed
  double left_since;    // and when that was
  unsigned asked0, answered0;  // the manifest counts when the mounting began
  unsigned answered;    // the answers when that last changed
  double answered_since;
  double t_mounted;     // when the list was empty and the last mount finished
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
// Archives are being mounted: when the last update said so, in ms of now_s (0: not), read by the loader thread. An
// update that stopped coming in the middle of a mounting leaves the thread its short pause for LOADER_BUSY_HOLD_S.
static _Atomic uint64_t g_loader_busy;
#define LOADER_BUSY_HOLD_S 0.5

static void on_manifest_request(rc_mid_frame *f, void *user) {
  (void)f, (void)user;
  COUNT(g_manifests_asked);
}
static void on_manifest_response(rc_mid_frame *f, void *user) {
  (void)user;
  if (g_manifest_points < 0) return;
  const uint8_t *name = (const uint8_t *)(uintptr_t)rc_mid_get(f, g_manifest_response);
  if (!name) return;
  name += RC_OFF(asset_load_name);  // the game's string: its text in place, or behind a pointer at its start
  int in_place = rc_mod_ptr(name + RC_OFF(efd_str_inline)) == (const void *)(name + RC_OFF(efd_str_size));
  const char *text = in_place ? (const char *)name : (const char *)rc_mod_ptr(name);
  if (text && !*text) COUNT(g_manifests_answered);
}

// The asset loader thread's pause between two passes, in microseconds. The thread's loop is the game's: a pass of
// the AssetFactoryManager, 10 ms, until its stop byte is set; a pass takes the requests that have come and goes
// through the loads it has in hand until one is finished, so with the game's pause a hundred files a second is the
// most it reads, however small. Each edition replaces the loop by the same one with this as its pause
// (asset_loader_thread): LOADER_BUSY_US while archives are mounted, the game's own at any other time.
unsigned rs_mods_loader_pause_us(void) {
  uint64_t busy = atomic_load_explicit(&g_loader_busy, memory_order_relaxed);
  return busy && now_s() * 1000.0 - (double)busy < LOADER_BUSY_HOLD_S * 1000.0 ? LOADER_BUSY_US : LOADER_IDLE_US;
}

// The file questions of a step (the fs_exists and fs_mount points). A step mounts its archive (fs_mount: where, and
// the archive's file system object) and then asks the whole file system its questions (fs_exists), the same ones
// in every step. One the step before had "no" for, with every archive mounted then, can only be "yes" now through
// what this step mounted: that is asked, with the game's own call on the archive (the whole file system's answer
// is that call on each archive in turn), and a "yes" goes to the whole file system after all, so the game goes on
// from the same answer either way. Nothing is taken as known across a mount that was not a step's, in a step that
// mounted at a place this does not know how to take a path from, or with an archive that does not answer for the
// appid.appid every archive of dlc/ has. When the mounting ends the whole file system is asked each question once
// more: an answer other than the last one given switches this off for the run, with a line in the log.
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
  char *control;              // "appid.appid", in memory the game can be given
} g_fs = {.point = -1};  // the game thread's, in a step
static _Thread_local int t_in_step;  // this thread is in an install step
static atomic_int g_fs_stale;        // something was mounted outside a step

static void fs_forget(void) {
  for (int i = 0; i < FS_QUESTIONS; i++) g_fs.no[i].step = 0;
}

// The game's own question of one file system object, the whole one or an archive's: has it this file (the path in
// the game's memory)
static int fs_has(uint64_t fs, const char *path) {
  return (uint8_t)OV_VCALL(fs, RC_SLOT(fs_exists), (uint64_t)(uintptr_t)path) != 0;
}

static void on_fs_mount(rc_mid_frame *f, void *user) {
  (void)user;
  if (g_fs.off) return;
  if (!t_in_step) {
    atomic_store(&g_fs_stale, 1);
    return;
  }
  // a "no" this step already had was the answer without this archive: asked of the whole file system again
  for (int i = 0; i < FS_QUESTIONS; i++)
    if (g_fs.no[i].step == g_fs.step) g_fs.no[i].step = 0;
  const char *at = (const char *)(uintptr_t)rc_mid_get(f, g_fs.m_path);
  const void *fsp = (const void *)(uintptr_t)rc_mid_get(f, g_fs.m_fs);
  uint64_t fs = fsp ? (uint64_t)(uintptr_t)rc_mod_ptr(fsp) : 0;
  int n = g_fs.nmounted;
  size_t len = at ? strlen(at) : 0;
  if (n < 0 || n == FS_STEP_MOUNTS || !fs || !len || len >= FS_AT || !g_fs.control) {
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
  if (!fs_has(fs, g_fs.control)) {
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
    if (fs_has(g_fs.mounted[i].fs, path + len + 1)) return 1;
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
    found = fs_has(whole, path);
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

// The mounting's end: every question last answered "no" to the whole file system once more. The path is the
// table's copy, which the game is given in memory of its own.
static void fs_check(void) {
  // (a mounting whose steps asked nothing has nothing to check: the slots are an earlier mounting's, or free)
  if (g_fs.off || !g_fs.whole || !g_fs.last || atomic_load(&g_fs_stale)) return;
  static char *path;  // the game thread's
  if (!path && !(path = rc_guest_alloc(FS_PATH))) return;
  for (int i = 0; i < FS_QUESTIONS; i++) {
    if (g_fs.no[i].step != g_fs.last) continue;
    strcpy(path, g_fs.no[i].path);
    if (!fs_has(g_fs.whole, path)) continue;
    g_fs.off = 1;
    rs_mods_logf("DLC: %s is there, and a step was told it was not: file questions go to the whole file system "
                 "for the rest of this run; start the game again to have the archives mounted with it",
                 g_fs.no[i].path);
    break;
  }
}

// The mid points of the mounting: the manifests' and the file system's.
static void mount_points(void) {
  int asked = rc_mid_add_name("manifest_request", on_manifest_request, NULL);
  int answered = rc_mid_add_name("manifest_response", on_manifest_response, NULL);
  g_manifest_response = answered < 0 ? -1 : rc_mid_value(answered, "response");
  g_manifest_points = asked >= 0 && g_manifest_response >= 0 ? 0 : -1;
  // (the fields the answer's name is read by: a game whose table lacks one has no name to tell the answers by)
  if (!RC_HAS_OFF(asset_load_name) || !RC_HAS_OFF(efd_str_inline) || !RC_HAS_OFF(efd_str_size)) g_manifest_points = -1;
  if (g_manifest_points < 0) rs_mods_logf("no points for the song manifests: the mounting does not wait for them");
  int mount = rc_mid_add_name("fs_mount", on_fs_mount, NULL);
  int exists = mount < 0 ? -1 : rc_mid_add_name("fs_exists", on_fs_exists, NULL);
  if (exists >= 0) {
    g_fs.m_path = rc_mid_value(mount, "path"), g_fs.m_fs = rc_mid_value(mount, "fs");
    g_fs.fs = rc_mid_value(exists, "fs"), g_fs.path = rc_mid_value(exists, "path");
    g_fs.found = rc_mid_value(exists, "found"), g_fs.answered = rc_mid_cont(exists, "answered");
    if (g_fs.m_path >= 0 && g_fs.m_fs >= 0 && g_fs.fs >= 0 && g_fs.path >= 0 && g_fs.found >= 0 &&
        g_fs.answered > 0 && RC_HAS_OFF(fs_exists_slot))
      g_fs.point = exists;
  }
  if (g_fs.point < 0) {
    g_fs.off = 1;
    rs_mods_logf("no points for the file system: a step asks all of it");
  }
}

static void mount_stall(const char *what) {
  g_mount.stalled = 1;
  rs_mods_logf("DLC: %s in %.0f s, the mounting goes on without waiting", what, LOAD_STALL_S);
}

// 1 while manifests this mounting asked for have not been answered. Ones that make no progress for LOAD_STALL_S end
// the waiting.
static int manifests_waiting(void) {
  if (g_mount.stalled || g_manifest_points < 0) return 0;
  unsigned answered = atomic_load(&g_manifests_answered);
  int left = (int)(atomic_load(&g_manifests_asked) - g_mount.asked0) - (int)(answered - g_mount.answered0);
  double t = now_s();
  if (answered != g_mount.answered) g_mount.answered = answered, g_mount.answered_since = t;
  if (left <= 0) return 0;
  if (t - g_mount.answered_since < LOAD_STALL_S) return 1;
  mount_stall("a song manifest was not answered");
  return 0;
}

static int g_batched;  // this update's steps run in the loop (rs_mods_mount_frame): the game thread's

int rs_mods_mount_frame(int installing) {
  int batched = installing && rs_mods_cfg.fixes && rs_mods_cfg.budget_s > 0;
  if (!installing && g_mount.frames) {
    // The flag was cleared by something else than the update that found it set (rs_mods_mount_end): what this
    // mounting kept is not the next one's.
    rs_mods_logf("DLC: the mounting ended outside its update, after %u archives in %u frames", g_mount.steps,
                 g_mount.frames);
    memset(&g_mount, 0, sizeof g_mount);
  }
  g_batched = batched;
  if (installing && !g_mount.frames++) {
    g_mount.t0 = now_s();
    g_mount.asked0 = atomic_load(&g_manifests_asked);
    g_mount.answered0 = atomic_load(&g_manifests_answered);
    fs_forget();
    g_fs.step = g_fs.last = g_fs.asked = g_fs.alone = 0;
  }
  atomic_store_explicit(&g_loader_busy, batched ? (uint64_t)(now_s() * 1000.0) | 1 : 0, memory_order_relaxed);
  return batched;
}

void rs_mods_mount_restarted(void) {
  if (!g_mount.holding) return;
  // The manifests still to come are the new mounting's to wait for, with its own.
  int left = (int)(atomic_load(&g_manifests_asked) - g_mount.asked0) -
             (int)(atomic_load(&g_manifests_answered) - g_mount.answered0);
  rs_mods_logf("DLC: a new enumeration with %d song manifests of the last one still to come", left > 0 ? left : 0);
  rs_mods_mount_end();
  g_mount.frames = 1;  // as rs_mods_mount_frame starts one, the baseline taken back by what is left
  g_mount.t0 = now_s();
  g_mount.asked0 = atomic_load(&g_manifests_asked) - (unsigned)(left > 0 ? left : 0);
  g_mount.answered0 = atomic_load(&g_manifests_answered);
  fs_forget();
  g_fs.step = g_fs.last = g_fs.asked = g_fs.alone = 0;
}

void rs_mods_mounting(int on) {
  t_mounting = on;
  if (on) {
    g_fs.step++;
    g_fs.nmounted = 0;
    if (!g_fs.control && (g_fs.control = rc_guest_alloc(sizeof "appid.appid"))) strcpy(g_fs.control, "appid.appid");
  }
  // The file questions' shortcut is the batched mounting's, whose end checks it (rs_mods_mount_listed): a step
  // taken one a frame (RS_MODS_BUDGET_MS=0, or with nothing being installed) asks as the game does.
  t_in_step = on && g_batched;
}

void rs_mods_mount_took(void) { g_mount.steps++; }

int rs_mods_mount_loading(uint64_t left) {
  if (g_mount.stalled) return 0;
  double t = now_s();
  if (left != g_mount.left) g_mount.left = left, g_mount.left_since = t;
  if (!left) return 0;
  if (t - g_mount.left_since < LOAD_STALL_S) return 1;
  mount_stall("a package did not load");
  return 0;
}

int rs_mods_mount_listed(void) {
  fs_check();
  g_mount.t_mounted = g_mount.answered_since = now_s();
  g_mount.frames_mounted = g_mount.frames;
  return g_mount.holding = manifests_waiting();
}

int rs_mods_mount_holding(void) { return g_mount.holding; }
int rs_mods_mount_waiting(void) { return manifests_waiting(); }

void rs_mods_mount_end(void) {
  double t = now_s();
  if (g_mount.holding)
    rs_mods_logf("DLC: %u archives mounted in %u frames, %.2f s, their songs in the lists %.2f s later",
                 g_mount.steps, g_mount.frames_mounted, g_mount.t_mounted - g_mount.t0, t - g_mount.t_mounted);
  else
    rs_mods_logf("DLC: %u archives mounted in %u frames, %.2f s", g_mount.steps, g_mount.frames, t - g_mount.t0);
  if (g_fs.asked)
    rs_mods_logf("DLC: %u of the steps' %u file questions asked of the step's archive alone", g_fs.alone, g_fs.asked);
  memset(&g_mount, 0, sizeof g_mount);
}

// ------------------------------------------------------------------------------------------------
// Two players on any input: Player 2's saved input. A player's input mode and input live in their profile, but
// the game restores a saved input for Player 1 alone, after the profile screen; Player 2 takes the first slot
// free. So before the game looks for a slot for Player 2's guitar (input_bind), this finds the input their profile
// names, when the profile is on Direct Connect or the microphone, and gives it the slot. It has to be there that
// early: moving the input of a running stream crashes the I/O thread, in the callback that feeds the player.
// RS_MODS_MULTIPLAYER=0 is the original. The fix's other half is each edition's own (mods_shared.h).
//
// Both games have the function: the Mac's takes (input, force), Learn & Play's the input in a register, with
// `force` folded away, 0 in every call of either game (each one's symbols.map has the reading), and the hook sees
// the Mac's call. What it reads is the same in both: the ten slots (input_binding: 0-3 an input, 5 nothing), the
// profile's keys (pointers to their text), a string of the profile, the input of a name and its slot.
// Each edition's own version of this left the profile's text to the game when it was too long to be in the
// string, a block of the game's memory at every call; this one frees it.
// ------------------------------------------------------------------------------------------------
#define INPUT_P1_MIC 1
#define INPUT_P2_GUITAR 2
#define INPUT_P2_MIC 3
#define INPUT_NONE 5
#define INPUT_SLOTS 10
#define P2_STRING 64  // room for the game's small string in either game
#define P2_NAME 256

// A pointer for the game, at its own width
static void put_ptr(uint8_t *at, const void *p) {
  uint64_t g = RC_GUEST(p);
  memcpy(at, &g, (size_t)rc_guest_ptr_size);
}

// The string of Player 2's profile under `key`, as text in `name` (P2_NAME bytes of the game's memory, as is `s`):
// empty when the profile has none. The game writes it into a small string of its own: the text in place or behind
// a pointer at its start, its end at efd_str_size, and efd_str_inline holds the address of that word while the
// text is in place. A text behind the pointer is the game's allocation, for a name too long to be in place: once
// the name is copied the string is destroyed as the game's own callers of input_profile_string destroy theirs
// (efd_str_destroy, a call in Learn & Play at 0x11dee5e, the same steps inlined on the Mac at 0x1003594b9), which
// frees that text and nothing when it is in place.
static void profile_string(const void *key, uint8_t *s, char *name) {
  uint8_t *end = s + RC_OFF(efd_str_size), *in_place = s + RC_OFF(efd_str_inline);
  memset(s, 0, P2_STRING);
  put_ptr(end, s);
  put_ptr(in_place, end);
  OV_GUEST(input_profile_string, RS_MODS_PLAYER_2, (uint64_t)(uintptr_t)key, (uint64_t)(uintptr_t)s);
  const char *from = rc_mod_ptr(in_place) == (void *)end ? (const char *)s : rc_mod_ptr(s);
  const char *to = rc_mod_ptr(end);
  size_t len = from && to > from ? (size_t)(to - from) : 0;
  if (len >= P2_NAME) len = P2_NAME - 1;
  if (len) memcpy(name, from, len);
  name[len] = 0;
  OV_GUEST(efd_str_destroy, (uint64_t)(uintptr_t)s);
}

static int before_input_bind(rc_ctx *c, void *user) {
  (void)user;
  OV_ARGS(a, c);
  if (!rs_mods_cfg.multiplayer || OV_U32(a) != INPUT_P2_GUITAR) return 0;
  uint8_t mode = (uint8_t)OV_GUEST(input_mode, RS_MODS_PLAYER_2);
  if (!rs_mods_any_input(mode)) return 0;
  int32_t *binding = (int32_t *)(uintptr_t)RC_GA(input_binding);
  for (int i = 0; i < INPUT_SLOTS; i++)
    if (binding[i] == INPUT_P2_GUITAR) return 0;
  // what the game is handed has to be memory it can reach (rc_mod.h): one block a thread, kept
  static _Thread_local uint8_t *mem;
  if (!mem && !(mem = rc_guest_alloc(P2_STRING + P2_NAME))) return 0;
  char *name = (char *)mem + P2_STRING;
  const void *key = rc_mod_ptr((const void *)(uintptr_t)(mode == RS_MODS_MODE_DIRECT ? RC_GA(input_key_direct)
                                                                                   : RC_GA(input_key_mic)));
  if (!key) {  // the table's word is not the key in this game: said once, as the saved input is then never found
    static int said;
    if (!said++) rs_mods_logf("player 2: no profile key for its saved input (mode %d)", mode);
    return 0;
  }
  profile_string(key, mem, name);
  if (!name[0]) return 0;
  uint64_t manager = OV_GUESTV(input_manager, NULL, 0);
  const uint8_t *in = RC_HOST(OV_GUEST(input_by_name, manager, (uint64_t)(uintptr_t)name));
  if (!in) return 0;
  uint32_t slot = RC_LD(uint32_t, in + RC_OFF(input_slot));
  if (slot >= INPUT_SLOTS) return 0;
  int32_t held = binding[slot];  // a microphone input takes any slot, and finds another
  if (held == INPUT_NONE || held == INPUT_P1_MIC || held == INPUT_P2_MIC) {
    binding[slot] = INPUT_P2_GUITAR;
    rs_mods_logf("player 2 on \"%s\"", name);
  }
  return 0;
}

// ------------------------------------------------------------------------------------------------
// Skip Tuner in a Guitarcade game's tuner. Where the tuner menu sets its buttons up, it offers Skip Tuner (button
// 0xac) unless it is the Tuner of the tools or a lesson's, a first encounter, or a tuner that goes on to a
// Guitarcade game: it reads the stage its OnComplete names and leaves the button out for "Guitarcade_Game". That
// one test is all that keeps it from those tuners. The skip itself is the menu's own whatever the stage: the same
// warning (RS2SkipTuner, unless the profile says not to show it), then the step on to OnComplete that a finished
// tuning takes. So a player whose tuner does not finish (an input the tuner can't follow, a guitar kept in another
// tuning) could play no game but the one that has no tuner; a song has always left that to the player.
//
// Both games make the test the same way and keep its answer until they act on it, the Mac in a register and
// Learn & Play in a local: a mid point there (tuner_skip_offer), whose answer the hook sets.
// RS_MODS_GUITARCADE_SKIP=0 is the original.
// ------------------------------------------------------------------------------------------------
static int g_skip_offer = -1;  // the answer's index at the point; -1: this game has no such point

static void on_skip_offer(rc_mid_frame *f, void *user) {
  (void)user;
  if (rs_mods_cfg.guitarcade_skip && g_skip_offer >= 0) rc_mid_set(f, g_skip_offer, 1);
}

// ------------------------------------------------------------------------------------------------
// The white loading screen (RS_MODS_MENU_RETRY=0 is the original). A menu is a Scaleform movie, and the game asks the
// asset locator for its file, urn:application:x-shockwave-flash:NAME, once, from the movie's setup. At start-up the
// local file index is still being built by the indexing threads. A locator that does not have the file yet refuses
// the request, discards it and answers no; the request's caller ignores the answer and nothing asks again, so the
// load group waits for a movie that is never made and the screen stays white. It takes a start where the index is
// late (a community fast-load cache.psarc, a busy CPU).
//
// The request is the game's own function (menu_movie_request) and takes the movie and the request's id, and the
// movie's file is the last part of its URI. Four mid points follow one request on the game thread: its start (the
// movie and the id), the locator call that is about to be made and what it answered (menu_request, menu_locate,
// menu_located; a movie with no URI makes no call, and what is answered then is not an answer). A rejected request is
// kept in a table of MENU_SLOTS. The indexing threads end each file at file_indexed: a table entry whose file name that
// is gets a flag (the threads do nothing else, under a short lock the game thread takes to change the table). The
// game thread, in front of every frame (the game_frame hook), asks again for each rejected request whose file has been
// indexed since, once, by the game's own function with the id it first had. If the flag never comes (a path of the
// index this does not follow) a rejected request is asked again every MENU_FALLBACK_S seconds, MENU_FALLBACK_MAX times
// at most, with a line in the log. A request that is accepted leaves the table; so does one whose movie is not what
// it was (its first word, its name) or that has waited MENU_MAX_AGE_S: the game may have let it go.
//
// The game's own timing is the earlier of two: the replay happens at a frame's start, not in the load group's update
// (which is where the request comes from). Nothing else asks in between, and the request only hands the movie to
// the locator, which answers on the loader thread as it does for any other.
// ------------------------------------------------------------------------------------------------
#define MENU_SLOTS 16
#define MENU_NAME 64
#define MENU_FALLBACK_S 5.0
#define MENU_FALLBACK_MAX 6
#define MENU_MAX_AGE_S 120.0

typedef struct {
  int used;
  int rejected;           // the locator said no (else: asked, no answer yet)
  const void *movie;      // as the game thread saw it (a host pointer)
  const void *vtable;     // the movie's first word then
  uint32_t id[2];         // the request's id, its two words
  char name[MENU_NAME];   // the movie's file, without its extension
  double born, since;     // when it was first asked, and when it last was rejected or asked again
  unsigned replays, fallbacks;
  atomic_int published;   // a file of that name was indexed since (the indexing threads set it)
} menu_req;

static struct {
  menu_req q[MENU_SLOTS];
  unsigned next;          // the slot that goes when all are used
  atomic_int active;      // entries in use: the indexing threads look no further when it is 0
  int on;                 // every point is there
  int m_movie, m_lo, m_hi, m_accepted, m_frame;
} g_menu;
static atomic_flag g_menu_lock = ATOMIC_FLAG_INIT;
// The game thread's: the request now running, from its start to the locator's answer
static _Thread_local struct {
  int on, located;
  const void *movie;
} t_menu;
static _Thread_local int t_menu_again;  // the request now running is one of ours

static void menu_lock(void) {
  while (atomic_flag_test_and_set_explicit(&g_menu_lock, memory_order_acquire)) {}
}
static void menu_unlock(void) { atomic_flag_clear_explicit(&g_menu_lock, memory_order_release); }

// A game string's text, copied (the text in place, or behind a pointer at its start); 0 when it does not fit n
static int game_text(const void *s, char *out, size_t n) {
  const uint8_t *str = s;
  int in_place = rc_mod_ptr(str + RC_OFF(efd_str_inline)) == (const void *)(str + RC_OFF(efd_str_size));
  const char *text = in_place ? (const char *)str : (const char *)rc_mod_ptr(str);
  if (!text) return 0;
  size_t len = 0;
  while (len < n && text[len]) len++;
  if (len == n) return 0;
  memcpy(out, text, len + 1);
  return 1;
}

// The movie's file: the last part of its URI; 0 for a movie with none
static int menu_movie_name(const void *movie, char *name, size_t n) {
  char uri[128];
  if (!game_text((const uint8_t *)movie + RC_OFF(menu_movie_uri), uri, sizeof uri)) return 0;
  const char *colon = strrchr(uri, ':'), *part = colon ? colon + 1 : uri;
  if (!*part || strlen(part) >= n) return 0;
  strcpy(name, part);
  return 1;
}

// 1 when the file (a name and an extension) is the movie's: its name, in any case, and then a dot
static int menu_name_is(const char *name, const char *file) {
  size_t i = 0;
  for (; name[i]; i++)
    if (tolower((unsigned char)name[i]) != tolower((unsigned char)file[i])) return 0;
  return name[0] && file[i] == '.';
}

static menu_req *menu_find(const void *movie) {  // locked
  for (int k = 0; k < MENU_SLOTS; k++)
    if (g_menu.q[k].used && g_menu.q[k].movie == movie) return &g_menu.q[k];
  return NULL;
}
static void menu_drop(menu_req *r) {  // locked
  if (r->used) atomic_fetch_sub(&g_menu.active, 1);
  r->used = 0;
}

static void on_menu_request(rc_mid_frame *f, void *user) {
  (void)user;
  t_menu.on = 0;
  if (!g_menu.on || !rs_mods_cfg.menu_retry) return;
  const void *movie = (const void *)(uintptr_t)rc_mid_get(f, g_menu.m_movie);
  char name[MENU_NAME];
  if (!movie || !menu_movie_name(movie, name, sizeof name)) return;
  t_menu.on = 1, t_menu.located = 0, t_menu.movie = movie;
  menu_lock();
  menu_req *r = menu_find(movie);
  if (!r || !t_menu_again) {  // a request of the game's: tracked from its start
    if (!r) {
      for (int k = 0; k < MENU_SLOTS && !r; k++)
        if (!g_menu.q[k].used) r = &g_menu.q[k];
      if (!r) menu_drop(r = &g_menu.q[g_menu.next++ % MENU_SLOTS]);  // all used: the oldest is a dead movie by now
      r->used = 1;
      atomic_fetch_add(&g_menu.active, 1);
    }
    double t = now_s();
    r->rejected = 0, r->movie = movie, r->vtable = rc_mod_ptr(movie), r->born = r->since = t;
    r->id[0] = (uint32_t)rc_mid_get(f, g_menu.m_lo), r->id[1] = (uint32_t)rc_mid_get(f, g_menu.m_hi);
    r->replays = r->fallbacks = 0;
    atomic_store(&r->published, 0);
    memcpy(r->name, name, strlen(name) + 1);
  }
  menu_unlock();
}

static void on_menu_locate(rc_mid_frame *f, void *user) {
  (void)f, (void)user;
  if (t_menu.on) t_menu.located = 1;
}

static void on_menu_located(rc_mid_frame *f, void *user) {
  (void)user;
  if (!t_menu.on || !t_menu.located) return;  // a movie with no URI: nothing was asked
  t_menu.on = t_menu.located = 0;
  int accepted = (uint8_t)rc_mid_get(f, g_menu.m_accepted) != 0;
  char name[MENU_NAME];
  unsigned replays = 0;
  menu_lock();
  menu_req *r = menu_find(t_menu.movie);
  if (r) {
    memcpy(name, r->name, sizeof name);
    replays = r->replays;
    if (accepted) menu_drop(r);
    else r->rejected = 1, r->since = now_s();
  }
  menu_unlock();
  if (!r) return;
  if (!accepted && !replays)
    rs_mods_logf("menu load: '%s' was rejected (not indexed yet); waiting for the indexer", name);
  else if (!accepted) rs_mods_logf("menu load: '%s' rejected again (request %u)", name, replays + 1);
  else if (replays) rs_mods_logf("menu load: '%s' accepted after %u requests", name, replays + 1);
}

// On the indexing threads (and the loader's), after a file is in the index: what is asked is the game thread's
static void on_file_indexed(rc_mid_frame *f, void *user) {
  (void)user;
  if (!g_menu.on || !rs_mods_cfg.menu_retry || !atomic_load(&g_menu.active)) return;
  const uint8_t *frame = (const uint8_t *)(uintptr_t)rc_mid_get(f, g_menu.m_frame);
  char file[2 * MENU_NAME];
  if (!frame || !game_text(frame + RC_OFF(file_index_name), file, sizeof file)) return;
  menu_lock();
  for (int k = 0; k < MENU_SLOTS; k++)
    if (g_menu.q[k].used && menu_name_is(g_menu.q[k].name, file)) atomic_store(&g_menu.q[k].published, 1);
  menu_unlock();
}

// The game's request for a movie again. The Mac's takes the movie and a pointer to the id; Learn & Play's has the movie
// in eax and the id's words on the stack, which a call by name can't do (its prototype is `custom`): eax is set in
// the thread's context, which the callee's caller-saved registers allow, and put back.
static void menu_ask_again(const void *movie, const uint32_t id[2]) {
  if (rc_guest_ptr_size == 8) {
    static _Thread_local uint32_t *words;
    if (!words && !(words = rc_guest_alloc(2 * sizeof *words))) return;
    words[0] = id[0], words[1] = id[1];
    OV_GUEST(menu_movie_request, (uint64_t)(uintptr_t)movie, (uint64_t)(uintptr_t)words);
    return;
  }
  rc_ctx *c = rc_thread_ctx();
  const uint64_t eax = c->r[0], args[2] = {id[0], id[1]};
  c->r[0] = RC_GUEST(movie);
  rc_call_guest(RC_GA(menu_movie_request_reg), args, 2, 0, 0, 0);
  c->r[0] = eax;
}

// One table entry that is due, asked again; 0 when none is
static int menu_replay_one(void) {
  menu_req *r = NULL;
  int indexed = 0;
  double t = now_s();
  menu_lock();
  for (int k = 0; k < MENU_SLOTS; k++) {
    menu_req *e = &g_menu.q[k];
    if (e->used && t - e->born > MENU_MAX_AGE_S) menu_drop(e);
    if (!r && e->used && e->rejected && atomic_exchange(&e->published, 0)) r = e, indexed = 1;
  }
  for (int k = 0; k < MENU_SLOTS && !r; k++) {
    menu_req *e = &g_menu.q[k];
    if (e->used && e->rejected && e->fallbacks < MENU_FALLBACK_MAX && t - e->since >= MENU_FALLBACK_S)
      r = e, r->fallbacks++, r->since = t;
  }
  menu_unlock();
  if (!r) return 0;
  const void *movie = r->movie;
  char name[MENU_NAME], now_name[MENU_NAME];
  memcpy(name, r->name, sizeof name);
  // still the movie it was: the game may have let it go (the first word of a freed object is the allocator's)
  if (rc_mod_ptr(movie) != r->vtable || !menu_movie_name(movie, now_name, sizeof now_name) || strcmp(name, now_name)) {
    menu_lock();
    menu_drop(r);
    menu_unlock();
    return 1;
  }
  r->replays++;
  if (indexed) rs_mods_logf("menu load: '%s' is indexed now; asking again (request %u)", name, r->replays + 1);
  else
    rs_mods_logf("menu load: WARNING '%s' still rejected and its file was not seen being indexed; asking again anyway "
                 "(fallback %u of %u)", name, r->fallbacks, MENU_FALLBACK_MAX);
  t_menu_again = 1;
  menu_ask_again(movie, r->id);
  t_menu_again = 0;
  return 1;
}

static int before_menu_frame(rc_ctx *c, void *user) {
  (void)c, (void)user;
  if (!g_menu.on || !rs_mods_cfg.menu_retry || !atomic_load(&g_menu.active)) return 0;
  for (int k = 0; k < MENU_SLOTS && menu_replay_one(); k++) {}
  return 0;
}

static void menu_points(void) {
  int request = rc_mid_add_name("menu_request", on_menu_request, NULL);
  int locate = rc_mid_add_name("menu_locate", on_menu_locate, NULL);
  int located = rc_mid_add_name("menu_located", on_menu_located, NULL);
  int indexed = rc_mid_add_name("file_indexed", on_file_indexed, NULL);
  int frame = rc_hook_add_name("game_frame", before_menu_frame, NULL, NULL);
  if (request < 0 || locate < 0 || located < 0 || indexed < 0 || frame < 0 || rc_mid_is_none(request) ||
      rc_mid_is_none(locate) || rc_mid_is_none(located) || rc_mid_is_none(indexed))
    return;
  g_menu.m_movie = rc_mid_value(request, "movie"), g_menu.m_lo = rc_mid_value(request, "id_lo");
  g_menu.m_hi = rc_mid_value(request, "id_hi"), g_menu.m_accepted = rc_mid_value(located, "accepted");
  g_menu.m_frame = rc_mid_value(indexed, "frame");
  if (g_menu.m_movie < 0 || g_menu.m_lo < 0 || g_menu.m_hi < 0 || g_menu.m_accepted < 0 || g_menu.m_frame < 0 ||
      !RC_HAS_OFF(menu_movie_uri) || !RC_HAS_OFF(file_index_name) || !RC_HAS_OFF(efd_str_inline) ||
      !RC_HAS_OFF(efd_str_size))
    return;
  g_menu.on = 1;
}

// ------------------------------------------------------------------------------------------------
// Learn a Song with more than five arrangements (RS_MODS_ARRANGEMENTS=0 is the original). The page's builder fills a
// slot, altArrangementsP1_slot%d, for each arrangement of the song but the selected one, and the movie
// (las_songoptions.gfx) has four: the selected arrangement and four others. A fifth alternate asks Scaleform for a
// slot the movie does not have, which leaves a display node with nothing behind it, and the renderer crashes on it
// (a misaligned 16-byte load) when it draws the page. At each alternate the builder has the slots it has filled so
// far in a local; with four filled the hook goes on at the loop's end, as the builder does for the selected
// arrangement. A song with five or fewer arrangements is not touched, and the others show their first four
// alternates, in the record order.
// ------------------------------------------------------------------------------------------------
#define LAS_ALT_SLOTS 4
static int g_alt_filled = -1, g_alt_latch = -1;  // the point's value and continuation; -1: this game has none

static void on_alt_slot(rc_mid_frame *f, void *user) {
  (void)user;
  if (!rs_mods_cfg.arrangements || g_alt_filled < 0 || g_alt_latch <= 0) return;
  if ((uint32_t)rc_mid_get(f, g_alt_filled) < LAS_ALT_SLOTS) return;
  static atomic_int said;
  if (!atomic_exchange(&said, 1))
    rs_mods_logf("Learn a Song: a song with more than five arrangements shows the first four alternates");
  f->go = g_alt_latch;
}

void rs_mods_start(void) {
  rs_mods_reload_config();
  if (rc_hook_add_name("boot_intro_update", before_intro_update, NULL, NULL)) rs_mods_logf("no intro hook point");
  if (rc_hook_add_name("steam_dlc_owned", before_dlc_owned, NULL, NULL)) rs_mods_logf("no DLC ownership hook point");
  mount_points();
  if (rc_hook_add_name("input_bind", before_input_bind, NULL, NULL))
    rs_mods_logf("no hook point for Player 2's saved input");
  if (rs_mods_cfg.fixes) rs_mods_raise_nofile();
  rs_mods_logf("fixes %s, bypass %s, overlay %s, intro skip %s, autosave %s, budget %.0f ms",
               rs_mods_cfg.fixes ? "on" : "off", rs_mods_cfg.bypass ? "on" : "off", rs_mods_cfg.overlay ? "on" : "off",
               rs_mods_cfg.intro ? "on" : "off", rs_mods_cfg.autosave ? "on" : "off", rs_mods_cfg.budget_s * 1000);
  int fr = rs_framerate_init();  // framerate.c: its own switch, RS_MODS_FRAME_RATE
  if (fr < 0) rs_mods_logf("frame rate: off");
  else rs_mods_logf("frame rate: on, %d of %d points", fr, RS_FR_POINTS);
  int p = rc_mid_add_name("tuner_skip_offer", on_skip_offer, NULL);  // a game without the point: a log line of its own
  if (p >= 0 && !rc_mid_is_none(p)) g_skip_offer = rc_mid_value(p, "offer");
  rs_mods_logf("Guitarcade Skip Tuner: %s",
               g_skip_offer < 0 ? "no point for it" : rs_mods_cfg.guitarcade_skip ? "on" : "off");
  menu_points();
  rs_mods_logf("menu movie request again: %s", !g_menu.on ? "no points for it" : rs_mods_cfg.menu_retry ? "on" : "off");
  int alt = rc_mid_add_name("las_alt_slot", on_alt_slot, NULL);
  if (alt >= 0 && !rc_mid_is_none(alt)) {
    g_alt_filled = rc_mid_value(alt, "filled");
    g_alt_latch = rc_mid_cont(alt, "latch");
  }
  rs_mods_logf("Learn a Song alternate arrangements: %s",
               g_alt_filled < 0 || g_alt_latch <= 0 ? "no point for it" : rs_mods_cfg.arrangements ? "on" : "off");
}
