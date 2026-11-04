// Native replacements for game functions dropped from translation (win-x86/build/skip.txt): the
// Windows counterpart of the Mac link-level overrides (tools/ws7/recomp/overrides). A function named
// here gets no W_<addr>; gen_image_pe.py emits the rc32_table entry {addr, F_<addr>} and the linker
// resolves F_<addr> to the definition below (win32_shims.a). Each is entered as the translated
// function would be: the return address at guest esp, the arguments above it, the result left in eax;
// it pops the return address into c->ret_to as a guest `ret` does (and removes stdcall arguments).
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include "rc32.h"
#include "rc_nofile.h"

// `ret` (ret 0): the return address into c->ret_to, esp past it. For a function with no stack
// arguments (a register `this`, or none); the same record a translated `ret` makes (rc32_gen.h),
// as win32_crtmath.c's crt_ret does.
static inline void ov_ret32(rc_ctx *c, uint32_t eax) {
  c->r[0] = eax;
  uint32_t sp = (uint32_t)c->r[4];
  memcpy(&c->ret_to, RC32_H(sp), 4);
  c->r[4] = sp + 4;
}

// No intro screens: the counterpart of the Mac intro skip (tools/ws7/recomp/overrides/mods/mods.c,
// before_intro_update), off with the same RS_MODS_INTRO=0. The Mac boot_intro_update is 0x1126950
// here (matched by its mPlaybackState / mScreenState reads and the same five-state switch), stdcall
// bool update(RSBootService *boot) (`ret 4`), translated as W_1126950 (skip.txt `wrap`); this runs
// first. RSBootService fields of the Windows build: u32 boot phase +0x48 (dispatcher 0x112614d; phase 2
// = 0x1126680, with the game's own skip at 0x1126759; phase 3 = 0x1126fc0), u32 phase 3 step counter
// +0x28 (10 once its steps are done: the same 11-case switch on counter + 1, 0x112727a), u32 intro
// state +0x60 (0 load the movie's model, 1 wait for it, 2 wait for the movie to start, 3 play, 4 end,
// 5 done), u8 intro playing +0x2d. As on the Mac: the model is loaded as usual; then the tick reports
// "not busy" without starting the movie until phase 3 has done its steps, and then takes the game's own
// skip (state 4, playing flag cleared) so the original ends the intro. On the Mac, ending it earlier or
// without the model left the title screen without its menu movie.
#define LP_BOOT_PHASE 0x48
#define LP_BOOT_STEP 0x28
#define LP_BOOT_PHASE3_STEPS 10
#define LP_BOOT_INTRO_STATE 0x60
#define LP_BOOT_INTRO_PLAYING 0x2d
void W_1126950(rc_ctx *c);
static int lp_intro_skip = -1;

void F_1126950(rc_ctx *c) {
  if (lp_intro_skip < 0) {
    const char *e = getenv("RS_MODS_INTRO");
    lp_intro_skip = !(e && (e[0] == '0' || e[0] == 'n' || e[0] == 'N' || e[0] == 'f' || e[0] == 'F'));
  }
  if (lp_intro_skip) {
    uint32_t sp = (uint32_t)c->r[4], boot, phase, state, step;
    memcpy(&boot, RC32_H(sp + 4), 4);
    uint8_t *b = RC32_H(boot);
    memcpy(&phase, b + LP_BOOT_PHASE, 4);
    memcpy(&state, b + LP_BOOT_INTRO_STATE, 4);
    memcpy(&step, b + LP_BOOT_STEP, 4);
    if ((phase == 2 || phase == 3) && (state == 2 || state == 3)) {
      if (phase == 2 || step < LP_BOOT_PHASE3_STEPS) {  // not busy, the movie not started
        c->r[0] = 0;
        memcpy(&c->ret_to, RC32_H(sp), 4);
        c->r[4] = sp + 8;  // ret 4
        return;
      }
      state = 4;  // the game's skip: the original ends the intro (state 5)
      memcpy(b + LP_BOOT_INTRO_STATE, &state, 4);
      b[LP_BOOT_INTRO_PLAYING] = 0;
    }
  }
  W_1126950(c);
}

// Direct Connect: the cache overlay (the Mac's is tools/ws7/recomp/overrides/mods/mods.c, a pre hook on the
// same function). FrontEnd_OpenFile (image VA 0x101ba80; the Mac's 0x1003c7810): DataStream **
// open(DataStream **out [stack], const char *path [ecx]), the caller removes `out`; eax = out. The game
// opens the archives inside cache.psarc (cache0.7z ... cache8.7z, the menu data among them) through it,
// and the installer's overlay (tools/play-native/cache_overlay.py) is <game>/Update/patcharchives.psarc,
// which the engine mounts at PatchUpdate/ (0x101b297, "%spatcharchives.psarc"). For a cacheN.7z this
// opens PatchUpdate/<name> first and the name itself when the overlay has none; RS_MODS_OVERLAY=0 turns
// that off. The rest is the original: the path in lower case (the original's own lower-casing is behind a
// packer stub), the front-end file system (the singleton at 0x1ee67c4) Open, slot +0x10, thiscall
// bool(const char *, DataStream **), and the stream's AddRef (slot +0x4) for *out.
#define FE_SINGLETON 0x1ee67c4u
#define FE_OPEN_SLOT 0x10u
#define STREAM_ADDREF_SLOT 0x4u

static uint32_t rd32(uint32_t g) {
  uint32_t v;
  memcpy(&v, RC32_H(g), 4);
  return v;
}

static void wr32(uint32_t g, uint32_t v) { memcpy(RC32_H(g), &v, 4); }

static int is_cache_7z(const char *p) {
  if (strncasecmp(p, "cache", 5) != 0) return 0;
  p += 5;
  if (*p < '0' || *p > '9') return 0;
  while (*p >= '0' && *p <= '9') p++;
  return strcasecmp(p, ".7z") == 0;
}

static int overlay_on(void) {
  static int on = -1;
  if (on < 0) {
    const char *v = getenv("RS_MODS_OVERLAY");
    on = !(v && (v[0] == '0' || v[0] == 'n' || v[0] == 'N' || v[0] == 'f' || v[0] == 'F'));
  }
  return on;
}

// Open `path` (guest, lower case) through the front end; the stream (0: none) with a reference for the caller.
static uint32_t fe_open(rc_ctx *c, uint32_t path, uint32_t slot) {
  uint32_t fs = rd32(FE_SINGLETON);
  wr32(slot, 0);
  uint32_t args[2] = {path, slot};
  rc32_call(c, rd32(rd32(fs) + FE_OPEN_SLOT), args, 2, fs, 0, 0);
  uint32_t s = rd32(slot);
  if (s) rc32_call(c, rd32(rd32(s) + STREAM_ADDREF_SLOT), 0, 0, s, 0, 0);
  return s;
}

void F_101ba80(rc_ctx *c) {
  uint32_t esp = (uint32_t)c->r[4];
  uint32_t out = rd32(esp + 4);
  const char *path = RC32_H((uint32_t)c->r[1]);
  // scratch on the guest stack below the caller's frame: the stream slot and the lower-case path
  size_t n = strnlen(path, 4096);
  uint32_t scratch = (esp - 64 - (uint32_t)n - 32) & ~15u;
  uint32_t slot = scratch, buf = scratch + 16;
  char *b = RC32_H(buf);
  static const char pre[] = "patchupdate/";
  const int pn = (int)sizeof pre - 1;
  memcpy(b, pre, pn);
  for (size_t i = 0; i < n; i++) b[pn + i] = (char)tolower((unsigned char)path[i]);
  b[pn + n] = 0;
  c->r[4] = scratch - 16;
  uint32_t s = 0;
  if (overlay_on() && is_cache_7z(b + pn)) s = fe_open(c, buf, slot);
  if (!s) s = fe_open(c, buf + pn, slot);
  c->r[4] = esp;
  wr32(out, s);
  ov_ret32(c, out);
}

// ------------------------------------------------------------------------------------------------
// Switches, named as on the Mac (tools/ws7/recomp/overrides/mods/mods.c): RS_MODS_FIXES=0 turns the
// fixes off, RS_MODS_BUDGET_MS=N sets the time per frame for mounting DLC archives (default 50; 0
// mounts one per frame, as the original game does), RS_MODS_LOG=1 logs what they did.
// ------------------------------------------------------------------------------------------------
static int env_on(const char *name, int dflt) {
  const char *v = getenv(name);
  if (!v || !*v) return dflt;
  return !(v[0] == '0' || v[0] == 'n' || v[0] == 'N' || v[0] == 'f' || v[0] == 'F');
}
static struct { int init, log, fixes, bypass; double budget_s; } g_cfg;
static void cfg_load(void) {
  if (g_cfg.init) return;
  g_cfg.log = env_on("RS_MODS_LOG", 0);
  g_cfg.bypass = env_on("RS_MODS_BYPASS", 1);
  g_cfg.fixes = env_on("RS_MODS_FIXES", 1);
  g_cfg.budget_s = 0.050;
  const char *b = getenv("RS_MODS_BUDGET_MS");
  if (b && *b) {
    char *end;
    double ms = strtod(b, &end);
    if (end != b && ms >= 0 && ms <= 1000) g_cfg.budget_s = ms / 1000;
  }
  g_cfg.init = 1;
}
// The open-file limit, as on the Mac (rc_nofile.h): each CreateFile holds a host descriptor. At the start of
// lp_main.c, after the log is set up; RS_MODS_FIXES=0 leaves it.
void lp_mods_init(void) {
  cfg_load();
  if (!g_cfg.fixes) return;
  uint64_t old;
  int failed;
  uint64_t now = rc_raise_nofile(&old, &failed);
  if (g_cfg.log && (now != old || failed))
    fprintf(stderr, "mods: open-file limit %llu -> %llu%s\n", (unsigned long long)old, (unsigned long long)now,
            failed ? " (setrlimit failed)" : "");
}
static double now_s(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (double)t.tv_sec + t.tv_nsec * 1e-9;
}
static inline uint8_t *g8(uint32_t g) { return (uint8_t *)RC32_H(g); }
static inline uint32_t ld32(uint32_t g) { uint32_t v; memcpy(&v, RC32_H(g), 4); return v; }
static inline void st32(uint32_t g, uint32_t v) { memcpy(RC32_H(g), &v, 4); }
static void log_counts(void);

// ------------------------------------------------------------------------------------------------
// CDLC (RSBypass): the SNG loader's signature check, 0x10d96a0 (edi = the loader; al: the song file
// verifies). It builds the public key, calls the DSA verifier 0x19bfeb0 on the file and returns its
// answer, with the loader's busy byte +0x21 set around the call. Returning true lets an unsigned
// song file (CDLC) load; official files verify anyway. Only this caller of the verifier is skipped,
// as on the Mac (sng_verify): its other caller (0x130aaed) takes a 0x38-byte signature off the end
// of data that verifies, so forcing it would cut unsigned data short. RS_MODS_BYPASS=0 checks again.
// ------------------------------------------------------------------------------------------------
#define GAME_ALLOC 0x147aa80u      // eax = size, one cdecl argument (0): a heap block
#define CRT_MEMSET 0x1adba30u      // cdecl (p, value, n)
#define SNG_KEY_DECODE 0x10d9730u  // ecx = the encoded key, edx = its length, cdecl (out)
#define DSA_VERIFY 0x19bfeb0u      // cdecl (data, len, key) -> al
#define GAME_ALLOCATOR 0x149e770u  // -> the allocator (its vtable +8 frees: block, 9, -1)
#define NO_FREE_FLAG 0x1ee1926u    // nonzero: blocks are not freed (shutdown)

void F_10d96a0(rc_ctx *c) {
  cfg_load();
  if (g_cfg.bypass) {
    if (g_cfg.log) fprintf(stderr, "mods: CDLC: song file signature check skipped\n");
    ov_ret32(c, 1);
    return;
  }
  // the original check, as the game does it
  uint32_t self = (uint32_t)c->r[7];
  uint32_t buf = ld32(self + 0x10), begin = ld32(self + 0x14), end = ld32(self + 0x18);
  *g8(self + 0x21) = 1;
  uint32_t zero = 0;
  c->r[0] = 0x68d;
  uint32_t key = rc32_call(c, GAME_ALLOC, &zero, 1, 0, 0, 0);
  uint32_t ms[3] = {key, 0, 0x68d};
  rc32_call(c, CRT_MEMSET, ms, 3, 0, 0, 0);
  rc32_call(c, SNG_KEY_DECODE, &key, 1, 0x1e6ea08u, 0x4e7, 0);
  uint32_t va[3] = {buf + begin, end - begin, key};
  uint8_t ok = (uint8_t)rc32_call(c, DSA_VERIFY, va, 3, 0, 0, 0);
  if (key && !*g8(NO_FREE_FLAG)) {
    uint32_t a = ld32(rc32_call(c, GAME_ALLOCATOR, 0, 0, 0, 0, 0));
    uint32_t fa[3] = {key, 9, 0xffffffffu};
    rc32_call(c, ld32(ld32(a) + 8), fa, 3, a, 0, 0);
  }
  *g8(self + 0x21) = 0;
  ov_ret32(c, ok);
}

// ------------------------------------------------------------------------------------------------
// CDLC: the song-key list. The Learn a Song list takes a song (0x10f4f20, called per manifest entry by
// the list builder 0x11da6a0) only when 0x10f41c0 finds the CRC-32 of its SongKey (eax = the key
// string; al: found) in a table of 1590 known keys in the image (0x1d1a0d0, 0x18d8 bytes; the hash
// through the pointer at 0x1ed9160). A custom song's key is not in it, so the song was left out of
// the list even with its archive mounted and its manifest read. The Mac image has no such table.
// Answering true lists every song whose manifest the game reads (official keys are in the table
// anyway); RS_MODS_BYPASS=0 asks the table again (W_10f41c0, skip.txt `wrap`).
// ------------------------------------------------------------------------------------------------
void W_10f41c0(rc_ctx *c);

void F_10f41c0(rc_ctx *c) {
  cfg_load();
  if (!g_cfg.bypass) {
    W_10f41c0(c);
    return;
  }
  ov_ret32(c, 1);
}

// ------------------------------------------------------------------------------------------------
// Batched DLC mounting: the counterpart of the Mac dlc_update (rsmacfixes batch_install_step).
// 0x1449310 (thiscall, ecx = the DLC coordinator) is the coordinator's per-frame update; it calls the
// install step 0x1449580 (esi = coordinator) once per frame, which takes one name off the list of
// dlc/*_p.psarc at +0xe0/+0xe4 and mounts that archive (0x1449a10), or, with the list empty, clears
// the installing flag +6. This native update calls the step in a loop bounded by a frame budget and
// otherwise does what the original does (the same calls in the same order). The step stays
// translated. Coordinator fields:
//   +4, +5   enumeration requested / checks on        +6    installing
//   +0x30    std::map (root +0x34, size +0x40)        +0x68 clear the map once installing is done
//   +0xdc    first update done                        +0xdd follow-up done
// ------------------------------------------------------------------------------------------------
// During the install step an archive in dlc/ counts as owned whatever AppID it carries, as on the Mac (mods.c,
// before_dlc_owned): the game's DLC list is made once at start, so it can't have a song added while the game runs.
// 0x1448b10 (thiscall, the AppID on the stack, `ret 4`; the Mac steam_dlc_owned) stays translated as W_1448b10 for
// everyone else and for RS_MODS_BYPASS=0.
void W_1448b10(rc_ctx *c);
static int dlc_mounting;  // the game thread's

void F_1448b10(rc_ctx *c) {
  cfg_load();
  if (!dlc_mounting || !g_cfg.bypass) {
    W_1448b10(c);
    return;
  }
  uint32_t sp = (uint32_t)c->r[4];
  c->r[0] = 1;
  memcpy(&c->ret_to, RC32_H(sp), 4);
  c->r[4] = sp + 8;  // ret 4
}

#define DLC_UPDATE_HEAD 0x1446570u   // thiscall (coordinator)
#define DLC_INSTALL_STEP 0x1449580u  // esi = coordinator
#define DLC_ENUM_CHECK 0x14493a0u    // esi = coordinator; al: the enumeration has started
#define DLC_UPDATE_TAIL 0x1449660u   // stdcall (coordinator)
#define STD_TREE_DESTROY 0xf964a0u   // thiscall (the map), the root node

void F_1449310(rc_ctx *c) {
  static unsigned n_installed, n_frames;
  static double t_first;
  cfg_load();
  uint32_t self = (uint32_t)c->r[1];
  uint64_t ebx = c->r[3], esi = c->r[6], edi = c->r[7];  // callee-saved
  rc32_call(c, DLC_UPDATE_HEAD, 0, 0, self, 0, 0);
  c->r[6] = self;
  dlc_mounting = 1;
  if (!g_cfg.fixes || g_cfg.budget_s <= 0 || !*g8(self + 6)) {
    rc32_call(c, DLC_INSTALL_STEP, 0, 0, 0, 0, 0);
  } else {
    double t0 = now_s();
    if (!n_frames++) t_first = t0;
    do {
      c->r[6] = self;
      rc32_call(c, DLC_INSTALL_STEP, 0, 0, 0, 0, 0);
      if (*(volatile uint8_t *)g8(self + 6)) n_installed++;  // the step that finds the list empty mounts none
    } while (*(volatile uint8_t *)g8(self + 6) && now_s() - t0 < g_cfg.budget_s);
    if (!*g8(self + 6)) {
      if (g_cfg.log) {
        fprintf(stderr, "mods: DLC: %u archives mounted in %u frames, %.2f s\n", n_installed, n_frames,
                now_s() - t_first);
        log_counts();
      }
      n_installed = n_frames = 0;
    }
  }
  dlc_mounting = 0;
  if (!*g8(self + 0xdc)) *g8(self + 0xdc) = 1;
  if (*g8(self + 5) && *g8(self + 4)) {
    c->r[6] = self;
    if ((uint8_t)rc32_call(c, DLC_ENUM_CHECK, 0, 0, 0, 0, 0)) {
      *g8(self + 4) = 0;
      *g8(self + 0xdd) = 0;
    }
  }
  if (!*g8(self + 0xdd)) rc32_call(c, DLC_UPDATE_TAIL, &self, 1, 0, 0, 0);
  if (*g8(self + 0x68)) {
    uint32_t m = self + 0x30;
    if (!*g8(self + 6) && ld32(m + 0x10)) {  // clear the std::map
      uint32_t root = ld32(m + 4);
      rc32_call(c, STD_TREE_DESTROY, &root, 1, m, 0, 0);
      st32(m + 8, m);
      st32(m + 4, 0);
      st32(m + 0xc, m);
      st32(m + 0x10, 0);
    }
    *g8(self + 0x68) = 0;
  }
  c->r[3] = ebx;
  c->r[6] = esi;
  c->r[7] = edi;
  ov_ret32(c, (uint32_t)c->r[0]);
}

// ------------------------------------------------------------------------------------------------
// Path MD5 cache: the counterpart of the Mac path_md5 (rsmacfixes hook_path_md5). 0x141c130 is the Mac
// path_intern: ecx = the length (-1: a C string), one cdecl argument, the path; eax = the interned
// object, with a reference for the caller. It takes the MD5 of the path (0x141be80) and looks it up in a
// std::map (0x1ed9158), under the path table's lock. Every mounted archive's file checks hash the same
// paths again, quadratic in the number of archives, as on the Mac. The Windows build inlines the Mac's
// wrapper, so the function itself is wrapped (skip.txt `wrap`), and a cached path takes the same object
// with its reference count raised, under the same lock.
// The lock (*0x1ed915c): a CRITICAL_SECTION, then the owner thread +0x18 and the depth +0x1c, which the
// game keeps around it (0x141c370). Object: refs +0x1c. A cached object has a reference of the cache's,
// so it stays until it is evicted: 0x141c3c0 (edi = the object) drops that one.
// ------------------------------------------------------------------------------------------------
#define PATH_TABLE 0x1ed9158u
#define PATH_LOCK 0x1ed915cu
#define PATH_OBJ_REFS 0x1cu
#define PATH_RELEASE 0x141c3c0u
#define PATH_SLOTS 1024  // direct-mapped
void W_141c130(rc_ctx *c);
void w32_cs_enter(rc_ctx *c, uint32_t cs);
void w32_cs_leave(uint32_t cs);

static struct {
  char *s;
  size_t n;
  uint32_t obj;
} path_cache[PATH_SLOTS];
static uint32_t path_cache_table;  // the path table the cached objects belong to
static unsigned long path_hits, path_misses;

static uint32_t path_lock(rc_ctx *c) {
  uint32_t m = ld32(PATH_LOCK);
  w32_cs_enter(c, m);
  st32(m + 0x18, ld32((uint32_t)c->fs + RC32_TEB_TID));
  st32(m + 0x1c, ld32(m + 0x1c) + 1);
  uint32_t table = ld32(PATH_TABLE);
  if (table != path_cache_table) {  // a new path table: the old objects went with the old table
    for (unsigned i = 0; i < PATH_SLOTS; i++) {
      free(path_cache[i].s);
      path_cache[i].s = 0;
      path_cache[i].obj = 0;
    }
    if (path_cache_table && g_cfg.log) fprintf(stderr, "mods: path table recreated; MD5 cache dropped\n");
    path_cache_table = table;
  }
  return m;
}
static void path_unlock(uint32_t m) {
  uint32_t d = ld32(m + 0x1c) - 1;
  st32(m + 0x1c, d);
  if (!d) st32(m + 0x18, 0);
  w32_cs_leave(m);
}

void F_141c130(rc_ctx *c) {
  cfg_load();
  if (!g_cfg.fixes || !ld32(PATH_LOCK)) {  // the game's way; the lock is made later than the first paths
    W_141c130(c);
    return;
  }
  uint32_t esp = (uint32_t)c->r[4], len = (uint32_t)c->r[1];
  const char *s = RC32_H(ld32(esp + 4));
  size_t n = len == 0xffffffffu ? strlen(s) : len;
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < n; i++) h = (h ^ (uint8_t)s[i]) * 16777619u;
  unsigned k = h % PATH_SLOTS;
  uint32_t m = path_lock(c);
  if (path_cache[k].obj && path_cache[k].n == n && !memcmp(path_cache[k].s, s, n)) {
    uint32_t obj = path_cache[k].obj;
    st32(obj + PATH_OBJ_REFS, ld32(obj + PATH_OBJ_REFS) + 1);
    path_hits++;
    path_unlock(m);
    ov_ret32(c, obj);
    return;
  }
  path_misses++;
  path_unlock(m);
  char *copy = malloc(n + 1);
  if (copy) {
    memcpy(copy, s, n);  // before the call: the caller's buffer may be the guest stack's
    copy[n] = 0;
  }
  W_141c130(c);  // +1 reference, for the caller
  uint32_t obj = (uint32_t)c->r[0], evicted = 0;
  if (obj && copy) {
    m = path_lock(c);
    evicted = path_cache[k].obj;
    free(path_cache[k].s);
    path_cache[k].s = copy;
    path_cache[k].n = n;
    path_cache[k].obj = obj;
    st32(obj + PATH_OBJ_REFS, ld32(obj + PATH_OBJ_REFS) + 1);  // the cache's own reference
    path_unlock(m);
  } else {
    free(copy);
  }
  if (evicted) {
    uint64_t edi = c->r[7];
    uint32_t ret_to = c->ret_to;
    c->r[7] = evicted;
    rc32_call(c, PATH_RELEASE, 0, 0, 0, 0, 0);
    c->r[7] = edi;
    c->ret_to = ret_to;
    c->r[0] = obj;
  }
}

// ------------------------------------------------------------------------------------------------
// Number-table hash: the counterpart of the Mac num_dtor, num_intern and num_rehash (rsmacfixes SITES).
// The game keeps one table of number objects, keyed by value, and hashes a double by its integer part,
// so every value in [0, 1) lands in one bucket. The Windows build's table is a Dinkumware hash_map
// (*0x1ee14c8) and its hash is a function of its own, 0x1400650 (eax = &key; eax = the hash:
// (long)v ^ 0xdeadbeef, then a minimal-standard generator step), which its find (0x1400460), equal_range
// (0x1400510), erase (0x14005c0) and insert (0x14006f0) all call: only that function changes here, where
// the Mac replaces the three functions it is inlined into. As on the Mac the hash is the value's bits,
// with -0 made +0 so equal numbers hash equally; the table masks it, so the bits are mixed down.
// ------------------------------------------------------------------------------------------------
void W_1400650(rc_ctx *c);
static unsigned long num_hashes;

void F_1400650(rc_ctx *c) {
  cfg_load();
  if (!g_cfg.fixes) {
    W_1400650(c);
    return;
  }
  double v;
  memcpy(&v, RC32_H((uint32_t)c->r[0]), 8);
  double z = 0.0 + v;  // -0 -> +0
  uint64_t u;
  memcpy(&u, &z, 8);
  u = (u ^ u >> 33) * 0xff51afd7ed558ccdull;
  u = (u ^ u >> 33) * 0xc4ceb9fe1a85ec53ull;
  num_hashes++;
  ov_ret32(c, (uint32_t)(u ^ u >> 33) & 0x7fffffffu);
}

static void log_counts(void) {
  fprintf(stderr, "mods: paths: %lu from the cache, %lu hashed; numbers: %lu hashed\n", path_hits, path_misses, num_hashes);
}

// ------------------------------------------------------------------------------------------------
// The DLC preview's download jobs, as the Mac dlc_job_preview and dlc_job_request
// (tools/ws7/recomp/overrides/src/online_glue.c): RSRDVDownloadManager's queue functions 0x144bc80
// (name, job) and 0x144bdb0 (type, job), thiscall on the manager at 0x1ee17d8, which goes to Ubisoft's
// servers. Offline every request ends at once, on the calling thread, as a failed transfer: the job's
// completion (slot 3, 0x1445f10: failed = true) releases the in-flight state, which the coordinator's
// shutdown waits for (0x1443960), and the job is deleted (slot 0, flag 1). The answer is true when a
// job was taken, as the original's. Job: the 0x14-byte Callback<RSDLCCoordinator> or
// Callback<PreviewFile>.
// ------------------------------------------------------------------------------------------------
#define DLC_JOB_DELETE_SLOT 0u
#define DLC_JOB_DONE_SLOT 3u

static void dlc_job_fail(rc_ctx *c) {
  uint32_t esp = (uint32_t)c->r[4], job = ld32(esp + 8), one = 1;
  cfg_load();
  if (g_cfg.log) fprintf(stderr, "mods: DLC preview: download job 0x%x failed (offline)\n", job);
  if (job) {
    rc32_call(c, ld32(ld32(job) + 4 * DLC_JOB_DONE_SLOT), &one, 1, job, 0, 0);
    rc32_call(c, ld32(ld32(job) + 4 * DLC_JOB_DELETE_SLOT), &one, 1, job, 0, 0);
  }
  c->r[0] = job != 0;
  memcpy(&c->ret_to, RC32_H(esp), 4);
  c->r[4] = esp + 12;  // ret 8
}
void F_144bc80(rc_ctx *c) { dlc_job_fail(c); }
void F_144bdb0(rc_ctx *c) { dlc_job_fail(c); }
// The manager's flush (0x144bbd0, esi = the manager; the Mac dlc_worker_flush): nothing in flight. The
// coordinator's shutdown calls it with a manager that was never made offline.
void F_144bbd0(rc_ctx *c) { ov_ret32(c, 0); }
