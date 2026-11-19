// The built-in fixes' part that is one source for both editions (overrides/mods/mods_shared.c), on the two
// runtimes (Makefile): the same object, built as a mod is, beside a 64-bit runtime with the Mac's kind of tables
// and a 32-bit one with Learn & Play's. The tables below are what targets.py generates in a real build, with
// values of the test's own.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef RC_GUEST32
#include "rc32.h"
#include "rc32_hook.h"
#endif
#include "rc_cc.h"
#include "rc_hook.h"
#include "rc_mid.h"

#include "mods_shared.h"

static void log_end(void);  // a failure while the log is captured is said on the real stderr
#define CHECK(x) do { if (!(x)) { log_end(); \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)
#define CHECK_EQ(a, b) do { uint64_t a_ = (a), b_ = (b); if (a_ != b_) { log_end(); \
    fprintf(stderr, "FAIL %s:%d: %s = 0x%llx, not 0x%llx\n", __FILE__, __LINE__, #a, (unsigned long long)a_, \
            (unsigned long long)b_); exit(1); } } while (0)

const char rc_target_id[] = "test";

// --- the target's tables: the hook points of the fixes, each game's own convention and field offsets, and what
// the fixes call and read in the game by name: its functions at ADDR, its data at DATA
enum { INTRO, OWNED, BIND, NPOINTS, MODE = 8, PROFILE, MANAGER, BYNAME, DESTROY, FSEXISTS = 14, MIDS = 16 };
#define ADDR(k) (0x401000u + 0x100u * (k))
#define DATA 0x402000u
enum { D_BINDING = 0x10, D_KEY_DIRECT = 0x40, D_KEY_MIC = 0x50, D_TEXT_DIRECT = 0x80, D_TEXT_MIC = 0xa0, D_SIZE = 0x100 };
#ifdef RC_GUEST32
const rc_target_value rc_target_offsets[] = {{"asset_load_name", 0x10}, {"boot_intro_playing", 0x2d},
                                             {"boot_intro_state", 0x60}, {"boot_phase", 0x48},
                                             {"boot_phase3_steps", 10}, {"boot_step", 0x28}, {"efd_str_inline", 0x14},
                                             {"efd_str_size", 0x10}, {"fs_exists_slot", 6}, {"input_slot", 0xc},
                                             {0, 0}};
// stdcall bool(void *boot), thiscall bool(void *steam, int app), and custom void(int input@eax, int force=0)
const rc_hook_name rc_target_hooks[] = {
    {"boot_intro_update", ADDR(INTRO), 1, 0, RC_CC_STDCALL, 'b', 4, 1, {{'p', 0}}},
    {"steam_dlc_owned", ADDR(OWNED), 2, 0, RC_CC_THISCALL, 'b', 4, 2, {{'p', ~1}, {'i', 0}}},
    {"input_bind", ADDR(BIND), 3, 0, RC_CC_CUSTOM, 'v', -1, 2, {{'i', ~0}, {'c', 0, 0}}},
    {0, 0, 0},
};
int rc_hook_code_pops(int index) {
  static const short pops[NPOINTS] = {4, 4, 0};
  return pops[index];
}
#define CC(cc) RC_CC_##cc
#else
const rc_target_value rc_target_offsets[] = {{"asset_load_name", 0x18}, {"boot_intro_playing", 0x39},
                                             {"boot_intro_state", 0x78}, {"boot_phase", 0x58},
                                             {"boot_phase3_steps", 12}, {"boot_step", 0x34}, {"efd_str_inline", 0x18},
                                             {"efd_str_size", 0x10}, {"fs_exists_slot", 7}, {"input_slot", 0x14},
                                             {0, 0}};
const rc_hook_name rc_target_hooks[] = {{"boot_intro_update", ADDR(INTRO), ADDR(INTRO)},
                                        {"steam_dlc_owned", ADDR(OWNED), ADDR(OWNED)},
                                        {"input_bind", ADDR(BIND), ADDR(BIND)}, {0, 0, 0}};
#define CC(cc) RC_CC_SYSV
#endif
const rc_target_value rc_target_symbols[] = {
    {"efd_str_destroy", ADDR(DESTROY), CC(THISCALL)},
    {"input_binding", DATA + D_BINDING, -1},     {"input_by_name", ADDR(BYNAME), CC(STDCALL)},
    {"input_key_direct", DATA + D_KEY_DIRECT, -1}, {"input_key_mic", DATA + D_KEY_MIC, -1},
    {"input_manager", ADDR(MANAGER), CC(CDECL)}, {"input_mode", ADDR(MODE), CC(CDECL)},
    {"input_profile_string", ADDR(PROFILE), CC(CDECL)}, {0, 0, 0}};
const uint64_t rc_hook_points[] = {ADDR(INTRO), ADDR(OWNED), ADDR(BIND), 0};
const int rc_hook_npoints = NPOINTS;
// The mid points the fixes here hook, with their values where each game has them. Skip Tuner's answer: the low
// byte of a register, a local below the frame pointer. The batched mounting's four: registers on the Mac; in
// Learn & Play an argument above the frame pointer too, and at the file question the path and the answer in one
// register, the game's call not yet made.
#define SKIP_LOCAL (-0x1d)
enum { M_SKIP, M_REQUEST, M_RESPONSE, M_MOUNT, M_EXISTS };
#ifdef RC_GUEST32
const rc_mid_name rc_target_mids[] = {
    {"tuner_skip_offer", ADDR(DESTROY + 1), 0, 1, {{"offer", 'b', RC_MID_MEM | 5, SKIP_LOCAL}}, 0, {0}, 0},
    {"manifest_request", ADDR(MIDS), 0, 1, {{"request", 'p', 3, 0}}, 0, {0}, 0},
    {"manifest_response", ADDR(MIDS + 1), 0, 2, {{"manager", 'p', 1, 0}, {"response", 'p', RC_MID_MEM | 5, 8}}, 0,
     {0}, 0},
    {"fs_mount", ADDR(MIDS + 2), 0, 2, {{"path", 'p', RC_MID_MEM | 5, 8}, {"fs", 'p', 0, 0}}, 0, {0}, 0},
    {"fs_exists", ADDR(MIDS + 3), 0, 3, {{"fs", 'p', 1, 0}, {"path", 'p', 0, 0}, {"found", 'b', 0, 0}}, 1,
     {"answered"}, 0},
    {0}};
#else
const rc_mid_name rc_target_mids[] = {
    {"tuner_skip_offer", ADDR(DESTROY + 1), 0, 1, {{"offer", 'b', 14, 0}}, 0, {0}, 0},
    {"manifest_request", ADDR(MIDS), 0, 1, {{"request", 'p', 13, 0}}, 0, {0}, 0},
    {"manifest_response", ADDR(MIDS + 1), 0, 2, {{"manager", 'p', 7, 0}, {"response", 'p', 6, 0}}, 0, {0}, 0},
    {"fs_mount", ADDR(MIDS + 2), 0, 2, {{"path", 'p', 7, 0}, {"fs", 'p', 6, 0}}, 0, {0}, 0},
    {"fs_exists", ADDR(MIDS + 3), 0, 3, {{"fs", 'p', 7, 0}, {"path", 'p', 6, 0}, {"found", 'b', 0, 0}}, 1,
     {"answered"}, 0},
    {0}};
#endif
#define OFF(name) ((uint32_t)rc_off(name))

// What is logged for a while: between log_start and log_stop into `logged`; logged_lines counts the lines that are
// exactly `line`
static FILE *log_file;
static int log_stderr;
static char logged[16384];
static void log_start(void) {
  fflush(stderr);
  log_file = tmpfile();
  CHECK(log_file);
  log_stderr = dup(2);
  dup2(fileno(log_file), 2);
}
static void log_end(void) {
  if (!log_file) return;
  fflush(stderr);
  dup2(log_stderr, 2);
  close(log_stderr);
}
static void log_stop(void) {
  log_end();
  rewind(log_file);
  logged[fread(logged, 1, sizeof logged - 1, log_file)] = 0;
  fclose(log_file);
  log_file = 0;
}
static int logged_lines(const char *line) {
  size_t len = strlen(line);
  int n = 0;
  for (const char *p = logged; *p; p = strchr(p, '\n') ? strchr(p, '\n') + 1 : p + strlen(p))
    n += !strncmp(p, line, len) && p[len] == '\n';
  return n;
}

_Noreturn void rc_fail(const char *what, uint64_t a) {
  fprintf(stderr, "rc_fail %s 0x%llx\n", what, (unsigned long long)a);
  exit(1);
}

// --- the game's side of Player 2's saved input: what the fix calls and reads, as each game has it. A pointer
// the game holds is its own width, and what it is handed has to be its own memory.
static uint8_t *data;  // DATA as the host sees it
#ifdef RC_GUEST32
static uint64_t as_game(const void *h) { return h ? RC32_G(h) : 0; }
static void *as_host(uint64_t g) { return g ? RC32_H((uint32_t)g) : 0; }
static void *game_alloc(uint32_t size) { return RC32_H(rc32_alloc(size)); }
#else
uint64_t rc_slide;
static uint64_t as_game(const void *h) { return (uint64_t)(uintptr_t)h; }
static void *as_host(uint64_t g) { return (void *)(uintptr_t)g; }
static void *game_alloc(uint32_t size) { return calloc(1, size); }
#endif
static void put_ptr(uint8_t *at, const void *h) {
  uint64_t g = as_game(h);
  memcpy(at, &g, (size_t)rc_guest_ptr_size);
}
static void *get_ptr(const uint8_t *at) {
  uint64_t g = 0;
  memcpy(&g, at, (size_t)rc_guest_ptr_size);
  return as_host(g);
}

static int game_mode_is, mode_calls, profile_calls, manager_calls, by_name_calls;
static uint32_t mode_player, profile_player;
static const char *profile_key;   // the key the profile was asked for
static const char *profile_text;  // what the profile has under it; null: a string with no text at all
static char by_name_seen[600];
#define TEXT_HEAP 512
static const char *input_name;    // the one input the manager has, and the object it is
static uint8_t *input_obj, *manager_obj, *text_heap;
static uint8_t *profile_out;      // the string the profile's text was last written into
static int text_live, text_frees, destroy_calls;  // text_heap holds a text the game has not freed; frees of it

// input_mode(player) -> al, the rest of eax whatever it was
static uint32_t game_mode(uint32_t player) {
  mode_calls++;
  mode_player = player;
  return 0xffffff00u | (uint32_t)game_mode_is;
}
// input_profile_string(player, key, string *out): the game's small string, given empty; a text that fits before
// its size word goes in place, a longer one behind a pointer at its start
static void game_profile_string(uint32_t player, const char *key, uint8_t *out) {
  uint8_t *end = out + rc_off("efd_str_size"), *in_place = out + rc_off("efd_str_inline");
  size_t n = profile_text ? strlen(profile_text) : 0;
  profile_calls++;
  profile_player = player;
  profile_key = key;
  profile_out = out;
  CHECK(key && out);  // null: the runtime could not hand the game what it was given
  CHECK(get_ptr(end) == out && get_ptr(in_place) == end);
  CHECK(n < TEXT_HEAP);
  CHECK(!text_live);  // the text of the call before was never freed
  if (!profile_text) {
    put_ptr(out, 0);
    put_ptr(end, 0);
    put_ptr(in_place, 0);
  } else if (n < (size_t)rc_off("efd_str_size")) {
    memcpy(out, profile_text, n);
    put_ptr(end, out + n);
  } else {
    memcpy(text_heap, profile_text, n);
    put_ptr(out, text_heap);
    put_ptr(end, text_heap + n);
    put_ptr(in_place, text_heap + n);  // the end of what was allocated
    text_live = 1;
  }
}
// efd_str_destroy(string *): frees the text unless it is in place, as the game's own callers do once they have
// read the string; what was there is gone
static void game_str_destroy(uint8_t *s) {
  uint8_t *end = s + rc_off("efd_str_size"), *in_place = s + rc_off("efd_str_inline");
  destroy_calls++;
  CHECK(s && s == profile_out);
  if (get_ptr(in_place) == end || !get_ptr(s)) return;
  CHECK(get_ptr(s) == text_heap && text_live);
  memset(text_heap, 0xdd, TEXT_HEAP);
  text_live = 0;
  text_frees++;
}
// input_by_name(manager, name) -> the input, or null
static void *game_by_name(void *manager, const char *name) {
  by_name_calls++;
  CHECK(manager == manager_obj && name);
  snprintf(by_name_seen, sizeof by_name_seen, "%s", name);
  return input_name && !strcmp(name, input_name) ? input_obj : 0;
}

// A file system object's question, has it this file: the whole file system's, and three archives', which answer
// for a path from where they are mounted. Each counts what it was asked.
#define ARCHIVES 3
static uint8_t *fs_whole, *fs_arch[ARCHIVES];
static int whole_asks, arch_asks[ARCHIVES];
static const char *whole_has, *arch_has[ARCHIVES];  // the one file each has beside an archive's appid.appid; null: none
static int arch_appid[ARCHIVES];                    // the archive answers for its appid.appid
static char whole_asked[200], arch_asked[200];      // the last path each kind was asked
static uint32_t game_fs_exists(uint8_t *fs, const char *path) {
  CHECK(fs && path);  // null: the runtime could not hand the game what it was given
  if (fs == fs_whole) {
    whole_asks++;
    snprintf(whole_asked, sizeof whole_asked, "%s", path);
    return 0xffffff00u | (uint32_t)(whole_has && !strcmp(path, whole_has));  // al, the rest whatever it was
  }
  for (int k = 0; k < ARCHIVES; k++) {
    if (fs != fs_arch[k]) continue;
    arch_asks[k]++;
    if (!strcmp(path, "appid.appid")) return (uint32_t)arch_appid[k];
    snprintf(arch_asked, sizeof arch_asked, "%s", path);
    return arch_has[k] && !strcmp(path, arch_has[k]);
  }
  CHECK(!"a file system object the game has not");
  return 0;
}

// --- what the runtimes need of an image: the game's functions above, by each one's convention
#ifdef RC_GUEST32
static uint32_t rd(uint32_t g) { uint32_t v; memcpy(&v, RC32_H(g), 4); return v; }
static void wr(uint32_t g, uint32_t v) { memcpy(RC32_H(g), &v, 4); }
// a guest `ret N` with eax; ecx and edx are the callee's to spoil
static void ret32(rc_ctx *c, uint32_t eax, uint32_t pops) {
  uint32_t sp = (uint32_t)c->r[4];
  c->r[0] = eax;
  c->r[1] = c->r[2] = 0xdeadbeefu;
  c->ret_to = rd(sp);
  c->r[4] = sp + 4 + pops;
}
static uint32_t arg32(rc_ctx *c, int k) { return rd((uint32_t)c->r[4] + 4 + 4 * (uint32_t)k); }
static void f_mode(rc_ctx *c) { ret32(c, game_mode(arg32(c, 0)), 0); }
static void f_profile(rc_ctx *c) {
  game_profile_string(arg32(c, 0), as_host(arg32(c, 1)), as_host(arg32(c, 2)));
  ret32(c, 1, 0);
}
static void f_manager(rc_ctx *c) {
  manager_calls++;
  ret32(c, (uint32_t)as_game(manager_obj), 0);
}
static void f_by_name(rc_ctx *c) {  // stdcall
  ret32(c, (uint32_t)as_game(game_by_name(as_host(arg32(c, 0)), as_host(arg32(c, 1)))), 8);
}
static void f_destroy(rc_ctx *c) {  // thiscall: the string in ecx
  game_str_destroy(as_host((uint32_t)c->r[1]));
  ret32(c, 0, 0);
}
static void f_fs_exists(rc_ctx *c) {  // thiscall: the object in ecx, the path on the stack, `ret 4`
  ret32(c, game_fs_exists(as_host((uint32_t)c->r[1]), as_host(arg32(c, 0))), 4);
}
rc_fn rc32_lookup(uint32_t g) {
  return g == ADDR(MODE) ? f_mode : g == ADDR(PROFILE) ? f_profile : g == ADDR(MANAGER) ? f_manager
       : g == ADDR(BYNAME) ? f_by_name : g == ADDR(DESTROY) ? f_destroy : g == ADDR(FSEXISTS) ? f_fs_exists : 0;
}
int rc32_tls_images(const rc32_tls_image **out) {
  *out = 0;
  return 0;
}
#else
uint64_t rc_call_guest(uint64_t fn, const uint64_t *ints, int nint, const uint64_t *dbls, int ndbl, uint64_t *xmm0) {
  (void)dbls; (void)ndbl; (void)xmm0;
  switch (fn - rc_slide) {
    case ADDR(MODE):
      CHECK_EQ(nint, 1);
      return game_mode((uint32_t)ints[0]);
    case ADDR(PROFILE):
      CHECK_EQ(nint, 3);
      game_profile_string((uint32_t)ints[0], as_host(ints[1]), as_host(ints[2]));
      return 1;
    case ADDR(MANAGER):
      manager_calls++;
      return as_game(manager_obj);
    case ADDR(BYNAME):
      CHECK_EQ(nint, 2);
      return as_game(game_by_name(as_host(ints[0]), as_host(ints[1])));
    case ADDR(DESTROY):
      CHECK_EQ(nint, 1);
      game_str_destroy(as_host(ints[0]));
      return 0;
  }
  return 0;
}
// OV_VCALL, the Mac app's (overrides/src/rc_ov.c): the one virtual call the fixes here make
uint64_t ov_vcall_n(uint64_t obj, int slot, const uint64_t *args, int nargs) {
  CHECK_EQ((uint64_t)slot, (uint64_t)rc_off("fs_exists_slot"));
  CHECK_EQ(nargs, 2);
  CHECK_EQ(args[0], obj);
  return game_fs_exists(as_host(obj), as_host(args[1]));
}
#endif

static const char *const SWITCHES[] = {"RS_MODS_LOG", "RS_MODS_BYPASS", "RS_MODS_FIXES", "RS_MODS_INTRO",
                                       "RS_MODS_OVERLAY", "RS_MODS_AUTOSAVE", "RS_MODS_MULTIPLAYER",
                                       "RS_MODS_BUDGET_MS", "RS_MODS_FRAME_RATE", "RS_MODS_GUITARCADE_SKIP"};
static void no_switches(void) {
  for (size_t k = 0; k < sizeof SWITCHES / sizeof *SWITCHES; k++) unsetenv(SWITCHES[k]);
}

// The switches: the defaults, what turns one off, and the budget's range
static void switches(void) {
  no_switches();
  rs_mods_reload_config();
  const rs_mods_config *c = rs_mods_config_get();
  CHECK(c == &rs_mods_cfg);
  CHECK(c->bypass && c->fixes && c->intro && c->overlay && c->autosave && c->multiplayer && !c->log);
  CHECK(c->guitarcade_skip);
  CHECK(c->budget_s == 0.050);
  static const char *const off[] = {"0", "no", "No", "false", "False"}, *const on[] = {"1", "yes", "on", "2", ""};
  for (int k = 0; k < 5; k++) {
    setenv("RS_MODS_X", off[k], 1);
    CHECK(!rs_mods_env_on("RS_MODS_X", 1));
    setenv("RS_MODS_X", on[k], 1);
    CHECK(rs_mods_env_on("RS_MODS_X", 1));
  }
  CHECK(!rs_mods_env_on("RS_MODS_X", 0));  // empty, as unset: the default
  unsetenv("RS_MODS_X");
  CHECK(!rs_mods_env_on("RS_MODS_X", 0) && rs_mods_env_on("RS_MODS_X", 1));
  setenv("RS_MODS_INTRO", "0", 1);
  setenv("RS_MODS_BYPASS", "n", 1);
  setenv("RS_MODS_LOG", "1", 1);
  setenv("RS_MODS_BUDGET_MS", "8", 1);
  setenv("RS_MODS_GUITARCADE_SKIP", "0", 1);
  rs_mods_reload_config();
  CHECK(!c->intro && !c->bypass && c->log && c->fixes && c->overlay && c->autosave && c->multiplayer);
  CHECK(!c->guitarcade_skip);
  CHECK(c->budget_s == 0.008);
  setenv("RS_MODS_BUDGET_MS", "0", 1);  // a step a frame, as the original
  rs_mods_reload_config();
  CHECK(c->budget_s == 0);
  static const char *const bad[] = {"-1", "1001", "fast", ""};
  for (int k = 0; k < 4; k++) {
    setenv("RS_MODS_BUDGET_MS", bad[k], 1);
    rs_mods_reload_config();
    CHECK(c->budget_s == 0.050);
  }
}

// The log: one prefix in both editions, and nothing without RS_MODS_LOG
static void log_lines(void) {
  no_switches();
  rs_mods_reload_config();
  log_start();
  rs_mods_logf("DLC: %u archives mounted in %u frames, %.2f s", 147u, 6u, 0.31);
  log_stop();
  CHECK_EQ(logged[0], 0);
  setenv("RS_MODS_LOG", "1", 1);
  rs_mods_reload_config();
  log_start();
  rs_mods_logf("DLC: %u archives mounted in %u frames, %.2f s", 147u, 6u, 0.31);
  log_stop();
  CHECK(!strcmp(logged, "[rs-mods] DLC: 147 archives mounted in 6 frames, 0.31 s\n"));
}

// The start: it reads the switches itself, adds the hooks of the fixes that are here and says what is on. The
// frame rate fixes find none of their points in this test's tables; Skip Tuner finds its one.
static void start(void) {
  no_switches();
  setenv("RS_MODS_LOG", "1", 1);
  setenv("RS_MODS_AUTOSAVE", "0", 1);
  setenv("RS_MODS_BUDGET_MS", "20", 1);
  log_start();
  rs_mods_start();
  log_stop();
  CHECK_EQ(logged_lines("[rs-mods] fixes on, bypass on, overlay on, intro skip on, autosave off, budget 20 ms"), 1);
  CHECK_EQ(logged_lines("[rs-mods] frame rate: on, 0 of 8 points"), 1);
  CHECK_EQ(logged_lines("[rs-mods] Guitarcade Skip Tuner: on"), 1);
  CHECK_EQ(logged_lines("[rs-mods] no intro hook point") + logged_lines("[rs-mods] no DLC ownership hook point") +
           logged_lines("[rs-mods] no hook point for Player 2's saved input"), 0);
  CHECK(!strstr(logged, "no points for"));  // the batched mounting's four mid points
  CHECK(!rs_mods_cfg.autosave && rs_mods_cfg.log);
}

// --- a call of a hook point as the game makes it, and the game's function behind it
static const uint32_t RET = 0x12345678;
static rc_ctx *ctx;
static uint8_t *obj;       // the game's object, as the host sees it
static int orig_ran;
static uint32_t orig_arg;  // what the original was given, where its convention has it (the low word)
#ifdef RC_GUEST32
static uint32_t esp0, obj_guest;
// stdcall (boot): the argument on the stack, `ret 4`; al = busy
static void o_intro(rc_ctx *c) {
  uint32_t sp = (uint32_t)c->r[4];
  orig_ran++;
  orig_arg = rd(sp + 4);
  c->r[0] = 0xffffff01u;
  c->ret_to = rd(sp);
  c->r[4] = sp + 8;
}
static uint64_t call_intro(void) {
  uint32_t sp = esp0 - 256;
  ctx->r[4] = sp;
  ctx->ret_to = 0;
  wr(sp, RET);
  wr(sp + 4, obj_guest);
  orig_ran = 0;
  rc_hook_call(INTRO, ctx, o_intro);
  CHECK_EQ(ctx->ret_to, RET);
  CHECK_EQ((uint32_t)ctx->r[4], sp + 8);  // the return address and the argument gone, whoever answered
  return ctx->r[0] & 0xff;
}
// thiscall (steam, app): `this` in ecx, the AppID on the stack, `ret 4`; al = owned
static uint32_t orig_app;
static int owned_answer;
static void o_owned(rc_ctx *c) {
  uint32_t sp = (uint32_t)c->r[4];
  orig_ran++;
  orig_arg = (uint32_t)c->r[1];
  orig_app = rd(sp + 4);
  c->r[0] = 0xffffff00u | (uint32_t)owned_answer;
  c->ret_to = rd(sp);
  c->r[4] = sp + 8;
}
static uint64_t call_owned(uint32_t app) {
  uint32_t sp = esp0 - 256;
  ctx->r[4] = sp;
  ctx->ret_to = 0;
  ctx->r[1] = obj_guest;
  wr(sp, RET);
  wr(sp + 4, app);
  orig_ran = 0;
  rc_hook_call(OWNED, ctx, o_owned);
  CHECK_EQ(ctx->ret_to, RET);
  CHECK_EQ((uint32_t)ctx->r[4], sp + 8);
  return ctx->r[0] & 0xff;
}
// custom (input): the input in eax and nothing else, `ret`; al = it looked for a slot. esi is not the call's.
#define ESI 0x5a5a5a5au
static void o_bind(rc_ctx *c) {
  uint32_t sp = (uint32_t)c->r[4];
  orig_ran++;
  orig_arg = (uint32_t)c->r[0];
  c->r[0] = 0xffffff01u;
  c->ret_to = rd(sp);
  c->r[4] = sp + 4;
}
static void call_bind(uint32_t input) {
  uint32_t sp = esp0 - 256;
  ctx->r[4] = sp;
  ctx->ret_to = 0;
  ctx->r[0] = input;
  ctx->r[6] = ESI;
  wr(sp, RET);
  orig_ran = 0;
  rc_hook_call(BIND, ctx, o_bind);
  CHECK_EQ(ctx->ret_to, RET);
  CHECK_EQ((uint32_t)ctx->r[4], sp + 4);
  CHECK_EQ(ctx->r[6], ESI);
}
#define OBJ_AS_GIVEN obj_guest
#else
static uint64_t stack_[16];
static void o_intro(rc_ctx *c) {
  orig_ran++;
  orig_arg = (uint32_t)c->r[7];
  c->r[0] = 1;
  c->r[4] += 8;
}
static uint64_t call_intro(void) {
  ctx->r[4] = (uint64_t)(uintptr_t)&stack_[8];
  stack_[8] = RET;
  ctx->r[7] = (uint64_t)(uintptr_t)obj;
  orig_ran = 0;
  rc_hook_call(INTRO, ctx, o_intro);
  CHECK_EQ(ctx->r[4], (uint64_t)(uintptr_t)&stack_[9]);
  return ctx->r[0] & 0xff;
}
static uint32_t orig_app;
static int owned_answer;
static void o_owned(rc_ctx *c) {
  orig_ran++;
  orig_arg = (uint32_t)c->r[7];
  orig_app = (uint32_t)c->r[6];
  c->r[0] = (uint64_t)owned_answer;
  c->r[4] += 8;
}
static uint64_t call_owned(uint32_t app) {
  ctx->r[4] = (uint64_t)(uintptr_t)&stack_[8];
  stack_[8] = RET;
  ctx->r[7] = (uint64_t)(uintptr_t)obj;
  ctx->r[6] = app;
  orig_ran = 0;
  rc_hook_call(OWNED, ctx, o_owned);
  CHECK_EQ(ctx->r[4], (uint64_t)(uintptr_t)&stack_[9]);
  return ctx->r[0] & 0xff;
}
// (input, force): force 0, as in every call of the game's
static void o_bind(rc_ctx *c) {
  orig_ran++;
  orig_arg = (uint32_t)c->r[7];
  c->r[0] = 1;
  c->r[4] += 8;
}
static void call_bind(uint32_t input) {
  ctx->r[4] = (uint64_t)(uintptr_t)&stack_[8];
  stack_[8] = RET;
  ctx->r[7] = input;
  ctx->r[6] = 0;
  orig_ran = 0;
  rc_hook_call(BIND, ctx, o_bind);
  CHECK_EQ(ctx->r[4], (uint64_t)(uintptr_t)&stack_[9]);
}
#define OBJ_AS_GIVEN ((uint32_t)(uintptr_t)obj)
#endif

// Skip Tuner in a Guitarcade game's tuner: at the point the game has its answer, 0 for a tuner that goes on to a
// Guitarcade game. The hook makes it 1 and lets the game's own test run; switched off, the answer is the game's.
static void skip_tuner(void) {
  uint32_t fl = 0x40;
#ifdef RC_GUEST32
  ctx->r[5] = esp0 - 64;  // the frame pointer: the answer is a byte below it, with the game's other locals around
  uint8_t *local = (uint8_t *)RC32_H((uint32_t)ctx->r[5] + (uint32_t)SKIP_LOCAL);
  local[-1] = 0x5a;
  local[1] = 0xa5;
#define ANSWER (*local)
#define SET_ANSWER(v) (*local = (v))
#define AROUND_KEPT (local[-1] == 0x5a && local[1] == 0xa5)
#else
#define ANSWER ((uint8_t)ctx->r[14])
#define SET_ANSWER(v) (ctx->r[14] = 0x1122334455667700ull | (uint8_t)(v))
#define AROUND_KEPT ((ctx->r[14] >> 8) == 0x11223344556677ull)
#endif
  no_switches();
  rs_mods_reload_config();
  SET_ANSWER(0);
  CHECK_EQ((uint32_t)rc_mid_call(0, ctx, &fl), 0);  // no continuation: the game's test, on the new answer
  CHECK_EQ(ANSWER, 1);
  CHECK(AROUND_KEPT);
  CHECK_EQ(fl, 0x40);
  SET_ANSWER(1);  // a song's tuner: offered already
  rc_mid_call(0, ctx, &fl);
  CHECK_EQ(ANSWER, 1);
  setenv("RS_MODS_GUITARCADE_SKIP", "0", 1);
  rs_mods_reload_config();
  SET_ANSWER(0);
  rc_mid_call(0, ctx, &fl);
  CHECK_EQ(ANSWER, 0);  // the original
  CHECK(AROUND_KEPT);
  no_switches();
  rs_mods_reload_config();
#undef ANSWER
#undef SET_ANSWER
#undef AROUND_KEPT
}

static uint32_t fld(const char *name) { uint32_t v; memcpy(&v, obj + OFF(name), 4); return v; }
static void set(const char *name, uint32_t v) { memcpy(obj + OFF(name), &v, 4); }
static void boot_at(uint32_t phase, uint32_t state, uint32_t step) {
  memset(obj, 0, 256);
  set("boot_phase", phase);
  set("boot_intro_state", state);
  set("boot_step", step);
  obj[OFF("boot_intro_playing")] = 1;
}

// No intro screens (the hook start() added): the tick as the game has it while the movie's model loads and once
// the intro is over; "not busy" without the tick while the movie would start or play, until phase 3 has done its
// steps; then the game's own skip, and the tick ends the intro.
static void intro(void) {
  const uint32_t steps = OFF("boot_phase3_steps");
  CHECK(rs_mods_cfg.intro);
  static const uint32_t untouched[][2] = {{0, 2}, {1, 3}, {2, 0}, {2, 1}, {3, 1}, {2, 4}, {3, 4}, {3, 5}, {4, 3}};
  for (size_t k = 0; k < sizeof untouched / sizeof *untouched; k++) {
    boot_at(untouched[k][0], untouched[k][1], 0);
    CHECK_EQ(call_intro(), 1);  // the original's answer
    CHECK_EQ(orig_ran, 1);
    CHECK_EQ(orig_arg, OBJ_AS_GIVEN);
    CHECK_EQ(fld("boot_intro_state"), untouched[k][1]);
    CHECK_EQ(obj[OFF("boot_intro_playing")], 1);
  }
  for (uint32_t state = 2; state <= 3; state++) {
    boot_at(2, state, steps);  // phase 2: its steps are phase 3's, whatever the counter says
    CHECK_EQ(call_intro(), 0);
    CHECK_EQ(orig_ran, 0);
    CHECK_EQ(fld("boot_intro_state"), state);
    boot_at(3, state, steps - 1);
    CHECK_EQ(call_intro(), 0);
    CHECK_EQ(orig_ran, 0);
    CHECK_EQ(fld("boot_intro_state"), state);
    CHECK_EQ(obj[OFF("boot_intro_playing")], 1);
    boot_at(3, state, steps);  // done: the game's skip, and the original ends the intro
    log_start();
    CHECK_EQ(call_intro(), 1);
    log_stop();
    CHECK_EQ(logged_lines("[rs-mods] intro: skipped"), 1);
    CHECK_EQ(orig_ran, 1);
    CHECK_EQ(orig_arg, OBJ_AS_GIVEN);
    CHECK_EQ(fld("boot_intro_state"), 4);
    CHECK_EQ(obj[OFF("boot_intro_playing")], 0);
  }
  rs_mods_cfg.intro = 0;  // RS_MODS_INTRO=0: the intro plays
  boot_at(3, 2, steps);
  CHECK_EQ(call_intro(), 1);
  CHECK_EQ(orig_ran, 1);
  CHECK_EQ(fld("boot_intro_state"), 2);
  boot_at(2, 3, 0);
  CHECK_EQ(call_intro(), 1);
  CHECK_EQ(orig_ran, 1);
  rs_mods_cfg.intro = 1;
}

// Song ownership during a mount: the game's own answer outside the install steps, yes inside them whatever the
// AppID, and the game's again with RS_MODS_BYPASS=0.
static void ownership(void) {
  CHECK(rs_mods_cfg.bypass);
  for (owned_answer = 0; owned_answer <= 1; owned_answer++) {
    CHECK_EQ(call_owned(248750), (uint64_t)owned_answer);
    CHECK_EQ(orig_ran, 1);
    CHECK_EQ(orig_arg, OBJ_AS_GIVEN);
    CHECK_EQ(orig_app, 248750);
  }
  owned_answer = 0;
  rs_mods_mounting(1);
  CHECK_EQ(call_owned(248750), 1);
  CHECK_EQ(orig_ran, 0);
  rs_mods_cfg.bypass = 0;
  CHECK_EQ(call_owned(248750), 0);
  CHECK_EQ(orig_ran, 1);
  rs_mods_cfg.bypass = 1;
  rs_mods_mounting(0);
  CHECK_EQ(call_owned(248750), 0);
  CHECK_EQ(orig_ran, 1);
}

// Player 2's saved input (the hook start() added on input_bind). The call a hook sees there is the Mac's on both
// runtimes: the input, and a force of 0, which Learn & Play's function has folded away.
static uint32_t seen_input, seen_force;
static int seen;
static int see_bind(rc_ctx *c, void *user) {
  (void)user;
  seen++;
  seen_input = (uint32_t)c->r[7];
  seen_force = (uint32_t)c->r[6];
  return 0;
}

enum { P1_GUITAR, P1_MIC, P2_GUITAR, P2_MIC, NONE = 5, SLOTS = 10 };
static int32_t *binding;
// The game before a call: Player 2's mode, what their profile names, the input the manager has and its slot, and
// every slot free but slot 0, Player 1's guitar. The words on either side of the ten slots read as free ones too:
// a write past the table would show.
static void bind_at(int mode, const char *in_profile, const char *name, uint32_t slot) {
  game_mode_is = mode;
  profile_text = in_profile;
  input_name = name;
  memcpy(input_obj + rc_off("input_slot"), &slot, 4);
  for (int k = -1; k <= SLOTS; k++) binding[k] = k ? NONE : P1_GUITAR;
  mode_calls = profile_calls = manager_calls = by_name_calls = seen = text_frees = destroy_calls = 0;
  profile_key = 0;
  by_name_seen[0] = 0;
}
// A call for `input`: the game's own search runs after the fix whatever it did, with the input it was called with.
// A string the fix had the game fill is destroyed by then, and so a text the game allocated for it is freed.
static void bind(uint32_t input) {
  call_bind(input);
  CHECK_EQ(orig_ran, 1);
  CHECK_EQ(orig_arg, input);
  CHECK_EQ(seen, 1);
  CHECK_EQ(seen_input, input);
  CHECK_EQ(seen_force, 0);
  CHECK_EQ(destroy_calls, profile_calls);
  CHECK(!text_live);
}
static int slots_as_set(void) {
  for (int k = -1; k <= SLOTS; k++)
    if (binding[k] != (k ? NONE : P1_GUITAR)) return 0;
  return 1;
}

static void saved_input(void) {
  static const char SHORT[] = "USB Codec", LONG[] = "Interface with a long name: Input 2";  // in place, and not
  CHECK(rs_mods_cfg.multiplayer);
  CHECK(sizeof SHORT - 1 < (size_t)rc_off("efd_str_size") && sizeof LONG - 1 > (size_t)rc_off("efd_str_size"));
  CHECK(!rc_hook_add_name("input_bind", see_bind, NULL, NULL));
  // another input than Player 2's guitar: nothing asked of the game
  static const uint32_t others[] = {P1_GUITAR, P1_MIC, P2_MIC, NONE};
  for (size_t k = 0; k < sizeof others / sizeof *others; k++) {
    bind_at(3, SHORT, SHORT, 4);
    bind(others[k]);
    CHECK_EQ(mode_calls + profile_calls + by_name_calls, 0);
    CHECK(slots_as_set());
  }
  // Player 2 on a cable, disconnected or not set: the game's own rule
  static const int cable[] = {0, 2, 4};
  for (size_t k = 0; k < sizeof cable / sizeof *cable; k++) {
    bind_at(cable[k], SHORT, SHORT, 4);
    bind(P2_GUITAR);
    CHECK_EQ(mode_calls, 1);
    CHECK_EQ(mode_player, 1);
    CHECK_EQ(profile_calls + by_name_calls, 0);
    CHECK(slots_as_set());
  }
  // Direct Connect, the name in place: the profile's Direct Connect key, the input of that name, its slot
  bind_at(3, SHORT, SHORT, 4);
  log_start();
  bind(P2_GUITAR);
  log_stop();
  CHECK_EQ(profile_calls, 1);
  CHECK_EQ(profile_player, 1);
  CHECK(profile_key == (const char *)data + D_TEXT_DIRECT);
  CHECK_EQ(manager_calls, 1);
  CHECK_EQ(by_name_calls, 1);
  CHECK(!strcmp(by_name_seen, SHORT));
  CHECK_EQ(text_frees, 0);  // in place: nothing of the game's to free
  CHECK_EQ(binding[4], P2_GUITAR);
  binding[4] = NONE;
  CHECK(slots_as_set());
  CHECK_EQ(logged_lines("[rs-mods] player 2 on \"USB Codec\""), 1);
  // the microphone, the name behind a pointer: the other key, and a slot a microphone input holds is taken
  static const int32_t taken[] = {P1_MIC, P2_MIC};
  for (size_t k = 0; k < sizeof taken / sizeof *taken; k++) {
    bind_at(1, LONG, LONG, 9);
    binding[9] = taken[k];
    log_start();
    bind(P2_GUITAR);
    log_stop();
    CHECK_EQ(logged_lines("[rs-mods] player 2 on \"Interface with a long name: Input 2\""), 1);
    CHECK(profile_key == (const char *)data + D_TEXT_MIC);
    CHECK(!strcmp(by_name_seen, LONG));  // the name as it was before its text was freed
    CHECK_EQ(text_frees, 1);
    CHECK_EQ(binding[9], P2_GUITAR);
  }
  // a slot a guitar holds stays that guitar's, and nothing is said
  bind_at(3, SHORT, SHORT, 0);
  log_start();
  bind(P2_GUITAR);
  log_stop();
  CHECK_EQ(by_name_calls, 1);
  CHECK(slots_as_set());
  CHECK(!strstr(logged, "player 2 on"));
  // Player 2's guitar has a slot already, any of the ten: nothing read of the profile
  static const int has[] = {1, 7, SLOTS - 1};
  for (size_t k = 0; k < sizeof has / sizeof *has; k++) {
    bind_at(3, SHORT, SHORT, 4);
    binding[has[k]] = P2_GUITAR;
    bind(P2_GUITAR);
    CHECK_EQ(mode_calls, 1);
    CHECK_EQ(profile_calls + by_name_calls, 0);
    CHECK_EQ(binding[4], NONE);
  }
  // no saved input in the profile, as an empty text and as a string with no text; one the manager doesn't have
  static const char *const none[] = {"", 0};
  for (int k = 0; k < 2; k++) {
    bind_at(3, none[k], SHORT, 4);
    bind(P2_GUITAR);
    CHECK_EQ(profile_calls, 1);
    CHECK_EQ(by_name_calls, 0);
    CHECK(slots_as_set());
  }
  bind_at(3, LONG, SHORT, 4);  // the interface of a long name unplugged: its text is freed all the same
  bind(P2_GUITAR);
  CHECK_EQ(by_name_calls, 1);
  CHECK_EQ(text_frees, 1);
  CHECK(slots_as_set());
  // a slot out of the table, by one, by a byte's worth and by all of its word: no slot is written, in it or past it
  static const uint32_t out_of_table[] = {SLOTS, 0x104, 0xffffffffu};
  for (size_t k = 0; k < sizeof out_of_table / sizeof *out_of_table; k++) {
    bind_at(3, SHORT, SHORT, out_of_table[k]);
    bind(P2_GUITAR);
    CHECK_EQ(by_name_calls, 1);
    CHECK(slots_as_set());
  }
  // a name longer than the fix keeps: the input is asked for by its first 255 bytes, and nothing past them is written
  static char longest[300];
  memset(longest, 'n', sizeof longest - 1);
  bind_at(3, longest, SHORT, 4);
  bind(P2_GUITAR);
  CHECK_EQ(by_name_calls, 1);
  CHECK_EQ(strlen(by_name_seen), 255);
  CHECK(!memcmp(by_name_seen, longest, 255));
  CHECK(slots_as_set());
  // the image without the key's pointer: nothing asked of the profile
  uint8_t kept[8];
  memcpy(kept, data + D_KEY_DIRECT, sizeof kept);
  memset(data + D_KEY_DIRECT, 0, sizeof kept);
  bind_at(3, SHORT, SHORT, 4);
  bind(P2_GUITAR);
  CHECK_EQ(mode_calls, 1);
  CHECK_EQ(profile_calls + by_name_calls, 0);
  CHECK(slots_as_set());
  memcpy(data + D_KEY_DIRECT, kept, sizeof kept);
  // RS_MODS_MULTIPLAYER=0: the original
  rs_mods_cfg.multiplayer = 0;
  bind_at(3, SHORT, SHORT, 4);
  bind(P2_GUITAR);
  CHECK_EQ(mode_calls + profile_calls + by_name_calls, 0);
  CHECK(slots_as_set());
  rs_mods_cfg.multiplayer = 1;
}

// --- Batched DLC mounting: what both editions' loops share. The calls below are an edition's dlc_update's
// (mods_shared.h), and the game's side is its mid points, called as the translated code calls them.
#define STARTS(text, with) (!strncmp((text), with, sizeof with - 1))
// The end of a mounting whose lines are not looked at
static void ends_quietly(void) {
  log_start();
  rs_mods_mount_end();
  log_stop();
}
// One update of the coordinator with its flag set: the first starts a mounting
static int frame(void) { return rs_mods_mount_frame(1); }
#ifdef RC_GUEST32
#define STACK_FOR_CALLS() (ctx->r[4] = esp0 - 1024, ctx->r[5] = esp0 - 64)
#else
#define STACK_FOR_CALLS() ((void)0)
#endif

// A response the ManifestManager's handler is given: the asset's name in the game's string, in place or behind a
// pointer. A mount's manifest was asked for by path: its name is empty.
static uint8_t *response_named(const char *name, int in_place) {
  uint8_t *r = game_alloc(128), *str = r + rc_off("asset_load_name");
  uint8_t *end = str + rc_off("efd_str_size"), *mark = str + rc_off("efd_str_inline");
  size_t n = strlen(name);
  if (in_place) {
    CHECK(n < (size_t)rc_off("efd_str_size"));
    memcpy(str, name, n + 1);
    put_ptr(end, str + n);
    put_ptr(mark, end);
  } else {
    uint8_t *text = game_alloc((uint32_t)n + 1);
    memcpy(text, name, n + 1);
    put_ptr(str, text);
    put_ptr(end, text + n);
    put_ptr(mark, text + n + 1);
  }
  return r;
}
static void manifest_asked(void) {
  uint32_t fl = 0;
  STACK_FOR_CALLS();
  CHECK_EQ((uint32_t)rc_mid_call(M_REQUEST, ctx, &fl), 0);
}
static void manifest_answered(uint8_t *response) {
  uint32_t fl = 0;
  STACK_FOR_CALLS();
#ifdef RC_GUEST32
  ctx->r[1] = as_game(manager_obj);
  wr((uint32_t)ctx->r[5] + 8, (uint32_t)as_game(response));
#else
  ctx->r[7] = as_game(manager_obj);
  ctx->r[6] = as_game(response);
#endif
  CHECK_EQ((uint32_t)rc_mid_call(M_RESPONSE, ctx, &fl), 0);
}

// The song manifests: the flag is held from the step that found the list empty until every manifest this mounting
// asked for has been answered. Answers with a name are the game's other loads, and one asked for before the
// mounting is not this mounting's. The loader thread's pause is short for as long as archives are mounted.
static void manifests(void) {
  no_switches();
  setenv("RS_MODS_LOG", "1", 1);
  rs_mods_reload_config();
  uint8_t *unnamed = response_named("", 1), *unnamed_far = response_named("", 0);
  uint8_t *named = response_named("urn:x:guitar", 1);
  uint8_t *named_far = response_named("urn:database:json-db:a-long-name", 0);
  CHECK_EQ(rs_mods_loader_pause_us(), 10000);  // the game's own
  manifest_asked();  // before the mounting: not waited for
  CHECK_EQ(frame(), 1);
  CHECK_EQ(rs_mods_loader_pause_us(), 200);
  CHECK(!rs_mods_mount_holding());
  for (int k = 0; k < 3; k++) {
    rs_mods_mounting(1);
    manifest_asked();
    rs_mods_mounting(0);
    rs_mods_mount_took();
  }
  manifest_answered(unnamed);
  CHECK_EQ(frame(), 1);
  CHECK_EQ(rs_mods_mount_listed(), 1);  // two to come: the update sets the flag again
  CHECK(rs_mods_mount_holding());
  CHECK_EQ(frame(), 1);
  CHECK(rs_mods_mount_waiting());
  manifest_answered(named);
  manifest_answered(named_far);
  manifest_answered(0);  // the handler given nothing
  CHECK(rs_mods_mount_waiting());
  manifest_answered(unnamed_far);
  CHECK(rs_mods_mount_waiting());
  manifest_answered(unnamed);
  CHECK(!rs_mods_mount_waiting());
  log_start();
  rs_mods_mount_end();
  log_stop();
  CHECK(STARTS(logged, "[rs-mods] DLC: 3 archives mounted in 2 frames, "));
  CHECK(strstr(logged, " s, their songs in the lists ") && strstr(logged, " s later\n"));
  CHECK(!strstr(logged, "file questions"));  // none asked
  CHECK(!rs_mods_mount_holding());
  CHECK_EQ(rs_mods_mount_frame(0), 0);  // the flag clear: no mounting, and the game's pause
  CHECK_EQ(rs_mods_loader_pause_us(), 10000);
  // every manifest in when the list is empty: no hold, and the line without the manifests' part
  CHECK_EQ(frame(), 1);
  rs_mods_mounting(1);
  manifest_asked();
  rs_mods_mounting(0);
  rs_mods_mount_took();
  manifest_answered(unnamed);
  CHECK_EQ(rs_mods_mount_listed(), 0);
  CHECK(!rs_mods_mount_holding());
  log_start();
  rs_mods_mount_end();
  log_stop();
  CHECK(STARTS(logged, "[rs-mods] DLC: 1 archives mounted in 1 frames, ") && !strstr(logged, "their songs"));
  // what an edition's loop waits for between two steps: while its number for what is left is not 0
  CHECK_EQ(frame(), 1);
  CHECK_EQ(rs_mods_mount_loading(0), 0);
  CHECK_EQ(rs_mods_mount_loading(3), 1);
  CHECK_EQ(rs_mods_mount_loading(5), 1);
  CHECK_EQ(rs_mods_mount_loading(0), 0);
  // a manifest that never comes: after 5 s without an answer the mounting waits for nothing more
  manifest_asked();
  CHECK_EQ(rs_mods_mount_listed(), 1);
  CHECK(rs_mods_mount_waiting());
  usleep(5100000);
  log_start();
  CHECK(!rs_mods_mount_waiting());
  log_stop();
  CHECK_EQ(logged_lines("[rs-mods] DLC: a song manifest was not answered in 5 s, the mounting goes on without "
                        "waiting"), 1);
  CHECK_EQ(rs_mods_mount_loading(7), 0);
  ends_quietly();
  manifest_answered(unnamed);  // it came after all: the next mounting starts from the counts as they are
  CHECK_EQ(frame(), 1);
  CHECK_EQ(rs_mods_mount_loading(7), 1);  // and waits again
  CHECK_EQ(rs_mods_mount_listed(), 0);
  ends_quietly();
  // RS_MODS_FIXES=0 and a budget of 0: a step a frame, the game's pause
  rs_mods_cfg.fixes = 0;
  CHECK_EQ(frame(), 0);
  CHECK_EQ(rs_mods_loader_pause_us(), 10000);
  ends_quietly();
  rs_mods_cfg.fixes = 1;
  rs_mods_cfg.budget_s = 0;
  CHECK_EQ(frame(), 0);
  ends_quietly();
  no_switches();
  rs_mods_reload_config();
}

// The game mounts the archive `fs` at `at` (fs_mount): the mount point and where the object's pointer is
static void mounts(const char *at, uint8_t *fs) {
  static char *where;
  static uint8_t *slot;
  uint32_t fl = 0;
  if (!where) where = game_alloc(64), slot = game_alloc(8);
  strcpy(where, at);
  put_ptr(slot, fs);
  STACK_FOR_CALLS();
#ifdef RC_GUEST32
  wr((uint32_t)ctx->r[5] + 8, (uint32_t)as_game(where));
  ctx->r[0] = as_game(slot);
#else
  ctx->r[7] = as_game(where);
  ctx->r[6] = as_game(slot);
#endif
  CHECK_EQ((uint32_t)rc_mid_call(M_MOUNT, ctx, &fl), 0);
}
// The game asks its whole file system for `path` (fs_exists): the answer it goes on with. Where the fix has
// answered, the game's own call is skipped; where it has not, the game makes it.
static int asks_answered;
static int asks(const char *path) {
  static char *text;
  uint32_t fl = 0;
  if (!text) text = game_alloc(256);
  strcpy(text, path);
  STACK_FOR_CALLS();
#ifdef RC_GUEST32
  ctx->r[1] = as_game(fs_whole);
  ctx->r[0] = as_game(text);
#else
  ctx->r[7] = as_game(fs_whole);
  ctx->r[6] = as_game(text);
  ctx->r[0] = 0x1122334455667700ull;
#endif
  int go = rc_mid_call(M_EXISTS, ctx, &fl);
  asks_answered = go != 0;
  if (!go) return (uint8_t)game_fs_exists(fs_whole, text) != 0;
  CHECK_EQ(go, 1);
  return (uint8_t)ctx->r[0] != 0;
}
// One install step that mounts `fs` at `at` (null: it mounts nothing) and asks for `n` of the paths
static const char *const QUESTION[] = {"data/shaders/generated/a.sha1", "data/shaders/generated/b.sha1",
                                       "data/shaders/generated/c.sha1"};
static int step_found[3];
static void step(const char *at, uint8_t *fs, int n) {
  rs_mods_mounting(1);
  if (at) mounts(at, fs);
  for (int k = 0; k < n; k++) step_found[k] = asks(QUESTION[k]);
  rs_mods_mounting(0);
  rs_mods_mount_took();
}
static void fs_reset(void) {
  whole_asks = 0;
  whole_has = 0;
  for (int k = 0; k < ARCHIVES; k++) arch_asks[k] = 0, arch_has[k] = 0, arch_appid[k] = 1;
}

// The cache overlay's names (rs_mods_overlay_name): a cacheN.7z as it is, a menu movie of static.psarc in lower case
// with '/', nothing for any other file, a path in the overlay already, or a name that doesn't fit.
static void overlay_names(void) {
  char alt[64];
  CHECK(rs_mods_overlay_name("cache8.7z", alt, sizeof alt) && !strcmp(alt, "patchupdate/cache8.7z"));
  CHECK(rs_mods_overlay_name("Cache12.7z", alt, sizeof alt) && !strcmp(alt, "patchupdate/Cache12.7z"));
  CHECK(rs_mods_overlay_name("data\\gfxassets\\views\\TD_SongList.gfx", alt, sizeof alt) &&
        !strcmp(alt, "patchupdate/gfxassets/views/td_songlist.gfx"));
  CHECK(rs_mods_overlay_name("gfxassets/views/td_songlist.gfx", alt, sizeof alt) &&
        !strcmp(alt, "patchupdate/gfxassets/views/td_songlist.gfx"));
  CHECK(rs_mods_overlay_name("/game/data/gfxassets/views/x.gfx", alt, sizeof alt) &&
        !strcmp(alt, "patchupdate/gfxassets/views/x.gfx"));
  const char *not[] = {"cache.7z", "cache8.7z.bak", "data\\gfxassets\\libs\\commonlib.gfx",
                       "data\\gfxassets\\tuner.gfx", "data\\gfxassets\\views\\sub\\x.gfx",
                       "data\\gfxassets\\views\\x.dds", "data\\gfxassets\\views\\.gfx",
                       "data\\mygfxassets\\views\\x.gfx", "patchupdate/gfxassets/views/x.gfx", "PatchUpdate/cache8.7z",
                       "dlc/x_m.psarc", ""};
  for (size_t i = 0; i < sizeof not / sizeof *not; i++) CHECK(!rs_mods_overlay_name(not[i], alt, sizeof alt));
  CHECK(!rs_mods_overlay_name(NULL, alt, sizeof alt));
  CHECK(!rs_mods_overlay_name("data\\gfxassets\\views\\a_name_too_long_for_the_buffer.gfx", alt, 40));
  CHECK(!rs_mods_overlay_name("cache8.7z", alt, 21) && rs_mods_overlay_name("cache8.7z", alt, 22));
}

// A step's file questions: one the step before had "no" for is asked of the archive this step mounted alone, by
// the path from its mount point, and the whole file system is asked whenever that is not known to be enough.
static void file_questions(void) {
  no_switches();
  setenv("RS_MODS_LOG", "1", 1);
  rs_mods_reload_config();
  fs_reset();
  CHECK_EQ(frame(), 1);
  step("data", fs_arch[0], 3);  // the first step: nothing known
  CHECK_EQ(arch_asks[0], 1);    // its appid.appid
  CHECK_EQ(whole_asks, 3);
  CHECK(asks_answered && !step_found[0] && !step_found[1] && !step_found[2]);
  step("Data", fs_arch[1], 3);  // the second: its own archive alone, whatever the mount point's case
  CHECK_EQ(whole_asks, 3);
  CHECK_EQ(arch_asks[1], 4);
  CHECK(!strcmp(arch_asked, "shaders/generated/c.sha1"));
  CHECK(asks_answered && !step_found[0] && !step_found[1] && !step_found[2]);
  // an archive that brings one of the files: its "yes" goes to the whole file system, whose answer is the game's
  arch_has[2] = "shaders/generated/b.sha1";
  whole_has = QUESTION[1];
  step("data", fs_arch[2], 3);
  CHECK_EQ(whole_asks, 4);
  CHECK(!strcmp(whole_asked, QUESTION[1]));
  CHECK(!step_found[0] && step_found[1] && !step_found[2]);
  step("data", fs_arch[0], 3);  // and a "yes" is not kept: the whole file system again, and the others alone
  CHECK_EQ(whole_asks, 5);
  CHECK(!step_found[0] && step_found[1] && !step_found[2]);
  CHECK_EQ(arch_asks[0], 1 + 1 + 2);
  whole_has = 0;  // (gone again)
  step("data", fs_arch[1], 3);
  CHECK_EQ(whole_asks, 6);  // b: last "yes"
  // something mounted outside a step: nothing is known in the next one
  mounts("data", fs_arch[2]);
  step("data", fs_arch[0], 3);
  CHECK_EQ(whole_asks, 9);
  step("data", fs_arch[1], 3);
  CHECK_EQ(whole_asks, 9);
  // a mount point a path can't be taken from, an archive that does not answer for its appid.appid, a step that
  // mounts nothing: the whole file system
  step("data/songs", fs_arch[0], 3);
  CHECK_EQ(whole_asks, 12);
  arch_appid[1] = 0;
  step("data", fs_arch[1], 3);
  CHECK_EQ(whole_asks, 15);
  arch_appid[1] = 1;
  step(0, 0, 3);
  CHECK_EQ(whole_asks, 18);
  // a file that is not under the step's mount point can't have come with it
  int at_other = arch_asks[0];
  step("other", fs_arch[0], 3);
  CHECK_EQ(whole_asks, 18);
  CHECK_EQ(arch_asks[0], at_other + 1);
  CHECK(asks_answered && !step_found[0]);
  // the mounting's end: each question once more to the whole file system, and the count in the log
  CHECK_EQ(rs_mods_mount_listed(), 0);
  CHECK_EQ(whole_asks, 21);
  log_start();
  rs_mods_mount_end();
  log_stop();
  CHECK(STARTS(logged, "[rs-mods] DLC: 11 archives mounted in 1 frames, "));
  CHECK_EQ(logged_lines("[rs-mods] DLC: 15 of the steps' 33 file questions asked of the step's archive alone"), 1);
  // the next mounting knows nothing of the last one's answers
  fs_reset();
  CHECK_EQ(frame(), 1);
  step("data", fs_arch[0], 3);
  CHECK_EQ(whole_asks, 3);
  rs_mods_mount_listed();
  ends_quietly();
  // RS_MODS_FIXES=0: the questions are the game's own
  rs_mods_cfg.fixes = 0;
  fs_reset();
  CHECK_EQ(frame(), 0);
  step("data", fs_arch[0], 3);
  CHECK(!asks_answered);
  step("data", fs_arch[1], 3);
  CHECK(!asks_answered);
  CHECK_EQ(whole_asks, 6);
  CHECK_EQ(arch_asks[0] + arch_asks[1], 0);
  ends_quietly();
  rs_mods_cfg.fixes = 1;
  // An answer that was not the file system's (the file is there, by a way this does not know): the check at the
  // mounting's end finds it, says so, and the questions are the game's own for the rest of the run.
  fs_reset();
  CHECK_EQ(frame(), 1);
  step("data", fs_arch[0], 3);
  whole_has = QUESTION[2];
  step("data", fs_arch[1], 3);
  CHECK(asks_answered && !step_found[2]);
  log_start();
  CHECK_EQ(rs_mods_mount_listed(), 0);
  rs_mods_mount_end();
  log_stop();
  CHECK(strstr(logged, "[rs-mods] DLC: data/shaders/generated/c.sha1 is there, and a step was told it was not: "));
  CHECK_EQ(frame(), 1);
  step("data", fs_arch[0], 3);
  CHECK(!asks_answered);
  step("data", fs_arch[1], 3);
  CHECK(!asks_answered && step_found[2]);
  ends_quietly();
  no_switches();
  rs_mods_reload_config();
}

// A mounting that does not go by the book: an enumeration the game starts while the flag is held, a flag cleared
// outside the update, steps one a frame, a mounting whose steps ask nothing, and a question asked before the
// step's mount.
static void mounting_edges(void) {
  no_switches();
  setenv("RS_MODS_LOG", "1", 1);
  rs_mods_reload_config();
  uint8_t *unnamed = response_named("", 1);
  // The game starts an enumeration while the flag is held for manifests: the hold ends, the new archives are
  // stepped as a mounting of their own, and the manifests still to come are waited for with it.
  fs_reset();
  CHECK_EQ(frame(), 1);
  rs_mods_mounting(1);
  manifest_asked();
  manifest_asked();
  rs_mods_mounting(0);
  rs_mods_mount_took();
  CHECK_EQ(rs_mods_mount_listed(), 1);
  CHECK(rs_mods_mount_holding());
  log_start();
  rs_mods_mount_restarted();
  log_stop();
  CHECK(STARTS(logged, "[rs-mods] DLC: a new enumeration with 2 song manifests of the last one still to come\n"));
  CHECK(strstr(logged, "[rs-mods] DLC: 1 archives mounted in 1 frames, "));
  CHECK(!rs_mods_mount_holding());
  CHECK_EQ(frame(), 1);  // the next update: steps again
  step("data", fs_arch[0], 3);
  CHECK(asks_answered);
  CHECK_EQ(whole_asks, 3);  // a first step: nothing is known from the mounting before
  manifest_answered(unnamed);
  CHECK_EQ(rs_mods_mount_listed(), 1);  // one of the two is still to come
  manifest_answered(unnamed);
  CHECK(!rs_mods_mount_waiting());
  ends_quietly();
  log_start();
  rs_mods_mount_restarted();  // with no hold: nothing
  log_stop();
  CHECK(!*logged);
  // The flag cleared by something else than the update that found it set: the next mounting starts clean.
  CHECK_EQ(frame(), 1);
  rs_mods_mounting(1);
  manifest_asked();
  rs_mods_mounting(0);
  rs_mods_mount_took();
  CHECK_EQ(rs_mods_mount_listed(), 1);
  log_start();
  CHECK_EQ(rs_mods_mount_frame(0), 0);
  log_stop();
  CHECK(STARTS(logged, "[rs-mods] DLC: the mounting ended outside its update, after 1 archives in 1 frames\n"));
  CHECK(!rs_mods_mount_holding());
  manifest_answered(unnamed);
  CHECK_EQ(frame(), 1);
  CHECK(!rs_mods_mount_holding());
  CHECK_EQ(rs_mods_mount_listed(), 0);
  log_start();
  rs_mods_mount_end();
  log_stop();
  CHECK(STARTS(logged, "[rs-mods] DLC: 0 archives mounted in 1 frames, "));
  // A step a frame (a budget of 0): the questions are the game's own, as with the fixes off; so is a step with
  // nothing being installed.
  fs_reset();
  rs_mods_cfg.budget_s = 0;
  CHECK_EQ(frame(), 0);
  step("data", fs_arch[0], 3);
  step("data", fs_arch[1], 3);
  CHECK(!asks_answered);
  CHECK_EQ(arch_asks[0] + arch_asks[1], 0);
  ends_quietly();
  rs_mods_reload_config();
  CHECK_EQ(rs_mods_mount_frame(0), 0);
  step("data", fs_arch[0], 3);
  CHECK(!asks_answered);
  // A mounting whose steps asked nothing checks nothing at its end: the answers kept are an earlier mounting's.
  fs_reset();
  CHECK_EQ(frame(), 1);
  step("data", fs_arch[0], 3);
  step("data", fs_arch[1], 3);
  CHECK_EQ(rs_mods_mount_listed(), 0);
  ends_quietly();
  int asked = whole_asks;
  whole_has = QUESTION[0];  // (a file that came since)
  CHECK_EQ(frame(), 1);
  step(0, 0, 0);
  log_start();
  CHECK_EQ(rs_mods_mount_listed(), 0);
  rs_mods_mount_end();
  log_stop();
  CHECK_EQ(whole_asks, asked);
  CHECK(!strstr(logged, "is there, and a step was told"));
  // A question a step asked before its mount is not known for the step after: the archive it then mounted may
  // have the file.
  fs_reset();
  CHECK_EQ(frame(), 1);
  step("data", fs_arch[0], 1);
  rs_mods_mounting(1);
  CHECK(!asks(QUESTION[0]));
  arch_has[1] = "shaders/generated/a.sha1";
  whole_has = QUESTION[0];
  mounts("data", fs_arch[1]);
  rs_mods_mounting(0);
  rs_mods_mount_took();
  step("data", fs_arch[2], 1);
  CHECK(step_found[0]);  // the whole file system's answer, not the third archive's alone
  log_start();
  CHECK_EQ(rs_mods_mount_listed(), 0);
  rs_mods_mount_end();
  log_stop();
  CHECK(!strstr(logged, "is there, and a step was told"));
  no_switches();
  rs_mods_reload_config();
}

int main(void) {
#ifdef RC_GUEST32
  CHECK(!rc32_mem_init());
  ctx = rc32_thread_ctx();
  esp0 = (uint32_t)ctx->r[4];
  obj_guest = rc32_alloc(256);
  obj = RC32_H(obj_guest);
  CHECK(!rc32_mem_commit(DATA, D_SIZE));
  data = RC32_H(DATA);
#else
  static rc_ctx c64;
  static uint8_t image[D_SIZE];
  ctx = &c64;
  obj = calloc(1, 256);
  data = image;
  rc_slide = (uint64_t)(uintptr_t)image - DATA;
#endif
  // the image's data: the ten slots, and the two pointers to the profile keys' text
  binding = (int32_t *)(data + D_BINDING);
  strcpy((char *)data + D_TEXT_DIRECT, "DirectKey");
  strcpy((char *)data + D_TEXT_MIC, "MicKey");
  put_ptr(data + D_KEY_DIRECT, data + D_TEXT_DIRECT);
  put_ptr(data + D_KEY_MIC, data + D_TEXT_MIC);
  input_obj = game_alloc(64);
  manager_obj = game_alloc(16);
  text_heap = game_alloc(TEXT_HEAP);
  // the file system objects: in Learn & Play's kind of image each has a vtable with the question's function
  uint8_t *vtable = game_alloc(64);
  put_ptr(vtable + rc_off("fs_exists_slot") * rc_guest_ptr_size, as_host(ADDR(FSEXISTS)));
  fs_whole = game_alloc(16);
  put_ptr(fs_whole, vtable);
  for (int k = 0; k < ARCHIVES; k++) put_ptr(fs_arch[k] = game_alloc(16), vtable);
  switches();
  log_lines();
  start();
  intro();
  ownership();
  saved_input();
  skip_tuner();
  manifests();
  mounting_edges();
  file_questions();
  overlay_names();
  printf("mods_shared: ok (%s)\n", rc_guest_ptr_size == 4 ? "32-bit guest" : "64-bit guest");
  return 0;
}
