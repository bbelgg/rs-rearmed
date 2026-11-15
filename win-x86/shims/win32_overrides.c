// Native replacements for game functions dropped from translation (tools/targets/names/overrides/win32.list,
// of which the build makes the translator's skip file): the Windows counterpart of the Mac link-level
// overrides (tools/ws7/recomp/overrides). A function named there gets no W_<addr> unless its line says `wrap`;
// gen_image_pe.py emits the rc32_table entry {addr, F_<addr>} and the linker resolves F_<addr> to the
// definition below (win32_shims.a). Each is entered as the translated function would be: the return address
// at guest esp, the arguments above it, the result left in eax; it pops the return address into c->ret_to as a
// guest `ret` does (and removes stdcall arguments).
// One that is also a hook point (tools/targets/names/hooks.list) is N_<addr> here: the generated F_<addr>
// runs a mod's hooks around it, and a mod sees the function as the game now has it.
//
// The game's functions, globals and fields are named, as in the Mac's (runtime/rc_abi.h): RC_FN(name),
// RC_WFN(name) and RC_NFN(name) are a function's F_, W_ and N_ symbols, RC_ADDR(name) its or a global's
// address in the guest (the image is mapped where it was linked, so it is what rc32_call and RC32_H take),
// RC_OFF(name) a field and RC_SLOT(name) a vtable slot. Their values are tools/targets/win-x86's; the
// addresses in the comments are evidence, as there.
#include <ctype.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

#include "rc_abi.h"
#include "mods_shared.h"

// `ret` (ret 0): the return address into c->ret_to, esp past it. For a function with no stack
// arguments (a register `this`, or none); the same record a translated `ret` makes (rc32_gen.h),
// as win32_crtmath.c's crt_ret does.
static inline void ov_ret32(rc_ctx *c, uint32_t eax) {
  c->r[0] = eax;
  uint32_t sp = (uint32_t)c->r[4];
  memcpy(&c->ret_to, RC32_H(sp), 4);
  c->r[4] = sp + 4;
}

// No intro screens: not here. The intro skip is the Mac's source, a pre hook on boot_intro_update
// (tools/ws7/recomp/overrides/mods/mods_shared.c), which the start below adds.

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

// Open `path` (guest, lower case) through the front end; the stream (0: none) with a reference for the caller.
static uint32_t fe_open(rc_ctx *c, uint32_t path, uint32_t slot) {
  uint32_t fs = rd32(RC_ADDR(frontend_fs));
  wr32(slot, 0);
  uint32_t args[2] = {path, slot};
  rc32_call(c, rd32(rd32(fs) + 4 * RC_SLOT(frontend_fs_open)), args, 2, fs, 0, 0);
  uint32_t s = rd32(slot);
  if (s) rc32_call(c, rd32(rd32(s) + 4 * RC_SLOT(stream_add_ref)), 0, 0, s, 0, 0);
  return s;
}

void RC_NFN(frontend_open_file)(rc_ctx *c) {
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
  if (rs_mods_cfg.overlay && is_cache_7z(b + pn)) s = fe_open(c, buf, slot);
  if (!s) s = fe_open(c, buf + pn, slot);
  c->r[4] = esp;
  wr32(out, s);
  ov_ret32(c, out);
}

// ------------------------------------------------------------------------------------------------
// The switches, the log and the start are the Mac's, one source (tools/ws7/recomp/overrides/mods/mods_shared.c,
// which says what each switch is): RS_MODS_FIXES=0 turns the fixes off, RS_MODS_BUDGET_MS=N sets the time per
// frame for mounting DLC archives (default 50; 0 mounts one per frame, as the original game does), RS_MODS_LOG=1
// logs what they did. The start reads them, raises the open-file limit as on the Mac (each CreateFile holds a
// host descriptor; RS_MODS_FIXES=0 leaves it) and adds the hooks of the fixes that are the Mac's source: the
// intro skip, the song ownership during a mount, the frame rate fixes (framerate.c). At the start of lp_main.c,
// once settings.env is in the environment and the log is set up.
// ------------------------------------------------------------------------------------------------
void lp_mods_init(void) { rs_mods_start(); }
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
// No Auto Save warning: the counterpart of the Mac skip (tools/ws7/recomp/overrides/mods/mods.c,
// before_autosave_warning), off with the same RS_MODS_AUTOSAVE=0. The handler is 0x120adf0, the Mac
// autosave_warning: a virtual (vtable slot at 0x1d3c34c), thiscall (category, event, payload), `ret 0xc`,
// `this` in ecx on entry and in ebx from 0x120ae1b on. `this` is the menu's listener at +0x2c0
// (0x120939b). With category "UI", event "OnSignInFlowComplete" and the sign-in predicate false (slot 0x1e of the
// global 0x1ef5e90, argument 1) it runs the input mode step (0x120ae72-0x120ae8d, inlined here: the Mac
// input_mode_restore), makes the RS2AutosaveWarning dialog with the navigate call 0x115db90 at 0x120aec9, stores
// it at this+0x14 (its reference count at +4, release at vtable +4), and writes 4.0 to the timer at this+0x20.
//
// The menu's tick 0x1208fe0 (the Mac 0x1005e48b0) reads the same two fields as +0x2d4 and +0x2e0: only with
// the slot non-NULL and the timer at most zero does it dismiss the dialog by name (0x115e3e0, which does
// nothing for a name it doesn't find), drop the slot's reference and go on (0x120a810). A NULL slot would stop the
// flow on the profile screen, as on the Mac, so the call's result is a token: guest memory with a high count, so
// no release calls through it.
//
// The Mac copies the branch into a pre hook. Here only the navigate call is left out and the handler runs
// as it is, so the input mode step, the slot, the string and the release of the third argument (0x120af52) all
// happen by construction, and a Direct Connect profile keeps its input (without that step it stops at the tuner with
// "It sounds like your cable is unplugged"). The timer is written after the call, so the handler's N_ clears it
// once the handler has returned, and the tick advances on its next run.
// ------------------------------------------------------------------------------------------------
#define AUTOSAVE_TOKEN_REFS 1000000u
void RC_WFN(ui_navigate)(rc_ctx *c);
void RC_WFN(autosave_warning)(rc_ctx *c);
static uint32_t autosave_token;  // made once, for the life of the process
static int autosave_skipped;     // the game thread's: ui_navigate's F_ tells autosave_warning's N_

// navigate (edi = the name), stdcall (menu system, Menu **out, payload); eax = out. The handler's payload is NULL.
void RC_FN(ui_navigate)(rc_ctx *c) {
  uint32_t sp = (uint32_t)c->r[4], ret = ld32(sp);
  if (ret != RC_ADDR(autosave_navigate_ret) || !rs_mods_cfg.autosave) {
    RC_WFN(ui_navigate)(c);
    return;
  }
  if (!autosave_token && (autosave_token = rc32_alloc(16))) {
    st32(autosave_token, 0);
    st32(autosave_token + RC_OFF(menu_refs), AUTOSAVE_TOKEN_REFS);
  }
  if (!autosave_token) {  // out of memory: the original shows the dialog
    RC_WFN(ui_navigate)(c);
    return;
  }
  uint32_t out = ld32(sp + 8);
  st32(out, autosave_token);
  autosave_skipped = 1;
  c->r[0] = out;
  c->ret_to = ret;
  c->r[4] = sp + 16;  // ret 0xc
}

void RC_NFN(autosave_warning)(rc_ctx *c) {
  uint32_t self = (uint32_t)c->r[1];
  RC_WFN(autosave_warning)(c);
  if (!autosave_skipped) return;
  autosave_skipped = 0;
  memset(g8(self + RC_OFF(autosave_timer)), 0, sizeof(double));
  rs_mods_logf("auto save warning skipped");
}

// ------------------------------------------------------------------------------------------------
// Two players on any input: the counterpart of the Mac's before_is_cable (tools/ws7/recomp/overrides/mods/mods.c,
// which says why), off with the same RS_MODS_MULTIPLAYER=0. 0x11f08d0 is the Mac's three inlined checks in one
// function (input_p2_force_cable): it sets Player 2 to Real Tone unless they are, and returns al = it did. The
// fix's other half, Player 2's saved input, is the Mac's source: a pre hook on input_bind (mods_shared.c).
// ------------------------------------------------------------------------------------------------
// The game's own: input_mode, cdecl (player) -> al.
void RC_WFN(input_p2_force_cable)(rc_ctx *c);

void RC_FN(input_p2_force_cable)(rc_ctx *c) {
  uint32_t player = RS_MODS_PLAYER_2;
  uint8_t mode = rs_mods_cfg.multiplayer ? (uint8_t)rc32_call(c, RC_ADDR(input_mode), &player, 1, 0, 0, 0) : 0;
  if (rs_mods_any_input(mode)) ov_ret32(c, 0);
  else RC_WFN(input_p2_force_cable)(c);
}

// ------------------------------------------------------------------------------------------------
// CDLC (RSBypass): the SNG loader's signature check, 0x10d96a0 (edi = the loader; al: the song file
// verifies). It builds the public key, calls the DSA verifier 0x19bfeb0 on the file and returns its
// answer, with the loader's busy byte +0x21 set around the call. Returning true lets an unsigned
// song file (CDLC) load; official files verify anyway. Only this caller of the verifier is skipped,
// as on the Mac (sng_verify): its other caller (0x130aaed) takes a 0x38-byte signature off the end
// of data that verifies, so forcing it would cut unsigned data short. RS_MODS_BYPASS=0 checks again.
// ------------------------------------------------------------------------------------------------
// The game's own: efd_malloc, eax = size, one cdecl argument (0): a heap block; crt_memset, cdecl (p, value, n);
// rsg_bpe_expand, ecx = the encoded key, edx = its length, cdecl (out); sng_verify, cdecl (data, len, key) -> al;
// efd_allocator, -> the allocator (its free: block, 9, -1); efd_no_free, nonzero: blocks are not freed (shutdown).
#define SNG_KEY_SIZE 0x68du   // the decoded key's buffer
#define SNG_KEY_BLOB 0x4e7u   // key_sng_dsa_blob's length, an immediate at the call

void RC_FN(sng_check)(rc_ctx *c) {
  if (rs_mods_cfg.bypass) {
    rs_mods_logf("CDLC: song file signature check skipped");
    ov_ret32(c, 1);
    return;
  }
  // the original check, as the game does it
  uint32_t self = (uint32_t)c->r[7];
  uint32_t buf = ld32(self + RC_OFF(sng_loader_data)), begin = ld32(self + RC_OFF(sng_loader_begin)),
           end = ld32(self + RC_OFF(sng_loader_end));
  *g8(self + RC_OFF(sng_loader_busy)) = 1;
  uint32_t zero = 0;
  c->r[0] = SNG_KEY_SIZE;
  uint32_t key = rc32_call(c, RC_ADDR(efd_malloc), &zero, 1, 0, 0, 0);
  uint32_t ms[3] = {key, 0, SNG_KEY_SIZE};
  rc32_call(c, RC_ADDR(crt_memset), ms, 3, 0, 0, 0);
  rc32_call(c, RC_ADDR(rsg_bpe_expand), &key, 1, RC_ADDR(key_sng_dsa_blob), SNG_KEY_BLOB, 0);
  uint32_t va[3] = {buf + begin, end - begin, key};
  uint8_t ok = (uint8_t)rc32_call(c, RC_ADDR(sng_verify), va, 3, 0, 0, 0);
  if (key && !*g8(RC_ADDR(efd_no_free))) {
    uint32_t a = ld32(rc32_call(c, RC_ADDR(efd_allocator), 0, 0, 0, 0, 0));
    uint32_t fa[3] = {key, 9, 0xffffffffu};
    rc32_call(c, ld32(ld32(a) + 4 * RC_SLOT(efd_allocator_free)), fa, 3, a, 0, 0);
  }
  *g8(self + RC_OFF(sng_loader_busy)) = 0;
  ov_ret32(c, ok);
}

// ------------------------------------------------------------------------------------------------
// CDLC: the song-key list. The Learn a Song list takes a song (0x10f4f20, called per manifest entry by
// the list builder 0x11da6a0) only when 0x10f41c0 finds the CRC-32 of its SongKey (eax = the key
// string; al: found) in a table of 1590 known keys in the image (0x1d1a0d0, 0x18d8 bytes; the hash
// through the pointer at 0x1ed9160). A custom song's key is not in it, so the song was left out of
// the list even with its archive mounted and its manifest read. The Mac image has no such table.
// Answering true lists every song whose manifest the game reads (official keys are in the table
// anyway); RS_MODS_BYPASS=0 asks the table again (its W_, win32.list `wrap`).
// ------------------------------------------------------------------------------------------------
void RC_WFN(song_key_known)(rc_ctx *c);

void RC_FN(song_key_known)(rc_ctx *c) {
  if (!rs_mods_cfg.bypass) {
    RC_WFN(song_key_known)(c);
    return;
  }
  ov_ret32(c, 1);
}

// ------------------------------------------------------------------------------------------------
// Batched DLC mounting: the counterpart of the Mac dlc_update (rsmacfixes batch_install_step).
// 0x1449310 (thiscall, ecx = the DLC coordinator) is the coordinator's per-frame update; it calls the
// install step 0x1449580 (esi = coordinator) once per frame, which takes one name off the list of
// dlc/*_p.psarc at +0xe0/+0xe4 and mounts that archive (0x1449a10), or, with the list empty, clears
// the installing flag. This native update calls the step in a loop bounded by a frame budget and
// otherwise does what the original does (the same calls in the same order). The step stays
// translated. Coordinator fields (tools/targets, as the Mac's dlc_update reads them):
//   dlc_pending, dlc_check_on   enumeration requested / checks on    dlc_installing  installing
//   dlc_map                     std::map (dlc_map_root, _begin, _end, _size)
//   dlc_clear_map               clear the map once installing is done
//   dlc_first_done              first update done                    dlc_tail_done   follow-up done
// ------------------------------------------------------------------------------------------------
// During the install steps an archive in dlc/ counts as owned whatever AppID it carries, as on the Mac: the Mac's
// source, mods_shared.c's hook on steam_dlc_owned, which rs_mods_mounting tells when the steps run.
extern _Atomic int w32_dlc_archives_open;  // win32_ksync.c: the song archives of dlc/ open now
extern _Atomic unsigned w32_dlc_archive_opens;

// The game's own: dlc_update_head, thiscall (coordinator); dlc_install_step, esi = coordinator; dlc_enum_check,
// esi = coordinator, al: the enumeration has started; dlc_update_tail, stdcall (coordinator); std_tree_destroy,
// thiscall (the map), the root node. MAP_FIELD: a field of the coordinator's map, from the map (the tables count
// them from the coordinator).
#define MAP_FIELD(m, off) ((m) + ((off) - RC_OFF(dlc_map)))

void RC_FN(dlc_update)(rc_ctx *c) {
  static unsigned n_installed, n_frames;
  static double t_first;
  uint32_t self = (uint32_t)c->r[1];
  uint64_t ebx = c->r[3], esi = c->r[6], edi = c->r[7];  // callee-saved
  rc32_call(c, RC_ADDR(dlc_update_head), 0, 0, self, 0, 0);
  c->r[6] = self;
  rs_mods_mounting(1);
  if (!rs_mods_cfg.fixes || rs_mods_cfg.budget_s <= 0 || !*g8(self + RC_OFF(dlc_installing))) {
    rc32_call(c, RC_ADDR(dlc_install_step), 0, 0, 0, 0, 0);
  } else {
    // The step takes a name off the list and starts mounting that archive, whose file it leaves open. The mount is
    // finished by the update's head (0x1446570, above: it closes the file; seen with LP_LOG_DLC_CLOSE), which the
    // game calls once a frame, and a step taken before that takes the next name off the list and mounts nothing.
    // (Steps alone for as long as a frame budget allowed mounted one archive a frame and dropped the rest: of 2250
    // about 150, their songs missing until an enumeration. One step a frame mounts every one, in 47 s for 2250.)
    // So after a step that left its archive open, the head is called until that file is closed, and only then the
    // next step is taken: several archives a frame, none dropped. `level`: how many archives were open before the
    // step whose mount is not finished yet (another may be open for as long as it likes: a song playing), -1: none.
    // One that does not finish within the frame is waited for in the next, without a step; after a second of that
    // it is taken to be an archive the game keeps open, and the steps go on.
    static int level = -1;
    static double waiting_since;
    static unsigned n_dropped;
    double t0 = now_s();
    if (!n_frames++) t_first = t0, n_dropped = 0, level = -1;
    do {
      if (level >= 0) {
        while (atomic_load(&w32_dlc_archives_open) > level && now_s() - t0 < rs_mods_cfg.budget_s) {
          rc32_call(c, RC_ADDR(dlc_update_head), 0, 0, self, 0, 0);
          if (atomic_load(&w32_dlc_archives_open) > level) usleep(50);  // what the head waits for is another thread's
        }
        if (atomic_load(&w32_dlc_archives_open) > level && now_s() - waiting_since < 1.0) break;
        level = -1;
      }
      int before = atomic_load(&w32_dlc_archives_open);
      unsigned opens = atomic_load(&w32_dlc_archive_opens);
      c->r[6] = self;
      rc32_call(c, RC_ADDR(dlc_install_step), 0, 0, 0, 0, 0);
      // the step that finds the list empty mounts none
      if (*(volatile uint8_t *)g8(self + RC_OFF(dlc_installing))) n_installed++;
      if (atomic_load(&w32_dlc_archive_opens) - opens == 3) n_dropped++;  // opened three times to look, not a fourth
      if (atomic_load(&w32_dlc_archives_open) > before) level = before, waiting_since = now_s();
    } while (*(volatile uint8_t *)g8(self + RC_OFF(dlc_installing)) && now_s() - t0 < rs_mods_cfg.budget_s);
    if (!*g8(self + RC_OFF(dlc_installing))) {
      if (n_dropped) rs_mods_logf("DLC: %u archives were not mounted", n_dropped);
      rs_mods_logf("DLC: %u archives mounted in %u frames, %.2f s", n_installed, n_frames, now_s() - t_first);
      log_counts();
      n_installed = n_frames = 0;
    }
  }
  rs_mods_mounting(0);
  if (!*g8(self + RC_OFF(dlc_first_done))) *g8(self + RC_OFF(dlc_first_done)) = 1;
  if (*g8(self + RC_OFF(dlc_check_on)) && *g8(self + RC_OFF(dlc_pending))) {
    c->r[6] = self;
    if ((uint8_t)rc32_call(c, RC_ADDR(dlc_enum_check), 0, 0, 0, 0, 0)) {
      *g8(self + RC_OFF(dlc_pending)) = 0;
      *g8(self + RC_OFF(dlc_tail_done)) = 0;
    }
  }
  if (!*g8(self + RC_OFF(dlc_tail_done))) rc32_call(c, RC_ADDR(dlc_update_tail), &self, 1, 0, 0, 0);
  if (*g8(self + RC_OFF(dlc_clear_map))) {
    uint32_t m = self + RC_OFF(dlc_map);
    if (!*g8(self + RC_OFF(dlc_installing)) && ld32(MAP_FIELD(m, RC_OFF(dlc_map_size)))) {  // clear the std::map
      uint32_t root = ld32(MAP_FIELD(m, RC_OFF(dlc_map_root)));
      rc32_call(c, RC_ADDR(std_tree_destroy), &root, 1, m, 0, 0);
      st32(MAP_FIELD(m, RC_OFF(dlc_map_begin)), m);
      st32(MAP_FIELD(m, RC_OFF(dlc_map_root)), 0);
      st32(MAP_FIELD(m, RC_OFF(dlc_map_end)), m);
      st32(MAP_FIELD(m, RC_OFF(dlc_map_size)), 0);
    }
    *g8(self + RC_OFF(dlc_clear_map)) = 0;
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
// wrapper, so the function itself is wrapped (win32.list `wrap`), and a cached path takes the same object
// with its reference count raised, under the same lock.
// The lock (*0x1ed915c, path_table_cs): a CRITICAL_SECTION, then the owner thread +0x18 and the depth +0x1c
// (cs_lock_owner, cs_lock_depth), which the game keeps around it (0x141c370). Object: refs +0x1c. A cached object
// has a reference of the cache's, so it stays until it is evicted: path_release (0x141c3c0, edi = the object)
// drops that one.
// ------------------------------------------------------------------------------------------------
#define PATH_OBJ_REFS RC_OFF(path_obj_refs)
#define PATH_SLOTS 1024  // direct-mapped
void RC_WFN(path_intern)(rc_ctx *c);
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
  uint32_t m = ld32(RC_ADDR(path_table_cs));
  w32_cs_enter(c, m);
  st32(m + RC_OFF(cs_lock_owner), ld32((uint32_t)c->fs + RC32_TEB_TID));
  st32(m + RC_OFF(cs_lock_depth), ld32(m + RC_OFF(cs_lock_depth)) + 1);
  uint32_t table = ld32(RC_ADDR(path_table));
  if (table != path_cache_table) {  // a new path table: the old objects went with the old table
    for (unsigned i = 0; i < PATH_SLOTS; i++) {
      free(path_cache[i].s);
      path_cache[i].s = 0;
      path_cache[i].obj = 0;
    }
    if (path_cache_table) rs_mods_logf("path table recreated; MD5 cache dropped");
    path_cache_table = table;
  }
  return m;
}
static void path_unlock(uint32_t m) {
  uint32_t d = ld32(m + RC_OFF(cs_lock_depth)) - 1;
  st32(m + RC_OFF(cs_lock_depth), d);
  if (!d) st32(m + RC_OFF(cs_lock_owner), 0);
  w32_cs_leave(m);
}

void RC_FN(path_intern)(rc_ctx *c) {
  // the game's way with the fixes off, and until its lock is made: later than the first paths
  if (!rs_mods_cfg.fixes || !ld32(RC_ADDR(path_table_cs))) {
    RC_WFN(path_intern)(c);
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
  RC_WFN(path_intern)(c);  // +1 reference, for the caller
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
    rc32_call(c, RC_ADDR(path_release), 0, 0, 0, 0, 0);
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
void RC_WFN(num_hash)(rc_ctx *c);
static unsigned long num_hashes;

void RC_FN(num_hash)(rc_ctx *c) {
  if (!rs_mods_cfg.fixes) {
    RC_WFN(num_hash)(c);
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
  rs_mods_logf("paths: %lu from the cache, %lu hashed; numbers: %lu hashed", path_hits, path_misses, num_hashes);
}

// ------------------------------------------------------------------------------------------------
// The DLC preview's download jobs, as the Mac dlc_job_preview and dlc_job_request
// (tools/ws7/recomp/overrides/src/online_glue.c): RSRDVDownloadManager's queue functions 0x144bc80
// (name, job) and 0x144bdb0 (type, job), thiscall on the manager at 0x1ee17d8, which goes to Ubisoft's
// servers. Offline every request ends at once, on the calling thread, as a failed transfer: the job's
// completion (dlc_job_fail_slot, 0x1445f10: failed = true) releases the in-flight state, which the coordinator's
// shutdown waits for (0x1443960), and the job is deleted (dlc_job_delete_slot, flag 1). The answer is true when a
// job was taken, as the original's. Job: the 0x14-byte Callback<RSDLCCoordinator> or
// Callback<PreviewFile>.
// ------------------------------------------------------------------------------------------------
static void dlc_job_fail(rc_ctx *c) {
  uint32_t esp = (uint32_t)c->r[4], job = ld32(esp + 8), one = 1;
  rs_mods_logf("DLC preview: download job 0x%x failed (offline)", job);
  if (job) {
    rc32_call(c, ld32(ld32(job) + 4 * RC_SLOT(dlc_job_fail)), &one, 1, job, 0, 0);
    rc32_call(c, ld32(ld32(job) + 4 * RC_SLOT(dlc_job_delete)), &one, 1, job, 0, 0);
  }
  c->r[0] = job != 0;
  memcpy(&c->ret_to, RC32_H(esp), 4);
  c->r[4] = esp + 12;  // ret 8
}
void RC_FN(dlc_job_preview)(rc_ctx *c) { dlc_job_fail(c); }
void RC_FN(dlc_job_request)(rc_ctx *c) { dlc_job_fail(c); }
// The manager's flush (0x144bbd0, esi = the manager; the Mac dlc_worker_flush): nothing in flight. The
// coordinator's shutdown calls it with a manager that was never made offline.
void RC_FN(dlc_worker_flush)(rc_ctx *c) { ov_ret32(c, 0); }

// ------------------------------------------------------------------------------------------------
// Any window or screen shape, as the Mac edition (RS_WINDOW_ANY_SHAPE, on unless 0, read by
// win-x86/app/lp_window.m, which calls these only when it is on; the Mac's is in rsp_shell.m).
//
// This game draws 16:9 whatever its resolution, where the Mac game draws at its resolution's shape. Its
// camera setup (0x1539470; the Mac's 0x1011684d0) makes a perspective frustum's width its height times
// the double 16/9 at 0x1da5e18 (0x1539adb), and an orthographic one with a set height the same way
// (0x1539a06); the Mac's takes its render target's width / height there (0x101168cdc). And
// GRDisplayRatioAdjuster's update (0x13ab190, at the end of a frame) compares the window's client size
// (GetClientRect) with the same constant (0x13ab236): more than 0.01 apart, or a client not the renderer's
// size, and it stretches the renderer's picture into a 16:9 band of its own surface and back into the
// back buffer (StretchRect twice), with bars, and maps the pointer to match (0x13ab9e0). Nothing else
// reads the constant. lp_game_shape makes it the back buffer's width / height at each CreateDevice and
// Reset (on the game's thread, before its next frame), so the cameras keep their height and take the
// width, as the Mac's do, and the fit finds nothing to do. A 16:9 back buffer keeps the constant as the
// game has it, so a 16:9 screen draws as before to the bit.
//
// A window resized on the Mac becomes the game's resolution, as the Mac game's shell does with a resized
// window (lp_request_resolution, from lp_window.m). This game has no such path (its window procedure takes
// no WM_SIZE, and its windows can't be resized on Windows), so the game's thread applies the size at its
// next PeekMessage (w32_pump, win32_user32.c) through the game's own settings calls: the read 0x13afbd0
// (esi = a 0x44-byte record: +0 width, +4 height) and the apply 0x13afc50 (eax = the record), as its pick
// of a display mode does (0x130c3a9). The apply resets the device (d3d9_platform_resize, and so
// lp_game_shape). The game takes F11 the same way, from its window procedure inside the message loop
// (0x130d905). The window's client size changes first, as a user's drag makes it on Windows, so the fit's
// comparison with the renderer's size holds. Only in Windowed mode: in the game's two full-screen modes the
// device keeps the desktop's or the game's own size whatever the record says (a window showing them, from
// RS_FULLSCREEN=0 or Control-Command-F, has the layer fit that back buffer), and a client size changed
// alone would bring the fit's bars back.
// ------------------------------------------------------------------------------------------------
// The game's own: display_ratio_16_9, a double, 1.7777777910232544 (16/9 as a float); settings_read, esi = the
// record; settings_apply, eax = the record (0x44 bytes: settings_width, settings_height, and settings_mode, the
// Windowed Mode flags as set_fullscreen, 0x13afd30, reads them).
#define MODE_NONEXCLUSIVE 0x80u    // Non-Exclusive Fullscreen (a windowed device the desktop's size)
#define MODE_EXCLUSIVE 0x04u       // Exclusive Fullscreen; neither: Windowed
void w32_window_size(uint32_t hwnd, int w, int h);  // win32_user32.c

static pthread_t shape_thread;  // the thread that makes and resets the device
static _Atomic int shape_thread_set;
static _Atomic uint64_t res_request;  // hwnd << 32 | width << 16 | height; 0: none

void lp_game_shape(int w, int h) {
  static double game_16_9;
  static int have;
  if (!have) {
    memcpy(&game_16_9, RC32_H(RC_ADDR(display_ratio_16_9)), 8);
    have = 1;
  }
  if (!atomic_load(&shape_thread_set)) {
    shape_thread = pthread_self();
    atomic_store(&shape_thread_set, 1);
  }
  double r = w > 0 && h > 0 && (int64_t)w * 9 != (int64_t)h * 16 ? (double)w / h : game_16_9, was;
  memcpy(&was, RC32_H(RC_ADDR(display_ratio_16_9)), 8);
  if (r == was) return;
  memcpy(RC32_H(RC_ADDR(display_ratio_16_9)), &r, 8);
  fprintf(stderr, "lp: the game draws %dx%d at its own shape (%.4f; 16:9 is %.4f)\n", w, h, r, game_16_9);
}

void lp_request_resolution(uint32_t hwnd, int w, int h) {
  if (w <= 0 || h <= 0 || w > 0xffff || h > 0xffff) return;
  atomic_store(&res_request, (uint64_t)hwnd << 32 | (uint64_t)w << 16 | (uint64_t)h);
}

void w32_pump(rc_ctx *c) {
  if (!atomic_load_explicit(&res_request, memory_order_relaxed) || !atomic_load(&shape_thread_set) ||
      !pthread_equal(pthread_self(), shape_thread))
    return;
  uint64_t q = atomic_exchange(&res_request, 0);  // the apply peeks too: it finds none
  if (!q) return;
  uint32_t hwnd = (uint32_t)(q >> 32);
  int w = (int)(q >> 16 & 0xffff), h = (int)(q & 0xffff);
  uint64_t saved[8];  // PeekMessage's caller sees its registers kept (eax is the shim's)
  memcpy(saved, c->r, sizeof saved);
  uint32_t esp = (uint32_t)c->r[4], rec = (esp - 0x100) & ~0xfu;  // below PeekMessage's arguments
  c->r[4] = rec;
  c->r[6] = rec;
  rc32_call(c, RC_ADDR(settings_read), 0, 0, 0, 0, 0);
  uint32_t ow = ld32(rec + RC_OFF(settings_width)), oh = ld32(rec + RC_OFF(settings_height));
  if (*g8(rec + RC_OFF(settings_mode)) & (MODE_NONEXCLUSIVE | MODE_EXCLUSIVE)) {
    // the game's full screen shown in a window (RS_FULLSCREEN=0, Control-Command-F): its device stays the
    // desktop's or its own size whatever the record says, so it keeps it, and the layer fits it to the window
    memcpy(c->r, saved, sizeof saved);
    fprintf(stderr, "lp: the game is in full screen (Windowed Mode is not Windowed): its resolution stays\n");
    return;
  }
  st32(rec + RC_OFF(settings_width), (uint32_t)w);
  st32(rec + RC_OFF(settings_height), (uint32_t)h);
  w32_window_size(hwnd, w, h);
  c->r[0] = rec;
  rc32_call(c, RC_ADDR(settings_apply), 0, 0, 0, 0, 0);
  memcpy(c->r, saved, sizeof saved);
  fprintf(stderr, "lp: the game's resolution %ux%u -> %dx%d, the window's\n", ow, oh, w, h);
}
